@echo off
rem Builds dist\PadPorter-<version>-Setup.exe. Run build.bat Release first.
setlocal
set "ROOT=%~dp0.."

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo Inno Setup 6 not found. Install it with:  winget install JRSoftware.InnoSetup
    exit /b 1
)

if not exist "%ROOT%\build\Release\plugin\PadPorter_artefacts\Release\VST3\PadPorter.vst3" (
    echo Release build missing - run build.bat Release first.
    exit /b 1
)

rem Single source of truth for the version: project(PadPorter VERSION x.y.z) in CMakeLists.txt
for /f "usebackq delims=" %%v in (`powershell -NoProfile -Command "(Select-String -Path '%ROOT%\CMakeLists.txt' -Pattern 'project\(PadPorter VERSION ([0-9.]+)').Matches[0].Groups[1].Value"`) do set "VERSION=%%v"
if "%VERSION%"=="" (
    echo Could not read the version from CMakeLists.txt
    exit /b 1
)

"%ISCC%" /DAppVersion=%VERSION% "%~dp0PadPorter.iss" || exit /b 1
echo.
echo Installer: %ROOT%\dist\PadPorter-%VERSION%-Setup.exe
endlocal
