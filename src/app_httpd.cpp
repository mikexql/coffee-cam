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
  char vol_str[32] = "0"; // <--- THIS WAS MISSING! Default to "0" string.
  char* buf;
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;

  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd));
      
      // Now this works because vol_str is declared
      httpd_query_key_value(buf, "vol", vol_str, sizeof(vol_str)); 
    }
    free(buf);
  }

  // Update currentResult with the manual volume
  // If "vol" wasn't in the URL, it uses the default "0", so atof returns 0.0
  currentResult.liquid_v = atof(vol_str); 

  performAction(String(cmd));

  // Package the results into JSON (Include AI Data!)
  char json[400]; 

  sprintf(json, 
    "{\"raw\":[%d,%d,%d],\"avg\":%d,\"empty\":%d,\"start\":%d,\"end\":%d,\"init_h\":%d,\"final_h\":%d,\"pct\":%.1f,\"status\":\"%s\",\"ml_label\":\"%s\",\"ml_conf\":%.2f}",
    currentResult.raw[0], currentResult.raw[1], currentResult.raw[2], currentResult.avg,
    currentResult.empty, currentResult.start, currentResult.end,
    currentResult.init_h, currentResult.final_h, currentResult.pct, currentResult.status,
    currentResult.ml_label, currentResult.ml_confidence 
  );

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

// Define AI dimensions locally since we don't include the AI library here
#define AI_WIDTH 96
#define AI_HEIGHT 96

