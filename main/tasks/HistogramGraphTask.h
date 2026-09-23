#ifndef HISTOGRAM_GRAPH_TASK_H
#define HISTOGRAM_GRAPH_TASK_H

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>
#include <U8g2lib.h>
#include "RfTask.h"

// AllChannelsSweepTask ile birebir aynı atama kuralına tabidir (ALL_CHANNELS
// modu, ModeAssignmentStrategy üzerinden) - tek farkı sonucu grafik olarak
// çizmesidir. validate/start/stop/pause/resume RfTask'tan geliyor, burada
// sadece renderStatus/renderConfig override ediliyor.
class HistogramGraphTask : public RfTask {
public:
    explicit HistogramGraphTask(RfSweeper& sweeper)
        : RfTask(sweeper, "Histogram Drawer",
                 std::make_unique<ModeAssignmentStrategy>(SweepMode::ALL_CHANNELS)) {}

    std::shared_ptr<Task> clone() const override {
        return std::make_shared<HistogramGraphTask>(sweeper_);
    }

    void renderStatus(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) override {
        display.setDrawColor(1);
        display.setFont(u8g2_font_6x10_tr);

        std::vector<uint32_t> combinedHistogram(126, 0);
        for (int moduleId : selectedModuleIds_) {
            const auto moduleHistogram = sweeper_.getModuleHistogram(moduleId);
            for (size_t i = 0; i < moduleHistogram.size() && i < combinedHistogram.size(); ++i) {
                combinedHistogram[i] += moduleHistogram[i];
            }
        }

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

        display.setFont(u8g2_font_4x6_tr);
        display.drawStr(1, 4, "8 MHz / bar");
        display.drawFrame(0, 15, 128, 48);
        constexpr uint8_t kMaxBarHeight = 42;
        constexpr uint8_t kBaselineY = 60;
        for (int bucket = 0; bucket < kBucketCount; ++bucket) {
            uint8_t height = 0;
            if (maxBucketValue > 0) {
                const uint32_t scaled = (bucketValues[bucket] * kMaxBarHeight) / maxBucketValue;
                height = static_cast<uint8_t>(std::min<uint32_t>(scaled, kMaxBarHeight));
                if (bucketValues[bucket] > 0 && height == 0) {
                    height = 1;  // küçük de olsa her algılama görünür kalsın
                }
            }
            const uint8_t x = 4 + bucket * 7;
            const uint8_t y = kBaselineY - height;
            display.drawBox(x, y, 5, height > 0 ? height : 1);
        }

        static constexpr const char* frequencyLabels[] = {
            "2400", "2432", "2464", "2496", "2520"
        };
        static constexpr uint8_t frequencyLabelX[] = {0, 26, 52, 78, 104};
        for (size_t index = 0; index < 5; ++index) {
            display.drawStr(frequencyLabelX[index], 63, frequencyLabels[index]);
        }
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
};

#endif