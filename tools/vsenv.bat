@echo off
rem Locates the newest Visual Studio installation with C++ tools (incl. previews)
rem and loads the x64 developer environment (cl, cmake, ninja) into this shell.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [vsenv] vswhere.exe not found - please install Visual Studio 2022 or newer with C++ tools.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
    echo [vsenv] No Visual Studio installation with C++ tools found.
    exit /b 1
)

rem Prefer the VS 2022 toolset (v143, 14.4x) when present - it is the most widely tested with JUCE.
set "VCVARS_VER="
if exist "%VSINSTALL%\VC\Tools\MSVC\14.44*" set "VCVARS_VER=-vcvars_ver=14.44"
if not "%PADPORTER_VCVARS_VER%"=="" set "VCVARS_VER=-vcvars_ver=%PADPORTER_VCVARS_VER%"

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" %VCVARS_VER% >nul
if errorlevel 1 (
    echo [vsenv] vcvars64.bat failed.
    exit /b 1
)
echo [vsenv] Using %VSINSTALL% %VCVARS_VER%
exit /b 0
