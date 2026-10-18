#pragma once
#include "library/LibraryView.h"
#include "ui/screens/Screens.h"
namespace screens {
    class ItemActionsScreen {
    public:
        void begin(LibraryView::Selection selected, std::string title, Screen origin) {
            selected_ = std::move(selected);
            title_ = std::move(title);
            origin_ = origin;
            ready_ = allowed_ = armed_ = recovery_ = false;
            status_ = LibraryOrganization::Status::unknown;
            error_ = false;
        }
        void loaded(LibraryOrganization::Status status, bool allowed, bool recovery = false) {
            status_ = status;
            allowed_ = allowed;
            recovery_ = recovery;
            ready_ = true;
            error_ = armed_ = false;
        }
        void failed() {
            error_ = true;
            armed_ = false;
        }
        Action draw(ui::Context&);
        const LibraryView::Selection& selected() const { return selected_; }
        Screen origin() const { return origin_; }
        bool ready() const { return ready_ && allowed_; }
        bool recovery() const { return recovery_; }

    private:
        LibraryView::Selection selected_;
        std::string title_;
        Screen origin_ = Screen::Read;
        LibraryOrganization::Status status_ = LibraryOrganization::Status::unknown;
        bool ready_ = false, allowed_ = false, armed_ = false, error_ = false, recovery_ = false;
    };
} // namespace screens
