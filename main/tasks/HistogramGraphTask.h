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
            if (!status.available) {
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

        uint32_t maxValue = 1;
        std::vector<uint32_t> combinedHistogram(126, 0);
        for (int moduleId : selectedModuleIds_) {
            const auto moduleHistogram = sweeper_.getModuleHistogram(moduleId);
            for (size_t i = 0; i < moduleHistogram.size(); ++i) {
                combinedHistogram[i] += moduleHistogram[i];
                if (combinedHistogram[i] > maxValue) {
                    maxValue = combinedHistogram[i];
                }
            }
        }

        display.drawFrame(0, 12, 128, 52);
        for (int bucket = 0; bucket < 16; ++bucket) {
            uint32_t bucketValue = 0;
            const int bucketStart = bucket * 8;
            const int bucketEnd = std::min(bucketStart + 8, 126);
            for (int channel = bucketStart; channel < bucketEnd; ++channel) {
                bucketValue += combinedHistogram[channel];
            }

            const uint8_t height = maxValue > 0 ? static_cast<uint8_t>((bucketValue * 42) / maxValue) : 0;
            const uint8_t x = 4 + bucket * 7;
            const uint8_t y = 61 - height;
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
