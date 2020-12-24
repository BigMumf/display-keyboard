#include "https_server.h"

#include <esp_log.h>

namespace {
constexpr char TAG[] = "kbd_httpd";
}

HTTPSServer::HTTPSServer() : server_(nullptr) {}

HTTPSServer::~HTTPSServer() {
  if (server_) {
    const esp_err_t err = httpd_stop(server_);
    if (err == ESP_OK)
      ESP_LOGD(TAG, "Stopped HTTPD server.");
    else
      ESP_LOGE(TAG, "Failure stopping HTTPD server: %s", esp_err_to_name(err));
  }
}

esp_err_t HTTPSServer::Initialize() {
  if (server_)
    return ESP_FAIL;
  const httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  const esp_err_t err = httpd_start(&server_, &config);
  if (err == ESP_OK)
    ESP_LOGD(TAG, "Started HTTPD server.");
  else
    ESP_LOGE(TAG, "Failure starting HTTPD server: %s", esp_err_to_name(err));
  return err;
}

esp_err_t HTTPSServer::RegisterURIHandler(const httpd_uri_t* handler_info) {
  const esp_err_t err = httpd_register_uri_handler(server_, handler_info);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Failure registering handler: %s", esp_err_to_name(err));
  return err;
}

esp_err_t HTTPSServer::UnregisterURIHandler(const char* uri,
                                            httpd_method_t method) {
  const esp_err_t err = httpd_unregister_uri_handler(server_, uri, method);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Failure unregistering handler: %s", esp_err_to_name(err));
  return err;
}