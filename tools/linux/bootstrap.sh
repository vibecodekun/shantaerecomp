#!/usr/bin/env bash
# Fetch and build what tools/build_linux.sh needs, once, into
# $SHANTAE_LINUX_CACHE (default ~/.cache/shantae-linux). Nothing needs root.
#
#   zig            C/C++ compiler for x86_64 and aarch64 Linux against glibc
#                  $GLIBC (its own glibc stubs and a static libc++, so the
#                  game runs on any distro with that glibc or newer)
#   cmake, ninja
#   sysroot-<arch> Ubuntu 24.04 X11/Wayland/audio/GL headers and libraries,
#                  fetched with an unprivileged apt. SDL's build reads them;
#                  the game only loads the libraries at run time.
#   sdl2-<arch>    SDL2, static, with every backend loaded at run time
#   rust           rustup + cargo-zigbuild, for librashader (bootstrap.sh rust)
#   buster-<arch>  Debian 10's glibc 2.28 and GL loader, to run the result on
#                  the oldest supported userland (bootstrap.sh buster)
#
# Usage: bootstrap.sh [base|rust|buster|appimage|all]   (default: base)
# Every step is skipped when its stamp is current.
set -euo pipefail

CACHE="${SHANTAE_LINUX_CACHE:-$HOME/.cache/shantae-linux}"
GLIBC=2.28
ZIG_VERSION=0.14.1
CMAKE_VERSION=3.31.8
NINJA_VERSION=1.12.1
SDL2_VERSION=2.32.10
UBUNTU_SUITE=noble
ARCHES="x86_64 aarch64"

HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
    x86_64|aarch64) ;;
    *) echo "unsupported build host $HOST_ARCH (x86_64 or aarch64 Linux)"; exit 1 ;;
esac
command -v apt-get > /dev/null || { echo "needs apt-get (Debian/Ubuntu build host, e.g. WSL Ubuntu)"; exit 1; }

DL="$CACHE/dl"
TOOLS="$CACHE/tools"
mkdir -p "$DL" "$TOOLS"

log() { printf '=== %s\n' "$*"; }

# deb architecture and GNU triple for an arch
deb_arch() { case "$1" in x86_64) echo amd64 ;; aarch64) echo arm64 ;; esac; }
triple() { echo "$1-linux-gnu"; }

# fetch <url> <file> [sha256]: download once, verify when a hash is given
fetch() {
    local url=$1 out="$DL/$2" sum=${3:-}
    if [ ! -s "$out" ]; then
        log "downloading $url"
        curl -fsSL --retry 3 -o "$out.part" "$url"
        mv "$out.part" "$out"
    fi
    if [ -n "$sum" ] && ! echo "$sum  $out" | sha256sum -c --quiet -; then
        echo "checksum mismatch for $out (expected $sum); delete it to download again"
        exit 1
    fi
}

stamp_ok() { [ "$(cat "$1" 2> /dev/null)" = "$2" ]; }

