# CMake toolchain for the Linux builds (tools/build_linux.sh): zig as the C and
# C++ compiler for SHANTAE_LINUX_ARCH (x86_64 or aarch64) against glibc 2.28,
# with the per-arch sysroot from tools/linux/bootstrap.sh for the headers and
# libraries SDL and the GL link look up.
#
#   -DSHANTAE_LINUX_ARCH=x86_64|aarch64 -DSHANTAE_LINUX_CACHE=<bootstrap cache>
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SHANTAE_LINUX_ARCH SHANTAE_LINUX_CACHE)
if(NOT SHANTAE_LINUX_ARCH OR NOT SHANTAE_LINUX_CACHE)
    message(FATAL_ERROR "toolchain.cmake needs -DSHANTAE_LINUX_ARCH and -DSHANTAE_LINUX_CACHE")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${SHANTAE_LINUX_ARCH})
set(CMAKE_LIBRARY_ARCHITECTURE ${SHANTAE_LINUX_ARCH}-linux-gnu)

set(_shantae_bin "${SHANTAE_LINUX_CACHE}/bin-${SHANTAE_LINUX_ARCH}")
set(_shantae_root "${SHANTAE_LINUX_CACHE}/sysroot-${SHANTAE_LINUX_ARCH}")
set(CMAKE_C_COMPILER "${_shantae_bin}/cc")
set(CMAKE_CXX_COMPILER "${_shantae_bin}/c++")
set(CMAKE_AR "${_shantae_bin}/ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_shantae_bin}/ranlib" CACHE FILEPATH "")
set(PKG_CONFIG_EXECUTABLE "${_shantae_bin}/pkg-config" CACHE FILEPATH "")
# The generated project strips with this after linking (debug info only).
find_program(CMAKE_STRIP NAMES ${SHANTAE_LINUX_ARCH}-linux-gnu-strip llvm-strip)
if(NOT CMAKE_STRIP AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL SHANTAE_LINUX_ARCH)
    find_program(CMAKE_STRIP NAMES strip)
endif()

# No CMAKE_SYSROOT: that would hand zig a libc to use instead of its own
# glibc stubs. The sysroot holds no libc, only the X11/Wayland/audio/GL files.
set(CMAKE_FIND_ROOT_PATH "${_shantae_root}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Link libGL.so.1, which every desktop distro has (GLVND or Mesa), rather than
# GLVND-only libOpenGL.so.0 + libGLX.so.0.
set(OpenGL_GL_PREFERENCE LEGACY)
