#include "MenuUi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#include <Wire.h>
#include "driver/gpio.h"
#include "esp_log.h"

namespace {
constexpr char TAG[] = "menu_ui";
constexpr gpio_num_t OLED_SDA = GPIO_NUM_21;
constexpr gpio_num_t OLED_SCL = GPIO_NUM_20;
constexpr gpio_num_t BTN_UP_PIN = GPIO_NUM_16;
constexpr gpio_num_t BTN_DOWN_PIN = GPIO_NUM_17;
constexpr gpio_num_t BTN_SELECT_PIN = GPIO_NUM_18;
constexpr uint8_t OLED_ADDRESS_PRIMARY = 0x3C;
constexpr uint8_t OLED_ADDRESS_ALTERNATE = 0x3D;
}

MenuUi* MenuUi::activeUi = nullptr;

MenuUi::MenuUi(RfSweeper& sweeper, TaskRegistry* registry, TaskHistory* history)
    : sweeper(sweeper),
      taskRegistry(registry),
      taskHistory(history),
      display(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA),
      mainPage("RF Menu"),
            createTaskPage("Create Task", mainPage),
            runningTasksPage("Current Running Tasks", mainPage),
            taskControlsPage("Task Controls", runningTasksPage),
            taskStatusPage("Task Screen", taskControlsPage),
            taskHistoryPage("Task History", mainPage),
            rfModulesPage("RF Modules", createTaskPage),
            moduleStatusPage("RF Status", rfModulesPage),
            wifiScanPage("WiFi Networks", createTaskPage),
            createTaskItem("Create Task", createTaskPage),
            runningTasksItem("Current Running Tasks", runningTasksPage),
            taskPauseItem("Pause Task", toggleTaskPause),
            taskStopItem("Stop Task", stopSelectedTask),
            taskViewItem("View Task Screen", showSelectedTaskScreen),
            taskHistoryItem("Task History", taskHistoryPage),
            rfModulesItem("RF Modules", rfModulesPage),
            statusItem("Current Status", showRfStatus),
            confirmTaskItem("Confirm RF Selection", confirmSelectedTask),
            infoItem("System Information", showInfo),
            wifiScanConfirmItem("Confirm Channels", confirmWifiChannels),
      menu(display, GEM_POINTER_ROW, GEM_ITEMS_COUNT_AUTO) {
    if (taskRegistry == nullptr) {
        static TaskRegistry defaultRegistry;
        taskRegistry = &defaultRegistry;
    }
    if (taskHistory == nullptr) {
        static TaskHistory defaultHistory;
        taskHistory = &defaultHistory;
    }

    taskRegistry->addTask(std::make_shared<BluetoothSweepTask>(sweeper));
    taskRegistry->addTask(std::make_shared<AllChannelsSweepTask>(sweeper));
    taskRegistry->addTask(std::make_shared<HistogramGraphTask>(sweeper));
    taskRegistry->addTask(std::make_shared<WifiJamTask>(sweeper));
}

void MenuUi::run() {
    activeUi = this;
    ESP_LOGI(TAG, "Menu UI task started.");

    initArduino();
    ESP_LOGI(TAG, "Arduino initialized. Starting I2C on SDA=%d, SCL=%d.",
             OLED_SDA, OLED_SCL);
    Wire.begin(OLED_SDA, OLED_SCL);
    bool oledDetected = false;
    for (const uint8_t address : {OLED_ADDRESS_PRIMARY, OLED_ADDRESS_ALTERNATE}) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            oledDetected = true;
            break;
        }
    }
    if (!oledDetected || display.begin() == 0) {
        ESP_LOGE(TAG, "OLED initialization failed: no responding SSD1306 found.");
        return;
    }
    ESP_LOGI(TAG, "OLED initialized.");

    gpio_config_t ioConfig = {};
    ioConfig.intr_type = GPIO_INTR_DISABLE;
    ioConfig.mode = GPIO_MODE_INPUT;
    ioConfig.pin_bit_mask = (1ULL << BTN_UP_PIN) | (1ULL << BTN_DOWN_PIN)
        | (1ULL << BTN_SELECT_PIN);
    ioConfig.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&ioConfig);
    ESP_LOGI(TAG, "Buttons initialized: UP=%d, DOWN=%d, SELECT=%d.",
             BTN_UP_PIN, BTN_DOWN_PIN, BTN_SELECT_PIN);

    initializeMenu();
    ESP_LOGI(TAG, "Menu initialized. Entering event loop.");

    int heartbeatCount = 0;
    for (;;) {
        handleButtons();
        drawTaskStatusSection();
        vTaskDelay(pdMS_TO_TICKS(20));

        if (++heartbeatCount >= 250) {
            ESP_LOGI(TAG, "Menu UI is running.");
            heartbeatCount = 0;
        }
    }
}

