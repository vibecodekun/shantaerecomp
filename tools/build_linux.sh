#!/usr/bin/env bash
# Build the Linux releases (x86_64 and aarch64) from generated/. Runs on a
# Debian/Ubuntu machine or in WSL (from Windows: wsl bash tools/build_linux.sh),
# after tools/build.sh has regenerated generated/ -- the generated C is the same
# for every platform, so the recompiler itself only runs on Windows.
#
#   bash tools/build_linux.sh [x86_64|aarch64 ...] [--no-package] [--no-librashader]
#
# The first run fetches the toolchain into ~/.cache/shantae-linux
# (tools/linux/bootstrap.sh; $SHANTAE_LINUX_CACHE moves it). Binaries target
# glibc 2.28 with SDL2 and the C++ runtime linked in, so they run on any
# distribution from 2019 on (Debian 10, Ubuntu 20.04, RHEL 8, SteamOS, ...).
# The build tree lives in the cache on the Linux file system; results:
#   dist/linux/shantae-recomp-<version>-linux-<arch>.tar.gz
#   dist/linux/Shantae_Recomp-<version>-<arch>.AppImage
# <version> is $SHANTAE_VERSION, or today's date.
set -euo pipefail
ROOT="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
CACHE="${SHANTAE_LINUX_CACHE:-$HOME/.cache/shantae-linux}"
export SHANTAE_LINUX_CACHE="$CACHE"
VERSION="${SHANTAE_VERSION:-$(date +%Y%m%d)}"
GLIBC_MAX=2.28

arches=()
package=1
librashader=1
for arg in "$@"; do
    case "$arg" in
        x86_64|aarch64) arches+=("$arg") ;;
        --no-package) package=0 ;;
        --no-librashader) librashader=0 ;;
        *) echo "usage: $0 [x86_64|aarch64 ...] [--no-package] [--no-librashader]"; exit 2 ;;
    esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x86_64 aarch64)

log() { printf '=== %s\n' "$*"; }

bash "$ROOT/tools/linux/bootstrap.sh" base
[ "$librashader" = 1 ] && bash "$ROOT/tools/linux/bootstrap.sh" rust
CMAKE="$CACHE/tools/cmake/bin/cmake"
NINJA="$CACHE/tools/ninja/ninja"

[ -f "$ROOT/generated/CMakeLists.txt" ] || { echo "no generated/ project; run tools/build.sh first"; exit 1; }

# ---- sources -> the Linux file system -------------------------------------------
# rsync keeps modification times, so an unchanged file is neither copied nor
# rebuilt.
TREE="$CACHE/tree"
log "syncing sources to $TREE"
mkdir -p "$TREE/third_party/librashader" "$TREE/gbrecompiled"
rsync -a --delete --include='*.c' --include='*.h' --include='CMakeLists.txt' --exclude='*' \
    "$ROOT/generated/" "$TREE/generated/"
rsync -a --delete --exclude='/build*/' "$ROOT/gbrecompiled/runtime/" "$TREE/gbrecompiled/runtime/"
rsync -a --delete --exclude='.git/' --exclude='/build*/' --exclude='/tests/' --exclude='/test_data/' \
    "$ROOT/recomp-ui/" "$TREE/recomp-ui/"
