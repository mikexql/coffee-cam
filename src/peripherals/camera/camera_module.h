#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <Arduino.h>
#include "esp_camera.h"
#include "board_config.h"
#include "ESP32_OV5640_AF.h"

#include "peripheral.h"

class Camera : public Peripheral
{
public:
    explicit Camera(
        const char *name = "Camera",
        bool autofocus_enabled = true,
        bool start_in_sleep = true)
        : Peripheral(name),
          config_{},
          ov5640_{},
          sensor_{nullptr},
          last_fb_{nullptr},
          autofocus_enabled_{autofocus_enabled},
          start_in_sleep_{start_in_sleep}
    {
        // Default config matches your current setup() block.
        config_.ledc_channel = LEDC_CHANNEL_0;
        config_.ledc_timer = LEDC_TIMER_0;
        config_.pin_d0 = Y2_GPIO_NUM;
        config_.pin_d1 = Y3_GPIO_NUM;
        config_.pin_d2 = Y4_GPIO_NUM;
        config_.pin_d3 = Y5_GPIO_NUM;
        config_.pin_d4 = Y6_GPIO_NUM;
        config_.pin_d5 = Y7_GPIO_NUM;
        config_.pin_d6 = Y8_GPIO_NUM;
        config_.pin_d7 = Y9_GPIO_NUM;
        config_.pin_xclk = XCLK_GPIO_NUM;
        config_.pin_pclk = PCLK_GPIO_NUM;
        config_.pin_vsync = VSYNC_GPIO_NUM;
        config_.pin_href = HREF_GPIO_NUM;
        config_.pin_sccb_sda = SIOD_GPIO_NUM;
        config_.pin_sccb_scl = SIOC_GPIO_NUM;
        config_.pin_pwdn = PWDN_GPIO_NUM;
        config_.pin_reset = RESET_GPIO_NUM;
        config_.xclk_freq_hz = 24000000;

        config_.frame_size = FRAMESIZE_QVGA;
        config_.pixel_format = PIXFORMAT_RGB565;
        config_.grab_mode = CAMERA_GRAB_LATEST;
        config_.fb_location = CAMERA_FB_IN_PSRAM;
        config_.jpeg_quality = 12;
        config_.fb_count = 1;
    }

    // Optional pre-init tweaks (must be called before initialize()).
    bool setFrameSize(framesize_t v)
    {
        return setIfNotInit_([&]
                             { config_.frame_size = v; });
    }
    bool setPixelFormat(pixformat_t v)
    {
        return setIfNotInit_([&]
                             { config_.pixel_format = v; });
    }
    bool setJpegQuality(uint8_t v)
    {
        return setIfNotInit_([&]
                             { config_.jpeg_quality = v; });
    }
    bool setFbCount(uint8_t v)
    {
        return setIfNotInit_([&]
                             { config_.fb_count = v; });
    }
    void setAutofocusEnabled(bool v) { autofocus_enabled_ = v; }

    camera_fb_t *capture(bool run_autofocus = true, int flush_count = 3)
    {
        if (!isInitialized())
            return nullptr;

        releaseLast();

        if (!wake_())
            return nullptr;

        if (autofocus_enabled_ && run_autofocus)
        {
            bool autofocused = autofocus_();
            if (!autofocused)
            {
                Serial.println("Autofocus failed");
            }
            delay(200); // mechanical settling (same as your sketch)
        }

        for (int i = 0; i < flush_count; ++i)
        {
            camera_fb_t *temp = esp_camera_fb_get();
            if (temp)
                esp_camera_fb_return(temp);
            delay(150);
        }

        last_fb_ = esp_camera_fb_get();

        sleep_();
        return last_fb_;
    }

    void release(camera_fb_t *fb)
    {
        if (!fb)
            return;
        esp_camera_fb_return(fb);
        if (fb == last_fb_)
            last_fb_ = nullptr;
    }

    void releaseLast()
    {
        if (last_fb_)
        {
            esp_camera_fb_return(last_fb_);
            last_fb_ = nullptr;
        }
    }

    camera_fb_t *lastFrame() const { return last_fb_; }

protected:
    bool begin() override
    {
        const esp_err_t err = esp_camera_init(&config_);
        if (err != ESP_OK)
            return false;

        sensor_ = esp_camera_sensor_get();

        if (autofocus_enabled_ && sensor_)
        {
            if (ov5640_.start(sensor_))
            {
                ov5640_.focusInit();
                ov5640_.autoFocusMode();
            }
        }

        if (start_in_sleep_)
        {
            sleep_();
        }

        return true;
    }

private:
    template <typename Fn>
    bool setIfNotInit_(Fn fn)
    {
        if (isInitialized())
            return false;
        fn();
        return true;
    }

    bool wake_()
    {
        if (!sensor_)
            return false;
        sensor_->set_reg(sensor_, 0x3008, 0xff, 0x02);
        delay(300);
        return true;
    }

    bool sleep_()
    {
        if (!sensor_)
            return false;
        sensor_->set_reg(sensor_, 0x3008, 0xff, 0x42);
        return true;
    }

    bool autofocus_()
    {
        if (!sensor_)
            return false;

        sensor_->set_reg(sensor_, 0x3023, 0xff, 0x01); // Handshake ACK
        sensor_->set_reg(sensor_, 0x3022, 0xff, 0x03); // Single focus command

        uint8_t status = 0x00;
        const unsigned long start_time = millis();

        while (true)
        {
            status = sensor_->get_reg(sensor_, 0x3029, 0xff);

            if (status == 0x10) // focused
                return true;
            if (status == 0x70) // idle/fail
                return false;
            if (status == 0xFF) // i2c error
                return false;

            if (millis() - start_time > 4000)
                return false;

            delay(50);
        }
    }

private:
    camera_config_t config_;
    OV5640 ov5640_;
    sensor_t *sensor_;
    camera_fb_t *last_fb_;
    bool autofocus_enabled_;
    bool start_in_sleep_;
};

#endif // CAMERA_MODULE_H