#!/usr/bin/env bash
# Build the Windows releases -- x64, 32-bit x86 and ARM64 -- from generated/,
# after tools/build.sh has regenerated it (the generated C is the same for every
# arch) and built the x64 game.
#
#   bash tools/build_windows.sh [x64|x86|arm64 ...] [--no-package] [--no-librashader]
#
# x64 is tools/build.sh's build in generated/build, brought up to date here.
# x86 builds in generated/build-x86 with msys2 mingw32's GCC, arm64 in
# generated/build-arm64 with msys2 mingw64's clang cross-compiling
# (tools/windows/toolchain.cmake); their SDL2, ANGLE and ARM64 runtime come
# from pinned msys2 packages (tools/windows/bootstrap.sh) in
# $SHANTAE_WINDOWS_CACHE (default third_party/windows; no spaces in it).
# librashader.dll for each arch is tools/build_librashader.sh's. Every build dir
# gets the DLLs the game needs beside it, so it runs in place (tools/windows/
# check.sh). Results:
#   dist/windows/shantae-recomp-<version>-windows-<arch>.zip
# <version> is $SHANTAE_VERSION, or today's date.
set -euo pipefail
ROOT="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
MSYS2="${SHANTAE_MSYS2:-/c/msys64}"
CACHE="${SHANTAE_WINDOWS_CACHE:-$ROOT/third_party/windows}"
export SHANTAE_MSYS2="$MSYS2" SHANTAE_WINDOWS_CACHE="$CACHE"
VERSION="${SHANTAE_VERSION:-$(date +%Y%m%d)}"
READOBJ="$MSYS2/mingw64/bin/llvm-readobj.exe"
# Windows-side tools only: other toolchains on PATH (devkitPro, Git's) conflict.
SYSPATH="$MSYS2/usr/bin:/c/Windows/system32:/c/Windows"

arches=()
package=1
librashader=1
for arg in "$@"; do
    case "$arg" in
        x64|x86|arm64) arches+=("$arg") ;;
        --no-package) package=0 ;;
        --no-librashader) librashader=0 ;;
        *) echo "usage: $0 [x64|x86|arm64 ...] [--no-package] [--no-librashader]"; exit 2 ;;
    esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x64 x86 arm64)

log() { printf '=== %s\n' "$*"; }
winpath() { cygpath -m "$1"; }

[ -f "$ROOT/generated/CMakeLists.txt" ] || { echo "no generated/ project; run tools/build.sh first"; exit 1; }
mkdir -p "$ROOT/logs"

if [ "$librashader" = 1 ]; then
    # The slang-shaders collection is copied once; after that only the DLLs.
    lrs_args=(--no-shaders)
    [ -f "$ROOT/third_party/librashader/shaders.stamp" ] || lrs_args=()
    log "librashader for ${arches[*]}"
    bash "$ROOT/tools/build_librashader.sh" "${arches[@]}" "${lrs_args[@]}" > "$ROOT/logs/librashader-windows.log" 2>&1 \
        || { tail -30 "$ROOT/logs/librashader-windows.log"; exit 1; }
fi

build_dir() {
    case "$1" in
        x64) echo "$ROOT/generated/build" ;;
        *) echo "$ROOT/generated/build-$1" ;;
    esac
}

# The DLLs that come with each arch's compiler and libraries.
dll_dirs() {
    case "$1" in
        x64) echo "$MSYS2/mingw64/bin" ;;
        x86) printf '%s\n' "$CACHE/sysroot-x86/mingw32/bin" "$MSYS2/mingw32/bin" ;;
        arm64) echo "$CACHE/sysroot-arm64/clangarm64/bin" ;;
    esac
}

build_x64() {
    local build
    build=$(build_dir x64)
    [ -f "$build/build.ninja" ] || { echo "no x64 build in $build; run tools/build.sh first"; exit 1; }
    log "building x64 (generated/build)"
    PATH="$MSYS2/mingw64/bin:$SYSPATH" ninja -C "$build" > "$build/build_windows.log" 2>&1 \
        || { grep -E "error|FAILED" "$build/build_windows.log" | head -30; exit 1; }
}

