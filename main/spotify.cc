#include "spotify.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include <esp_err.h>
#include <esp_log.h>
#include <json/cJSON/cJSON.h>
#include <mbedtls/mbedtls/include/mbedtls/base64.h>

#include "config.h"
#include "event_ids.h"
#include "http_client.h"
#include "http_server.h"
#include "wifi.h"

using std::string;

namespace {

struct RequestData {
  struct {
    uint32_t progress_ms;
    uint32_t duration_ms;
  } times;
  bool is_playing;
  bool is_player_active;
  string artist_name;
  string song_title;
  struct {
    string url_640;
    string url_300;
    string url_64;
  } image;
};

constexpr char TAG[] = "Spotify";
constexpr char kApiHost[] = "api.spotify.com";
constexpr char kCurrentlyPlayingResource[] = "/v1/me/player/currently-playing";
constexpr char kRootURI[] = "/";
constexpr char kCallbackURI[] = "/callback/";

string Base64Encode(const string& str) {
  size_t dest_buff_size(0);

  // First get the necessary destination buffer size.
  mbedtls_base64_encode(nullptr, 0, &dest_buff_size,
                        reinterpret_cast<const unsigned char*>(str.c_str()),
                        str.length());

  std::unique_ptr<unsigned char> dst(new unsigned char[dest_buff_size]);
  if (!dst)
    return string();

  int res = mbedtls_base64_encode(
      dst.get(), dest_buff_size, &dest_buff_size,
      reinterpret_cast<const unsigned char*>(str.c_str()), str.length());
  if (res)
    return string();

  return string(reinterpret_cast<char*>(dst.get()), dest_buff_size);
}

string EntityEncode(const string& str) {
  // TODO: Surely there is an ESP-IDF/FreeRTOS function to do this.
  string encoded;
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if (std::isalnum(c)) {
      encoded += c;
    } else {
      char code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
        code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      char code0 = c + '0';
      if (c > 9)
        code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
    taskYIELD();
  }
  return encoded;
}

string GetQueryString(httpd_req_t* request, const char* key) {
  std::unique_ptr<char> query(new char[HTTPD_MAX_URI_LEN + 1]);
  if (httpd_req_get_url_query_str(request, query.get(), HTTPD_MAX_URI_LEN) !=
      ESP_OK) {
    return string();
  }
  std::unique_ptr<char> value(new char[HTTPD_MAX_URI_LEN + 1]);
  if (httpd_query_key_value(query.get(), "code", value.get(),
                            HTTPD_MAX_URI_LEN) != ESP_OK) {
    return string();
  }
  query.reset();
  return string(value.get());
}

std::string GetJSONString(const cJSON* json, const char* key) {
  cJSON* value = cJSON_GetObjectItem(json, key);
  if (!value)
    return string();

  string str_value;
  if (cJSON_IsString(value))
    str_value = cJSON_GetStringValue(value);

  return str_value;
}

int GetJSONNumber(const cJSON* json, const char* key) {
  cJSON* value = cJSON_GetObjectItem(json, key);
  if (!value)
    return 0;

  int num_val = 0;
  if (cJSON_IsNumber(value))
    num_val = value->valueint;

  return num_val;
}

std::string CreateAccessTokenAuthorizationContent(
    const std::string& code,
    const std::string& redirect_url) {
  return "grant_type=authorization_code&code=" + code +
         "&redirect_uri=" + EntityEncode(redirect_url);
}

std::string CreateAccessTokenRefreshContent(const std::string& code) {
  return "grant_type=refresh_token&refresh_token=" + code;
}

}  // namespace

// static
esp_err_t Spotify::RootHandler(httpd_req_t* request) {
  return static_cast<Spotify*>(request->user_ctx)->HandleRootRequest(request);
}

// static
esp_err_t Spotify::CallbackHandler(httpd_req_t* request) {
  return static_cast<Spotify*>(request->user_ctx)
      ->HandleCallbackRequest(request);
}

