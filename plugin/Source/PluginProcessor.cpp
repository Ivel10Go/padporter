#include "PluginProcessor.h"
#include "OfflineRenderer.h"
#include "PluginEditor.h"

#include <padporter/Exporter.h>
#include <padporter/MidiWriter.h>
#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
const juce::Identifier kStateType ("PadPorterState");
const juce::Identifier kOverridesType ("Overrides");
const juce::Identifier kPadType ("Pad");

constexpr float kMinMasterDb = -48.0f;
} // namespace

PadPorterProcessor::BusesProperties PadPorterProcessor::createBuses()
{
    auto props = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= SamplerEngine::kNumOutputs; ++i)
        props = props.withOutput ("Pad Out " + juce::String (i), juce::AudioChannelSet::stereo(), true);
    return props;
}

juce::AudioProcessorValueTreeState::ParameterLayout PadPorterProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "master", 1 }, "Master Volume",
        juce::NormalisableRange<float> (kMinMasterDb, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "velsens", 1 }, "Velocity Sensitivity",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tune", 1 }, "Global Tune",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "outmode", 1 }, "Output Mode",
        juce::StringArray { "Stereo Mix", "Multi-Out (16 x Stereo)" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "devicecomp", 1 }, "Device Master Compressor", true));

    return layout;
}

PadPorterProcessor::PadPorterProcessor()
    : juce::AudioProcessor (createBuses()),
      parameters (*this, nullptr, "PARAMS", createParameterLayout()),
      workers (juce::ThreadPoolOptions {}.withThreadName ("PadPorter worker").withNumberOfThreads (1))
{
    masterParam = parameters.getRawParameterValue ("master");
    velocityParam = parameters.getRawParameterValue ("velsens");
    tuneParam = parameters.getRawParameterValue ("tune");
    outputModeParam = parameters.getRawParameterValue ("outmode");
    deviceCompParam = parameters.getRawParameterValue ("devicecomp");

    selfRef = this;
    setStatus (juce::String::fromUTF8 ("Gerät per USB im SD-Card-Access-Modus anschließen oder ein Projekt öffnen."), false);
    startTimerHz (10);
    pollDrives();
}

PadPorterProcessor::~PadPorterProcessor()
{
    stopTimer();
    workers.removeAllJobs (true, 10000);
}

// ---------------------------------------------------------------------------
// Audio

void PadPorterProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate > 0.0)
        currentSampleRate.store (sampleRate);
    engine.prepare (sampleRate);
    masterCompressor.prepare (sampleRate);
}

bool PadPorterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled())
            return false;

    return true;
}

void PadPorterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels < 1)
        return;

    float* mainL = buffer.getWritePointer (0);
    float* mainR = numChannels > 1 ? buffer.getWritePointer (1) : mainL;

    const bool multiOut = outputModeParam->load() > 0.5f;
    const int busCount = getBusCount (false);

    SamplerEngine::Outputs outs;
    for (int o = 0; o < SamplerEngine::kNumOutputs; ++o)
    {
        outs.left[static_cast<std::size_t> (o)] = mainL;
        outs.right[static_cast<std::size_t> (o)] = mainR;

        if (multiOut && o > 0 && o < busCount)
        {
            const auto* bus = getBus (false, o);
            if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2)
            {
                const int index = bus->getChannelIndexInProcessBlockBuffer (0);
                if (index >= 0 && index + 1 < numChannels)
                {
                    outs.left[static_cast<std::size_t> (o)] = buffer.getWritePointer (index);
                    outs.right[static_cast<std::size_t> (o)] = buffer.getWritePointer (index + 1);
                }
            }
        }
    }

    SamplerEngine::Params p;
    p.velocitySensitivity = velocityParam->load() / 100.0f;
    p.globalTuneSemitones = tuneParam->load();
    engine.render (outs, numSamples, midi, p);

    // The device's main output runs through its master compressor (approximation).
    if (numChannels > 1 && deviceCompParam->load() > 0.5f && projectCompActive.load (std::memory_order_relaxed))
    {
        masterCompressor.setColour (projectCompColour.load (std::memory_order_relaxed));
        masterCompressor.process (mainL, mainR, numSamples);
    }

    const auto masterDb = masterParam->load();
    const auto gain = masterDb <= kMinMasterDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (masterDb);
    if (std::abs (gain - 1.0f) > 1.0e-6f)
        buffer.applyGain (gain);

    for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, numSamples);
        if (peak > outputPeak[ch].load (std::memory_order_relaxed))
            outputPeak[ch].store (peak, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// State

void PadPorterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state (kStateType);
    state.setProperty ("version", 1, nullptr);

    {
        std::lock_guard<std::mutex> lock (stateMutex);
        state.setProperty ("projectPath", projectFile.getFullPathName(), nullptr);
        state.setProperty ("track", trackSetting, nullptr);
        state.setProperty ("bank", bank, nullptr);
        state.setProperty ("selectedPad", selectedPad, nullptr);
        state.setProperty ("sequence", selectedSequence, nullptr);
        state.setProperty ("dragAudio", dragAsAudio, nullptr);
        state.setProperty ("uiScale", editorScale.load(), nullptr);

        juce::ValueTree ovTree (kOverridesType);
        for (const auto& [index, ov] : overrides)
        {
            if (ov.isEmpty())
                continue;
            juce::ValueTree p (kPadType);
            p.setProperty ("index", index, nullptr);
            if (ov.tuneSemitones) p.setProperty ("tune", *ov.tuneSemitones, nullptr);
            if (ov.gainDb)        p.setProperty ("gainDb", *ov.gainDb, nullptr);
            if (ov.pan)           p.setProperty ("pan", *ov.pan, nullptr);
            if (ov.oneShot)       p.setProperty ("oneShot", *ov.oneShot, nullptr);
            if (ov.muteGroup)     p.setProperty ("muteGroup", *ov.muteGroup, nullptr);
            if (ov.output)        p.setProperty ("output", *ov.output, nullptr);
            if (ov.role)          p.setProperty ("role", *ov.role == PartRole::Melody ? 1 : 0, nullptr);
            ovTree.appendChild (p, nullptr);
        }
        state.appendChild (ovTree, nullptr);
    }

    state.appendChild (parameters.copyState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PadPorterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (kStateType))
        return;

    if (const auto params = state.getChildWithName (parameters.state.getType()); params.isValid())
        parameters.replaceState (params);

    OverrideMap restored;
    for (const auto& p : state.getChildWithName (kOverridesType))
    {
        if (! p.hasType (kPadType))
            continue;
        PadOverride ov;
        if (p.hasProperty ("tune"))      ov.tuneSemitones = static_cast<double> (p["tune"]);
        if (p.hasProperty ("gainDb"))    ov.gainDb = static_cast<double> (p["gainDb"]);
        if (p.hasProperty ("pan"))       ov.pan = static_cast<double> (p["pan"]);
        if (p.hasProperty ("oneShot"))   ov.oneShot = static_cast<bool> (p["oneShot"]);
        if (p.hasProperty ("muteGroup")) ov.muteGroup = static_cast<int> (p["muteGroup"]);
        if (p.hasProperty ("output"))    ov.output = static_cast<int> (p["output"]);
        if (p.hasProperty ("role"))      ov.role = static_cast<int> (p["role"]) == 1 ? PartRole::Melody : PartRole::Drums;
        restored[static_cast<int> (p["index"])] = ov;
    }

    const juce::File file (state["projectPath"].toString());
    const int track = state.getProperty ("track", -1);
    const int newBank = state.getProperty ("bank", 0);
    const int newPad = state.getProperty ("selectedPad", 0);
    const int newSequence = state.getProperty ("sequence", -1);
    const bool newDragAudio = state.getProperty ("dragAudio", true);
    editorScale.store (static_cast<double> (state.getProperty ("uiScale", 0.0)));

    auto apply = [weak = selfRef, file, track, newBank, newPad, newSequence, newDragAudio, restored] {
        if (auto* self = weak.get())
        {
            self->dragAsAudio = newDragAudio;
            {
                std::lock_guard<std::mutex> lock (self->stateMutex);
                self->bank = juce::jlimit (0, 7, newBank);
                self->selectedPad = juce::jlimit (0, kMaxPads - 1, newPad);
                self->selectedSequence = newSequence;
            }
            self->restoreProject (file, track, restored);
        }
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}

void PadPorterProcessor::restoreProject (const juce::File& xpj, int track, OverrideMap overridesToApply)
{
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        projectFile = xpj;
        trackSetting = track;
        overrides = overridesToApply;
    }

    if (xpj.getFullPathName().isEmpty())
    {
        sendChangeMessage();
        return;
    }

    if (! xpj.existsAsFile())
    {
        setStatus ("Projekt nicht gefunden: " + xpj.getFullPathName() + juce::String::fromUTF8 (" - bitte neu öffnen oder erneut importieren."), true);
        return;
    }

    startLoad (xpj, track, std::move (overridesToApply));
}

