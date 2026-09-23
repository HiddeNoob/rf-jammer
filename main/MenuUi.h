#ifndef MENU_UI_H
#define MENU_UI_H

#include <array>
#include <memory>
#include <vector>
#include "Arduino.h"
#include <GEM_u8g2.h>
#include <U8g2lib.h>
#include "RfSweeper.h"
#include "tasks/Task.h"
#include "tasks/ChannelSweeperTask.h"
#include "tasks/HistogramGraphTask.h"

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
    static MenuUi* activeUi;

    void initializeMenu();
    void updateModuleStatusItems();
    void handleButtons();
    void pushMenuPage(GEMPage* page);
    void popMenuPage();
    void clearTaskOverlay();
    void drawTaskStatusSection();
    void drawTaskHistorySection();
    void renderTaskStatusForCurrentSelection();
    void refreshTaskPages();

    void buildCreateTaskPage();
    void buildRunningTasksPage();
    void buildHistoryPage();

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
    std::vector<GEMPage*> menuStack;
    std::vector<bool> selectedModules;
    std::shared_ptr<Task> selectedTaskForEdit;
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C display;
    GEMPage mainPage;
    GEMPage createTaskPage;
    GEMPage runningTasksPage;
    GEMPage taskControlsPage;
    GEMPage taskStatusPage;
    GEMPage taskHistoryPage;
    GEMPage rfModulesPage;
    GEMPage moduleStatusPage;
    bool historyBuilt = false;
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
    GEM_u8g2 menu;
};

#endif