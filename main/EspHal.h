#ifndef ESP_HAL_H
#define ESP_HAL_H

#include <RadioLib.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class EspHal : public RadioLibHal {
public:
    // ESP32-S3 varsayılan SPI pinleri (İhtiyacınıza göre değiştirebilirsiniz)
    EspHal(int8_t sck = 12, int8_t miso = 13, int8_t mosi = 11, spi_host_device_t host = SPI2_HOST);
    virtual ~EspHal() = default;

    // RadioLibHal Saf Sanal (Pure Virtual) Metotların Uygulaması
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

    void yield() override;
    uint32_t pinToInterrupt(uint32_t pin) override;

private:
    int8_t sckPin;
    int8_t misoPin;
    int8_t mosiPin;
    spi_host_device_t spiHost;
    spi_device_handle_t spiHandle = NULL;
    bool isrServiceInstalled = false;
};

#endif // ESP_HAL_H