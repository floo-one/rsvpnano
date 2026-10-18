#include "library/ReadingOrganization.h"
#include "library/ReadingStateStore.h"
#include "reader/ReadingLoop.h"
#include <algorithm>
#include <limits>
using namespace LibraryOrganization;
namespace ReadingOrganization {
std::optional<Request> takeActivity(ReadingSession& session) {
    if (!session.organization || session.restartBlocked
        || session.organization->generation != session.generation) {
        session.readingStarted = session.naturalEnd = false;
        return std::nullopt;
    }
    if (!session.readingStarted && !session.naturalEnd) return std::nullopt;
    // Retry a failed status save on explicit new activity, with the same operation
    // ID. Never retry in a frame loop or turn a failed completion into Started.
    if (session.failedOrganization && session.failedOrganization->target == *session.organization) {
        auto request = std::move(*session.failedOrganization);
        session.failedOrganization.reset();
        request.activity = session.activity;
        session.organizationError.clear();
        session.organizationErrorReported = false;
        session.readingStarted = session.naturalEnd = false;
        return request;
    }
    if (session.organizationError) {
        session.readingStarted = session.naturalEnd = false;
        return std::nullopt;
    }
    Request request{*session.organization, {}, session.naturalEnd ? Command::naturalEnd : Command::started, session.activity};
    session.readingStarted = session.naturalEnd = false;
    return request;
}
std::expected<Request, std::error_code> manualAction(ReadingSession& session, Command command, const Target& selected) {
    if (command != Command::markRead && command != Command::markUnread && command != Command::startAgain)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!session.organization || *session.organization != selected || selected.generation != session.generation)
        return std::unexpected(make_error_code(Error::stale_identity));
    if (session.restartBlocked) return std::unexpected(make_error_code(Error::recovery_required));
    ReadingLoop::pause(session);
    ReadingLoop::clearActivity(session);
    session.failedOrganization.reset(); // An explicit correction supersedes a failed status event.
    return Request{selected, {}, command, session.activity};
}
bool publishRestart(ReadingSession& session, const Request& request, reading::State state) {
    if (request.command != Command::startAgain || !session.organization || *session.organization != request.target
        || session.generation != request.target.generation || session.activity != request.activity || state.wordIndex != 0) return false;
    session.state = std::move(state);
    session.restartBlocked = false;
    ReadingLoop::pause(session);
    ReadingLoop::seekTo(session, 0);
    ReadingLoop::clearActivity(session);
    return true;
}
namespace {
    IndexIdentity indexed(const reading::BookIdentity& i) { return {i.sourceSize, i.sourceFingerprint, i.wordCount}; }
    reading::BookIdentity indexed(const IndexIdentity& i) { return {i.source_size, i.source_fingerprint, i.word_count}; }
    bool missing(std::error_code e) { return e == std::errc::no_such_file_or_directory; }
    std::expected<Feeds, std::error_code> registry(fs::FS& fs) {
        auto r = loadFeeds(fs);
        if (!r && missing(r.error())) return Feeds{};
        return r;
    }
    std::expected<uint64_t, std::error_code> reserve(fs::FS& fs, const Item& item) {
        auto r = registry(fs);
        if (!r) return std::unexpected(r.error());
        const uint64_t maximum = std::max(item.last_read_order, item.completed_order);
        if (maximum == UINT64_MAX || r->next_order == UINT64_MAX)
            return std::unexpected(std::make_error_code(std::errc::value_too_large));
        const uint64_t value = std::max(r->next_order, maximum + 1);
        if (value == UINT64_MAX) return std::unexpected(std::make_error_code(std::errc::value_too_large));
        r->next_order = value + 1;
        auto saved = saveFeeds(fs, *r, r->revision);
        if (!saved) return std::unexpected(saved.error());
        return value;
    }
    std::expected<Item, std::error_code> load(fs::FS& fs, const Target& target) {
        auto r = loadItem(fs, target.path, target.source);
        if (!r && missing(r.error())) {
            Item legacy;
            legacy.path = target.path; legacy.source = target.source; legacy.indexed = indexed(target.indexed);
            legacy.status = Status::unknown; legacy.unknown_reason = "legacy-unclassified";
            return legacy; // Opening alone never writes or labels it Unread.
        }
        return r;
    }
    std::expected<Item, std::error_code> finishRestart(fs::FS& fs, Item item, const Target& target) {
        if (!item.restart) return item;
        reading::StoredState state{target.indexed, {}};
        if (item.restart->had_bookmark) {
            if (!item.indexed) return std::unexpected(make_error_code(Error::corrupt));
            auto previous = ReadingStateStore::decode(item.restart->previous_bookmark, indexed(*item.indexed), true);
            if (!previous) return std::unexpected(previous.error());
            state.reading.overrides = std::move(previous->reading.overrides);
        }
        // The order belongs to the durable intent, not to each recovery attempt.
        if (item.restart->order == 0) {
            auto order = reserve(fs, item);
            if (!order) return std::unexpected(order.error());
            item.restart->order = *order;
            auto saved = saveItem(fs, item, item.revision);
            if (!saved) return saved;
            item = std::move(*saved);
        }
        auto written = ReadingStateStore::write(fs, target.path, state);
        if (!written) return std::unexpected(written.error());
        item.status = Status::reading; item.unknown_reason.clear();
        item.last_read_order = item.restart->order; item.completed_order = 0;
        item.last_operation = item.restart->operation_id;
        item.indexed = indexed(target.indexed);
        item.restart.reset();
        return saveItem(fs, item, item.revision);
    }
}
std::expected<Target, std::error_code> prepare(fs::FS& fs, std::string_view path,
    const reading::BookIdentity& identity, uint64_t generation, const std::function<bool(uint64_t, uint64_t)>& proceed) {
    if (!generation || !identity.sourceSize || !identity.wordCount)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto source = hashSource(fs, path, proceed);
    if (!source) return std::unexpected(source.error());
    Target target{std::string(path), *source, identity, generation};
    auto item = load(fs, target);
    if (!item) return std::unexpected(item.error());
    if (item->restart) {
        auto recovered = finishRestart(fs, std::move(*item), target);
        if (!recovered) return std::unexpected(recovered.error());
    }
    return target;
}
std::expected<Item, std::error_code> apply(fs::FS& fs, const Request& request, const Target& current,
    uint64_t currentActivity, const std::function<bool(uint64_t, uint64_t)>& proceed) {
    switch (request.command) {
    case Command::started: case Command::naturalEnd: case Command::markRead:
    case Command::markUnread: case Command::startAgain: break;
    default: return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (request.target != current || !current.generation || request.activity != currentActivity)
        return std::unexpected(make_error_code(Error::stale_identity));
    // Full source validation is job work, never touch/paint work. It also catches
    // same-name, same-size replacement after companion/USB access.
    auto source = hashSource(fs, current.path, proceed);
    if (!source) return std::unexpected(source.error());
    if (*source != current.source) return std::unexpected(make_error_code(Error::stale_identity));
    auto loaded = load(fs, current);
    if (!loaded) return loaded;
    Item item = std::move(*loaded);
    if (request.operation.empty()) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto validation = item; validation.revision = std::max<uint64_t>(1, item.revision);
    validation.last_operation = request.operation;
    if (!encode(validation)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (item.last_operation == request.operation) return item;
    if (item.restart) return std::unexpected(make_error_code(Error::recovery_required));
    if (request.command == Command::naturalEnd && item.status == Status::read) return item;
    if (request.command == Command::startAgain) {
        auto previous = ReadingStateStore::snapshot(fs, current.path);
        if (!previous) return std::unexpected(previous.error());
        if (*previous) {
            auto state = ReadingStateStore::decode(**previous, current.indexed, true);
            if (!state) return std::unexpected(state.error());
        }
        auto order = reserve(fs, item);
        if (!order) return std::unexpected(order.error());
        item.indexed = indexed(current.indexed);
        item.restart = RestartIntent{request.operation, previous->has_value(), previous->value_or(""), 0, *order};
        auto saved = saveItem(fs, item, item.revision);
        if (!saved) return saved;
        return finishRestart(fs, std::move(*saved), current);
    }
    auto order = reserve(fs, item);
    if (!order) return std::unexpected(order.error());
    switch (request.command) {
    case Command::started:
        if (item.status != Status::read) item.status = Status::reading;
        item.last_read_order = *order;
        break;
    case Command::naturalEnd:
    case Command::markRead:
        item.status = Status::read; item.completed_order = *order;
        break;
    case Command::markUnread:
        item.status = Status::unread; item.completed_order = 0;
        break;
    case Command::startAgain: break;
    }
    item.unknown_reason.clear(); item.indexed = indexed(current.indexed);
    item.last_operation = request.operation;
    return saveItem(fs, item, item.revision);
}
std::expected<void, std::error_code> allowBookmarkWrite(fs::FS& fs, std::string_view path) {
    auto item = inspectItem(fs, path);
    if (!item) {
        if (missing(item.error())) return {};
        return std::unexpected(item.error());
    }
    if (item->restart) return std::unexpected(make_error_code(Error::recovery_required));
    return {};
}
std::expected<void, std::error_code> reconcileOrder(fs::FS& fs, std::span<const std::string_view> paths) {
    auto r = registry(fs);
    if (!r) return std::unexpected(r.error());
    uint64_t maximum = r->next_order - 1;
    for (auto path : paths) {
        auto item = inspectItem(fs, path);
        if (!item) continue; // Unknown/corrupt records are never changed here.
        maximum = std::max({maximum, item->last_read_order, item->completed_order,
                            item->restart ? item->restart->order : 0});
    }
    if (maximum == UINT64_MAX) return std::unexpected(std::make_error_code(std::errc::value_too_large));
    if (r->revision && r->next_order > maximum) return {};
    r->next_order = maximum + 1;
    auto saved = saveFeeds(fs, *r, r->revision);
    if (!saved) return std::unexpected(saved.error());
    return {};
}
}
