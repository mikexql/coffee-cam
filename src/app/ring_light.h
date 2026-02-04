#pragma once

#include <stdint.h>
#include <Adafruit_NeoPixel.h>

namespace coffee_cam {
namespace app {

class RingLight {
 public:
  RingLight();

  void begin();
  void on(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
  void off();
  void debug_test();

 private:
  Adafruit_NeoPixel ring_;
};

}  // namespace app
}  // namespace coffee_cam
