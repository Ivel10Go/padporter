#include "SamplerEngine.h"

#include <cmath>

namespace padporter::plugin
{
namespace
{
constexpr float kAttackSeconds = 0.0005f;   // anti-click fade-in
constexpr float kChokeSeconds = 0.004f;     // mute group / mono cut
}

SamplerEngine::~SamplerEngine()
{
    // The audio callback has stopped when the engine is destroyed.
    delete pendingKit.exchange (nullptr);
    delete activeKit;
    delete pendingPattern.exchange (nullptr);
    delete activePattern;
    collectGarbage();
}

void SamplerEngine::setPattern (std::unique_ptr<Pattern> pattern)
{
    collectGarbage();
    if (pattern == nullptr)
    {
        delete pendingPattern.exchange (nullptr, std::memory_order_acq_rel);
        stopPatternRequested.store (true, std::memory_order_release);
        return;
    }

    pattern->lengthTicks = juce::jmax<std::int64_t> (96, pattern->lengthTicks);
    pattern->bpm = juce::jlimit (20.0, 400.0, pattern->bpm);
    delete pendingPattern.exchange (pattern.release(), std::memory_order_acq_rel);
}

void SamplerEngine::updatePattern() noexcept
{
    auto retire = [this] (Pattern* p) {
        const auto scope = retiredPatternFifo.write (1);
        retiredPatterns[static_cast<std::size_t> (scope.startIndex1)] = p;
    };

    if (stopPatternRequested.load (std::memory_order_acquire))
    {
        if (activePattern != nullptr && retiredPatternFifo.getFreeSpace() < 1)
            return; // try again next block
        stopPatternRequested.store (false, std::memory_order_release);
        if (activePattern != nullptr)
        {
            retire (activePattern);
            activePattern = nullptr;
            releaseAll (kChokeSeconds);
        }
        playingPatternId.store (-1, std::memory_order_relaxed);
    }

    if (pendingPattern.load (std::memory_order_acquire) == nullptr)
        return;
    if (activePattern != nullptr && retiredPatternFifo.getFreeSpace() < 1)
        return;

    Pattern* next = pendingPattern.exchange (nullptr, std::memory_order_acq_rel);
    if (next == nullptr)
        return;

    if (activePattern != nullptr)
        retire (activePattern);

    releaseAll (kChokeSeconds);
    activePattern = next;
    patternTick = 0.0;
    loopOffset = 0.0;
    patternCursor = 0;
    playingPatternId.store (next->id, std::memory_order_relaxed);
}

int SamplerEngine::nextPatternOffset (int from, int numSamples, double ticksPerSample) const noexcept
{
    if (activePattern == nullptr || activePattern->events.empty() || ticksPerSample <= 0.0)
        return numSamples;

    const auto& e = activePattern->events[patternCursor];
    const double samples = (static_cast<double> (e.tick) + loopOffset - patternTick) / ticksPerSample;
    if (samples >= static_cast<double> (numSamples))
        return numSamples;
    return juce::jmax (from, static_cast<int> (samples));
}

void SamplerEngine::firePatternEvent() noexcept
{
    const auto& e = activePattern->events[patternCursor];
    if (e.noteOn)
        noteOn (e.note, e.velocity, e.semitones);
    else
        noteOff (e.note);

    if (++patternCursor >= activePattern->events.size())
    {
        patternCursor = 0;
        loopOffset += static_cast<double> (activePattern->lengthTicks);
    }
}

// ---------------------------------------------------------------------------
// Message thread

void SamplerEngine::setKit (std::unique_ptr<Kit> kit)
{
    collectGarbage();
    // A kit still pending was never seen by the audio thread and can be deleted here.
    delete pendingKit.exchange (kit.release(), std::memory_order_acq_rel);
}

void SamplerEngine::collectGarbage()
{
    {
        const auto scope = retiredFifo.read (retiredFifo.getNumReady());
        scope.forEach ([this] (int index) {
            auto& slot = retired[static_cast<std::size_t> (index)];
            delete slot;
            slot = nullptr;
        });
    }
    {
        const auto scope = retiredPatternFifo.read (retiredPatternFifo.getNumReady());
        scope.forEach ([this] (int index) {
            auto& slot = retiredPatterns[static_cast<std::size_t> (index)];
            delete slot;
            slot = nullptr;
        });
    }
}

void SamplerEngine::triggerFromUi (int note, float velocity, bool noteOn)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0)
        uiEvents[static_cast<std::size_t> (scope.startIndex1)] = { note, velocity, noteOn };
}

std::uint64_t SamplerEngine::getActivity (int half) const noexcept
{
    return activity[static_cast<std::size_t> (half & 1)].load (std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Audio thread

void SamplerEngine::prepare (double sampleRate) noexcept
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& v : voices)
        v.active = false;
}

