#ifndef RING_LIGHT_H
#define RING_LIGHT_H

#include <Adafruit_NeoPixel.h>

#include "peripheral.h"

class RingLight : public Peripheral
{
public:
    explicit RingLight(
        const char *name,
        int pin,
        int count,
        neoPixelType type = NEO_GRB + NEO_KHZ800)
        : Peripheral(name),
          ring_(count, pin, type),
          pin_(pin),
          count_(count),
          brightness_(255),
          r_(255),
          g_(255),
          b_(255),
          is_on_(false)
    {
    }

    bool begin() override
    {
        ring_.begin();
        ring_.clear();
        ring_.setBrightness(brightness_);
        ring_.show();
        return true;
    }

    bool wake() override
    {
        if (!isInitialized())
            return true;

        if (is_on_)
            apply_();

        return true;
    }

    bool sleep() override
    {
        if (!isInitialized())
            return true;

        return off();
    }

    bool on(int r = 255, int g = 255, int b = 255)
    {
        setColor(r, g, b);
        is_on_ = true;
        apply_();
        return true;
    }

    bool off()
    {
        is_on_ = false;
        ring_.clear();
        ring_.show();
        return true;
    }

    bool setBrightness(int brightness)
    {
        brightness_ = clamp8_(brightness);
        ring_.setBrightness(brightness_);
        if (is_on_)
            apply_();
        return true;
    }

    bool setColor(int r, int g, int b)
    {
        r_ = clamp8_(r);
        g_ = clamp8_(g);
        b_ = clamp8_(b);

        if (is_on_)
            apply_();

        return true;
    }

private:
    static uint8_t clamp8_(int v)
    {
        if (v < 0)
            return 0;
        if (v > 255)
            return 255;
        return static_cast<uint8_t>(v);
    }

    void apply_()
    {
        ring_.setBrightness(brightness_);
        for (int i = 0; i < count_; ++i)
        {
            ring_.setPixelColor(i, ring_.Color(r_, g_, b_));
        }
        ring_.show();
    }

private:
    Adafruit_NeoPixel ring_;
    int pin_;
    int count_;
    uint8_t brightness_;
    uint8_t r_;
    uint8_t g_;
    uint8_t b_;
    bool is_on_;
};

#endif // RING_LIGHT_H