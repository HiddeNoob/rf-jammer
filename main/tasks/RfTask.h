#ifndef RF_TASK_H
#define RF_TASK_H

#include <cstdio>
#include <memory>
#include <vector>
#include <U8g2lib.h>
#include "Task.h"
#include "RfSweeper.h"

// Encapsulates HOW a set of RF modules gets assigned to do their job:
// split-spectrum scanning (ModeAssignmentStrategy) or lock-step jamming of
// an explicit channel list (CustomChannelAssignmentStrategy). RfTask owns
// one of these and defers to it for both "can these modules even do this"
// and "actually assign them". Everything else about a task's lifecycle
// (validate/start/pause/resume/stop) is identical no matter which strategy
// is used, so that part lives exactly once, in RfTask.
class RfAssignmentStrategy {
public:
    virtual ~RfAssignmentStrategy() = default;

    // Extra, strategy-specific precondition on top of the generic
    // module-availability checks RfTask::validate() already does - e.g.
    // "enough channels for this many modules" for a mode sweep, or
    // "at least one channel has been chosen" for a custom jam list.
    virtual bool canAssign(const std::vector<int>& moduleIds) const = 0;

    virtual bool assign(RfSweeper& sweeper, const std::vector<int>& moduleIds) const = 0;
};

// Splits a SweepMode's channel range across the selected modules. Used by
// every plain scan: Bluetooth sweep, all-channels sweep, histogram.
class ModeAssignmentStrategy : public RfAssignmentStrategy {
public:
    explicit ModeAssignmentStrategy(SweepMode mode) : mode_(mode) {}

    bool canAssign(const std::vector<int>& moduleIds) const override {
        return moduleIds.size() <= getChannelsForMode(mode_).size();
    }

    bool assign(RfSweeper& sweeper, const std::vector<int>& moduleIds) const override {
        return sweeper.assignTask(moduleIds, mode_);
    }

private:
    SweepMode mode_;
};

// Every selected module cycles through the SAME explicit channel list
// (set later - e.g. once the user has picked which WiFi networks to jam -
// via setChannels()), instead of splitting a range across modules. Used by
// WiFi jamming today; any future "jam these specific frequencies" task can
// reuse this unchanged.
class CustomChannelAssignmentStrategy : public RfAssignmentStrategy {
public:
    void setChannels(std::vector<int> channels) { channels_ = std::move(channels); }
    const std::vector<int>& channels() const { return channels_; }

    bool canAssign(const std::vector<int>& /*moduleIds*/) const override {
        return !channels_.empty();
    }

    bool assign(RfSweeper& sweeper, const std::vector<int>& moduleIds) const override {
        return sweeper.assignCustomChannels(moduleIds, channels_);
    }

private:
    std::vector<int> channels_;
};

// Common lifecycle for every task that drives RfSweeper modules directly:
// module selection, validate/start/pause/resume/stop, and a default status
// render. A new task only needs to supply a name and a strategy - and, if
// the default render doesn't fit, its own renderStatus/renderConfig.
//
// This is the piece that used to be duplicated: WifiJamTask used to
// reimplement all of validate/start/pause/resume/stop by hand just because
// it needed a different sweeper call to assign its modules. Now that
// difference is the only thing a strategy has to express, and any new
// task - jam or sweep - just plugs one in.
class RfTask : public Task {
public:
    RfTask(RfSweeper& sweeper, const char* label, std::unique_ptr<RfAssignmentStrategy> strategy)
        : sweeper_(sweeper), label_(label), strategy_(std::move(strategy)) {}

    const char* name() const override { return label_; }

    void setSelectedModuleIds(const std::vector<int>& ids) override { selectedModuleIds_ = ids; }
    std::vector<int> getSelectedModuleIds() const override { return selectedModuleIds_; }

    bool validate() const override {
        if (selectedModuleIds_.empty() || !strategy_->canAssign(selectedModuleIds_)) {
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
        const bool ok = strategy_->assign(sweeper_, selectedModuleIds_);
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
    const char* label_;
    std::unique_ptr<RfAssignmentStrategy> strategy_;
    std::vector<int> selectedModuleIds_;
};

#endif