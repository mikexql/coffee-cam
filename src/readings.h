#ifndef READINGS_H
#define READINGS_H

#include <stdint.h>

// Strong-ish types (no unit mixing by accident)
struct Lux
{
    float value = 0.0f;
    constexpr Lux() : value(0.0f) {}
    constexpr explicit Lux(float v) : value(v) {}
};

struct Millimeters
{
    int32_t value = 0;
    constexpr Millimeters() : value(0) {}
    constexpr explicit Millimeters(int32_t v) : value(v) {}
};

// Intentionally undefined: you must specialize for each TReading you use.
template <typename TReading>
struct ReadingOps;

// --- Specializations ---

template <>
struct ReadingOps<Lux>
{
    using Accum = float;

    static inline Accum toAccum(Lux r) { return r.value; }
    static inline bool isValid(Lux) { return true; }

    static inline Lux fromAccum(Accum sum, int count)
    {
        if (count <= 0)
            return Lux{0.0f};
        return Lux{sum / static_cast<float>(count)};
    }
};

template <>
struct ReadingOps<Millimeters>
{
    using Accum = int64_t;

    static inline Accum toAccum(Millimeters r) { return static_cast<Accum>(r.value); }
    static inline bool isValid(Millimeters r) { return r.value > 0; }

    static inline Millimeters fromAccum(Accum sum, int count)
    {
        if (count <= 0)
            return Millimeters{0};
        return Millimeters{static_cast<int32_t>(sum / static_cast<Accum>(count))};
    }
};

#endif // READINGS_H