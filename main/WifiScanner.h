#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include <string>
#include <vector>

struct WifiNetwork {
    std::string ssid;
    int channel;
    int rssi;
};

// Performs a blocking scan using the ESP32's built-in WiFi radio and returns
// the networks found, one entry per unique (SSID, channel) pair, strongest
// signal first. Safe to call repeatedly; the WiFi driver is brought up once
// and reused on later calls. Takes roughly 1-2 seconds.
std::vector<WifiNetwork> scanWifiNetworks();

// Converts a 2.4 GHz WiFi channel number (1-13) into the nRF24 channel
// offset RfSweeper expects (RF frequency = 2400 MHz + offset).
inline int wifiChannelToRfOffset(int wifiChannel) {
    return 12 + 5 * (wifiChannel - 1);
}

#endif
