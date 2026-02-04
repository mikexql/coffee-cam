#include "app/lux_sensor.h"

#include <Arduino.h>

namespace coffee_cam {
namespace app {

bool LuxSensor::begin() {
  if (!lux_.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 init failed");
    return false;
  }
  Serial.println("BH1750 ready");
  return true;
}

float LuxSensor::read() {
  float lux = lux_.readLightLevel();
  Serial.printf("Lux: %.2f\n", lux);
  return lux;
}

void LuxSensor::test() {
  Serial.println("Starting Lux Test...");
  for (int i = 0; i < 5; i++) {
    read();
    delay(500);
  }
  Serial.println("Lux Test done.");
}

}  // namespace app
}  // namespace coffee_cam
