#ifndef WIFI_JAM_TASK_H
#define WIFI_JAM_TASK_H

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>
#include <U8g2lib.h>
#include "Task.h"
#include "WifiScanner.h"

// Jams a set of RF channels derived from WiFi networks the user picked in
// the UI (see MenuUi's WiFi scan page). Unlike ChannelSweeperTask, the
// channel list isn't a fixed preset: it's supplied at configuration time via
// setSelectedChannels(), so the menu must call that before start().
class WifiJamTask : public Task {
public:
    explicit WifiJamTask(RfSweeper& sweeper) : sweeper_(sweeper) {}

    const char* name() const override { return "WiFi Jam"; }

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<WifiJamTask>(sweeper_);
    }

    WifiJamTask* asWifiJamTask() override { return this; }

    void setSelectedModuleIds(const std::vector<int>& ids) override { selectedModuleIds_ = ids; }
    std::vector<int> getSelectedModuleIds() const override { return selectedModuleIds_; }

    // Not part of the base Task interface (only WiFi jamming needs it) -
    // the menu sets this after the user confirms which networks to jam,
    // before the RF module picker and start() are reached.
    void setSelectedChannels(const std::vector<int>& channels) { selectedChannels_ = channels; }
    const std::vector<int>& getSelectedChannels() const { return selectedChannels_; }

    bool validate() const override {
        if (selectedModuleIds_.empty() || selectedChannels_.empty()) {
            return false;
        }
        for (int moduleId : selectedModuleIds_) {
            if (moduleId < 0 || moduleId >= sweeper_.getModuleCount()) {
                return false;
            }
            const RfModuleStatus status = sweeper_.getModuleStatus(moduleId);
            if (!status.available || status.assigned) {
                return false;
            }
        }
        return true;
    }

    bool start() override {
        if (!validate()) {
            setStatus(TaskStatus::FAILED);
            return false;
        }
        const bool ok = sweeper_.assignCustomChannels(selectedModuleIds_, selectedChannels_);
        setStatus(ok ? TaskStatus::RUNNING : TaskStatus::FAILED);
        return ok;
    }

    void stop() override {
        sweeper_.stopTask(selectedModuleIds_);
        setStatus(TaskStatus::STOPPED);
    }

    bool pause() override {
        if (status() != TaskStatus::RUNNING) return false;
        sweeper_.pauseTask(selectedModuleIds_);
        setStatus(TaskStatus::PAUSED);
        return true;
    }

    bool resume() override {
        if (status() != TaskStatus::PAUSED) return false;
        sweeper_.resumeTask(selectedModuleIds_);
        setStatus(TaskStatus::RUNNING);
        return true;
    }

    void renderStatus(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, "WiFi Jam");
        char line[24];
        snprintf(line, sizeof(line), "%d channel(s)", static_cast<int>(selectedChannels_.size()));
        display.drawStr(0, 24, line);
        snprintf(line, sizeof(line), "%d module(s)", static_cast<int>(selectedModuleIds_.size()));
        display.drawStr(0, 36, line);
        display.drawStr(0, 48, status() == TaskStatus::PAUSED ? "PAUSED" : "JAMMING");
    }

    void renderConfig(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, "WiFi Jam");
        for (size_t i = 0; i < selectedChannels_.size() && i < 3; ++i) {
            char line[16];
            snprintf(line, sizeof(line), "Ch %d", selectedChannels_[i]);
            display.drawStr(0, 24 + static_cast<int>(i) * 10, line);
        }
    }

private:
    RfSweeper& sweeper_;
    std::vector<int> selectedModuleIds_;
    std::vector<int> selectedChannels_;
};

#endif