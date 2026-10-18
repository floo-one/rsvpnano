#include <unity.h>
#include <FS.h>
#include "library/LibraryOrganization.h"
#include "storage/fs/StoragePaths.h"
#include "hash/Fnv1a.h"
#include <cstdio>
using namespace LibraryOrganization;
namespace Board::Storage { fs::FS& filesystem() { static fs::FS instance; return instance; } }
void setUp() {}
void tearDown() {}
Item item() {
    Item r;
    r.path = "/library/books/Book.epub";
    r.source = {123, std::string(64, 'a')};
    r.status = Status::reading;
    r.revision = 1;
    return r;
}
void roundtrip_item() {
    auto r = item();
    r.indexed = IndexIdentity{123, 42, 400};
    r.memberships = {{"feed-1", 2, 3}, {"feed-2", 4, 5}};
    r.restart = RestartIntent{"operation-1", true, "word_index = 42\nfont = \"hello\\world\"\n", 0};
    auto encoded = encode(r);
    TEST_ASSERT_TRUE(encoded.has_value());
    auto decoded = decodeItem(*encoded);
    if (!decoded) std::printf("%s\n%s\n", decoded.error().message().c_str(), encoded->c_str());
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_TRUE(*decoded == r);
}
void roundtrip_feeds_views() {
    Feeds feeds; feeds.revision = 1; feeds.next_order = 3;
    feeds.feeds = {{"one", "https://one.example/rss?a=1&b=2", "Café \"one\"", true, 1},
                   {"two", "https://two.example/rss", "二", false, 2}};
    auto bytes = encode(feeds);
    TEST_ASSERT_TRUE(bytes.has_value());
    auto result = decodeFeeds(*bytes);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(*result == feeds);
    Views views; views.revision = 1;
    views.views = {{"books", Tab::reading, {{Tab::reading, item().path, 13}, {Tab::unread, "", 0}}},
                   {"feed:one", Tab::unread, {{Tab::unread, "/library/articles/one.txt", 51}}}};
    bytes = encode(views);
    TEST_ASSERT_TRUE(bytes.has_value());
    auto v = decodeViews(*bytes);
    if (!v) std::printf("%s\n%s\n", v.error().message().c_str(), bytes->c_str());
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_TRUE(*v == views);
}
void save_load() {
    fs::FS fs;
    auto r = item();
    auto saved = saveItem(fs, r, 0);
    if (!saved) {
        std::printf("save: %s\n", saved.error().message().c_str());
        for (const auto& [name, node] : fs.disk->nodes) std::printf("%s:\n%s\n", name.c_str(), node->bytes.c_str());
    }
    TEST_ASSERT_TRUE(saved.has_value());
    auto loaded = loadItem(fs, r.path, r.source);
    TEST_ASSERT_TRUE(loaded.has_value());
    TEST_ASSERT_TRUE(*loaded == r);
}
std::string recordPath() { return StoragePaths::libraryOrganizationPathFor(item().path); }
std::string envelope(std::string body) {
    char sum[9]; std::snprintf(sum, sizeof(sum), "%08x", Fnv1a::hash(body));
    return std::string("# rsvpnano-library v1 ") + sum + "\n" + body;
}
std::string bodyOf(const std::string& bytes) { return bytes.substr(bytes.find('\n') + 1); }
std::string editRecord(std::string bytes, const std::string& from, const std::string& to) {
    auto body = bodyOf(bytes);
    auto pos = body.find(from);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, pos);
    body.replace(pos, from.size(), to);
    return envelope(body);
}
std::string encodedItem() { auto r = encode(item()); TEST_ASSERT_TRUE(r.has_value()); return *r; }
template<class T> void errorIs(const std::expected<T, std::error_code>& result, std::error_code error) {
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL_STRING(error.message().c_str(), result.error().message().c_str());
    TEST_ASSERT_TRUE(result.error() == error);
}
template<class T> void errorIs(const std::expected<T, std::error_code>& result, Error error) {
    errorIs(result, make_error_code(error));
}
auto snapshot(const fs::FS& fs) {
    std::map<std::string, std::pair<std::string, bool>> result;
    for (const auto& [path, node] : fs.disk->nodes) result[path] = {node->bytes, node->directory};
    return result;
}
void empty_and_unknown_roundtrips() {
    auto r = item(); r.status = Status::unknown; r.unknown_reason = "legacy-unindexed";
    TEST_ASSERT_TRUE(decodeItem(*encode(r)) == r);
    Feeds feeds; feeds.revision = 1;
    Views views; views.revision = 1;
    TEST_ASSERT_TRUE(decodeFeeds(*encode(feeds)) == feeds);
    TEST_ASSERT_TRUE(decodeViews(*encode(views)) == views);
}
void rejects_corruption_and_truncation() {
    const auto bytes = encodedItem();
    // Every prefix, including valid TOML prefixes, must fail integrity checking.
    for (size_t size = 0; size < bytes.size(); ++size) TEST_ASSERT_FALSE(decodeItem(bytes.substr(0, size)).has_value());
    auto broken = bytes; broken.back() ^= 1;
    errorIs(decodeItem(broken), Error::corrupt);
    errorIs(decodeItem(envelope("schema_version = 1\nrevision = 1\n")), Error::corrupt);
    errorIs(decodeItem(envelope("this is not toml")), Error::corrupt);
}
void rejects_future_versions_and_fields() {
    auto bytes = encodedItem();
    errorIs(decodeItem(editRecord(bytes, "schema_version = 1", "schema_version = 2")), Error::unsupported_version);
    errorIs(decodeItem(envelope("new_field = 42\n" + bodyOf(bytes))), Error::unsupported_fields);
    errorIs(decodeItem(envelope(bodyOf(bytes) + "new_source_field = 42\n")), Error::unsupported_fields);
    bytes.replace(bytes.find("v1"), 2, "v2");
    errorIs(decodeItem(bytes), Error::unsupported_version);
}
void rejects_bad_identity_path_status() {
    for (auto name : {"/library/../book.txt", "/other/book.txt", "/library/books/a/b.txt", "/library/.hidden.txt",
                      "/library/books/.hidden.txt", "/library/books/book.rstate.toml", "/library/books/a\\b.txt"}) {
        auto r = item(); r.path = name; TEST_ASSERT_FALSE(encode(r).has_value());
    }
    for (auto name : {"/library/Book.txt", "/library/books/Book.PDF", "/library/articles/a.rsvp"}) {
        auto r = item(); r.path = name; TEST_ASSERT_TRUE(encode(r).has_value());
    }
    auto r = item(); r.source.sha256[0] = 'g'; TEST_ASSERT_FALSE(encode(r).has_value());
    r = item(); r.status = Status::unknown; TEST_ASSERT_FALSE(encode(r).has_value());
    r = item(); r.unknown_reason = "legacy"; TEST_ASSERT_FALSE(encode(r).has_value());
    r = item(); r.status = Status::invalid; TEST_ASSERT_FALSE(encode(r).has_value());
    r = item(); r.indexed = IndexIdentity{}; TEST_ASSERT_FALSE(encode(r).has_value());
    r = item(); r.source = {}; TEST_ASSERT_FALSE(encode(r).has_value());
    auto bytes = encodedItem();
    TEST_ASSERT_FALSE(decodeItem(editRecord(bytes, "status = \"reading\"", "status = \"finished\"")).has_value());
    TEST_ASSERT_FALSE(decodeItem(editRecord(bytes, "revision = 1", "revision = -1")).has_value());
    TEST_ASSERT_FALSE(decodeItem(editRecord(bytes, "revision = 1", "revision = 0")).has_value());
}
void rejects_limits_duplicates_and_invalid_utf8() {
    auto r = item(); r.status = Status::unknown; r.unknown_reason = std::string(kMaxLabelBytes, 'a');
    TEST_ASSERT_TRUE(encode(r).has_value());
    r.unknown_reason += 'a'; TEST_ASSERT_FALSE(encode(r).has_value());
    r.unknown_reason = "\xc0\xaf"; TEST_ASSERT_FALSE(encode(r).has_value());
    r.unknown_reason = "control\n"; TEST_ASSERT_FALSE(encode(r).has_value());
    r = item(); r.memberships = {{"one", 1, 0}, {"one", 2, 0}}; TEST_ASSERT_FALSE(encode(r).has_value());
    r.memberships.clear();
    for (size_t i = 0; i < kMaxMemberships; ++i) r.memberships.push_back({"feed-" + std::to_string(i), 1, 0});
    TEST_ASSERT_TRUE(encode(r).has_value());
    r.memberships.push_back({"overflow", 1, 0}); TEST_ASSERT_FALSE(encode(r).has_value());
    errorIs(decodeItem(std::string(kMaxRecordBytes + 1, ' ')), std::make_error_code(std::errc::value_too_large));
    Feeds feeds; feeds.revision = 1; feeds.next_order = 1000;
    for (size_t i = 0; i < kMaxActiveFeeds; ++i)
        feeds.feeds.push_back({"f" + std::to_string(i), "https://example.com/" + std::to_string(i), "", true, i + 1});
    TEST_ASSERT_TRUE(encode(feeds).has_value());
    feeds.feeds.push_back({"extra", "https://extra.example/rss", "", true, 25}); TEST_ASSERT_FALSE(encode(feeds).has_value());
    feeds.feeds.back().active = false; TEST_ASSERT_TRUE(encode(feeds).has_value());
    feeds.feeds.back().url = feeds.feeds.front().url; TEST_ASSERT_FALSE(encode(feeds).has_value());
    feeds.feeds.back().url = "https://extra.example/rss";
    feeds.last_attempted_feed = "missing"; TEST_ASSERT_FALSE(encode(feeds).has_value());
    feeds.last_attempted_feed.clear();
    feeds.feeds[0].title.assign(kMaxLabelBytes + 1, 'a'); TEST_ASSERT_FALSE(encode(feeds).has_value());
}
void views_and_restart_validation() {
    Views views; views.revision = 1;
    views.views = {{"books", Tab::reading, {{Tab::reading, item().path, 52}}}};
    TEST_ASSERT_FALSE(encode(views).has_value());
    views.views[0].anchors[0].offset_px = 51; TEST_ASSERT_TRUE(encode(views).has_value());
    views.views[0].anchors.push_back(views.views[0].anchors[0]); TEST_ASSERT_FALSE(encode(views).has_value());
    views.views[0].anchors.clear(); views.views[0].collection = "feed:"; TEST_ASSERT_FALSE(encode(views).has_value());
    auto r = item(); r.restart = RestartIntent{"operation", false, "", 0}; TEST_ASSERT_TRUE(encode(r).has_value());
    r.restart->desired_word_index = 1; TEST_ASSERT_FALSE(encode(r).has_value());
    r.restart->desired_word_index = 0; r.restart->had_bookmark = true; TEST_ASSERT_FALSE(encode(r).has_value());
}
void full_filename_sidecars_and_bookmarks_untouched() {
    fs::FS fs; auto r = item();
    const auto bookState = StoragePaths::bookStatePathFor(r.path);
    fs.put(r.path, "original book bytes"); fs.put(bookState, "exact original bookmark");
    TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
    auto txt = r; txt.path = "/library/books/Book.txt";
    TEST_ASSERT_TRUE(saveItem(fs, txt, 0).has_value());
    TEST_ASSERT_TRUE(recordPath() != StoragePaths::libraryOrganizationPathFor(txt.path));
    TEST_ASSERT_TRUE(StoragePaths::isHiddenOrSidecarPath(recordPath()));
    TEST_ASSERT_EQUAL_STRING("original book bytes", fs.bytes(r.path).c_str());
    TEST_ASSERT_EQUAL_STRING("exact original bookmark", fs.bytes(bookState).c_str());
}
void revisions_and_other_record_stores() {
    fs::FS fs; auto r = item();
    TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
    errorIs(saveItem(fs, r, 0), Error::revision_conflict);
    auto updated = saveItem(fs, r, 1); TEST_ASSERT_TRUE(updated.has_value()); TEST_ASSERT_EQUAL_UINT64(2, updated->revision);
    errorIs(saveItem(fs, r, UINT64_MAX), std::make_error_code(std::errc::value_too_large));
    Feeds feeds; Views views;
    auto f = saveFeeds(fs, feeds, 0); auto v = saveViews(fs, views, 0);
    TEST_ASSERT_TRUE(f.has_value()); TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_TRUE(loadFeeds(fs) == *f); TEST_ASSERT_TRUE(loadViews(fs) == *v);
    fs.remove(StoragePaths::kLibraryFeedsPath);
    fs.put(std::string(StoragePaths::kLibraryFeedsPath) + ".bak", *encode(*f));
    TEST_ASSERT_TRUE(loadFeeds(fs) == *f);
    fs.put(StoragePaths::kLibraryViewPath, "broken");
    fs.put(std::string(StoragePaths::kLibraryViewPath) + ".bak", *encode(*v));
    TEST_ASSERT_TRUE(loadViews(fs) == *v);
}
void stale_source_and_path_never_fall_back() {
    fs::FS fs; auto r = item();
    auto old = r; old.source.sha256[63] = 'b';
    fs.put(recordPath(), *encode(old)); fs.put(recordPath() + ".bak", encodedItem());
    auto before = snapshot(fs);
    errorIs(loadItem(fs, r.path, r.source), Error::stale_identity);
    errorIs(saveItem(fs, r, 1), Error::stale_identity);
    TEST_ASSERT_TRUE(before == snapshot(fs));
    old = r; old.path = "/library/books/Other.epub"; fs.put(recordPath(), *encode(old));
    errorIs(loadItem(fs, r.path, r.source), Error::stale_identity);
}
void final_wins_without_mutation() {
    fs::FS fs; auto r = item();
    fs.put(recordPath(), encodedItem()); fs.put(recordPath() + ".bak", "bad"); fs.put(recordPath() + ".tmp", "bad");
    auto before = snapshot(fs);
    TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == r);
    TEST_ASSERT_TRUE(before == snapshot(fs));
}
void recover_backup_for_missing_corrupt_and_empty_final() {
    for (auto mode : {0, 1, 2}) {
        fs::FS fs; auto r = item();
        if (mode) fs.put(recordPath(), mode == 1 ? "broken" : "");
        fs.put(recordPath() + ".bak", encodedItem());
        auto newer = r; newer.revision = 2; newer.status = Status::read;
        fs.put(recordPath() + ".tmp", *encode(newer));
        TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == r);
        TEST_ASSERT_EQUAL_STRING(encodedItem().c_str(), fs.bytes(recordPath()).c_str());
        TEST_ASSERT_TRUE(fs.exists(recordPath() + ".tmp"));
    }
}
void staged_creation_requires_explicit_intent() {
    fs::FS fs; auto r = item(); fs.put(recordPath() + ".tmp", encodedItem());
    auto before = snapshot(fs);
    errorIs(loadItem(fs, r.path, r.source), Error::recovery_required);
    errorIs(saveItem(fs, r, 0), Error::recovery_required);
    TEST_ASSERT_TRUE(before == snapshot(fs));
    TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source, Recovery::stagedCreation) == r);
    TEST_ASSERT_FALSE(fs.exists(recordPath() + ".tmp"));
    fs::FS update; r.revision = 2; update.put(recordPath() + ".tmp", *encode(r));
    errorIs(loadItem(update, r.path, r.source, Recovery::stagedCreation), Error::recovery_required);
    fs::FS damaged; damaged.put(recordPath(), "bad"); damaged.put(recordPath() + ".tmp", encodedItem());
    errorIs(loadItem(damaged, r.path, r.source, Recovery::stagedCreation), Error::recovery_required);
}
void unsupported_candidates_are_preserved() {
    auto r = item();
    const auto future = editRecord(encodedItem(), "schema_version = 1", "schema_version = 2");
    for (auto suffix : {"", ".bak", ".tmp"}) {
        fs::FS fs; fs.put(recordPath() + suffix, future);
        if (std::string(suffix).empty()) fs.put(recordPath() + ".bak", encodedItem());
        auto before = snapshot(fs);
        errorIs(loadItem(fs, r.path, r.source), Error::unsupported_version);
        errorIs(saveItem(fs, r, 0), Error::unsupported_version);
        TEST_ASSERT_TRUE(before == snapshot(fs));
    }
    for (auto suffix : {".tmp", ".bak"}) {
        fs::FS fs; fs.put(recordPath(), encodedItem()); fs.put(recordPath() + suffix, future);
        auto before = snapshot(fs);
        errorIs(saveItem(fs, r, 1), Error::unsupported_version);
        TEST_ASSERT_TRUE(before == snapshot(fs));
    }
}
void missing_corrupt_oversized_directory_are_distinct() {
    fs::FS fs; auto r = item();
    errorIs(loadItem(fs, r.path, r.source), std::make_error_code(std::errc::no_such_file_or_directory));
    fs.put(recordPath(), "bad"); errorIs(loadItem(fs, r.path, r.source), Error::corrupt);
    fs.put(recordPath(), std::string(kMaxRecordBytes + 1, ' '));
    fs.put(recordPath() + ".bak", encodedItem());
    errorIs(loadItem(fs, r.path, r.source), std::make_error_code(std::errc::value_too_large));
    fs.disk->nodes.erase(recordPath()); fs.mkdir(recordPath().c_str());
    auto before = snapshot(fs);
    errorIs(loadItem(fs, r.path, r.source), std::make_error_code(std::errc::is_a_directory));
    errorIs(saveItem(fs, r, 1), std::make_error_code(std::errc::is_a_directory));
    TEST_ASSERT_TRUE(before == snapshot(fs));
}
void read_io_never_becomes_absence_or_corruption() {
    for (auto operation : {"open-read", "read"}) {
        fs::FS fs; auto r = item();
        fs.put(recordPath(), encodedItem()); fs.put(recordPath() + ".bak", encodedItem());
        fs.disk->fail = [&](const auto& op, const auto& path) { return op == operation && path == recordPath(); };
        auto before = snapshot(fs);
        errorIs(loadItem(fs, r.path, r.source), std::make_error_code(std::errc::io_error));
        errorIs(saveItem(fs, r, 1), std::make_error_code(std::errc::io_error));
        TEST_ASSERT_TRUE(before == snapshot(fs));
    }
}
void update_faults_keep_a_committed_record() {
    for (auto operation : {"open-write", "write", "flush", "rename"}) {
        for (bool failFinalRename : {false, true}) {
            fs::FS fs; auto r = item(); TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
            fs.disk->fail = [&](const auto& op, const auto& path) {
                return op == operation && (op != "rename" || path == recordPath() + (failFinalRename ? ".tmp" : ""));
            };
            r.status = Status::read;
            TEST_ASSERT_FALSE(saveItem(fs, r, 1).has_value());
            fs.reboot();
            TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == item());
            TEST_ASSERT_TRUE(saveItem(fs, r, 1).has_value());
        }
    }
}
void cleanup_and_recovery_failures_preserve_backup() {
    fs::FS fs; auto r = item();
    fs.put(recordPath(), encodedItem()); fs.put(recordPath() + ".bak", encodedItem());
    fs.disk->fail = [](const auto& op, const auto&) { return op == "remove"; };
    errorIs(saveItem(fs, r, 1), std::make_error_code(std::errc::io_error));
    TEST_ASSERT_TRUE(fs.exists(recordPath())); TEST_ASSERT_TRUE(fs.exists(recordPath() + ".bak"));
    for (auto op : {"remove", "rename"}) {
        fs.reboot(); fs.put(recordPath(), "bad");
        fs.disk->fail = [&](const auto& operation, const auto&) { return operation == op; };
        errorIs(loadItem(fs, r.path, r.source), std::make_error_code(std::errc::io_error));
        TEST_ASSERT_EQUAL_STRING(encodedItem().c_str(), fs.bytes(recordPath() + ".bak").c_str());
        fs.reboot(); TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == r);
        fs.put(recordPath() + ".bak", encodedItem());
    }
}
void power_loss_at_every_update_boundary() {
    size_t boundaries = 0;
    for (size_t crash = 0; crash <= boundaries || crash == 0; ++crash) {
        fs::FS fs; auto r = item();
        TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
        auto second = saveItem(fs, r, 1); TEST_ASSERT_TRUE(second.has_value());
        // Exercise removal of both stale stage and the retained previous backup.
        fs.put(recordPath() + ".tmp", "old partial write");
        fs.reboot(); fs.disk->crashAfter = crash;
        r.status = Status::read;
        bool interrupted = false;
        try { TEST_ASSERT_TRUE(saveItem(fs, r, 2).has_value()); }
        catch (const fs::PowerLoss&) { interrupted = true; }
        if (crash == 0) boundaries = fs.disk->mutations.size();
        else TEST_ASSERT_TRUE(interrupted);
        fs.reboot();
        auto loaded = loadItem(fs, r.path, r.source); TEST_ASSERT_TRUE(loaded.has_value());
        TEST_ASSERT_TRUE((loaded->revision == 2 && loaded->status == Status::reading)
                      || (loaded->revision == 3 && loaded->status == Status::read));
        TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == *loaded); // Recovery is idempotent.
        TEST_ASSERT_TRUE(saveItem(fs, r, loaded->revision).has_value());
    }
    TEST_ASSERT_EQUAL_UINT(7, boundaries);
}
void power_loss_during_backup_recovery() {
    for (size_t crash = 1; crash <= 2; ++crash) {
        fs::FS fs; auto r = item(); fs.put(recordPath(), "bad"); fs.put(recordPath() + ".bak", encodedItem());
        fs.disk->crashAfter = crash;
        bool interrupted = false;
        try { (void)loadItem(fs, r.path, r.source); } catch (const fs::PowerLoss&) { interrupted = true; }
        TEST_ASSERT_TRUE(interrupted);
        fs.reboot(); TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == r);
    }
}
void power_loss_during_creation_is_not_silent_commit() {
    for (size_t crash = 1; crash <= 4; ++crash) {
        fs::FS fs; auto r = item(); fs.disk->crashAfter = crash;
        bool interrupted = false;
        try { (void)saveItem(fs, r, 0); } catch (const fs::PowerLoss&) { interrupted = true; }
        TEST_ASSERT_TRUE(interrupted);
        fs.reboot(); auto loaded = loadItem(fs, r.path, r.source);
        if (crash == 4) TEST_ASSERT_TRUE(loaded == r);
        else if (crash == 1) errorIs(loaded, Error::corrupt);
        else {
            errorIs(loaded, Error::recovery_required);
            TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source, Recovery::stagedCreation) == r);
        }
    }
}
void hash_known_vectors_and_bounded_chunks() {
    fs::FS fs; const auto path = item().path;
    const std::pair<std::string, std::string> vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"}
    };
    for (const auto& [bytes, expected] : vectors) {
        fs.put(path, bytes); uint64_t previous = 0; size_t calls = 0;
        auto hash = hashSource(fs, path, [&](uint64_t processed, uint64_t total) {
            TEST_ASSERT_EQUAL_UINT64(bytes.size(), total); TEST_ASSERT_TRUE(processed - previous <= 512);
            previous = processed; ++calls; return true;
        });
        TEST_ASSERT_TRUE(hash.has_value()); TEST_ASSERT_EQUAL_STRING(expected.c_str(), hash->sha256.c_str());
        TEST_ASSERT_EQUAL_UINT64(bytes.size(), hash->bytes);
        TEST_ASSERT_EQUAL_UINT64(bytes.size(), previous); TEST_ASSERT_EQUAL_UINT((bytes.size() + 511) / 512 + 1, calls);
    }
}
void hashing_cancellation_read_failure_and_changed_size() {
    fs::FS fs; const auto path = item().path; fs.put(path, std::string(1025, 'x'));
    errorIs(hashSource(fs, path, [](auto done, auto) { return done < 512; }), std::make_error_code(std::errc::operation_canceled));
    fs.disk->fail = [](const auto& op, const auto&) { return op == "read"; };
    errorIs(hashSource(fs, path), std::make_error_code(std::errc::io_error));
    fs.reboot();
    errorIs(hashSource(fs, path, [&](auto done, auto) {
        if (done) fs.disk->nodes.at(path)->bytes += 'y';
        return true;
    }), Error::stale_identity);
    fs::FS absent;
    errorIs(hashSource(absent, path), std::make_error_code(std::errc::no_such_file_or_directory));
    absent.mkdir(path.c_str()); errorIs(hashSource(absent, path), std::make_error_code(std::errc::is_a_directory));
    errorIs(hashSource(fs, "/config/settings.toml"), std::make_error_code(std::errc::invalid_argument));
}

