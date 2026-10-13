# PadPorter

PadPorter liest `.xpj`-Projekte (MPC-Sample-/MPC-3.x-Format) samt `_[ProjectData]`-Sampleordner. Es spielt die Pads als Drum-Sampler ab und bringt Sequences als MIDI in deine DAW.

PadPorter gibt es in drei Varianten:

| Variante | Wofür |
|---|---|
| **Standalone-App** | Projekt öffnen, Pads vorhören, Wellenform ansehen, alles exportieren |
| **VST3-Plugin** | Drum-Sampler in Ableton Live; Sequences per Drag & Drop als MIDI-Clips |
| **`padporter-cli`** | Projekte analysieren und exportieren auf der Kommandozeile |

> Originalprojekte werden **niemals verändert**. PadPorter öffnet sie nur lesend und verweigert Exporte in den Projekt- bzw. Sampleordner.

## Installation

`dist\PadPorter-<version>-Setup.exe` ausführen. Das Setup installiert:
- die App nach `C:\Program Files\PadPorter\`
- das Plugin nach `C:\Program Files\Common Files\VST3\PadPorter.vst3`

Wie du das Plugin in Ableton Live scannst und lädst, steht in **[docs/ableton-live.md](docs/ableton-live.md)**.

## Projekte direkt vom Gerät holen

1. Gerät per USB anschließen und dort den **SD-Card-Access-Modus** aktivieren.
2. PadPorter erkennt das Laufwerk innerhalb von etwa 2 Sekunden. Der Tab **GERÄT** zeigt dann alle Projekte aus `MPC-Sample\Projects`.
3. **Importieren** kopiert das Projekt mit allen Samples nach `Dokumente\PadPorter\Projekte\<Name>\` und öffnet es sofort. Samples aus der Geräte-Bibliothek (`MPC-Sample\Samples`) werden ebenfalls mitkopiert.
4. Importierte Projekte findest du unter **BIBLIOTHEK**. Sie funktionieren auch ohne angeschlossenes Gerät.

Die SD-Karte wird dabei nur gelesen. Vor dem Abziehen das Laufwerk in Windows auswerfen.

## Benutzung

Die Oberfläche ist dem Gerät nachgebaut: Jedes Bedienelement sitzt an derselben Stelle wie an der Hardware. Das Fenster lässt sich frei skalieren, die Proportionen bleiben erhalten.

1. **Projekt öffnen:** Softkey **Projekte** über dem Display drücken (oder eine `.xpj`-Datei ins Fenster ziehen). Dort stehen die Projekte auf dem Gerät (**IMPORT**) und in der Bibliothek (**ÖFFNEN**).
2. **SEQ** zeigt das Display mit drei Seiten, umgeschaltet über die drei Softkeys:
   - **Sequenzen:** Liste aller Sequences. Eine Zeile ziehst du direkt in eine Ableton-Spur. Doppelklick öffnet das Pattern.
   - **Pattern:** Chips **DRUMS / MELODIE / ALLES** zum Anhören (▶) und Ziehen; die Pad-Namen links ziehen einzelne Pads. **MIDI | AUDIO** wählt, was gezogen wird.
   - **Projekte:** Gerät und Bibliothek, dazu **ORDNER**, **EXPORT** und **INFO** (Hinweise zum Projekt).
3. **Pads anklicken** spielt das Sample ab und wählt das Pad aus. Weiter oben im Pad geklickt heißt höhere Velocity. **PAD BANK** schaltet die Banks A–H weiter; die aktive Bank leuchtet links neben dem Display.
4. **SAMPLE** zeigt das gewählte Pad mit Wellenform. Die Softkeys ordnen es **Auto / Drums / Melodie** zu, **K1 / K2 / K3** stellen Tune, Level und Pan ein (geänderte Werte erscheinen blau). Änderungen werden im Live-Set gespeichert, **ERASE** stellt die Projektwerte wieder her.
5. Weitere Tasten:
   - **Datenrad** sowie **– / +**: Sequence (SEQ) bzw. Pad (SAMPLE) wählen
   - **▶ / ■**: gewählte Sequence anhören bzw. stoppen
   - **PAD FX**: Master-Kompressor des Geräts nachbilden (nur wenn er im Projekt aktiv ist)
   - **MAIN VOLUME**: Lautstärke, **Fader**: Global Tune
   - **SHIFT** rastet ein: + PAD BANK = Bank zurück, + Pad 16 = Projekte, + Pad 12 = Sequenzen, + Pad 5 = Kompressor

   Tasten ohne Gegenstück in PadPorter (z. B. CHOP, TAP TEMPO, RECORD) sind nur abgebildet.
6. **EXPORT** legt im gewählten Ordner einen Unterordner `<Projektname>` an mit:
   - `Samples/` (Kopien)
   - `MIDI/NN - <Sequence>.mid` (Typ 1, 960 PPQ, Tempo und Taktart)
   - `mapping.json` (Pad → Note → Sample → Parameter)
   - `export-log.txt`

### Kommandozeile
```
padporter-cli info "D:\Beats\Mein Beat.xpj"     (oder ein Ordner: analysiert alle .xpj darin)
padporter-cli export "D:\Beats\Mein Beat.xpj" "D:\Export"
padporter-cli dump-json "D:\Beats\Mein Beat.xpj" roh.json
```

## Bekannte Einschränkungen

- Das Format ist undokumentiert und per Reverse Engineering erschlossen. Bisher ist es **nur mit synthetischen Testprojekten** verifiziert (siehe [docs/format-notes.md](docs/format-notes.md)). Unbekannte Strukturen zeigt PadPorter als Hinweise an („Details“ bzw. `export-log.txt`).
- Nur **Drum-Programme**: Keygroup-, Plugin- und MIDI-Tracks werden erkannt, aber ignoriert.
- Pro Pad wird nur der **erste Layer** abgespielt. Im Export und in `mapping.json` stehen alle Layer.
- Folgendes wird **nicht übertragen**: Effekte, Filter, Hüllkurven (außer einer angenäherten Release-Zeit), Pad-Automation, 16-Levels- und Chop-Modifier, Warp und Time-Stretch.
- Im Plugin wird nur die **erste Taktart** einer Sequence verwendet.
- Die Samples werden **nicht ins Live-Set eingebettet**. Das Plugin merkt sich den Projektpfad. Verschiebst du das Projekt, öffne es einfach neu. Ableton „Collect All and Save“ sammelt diese Samples nicht ein, dafür ist der Export da.
- Ableton Live **Lite** erlaubt 16 Spuren. Multi-Out braucht pro Ausgang eine eigene Audiospur.

## Selbst bauen

Voraussetzung ist Visual Studio 2022 oder neuer mit „Desktopentwicklung mit C++“ (siehe [docs/setup-windows.md](docs/setup-windows.md)).

```
build.bat Release                    (Core, CLI, Tests, App, VST3; lädt JUCE beim ersten Mal)
build.bat Release core               (nur Core + CLI + Tests, ohne JUCE)
installer\build-installer.bat        (braucht Inno Setup 6)
```

Echte Projekte für die Tests gehören nach `testprojekte/`. Der Ordner ist per `.gitignore` von Git ausgenommen.

**Version:** Die Versionsnummer steht nur an einer Stelle, in `project(PadPorter VERSION x.y.z)` in `CMakeLists.txt`. App, Plugin, CLI und Installer übernehmen sie von dort.

## Lizenz und Marken

Drittanbieter-Komponenten sind in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) aufgeführt. **Wichtig:** JUCE steht unter AGPLv3 oder einer kommerziellen Lizenz. Wer Binaries weitergibt, muss entweder den eigenen Quellcode unter AGPLv3 veröffentlichen oder eine JUCE-Lizenz haben.

PadPorter steht in keiner Verbindung zu inMusic/Akai Professional oder Ableton. „MPC“ und „Akai“ sind Marken von inMusic Brands, „Ableton Live“ ist eine Marke der Ableton AG und „VST“ eine Marke der Steinberg Media Technologies GmbH.
