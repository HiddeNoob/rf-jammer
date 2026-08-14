/*
   RadioLib Non-Arduino ESP-IDF nRF24 Jammer Example
*/

#include <RadioLib.h>
#include "esp_log.h"
#include "EspHal.h"

// create a new instance of the HAL class
EspHal* hal = new EspHal(12, 13, 11);

// now we can create the radio module
nRF24 radio = new Module(hal, 9, 10, 46);

static const char *TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "[nRF24 Jammer] Initializing ... ");
    hal->spiBegin();
    
    int state = radio.begin(2400, 1000, -6, 5);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGI(TAG, "failed, code %d\n", state);
        while(true) {
            hal->delay(1000);
        }
    }
    ESP_LOGI(TAG, "success! Starting 2.4GHz \n");

    int currentFreq = 2400;
    radio.setFrequency(currentFreq);
    
    for(;;) {


        radio.setFrequency(currentFreq);
        radio.transmitDirect();

        hal->delay(1);

        currentFreq++;

        if(currentFreq > 2525){
            currentFreq = 2400;
        }
        
    }
}