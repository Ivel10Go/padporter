# Projektplan (Phase 0)

## 1. Referenz-Analyse

### MPC2Live (Duffman007) – MIT, Swift/AppKit, macOS, Beta 0.10
**Was es kann:** Es konvertiert `.xpj` (MPC Sample) in ein Ableton-`.als`. Pads werden zum Drum Rack mit Simpler, Sequences zu Session-Clips. Es kopiert Samples, liest Chop-Slices aus dem WAV-`atem`-Chunk und übersetzt Mute Groups (Choke), Tune, Reverse, Hüllkurven, Filter und 16-Levels-Modi in Simpler-Parameter.

**Wie es parst:** Es sucht nach der gzip-Magic, entpackt, parst die JSON mit `JSONSerialization` und greift über String-Keys auf ein ungetyptes Dictionary zu. Fehlende Felder bekommen still einen Default.

**Was fehlt / Schwächen:**
- Nur macOS, kein Windows, kein Plugin, kein Prelistening, kein Standard-MIDI-Export.
- Nur **der erste** Drum-Track, nur `trackClipMaps[0][0]`, nur der erste Layer.
- Taktlänge ist hart mit `× 4` codiert (falsch bei Nicht-4/4).
- Keine Warnungen bei unbekannten Strukturen: Fehlende Felder fallen still auf Defaults zurück.
- Keine Tests.

### FORCEABLE (marcelkar78-rgb)
- Das Repo enthält **nur eine README**. Das Programm ist ein **closed-source `Forceable.exe`** im Release.
- Zielgerät ist die **Akai Force**, nicht die MPC Sample. Die Force nutzt auch `.xpj`, aber mit eigener Struktur (Clip-Matrix, Song-/Arrangement-Ansicht, alte und neue Formatvarianten).
- **Für uns nur als Hinweis nutzbar:** Es gibt mehrere Formatgenerationen, und Arrangement-Daten existieren. Code kann man nicht übernehmen. Die `.exe` habe ich bewusst **nicht** heruntergeladen oder ausgeführt.

## 2. Fork oder Eigenbau? → **Eigenbau, MPC2Live als Format-Referenz**

Ehrliche Begründung:
- Ein Fork bringt **null wiederverwendbaren Code**: Swift/AppKit läuft nicht in einem JUCE/C++-VST3 unter Windows. „Forken“ hieße faktisch, alles neu zu schreiben.
- Wertvoll ist das **Formatwissen** (Feldnamen, Semantik, Fallstricke wie `001`-Duplikate und `atem`). Das übernehmen wir dokumentiert. Die MIT-Lizenz erlaubt es, mit Copyright-Hinweis in `THIRD_PARTY_NOTICES.md`.
- Das Ziel ist ohnehin ein anderes: MPC2Live erzeugt `.als`, wir bauen Sampler + MIDI-Export + Plugin.
- **Option für später:** Ein `.als`-Export ließe sich nachrüsten, mit MPC2Live als Vorlage für die Live-XML.

## 3. Architektur

```
padporter/                     (Arbeitsname, siehe Abschnitt 5)
├── CMakeLists.txt
├── core/                      Core-Library, OHNE JUCE
│   ├── include/padporter/     Model.h, XpjReader.h, SampleResolver.h, WavInfo.h, MidiWriter.h, Diagnostics.h
│   └── src/
├── cli/                       padporter-cli: info | dump-json | export
├── tests/                     doctest; Tests laufen gegen ./testprojekte/ (falls vorhanden) + synthetische Fixtures
├── app/                       JUCE-Standalone (Phase 2)
├── plugin/                    JUCE-VST3 (Phase 3)
├── installer/                 Inno-Setup-Skript (Phase 4)
└── docs/
```

**Abhängigkeiten (alle per FetchContent, nichts eingecheckt):**

| Lib                | Zweck                          | Lizenz      |
|--------------------|--------------------------------|-------------|
| nlohmann/json      | JSON-Parsing im Core           | MIT         |
| miniz              | gzip-Dekompression im Core     | MIT         |
| doctest            | Unit-Tests                     | MIT         |
| JUCE 8             | App + Plugin                   | AGPLv3 / kommerziell* |

