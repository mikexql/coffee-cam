#ifndef TOF_SENSOR_H
#define TOF_SENSOR_H

#include <Adafruit_VL53L0X.h>

#include "sensor.h"
#include "readings.h"

class TofSensor : public Sensor<Millimeters>
{
public:
    explicit TofSensor(
        const char *name = "ToF",
        bool longRange = false,
        uint16_t timing_budget_ms = 33,
        int defaultSamples = 3)
        : Sensor<Millimeters>(name, defaultSamples),
          longRange_(longRange),
          timing_budget_ms_(timing_budget_ms)
    {
    }

    bool begin() override
    {
        if (!lox_.begin())
            return false;

        bool ok = true;
        ok = ok && applyRangeConfig_();
        ok = ok && applyTimingBudget_();
        return ok;
    }

    bool setLongRange(bool enable)
    {
        longRange_ = enable;
        if (!isInitialized())
            return true;
        return applyRangeConfig_();
    }

    bool setTimingBudget(uint16_t timing_budget_ms)
    {
        timing_budget_ms_ = timing_budget_ms;
        if (!isInitialized())
            return true;
        return applyTimingBudget_();
    }

    Millimeters readOnce() override
    {
        // Try a few times to avoid averaging invalid "out of range" samples.
        VL53L0X_RangingMeasurementData_t measure;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            lox_.rangingTest(&measure, false);

            // Your existing code treats RangeStatus==4 as invalid/out-of-range.
            if (measure.RangeStatus != 4)
                return Millimeters{static_cast<int32_t>(measure.RangeMilliMeter)};

            delay(5);
        }

        // Fallback when out-of-range repeatedly.
        return Millimeters{0};
    }

private:
    bool applyRangeConfig_()
    {
        const auto cfg = longRange_
                             ? Adafruit_VL53L0X::VL53L0X_SENSE_LONG_RANGE
                             : Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT;
        return lox_.configSensor(cfg);
    }

    bool applyTimingBudget_()
    {
        const uint32_t budget_us = static_cast<uint32_t>(timing_budget_ms_) * 1000u;
        return lox_.setMeasurementTimingBudgetMicroSeconds(budget_us);
    }

private:
    bool longRange_;
    uint16_t timing_budget_ms_;
    Adafruit_VL53L0X lox_;
};

#endif // TOF_SENSOR_H