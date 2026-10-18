#include <unity.h>
#include <FS.h>
#include "library/ReadingOrganization.h"
#include "library/ReadingStateStore.h"
#include "reader/ReadingLoop.h"
#include "library/ReadingProgress.h"
#include "library/BookLibrary.h"
#include "storage/fs/StoragePaths.h"
using namespace LibraryOrganization;
using ReadingOrganization::Command;
namespace Board::Storage { fs::FS& filesystem() { static fs::FS instance; return instance; } }
namespace BookLibrary { std::string_view displayName(const Entry& entry) { return entry.title; } }
void setUp() { Board::Storage::filesystem() = fs::FS{}; }
void tearDown() {}
const std::string path = "/library/books/story.txt";
const reading::BookIdentity indexIdentity{3, 42, 3};
ReadingOrganization::Target target(fs::FS& fs) {
    fs.put(path, "abc");
    Feeds feeds; TEST_ASSERT_TRUE(saveFeeds(fs, feeds, 0).has_value());
    auto result = ReadingOrganization::prepare(fs, path, indexIdentity, 1);
    TEST_ASSERT_TRUE(result.has_value());
    return *result;
}
auto command(fs::FS& fs, const ReadingOrganization::Target& target, Command cmd, std::string operation) {
    return ReadingOrganization::apply(fs, {target, operation, cmd}, target);
}
void normal_open_does_not_infer_status() {
    fs::FS fs; const auto t = target(fs);
    auto result = loadItem(fs, path, t.source);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_TRUE(result.error() == std::errc::no_such_file_or_directory);
}
void transitions_and_bookmark_preservation() {
    fs::FS fs; const auto t = target(fs);
    const reading::StoredState state{indexIdentity, {.wordIndex=2}};
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, state).has_value());
    const auto original = fs.bytes(StoragePaths::bookStatePathFor(path));
    auto r = command(fs, t, Command::started, "start-1"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::reading);
    auto repeated = command(fs, t, Command::started, "start-1"); TEST_ASSERT_TRUE(repeated == r);
    r = command(fs, t, Command::naturalEnd, "end-1"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::read);
    const auto completed = r->completed_order;
    r = command(fs, t, Command::started, "start-2"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::read);
    TEST_ASSERT_EQUAL_UINT64(completed, r->completed_order);
    r = command(fs, t, Command::markUnread, "unread-1"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::unread);
    TEST_ASSERT_EQUAL_STRING(original.c_str(), fs.bytes(StoragePaths::bookStatePathFor(path)).c_str());
    r = command(fs, t, Command::started, "start-3"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::reading);
    r = command(fs, t, Command::markRead, "read-1"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::read);
    TEST_ASSERT_EQUAL_STRING(original.c_str(), fs.bytes(StoragePaths::bookStatePathFor(path)).c_str());
}
void restart_preserves_typography_and_is_idempotent() {
    fs::FS fs; const auto t = target(fs);
    reading::StoredState state{indexIdentity, {.wordIndex=2}};
    state.reading.overrides.languageFonts = {{"en", "test-font"}};
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, state).has_value());
    auto r = command(fs, t, Command::startAgain, "reset-1"); TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->status == Status::reading); TEST_ASSERT_FALSE(r->restart.has_value());
    auto reset = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(reset.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, reset->reading.wordIndex);
    TEST_ASSERT_TRUE(reset->reading.overrides == state.reading.overrides);
    TEST_ASSERT_TRUE(command(fs, t, Command::startAgain, "reset-1") == r);
}
void completion_requires_timed_consumption() {
    const std::vector<std::string> words{"a", "b", "c"};
    ReadingSession session; settings::ReadingSettings settings; settings.wpm = 300;
    ReadingLoop::setWords(session, words, 0);
    ReadingLoop::seekTo(session, 2);
    TEST_ASSERT_FALSE(session.readingStarted); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::update(session, settings, 10000);
    TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::start(session, 10000);
    TEST_ASSERT_TRUE(session.readingStarted);
    ReadingLoop::update(session, settings, 10199); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::update(session, settings, 10200); TEST_ASSERT_TRUE(session.naturalEnd); TEST_ASSERT_FALSE(session.playing);
    session.naturalEnd = false;
    ReadingLoop::update(session, settings, 20000); TEST_ASSERT_FALSE(session.naturalEnd);
}
void one_word_empty_and_seek_clock() {
    const std::vector<std::string> words{"a"};
    ReadingSession session; settings::ReadingSettings settings; settings.wpm = 300;
    ReadingLoop::setWords(session, words, 0); ReadingLoop::start(session, 0);
    ReadingLoop::update(session, settings, 199); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::update(session, settings, 200); TEST_ASSERT_TRUE(session.naturalEnd);
    ReadingLoop::setWords(session, {}, 0); ReadingLoop::start(session, 0);
    ReadingLoop::update(session, settings, 10000); TEST_ASSERT_FALSE(session.naturalEnd); TEST_ASSERT_FALSE(session.playing);
    ReadingLoop::setWords(session, words, 0); ReadingLoop::start(session, 0);
    ReadingLoop::seekTo(session, 0); ReadingLoop::update(session, settings, 10000); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::update(session, settings, 10199); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::update(session, settings, 10200); TEST_ASSERT_TRUE(session.naturalEnd);
}
auto diskSnapshot(const fs::FS& fs) {
    std::map<std::string, std::string> result;
    for (const auto& [name, node] : fs.disk->nodes) result[name] = node->bytes;
    return result;
}
ReadingOrganization::Target restartFixture(fs::FS& fs) {
    auto t = target(fs);
    TEST_ASSERT_TRUE(command(fs, t, Command::started, "initial").has_value());
    reading::StoredState state{indexIdentity, {.wordIndex=2}};
    state.reading.overrides.languageFonts = {{"en", "test-font"}};
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, state).has_value());
    fs.reboot();
    return t;
}
void source_replacement_and_stale_generation_block_before_write() {
    fs::FS fs; auto t = restartFixture(fs);
    auto stale = t; ++stale.generation;
    auto before = diskSnapshot(fs);
    auto r = ReadingOrganization::apply(fs, {stale, "stale", Command::startAgain}, t);
    TEST_ASSERT_FALSE(r.has_value()); TEST_ASSERT_TRUE(r.error() == make_error_code(Error::stale_identity));
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    fs.put(path, "xyz"); before = diskSnapshot(fs);
    r = command(fs, t, Command::startAgain, "replacement"); TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == make_error_code(Error::stale_identity)); TEST_ASSERT_TRUE(before == diskSnapshot(fs));
}
void memory_event_boundary_and_manual_correction_require_fresh_start() {
    fs::FS fs; auto t = target(fs);
    const std::vector<std::string> words{"a", "b", "c"};
    ReadingSession session; ReadingLoop::setWords(session, words, 0);
    t.generation = session.generation; session.organization = t;
    auto before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
    ReadingLoop::start(session, 0);
    auto started = ReadingOrganization::takeActivity(session); TEST_ASSERT_TRUE(started.has_value());
    TEST_ASSERT_TRUE(started->command == Command::started);
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
    auto correction = ReadingOrganization::manualAction(session, Command::markUnread, t);
    TEST_ASSERT_TRUE(correction.has_value()); TEST_ASSERT_FALSE(session.playing);
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs)); // Touch/frame boundary has no filesystem work.
    started->operation = "old-start";
    auto rejected = ReadingOrganization::apply(fs, *started, t, session.activity);
    TEST_ASSERT_FALSE(rejected.has_value()); TEST_ASSERT_TRUE(rejected.error() == make_error_code(Error::stale_identity));
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    ReadingLoop::start(session, 100);
    auto fresh = ReadingOrganization::takeActivity(session); TEST_ASSERT_TRUE(fresh.has_value());
    TEST_ASSERT_TRUE(fresh->activity > correction->activity);
}
void restart_publication_is_paused_and_rejects_old_callback() {
    fs::FS fs; auto t = target(fs);
    const std::vector<std::string> words{"a", "b", "c"};
    ReadingSession session; ReadingLoop::setWords(session, words, 0);
    t.generation = session.generation; session.organization = t;
    ReadingLoop::seekTo(session, 2); ReadingLoop::start(session, 0);
    auto request = ReadingOrganization::manualAction(session, Command::startAgain, t); TEST_ASSERT_TRUE(request.has_value());
    TEST_ASSERT_FALSE(session.playing); TEST_ASSERT_EQUAL_UINT32(2, session.state.wordIndex);
    reading::State reset; reset.wordIndex = 0;
    TEST_ASSERT_TRUE(ReadingOrganization::publishRestart(session, *request, reset));
    TEST_ASSERT_FALSE(session.playing); TEST_ASSERT_EQUAL_UINT32(0, session.state.wordIndex);
    TEST_ASSERT_FALSE(session.readingStarted); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::setWords(session, words, 0);
    TEST_ASSERT_FALSE(ReadingOrganization::publishRestart(session, *request, reset));
}
void page_activity_is_started_not_completed_and_demo_has_no_command() {
    const std::vector<std::string> words{"a"};
    ReadingSession session; ReadingLoop::setWords(session, words, 0);
    ReadingLoop::pageActivity(session);
    TEST_ASSERT_TRUE(session.readingStarted); TEST_ASSERT_FALSE(session.naturalEnd); TEST_ASSERT_FALSE(session.playing);
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
    ReadingLoop::begin(session, 0); ReadingLoop::start(session, 0);
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
}
void failed_reset_before_intent_preserves_position_and_typography() {
    fs::FS fs; auto t = restartFixture(fs);
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    const auto original = fs.bytes(bookmark);
    fs.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == StoragePaths::libraryOrganizationPathFor(path) + ".tmp"; };
    TEST_ASSERT_FALSE(command(fs, t, Command::startAgain, "failed-reset").has_value());
    TEST_ASSERT_EQUAL_STRING(original.c_str(), fs.bytes(bookmark).c_str());
    fs.reboot(); TEST_ASSERT_TRUE(ReadingOrganization::prepare(fs, path, indexIdentity, 2).has_value());
    TEST_ASSERT_EQUAL_STRING(original.c_str(), fs.bytes(bookmark).c_str());
}
void durable_intent_blocks_writers_and_replays_once() {
    fs::FS fs; auto t = restartFixture(fs);
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    fs.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == bookmark + ".tmp"; };
    TEST_ASSERT_FALSE(command(fs, t, Command::startAgain, "recover-reset").has_value());
    fs.reboot();
    auto intent = loadItem(fs, path, t.source); TEST_ASSERT_TRUE(intent.has_value()); TEST_ASSERT_TRUE(intent->restart.has_value());
    const auto order = intent->restart->order;
    auto guard = ReadingOrganization::allowBookmarkWrite(fs, path); TEST_ASSERT_FALSE(guard.has_value());
    TEST_ASSERT_TRUE(guard.error() == make_error_code(Error::recovery_required));
    TEST_ASSERT_TRUE(ReadingOrganization::prepare(fs, path, indexIdentity, 2).has_value());
    auto state = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, state->reading.wordIndex); TEST_ASSERT_EQUAL_STRING("test-font", state->reading.overrides.languageFonts[0].fontId.c_str());
    auto done = loadItem(fs, path, t.source); TEST_ASSERT_TRUE(done.has_value());
    TEST_ASSERT_FALSE(done->restart.has_value()); TEST_ASSERT_EQUAL_UINT64(order, done->last_read_order);
    auto before = diskSnapshot(fs);
    TEST_ASSERT_TRUE(ReadingOrganization::prepare(fs, path, indexIdentity, 3).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
}
void pending_restart_rejects_replaced_source() {
    fs::FS fs; auto t = restartFixture(fs);
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    fs.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == bookmark + ".tmp"; };
    TEST_ASSERT_FALSE(command(fs, t, Command::startAgain, "recover-reset").has_value());
    fs.reboot(); fs.put(path, "xyz"); const auto before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(ReadingOrganization::prepare(fs, path, indexIdentity, 2).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
}
void reset_without_bookmark_and_failed_open() {
    fs::FS fs; auto t = target(fs);
    TEST_ASSERT_TRUE(command(fs, t, Command::startAgain, "new-reset").has_value());
    auto state = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, state->reading.wordIndex);
    const auto before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(ReadingOrganization::prepare(fs, path, {}, 1).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    fs.disk->nodes.erase(path);
    TEST_ASSERT_FALSE(ReadingOrganization::prepare(fs, path, indexIdentity, 2).has_value());
    TEST_ASSERT_EQUAL_UINT32(0, ReadingStateStore::read(fs, path, indexIdentity)->reading.wordIndex);
}
void malformed_or_newer_bookmark_is_not_reset() {
    fs::FS fs; auto t = target(fs);
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    fs.put(bookmark, "bad bookmark"); auto before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(command(fs, t, Command::startAgain, "bad-reset").has_value()); TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    reading::StoredState state{indexIdentity, {.wordIndex=2}};
    fs.put(bookmark, "future_key = 1\n" + *ReadingStateStore::encode(state)); before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(command(fs, t, Command::startAgain, "future-reset").has_value()); TEST_ASSERT_TRUE(before == diskSnapshot(fs));
}
void order_is_durable_and_reconstructed_from_saved_items() {
    fs::FS fs; auto t = target(fs);
    auto r = command(fs, t, Command::started, "first"); TEST_ASSERT_TRUE(r.has_value());
    r->last_read_order = 500; TEST_ASSERT_TRUE(saveItem(fs, *r, r->revision).has_value());
    const std::string_view paths[]{path};
    TEST_ASSERT_TRUE(ReadingOrganization::reconcileOrder(fs, paths).has_value());
    auto feeds = loadFeeds(fs); TEST_ASSERT_TRUE(feeds.has_value()); TEST_ASSERT_EQUAL_UINT64(501, feeds->next_order);
    r = command(fs, t, Command::markRead, "second"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_EQUAL_UINT64(501, r->completed_order);
    fs.reboot();
    r = command(fs, t, Command::started, "third"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->last_read_order > 501);
}
void power_loss_at_every_restart_mutation() {
    size_t boundaries = 0;
    for (size_t crash = 0; crash <= boundaries; ++crash) {
        fs::FS fs; const auto t = restartFixture(fs);
        fs.disk->crashAfter = crash;
        bool interrupted = false;
        try { TEST_ASSERT_TRUE(command(fs, t, Command::startAgain, "power-reset").has_value()); }
        catch (const fs::PowerLoss&) { interrupted = true; }
        if (crash == 0) boundaries = fs.disk->mutations.size();
        else TEST_ASSERT_TRUE(interrupted);
        fs.reboot();
        auto recovered = ReadingOrganization::prepare(fs, path, indexIdentity, 2);
        TEST_ASSERT_TRUE_MESSAGE(recovered.has_value(), ("crash boundary " + std::to_string(crash)).c_str());
        auto state = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(state.has_value());
        TEST_ASSERT_TRUE(state->reading.wordIndex == 0 || state->reading.wordIndex == 2);
        TEST_ASSERT_EQUAL_STRING("test-font", state->reading.overrides.languageFonts[0].fontId.c_str());
        auto record = loadItem(fs, path, t.source); TEST_ASSERT_TRUE(record.has_value()); TEST_ASSERT_FALSE(record->restart.has_value());
        TEST_ASSERT_EQUAL_UINT32(record->last_operation == "power-reset" ? 0 : 2, state->reading.wordIndex);
        const auto before = diskSnapshot(fs);
        TEST_ASSERT_TRUE(ReadingOrganization::prepare(fs, path, indexIdentity, 3).has_value());
        TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    }
    TEST_ASSERT_GREATER_THAN(10, boundaries);
}
void every_restart_io_failure_is_recoverable() {
    size_t operations = 0;
    for (size_t failAt = 0; failAt <= operations; ++failAt) {
        fs::FS fs; const auto t = restartFixture(fs); size_t calls = 0;
        fs.disk->fail = [&](const auto&, const auto&) { return ++calls == failAt; };
        auto result = command(fs, t, Command::startAgain, "io-reset");
        if (failAt == 0) { operations = calls; TEST_ASSERT_TRUE(result.has_value()); }
        fs.reboot();
        auto recovered = ReadingOrganization::prepare(fs, path, indexIdentity, 2);
        TEST_ASSERT_TRUE_MESSAGE(recovered.has_value(), ("I/O boundary " + std::to_string(failAt)).c_str());
        auto state = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(state.has_value());
        TEST_ASSERT_TRUE(state->reading.wordIndex == 0 || state->reading.wordIndex == 2);
        TEST_ASSERT_EQUAL_STRING("test-font", state->reading.overrides.languageFonts[0].fontId.c_str());
        if (result) TEST_ASSERT_EQUAL_UINT32(0, state->reading.wordIndex);
        auto record = loadItem(fs, path, t.source); TEST_ASSERT_TRUE(record.has_value()); TEST_ASSERT_FALSE(record->restart.has_value());
        TEST_ASSERT_EQUAL_UINT32(record->last_operation == "io-reset" ? 0 : 2, state->reading.wordIndex);
    }
    TEST_ASSERT_GREATER_THAN(15, operations);
}
void reindex_same_content_preserves_status_and_pending_reset_fonts() {
    fs::FS fs; auto t = restartFixture(fs);
    auto r = command(fs, t, Command::markRead, "completed"); TEST_ASSERT_TRUE(r.has_value());
    auto newIndex = indexIdentity; newIndex.wordCount = 4;
    auto reindexed = ReadingOrganization::prepare(fs, path, newIndex, 2); TEST_ASSERT_TRUE(reindexed.has_value());
    r = command(fs, *reindexed, Command::started, "reindexed"); TEST_ASSERT_TRUE(r.has_value()); TEST_ASSERT_TRUE(r->status == Status::read);
    // A pending reset carries the old index and typography across reindexing.
    fs::FS pending; const auto old = restartFixture(pending);
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    pending.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == bookmark + ".tmp"; };
    TEST_ASSERT_FALSE(command(pending, old, Command::startAgain, "pending-reindex").has_value());
    pending.reboot();
    TEST_ASSERT_TRUE(ReadingOrganization::prepare(pending, path, newIndex, 2).has_value());
    auto state = ReadingStateStore::read(pending, path, newIndex); TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, state->reading.wordIndex);
    TEST_ASSERT_EQUAL_STRING("test-font", state->reading.overrides.languageFonts[0].fontId.c_str());
}
void restart_blocked_session_cannot_start_or_emit_activity() {
    const std::vector<std::string> words{"a"}; ReadingSession session;
    ReadingLoop::setWords(session, words, 0); session.restartBlocked = true;
    ReadingLoop::start(session, 0); ReadingLoop::pageActivity(session);
    TEST_ASSERT_FALSE(session.playing); TEST_ASSERT_FALSE(session.readingStarted); TEST_ASSERT_FALSE(session.naturalEnd);
}

