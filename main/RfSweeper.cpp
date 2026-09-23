#include "RfSweeper.h"

#include <algorithm>
#include <numeric>
#include <utility>
#include "EspHal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

// Bundles the three objects a radio needs so they always travel and get
// freed together. This is the fix for the old leak where EspHal/Module
// were created but never tracked past the nRF24 wrapper.
struct RadioHandle {
    nRF24* radio = nullptr;
    Module* module = nullptr;
    EspHal* hal = nullptr;
};

RadioHandle openRadio(const RadioConfig& config) {
    RadioHandle handle;
    handle.hal = new EspHal(config.sck, config.miso, config.mosi, config.host);
    handle.hal->spiBegin();
    handle.module = new Module(handle.hal, config.csn, config.irq, config.ce);
    handle.radio = new nRF24(handle.module);

    const int state = handle.radio->begin(2400.0f, 1000, config.power, 5);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE("init_radio", "Radio init failed (CSN %d): %d", config.csn, state);
        delete handle.radio;
        delete handle.module;
        delete handle.hal;
        return {};
    }
    return handle;
}

void closeRadio(RadioHandle& handle) {
    delete handle.radio;
    delete handle.module;
    delete handle.hal;
    handle = {};
}

// Used only at boot to check whether a module answers, without keeping it
// open. Previously this leaked the Module/EspHal every time.
bool probeRadioOnline(const RadioConfig& config) {
    RadioHandle handle = openRadio(config);
    if (handle.radio == nullptr) {
        return false;
    }
    closeRadio(handle);
    return true;
}

const std::vector<RadioConfig> RADIO_CONFIGS = {
    {0, 12, 13, 11, 9, 10, 46, 0, SPI2_HOST},
    {1, 36, 37, 39, 38, 40, 35, 0, SPI3_HOST},
};

}  // namespace

RfSweeper::RadioWorker::~RadioWorker() {
    delete radio;
    delete module;
    delete hal;
}

void RfSweeper::runRfTask(void* parameters) {
    runRadioJob(*static_cast<RadioWorker*>(parameters));
}

