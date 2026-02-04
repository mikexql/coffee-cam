#include "app/coffee_cam_app.h"
#include "app/coffee_cam_controller.h"

namespace coffee_cam {
namespace app {
namespace {

CoffeeCamApp g_app;

}  // namespace

void setup() {
  g_app.setup();
}

void loop() {
  g_app.loop();
}

}  // namespace app
}  // namespace coffee_cam
