#include "esp_http_server.h"
#include "esp_camera.h"
#include "camera_index.h"
#include "Arduino.h"
#include "microfoam_logic.h"
#include "img_converters.h" // Required for RGB565 -> JPEG conversion

// Access the objects from CameraWebServer.ino
extern struct MicrofoamResult currentResult;
void performAction(String cmd);

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_ov5640_html, strlen(index_ov5640_html));
}

static esp_err_t action_handler(httpd_req_t *req) {
  char cmd[32] = {0};
  char* buf;
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;

  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd));
    }
    free(buf);
  }

  performAction(String(cmd));

  // Package the results into JSON (Include AI Data!)
  char json[400]; // Increased buffer size

  sprintf(json, 
    "{\"raw\":[%d,%d,%d],\"avg\":%d,\"empty\":%d,\"start\":%d,\"end\":%d,\"init_h\":%d,\"final_h\":%d,\"pct\":%.1f,\"status\":\"%s\",\"ml_label\":\"%s\",\"ml_conf\":%.2f}",
    currentResult.raw[0], currentResult.raw[1], currentResult.raw[2], currentResult.avg,
    currentResult.empty, currentResult.start, currentResult.end,
    currentResult.init_h, currentResult.final_h, currentResult.pct, currentResult.status,
    currentResult.ml_label, currentResult.ml_confidence // Added AI fields
  );

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t capture_handler(httpd_req_t *req) {
  if (!currentResult.fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  
  // CONVERT RGB565 -> JPEG for Browser Display
  uint8_t * jpg_buf = NULL;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(
      currentResult.fb->buf, 
      currentResult.fb->len, 
      currentResult.fb->width, 
      currentResult.fb->height, 
      PIXFORMAT_RGB565, 
      12,          // Quality (10-15 is good balance)
      &jpg_buf,    
      &jpg_len     
  );

  if(!converted){
      Serial.println("JPEG compression failed");
      httpd_resp_send_500(req);
      return ESP_FAIL;
  }
    
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  esp_err_t res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);

  free(jpg_buf); // Free the temp JPEG buffer
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 32768;
  httpd_handle_t camera_httpd = NULL;

  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
  httpd_uri_t action_uri = { .uri = "/action", .method = HTTP_GET, .handler = action_handler, .user_ctx = NULL };
  httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &action_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }
}