Shantae (Game Boy Color), recompiled - Windows build
====================================================

There is one zip per kind of processor:

  windows-x64     64-bit Intel and AMD PCs: almost every Windows PC
  windows-x86     32-bit Windows (x86)
  windows-arm64   ARM PCs: Snapdragon X laptops, Surface Pro X and 9 5G,
                  Windows Dev Kit 2023. (They run the x64 build too, through
                  emulation; this one runs natively.)

Windows 10 or 11, and a graphics driver with Direct3D 11. The game draws
through ANGLE (OpenGL ES on Direct3D), which comes with it, as do SDL2 and
librashader for the shader presets; nothing else needs installing.

You need your own Shantae (USA) ROM (.gbc). The launcher asks for it on the
first start. It must be the No-Intro dump, SHA-256
1b92e22d5510c51bab97d23074e4aad7464d93eb15f7596ef7da0a5efa27a19d: the
launcher reads "ROM verified" for it and starts no other file.


Running
-------
Extract the zip to a folder you can write to (your Documents or Desktop, not
Program Files) and run shantae.exe in it. Settings, saves, save states and
rom.cfg are kept in that folder; to update, copy a newer version's files over
the old ones there.

Windows may say it protected your PC from an unrecognized app: click
"More info", then "Run anyway".

Esc, or a controller's Guide/Home button, opens the in-game menu (settings,
Shantae's options, shader presets).


Troubleshooting
---------------
- The console window beside the game shows what it reports.
- A preset under Shader Presets compiles in the background the first time it
  is picked (up to a minute for the biggest Mega Bezel ones) and loads in a
  moment after that.
