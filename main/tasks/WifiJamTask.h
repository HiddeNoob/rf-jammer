#ifndef WIFI_JAM_TASK_H
#define WIFI_JAM_TASK_H

#include <cstdio>
#include <memory>
#include <vector>
#include <U8g2lib.h>
#include "RfTask.h"
#include "WifiScanner.h"

// Jams a set of RF channels derived from WiFi networks the user picked in
// the UI (see MenuUi's WiFi scan page). The only thing that makes this
// different from a plain sweep task is WHICH channels get assigned and HOW
// (the same explicit list to every module, instead of splitting a range) -
// both expressed entirely by CustomChannelAssignmentStrategy. Everything
// else (module selection, validate/start/pause/resume/stop) is inherited,
// unchanged, from RfTask.
class WifiJamTask : public RfTask {
public:
    explicit WifiJamTask(RfSweeper& sweeper)
        : RfTask(sweeper, "WiFi Jam", std::make_unique<CustomChannelAssignmentStrategy>()),
          channelStrategy_(static_cast<CustomChannelAssignmentStrategy*>(strategy_.get())) {}

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<WifiJamTask>(sweeper_);
    }

    WifiJamTask* asWifiJamTask() override { return this; }

    // Not part of the base Task interface (only WiFi jamming needs it) -
    // the menu sets this after the user confirms which networks to jam,
    // before the RF module picker and start() are reached.
    void setSelectedChannels(const std::vector<int>& channels) {
        channelStrategy_->setChannels(channels);
    }
    const std::vector<int>& getSelectedChannels() const { return channelStrategy_->channels(); }

    void renderStatus(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, "WiFi Jam");
        char line[24];
        const auto& channels = channelStrategy_->channels();
        snprintf(line, sizeof(line), "%d channel(s)", static_cast<int>(channels.size()));
        display.drawStr(0, 24, line);
        snprintf(line, sizeof(line), "%d module(s)", static_cast<int>(selectedModuleIds_.size()));
        display.drawStr(0, 36, line);
        display.drawStr(0, 48, status() == TaskStatus::PAUSED ? "PAUSED" : "JAMMING");
    }

    void renderConfig(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, "WiFi Jam");
        const auto& channels = channelStrategy_->channels();
        for (size_t i = 0; i < channels.size() && i < 3; ++i) {
            char line[16];
            snprintf(line, sizeof(line), "Ch %d", channels[i]);
            display.drawStr(0, 24 + static_cast<int>(i) * 10, line);
        }
    }

private:
    // Owned by strategy_ in the base class; kept here typed so
    // setSelectedChannels()/getSelectedChannels() and rendering can reach
    // the channel list without every caller needing an
    // RfAssignmentStrategy-vs-CustomChannelAssignmentStrategy cast.
    CustomChannelAssignmentStrategy* channelStrategy_;
};

#endif