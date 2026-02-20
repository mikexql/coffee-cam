#include "esp_camera.h"
#include <WiFi.h>
#include "secrets.h"
#include "board_config.h"
#include "microfoam_logic.h"
#include "ring_light.h"
#include "lux_sensor.h"
#include "tof_sensor.h"
#include "camera_module.h"

#include <milk_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "esp_log.h"

#include <stdarg.h>
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

// Define I2C Pins for shared i2c
#define TOF_SDA 4
#define TOF_SCL 5

// Pick the GPIO your ringlight data line uses (change to your wiring)
#define RING_PIN 14
#define RING_COUNT 1
#define RING_BRIGHTNESS 155
const int positions[] = {6};

// Global instances
Camera camera("Camera", true, true);
// LuxSensor luxSensor("LuxSensor", BH1750::CONTINUOUS_HIGH_RES_MODE, 1.0f, 3);
TofSensor tofSensor("ToF", false, 200, 3);
// RingLight ringLight("RingLight", RING_PIN, RING_COUNT);

// Initialize all fields of microfoam_logic
MicrofoamResult currentResult = {{0, 0, 0}, 0, 0, 0, 0, 0, 0, 0.0, 0.0, "--", "--", 0.0, NULL};

// Global helper for AI buffer
static camera_fb_t *loop_fb = NULL;

bool useAdaptiveLighting = true;

// Tunables
const int LUX_DEADBAND = 5; // your suggested deadband
const int MAX_STEPS = 16;   // safety limit for binary search
const int RAMP_STEP = 32;   // coarse brightness step for ramp
const int SETTLE_MS = 150;  // time for sensor & light to stabilise

// void setLux(int target_lux)
// {
//   Serial.printf("Setting target lux: %d\n", target_lux);

//   // --- Phase 0: start from dark ---
//   uint8_t brightness = 0;
//   ringLight.setBrightness(brightness);
//   // ringLight.onWithPositions(positions, 255, 255, 255);
//   ringLight.on(255, 255, 255);
//   delay(SETTLE_MS);

//   float lux = luxSensor.read().value;
//   Serial.printf("Initial lux: %.2f (bright=%u)\n", lux, brightness);

//   // Edge case: already bright enough from ambient
//   if (lux >= target_lux - LUX_DEADBAND)
//   {
//     Serial.println("Ambient light already within target range, no adjustment needed.");
//     return;
//   }

//   // --- Phase 1: ramp up until we cross the target or hit max brightness ---
//   uint8_t lowB = brightness; // brightness known to be too dark
//   float lowLux = lux;

//   uint8_t highB = 255; // will be updated once we cross
//   float highLux = lux;

//   while (lux < target_lux - LUX_DEADBAND && brightness < 255)
//   {
//     // coarse step up
//     brightness = (uint8_t)min<int>(brightness + RAMP_STEP, 255);
//     ringLight.setBrightness(brightness);
//     delay(SETTLE_MS);
//     lux = luxSensor.read().value;

//     Serial.printf("[RAMP] bright=%u, lux=%.2f\n", brightness, lux);

//     if (lux < target_lux - LUX_DEADBAND)
//     {
//       lowB = brightness;
//       lowLux = lux;
//     }
//     else
//     {
//       highB = brightness;
//       highLux = lux;
//       break; // we crossed the target
//     }
//   }

//   // If even at max brightness we never reached target_lux, just keep max
//   if (brightness == 255 && lux < target_lux - LUX_DEADBAND)
//   {
//     Serial.println("Warning: cannot reach target lux even at max brightness.");
//     return;
//   }

//   // --- Phase 2: binary search between lowB and highB ---
//   for (int step = 0; step < MAX_STEPS; step++)
//   {
//     // If already good enough, stop
//     float err = target_lux - lux;
//     if (fabs(err) <= LUX_DEADBAND)
//     {
//       Serial.printf("Converged: lux=%.2f within deadband ±%d (bright=%u)\n",
//                     lux, LUX_DEADBAND, brightness);
//       break;
//     }

//     // Classic binary search on brightness
//     uint8_t midB = (uint8_t)((lowB + highB) / 2);
//     ringLight.setBrightness(midB);
//     delay(SETTLE_MS);
//     float midLux = luxSensor.read().value;

//     Serial.printf("[BIN] low=(%u, %.2f) mid=(%u, %.2f) high=(%u, %.2f)\n",
//                   lowB, lowLux, midB, midLux, highB, highLux);

//     // Decide which side the target is on
//     if (midLux < target_lux - LUX_DEADBAND)
//     {
//       // still too dark → move low up
//       lowB = midB;
//       lowLux = midLux;
//     }
//     else if (midLux > target_lux + LUX_DEADBAND)
//     {
//       // too bright → move high down
//       highB = midB;
//       highLux = midLux;
//     }
//     else
//     {
//       // inside deadband → good enough
//       brightness = midB;
//       lux = midLux;
//       Serial.printf("Binary search landed inside deadband: lux=%.2f, bright=%u\n",
//                     lux, brightness);
//       break;
//     }

