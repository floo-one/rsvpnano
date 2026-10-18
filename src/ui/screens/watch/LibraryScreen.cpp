#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ScreenCommon.h"
namespace screens {
    Action LibraryScreen::draw(ui::Context& ui, const std::vector<LibraryItem>&, uint32_t, Screen& screen) {
        detail::navigation(ui, Screen::Library, screen);
        if (screen != Screen::Library) {
            reset();
            return Action::None;
        }
        return drawList(ui, detail::tabContent(ui), false);
    }
} // namespace screens
