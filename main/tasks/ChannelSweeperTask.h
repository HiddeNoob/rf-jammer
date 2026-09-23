#ifndef CHANNEL_SWEEPER_TASK_H
#define CHANNEL_SWEEPER_TASK_H

#include <memory>
#include "RfTask.h"

// Both of these are pure "sweep this SweepMode's channel range" tasks -
// module selection, validate/start/pause/resume/stop, and the default
// status render all come from RfTask via ModeAssignmentStrategy. There is
// nothing left to write here beyond the name and the mode.

class BluetoothSweepTask : public RfTask {
public:
    explicit BluetoothSweepTask(RfSweeper& sweeper)
        : RfTask(sweeper, "Bluetooth Sweep",
                 std::make_unique<ModeAssignmentStrategy>(SweepMode::BLUETOOTH)) {}

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<BluetoothSweepTask>(sweeper_);
    }
};

class AllChannelsSweepTask : public RfTask {
public:
    explicit AllChannelsSweepTask(RfSweeper& sweeper)
        : RfTask(sweeper, "All Channels Sweep",
                 std::make_unique<ModeAssignmentStrategy>(SweepMode::ALL_CHANNELS)) {}

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<AllChannelsSweepTask>(sweeper_);
    }
};

#endif