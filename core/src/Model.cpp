#include "padporter/Model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace padporter
{
std::string Pad::label() const
{
    char buffer[8];
    std::snprintf (buffer, sizeof (buffer), "%c%02d", static_cast<char> ('A' + std::clamp (bank(), 0, 25)), padInBank() + 1);
    return buffer;
}

int Track::loadedPadCount() const noexcept
{
    return static_cast<int> (std::count_if (pads.begin(), pads.end(), [] (const Pad& p) { return p.hasSample(); }));
}

const Pad* Track::padForNote (int note) const noexcept
{
    for (const auto& p : pads)
        if (p.midiNote == note && p.hasSample())
            return &p;
    return nullptr;
}

int NoteEvent::velocity127() const noexcept
{
    const auto v = static_cast<int> (std::lround (velocity * 127.0));
    return std::clamp (v, 1, 127);
}

double NoteEvent::tuneSemitones() const noexcept
{
    const auto it = modifiers.find (0);
    return it != modifiers.end() ? (it->second - 0.5) * 240.0 : 0.0;
}

int Clip::playableNoteCount() const noexcept
{
    return static_cast<int> (std::count_if (notes.begin(), notes.end(), [] (const NoteEvent& n) { return ! n.muted; }));
}

std::int64_t Sequence::lengthTicks() const noexcept
{
    if (lengthPulses > 0)
        return lengthPulses;
    return static_cast<std::int64_t> (std::max (1, lengthBars)) * timeSignature.ticksPerBar();
}

const Clip* Sequence::clipForTrack (const std::string& trackName) const noexcept
{
    for (const auto& c : clips)
        if (c.trackName == trackName)
            return &c;
    return nullptr;
}

double Project::tempoFor (const Sequence& s) const noexcept
{
    if (masterTempoEnabled || ! s.hasOwnBpm)
        return masterTempo;
    return s.bpm;
}

const Track* Project::findTrack (const std::string& trackName) const noexcept
{
    for (const auto& t : tracks)
        if (t.name == trackName)
            return &t;
    return nullptr;
}

int Project::defaultDrumTrack() const noexcept
{
    int firstDrum = -1;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i].kind != TrackKind::Drum)
            continue;
        if (firstDrum < 0)
            firstDrum = static_cast<int> (i);
        if (tracks[i].loadedPadCount() > 0)
            return static_cast<int> (i);
    }
    return firstDrum;
}
} // namespace padporter
