#!/usr/bin/env bash
# check.sh [x64|x86|arm64 ...]: check finished tools/build_windows.sh builds
# in their build dirs, with the DLLs staged there:
#   - tools/windows/check_imports.py: every module is the arch's, and every
#     function imported from a DLL beside the exe is exported by it (all arches)
#   - the startup smoke test (tools/check_startup.py): 120 frames of the game
#     loop, no interpreter fallback
#   - the generated-vs-interpreter differential check, 3000 frames
# The last two run the build: an x64 PC runs the x64 and x86 builds; the ARM64
# build needs an ARM64 PC (Windows 11 on ARM runs all three). Needs the ROM at
# roms/shantae.gbc ($SHANTAE_ROM to use another).
set -euo pipefail
HERE="$(cd -- "${BASH_SOURCE[0]%/*}" && pwd)"
ROOT="$(cd -- "$HERE/../.." && pwd)"
ROM="${SHANTAE_ROM:-$ROOT/roms/shantae.gbc}"
PYTHON="${SHANTAE_PYTHON:-$(command -v python || command -v python3)}"
LOGS="$ROOT/logs"

arches=()
for arg in "$@"; do
    case "$arg" in
        x64|x86|arm64) arches+=("$arg") ;;
        *) echo "usage: $0 [x64|x86|arm64 ...]"; exit 2 ;;
    esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x64 x86 arm64)
[ -f "$ROM" ] || { echo "no ROM at $ROM (set SHANTAE_ROM)"; exit 1; }
mkdir -p "$LOGS"
host="${PROCESSOR_ARCHITEW6432:-${PROCESSOR_ARCHITECTURE:-AMD64}}"
failed=0

build_dir() {
    case "$1" in
        x64) echo "$ROOT/generated/build" ;;
        *) echo "$ROOT/generated/build-$1" ;;
    esac
}

runs_here() {
    case "$1" in
        x64|x86) [ "$host" = AMD64 ] || [ "$host" = ARM64 ] ;;
        arm64) [ "$host" = ARM64 ] ;;
    esac
}

# Nothing shows on the desktop or plays: the smoke test needs no window, and
# the differential check runs in a hidden one (under SDL's dummy video it stops
# early without a verdict).
smoke() {  # smoke <arch>
    local log="$LOGS/startup-smoke-windows-$1.log"
    if SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        "$PYTHON" "$ROOT/tools/check_startup.py" --exe "$(build_dir "$1")/shantae.exe" --rom "$ROM" \
        --log "$log" > /dev/null; then
        echo "PASS $1 startup"
    else
        echo "FAIL $1 startup; see $log"
        failed=1
    fi
}

differential() {  # differential <arch>
    local state log="$LOGS/differential-windows-$1.log"
    state=$(mktemp -d)
    cygpath -w "$ROM" > "$state/rom.cfg"
    (cd "$state" && GBRECOMP_STATE_DIR="$(cygpath -w "$state")" GBRECOMP_NO_LAUNCHER=1 \
        GBRECOMP_DEBUG_PORT=14511 GBRECOMP_HIDDEN_WINDOW=1 SDL_AUDIODRIVER=dummy \
        "$(build_dir "$1")/shantae.exe" --differential 2000000000 --differential-frames 3000 \
        --differential-no-memory > "$log" 2>&1) || true
    rm -rf "$state"
    if grep -q "Matched generated and interpreter execution" "$log"; then
        echo "PASS $1 differential: $(grep -o 'for [0-9]* steps / [0-9]* frames' "$log" | tail -1)"
    else
        echo "FAIL $1 differential; see $log"
        failed=1
    fi
}

for arch in "${arches[@]}"; do
    [ -f "$(build_dir "$arch")/shantae.exe" ] || { echo "no $arch build; run tools/build_windows.sh $arch"; failed=1; continue; }
    "$PYTHON" "$HERE/check_imports.py" "$arch" "$(cygpath -w "$(build_dir "$arch")")" || failed=1
    if ! runs_here "$arch"; then
        echo "SKIP $arch (this is an $host PC)"
        continue
    fi
    smoke "$arch"
    differential "$arch"
done
exit $failed
