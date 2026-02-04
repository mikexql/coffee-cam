#include "app/volume_lut.h"

namespace coffee_cam {
namespace app {

const int VolumeLut::kHeightsAt10mlSteps[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  10, 12, 15, 16, 19, 21,
    24, 26, 28, 30, 32, 35, 38, 41, 44, 40, 42, 44, 46, 44,
    42, 40, 37, 34, 32, 30, 28, 26, 24,
};
const int VolumeLut::kLutSize = sizeof(VolumeLut::kHeightsAt10mlSteps) / sizeof(VolumeLut::kHeightsAt10mlSteps[0]);

float VolumeLut::volume_from_height(int measured_h) {
  if (measured_h <= 0) {
    return 0.0f;
  }

  for (int i = 0; i < kLutSize - 1; i++) {
    int h_low = kHeightsAt10mlSteps[i];
    int h_high = kHeightsAt10mlSteps[i + 1];

    if (measured_h >= h_low && measured_h <= h_high) {
      float range = static_cast<float>(h_high - h_low);
      float diff = static_cast<float>(measured_h - h_low);
      float fraction = (range == 0.0f) ? 0.0f : diff / range;
      float vol_low = static_cast<float>(i) * 10.0f;
      return vol_low + (fraction * 10.0f);
    }
  }

  return static_cast<float>((kLutSize - 1) * 10);
}

}  // namespace app
}  // namespace coffee_cam
