**ESP32 RF Sweeper & WiFi Jammer**

An RF task manager running on the ESP32-S3 that scans the 2.4 GHz spectrum using two nRF24 modules, scans WiFi networks and jams the channels of selected networks, and displays the results on a small SSD1306 OLED screen via a button-controlled menu.

⚠️ **Legal Disclaimer:** RF jamming involves intentionally interfering with communications equipment and is illegal in many countries. This project must only be used in isolated test environments under your control for educational/research purposes.

### Features

* **Bluetooth Sweep:** Scans 2.4 GHz Bluetooth channels.
* **All Channels Sweep:** Scans the entire 2.4 GHz spectrum (126 channels).
* **Histogram Drawer:** Performs the same scan as All Channels and plots the results as a bar graph on the OLED.
* **WiFi Jam:** Scans nearby networks using the device's native WiFi radio and jams the channels of user-selected networks using the nRF24 modules.
* Tasks can be distributed across multiple RF modules; modules can be paused, resumed, or stopped while running.
* OLED menu navigated via 3 buttons (Up / Down / Select), featuring task history and module status screens.

### Hardware

* ESP32-S3
* 2× nRF24L01(+) modules (separate SPI buses: SPI2 and SPI3)
* SSD1306 128x64 I2C OLED
* 3 buttons (Up, Down, Select)
* Pin assignments are defined in `RADIO_CONFIGS` inside `RfSweeper.cpp`; OLED and button pins are defined inside `MenuUi.cpp`.

### Architecture

* **RfSweeper:** Manages nRF24 module ownership, channel assignment, and lifecycle (start/pause/resume/stop); each module runs in its own FreeRTOS task.
* **Task / RfTask:** Common interface and lifecycle for every menu task. How channels are assigned to modules (`RfAssignmentStrategy`) is decoupled from the task:
* `ModeAssignmentStrategy` → Divides the channel range of a fixed `SweepMode` among the modules (sweep tasks).
* `CustomChannelAssignmentStrategy` → Assigns an explicit channel list identically to all modules (WiFi Jam).


* **WifiScanner:** Performs blocking network scans using the ESP32's native WiFi radio.
* **MenuUi:** GEM/u8g2-based menu, button inputs, task selection/configuration, and display rendering.

### Building

Requires ESP-IDF (>=4.1) and IDF Component Manager.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor

```

**Dependencies (`idf_component.yml` and `CMakeLists.txt`):** `jgromes/radiolib`, `u8g2`, `GEM`, and ESP-IDF components: `esp_wifi`, `esp_netif`, `nvs_flash`, `driver`.

To enable debug logging, define the `APP_LOG_ENABLED=1` build flag (see `AppLog.h`).

### Usage

1. Upon power-on, the main menu appears on the OLED; navigate with Up/Down and confirm with the Select button.
2. **Create Task** → Select a task type → (If WiFi Jam: scan networks and select channels first) → Check available/suitable RF module(s) → Confirm.
3. **Current Running Tasks** → Select a running task to view its status, pause/resume it, or stop it.
4. **Task History** → Brief log of past task transitions.

### Known Limitations

* An unresponsive nRF24 module at startup is completely disabled and will not be re-detected even if reconnected (requires a system restart).
* WiFi scanning may take a few seconds, blocking the screen during execution.