void record_and_collection_caps_are_enforced() {
    Feeds feeds; feeds.revision = 1; feeds.next_order = 1000;
    for (size_t i = 0; i < kMaxFeeds; ++i)
        feeds.feeds.push_back({"f" + std::to_string(i), "https://example.com/" + std::to_string(i), "", false, i + 1});
    auto encoded = encode(feeds); TEST_ASSERT_TRUE(encoded.has_value());
    TEST_ASSERT_TRUE(decodeFeeds(*encoded) == feeds);
    feeds.feeds.push_back({"overflow", "https://extra.example", "", false, 999});
    TEST_ASSERT_FALSE(encode(feeds).has_value());
    feeds.feeds.pop_back();
    for (auto& feed : feeds.feeds) feed.url += std::string(3900, 'x');
    errorIs(encode(feeds), std::make_error_code(std::errc::value_too_large));
    fs::FS fs; errorIs(saveFeeds(fs, feeds, 0), std::make_error_code(std::errc::value_too_large));
    TEST_ASSERT_TRUE(fs.disk->nodes.empty());
    Views views; views.revision = 1;
    views.views = {{"books", Tab::unread, {}}, {"needs-review", Tab::unread, {}}, {"other-articles", Tab::read, {}}};
    for (size_t i = 0; i < kMaxFeeds; ++i) views.views.push_back({"feed:f" + std::to_string(i), Tab::unread, {}});
    encoded = encode(views); TEST_ASSERT_TRUE(encoded.has_value()); TEST_ASSERT_TRUE(decodeViews(*encoded) == views);
    views.views.push_back({"feed:extra", Tab::unread, {}}); TEST_ASSERT_FALSE(encode(views).has_value());
    auto r = item(); r.source_url = "https://example.com/";
    r.source_url.resize(kMaxPathBytes, 'a'); TEST_ASSERT_TRUE(encode(r).has_value());
    r.source_url += 'a'; TEST_ASSERT_FALSE(encode(r).has_value());
}
void partial_creation_can_be_retried_only_with_intent() {
    fs::FS fs; auto r = item(); fs.put(recordPath() + ".tmp", "partial bytes");
    errorIs(loadItem(fs, r.path, r.source), Error::corrupt);
    errorIs(saveItem(fs, r, 0), Error::corrupt);
    errorIs(loadItem(fs, r.path, r.source, Recovery::stagedCreation), std::make_error_code(std::errc::no_such_file_or_directory));
    TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
    fs::FS protectedBackup; protectedBackup.put(recordPath() + ".bak", "bad"); protectedBackup.put(recordPath() + ".tmp", "partial");
    auto before = snapshot(protectedBackup);
    errorIs(loadItem(protectedBackup, r.path, r.source, Recovery::stagedCreation), Error::corrupt);
    TEST_ASSERT_TRUE(before == snapshot(protectedBackup));
}
void all_candidates_checked_before_cleanup() {
    fs::FS fs; auto r = item(); fs.put(recordPath(), encodedItem());
    fs.put(recordPath() + ".tmp", "partial old update");
    fs.put(recordPath() + ".bak", editRecord(encodedItem(), "schema_version = 1", "schema_version = 2"));
    auto before = snapshot(fs);
    errorIs(saveItem(fs, r, 1), Error::unsupported_version); TEST_ASSERT_TRUE(before == snapshot(fs));
    r.source.sha256[0] = 'b'; fs.put(recordPath() + ".bak", *encode(r));
    before = snapshot(fs);
    errorIs(saveItem(fs, item(), 1), Error::stale_identity); TEST_ASSERT_TRUE(before == snapshot(fs));
}
void staging_read_and_postcommit_read_failure() {
    auto r = item();
    fs::FS fs; TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
    // Fail only when the completed temporary file is read, after preflight.
    fs.disk->fail = [&](const auto& op, const auto& path) {
        return op == "read" && path == recordPath() + ".tmp";
    };
    errorIs(saveItem(fs, r, 1), std::make_error_code(std::errc::io_error));
    fs.reboot(); TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == r);
    fs.disk->fail = [&](const auto& op, const auto& path) {
        return op == "read" && path == recordPath() && fs.exists(recordPath() + ".bak");
    };
    errorIs(saveItem(fs, r, 1), std::make_error_code(std::errc::io_error));
    fs.reboot(); auto loaded = loadItem(fs, r.path, r.source);
    TEST_ASSERT_TRUE(loaded.has_value()); TEST_ASSERT_EQUAL_UINT64(2, loaded->revision);
    errorIs(saveItem(fs, r, 1), Error::revision_conflict); // Reload, don't blindly repeat.
}
void empty_file_reopen_error_does_not_trigger_recovery() {
    fs::FS fs; auto r = item(); fs.put(recordPath(), ""); fs.put(recordPath() + ".bak", encodedItem());
    size_t opens = 0;
    fs.disk->fail = [&](const auto& op, const auto& path) {
        return op == "open-read" && path == recordPath() && ++opens == 2;
    };
    const auto before = snapshot(fs);
    errorIs(loadItem(fs, r.path, r.source), std::make_error_code(std::errc::io_error));
    TEST_ASSERT_TRUE(before == snapshot(fs));
}
void deeply_nested_or_wrong_type_input_is_rejected() {
    const auto nested = "new_field = " + std::string(300, '[') + "1" + std::string(300, ']') + "\n";
    TEST_ASSERT_FALSE(decodeItem(envelope(nested + bodyOf(encodedItem()))).has_value());
    TEST_ASSERT_FALSE(decodeItem(editRecord(encodedItem(), "bytes = 123", "bytes = \"123\"")).has_value());
    TEST_ASSERT_FALSE(decodeItem(editRecord(encodedItem(), "bytes = 123", "bytes = 18446744073709551616")).has_value());
}

