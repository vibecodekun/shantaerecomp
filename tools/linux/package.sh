#!/usr/bin/env bash
# package.sh <arch> <build dir> <version>: the release files for one arch from
# a finished tools/build_linux.sh build, into dist/linux/:
#   shantae-recomp-<version>-linux-<arch>.tar.gz   a folder to extract anywhere
#   Shantae_Recomp-<version>-<arch>.AppImage       one file; state beside it
set -euo pipefail
HERE="$(cd -- "${BASH_SOURCE[0]%/*}" && pwd)"
ROOT="$(cd -- "$HERE/../.." && pwd)"
CACHE="${SHANTAE_LINUX_CACHE:-$HOME/.cache/shantae-linux}"
arch=$1 build=$2 version=$3
dist="$ROOT/dist/linux"
name="shantae-recomp-$version-linux-$arch"
mkdir -p "$dist" "$CACHE/stage"

# What the game needs beside it; nothing a run creates (settings, saves,
# rom.cfg, shader_cache/, cheats/) goes in.
stage="$CACHE/stage/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp "$build/shantae" "$stage/"
[ -f "$build/librashader.so" ] && cp "$build/librashader.so" "$stage/"
cp -r "$build/assets" "$stage/"
[ -d "$build/shaders" ] && cp -r "$build/shaders" "$stage/"
cp "$HERE/README-linux.txt" "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-LICENSES.md" "$stage/"
tar -C "$CACHE/stage" -czf "$dist/$name.tar.gz.part" "$name"
mv "$dist/$name.tar.gz.part" "$dist/$name.tar.gz"
echo "=== $dist/$name.tar.gz ($(du -h "$dist/$name.tar.gz" | cut -f1))"

bash "$HERE/bootstrap.sh" appimage > /dev/null
appdir="$CACHE/stage/AppDir-$arch"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin"
cp -r "$stage"/. "$appdir/usr/bin/"
rm "$appdir/usr/bin/README-linux.txt"
cp "$HERE/AppRun" "$HERE/shantae.desktop" "$HERE/shantae.png" "$appdir/"
chmod +x "$appdir/AppRun"
ln -s shantae.png "$appdir/.DirIcon"
image="$dist/Shantae_Recomp-$version-$arch.AppImage"
ARCH="$arch" "$CACHE/appimage/appimagetool/AppRun" --no-appstream \
    --runtime-file "$CACHE/appimage/runtime-$arch" "$appdir" "$image.part" > "$CACHE/appimage-$arch.log" 2>&1 \
    || { tail -20 "$CACHE/appimage-$arch.log"; exit 1; }
mv "$image.part" "$image"
chmod +x "$image"
echo "=== $image ($(du -h "$image" | cut -f1))"
