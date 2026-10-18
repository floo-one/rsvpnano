#include "ui/screens/ItemActionsScreen.h"
#include "ui/screens/ScreenCommon.h"
namespace screens {
    Action ItemActionsScreen::draw(ui::Context& ui) {
        const auto* touch = ui.touch();
        if (touch && ui::hasTouch(*touch, ui::TouchStart))
            armed_ = true;
        const bool wide = ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240;
        auto area = wide ? detail::tabContent(ui) : detail::content(ui);
        const ui::Rect back = wide ? ui::Rect{0, 0, 136, 43} : ui::Rect{area.x, area.y, 72, 32};
        if (ui.button(back, ui.text(UiText::LibraryBack)) && armed_) {
            armed_ = false;
            return Action::ItemActionsBack;
        }
        if (wide && ui.iconButton({static_cast<int16_t>(ui.width() - 46), 4, 36, 36}, ui::Icon::Power))
            return Action::PowerOff;
        if (!wide) {
            area.y += 36;
            area.h -= 36;
        }
        ui.label({area.x, area.y, area.w, 20}, title_, 2);
        const auto label = !ready_                                           ? UiText::Checking
                           : error_ || recovery_                             ? UiText::Retry
                           : !allowed_                                       ? UiText::OrganizationUnavailable
                           : status_ == LibraryOrganization::Status::read    ? UiText::Read
                           : status_ == LibraryOrganization::Status::unread  ? UiText::Unread
                           : status_ == LibraryOrganization::Status::reading ? UiText::Reading
                                                                             : UiText::PreviousStatusUnknown;
        ui.label({area.x, static_cast<int16_t>(area.y + 20), area.w, 16}, ui.text(label), 1, ui::themes::Muted);
        const int16_t height = std::min<int16_t>(56, (area.h - 44) / 2), half = (area.w - 8) / 2;
        const ui::Rect markRead{area.x, static_cast<int16_t>(area.y + 36), half, height};
        const ui::Rect markUnread{static_cast<int16_t>(area.x + half + 8), markRead.y, half, height};
        const ui::Rect restart{area.x, static_cast<int16_t>(markRead.y + height + 8), area.w, height};
        const bool enabled = ready_ && allowed_;
        const auto pressed = [&](ui::Rect rect, UiText label, UiText caption, bool active) {
            const bool result = ui.button(rect, ui.text(label), active, ui::Icon::None, 1, ui.text(caption));
            return result && armed_ && touch && ui::contains(rect, touch->x, touch->y);
        };
        Action result = Action::None;
        if (pressed(markRead, UiText::MarkRead, UiText::KeepPosition,
                    enabled && !recovery_ && status_ != LibraryOrganization::Status::read))
            result = Action::MarkRead;
        if (pressed(markUnread, UiText::MarkUnread, UiText::KeepPosition,
                    enabled && !recovery_ && status_ != LibraryOrganization::Status::unread))
            result = Action::MarkUnread;
        if (pressed(restart, UiText::StartAgain, UiText::ResetPosition, enabled))
            result = Action::StartAgain;
        if (result != Action::None)
            armed_ = false;
        return result;
    }
} // namespace screens
