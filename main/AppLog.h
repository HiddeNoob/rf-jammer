#ifndef APP_LOG_H
#define APP_LOG_H

#ifndef APP_LOG_ENABLED
#define APP_LOG_ENABLED 0
#endif

#if APP_LOG_ENABLED
#include "esp_log.h"
#define APP_LOGE ESP_LOGE
#define APP_LOGW ESP_LOGW
#define APP_LOGI ESP_LOGI
#else
#define APP_LOGE(tag, format, ...) do { } while (0)
#define APP_LOGW(tag, format, ...) do { } while (0)
#define APP_LOGI(tag, format, ...) do { } while (0)
#endif

#endif