# Format-Notizen: `.xpj`-Projekte (Akai MPC Sample / MPC-Firmware 3.x)

> **Update 2026-10-13: an 4 echten Projekten einer MPC Sample verifiziert (Header-Version 1.3.0.12).**
> - Die **ganze Datei ist ein gzip-Stream**; die 5 Header-Zeilen stehen *im* entpackten Text vor dem JSON.
> - SD-Karte: `<Laufwerk>\MPC-Sample\Projects\<Name>.xpj` + `<Name>_[ProjectData]\`, Bibliothek unter `MPC-Sample\Samples\`.
> - `program.type`: 0 = Drum, 8 = Submix-Bus, 9 = Output-Bus (jedes Projekt hat "Submix 1", "Out 1/2", "Out 3/4").
> - Event `type 1` mit `"invented": true` = vom Gerät erzeugte Start-Automation (z. B. `parameter 131`), wird ignoriert.
> - `sequences[].value.lengthPulses` gibt die exakte Länge an; `key + 1` ist die Sequence-Nummer am Gerät.
> - Sequence-Namen können mehrfach vorkommen (kopierte Sequences behalten den Namen).
> - **16 Levels (Pitch):** Die Note bleibt auf der Note des Quell-Pads; `modifierActiveState0 = true`, `modifierValue0` = Tonhöhe relativ, **Halbtöne = (Wert − 0,5) × 240** (ganzzahlig in allen Testdaten, Formel aus MPC2Live, bestätigt).
> - **Warp:** `warpEnable`, `tempo` (Sample-BPM); das Gerät streckt auf das Sequence-Tempo. `bpmLock` / `stretchPercentage` (z. B. 197) sind noch ungeklärt.
> - **Ausgang:** `mixer.outputs[0].mixable.volume` und `inserts.effects[]` – in allen Projekten: „Pad-Controlled FXs“, „Flex Beat Player“, „Color Compressor“ (aktiv). Programm-Fader: `program.mixable.volume`.
> - Noch ungeklärt: Noten auf leeren Pads (projectcd, Bank B, Noten 53–66, teils mit Slice-Modifier 15).
> - Alle referenzierten Samples lagen im `_[ProjectData]`-Ordner (`loadImpl: 0`). Samples, WAV 24/32 bit, 44,1 kHz, stereo.

> **Status:** Phase 0, Stand 2026-10-13.
> Alles hier stammt aus der Analyse des Quellcodes von
> [Duffman007/MPC2Live](https://github.com/Duffman007/MPC2Live) (MIT, Beta 0.10).
> Gegen echte Projekte aus `./testprojekte/` ist noch **nichts verifiziert** (der Ordner fehlt bisher).
> Markierungen: ✅ verifiziert · ⚠️ nur aus Referenzcode abgeleitet · ❓ offen

---

## 1. Projektstruktur auf Datenträger ⚠️

```
<Name>.xpj                     ← Projektdatei
<Name>_[ProjectData]/          ← Samples (WAV), flach, ohne Unterordner (?)
    Kick 1.wav
    Kick 1001.wav              ← Duplikat: MPC hängt 001/002/… an, wenn ein Sample auf mehreren Pads liegt
    ...
