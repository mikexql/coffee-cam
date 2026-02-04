#include "web/http_server.h"

#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "camera_index.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"

namespace coffee_cam {
namespace web {
namespace {

constexpr int kAiWidth = 96;
constexpr int kAiHeight = 96;

MicrofoamResult* g_result = nullptr;
ActionHandler g_action_handler = nullptr;

inline bool ensure_initialized(httpd_req_t* req) {
  if (!g_result) {
    httpd_resp_send_500(req);
    return false;
  }
  return true;
}

inline MicrofoamResult& state() {
  return *g_result;
}

static esp_err_t index_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_ov5640_html, strlen(index_ov5640_html));
}

static esp_err_t coffee_cam_http_action_request(httpd_req_t* req) {
  if (!ensure_initialized(req) || !g_action_handler) {
    return ESP_FAIL;
  }

  char cmd[32] = {0};
  char vol_str[32] = "0";
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;

  if (buf_len > 1) {
    char* buf = static_cast<char*>(malloc(buf_len));
    if (buf && httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd));
      httpd_query_key_value(buf, "vol", vol_str, sizeof(vol_str));
    }
    free(buf);
  }

  state().liquid_v = atof(vol_str);
  g_action_handler(String(cmd));

  char json[400];
  snprintf(json, sizeof(json),
           "{\"raw\":[%d,%d,%d],\"avg\":%d,\"empty\":%d,\"start\":%d,\"end\":%d,\"init_h\":%d,\"final_h\":%d,\"pct\":%.1f,\"status\":\"%s\",\"ml_label\":\"%s\",\"ml_conf\":%.2f}",
           state().raw[0], state().raw[1], state().raw[2], state().avg,
           state().empty, state().start, state().end,
           state().init_h, state().final_h, state().pct, state().status,
           state().ml_label, state().ml_confidence);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t (*const kActionUriHandler)(httpd_req_t*) = coffee_cam_http_action_request;

