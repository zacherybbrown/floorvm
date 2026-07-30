#!/usr/bin/env bash
set -e

# Use a locally-vendored SDL3 (third_party/SDL3-*/) if present; otherwise rely
# on a system-installed SDL3 that find_package can locate on its own.
EXTRA=()
SDL_CMAKE=$(ls -d third_party/SDL3-*/cmake 2>/dev/null | head -1 || true)
if [ -n "$SDL_CMAKE" ]; then
    EXTRA+=("-DSDL3_DIR=$(cd "$SDL_CMAKE" && pwd -W 2>/dev/null || pwd)")
fi

cmake -S . -B build "${EXTRA[@]}"
cmake --build build --config Release

# Make the executable runnable in place: SDL3.dll must sit next to it.
SDL_DLL=$(ls third_party/SDL3-*/lib/x64/SDL3.dll 2>/dev/null | head -1 || true)
if [ -n "$SDL_DLL" ]; then
    for d in build/Release build; do
        [ -d "$d" ] && cp -f "$SDL_DLL" "$d/" 2>/dev/null || true
    done
fi
