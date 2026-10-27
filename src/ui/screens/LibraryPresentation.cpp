#include "ui/screens/LibraryScreen.h"
#include <array>

namespace screens {
    namespace {
        using Tab = LibraryOrganization::Tab;
        constexpr std::array tabs{Tab::unread, Tab::reading, Tab::read};
        constexpr std::array labels{UiText::Unread, UiText::Reading, UiText::Read};
        size_t total(const LibraryView::Counts& c) { return c.unread + c.reading + c.read + c.unknown; }
        std::string_view collectionTitle(ui::Context& ui, const LibraryView::Collection& c) {
            if (c.id == "books")
                return ui.text(UiText::LibraryBooks);
            if (c.id == "needs-review")
                return ui.text(UiText::NeedsReview);
            if (c.id == "other-articles")
                return ui.text(UiText::OtherArticles);
            return c.title;
        }
        void clipLines(ui::TextLayout& layout, ui::Rect clip) {
            // Keep complete shaped lines inside the viewport. Partial rows retain
            // their real background/title lines without bleeding into the header.
            std::erase_if(layout.lines, [&](const auto& line) {
                return line.ink.w <= 0 || line.ink.h <= 0 || !(ui::intersection(line.ink, clip) == line.ink);
            });
        }
    } // namespace
    Action LibraryScreen::drawList(ui::Context& ui, ui::Rect area, bool wide) {
        const auto* touch = ui.touch();
        if (touch && ui::hasTouch(*touch, ui::TouchStart))
            freshPress_ = true;
        const Page beforeNavigation = page_;
        const bool filtered = page_ == Page::collection && view_.collection() != "needs-review";
        if (page_ != Page::root) {
            const int16_t h = wide ? ui.height() / 4 : 32;
            ui::Rect backRect = wide ? ui::Rect{0, 0, 136, h}
                                     : ui::Rect{area.x, area.y, static_cast<int16_t>(area.w / (filtered ? 4 : 1)), h};
            if (ui.button(backRect, ui.text(UiText::LibraryBack)) && freshPress_)
                back();
            if (filtered && page_ == Page::collection)
                for (size_t i = 0; i < tabs.size(); ++i) {
                    const ui::Rect rect =
                        wide ? ui::Rect{0, static_cast<int16_t>((i + 1) * h), 136, h}
                             : ui::Rect{static_cast<int16_t>(area.x + (i + 1) * backRect.w), area.y, backRect.w, h};
                    const bool tapped = ui.tab(rect, ui.text(labels[i]), view_.tab() == tabs[i]);
                    if (tapped && freshPress_)
                        changeTab(tabs[i]);
                }
            if (!wide) {
                area.y += h + 4;
                area.h -= h + 4;
            }
            if (wide && ui.iconButton({static_cast<int16_t>(ui.width() - 46), 4, 36, 36}, ui::Icon::Power))
                return Action::PowerOff;
        }
        if (beforeNavigation != page_) {
            ui.invalidate();
            return Action::None;
        }
        std::string_view heading = ui.text(page_ == Page::root ? UiText::Library : UiText::Newsletters);
        if (page_ == Page::collection) {
            for (const auto& c : collections_)
                if (c.id == view_.collection())
                    heading = collectionTitle(ui, c);
        }
        const ui::Rect header{area.x, area.y, area.w, 26};
        const ui::Rect viewport{area.x, static_cast<int16_t>(area.y + 26), area.w,
                                static_cast<int16_t>(std::max(0, area.h - 26))};
        const bool hint = page_ == Page::collection && header.w >= 400;
        ui.label({header.x, header.y, static_cast<int16_t>(header.w - (hint ? 144 : 0)), header.h}, heading, 2);
        if (hint)
            ui.label({static_cast<int16_t>(header.x + header.w - 144), header.y, 144, header.h},
                     ui.text((viewSaveFailed_ || view_.persistenceError()) ? UiText::LibrarySaveFailed
                                                                           : UiText::HoldForActions),
                     1, ui::themes::Muted, ui::TextAlign::Right);
        if (!itemsValid_) {
            ui.label(viewport, ui.text(UiText::Checking), 2, ui::themes::Muted, ui::TextAlign::Center);
            reset();
            return Action::None;
        }
        scrollList(listScroll(), viewport.h);
        Action result = Action::None;
        if (touch && ui::hasTouch(*touch, ui::TouchStart)) {
            dragging_ = ui::contains(viewport, touch->x, touch->y);
            moved_ = held_ = false;
            startX_ = touch->x;
            startY_ = touch->y;
            startOffset_ = listScroll();
            pressGeneration_ = view_.generation();
            pressedRow_ =
                dragging_ ? LibraryView::fullyVisibleRow(touch->y - viewport.y, listScroll(), listSize(), viewport.h)
                          : std::nullopt;
        }
        if (dragging_ && touch) {
            const int32_t dx = int32_t(touch->x) - startX_, dy = int32_t(touch->y) - startY_;
            moved_ = moved_ || std::abs(dx) > ui::TouchTiming{}.tapMoveTolerancePx ||
                     std::abs(dy) > ui::TouchTiming{}.tapMoveTolerancePx;
            if (moved_) {
                pressedRow_.reset();
                scrollList(startOffset_ - dy, viewport.h);
            }
            const auto under =
                ui::contains(viewport, touch->x, touch->y)
                    ? LibraryView::fullyVisibleRow(touch->y - viewport.y, listScroll(), listSize(), viewport.h)
                    : std::nullopt;
            const bool valid =
                !moved_ && !held_ && pressedRow_ && under == pressedRow_ && pressGeneration_ == view_.generation();
            if (valid && ui::hasTouch(*touch, ui::TouchHold) && page_ == Page::collection &&
                touch->x < viewport.x + viewport.w - 72 && startX_ < viewport.x + viewport.w - 72 &&
                (touch->y - viewport.y + listScroll()) % 52 < 36 && (startY_ - viewport.y + startOffset_) % 52 < 36) {
                selectedIndex_ = *pressedRow_;
                held_ = true;
                dragging_ = false;
                if (selection())
                    result = Action::ItemActions;
            } else if (ui::hasTouch(*touch, ui::TouchRelease)) {
                if (valid && ui::hasTouch(*touch, ui::TouchTap)) {
                    if (page_ == Page::collection) {
                        selectedIndex_ = *pressedRow_;
                        if (selection())
                            result = Action::OpenBook;
                    } else {
                        activate(*pressedRow_);
                        ui.invalidate();
                        return Action::None;
                    }
                }
                dragging_ = false;
                pressedRow_.reset();
            }
        }
        if (!listSize()) {
            ui.label(viewport, ui.text(UiText::NoItemsInTab), 2, ui::themes::Muted, ui::TextAlign::Center);
            return result;
        }
        struct PaintRow {
            ui::Rect rect;
            ui::TextLayout title, detail, value;
        };
        std::array<PaintRow, 4> paints;
        size_t used = 0;
        const size_t first = listScroll() / 52;
        // Larger displays can show more rows; each row is prepared/painted once,
        // while the target's 130px viewport uses at most four clipped rows.
        uint32_t signature = ui::Context::combine(Fnv1a::kOffsetBasis, view_.generation());
        signature = ui::Context::combine(signature, static_cast<uint32_t>(page_));
        signature = ui::Context::signature(view_.collection(), signature);
        signature = ui::Context::combine(signature, static_cast<uint32_t>(view_.tab()));
        signature = ui::Context::combine(signature, listScroll());
        signature = ui::Context::combine(signature, listSize());
        for (size_t i = first; i < listSize() && int64_t(i) * 52 - listScroll() < viewport.h; ++i) {
            if (page_ == Page::collection)
                signature = ui::Context::combine(signature, items_[view_.order()[i]].progress);
        }
        if (!ui.redraw(viewport, signature, true))
            return result;
        ui.paint(viewport, [&](Arduino_GFX& output, ui::Rect translated) {
            output.fillRect(translated.x, translated.y, translated.w, translated.h, ui.color(ui::themes::Background));
        });
        const auto flush = [&] {
            for (size_t j = 0; j < used; ++j) {
                auto& p = paints[j];
                const auto clipped = ui.paintBounds(ui::intersection(p.rect, viewport));
                ui.paint(clipped, [&](Arduino_GFX& output, ui::Rect translated) {
                    const int16_t dx = translated.x - clipped.x, dy = translated.y - clipped.y;
                    output.fillRect(translated.x, translated.y, translated.w, translated.h,
                                    ui.color(ui::themes::Surface));
                    if (p.rect.y + 51 >= viewport.y && p.rect.y + 51 < viewport.y + viewport.h)
                        output.drawFastHLine(p.rect.x + dx, p.rect.y + 51 + dy, p.rect.w,
                                             ui.color(ui::themes::Outline));
                    ui.drawText(output, p.title, ui.color(ui::themes::Foreground), dx, dy);
                    ui.drawText(output, p.detail, ui.color(ui::themes::Muted), dx, dy);
                    ui.drawText(output, p.value, ui.color(ui::themes::Accent), dx, dy);
                });
            }
            used = 0;
        };
        for (size_t i = first; i < listSize() && int64_t(i) * 52 - listScroll() < viewport.h; ++i) {
            const ui::Rect rect{viewport.x, static_cast<int16_t>(viewport.y + i * 52 - listScroll()), viewport.w, 52};
            std::string_view title, detail;
            std::string value;
            if (page_ == Page::collection) {
                const auto idx = view_.order()[i];
                const auto& r = view_.rows()[idx];
                title = r.title;
                detail = r.availability != LibraryView::Availability::ready ? ui.text(UiText::OrganizationUnavailable)
                         : r.status == LibraryOrganization::Status::unknown ? ui.text(UiText::PreviousStatusUnknown)
                         : items_[idx].book                                 ? std::string_view{items_[idx].book->author}
                                                                            : std::string_view{};
                if (r.progressKnown)
                    value = std::to_string(items_[idx].progress) + "%";
            } else if (page_ == Page::root) {
                title = ui.text(i == 0 ? UiText::LibraryBooks : i == 1 ? UiText::Newsletters : UiText::NeedsReview);
                const auto counts = LibraryView::count(view_.rows(), i == 0   ? "books"
                                                                     : i == 1 ? "newsletters"
                                                                              : "needs-review");
                value = std::to_string(total(counts));
                detail = ui.text(UiText::LibraryItems);
            } else {
                const auto& c = collections_[feedChoices_[i]];
                title = collectionTitle(ui, c);
                detail = ui.text(c.id.starts_with("source:") ? UiText::LibraryPublisher
                                                             : c.active ? UiText::LibraryItems : UiText::SavedNewsletter);
                value = std::to_string(total(c.counts));
            }
            auto& p = paints[used++];
            p.rect = rect;
            p.title = ui.prepareText({static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + 2),
                                      static_cast<int16_t>(rect.w - 80), 36},
                                     title, 2, ui::TextAlign::Start, 2);
            if (p.title.lines.size() < 2)
                p.title = ui.prepareText({static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + 2),
                                          static_cast<int16_t>(rect.w - 80), 22},
                                         title, 2, ui::TextAlign::Start, 1);
            p.detail = p.title.lines.size() < 2
                           ? ui.prepareText({static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + 36),
                                             static_cast<int16_t>(rect.w - 80), 14},
                                            detail, 1)
                           : ui::TextLayout{};
            p.value =
                ui.prepareText({static_cast<int16_t>(rect.x + rect.w - 68), static_cast<int16_t>(rect.y + 12), 60, 26},
                               value, 2, ui::TextAlign::Right);
            clipLines(p.title, viewport);
            clipLines(p.detail, viewport);
            clipLines(p.value, viewport);
            if (used == paints.size())
                flush();
        }
        flush();
        return result;
    }
} // namespace screens