void legacy_writers_and_mirror_cannot_overwrite_pending_restart() {
    fs::FS fs; const auto t = restartFixture(fs);
    Board::Storage::filesystem() = fs;
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    fs.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == bookmark + ".tmp"; };
    TEST_ASSERT_FALSE(command(fs, t, Command::startAgain, "writer-guard").has_value()); fs.reboot();
    const auto before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(ReadingProgress::writeBookStatePosition(path, indexIdentity, 1).has_value());
    TEST_ASSERT_FALSE(ReadingProgress::writeBookLanguageFonts(path, indexIdentity, {{"en", "different"}}).has_value());
    IndexedBookStore store; store.opened = true; store.path = path; store.bookIdentity = indexIdentity; store.words = {"a", "b", "c"};
    ReadingSession session; ReadingLoop::setBookStore(session, store, 0); session.state.wordIndex = 2;
    TEST_ASSERT_FALSE(ReadingProgress::mirrorChecked(session, store).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    TEST_ASSERT_TRUE(ReadingOrganization::prepare(fs, path, indexIdentity, 2).has_value());
    // A failed post-commit publication must also fence a stale in-memory session,
    // even though the durable intent has already been cleared.
    session.restartBlocked = true;
    const auto committed = diskSnapshot(fs);
    TEST_ASSERT_FALSE(ReadingProgress::mirrorChecked(session, store).has_value());
    TEST_ASSERT_TRUE(committed == diskSnapshot(fs));
}
void companion_position_and_font_writes_preserve_status() {
    fs::FS fs; const auto t = restartFixture(fs); Board::Storage::filesystem() = fs;
    const auto before = fs.bytes(StoragePaths::libraryOrganizationPathFor(path));
    TEST_ASSERT_TRUE(ReadingProgress::writeBookStatePosition(path, indexIdentity, 2).has_value());
    TEST_ASSERT_TRUE(ReadingProgress::writeBookLanguageFonts(path, indexIdentity, {{"en", "changed"}}).has_value());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), fs.bytes(StoragePaths::libraryOrganizationPathFor(path)).c_str());
    const auto state = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT32(2, state->reading.wordIndex); TEST_ASSERT_EQUAL_STRING("changed", state->reading.overrides.languageFonts[0].fontId.c_str());
}
void chapter_rewind_cancels_catchup_completion() {
    fs::FS fs; const auto t = restartFixture(fs); Board::Storage::filesystem() = fs;
    IndexedBookStore store; store.opened = true; store.path = path; store.bookIdentity = indexIdentity; store.words = {"a", "b", "c"};
    ReadingSession session; ReadingLoop::setBookStore(session, store, 0);
    session.metadata.chapters = {{"Chapter two", 1}};
    Preferences preferences; settings::ReadingSettings settings; settings.wpm = 300;
    ReadingLoop::start(session, 0); ReadingLoop::update(session, settings, 600);
    TEST_ASSERT_TRUE(session.naturalEnd);
    TEST_ASSERT_TRUE(ReadingProgress::saveChapterTransition(session, preferences, store, 0, 2, 600));
    TEST_ASSERT_FALSE(session.naturalEnd); TEST_ASSERT_TRUE(session.playing); TEST_ASSERT_EQUAL_UINT32(1, session.state.wordIndex);
    ReadingLoop::update(session, settings, 800); TEST_ASSERT_FALSE(session.naturalEnd);
    ReadingLoop::update(session, settings, 1000); TEST_ASSERT_TRUE(session.naturalEnd);
}
void bookmark_atomic_write_interruption_restores_a_valid_position() {
    size_t boundaries = 0;
    for (size_t crash = 0; crash <= boundaries; ++crash) {
        fs::FS fs; target(fs); reading::StoredState old{indexIdentity, {.wordIndex=2}};
        TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, old).has_value());
        fs.reboot(); fs.disk->crashAfter = crash;
        auto next = old; next.reading.wordIndex = 1;
        try { TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, next).has_value()); } catch (const fs::PowerLoss&) {}
        if (crash == 0) boundaries = fs.disk->mutations.size();
        fs.reboot(); auto restored = ReadingStateStore::read(fs, path, indexIdentity);
        TEST_ASSERT_TRUE(restored.has_value());
        TEST_ASSERT_TRUE(restored->reading.wordIndex == 1 || restored->reading.wordIndex == 2);
    }
}

