; Inno Setup 6 script for PadPorter.
; Build with installer\build-installer.bat (passes AppVersion from CMakeLists.txt).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define BuildDir "..\build\Release"
#define Artefacts BuildDir + "\plugin\PadPorter_artefacts\Release"

[Setup]
AppId={{8C1E5B7A-4F2D-4C61-9A3E-5D7B2E9F1A40}
AppName=PadPorter
AppVersion={#AppVersion}
AppVerName=PadPorter {#AppVersion}
AppPublisher=PadPorter
DefaultDirName={autopf}\PadPorter
DefaultGroupName=PadPorter
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=PadPorter-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayIcon={app}\PadPorter.exe
VersionInfoVersion={#AppVersion}

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "app";  Description: "PadPorter Standalone app"; Types: full custom
Name: "vst3"; Description: "PadPorter VST3 plugin (Ableton Live, ...)"; Types: full custom
Name: "cli";  Description: "Command line tool (padporter-cli)"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Components: app; Flags: unchecked

[Files]
Source: "{#Artefacts}\Standalone\PadPorter.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Artefacts}\VST3\PadPorter.vst3\*"; DestDir: "{commoncf64}\VST3\PadPorter.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\cli\padporter-cli.exe"; DestDir: "{app}"; Components: cli; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\ableton-live.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\PadPorter"; Filename: "{app}\PadPorter.exe"; Components: app
Name: "{group}\{cm:UninstallProgram,PadPorter}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\PadPorter"; Filename: "{app}\PadPorter.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\PadPorter.exe"; Description: "{cm:LaunchProgram,PadPorter}"; Components: app; Flags: nowait postinstall skipifsilent