// ---------------------------------------------------------------------------
// Loading

void PadPorterProcessor::openProject (const juce::File& xpj)
{
    if (loaded == nullptr || loaded->file != xpj)
        sampleCache->clear();

    {
        std::lock_guard<std::mutex> lock (stateMutex);
        projectFile = xpj;
        trackSetting = -1;
        selectedPad = 0;
        bank = 0;
        selectedSequence = -1;
    }
    startLoad (xpj, -1, {});
}

void PadPorterProcessor::selectTrack (int trackIndex)
{
    if (loaded == nullptr || loaded->trackIndex == trackIndex)
        return;

    {
        std::lock_guard<std::mutex> lock (stateMutex);
        trackSetting = trackIndex;
        selectedPad = 0;
        bank = 0;
    }
    startLoad (loaded->file, trackIndex, {}); // overrides belong to the previous track's pads
}

void PadPorterProcessor::startLoad (const juce::File& xpj, int track, OverrideMap overridesToApply, double targetBpm)
{
    const int requestId = ++loadRequestId;
    loading = true;
    setStatus ("Lade " + xpj.getFileNameWithoutExtension() + " ...", false);

    auto cache = sampleCache;
    workers.addJob ([weak = selfRef, xpj, track, cache, requestId, targetBpm, ov = std::move (overridesToApply)] {
        auto outcome = std::make_shared<LoadOutcome> (loadProject (xpj, track, *cache, targetBpm));
        juce::MessageManager::callAsync ([weak, outcome, requestId, ov] {
            if (auto* self = weak.get())
                self->applyLoadOutcome (std::move (*outcome), requestId, ov);
        });
    });
}

void PadPorterProcessor::applyLoadOutcome (LoadOutcome outcome, int requestId, OverrideMap overridesToApply)
{
    if (requestId != loadRequestId)
        return; // superseded by a newer request

    loading = false;

    if (! outcome.ok)
    {
        pendingStatusNote.clear();
        setStatus ("Projekt konnte nicht geladen werden: " + outcome.error, true);
        return;
    }

    outcome.loaded->generation = ++generationCounter;
    loaded = std::move (outcome.loaded);

    stopPreview();

    // Keep a valid selection; prefer the first sequence that has notes on the drum track.
    const int numSequences = static_cast<int> (loaded->project->sequences.size());
    if (selectedSequence < 0 || selectedSequence >= numSequences)
    {
        selectedSequence = numSequences > 0 ? 0 : -1;
        if (const auto* t = loaded->track())
            for (int i = 0; i < numSequences; ++i)
                if (const auto* c = loaded->project->sequences[static_cast<std::size_t> (i)].clipForTrack (t->name);
                    c != nullptr && c->playableNoteCount() > 0)
                {
                    selectedSequence = i;
                    break;
                }
    }

    {
        std::lock_guard<std::mutex> lock (stateMutex);
        overrides = std::move (overridesToApply);
        trackSetting = loaded->trackIndex;
    }

    projectCompActive.store (loaded->project->masterCompressor.active());
    projectCompColour.store (loaded->project->masterCompressor.colorOn);

    rebuildKit();
    restretchForSelectedSequence();

    const auto& diag = loaded->diagnostics;
    juce::String text = fromUtf8 (loaded->project->name);
    if (const auto* t = loaded->track())
        text << "  -  " << fromUtf8 (t->name) << ": " << t->loadedPadCount() << " Pads";
    text << ", " << static_cast<int> (loaded->project->sequences.size()) << " Sequences";
    const auto warnings = diag.count (Severity::Warning) + diag.count (Severity::Error);
    if (warnings > 0)
        text << "  -  " << static_cast<int> (warnings) << " Warnung(en)";
    if (pendingStatusNote.isNotEmpty())
        text << "  -  " << pendingStatusNote;
    pendingStatusNote.clear();

    setStatus (text, false);
}

