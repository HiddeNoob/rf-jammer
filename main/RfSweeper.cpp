#include "RfSweeper.h"

#include <algorithm>
#include <memory>
#include <numeric>
#include <utility>
#include "EspHal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

nRF24* initializeRadio(const RadioConfig& config) {
    auto* hal = new EspHal(config.sck, config.miso, config.mosi, config.host);
    hal->spiBegin();

    auto* radio = new nRF24(new Module(hal, config.csn, config.irq, config.ce));
    const int state = radio->begin(2400.0f, 1000, config.power, 5);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE("init_radio", "Radio init failed (CSN %d): %d", config.csn, state);
        delete radio;
        delete hal;
        return nullptr;
    }
    return radio;
}

const std::vector<RadioConfig> RADIO_CONFIGS = {
    {0, 12, 13, 11, 9, 10, 46, 0, SPI2_HOST},
    {1, 36, 37, 39, 38, 40, 35, 0, SPI3_HOST},
};

}  // namespace

void RfSweeper::runRfTask(void* parameters) {
    runRadioJob(*static_cast<RadioWorker*>(parameters));
}

void RfSweeper::runRadioJob(RadioWorker& job) {
    char taskTag[32];
    snprintf(taskTag, sizeof(taskTag), "RF_Task_%d", job.moduleId);
    job.histogram.fill(0);
    ESP_LOGI(taskTag, "Task %s started. Sweeping %d channels.",
             job.mode == SweepMode::BLUETOOTH ? "BLUETOOTH" : "ALL_CHANNELS",
             job.channels.size());

    size_t channelIndex = 0;
    for (;;) {
        const int channel = job.channels[channelIndex];
        const float frequency = 2400.0f + static_cast<float>(channel);
        job.radio->setFrequency(frequency);
        job.radio->transmitDirect();

        if (job.radio->isCarrierDetected()) {
            job.histogram[channel]++;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        channelIndex = (channelIndex + 1) % job.channels.size();
    }
}

std::vector<int> getChannelsForMode(SweepMode mode) {
    if (mode == SweepMode::BLUETOOTH) {
        return {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
                32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
                60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80};
    }

    std::vector<int> channels(126);
    std::iota(channels.begin(), channels.end(), 0);
    return channels;
}

RfSweeper::RfSweeper() {
    modules.clear();
    modules.reserve(RADIO_CONFIGS.size());

    for (const RadioConfig& config : RADIO_CONFIGS) {
        std::unique_ptr<nRF24> radio(initializeRadio(config));
        if (!radio) {
            ESP_LOGW("rf_sweeper",
                    "RF module %d is offline at boot; it will be added only when it is online.",
                    config.id);
            continue;
        }

        modules.push_back({config.id, true, false, SweepMode::ALL_CHANNELS});
    }

    if (modules.empty()) {
        ESP_LOGW("rf_sweeper", "No RF modules are online at boot.");
    }
}

void RfSweeper::start(SweepMode mode) {
    std::vector<int> moduleIds;
    for (const RfModuleStatus& module : modules) {
        if (!module.assigned && module.available) {
            moduleIds.push_back(module.id);
        }
    }
    assignTask(moduleIds, mode);
}

bool RfSweeper::assignTask(const std::vector<int>& moduleIds, SweepMode mode) {
    if (moduleIds.empty()) {
        ESP_LOGW("rf_sweeper", "No RF modules selected.");
        return false;
    }

    std::vector<int> channels = getChannelsForMode(mode);
    if (moduleIds.size() > channels.size()) {
        ESP_LOGW("rf_sweeper", "Cannot assign %d RF modules to %d channels.",
                 moduleIds.size(), channels.size());
        return false;
    }

    std::vector<bool> selected(modules.size(), false);
    for (const int moduleId : moduleIds) {
        if (moduleId < 0 || static_cast<size_t>(moduleId) >= modules.size()
            || selected[moduleId] || modules[moduleId].assigned
            || !modules[moduleId].available) {
            ESP_LOGW("rf_sweeper", "RF module %d cannot be assigned.", moduleId);
            return false;
        }
        selected[moduleId] = true;
    }

    const size_t baseJobChannels = channels.size() / moduleIds.size();
    const size_t extraChannels = channels.size() % moduleIds.size();
    std::vector<nRF24*> radios;
    radios.reserve(moduleIds.size());

    for (size_t index = 0; index < moduleIds.size(); ++index) {
        const int moduleId = moduleIds[index];
        nRF24* radio = initializeRadio(RADIO_CONFIGS[moduleId]);
        if (radio == nullptr) {
            modules[moduleId].available = false;
            return false;
        }

        radios.push_back(radio);
    }

    if (!runTask(RFJob{moduleIds, std::move(radios), std::move(channels), mode})) {
        ESP_LOGE("rf_sweeper", "Could not start RF task.");
        return false;
    }

    for (size_t index = 0; index < moduleIds.size(); ++index) {
        modules[moduleIds[index]] = {moduleIds[index], true, true, mode};
        ESP_LOGI("rf_sweeper", "RF module %d assigned %d channels.",
                 moduleIds[index],
                 static_cast<int>(baseJobChannels + (index < extraChannels ? 1 : 0)));
    }

    return true;
}

bool RfSweeper::runTask(RFJob job) {
    if (job.moduleIds.empty() || job.radios.size() != job.moduleIds.size()
        || job.channels.size() < job.radios.size()) {
        return false;
    }

    const size_t baseJobChannels = job.channels.size() / job.radios.size();
    const size_t extraChannels = job.channels.size() % job.radios.size();
    const size_t firstJobIndex = radioJobs.size();
    jobs.push_back(job);
    radioJobs.reserve(radioJobs.size() + job.radios.size());

    for (size_t index = 0; index < job.radios.size(); ++index) {
        const size_t firstChannel = index * baseJobChannels + std::min(index, extraChannels);
        const size_t assignedChannelCount = baseJobChannels + (index < extraChannels ? 1 : 0);
        const size_t lastChannel = firstChannel + assignedChannelCount;
        radioJobs.push_back(std::make_unique<RadioWorker>(RadioWorker{
            job.moduleIds[index],
            job.radios[index],
            std::vector<int>(job.channels.begin() + firstChannel,
                             job.channels.begin() + lastChannel),
            {},
            job.mode
        }));
    }

    for (size_t index = 0; index < job.radios.size(); ++index) {
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "rf_%d", job.moduleIds[index]);
        if (xTaskCreate(runRfTask, taskName, 4096, radioJobs[firstJobIndex + index].get(),
                        5, nullptr) != pdPASS) {
            return false;
        }
    }
    return true;
}

RfModuleStatus RfSweeper::getModuleStatus(int moduleId) const {
    if (moduleId < 0 || static_cast<size_t>(moduleId) >= modules.size()) {
        return {-1, false, false, SweepMode::ALL_CHANNELS};
    }
    return modules[moduleId];
}

int RfSweeper::getModuleCount() const {
    return static_cast<int>(modules.size());
}

std::vector<uint32_t> RfSweeper::getModuleHistogram(int moduleId) const {
    std::vector<uint32_t> histogram(126, 0);
    for (const auto& radioJob : radioJobs) {
        if (radioJob != nullptr && radioJob->moduleId == moduleId) {
            for (size_t index = 0; index < histogram.size(); ++index) {
                histogram[index] += radioJob->histogram[index];
            }
        }
    }
    return histogram;
}

bool RfSweeper::isRunning() const {
    for (const RfModuleStatus& module : modules) {
        if (module.assigned) {
            return true;
        }
    }
    return false;
}