Spotify::Spotify(const Config* config,
                 HTTPServer* https_server,
                 WiFi* wifi,
                 EventGroupHandle_t event_group)
    : config_(config),
      https_server_(https_server),
      event_group_(event_group),
      wifi_(wifi),
      initialized_(false),
      token_refresh_timer_(nullptr),
      mutex_(xSemaphoreCreateMutex()) {
  assert(config != nullptr);
  assert(https_server != nullptr);
  assert(wifi != nullptr);
  assert(event_group != nullptr);
}

Spotify::~Spotify() {
  if (token_refresh_timer_)
    esp_timer_delete(token_refresh_timer_);
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      https_server_->UnregisterURIHandler(kCallbackURI, HTTP_GET));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      https_server_->UnregisterURIHandler(kRootURI, HTTP_GET));
};

// static:
void Spotify::TokenRefreshCb(void* arg) {
  ESP_LOGD(TAG, "Timer fired to refresh access token");
  xEventGroupSetBits(static_cast<Spotify*>(arg)->event_group_,
                     EVENT_SPOTIFY_ACCESS_TOKEN_EXPIRE);
}

esp_err_t Spotify::Initialize() {
  esp_err_t err = https_server_->Initialize();
  if (err != ESP_OK)
    return err;
  const httpd_uri_t root_handler_info{
      .uri = kRootURI,
      .method = HTTP_GET,
      .handler = Spotify::RootHandler,
      .user_ctx = this,
  };
  err = https_server_->RegisterURIHandler(&root_handler_info);
  if (err != ESP_OK)
    return err;
  const httpd_uri_t callback_handler_info{
      .uri = kCallbackURI,
      .method = HTTP_GET,
      .handler = Spotify::CallbackHandler,
      .user_ctx = this,
  };
  err = https_server_->RegisterURIHandler(&callback_handler_info);
  if (err != ESP_OK)
    return err;

  const esp_timer_create_args_t timer_args = {
    .callback = TokenRefreshCb,
    .arg = this,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "AccessTokenRefresh",
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
    .skip_unhandled_events = true,
#endif
  };
  err = esp_timer_create(&timer_args, &token_refresh_timer_);
  if (err != ESP_OK)
    return err;

  initialized_ = true;
  return ESP_OK;
}

// Redirect the user agent to Spotify to authenticate. After successful
// authentication Spotify will redirect the user agent to a second URL
// that will contain our authorization code.
esp_err_t Spotify::HandleRootRequest(httpd_req_t* request) {
  ESP_LOGD(TAG, "In HandleRootRequest");
  esp_err_t err = httpd_resp_set_status(request, "302 Found");
  if (err != ESP_OK)
    return err;
  err = httpd_resp_set_type(request, "text/plain");
  if (err != ESP_OK)
    return err;

  constexpr char kScopes[] =
      "user-read-private%20"
      "user-read-currently-playing%20"
      "user-read-playback-state%20"
      "user-modify-playback-state";

  string redirect_url;
  err = GetRedirectURL(&redirect_url);
  if (err != ESP_OK)
    return err;
  const string location = "https://accounts.spotify.com/authorize/?client_id=" +
                          config_->spotify.client_id + "&response_type=code" +
                          "&redirect_uri=" + EntityEncode(redirect_url) +
                          "&scope=" + kScopes;

  err = httpd_resp_set_hdr(request, "Location", location.c_str());
  if (err != ESP_OK)
    return err;

  return httpd_resp_sendstr(request, "Please authenticate with Spotify");
}

