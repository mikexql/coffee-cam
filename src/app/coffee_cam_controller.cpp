#include "app/coffee_cam_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include "app/volume_lut.h"
#include "config/hardware_config.h"
#include "esp_log.h"
#include "secrets.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "web/http_server.h"

namespace coffee_cam {
namespace app {

using coffee_cam::config::kTofSclPin;
using coffee_cam::config::kTofSdaPin;

CoffeeCamApp* CoffeeCamApp::s_instance_ = nullptr;

CoffeeCamApp::CoffeeCamApp()
    : result_{{0, 0, 0}, 0, 0, 0, 0, 0, 0, 0.0, 0.0, "--", "--", 0.0, nullptr},
      camera_(ring_, result_) {}

void CoffeeCamApp::setup() {
  s_instance_ = this;
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println();

  init_logging();

  Wire.begin(kTofSdaPin, kTofSclPin);
  Wire.setClock(100000);

  if (!camera_.init()) {
    return;
  }

  init_sensors();
  camera_.init_focus();

  ring_.begin();
  ring_.debug_test();
  connect_wifi();

  coffee_cam::web::start_camera_http_server(&result_, &CoffeeCamApp::handle_action_trampoline);

  Serial.print("Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void CoffeeCamApp::loop() {
  delay(1000);
}

void CoffeeCamApp::handle_action_trampoline(const String& cmd) {
  if (s_instance_) {
    s_instance_->handle_action(cmd);
  }
}

void CoffeeCamApp::handle_action(const String& cmd) {
  if (cmd == "reset") {
    result_.start = 0;
    result_.end = 0;
    result_.status = "--";
    return;
  }

  result_.avg = tof_.averaged_distance(result_.raw);

  if (cmd == "empty") {
    result_.empty = result_.avg;
    return;
  }

  if (cmd == "end") {
    result_.end = result_.avg;
    result_.final_h = result_.empty - result_.end;

    float total_volume = VolumeLut::volume_from_height(result_.final_h);

    if (result_.liquid_v > 0) {
      result_.pct = ((total_volume - result_.liquid_v) / result_.liquid_v) * 100.0f;
    } else if (result_.init_h > 0) {
      result_.pct = ((static_cast<float>(result_.final_h - result_.init_h)) / result_.init_h) * 100.0f;
    } else {
      result_.pct = 0.0f;
    }

    if (result_.pct < 10) {
      result_.status = "UNDERFROTHED";
    } else if (result_.pct > 50) {
      result_.status = "OVERLY FROTHY";
    } else {
      result_.status = "WELL FROTHED";
    }

    camera_.capture_to_result();
  }
}

void CoffeeCamApp::init_logging() {
  esp_log_level_set("camera", ESP_LOG_NONE);
  esp_log_level_set("cam_hal", ESP_LOG_NONE);
}

void CoffeeCamApp::init_sensors() {
  tof_.begin();
  if (lux_.begin()) {
    lux_.test();
  }
}

void CoffeeCamApp::connect_wifi() {
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
}

}  // namespace app
}  // namespace coffee_cam
