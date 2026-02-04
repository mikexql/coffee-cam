#pragma once

#include <Arduino.h>

#include "app/camera_pipeline.h"
#include "app/lux_sensor.h"
#include "app/ring_light.h"
#include "app/tof_sensor.h"
#include "microfoam_logic.h"

namespace coffee_cam {
namespace app {

class CoffeeCamApp {
 public:
  CoffeeCamApp();

  void setup();
  void loop();

 private:
  static void handle_action_trampoline(const String& cmd);
  void handle_action(const String& cmd);

  void init_logging();
  void init_sensors();
  void connect_wifi();

  static CoffeeCamApp* s_instance_;
  RingLight ring_;
  LuxSensor lux_;
  ToFSensor tof_;
  MicrofoamResult result_;
  CameraPipeline camera_;
};

}  // namespace app
}  // namespace coffee_cam
