#include "app/camera_pipeline.h"

#include <Arduino.h>

#include "ESP32_OV5640_AF.h"
#include "config/board_selector.h"
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "esp_camera.h"
#include <milk_inferencing.h>

namespace coffee_cam {
namespace app {

CameraPipeline* CameraPipeline::s_instance_ = nullptr;

CameraPipeline::CameraPipeline(RingLight& ring, MicrofoamResult& result)
    : ring_(ring), result_(result), loop_fb_(nullptr) {
  s_instance_ = this;
}

bool CameraPipeline::init() {
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
  config.pin_sccb_sda = -1;
  config.pin_sccb_scl = -1;
  config.sccb_i2c_port = 0;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 24000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_RGB565;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }
  return true;
}

void CameraPipeline::init_focus() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("Sensor pointer is null");
    return;
  }

  if (ov5640_.start(s)) {
    Serial.println("OV5640 AF Started");

    if (ov5640_.focusInit() == 0) {
      Serial.println("OV5640 Focus Init Successful");
    }

    if (ov5640_.autoFocusMode() == 0) {
      Serial.println("OV5640_Auto_Focus Successful!");
    }

    s->set_reg(s, 0x3008, 0xff, 0x42);
    Serial.println("Sensor in Sleep Mode. Ready.");
  }
}

void CameraPipeline::capture_to_result() {
  ring_.on(255, 0, 255);
  sensor_t* s = esp_camera_sensor_get();

  if (!s) {
    Serial.println("Sensor not available");
    return;
  }

  s->set_reg(s, 0x3008, 0xff, 0x02);
  vTaskDelay(300 / portTICK_PERIOD_MS);
  Serial.println("Sensor turned on");

  Serial.println("Starting Autofocus...");
  s->set_reg(s, 0x3023, 0xff, 0x01);
  s->set_reg(s, 0x3022, 0xff, 0x03);

  uint8_t status = 0x00;
  unsigned long start_time = millis();

  while (true) {
    status = s->get_reg(s, 0x3029, 0xff);

    if (status == 0x10) {
      break;
    }
    if (status == 0x70) {
      Serial.println("AF Idle/Fail");
      break;
    }
    if (status == 0xFF) {
      Serial.println("AF Error (I2C Fail)");
      break;
    }
    if (millis() - start_time > 4000) {
      Serial.println("AF Timeout!");
      break;
    }

    delay(100);
    ring_.off();
  }

  Serial.printf("Final AF Status: 0x%02X\n", status);
  delay(200);

  if (result_.fb) {
    esp_camera_fb_return(result_.fb);
    result_.fb = nullptr;
  }

  for (int i = 0; i < 3; i++) {
    camera_fb_t* temp = esp_camera_fb_get();
    if (temp) {
      esp_camera_fb_return(temp);
    }
    vTaskDelay(150 / portTICK_PERIOD_MS);
  }

  Serial.println("Capturing Photo...");
  result_.fb = esp_camera_fb_get();

  if (!result_.fb) {
    Serial.println("Camera capture failed");
    s->set_reg(s, 0x3008, 0xff, 0x42);
    Serial.println("Sensor in Sleep Mode.");
    return;
  }

  Serial.printf("Success! Photo size: %zu bytes\n", result_.fb->len);
  s->set_reg(s, 0x3008, 0xff, 0x42);
  delay(500);

  Serial.println("Running Inference...");
  loop_fb_ = result_.fb;

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &CameraPipeline::raw_feature_get_data;

  ei_impulse_result_t result = {0};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

  if (err != EI_IMPULSE_OK) {
    Serial.printf("ERR: Classifier failed (%d)\n", err);
    result_.ml_label = "Error";
    result_.ml_confidence = 0.0f;
  } else {
    float max_val = 0.0f;
    int best_idx = 0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      if (result.classification[ix].value > max_val) {
        max_val = result.classification[ix].value;
        best_idx = static_cast<int>(ix);
      }
    }

    result_.ml_label = result.classification[best_idx].label;
    result_.ml_confidence = max_val;

    Serial.printf("Prediction: %s (%.2f)\n", result_.ml_label, result_.ml_confidence);
  }

  loop_fb_ = nullptr;
  s->set_reg(s, 0x3008, 0xff, 0x42);
  Serial.println("Sensor in Sleep Mode.");
}

int CameraPipeline::raw_feature_get_data(size_t offset, size_t length, float* out_ptr) {
  if (!s_instance_) {
    return -1;
  }
  return s_instance_->feature_data(offset, length, out_ptr);
}

int CameraPipeline::feature_data(size_t offset, size_t length, float* out_ptr) {
  if (!loop_fb_ || !loop_fb_->buf) {
    return -1;
  }

  int min_dim = (loop_fb_->width < loop_fb_->height) ? loop_fb_->width : loop_fb_->height;
  int start_x = (loop_fb_->width - min_dim) / 2;
  int start_y = (loop_fb_->height - min_dim) / 2;

  for (size_t i = 0; i < length; i++) {
    int x_model = (offset + i) % EI_CLASSIFIER_INPUT_WIDTH;
    int y_model = (offset + i) / EI_CLASSIFIER_INPUT_WIDTH;

    int x_cam = start_x + (x_model * min_dim) / EI_CLASSIFIER_INPUT_WIDTH;
    int y_cam = start_y + (y_model * min_dim) / EI_CLASSIFIER_INPUT_HEIGHT;
    int pixel_idx = (y_cam * loop_fb_->width + x_cam) * 2;

    if (pixel_idx + 1 >= loop_fb_->len) {
      out_ptr[i] = 0;
      continue;
    }

    uint8_t lo = loop_fb_->buf[pixel_idx];
    uint8_t hi = loop_fb_->buf[pixel_idx + 1];
    uint16_t pixel = (lo << 8) | hi;

    float r = (pixel & 0x1F) * 255.0f / 31.0f;
    float g = ((pixel >> 5) & 0x3F) * 255.0f / 63.0f;
    float b = (pixel >> 11) * 255.0f / 31.0f;

    out_ptr[i] = (r * 0.299f) + (g * 0.587f) + (b * 0.114f);
  }

  return 0;
}

}  // namespace app
}  // namespace coffee_cam
