#pragma once

#include "Model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace padporter
{
struct MidiExportOptions
{
    int channel = 0;            // 0..15
    bool includeTempo = true;
    bool includeTimeSignature = true;
};

/**
    Builds a Standard MIDI File (format 1, 960 PPQ) for one sequence.
    Track 0 carries name, tempo and time signature; one track follows per clip.
    Muted notes are skipped; overlapping notes of the same pitch are truncated.
*/
std::vector<std::uint8_t> writeSequenceMidi (const Project& project,
                                             const Sequence& sequence,
                                             const std::vector<const Clip*>& clips,
                                             const MidiExportOptions& options = {});

/** Convenience: all clips of the sequence. */
std::vector<std::uint8_t> writeSequenceMidi (const Project& project,
                                             const Sequence& sequence,
                                             const MidiExportOptions& options = {});

/** File name like "01 - Sequence 01.mid" (sanitised). */
std::string midiFileName (int sequenceNumber, const std::string& sequenceName,
                          const std::string& suffix = {});
} // namespace padporter
