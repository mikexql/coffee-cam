#pragma once

#include <Arduino.h>
#include "microfoam_logic.h"

namespace coffee_cam {
namespace web {

using ActionHandler = void (*)(const String& cmd);

void start_camera_http_server(MicrofoamResult* shared_result, ActionHandler action_callback);

}  // namespace web
}  // namespace coffee_cam
