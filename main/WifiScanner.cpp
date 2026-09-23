#include "WifiScanner.h"

#include <algorithm>
#include <map>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace {
constexpr char TAG[] = "wifi_scanner";
bool wifiReady = false;

bool ensureWifiInitialized() {
    if (wifiReady) {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
        return false;
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed.");
        return false;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    wifiReady = true;
    return true;
}
}  // namespace

std::vector<WifiNetwork> scanWifiNetworks() {
    std::vector<WifiNetwork> results;
    if (!ensureWifiInitialized()) {
        return results;
    }

    wifi_scan_config_t scanConfig = {};
    scanConfig.show_hidden = true;
    if (esp_wifi_scan_start(&scanConfig, true /* block */) != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed to start.");
        return results;
    }

    uint16_t apCount = 0;
    esp_wifi_scan_get_ap_num(&apCount);
    if (apCount == 0) {
        return results;
    }

    std::vector<wifi_ap_record_t> records(apCount);
    esp_wifi_scan_get_ap_records(&apCount, records.data());

    // Collapse duplicate SSID+channel pairs (e.g. multiple APs of the same
    // mesh network), keeping the strongest signal for each.
    std::map<std::pair<std::string, int>, WifiNetwork> byKey;
    for (uint16_t i = 0; i < apCount; ++i) {
        const wifi_ap_record_t& record = records[i];
        const std::string ssid(reinterpret_cast<const char*>(record.ssid));
        const auto key = std::make_pair(ssid, static_cast<int>(record.primary));
        const auto it = byKey.find(key);
        if (it == byKey.end() || record.rssi > it->second.rssi) {
            byKey[key] = WifiNetwork{ssid, static_cast<int>(record.primary), static_cast<int>(record.rssi)};
        }
    }

    results.reserve(byKey.size());
    for (const auto& entry : byKey) {
        results.push_back(entry.second);
    }
    std::sort(results.begin(), results.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        return a.rssi > b.rssi;
    });

    return results;
}