// The handler for the second part of the user authenticaton where
// Spotify delivers the authorization code.
esp_err_t Spotify::HandleCallbackRequest(httpd_req_t* request) {
  ESP_LOGD(TAG, "In HandleCallbackRequest");
  bool give_mutex = xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
  auth_data_.auth_code = GetQueryString(request, "code");
  bool is_empty = auth_data_.auth_code.empty();
  if (give_mutex)
    xSemaphoreGive(mutex_);
  if (is_empty)
    return httpd_resp_send_500(request);

  // Now that we have the authorization code, notify the application so that
  // it can update any UI (if desired) and continue the process of connecting
  // to Spotify.
  xEventGroupSetBits(event_group_, EVENT_SPOTIFY_GOT_AUTHORIZATION_CODE);

  constexpr char kCallbackSuccess[] =
      "<html><head></head><body>Succesfully authentiated this device with "
      "Spotify.</body></html>";
  constexpr size_t kCallbackSuccessLen = sizeof(kCallbackSuccess) - 1;
  return httpd_resp_send(request, kCallbackSuccess, kCallbackSuccessLen);
}

esp_err_t Spotify::GetRedirectURL(string* url) const {
  string host;
  const esp_err_t err = GetHostname(&host);
  if (err != ESP_OK)
    return err;
  *url = "http://" + host + kCallbackURI;
  return ESP_OK;
}

esp_err_t Spotify::GetCurrentlyPlaying() {
  constexpr char kCurrentlyPlayingURL[] =
      "https://api.spotify.com/v1/me/player/currently-playing";

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE)
    return ESP_FAIL;
  const std::vector<HTTPClient::HeaderValue> header_values = {
      {"Authorization", "Bearer " + auth_data_.access_token},
      {"Connection", "close"},
  };
  xSemaphoreGive(mutex_);

  std::string response;
  HTTPClient https_client;
  int status_code(0);
  esp_err_t err = https_client.DoGET(
      kCurrentlyPlayingURL, header_values,
      [&response](const void* data, int data_len) {
        response.append(static_cast<const char*>(data), data_len);
        return ESP_OK;
      },
      &status_code);

  if (err != ESP_OK)
    return ESP_FAIL;

  if (status_code != HttpStatus_Ok) {
    ESP_LOGE(TAG, "Request error: %d", status_code);
    return ESP_FAIL;
  }

  if (response.empty()) {
    ESP_LOGE(TAG, "Got empty response.");
    return ESP_FAIL;
  }
  cJSON* json = cJSON_Parse(response.c_str());
  if (!json) {
    ESP_LOGE(TAG, "Failure parsing JSON response.");
    return ESP_FAIL;
  }

  cJSON_Delete(json);

  ESP_LOGI(TAG, "Got currently-playing response.");
  return ESP_OK;
}

esp_err_t Spotify::ContinueLogin() {
  bool give_mutex = xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
  std::string authorization_code = std::move(auth_data_.auth_code);
  if (give_mutex)
    xSemaphoreGive(mutex_);
  return GetAccessToken(TokenGrantType::AuthorizationCode,
                        std::move(authorization_code));
}

esp_err_t Spotify::RefreshAccessToken() {
  bool give_mutex = xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
  std::string refresh_token = auth_data_.refresh_token;
  if (give_mutex)
    xSemaphoreGive(mutex_);
  return GetAccessToken(TokenGrantType::Refresh, std::move(refresh_token));
}

