#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "library/IndexedBookStore.h"
#include "library/LibraryView.h"
#include "library/ReadingProgress.h"
#include "library/StorageManager.h"
#include "reader/ReadingLoop.h"
#include "ui/Layouts.h"
#include "ui/screens/Screens.h"

namespace screens {

    struct LibraryItem {
        const BookLibrary::Entry* book = nullptr;
        std::string_view chapter;
        uint8_t progress = 0;
        size_t row = 0;
    };

    class LibraryScreen {
    public:
        Action draw(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs, Screen& screen);
        void reset();
        void invalidate();
        const std::vector<LibraryItem>& items(StorageManager& storage, const IndexedBookStore& bookStore,
                                              const ReadingSession& session);
        bool needsPreparation(const StorageManager& storage) const {
            return !itemsValid_ || inventoryGeneration_ != storage.generation();
        }
        // Called only in the App's serialized SD job. No paint/touch I/O.
        std::expected<void, std::error_code> prepare(StorageManager&, fs::FS&,
                                                     const std::function<bool(uint64_t, uint64_t)>& proceed = {});
        std::expected<void, std::error_code> saveViews(fs::FS& fs) {
            auto result = view_.save(fs);
            viewSaveFailed_ = !result;
            return result;
        }
        std::optional<LibraryView::Selection> selection() const;
        void observeTouch(const ui::Touch* touch) {
            if (touch && ui::hasTouch(*touch, ui::TouchStart))
                freshPress_ = true;
        }
        LibraryView::Model& view() { return view_; }
        enum class Page { root, feeds, collection };
        Page page() const { return page_; }
        bool back(); // false at the library root; App then returns to Read.
        void showRoot();
        void rememberReaderOrigin();
        void restoreReaderOrigin();
        void cancelGesture() { reset(); }
        // Memory-only adapters; storage generation is validated by items().
        bool openCollection(std::string_view, const StorageManager&);
        void selectTab(LibraryOrganization::Tab, const StorageManager&);
        void showAll(const StorageManager&);

    private:
        Action drawList(ui::Context&, ui::Rect, bool wide);
        bool browse(std::string_view);
        void changeTab(LibraryOrganization::Tab);
        void updateCollections();
        size_t listSize() const;
        int32_t listScroll() const;
        void scrollList(int32_t, int32_t);
        void activate(size_t);
        void rebuildItems(const StorageManager&);
        LibraryView::Model view_;
        uint64_t inventoryGeneration_ = 0;
        bool viewsLoaded_ = false;
        bool viewSaveFailed_ = false;
        bool freshPress_ = false;
        Page page_ = Page::root;
        std::string readerCollection_;
        LibraryOrganization::Tab readerTab_ = LibraryOrganization::Tab::unread;
        std::vector<LibraryView::Collection> collections_;
        std::vector<size_t> feedChoices_;
        bool hasReview_ = false;
        int32_t rootScroll_ = 0, feedScroll_ = 0;
        std::optional<size_t> pressedRow_;
        bool held_ = false;
        uint64_t pressGeneration_ = 0;
        bool dragging_ = false;
        bool moved_ = false;
        uint16_t startX_ = 0;
        uint16_t startY_ = 0;
        int32_t startOffset_ = 0;
        size_t selectedIndex_ = 0;
        std::vector<LibraryItem> items_;
        bool itemsValid_ = false;
    };

} // namespace screens
