#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace padporter::plugin
{
/**
    Stereo-linked bus compressor with soft knee and optional soft saturation.

    It approximates the device's "Color Compressor" on the main output. The device stores
    that plugin's settings as an undocumented blob, so fixed "glue" settings are used.
    Real-time safe: no allocation, no locks.
*/
class BusCompressor
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        attackCoeff = std::exp (-1.0 / (kAttackSeconds * sr));
        releaseCoeff = std::exp (-1.0 / (kReleaseSeconds * sr));
        reset();
    }

    void reset() noexcept { reductionDb = 0.0; }

    void setColour (bool shouldSaturate) noexcept { colour = shouldSaturate; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float peak = juce::jmax (std::abs (left[i]), std::abs (right[i]));
            const double levelDb = peak > 1.0e-6f ? 20.0 * std::log10 (static_cast<double> (peak)) : -120.0;
            const double target = staticCurve (levelDb);

            const double coeff = target < reductionDb ? attackCoeff : releaseCoeff;
            reductionDb = coeff * reductionDb + (1.0 - coeff) * target;

            const auto gain = static_cast<float> (std::pow (10.0, (reductionDb + kMakeupDb) / 20.0));
            float l = left[i] * gain;
            float r = right[i] * gain;
            if (colour)
            {
                l = std::tanh (l * kDrive) / kDrive;
                r = std::tanh (r * kDrive) / kDrive;
            }
            left[i] = l;
            right[i] = r;
        }
    }

    double getGainReductionDb() const noexcept { return reductionDb; }

private:
    static constexpr double kThresholdDb = -14.0;
    static constexpr double kRatio = 3.0;
    static constexpr double kKneeDb = 6.0;
    static constexpr double kMakeupDb = 3.0;
    static constexpr double kAttackSeconds = 0.010;
    static constexpr double kReleaseSeconds = 0.150;
    static constexpr float kDrive = 1.3f;

    /** Gain change in dB (<= 0) for an input level. */
    static double staticCurve (double levelDb) noexcept
    {
        const double over = levelDb - kThresholdDb;
        const double slope = 1.0 / kRatio - 1.0;
        if (2.0 * over < -kKneeDb)
            return 0.0;
        if (2.0 * std::abs (over) <= kKneeDb)
            return slope * (over + kKneeDb / 2.0) * (over + kKneeDb / 2.0) / (2.0 * kKneeDb);
        return slope * over;
    }

    double attackCoeff = 0.0;
    double releaseCoeff = 0.0;
    double reductionDb = 0.0;
    bool colour = false;
};
} // namespace padporter::plugin