// Handler for Full 320x240 Raw View
static esp_err_t capture_full_handler(httpd_req_t *req) {
  if (!currentResult.fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  uint8_t *jpg_buf = NULL;
  size_t jpg_len = 0;

  // Convert the full 320x240 RGB565 buffer to JPEG
  bool converted = fmt2jpg(
      currentResult.fb->buf, 
      currentResult.fb->len, 
      currentResult.fb->width, 
      currentResult.fb->height, 
      PIXFORMAT_RGB565, 
      30, // Quality
      &jpg_buf,    
      &jpg_len     
  );

  if(!converted){
      Serial.println("JPEG compression failed");
      httpd_resp_send_500(req);
      return ESP_FAIL;
  }
    
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=full.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  esp_err_t res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
  free(jpg_buf);
  return res;
}

// Handler for Cropped + Resized (Color) View
static esp_err_t capture_color_handler(httpd_req_t *req) {
  if (!currentResult.fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // 1. Allocate a buffer for the Color AI-processed image
  // Size = 96 * 96 * 3 bytes per pixel (RGB888)
  // We use RGB888 because that is what the JPEG encoder expects
  size_t rgb_len = AI_WIDTH * AI_HEIGHT * 3;
  uint8_t *rgb_buf = (uint8_t *)malloc(rgb_len);

  if (!rgb_buf) {
    Serial.println("ERR: OOM for Web Color Buffer");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // 2. Perform the Crop & Resize (Identical logic to AI Helper)
  int w = currentResult.fb->width;
  int h = currentResult.fb->height;
  int min_dim = (w < h) ? w : h;        // 240
  int start_x = (w - min_dim) / 2;      // 40
  int start_y = (h - min_dim) / 2;      // 0

  for (int y = 0; y < AI_HEIGHT; y++) {
    for (int x = 0; x < AI_WIDTH; x++) {
      
      // Map 96x96 pixel to the 240x240 Crop Window
      int x_cam = start_x + (x * min_dim) / AI_WIDTH;
      int y_cam = start_y + (y * min_dim) / AI_HEIGHT;

      // Index in Source Buffer (RGB565)
      int src_idx = (y_cam * w + x_cam) * 2;

      // Index in Destination Buffer (RGB888)
      int dst_idx = (y * AI_WIDTH + x) * 3;

      if (src_idx + 1 >= currentResult.fb->len) {
         rgb_buf[dst_idx] = 0;
         rgb_buf[dst_idx+1] = 0;
         rgb_buf[dst_idx+2] = 0;
         continue;
      }

      // Read RGB565
      uint8_t lo = currentResult.fb->buf[src_idx];
      uint8_t hi = currentResult.fb->buf[src_idx + 1];
      uint16_t pixel = (hi << 8) | lo;

      // Convert RGB565 -> RGB888 (Color!)
      rgb_buf[dst_idx] = ((pixel >> 11) & 0x1F) * 255 / 31;     // Red
      rgb_buf[dst_idx+1] = ((pixel >> 5) & 0x3F) * 255 / 63;    // Green
      rgb_buf[dst_idx+2] = (pixel & 0x1F) * 255 / 31;           // Blue
    }
  }

  // 3. Convert to JPEG
  uint8_t *jpg_buf = NULL;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(
      rgb_buf, 
      rgb_len, 
      AI_WIDTH, 
      AI_HEIGHT, 
      PIXFORMAT_RGB888,    // Tell converter this is full color
      40,                  // Quality
      &jpg_buf,    
      &jpg_len     
  );

  free(rgb_buf); // Free the raw buffer

  if(!converted){
      Serial.println("JPEG compression failed");
      httpd_resp_send_500(req);
      return ESP_FAIL;
  }
    
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=ai_color.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  esp_err_t res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
  free(jpg_buf);
  return res;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  if (!currentResult.fb) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // 1. Allocate a buffer for the AI-processed image (96x96 Grayscale)
  // Size = 96 * 96 * 1 byte per pixel
  size_t gray_len = AI_WIDTH * AI_HEIGHT;
  uint8_t *gray_buf = (uint8_t *)malloc(gray_len);

  if (!gray_buf) {
    Serial.println("ERR: OOM for Web Grayscale Buffer");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // 2. Perform the Crop, Resize, and Grayscale conversion
  // This logic MATCHES your AI Helper to show exactly what the AI sees.
  
  int w = currentResult.fb->width;
  int h = currentResult.fb->height;
  int min_dim = (w < h) ? w : h;        // 240
  int start_x = (w - min_dim) / 2;      // 40 (Left offset)
  int start_y = (h - min_dim) / 2;      // 0  (Top offset)

  for (int y = 0; y < AI_HEIGHT; y++) {
    for (int x = 0; x < AI_WIDTH; x++) {
      
      // A. Map 96x96 pixel to the 240x240 Crop Window
      int x_cam = start_x + (x * min_dim) / AI_WIDTH;
      int y_cam = start_y + (y * min_dim) / AI_HEIGHT;

      // B. Calculate Index in Source Buffer (RGB565 = 2 bytes)
      int pixel_idx = (y_cam * w + x_cam) * 2;

      // Safety Check
      if (pixel_idx + 1 >= currentResult.fb->len) {
         gray_buf[y * AI_WIDTH + x] = 0;
         continue;
      }

      // C. Read RGB565
      uint8_t lo = currentResult.fb->buf[pixel_idx];
      uint8_t hi = currentResult.fb->buf[pixel_idx + 1];
      uint16_t pixel = (hi << 8) | lo;

      // D. Convert to Grayscale (Luminance)
      float r = ((pixel >> 11) & 0x1F) * 255.0f / 31.0f;
      float g = ((pixel >> 5) & 0x3F) * 255.0f / 63.0f;
      float b = (pixel & 0x1F) * 255.0f / 31.0f;
      
      // Store as 8-bit integer (0-255)
      gray_buf[y * AI_WIDTH + x] = (uint8_t)((r * 0.299f) + (g * 0.587f) + (b * 0.114f));
    }
  }

  // 3. Convert the 96x96 Grayscale buffer to JPEG for the browser
  uint8_t *jpg_buf = NULL;
  size_t jpg_len = 0;

  bool converted = fmt2jpg(
      gray_buf, 
      gray_len, 
      AI_WIDTH, 
      AI_HEIGHT, 
      PIXFORMAT_GRAYSCALE, // Tell the converter this is 1-byte grayscale
      12,                  // Quality (10-63). 30 is fine for debug views.
      &jpg_buf,    
      &jpg_len     
  );

  // Free the raw grayscale buffer immediately
  free(gray_buf);

  if(!converted){
      Serial.println("JPEG compression failed");
      httpd_resp_send_500(req);
      return ESP_FAIL;
  }
    
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=ai_view.jpg");
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
  httpd_uri_t color_uri = { .uri = "/capture_color", .method = HTTP_GET, .handler = capture_color_handler, .user_ctx = NULL };
  httpd_uri_t full_uri = { .uri = "/capture_full", .method = HTTP_GET, .handler = capture_full_handler, .user_ctx = NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &action_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &color_uri);
    httpd_register_uri_handler(camera_httpd, &full_uri);
  }
}