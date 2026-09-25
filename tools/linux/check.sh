#!/usr/bin/env bash
# check.sh [x86_64|aarch64 ...] [--quick]: run finished tools/build_linux.sh
# builds through
#   - the startup smoke test (tools/check_startup.py): 120 frames of the game
#     loop, no interpreter fallback, on this machine's glibc (x86_64 hosts)
#   - the same on Debian 10's glibc 2.28, the oldest userland the builds
#     target (aarch64 through qemu-user, with Debian 10 arm64 as its root)
#   - the generated-vs-interpreter differential check, 3000 frames
#     (--quick: 300 under qemu)
# Needs the ROM at roms/shantae.gbc ($SHANTAE_ROM to use another).
set -euo pipefail
HERE="$(cd -- "${BASH_SOURCE[0]%/*}" && pwd)"
ROOT="$(cd -- "$HERE/../.." && pwd)"
CACHE="${SHANTAE_LINUX_CACHE:-$HOME/.cache/shantae-linux}"
ROM="${SHANTAE_ROM:-$ROOT/roms/shantae.gbc}"
LOGS="$ROOT/logs"

arches=()
quick=0
for arg in "$@"; do
    case "$arg" in
        x86_64|aarch64) arches+=("$arg") ;;
        --quick) quick=1 ;;
        *) echo "usage: $0 [x86_64|aarch64 ...] [--quick]"; exit 2 ;;
    esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x86_64 aarch64)
[ -f "$ROM" ] || { echo "no ROM at $ROM (set SHANTAE_ROM)"; exit 1; }
bash "$HERE/bootstrap.sh" buster
mkdir -p "$LOGS"
failed=0

# The command prefix that runs an <arch> binary on Debian 10's libraries.
buster_runner() {
    local arch=$1 root="$CACHE/buster-$1"
    case "$arch" in
        x86_64)
            [ "$(uname -m)" = x86_64 ] || return 1
            echo "$root/lib64/ld-linux-x86-64.so.2 --library-path $root/lib/x86_64-linux-gnu:$root/usr/lib/x86_64-linux-gnu" ;;
        aarch64)
            if [ "$(uname -m)" = aarch64 ]; then
                echo "$root/lib/ld-linux-aarch64.so.1 --library-path $root/lib/aarch64-linux-gnu:$root/usr/lib/aarch64-linux-gnu"
            else
                command -v qemu-aarch64-static > /dev/null || return 1
                echo "qemu-aarch64-static -L $root"
            fi ;;
    esac
}

smoke() {  # smoke <arch> <label> <wrapper>
    local log="$LOGS/startup-smoke-linux-$1-$2.log"
    if python3 "$ROOT/tools/check_startup.py" --exe "$CACHE/build-$1/shantae" --rom "$ROM" \
        --log "$log" --timeout 900 ${3:+--wrapper "$3"} > /dev/null; then
        echo "PASS $1 startup on $2"
    else
        echo "FAIL $1 startup on $2; see $log"
        failed=1
    fi
}

differential() {  # differential <arch> <frames> <wrapper>
    local state log="$LOGS/differential-linux-$1.log"
    state=$(mktemp -d)
    echo "$ROM" > "$state/rom.cfg"
    # shellcheck disable=SC2086
    (cd "$state" && GBRECOMP_STATE_DIR="$state" GBRECOMP_NO_LAUNCHER=1 GBRECOMP_DEBUG_PORT=14510 \
        $3 "$CACHE/build-$1/shantae" --differential 2000000000 --differential-frames "$2" \
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
    [ -x "$CACHE/build-$arch/shantae" ] || { echo "no $arch build; run tools/build_linux.sh $arch"; failed=1; continue; }
    native=""
    [ "$(uname -m)" = "$arch" ] && native=1
    [ -n "$native" ] && smoke "$arch" host ""
    if runner=$(buster_runner "$arch"); then
        smoke "$arch" debian10 "$runner"
    else
        echo "SKIP $arch on Debian 10 (no way to run $arch here)"
    fi
    if [ -n "$native" ]; then
        differential "$arch" 3000 ""
    elif [ -n "${runner:-}" ]; then
        differential "$arch" $([ "$quick" = 1 ] && echo 300 || echo 3000) "$runner"
    fi
done
exit $failed
