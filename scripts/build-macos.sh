#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "The AppKit administrator application requires macOS." >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/macos}"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
QT_PREFIX="${QT_PREFIX:-}"

if [[ -z "$QT_PREFIX" ]]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX)"
    elif command -v qmake >/dev/null 2>&1; then
        QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX)"
    elif [[ -d "$HOME/Qt/6.8.3/macos" ]]; then
        QT_PREFIX="$HOME/Qt/6.8.3/macos"
    elif command -v brew >/dev/null 2>&1; then
        QT_PREFIX="$(brew --prefix qt)"
    fi
fi

ARGS=(
    -S "$ROOT" -B "$BUILD_DIR" -G "${GENERATOR:-Ninja}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Debug}"
    -DSMARTPARK_BUILD_MACOS_ADMIN=ON -DBUILD_TESTING=ON
)
if [[ -n "$QT_PREFIX" ]]; then
    ARGS+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi

"$CMAKE_BIN" "${ARGS[@]}"
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel "${BUILD_JOBS:-4}"
APP="$BUILD_DIR/apps/macos/smartpark_admin_macos.app/Contents/MacOS/smartpark_admin_macos"
[[ -x "$APP" ]] || { echo "Built executable not found: $APP" >&2; exit 1; }
echo "Build OK: $APP"
