#ifndef RF_SWEEPER_H
#define RF_SWEEPER_H

#include <array>
#include <memory>
#include <vector>
#include "driver/spi_common.h"
#include "RadioLib.h"

enum class SweepMode {
    BLUETOOTH,
    ALL_CHANNELS
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

struct RFJob {
    std::vector<int> moduleIds;
    std::vector<nRF24*> radios;
    std::vector<int> channels;
    SweepMode mode;
};

class RfSweeper {
public:
    RfSweeper();
    void start(SweepMode mode);
    bool assignTask(const std::vector<int>& moduleIds, SweepMode mode);
    void pauseTask(const std::vector<int>& moduleIds);
    void resumeTask(const std::vector<int>& moduleIds);
    void stopTask(const std::vector<int>& moduleIds);
    bool runTask(RFJob job);
    RfModuleStatus getModuleStatus(int moduleId) const;
    int getModuleCount() const;
    bool isRunning() const;

private:
    struct RadioWorker {
        int moduleId;
        nRF24* radio;
        std::vector<int> channels;
        std::array<uint32_t, 126> histogram;
        SweepMode mode;
        volatile bool active = true;
        volatile bool paused = false;
    };

    static void runRfTask(void* parameters);
    static void runRadioJob(RadioWorker& job);

    std::vector<RfModuleStatus> modules;
    std::vector<RFJob> jobs;
    std::vector<std::unique_ptr<RadioWorker>> radioJobs;

public:
    std::vector<uint32_t> getModuleHistogram(int moduleId) const;
};

std::vector<int> getChannelsForMode(SweepMode mode);

#endif