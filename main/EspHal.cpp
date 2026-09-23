#include "EspHal.h"

namespace {
bool spiBusInitialized[2] = {false, false};

int spiBusIndex(spi_host_device_t host) {
    return host == SPI3_HOST ? 1 : 0;
}
}

EspHal::EspHal(int8_t sck, int8_t miso, int8_t mosi, spi_host_device_t host)
    : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
      sckPin(sck), misoPin(miso), mosiPin(mosi), spiHost(host) {}

void EspHal::pinMode(uint32_t pin, uint32_t mode) {
    if (pin == RADIOLIB_NC) return;
    
    gpio_config_t conf = {};
    conf.pin_bit_mask = (1ULL << pin);
    conf.mode = (gpio_mode_t)mode;
    conf.pull_up_en = GPIO_PULLUP_DISABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&conf);
}

void EspHal::digitalWrite(uint32_t pin, uint32_t value) {
    if (pin == RADIOLIB_NC) return;
    gpio_set_level((gpio_num_t)pin, value);
}

uint32_t EspHal::digitalRead(uint32_t pin) {
    if (pin == RADIOLIB_NC) return 0;
    return gpio_get_level((gpio_num_t)pin);
}

void EspHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    if (interruptNum == RADIOLIB_NC) return;

    if (!isrServiceInstalled) {
        gpio_install_isr_service(0);
        isrServiceInstalled = true;
    }

    gpio_set_intr_type((gpio_num_t)interruptNum, (gpio_int_type_t)mode);
    gpio_isr_handler_add((gpio_num_t)interruptNum, (gpio_isr_t)interruptCb, NULL);
}

void EspHal::detachInterrupt(uint32_t interruptNum) {
    if (interruptNum == RADIOLIB_NC) return;
    gpio_isr_handler_remove((gpio_num_t)interruptNum);
    gpio_set_intr_type((gpio_num_t)interruptNum, GPIO_INTR_DISABLE);
}

void EspHal::delay(RadioLibTime_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void EspHal::delayMicroseconds(RadioLibTime_t us) {
    esp_rom_delay_us(us);
}

RadioLibTime_t EspHal::millis() {
    return esp_timer_get_time() / 1000;
}

RadioLibTime_t EspHal::micros() {
    return esp_timer_get_time();
}

long EspHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) {
    if (pin == RADIOLIB_NC) return 0;

    RadioLibTime_t start = micros();
    while (digitalRead(pin) != state) {
        if (micros() - start > timeout) return 0;
    }

    RadioLibTime_t pulseStart = micros();
    while (digitalRead(pin) == state) {
        if (micros() - start > timeout) return 0;
    }

    return micros() - pulseStart;
}

void EspHal::spiBegin() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = mosiPin;
    buscfg.miso_io_num = misoPin;
    buscfg.sclk_io_num = sckPin;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE;

    const int busIndex = spiBusIndex(spiHost);
    if (!spiBusInitialized[busIndex]) {
        const esp_err_t result = spi_bus_initialize(spiHost, &buscfg, SPI_DMA_CH_AUTO);
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            return;
        }
        spiBusInitialized[busIndex] = true;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 8 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.spics_io_num = -1;
    devcfg.queue_size = 1;

    spi_bus_add_device(spiHost, &devcfg, &spiHandle);
}

void EspHal::spiBeginTransaction() {
    // sending data with polling mode
}

void EspHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    if (len == 0) return;

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;

    spi_device_polling_transmit(spiHandle, &t);
}

void EspHal::spiEndTransaction() {
    // sending data with polling mode
}

void EspHal::spiEnd() {
    if (spiHandle) {
        spi_bus_remove_device(spiHandle);
        spi_bus_free(spiHost);
        spiHandle = NULL;
    }
}

void EspHal::yield() {
    vTaskDelay(1);
}

uint32_t EspHal::pinToInterrupt(uint32_t pin) {
    return pin;
}