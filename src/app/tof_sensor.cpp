#include "app/tof_sensor.h"

#include <Arduino.h>

namespace coffee_cam {
namespace app {

bool ToFSensor::begin() {
  Serial.println("Initializing VL53L0X Sensor...");
  if (!tof_.begin()) {
    Serial.println("ToF Failed");
    return false;
  }
  Serial.println(F("VL53L0X Ready!"));
  return true;
}

int ToFSensor::averaged_distance(int readings[3]) {
  long sum = 0;
  int valid = 0;

  for (int i = 0; i < 3; i++) {
    VL53L0X_RangingMeasurementData_t measure;
    tof_.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) {
      readings[i] = measure.RangeMilliMeter;
      sum += readings[i];
      valid++;
    } else {
      readings[i] = -1;
    }

    delay(10);
  }

  return (valid > 0) ? static_cast<int>(sum / valid) : 0;
}

}  // namespace app
}  // namespace coffee_cam
