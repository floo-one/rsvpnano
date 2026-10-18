#include "feeds/FeedOrganization.h"
#include "conversion/rsvp/RsvpWriter.h"
#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"
#include "text/Utf8Text.h"
#include "text/TextNormalizer.h"
#include "hash/Fnv1a.h"
#include <FS.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
using namespace LibraryOrganization;
namespace FeedOrganization {
namespace {
    bool missing(std::error_code e) { return e == std::errc::no_such_file_or_directory; }
    auto error(std::errc e) { return std::unexpected(std::make_error_code(e)); }
    std::error_code ioError() { return errno ? std::error_code(errno, std::generic_category()) : std::make_error_code(std::errc::io_error); }
    std::string label(std::string_view value) {
        auto normalized = RsvpText::normalizeDisplayText(value);
        std::string result;
        std::string_view rest = normalized;
        uint32_t cp;
        while (!rest.empty()) {
            const auto before = rest;
            if (!Utf8Text::decode(rest, cp)) continue;
            if (cp < 0x20 || (cp >= 0x7f && cp <= 0x9f)) result += ' ';
            else result.append(before.substr(0, before.size() - rest.size()));
        }
        return std::string(Utf8Text::prefix(AsciiText::trim(result), kMaxLabelBytes));
    }
    bool httpUrl(std::string_view url) {
        if (url.size() > kMaxPathBytes || (!url.starts_with("http://") && !url.starts_with("https://"))) return false;
        const auto start = url.find("://") + 3;
        if (start == url.size() || url[start] == '/' || url[start] == '?' || url[start] == '#') return false;
        return std::ranges::none_of(url, [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; });
    }
    std::string normalizedBody(const feedparser::FeedItem& item) {
        // The parser has decoded markup. Preserve paragraph distinctions, using
        // the same display normalization as the writer rather than title equality.
        std::string result;
        for (size_t at = 0; at < item.body.size();) {
            const auto end = item.body.find('\n', at);
            auto line = RsvpText::normalizeDisplayText(std::string_view(item.body).substr(at, end == item.body.npos ? end : end - at));
            result += AsciiText::trim(line); result += '\n';
            at = end == item.body.npos ? item.body.size() : end + 1;
        }
        return result;
    }
    struct Key { std::string url, body, scope; };
    Key key(const Feed& feed, const feedparser::FeedItem& item) {
        auto url = articleUrl(feed.url, item.link);
        auto scope = url.empty() || !item.bodyComplete ? feed.id : std::string{};
        return {std::move(url), hashBytes(normalizedBody(item)), std::move(scope)};
    }
    std::string identity(const Key& k) { return hashBytes(k.scope + '\n' + k.url + '\n' + k.body); }
    Candidate candidate(const Item& item) { return {item.path, item.source_url, item.body_sha256, item.match_scope, item.source}; }
    size_t memory(const Candidate& c) { return sizeof(c) + c.path.capacity() + c.url.capacity() + c.body.capacity() + c.scope.capacity() + c.source.sha256.capacity(); }
    size_t capacityAfterAppend(const Catalog& catalog) {
        return catalog.size() < catalog.capacity() ? catalog.capacity() : std::max<size_t>(8, catalog.capacity() * 2);
    }
    bool fits(const Catalog& catalog, const Candidate& next) {
        size_t bytes = capacityAfterAppend(catalog) * sizeof(Candidate) + memory(next) - sizeof(Candidate);
        for (const auto& c : catalog) bytes += memory(c) - sizeof(Candidate);
        return bytes <= kCatalogBytes;
    }
    Result append(Catalog& catalog, const Item& item) {
        auto c = candidate(item);
        if (!fits(catalog, c)) return error(std::errc::no_buffer_space);
        catalog.reserve(capacityAfterAppend(catalog));
        catalog.push_back(std::move(c)); return {};
    }
    Result persist(fs::FS& fs, Registry registry) {
        auto saved = saveFeeds(fs, registry, registry.revision);
        if (!saved) return std::unexpected(saved.error());
        return {};
    }
    std::expected<uint64_t, std::error_code> reserve(Registry& r) {
        if (r.next_order == UINT64_MAX) return error(std::errc::value_too_large);
        return r.next_order++;
    }
    Result writeStage(fs::FS& fs, const feedparser::FeedItem& item, std::string_view url) {
        // recover() has already resolved any earlier durable intent before reuse.
        errno = 0;
        File file = fs.open(kImportStage, FILE_WRITE);
        if (!file || file.isDirectory()) return std::unexpected(ioError());
        const auto title = label(item.title), author = label(item.author);
        RsvpWriter writer(file, {.source=url, .title=title, .author=author});
        bool ok = writer.writeChapter(title);
        for (size_t at = 0; ok && at < item.body.size();) {
            const auto end = item.body.find('\n', at);
            const auto line = std::string_view(item.body).substr(at, end == item.body.npos ? end : end - at);
            if (AsciiText::trim(line).empty()) writer.endParagraph();
            else ok = writer.writeText(line, false);
            at = end == item.body.npos ? item.body.size() : end + 1;
        }
        ok = ok && writer.finish() && writer.wordCount() > 0;
        const auto writtenBytes = file.size();
        file.flush(); file.close();
        // Reopen only after flush/close; FAT need not expose a writer's length to
        // another handle before then. Detect short/truncated persistence before intent.
        auto verified = hashImportStage(fs);
        if (!verified || verified->bytes != writtenBytes) return error(std::errc::io_error);
        if (!ok) return error(std::errc::io_error);
        return {};
    }
    Result membership(fs::FS& fs, Item item, const Feed& feed, uint64_t batch, uint32_t position) {
        if (item.restart) return std::unexpected(make_error_code(Error::recovery_required));
        if (std::ranges::any_of(item.memberships, [&](const auto& m) { return m.feed_id == feed.id; })) return {};
        if (item.memberships.size() == kMaxMemberships) return error(std::errc::no_buffer_space);
        item.memberships.push_back({feed.id, batch, position});
        auto saved = saveItem(fs, item, item.revision);
        if (!saved) return std::unexpected(saved.error());
        return {};
    }
}
std::string articleUrl(std::string_view feed, std::string_view link) {
    link = AsciiText::trim(link);
    if (link.empty()) return {};
    if (link.starts_with("http://") || link.starts_with("https://")) return httpUrl(link) ? std::string(link) : std::string{};
    if (!httpUrl(feed)) return {};
    // Opaque GUIDs and unsupported schemes are not relative article URLs.
    if (link.find(':') != link.npos && !link.starts_with('/')) return {};
    const auto schemeEnd = feed.find("://");
    const auto originEnd = feed.find_first_of("/?#", schemeEnd + 3);
    const auto origin = feed.substr(0, originEnd);
    std::string result;
    if (link.starts_with("//")) result = std::string(feed.substr(0, schemeEnd)) + ':' + std::string(link);
    else if (link.starts_with('/')) result = std::string(origin) + std::string(link);
    else if (link.starts_with('?') || link.starts_with('#')) result = std::string(feed.substr(0, link[0] == '?' ? feed.find_first_of("?#") : feed.find('#'))) + std::string(link);
    else {
        const auto base = feed.substr(0, feed.find_first_of("?#"));
        const auto slash = base.rfind('/');
        result = (slash < schemeEnd + 3 ? std::string(origin) + '/' : std::string(base.substr(0, slash + 1))) + std::string(link);
    }
    // Keep queries/fragments and dot segments exact. Ambiguous URLs can remain
    // separate; never strip tracking parameters or guess a canonical publisher URL.
    return httpUrl(result) ? result : std::string{};
}
std::string seenIdentity(const Feed& feed, const feedparser::FeedItem& item) { return identity(key(feed, item)); }
std::string legacyFilename(const feedparser::FeedItem& item) {
    std::string cleaned = StoragePaths::sanitizeFilename(std::string_view(item.title).substr(0, 72));
    while (cleaned.contains("--")) cleaned.erase(cleaned.find("--"), 1);
    if (cleaned.empty()) cleaned = "rss-article";
    char suffix[24]; std::snprintf(suffix, sizeof(suffix), "-%08lx.rsvp", static_cast<unsigned long>(Fnv1a::hash(!item.legacyIdentity.empty() ? item.legacyIdentity : item.link.empty() ? item.title : item.link)));
    return cleaned + suffix;
}
std::expected<Registry, std::error_code> reconcile(fs::FS& fs, std::span<const std::string> urls) {
    if (urls.size() > kMaxActiveFeeds) return error(std::errc::no_buffer_space);
    auto loaded = loadFeeds(fs, Recovery::stagedCreation);
    if (!loaded && !missing(loaded.error())) return std::unexpected(loaded.error());
    Registry r = loaded ? *loaded : Registry{};
    const auto before = r;
    for (auto& feed : r.feeds) feed.active = false;
    for (const auto& url : urls) {
        if (!httpUrl(url)) return error(std::errc::invalid_argument);
        auto found = std::ranges::find(r.feeds, url, &Feed::url);
        if (found != r.feeds.end()) { found->active = true; continue; }
        if (r.feeds.size() == kMaxFeeds) return error(std::errc::no_buffer_space);
        auto order = reserve(r); if (!order) return std::unexpected(order.error());
        // Sequence IDs avoid hash collisions; exact URL matching retains identity.
        Feed feed; feed.id = "feed-" + std::to_string(*order); feed.url = url;
        feed.title = label(url.substr(url.find("://") + 3)); feed.active = true; feed.subscription_order = *order;
        r.feeds.push_back(std::move(feed));
    }
    if (r == before && r.revision) return r;
    return saveFeeds(fs, r, r.revision);
}
std::vector<std::string> attempts(const Registry& r) {
    std::vector<std::string> result;
    size_t start = 0;
    auto last = std::ranges::find(r.feeds, r.last_attempted_feed, &Feed::id);
    if (last != r.feeds.end()) start = static_cast<size_t>(last - r.feeds.begin()) + 1;
    for (size_t i = 0; i < r.feeds.size() && result.size() < kAttemptBudget; ++i) {
        const auto& feed = r.feeds[(start + i) % r.feeds.size()];
        if (feed.active) result.push_back(feed.id);
    }
    return result;
}
Result attempted(fs::FS& fs, std::string_view id) {
    auto r = loadFeeds(fs); if (!r) return std::unexpected(r.error());
    auto f = std::ranges::find(r->feeds, id, &Feed::id);
    if (f == r->feeds.end() || !f->active) return error(std::errc::invalid_argument);
    r->last_attempted_feed = id;
    return persist(fs, std::move(*r));
}
std::expected<Feed, std::error_code> beginScan(fs::FS& fs, std::string_view id, std::string_view body) {
    if (body.size() > 4 * 1024 * 1024) return error(std::errc::value_too_large);
    auto r = loadFeeds(fs); if (!r) return std::unexpected(r.error());
    auto f = std::ranges::find(r->feeds, id, &Feed::id);
    if (f == r->feeds.end() || !f->active) return error(std::errc::invalid_argument);
    auto title = label(feedparser::feedTitle(body));
    if (!title.empty()) f->title = std::move(title);
    const auto digest = hashBytes(body);
    if (f->scan_digest != digest || f->scan_batch == 0) {
        auto order = reserve(*r); if (!order) return std::unexpected(order.error());
        f->scan_digest = digest; f->scan_cursor = f->scan_position = 0; f->scan_batch = *order;
    }
    const auto result = *f;
    if (auto saved = persist(fs, *r); !saved) return std::unexpected(saved.error());
    return result;
}
Result checkpointScan(fs::FS& fs, const Feed& feed) {
    auto r = loadFeeds(fs); if (!r) return std::unexpected(r.error());
    auto f = std::ranges::find(r->feeds, feed.id, &Feed::id);
    if (f == r->feeds.end() || f->scan_digest != feed.scan_digest || f->scan_batch != feed.scan_batch)
        return std::unexpected(make_error_code(Error::stale_identity));
    f->scan_cursor = feed.scan_cursor; f->scan_position = feed.scan_position;
    return persist(fs, std::move(*r));
}
std::expected<Catalog, std::error_code> catalog(fs::FS& fs, std::span<const std::string> paths) {
    Catalog result;
    for (const auto& path : paths) {
        if (!path.starts_with(StoragePaths::kArticleFilesPrefix)) continue;
        auto item = inspectItem(fs, path);
        if (!item) {
            if (missing(item.error()) || item.error() == make_error_code(Error::corrupt)
                || item.error() == make_error_code(Error::unsupported_fields) || item.error() == make_error_code(Error::unsupported_version)) continue;
            return std::unexpected(item.error());
        }
        if (item->body_sha256.empty()) continue;
        if (auto added = append(result, *item); !added) return std::unexpected(added.error());
    }
    std::ranges::sort(result, {}, &Candidate::path); // Deterministic; preserve older duplicates as separate files.
    return result;
}
std::expected<std::optional<std::string>, std::error_code> recover(fs::FS& fs) {
    // The validated journal is the import intent. A partial first journal cannot
    // have installed content yet; discarding only that partial stage is safe.
    auto intent = loadImport(fs, Recovery::stagedCreation);
    if (!intent) { if (missing(intent.error())) return std::nullopt; return std::unexpected(intent.error()); }
    if (intent->completed) return std::nullopt;
    auto source = hashSource(fs, intent->item.path);
    if (!source) {
        if (!missing(source.error()) || intent->existing_content) return std::unexpected(source.error());
        auto staged = hashImportStage(fs);
        if (!staged) return std::unexpected(staged.error());
        if (*staged != intent->item.source) return std::unexpected(make_error_code(Error::stale_identity));
        errno = 0;
        if (!fs.rename(kImportStage, intent->item.path.c_str())) return std::unexpected(ioError());
        source = hashSource(fs, intent->item.path);
        if (!source) return std::unexpected(source.error());
    }
    if (*source != intent->item.source) return std::unexpected(make_error_code(Error::stale_identity));
    auto item = loadItem(fs, intent->item.path, *source, Recovery::stagedCreation);
    if (!item) {
        if (!missing(item.error())) return std::unexpected(item.error());
        item = saveItem(fs, intent->item, 0);
        if (!item) return std::unexpected(item.error());
    } else {
        const auto& member = intent->item.memberships.front();
        Feed feed; feed.id = member.feed_id;
        if (auto saved = membership(fs, *item, feed, member.batch, member.position); !saved) return std::unexpected(saved.error());
    }
    intent->completed = true;
    auto saved = saveImport(fs, *intent, intent->revision);
    if (!saved) return std::unexpected(saved.error());
    return std::optional{intent->item.path};
}
std::expected<ScanResult, std::error_code> scan(fs::FS& fs, Catalog& catalog, Feed feed,
    std::string_view body, size_t saveBudget, const Seen& seen, const MarkSeen& mark) {
    if (hashBytes(body) != feed.scan_digest || feed.scan_cursor > body.size())
        return std::unexpected(make_error_code(Error::stale_identity));
    ScanResult result; size_t cursor = feed.scan_cursor;
    feedparser::FeedItem article;
    while (result.scanned < kScanBudget && feedparser::parseNextItem(body, cursor, article)) {
        const auto position = feed.scan_position++;
        ++result.scanned;
        const auto id = seenIdentity(feed, article);
        auto ingested = ingest(fs, catalog, feed, article, feed.scan_batch, position,
            result.saved < saveBudget, seen && seen(article, id, false), seen && seen(article, id, true));
        if (!ingested) { ++result.failed; continue; }
        switch (ingested->outcome) {
        case Outcome::saved: ++result.saved; if (mark) mark(id); break;
        case Outcome::associated: ++result.associated; if (mark) mark(id); break;
        case Outcome::unchanged: ++result.skipped; if (mark) mark(id); break;
        case Outcome::skipped: ++result.skipped; break;
        case Outcome::deferred: ++result.deferred; break;
        }
    }
    size_t probe = cursor;
    result.partial = result.deferred || result.failed || feedparser::advancePastItem(body, probe);
    // Every bounded pass advances, even past failures and deferred saves. At end
    // wrap to revisit them; membership work does not consume the new-save budget.
    size_t next = cursor;
    if (!feedparser::advancePastItem(body, next)) feed.scan_cursor = feed.scan_position = 0;
    else feed.scan_cursor = static_cast<uint32_t>(cursor);
    if (auto saved = checkpointScan(fs, feed); !saved) return std::unexpected(saved.error());
    return result;
}
std::expected<Ingested, std::error_code> ingest(fs::FS& fs, Catalog& catalog, const Feed& feed,
    const feedparser::FeedItem& article, uint64_t batch, uint32_t position, bool permitSave, bool seen, bool legacySeen) {
    if (article.body.empty() || article.body.size() > feedparser::kMaxArticleChars || batch == 0) return error(std::errc::invalid_argument);
    auto recovery = recover(fs); if (!recovery) return std::unexpected(recovery.error());
    if (*recovery) {
        auto item = inspectItem(fs, **recovery); if (!item) return std::unexpected(item.error());
        if (std::ranges::find(catalog, item->path, &Candidate::path) == catalog.end())
            if (auto added = append(catalog, *item); !added) return std::unexpected(added.error());
    }
    const auto k = key(feed, article);
    bool knownUrl = false;
    for (const auto& c : catalog) {
        knownUrl |= !k.url.empty() && c.url == k.url;
        if (c.url != k.url || c.body != k.body || c.scope != k.scope) continue;
        auto source = hashSource(fs, c.path);
        if (!source) { if (missing(source.error())) continue; return std::unexpected(source.error()); }
        if (*source != c.source) continue; // Replaced files do not inherit membership/progress.
        auto item = loadItem(fs, c.path, *source); if (!item) return std::unexpected(item.error());
        if (item->source_url != k.url || item->body_sha256 != k.body || item->match_scope != k.scope) continue;
        const bool present = std::ranges::any_of(item->memberships, [&](const auto& m) { return m.feed_id == feed.id; });
        if (auto saved = membership(fs, *item, feed, batch, position); !saved) return std::unexpected(saved.error());
        return Ingested{present ? Outcome::unchanged : Outcome::associated, c.path};
    }
    // Recognize an old downloader's exact file without guessing from its title.
    const auto legacy = std::string(StoragePaths::kArticleFilesPrefix) + legacyFilename(article);
    auto legacySource = hashSource(fs, legacy);
    if (!legacySource && !missing(legacySource.error())) return std::unexpected(legacySource.error());
    if (seen || (legacySeen && !legacySource && !knownUrl)) return Ingested{Outcome::skipped, {}};
    if (!permitSave && !legacySource) return Ingested{Outcome::deferred, {}};
    if (auto written = writeStage(fs, article, k.url); !written) return std::unexpected(written.error());
    auto staged = hashImportStage(fs); if (!staged) return std::unexpected(staged.error());
    if (legacySource && *legacySource == *staged && k.scope.empty() && !k.url.empty() && k.url.size() <= 512
        && RsvpText::normalizeDisplayText(k.url) == k.url) {
        auto old = loadItem(fs, legacy, *legacySource);
        if (!old && missing(old.error())) {
            Item item; item.path = legacy; item.source = *legacySource; item.status = Status::unknown;
            item.unknown_reason = "legacy-unclassified"; item.source_url = k.url; item.body_sha256 = k.body; item.match_scope = k.scope;
            item.memberships.push_back({feed.id, batch, position});
            item.revision = 1;
            auto previous = loadImport(fs);
            if (!previous && !missing(previous.error())) return std::unexpected(previous.error());
            Import intent; intent.item = item; intent.existing_content = true;
            auto saved = saveImport(fs, intent, previous ? previous->revision : 0);
            if (!saved) return std::unexpected(saved.error());
            auto done = recover(fs); if (!done) return std::unexpected(done.error());
            old = loadItem(fs, legacy, *legacySource);
        } else if (old) {
            if (old->restart) return std::unexpected(make_error_code(Error::recovery_required));
            old->source_url = k.url; old->body_sha256 = k.body; old->match_scope = k.scope;
            if (!std::ranges::any_of(old->memberships, [&](const auto& m) { return m.feed_id == feed.id; }))
                old->memberships.push_back({feed.id, batch, position});
            old = saveItem(fs, *old, old->revision);
        }
        if (!old) return std::unexpected(old.error());
        if (auto added = append(catalog, *old); !added) return std::unexpected(added.error());
        return Ingested{Outcome::associated, legacy};
    }
    if (!permitSave) return Ingested{Outcome::deferred, {}};
    const auto stem = std::string(StoragePaths::kArticleFilesPrefix) + "article-" + identity(k);
    std::string path;
    bool allocated = false;
    for (size_t suffix = 0; suffix < 1024; ++suffix) {
        path = stem + (suffix ? "-" + std::to_string(suffix) : "") + ".rsvp";
        auto exists = hashSource(fs, path);
        if (exists) continue; // Never replace content, including a hash/filename collision.
        if (!missing(exists.error())) return std::unexpected(exists.error());
        auto metadata = inspectItem(fs, path);
        if (metadata) continue;
        if (!missing(metadata.error())) {
            if (metadata.error().category() == std::generic_category()) return std::unexpected(metadata.error());
            continue; // Preserve unsupported/damaged/orphan organization too.
        }
        bool hasBookmark = false;
        for (const auto* suffix : {"", ".bak", ".tmp"}) {
            errno = 0; auto bookmark = fs.open((StoragePaths::bookStatePathFor(path) + suffix).c_str(), FILE_READ);
            if (bookmark) { hasBookmark = true; break; }
            if (errno && errno != ENOENT) return std::unexpected(ioError());
        }
        if (hasBookmark) continue;
        allocated = true; break;
    }
    if (!allocated) return error(std::errc::file_exists);
    auto exists = hashSource(fs, path);
    if (exists || !missing(exists.error())) return error(std::errc::file_exists);
    Item item; item.revision = 1; item.path = path; item.source = *staged; item.status = Status::unread;
    item.source_url = k.url; item.body_sha256 = k.body; item.match_scope = k.scope;
    item.memberships.push_back({feed.id, batch, position});
    // Check catalog capacity before committing content, not after publishing it.
    if (!fits(catalog, candidate(item))) return error(std::errc::no_buffer_space);
    auto previous = loadImport(fs); if (!previous && !missing(previous.error())) return std::unexpected(previous.error());
    Import intent; intent.item = item;
    auto saved = saveImport(fs, intent, previous ? previous->revision : 0);
    if (!saved) return std::unexpected(saved.error());
    auto done = recover(fs); if (!done) return std::unexpected(done.error());
    if (auto added = append(catalog, item); !added) return std::unexpected(added.error());
    return Ingested{Outcome::saved, path};
}
}