void PadPorterProcessor::rebuildKit()
{
    if (loaded == nullptr)
        return;

    OverrideMap copy;
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        copy = overrides;
    }
    engine.setKit (buildKit (*loaded, copy));
}

void PadPorterProcessor::setStatus (const juce::String& text, bool isError)
{
    status = text;
    statusError = isError;
    sendChangeMessage();
}

void PadPorterProcessor::timerCallback()
{
    engine.collectGarbage();

    if (++timerTicks % 20 == 0) // every 2 s
        pollDrives();
}

// ---------------------------------------------------------------------------
// Device detection and import

juce::File PadPorterProcessor::getLibraryDir() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PadPorter").getChildFile ("Projekte");
}

void PadPorterProcessor::pollDrives()
{
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);

    juce::StringArray removable;
    for (const auto& root : roots)
        if (root.isOnRemovableDrive())
            removable.add (root.getFullPathName());

    if (removable != lastRemovableRoots)
    {
        lastRemovableRoots = removable;
        startScan();
    }
}

void PadPorterProcessor::rescanDevices()
{
    lastRemovableRoots.clear();
    pollDrives();
    if (! scanning)
        startScan();
}

void PadPorterProcessor::startScan()
{
    if (scanning)
    {
        scanPending = true;
        return;
    }

    scanning = true;
    sendChangeMessage();

    std::vector<std::filesystem::path> roots;
    for (const auto& r : lastRemovableRoots)
        roots.push_back (toStdPath (juce::File (r)));
    const auto libDir = toStdPath (getLibraryDir());

    workers.addJob ([weak = selfRef, roots, libDir] {
        auto found = std::make_shared<std::vector<DeviceVolume>>();
        for (const auto& root : roots)
        {
            try
            {
                if (auto volume = inspectVolume (root))
                    found->push_back (std::move (*volume));
            }
            catch (const std::exception&) {} // unreadable drive: ignore
        }

        auto lib = std::make_shared<std::vector<ProjectEntry>>();
        try
        {
            *lib = findProjects (libDir, 1);
        }
        catch (const std::exception&) {}

        juce::MessageManager::callAsync ([weak, found, lib] {
            if (auto* self = weak.get())
                self->applyScan (std::move (*found), std::move (*lib));
        });
    });
}

void PadPorterProcessor::applyScan (std::vector<DeviceVolume> found, std::vector<ProjectEntry> lib)
{
    bool newDevice = false;
    for (const auto& v : found)
    {
        const bool known = std::any_of (devices.begin(), devices.end(), [&v] (const DeviceVolume& d) { return d.root == v.root; });
        newDevice = newDevice || (! known && ! v.projects.empty());
    }

    devices = std::move (found);
    library = std::move (lib);
    scanning = false;

    if (newDevice)
        ++deviceGeneration;

    if (scanPending)
    {
        scanPending = false;
        startScan();
    }
    sendChangeMessage();
}

