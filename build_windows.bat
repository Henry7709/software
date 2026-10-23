@echo off
setlocal enabledelayedexpansion

REM Needs: Qt 6.3+ (MSVC or MinGW), CMake, NSIS, and windeployqt on PATH.
REM Run from a Qt command prompt or ensure the toolchain and Qt bin folder are on PATH.

if "%CMAKE_GENERATOR%"=="" (
    set "CMAKE_GENERATOR=NMake Makefiles"
)

where cmake >nul 2>nul || (
    echo CMake was not found on PATH.
    exit /b 1
)

where windeployqt >nul 2>nul || (
    echo windeployqt was not found on PATH. Open a Qt 6 command prompt and try again.
    exit /b 1
)

cmake -S . -B build -G "%CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
ctest --test-dir build --output-on-failure -C Release || exit /b 1

windeployqt build\MillManager.exe --release || exit /b 1
windeployqt build\MillManagerF.exe --release || exit /b 1

cd build || exit /b 1
cpack -G NSIS -C Release || exit /b 1

echo Installer created in build\