void every_filesystem_failure_in_update_preserves_committed_state() {
    size_t operationCount = 0;
    for (size_t failAt = 0; failAt <= operationCount; ++failAt) {
        fs::FS fs; auto r = item();
        TEST_ASSERT_TRUE(saveItem(fs, r, 0).has_value());
        TEST_ASSERT_TRUE(saveItem(fs, r, 1).has_value());
        fs.put(recordPath() + ".tmp", "partial previous update");
        fs.reboot();
        size_t calls = 0;
        fs.disk->fail = [&](const auto&, const auto&) { return ++calls == failAt; };
        r.status = Status::read;
        auto result = saveItem(fs, r, 2);
        if (failAt == 0) { operationCount = calls; TEST_ASSERT_TRUE(result.has_value()); }
        else TEST_ASSERT_FALSE(result.has_value());
        fs.reboot();
        auto loaded = loadItem(fs, r.path, r.source); TEST_ASSERT_TRUE(loaded.has_value());
        TEST_ASSERT_TRUE((loaded->revision == 2 && loaded->status == Status::reading)
                     || (loaded->revision == 3 && loaded->status == Status::read));
        TEST_ASSERT_TRUE(saveItem(fs, r, loaded->revision).has_value());
    }
    TEST_ASSERT_GREATER_THAN(10, operationCount);
}
void every_filesystem_failure_in_recovery_preserves_a_valid_copy() {
    size_t operationCount = 0;
    for (size_t failAt = 0; failAt <= operationCount; ++failAt) {
        fs::FS fs; auto r = item(); fs.put(recordPath(), "bad"); fs.put(recordPath() + ".bak", encodedItem());
        size_t calls = 0;
        fs.disk->fail = [&](const auto&, const auto&) { return ++calls == failAt; };
        auto result = loadItem(fs, r.path, r.source);
        if (failAt == 0) { operationCount = calls; TEST_ASSERT_TRUE(result.has_value()); }
        else TEST_ASSERT_FALSE(result.has_value());
        fs.reboot(); TEST_ASSERT_TRUE(loadItem(fs, r.path, r.source) == r);
    }
    TEST_ASSERT_GREATER_THAN(5, operationCount);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(roundtrip_item);
    RUN_TEST(roundtrip_feeds_views);
    RUN_TEST(save_load);
    RUN_TEST(empty_and_unknown_roundtrips);
    RUN_TEST(rejects_corruption_and_truncation);
    RUN_TEST(rejects_future_versions_and_fields);
    RUN_TEST(rejects_bad_identity_path_status);
    RUN_TEST(rejects_limits_duplicates_and_invalid_utf8);
    RUN_TEST(views_and_restart_validation);
    RUN_TEST(full_filename_sidecars_and_bookmarks_untouched);
    RUN_TEST(revisions_and_other_record_stores);
    RUN_TEST(stale_source_and_path_never_fall_back);
    RUN_TEST(final_wins_without_mutation);
    RUN_TEST(recover_backup_for_missing_corrupt_and_empty_final);
    RUN_TEST(staged_creation_requires_explicit_intent);
    RUN_TEST(unsupported_candidates_are_preserved);
    RUN_TEST(missing_corrupt_oversized_directory_are_distinct);
    RUN_TEST(read_io_never_becomes_absence_or_corruption);
    RUN_TEST(update_faults_keep_a_committed_record);
    RUN_TEST(cleanup_and_recovery_failures_preserve_backup);
    RUN_TEST(power_loss_at_every_update_boundary);
    RUN_TEST(power_loss_during_backup_recovery);
    RUN_TEST(power_loss_during_creation_is_not_silent_commit);
    RUN_TEST(hash_known_vectors_and_bounded_chunks);
    RUN_TEST(hashing_cancellation_read_failure_and_changed_size);
    RUN_TEST(record_and_collection_caps_are_enforced);
    RUN_TEST(partial_creation_can_be_retried_only_with_intent);
    RUN_TEST(all_candidates_checked_before_cleanup);
    RUN_TEST(staging_read_and_postcommit_read_failure);
    RUN_TEST(empty_file_reopen_error_does_not_trigger_recovery);
    RUN_TEST(deeply_nested_or_wrong_type_input_is_rejected);
    RUN_TEST(every_filesystem_failure_in_update_preserves_committed_state);
    RUN_TEST(every_filesystem_failure_in_recovery_preserves_a_valid_copy);
    return UNITY_END();
}
