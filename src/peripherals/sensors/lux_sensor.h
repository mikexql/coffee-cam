#ifndef LUX_SENSOR_H
#define LUX_SENSOR_H

#include <BH1750.h>

#include "sensor.h"
#include "readings.h"

class LuxSensor : public Sensor<Lux>
{
public:
    explicit LuxSensor(
        const char *name = "LuxSensor",
        BH1750::Mode mode = BH1750::CONTINUOUS_HIGH_RES_MODE,
        float calibration = 1.0f,
        int defaultSamples = 3)
        : Sensor<Lux>(name, defaultSamples),
          lux_(),
          mode_(mode)
    {
    }

    bool begin() override
    {
        return lux_.begin(mode_);
    }

    bool wake() override
    {
        if (!isInitialized())
            return true;
        return lux_.configure(mode_);
    }

    bool sleep() override
    {
        if (!isInitialized())
            return true;
        return lux_.configure(BH1750::UNCONFIGURED); // power-down
    }

    Lux readOnce() override
    {
        const float rawLux = lux_.readLightLevel();
        return Lux{rawLux};
    }   

    bool setMode(BH1750::Mode mode)
    {
        mode_ = mode;
        if (!isInitialized())
            return true;
        return lux_.configure(mode_);
    }

private:
    BH1750 lux_;
    BH1750::Mode mode_;
};

#endif // LUX_SENSOR_H