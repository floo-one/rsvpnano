#include "library/ReadingStateStore.h"
#include "ui/screens/ItemActionsScreen.h"
#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ScreenCommon.h"
#include <FS.h>
#include <algorithm>
#include <unity.h>
using namespace screens;
namespace LO = LibraryOrganization;
namespace Board::Storage {
    fs::FS& filesystem() {
        static fs::FS fs;
        return fs;
    }
} // namespace Board::Storage
namespace BookLibrary {
    std::string_view displayName(const Entry& entry) { return entry.title; }
} // namespace BookLibrary
void setUp() {}
void tearDown() {}
ui::TouchContact contact;
ui::TouchSampleResult poll(ui::TouchContact& out) {
    out = contact;
    return ui::TouchSampleResult::Contact;
}
struct Gfx : Arduino_GFX {
    Gfx(int w = 640, int h = 172) : Arduino_GFX(w, h) {}
    bool outside = false;
    void getTextBounds(const char* text, int16_t x, int16_t y, int16_t* ix, int16_t* iy, uint16_t* w,
                       uint16_t* h) override {
        Arduino_GFX::getTextBounds(text, x, y, ix, iy, w, h);
        *iy = y - 8 * lastTextSize;
    }
    std::vector<ui::Rect> fills;
    std::string text;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
        outside |= x < 0 || y < 0 || w < 0 || h < 0 || x + w > width() || y + h > height();
        fills.push_back({x, y, w, h});
        Arduino_GFX::fillRect(x, y, w, h, c);
    }
    size_t write(uint8_t c) override {
        text += char(c);
        return Arduino_GFX::write(c);
    }
};
struct Fixture {
    fs::FS fs;
    StorageManager storage;
    IndexedBookStore store;
    ReadingSession session;
    Gfx gfx;
    ui::themes::Theme theme = ui::themes::defaultTheme();
    ui::Context ui{gfx};
    LibraryScreen library;
    ItemActionsScreen actions;
    Screen screen = Screen::Library;
    uint32_t clock = 1;
    size_t io = 0;
    Fixture(size_t n = 6, LO::Status status = LO::Status::unread, int w = 640, int h = 172,
            bool legacy = false) : gfx(w, h) {
        contact = {};
        ui.setTheme(theme);
        ui.setTouchSource({.surface = {static_cast<uint16_t>(w), static_cast<uint16_t>(h)}, .poll = &poll});
        for (size_t i = 0; i < n; ++i) {
            auto path = "/library/books/" + std::to_string(i) + ".txt";
            fs.put(path, "abc");
            storage.inventory.push_back({path, 3, "Title " + std::to_string(i), "Author"});
            if (!legacy) {
                LO::Item item;
                item.path = path;
                item.source = *LO::hashSource(fs, path);
                item.status = status;
                if (status == LO::Status::unknown) item.unknown_reason = "previous-status-unknown";
                TEST_ASSERT_TRUE(LO::saveItem(fs, item, 0).has_value());
            }
        }
        TEST_ASSERT_TRUE(library.prepare(storage, fs).has_value());
        fs.disk->fail = [&](const auto&, const auto&) {
            ++io;
            return false;
        };
    }
    Action draw() {
        const auto& items = library.items(storage, store, session);
        ui.beginFrame(static_cast<uint8_t>(screen));
        Action result = screen == Screen::ItemActions ? actions.draw(ui)
                        : screen == Screen::Read      ? screens::read(ui, "Current book", "Author", 42, screen)
                                                      : library.draw(ui, items, clock, screen);
        ui.endFrame();
        return result;
    }
    Action sample(bool down, int x, int y, uint32_t delta = 1) {
        contact = {down, static_cast<uint16_t>(x), static_cast<uint16_t>(y)};
        clock += delta;
        ui.pollTouch(clock);
        return draw();
    }
    Action tap(int x, int y) {
        sample(true, x, y);
        return sample(false, x, y);
    }
    void books() {
        TEST_ASSERT_TRUE(library.openCollection("books", storage));
        draw();
    }
    void actionScreen(LO::Status status = LO::Status::reading) {
        actions.begin({storage.inventory[0].path, *LO::hashSource(fs, storage.inventory[0].path), storage.generation()},
                      "Title 0", Screen::Library);
        actions.loaded(status, true);
        screen = Screen::ItemActions;
        draw();
    }
};
void target_shows_two_and_half_rows_with_title_preview() {
    Fixture f;
    f.books();
    TEST_ASSERT_TRUE(std::ranges::find(f.gfx.fills, ui::Rect{148, 34, 444, 52}) != f.gfx.fills.end());
    TEST_ASSERT_TRUE(std::ranges::find(f.gfx.fills, ui::Rect{148, 86, 444, 52}) != f.gfx.fills.end());
    TEST_ASSERT_TRUE(std::ranges::find(f.gfx.fills, ui::Rect{148, 138, 444, 26}) != f.gfx.fills.end());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("Title 2"));
    TEST_ASSERT_EQUAL(std::string::npos, f.gfx.text.find("Title 3"));
    TEST_ASSERT_EQUAL(0, f.io);
}
void full_row_tap_resolves_filtered_item() {
    Fixture f;
    f.books();
    TEST_ASSERT_TRUE(f.tap(220, 100) == Action::OpenBook);
    TEST_ASSERT_TRUE(f.library.selection()->path == f.storage.inventory[1].path);
    TEST_ASSERT_EQUAL(0, f.io);
}
void partial_row_cannot_tap_or_hold_but_can_scroll() {
    Fixture f;
    f.books();
    TEST_ASSERT_TRUE(f.tap(220, 150) == Action::None);
    f.sample(true, 220, 150);
    TEST_ASSERT_TRUE(f.sample(true, 220, 150, 650) == Action::None);
    f.sample(false, 220, 150);
    f.sample(true, 220, 150);
    f.sample(true, 220, 90);
    TEST_ASSERT_TRUE(f.sample(false, 220, 90) == Action::None);
    TEST_ASSERT_EQUAL(60, f.library.view().scroll());
    TEST_ASSERT_EQUAL(0, f.io);
}
void drag_and_outside_gestures_never_open() {
    Fixture f;
    f.books();
    f.sample(true, 220, 60);
    f.sample(true, 220, 100);
    f.sample(true, 220, 60);
    TEST_ASSERT_TRUE(f.sample(false, 220, 60) == Action::None);
    f.sample(true, 610, 60);
    f.sample(true, 580, 60);
    TEST_ASSERT_TRUE(f.sample(false, 580, 60) == Action::None);
    TEST_ASSERT_TRUE(f.tap(220, 30) == Action::None);
}
void hold_opens_actions_without_opening_and_release_is_consumed() {
    Fixture f;
    f.books();
    f.sample(true, 220, 50);
    TEST_ASSERT_TRUE(f.sample(true, 220, 50, 650) == Action::ItemActions);
    TEST_ASSERT_TRUE(f.library.selection()->path == f.storage.inventory[0].path);
    f.actionScreen();
    TEST_ASSERT_TRUE(f.sample(false, 220, 50) == Action::None);
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::MarkRead);
}
void progress_and_detail_are_not_hold_shortcuts() {
    Fixture f;
    f.books();
    f.sample(true, 560, 50);
    TEST_ASSERT_TRUE(f.sample(true, 560, 50, 650) == Action::None);
    f.sample(false, 560, 50);
    f.sample(true, 220, 78);
    TEST_ASSERT_TRUE(f.sample(true, 220, 78, 650) == Action::None);
}
void root_collection_navigation_and_tabs() {
    Fixture f;
    f.draw();
    f.tap(220, 50);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::collection);
    TEST_ASSERT_TRUE(f.library.view().collection() == "books");
    f.tap(60, 108);
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::reading);
    TEST_ASSERT_TRUE(f.library.view().order().empty());
    f.tap(60, 20);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::root);
    f.tap(220, 50);
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::reading);
    TEST_ASSERT_TRUE(f.library.back());
    TEST_ASSERT_FALSE(f.library.back());
}
void feed_navigation_returns_to_chooser_then_root() {
    Fixture f(0);
    f.fs.disk->fail = {};
    LO::Feeds feeds;
    feeds.next_order = 2;
    feeds.feeds = {{.id = "one",
                    .url = "https://example.com/feed",
                    .title = "Newsletter One",
                    .active = true,
                    .subscription_order = 1}};
    TEST_ASSERT_TRUE(LO::saveFeeds(f.fs, feeds, 0).has_value());
    TEST_ASSERT_TRUE(f.library.prepare(f.storage, f.fs).has_value());
    f.draw();
    f.tap(220, 100);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::feeds);
    f.tap(220, 50);
    TEST_ASSERT_TRUE(f.library.view().collection() == "feed:one");
    f.tap(60, 20);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::feeds);
    f.tap(60, 20);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::root);
}
void needs_review_is_temporary_and_unfiltered() {
    Fixture f(3, LO::Status::unknown);
    f.draw();
    f.sample(true, 220, 150);
    f.sample(true, 220, 110);
    f.sample(false, 220, 110);
    TEST_ASSERT_TRUE(f.tap(220, 140) == Action::None);
    TEST_ASSERT_TRUE(f.library.view().collection() == "needs-review");
    TEST_ASSERT_EQUAL(3, f.library.view().order().size());
    f.gfx.text.clear();
    f.ui.invalidate();
    f.draw();
    TEST_ASSERT_EQUAL(std::string::npos, f.gfx.text.find("Unread"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("Previous status unknown"));
}
void legacy_books_are_unread_without_reviewing_first() {
    Fixture f(3, LO::Status::unknown, 640, 172, true);
    const auto mutations = f.fs.disk->mutations.size();
    f.draw();
    TEST_ASSERT_EQUAL(std::string::npos, f.gfx.text.find("Needs review"));
    f.books();
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::unread);
    TEST_ASSERT_EQUAL(3, f.library.view().order().size());
    TEST_ASSERT_EQUAL(std::string::npos, f.gfx.text.find("Previous status unknown"));
    TEST_ASSERT_EQUAL(3, LibraryView::count(f.library.view().rows(), "books").unread);
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::OpenBook);
    TEST_ASSERT_TRUE(f.library.selection()->path == f.storage.inventory[0].path);
    TEST_ASSERT_EQUAL(0, f.io);
    TEST_ASSERT_EQUAL(mutations, f.fs.disk->mutations.size());
}
void publisher_navigation_selects_canonical_article_and_restores_context() {
    Fixture f(1);
    f.fs.disk->fail = {};
    const auto append = [&](std::string path, std::string title, std::string source) {
        const std::string bytes = "@title " + title + "\n@source " + source + "\n\nArticle body";
        f.fs.put(path, bytes);
        f.storage.inventory.push_back({std::move(path), bytes.size(), std::move(title), "Author"});
    };
    append("/library/articles/other.rsvp", "Different publisher", "https://z.example/post");
    // Inventory order differs from the displayed order; taps must retain file identity.
    append("/library/articles/z.rsvp", "Zeta", "https://example.com/z");
    append("/library/articles/a.rsvp", "Alpha", "https://example.com/a");
    for (size_t i = 0; i < 4; ++i)
        append("/library/articles/b" + std::to_string(i) + ".rsvp", "Beta " + std::to_string(i),
               "https://example.com/b" + std::to_string(i));
    TEST_ASSERT_TRUE(f.library.prepare(f.storage, f.fs).has_value());
    f.io = 0;
    f.fs.disk->fail = [&](const auto&, const auto&) { ++f.io; return false; };
    f.draw();
    f.tap(220, 100);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::feeds);
    f.draw();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("example.com"));
    f.tap(220, 50);
    TEST_ASSERT_TRUE(f.library.view().collection() == "source:example.com");
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::unread);
    TEST_ASSERT_EQUAL(6, f.library.view().order().size());
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::OpenBook);
    TEST_ASSERT_TRUE(f.library.selection()->path == f.storage.inventory[3].path);

    f.tap(60, 150);
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::read);
    f.tap(60, 20);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::feeds);
    f.tap(60, 20);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::root);
    f.tap(220, 100);
    f.tap(220, 50);
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::read);
    f.tap(60, 60);
    f.sample(true, 220, 150);
    f.sample(true, 220, 90);
    f.sample(false, 220, 90);
    TEST_ASSERT_EQUAL(60, f.library.view().scroll());
    f.library.rememberReaderOrigin();
    f.library.showRoot();
    f.books();
    f.library.restoreReaderOrigin();
    TEST_ASSERT_TRUE(f.library.view().collection() == "source:example.com");
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::unread);
    TEST_ASSERT_EQUAL(60, f.library.view().scroll());
    f.draw();
    TEST_ASSERT_EQUAL(0, f.io);

    f.fs.disk->fail = {};
    TEST_ASSERT_TRUE(f.library.saveViews(f.fs).has_value());
    LibraryScreen restored;
    TEST_ASSERT_TRUE(restored.prepare(f.storage, f.fs).has_value());
    TEST_ASSERT_TRUE(restored.openCollection("source:example.com", f.storage));
    TEST_ASSERT_TRUE(restored.view().tab() == LO::Tab::unread);
    TEST_ASSERT_EQUAL(60, restored.view().scroll());
}
void refreshed_inventory_cancels_old_press() {
    Fixture f;
    f.books();
    f.sample(true, 220, 60);
    ++f.storage.revision;
    TEST_ASSERT_TRUE(f.sample(false, 220, 60) == Action::None);
    TEST_ASSERT_FALSE(f.library.selection().has_value());
}
void actions_disable_redundant_marks_and_keep_reset_separate() {
    Fixture f;
    f.actionScreen(LO::Status::read);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("Keep position"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("Reset position to beginning"));
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::None);
    TEST_ASSERT_TRUE(f.tap(450, 60) == Action::MarkUnread);
    TEST_ASSERT_TRUE(f.tap(220, 130) == Action::StartAgain);
    TEST_ASSERT_TRUE(f.tap(60, 20) == Action::ItemActionsBack);
    f.actions.loaded(LO::Status::unread, true);
    TEST_ASSERT_TRUE(f.tap(450, 60) == Action::None);
}
void actions_require_ready_and_recovery_blocks_marks() {
    Fixture f;
    f.actionScreen();
    f.actions.loaded(LO::Status::unknown, false);
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::None);
    TEST_ASSERT_TRUE(f.tap(220, 130) == Action::None);
    f.actions.loaded(LO::Status::unknown, true, true);
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::None);
    TEST_ASSERT_TRUE(f.tap(450, 60) == Action::None);
    TEST_ASSERT_TRUE(f.tap(220, 130) == Action::StartAgain);
}
void read_menu_keeps_resume_font_chapters_and_library() {
    Fixture f;
    f.screen = Screen::Read;
    f.draw();
    TEST_ASSERT_TRUE(f.tap(220, 30) == Action::Resume);
    TEST_ASSERT_TRUE(f.tap(520, 120) == Action::ItemActions);
    f.tap(220, 120);
    TEST_ASSERT_TRUE(f.screen == Screen::Chapters);
    f.screen = Screen::Read;
    f.draw();
    f.tap(370, 120);
    TEST_ASSERT_TRUE(f.screen == Screen::Library);
    f.screen = Screen::Read;
    f.draw();
    f.tap(570, 30);
    TEST_ASSERT_TRUE(f.screen == Screen::BookFonts);
}
void short_lists_have_no_blank_scroll_and_last_row_activates_only_when_visible() {
    for (size_t n : {0, 1, 2, 3}) {
        Fixture f(n);
        f.books();
        f.sample(true, 220, 150);
        f.sample(true, 220, 50);
        f.sample(false, 220, 50);
        TEST_ASSERT_EQUAL(n == 3 ? 26 : 0, f.library.view().scroll());
        if (n == 3) {
            TEST_ASSERT_TRUE(f.tap(220, 150) == Action::OpenBook);
            TEST_ASSERT_TRUE(f.library.selection()->path == f.storage.inventory[2].path);
        }
        if (!n)
            TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("No items in this tab"));
    }
}
void reader_return_restores_collection_and_tab_after_other_browsing() {
    Fixture f;
    f.books();
    f.library.selectTab(LO::Tab::read, f.storage);
    f.library.rememberReaderOrigin();
    f.library.showRoot();
    f.library.openCollection("needs-review", f.storage);
    f.library.restoreReaderOrigin();
    TEST_ASSERT_TRUE(f.library.view().collection() == "books");
    TEST_ASSERT_TRUE(f.library.view().tab() == LO::Tab::read);
    TEST_ASSERT_TRUE(f.library.page() == LibraryScreen::Page::collection);
}
void long_unicode_titles_and_partial_top_rows_remain_bounded() {
    Fixture f;
    f.fs.disk->fail = {};
    f.storage.inventory[0].title = "A very long newsletter title that needs two "
                                   "lines and still must be shortened safely";
    f.storage.inventory[1].title = "日本語の本と読み続けるためのタイトル";
    TEST_ASSERT_TRUE(f.library.prepare(f.storage, f.fs).has_value());
    f.books();
    TEST_ASSERT_FALSE(f.gfx.outside);
    f.library.view().scrollTo(17);
    f.draw();
    TEST_ASSERT_TRUE(f.tap(220, 40) == Action::None);
    TEST_ASSERT_FALSE(f.gfx.outside);
}
void failed_action_requires_a_new_press_before_retry() {
    Fixture f;
    f.actionScreen();
    f.sample(true, 220, 60);
    f.actions.failed();
    TEST_ASSERT_TRUE(f.sample(false, 220, 60) == Action::None);
    TEST_ASSERT_TRUE(f.tap(220, 60) == Action::MarkRead);
}
void failed_view_save_is_visible_and_successful_retry_clears_it() {
    Fixture f;
    f.books();
    f.fs.disk->fail = {};
    TEST_ASSERT_TRUE(f.library.saveViews(f.fs).has_value());
    f.library.view().scrollTo(80);
    f.fs.disk->fail = [](const auto& op, const auto&) { return op == "write"; };
    TEST_ASSERT_FALSE(f.library.saveViews(f.fs).has_value());
    f.gfx.text.clear();
    f.draw();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("List position not saved"));
    f.fs.disk->fail = {};
    TEST_ASSERT_TRUE(f.library.saveViews(f.fs).has_value());
    f.gfx.text.clear();
    f.draw();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.gfx.text.find("Hold for actions"));
}
void hold_must_begin_on_the_title() {
    Fixture f;
    f.books();
    f.sample(true, 220, 78);
    f.sample(true, 220, 65);
    TEST_ASSERT_TRUE(f.sample(true, 220, 65, 650) == Action::None);
}
void watch_library_and_actions_fit_supported_sizes() {
    for (auto size : {ui::TouchSurface{450, 600},
                      {480, 480},
                      {410, 502},
                      {368, 448},
                      {172, 320},
                      {600, 450},
                      {502, 410},
                      {448, 368},
                      {320, 172}}) {
        Fixture f(6, LO::Status::unread, size.width, size.height);
        f.draw();
        f.books();
        f.actionScreen();
        TEST_ASSERT_FALSE(f.gfx.outside);
    }
}
int main() {
    UNITY_BEGIN();
#ifdef LIBRARY_WATCH_UI
    RUN_TEST(watch_library_and_actions_fit_supported_sizes);
#else
    RUN_TEST(failed_view_save_is_visible_and_successful_retry_clears_it);
    RUN_TEST(hold_must_begin_on_the_title);
    RUN_TEST(short_lists_have_no_blank_scroll_and_last_row_activates_only_when_visible);
    RUN_TEST(reader_return_restores_collection_and_tab_after_other_browsing);
    RUN_TEST(long_unicode_titles_and_partial_top_rows_remain_bounded);
    RUN_TEST(failed_action_requires_a_new_press_before_retry);
    RUN_TEST(target_shows_two_and_half_rows_with_title_preview);
    RUN_TEST(full_row_tap_resolves_filtered_item);
    RUN_TEST(partial_row_cannot_tap_or_hold_but_can_scroll);
    RUN_TEST(drag_and_outside_gestures_never_open);
    RUN_TEST(hold_opens_actions_without_opening_and_release_is_consumed);
    RUN_TEST(progress_and_detail_are_not_hold_shortcuts);
    RUN_TEST(root_collection_navigation_and_tabs);
    RUN_TEST(feed_navigation_returns_to_chooser_then_root);
    RUN_TEST(needs_review_is_temporary_and_unfiltered);
    RUN_TEST(legacy_books_are_unread_without_reviewing_first);
    RUN_TEST(publisher_navigation_selects_canonical_article_and_restores_context);
    RUN_TEST(refreshed_inventory_cancels_old_press);
    RUN_TEST(actions_disable_redundant_marks_and_keep_reset_separate);
    RUN_TEST(actions_require_ready_and_recovery_blocks_marks);
    RUN_TEST(read_menu_keeps_resume_font_chapters_and_library);
#endif
    return UNITY_END();
}
