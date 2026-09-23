#ifndef MENU_UI_H
#define MENU_UI_H

#include <array>
#include <memory>
#include <vector>
#include "Arduino.h"
#include <GEM_u8g2.h>
#include <U8g2lib.h>
#include "RfSweeper.h"
#include "WifiScanner.h"
#include "tasks/Task.h"
#include "tasks/ChannelSweeperTask.h"
#include "tasks/HistogramGraphTask.h"
#include "tasks/WifiJamTask.h"

// Maximum number of scanned WiFi networks shown at once on the WiFi Jam
// channel picker. GEM pages need a fixed set of items, so slots beyond the
// scan result count are simply hidden (same pattern as the history page).
inline constexpr size_t MAX_WIFI_SCAN_RESULTS = 16;

class MenuUi {
public:
    explicit MenuUi(RfSweeper& sweeper, TaskRegistry* registry = nullptr, TaskHistory* history = nullptr);
    void run();

    void addTask(std::shared_ptr<Task> task);
    void addRunningTask(std::shared_ptr<Task> task);
    void addHistoryEntry(const std::string& name, TaskStatus status, const std::string& message);
    void renderRunningTasks();

private:
    static void chooseTask(GEMCallbackData data);
    static void showRunningTaskStatus(GEMCallbackData data);
    static void showSelectedTaskScreen();
    static void toggleTaskPause();
    static void stopSelectedTask();
    static void showRfStatus();
    static void toggleSelectedModule(GEMCallbackData data);
    static void confirmSelectedTask();
    static void showInfo();
    static void toggleSelectedNetwork(GEMCallbackData data);
    static void confirmWifiChannels();
    static MenuUi* activeUi;

    void initializeMenu();
    void updateModuleStatusItems();
    void handleButtons();
    void clearTaskOverlay();
    void drawTaskStatusSection();
    void drawTaskHistorySection();
    void renderTaskStatusForCurrentSelection();
    void refreshTaskPages();

    void buildCreateTaskPage();
    void buildRunningTasksPage();
    void buildHistoryPage();
    void buildWifiScanPage();
    void beginWifiScan();
    void refreshWifiScanItems();

    RfSweeper& sweeper;
    TaskRegistry* taskRegistry;
    TaskHistory* taskHistory;
    std::vector<std::shared_ptr<Task>> runningTasks;
    std::vector<std::shared_ptr<Task>> selectedTasks;
    std::vector<std::unique_ptr<GEMItem>> taskItems;
    std::vector<std::unique_ptr<GEMItem>> runningTaskItems;
    std::vector<std::unique_ptr<GEMItem>> historyItems;
    std::array<std::array<char, 40>, 8> historyTitles{};
    std::vector<std::unique_ptr<GEMItem>> moduleItems;
    std::vector<std::array<char, 50>> moduleStatusTitles;
    std::vector<bool> selectedModules;
    std::shared_ptr<Task> selectedTaskForEdit;

    // WiFi Jam flow state: which task is being configured, what the last
    // scan found, and which of those results the user has checked.
    WifiJamTask* pendingWifiJamTask = nullptr;
    std::vector<WifiNetwork> scannedNetworks;
    std::vector<bool> selectedNetworks;
    std::vector<std::unique_ptr<GEMItem>> wifiNetworkItems;
    std::array<std::array<char, 40>, MAX_WIFI_SCAN_RESULTS> wifiNetworkTitles{};

    bool historyBuilt = false;
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C display;
    GEMPage mainPage;
    GEMPage createTaskPage;
    GEMPage runningTasksPage;
    GEMPage taskControlsPage;
    GEMPage taskStatusPage;
    GEMPage taskHistoryPage;
    GEMPage rfModulesPage;
    GEMPage moduleStatusPage;
    GEMPage wifiScanPage;
    GEMItem createTaskItem;
    GEMItem runningTasksItem;
    GEMItem taskPauseItem;
    GEMItem taskStopItem;
    GEMItem taskViewItem;
    GEMItem taskHistoryItem;
    GEMItem rfModulesItem;
    GEMItem statusItem;
    GEMItem confirmTaskItem;
    GEMItem infoItem;
    GEMItem wifiScanConfirmItem;
    GEM_u8g2 menu;
};

#endif
