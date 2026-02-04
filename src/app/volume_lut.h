#pragma once

namespace coffee_cam {
namespace app {

class VolumeLut {
 public:
  static float volume_from_height(int measured_h);

 private:
  static const int kHeightsAt10mlSteps[];
  static const int kLutSize;
};

}  // namespace app
}  // namespace coffee_cam
