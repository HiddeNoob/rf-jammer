#ifndef RF_SWEEPER_H
#define RF_SWEEPER_H

#include <array>
#include <memory>
#include <vector>
#include "driver/spi_common.h"
#include "RadioLib.h"

class EspHal;  // defined in EspHal.h, only needed as a pointer here

enum class SweepMode {
    BLUETOOTH,
    ALL_CHANNELS,
    WIFI_JAM
};

struct RadioConfig {
    int id;
    int sck;
    int miso;
    int mosi;
    int csn;
    int irq;
    int ce;
    int power;
    spi_host_device_t host;
};

struct RfModuleStatus {
    int id;
    bool available;
    bool assigned;
    SweepMode task;
};

class RfSweeper {
public:
    RfSweeper();

    // Assigns every currently free/available module to a scanning task,
    // splitting the mode's channel range across them.
    void start(SweepMode mode);
    bool assignTask(const std::vector<int>& moduleIds, SweepMode mode);

    // Assigns modules to jam a fixed, explicit set of channels. Unlike
    // assignTask (which splits the spectrum across modules for scanning),
    // every assigned module here cycles through the SAME full channel list,
    // so multiple modules reinforce each other on the same target channels.
    bool assignCustomChannels(const std::vector<int>& moduleIds, const std::vector<int>& channels);

    void pauseTask(const std::vector<int>& moduleIds);
    void resumeTask(const std::vector<int>& moduleIds);
    void stopTask(const std::vector<int>& moduleIds);

    RfModuleStatus getModuleStatus(int moduleId) const;
    int getModuleCount() const;
    bool isRunning() const;
    std::vector<uint32_t> getModuleHistogram(int moduleId) const;

private:
    struct RadioWorker {
        int moduleId = -1;
        nRF24* radio = nullptr;
        Module* module = nullptr;
        EspHal* hal = nullptr;
        std::vector<int> channels;
        std::array<uint32_t, 126> histogram{};
        SweepMode mode = SweepMode::ALL_CHANNELS;
        volatile bool active = true;
        volatile bool paused = false;
        volatile bool exited = false;  // set by the worker task right before it deletes

        // Safety net only: in normal operation the worker task frees
        // radio/module/hal itself (see runRadioJob) and nulls these out
        // before this destructor ever runs. This only fires for a worker
        // whose task never got to run (e.g. xTaskCreate failed).
        ~RadioWorker();
    };

    static void runRfTask(void* parameters);
    static void runRadioJob(RadioWorker& job);

    bool validateModulesFree(const std::vector<int>& moduleIds) const;
    bool openRadiosForModules(const std::vector<int>& moduleIds, std::vector<nRF24*>& radios,
                               std::vector<Module*>& modulesOut, std::vector<EspHal*>& halsOut);
    void purgeStaleWorkers(const std::vector<int>& moduleIds);
    bool launchWorkers(const std::vector<int>& moduleIds, const std::vector<nRF24*>& radios,
                        const std::vector<Module*>& modulesOut, const std::vector<EspHal*>& halsOut,
                        const std::vector<std::vector<int>>& channelsPerWorker, SweepMode mode);

    std::vector<RfModuleStatus> modules;
    std::vector<std::unique_ptr<RadioWorker>> radioJobs;
};

std::vector<int> getChannelsForMode(SweepMode mode);

#endif
