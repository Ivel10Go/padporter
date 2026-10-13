#pragma once

#include "SampleKit.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>

namespace padporter::plugin
{
/**
    Drum sampler voice engine.

    Threading contract:
    - setKit(), collectGarbage(), triggerFromUi() and getActivity() run on the message thread.
    - prepare() and render() run on the audio thread.
    The audio thread never allocates, frees or locks: kits are handed over through an atomic
    pointer, retired kits are passed back through a lock-free FIFO and deleted by the message thread.
*/
class SamplerEngine
{
public:
    static constexpr int kMaxVoices = 64;
    static constexpr int kNumOutputs = 16;

    struct Outputs
    {
        std::array<float*, kNumOutputs> left {};
        std::array<float*, kNumOutputs> right {};
    };

    struct Params
    {
        float velocitySensitivity = 1.0f;   // 0..1
        float globalTuneSemitones = 0.0f;
    };

    /** A looping note pattern for previewing a sequence (built on the message thread). */
    struct PatternEvent
    {
        std::int64_t tick = 0;
        int note = 0;
        float velocity = 0.0f;
        bool noteOn = false;
        float semitones = 0.0f;   // per-note pitch ("16 Levels")
    };

    /** A note at a sample offset inside the current block (offline rendering). */
    struct ScheduledNote
    {
        int offset = 0;
        int note = 0;
        float velocity = 0.0f;
        bool noteOn = false;
        float semitones = 0.0f;
    };

    struct Pattern
    {
        std::vector<PatternEvent> events;     // sorted by tick, note-offs before note-ons
        std::int64_t lengthTicks = 3840;
        double bpm = 120.0;
        int id = 0;
    };

    SamplerEngine() = default;
    ~SamplerEngine();

    // message thread
    void setKit (std::unique_ptr<Kit> kit);
    void collectGarbage();
    void triggerFromUi (int note, float velocity, bool noteOn);
    std::uint64_t getActivity (int half) const noexcept;

    /** Starts looping a pattern (replacing any running one); nullptr stops the preview. */
    void setPattern (std::unique_ptr<Pattern> pattern);
    /** Id of the pattern currently playing, or -1. */
    int getPlayingPatternId() const noexcept { return playingPatternId.load (std::memory_order_relaxed); }
    /** Loop position of the playing pattern, 0..1. */
    float getPatternPosition() const noexcept { return patternPosition.load (std::memory_order_relaxed); }

    // audio thread
    void prepare (double sampleRate) noexcept;
    void render (const Outputs& outputs, int numSamples, const juce::MidiBuffer& midi, const Params& params,
                 const ScheduledNote* scheduled = nullptr, int numScheduled = 0) noexcept;

private:
    enum class Stage
    {
        Attack,
        Sustain,
        Release
    };

    struct Voice
    {
        bool active = false;
        int pad = -1;
        int note = -1;
        double position = 0.0;
        double notePitch = 1.0;     // per-note pitch ratio ("16 Levels")
        float velocityGain = 1.0f;
        float envelope = 0.0f;
        float envelopeStep = 0.0f;
        Stage stage = Stage::Sustain;
        bool held = false;
        std::uint32_t age = 0;
    };

    struct UiEvent
    {
        int note = 0;
        float velocity = 0.0f;
        bool noteOn = false;
    };

    void swapInPendingKit() noexcept;
    void updatePattern() noexcept;
    int nextPatternOffset (int from, int numSamples, double ticksPerSample) const noexcept;
    void firePatternEvent() noexcept;
    void handleMidi (const juce::uint8* data, int size) noexcept;
    void noteOn (int note, float velocity, float semitones = 0.0f) noexcept;
    void noteOff (int note) noexcept;
    void releaseAll (float seconds) noexcept;
    void startRelease (Voice& v, float seconds) noexcept;
    Voice& allocateVoice() noexcept;
    void renderVoices (const Outputs& outputs, int start, int num) noexcept;

    std::atomic<Kit*> pendingKit { nullptr };
    Kit* activeKit = nullptr;                       // audio thread only

    static constexpr int kRetiredSlots = 32;
    juce::AbstractFifo retiredFifo { kRetiredSlots };
    std::array<Kit*, kRetiredSlots> retired {};

    static constexpr int kUiSlots = 128;
    juce::AbstractFifo uiFifo { kUiSlots };
    std::array<UiEvent, kUiSlots> uiEvents {};

    // Pattern preview: same hand-over scheme as the kit.
    std::atomic<Pattern*> pendingPattern { nullptr };
    std::atomic<bool> stopPatternRequested { false };
    Pattern* activePattern = nullptr;               // audio thread only
    static constexpr int kRetiredPatternSlots = 16;
    juce::AbstractFifo retiredPatternFifo { kRetiredPatternSlots };
    std::array<Pattern*, kRetiredPatternSlots> retiredPatterns {};
    double patternTick = 0.0;                       // ticks since start (not wrapped)
    double loopOffset = 0.0;                        // ticks added for completed loops
    std::size_t patternCursor = 0;
    std::atomic<int> playingPatternId { -1 };
    std::atomic<float> patternPosition { 0.0f };

    std::array<Voice, kMaxVoices> voices {};
    std::array<std::atomic<std::uint64_t>, 2> activity {};

    double hostSampleRate = 44100.0;
    double tuneRatio = 1.0;
    Params params;
    std::uint32_t voiceCounter = 0;
};
} // namespace padporter::plugin
