#include "TimeStretch.h"

#include <cmath>
#include <vector>

namespace padporter::plugin
{
namespace
{
constexpr int kDecimation = 4;

float sampleAt (const std::vector<float>& signal, long long index) noexcept
{
    return index >= 0 && index < static_cast<long long> (signal.size()) ? signal[static_cast<std::size_t> (index)] : 0.0f;
}

double correlate (const std::vector<float>& signal, long long a, long long b, int length, int step) noexcept
{
    double sum = 0.0;
    for (int i = 0; i < length; i += step)
        sum += static_cast<double> (sampleAt (signal, a + i)) * sampleAt (signal, b + i);
    return sum;
}
} // namespace

juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& input, double sampleRate, double factor)
{
    const int inLength = input.getNumSamples();
    const int channels = input.getNumChannels();

    if (inLength < 16 || channels == 0 || factor <= 0.0 || std::abs (factor - 1.0) < 1.0e-4)
        return input;

    // ~46 ms frames, 50 % overlap, +-11 ms alignment search.
    const int frame = juce::jlimit (512, 8192, static_cast<int> (std::lround (sampleRate * 0.046)) & ~1);
    const int hopOut = frame / 2;
    const double hopIn = hopOut / factor;
    const int tolerance = frame / 4;
    const auto outLength = static_cast<int> (std::llround (static_cast<double> (inLength) * factor));

    std::vector<float> window (static_cast<std::size_t> (frame));
    for (int i = 0; i < frame; ++i)
        window[static_cast<std::size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (frame));

    // Mono analysis signal and a decimated copy for the coarse search.
    std::vector<float> mono (static_cast<std::size_t> (inLength), 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* data = input.getReadPointer (ch);
        for (int i = 0; i < inLength; ++i)
            mono[static_cast<std::size_t> (i)] += data[i] / static_cast<float> (channels);
    }

    juce::AudioBuffer<float> output (channels, outLength + frame);
    output.clear();

    long long previous = 0;
    for (int k = 0;; ++k)
    {
        const long long outPos = static_cast<long long> (k) * hopOut;
        if (outPos >= outLength)
            break;

        long long pos = 0;
        if (k > 0)
        {
            // Find the input position near the nominal one whose waveform best continues
            // the previously copied frame (natural continuation = previous + hopOut).
            const auto nominal = static_cast<long long> (std::llround (k * hopIn));
            const long long natural = previous + hopOut;

            long long best = nominal;
            double bestScore = -1.0e300;
            for (long long c = nominal - tolerance; c <= nominal + tolerance; c += kDecimation)
            {
                const double score = correlate (mono, natural, c, frame / 2, kDecimation);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = c;
                }
            }
            for (long long c = best - kDecimation + 1; c < best + kDecimation; ++c)
            {
                const double score = correlate (mono, natural, c, frame / 2, 1);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = c;
                }
            }
            pos = juce::jlimit<long long> (0, inLength - 1, best);
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = input.getReadPointer (ch);
            float* out = output.getWritePointer (ch);
            for (int i = 0; i < frame; ++i)
            {
                const auto src = pos + i;
                if (src >= inLength)
                    break;
                // The very first frame starts at full level so attacks are not faded in.
                const float w = (k == 0 && i < hopOut) ? 1.0f : window[static_cast<std::size_t> (i)];
                out[outPos + i] += in[src] * w;
            }
        }
        previous = pos;
    }

    output.setSize (channels, outLength, true);
    return output;
}
} // namespace padporter::plugin
