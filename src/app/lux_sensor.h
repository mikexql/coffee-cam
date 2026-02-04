#pragma once

#include "BH1750.h"

namespace coffee_cam {
namespace app {

class LuxSensor {
 public:
  bool begin();
  float read();
  void test();

 private:
  BH1750 lux_;
};

}  // namespace app
}  // namespace coffee_cam