void RfSweeper::runRadioJob(RadioWorker& job) {
    char taskTag[32];
    snprintf(taskTag, sizeof(taskTag), "RF_Task_%d", job.moduleId);
    ESP_LOGI(taskTag, "Task started. %d channel(s) assigned.", static_cast<int>(job.channels.size()));

    size_t channelIndex = 0;
    while (job.active) {
        if (job.paused) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

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

    // We are the exclusive owner of this hardware once assigned, so it's
    // safe to free it here, right before the task disappears. This closes
    // the leak where stopTask() left the radio/Module/EspHal alive forever.
    delete job.radio;
    job.radio = nullptr;
    delete job.module;
    job.module = nullptr;
    delete job.hal;
    job.hal = nullptr;
    job.exited = true;

    vTaskDelete(nullptr);
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
        if (!probeRadioOnline(config)) {
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

bool RfSweeper::validateModulesFree(const std::vector<int>& moduleIds) const {
    if (moduleIds.empty()) {
        ESP_LOGW("rf_sweeper", "No RF modules selected.");
        return false;
    }

    std::vector<bool> seen(modules.size(), false);
    for (const int moduleId : moduleIds) {
        if (moduleId < 0 || static_cast<size_t>(moduleId) >= modules.size()
            || seen[moduleId] || modules[moduleId].assigned || !modules[moduleId].available) {
            ESP_LOGW("rf_sweeper", "RF module %d cannot be assigned (busy, offline, or invalid).", moduleId);
            return false;
        }
        seen[moduleId] = true;
    }
    return true;
}

bool RfSweeper::openRadiosForModules(const std::vector<int>& moduleIds, std::vector<nRF24*>& radios,
                                      std::vector<Module*>& modulesOut, std::vector<EspHal*>& halsOut) {
    radios.clear();
    modulesOut.clear();
    halsOut.clear();
    radios.reserve(moduleIds.size());
    modulesOut.reserve(moduleIds.size());
    halsOut.reserve(moduleIds.size());

    for (const int moduleId : moduleIds) {
        RadioHandle handle = openRadio(RADIO_CONFIGS[moduleId]);
        if (handle.radio == nullptr) {
            // Roll back everything already opened in this call so a
            // partial failure never leaks hardware objects.
            for (size_t i = 0; i < radios.size(); ++i) {
                RadioHandle toClose{radios[i], modulesOut[i], halsOut[i]};
                closeRadio(toClose);
            }
            modules[moduleId].available = false;
            return false;
        }
        radios.push_back(handle.radio);
        modulesOut.push_back(handle.module);
        halsOut.push_back(handle.hal);
    }
    return true;
}

void RfSweeper::purgeStaleWorkers(const std::vector<int>& moduleIds) {
    // Wait (briefly, bounded) for any previous, already-stopped worker on
    // these modules to finish freeing its own hardware, then drop its
    // record. This is what used to make getModuleHistogram()/pauseTask()/
    // resumeTask() see accumulated stale entries forever.
    for (const int moduleId : moduleIds) {
        for (auto& worker : radioJobs) {
            if (worker != nullptr && worker->moduleId == moduleId && !worker->active) {
                int waitedMs = 0;
                while (!worker->exited && waitedMs < 100) {
                    vTaskDelay(pdMS_TO_TICKS(2));
                    waitedMs += 2;
                }
            }
        }
    }

    radioJobs.erase(std::remove_if(radioJobs.begin(), radioJobs.end(),
        [&](const std::unique_ptr<RadioWorker>& worker) {
            return worker != nullptr && !worker->active &&
                   std::find(moduleIds.begin(), moduleIds.end(), worker->moduleId) != moduleIds.end();
        }), radioJobs.end());
}

bool RfSweeper::launchWorkers(const std::vector<int>& moduleIds, const std::vector<nRF24*>& radios,
                               const std::vector<Module*>& modulesOut, const std::vector<EspHal*>& halsOut,
                               const std::vector<std::vector<int>>& channelsPerWorker, SweepMode mode) {
    purgeStaleWorkers(moduleIds);

    const size_t firstJobIndex = radioJobs.size();
    for (size_t index = 0; index < moduleIds.size(); ++index) {
        auto worker = std::make_unique<RadioWorker>();
        worker->moduleId = moduleIds[index];
        worker->radio = radios[index];
        worker->module = modulesOut[index];
        worker->hal = halsOut[index];
        worker->channels = channelsPerWorker[index];
        worker->mode = mode;
        radioJobs.push_back(std::move(worker));
    }

    for (size_t index = 0; index < moduleIds.size(); ++index) {
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "rf_%d", moduleIds[index]);
        if (xTaskCreate(runRfTask, taskName, 4096, radioJobs[firstJobIndex + index].get(),
                        5, nullptr) != pdPASS) {
            // Nothing is running for the entries from here on, so free
            // their hardware directly instead of leaving it dangling.
            for (size_t cleanupIndex = index; cleanupIndex < moduleIds.size(); ++cleanupIndex) {
                auto& worker = radioJobs[firstJobIndex + cleanupIndex];
                delete worker->radio; worker->radio = nullptr;
                delete worker->module; worker->module = nullptr;
                delete worker->hal; worker->hal = nullptr;
                worker->active = false;
                worker->exited = true;
            }
            return false;
        }
    }
    return true;
}

bool RfSweeper::assignTask(const std::vector<int>& moduleIds, SweepMode mode) {
    if (!validateModulesFree(moduleIds)) {
        return false;
    }

    std::vector<int> channels = getChannelsForMode(mode);
    if (moduleIds.size() > channels.size()) {
        ESP_LOGW("rf_sweeper", "Cannot assign %d RF modules to %d channels.",
                 static_cast<int>(moduleIds.size()), static_cast<int>(channels.size()));
        return false;
    }

    std::vector<nRF24*> radios;
    std::vector<Module*> modulesOut;
    std::vector<EspHal*> halsOut;
    if (!openRadiosForModules(moduleIds, radios, modulesOut, halsOut)) {
        return false;
    }

    const size_t baseJobChannels = channels.size() / moduleIds.size();
    const size_t extraChannels = channels.size() % moduleIds.size();
    std::vector<std::vector<int>> channelsPerWorker(moduleIds.size());
    for (size_t index = 0; index < moduleIds.size(); ++index) {
        const size_t firstChannel = index * baseJobChannels + std::min(index, extraChannels);
        const size_t assignedChannelCount = baseJobChannels + (index < extraChannels ? 1 : 0);
        channelsPerWorker[index] = std::vector<int>(channels.begin() + firstChannel,
                                                      channels.begin() + firstChannel + assignedChannelCount);
    }

    if (!launchWorkers(moduleIds, radios, modulesOut, halsOut, channelsPerWorker, mode)) {
        ESP_LOGE("rf_sweeper", "Could not start RF task.");
        return false;
    }

    for (size_t index = 0; index < moduleIds.size(); ++index) {
        modules[moduleIds[index]] = {moduleIds[index], true, true, mode};
        ESP_LOGI("rf_sweeper", "RF module %d assigned %d channel(s).",
                 moduleIds[index], static_cast<int>(channelsPerWorker[index].size()));
    }
    return true;
}

bool RfSweeper::assignCustomChannels(const std::vector<int>& moduleIds, const std::vector<int>& channels) {
    if (channels.empty()) {
        ESP_LOGW("rf_sweeper", "No channels given for custom RF task.");
        return false;
    }
    if (!validateModulesFree(moduleIds)) {
        return false;
    }

    std::vector<nRF24*> radios;
    std::vector<Module*> modulesOut;
    std::vector<EspHal*> halsOut;
    if (!openRadiosForModules(moduleIds, radios, modulesOut, halsOut)) {
        return false;
    }

    // Every module jams the same full channel set (no splitting), so
    // multiple modules reinforce coverage on the same target channels.
    std::vector<std::vector<int>> channelsPerWorker(moduleIds.size(), channels);

    if (!launchWorkers(moduleIds, radios, modulesOut, halsOut, channelsPerWorker, SweepMode::WIFI_JAM)) {
        ESP_LOGE("rf_sweeper", "Could not start WiFi jam task.");
        return false;
    }

    for (const int moduleId : moduleIds) {
        modules[moduleId] = {moduleId, true, true, SweepMode::WIFI_JAM};
    }
    ESP_LOGI("rf_sweeper", "WiFi jam started: %d channel(s), %d module(s).",
             static_cast<int>(channels.size()), static_cast<int>(moduleIds.size()));
    return true;
}

void RfSweeper::pauseTask(const std::vector<int>& moduleIds) {
    for (const auto& radioJob : radioJobs) {
        if (radioJob != nullptr && radioJob->active &&
            std::find(moduleIds.begin(), moduleIds.end(), radioJob->moduleId) != moduleIds.end()) {
            radioJob->paused = true;
        }
    }
}

void RfSweeper::resumeTask(const std::vector<int>& moduleIds) {
    for (const auto& radioJob : radioJobs) {
        if (radioJob != nullptr && radioJob->active &&
            std::find(moduleIds.begin(), moduleIds.end(), radioJob->moduleId) != moduleIds.end()) {
            radioJob->paused = false;
        }
    }
}

void RfSweeper::stopTask(const std::vector<int>& moduleIds) {
    for (const int moduleId : moduleIds) {
        for (const auto& radioJob : radioJobs) {
            if (radioJob != nullptr && radioJob->moduleId == moduleId) {
                radioJob->active = false;
            }
        }
        if (moduleId >= 0 && static_cast<size_t>(moduleId) < modules.size()) {
            modules[moduleId].assigned = false;
        }
    }
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
        // Only the currently active worker for this module counts. Stopped
        // workers are purged on the next reassignment (see
        // purgeStaleWorkers), so this can no longer mix in stale runs.
        if (radioJob != nullptr && radioJob->active && radioJob->moduleId == moduleId) {
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
