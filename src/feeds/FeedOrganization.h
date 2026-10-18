#pragma once
#include "feeds/FeedParser.h"
#include "library/LibraryOrganization.h"
#include <span>
namespace FeedOrganization {
    constexpr size_t kAttemptBudget = 8;
    constexpr size_t kSaveBudget = 12;
    constexpr size_t kScanBudget = 128;
    constexpr size_t kCatalogBytes = 1024 * 1024;
    using Result = std::expected<void, std::error_code>;
    using Registry = LibraryOrganization::Feeds;
    using Feed = LibraryOrganization::Feed;
    struct Candidate {
        std::string path, url, body, scope;
        LibraryOrganization::SourceIdentity source;
    };
    using Catalog = std::vector<Candidate>;
    enum class Outcome { saved, associated, unchanged, skipped, deferred };
    struct Ingested { Outcome outcome; std::string path; };
    // SD-owning job only; callers create /config and /library/articles.
    std::expected<Registry, std::error_code> reconcile(fs::FS&, std::span<const std::string> urls);
    std::vector<std::string> attempts(const Registry&);
    Result attempted(fs::FS&, std::string_view id);
    std::expected<Feed, std::error_code> beginScan(fs::FS&, std::string_view id, std::string_view body);
    Result checkpointScan(fs::FS&, const Feed&);
    std::expected<Catalog, std::error_code> catalog(fs::FS&, std::span<const std::string> paths);
    // A completed receipt is retained; recovery never re-downloads deleted content.
    std::expected<std::optional<std::string>, std::error_code> recover(fs::FS&);
    std::expected<Ingested, std::error_code> ingest(fs::FS&, Catalog&, const Feed&, const feedparser::FeedItem&,
        uint64_t batch, uint32_t position, bool permitSave, bool seen, bool legacySeen);
    struct ScanResult {
        size_t scanned = 0, saved = 0, associated = 0, skipped = 0, deferred = 0, failed = 0;
        bool partial = false;
    };
    using Seen = std::function<bool(const feedparser::FeedItem&, std::string_view identity, bool legacy)>;
    using MarkSeen = std::function<void(std::string_view identity)>;
    std::expected<ScanResult, std::error_code> scan(fs::FS&, Catalog&, Feed, std::string_view body,
        size_t saveBudget, const Seen&, const MarkSeen&);
    std::string articleUrl(std::string_view feedUrl, std::string_view link);
    std::string seenIdentity(const Feed&, const feedparser::FeedItem&);
    std::string legacyFilename(const feedparser::FeedItem&);
}
