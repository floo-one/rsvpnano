#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs { class FS; }

// Separate from .rstate.toml: older firmware can keep writing bookmarks without
// erasing organization. Call only while the application owns SD access; not from
// paint/touch callbacks. The caller serializes operations and verifies source bytes.
namespace LibraryOrganization {
    constexpr uint32_t kSchemaVersion = 1;
    constexpr size_t kMaxRecordBytes = 64 * 1024;
    constexpr size_t kMaxPathBytes = 4096;
    constexpr size_t kMaxLabelBytes = 256;
    constexpr size_t kMaxFeeds = 256;
    constexpr size_t kMaxActiveFeeds = 24;
    constexpr size_t kMaxMemberships = 256;
    constexpr size_t kMaxViews = kMaxFeeds + 3;

    enum class Error {
        corrupt = 1,
        unsupported_version,
        unsupported_fields,
        stale_identity,
        revision_conflict,
        recovery_required,
    };
    std::error_code make_error_code(Error error);

    enum class Status { invalid, unknown, unread, reading, read };
    enum class Tab { invalid, unread, reading, read };

    struct SourceIdentity {
        uint64_t bytes = std::numeric_limits<uint64_t>::max();
        std::string sha256;
        bool operator==(const SourceIdentity&) const = default;
    };
    // Run in an SD-owning job with exclusive content access. Reads at most 512
    // bytes per step; callback may yield/cancel, but must not mutate the source.
    // No partial digest is returned on cancellation, short read or size change.
    std::expected<SourceIdentity, std::error_code> hashSource(
        fs::FS&, std::string_view path,
        const std::function<bool(uint64_t processed, uint64_t total)>& proceed = {});
    struct IndexIdentity {
        uint32_t source_size = 0;
        uint32_t source_fingerprint = 0;
        uint32_t word_count = 0;
        bool operator==(const IndexIdentity&) const = default;
    };
    struct Membership {
        std::string feed_id;
        uint64_t batch = 0;
        uint32_t position = 0;
        bool operator==(const Membership&) const = default;
    };
    // Data only. T02 will execute this intent after a successful paused open.
    // Preserve the exact prior bookmark (including typography), not just its index.
    struct RestartIntent {
        std::string operation_id;
        bool had_bookmark = false;
        std::string previous_bookmark;
        uint32_t desired_word_index = 0;
        bool operator==(const RestartIntent&) const = default;
    };
    struct Item {
        uint32_t schema_version = kSchemaVersion;
        uint64_t revision = 0;
        std::string path;
        SourceIdentity source;
        std::optional<IndexIdentity> indexed;
        Status status = Status::invalid;
        std::string unknown_reason;
        std::string source_url;
        std::string body_sha256;
        uint64_t last_read_order = 0;
        uint64_t completed_order = 0;
        std::vector<Membership> memberships;
        std::optional<RestartIntent> restart;
        bool operator==(const Item&) const = default;
    };
    struct Feed {
        std::string id;
        std::string url;
        std::string title;
        bool active = false;
        uint64_t subscription_order = 0;
        bool operator==(const Feed&) const = default;
    };
    struct Feeds {
        uint32_t schema_version = kSchemaVersion;
        uint64_t revision = 0;
        uint64_t next_order = 1;
        std::string last_attempted_feed;
        std::vector<Feed> feeds;
        bool operator==(const Feeds&) const = default;
    };
    struct Anchor {
        Tab tab = Tab::invalid;
        std::string path;
        int32_t offset_px = 0;
        bool operator==(const Anchor&) const = default;
    };
    struct View {
        std::string collection;
        Tab selected = Tab::invalid;
        std::vector<Anchor> anchors;
        bool operator==(const View&) const = default;
    };
    struct Views {
        uint32_t schema_version = kSchemaVersion;
        uint64_t revision = 0;
        std::vector<View> views;
        bool operator==(const Views&) const = default;
    };

    // Strict, bounded TOML with a checksum comment covering the exact TOML bytes.
    // Unknown fields/versions are read-only errors, never silently dropped.
    std::expected<std::string, std::error_code> encode(const Item& record);
    std::expected<std::string, std::error_code> encode(const Feeds& record);
    std::expected<std::string, std::error_code> encode(const Views& record);
    std::expected<Item, std::error_code> decodeItem(std::string_view input);
    std::expected<Feeds, std::error_code> decodeFeeds(std::string_view input);
    std::expected<Views, std::error_code> decodeViews(std::string_view input);

    enum class Recovery {
        committedOnly,
        // Only for a caller with a durable import/create intent. Never promotes
        // an interrupted update over a committed final/backup; revision must be 1.
        // A corrupt stage with no final/backup is removed to allow that intent's
        // retry; unsupported/stale/unreadable stages remain blocked.
        stagedCreation,
    };

    // Loads repair a missing/corrupt final from a validated backup. Unsupported,
    // stale or unreadable candidates block mutation. They are not corruption.
    std::expected<Item, std::error_code> loadItem(fs::FS&, std::string_view path,
                                                const SourceIdentity&, Recovery = Recovery::committedOnly);
    std::expected<Feeds, std::error_code> loadFeeds(fs::FS&, Recovery = Recovery::committedOnly);
    std::expected<Views, std::error_code> loadViews(fs::FS&, Recovery = Recovery::committedOnly);

    // Revision 0 means create; otherwise compare against the current revision.
    // Assigns expectedRevision+1. Never touches content or .rstate.toml.
    std::expected<Item, std::error_code> saveItem(fs::FS&, Item record, uint64_t expectedRevision);
    std::expected<Feeds, std::error_code> saveFeeds(fs::FS&, Feeds record, uint64_t expectedRevision);
    std::expected<Views, std::error_code> saveViews(fs::FS&, Views record, uint64_t expectedRevision);
}
