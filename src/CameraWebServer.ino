#include "esp_camera.h"
#include <WiFi.h>
#include "secrets.h"
#include "ESP32_OV5640_AF.h"
#include "Adafruit_VL53L0X.h"
#include "board_config.h"
#include "microfoam_logic.h"

#include <milk_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "esp_log.h"

#include <stdarg.h>
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

// Define I2C Pins for ToF
// #define TOF_SDA 14
// #define TOF_SCL 3

// Define I2C Pins for shared i2c
#define TOF_SDA 4
#define TOF_SCL 5

// OV5640 AF firmware interface registers
static constexpr uint16_t REG_CHIP_ID_H = 0x300A;
static constexpr uint16_t REG_CHIP_ID_L = 0x300B;

static constexpr uint16_t REG_CMD_MAIN = 0x3022;  // main command
static constexpr uint16_t REG_CMD_ACK = 0x3023;   // ack
static constexpr uint16_t REG_FW_STATUS = 0x3029; // AF firmware status

// Global instances
OV5640 ov5640 = OV5640();
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

// Initialize all fields of microfoam_logic
MicrofoamResult currentResult = {{0, 0, 0}, 0, 0, 0, 0, 0, 0, 0.0, 0.0, "--", "--", 0.0, NULL};

// Global helper for AI buffer
static camera_fb_t *loop_fb = NULL;

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

// --- Measurement Module ---
int getAveragedDistance(int readings[3])
{
  long sum = 0;
  int valid = 0;

  // take 3 readings to get average
  for (int i = 0; i < 3; i++)
  {
    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false); // triggers ToF laser

    // status 4 means out of range
    if (measure.RangeStatus != 4)
    {
      readings[i] = measure.RangeMilliMeter;
      sum += readings[i];
      valid++;
    }

    else
    {
      readings[i] = -1; // mark reading as invalid
    }

    delay(10);
  }

  return (valid > 0) ? (int)(sum / valid) : 0; // return only if more than 1 valid reading to prevent division by 0
}

static inline bool is_focusing(uint8_t fw)
{
  // OV5640 AF guide: 0x00-0x0F and 0x80-0x8F are focusing
  return (fw <= 0x0F) || (fw >= 0x80 && fw <= 0x8F);
}

static bool read_reg_u8(sensor_t *s, uint16_t reg, uint8_t &out)
{
  int v = s->get_reg(s, reg, 0xFF);
  if (v < 0)
    return false;
  out = (uint8_t)v;
  return true;
}

static bool ov5640_check_chip_id(sensor_t *s)
{
  uint8_t hi = 0, lo = 0;
  if (!read_reg_u8(s, REG_CHIP_ID_H, hi))
    return false;
  if (!read_reg_u8(s, REG_CHIP_ID_L, lo))
    return false;

  // OV5640 is expected to be 0x56 0x40 (0x5640)
  if (hi == 0xFF && lo == 0xFF)
    return false; // typical read-fail pattern
  return (hi == 0x56 && lo == 0x40);
}

bool ov5640_run_single_af(sensor_t *s, uint32_t timeout_ms = 5000)
{
  if (!s)
    return false;

  // 0) Quick SCCB sanity: if this fails, AF checks are meaningless
  if (!ov5640_check_chip_id(s))
  {
    Serial.println("AF ERR: OV5640 chip ID read failed (SCCB not healthy).");
    return false;
  }

  // 1) Trigger AF: write CMD_ACK=1 then CMD_MAIN=0x03 (Trig Auto Focus)
  // Per OV5640 AF docs, CMD_ACK auto-clears to 0 when command completes.
  if (s->set_reg(s, REG_CMD_ACK, 0xFF, 0x01) != 0)
  {
    Serial.println("AF ERR: write CMD_ACK failed");
    return false;
  }
  if (s->set_reg(s, REG_CMD_MAIN, 0xFF, 0x03) != 0)
  {
    Serial.println("AF ERR: write CMD_MAIN failed");
    return false;
  }

  // 2) Wait for CMD_ACK to clear to 0
  uint32_t t0 = millis();
  while (true)
  {
    uint8_t ack = 0xFF;
    if (!read_reg_u8(s, REG_CMD_ACK, ack) || ack == 0xFF)
    {
      Serial.println("AF ERR: CMD_ACK read failed");
      return false;
    }
    if (ack == 0x00)
      break;

    if (millis() - t0 > timeout_ms)
    {
      Serial.println("AF ERR: CMD_ACK timeout");
      return false;
    }
    delay(10);
  }

  // 3) Poll FW_STATUS until focused (0x10) or timeout
  // Docs: 0x10 = focused, focusing states are 0x00-0x0F,0x80-0x8F, idle is 0x70
  while (true)
  {
    uint8_t fw = 0xFF;
    if (!read_reg_u8(s, REG_FW_STATUS, fw) || fw == 0xFF)
    {
      Serial.println("AF ERR: FW_STATUS read failed");
      return false;
    }

    if (fw == 0x10)
    { // S_FOCUSED
      Serial.println("AF OK: focused (FW_STATUS=0x10)");
      return true;
    }

    // Optional: treat "firmware not ready" as hard fail
    if (fw == 0x7F || fw == 0x7E)
    {
      Serial.printf("AF ERR: firmware not ready (FW_STATUS=0x%02X)\n", fw);
      return false;
    }

    if (millis() - t0 > timeout_ms)
    {
      Serial.printf("AF ERR: focus timeout (FW_STATUS=0x%02X)\n", fw);
      return false;
    }
    delay(20);
  }
}

