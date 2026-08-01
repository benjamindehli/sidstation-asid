#!/usr/bin/env bash
# Quick build without CMake. On macOS this links CoreMIDI; elsewhere it builds
# the dry-run backend (prints messages instead of sending them).
set -euo pipefail
cd "$(dirname "$0")"

SRC="main.cpp ../core/src/Parameters.cpp ../core/src/Patch.cpp"
FLAGS="-std=c++17 -Wall -Wextra -I../core/include -O2"

if [[ "$(uname)" == "Darwin" ]]; then
    # shellcheck disable=SC2086
    clang++ $FLAGS $SRC -framework CoreMIDI -framework CoreFoundation -o sidprobe
else
    # shellcheck disable=SC2086
    ${CXX:-g++} $FLAGS $SRC -o sidprobe
    echo "(non-macOS: built dry-run backend)"
fi
echo "Built ./sidprobe"