void failed_completion_retries_once_on_explicit_activity() {
    fs::FS fs; auto t = restartFixture(fs);
    const std::vector<std::string> words{"a", "b", "c"};
    ReadingSession session; ReadingLoop::setWords(session, words, 0);
    t.generation = session.generation; session.organization = t;
    ReadingOrganization::Request failed{t, "failed-end", Command::naturalEnd, session.activity};
    fs.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == StoragePaths::libraryOrganizationPathFor(path) + ".tmp"; };
    auto result = ReadingOrganization::apply(fs, failed, t, session.activity); TEST_ASSERT_FALSE(result.has_value());
    session.organizationError = result.error(); session.failedOrganization = failed;
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
    fs.reboot(); ReadingLoop::start(session, 100);
    auto retry = ReadingOrganization::takeActivity(session); TEST_ASSERT_TRUE(retry.has_value());
    TEST_ASSERT_TRUE(retry->command == Command::naturalEnd); TEST_ASSERT_EQUAL_STRING("failed-end", retry->operation.c_str());
    result = ReadingOrganization::apply(fs, *retry, t, session.activity); TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->status == Status::read);
    TEST_ASSERT_FALSE(ReadingOrganization::takeActivity(session).has_value());
    session.failedOrganization = failed;
    TEST_ASSERT_TRUE(ReadingOrganization::manualAction(session, Command::markUnread, t).has_value());
    TEST_ASSERT_FALSE(session.failedOrganization.has_value());
    session.failedOrganization = failed; ReadingLoop::setWords(session, words, 0);
    TEST_ASSERT_FALSE(session.failedOrganization.has_value());
}
void first_record_interruption_never_resets_without_committed_intent() {
    size_t boundaries = 0;
    for (size_t crash = 0; crash <= boundaries; ++crash) {
        fs::FS fs; const auto t = target(fs);
        TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, {indexIdentity, {.wordIndex=2}}).has_value());
        fs.reboot(); fs.disk->crashAfter = crash;
        try { TEST_ASSERT_TRUE(command(fs, t, Command::startAgain, "first-reset").has_value()); } catch (const fs::PowerLoss&) {}
        if (crash == 0) boundaries = fs.disk->mutations.size();
        fs.reboot();
        auto prepared = ReadingOrganization::prepare(fs, path, indexIdentity, 2);
        auto state = ReadingStateStore::read(fs, path, indexIdentity); TEST_ASSERT_TRUE(state.has_value());
        auto record = loadItem(fs, path, t.source);
        if (!prepared) {
            TEST_ASSERT_TRUE(prepared.error() == make_error_code(Error::corrupt) || prepared.error() == make_error_code(Error::recovery_required));
            TEST_ASSERT_EQUAL_UINT32(2, state->reading.wordIndex);
            TEST_ASSERT_FALSE(ReadingOrganization::allowBookmarkWrite(fs, path).has_value());
        } else if (record && record->last_operation == "first-reset") {
            TEST_ASSERT_EQUAL_UINT32(0, state->reading.wordIndex); TEST_ASSERT_FALSE(record->restart.has_value());
        } else TEST_ASSERT_EQUAL_UINT32(2, state->reading.wordIndex);
    }
    TEST_ASSERT_GREATER_THAN(10, boundaries);
}

