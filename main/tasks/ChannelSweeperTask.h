#ifndef CHANNEL_SWEEPER_TASK_H
#define CHANNEL_SWEEPER_TASK_H

#include <algorithm>
#include <memory>
#include <vector>
#include <U8g2lib.h>
#include "Task.h"

class ChannelSweeperTask : public Task {
public:
    ChannelSweeperTask(RfSweeper& sweeper, SweepMode mode, const char* label)
        : sweeper_(sweeper), mode_(mode), label_(label) {}

    const char* name() const override { return label_; }

    void setSelectedModuleIds(const std::vector<int>& ids) override { selectedModuleIds_ = ids; }
    std::vector<int> getSelectedModuleIds() const override { return selectedModuleIds_; }

    bool validate() const override {
        if (selectedModuleIds_.empty()) {
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

        const bool ok = sweeper_.assignTask(selectedModuleIds_, mode_);
        setStatus(ok ? TaskStatus::RUNNING : TaskStatus::FAILED);
        return ok;
    }

    void stop() override {
        setStatus(TaskStatus::STOPPED);
    }

    void renderStatus(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, label_);
        display.drawStr(0, 24, selectedModuleIds_.empty() ? "No modules" : "Modules selected");
    }

    void renderConfig(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, label_);
        for (size_t i = 0; i < selectedModuleIds_.size() && i < 4; ++i) {
            char line[16];
            snprintf(line, sizeof(line), "RF %d", selectedModuleIds_[i]);
            display.drawStr(0, 24 + static_cast<int>(i) * 10, line);
        }
    }

protected:
    RfSweeper& sweeper_;
    SweepMode mode_;
    const char* label_;
    std::vector<int> selectedModuleIds_;
};

class BluetoothSweepTask : public ChannelSweeperTask {
public:
    explicit BluetoothSweepTask(RfSweeper& sweeper)
        : ChannelSweeperTask(sweeper, SweepMode::BLUETOOTH, "Bluetooth Sweep") {}

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<BluetoothSweepTask>(sweeper_);
    }
};

class AllChannelsSweepTask : public ChannelSweeperTask {
public:
    explicit AllChannelsSweepTask(RfSweeper& sweeper)
        : ChannelSweeperTask(sweeper, SweepMode::ALL_CHANNELS, "All Channels Sweep") {}

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<AllChannelsSweepTask>(sweeper_);
    }
};

#endif
