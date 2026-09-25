# Third-Party Components and Licenses

Shantae Recomp is built from the gbrecompiled recompiler and runtime and the
recomp-ui launcher. The components below are compiled into `shantae.exe` /
`shantae`, or ship beside it as DLLs, `librashader.so`, `assets/` and
`shaders/`. Each keeps its own license, separate from the project's PolyForm
Noncommercial license (`LICENSE`).

## Engine and launcher

| Component | Role | License | Source |
|---|---|---|---|
| **gbrecompiled** (fork of arcanite24/gb-recompiled) | Recompiler and the runtime compiled into the game (CPU glue, PPU, APU, timers, frontend) | gb-recompiled: MIT; the fork states no license of its own | <https://github.com/mstan/gbrecompiled>, this build: <https://github.com/vibecodekun/gbrecompiled/tree/shantae> |
| **recomp-ui**, Copyright (c) 2026 Matthew Stanley | Pre-boot launcher and in-game menu | MIT | <https://github.com/RetroPortingToolKit/recomp-ui> |
| **Dear ImGui**, Copyright (c) 2014-2024 Omar Cornut | Launcher and menu UI | MIT | <https://github.com/ocornut/imgui> |
| **stb_image / stb_image_write / stb_truetype** | Image and font loading, screenshots | Public domain or MIT | <https://github.com/nothings/stb> |
| **tinyfiledialogs** | Native ROM file picker | zlib | <https://sourceforge.net/projects/tinyfiledialogs/> |

## Libraries

| Component | Where | License | Source |
|---|---|---|---|
| **SDL2** 2.32 | `SDL2.dll` (Windows); linked in (Linux) | zlib | <https://libsdl.org> |
| **ANGLE** | `libEGL.dll`, `libGLESv2.dll` (Windows) | BSD-3-Clause | <https://chromium.googlesource.com/angle/angle> |
| **librashader** 0.12.0, with the patches in `third_party/librashader/patches/` | `librashader.dll` / `librashader.so` | MPL-2.0 (C headers: MIT) | <https://github.com/SnowflakePowered/librashader> |
| **slang-shaders** | `shaders/` | Per shader, stated in each file (mostly GPL-2.0-or-later, MIT or public domain) | <https://github.com/libretro/slang-shaders> |
| **zlib** 1.3 | `zlib1.dll` (Windows) | zlib | <https://zlib.net> |
| **GCC runtime** (`libgcc_s_*.dll`, `libstdc++-6.dll`) | Windows x64 and x86 | GPL-3.0 with the GCC Runtime Library Exception | <https://gcc.gnu.org> |
| **mingw-w64 winpthreads** (`libwinpthread-1.dll`) | Windows | MIT and others (see its COPYING) | <https://www.mingw-w64.org> |
| **LLVM libc++, libunwind, compiler-rt** | `libc++.dll` (Windows ARM64); linked in (Linux) | Apache-2.0 WITH LLVM-exception | <https://llvm.org> |
| **AppImage runtime** | The `.AppImage` files | MIT | <https://github.com/AppImage/type2-runtime> |

## Fonts and images

| Component | License | Source |
|---|---|---|
| **Lato** (`assets/fonts/LatoLatin-*.ttf`) | SIL Open Font License 1.1 | <https://www.latofonts.com> |
| **Noto Sans Symbols 2** (`assets/fonts/NotoSansSymbols2-Regular.ttf`), Copyright (c) The Noto Project Authors | SIL Open Font License 1.1 | <https://github.com/notofonts/symbols> |
| **OpenMoji** (`assets/fonts/OpenMoji-black-glyf.ttf`), Copyright (c) OpenMoji Project | CC BY-SA 4.0 | <https://openmoji.org> |
| **Country flags** (`assets/img/flags.png`), rendered from Noto Color Emoji, Copyright (c) The Noto Project Authors | SIL Open Font License 1.1 | <https://github.com/googlefonts/noto-emoji> |

Full license texts:

- PolyForm Noncommercial 1.0.0: <https://polyformproject.org/licenses/noncommercial/1.0.0>
- MIT: <https://opensource.org/license/mit>
- zlib: <https://zlib.net/zlib_license.html>
- BSD-3-Clause: <https://opensource.org/license/bsd-3-clause>
- MPL-2.0: <https://www.mozilla.org/MPL/2.0/>
- GPL-3.0 and the GCC Runtime Library Exception: <https://www.gnu.org/licenses/gcc-exception-3.1.html>
- Apache-2.0 WITH LLVM-exception: <https://llvm.org/LICENSE.txt>
- SIL Open Font License 1.1: <https://openfontlicense.org>
- CC BY-SA 4.0: <https://creativecommons.org/licenses/by-sa/4.0/>

## The game

Shantae is a trademark of WayForward Technologies. The Game Boy Color game was
developed by WayForward and published by Capcom in 2002. This release contains
no ROM: you supply your own copy. The executable holds a machine translation of
the game's SM83 code, and the licenses above cannot grant any rights to that
code. This project is not affiliated with or endorsed by WayForward, Capcom, or
Nintendo.
