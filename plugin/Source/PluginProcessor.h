#pragma once

#include "BusCompressor.h"
#include "SampleKit.h"
#include "SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <padporter/DeviceLibrary.h>

#include <functional>
#include <mutex>

namespace padporter::plugin
{
class PadPorterProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster,
                                 private juce::Timer
{
public:
    PadPorterProcessor();
    ~PadPorterProcessor() override;

    // ---- AudioProcessor ---------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Message-thread API for the editor --------------------------------
    void openProject (const juce::File& xpj);
    void selectTrack (int trackIndex);

    const LoadedProject* getLoadedProject() const noexcept { return loaded.get(); }
    juce::File getProjectFile() const;
    bool isLoading() const noexcept { return loading; }
    juce::String getStatusText() const { return status; }
    bool statusIsError() const noexcept { return statusError; }

    int getSelectedPad() const noexcept { return selectedPad; }
    void setSelectedPad (int padIndex);
    int getBank() const noexcept { return bank; }
    void setBank (int newBank);

    EffectivePad getEffectivePad (int padIndex) const;
    PadOverride getOverride (int padIndex) const;
    bool hasOverride (int padIndex) const;
    void setOverride (int padIndex, const PadOverride& override);
    void resetOverride (int padIndex);

    void triggerPad (int padIndex, float velocity, bool noteOn);
    std::uint64_t getPadActivity (int half) const noexcept { return engine.getActivity (half); }

    /** Writes a MIDI file with one part (or one pad, padIndex >= 0) of a sequence for dragging into the host. */
    juce::File createDragMidiFile (int sequenceIndex, Part part, int padIndex, juce::String& error) const;

    /** Renders a WAV of one part (or one pad, padIndex >= 0) of a sequence. Runs synchronously. */
    juce::File createDragAudioFile (int sequenceIndex, Part part, int padIndex, juce::String& error) const;

    /** MIDI or audio, depending on the drag mode chosen in the UI. */
    juce::File createDragFile (int sequenceIndex, Part part, int padIndex, juce::String& error) const;

    bool isDragAsAudio() const noexcept { return dragAsAudio; }
    void setDragAsAudio (bool shouldUseAudio);

    /** Rendered audio is kept here so that hosts can keep referencing it: Documents/PadPorter/Renders/<project>. */
    juce::File getRenderDir() const;

    // ---- Sequence parts (drums / melody) ----------------------------------
    RoleMap getRoles() const;
    PartRole getPadRole (int padIndex) const;
    bool isPadRoleAutomatic (int padIndex) const;
    void setPadRole (int padIndex, std::optional<PartRole> role);
    PartCounts getPartCounts (int sequenceIndex) const;

    int getSelectedSequence() const noexcept { return selectedSequence; }
    void setSelectedSequence (int index);

    /** Loops one part of a sequence through the sampler (preview). */
    void startPreview (int sequenceIndex, Part part);
    void stopPreview();
    /** Sequence currently previewed, or -1. */
    int getPreviewSequence() const noexcept;
    Part getPreviewPart() const noexcept { return previewPart; }
    float getPreviewPosition() const noexcept { return engine.getPatternPosition(); }

    using ExportCallback = std::function<void (bool ok, juce::String message, juce::File folder)>;
    void exportProject (const juce::File& destination, ExportCallback onFinished);

    // ---- Device (SD card) and local library -------------------------------
    const std::vector<DeviceVolume>& getDevices() const noexcept { return devices; }
    const std::vector<ProjectEntry>& getLibrary() const noexcept { return library; }
    juce::File getLibraryDir() const;
    /** Increments whenever a new device with projects appears (the editor switches to it). */
    int getDeviceGeneration() const noexcept { return deviceGeneration; }
    bool isScanning() const noexcept { return scanning; }
    bool isImporting() const noexcept { return importing; }
    void rescanDevices();
    /** Copies a device project with all samples into the library, then opens the copy. */
    void importFromDevice (const ProjectEntry& entry);

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties createBuses();

    void timerCallback() override;
    void startLoad (const juce::File& xpj, int track, OverrideMap overridesToApply, double targetBpm = 0.0);
    void restretchForSelectedSequence();
    void applyLoadOutcome (LoadOutcome outcome, int requestId, OverrideMap overridesToApply);
    void restoreProject (const juce::File& xpj, int track, OverrideMap overridesToApply);
    void rebuildKit();
    void setStatus (const juce::String& text, bool isError);
    void pollDrives();
    void startScan();
    void applyScan (std::vector<DeviceVolume> found, std::vector<ProjectEntry> lib);
    void applyImport (const ImportResult& result, const juce::String& error);

    std::vector<DeviceVolume> devices;
    std::vector<ProjectEntry> library;
    juce::StringArray lastRemovableRoots;
    int deviceGeneration = 0;
    bool scanning = false;
    bool scanPending = false;
    bool importing = false;
    int timerTicks = 0;
    juce::String pendingStatusNote;

    int selectedSequence = 0;
    bool dragAsAudio = true;
    std::atomic<double> currentSampleRate { 48000.0 };
    int previewSequence = -1;
    Part previewPart = Part::All;
    int previewId = 0;

    SamplerEngine engine;
    std::shared_ptr<SampleCache> sampleCache = std::make_shared<SampleCache>();
    std::unique_ptr<LoadedProject> loaded;

    // Guarded by stateMutex: also read by getStateInformation(), which some hosts call off the message thread.
    mutable std::mutex stateMutex;
    juce::File projectFile;
    int trackSetting = -1;
    OverrideMap overrides;
    int selectedPad = 0;
    int bank = 0;

    juce::String status;
    bool statusError = false;
    bool loading = false;
    int loadRequestId = 0;
    std::uint64_t generationCounter = 0;

    juce::ThreadPool workers;

    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* velocityParam = nullptr;
    std::atomic<float>* tuneParam = nullptr;
    std::atomic<float>* outputModeParam = nullptr;
    std::atomic<float>* deviceCompParam = nullptr;

    BusCompressor masterCompressor;                    // audio thread
    std::atomic<bool> projectCompActive { false };
    std::atomic<bool> projectCompColour { false };

public:
    /** True if the loaded project uses the device's master compressor. */
    bool projectHasMasterCompressor() const noexcept { return projectCompActive.load(); }

private:

    JUCE_DECLARE_WEAK_REFERENCEABLE (PadPorterProcessor)
    juce::WeakReference<PadPorterProcessor> selfRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadPorterProcessor)
};
} // namespace padporter::plugin