void MenuUi::initializeMenu() {
    ESP_LOGI(TAG, "Adding menu items.");

    mainPage.addMenuItem(createTaskItem);
    mainPage.addMenuItem(runningTasksItem);
    mainPage.addMenuItem(taskHistoryItem);
    mainPage.addMenuItem(infoItem);

    buildCreateTaskPage();
    buildRunningTasksPage();
    taskControlsPage.addMenuItem(taskViewItem);
    taskControlsPage.addMenuItem(taskPauseItem);
    taskControlsPage.addMenuItem(taskStopItem);
    buildHistoryPage();
    historyBuilt = true;
    buildWifiScanPage();

    moduleItems.reserve(sweeper.getModuleCount());
    moduleStatusTitles.resize(sweeper.getModuleCount());
    selectedModules.assign(sweeper.getModuleCount(), false);
    for (int moduleId = 0; moduleId < sweeper.getModuleCount(); ++moduleId) {
        moduleItems.push_back(std::make_unique<GEMItem>(
            "RF", toggleSelectedModule, moduleId));
        moduleStatusPage.addMenuItem(*moduleItems.back());
    }
    moduleStatusPage.addMenuItem(confirmTaskItem);
    updateModuleStatusItems();

    menu.setMenuPageCurrent(mainPage);
    menu.init();
    menu.drawMenu();
}

void MenuUi::clearTaskOverlay() {
    selectedTaskForEdit = nullptr;
    display.clearBuffer();
}

void MenuUi::updateModuleStatusItems() {
    for (int moduleId = 0; moduleId < sweeper.getModuleCount(); ++moduleId) {
        const RfModuleStatus status = sweeper.getModuleStatus(moduleId);
        const char* taskName = "FREE";
        if (!status.available) {
            taskName = "OFFLINE";
        } else if (status.assigned) {
            switch (status.task) {
                case SweepMode::BLUETOOTH: taskName = "BLUETOOTH"; break;
                case SweepMode::ALL_CHANNELS: taskName = "ALL CHANNELS"; break;
                case SweepMode::WIFI_JAM: taskName = "WIFI JAM"; break;
            }
        }

        snprintf(moduleStatusTitles[moduleId].data(), moduleStatusTitles[moduleId].size(),
                 "RF %d %s %s", moduleId,
                 selectedModules[moduleId] ? "[SELECTED] " : "", taskName);
        moduleItems[moduleId]->setTitle(moduleStatusTitles[moduleId].data());
    }
}

void MenuUi::chooseTask(GEMCallbackData data) {
    if (activeUi == nullptr) {
        return;
    }

    if (data.valInt < 0 || static_cast<size_t>(data.valInt) >= activeUi->taskRegistry->tasks().size()) {
        return;
    }

    const auto selected = activeUi->taskRegistry->tasks()[data.valInt];

    // A task that is already running/paused must not be reconfigured and
    // re-started from here - that used to silently call start() again on a
    // live task. Send the user to its controls (pause/stop/view) instead.
    if (selected->status() == TaskStatus::RUNNING || selected->status() == TaskStatus::PAUSED) {
        activeUi->selectedTaskForEdit = selected;
        activeUi->taskPauseItem.setTitle(
            selected->status() == TaskStatus::PAUSED ? "Resume Task" : "Pause Task");
        activeUi->display.clearBuffer();
        activeUi->menu.setMenuPageCurrent(activeUi->taskControlsPage);
        activeUi->menu.drawMenu();
        return;
    }

    activeUi->selectedTaskForEdit = selected;
    activeUi->selectedModules.assign(activeUi->sweeper.getModuleCount(), false);
    const auto savedSelection = selected->getSelectedModuleIds();
    for (int moduleId : savedSelection) {
        if (moduleId >= 0 && moduleId < static_cast<int>(activeUi->selectedModules.size())) {
            activeUi->selectedModules[moduleId] = true;
        }
    }
    activeUi->updateModuleStatusItems();
    activeUi->display.clearBuffer();

    // WiFi Jam needs an extra step first: pick which networks/channels to
    // target before picking the RF module(s).
    if (auto* wifiTask = selected->asWifiJamTask()) {
        activeUi->pendingWifiJamTask = wifiTask;
        activeUi->beginWifiScan();
        activeUi->menu.setMenuPageCurrent(activeUi->wifiScanPage);
    } else {
        activeUi->menu.setMenuPageCurrent(activeUi->moduleStatusPage);
    }
    activeUi->menu.drawMenu();
}

