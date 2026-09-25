#pragma once
#include <cstddef>
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_INVALID_ARG = 1;
constexpr int HTTP_EVENT_ON_DATA = 1;
struct esp_http_client_event_t {
  int event_id;
  void* user_data;
  int data_len;
  void* data;
};
struct esp_http_client_config_t {
  const char* url;
  esp_err_t (*event_handler)(esp_http_client_event_t*);
  int buffer_size;
  int buffer_size_tx;
  void* user_data;
  bool skip_cert_common_name_check;
  esp_err_t (*crt_bundle_attach)(void*);
  bool keep_alive_enable;
};
using esp_http_client_handle_t = esp_http_client_config_t*;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
