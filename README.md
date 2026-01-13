# Coffee Cam

Coffee Cam is an **ESP32-S3 camera firmware project** built with **PlatformIO (Arduino framework)**.  
It integrates:

- ESP32 camera web server
- Edge Impulse–generated inference library (`milk_inferencing`)
- Wi-Fi, MQTT, JSON, ToF distance sensor

The main application is implemented in `CameraWebServer.ino`.

---

## Hardware Target

- **Board**: ESP32-S3 DevKitC-1 (Freenove ESP32-S3 WROOM)
- **Camera**: ESP32-S3 compatible camera module
- **Peripherals**:
  - VL53L0X Time-of-Flight distance sensor

---

## Project Structure

```
coffee-cam/
├── src/
│   └── CameraWebServer.ino
├── lib/
│   └── milk_inferencing/
│       ├── edge-impulse-sdk/
│       ├── model-parameters/
│       ├── tflite-model/
│       └── examples/
├── aws/
│   └── CameraWebServer.ino
├── include/
├── test/
└── platformio.ini
```

---

## PlatformIO Configuration

- Platform: `espressif32`
- Framework: Arduino
- PSRAM enabled
- Serial baud rate: 115200

### Libraries Used
- WiFi
- Wire
- PubSubClient (MQTT)
- ArduinoJson
- Adafruit_VL53L0X
- Adafruit NeoPixel

---

## Build & Flash

### Requirements
- PlatformIO
- ESP32-S3 DevKit
- USB data cable

### Build
```bash
platformio run
```

### Upload
```bash
platformio run --target upload
```

### Monitor
```bash
platformio device monitor
```

---

## Edge Impulse

The `milk_inferencing` folder contains the full Edge Impulse C++ SDK and compiled model.
Model retraining must be done in Edge Impulse Studio and re-exported.
