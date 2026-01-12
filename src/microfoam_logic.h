#ifndef MICROFOAM_LOGIC_H
#define MICROFOAM_LOGIC_H

#include "esp_camera.h"

// This is the "Envelope" or "Package" definition
struct MicrofoamResult {
  int raw[3];
  int avg;
  int empty;
  int start;
  int end;
  int init_h;
  int final_h;
  float pct;
  const char* status;
  const char* ml_label;
  float ml_confidence;
  camera_fb_t * fb; 
};

#endif