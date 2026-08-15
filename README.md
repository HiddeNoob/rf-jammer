# ESP32 Multi-Module 2.4GHz RF Sweeper

A C++ application built on ESP-IDF, FreeRTOS, and RadioLib that utilizes multiple nRF24L01+ modules to perform parallel frequency sweeping across the 2.4 GHz spectrum using continuous unmodulated carrier transmissions

## Features

* **Multi-Module Parallelism:** Distributes targeted RF channels evenly across multiple nRF24 transceivers operating on separate SPI buses (`SPI2_HOST`, `SPI3_HOST`).
* **Spectrum Sweep Modes:**
  * **Bluetooth Mode:** Focuses on specific BLE/Bluetooth frequency sub-ranges.
  * **All-Channel Mode:** Sweeps the entire nRF24 spectrum (Channels 0–125 / 2400 MHz – 2525 MHz).
* **FreeRTOS Architecture:** Spawns an independent background task per radio module for fast, non-blocking frequency hopping (1 ms step interval).

## Hardware Configuration

* **MCU:** ESP32 / ESP32-S3 microcontroller
* **Transceivers:** 2x (or more) nRF24L01+ modules
* **Environment:** ESP-IDF v5.0+ and [RadioLib](https://github.com/jgromes/RadioLib)

### Default Pin Allocation

| Radio ID | SPI Host | SCK | MISO | MOSI | CSN | IRQ | CE |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Radio 0** | `SPI2_HOST` | 12 | 13 | 11 | 9 | 10 | 46 |
| **Radio 1** | `SPI3_HOST` | 36 | 37 | 39 | 38 | 40 | 35 |

## Building & Flashing

```bash
# Set your target chip
idf.py set-target esp32s3

# Build, flash, and open serial monitor
idf.py build
idf.py flash monitor
```
## Disclaimer
This project and its associated source code are intended strictly for educational, research, and authorized laboratory testing purposes. Transmitting unmodulated carrier waves or intentionally interfering with wireless communications may violate local telecommunications laws and regulations (e.g., FCC, CE, BTK). The authors assume no liability and are not responsible for any misuse, legal consequences, or damage caused by the operation of this software.