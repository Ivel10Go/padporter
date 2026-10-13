# Changelog

## 1.5.0 – 2026-10-13

- **16 Levels (Pitch) wird abgespielt:** Mit 16 Levels aufgenommene Noten tragen am Gerät pro Note einen Tuning-Wert (Halbtöne = (Wert − 0,5) × 240). PadPorter hat sie bisher alle in Originaltonhöhe gespielt. Jetzt erklingen sie in der aufgenommenen Tonhöhe, beim Vorhören, beim Audio-Drag und bei den Pad-Stems. Geprüft an echten Projekten: Bassmelodie 0/−1/−2 und Akkordtöne 0/+4/+7/−1.
- **MIDI:** Pads mit 16-Levels-Noten werden beim Ziehen der Pad-Spur bzw. im Export als **chromatische Melodie** geschrieben (C3 = Originaltonhöhe, Datei „… (16 Levels).mid“). Damit lassen sie sich in Ableton z. B. mit Simpler und demselben Sample nachspielen.
- **Pattern-Ansicht:** 16-Levels-Spuren sind mit „(16 Lv)“ markiert; jede Note zeigt ihre Tonhöhe (+4, −1 …).

## 1.4.0 – 2026-10-13

Klang näher am Gerät (gilt für Wiedergabe, Vorhören und Audio-Drag):

- **Warp:** Pads mit aktiviertem Warp werden wie am Gerät auf das Sequence-Tempo gestreckt (WSOLA-Time-Stretch). Die Tonhöhe bleibt erhalten, Tune verändert nur die Tonhöhe. Wechselt man auf eine Sequence mit anderem Tempo, werden die Loops neu gestreckt.
- **Lautstärke-Kette:** Pad × Programm-Fader × Track-Fader × Ausgangs-Fader, wie am Gerät.
- **Master-Kompressor:** Der „Color Compressor“ auf dem Hauptausgang wird angenähert, wenn er im Projekt aktiv ist. Er lässt sich über **MASTER-KOMP** umschalten. Seine genauen Geräteeinstellungen sind nicht auslesbar, deshalb kommen feste Glue-Werte zum Einsatz.
- **Instrument-Tuning** wird zum Layer-Tuning addiert.
- **Velocity:** linear wie am Gerät (vorher stärker gekrümmt).
- **Interpolation:** 4-Punkt-Hermite statt linear, sauberer beim Umstimmen.
- **CLI:** `info` zeigt Ausgangs-Lautstärke und Kompressor-Status.

## 1.3.0 – 2026-10-13

- **Audio-Drag nach Ableton:** Ganze Sequences, nur Drums, nur Melodie oder **jedes einzelne Pad** einer Sequence lassen sich als Audiodatei in Ableton ziehen.
  - Umschalter **ZIEHEN ALS MIDI | AUDIO** oben rechts im Display; Standard ist AUDIO.
  - Einzelne Pads zieht man an ihrem Namen in der Pattern-Ansicht.
- **Offline-Renderer:**
  - Er nutzt dieselbe Sampler-Engine wie das Plugin.
  - Der Clip ist exakt einen Loop lang und loopt nahtlos: Ausklänge vom Loop-Ende laufen in den Anfang.
  - Nicht gewählte Pads spielen unhörbar mit. Dadurch greifen Mute Groups wie im Mix, und die Pad-Stems ergeben zusammen exakt den Gesamt-Mix (per Test geprüft).