static esp_err_t capture_full_handler(httpd_req_t* req) {
  if (!ensure_initialized(req) || !state().fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(state().fb->buf,
                           state().fb->len,
                           state().fb->width,
                           state().fb->height,
                           PIXFORMAT_RGB565,
                           30,
                           &jpg_buf,
                           &jpg_len);

  if (!converted) {
    Serial.println("JPEG compression failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=full.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(jpg_buf), jpg_len);
  free(jpg_buf);
  return res;
}

static esp_err_t capture_color_handler(httpd_req_t* req) {
  if (!ensure_initialized(req) || !state().fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  size_t rgb_len = kAiWidth * kAiHeight * 3;
  uint8_t* rgb_buf = static_cast<uint8_t*>(malloc(rgb_len));

  if (!rgb_buf) {
    Serial.println("ERR: OOM for Web Color Buffer");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  const int w = state().fb->width;
  const int h = state().fb->height;
  const int min_dim = (w < h) ? w : h;
  const int start_x = (w - min_dim) / 2;
  const int start_y = (h - min_dim) / 2;

  for (int y = 0; y < kAiHeight; y++) {
    for (int x = 0; x < kAiWidth; x++) {
      int x_cam = start_x + (x * min_dim) / kAiWidth;
      int y_cam = start_y + (y * min_dim) / kAiHeight;
      int src_idx = (y_cam * w + x_cam) * 2;
      int dst_idx = (y * kAiWidth + x) * 3;

      if (src_idx + 1 >= state().fb->len) {
        rgb_buf[dst_idx] = 0;
        rgb_buf[dst_idx + 1] = 0;
        rgb_buf[dst_idx + 2] = 0;
        continue;
      }

      uint8_t lo = state().fb->buf[src_idx];
      uint8_t hi = state().fb->buf[src_idx + 1];
      uint16_t pixel = (lo << 8) | hi;

      rgb_buf[dst_idx] = (pixel & 0x1F) * 255 / 31;
      rgb_buf[dst_idx + 1] = ((pixel >> 5) & 0x3F) * 255 / 63;
      rgb_buf[dst_idx + 2] = (pixel >> 11) * 255 / 31;
    }
  }

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(rgb_buf,
                           rgb_len,
                           kAiWidth,
                           kAiHeight,
                           PIXFORMAT_RGB888,
                           40,
                           &jpg_buf,
                           &jpg_len);

  free(rgb_buf);

  if (!converted) {
    Serial.println("JPEG compression failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=ai_color.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(jpg_buf), jpg_len);
  free(jpg_buf);
  return res;
}

static esp_err_t capture_handler(httpd_req_t* req) {
  if (!ensure_initialized(req) || !state().fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  size_t gray_len = kAiWidth * kAiHeight;
  uint8_t* gray_buf = static_cast<uint8_t*>(malloc(gray_len));

  if (!gray_buf) {
    Serial.println("ERR: OOM for Web Grayscale Buffer");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  const int w = state().fb->width;
  const int h = state().fb->height;
  const int min_dim = (w < h) ? w : h;
  const int start_x = (w - min_dim) / 2;
  const int start_y = (h - min_dim) / 2;

  for (int y = 0; y < kAiHeight; y++) {
    for (int x = 0; x < kAiWidth; x++) {
      int x_cam = start_x + (x * min_dim) / kAiWidth;
      int y_cam = start_y + (y * min_dim) / kAiHeight;
      int pixel_idx = (y_cam * w + x_cam) * 2;

      if (pixel_idx + 1 >= state().fb->len) {
        gray_buf[y * kAiWidth + x] = 0;
        continue;
      }

      uint8_t lo = state().fb->buf[pixel_idx];
      uint8_t hi = state().fb->buf[pixel_idx + 1];
      uint16_t pixel = (hi << 8) | lo;

      float r = ((pixel >> 11) & 0x1F) * 255.0f / 31.0f;
      float g = ((pixel >> 5) & 0x3F) * 255.0f / 63.0f;
      float b = (pixel & 0x1F) * 255.0f / 31.0f;

      gray_buf[y * kAiWidth + x] = static_cast<uint8_t>((r * 0.299f) + (g * 0.587f) + (b * 0.114f));
    }
  }

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(gray_buf,
                           gray_len,
                           kAiWidth,
                           kAiHeight,
                           PIXFORMAT_GRAYSCALE,
                           12,
                           &jpg_buf,
                           &jpg_len);

  free(gray_buf);

  if (!converted) {
    Serial.println("JPEG compression failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=ai_view.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(jpg_buf), jpg_len);
  free(jpg_buf);
  return res;
}

static esp_err_t download_handler(httpd_req_t* req) {
  if (!ensure_initialized(req)) {
    return ESP_FAIL;
  }

  if (state().fb) {
    esp_camera_fb_return(state().fb);
    state().fb = nullptr;
  }

  sensor_t* s = esp_camera_sensor_get();

  if (s) {
    s->set_reg(s, 0x3008, 0xff, 0x02);
    vTaskDelay(300 / portTICK_PERIOD_MS);
  }

  if (s) {
    s->set_reg(s, 0x3023, 0xff, 0x01);
    s->set_reg(s, 0x3022, 0xff, 0x03);

    uint8_t status = 0x00;
    unsigned long start_time = millis();

    while (status != 0x10) {
      status = s->get_reg(s, 0x3029, 0xff);

      if (status == 0x70) {
        Serial.println("AF Idle/Fail (0x70)");
        break;
      }
      if (status == 0xFF) {
        Serial.println("AF I2C Error (0xFF)");
        break;
      }
      if (millis() - start_time > 4000) {
        Serial.println("AF Timeout!");
        break;
      }
      delay(100);
    }
  }

  for (int i = 0; i < 3; i++) {
    camera_fb_t* temp = esp_camera_fb_get();
    if (temp) {
      esp_camera_fb_return(temp);
    }
    vTaskDelay(150 / portTICK_PERIOD_MS);
  }

  camera_fb_t* fb = esp_camera_fb_get();

  if (s) {
    s->set_reg(s, 0x3008, 0xff, 0x42);
  }

  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(fb->buf,
                           fb->len,
                           fb->width,
                           fb->height,
                           PIXFORMAT_RGB565,
                           80,
                           &jpg_buf,
                           &jpg_len);

  esp_camera_fb_return(fb);

  if (!converted) {
    Serial.println("JPEG compression failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(jpg_buf), jpg_len);
  free(jpg_buf);
  return res;
}

}  // namespace

void start_camera_http_server(MicrofoamResult* shared_result, ActionHandler action_callback) {
  g_result = shared_result;
  g_action_handler = action_callback;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 32768;
  httpd_handle_t camera_httpd = nullptr;

  httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr};
  httpd_uri_t action_uri = {.uri = "/action", .method = HTTP_GET, .handler = kActionUriHandler, .user_ctx = nullptr};
  httpd_uri_t capture_uri = {.uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = nullptr};
  httpd_uri_t color_uri = {.uri = "/capture_color", .method = HTTP_GET, .handler = capture_color_handler, .user_ctx = nullptr};
  httpd_uri_t full_uri = {.uri = "/capture_full", .method = HTTP_GET, .handler = capture_full_handler, .user_ctx = nullptr};
  httpd_uri_t download_uri = {.uri = "/download", .method = HTTP_GET, .handler = download_handler, .user_ctx = nullptr};

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &action_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &color_uri);
    httpd_register_uri_handler(camera_httpd, &full_uri);
    httpd_register_uri_handler(camera_httpd, &download_uri);
  }
}

}  // namespace web
}  // namespace coffee_cam
