#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

namespace vsat
{
inline juce::StringArray typeNames() { return { "Tube", "Tape", "Console", "Transistor", "Fuzz", "Wavefold", "Bitcrush" }; }

// static shaper (no state). x is already pre-gained; bias adds even harmonics.
inline float shape (int type, float x, float bias)
{
    auto f = [type] (float v)
    {
        switch (type)
        {
            case 0: return std::tanh (v);                                                         // tube (bias adds 2nd harmonic)
            case 1: return 0.6366198f * std::atan (v * 1.5707963f);                                // tape: very soft knee
            case 2: { const float c = juce::jlimit (-1.5f, 1.5f, v); return c - c * c * c / 6.75f; } // console: cubic, gentle
            case 3: return (v >= 0 ? 1.0f : -1.0f) * (1.0f - std::exp (-std::abs (v)));            // transistor: harder knee
            case 4: return juce::jlimit (-1.0f, 1.0f, std::tanh (v * 3.0f) * 1.1f);                // fuzz: near square
            case 5: return std::sin (v * 1.5707963f);                                              // wavefolder
            default: return juce::jlimit (-1.0f, 1.0f, v);
        }
    };
    const float b = type == 0 ? bias + 0.15f : bias;           // tubes are a little asymmetric even at 0
    return f (x + b) - f (b);
}

inline float driveToGain (float drive01) { return std::pow (10.0f, drive01 * 36.0f / 20.0f); }   // 0..36 dB

// level compensation so turning up drive doesn't just make it louder (measured on a -12 dBFS sine)
inline float autoGain (int type, float g, float bias)
{
    double in = 0, out = 0;
    for (int i = 0; i < 64; ++i)
    {
        const float x = 0.25f * std::sin (juce::MathConstants<float>::twoPi * (i + 0.5f) / 64.0f);
        const float y = type == 6 ? x * g : shape (type, x * g, bias);
        in += x * x; out += y * y;
    }
    const float r = (float) std::sqrt (in / std::max (out, 1.0e-12));
    return juce::jlimit (0.02f, 4.0f, r);
}
} // namespace vsat