void SamplerEngine::swapInPendingKit() noexcept
{
    if (pendingKit.load (std::memory_order_acquire) == nullptr)
        return;

    // Only take the new kit if the old one can be handed back for deletion.
    if (activeKit != nullptr && retiredFifo.getFreeSpace() < 1)
        return;

    Kit* next = pendingKit.exchange (nullptr, std::memory_order_acq_rel);
    if (next == nullptr)
        return;

    Kit* old = activeKit;
    activeKit = next;

    if (old != nullptr)
    {
        if (old->sampleGeneration != next->sampleGeneration)
            for (auto& v : voices)
                v.active = false; // samples may be freed with the old kit

        const auto scope = retiredFifo.write (1);
        retired[static_cast<std::size_t> (scope.startIndex1)] = old;
    }
}

void SamplerEngine::render (const Outputs& outputs, int numSamples, const juce::MidiBuffer& midi, const Params& newParams,
                            const ScheduledNote* scheduled, int numScheduled) noexcept
{
    params = newParams;
    tuneRatio = std::pow (2.0, static_cast<double> (params.globalTuneSemitones) / 12.0);

    swapInPendingKit();

    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        scope.forEach ([this] (int index) {
            const auto& e = uiEvents[static_cast<std::size_t> (index)];
            if (e.noteOn)
                noteOn (e.note, e.velocity);
            else
                noteOff (e.note);
        });
    }

    updatePattern();
    const double ticksPerSample = activePattern != nullptr
                                      ? activePattern->bpm / 60.0 * kTicksPerQuarter / hostSampleRate
                                      : 0.0;

    // Merge host MIDI and preview pattern events sample-accurately.
    int position = 0;
    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();

    int scheduledIndex = 0;

    for (;;)
    {
        const int midiPos = midiIt != midiEnd ? juce::jlimit (position, numSamples, (*midiIt).samplePosition) : numSamples;
        const int patternPos = nextPatternOffset (position, numSamples, ticksPerSample);
        const int scheduledPos = scheduledIndex < numScheduled ? juce::jlimit (position, numSamples, scheduled[scheduledIndex].offset) : numSamples;
        const int next = juce::jmin (midiPos, patternPos, scheduledPos);
        if (next >= numSamples)
            break;

        if (next > position)
        {
            renderVoices (outputs, position, next - position);
            position = next;
        }

        if (scheduledPos == next)
        {
            const auto& s = scheduled[scheduledIndex++];
            if (s.noteOn)
                noteOn (s.note, s.velocity, s.semitones);
            else
                noteOff (s.note);
        }
        else if (patternPos <= midiPos)
            firePatternEvent();
        else
        {
            const auto metadata = *midiIt;
            handleMidi (metadata.data, metadata.numBytes);
            ++midiIt;
        }
    }

    if (position < numSamples)
        renderVoices (outputs, position, numSamples - position);

    if (activePattern != nullptr)
    {
        patternTick += ticksPerSample * numSamples;
        const auto length = static_cast<double> (activePattern->lengthTicks);
        patternPosition.store (static_cast<float> (std::fmod (patternTick, length) / length), std::memory_order_relaxed);
    }

    std::uint64_t bits[2] = { 0, 0 };
    for (const auto& v : voices)
        if (v.active && v.pad >= 0)
            bits[v.pad / 64] |= (std::uint64_t { 1 } << (v.pad % 64));

    activity[0].store (bits[0], std::memory_order_relaxed);
    activity[1].store (bits[1], std::memory_order_relaxed);
}

void SamplerEngine::handleMidi (const juce::uint8* data, int size) noexcept
{
    if (size < 1)
        return;

    const auto status = data[0] & 0xf0;
    if (status == 0x90 && size >= 3)
    {
        if (data[2] == 0)
            noteOff (data[1]);
        else
            noteOn (data[1], static_cast<float> (data[2]) / 127.0f);
    }
    else if (status == 0x80 && size >= 3)
    {
        noteOff (data[1]);
    }
    else if (status == 0xb0 && size >= 3 && (data[1] == 120 || data[1] == 123))
    {
        releaseAll (kChokeSeconds);
    }
}

SamplerEngine::Voice& SamplerEngine::allocateVoice() noexcept
{
    Voice* oldest = &voices[0];
    for (auto& v : voices)
    {
        if (! v.active)
            return v;
        if (v.age < oldest->age)
            oldest = &v;
    }
    return *oldest; // voice stealing
}

void SamplerEngine::startRelease (Voice& v, float seconds) noexcept
{
    if (v.stage == Stage::Release && v.envelopeStep > 0.0f)
    {
        // Already releasing: only ever speed it up.
        const auto step = v.envelope / juce::jmax (1.0f, seconds * static_cast<float> (hostSampleRate));
        v.envelopeStep = juce::jmax (v.envelopeStep, step);
        return;
    }

    v.stage = Stage::Release;
    v.envelopeStep = juce::jmax (1.0e-6f, v.envelope / juce::jmax (1.0f, seconds * static_cast<float> (hostSampleRate)));
}

void SamplerEngine::releaseAll (float seconds) noexcept
{
    for (auto& v : voices)
        if (v.active)
            startRelease (v, seconds);
}

