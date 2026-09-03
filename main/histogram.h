#include <stdio.h>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h" // Mikrosaniye gecikmesi için (ESP-IDF v5.0+)

// Tarayıcı yapılandırması ve histogram veri yapısı
struct SpectrumScannerConfig {
    nRF24* radio;                 // RadioLib nRF24 nesnesi
    uint16_t startChannel;        // Başlangıç kanalı (0 - 125)
    uint16_t endChannel;          // Bitiş kanalı (0 - 125)
    uint32_t dwellTimeUs;         // Kanal başına bekleme süresi (µs)
    uint32_t histogram[126];      // Kanal başına tespit sayacı
    volatile bool isRunning;      // Görev durum bayrağı
};

// RF Spektrum Tarama Görevi (Rx / Carrier Detect)
void vSpectrumScannerTask(void* pvParameters) {
    SpectrumScannerConfig* config = static_cast<SpectrumScannerConfig*>(pvParameters);
    config->isRunning = true;

    // Modülü dinleme (Rx) moduna al
    config->radio->startReceive();

    while (config->isRunning) {
        for (uint16_t ch = config->startChannel; ch <= config->endChannel; ch++) {
            // 1. Kanal frekansını ayarla (2400 MHz + ch)
            config->radio->setFrequency(2400.0f + ch);

            // 2. Sinyal tespiti ve RPD entegrasyonu için mikro-gecikme
            esp_rom_delay_us(config->dwellTimeUs);

            // 3. Taşıyıcı Sinyal Tespiti (-64 dBm üzerindeki RF gücü algılanır)
            if (config->radio->isCarrierDetected()) {
                config->histogram[ch]++;
            }
        }

        // Watchdog Timer (WDT) tetiklenmesini önlemek için her tarama turunda 1 ms RTOS gecikmesi
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    vTaskDelete(NULL);
}

// Görevi başlatan yardımcı fonksiyon
BaseType_t startSpectrumScanner(SpectrumScannerConfig* config, UBaseType_t priority, BaseType_t coreId) {
    // Histogram verilerini sıfırla
    for (int i = 0; i < 126; i++) {
        config->histogram[i] = 0;
    }

    // FreeRTOS görevini oluştur ve belirtilen çekirdeğe sabitle
    return xTaskCreatePinnedToCore(
        vSpectrumScannerTask,       // Görev fonksiyonu
        "rf_histogram_task",        // Görev adı
        4096,                       // Stack boyutu
        config,                     // Parametre
        priority,                   // Görev önceliği
        NULL,                       // Task handle
        coreId                      // CPU Çekirdeği (0 veya 1)
    );
}

void printHistogram(SpectrumScannerConfig* cfg) {
    printf("\n--- FREKANS KULLANIM HISTOGRAMI ---\n");
    for (int ch = cfg->startChannel; ch <= cfg->endChannel; ch++) {
        printf("Ch %02d (24%02d MHz): %lu tespit\n", ch, ch, cfg->histogram[ch]);
    }
}