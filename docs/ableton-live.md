# PadPorter in Ableton Live 12 Lite (Windows)

## 1. Plugin installieren

**Variante A (empfohlen):** `PadPorter-<version>-Setup.exe` ausführen und die Komponente „VST3 plugin“ auswählen.

**Variante B (manuell, nach eigenem Build):** Den Ordner
`build\Release\plugin\PadPorter_artefacts\Release\VST3\PadPorter.vst3`
nach `C:\Program Files\Common Files\VST3\` kopieren. Dafür sind Administratorrechte nötig.

Live sucht VST3-Plugins unter Windows nur in diesem Systemordner.

## 2. In Live scannen

1. Live starten → **Options → Preferences** (`Strg + ,`) → Reiter **Plug-Ins**.
2. **„Use VST3 Plug-In System Folders“** auf **On** stellen.
3. Auf **Rescan** klicken. Wird PadPorter nicht gefunden, mit gedrückter **Alt**-Taste auf **Rescan** klicken; das erzwingt einen vollständigen Neuscan.
4. Preferences schließen.

## 3. Plugin laden

1. Im Browser links auf **Plug-Ins** gehen. Unter **PadPorter** steht der Eintrag **PadPorter**.
2. Ihn auf eine **MIDI-Spur** ziehen oder doppelklicken.
3. Das Plugin-Fenster öffnet sich. Falls nicht: Im Device-Titel auf das **Schraubenschlüssel-/Fenster-Symbol** klicken.
4. Softkey **Projekte** über dem Display drücken (oder die `.xpj`-Datei ins Fenster ziehen) und das Projekt öffnen.

Jetzt spielen MIDI-Noten der Spur die Pads. Die Noten kommen aus dem Projekt; Standard ist Pad A01 = C#1 (37) bzw. C1 (36), je nach Projekt. Die Note jedes Pads siehst du im Inspector.

## 4. Sequences als MIDI-Clips

1. In der Liste **SEQUENCES** eine Zeile greifen.
2. Die Zeile in einen **leeren Clip-Slot** der PadPorter-Spur (Session View) oder in die Arrangement-Spur ziehen.
3. Live legt einen MIDI-Clip an. Er enthält die Noten der Sequence für den gewählten Drum-Track.

**Hinweis:** Live übernimmt beim Import nicht das Tempo aus der MIDI-Datei. Das Projekttempo steht in der Kopfzeile von PadPorter; stelle es bei Bedarf in Live ein.

## 5. Multi-Out (optional)

1. Im Plugin unter **GLOBAL → Output mode** **Multi-Out (16 x Stereo)** wählen.
2. Jedes Pad geht auf seinen Ausgang. Standard: Pad 1–16 einer Bank → Out 1–16. Im Inspector kannst du das pro Pad unter „Multi-out“ ändern.
3. Eine neue **Audiospur** anlegen und einstellen:
   - **Audio From** = die PadPorter-Spur
   - darunter **Pad Out N** wählen
   - **Monitor** = **In**
4. **Out 1** liegt weiterhin auf der PadPorter-Spur selbst.

**Achtung, Live Lite:** maximal 16 Spuren insgesamt. Lege nur die Ausgänge an, die du wirklich brauchst.

## 6. Speichern

Beim Speichern des Live-Sets speichert PadPorter Folgendes mit:
- den Projektpfad, den gewählten Drum-Track und die Bank
- alle Pad-Änderungen
- die globalen Parameter (Master, Velocity, Tune, Output mode)

Die **Samples** werden beim Laden des Sets neu aus dem Projektordner gelesen. Wurde das Projekt verschoben, zeigt PadPorter „Project not found“. Öffne es dann neu; die Pad-Änderungen bleiben erhalten, solange du die Spur nicht wechselst.

## Fehlersuche

| Problem | Lösung |
|---|---|
| PadPorter fehlt in der Plug-In-Liste | Prüfen, ob `C:\Program Files\Common Files\VST3\PadPorter.vst3` existiert; „VST3 System Folders“ auf On; Rescan mit Alt |
| Kein Ton | Spur auf **Monitor Auto/In** stellen; Noten im Clip müssen den Pad-Noten entsprechen; Pad im Plugin anklicken, um den Ton zu testen |
| Pads rot „missing“ | Samples fehlen im Ordner `<Projekt>_[ProjectData]` neben der `.xpj` |
| Drag & Drop erzeugt keinen Clip | Auf einen **leeren** Clip-Slot einer MIDI-Spur ziehen, nicht auf eine Audiospur |
| Live-Log | `%APPDATA%\Ableton\Live 12.x\Preferences\Log.txt` |
