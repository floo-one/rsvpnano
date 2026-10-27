#pragma once

#include <span>
#include "library/BookLibrary.h"
#include "library/BookMetadata.h"
#include "library/LibraryOrganization.h"
#include "reader/ReadingState.h"

// Owned snapshots: never keep pointers into the mutable inventory. Projection,
// navigation and selection are memory-only. prepare/load/save require SD ownership.
namespace LibraryView {
    using LibraryOrganization::Status;
    using LibraryOrganization::Tab;
    constexpr int32_t kRowHeight = 52;
    constexpr int32_t kViewportHeight = 130;
    constexpr size_t kMemoryBudget = 1024 * 1024;
    constexpr size_t kWorkspaceReserve = 160 * 1024; // bounded codec + sort + UI adapters
    enum class Availability { loading, ready, missing, corrupt, unsupported, stale, ioError, recovery };
    struct Row {
        std::string path, title, chapter;
        std::string publisherHost; // Display-only fallback; never a feed membership.
        LibraryOrganization::SourceIdentity source;
        size_t sourceIndex = 0;
        Status status = Status::unknown;
        Availability availability = Availability::loading;
        uint64_t lastRead = 0, completed = 0;
        std::vector<LibraryOrganization::Membership> memberships;
        uint8_t progress = 0;
        bool article = false, progressKnown = false;
    };
    struct Counts { size_t unread = 0, reading = 0, read = 0, unknown = 0, loading = 0, unavailable = 0; };
    struct Collection { std::string id, title; bool active = true; Counts counts; };
    struct Selection {
        std::string path;
        LibraryOrganization::SourceIdentity source;
        uint64_t generation = 0;
    };
    size_t memoryBytes(std::span<const Row> rows, size_t capacity);
    std::string displayTitle(std::string_view title, std::string_view path);
    bool isNewsletterCollection(std::string_view collection);
    struct BookmarkInfo {
        Status legacyStatus = Status::unknown;
        uint32_t wordIndex = 0;
        bool known = false;
    };
    // Shared by library preparation and Item actions before the library has
    // been visited. Requires SD ownership; never mutates a bookmark.
    BookmarkInfo inspectBookmark(fs::FS&, std::string_view path,
                                 const std::optional<reading::BookIdentity>&, bool ambiguous = false);
    Counts count(std::span<const Row>, std::string_view collection = {});
    std::vector<size_t> project(std::span<const Row>, std::string_view collection, Tab);
    // Returns an inventory index, never the position in a filtered list.
    std::optional<size_t> resolve(const Selection&, uint64_t generation, std::span<const Row>);
    int32_t clampScroll(int64_t pixels, size_t count, int32_t viewport = kViewportHeight);
    std::optional<size_t> fullyVisibleRow(int32_t y, int32_t scroll, size_t count,
                                           int32_t viewport = kViewportHeight);

    using MetadataReader = std::function<std::optional<reading::BookIdentity>(size_t, Row&, BookMetadata&)>;
    // MetadataReader is invoked once per item, outside paint. Prefer the prior
    // top visible item, then scan remaining entries, yielding between chunks.
    std::expected<std::vector<Row>, std::error_code> prepare(
        fs::FS&, std::span<const BookLibrary::Entry>, const MetadataReader&,
        size_t retainedBytes = 0, std::string_view priorityPath = {},
        const std::function<bool(uint64_t, uint64_t)>& proceed = {});

    class Model {
    public:
        std::expected<void, std::error_code> load(fs::FS&);
        std::expected<void, std::error_code> save(fs::FS&);
        std::expected<void, std::error_code> replace(std::vector<Row>, LibraryOrganization::Feeds, uint64_t generation);
        void discardRows(); // Release snapshots on resource/cancellation failure; keep saved context.
        bool open(std::string_view collection);
        void selectTab(Tab);
        void scrollTo(int64_t pixels, int32_t viewport = kViewportHeight);
        std::optional<Selection> select(size_t visibleRow) const;
        std::vector<Collection> collections() const;
        const std::vector<Row>& rows() const { return rows_; }
        const std::vector<size_t>& order() const { return order_; }
        std::string_view collection() const { return collection_; }
        Tab tab() const { return tab_; }
        int32_t scroll() const { return scroll_; }
        uint64_t generation() const { return generation_; }
        bool dirty() const { return dirty_; }
        size_t memoryBytes() const;
        std::error_code persistenceError() const { return persistenceError_ ? persistenceError_ : contextError_; }
        const LibraryOrganization::Views& saved() const { return views_; }
    private:
        size_t contextBytes() const;
        void remember();
        void restore();
        bool exists(std::string_view) const;
        LibraryOrganization::View* view();
        std::vector<Row> rows_;
        LibraryOrganization::Feeds feeds_;
        LibraryOrganization::Views views_;
        std::vector<size_t> order_;
        std::string collection_;
        Tab tab_ = Tab::unread;
        int32_t scroll_ = 0, viewport_ = kViewportHeight;
        uint64_t generation_ = 0;
        bool dirty_ = false;
        std::error_code persistenceError_, contextError_;
    };
}
