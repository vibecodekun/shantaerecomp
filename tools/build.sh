#!/usr/bin/env bash
# Rebuild the recompiler, regenerate generated/ from shantae.toml, build the game.
# Uses msys2 mingw64 only (devkitPro and other toolchains on PATH conflict).
# Build logs go to generated/build/*.log.
set -e
ROOT="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
# Resolve Python before restricting PATH to the compiler toolchain.
PYTHON="${SHANTAE_PYTHON:-$(command -v python || command -v python3)}"
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:/c/Windows/system32:/c/Windows"
cd "$ROOT"
LOGS="$ROOT/generated/build"
mkdir -p "$LOGS"
"$PYTHON" tools/gen_annotations.py --output shantae.annotations
# A fresh clone has no recompiler build tree yet.
if [ ! -f gbrecompiled/build/build.ninja ]; then
    cmake -G Ninja -S gbrecompiled -B gbrecompiled/build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_MAKE_PROGRAM=/c/msys64/mingw64/bin/ninja.exe \
        -DCMAKE_C_COMPILER=/c/msys64/mingw64/bin/gcc.exe \
        -DCMAKE_CXX_COMPILER=/c/msys64/mingw64/bin/g++.exe > "$LOGS/cmake_recomp.log" 2>&1 \
        || { tail -20 "$LOGS/cmake_recomp.log"; exit 1; }
fi
ninja -C gbrecompiled/build gbrecomp > "$LOGS/build_recomp.log" 2>&1 || { grep -E "error|FAILED" "$LOGS/build_recomp.log"; exit 1; }
./gbrecompiled/build/bin/gbrecomp.exe --config shantae.toml > "$LOGS/recomp.log" 2>&1 || { tail -20 "$LOGS/recomp.log"; exit 1; }
"$PYTHON" tools/audit_native_coverage.py --report "$LOGS/native-coverage.json"
"$PYTHON" tools/audit_whole_rom.py
"$PYTHON" tools/audit_ram_coverage.py
grep -E "Found [0-9]+ functions" "$LOGS/recomp.log"
# Configure every time (quick when nothing changed) so the options below stick.
# RECOMP_UI_ENABLE_MODS shows Shantae's options on the launcher's Mods page.
cmake -G Ninja -S generated -B generated/build \
    -DCMAKE_MAKE_PROGRAM=/c/msys64/mingw64/bin/ninja.exe \
    -DCMAKE_C_COMPILER=/c/msys64/mingw64/bin/gcc.exe \
    -DCMAKE_CXX_COMPILER=/c/msys64/mingw64/bin/g++.exe \
    -DRECOMP_UI_ENABLE_MODS=ON > "$LOGS/cmake.log" 2>&1 || { tail -20 "$LOGS/cmake.log"; exit 1; }
ninja -C generated/build > "$LOGS/build_game.log" 2>&1 || { grep -E "error|FAILED" "$LOGS/build_game.log" | head -20; exit 1; }
"$PYTHON" tools/check_startup.py
echo "build ok: generated/build/shantae.exe"
