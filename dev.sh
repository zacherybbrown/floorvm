#!/usr/bin/env bash
set -e
./build.sh
# Multi-config (MSVC) puts the exe under build/Release; single-config under build.
EXE=$(ls build/Release/FloorVM.exe build/Release/FloorVM build/FloorVM.exe build/FloorVM 2>/dev/null | head -1)
"$EXE" "$@"
