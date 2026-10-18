#include "ui/screens/LibraryScreen.h"

#include <algorithm>
#include "logging/Logger.h"

namespace screens {
    void LibraryScreen::reset() {
        carouselGesture_ = {};
        dragging_ = moved_ = false;
        offset_ = 0;
        freshPress_ = false;
    }
    void LibraryScreen::invalidate() {
        items_.clear();
        itemsValid_ = false;
        reset(); // A release after refresh must never activate the replacement row.
    }
    std::expected<void, std::error_code> LibraryScreen::prepare(StorageManager& storage, fs::FS& fs,
        const std::function<bool(uint64_t, uint64_t)>& proceed) {
        invalidate();
        if (!storage.mounted()) return std::unexpected(std::make_error_code(std::errc::no_such_device));
        if (!viewsLoaded_) {
            // Unsupported view records are read-only; browsing remains available.
            if (auto result = view_.load(fs); !result)
                Logger::failure("library", "load views", "/config/library-view.toml", result.error());
            viewsLoaded_ = true;
        }
        std::string priority;
        if (!view_.order().empty()) priority = view_.rows()[view_.order()[view_.scroll() / LibraryView::kRowHeight]].path;
        auto rows = LibraryView::prepare(fs, storage.books(), [&](size_t index, LibraryView::Row& row, BookMetadata& metadata) {
            auto identity = storage.readBookMetadata(index, metadata);
            row.title = LibraryView::displayTitle(storage.books()[index].title, row.path);
            return identity;
        }, view_.memoryBytes(), priority, proceed);
        if (!rows) {
            if (rows.error() == std::errc::not_enough_memory) view_.discardRows();
            return std::unexpected(rows.error());
        }
        auto feeds = LibraryOrganization::loadFeeds(fs);
        if (!feeds && feeds.error() != std::errc::no_such_file_or_directory)
            Logger::failure("library", "load feeds", "/config/library-feeds.toml", feeds.error());
        auto result = view_.replace(std::move(*rows), feeds ? std::move(*feeds) : LibraryOrganization::Feeds{}, storage.generation());
        if (!result) {
            if (result.error() == std::errc::not_enough_memory) view_.discardRows();
            return result;
        }
        inventoryGeneration_ = storage.generation();
        rebuildItems(storage);
        return {};
    }
    void LibraryScreen::rebuildItems(const StorageManager& storage) {
        items_.clear(); reset();
        const auto append = [&](size_t rowIndex) {
            const auto& row = view_.rows()[rowIndex];
            const auto* book = storage.book(row.sourceIndex);
            if (book && book->path == row.path) items_.push_back({.book=book, .chapter=row.chapter, .progress=row.progress, .row=rowIndex});
        };
        items_.reserve(allItems_ ? view_.rows().size() : view_.order().size());
        if (allItems_) {
            for (size_t i = 0; i < view_.rows().size(); ++i) append(i);
            std::sort(items_.begin(), items_.end(), [&](const auto& a, const auto& b) { return view_.rows()[a.row].sourceIndex < view_.rows()[b.row].sourceIndex; });
        } else for (size_t i : view_.order()) append(i);
        selectedIndex_ = 0;
        itemsValid_ = true;
    }
    bool LibraryScreen::openCollection(std::string_view id, const StorageManager& storage) {
        if (needsPreparation(storage) || !view_.open(id)) return false;
        allItems_ = false; rebuildItems(storage); return true;
    }
    void LibraryScreen::selectTab(LibraryOrganization::Tab tab, const StorageManager& storage) {
        if (needsPreparation(storage)) return;
        view_.selectTab(tab); allItems_ = false; rebuildItems(storage);
    }
    void LibraryScreen::showAll(const StorageManager& storage) { allItems_ = true; rebuildItems(storage); }
    std::optional<LibraryView::Selection> LibraryScreen::selection() const {
        if (!itemsValid_ || !freshPress_ || selectedIndex_ >= items_.size()) return {};
        const auto& row = view_.rows()[items_[selectedIndex_].row];
        if (row.source.sha256.empty()) return {};
        return LibraryView::Selection{row.path, row.source, inventoryGeneration_};
    }
    const std::vector<LibraryItem>& LibraryScreen::items(StorageManager& storage, const IndexedBookStore&,
                                                         const ReadingSession& session) {
        if (inventoryGeneration_ != storage.generation()) invalidate();
        if (!itemsValid_) return items_;
        // Memory-only active reader overlay; persisted status comes from preparation.
        for (auto& item : items_) {
            item.chapter = view_.rows()[item.row].chapter;
            if (item.book->path != session.sourcePath()) continue;
            item.progress = ReadingProgress::percent(session.state.wordIndex, ReadingLoop::wordCount(session));
            if (const auto* chapter = session.metadata.chapterAt(session.state.wordIndex)) item.chapter = chapter->title;
            else item.chapter = {};
        }
        return items_;
    }
}
