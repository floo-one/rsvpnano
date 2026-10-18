#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <atomic>
#include <string>
#include <string_view>

#include "board/BoardDisplay.h"
#include "board/BoardPower.h"
#include "companion/http/CompanionApi.h"
#include "companion/serial/CompanionSerial.h"
#include "feeds/RssFeeds.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "input/Input.h"
#include "library/StorageManager.h"
#include "localization/LocaleCatalog.h"
#include "settings/SettingsStore.h"
#include "ui/Ui.h"
#include "ui/screens/ChaptersScreen.h"
#include "ui/screens/ItemActionsScreen.h"
#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ReaderScreen.h"
#include "ui/screens/Screens.h"
#include "ui/screens/StandbyScreen.h"
#include "update/OtaUpdater.h"
#include "usb/UsbMassStorageManager.h"

class App {
public:
    void begin();
    void update(uint32_t nowMs);
    // Shared action-screen boundary. Memory-only enqueue; the App job performs
    // I/O after the frame. Currently targets a successfully opened, paused item.
    bool requestReadingAction(ReadingOrganization::Command, const ReadingOrganization::Target&);

private:
    enum class JobKind : uint8_t {
        None,
        Rss,
        StorageCheck,
        OtaCheck,
        OtaInstall,
        Book,
        Typography,
        ReadingOrganization,
        Library,
        ItemInfo,
        LibraryAction,
    };

    struct JobUpdate {
        bool complete = false;
        char title[24] = {};
        char line1[96] = {};
        char line2[96] = {};
        int progressPercent = -1;
        bool rebootRequired = false;
    };

    void renderScreen(uint32_t nowMs);
    void serviceReadingOrganization(uint32_t nowMs);
    void serviceLibrary();
    void enterItemActions();
    void serviceItemActions();
    void dispatchItemAction(screens::Action);
    void finishOpenedBook();
    void saveLibraryViews();
    void reconcileReadingOrder();
    void handleScreenAction(screens::Action action, uint32_t nowMs);
    void handleInput(Input::ActionMask actions, uint32_t nowMs);
    void handleTouch(uint32_t nowMs);
    void runRss();
    void runBookOpen(const LibraryView::Selection&, uint32_t nowMs);
    bool requestTypographyRefresh();
    void updateBackgroundJob();
    bool startBackgroundJob(JobKind kind);
    static void backgroundJobEntry(void* context);
    void runBackgroundJob();
    void enqueueJobUpdate(JobUpdate update, bool mustSucceed = false);
    void showTransientStatus(std::string_view title, std::string_view line1, std::string_view line2,
                             uint32_t durationMs, screens::Screen destination, int progressPercent = -1);
    void applySettings();
    void loadAppearanceSettings();
    void reloadUiAssets();
    void enterUsbTransfer(uint32_t nowMs);
    void exitUsbTransfer(screens::Screen destination = screens::Screen::Reader);
    void runOtaCheck(bool install);
    void enterStandby(uint32_t nowMs);
    void exitStandby(uint32_t nowMs);
    void lightSleepFromStandby();
    void powerOff(uint32_t nowMs);
    static void renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);
    bool backgroundJobActive() const {
        return jobKind_ != JobKind::None;
    }
    bool typographyJobActive() const {
        return jobKind_ == JobKind::Typography;
    }

    ui::Context immediateUi_{Board::Display::gfx()};
    settings::SettingsStore settingsStore_;
    locales::Catalog localeCatalog_;
    Board::Power::BatteryState battery_;
    screens::ReaderScreen readerScreen_{Board::Display::gfx(), settingsStore_.settings().reading};
    screens::LibraryScreen libraryScreen_;
    screens::ItemActionsScreen itemActions_;
    screens::ChaptersScreen chaptersScreen_;
    screens::InterfaceScreen interfaceScreen_;
    screens::NetworkScreen networkScreen_;
    StorageManager storage_;
    Preferences prefs_;
    screens::FocusScreen focusScreen_;
    CompanionApi companionApi_{settingsStore_,   storage_,       localeCatalog_, immediateUi_, readerScreen_,
                               interfaceScreen_, networkScreen_, libraryScreen_, focusScreen_};
    UsbMassStorageManager usbTransfer_;
    CompanionSerial serialCompanion_{companionApi_, usbTransfer_};
    screens::StandbyScreen standbyScreen_;
    QueueHandle_t jobQueue_ = nullptr;
    JobKind jobKind_ = JobKind::None;
    std::optional<ReadingOrganization::Request> queuedReadingCommand_;
    ReadingOrganization::Request jobReadingCommand_;
    std::error_code jobReadingError_;
    std::optional<reading::State> jobRestartState_;
    bool jobResumeReading_ = false;
    uint32_t readingOperationNonce_ = 0;
    uint64_t readingOperation_ = 0;
    size_t jobBookIndex_ = 0;
    bool jobBookLoaded_ = false;
    LibraryView::Selection jobBookSelection_;
    LibraryView::Selection pendingBookSelection_;
    std::error_code jobLibraryError_;
    bool libraryWasVisible_ = false;
    std::string readerLibraryPath_;
    bool jobItemRecovery_ = false;
    bool itemInfoPending_ = false;
    bool manualItemCommand_ = false;
    bool restartAfterOpen_ = false;
    bool readerFromLibrary_ = false;
    LibraryOrganization::Status jobItemStatus_ = LibraryOrganization::Status::unknown;
    std::error_code jobItemError_;
    LibraryView::Selection jobItemSelection_;
    ReadingOrganization::Command jobItemCommand_ = ReadingOrganization::Command::markRead;
    std::string jobItemOperation_;
    std::atomic<bool> cancelItemInfo_{false};
    std::atomic<bool> cancelLibraryPreparation_{false};
    bool bookOpenPending_ = false;
    bool typographyRefreshPending_ = false;
    bool typographyOpensBook_ = false;
    screens::Screen screen_ = screens::Screen::Status;
    screens::Screen statusDestination_ = screens::Screen::Reader;
    uint32_t bootMs_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t standbyEnteredMs_ = 0;
    uint32_t statusUntilMs_ = 0;
    bool restartAfterStatus_ = false;
};
