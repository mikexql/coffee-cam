#pragma once

#include <stddef.h>

#include "ESP32_OV5640_AF.h"
#include "app/ring_light.h"
#include "esp_camera.h"
#include "microfoam_logic.h"

namespace coffee_cam {
namespace app {

class CameraPipeline {
 public:
  CameraPipeline(RingLight& ring, MicrofoamResult& result);

  bool init();
  void init_focus();
  void capture_to_result();

 private:
  static int raw_feature_get_data(size_t offset, size_t length, float* out_ptr);
  int feature_data(size_t offset, size_t length, float* out_ptr);

  static CameraPipeline* s_instance_;
  RingLight& ring_;
  MicrofoamResult& result_;
  OV5640 ov5640_;
  camera_fb_t* loop_fb_;
};

}  // namespace app
}  // namespace coffee_cam
