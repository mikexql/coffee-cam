# 1 "C:\\Users\\mclok\\AppData\\Local\\Temp\\tmp5kpsc10c"
#include <Arduino.h>
# 1 "C:/Users/mclok/Documents/PlatformIO/coffee-cam/src/CameraWebServer.ino"
#include "esp_camera.h"
#include <WiFi.h>
#include "secrets.h"
#include "board_config.h"
#include "microfoam_logic.h"
#include "ring_light.h"
#include "lux_sensor.h"
#include "tof_sensor.h"
#include "camera_module.h"
#include "screen/screen.h"

#include <milk_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "esp_log.h"

#include <stdarg.h>
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"


#define TOF_SDA 4
#define TOF_SCL 5


#define RING_PIN 14
#define RING_COUNT 12
#define RING_BRIGHTNESS 155


Camera camera("Camera", true, true);
LuxSensor luxSensor("LuxSensor", BH1750::CONTINUOUS_HIGH_RES_MODE, 1.0f, 3);
TofSensor tofSensor("ToF", false, 200, 3);
RingLight ringLight("RingLight", RING_PIN, RING_COUNT);
Screen screen("Screen");


MicrofoamResult currentResult = {{0, 0, 0}, 0, 0, 0, 0, 0, 0, 0.0, 0.0, "--", "--", 0.0, NULL};


static camera_fb_t *loop_fb = NULL;


const int LUX_DEADBAND = 8;
const int MAX_STEPS = 16;
const int RAMP_STEP = 32;
const int SETTLE_MS = 150;
void setLux(int target_lux);
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr);
void captureToResult();
float getVolumeFromHeight(int measured_h);
void performAction(String cmd);
void setup();
void loop();
#line 51 "C:/Users/mclok/Documents/PlatformIO/coffee-cam/src/CameraWebServer.ino"
void setLux(int target_lux)
{
  Serial.printf("Setting target lux: %d\n", target_lux);


  uint8_t brightness = 0;
  ringLight.setBrightness(brightness);
  ringLight.on();
  delay(SETTLE_MS);

  float lux = luxSensor.read().value;
  Serial.printf("Initial lux: %.2f (bright=%u)\n", lux, brightness);


  if (lux >= target_lux - LUX_DEADBAND)
  {
    Serial.println("Ambient light already within target range, no adjustment needed.");
    return;
  }


  uint8_t lowB = brightness;
  float lowLux = lux;

  uint8_t highB = 255;
  float highLux = lux;

  while (lux < target_lux - LUX_DEADBAND && brightness < 255)
  {

    brightness = (uint8_t)min<int>(brightness + RAMP_STEP, 255);
    ringLight.setBrightness(brightness);
    delay(SETTLE_MS);
    lux = luxSensor.read().value;

    Serial.printf("[RAMP] bright=%u, lux=%.2f\n", brightness, lux);

    if (lux < target_lux - LUX_DEADBAND)
    {
      lowB = brightness;
      lowLux = lux;
    }
    else
    {
      highB = brightness;
      highLux = lux;
      break;
    }
  }


  if (brightness == 255 && lux < target_lux - LUX_DEADBAND)
  {
    Serial.println("Warning: cannot reach target lux even at max brightness.");
    return;
  }


  for (int step = 0; step < MAX_STEPS; step++)
  {

    float err = target_lux - lux;
    if (fabs(err) <= LUX_DEADBAND)
    {
      Serial.printf("Converged: lux=%.2f within deadband ±%d (bright=%u)\n",
                    lux, LUX_DEADBAND, brightness);
      break;
    }


    uint8_t midB = (uint8_t)((lowB + highB) / 2);
    ringLight.setBrightness(midB);
    delay(SETTLE_MS);
    float midLux = luxSensor.read().value;

    Serial.printf("[BIN] low=(%u, %.2f) mid=(%u, %.2f) high=(%u, %.2f)\n",
                  lowB, lowLux, midB, midLux, highB, highLux);


    if (midLux < target_lux - LUX_DEADBAND)
    {

      lowB = midB;
      lowLux = midLux;
    }
    else if (midLux > target_lux + LUX_DEADBAND)
    {

      highB = midB;
      highLux = midLux;
    }
    else
    {

      brightness = midB;
      lux = midLux;
      Serial.printf("Binary search landed inside deadband: lux=%.2f, bright=%u\n",
                    lux, brightness);
      break;
    }

    brightness = midB;
    lux = midLux;
  }

  Serial.printf("Final lux: %.2f at brightness=%u\n", lux, brightness);
}

void startCameraServer();