build_cross() {
    local arch=$1 build prefix path
    build=$(build_dir "$arch")
    bash "$ROOT/tools/windows/bootstrap.sh" "$arch"
    case "$arch" in
        # mingw32 first: its compiler's own DLLs have the same names as mingw64's.
        x86) prefix=mingw32 path="$MSYS2/mingw32/bin:$MSYS2/mingw64/bin" ;;
        arm64) prefix=clangarm64 path="$MSYS2/mingw64/bin" ;;
    esac
    mkdir -p "$build"
    log "configuring $arch"
    # The runtime DLLs the generated project would copy from the compiler's
    # folder are mingw64's; stage_dlls picks this arch's instead.
    PATH="$path:$SYSPATH" "$MSYS2/mingw64/bin/cmake.exe" -G Ninja -S "$(winpath "$ROOT/generated")" -B "$(winpath "$build")" \
        -DCMAKE_MAKE_PROGRAM="$(winpath "$MSYS2/mingw64/bin/ninja.exe")" \
        -DCMAKE_TOOLCHAIN_FILE="$(winpath "$ROOT/tools/windows/toolchain.cmake")" \
        -DSHANTAE_WINDOWS_ARCH="$arch" -DSHANTAE_WINDOWS_CACHE="$(winpath "$CACHE")" \
        -DSHANTAE_MSYS2="$(winpath "$MSYS2")" \
        -DSDL2_DIR="$(winpath "$CACHE/sysroot-$arch/$prefix/lib/cmake/SDL2")" \
        -DGBRECOMP_STAGE_RUNTIME_DLLS=OFF \
        -DRECOMP_UI_ENABLE_MODS=ON > "$build/cmake.log" 2>&1 \
        || { tail -30 "$build/cmake.log"; exit 1; }
    log "building $arch (generated/build-$arch)"
    PATH="$path:$SYSPATH" "$MSYS2/mingw64/bin/ninja.exe" -C "$(winpath "$build")" > "$build/build.log" 2>&1 \
        || { grep -E "error|FAILED" "$build/build.log" | head -30; exit 1; }
}

# Copy the DLLs the game needs from its arch's libraries to beside it (x64's
# generated project copies its own), then check that everything there is <arch>.
stage_dlls() {
    local arch=$1 build list dll want machine file
    local -a dirs dlls
    build=$(build_dir "$arch")
    mapfile -t dirs < <(dll_dirs "$arch")
    # Through a variable, so that a missing DLL stops the build (set -e).
    list=$(bash "$ROOT/tools/windows/dlls.sh" "$build" "${dirs[@]}")
    mapfile -t dlls <<< "$list"
    for dll in "${dlls[@]}"; do
        [ "${dll%/*}" = "$build" ] || cmp -s "$dll" "$build/${dll##*/}" || cp "$dll" "$build/"
    done
    case "$arch" in x64) want=AMD64 ;; x86) want=I386 ;; arm64) want=ARM64 ;; esac
    list=$(bash "$ROOT/tools/windows/dlls.sh" "$build")
    mapfile -t dlls <<< "$list"
    for file in "$build/shantae.exe" "${dlls[@]}"; do
        machine=$("$READOBJ" --file-headers "$file" | sed -n 's/.*Machine: IMAGE_FILE_MACHINE_\([A-Z0-9]*\).*/\1/p' | head -1)
        [ "$machine" = "$want" ] || { echo "$file is $machine code, not $want"; exit 1; }
    done
    echo "  $arch: shantae.exe + ${dlls[*]##*/}"
}

for arch in "${arches[@]}"; do
    if [ "$arch" = x64 ]; then build_x64; else build_cross "$arch"; fi
    stage_dlls "$arch"
done

[ "$package" = 1 ] || { log "done (not packaged)"; exit 0; }
for arch in "${arches[@]}"; do
    bash "$ROOT/tools/windows/package.sh" "$arch" "$(build_dir "$arch")" "$VERSION"
done
