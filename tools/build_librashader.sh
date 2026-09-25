#!/usr/bin/env bash
# Build librashader (OpenGL runtime only) and stage the slang-shaders preset
# collection for the runtime's Shader Presets menu. game_build.cmake copies
# both next to shantae.exe on the next build (tools/build.sh or ninja).
#
#   bash tools/build_librashader.sh [x64|x86|arm64 ...] [--no-shaders]
#   LIBRASHADER_SRC   librashader checkout (default third_party/librashader/checkout,
#                     cloned at LIBRASHADER_REV, librashader 0.12.0, when missing)
#
# x64 (the default) builds third_party/librashader/librashader.dll, the one
# tools/build.sh stages; x86 and arm64 (tools/build_windows.sh) build
# third_party/librashader/windows-<arch>/librashader.dll. The C and C++
# runtimes are linked in (crt-static), so the DLL needs no Visual C++
# Redistributable on the player's machine.
#
# The checkout is never modified: its HEAD is exported to
# third_party/librashader/src, the patches in third_party/librashader/patches
# are applied there, and that copy is what gets built.
#
# Needs cargo (Rust, MSVC toolchain) and Visual Studio's C++ tools for each
# arch (x86 and ARM64 are their own installer components); rustup adds the x86
# and arm64 Rust targets on first use. The runtime only opens librashader.dll
# at run time, so the game builds and runs without any of this.
set -e
ROOT="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
OUT="$ROOT/third_party/librashader"
SRC="${LIBRASHADER_SRC:-$OUT/checkout}"
WORK="$OUT/src"
# The revision the patches and the preset sweep were made against.
REV="${LIBRASHADER_REV:-87e8a97b50516d997defeaa168173dcd185d4022}"
arches=()
shaders=1
for arg in "$@"; do
    case "$arg" in
        x64|x86|arm64) arches+=("$arg") ;;
        --no-shaders) shaders=0 ;;
        *) echo "usage: $0 [x64|x86|arm64 ...] [--no-shaders]"; exit 2 ;;
    esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x64)
if [ -z "${LIBRASHADER_SRC:-}" ] && [ ! -f "$SRC/Cargo.toml" ]; then
    echo "=== cloning librashader @ ${REV:0:8} into $SRC ==="
    git clone https://github.com/SnowflakePowered/librashader "$SRC"
    git -C "$SRC" checkout --quiet "$REV"
fi
[ -f "$SRC/Cargo.toml" ] || { echo "no librashader checkout at $SRC (set LIBRASHADER_SRC)"; exit 1; }
command -v cargo > /dev/null || { echo "cargo not found; install Rust (rustup) first"; exit 1; }
mkdir -p "$OUT"

# Re-export only when the checkout or the patches change, so cargo keeps its
# incremental state between runs.
STAMP="$(git -C "$SRC" rev-parse HEAD) $(cat "$OUT"/patches/*.patch 2> /dev/null | git hash-object --stdin)"
if [ "$(cat "$WORK/.source-stamp" 2> /dev/null)" != "$STAMP" ]; then
    echo "=== exporting $SRC @ $(git -C "$SRC" rev-parse --short HEAD) ==="
    rm -rf "$WORK"
    mkdir -p "$WORK"
    git -C "$SRC" archive HEAD | tar -x -C "$WORK"
    for p in "$OUT"/patches/*.patch; do
        [ -f "$p" ] || continue
        echo "applying ${p##*/}"
        git -C "$WORK" apply --whitespace=nowarn "$p"
    done
    echo "$STAMP" > "$WORK/.source-stamp"
fi

# The default feature set is every runtime (Vulkan, D3D9/11/12, Metal); the
# game only renders through GL (ANGLE's GLES 3.1 on Windows).
for arch in "${arches[@]}"; do
    case "$arch" in
        x64) triple=x86_64-pc-windows-msvc dest="$OUT/librashader.dll" ;;
        x86) triple=i686-pc-windows-msvc dest="$OUT/windows-x86/librashader.dll" ;;
        arm64) triple=aarch64-pc-windows-msvc dest="$OUT/windows-arm64/librashader.dll" ;;
    esac
    rustup target list --installed | tr -d '\r' | grep -qx "$triple" || rustup target add "$triple"
    echo "=== building librashader for $arch (OpenGL runtime) ==="
    # With --target, RUSTFLAGS reach only the library, not build scripts; the
    # cc-built C++ (glslang, SPIRV-Cross) follows crt-static to /MT.
    RUSTFLAGS="-C target-feature=+crt-static" \
        cargo build --release --manifest-path "$WORK/Cargo.toml" -p librashader-capi \
        --no-default-features --features runtime-opengl --target "$triple" --target-dir "$OUT/target"
    # LoadLibrary looks for librashader.dll; cargo names it after the crate.
    mkdir -p "${dest%/*}"
    cp "$OUT/target/$triple/release/librashader_capi.dll" "$dest"
    echo "ok -> $dest"
done
[ "$shaders" = 1 ] || exit 0

SLANG="$SRC/test/shaders_slang"
if [ ! -f "$SLANG/README.md" ]; then
    git -C "$SRC" submodule update --init test/shaders_slang
fi
echo "=== copying the slang-shaders collection ==="
rm -rf "$OUT/shaders"
mkdir -p "$OUT/shaders"
(cd "$SLANG" && tar --exclude=.git -cf - .) | (cd "$OUT/shaders" && tar -xf -)
# game_build.cmake re-copies the collection when this stamp changes.
date > "$OUT/shaders.stamp"
echo "ok -> $OUT/shaders ($(find "$OUT/shaders" -name '*.slangp' | wc -l) presets)"