\* JUCE 8: Für ein privates bzw. Open-Source-Projekt passt AGPLv3. Bei einem kommerziellen Vertrieb brauchst du eine JUCE-Lizenz (es gibt einen kostenlosen Tier bis zu einer Umsatzgrenze). **Bitte entscheiden, bevor Phase 4 beginnt.**

**Warum der Core ohne JUCE?** Core und Tests bauen in Sekunden statt Minuten, der Code bleibt unabhängig von JUCE-Lizenz und -Version testbar, und die Grenze bleibt sauber: Der Core liefert nur Daten. Audio-Dekodierung (WAV/AIFF → Float) macht erst die App bzw. das Plugin über `juce::AudioFormatManager`.

**Designprinzipien Core:**
- **Read-only:** Quelldateien werden nur über `std::ifstream` im Binärmodus gelesen. Exporte landen in ein separates Zielverzeichnis. Liegt das Ziel innerhalb des Quellprojekts oder soll eine Quelldatei überschrieben werden, wird abgebrochen. Ein Test prüft Hash und mtime der Originale vor und nach dem Export.
- **Tolerant mit Diagnose:** Unbekannte Event-Typen, Track-Typen und fehlende Felder werden nicht still verschluckt, sondern als `Diagnostic { severity, jsonPath, message }` gesammelt. Die CLI zeigt sie an. Harte Fehler (keine gzip/JSON, kein `data`) werden als Exception gemeldet.
- **Vollständiger als MPC2Live:** alle Tracks, alle Clips, alle Layer und echte Taktart.

## 4. Phasen in kleinen Schritten

**Phase 1 – Core + CLI** (jeweils mit Test gegen deine Projekte)
1. CMake-Skelett, doctest, `padporter-cli dump-json <xpj>`: Header anzeigen und JSON entpackt und formatiert ausgeben. → Damit verifizieren wir sofort `format-notes.md` an deinen echten Daten.
2. Datenmodell + Parser (Projekt, Tracks, Pads, Layer, Sequences, Events) mit Diagnostics. CLI `info`.
3. Sample-Auflösung + WAV-Header + `atem`-Chunk. Fehlende Samples melden.
4. Standard-MIDI-Datei (Typ 1, PPQ aus dem Projekt, Tempo- und Taktart-Meta-Event) pro Sequence.
5. `export`: Samples kopieren + `.mid` pro Sequence + `mapping.json` (Pad → Note → Sample → Parameter).

**Phase 2 – Standalone-App:** Projekt öffnen, 16-Pad-Grid mit Bank-Umschalter A–H, Prelisten, Wellenform, Export-Button.

**Phase 3 – VST3:** Sampler mit 16 Voices pro Pad-Set, Velocity, Pitch, One-Shot/Gate und Mute Groups. Alle Buffer werden in `prepareToPlay` bzw. beim Projektladen (Message-Thread) vorallokiert und per atomarem Pointer-Swap ins Audio-Thread übergeben, ohne Locks und ohne Allokation in `processBlock`. Dazu kommen Multi-Out (16 Stereo) bzw. ein Summenbus, MIDI-Drag-and-Drop nach Live (temporäre `.mid` + `performExternalDragDropOfFiles`) und State im Live-Set (Projektpfad + Overrides, Samples werden beim Laden neu gelesen).

**Phase 4 – Packaging:** Inno-Setup-Installer (VST3 nach `C:\Program Files\Common Files\VST3`, App nach `Program Files`), Versionierung über CMake `project(VERSION)`, README.

## 5. Neutrale Produktnamen (ohne „MPC“/„Akai“)
1. **PadPorter** ← mein Favorit (beschreibt genau den Zweck)
2. Crate Unpacker
3. SampleFolio
4. PadBridge

Nur `.xpj` als Dateiendung in der Doku zu nennen, ist unkritisch. Gemeint ist „liest `.xpj`-Projekte“, nicht „für MPC“.

## 6. Risiken & Hinweise
- **Ableton Live Lite** hat ein Limit von 16 Tracks. 16 Stereo-Ausgänge würden alle Tracks verbrauchen. Deshalb ist der **Summenbus der Default**, Multi-Out optional.
- Das Format ist undokumentiert und kann sich mit Firmware-Updates ändern. Deshalb gibt es Versions-Logging und Diagnostics.
- Ich kann Live nicht selbst bedienen. Plugin-Tests (Scan, Laden, Drag & Drop) liegen bei dir.
