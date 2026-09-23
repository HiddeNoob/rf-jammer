#ifndef HISTOGRAM_GRAPH_TASK_H
#define HISTOGRAM_GRAPH_TASK_H

#include <algorithm>
#include <memory>
#include <vector>
#include <U8g2lib.h>
#include "Task.h"

class HistogramGraphTask : public Task {
public:
    explicit HistogramGraphTask(RfSweeper& sweeper)
        : sweeper_(sweeper) {}

    const char* name() const override { return "Histogram Drawer"; }

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<HistogramGraphTask>(sweeper_);
    }

    bool allowsBusyModuleSelection() const override { return true; }

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
            // Must be online AND actively assigned to a sweep - a free
            // module has no live data, so picking one would just show a
            // permanently empty graph.
            if (!status.available || !status.assigned) {
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
        setStatus(TaskStatus::RUNNING);
        return true;
    }

    void stop() override {
        setStatus(TaskStatus::STOPPED);
    }

    bool pause() override {
        if (status() != TaskStatus::RUNNING) return false;
        setStatus(TaskStatus::PAUSED);
        return true;
    }

    bool resume() override {
        if (status() != TaskStatus::PAUSED) return false;
        setStatus(TaskStatus::RUNNING);
        return true;
    }

    void renderStatus(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 9, "HISTOGRAM");
        display.drawStr(76, 9, status() == TaskStatus::PAUSED ? "PAUSED" : "LIVE");

        std::vector<uint32_t> combinedHistogram(126, 0);
        for (int moduleId : selectedModuleIds_) {
            const auto moduleHistogram = sweeper_.getModuleHistogram(moduleId);
            for (size_t i = 0; i < moduleHistogram.size() && i < combinedHistogram.size(); ++i) {
                combinedHistogram[i] += moduleHistogram[i];
            }
        }

        // Aggregate into the 16 display buckets FIRST, then find the
        // tallest bucket. The old code scaled bar height against the
        // tallest single channel, but each bar sums up to 8 channels - so
        // a busy region's bucket total could be several times larger than
        // that "max", overflowing the uint8_t height/y math below and
        // pushing every bar off the top of the screen (nothing visible).
        static constexpr int kBucketCount = 16;
        static constexpr int kChannelsPerBucket = 8;
        uint32_t bucketValues[kBucketCount] = {0};
        uint32_t maxBucketValue = 0;
        for (int bucket = 0; bucket < kBucketCount; ++bucket) {
            const int bucketStart = bucket * kChannelsPerBucket;
            const int bucketEnd = std::min(bucketStart + kChannelsPerBucket,
                                            static_cast<int>(combinedHistogram.size()));
            uint32_t value = 0;
            for (int channel = bucketStart; channel < bucketEnd; ++channel) {
                value += combinedHistogram[channel];
            }
            bucketValues[bucket] = value;
            maxBucketValue = std::max(maxBucketValue, value);
        }

        display.drawFrame(0, 12, 128, 52);
        constexpr uint8_t kMaxBarHeight = 42;
        constexpr uint8_t kBaselineY = 61;
        for (int bucket = 0; bucket < kBucketCount; ++bucket) {
            uint8_t height = 0;
            if (maxBucketValue > 0) {
                const uint32_t scaled = (bucketValues[bucket] * kMaxBarHeight) / maxBucketValue;
                height = static_cast<uint8_t>(std::min<uint32_t>(scaled, kMaxBarHeight));
                if (bucketValues[bucket] > 0 && height == 0) {
                    height = 1;  // any real detection stays visible, even if tiny
                }
            }
            const uint8_t x = 4 + bucket * 7;
            const uint8_t y = kBaselineY - height;
            display.drawBox(x, y, 5, height > 0 ? height : 1);
        }
    }

    void renderConfig(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 12, "Histogram Drawer");
        for (size_t i = 0; i < selectedModuleIds_.size() && i < 4; ++i) {
            char line[16];
            snprintf(line, sizeof(line), "RF %d", selectedModuleIds_[i]);
            display.drawStr(0, 24 + static_cast<int>(i) * 10, line);
        }
    }

private:
    RfSweeper& sweeper_;
    std::vector<int> selectedModuleIds_;
};

#endif