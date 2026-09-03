#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "MenuUi.h"

extern "C" void app_main(void) {
    static RfSweeper sweeper;
    static MenuUi menu(sweeper);
    xTaskCreate([](void* context) {
        static_cast<MenuUi*>(context)->run();
        vTaskDelete(nullptr);
    }, "gem_menu", 8192, &menu, 5, nullptr);
}