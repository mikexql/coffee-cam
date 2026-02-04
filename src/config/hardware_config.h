#pragma once

#include <stdint.h>

namespace coffee_cam {
namespace config {

// Shared peripheral pin assignments
constexpr uint8_t kTofSdaPin = 4;
constexpr uint8_t kTofSclPin = 5;

constexpr uint8_t kRingPin = 14;
constexpr uint8_t kRingLedCount = 16;
constexpr uint8_t kRingBrightness = 80;

}  // namespace config
}  // namespace coffee_cam
