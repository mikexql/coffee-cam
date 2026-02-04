#ifndef SENSOR_H
#define SENSOR_H

#include "peripheral.h"
#include "readings.h"

template <typename TReading>
class Sensor : public Peripheral
{
public:
    explicit Sensor(const char *name, int defaultSamples = 3)
        : Peripheral(name),
          default_samples_(defaultSamples > 0 ? defaultSamples : 1)
    {
    }

    virtual ~Sensor() = default;

    // Non-virtual: forces the shared averaging behavior
    TReading read()
    {
        return readAveraged(default_samples_);
    }

    TReading readAveraged(int samples)
    {
        return getAverageReading(samples);
    }

    // Device-specific single sample
    virtual TReading readOnce() = 0;

protected:
    TReading getAverageReading(int samples)
    {
        const int n = (samples > 0) ? samples : 1;

        using Ops = ReadingOps<TReading>;
        typename Ops::Accum sum{};
        int count = 0;

        for (int i = 0; i < n; ++i)
        {
            const TReading r = readOnce();
            if (!Ops::isValid(r))
                continue;

            sum += Ops::toAccum(r);
            ++count;
        }

        return Ops::fromAccum(sum, count);
    }

private:
    int default_samples_;
};

#endif // SENSOR_H