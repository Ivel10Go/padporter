# Toolchain-Setup (Windows 11)

## Ist-Zustand (geprüft am 2026-10-13)

| Tool | Status |
|---|---|
| **Visual Studio Community 2026 (Insiders)** | ✅ mit MSVC 14.44 (v143) und 14.50, CMake 4.1.1 und Ninja. Der Build verwendet das v143-Toolset 14.44 |
| Windows SDK 10.0.26100 | ✅ |
| Git 2.55 | ✅ |
| VS 2019 Build Tools | ⚠️ registriert, aber der Ordner fehlt (verwaiste Installation, stört nicht). Aufräumen geht im Visual Studio Installer unter „Mehr → Deinstallieren“ |
| CMake 4.1.0-rc1 (im PATH) | ⚠️ wird nicht benutzt; `build.bat` nimmt das CMake aus Visual Studio |
| Ableton Live 12 Lite | ✅ |
| Inno Setup 6 | ❌ wird nur für den Installer gebraucht: `winget install JRSoftware.InnoSetup` |
| **Freier Speicher auf C:** | ❌ **unter 1 GB.** Für Build und Installer sollten ≥ 3 GB frei sein |

> Phase 0 hatte die VS-2026-Installation übersehen, weil `vswhere` Vorabversionen nur mit `-prerelease` anzeigt. `tools\vsenv.bat` berücksichtigt das.

## Bauen

Im Projektordner (normale Eingabeaufforderung oder PowerShell genügt):

```
build.bat Release
```

Das Skript erledigt Folgendes:
1. Es lädt über `tools\vsenv.bat` die neueste Visual-Studio-Umgebung mit C++.
2. Es konfiguriert mit CMake und Ninja nach `build\Release`. Beim ersten Mal lädt es JUCE, nlohmann/json, miniz und doctest (~120 MB).
3. Es baut Core, CLI, Tests, die Standalone-App und das VST3-Plugin.
4. Es führt alle Tests aus.

Ergebnisse:

| Datei | Pfad |
|---|---|
| App | `build\Release\plugin\PadPorter_artefacts\Release\Standalone\PadPorter.exe` |
| VST3 | `build\Release\plugin\PadPorter_artefacts\Release\VST3\PadPorter.vst3` |
| CLI | `build\Release\cli\padporter-cli.exe` |

Platzbedarf: etwa 1 GB für `build\`. Mit `rmdir /s /q build` gibst du ihn wieder frei.

## Installer

```
winget install JRSoftware.InnoSetup
installer\build-installer.bat
```
→ `dist\PadPorter-<version>-Setup.exe`
