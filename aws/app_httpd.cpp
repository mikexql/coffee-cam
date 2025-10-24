#include "esp_camera.h" 
#include <WiFi.h>
#include <esp_wpa2.h>
#include <HTTPClient.h>
#include "board_config.h"
#include "secrets.h"



// ===== API Gateway Endpoint (Modify this with your own URL) =====
const char* serverURL = "https://xyc415wedc.execute-api.us-east-1.amazonaws.com/esp32-s3/bkt-whatever/"; // <-- CHANGE TO YOUR ENDPOINT

// ===== Photo Capture Control =====
int photo_count = 0;
const int max_photos = 1;

// ===== Camera Configuration (AI Thinker) =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===== Connect to WPA2-Enterprise WiFi =====
void connectToWiFi() {
  WiFi.disconnect(true);
  delay(1000);

  WiFi.mode(WIFI_STA);
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t*)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t*)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
  esp_wifi_sta_wpa2_ent_enable();

  WiFi.begin(WIFI_SSID);
  Serial.print("Connecting to WiFi");
  int count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++count > 40) {
      Serial.println("\nFailed to connect to WiFi");
      return;
    }
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// ===== Capture Photo and Upload to HTTP Endpoint =====
void takePhotoAndUpload() {
  if(photo_count >= max_photos){
    Serial.println("Reached max photos. Skipping further captures.");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if(!fb){
    Serial.println("Camera capture failed");
    return;
  }

  // Generate file name: timestamp + .jpg
  String filename = "photo_" + String(millis()) + ".jpg";
  String url = String(serverURL) + filename;

  Serial.printf("Uploading to %s ...\n", url.c_str());

  HTTPClient http;
  http.begin(url);  // HTTP/HTTPS
  http.addHeader("Content-Type", "image/jpg");

  int httpResponseCode = http.PUT(fb->buf, fb->len); // Upload raw binary data
  if(httpResponseCode > 0){
    Serial.printf("Upload response code: %d\n", httpResponseCode);
  } else {
    Serial.printf("HTTP PUT failed: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
  esp_camera_fb_return(fb);

  photo_count++;
}

// ===== Arduino setup =====
void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting ESP32-CAM...");

  connectToWiFi();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 20;
  config.fb_count = 1;

  if(esp_camera_init(&config) != ESP_OK){
    Serial.println("Camera init failed!");
    return;
  }

  Serial.println("Setup complete. Ready to take photos.");
}

// ===== Arduino loop =====
void loop() {
  if(photo_count < max_photos){
    takePhotoAndUpload();
  } else {
    Serial.println("Reached max photos. Stopping further captures.");
    while(true){ delay(10000); } // Stop loop after max photos
  }

  delay(80000); // Delay between photos, can adjust as needed
}