void MenuUi::showRunningTaskStatus(GEMCallbackData data) {
    if (activeUi == nullptr) {
        return;
    }

    if (data.valInt < 0 || static_cast<size_t>(data.valInt) >= activeUi->taskRegistry->tasks().size()) {
        return;
    }

    activeUi->selectedTaskForEdit = activeUi->taskRegistry->tasks()[data.valInt];
    if (activeUi->selectedTaskForEdit == nullptr ||
        (activeUi->selectedTaskForEdit->status() != TaskStatus::RUNNING &&
         activeUi->selectedTaskForEdit->status() != TaskStatus::PAUSED)) {
        return;
    }
    activeUi->display.clearBuffer();
    activeUi->taskPauseItem.setTitle(
        activeUi->selectedTaskForEdit->status() == TaskStatus::PAUSED
            ? "Resume Task" : "Pause Task");
    activeUi->menu.setMenuPageCurrent(activeUi->taskControlsPage);
    activeUi->menu.drawMenu();
}

void MenuUi::showSelectedTaskScreen() {
    if (activeUi == nullptr || activeUi->selectedTaskForEdit == nullptr) {
        return;
    }
    activeUi->menu.setMenuPageCurrent(activeUi->taskStatusPage);
    activeUi->menu.drawMenu();
    activeUi->drawTaskStatusSection();
}

void MenuUi::toggleTaskPause() {
    if (activeUi == nullptr || activeUi->selectedTaskForEdit == nullptr) {
        return;
    }

    const bool changed = activeUi->selectedTaskForEdit->status() == TaskStatus::PAUSED
        ? activeUi->selectedTaskForEdit->resume()
        : activeUi->selectedTaskForEdit->pause();
    if (changed) {
        activeUi->addHistoryEntry(activeUi->selectedTaskForEdit->name(),
                                  activeUi->selectedTaskForEdit->status(),
                                  activeUi->selectedTaskForEdit->status() == TaskStatus::PAUSED
                                      ? "Task paused" : "Task resumed");
        activeUi->taskPauseItem.setTitle(
            activeUi->selectedTaskForEdit->status() == TaskStatus::PAUSED
                ? "Resume Task" : "Pause Task");
        activeUi->refreshTaskPages();
        activeUi->menu.drawMenu();
    }
}

void MenuUi::stopSelectedTask() {
    if (activeUi == nullptr || activeUi->selectedTaskForEdit == nullptr) {
        return;
    }

    activeUi->selectedTaskForEdit->stop();
    activeUi->addHistoryEntry(activeUi->selectedTaskForEdit->name(),
                              TaskStatus::STOPPED, "Task stopped");
    activeUi->refreshTaskPages();
    activeUi->selectedTaskForEdit = nullptr;
    activeUi->display.clearBuffer();
    activeUi->menu.setMenuPageCurrent(activeUi->runningTasksPage);
    activeUi->menu.drawMenu();
}

void MenuUi::showRfStatus() {
    if (activeUi != nullptr) {
        activeUi->clearTaskOverlay();
        activeUi->updateModuleStatusItems();
        activeUi->display.clearBuffer();
        activeUi->menu.setMenuPageCurrent(activeUi->moduleStatusPage);
        activeUi->menu.drawMenu();
        ESP_LOGI(TAG, "RF module status displayed.");
    }
}

