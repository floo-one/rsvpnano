#pragma once
#include "library/LibraryOrganization.h"
#include "reader/ReadingState.h"
#include <span>
struct ReadingSession;
namespace ReadingOrganization {
    enum class Command { started, naturalEnd, markRead, markUnread, startAgain };
    struct Target {
        std::string path;
        LibraryOrganization::SourceIdentity source;
        reading::BookIdentity indexed;
        uint64_t generation = 0;
        bool operator==(const Target&) const = default;
    };
    struct Request {
        Target target;
        std::string operation;
        Command command = Command::started;
        uint64_t activity = 0;
    };
    // Memory-only App/reader boundary; safe to call after an input/frame.
    std::optional<Request> takeActivity(ReadingSession&);
    std::expected<Request, std::error_code> manualAction(ReadingSession&, Command, const Target& selected);
    bool publishRestart(ReadingSession&, const Request&, reading::State);
    // Called after successful indexed open, under exclusive SD ownership.
    // Restores a durable restart before any bookmark is loaded/published.
    std::expected<Target, std::error_code> prepare(fs::FS&, std::string_view path,
        const reading::BookIdentity&, uint64_t generation,
        const std::function<bool(uint64_t, uint64_t)>& proceed = {});
    // current is the App-owned session snapshot. Reject stale generation/path/
    // identity before I/O, then revalidate bytes before any persisted command.
    std::expected<LibraryOrganization::Item, std::error_code> apply(fs::FS&, const Request&, const Target& current,
        uint64_t currentActivity = 0, const std::function<bool(uint64_t, uint64_t)>& proceed = {});
    // Every legacy bookmark writer must consult this before writing. A pending
    // restart blocks writes until prepare() rolls it forward; no API shape change.
    std::expected<void, std::error_code> allowBookmarkWrite(fs::FS&, std::string_view path);
    std::expected<void, std::error_code> reconcileOrder(fs::FS&, std::span<const std::string_view> paths);
}