void PadPorterProcessor::importFromDevice (const ProjectEntry& entry)
{
    if (importing)
        return;

    std::filesystem::path samplesDir;
    for (const auto& v : devices)
        if (isSameOrInside (entry.xpjPath, v.root))
            samplesDir = v.samplesDir;

    importing = true;
    setStatus ("Importiere " + fromUtf8 (entry.name) + " (Projekt + Samples) ...", false);

    const auto libDir = toStdPath (getLibraryDir());
    workers.addJob ([weak = selfRef, xpj = entry.xpjPath, libDir, samplesDir] {
        auto result = std::make_shared<ImportResult>();
        juce::String error;
        try
        {
            ImportOptions options;
            options.sampleLibraryDir = samplesDir;
            *result = importProject (xpj, libDir, options);
        }
        catch (const std::exception& e)
        {
            error = fromUtf8 (e.what());
        }

        juce::MessageManager::callAsync ([weak, result, error] {
            if (auto* self = weak.get())
                self->applyImport (*result, error);
        });
    });
}

void PadPorterProcessor::applyImport (const ImportResult& result, const juce::String& error)
{
    importing = false;

    if (error.isNotEmpty())
    {
        setStatus ("Import fehlgeschlagen: " + error, true);
        return;
    }

    pendingStatusNote = "importiert: " + juce::String (result.filesCopied) + " Dateien ("
                      + juce::File::descriptionOfSizeInBytes (static_cast<juce::int64> (result.bytesCopied)) + ")";
    if (result.samplesRecovered > 0)
        pendingStatusNote << ", " << result.samplesRecovered << juce::String::fromUTF8 (" aus der Geräte-Bibliothek");
    if (result.samplesMissing > 0)
        pendingStatusNote << ", " << result.samplesMissing << " Samples fehlen";

    startScan(); // refresh the library list
    openProject (toJuceFile (result.projectFile));
}

// ---------------------------------------------------------------------------
// Editor API

juce::File PadPorterProcessor::getProjectFile() const
{
    std::lock_guard<std::mutex> lock (stateMutex);
    return projectFile;
}

void PadPorterProcessor::setSelectedPad (int padIndex)
{
    std::lock_guard<std::mutex> lock (stateMutex);
    selectedPad = juce::jlimit (0, kMaxPads - 1, padIndex);
}

void PadPorterProcessor::setBank (int newBank)
{
    std::lock_guard<std::mutex> lock (stateMutex);
    bank = juce::jlimit (0, 7, newBank);
}

EffectivePad PadPorterProcessor::getEffectivePad (int padIndex) const
{
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    if (pad == nullptr)
        return {};

    std::lock_guard<std::mutex> lock (stateMutex);
    const auto it = overrides.find (padIndex);
    return computeEffectivePad (*pad, it != overrides.end() ? &it->second : nullptr);
}

PadOverride PadPorterProcessor::getOverride (int padIndex) const
{
    std::lock_guard<std::mutex> lock (stateMutex);
    const auto it = overrides.find (padIndex);
    return it != overrides.end() ? it->second : PadOverride {};
}

bool PadPorterProcessor::hasOverride (int padIndex) const
{
    std::lock_guard<std::mutex> lock (stateMutex);
    const auto it = overrides.find (padIndex);
    return it != overrides.end() && ! it->second.isEmpty();
}

void PadPorterProcessor::setOverride (int padIndex, const PadOverride& ov)
{
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        overrides[padIndex] = ov;
    }
    rebuildKit();
}

void PadPorterProcessor::resetOverride (int padIndex)
{
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        overrides.erase (padIndex);
    }
    rebuildKit();
}

void PadPorterProcessor::triggerPad (int padIndex, float velocity, bool noteOn)
{
    if (const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr)
        engine.triggerFromUi (pad->midiNote, velocity, noteOn);
}

juce::File PadPorterProcessor::createDragFile (int sequenceIndex, Part part, int padIndex, juce::String& error) const
{
    return dragAsAudio ? createDragAudioFile (sequenceIndex, part, padIndex, error)
                       : createDragMidiFile (sequenceIndex, part, padIndex, error);
}

void PadPorterProcessor::setDragAsAudio (bool shouldUseAudio)
{
    dragAsAudio = shouldUseAudio;
    sendChangeMessage();
}

juce::File PadPorterProcessor::getRenderDir() const
{
    const auto name = loaded != nullptr ? fromUtf8 (sanitizeFileName (loaded->project->name)) : juce::String ("Projekt");
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("PadPorter").getChildFile ("Renders").getChildFile (name);
}

