@echo off
REM Needs: Qt 6 (MSVC or MinGW kit), CMake, NSIS. Run from a Qt command prompt.
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
windeployqt build\MillManager.exe --release
cd build && cpack -G NSIS
echo Installer: build\mill-manager-*-win64.exe