void MenuUi::toggleSelectedModule(GEMCallbackData data) {
    if (activeUi == nullptr) {
        return;
    }

    const int moduleId = data.valInt;
    const RfModuleStatus status = activeUi->sweeper.getModuleStatus(moduleId);
    const bool allowBusyModule = activeUi->selectedTaskForEdit != nullptr &&
                                  activeUi->selectedTaskForEdit->allowsBusyModuleSelection();
    if (!status.available || (status.assigned && !allowBusyModule)) {
        ESP_LOGW(TAG, "RF module %d is not available for selection.", moduleId);
        return;
    }

    activeUi->selectedModules[moduleId] = !activeUi->selectedModules[moduleId];
    if (activeUi->selectedTaskForEdit != nullptr) {
        std::vector<int> selectedIds;
        for (size_t index = 0; index < activeUi->selectedModules.size(); ++index) {
            if (activeUi->selectedModules[index]) {
                selectedIds.push_back(static_cast<int>(index));
            }
        }
        activeUi->selectedTaskForEdit->setSelectedModuleIds(selectedIds);
    }
    activeUi->updateModuleStatusItems();
    activeUi->display.clearBuffer();
    activeUi->menu.drawMenu();
}

void MenuUi::confirmSelectedTask() {
    if (activeUi != nullptr && activeUi->selectedTaskForEdit != nullptr) {
        std::vector<int> moduleIds;
        for (size_t moduleId = 0; moduleId < activeUi->selectedModules.size(); ++moduleId) {
            if (activeUi->selectedModules[moduleId]) {
                moduleIds.push_back(static_cast<int>(moduleId));
            }
        }

        activeUi->selectedTaskForEdit->setSelectedModuleIds(moduleIds);
        const bool valid = activeUi->selectedTaskForEdit->validate();
        if (!valid) {
            activeUi->selectedTaskForEdit->setStatus(TaskStatus::FAILED);
            activeUi->addHistoryEntry(activeUi->selectedTaskForEdit->name(), TaskStatus::FAILED, "Validation failed");
            ESP_LOGW(TAG, "Task validation failed for %s.", activeUi->selectedTaskForEdit->name());
        } else {
            const bool started = activeUi->selectedTaskForEdit->start();
            activeUi->selectedTaskForEdit->setStatus(started ? TaskStatus::RUNNING : TaskStatus::FAILED);
            if (started) {
                activeUi->addRunningTask(activeUi->selectedTaskForEdit);
            }
            activeUi->addHistoryEntry(activeUi->selectedTaskForEdit->name(), started ? TaskStatus::RUNNING : TaskStatus::FAILED,
                                     started ? "Task started" : "Task start failed");
            ESP_LOGI(TAG, "RF selection confirmation %s.", started ? "accepted" : "rejected");
        }

        activeUi->selectedTaskForEdit = nullptr;
        activeUi->pendingWifiJamTask = nullptr;
        activeUi->selectedModules.assign(activeUi->sweeper.getModuleCount(), false);
        activeUi->updateModuleStatusItems();
        activeUi->refreshTaskPages();
        activeUi->display.clearBuffer();
        activeUi->menu.setMenuPageCurrent(activeUi->mainPage);
        activeUi->menu.drawMenu();
    }
}