juce::File PadPorterProcessor::createDragAudioFile (int sequenceIndex, Part part, int padIndex, juce::String& error) const
{
    if (loaded == nullptr || loaded->track() == nullptr)
    {
        error = "Kein Projekt geladen.";
        return {};
    }

    RenderRequest request;
    request.loaded = loaded.get();
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        request.overrides = overrides;
    }
    request.sequenceIndex = sequenceIndex;
    request.sampleRate = currentSampleRate.load();
    request.params.velocitySensitivity = velocityParam->load() / 100.0f;
    request.params.globalTuneSemitones = tuneParam->load();

    const auto roles = getRoles();
    if (padIndex >= 0)
        request.includePad = [padIndex] (int i) { return i == padIndex; };
    else if (part == Part::Drums)
        request.includePad = [roles] (int i) { return roles[static_cast<std::size_t> (i)] == PartRole::Drums; };
    else if (part == Part::Melody)
        request.includePad = [roles] (int i) { return roles[static_cast<std::size_t> (i)] == PartRole::Melody; };

    request.applyCompressor = deviceCompParam->load() > 0.5f && projectCompActive.load();
    request.compressorColour = projectCompColour.load();

    // Warped pads must be stretched to the tempo of the rendered sequence.
    std::unique_ptr<LoadedProject> retimed;
    const auto seqBpm = loaded->project->tempoFor (loaded->project->sequences[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (loaded->project->sequences.size()) - 1, sequenceIndex))]);
    if (std::abs (seqBpm - loaded->targetBpm) >= 0.01)
    {
        retimed = std::make_unique<LoadedProject> (*loaded);
        applyWarp (*retimed, seqBpm, *sampleCache);
        request.loaded = retimed.get();
    }

    const auto result = renderSequence (request);
    if (! result.ok)
    {
        error = result.error;
        return {};
    }

    const auto& seq = loaded->project->sequences[static_cast<std::size_t> (sequenceIndex)];
    juce::String name = juce::String (seq.number()).paddedLeft ('0', 2) + " " + fromUtf8 (seq.name) + " - ";
    if (padIndex >= 0)
    {
        const auto* pad = loaded->pad (padIndex);
        name << (pad != nullptr ? fromUtf8 (pad->label()) + " " + fromUtf8 (pad->hasSample() ? pad->layers.front().sampleName : std::string()) : juce::String ("Pad"));
    }
    else
    {
        name << fromUtf8 (partName (part));
    }
    name << " (" << juce::String (result.bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << " BPM)";

    return writeRender (result, getRenderDir(), name, error);
}

juce::File PadPorterProcessor::createDragMidiFile (int sequenceIndex, Part part, int padIndex, juce::String& error) const
{
    if (loaded == nullptr || loaded->track() == nullptr)
    {
        error = "Kein Projekt geladen.";
        return {};
    }

    const auto& project = *loaded->project;
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int> (project.sequences.size()))
    {
        error = juce::String::fromUTF8 ("Ungültige Sequence.");
        return {};
    }

    const auto& seq = project.sequences[static_cast<std::size_t> (sequenceIndex)];
    const auto* clip = seq.clipForTrack (loaded->track()->name);
    auto filtered = clip != nullptr ? filterClip (*clip, *loaded->track(), getRoles(), part) : Clip {};
    const bool sixteenLevels = padIndex >= 0 && clip != nullptr && hasTunedNotes (*clip, *loaded->track(), padIndex);
    if (sixteenLevels)
    {
        // Recorded with "16 Levels": export the melody chromatically (C3 = original pitch).
        filtered = chromaticClipForPad (*clip, *loaded->track(), padIndex);
    }
    else if (padIndex >= 0)
    {
        const auto* track = loaded->track();
        std::erase_if (filtered.notes, [track, padIndex] (const NoteEvent& n) {
            const auto* pad = track->padForNote (n.note);
            return pad == nullptr || pad->index != padIndex;
        });
    }
    if (filtered.notes.empty())
    {
        error = juce::String::fromUTF8 ("Dieser Teil der Sequence enthält keine Noten.");
        return {};
    }

    const auto bytes = writeSequenceMidi (project, seq, { &filtered });

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PadPorter").getChildFile ("Drag");
    if (! dir.createDirectory())
    {
        error = "Cannot create temporary folder.";
        return {};
    }

    // Live names the clip after the file: "01 Sequence 01 - Drums".
    auto baseName = juce::String (seq.number()).paddedLeft ('0', 2).toStdString() + " " + seq.name;
    if (padIndex >= 0)
    {
        if (const auto* pad = loaded->pad (padIndex))
            baseName += " - " + pad->label() + " " + (pad->hasSample() ? pad->layers.front().sampleName : std::string());
        if (sixteenLevels)
            baseName += " (16 Levels, C3)";
    }
    else if (part != Part::All)
        baseName += std::string (" - ") + partName (part);
    const auto name = fromUtf8 (sanitizeFileName (baseName)) + ".mid";
    const auto file = dir.getChildFile (name);
    if (! file.replaceWithData (bytes.data(), bytes.size()))
    {
        error = juce::String::fromUTF8 ("Temporäre MIDI-Datei konnte nicht geschrieben werden.");
        return {};
    }
    return file;
}