int raw_feature_get_data(size_t offset, size_t length, float *out_ptr)
{
  if (!loop_fb || !loop_fb->buf)
    return -1;



  int min_dim = (loop_fb->width < loop_fb->height) ? loop_fb->width : loop_fb->height;
  int start_x = (loop_fb->width - min_dim) / 2;
  int start_y = (loop_fb->height - min_dim) / 2;


  for (size_t i = 0; i < length; i++)
  {

    int x_model = (offset + i) % EI_CLASSIFIER_INPUT_WIDTH;
    int y_model = (offset + i) / EI_CLASSIFIER_INPUT_WIDTH;



    int x_cam = start_x + (x_model * min_dim) / EI_CLASSIFIER_INPUT_WIDTH;
    int y_cam = start_y + (y_model * min_dim) / EI_CLASSIFIER_INPUT_HEIGHT;


    int pixel_idx = (y_cam * loop_fb->width + x_cam) * 2;


    if (pixel_idx + 1 >= loop_fb->len)
    {
      out_ptr[i] = 0;
      continue;
    }


    uint8_t lo = loop_fb->buf[pixel_idx];
    uint8_t hi = loop_fb->buf[pixel_idx + 1];
    uint16_t pixel = (lo << 8) | hi;


    float r = ((pixel & 0x1F) & 0x1F) * 255.0f / 31.0f;
    float g = ((pixel >> 5) & 0x3F) * 255.0f / 63.0f;
    float b = (pixel >> 11) * 255.0f / 31.0f;


    out_ptr[i] = (r * 0.299f) + (g * 0.587f) + (b * 0.114f);
  }

  return 0;
}


void captureToResult()
{
  setLux(100);
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
    ringLight.off();
    return;
  }

  Serial.printf("Success! Photo size: %zu bytes\n", currentResult.fb->len);
  ringLight.off();
  delay(500);


  Serial.println("Running Inference...");


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


  loop_fb = NULL;

}


const int heightsAt10mlSteps[] = {
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    10,
    12,
    15,
    16,
    19,
    21,
    24,
    26,
    28,
    30,
    32,
    35,
    38,
    41,
    44,
    40,
    42,
    44,
    46,
    44,
    42,
    40,
    37,
    34,
    32,
    30,
    28,
    26,
    24,
};


const int lutSize = sizeof(heightsAt10mlSteps) / sizeof(heightsAt10mlSteps[0]);

float getVolumeFromHeight(int measured_h)
{
  if (measured_h <= 0)
    return 0.0;


  for (int i = 0; i < lutSize - 1; i++)
  {
    int h_low = heightsAt10mlSteps[i];
    int h_high = heightsAt10mlSteps[i + 1];


    if (measured_h >= h_low && measured_h <= h_high)
    {


      float range = h_high - h_low;
      float diff = measured_h - h_low;
      float fraction = diff / range;


      float vol_low = i * 10.0;

      return vol_low + (fraction * 10.0);
    }
  }


  return (lutSize - 1) * 10.0;
}


void performAction(String cmd)
{
  if (cmd == "reset")
  {
    currentResult.start = 0;
    currentResult.end = 0;
    currentResult.status = "--";
    return;
  }

  int avg = tofSensor.read().value;
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


    float totalVolume = getVolumeFromHeight(currentResult.final_h);


    if (currentResult.liquid_v > 0)
    {
      currentResult.pct = ((totalVolume - currentResult.liquid_v) / currentResult.liquid_v) * 100.0;
    }
    else
    {

      if (currentResult.init_h > 0)
      {
        currentResult.pct = ((float)(currentResult.final_h - currentResult.init_h) / currentResult.init_h) * 100.0;
      }
      else
      {
        currentResult.pct = 0;
      }
    }


    if (currentResult.pct < 10)
      currentResult.status = "UNDERFROTHED";
    else if (currentResult.pct > 50)
      currentResult.status = "OVERLY FROTHY";
    else
      currentResult.status = "WELL FROTHED";

    captureToResult();
  }
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  Serial.println();

  esp_log_level_set("camera", ESP_LOG_VERBOSE);
  esp_log_level_set("cam_hal", ESP_LOG_VERBOSE);


  Serial.println("Initializing Camera...");
  delay(500);
  if (!camera.initialize())
  {
    Serial.println("Camera Init Failed");
    return;
  }
  delay(300);


  Wire.begin(TOF_SDA, TOF_SCL);


  Wire.setClock(100000);


  Serial.println("Initializing VL53L0X Sensor...");

  Serial.println("Initializing ToF Sensor...");
  if (!tofSensor.initialize())
  {
    Serial.println("ToF Failed");
  }
  delay(200);

  Serial.println("Initializing BH1750 Lux Sensor...");
  if (!luxSensor.initialize())
  {
    Serial.println("Lux sensor init failed");
  }
  delay(200);

  ringLight.initialize();
  ringLight.setBrightness(RING_BRIGHTNESS);
  delay(200);


  Serial.println("Initializing ST77916 Display...");
  delay(300);
  if (!screen.initialize())
  {
    Serial.println("Display init failed — continuing without screen");
  }

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop()
{
  delay(1000);
}