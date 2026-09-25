#pragma once
#include "esp_http_client.h"
using wifi_ps_type_t = int;
constexpr int WIFI_PS_NONE = 0;
constexpr int WIFI_PS_MIN_MODEM = 1;
inline int esp_wifi_get_ps(int* value) {
  *value = WIFI_PS_MIN_MODEM;
  return ESP_OK;
}
inline int esp_wifi_set_ps(int) { return ESP_OK; }