//     brightness = midB;
//     lux = midLux;
//   }

//   Serial.printf("Final lux: %.2f at brightness=%u\n", lux, brightness);
// }

void startCameraServer(); // refer to app_httpd

// --- REPLACEMENT AI Helper: Center Crops then Resizes ---
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr)
{
  if (!loop_fb || !loop_fb->buf)
    return -1;

  // 1. Calculate Crop Offsets (Fit Shortest Axis)
  // We identify the 240x240 square in the center of the 320x240 image.
  int min_dim = (loop_fb->width < loop_fb->height) ? loop_fb->width : loop_fb->height; // 240
  int start_x = (loop_fb->width - min_dim) / 2;                                        // (320 - 240) / 2 = 40 pixels (Left/Right Crop)
  int start_y = (loop_fb->height - min_dim) / 2;                                       // 0 pixels (Top/Bottom Crop)

  // 2. Iterate through the AI's requested pixels (96x96)
  for (size_t i = 0; i < length; i++)
  {
    // A. Calculate which pixel the AI needs (0..96)
    int x_model = (offset + i) % EI_CLASSIFIER_INPUT_WIDTH;
    int y_model = (offset + i) / EI_CLASSIFIER_INPUT_WIDTH;

    // B. Map that model pixel to the CROP window in the camera buffer
    // instead of the full width. This effectively "zooms in" to the center.
    int x_cam = start_x + (x_model * min_dim) / EI_CLASSIFIER_INPUT_WIDTH;
    int y_cam = start_y + (y_model * min_dim) / EI_CLASSIFIER_INPUT_HEIGHT;

    // C. Calculate the index in the 1D buffer (RGB565 = 2 bytes per pixel)
    int pixel_idx = (y_cam * loop_fb->width + x_cam) * 2;

    // Safety check to prevent crashing if index is out of bounds
    if (pixel_idx + 1 >= loop_fb->len)
    {
      out_ptr[i] = 0;
      continue;
    }

    // D. Read the pixel (RGB565 format)
    uint8_t lo = loop_fb->buf[pixel_idx];
    uint8_t hi = loop_fb->buf[pixel_idx + 1];
    uint16_t pixel = (lo << 8) | hi;

    // E. Convert RGB565 -> RGB888
    float r = ((pixel & 0x1F) & 0x1F) * 255.0f / 31.0f; // comment out if only using green
    float g = ((pixel >> 5) & 0x3F) * 255.0f / 63.0f;   // change to float g = ((pixel >> 5) & 0x3F) if only using green
    float b = (pixel >> 11) * 255.0f / 31.0f;           // comment out if only using green

    // F. Convert to Grayscale (Luminance)
    out_ptr[i] = (r * 0.299f) + (g * 0.587f) + (b * 0.114f); // change to out_ptr[i] = g * 255.0f / 63.0f; if only using green
  }

  return 0;
}

// --- Camera Module ---
void captureToResult()
{
  // if (useAdaptiveLighting)
  // {
  //   setLux(100); // target lux for consistent lighting
  // }
  // else
  // {
  //   ringLight.setBrightness(0);
  //   ringLight.on();
  // }

  if (currentResult.fb)
  {
    esp_camera_fb_return(currentResult.fb);
    currentResult.fb = NULL;
  }

  Serial.println("Capturing Photo...");
  currentResult.fb = camera.capture(true);

  if (!currentResult.fb)
  {
    Serial.println("Camera capture failed");
    // ringLight.off();
    return;
  }

  Serial.printf("Success! Photo size: %zu bytes\n", currentResult.fb->len);
  // ringLight.off();
  delay(500);

  // --- START AI INFERENCE ---
  Serial.println("Running Inference...");

  // Set global pointer so callback can read it
  loop_fb = currentResult.fb;

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &raw_feature_get_data;

  ei_impulse_result_t result = {0};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

  if (err != EI_IMPULSE_OK)
  {
    Serial.printf("ERR: Classifier failed (%d)\n", err);
    currentResult.ml_label = "Error";
    currentResult.ml_confidence = 0.0;
  }
  else
  {
    // Find highest confidence prediction
    float max_val = 0.0;
    int best_idx = 0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
    {
      if (result.classification[ix].value > max_val)
      {
        max_val = result.classification[ix].value;
        best_idx = ix;
      }
    }

    currentResult.ml_label = result.classification[best_idx].label;
    currentResult.ml_confidence = max_val;

    Serial.printf("Prediction: %s (%.2f)\n", currentResult.ml_label, currentResult.ml_confidence);
  }

  // Clear global pointer (don't free the FB yet, webserver needs it!)
  loop_fb = NULL;
  // --- END AI INFERENCE ---
}

