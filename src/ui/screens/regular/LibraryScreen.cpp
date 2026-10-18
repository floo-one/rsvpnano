#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ScreenCommon.h"
namespace screens {
    Action LibraryScreen::draw(ui::Context& ui, const std::vector<LibraryItem>&, uint32_t, Screen& screen) {
        if (page_ == Page::root) {
            if (auto action = detail::navigation(ui, Screen::Library, screen); action != Action::None)
                return action;
            if (screen != Screen::Library) {
                reset();
                return Action::None;
            }
        }
        const bool wide = ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240;
        return drawList(ui, detail::tabContent(ui), wide);
    }
} // namespace screens