# ---- zig, cmake, ninja -------------------------------------------------------
tools_step() {
    local zig_dir="$TOOLS/zig-$ZIG_VERSION"
    if [ ! -x "$zig_dir/zig" ]; then
        fetch https://ziglang.org/download/index.json zig-index.json
        local url sum
        read -r url sum < <(python3 - "$DL/zig-index.json" "$ZIG_VERSION" "$HOST_ARCH-linux" << 'EOF'
import json, sys
entry = json.load(open(sys.argv[1]))[sys.argv[2]][sys.argv[3]]
print(entry["tarball"], entry["shasum"])
EOF
)
        fetch "$url" "zig-$ZIG_VERSION-$HOST_ARCH.tar.xz" "$sum"
        rm -rf "$zig_dir" "$zig_dir.tmp"
        mkdir -p "$zig_dir.tmp"
        tar -xJf "$DL/zig-$ZIG_VERSION-$HOST_ARCH.tar.xz" -C "$zig_dir.tmp" --strip-components=1
        mv "$zig_dir.tmp" "$zig_dir"
    fi
    local cmake_dir="$TOOLS/cmake-$CMAKE_VERSION"
    if [ ! -x "$cmake_dir/bin/cmake" ]; then
        local base="https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION"
        local name="cmake-$CMAKE_VERSION-linux-$HOST_ARCH.tar.gz"
        fetch "$base/cmake-$CMAKE_VERSION-SHA-256.txt" "cmake-$CMAKE_VERSION-SHA-256.txt"
        fetch "$base/$name" "$name" "$(awk -v n="$name" '$2 == n {print $1}' "$DL/cmake-$CMAKE_VERSION-SHA-256.txt")"
        rm -rf "$cmake_dir" "$cmake_dir.tmp"
        mkdir -p "$cmake_dir.tmp"
        tar -xzf "$DL/$name" -C "$cmake_dir.tmp" --strip-components=1
        mv "$cmake_dir.tmp" "$cmake_dir"
    fi
    if [ ! -x "$TOOLS/ninja-$NINJA_VERSION/ninja" ]; then
        local name=ninja-linux.zip
        [ "$HOST_ARCH" = aarch64 ] && name=ninja-linux-aarch64.zip
        fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/$name" "ninja-$NINJA_VERSION-$name"
        mkdir -p "$TOOLS/ninja-$NINJA_VERSION"
        python3 -m zipfile -e "$DL/ninja-$NINJA_VERSION-$name" "$TOOLS/ninja-$NINJA_VERSION"
        chmod +x "$TOOLS/ninja-$NINJA_VERSION/ninja"
    fi
    ln -sfn "zig-$ZIG_VERSION" "$TOOLS/zig"
    ln -sfn "cmake-$CMAKE_VERSION" "$TOOLS/cmake"
    ln -sfn "ninja-$NINJA_VERSION" "$TOOLS/ninja"
}

# ---- unprivileged apt ----------------------------------------------------------
# apt_download <state dir> <deb arch> <keyring> <"deb lines"> pkg...
# Downloads the named packages (not their dependencies) into <dir>/archives.
apt_download() {
    local dir=$1 arch=$2 keyring=$3 sources=$4
    shift 4
    mkdir -p "$dir/lists/partial" "$dir/archives/partial" "$dir/empty"
    : > "$dir/status"
    printf '%s\n' "$sources" | sed "s|^deb |deb [arch=$arch signed-by=$keyring] |" > "$dir/sources.list"
    local opts=(
        -o Dir::Etc::SourceList="$dir/sources.list" -o Dir::Etc::SourceParts="$dir/empty"
        -o Dir::Etc::Preferences=/nonexistent -o Dir::Etc::PreferencesParts="$dir/empty"
        -o Dir::State::Lists="$dir/lists" -o Dir::State::Status="$dir/status"
        -o Dir::Cache="$dir" -o Dir::Cache::Archives="$dir/archives"
        -o APT::Architecture="$arch" -o APT::Architectures="$arch"
        -o Debug::NoLocking=1 -o APT::Sandbox::User="$(id -un)"
        -o Acquire::Languages=none -o Acquire::Check-Valid-Until=false
    )
    apt-get "${opts[@]}" -qq update
    (cd "$dir/archives" && apt-get "${opts[@]}" -qq download "$@")
}

ubuntu_sources() {
    local mirror=http://archive.ubuntu.com/ubuntu
    [ "$1" = arm64 ] && mirror=http://ports.ubuntu.com/ubuntu-ports
    local s
    for s in "$UBUNTU_SUITE" "$UBUNTU_SUITE-updates" "$UBUNTU_SUITE-security"; do
        echo "deb $mirror $s main universe"
    done
}

# Headers SDL builds against, and the libraries whose sonames it records.
SYSROOT_PKGS=(
    libx11-dev x11proto-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxrender-dev
    libxss-dev libxfixes-dev libxcb1-dev libxau-dev libxdmcp-dev libxkbcommon-dev
    libwayland-dev wayland-protocols libdecor-0-dev libffi-dev
    libasound2-dev libpulse-dev libpipewire-0.3-dev libspa-0.2-dev libdbus-1-dev libudev-dev
    libdrm-dev libgbm-dev libgl-dev libegl-dev libgles-dev libglx-dev libglvnd-dev
    libx11-6 libxext6 libxcursor1 libxi6 libxrandr2 libxrender1 libxss1 libxfixes3 libxcb1
    libxau6 libxdmcp6 libbsd0 libmd0 libxkbcommon0 libwayland-client0 libwayland-cursor0
    libwayland-egl1 libwayland-server0 libffi8 libdecor-0-0 libasound2t64 libpulse0 libpipewire-0.3-0t64
    libdbus-1-3 libudev1 libdrm2 libgbm1 libgl1 libglvnd0 libglx0 libegl1 libgles2 libopengl0
)
# Build-host programs SDL's configure runs.
HOSTTOOL_PKGS=(pkgconf-bin libpkgconf3 libwayland-bin)
SYSROOT_STAMP="$UBUNTU_SUITE ${SYSROOT_PKGS[*]}"