// --- Lookup Table & Helpers ---
const int heightsAt10mlSteps[] = {
    0,  // 0ml
    1,  // 10ml
    2,  // 20ml
    3,  // 30ml
    4,  // 40ml
    5,  // 50ml
    6,  // 60ml
    7,  // 70ml
    10, // 80ml
    12, // 90ml
    15, // 100ml
    16, // 110ml
    19, // 120ml
    21, // 130ml
    24, // 140ml
    26, // 150ml
    28, // 160ml
    30, // 170ml
    32, // 180ml
    35, // 190ml
    38, // 200ml
    41, // 210ml
    44, // 220ml
    40, // 230ml
    42, // 240ml
    44, // 250ml
    46, // 260ml
    44, // 270ml
    42, // 280ml
    40, // 290ml
    37, // 300ml
    34, // 310ml
    32, // 320ml
    30, // 330ml
    28, // 340ml
    26, // 350ml
    24, // 360ml
};

// Calculate total array size automatically
const int lutSize = sizeof(heightsAt10mlSteps) / sizeof(heightsAt10mlSteps[0]);

float getVolumeFromHeight(int measured_h)
{
  if (measured_h <= 0)
    return 0.0;

  // 1. Iterate through the table to find where this height fits
  for (int i = 0; i < lutSize - 1; i++)
  {
    int h_low = heightsAt10mlSteps[i];
    int h_high = heightsAt10mlSteps[i + 1];

    // Check if our measurement falls between these two steps
    if (measured_h >= h_low && measured_h <= h_high)
    {

      // 2. Interpolate: Calculate exactly where we are between the two steps
      float range = h_high - h_low;
      float diff = measured_h - h_low;
      float fraction = diff / range; // e.g., 0.5 if we are halfway

      // Base volume is index * 10ml
      float vol_low = i * 10.0;

      return vol_low + (fraction * 10.0);
    }
  }

  // Fallback: If height is higher than our table goes, extrapolate
  return (lutSize - 1) * 10.0;
}

// --- Logic Module ---
void performAction(String cmd)
{
  if (cmd == "reset")
  {
    currentResult.start = 0;
    currentResult.end = 0;
    currentResult.status = "--";
    return;
  }

  int avg = tofSensor.read().value; // Use ToF Sensor
  Serial.printf("ToF Average Distance: %d mm\n", avg);
  currentResult.avg = avg;

  if (cmd == "empty")
  {
    currentResult.empty = avg;
  }

  else if (cmd == "end")
  {
    currentResult.end = avg;
    currentResult.final_h = currentResult.empty - currentResult.end;

    // 1. Convert height to total volume using LUT
    float totalVolume = getVolumeFromHeight(currentResult.final_h);

    // 2. Expansion % = ((Total Vol - Liquid Vol) / Liquid Vol) * 100
    if (currentResult.liquid_v > 0)
    {
      currentResult.pct = ((totalVolume - currentResult.liquid_v) / currentResult.liquid_v) * 100.0;
    }
    else
    {
      // Fallback if user forgot to enter volume: Use old Height math
      if (currentResult.init_h > 0)
      {
        currentResult.pct = ((float)(currentResult.final_h - currentResult.init_h) / currentResult.init_h) * 100.0;
      }
      else
      {
        currentResult.pct = 0;
      }
    }

    // 3. Status Logic
    if (currentResult.pct < 10)
      currentResult.status = "UNDERFROTHED";
    else if (currentResult.pct > 50)
      currentResult.status = "OVERLY FROTHY";
    else
      currentResult.status = "WELL FROTHED";

    captureToResult(); // Trigger camera immediately with ToF
  }
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable Brownout Detector
  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  Serial.println();

  esp_log_level_set("camera", ESP_LOG_VERBOSE);
  esp_log_level_set("cam_hal", ESP_LOG_VERBOSE);

  // --- 1. CONFIGURE CAMERA (Primary Device) ---
  Serial.println("Initializing Camera...");
  if (!camera.initialize())
  {
    Serial.println("Camera Init Failed");
    return;
  }

  // Join the I2C Bus that the camera just started
  Wire.begin(TOF_SDA, TOF_SCL);

  // SPEED LIMIT: Force 100kHz so we don't crash the Camera
  Wire.setClock(100000);

  // --- 2. CONFIGURE TOF SENSOR (Secondary Device) ---
  // Serial.println("Initializing VL53L0X Sensor...");

  Serial.println("Initializing ToF Sensor...");
  if (!tofSensor.initialize())
  {
    Serial.println("ToF Failed");
  }

  // Serial.println("Initializing BH1750 Lux Sensor...");
  // if (!luxSensor.initialize())
  // {
  //   Serial.println("Lux sensor init failed");
  // }

  // ringLight.initialize();
  // for (int i = 0; i < RING_COUNT; ++i)
  // {
  //   const int pos[] = {i};
  //   ringLight.onWithPositions(pos, 255, 255, 255);
  //   delay(300);
  // }
  // ringLight.off();
  // ringLight.setBrightness(RING_BRIGHTNESS);

  WiFi.begin(ssid, password);
  WiFi.setSleep(false); // Wifi active 100% of the time

  Serial.print("WiFi connecting");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer(); // launch button for web interface: refer to app_httpd

  Serial.print("Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop()
{
  delay(1000);
}