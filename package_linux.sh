#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DIST_DIR="$SCRIPT_DIR/dist"
STAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/mill-manager-cpp-package.XXXXXX")"
trap 'rm -rf -- "$STAGE_DIR"' EXIT

command -v cmake >/dev/null || { echo "CMake is required." >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb is required." >&2; exit 1; }
ARCH="$(dpkg --print-architecture)"
if [[ "$ARCH" != "amd64" ]]; then
    echo "This package recipe currently supports amd64 only (detected: $ARCH)." >&2
    exit 1
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel 2
ctest --test-dir "$BUILD_DIR" --output-on-failure
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR" --prefix /usr

mkdir -p "$STAGE_DIR/DEBIAN" "$DIST_DIR"
cat >"$STAGE_DIR/DEBIAN/control" <<EOF
Package: mill-manager-cpp
Version: 23.0.0-9
Section: office
Priority: optional
Architecture: $ARCH
Maintainer: JM Group <mutebiauhad4@gmail.com>
Depends: libqt6core6 | libqt6core6t64, libqt6gui6 | libqt6gui6t64, libqt6widgets6 | libqt6widgets6t64, libqt6network6 | libqt6network6t64, libqt6sql6 | libqt6sql6t64, libqt6printsupport6 | libqt6printsupport6t64, libqt6sql6-sqlite, qt6-qpa-plugins
Description: C++ coffee and grain mill management
 C++/Qt editions of Mill Manager and Mill Manager F, with separate
 databases, configurable themes, offline KIYAMBI business insights and
 surveys, reports, and document editing.
EOF

dpkg-deb --build --root-owner-group "$STAGE_DIR" \
    "$DIST_DIR/mill-manager-cpp_23.0.0-9_amd64.deb"
echo "Built package: $DIST_DIR/mill-manager-cpp_23.0.0-9_amd64.deb"

tar --exclude=./build --exclude=./dist -czf \
    "$DIST_DIR/Mill_Manager_CPP_23.0.0_source.tar.gz" -C "$SCRIPT_DIR" .
echo "Built source archive: $DIST_DIR/Mill_Manager_CPP_23.0.0_source.tar.gz"
