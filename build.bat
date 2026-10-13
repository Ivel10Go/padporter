@echo off
rem Usage: build.bat [Release|Debug] [core]
rem   core  -> build only core library, CLI and tests (no JUCE download)
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "BUILD_PLUGIN=ON"
if /i "%~2"=="core" set "BUILD_PLUGIN=OFF"

call "%~dp0tools\vsenv.bat" || exit /b 1

set "BUILD_DIR=%~dp0build\%CONFIG%"
cmake -S "%~dp0." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DPADPORTER_BUILD_PLUGIN=%BUILD_PLUGIN% || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1
ctest --test-dir "%BUILD_DIR%" --output-on-failure || exit /b 1

echo.
echo Build finished: %BUILD_DIR%
endlocal