- **WAV-Format:** 32-bit Float mit ACID-Tempoinfo, abgelegt in `Dokumente\PadPorter\Renders\<Projekt>\`. Vorhandene Dateien werden nie überschrieben, sondern bekommen eine Variante wie „(2)“. So ändern sich Clips, die schon in Ableton liegen, nie nachträglich.

## 1.2.0 – 2026-10-13

- **Sequences in Drums und Melodie getrennt:** Jedes Pad wird automatisch als Drum oder Melodie erkannt. Kriterien sind der Sample-Name (Kick/Snare/Hat … bzw. Bass/Lead/Piano/Keys/Vox, Tonart wie „Am“), die Länge, das Loop-Tempo und chromatisches Spiel. Pro Pad lässt sich das umstellen (AUTO / DRUMS / MELODIE). Noten auf leeren Pads erscheinen getrennt als „ohne Pad“.
- **Neue Oberfläche, auf die Sequences fokussiert:**
  - Großes Display mit Sequence-Liste und Pattern-Ansicht, Spuren gruppiert nach DRUMS (blau), MELODIE (orange) und OHNE PAD.
  - Drei Chips **DRUMS / MELODIE / ALLES**: ziehen = MIDI-Clip in Ableton, ▶ = in PadPorter anhören (loopt, mit Playhead).
  - Pads leuchten in ihrer Part-Farbe. Projekte vom Gerät erscheinen als Overlay.
  - Weniger Bedienelemente: Die Knopf-, Fader- und Jog-Bedienung ist entfallen.
- **Export:** pro Sequence zusätzlich `NN - Name - Drums.mid` und `NN - Name - Melodie.mid`; `mapping.json` enthält die Rolle jedes Pads.
- **CLI:** `info` zeigt die Rolle jedes Pads und die Aufteilung jeder Sequence.

## 1.1.0 – 2026-10-13

- **Neues Design** im Stil der Hardware:
  - helles Gehäuse mit schwarzem Display-Panel und Farbdisplay (gelbe Wellenform)
  - 4×4-Pads mit blauem Leuchten, K1–K3 (Tune/Level/Pan), Velocity-Fader und Data-Rad mit −/+
  - blaue Funktionstasten und grüne PLAY-Taste
  - Rechts ein Browser mit den Tabs Gerät, Bibliothek und Sequences
- **Geräteerkennung:** Wechseldatenträger werden alle 2 s geprüft. Ein Gerät im SD-Card-Access-Modus (`<Laufwerk>\MPC-Sample\Projects`) wird automatisch erkannt, seine Projekte werden aufgelistet.
- **Import mit Samples:** Ein Klick kopiert das Projekt mit dem kompletten `_[ProjectData]`-Ordner nach `Dokumente\PadPorter\Projekte\<Name>\` und öffnet es. Samples, die im Projektordner fehlen, werden in der Geräte-Bibliothek `MPC-Sample\Samples` gesucht und mitkopiert. Die Karte wird nur gelesen. Ein erneuter Import ersetzt die alte Kopie atomar.
- **Format an echten Geräteprojekten verifiziert (Firmware 1.3.0.12):**
  - Der Header steckt im gzip-Stream.
  - Programmtypen: 0 = Drum, 8 = Submix, 9 = Output.
  - Gerät-Automation (`invented`) wird ignoriert.
  - Die exakte Länge kommt aus `lengthPulses`; die Sequence-Nummer entspricht dem Gerät.
- CLI: neue Befehle `scan` und `import`.
- Oberfläche jetzt auf Deutsch.

## 1.0.0 – 2026-10-13

Erste vollständige Version.

- **Core-Library:** liest `.xpj` (ACVS-Header + gzip-JSON bzw. unkomprimiertes JSON), alle Tracks, Pads (128), Layer und Sequences inklusive aller Clips und echter Taktart. Unbekannte Strukturen werden als Diagnosen gemeldet, nicht still verschluckt.
- **Samples:** Auflösung im `_[ProjectData]`-Ordner (Groß-/Kleinschreibung egal, Unicode-Namen), WAV-Header und `atem`-Chop-Slices.
- **MIDI-Export:** Standard-MIDI-Datei Typ 1 mit 960 PPQ, Tempo und Taktart; überlappende Noten werden gekürzt.
- **Export:** Samples, MIDI, `mapping.json` und `export-log.txt`, mit Read-only-Garantie für die Originale.
- **CLI:** `padporter-cli` mit den Befehlen `info`, `header`, `dump-json` und `export`.
- **Standalone-App und VST3:**
  - 4×4-Pad-Grid mit 8 Banks, Prelistening und Wellenform mit Slice-Markern
  - Pad-Editor (Tune, Level, Pan, One-Shot/Gate, Mute Group, Ausgang)
  - Sequence-Drag-&-Drop in die DAW, Export-Button
- **Sampler:** Velocity, Pitch, One-Shot/Gate, Mute Groups, mono Pads, Loops im Gate-Modus. Ausgabe wahlweise als Stereo-Mix oder Multi-Out (16 × Stereo). Keine Allokationen und keine Locks im Audio-Thread (per Test geprüft).
- **State:** Projektpfad, Track, Bank, Pad-Änderungen und Parameter werden im Live-Set gespeichert.
- **Installer:** Inno-Setup-Skript für App, VST3 und CLI.
