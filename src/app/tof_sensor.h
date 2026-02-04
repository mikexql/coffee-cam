#pragma once

#include "Adafruit_VL53L0X.h"

namespace coffee_cam {
namespace app {

class ToFSensor {
 public:
  bool begin();
  int averaged_distance(int readings[3]);

 private:
  Adafruit_VL53L0X tof_;
};

}  // namespace app
}  // namespace coffee_cam
