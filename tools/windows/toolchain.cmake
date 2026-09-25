# CMake toolchain for the 32-bit x86 and ARM64 Windows builds
# (tools/build_windows.sh; the x64 build is tools/build.sh's, without one):
#   x86    msys2 mingw32's GCC, the same compiler family and runtime as x64
#   arm64  msys2 mingw64's clang and lld cross-compiling for aarch64-w64-mingw32
#          (msys2's own ARM64 compilers only run on ARM64 Windows)
# SDL2, ANGLE and the ARM64 runtime come from the pinned packages that
# tools/windows/bootstrap.sh unpacks into the cache.
#
#   -DSHANTAE_WINDOWS_ARCH=x86|arm64 -DSHANTAE_WINDOWS_CACHE=<bootstrap cache>
#   -DSHANTAE_MSYS2=<msys2 root, C:/msys64>
# The cache path goes into compiler flags, so it must not contain spaces.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SHANTAE_WINDOWS_ARCH SHANTAE_WINDOWS_CACHE SHANTAE_MSYS2)
if(NOT SHANTAE_WINDOWS_ARCH OR NOT SHANTAE_WINDOWS_CACHE OR NOT SHANTAE_MSYS2)
    message(FATAL_ERROR "toolchain.cmake needs -DSHANTAE_WINDOWS_ARCH, -DSHANTAE_WINDOWS_CACHE and -DSHANTAE_MSYS2")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(_shantae_root "${SHANTAE_WINDOWS_CACHE}/sysroot-${SHANTAE_WINDOWS_ARCH}")
if(SHANTAE_WINDOWS_ARCH STREQUAL "x86")
    set(CMAKE_SYSTEM_PROCESSOR X86)
    set(_shantae_prefix "${_shantae_root}/mingw32")
    set(CMAKE_C_COMPILER "${SHANTAE_MSYS2}/mingw32/bin/gcc.exe")
    set(CMAKE_CXX_COMPILER "${SHANTAE_MSYS2}/mingw32/bin/g++.exe")
    # SSE2 floating point, as on x64 (x87 rounds differently); Windows 8 and
    # later need SSE2 anyway. The sysroot adds SDL2, ANGLE and the GL headers
    # to the compiler's own.
    set(_shantae_flags "-msse2 -mfpmath=sse -isystem ${_shantae_prefix}/include")
    set(CMAKE_C_FLAGS_INIT "${_shantae_flags}")
    set(CMAKE_CXX_FLAGS_INIT "${_shantae_flags}")
    # Up to 4 GB of address space on 64-bit Windows instead of 2 GB, for the
    # largest expanded views.
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${_shantae_prefix}/lib -Wl,--large-address-aware")
elseif(SHANTAE_WINDOWS_ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR ARM64)
    set(_shantae_prefix "${_shantae_root}/clangarm64")
    set(_shantae_bin "${SHANTAE_MSYS2}/mingw64/bin")
    set(CMAKE_C_COMPILER "${_shantae_bin}/clang.exe")
    set(CMAKE_CXX_COMPILER "${_shantae_bin}/clang++.exe")
    set(CMAKE_C_COMPILER_TARGET aarch64-w64-mingw32)
    set(CMAKE_CXX_COMPILER_TARGET aarch64-w64-mingw32)
    set(CMAKE_SYSROOT "${_shantae_prefix}")
    # mingw64's clang defaults to GCC's runtime and linker; the ARM64 sysroot
    # is LLVM's (as in msys2's clangarm64). Plain char is signed, as on x86.
    set(_shantae_flags "-resource-dir=${SHANTAE_WINDOWS_CACHE}/clang-resource-arm64 -fsigned-char")
    set(CMAKE_C_FLAGS_INIT "${_shantae_flags}")
    set(CMAKE_CXX_FLAGS_INIT "${_shantae_flags} -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -rtlib=compiler-rt -unwindlib=libunwind -stdlib=libc++")
    set(CMAKE_AR "${_shantae_bin}/llvm-ar.exe" CACHE FILEPATH "")
    set(CMAKE_RANLIB "${_shantae_bin}/llvm-ranlib.exe" CACHE FILEPATH "")
    # The generated project strips with this after linking (debug info only).
    set(CMAKE_STRIP "${_shantae_bin}/llvm-strip.exe" CACHE FILEPATH "")
    set(CMAKE_RC_COMPILER "${_shantae_bin}/llvm-windres.exe" CACHE FILEPATH "")
else()
    message(FATAL_ERROR "SHANTAE_WINDOWS_ARCH must be x86 or arm64, not ${SHANTAE_WINDOWS_ARCH}")
endif()

set(CMAKE_FIND_ROOT_PATH "${_shantae_prefix}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