// --- Camera Module ---
void captureToResult()
{
  Serial.println("Starting Camera Capture with Autofocus...");
  sensor_t *s = esp_camera_sensor_get();
  Serial.println("Sensor obtained.");

  // WAKE UP
  s->set_reg(s, 0x3008, 0xff, 0x02);
  Serial.println("Sensor waking up...");
  vTaskDelay(300 / portTICK_PERIOD_MS);
  Serial.println("Sensor turned on");

  // 1. Trigger Single Focus Search
  Serial.println("Starting Autofocus...");
  s->set_reg(s, 0x3023, 0xff, 0x01); // Handshake ACK
  s->set_reg(s, 0x3022, 0xff, 0x03); // Single Focus Command
  bool ok = ov5640_run_single_af(s, 5000);
  Serial.printf("AF %s\n", ok ? "SUCCESS" : "FAILED");

  // 2. Wait for focus to finish
  uint8_t status = 0x10; // Register 0x3029 is 0x10 while the motor is moving
  unsigned long startTime = millis();

  while (status == 0x10)
  {
    status = s->get_reg(s, 0x3029, 0xff); // get all 8 bits of register data (0xff = all 8 bits)
    Serial.printf("Focusing... Status: 0x%02X\n", status);

    if (millis() - startTime > 5000)
    { // 5 second timeout
      Serial.println("Focus Timeout!");
      break;
    }

    delay(100);
  }

  Serial.printf("Focus Complete. Status: 0x%02X\n", status);

  delay(500);

  // clear buffer: if there is a picture in buffer, return frame buffer to be reused again
  if (currentResult.fb)
  {
    esp_camera_fb_return(currentResult.fb);
    currentResult.fb = NULL;
  }

  // at the current focal length, flush buffer by taking 3 pictures to get rid of out of focus pictures
  for (int i = 0; i < 4; i++)
  {
    camera_fb_t *temp_fb = esp_camera_fb_get();

    if (temp_fb)
    {
      esp_camera_fb_return(temp_fb);
    }

    vTaskDelay(150 / portTICK_PERIOD_MS); // Small gap for stability
  }

  // 3. Capture photo and store it in the Shared Result Object
  Serial.println("Capturing Photo...");
  currentResult.fb = esp_camera_fb_get();

  if (!currentResult.fb)
  {
    Serial.println("Camera capture failed");
    s->set_reg(s, 0x3008, 0xff, 0x42);
    Serial.println("Sensor in Sleep Mode.");
    return;
  }

  Serial.printf("Success! Photo size: %zu bytes\n", currentResult.fb->len);
  s->set_reg(s, 0x3008, 0xff, 0x42);
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

  // E. GO TO SLEEP (Cool down)
  s->set_reg(s, 0x3008, 0xff, 0x42);
  Serial.println("Sensor in Sleep Mode.");
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

  int avg = getAveragedDistance(currentResult.raw);
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

  esp_log_level_set("camera", ESP_LOG_NONE);
  esp_log_level_set("cam_hal", ESP_LOG_NONE);

  // Join the I2C Bus that the camera just started
  Wire.begin(TOF_SDA, TOF_SCL);

  // SPEED LIMIT: Force 100kHz so we don't crash the Camera
  Wire.setClock(100000);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = -1;
  config.pin_sccb_scl = -1;
  // CRITICAL: Force Hardware I2C (Port 0)
  config.sccb_i2c_port = 0;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 24000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_RGB565;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // initialise camera with config settings defined above
  // esp_camera_init expected to return "ESP_OK"
  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // --- 2. CONFIGURE TOF SENSOR (Secondary Device) ---
  Serial.println("Initializing VL53L0X Sensor...");

  if (!lox.begin())
  {
    Serial.println("ToF Failed");
  }
  else
  {
    Serial.println(F("VL53L0X Ready!"));
  }

  // s is pointer to camera sensor
  sensor_t *s = esp_camera_sensor_get();

  // 1. Start the AF object
  if (ov5640.start(s))
  {
    Serial.println("OV5640 AF Started");

    // 2. Load AF firmware
    if (ov5640.focusInit() == 0)
    {
      Serial.println("OV5640 Focus Init Successful");
    }

    // 3. Start AF
    if (ov5640.autoFocusMode() == 0)
    {
      Serial.println("OV5640_Auto_Focus Successful!");
    }

    if (s)
    {
      s->set_reg(s, 0x3008, 0xff, 0x42); // Start in Sleep Mode to prevent overheating
      Serial.println("Sensor in Sleep Mode. Ready.");
    }
  }

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