```

- Der Ordnername wird aus dem Dateinamen der `.xpj` gebildet: `"<stem>_[ProjectData]"`.
- `layer.sampleFile` in der JSON enthält nur den **Dateinamen**, der relativ zu diesem Ordner aufgelöst wird.
- Dateien mit Suffix `001`, `002`, … können **unterschiedliche `atem`-Chunks** (Chop-Positionen) bei gleichem Audio haben. Sie dürfen also nicht blind dedupliziert werden.
- ❓ Gibt es Unterordner, andere Formate (AIFF, FLAC) oder absolute Pfade bei `loadImpl = 1`?

## 2. Container-Format der `.xpj` ⚠️

**Kein XML.** Die Datei besteht aus einem ASCII-Header und einem gzip-Stream mit JSON:

| Zeile | Inhalt (Beispiel)            | Bedeutung                     |
|-------|------------------------------|-------------------------------|
| 1     | `ACVS`                       | Magic                         |
| 2     | `1.2.1.2`                    | Format-/Firmware-Version      |
| 3     | `SerialisableProjectData`    | Typ                           |
| 4     | `json`                       | Payload-Encoding              |
| 5     | `Linux`                      | Plattform der Erzeugung       |
| Rest  | `1F 8B …`                    | gzip → UTF-8-JSON             |

Robustes Lesen (wie MPC2Live):
1. In den ersten 512 Bytes nach der gzip-Magic `0x1F 0x8B` suchen.
2. Wird sie nicht gefunden, die Datei als unkomprimiertes JSON behandeln (Fallback).
3. Nach dem Entpacken zum ersten `{` springen, denn Header-Reste sind möglich.
4. Die Wurzel hat die Form `{ "data": { … } }`.

❓ Welche Versionsstrings kommen in deinen Projekten vor? Schreiben andere Plattformen (MPC Software Win/Mac) andere Zeilen 4/5 oder ein anderes Encoding?

## 3. JSON-Struktur (relevanter Ausschnitt) ⚠️

```
data
├── masterTempo            : double   (BPM)
├── masterTempoEnabled     : bool     (true = globales Tempo gilt für alle Sequences)
├── tracks[]                          (Tracks / Programs)
│   ├── name               : string   ("Drum 001")
│   ├── volume, pan        : double   (0..1, pan 0.5 = Mitte)
│   ├── samples[]                     (Sample-Pool des Tracks)
│   │   ├── path           : string
│   │   ├── loadImpl       : int      (0 = im Projekt eingebettet, 1 = aus Geräte-Library)
│   │   └── metadata.rootNote : int
│   └── program
│       ├── padNoteMap.noteForPad : { "value0": 36, "value1": 37, … }
│       └── drum.instruments[]        (Index = Pad-Index 0..127 → 8 Banks A–H à 16 Pads)
│           ├── triggerMode    : int  (1 = One-Shot, 2 = Gate/Note-On, 0 = ?)
│           ├── whichMuteGroup : int  (0 = keine, 1..32)
│           ├── monophonic     : bool
│           ├── warpEnable, tempo
│           ├── mixable { volume, pan, mute }   (Pad-Strip; volume Default 0.70794 ≈ -3 dB)
│           ├── chopProperties { chopRegions, chopMode (0 manual, 1 threshold, 2 regions, 3 beatgrid) }
│           ├── synthSection
│           │   ├── ampEnvelope { Attack.value0, Decay.value0, Release.value0, OneShot.value0 }
│           │   └── filterData.value0 { filterType, filterCutoff (0..1), filterResonance (0..1) }
│           └── layersv[]            (Sample-Layer; MPC2Live nutzt nur den ersten)
│               ├── sampleFile, sampleName
│               ├── coarseTune (±24 st), fineTune (±90 ct?)
│               ├── volume.gainCoefficient, pan
│               ├── direction  (1 = reverse)
│               ├── loop, offset
│               └── sliceInfo { Start, End, LoopStart }   (Sample-Frames)
├── sequences[]                       (Liste von { key: int, value: {…} }, nach key sortieren)
│   └── value
│       ├── name, bpm, lengthBars
│       ├── timeSignatureTrack.timeSignatures[] { beatsPerBar, beatLength (= PPQ, typ. 960) }
│       └── trackClipMaps : [[ { key: <Trackname>, value: { eventList: { events[] } } } ]]
└── mixer.outputs[0].mixable.inserts.effects[]   (Master-Inserts, Plugin-State als Blob)
```

### Events (`eventList.events[]`)

| `type` | Bedeutung              | Felder                                                                 |
|--------|------------------------|------------------------------------------------------------------------|
| 3      | Note                   | `time` (Pulses), `muted`, `note { note, velocity (0..1), length (Pulses), modifierActiveStateN, modifierValueN }` |
| 2      | Pad-Automation         | `time`, `automation { note, parameter (z. B. 518 = Pad-Volume), value 0..1 }` |
| andere | ❓ unbekannt           | Phase 1: zählen, loggen, ignorieren                                    |

Note-Modifier (pro Event):
- Slot 0 = *16 Levels Tune* (Tonhöhe)
- Slot 2 = *16 Levels Filter* (Cutoff, absolut 0..1)
- Slot 15 = *Chop-Slice-Index* (`round(value * 127)`)

### Timing → MIDI
- PPQ = `beatLength` (typ. 960). Sequence-Länge = `lengthBars × beatsPerBar × PPQ`. Achtung: MPC2Live rechnet hart mit `× 4`.
- Velocity: `clamp(round(v × 127), 1, 127)`
- Tempo: Ist `masterTempoEnabled` gesetzt, gilt `masterTempo`, sonst `sequence.bpm`. Der Default ist 128 BPM.

## 4. WAV-Zusatzdaten: `atem`-Chunk ⚠️

Ein proprietärer RIFF-Chunk mit der ID `atem` enthält UTF-8-JSON (mit Null-Terminator):
```json
{ "value0": { "Num slices": 4, "Slice 0": { "Start": 0, "End": 11025 }, … } }
```
Er enthält die manuellen Chop-Positionen pro Datei. Andere Software ignoriert den Chunk, das Audio bleibt also normal lesbar.

## 5. Nicht unterstützt / bewusst ignoriert (Scope)

| Feature                                   | Status | Grund                                                  |
|-------------------------------------------|--------|--------------------------------------------------------|
| Insert-Effekte (Pad, Track, Master)       | ❌     | Plugin-State ist ein proprietärer Base64-Blob (`"119.<…>"`) |
| Filter / Hüllkurven                       | später | Werte lesbar, die DSP-Nachbildung ist optional          |
| Pad-Automation (`type 2`)                 | ❌ (v1) | Wird gelesen und gemeldet, aber nicht exportiert        |
| 16 Levels / Chop-Modifier                 | später | Werden gelesen; MIDI-Export zunächst nur als Rohnote    |
| Mehrere Layer pro Pad                     | ❓     | Format unterstützt `layersv[]`, Verhalten noch unklar   |
| Keygroup-/Plugin-/MIDI-Tracks             | ❓     | Phase 1 erkennt sie und meldet sie, sonst ignoriert     |
| Songs / Arrangement                       | ❓     | Struktur unbekannt                                     |
| Akai Force-Projekte                       | ❌     | Anderes Gerät mit eigenen Formatvarianten (vgl. FORCEABLE) |

## 6. Annahmen in der Implementierung (v1.0.0, noch unverifiziert)

| Thema | Annahme im Code | Wo |
|---|---|---|
| `beatLength` | Ticks pro Taktschlag. Die Taktlänge ergibt sich aus `beatsPerBar × beatLength`, der Nenner aus `4×960 / beatLength`. Die MIDI-Auflösung ist fest 960 PPQ | `XpjReader.cpp` |
| `triggerMode` | 1 = One-Shot; 0/2 = Gate, außer wenn `ampEnvelope.OneShot` gesetzt ist; fehlend = One-Shot; andere Werte erzeugen eine Warnung | `XpjReader.cpp` |
| `velocity` | 0..1; Werte > 1 werden als 0..127 gedeutet (mit Hinweis) | `XpjReader.cpp` |
| Release | `ampEnvelope.Release.value0 × 2 s`, mindestens 5 ms | `plugin/Source/SampleKit.cpp` |
| Pad-Lautstärke | `mixable.volume × layer.gainCoefficient` (linear); Track-Volume wird ignoriert | `SampleKit.cpp` |
| Pan | `mixable.pan + layer.pan − 0.5`, Balance-Gesetz (Mitte = 0 dB) | `SampleKit.cpp` |
| `trackClipMaps` | Alle Zeilen und Spalten werden gelesen. `key` ist der Trackname (String) oder ein Track-Index (Zahl) | `XpjReader.cpp` |
| Tempo | `masterTempoEnabled` → `masterTempo`; sonst `sequence.bpm`, ohne eigenes BPM gilt `masterTempo` | `Model.cpp` |

## 7. Offene Fragen an echte Testprojekte

1. Header-Zeilen und Versionsstrings in allen Projekten?
2. Gibt es mehr als einen Drum-Track? MPC2Live liest nur den ersten.
3. Hat `trackClipMaps` mehrere Zeilen oder Spalten? MPC2Live liest nur `[0][0]`.
4. Gibt es weitere Event-Typen außer 2 und 3?
5. Welche Werte nimmt `triggerMode` an (0/1/2)?
6. Kommen Pads mit mehr als einem Layer vor?
7. Gibt es Samples mit `loadImpl = 1` (Library) und fehlender Datei im `_[ProjectData]`?
8. Welche Sample-Formate, Samplerates und Bit-Tiefen kommen vor?
9. Wie werden Umlaute und Sonderzeichen in Datei- und Sample-Namen gespeichert?