void SamplerEngine::noteOn (int note, float velocity, float semitones) noexcept
{
    if (activeKit == nullptr || note < 0 || note > 127)
        return;

    const int padIndex = activeKit->noteToPad[static_cast<std::size_t> (note)];
    if (padIndex < 0)
        return;

    const auto& pad = activeKit->pads[static_cast<std::size_t> (padIndex)];
    if (pad.sample == nullptr || pad.end - pad.start < 2)
        return;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& other = activeKit->pads[static_cast<std::size_t> (v.pad)];
        const bool sameGroup = pad.muteGroup > 0 && other.muteGroup == pad.muteGroup && v.pad != padIndex;
        const bool monoRetrigger = pad.monophonic && v.pad == padIndex;
        if (sameGroup || monoRetrigger)
            startRelease (v, kChokeSeconds);
    }

    auto& v = allocateVoice();
    v = Voice {};
    v.active = true;
    v.pad = padIndex;
    v.note = note;
    v.position = pad.start;
    v.notePitch = semitones == 0.0f ? 1.0 : std::pow (2.0, static_cast<double> (semitones) / 12.0);
    v.held = true;
    v.age = ++voiceCounter;

    const auto vel = juce::jlimit (0.0f, 1.0f, velocity);
    const auto s = juce::jlimit (0.0f, 1.0f, params.velocitySensitivity);
    v.velocityGain = (1.0f - s) + s * vel; // linear velocity-to-level, like the device

    v.envelope = 0.0f;
    v.stage = Stage::Attack;
    v.envelopeStep = 1.0f / juce::jmax (1.0f, kAttackSeconds * static_cast<float> (hostSampleRate));
}

void SamplerEngine::noteOff (int note) noexcept
{
    if (activeKit == nullptr)
        return;

    for (auto& v : voices)
    {
        if (! v.active || v.note != note || ! v.held)
            continue;

        v.held = false;
        const auto& pad = activeKit->pads[static_cast<std::size_t> (v.pad)];
        if (! pad.oneShot)
            startRelease (v, pad.releaseSeconds);
    }
}

void SamplerEngine::renderVoices (const Outputs& outputs, int start, int num) noexcept
{
    if (activeKit == nullptr)
        return;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& pad = activeKit->pads[static_cast<std::size_t> (v.pad)];
        const auto* sample = pad.sample;
        if (sample == nullptr)
        {
            v.active = false;
            continue;
        }

        const auto& audio = sample->audio;
        const float* srcL = audio.getReadPointer (0);
        const float* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
        const int end = juce::jmin (pad.end, audio.getNumSamples());
        const double increment = pad.pitchRatio * v.notePitch * tuneRatio * sample->sampleRate / hostSampleRate;
        const bool canLoop = pad.loop && ! pad.oneShot && (end - 1 - pad.loopStart) > 1;

        float* outL = outputs.left[static_cast<std::size_t> (pad.output)];
        float* outR = outputs.right[static_cast<std::size_t> (pad.output)];
        if (outL == nullptr || outR == nullptr)
            continue;

        const float gainL = pad.gainL * v.velocityGain;
        const float gainR = pad.gainR * v.velocityGain;

        for (int i = start; i < start + num; ++i)
        {
            int index = static_cast<int> (v.position);
            if (index >= end - 1)
            {
                if (canLoop && v.held)
                {
                    const double loopLength = static_cast<double> (end - 1 - pad.loopStart);
                    v.position = pad.loopStart + std::fmod (v.position - pad.loopStart, loopLength);
                    index = static_cast<int> (v.position);
                }
                else
                {
                    v.active = false;
                    break;
                }
            }
            if (index < 0)
            {
                v.active = false;
                break;
            }

            // 4-point Hermite interpolation (clamped to the play range).
            const auto frac = static_cast<float> (v.position - index);
            const int i0 = juce::jmax (pad.start, index - 1);
            const int i2 = index + 1;
            const int i3 = juce::jmin (end - 1, index + 2);
            const auto hermite = [frac] (float y0, float y1, float y2, float y3) {
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * frac + c2) * frac + c1) * frac + y1;
            };
            const float l = hermite (srcL[i0], srcL[index], srcL[i2], srcL[i3]);
            const float r = hermite (srcR[i0], srcR[index], srcR[i2], srcR[i3]);

            if (v.stage == Stage::Attack)
            {
                v.envelope += v.envelopeStep;
                if (v.envelope >= 1.0f)
                {
                    v.envelope = 1.0f;
                    v.stage = Stage::Sustain;
                }
            }
            else if (v.stage == Stage::Release)
            {
                v.envelope -= v.envelopeStep;
                if (v.envelope <= 0.0f)
                {
                    v.active = false;
                    break;
                }
            }

            outL[i] += l * v.envelope * gainL;
            outR[i] += r * v.envelope * gainR;
            v.position += increment;
        }
    }
}
} // namespace padporter::plugin
