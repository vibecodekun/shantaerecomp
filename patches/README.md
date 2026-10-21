# patches/

ROM patches made for Shantae (USA) that the build compiles: every instruction one of them
changes is compiled from its patched bytes too (`shantae.toml`, `rom_patches`), so their code
changes run natively ([docs/rom-patches.md](../docs/rom-patches.md)). Any other patch a player
picks works if it changes only data.

The patches themselves are not in the repository, as the ROM is not: put these files here, in
these folders, before `tools/build.sh`. A missing one is simply not compiled in (the build lists
the ones it found in `generated/build/recomp.log`). `patches.toml` names them for players;
`python tools/check_patches.py` tests every patch here.

| File | SHA-256 | What |
|------|---------|------|
| `ShantaeFR/ShantaeFR.ips` | `d7332844c15f6733a5449ddd003a3e29ca2e89670b2aa8e21585c581770b4262` | French translation by Anthiflo, v1.2 ([romhacking.net](https://www.romhacking.net/translations/5578/)) |
| `ShantaeGBCITA/ShantaeGBCITA.ips` | `255a55745640ec0189a31f4a628c3da858e91d676805fcf03c001a9e6d054df7` | Italian translation |
| `ShantaePL-v2/ShantaePL-v2.ips` | `89f8130d0d1d07a3b7ec9090ef29e9e36a2c3ed1e70e2c2d4ea23d1a8f31c759` | Polish translation by PieniondzPS, v2 |
| `2024_Shantae_PT-BR_v1_sliter_ajkmetiuk/Shantae_PT-BR_v1+dance_fix.ips` | `8ce6636012f06fe118d1a30c370ead0fcb73d093a75d078c9167cac211d16bbb` | Brazilian Portuguese translation by Sliter and ajkmetiuk, v1, with the dance floor fix |
| `2024_Shantae_PT-BR_v1_sliter_ajkmetiuk/Shantae_PT-BR_v1+dance+gba+color_fix.ips` | `7d97f780a5d86a1566e62e7413e58cacf802cb2f32ca09be6ac05581eba3dfe0` | The same, with the dance floor fix, GBA Enhanced mode and GBC palettes |
| `Shantae (U) [C][!] dance bugfix/Shantae (U) [C][!] dance bugfix.ips` | `e7d49e15bcdb8bdc9cab065b49e6bd59dd67b73d53fc3e437171a6b38be8251d` | Dance floor hang fix by Griever |
| `Shantae GBA Color Patch/Shantae GBA Color Patch.ips` | `d367fd2b71b78e7e6686e24e97acacb32768cb2c64ada8912bb8ab087af71ac2` | GBA Enhanced mode on any Game Boy Color (Polar-Star) |
| `Shantae GBC palettes/Shantae GBC palettes v1.1.ips` | `a3a2d46f9a191270f49ed1fd528e1da47dda7925d943497aa6624c495bbf389a` | GBC palettes by Drenn, v1.1 |

The credits are the patches' own readmes'; the Brazilian Portuguese translation's readme credits
the three small fixes it includes.
