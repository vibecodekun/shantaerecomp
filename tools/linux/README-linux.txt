Shantae (Game Boy Color), recompiled - Linux build
====================================================

Runs on 64-bit x86 (x86_64) and 64-bit ARM (aarch64) Linux with glibc 2.28
or newer: Debian 10+, Ubuntu 20.04+, Fedora, Arch, RHEL/Alma/Rocky 8+,
openSUSE Leap 15.3+, SteamOS 3 (Steam Deck), Raspberry Pi OS (64-bit) and
the like. It needs an OpenGL ES 2.0 capable graphics driver (Mesa or
NVIDIA's; 3.0 for the shader presets) and X11 or Wayland. SDL2 and the C++
runtime are built in.

You need your own Shantae (USA) ROM (.gbc). The launcher asks for it on the
first start.


Running
-------
Tarball: extract it anywhere you can write to and run ./shantae in the
extracted folder (or double-click it). Settings, saves, save states and
rom.cfg are kept in that folder.

AppImage: make it executable (chmod +x Shantae_Recomp-*.AppImage, or the
file's Properties > Permissions) and run it. Settings and saves are kept in
the folder the .AppImage is in. If it says it cannot mount (no FUSE), run it
with --appimage-extract-and-run.

Esc, or a controller's Guide/Home button, opens the in-game menu (settings,
Shantae's options, shader presets).

Steam Deck: in Desktop Mode, put the AppImage (or the extracted folder) in
your home folder, e.g. ~/Games/Shantae, make it executable, then right-click
it > "Add to Steam". It then starts from Game Mode like any other game,
with the Deck's controls through Steam Input. The Steam button belongs to
Steam there, so to reach the in-game menu bind one of the back buttons
(L4/R4/L5/R5) to the keyboard's Escape key in the game's controller
settings.


Troubleshooting
---------------
- Run it from a terminal to see what it reports.
- The ROM file picker uses zenity or kdialog when either is installed, and
  its own picker otherwise.
- SDL uses X11 (XWayland on a Wayland desktop) by default; set
  SDL_VIDEODRIVER=wayland to run on Wayland directly.
- No sound: it plays through PipeWire, PulseAudio or ALSA, whichever the
  system has; with none of them it runs silently.
- Shader presets need librashader.so beside the game (included).
