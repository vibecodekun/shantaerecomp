#!/usr/bin/env bash
# package.sh <arch> <build dir> <version>: the release zip for one arch from a
# finished tools/build_windows.sh build, into dist/windows/:
#   shantae-recomp-<version>-windows-<arch>.zip   a folder to extract anywhere
set -euo pipefail
HERE="$(cd -- "${BASH_SOURCE[0]%/*}" && pwd)"
ROOT="$(cd -- "$HERE/../.." && pwd)"
MSYS2="${SHANTAE_MSYS2:-/c/msys64}"
arch=$1 build=$2 version=$3
dist="$ROOT/dist/windows"
name="shantae-recomp-$version-windows-$arch"
stage_root="${SHANTAE_WINDOWS_CACHE:-$ROOT/third_party/windows}/stage"
mkdir -p "$dist" "$stage_root"

# What the game needs beside it; nothing a run creates (settings, saves,
# rom.cfg, shader_cache/, cheats/, logs) goes in, nor the build's check tools.
stage="$stage_root/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp "$build/shantae.exe" "$stage/"
list=$(bash "$HERE/dlls.sh" "$build")
mapfile -t dlls <<< "$list"
cp "${dlls[@]}" "$stage/"
cp -r "$build/assets" "$stage/"
[ -d "$build/shaders" ] && cp -r "$build/shaders" "$stage/"
cp "$HERE/README-windows.txt" "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-LICENSES.md" "$stage/"
rm -f "$dist/$name.zip"
"$MSYS2/usr/bin/bsdtar" --format zip -cf "$dist/$name.zip.part" -C "$stage_root" "$name"
mv "$dist/$name.zip.part" "$dist/$name.zip"
rm -rf "$stage"
echo "=== $dist/$name.zip ($(du -h "$dist/$name.zip" | cut -f1))"