rsync -a --delete --include='*.c' --include='*.cpp' --exclude='*' "$ROOT/tools/" "$TREE/tools/"
rsync -a "$ROOT"/*.c "$ROOT"/*.cpp "$ROOT"/*.h "$ROOT"/*.inc "$ROOT/game_build.cmake" "$TREE/"
if [ -f "$ROOT/third_party/librashader/shaders.stamp" ]; then
    rsync -a --delete "$ROOT/third_party/librashader/shaders/" "$TREE/third_party/librashader/shaders/"
    rsync -a "$ROOT/third_party/librashader/shaders.stamp" "$TREE/third_party/librashader/"
fi

# ---- librashader ------------------------------------------------------------------
# From the source tools/build_librashader.sh exported and patched.
LRS_SRC="$ROOT/third_party/librashader/src"
build_librashader() {
    local arch=$1
    local out="$ROOT/third_party/librashader/linux-$arch"
    local stamp
    stamp="$(cat "$LRS_SRC/.source-stamp") $(cat "$CACHE/rust/.stamp")"
    if [ "$(cat "$out/.stamp" 2> /dev/null)" != "$stamp" ]; then
        log "librashader for $arch"
        rsync -a --delete --exclude='/target/' "$LRS_SRC/" "$CACHE/librashader-src/"
        PATH="$CACHE/tools/zig:$CACHE/rust/cargo/bin:$PATH" \
        RUSTUP_HOME="$CACHE/rust/rustup" CARGO_HOME="$CACHE/rust/cargo" \
        ZIG_GLOBAL_CACHE_DIR="$CACHE/zig-cache" ZIG_LOCAL_CACHE_DIR="$CACHE/zig-cache" \
            cargo zigbuild --release --manifest-path "$CACHE/librashader-src/Cargo.toml" \
                -p librashader-capi --no-default-features --features runtime-opengl \
                --target "$arch-unknown-linux-gnu.$GLIBC_MAX" --target-dir "$CACHE/librashader-target" \
                > "$CACHE/librashader-$arch.log" 2>&1 \
            || { tail -30 "$CACHE/librashader-$arch.log"; exit 1; }
        mkdir -p "$out"
        cp "$CACHE/librashader-target/$arch-unknown-linux-gnu/release/liblibrashader_capi.so" "$out/librashader.so"
        echo "$stamp" > "$out/.stamp"
    fi
    mkdir -p "$TREE/third_party/librashader/linux-$arch"
    rsync -a "$out/librashader.so" "$TREE/third_party/librashader/linux-$arch/"
}

# ---- checks on a finished binary ----------------------------------------------------
# Newest glibc symbol version it needs, and what it links.
check_elf() {
    local file=$1
    local newest
    newest=$(readelf -W -V "$file" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
    echo "  ${file##*/}: needs ${newest:-no glibc}; links $(readelf -d "$file" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')"
    if [ -n "$newest" ] && [ "$(printf '%s\n' "${newest#GLIBC_}" "$GLIBC_MAX" | sort -V | tail -1)" != "$GLIBC_MAX" ]; then
        echo "  $file needs $newest, newer than the $GLIBC_MAX this build targets"
        exit 1
    fi
}

# ---- build -------------------------------------------------------------------------
for arch in "${arches[@]}"; do
    if [ "$librashader" = 1 ]; then
        if [ -f "$LRS_SRC/.source-stamp" ]; then
            build_librashader "$arch"
        else
            echo "no librashader source at $LRS_SRC (tools/build_librashader.sh exports it); building without"
        fi
    fi
    build="$CACHE/build-$arch"
    log "configuring $arch"
    "$CMAKE" -G Ninja -S "$TREE/generated" -B "$build" \
        -DCMAKE_MAKE_PROGRAM="$NINJA" \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/linux/toolchain.cmake" \
        -DSHANTAE_LINUX_ARCH="$arch" -DSHANTAE_LINUX_CACHE="$CACHE" \
        -DSDL2_DIR="$CACHE/sdl2-$arch/lib/cmake/SDL2" \
        -DRECOMP_UI_ENABLE_MODS=ON > "$CACHE/cmake-$arch.log" 2>&1 \
        || { tail -30 "$CACHE/cmake-$arch.log"; exit 1; }
    log "building $arch"
    "$NINJA" -C "$build" > "$CACHE/build-$arch.log" 2>&1 \
        || { grep -E "error|FAILED" "$CACHE/build-$arch.log" | head -30; exit 1; }
    check_elf "$build/shantae"
    [ -f "$build/librashader.so" ] && check_elf "$build/librashader.so"
done

[ "$package" = 1 ] || { log "done (not packaged)"; exit 0; }
for arch in "${arches[@]}"; do
    bash "$ROOT/tools/linux/package.sh" "$arch" "$CACHE/build-$arch" "$VERSION"
done