sysroot_step() {
    local arch=$1 da
    da=$(deb_arch "$arch")
    local root="$CACHE/sysroot-$arch"
    stamp_ok "$root/.stamp" "$SYSROOT_STAMP" && return
    log "sysroot for $arch (Ubuntu $UBUNTU_SUITE $da)"
    local state="$CACHE/apt-ubuntu-$da"
    rm -rf "$state/archives"
    apt_download "$state" "$da" /usr/share/keyrings/ubuntu-archive-keyring.gpg \
        "$(ubuntu_sources "$da")" "${SYSROOT_PKGS[@]}"
    rm -rf "$root"
    mkdir -p "$root"
    extract_debs "$state/archives" "$root"
    rm -rf "$state"   # the package lists alone are ~200 MB
    echo "$SYSROOT_STAMP" > "$root/.stamp"
}

# extract_debs <dir of .debs> <root>: unpack them all into <root>, with
# absolute symlinks (libfoo.so -> /lib/..., ld.so) pointing inside <root>.
extract_debs() {
    local deb link target
    for deb in "$1"/*.deb; do dpkg-deb -x "$deb" "$2"; done
    find "$2" -type l | while read -r link; do
        target=$(readlink "$link")
        case "$target" in /*) ln -sfn "$2$target" "$link" ;; esac
    done
}

hosttools_step() {
    local ht="$CACHE/hosttools" da
    da=$(deb_arch "$HOST_ARCH")
    stamp_ok "$ht/.stamp" "$UBUNTU_SUITE ${HOSTTOOL_PKGS[*]}" && return
    log "build-host tools (pkg-config, wayland-scanner)"
    local state="$CACHE/apt-ubuntu-$da-host"
    rm -rf "$state/archives"
    apt_download "$state" "$da" /usr/share/keyrings/ubuntu-archive-keyring.gpg \
        "$(ubuntu_sources "$da")" "${HOSTTOOL_PKGS[@]}"
    rm -rf "$ht"
    mkdir -p "$ht"
    extract_debs "$state/archives" "$ht"
    rm -rf "$state"
    echo "$UBUNTU_SUITE ${HOSTTOOL_PKGS[*]}" > "$ht/.stamp"
}

# ---- per-arch wrappers ---------------------------------------------------------
# zig cc/c++/ar/ranlib and pkg-config for one target, in $CACHE/bin-<arch>.
wrappers_step() {
    local arch=$1 t
    t=$(triple "$arch")
    local bin="$CACHE/bin-$arch" root="$CACHE/sysroot-$arch" ht="$CACHE/hosttools"
    local hl
    hl="$ht/usr/lib/$(triple "$HOST_ARCH")"
    mkdir -p "$bin"
    # Same char signedness as x86 on every target (aarch64's plain char is
    # unsigned); UBSan off (zig turns it on for unoptimized C, e.g. configure
    # checks); zig's caches kept inside $CACHE.
    local common="export ZIG_GLOBAL_CACHE_DIR=\"$CACHE/zig-cache\" ZIG_LOCAL_CACHE_DIR=\"$CACHE/zig-cache\""
    cat > "$bin/cc" << EOF
#!/bin/sh
$common
exec "$TOOLS/zig/zig" cc -target $arch-linux-gnu.$GLIBC -fsigned-char -fno-sanitize=undefined "\$@"
EOF
    cat > "$bin/c++" << EOF
#!/bin/sh
$common
exec "$TOOLS/zig/zig" c++ -target $arch-linux-gnu.$GLIBC -fsigned-char -fno-sanitize=undefined "\$@"
EOF
    cat > "$bin/ar" << EOF
#!/bin/sh
exec "$TOOLS/zig/zig" ar "\$@"
EOF
    cat > "$bin/ranlib" << EOF
#!/bin/sh
exec "$TOOLS/zig/zig" ranlib "\$@"
EOF
    cat > "$bin/pkg-config" << EOF
#!/bin/sh
unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR="$root/usr/lib/$t/pkgconfig:$root/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$root"
export LD_LIBRARY_PATH="$hl\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "$ht/usr/bin/pkgconf" "\$@"
EOF
    cat > "$bin/wayland-scanner" << EOF
#!/bin/sh
export LD_LIBRARY_PATH="$hl\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "$ht/usr/bin/wayland-scanner" "\$@"
EOF
    chmod +x "$bin"/*
}

# ---- SDL2 ------------------------------------------------------------------------
SDL2_OPTIONS=(
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL2_DISABLE_SDL2MAIN=ON
    -DSDL_X11=ON -DSDL_X11_SHARED=ON
    -DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR=ON -DSDL_WAYLAND_LIBDECOR_SHARED=ON
    -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON
    -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF
    -DSDL_PIPEWIRE=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON
    -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF
    -DSDL_NAS=OFF -DSDL_FUSIONSOUND=OFF -DSDL_LIBSAMPLERATE=OFF
    -DSDL_DBUS=ON -DSDL_IBUS=OFF -DSDL_LIBUDEV=ON -DSDL_HIDAPI=ON -DSDL_HIDAPI_LIBUSB=OFF
    -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_RPATH=OFF
)
SDL2_STAMP="$SDL2_VERSION zig-$ZIG_VERSION glibc-$GLIBC ${SDL2_OPTIONS[*]} / $SYSROOT_STAMP"

sdl2_step() {
    local arch=$1
    local prefix="$CACHE/sdl2-$arch"
    stamp_ok "$prefix/.stamp" "$SDL2_STAMP" && return
    local name="SDL2-$SDL2_VERSION.tar.gz"
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/$name" "$name"
    local src="$CACHE/src/SDL2-$SDL2_VERSION"
    if [ ! -f "$src/CMakeLists.txt" ]; then
        mkdir -p "$CACHE/src"
        tar -xzf "$DL/$name" -C "$CACHE/src"
    fi
    log "SDL2 $SDL2_VERSION for $arch"
    local build="$CACHE/build-sdl2-$arch"
    rm -rf "$build" "$prefix"
    PATH="$CACHE/bin-$arch:$PATH" "$TOOLS/cmake/bin/cmake" -G Ninja -S "$src" -B "$build" \
        -DCMAKE_MAKE_PROGRAM="$TOOLS/ninja/ninja" \
        -DCMAKE_TOOLCHAIN_FILE="$(dirname "${BASH_SOURCE[0]}")/toolchain.cmake" \
        -DSHANTAE_LINUX_ARCH="$arch" -DSHANTAE_LINUX_CACHE="$CACHE" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        "${SDL2_OPTIONS[@]}" > "$CACHE/sdl2-$arch-configure.log" 2>&1 \
        || { tail -30 "$CACHE/sdl2-$arch-configure.log"; exit 1; }
    PATH="$CACHE/bin-$arch:$PATH" "$TOOLS/ninja/ninja" -C "$build" install > "$CACHE/sdl2-$arch-build.log" 2>&1 \
        || { grep -E "error|FAILED" "$CACHE/sdl2-$arch-build.log" | head -30; exit 1; }
    echo "$SDL2_STAMP" > "$prefix/.stamp"
}

base() {
    tools_step
    hosttools_step
    local arch
    for arch in $ARCHES; do
        sysroot_step "$arch"
        wrappers_step "$arch"
        sdl2_step "$arch"
    done
    log "base toolchain ready in $CACHE"
}

# ---- Rust, for librashader -------------------------------------------------------------
# rustup and cargo stay inside $CACHE/rust; cargo-zigbuild links with zig, so
# librashader.so gets the same glibc floor as the game.
RUST_TARGETS="x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu"
rust() {
    local r="$CACHE/rust"
    export RUSTUP_HOME="$r/rustup" CARGO_HOME="$r/cargo"
    if [ ! -x "$CARGO_HOME/bin/rustup" ]; then
        log "installing Rust (rustup, stable) into $r"
        fetch https://sh.rustup.rs rustup-init.sh
        sh "$DL/rustup-init.sh" -y --no-modify-path --profile minimal --default-toolchain stable
    fi
    # shellcheck disable=SC2086
    "$CARGO_HOME/bin/rustup" target add $RUST_TARGETS > /dev/null
    if [ ! -x "$CARGO_HOME/bin/cargo-zigbuild" ]; then
        log "installing cargo-zigbuild"
        "$CARGO_HOME/bin/cargo" install --locked cargo-zigbuild
    fi
    local stamp
    stamp="$("$CARGO_HOME/bin/rustc" --version) / $("$CARGO_HOME/bin/cargo-zigbuild" --version) / zig-$ZIG_VERSION"
    echo "$stamp" > "$r/.stamp"
}

# ---- Debian 10, the oldest userland the builds target ----------------------------------
# glibc 2.28 and the GL loader (the game links libGL.so.1), for running the
# result on it: ld.so from here on x86_64, qemu with QEMU_LD_PREFIX on aarch64.
BUSTER_PKGS=(libc6 libgl1 libglvnd0 libglx0 libx11-6 libxext6 libxcb1 libxau6 libxdmcp6 libbsd0)
buster() {
    local keys="$CACHE/debian-keyring"
    if [ ! -s "$keys/debian.gpg" ]; then
        log "Debian archive keyring (from Ubuntu's debian-archive-keyring)"
        local da
        da=$(deb_arch "$HOST_ARCH")
        apt_download "$CACHE/apt-keyring" "$da" /usr/share/keyrings/ubuntu-archive-keyring.gpg \
            "$(ubuntu_sources "$da")" debian-archive-keyring
        rm -rf "$keys"
        mkdir -p "$keys/x"
        dpkg-deb -x "$CACHE"/apt-keyring/archives/debian-archive-keyring_*.deb "$keys/x"
        # Buster's keys may have moved to the removed-keys ring by now.
        cat "$keys"/x/usr/share/keyrings/debian-archive-keyring.gpg \
            "$keys"/x/usr/share/keyrings/debian-archive-removed-keys.gpg > "$keys/debian.gpg"
        rm -rf "$CACHE/apt-keyring" "$keys/x"
    fi
    local arch
    for arch in $ARCHES; do
        local root="$CACHE/buster-$arch" da
        da=$(deb_arch "$arch")
        stamp_ok "$root/.stamp" "${BUSTER_PKGS[*]} links-fixed" && continue
        log "Debian 10 runtime for $arch"
        local state="$CACHE/apt-buster-$da"
        apt_download "$state" "$da" "$keys/debian.gpg" "deb http://archive.debian.org/debian buster main" \
            "${BUSTER_PKGS[@]}"
        rm -rf "$root"
        mkdir -p "$root"
        extract_debs "$state/archives" "$root"
        rm -rf "$state"
        echo "${BUSTER_PKGS[*]} links-fixed" > "$root/.stamp"
    done
}

# ---- AppImage tooling ------------------------------------------------------------------
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/continuous
appimage() {
    local a="$CACHE/appimage"
    mkdir -p "$a"
    if [ ! -x "$a/appimagetool/AppRun" ]; then
        log "appimagetool"
        fetch "$APPIMAGETOOL_URL/appimagetool-$HOST_ARCH.AppImage" "appimagetool-$HOST_ARCH.AppImage"
        chmod +x "$DL/appimagetool-$HOST_ARCH.AppImage"
        # Extracted, so it needs no FUSE (WSL has none by default).
        rm -rf "$a/appimagetool" "$a/squashfs-root"
        (cd "$a" && "$DL/appimagetool-$HOST_ARCH.AppImage" --appimage-extract > /dev/null)
        mv "$a/squashfs-root" "$a/appimagetool"
    fi
    local arch
    for arch in $ARCHES; do
        if [ ! -s "$a/runtime-$arch" ]; then
            fetch "$RUNTIME_URL/runtime-$arch" "appimage-runtime-$arch"
            cp "$DL/appimage-runtime-$arch" "$a/runtime-$arch"
        fi
    done
}

case "${1:-base}" in
    base) base ;;
    rust) rust ;;
    buster) buster ;;
    appimage) appimage ;;
    all) base; rust; buster; appimage ;;
    *) echo "usage: $0 [base|rust|buster|appimage|all]"; exit 2 ;;
esac
