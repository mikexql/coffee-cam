#include "esp_camera.h"
#include <WiFi.h>
#include "secrets.h"
#include "ESP32_OV5640_AF.h"
#include "Adafruit_VL53L0X.h"
#include "board_config.h"
#include "microfoam_logic.h"

// Define I2C Pins for ToF
#define TOF_SDA 14
#define TOF_SCL 3

// Global instances
OV5640 ov5640 = OV5640();
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
MicrofoamResult currentResult = {{0,0,0}, 0, 0, 0, 0, 0, 0, 0.0, "--", NULL};

void startCameraServer(); //refer to app_httpd

// --- Measurement Module ---
int getAveragedDistance(int readings[3]) {
  long sum = 0;
  int valid = 0;

  //take 3 readings to get average
  for (int i = 0; i < 3; i++) {
    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false); //triggers ToF laser

    //status 4 means out of range 
    if (measure.RangeStatus != 4) { 
      readings[i] = measure.RangeMilliMeter;
      sum += readings[i];
      valid++;
    } 
    
    else {
      readings[i] = -1; //mark reading as invalid
    }

    delay(100);
  }

  return (valid > 0) ? (int)(sum / valid) : 0; //return only if more than 1 valid reading to prevent division by 0
}

// --- Camera Module ---
void captureToResult() {
  sensor_t *s = esp_camera_sensor_get();

  // WAKE UP
  s->set_reg(s, 0x3008, 0xff, 0x02);
  vTaskDelay(300 / portTICK_PERIOD_MS);
  Serial.println("Sensor turned on");

  // 1. Trigger Single Focus Search
  Serial.println("Starting Autofocus...");
  s->set_reg(s, 0x3023, 0xff, 0x01); // Handshake ACK 
  s->set_reg(s, 0x3022, 0xff, 0x03); // Single Focus Command 

  // 2. Wait for focus to finish 
  uint8_t status = 0x10; // Register 0x3029 is 0x10 while the motor is moving
  unsigned long startTime = millis();

  while (status == 0x10) {
    status = s->get_reg(s, 0x3029, 0xff); //get all 8 bits of register data (0xff = all 8 bits)

    if (millis() - startTime > 5000) { // 5 second timeout
      Serial.println("Focus Timeout!");
      break;
    }

    delay(100);
  }

  Serial.printf("Focus Complete. Status: 0x%02X\n", status);

  //clear buffer: if there is a picture in buffer, return frame buffer to be reused again
  if(currentResult.fb) {
    esp_camera_fb_return(currentResult.fb);
    currentResult.fb = NULL;
  }

  //at the current focal length, flush buffer by taking 3 pictures to get rid of out of focus pictures
  for (int i = 0; i < 3; i++) {
    camera_fb_t * temp_fb = esp_camera_fb_get();

    if(temp_fb) {
      esp_camera_fb_return(temp_fb);
    }

    vTaskDelay(150 / portTICK_PERIOD_MS); // Small gap for stability 
  } 

  // 3. Capture photo and store it in the Shared Result Object
  Serial.println("Capturing Photo...");
  currentResult.fb = esp_camera_fb_get(); 

  if (!currentResult.fb) {
    Serial.println("Camera capture failed");
    s->set_reg(s, 0x3008, 0xff, 0x42);
    Serial.println("Sensor in Sleep Mode.");
    return;
  }

  Serial.printf("Success! Photo size: %zu bytes\n", currentResult.fb->len);

  // E. GO TO SLEEP (Cool down)
  s->set_reg(s, 0x3008, 0xff, 0x42);
  Serial.println("Sensor in Sleep Mode.");
}

// --- Logic Module ---
void performAction(String cmd) {
  if (cmd == "reset") {
    currentResult.start = 0;
    currentResult.end = 0;
    currentResult.status = "--";
    return;
  }

  int avg = getAveragedDistance(currentResult.raw);
  currentResult.avg = avg;

  if (cmd == "empty") {
    currentResult.empty = avg;
  }
  
  else if (cmd == "start") {
    currentResult.start = avg;
  }

  else if (cmd == "end") {
    currentResult.end = avg;
    captureToResult(); // Trigger camera immediately with ToF
  }

  // Calculate math
  if (currentResult.empty > 0 && currentResult.start > 0) {
    currentResult.init_h = currentResult.empty - currentResult.start;
  }

  if (currentResult.empty > 0 && currentResult.end > 0) {
    currentResult.final_h = currentResult.empty - currentResult.end;
  }

  if (currentResult.init_h > 0 && currentResult.final_h > 0) {
    currentResult.pct = ((float)(currentResult.final_h - currentResult.init_h) / currentResult.init_h) * 100.0;

    if (currentResult.pct < 50.0) {
      currentResult.status = "UNDERFROTHED";
    }

    else if (currentResult.pct <= 100.0) {
      currentResult.status = "WELL FROTHED";
    }
  
    else {
      currentResult.status = "OVERFROTHED";
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  Serial.println("Initializing VL53L0X Sensor...");
  Wire.setPins(TOF_SDA, TOF_SCL); //use setPins first and call later

  if (!lox.begin()) {
    Serial.println("ToF Failed");
  }

  else {
    Serial.println(F("VL53L0X Ready!"));
  }

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 24000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 3;
  config.fb_count = 2;

  //initialise camera with config settings defined above
  //esp_camera_init expected to return "ESP_OK"
  esp_err_t err = esp_camera_init(&config); 

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  //s is pointer to camera sensor
  sensor_t *s = esp_camera_sensor_get();   

  // 1. Start the AF object
  if (ov5640.start(s)) {
    Serial.println("OV5640 AF Started");

    // 2. Load AF firmware
    if (ov5640.focusInit() == 0) {
      Serial.println("OV5640 Focus Init Successful");
    }

    //3. Start AF
    if (ov5640.autoFocusMode() == 0) {
      Serial.println("OV5640_Auto_Focus Successful!");
    }

    if (s) {
      s->set_reg(s, 0x3008, 0xff, 0x42); // Start in Sleep Mode to prevent overheating
      Serial.println("Sensor in Sleep Mode. Ready.");
    }
  }

  WiFi.begin(ssid, password);
  WiFi.setSleep(false); //Wifi active 100% of the time

  Serial.print("WiFi connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer(); //launch button for web interface: refer to app_httpd

  Serial.print("Use 'http://"); 
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() { 
  delay(1000); 
}