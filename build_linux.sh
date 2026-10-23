#!/usr/bin/env bash
set -euo pipefail
# Needs: cmake, g++, qt6-base-dev, libqt6sql6-sqlite
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
(cd build && cpack -G DEB)
echo "Installer: build/mill-manager_*.deb"
