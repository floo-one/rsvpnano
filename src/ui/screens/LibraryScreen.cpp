#include "ui/screens/LibraryScreen.h"

#include "logging/Logger.h"
#include <algorithm>

namespace screens {
    void LibraryScreen::updateCollections() {
        collections_ = view_.collections();
        feedChoices_.clear();
        hasReview_ = false;
        for (size_t i = 0; i < collections_.size(); ++i) {
            if (collections_[i].id == "needs-review")
                hasReview_ = true;
            if (collections_[i].id.starts_with("feed:") || collections_[i].id == "other-articles")
                feedChoices_.push_back(i);
        }
        if (page_ == Page::collection && view_.collection().empty())
            page_ = Page::feeds;
    }
    bool LibraryScreen::browse(std::string_view id) {
        if (!view_.open(id))
            return false;
        page_ = Page::collection;
        reset();
        return true;
    }
    void LibraryScreen::changeTab(LibraryOrganization::Tab tab) {
        view_.selectTab(tab);
        reset();
    }
    void LibraryScreen::showRoot() {
        page_ = Page::root;
        reset();
    }
    bool LibraryScreen::back() {
        if (page_ == Page::root)
            return false;
        if (page_ == Page::feeds)
            page_ = Page::root;
        else
            page_ = view_.collection().starts_with("feed:") || view_.collection() == "other-articles" ? Page::feeds
                                                                                                      : Page::root;
        reset();
        return true;
    }
    size_t LibraryScreen::listSize() const {
        if (page_ == Page::root)
            return hasReview_ ? 3 : 2;
        return page_ == Page::feeds ? feedChoices_.size() : view_.order().size();
    }
    int32_t LibraryScreen::listScroll() const {
        return page_ == Page::root ? rootScroll_ : page_ == Page::feeds ? feedScroll_ : view_.scroll();
    }
    void LibraryScreen::scrollList(int32_t value, int32_t viewport) {
        value = LibraryView::clampScroll(value, listSize(), viewport);
        if (page_ == Page::root)
            rootScroll_ = value;
        else if (page_ == Page::feeds)
            feedScroll_ = value;
        else
            view_.scrollTo(value, viewport);
    }
    void LibraryScreen::activate(size_t row) {
        if (page_ == Page::root) {
            if (row == 0)
                browse("books");
            else if (row == 1) {
                page_ = Page::feeds;
                reset();
            } else if (row == 2 && hasReview_)
                browse("needs-review");
        } else if (page_ == Page::feeds && row < feedChoices_.size())
            browse(collections_[feedChoices_[row]].id);
    }

    void LibraryScreen::rememberReaderOrigin() {
        readerCollection_ = view_.collection();
        readerTab_ = view_.tab();
    }
    void LibraryScreen::restoreReaderOrigin() {
        if (browse(readerCollection_))
            changeTab(readerTab_);
        else {
            page_ = readerCollection_.starts_with("feed:") ? Page::feeds : Page::root;
            reset();
        }
    }
    void LibraryScreen::reset() {
        pressedRow_.reset();
        held_ = false;
        dragging_ = moved_ = false;
        freshPress_ = false;
    }
    void LibraryScreen::invalidate() {
        items_.clear();
        itemsValid_ = false;
        reset(); // A release after refresh must never activate the replacement row.
    }
    std::expected<void, std::error_code>
    LibraryScreen::prepare(StorageManager& storage, fs::FS& fs,
                           const std::function<bool(uint64_t, uint64_t)>& proceed) {
        invalidate();
        if (!storage.mounted())
            return std::unexpected(std::make_error_code(std::errc::no_such_device));
        if (!viewsLoaded_) {
            // Unsupported view records are read-only; browsing remains available.
            if (auto result = view_.load(fs); !result)
                Logger::failure("library", "load views", "/config/library-view.toml", result.error());
            viewsLoaded_ = true;
        }
        std::string priority;
        if (!view_.order().empty())
            priority = view_.rows()[view_.order()[view_.scroll() / LibraryView::kRowHeight]].path;
        auto rows = LibraryView::prepare(
            fs, storage.books(),
            [&](size_t index, LibraryView::Row& row, BookMetadata& metadata) {
                auto identity = storage.readBookMetadata(index, metadata);
                row.title = LibraryView::displayTitle(storage.books()[index].title, row.path);
                return identity;
            },
            view_.memoryBytes(), priority, proceed);
        if (!rows) {
            if (rows.error() == std::errc::not_enough_memory)
                view_.discardRows();
            return std::unexpected(rows.error());
        }
        auto feeds = LibraryOrganization::loadFeeds(fs);
        if (!feeds && feeds.error() != std::errc::no_such_file_or_directory)
            Logger::failure("library", "load feeds", "/config/library-feeds.toml", feeds.error());
        auto result = view_.replace(std::move(*rows), feeds ? std::move(*feeds) : LibraryOrganization::Feeds{},
                                    storage.generation());
        if (!result) {
            if (result.error() == std::errc::not_enough_memory)
                view_.discardRows();
            return result;
        }
        inventoryGeneration_ = storage.generation();
        rebuildItems(storage);
        updateCollections();
        return {};
    }
    void LibraryScreen::rebuildItems(const StorageManager& storage) {
        items_.clear();
        reset();
        const auto append = [&](size_t rowIndex) {
            const auto& row = view_.rows()[rowIndex];
            const auto* book = storage.book(row.sourceIndex);
            if (book && book->path != row.path)
                book = nullptr;
            items_.push_back({.book = book, .chapter = row.chapter, .progress = row.progress, .row = rowIndex});
        };
        items_.reserve(view_.rows().size());
        for (size_t i = 0; i < view_.rows().size(); ++i)
            append(i);
        selectedIndex_ = 0;
        itemsValid_ = true;
    }
    bool LibraryScreen::openCollection(std::string_view id, const StorageManager& storage) {
        if (needsPreparation(storage) || !view_.open(id))
            return false;
        page_ = Page::collection;
        reset();
        return true;
    }
    void LibraryScreen::selectTab(LibraryOrganization::Tab tab, const StorageManager& storage) {
        if (needsPreparation(storage))
            return;
        view_.selectTab(tab);
        reset();
    }
    void LibraryScreen::showAll(const StorageManager&) { showRoot(); }
    std::optional<LibraryView::Selection> LibraryScreen::selection() const {
        if (!itemsValid_ || !freshPress_ || page_ != Page::collection)
            return {};
        return view_.select(selectedIndex_);
    }
    const std::vector<LibraryItem>& LibraryScreen::items(StorageManager& storage, const IndexedBookStore&,
                                                         const ReadingSession& session) {
        if (inventoryGeneration_ != storage.generation())
            invalidate();
        if (!itemsValid_)
            return items_;
        // Memory-only active reader overlay; persisted status comes from preparation.
        for (auto& item : items_) {
            item.chapter = view_.rows()[item.row].chapter;
            if (!item.book || item.book->path != session.sourcePath())
                continue;
            item.progress = ReadingProgress::percent(session.state.wordIndex, ReadingLoop::wordCount(session));
            if (const auto* chapter = session.metadata.chapterAt(session.state.wordIndex))
                item.chapter = chapter->title;
            else
                item.chapter = {};
        }
        return items_;
    }
} // namespace screens