void PadPorterProcessor::exportProject (const juce::File& destination, ExportCallback onFinished)
{
    if (loaded == nullptr)
    {
        onFinished (false, "Kein Projekt geladen.", {});
        return;
    }

    auto project = loaded->project;
    auto diagnostics = std::make_shared<Diagnostics> (loaded->diagnostics);

    // The export uses the drum/melody roles as currently shown (incl. manual changes).
    ExportOptions options;
    const auto roles = getRoles();
    const auto trackName = loaded->track() != nullptr ? loaded->track()->name : std::string();
    options.roleFor = [roles, trackName] (const Track& track, const Pad& pad) -> std::optional<PartRole> {
        if (track.name != trackName || pad.index < 0 || pad.index >= kMaxPads)
            return std::nullopt;
        return roles[static_cast<std::size_t> (pad.index)];
    };

    workers.addJob ([weak = selfRef, project, diagnostics, destination, onFinished, options] {
        bool ok = false;
        juce::String message;
        juce::File folder;
        try
        {
            const auto result = padporter::exportProject (*project, toStdPath (destination), options, diagnostics.get());
            ok = true;
            folder = toJuceFile (result.outputDir);
            message << "Exportiert nach " << folder.getFullPathName() << "\n\n"
                    << result.samplesCopied << " Samples kopiert"
                    << (result.samplesMissing > 0 ? " (" + juce::String (result.samplesMissing) + " fehlen)" : juce::String())
                    << "\n" << result.midiFiles << " MIDI-Dateien (je Sequence: Alles, Drums, Melodie)\nmapping.json + export-log.txt";
        }
        catch (const std::exception& e)
        {
            message = fromUtf8 (e.what());
        }

        juce::MessageManager::callAsync ([weak, onFinished, ok, message, folder] {
            if (weak.get() != nullptr)
                onFinished (ok, message, folder);
        });
    });
}

// ---------------------------------------------------------------------------
// Sequence parts and preview

RoleMap PadPorterProcessor::getRoles() const
{
    if (loaded == nullptr)
        return {};
    std::lock_guard<std::mutex> lock (stateMutex);
    return effectiveRoles (*loaded, overrides);
}

PartRole PadPorterProcessor::getPadRole (int padIndex) const
{
    if (padIndex < 0 || padIndex >= kMaxPads)
        return PartRole::Drums;
    return getRoles()[static_cast<std::size_t> (padIndex)];
}

bool PadPorterProcessor::isPadRoleAutomatic (int padIndex) const
{
    std::lock_guard<std::mutex> lock (stateMutex);
    const auto it = overrides.find (padIndex);
    return it == overrides.end() || ! it->second.role.has_value();
}

void PadPorterProcessor::setPadRole (int padIndex, std::optional<PartRole> role)
{
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        auto& ov = overrides[padIndex];
        ov.role = role;
        if (ov.isEmpty())
            overrides.erase (padIndex);
    }

    // A running preview of a part must pick up the new assignment.
    if (getPreviewSequence() >= 0)
        startPreview (previewSequence, previewPart);
    sendChangeMessage();
}

