#include <vector>
#include <numeric>
#include <driver/spi_common.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "EspHal.h"

// Enum defining the sweep modes
enum class SweepMode {
    BLUETOOTH,
    ALL_CHANNELS,
    WIFI_CHANNELS // Can be added as an example
};

// Radio hardware configuration (Frequency range removed)
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

// Context passed to the FreeRTOS task (Now contains the channel list)
struct RadioTaskContext {
    int id;
    nRF24* radio;
    std::vector<int> channels; // Specific channels assigned to this radio
};

// Helper function that returns channels based on the selected mode
std::vector<int> getChannelsForMode(SweepMode mode) {
    std::vector<int> channels;
    switch (mode) {
        case SweepMode::BLUETOOTH:
            //channels = {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};
            channels = {2,  4,  6,  8,  10, 12, 14, 16, 18, 20,
                        22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
                        42, 44, 46, 48, 50, 52, 54, 56, 58, 60,
                        62, 64, 66, 68, 70, 72, 74, 76, 78, 80};
            break;
        case SweepMode::ALL_CHANNELS:
            // All channels for nRF24 from 0 to 125 (2400 MHz - 2525 MHz)
            channels.resize(126);
            std::iota(channels.begin(), channels.end(), 0); 
            break;
        default:
            channels = {0}; // Fallback
            break;
    }
    return channels;
}

// Independent FreeRTOS task performing frequency hopping over the assigned channels
void rfTask(void *pvParameters) {
    RadioTaskContext* ctx = (RadioTaskContext*)pvParameters;
    char taskTag[32];
    snprintf(taskTag, sizeof(taskTag), "RF_Task_%d", ctx->id);

    if(ctx->channels.empty()){
        ESP_LOGE(taskTag, "No channels assigned. Task stopping.");
        vTaskDelete(NULL);
    }

    ESP_LOGI(taskTag, "Sweep task started. Sweeping %d channels.", ctx->channels.size());

    int count = 0;
    int numChannels = ctx->channels.size();

    for(;;) {
        // nRF24 base frequency is 2400 MHz. The channel value is added to it.
        float freq = 2400.0f + (float)ctx->channels[count];
        ctx->radio->setFrequency(freq);
        ctx->radio->transmitDirect();

        // Delay per channel step
        vTaskDelay(pdMS_TO_TICKS(1));

        // Move to the next channel, loop back to the start when reaching the end of the list
        count = (count + 1) % numChannels;
    }
}

// Manager class handling initialization and task creation
class RFTaskManager {
private:
    const char* TAG = "rf_task_manager";
    bool spi2Initialized = false;
    bool spi3Initialized = false;

    // Helper function to manage SPI bus initialization
    void initSpiBus(EspHal* hal, spi_host_device_t host) {
        if (host == SPI2_HOST) {
            if (!spi2Initialized) {
                ESP_LOGI(TAG, "Initializing SPI2 Bus...");
                hal->spiBegin();
                spi2Initialized = true;
            }
        } 
        else if (host == SPI3_HOST) {
            if (!spi3Initialized) {
                ESP_LOGI(TAG, "Initializing SPI3 Host...");
                hal->spiBegin();
                spi3Initialized = true;
            }
        }
    }

public:
    // Takes an array of already-initialized `nRF24*` radios and the entire targeted channel list
    void initAndRun(const std::vector<nRF24*>& radios, const std::vector<int>& modeChannels) {
        int totalRadios = radios.size();
        if (totalRadios == 0 || modeChannels.empty()) {
            ESP_LOGE(TAG, "No radios provided or empty channel list.");
            return;
        }

        // Distribute channels among available radios (Chunking logic)
        std::vector<std::vector<int>> dividedChannels(totalRadios);
        size_t offset = 0;
        for (size_t i = 0; i < (size_t)totalRadios; ++i) {
            size_t count = modeChannels.size() / totalRadios + (i < (modeChannels.size() % totalRadios) ? 1 : 0);
            for (size_t j = 0; j < count; ++j) {
                dividedChannels[i].push_back(modeChannels[offset++]);
            }
        }

        // Create tasks for each radio instance
        for (size_t i = 0; i < (size_t)totalRadios; ++i) {
            nRF24* radio = radios[i];
            const auto& assignedChannels = dividedChannels[i];

            ESP_LOGI(TAG, "Configuring Radio Index %d. Assigned %d channels.", (int)i, assignedChannels.size());

            if (radio == nullptr) {
                ESP_LOGW(TAG, "Radio at index %d is null. Skipping.", (int)i);
                continue;
            }

            if (assignedChannels.empty()) {
                ESP_LOGW(TAG, "Radio index %d has no channels assigned. Skipping.", (int)i);
                continue;
            }

            // Create RadioTaskContext and copy the assigned channels into it
            RadioTaskContext* ctx = new RadioTaskContext{
                (int)i,
                radio,
                assignedChannels
            };

            xTaskCreate(
                rfTask,
                "rf_sweep_worker",
                4096,
                (void*)ctx,
                5,
                NULL
            );
        }
    }
};

// Initialize a single radio from a hardware config. Returns nRF24* or nullptr on failure
nRF24* initializeRadio(const RadioConfig& cfg) {
    EspHal* hal = new EspHal(cfg.sck, cfg.miso, cfg.mosi, cfg.host);
    // Start SPI for this module
    hal->spiBegin();

    nRF24* radio = new nRF24(new Module(hal, cfg.csn, cfg.irq, cfg.ce));

    // Default start frequency (channel 0)
    float startFreq = 2400.0f;
    int state = radio->begin(startFreq, 1000, cfg.power, 5);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE("init_radio", "Radio init failed (CSN %d): %d", cfg.csn, state);
        delete radio;
        delete hal;
        return nullptr;
    }

    return radio;
}

extern "C" void app_main(void) {
    static const char TAG[] = "app_main";
    ESP_LOGI(TAG, "[Multi-Task RF Manager] Starting up...");

    // Format: {id, sck, miso, mosi, csn, irq, ce, power, host}
    RadioConfig rf1 = {0, 12, 13, 11,  9, 10, 46, 0, SPI2_HOST};
    RadioConfig rf2 = {1, 36, 37, 39, 38, 40, 35, 0, SPI3_HOST};
    std::vector<RadioConfig> myRadios = {rf1,rf2};

    // Select the desired operation mode and get the channels
    SweepMode currentMode = SweepMode::BLUETOOTH; // or SweepMode::ALL_CHANNELS
    std::vector<int> targetChannels = getChannelsForMode(currentMode);

    ESP_LOGI(TAG, "Selected mode has %d total channels. Dispatching to manager...", targetChannels.size());

    // The manager only takes the radios and the targeted channels; it handles distribution and task creation itself
    RFTaskManager manager;
    std::vector<nRF24*> radios;
    radios.reserve(myRadios.size());
    for (const auto& cfg : myRadios) {
        radios.push_back(initializeRadio(cfg));
    }
    manager.initAndRun(radios, targetChannels);

    ESP_LOGI(TAG, "Manager execution completed. Main loop idle.");

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}