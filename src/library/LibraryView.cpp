#include "library/LibraryView.h"

#include <algorithm>
#include <limits>
#include "library/ReadingStateStore.h"
#include "storage/fs/StoragePaths.h"
#include "text/UnicodeText.h"

namespace LibraryView {
    namespace LO = LibraryOrganization;
    namespace {
        bool belongs(const Row& row, std::string_view collection) {
            if (collection.empty()) return true;
            if (collection == "books") return !row.article;
            if (collection == "newsletters") return row.article;
            if (collection == "needs-review") return row.availability != Availability::loading && row.status == Status::unknown;
            if (collection == "other-articles") return row.article && row.memberships.empty();
            if (!collection.starts_with("feed:")) return false;
            return row.article && std::ranges::any_of(row.memberships, [&](const auto& m) { return m.feed_id == collection.substr(5); });
        }
        bool accepts(const Row& row, std::string_view collection, Tab tab) {
            if (!belongs(row, collection) || row.availability == Availability::loading) return false;
            if (collection == "needs-review") return true;
            return (tab == Tab::unread && row.status == Status::unread)
                || (tab == Tab::reading && row.status == Status::reading)
                || (tab == Tab::read && row.status == Status::read);
        }
        int compareTitle(std::string_view a, std::string_view b) {
            uint32_t ac = 0, bc = 0;
            while (!a.empty() && !b.empty()) {
                Utf8Text::next(a, ac); Utf8Text::next(b, bc);
                // Reuse supported Latin folding. Other scripts use codepoint order.
                if (ac < 0x250) ac = UnicodeText::toLowercase(ac);
                if (bc < 0x250) bc = UnicodeText::toLowercase(bc);
                if (ac != bc) return ac < bc ? -1 : 1;
            }
            return a.empty() == b.empty() ? 0 : a.empty() ? -1 : 1;
        }
        Availability availability(std::error_code error) {
            if (error == std::errc::no_such_file_or_directory) return Availability::missing;
            if (error == LO::make_error_code(LO::Error::corrupt)) return Availability::corrupt;
            if (error == LO::make_error_code(LO::Error::unsupported_version) || error == LO::make_error_code(LO::Error::unsupported_fields)) return Availability::unsupported;
            if (error == LO::make_error_code(LO::Error::stale_identity)) return Availability::stale;
            if (error == LO::make_error_code(LO::Error::recovery_required)) return Availability::recovery;
            return Availability::ioError;
        }
        size_t feedBytes(const LO::Feeds& feeds) {
            size_t n = feeds.feeds.capacity() * sizeof(LO::Feed) + feeds.last_attempted_feed.capacity() + 1;
            for (const auto& f : feeds.feeds) n += f.id.capacity() + f.url.capacity() + f.title.capacity() + f.scan_digest.capacity() + 4;
            return n;
        }
        size_t strings(const Row& r) {
            size_t n = r.path.capacity() + r.title.capacity() + r.source.sha256.capacity() + r.chapter.capacity() + 4;
            n += r.memberships.capacity() * sizeof(LO::Membership);
            for (const auto& m : r.memberships) n += m.feed_id.capacity() + 1;
            return n;
        }
    }
    std::string displayTitle(std::string_view title, std::string_view path) {
        if (title.empty()) title = path.substr(path.find_last_of('/') + 1);
        return std::string{Utf8Text::prefix(title, LO::kMaxLabelBytes)};
    }
    size_t memoryBytes(std::span<const Row> rows, size_t capacity) {
        size_t bytes = capacity * sizeof(Row);
        for (const auto& r : rows) bytes += strings(r);
        return bytes;
    }
    Counts count(std::span<const Row> rows, std::string_view collection) {
        Counts result;
        for (const auto& row : rows) {
            if (!belongs(row, collection)) continue;
            if (row.availability == Availability::loading) { ++result.loading; continue; }
            if (row.availability != Availability::ready) ++result.unavailable;
            switch (row.status) {
            case Status::unread: ++result.unread; break;
            case Status::reading: ++result.reading; break;
            case Status::read: ++result.read; break;
            default: ++result.unknown; break;
            }
        }
        return result;
    }
    std::vector<size_t> project(std::span<const Row> rows, std::string_view collection, Tab tab) {
        std::vector<size_t> order;
        order.reserve(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) if (accepts(rows[i], collection, tab)) order.push_back(i);
        const auto arrival = [&](const Row& r) {
            for (const auto& m : r.memberships) if (m.feed_id == collection.substr(5)) return std::pair{m.batch, m.position};
            return std::pair<uint64_t, uint32_t>{0, 0};
        };
        std::sort(order.begin(), order.end(), [&](size_t ai, size_t bi) {
            const auto& a = rows[ai]; const auto& b = rows[bi];
            if (collection != "needs-review") {
                if (tab == Tab::reading && a.lastRead != b.lastRead) return a.lastRead > b.lastRead;
                if (tab == Tab::read && a.completed != b.completed) return a.completed > b.completed;
                if (tab == Tab::unread && collection.starts_with("feed:")) {
                    const auto am = arrival(a), bm = arrival(b);
                    if (am.first != bm.first) return am.first > bm.first;
                    if (am.first && am.second != bm.second) return am.second < bm.second;
                }
            }
            const int title = compareTitle(a.title, b.title);
            return title ? title < 0 : a.path < b.path;
        });
        return order;
    }
    std::optional<size_t> resolve(const Selection& selection, uint64_t generation, std::span<const Row> rows) {
        if (selection.generation != generation) return {};
        for (const auto& row : rows)
            if (row.path == selection.path && row.source == selection.source && !row.source.sha256.empty()) return row.sourceIndex;
        return {};
    }
    int32_t clampScroll(int64_t pixels, size_t count, int32_t viewport) {
        const int64_t end = std::max<int64_t>(0, static_cast<int64_t>(count) * kRowHeight - std::max<int32_t>(1, viewport));
        return static_cast<int32_t>(std::clamp<int64_t>(pixels, 0, std::min<int64_t>(end, INT32_MAX)));
    }
    std::optional<size_t> fullyVisibleRow(int32_t y, int32_t scroll, size_t count, int32_t viewport) {
        if (y < 0 || y >= viewport || scroll < 0) return {};
        const size_t row = (static_cast<int64_t>(y) + scroll) / kRowHeight;
        const int64_t top = static_cast<int64_t>(row) * kRowHeight - scroll;
        if (row >= count || top < 0 || top + kRowHeight > viewport) return {};
        return row;
    }
    std::expected<std::vector<Row>, std::error_code> prepare(
        fs::FS& fs, std::span<const BookLibrary::Entry> books, const MetadataReader& metadata,
        size_t retained, std::string_view priority, const std::function<bool(uint64_t, uint64_t)>& proceed) {
        const auto tooLarge = [] { return std::unexpected(std::make_error_code(std::errc::not_enough_memory)); };
        if (retained > kMemoryBudget - kWorkspaceReserve || books.size() > (kMemoryBudget - retained - kWorkspaceReserve) / sizeof(Row)) return tooLarge();
        // Compare legacy bookmark stems without allocating one path per pair.
        // Inventory sibling scanning remains unchanged; this new check is O(n log n).
        const auto stem = [](std::string_view path) {
            const auto dot = path.find_last_of('.'), slash = path.find_last_of('/');
            return dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash) ? path.substr(0, dot) : path;
        };
        std::vector<size_t> stemOrder(books.size());
        std::vector<bool> ambiguous(books.size(), false);
        for (size_t i = 0; i < books.size(); ++i) stemOrder[i] = i;
        std::sort(stemOrder.begin(), stemOrder.end(), [&](size_t a, size_t b) { return stem(books[a].path) < stem(books[b].path); });
        for (size_t i = 1; i < stemOrder.size(); ++i)
            if (stem(books[stemOrder[i-1]].path) == stem(books[stemOrder[i]].path)) ambiguous[stemOrder[i-1]] = ambiguous[stemOrder[i]] = true;
        std::vector<Row> rows;
        rows.reserve(books.size());
        size_t bytes = rows.capacity() * sizeof(Row) + retained + kWorkspaceReserve;
        size_t first = 0;
        for (size_t i = 0; i < books.size(); ++i) if (books[i].path == priority) { first = i; break; }
        for (size_t step = 0; step < books.size(); ++step) {
            if (proceed && !proceed(step, books.size())) return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            const size_t i = step == 0 ? first : step <= first ? step - 1 : step;
            const auto& book = books[i];
            Row row;
            row.path = book.path; row.title = displayTitle(book.title, book.path);
            row.sourceIndex = i; row.article = book.path.starts_with("/library/articles/");
            BookMetadata bookMetadata;
            const auto identity = metadata ? metadata(i, row, bookMetadata) : std::nullopt;
            uint32_t wordIndex = 0;
            row.title = displayTitle(row.title, row.path);
            auto source = LO::hashSource(fs, row.path, [&](uint64_t, uint64_t) { return !proceed || proceed(step, books.size()); });
            if (!source && source.error() == std::errc::operation_canceled) return std::unexpected(source.error());
            if (!source) row.availability = availability(source.error());
            else {
                row.source = *source;
                auto organization = LO::loadItem(fs, row.path, *source);
                row.availability = organization ? Availability::ready : availability(organization.error());
                if (organization) {
                    row.status = organization->restart ? Status::unknown : organization->status;
                    if (organization->restart) row.availability = Availability::recovery;
                    row.lastRead = organization->last_read_order; row.completed = organization->completed_order;
                    row.memberships = std::move(organization->memberships);
                }
                // A shared legacy stem cannot tell which file owns its bookmark.
                if (identity && identity->wordCount > 0 && !ambiguous[i]) {
                    auto state = ReadingStateStore::read(fs, row.path, *identity);
                    if (state) {
                        row.progressKnown = true; wordIndex = state->reading.wordIndex;
                        row.progress = identity->wordCount <= 1 ? 0 : static_cast<uint8_t>(static_cast<uint64_t>(state->reading.wordIndex) * 100 / (identity->wordCount - 1));
                        if (!organization && organization.error() == std::errc::no_such_file_or_directory
                            && state->reading.wordIndex > 0 && state->reading.wordIndex < identity->wordCount - 1) row.status = Status::reading;
                    }
                }
                if (!organization && organization.error() == std::errc::no_such_file_or_directory) row.availability = Availability::ready;
            }
            if (const auto* chapter = bookMetadata.chapterAt(wordIndex)) row.chapter = std::string{Utf8Text::prefix(chapter->title, LO::kMaxLabelBytes)};
            bytes += strings(row);
            if (bytes > kMemoryBudget) return tooLarge();
            rows.push_back(std::move(row));
        }
        return rows;
    }
    size_t Model::memoryBytes() const {
        size_t n = LibraryView::memoryBytes(rows_, rows_.capacity()) + order_.capacity() * sizeof(size_t) + collection_.capacity() + sizeof(*this);
        n += feedBytes(feeds_);
        return n + contextBytes();
    }
    size_t Model::contextBytes() const {
        size_t n = views_.views.capacity() * sizeof(LO::View);
        for (const auto& v : views_.views) {
            n += v.collection.capacity() + 1 + v.anchors.capacity() * sizeof(LO::Anchor);
            for (const auto& a : v.anchors) n += a.path.capacity() + 1;
        }
        return n;
    }
    std::expected<void, std::error_code> Model::load(fs::FS& fs) {
        auto loaded = LO::loadViews(fs);
        if (!loaded && loaded.error() != std::errc::no_such_file_or_directory) {
            persistenceError_ = loaded.error(); return std::unexpected(persistenceError_);
        }
        views_ = loaded ? std::move(*loaded) : LO::Views{};
        persistenceError_.clear(); contextError_.clear(); dirty_ = false;
        return {};
    }
    std::expected<void, std::error_code> Model::save(fs::FS& fs) {
        if (persistenceError()) return std::unexpected(persistenceError());
        if (!dirty_) return {};
        auto saved = LO::saveViews(fs, views_, views_.revision);
        if (!saved) return std::unexpected(saved.error()); // keep dirty; caller exposes/retries failure
        views_ = std::move(*saved); dirty_ = false;
        return {};
    }
    bool Model::exists(std::string_view id) const {
        if (id == "books" || id == "other-articles" || id == "needs-review") return true;
        if (!id.starts_with("feed:")) return false;
        for (const auto& feed : feeds_.feeds) if (feed.id == id.substr(5) && feed.active) return true;
        for (const auto& row : rows_) for (const auto& m : row.memberships) if (m.feed_id == id.substr(5)) return true;
        return false;
    }
    LO::View* Model::view() {
        for (auto& v : views_.views) if (v.collection == collection_) return &v;
        return nullptr;
    }
    void Model::remember() {
        auto* v = view(); if (!v) return;
        const std::string_view path = order_.empty() ? std::string_view{} : rows_[order_[scroll_ / kRowHeight]].path;
        size_t replacedBytes = 0;
        for (const auto& a : v->anchors) if (a.tab == tab_) replacedBytes = a.path.capacity() + 1;
        // Bound context growth during browsing too, not only during preparation.
        // Refuse a new context rather than silently evict a live collection.
        if (contextBytes() - replacedBytes + path.size() + 1 + 3 * sizeof(LO::Anchor) > LO::kMaxRecordBytes) {
            contextError_ = std::make_error_code(std::errc::not_enough_memory); dirty_ = true; return;
        }
        contextError_.clear();
        const LO::Anchor anchor{tab_, std::string{path}, order_.empty() ? 0 : scroll_ % kRowHeight};
        for (auto& a : v->anchors) if (a.tab == tab_) { if (a != anchor) { a = anchor; dirty_ = true; } return; }
        v->anchors.push_back(anchor); dirty_ = true;
    }
    void Model::restore() {
        order_ = project(rows_, collection_, tab_); scroll_ = 0;
        if (auto* v = view()) for (const auto& a : v->anchors) if (a.tab == tab_) {
            for (size_t i = 0; i < order_.size(); ++i) if (rows_[order_[i]].path == a.path) { scroll_ = clampScroll(static_cast<int64_t>(i) * kRowHeight + a.offset_px, order_.size(), viewport_); break; }
        }
        remember();
    }
    void Model::discardRows() {
        remember();
        std::vector<Row>{}.swap(rows_);
        std::vector<size_t>{}.swap(order_);
        // No subsequent remember() until replace() restores this context.
        collection_.clear(); scroll_ = 0;
    }
    bool Model::open(std::string_view collection) {
        if (!exists(collection)) return false;
        if (views_.views.size() >= LO::kMaxViews && std::ranges::none_of(views_.views, [&](const auto& v) { return v.collection == collection; })) return false;
        const bool newContext = std::ranges::none_of(views_.views, [&](const auto& v) { return v.collection == collection; });
        if (newContext && contextBytes() + collection.size() + LO::kMaxPathBytes + 2 * sizeof(LO::View) + 3 * sizeof(LO::Anchor) > LO::kMaxRecordBytes) {
            contextError_ = std::make_error_code(std::errc::not_enough_memory); dirty_ = true; return false;
        }
        remember(); collection_ = collection;
        if (!view()) {
            const Tab initial = collection == "books" && count(rows_, collection).reading > 0 ? Tab::reading : Tab::unread;
            views_.views.push_back({std::string{collection}, initial, {}}); dirty_ = true;
        }
        tab_ = view()->selected; restore(); return true;
    }
    void Model::selectTab(Tab tab) {
        if (tab == Tab::invalid || collection_.empty() || collection_ == "needs-review" || tab == tab_) return;
        remember(); tab_ = tab;
        if (auto* v = view()) { v->selected = tab; dirty_ = true; }
        restore();
    }
    void Model::scrollTo(int64_t pixels, int32_t viewport) {
        viewport_ = std::max<int32_t>(1, viewport); scroll_ = clampScroll(pixels, order_.size(), viewport_); remember();
    }
    std::optional<Selection> Model::select(size_t visibleRow) const {
        if (visibleRow >= order_.size()) return {};
        const auto& row = rows_[order_[visibleRow]];
        if (row.source.sha256.empty()) return {};
        return Selection{row.path, row.source, generation_};
    }
    std::expected<void, std::error_code> Model::replace(std::vector<Row> next, LO::Feeds feeds, uint64_t generation) {
        // Bound retained/orphan feed identities as well as the registry itself.
        std::vector<std::string_view> ids;
        ids.reserve(LO::kMaxFeeds);
        const auto addId = [&](std::string_view id) {
            if (std::ranges::find(ids, id) != ids.end()) return true;
            if (ids.size() == LO::kMaxFeeds) return false;
            ids.push_back(id); return true;
        };
        for (const auto& f : feeds.feeds) if (!addId(f.id)) return std::unexpected(std::make_error_code(std::errc::value_too_large));
        for (const auto& r : next) for (const auto& m : r.memberships) if (!addId(m.feed_id)) return std::unexpected(std::make_error_code(std::errc::value_too_large));
        // Reject duplicate canonical paths: counts and membership aggregates are unique.
        std::vector<size_t> paths(next.size());
        for (size_t i = 0; i < paths.size(); ++i) paths[i] = i;
        std::sort(paths.begin(), paths.end(), [&](size_t a, size_t b) { return next[a].path < next[b].path; });
        for (size_t i = 1; i < paths.size(); ++i) if (next[paths[i]].path == next[paths[i-1]].path) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (memoryBytes() + LibraryView::memoryBytes(next, next.capacity()) + feedBytes(feeds) + kWorkspaceReserve > kMemoryBudget) return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
        remember();
        for (auto& v : views_.views) for (auto& a : v.anchors) {
            const auto newer = project(next, v.collection, a.tab);
            const auto survives = [&](std::string_view path) { return std::ranges::any_of(newer, [&](size_t i) { return next[i].path == path; }); };
            if (a.path.empty() || survives(a.path)) continue;
            const auto older = project(rows_, v.collection, a.tab);
            bool after = false; std::string replacement;
            for (size_t i : older) {
                if (after && survives(rows_[i].path)) { replacement = rows_[i].path; break; }
                if (rows_[i].path == a.path) after = true;
            }
            a.path = std::move(replacement); a.offset_px = 0; dirty_ = true;
        }
        rows_ = std::move(next); feeds_ = std::move(feeds); generation_ = generation;
        const auto removed = std::erase_if(views_.views, [&](const auto& v) { return !exists(v.collection); });
        dirty_ = dirty_ || removed > 0;
        if (!collection_.empty()) {
            if (!exists(collection_)) { collection_.clear(); order_.clear(); scroll_ = 0; }
            else restore();
        }
        return {};
    }
    std::vector<Collection> Model::collections() const {
        std::vector<Collection> result{{"books", "Books", true, count(rows_, "books")}};
        std::vector<const LO::Feed*> feeds;
        for (const auto& f : feeds_.feeds) if (exists("feed:" + f.id)) feeds.push_back(&f);
        std::sort(feeds.begin(), feeds.end(), [](const auto* a, const auto* b) { return a->subscription_order != b->subscription_order ? a->subscription_order < b->subscription_order : a->id < b->id; });
        for (const auto* f : feeds) result.push_back({"feed:" + f->id, f->title.empty() ? f->id : f->title, f->active, count(rows_, "feed:" + f->id)});
        // Orphan memberships remain accessible even if the registry is damaged.
        for (const auto& r : rows_) for (const auto& m : r.memberships) {
            const auto id = "feed:" + m.feed_id;
            if (std::ranges::none_of(result, [&](const auto& c) { return c.id == id; })) result.push_back({id, m.feed_id, false, count(rows_, id)});
        }
        const auto other = count(rows_, "other-articles");
        if (other.unread + other.reading + other.read + other.unknown + other.loading) result.push_back({"other-articles", "Other articles", true, other});
        const auto unknown = count(rows_, "needs-review");
        if (unknown.unknown) result.push_back({"needs-review", "Needs review", true, unknown});
        return result;
    }
}
