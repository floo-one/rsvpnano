#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/ReadingLoop.h"
#include "library/LibraryView.h"
#include "library/StorageManager.h"
#include "library/IndexedBookStore.h"
#include "library/ReadingProgress.h"
#include "ui/screens/Screens.h"
#include "ui/Layouts.h"

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
        std::expected<void, std::error_code> saveViews(fs::FS& fs) { return view_.save(fs); }
        std::optional<LibraryView::Selection> selection() const;
        void observeTouch(const ui::Touch* touch) {
            if (touch && ui::hasTouch(*touch, ui::TouchStart)) freshPress_ = true;
        }
        LibraryView::Model& view() { return view_; }
        // T05 uses these memory-only adapters to switch the renderer to collections.
        bool openCollection(std::string_view, const StorageManager&);
        void selectTab(LibraryOrganization::Tab, const StorageManager&);
        void showAll(const StorageManager&);

    private:
        int32_t centeredOffset(const std::vector<LibraryItem>& items, size_t index, int16_t viewportWidth) const;
        int32_t clampOffset(const std::vector<LibraryItem>& items, int32_t offset, int16_t viewportWidth) const;
        size_t nearest(const std::vector<LibraryItem>& items, int32_t offset, int16_t x, int16_t viewportX) const;
        size_t spineAt(const std::vector<LibraryItem>& items, int32_t offset, const ui::Rect& viewport, uint16_t x,
                       uint16_t y) const;
        int16_t spineHeight(const LibraryItem& item, size_t index) const;
        uint32_t signature(const std::vector<LibraryItem>& items, size_t current, size_t first,
                           size_t pastLast) const;
        void rebuildItems(const StorageManager&);
        LibraryView::Model view_;
        uint64_t inventoryGeneration_ = 0;
        bool viewsLoaded_ = false;
        bool freshPress_ = false;
        bool allItems_ = true; // Existing shelf/watch presentation until T05.
        bool dragging_ = false;
        bool moved_ = false;
        uint16_t startX_ = 0;
        uint16_t startY_ = 0;
        int32_t startOffset_ = 0;
        int32_t offset_ = 0;
        size_t selectedIndex_ = 0;
        std::vector<LibraryItem> items_;
        bool itemsValid_ = false;
        ui::CarouselGesture carouselGesture_;
    };

} // namespace screens
