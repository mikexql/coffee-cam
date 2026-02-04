#include "app/ring_light.h"

#include <Arduino.h>

#include "config/hardware_config.h"

namespace coffee_cam {
namespace app {

using coffee_cam::config::kRingBrightness;
using coffee_cam::config::kRingLedCount;
using coffee_cam::config::kRingPin;

RingLight::RingLight() : ring_(kRingLedCount, kRingPin, NEO_GRB + NEO_KHZ800) {}

void RingLight::begin() {
  ring_.begin();
  ring_.show();
  Serial.println("Ring begin+show done");
}

void RingLight::on(uint8_t r, uint8_t g, uint8_t b) {
  Serial.printf("Ring ON r=%u g=%u b=%u\n", r, g, b);
  ring_.setBrightness(kRingBrightness);
  for (int i = 0; i < kRingLedCount; i++) {
    ring_.setPixelColor(i, ring_.Color(r, g, b));
  }
  ring_.show();
  Serial.println("Ring show done");
  delay(1000);
}

void RingLight::off() {
  Serial.println("Ring OFF");
  ring_.clear();
  ring_.show();
}

void RingLight::debug_test() {
  Serial.printf("Ring init: pin=%d count=%d brightness=%d\n", kRingPin, kRingLedCount, kRingBrightness);
  ring_.setBrightness(kRingBrightness);
  ring_.clear();
  ring_.show();
  Serial.println("Ring cleared");

  ring_.setPixelColor(0, ring_.Color(255, 0, 0));
  ring_.show();
  Serial.println("Ring pixel 0 set to red");
}

}  // namespace app
}  // namespace coffee_cam