void MenuUi::handleButtons() {
    if (gpio_get_level(BTN_UP_PIN) == 0) {
        ESP_LOGI(TAG, "UP button pressed.");
        menu.registerKeyPress(GEM_KEY_UP);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (gpio_get_level(BTN_DOWN_PIN) == 0) {
        ESP_LOGI(TAG, "DOWN button pressed.");
        menu.registerKeyPress(GEM_KEY_DOWN);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (gpio_get_level(BTN_SELECT_PIN) == 0) {
        ESP_LOGI(TAG, "SELECT button pressed.");
        menu.registerKeyPress(GEM_KEY_OK);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void MenuUi::drawTaskStatusSection() {
    if (activeUi == nullptr || activeUi->selectedTaskForEdit == nullptr) {
        return;
    }

    // NOTE: relies on GEM_u8g2::getCurrentMenuPage() returning a GEMPage*.
    // If your GEM version exposes it differently (e.g. by reference),
    // adjust this comparison to match.
    if (activeUi->menu.getCurrentMenuPage() != &activeUi->taskStatusPage) {
        return;
    }

    activeUi->display.clearBuffer();
    activeUi->selectedTaskForEdit->renderStatus(activeUi->display);
    activeUi->display.sendBuffer();
}

void MenuUi::drawTaskHistorySection() {
    if (activeUi == nullptr || activeUi->taskHistory == nullptr) {
        return;
    }

    activeUi->display.setDrawColor(1);
    activeUi->display.drawFrame(0, 48, 128, 16);
    activeUi->display.setFont(u8g2_font_6x10_tr);
    activeUi->display.drawStr(2, 58, "HISTORY");
    const auto& entries = activeUi->taskHistory->entries();
    for (size_t i = 0; i < entries.size() && i < 2; ++i) {
        const auto& entry = entries[entries.size() - 1 - i];
        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%s %d", entry.taskName.c_str(), static_cast<int>(entry.status));
        activeUi->display.drawStr(2, 70 + static_cast<int>(i) * 8, buffer);
    }
    activeUi->display.sendBuffer();
}

void MenuUi::renderRunningTasks() {
    if (activeUi == nullptr) {
        return;
    }

    for (const auto& task : activeUi->runningTasks) {
        if (task != nullptr) {
            task->renderStatus(activeUi->display);
        }
    }
}

void MenuUi::renderTaskStatusForCurrentSelection() {
    if (activeUi != nullptr && activeUi->selectedTaskForEdit != nullptr) {
        activeUi->selectedTaskForEdit->renderConfig(activeUi->display);
    }
}

void MenuUi::buildCreateTaskPage() {
    if (taskRegistry != nullptr) {
        for (size_t index = 0; index < taskRegistry->tasks().size(); ++index) {
            taskItems.push_back(std::make_unique<GEMItem>(taskRegistry->tasks()[index]->name(), chooseTask, static_cast<int>(index)));
            createTaskPage.addMenuItem(*taskItems.back());
        }
    }
    rfModulesPage.addMenuItem(statusItem);
}

void MenuUi::buildRunningTasksPage() {
    if (taskRegistry == nullptr) {
        return;
    }

    for (size_t index = 0; index < taskRegistry->tasks().size(); ++index) {
        const auto& task = taskRegistry->tasks()[index];
        if (task == nullptr) {
            continue;
        }

        runningTaskItems.push_back(std::make_unique<GEMItem>(task->name(), showRunningTaskStatus, static_cast<int>(index)));
        runningTasksPage.addMenuItem(*runningTaskItems.back());
        if (task->status() != TaskStatus::RUNNING && task->status() != TaskStatus::PAUSED) {
            runningTaskItems.back()->hide();
        }
    }
}

void MenuUi::buildHistoryPage() {
    if (historyBuilt) {
        return;
    }

    historyItems.reserve(historyTitles.size());
    for (size_t index = 0; index < historyTitles.size(); ++index) {
        snprintf(historyTitles[index].data(), historyTitles[index].size(), "No task history");
        historyItems.push_back(std::make_unique<GEMItem>(historyTitles[index].data(), []() {}));
        taskHistoryPage.addMenuItem(*historyItems.back());
        historyItems.back()->hide();
    }
}

void MenuUi::buildWifiScanPage() {
    wifiNetworkItems.reserve(MAX_WIFI_SCAN_RESULTS);
    for (size_t index = 0; index < MAX_WIFI_SCAN_RESULTS; ++index) {
        snprintf(wifiNetworkTitles[index].data(), wifiNetworkTitles[index].size(), "-- empty --");
        wifiNetworkItems.push_back(std::make_unique<GEMItem>(
            wifiNetworkTitles[index].data(), toggleSelectedNetwork, static_cast<int>(index)));
        wifiScanPage.addMenuItem(*wifiNetworkItems.back());
        wifiNetworkItems.back()->hide();
    }
    wifiScanPage.addMenuItem(wifiScanConfirmItem);
}

void MenuUi::beginWifiScan() {
    display.clearBuffer();
    display.setDrawColor(1);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 32, "Scanning WiFi...");
    display.sendBuffer();

    scannedNetworks = scanWifiNetworks();
    if (scannedNetworks.size() > MAX_WIFI_SCAN_RESULTS) {
        scannedNetworks.resize(MAX_WIFI_SCAN_RESULTS);
    }
    selectedNetworks.assign(MAX_WIFI_SCAN_RESULTS, false);
    refreshWifiScanItems();
}

void MenuUi::refreshWifiScanItems() {
    for (size_t index = 0; index < MAX_WIFI_SCAN_RESULTS; ++index) {
        if (index < scannedNetworks.size()) {
            const auto& network = scannedNetworks[index];
            snprintf(wifiNetworkTitles[index].data(), wifiNetworkTitles[index].size(),
                     "%s%.*s Ch%d", selectedNetworks[index] ? "[X] " : "[ ] ",
                     28, network.ssid.empty() ? "(hidden)" : network.ssid.c_str(), network.channel);
            wifiNetworkItems[index]->setTitle(wifiNetworkTitles[index].data());
            wifiNetworkItems[index]->show();
        } else {
            wifiNetworkItems[index]->hide();
        }
    }
}

void MenuUi::toggleSelectedNetwork(GEMCallbackData data) {
    if (activeUi == nullptr) {
        return;
    }

    const int slot = data.valInt;
    if (slot < 0 || static_cast<size_t>(slot) >= activeUi->scannedNetworks.size()) {
        return;
    }

    activeUi->selectedNetworks[slot] = !activeUi->selectedNetworks[slot];
    activeUi->refreshWifiScanItems();
    activeUi->menu.drawMenu();
}

void MenuUi::confirmWifiChannels() {
    if (activeUi == nullptr || activeUi->pendingWifiJamTask == nullptr) {
        return;
    }

    std::vector<int> channels;
    for (size_t index = 0; index < activeUi->scannedNetworks.size(); ++index) {
        if (!activeUi->selectedNetworks[index]) {
            continue;
        }
        const int channel = wifiChannelToRfOffset(activeUi->scannedNetworks[index].channel);
        if (std::find(channels.begin(), channels.end(), channel) == channels.end()) {
            channels.push_back(channel);
        }
    }

    if (channels.empty()) {
        ESP_LOGW(TAG, "No WiFi network selected for jamming.");
        return;
    }

    activeUi->pendingWifiJamTask->setSelectedChannels(channels);
    activeUi->pendingWifiJamTask = nullptr;
    activeUi->display.clearBuffer();
    activeUi->menu.setMenuPageCurrent(activeUi->moduleStatusPage);
    activeUi->menu.drawMenu();
}

void MenuUi::refreshTaskPages() {
    if (taskRegistry != nullptr) {
        size_t itemIndex = 0;
        for (const auto& task : taskRegistry->tasks()) {
            if (task == nullptr || itemIndex >= runningTaskItems.size()) {
                continue;
            }
            if (task->status() == TaskStatus::RUNNING || task->status() == TaskStatus::PAUSED) {
                runningTaskItems[itemIndex]->show();
            } else {
                runningTaskItems[itemIndex]->hide();
            }
            ++itemIndex;
        }
    }

    if (taskHistory == nullptr) {
        return;
    }
    const auto& entries = taskHistory->entries();
    for (size_t index = 0; index < historyItems.size(); ++index) {
        if (index < entries.size()) {
            const auto& entry = entries[entries.size() - 1 - index];
            snprintf(historyTitles[index].data(), historyTitles[index].size(), "%s %s",
                     entry.taskName.c_str(),
                     entry.status == TaskStatus::RUNNING ? "RUN" :
                     entry.status == TaskStatus::FAILED ? "FAIL" :
                     entry.status == TaskStatus::SUCCEEDED ? "OK" : "IDLE");
            historyItems[index]->setTitle(historyTitles[index].data());
            historyItems[index]->show();
        } else {
            historyItems[index]->hide();
        }
    }
}

void MenuUi::addTask(std::shared_ptr<Task> task) {
    if (taskRegistry != nullptr && task != nullptr) {
        taskRegistry->addTask(task);
    }
}

void MenuUi::addRunningTask(std::shared_ptr<Task> task) {
    if (task == nullptr) {
        return;
    }
    for (const auto& existing : runningTasks) {
        if (existing != nullptr && std::strcmp(existing->name(), task->name()) == 0) {
            return;
        }
    }
    runningTasks.push_back(task);
}

void MenuUi::addHistoryEntry(const std::string& name, TaskStatus status, const std::string& message) {
    if (taskHistory != nullptr) {
        taskHistory->add(name, status, message);
    }
}

void MenuUi::showInfo() {
    if (activeUi != nullptr) {
        activeUi->drawTaskStatusSection();
        std::printf(">> RF sweep: %s\n", activeUi->sweeper.isRunning() ? "RUNNING" : "STOPPED");
    }
}