PartCounts PadPorterProcessor::getPartCounts (int sequenceIndex) const
{
    if (loaded == nullptr || loaded->track() == nullptr || sequenceIndex < 0
        || sequenceIndex >= static_cast<int> (loaded->project->sequences.size()))
        return {};

    const auto* clip = loaded->project->sequences[static_cast<std::size_t> (sequenceIndex)].clipForTrack (loaded->track()->name);
    return clip != nullptr ? countParts (*clip, *loaded->track(), getRoles()) : PartCounts {};
}

void PadPorterProcessor::setSelectedSequence (int index)
{
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        selectedSequence = index;
    }
    restretchForSelectedSequence();
    sendChangeMessage();
}

void PadPorterProcessor::restretchForSelectedSequence()
{
    // Warped pads are stretched to one tempo; reload them when the selected sequence differs.
    if (loaded == nullptr || loading || selectedSequence < 0 || selectedSequence >= static_cast<int> (loaded->project->sequences.size()))
        return;

    const auto bpm = loaded->project->tempoFor (loaded->project->sequences[static_cast<std::size_t> (selectedSequence)]);
    if (std::abs (bpm - loaded->targetBpm) < 0.01)
        return;

    bool anyWarped = false;
    if (const auto* t = loaded->track())
        for (const auto& pad : t->pads)
            anyWarped = anyWarped || (pad.hasSample() && pad.warpEnable);
    if (! anyWarped)
        return;

    OverrideMap copy;
    {
        std::lock_guard<std::mutex> lock (stateMutex);
        copy = overrides;
    }
    startLoad (loaded->file, loaded->trackIndex, std::move (copy), bpm);
}

void PadPorterProcessor::startPreview (int sequenceIndex, Part part)
{
    if (loaded == nullptr || loaded->track() == nullptr || sequenceIndex < 0
        || sequenceIndex >= static_cast<int> (loaded->project->sequences.size()))
        return;

    const auto& project = *loaded->project;
    const auto& seq = project.sequences[static_cast<std::size_t> (sequenceIndex)];
    const auto* clip = seq.clipForTrack (loaded->track()->name);
    if (clip == nullptr)
        return;

    auto pattern = std::make_unique<SamplerEngine::Pattern>();
    pattern->bpm = project.tempoFor (seq);
    pattern->lengthTicks = juce::jmax<std::int64_t> (96, seq.lengthTicks());
    pattern->id = ++previewId;

    const auto filtered = filterClip (*clip, *loaded->track(), getRoles(), part);
    for (const auto& n : filtered.notes)
    {
        if (n.tick >= pattern->lengthTicks)
            continue;
        const auto off = juce::jmin (pattern->lengthTicks - 1, n.tick + juce::jmax<std::int64_t> (1, n.length));
        pattern->events.push_back ({ n.tick, n.note, static_cast<float> (n.velocity), true, static_cast<float> (n.tuneSemitones()) });
        pattern->events.push_back ({ off, n.note, 0.0f, false, 0.0f });
    }
    std::stable_sort (pattern->events.begin(), pattern->events.end(), [] (const auto& a, const auto& b) {
        return a.tick != b.tick ? a.tick < b.tick : (! a.noteOn && b.noteOn);
    });

    previewSequence = sequenceIndex;
    previewPart = part;
    engine.setPattern (std::move (pattern));
    sendChangeMessage();
}

void PadPorterProcessor::stopPreview()
{
    previewSequence = -1;
    engine.setPattern (nullptr);
    sendChangeMessage();
}

int PadPorterProcessor::getPreviewSequence() const noexcept
{
    return previewSequence >= 0 && engine.getPlayingPatternId() == previewId ? previewSequence : -1;
}

juce::AudioProcessorEditor* PadPorterProcessor::createEditor()
{
    return new PadPorterEditor (*this);
}
} // namespace padporter::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new padporter::plugin::PadPorterProcessor();
}
