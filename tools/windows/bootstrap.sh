#!/usr/bin/env bash
# bootstrap.sh [x86|arm64 ...]: the libraries the 32-bit x86 and ARM64 Windows
# builds (tools/build_windows.sh) link, from pinned msys2 packages, into
# $SHANTAE_WINDOWS_CACHE (default third_party/windows):
#   pkg/                  the downloaded packages, checked against the hashes below
#   sysroot-x86/mingw32   SDL2, ANGLE (libEGL/libGLESv2), zlib, EGL/GLES headers
#   sysroot-arm64/clangarm64
#                         the same, plus the mingw-w64 headers and CRT, libc++,
#                         libunwind, winpthreads and compiler-rt: the whole
#                         target side for msys2's (x86_64-hosted) clang
#   clang-resource-arm64  clang's own headers with the ARM64 compiler-rt
#                         builtins, for clang's -resource-dir
# Nothing is installed into msys2 itself. The compilers are msys2's: mingw32's
# GCC for x86 (pacman -S mingw-w64-i686-gcc) and mingw64's clang and lld for
# ARM64 (pacman -S mingw-w64-x86_64-clang mingw-w64-x86_64-lld).
#
# The x86 ANGLE is older than the x64 and ARM64 one (2.1.r21358 against
# r25748): msys2 stopped building ANGLE for 32-bit, and this is its last i686
# package. The hashes pin packages whose msys2 signatures were checked when they
# were added here.
set -euo pipefail
HERE="$(cd -- "${BASH_SOURCE[0]%/*}" && pwd)"
ROOT="$(cd -- "$HERE/../.." && pwd)"
CACHE="${SHANTAE_WINDOWS_CACHE:-$ROOT/third_party/windows}"
MSYS2="${SHANTAE_MSYS2:-/c/msys64}"
export PATH="$MSYS2/usr/bin:$PATH"

# arch repo package sha256
PACKAGES='
x86 mingw32 mingw-w64-i686-SDL2-2.32.10-1-any.pkg.tar.zst 81b39469a581fd4b1bd1d88785fbb02d5a46c9ef85343a620c8ae2f877ac6c42
x86 mingw32 mingw-w64-i686-angleproject-2.1.r21358.2e285bb5-5-any.pkg.tar.zst 09c7609df3be2b1348049777e8372e35a4bc8b9c4f5703b684966bc2390e0f0c
x86 mingw32 mingw-w64-i686-egl-headers-1.5.r284.3ae2b7c-1-any.pkg.tar.zst bc011e1b220c0f04ad2c11a07cc081b90c35140cb923cf9af3f1a064feff9a9e
x86 mingw32 mingw-w64-i686-gles-headers-3.2.r1065.7fc154c-1-any.pkg.tar.zst 9a28b40ee5f377bb81270967168a65c37bfeaed26bfb96974078fedcb27e724e
x86 mingw32 mingw-w64-i686-zlib-1.3.2-2-any.pkg.tar.zst 03d1f7030adc892eb1ee8434f7201847093ff69b8ad9ff77a650d6dfae72083a
arm64 clangarm64 mingw-w64-clang-aarch64-headers-14.0.0.r220.gd999af622-1-any.pkg.tar.zst 5b1ba3b75c43fa236eaedcc455c4c3f955b4d2b9e43a29bab581f4f40a157368
arm64 clangarm64 mingw-w64-clang-aarch64-crt-14.0.0.r220.gd999af622-1-any.pkg.tar.zst 0a49ac3722ee270987b243c582259c4b48187a09fa4c64950f76c1db2395d081
arm64 clangarm64 mingw-w64-clang-aarch64-winpthreads-14.0.0.r220.gd999af622-1-any.pkg.tar.zst be22e8c441b328ab14584552a6e9a9a1ce4475b1b81a4540e0ed9f224c76d000
arm64 clangarm64 mingw-w64-clang-aarch64-libwinpthread-14.0.0.r220.gd999af622-1-any.pkg.tar.zst fd55313c565c2fa29e5bce34885325fd590acf9297b1abbd952ffea57b52da82
arm64 clangarm64 mingw-w64-clang-aarch64-libc++-22.1.8-1-any.pkg.tar.zst 6755aa5a658d0a906e1e8477858b3518fd87d380ac5c854a226f5a3a2c78d794
arm64 clangarm64 mingw-w64-clang-aarch64-libunwind-22.1.8-1-any.pkg.tar.zst 539ab7e4dd324094e24616167f3c3ff0ec115c618604b6fc79de91ff915fbfb4
arm64 clangarm64 mingw-w64-clang-aarch64-compiler-rt-22.1.8-2-any.pkg.tar.zst 857871083d1fb7f0fb733793306fe0352dc3fde4f0d42834a95588d81e3b593d
arm64 clangarm64 mingw-w64-clang-aarch64-SDL2-2.32.10-1-any.pkg.tar.zst e21d2dd5879b3f5ec95aa86afb89aaa7cb983262c19ba519f346ad1e989bbc75
arm64 clangarm64 mingw-w64-clang-aarch64-angleproject-2.1.r25748.890b5d8f-3-any.pkg.tar.zst 27eb479ffaa2958e771999f31387beced9147c9c117608a24fa43a7a7d5e0527
arm64 clangarm64 mingw-w64-clang-aarch64-egl-headers-1.5.r284.3ae2b7c-1-any.pkg.tar.zst 4570b61ae66789772e9f9fe9164d8870006c6bd6825f07340e376911d54f14f0
arm64 clangarm64 mingw-w64-clang-aarch64-gles-headers-3.2.r1065.7fc154c-1-any.pkg.tar.zst da7bfb6d445a3dc7d37e81eb2d01683c12716730436ca384fd48371cefff9f3b
arm64 clangarm64 mingw-w64-clang-aarch64-zlib-1.3.2-2-any.pkg.tar.zst 28c111cc05d923ac56faa33aa5d7618c9dc16ac2d7c9e43a87f875403517894a
'

arches=("$@")
[ ${#arches[@]} -gt 0 ] || arches=(x86 arm64)
mkdir -p "$CACHE/pkg"

fetch() {  # fetch <repo> <file> <sha256>
    local out="$CACHE/pkg/$2"
    if [ ! -f "$out" ] || [ "$(sha256sum "$out" | cut -d' ' -f1)" != "$3" ]; then
        echo "fetching $2"
        curl -fsSL --retry 3 -o "$out.part" "https://repo.msys2.org/mingw/$1/$2"
        [ "$(sha256sum "$out.part" | cut -d' ' -f1)" = "$3" ] || { echo "$2: checksum mismatch"; exit 1; }
        mv "$out.part" "$out"
    fi
}

for arch in "${arches[@]}"; do
    case "$arch" in
        x86) compiler="$MSYS2/mingw32/bin/gcc.exe" ;;
        arm64) compiler="$MSYS2/mingw64/bin/clang.exe" ;;
        *) echo "usage: $0 [x86|arm64 ...]"; exit 2 ;;
    esac
    [ -x "$compiler" ] || { echo "no $compiler (see the top of $0)"; exit 1; }
    list=$(awk -v a="$arch" '$1 == a' <<< "$PACKAGES")
    root="$CACHE/sysroot-$arch"
    stamp=$(sha256sum <<< "$list" | cut -d' ' -f1)
    if [ "$(cat "$root/.stamp" 2> /dev/null)" != "$stamp" ]; then
        rm -rf "$root"
        mkdir -p "$root"
        while read -r _ repo file sum; do
            fetch "$repo" "$file" "$sum"
            tar --zstd -xf "$CACHE/pkg/$file" -C "$root" \
                --exclude=.PKGINFO --exclude=.BUILDINFO --exclude=.MTREE --exclude=.INSTALL
        done <<< "$list"
        echo "$stamp" > "$root/.stamp"
        echo "sysroot-$arch ready"
    fi
    if [ "$arch" = arm64 ]; then
        # clang looks for compiler-rt in its resource dir, whose headers must be
        # the running clang's own: copy them beside the ARM64 builtins, again
        # whenever msys2 updates clang.
        res="$CACHE/clang-resource-arm64"
        src="$("$compiler" -print-resource-dir | tr -d '\r')"
        rstamp="$("$compiler" --version | head -1) $stamp"
        if [ "$(cat "$res/.stamp" 2> /dev/null)" != "$rstamp" ]; then
            rm -rf "$res"
            mkdir -p "$res/lib"
            cp -r "$src/include" "$res/"
            cp -r "$root"/clangarm64/lib/clang/*/lib/windows "$res/lib/"
            echo "$rstamp" > "$res/.stamp"
            echo "clang-resource-arm64 ready"
        fi
    fi
done