esp_err_t Spotify::GetAccessToken(TokenGrantType grant_type, string code) {
  // How long does it take to do a refresh?
  constexpr uint32_t kMaxTokenRefreshDurationSecs = 120;

  esp_err_t err = ESP_OK;
  cJSON* json = nullptr;
  string redirect_url;
  string response;
  bool give_mutex = false;
  uint32_t expires_in_secs = 0;
  const string kGetAccessTokenURL("https://accounts.spotify.com/api/token");
  const std::vector<HTTPClient::HeaderValue> header_values = {
      {"Authorization",
       "Basic " + Base64Encode(config_->spotify.client_id + ":" +
                               config_->spotify.client_secret)},
      {"Content-Type", "application/x-www-form-urlencoded"},
      {"Connection", "close"},
  };
  string content;
  std::string access_token;
  std::string refresh_token;
  std::string token_type;
  std::string scope;
  HTTPClient http_client;
  int status_code(0);

  err = GetRedirectURL(&redirect_url);
  if (err != ESP_OK)
    goto exit;
  content = grant_type == TokenGrantType::AuthorizationCode
                ? CreateAccessTokenAuthorizationContent(code, redirect_url)
                : CreateAccessTokenRefreshContent(code);

  err = http_client.DoPOST(kGetAccessTokenURL, content, header_values,
                           [&response](const void* data, int data_len) {
                             response.append(static_cast<const char*>(data),
                                             data_len);
                             return ESP_OK;
                           },
                           &status_code);
  if (err != ESP_OK)
    goto exit;
  if (status_code != HttpStatus_Ok) {
    ESP_LOGE(TAG, "Invalid status: %d", status_code);
    goto exit;
  }
  if (response.empty()) {
    ESP_LOGE(TAG, "Got empty response.");
    err = ESP_FAIL;
    goto exit;
  }
  json = cJSON_Parse(response.c_str());
  if (!json) {
    ESP_LOGE(TAG, "Failure parsing JSON response.");
    err = ESP_FAIL;
    goto exit;
  }

  access_token = GetJSONString(json, "access_token");
  refresh_token = GetJSONString(json, "refresh_token");
  token_type = GetJSONString(json, "token_type");
  scope = GetJSONString(json, "scope");

  give_mutex = xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
  if (!access_token.empty())
    auth_data_.access_token = std::move(access_token);
  if (!token_type.empty())
    auth_data_.token_type = std::move(token_type);
  if (!refresh_token.empty())
    auth_data_.refresh_token = std::move(refresh_token);
  if (!scope.empty())
    auth_data_.scope = std::move(scope);
  auth_data_.auth_code.clear();
  if (give_mutex)
    xSemaphoreGive(mutex_);
  expires_in_secs = GetJSONNumber(json, "expires_in");
  if (expires_in_secs > kMaxTokenRefreshDurationSecs)
    expires_in_secs -= kMaxTokenRefreshDurationSecs;

  cJSON_Delete(json);

  ESP_LOGI(TAG, "%s access code. Refreshing in %u secs.",
           grant_type == TokenGrantType::Refresh ? "Refreshed" : "Got",
           expires_in_secs);
  esp_timer_start_once(token_refresh_timer_,
                       static_cast<uint64_t>(expires_in_secs) * 1000 * 1000);

exit:
  xEventGroupSetBits(event_group_, err == ESP_OK
                                       ? EVENT_SPOTIFY_ACCESS_TOKEN_GOOD
                                       : EVENT_SPOTIFY_ACCESS_TOKEN_FAILURE);
  return err;
}

esp_err_t Spotify::GetHostname(string* host) const {
  if (wifi_->GetHostname(host) == ESP_OK)
    return ESP_OK;

  esp_ip4_addr_t ip_addr;
  const esp_err_t err = wifi_->GetIPAddress(&ip_addr);
  if (err != ESP_OK)
    return err;

  char buff[30];
  std::snprintf(buff, sizeof(buff), IPSTR, IP2STR(&ip_addr));
  buff[sizeof(buff) - 1] = '\0';
  *host = buff;
  return ESP_OK;
}

string Spotify::GetAuthStartURL() const {
  string host;
  if (GetHostname(&host) != ESP_OK)
    return string();
  return "http://" + host + '/';
}

bool Spotify::HaveAuthorizatonCode() const {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE)
    return false;
  const bool have_it = !auth_data_.auth_code.empty();
  xSemaphoreGive(mutex_);
  return have_it;
}

bool Spotify::HaveAccessToken() const {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE)
    return false;
  const bool have_it = !auth_data_.access_token.empty();
  xSemaphoreGive(mutex_);
  return have_it;
}