void cancelled_identity_check_cannot_change_status_or_bookmark() {
    fs::FS fs; const auto t = restartFixture(fs); const auto before = diskSnapshot(fs);
    const auto cancel = [](uint64_t, uint64_t) { return false; };
    TEST_ASSERT_FALSE(ReadingOrganization::prepare(fs, path, indexIdentity, 2, cancel).has_value());
    TEST_ASSERT_FALSE(ReadingOrganization::apply(fs, {t, "cancel", Command::startAgain}, t, 0, cancel).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
}
void interrupted_registry_creation_stays_explicitly_unavailable() {
    size_t boundaries = 0;
    for (size_t crash = 0; crash <= boundaries; ++crash) {
        fs::FS fs; fs.disk->crashAfter = crash;
        try { TEST_ASSERT_TRUE(ReadingOrganization::reconcileOrder(fs, {}).has_value()); } catch (const fs::PowerLoss&) {}
        if (crash == 0) boundaries = fs.disk->mutations.size();
        fs.reboot(); const auto before = diskSnapshot(fs);
        auto retry = ReadingOrganization::reconcileOrder(fs, {});
        if (!retry) {
            TEST_ASSERT_TRUE(retry.error() == make_error_code(Error::corrupt) || retry.error() == make_error_code(Error::recovery_required));
            TEST_ASSERT_TRUE(before == diskSnapshot(fs)); // No invented permission to promote an uncommitted stage.
        } else TEST_ASSERT_TRUE(loadFeeds(fs).has_value());
    }
}

void restart_checkpoint_validates_before_overwriting_the_old_bookmark() {
    fs::FS fs; restartFixture(fs); Board::Storage::filesystem() = fs;
    IndexedBookStore store; store.opened = true; store.path = path; store.bookIdentity = indexIdentity; store.words = {"a", "b", "c"};
    ReadingSession session; ReadingLoop::setBookStore(session, store, 0); session.state.wordIndex = 1;
    const auto bookmark = StoragePaths::bookStatePathFor(path);
    fs.put(bookmark, "future_key = 1\n" + fs.bytes(bookmark));
    const auto before = diskSnapshot(fs);
    TEST_ASSERT_FALSE(ReadingProgress::checkpointRestart(session, store).has_value());
    TEST_ASSERT_TRUE(before == diskSnapshot(fs));
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs, path, {indexIdentity, {.wordIndex=2}}).has_value());
    fs.disk->fail = [&](const auto& op, const auto& name) { return op == "open-write" && name == bookmark + ".tmp"; };
    TEST_ASSERT_FALSE(ReadingProgress::checkpointRestart(session, store).has_value()); fs.reboot();
    TEST_ASSERT_EQUAL_UINT32(2, ReadingStateStore::read(fs, path, indexIdentity)->reading.wordIndex);
    TEST_ASSERT_TRUE(ReadingProgress::checkpointRestart(session, store).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, ReadingStateStore::read(fs, path, indexIdentity)->reading.wordIndex);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(normal_open_does_not_infer_status);
    RUN_TEST(transitions_and_bookmark_preservation);
    RUN_TEST(restart_preserves_typography_and_is_idempotent);
    RUN_TEST(completion_requires_timed_consumption);
    RUN_TEST(one_word_empty_and_seek_clock);
    RUN_TEST(source_replacement_and_stale_generation_block_before_write);
    RUN_TEST(memory_event_boundary_and_manual_correction_require_fresh_start);
    RUN_TEST(restart_publication_is_paused_and_rejects_old_callback);
    RUN_TEST(page_activity_is_started_not_completed_and_demo_has_no_command);
    RUN_TEST(failed_reset_before_intent_preserves_position_and_typography);
    RUN_TEST(durable_intent_blocks_writers_and_replays_once);
    RUN_TEST(pending_restart_rejects_replaced_source);
    RUN_TEST(reset_without_bookmark_and_failed_open);
    RUN_TEST(malformed_or_newer_bookmark_is_not_reset);
    RUN_TEST(order_is_durable_and_reconstructed_from_saved_items);
    RUN_TEST(power_loss_at_every_restart_mutation);
    RUN_TEST(every_restart_io_failure_is_recoverable);
    RUN_TEST(reindex_same_content_preserves_status_and_pending_reset_fonts);
    RUN_TEST(restart_blocked_session_cannot_start_or_emit_activity);
    RUN_TEST(legacy_writers_and_mirror_cannot_overwrite_pending_restart);
    RUN_TEST(companion_position_and_font_writes_preserve_status);
    RUN_TEST(chapter_rewind_cancels_catchup_completion);
    RUN_TEST(bookmark_atomic_write_interruption_restores_a_valid_position);
    RUN_TEST(failed_completion_retries_once_on_explicit_activity);
    RUN_TEST(first_record_interruption_never_resets_without_committed_intent);
    RUN_TEST(cancelled_identity_check_cannot_change_status_or_bookmark);
    RUN_TEST(interrupted_registry_creation_stays_explicitly_unavailable);
    RUN_TEST(restart_checkpoint_validates_before_overwriting_the_old_bookmark);
    return UNITY_END();
}
