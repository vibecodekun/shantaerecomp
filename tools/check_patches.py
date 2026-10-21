"""ROM patches on this build (gb_rom_patch.h): every IPS/BPS patch in patches/.

patches/ holds patches made for Shantae (USA), named in patches/patches.toml;
the build compiles every instruction each one changes from its patched bytes
(shantae.toml, rom_patches). Each patch is chosen as a player chooses it, on
the stock ROM (runtime_prefs.ini's rom.patch_file), and:

- positions: every instruction position it changes, run through the game's
  dispatcher against the reference interpreter on the patched ROM, from both
  flag states (whole_rom_check's patches mode; build it first);
- boot: the runtime must recognize it and find every instruction position it
  changes compiled;
- differential: from a cold boot, the game's native code against the reference
  interpreter on the patched ROM, instruction by instruction, registers and
  memory compared after every one, for --frames frames: the boot, the logos and
  the title, where the hardware check and the palette loader that some patches
  change run. The patched instructions run are counted;
- play: from a cold boot, WayForward's debug mode (the title code, then Start
  Debug Game, as tools/make_states.py does) to the scene grid; scene 3C,
  Shantae's remark as she lands, into its text; the first town's shop to its
  menu, as make_states.py goes there. Each is pictured in logs/patches/<patch>/,
  where the patch's own text and graphics show. Then a differential from the
  shop's menu, --shop-frames frames: the cursor down, an item bought, out again;
- dance floor, for a patch with the dance floor fix (12:5725): from its debug
  grid, the way a player gets to the dance floor at night: the Zombie Caravan,
  out its gates, the warp dance to Scuttle Town's wall, Up held at the door,
  the Dance Parlor, the owner, a round left to run to its end. The fix must set
  D719-D71C (WRAM bank 3) to 8, 8, 8, 2; then a differential from 120 frames
  before the round ends, --round-frames frames, the fix's code among them;
- the patched ROM file itself, picked as the ROM with no patch on, must start
  as that patch.

Then patches made here, not in patches/:
- code: the boot's hardware check changed (00:3976 LD A,$00 to LD A,$01), which
  this build has no native code for: the game must stop there with its
  "ROM patch" message (a watcher closes it);
- data: a byte no analyzed routine reaches changed: it must start and run;
- size: a byte past the ROM's end: the game must refuse it with its message.

And the launcher (LNG_SCRIPT, 1280x800), its Mods page's ROM PATCH line read
from a screenshot: green for a patch of patches/ and for the data patch, amber
for the code patch (PLAY still starts the game) and for the size patch (PLAY
does not).

Never touches user settings or saves: every run has its own state folder.

    python tools/check_patches.py                       # everything
    python tools/check_patches.py --only ShantaeFR      # patches whose path has this
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
PATCHES = ROOT / "patches"
SIZE = (1280, 800)
MODS = (1181, 59)
PLAY = (1118, 723)
PATCH_LINE = (60, 335, 1240, 420)   # the ROM PATCH box's verdict, with room to wrap
SCENE = 0x3C
TITLE_CODE_FRAMES = 1300
# The dance floor fix's change: 12:5725, the routine a round's controller
# (script 12:5713, op 32) calls once the round is over.
DANCE_FLOOR_FIX = 0x12 * 0x4000 + 0x1725
ROUND_OVER = range(0x5713, 0x5725)   # the controller's script after that call
CARAVAN = 27 + 3   # the debug grid's row N, fourth entry: the Zombie Caravan
PARLOR = 7         # Scuttle Town's Dance Parlor (its door label, CBD6)
WARP_DOOR = (0x18, 0x4425)   # the town's door outside its wall, where the first warp sets her down
PARLOR_OWNER = 1335   # where she stands talking to the owner, and is put back after a round


def _ips(rom, patch, width, footer):
    out = bytearray(rom)
    pos = 5
    while patch[pos:pos + len(footer)] != footer:
        offset = int.from_bytes(patch[pos:pos + width], "big")
        size = int.from_bytes(patch[pos + width:pos + width + 2], "big")
        pos += width + 2
        if size == 0:
            size = int.from_bytes(patch[pos:pos + 2], "big")
            data = bytes([patch[pos + 2]]) * size
            pos += 3
        else:
            data = patch[pos:pos + size]
            pos += size
        if offset + size > len(out):
            out.extend(bytes(offset + size - len(out)))
        out[offset:offset + size] = data
    pos += len(footer)
    if width == 3 and len(patch) - pos == 3:
        del out[int.from_bytes(patch[pos:pos + 3], "big"):]
    return bytes(out)


def _bps_number(patch, pos):
    value, shift = 0, 1
    while True:
        x = patch[pos]
        pos += 1
        value += (x & 0x7F) * shift
        if x & 0x80:
            return value, pos
        shift <<= 7
        value += shift


def _bps(rom, patch):
    pos = 4
    source_size, pos = _bps_number(patch, pos)
    target_size, pos = _bps_number(patch, pos)
    meta, pos = _bps_number(patch, pos)
    pos += meta
    if source_size != len(rom) or zlib.crc32(rom) != int.from_bytes(patch[-12:-8], "little"):
        raise ValueError("the BPS patch is for another ROM")
    out = bytearray()
    source_rel = target_rel = 0
    end = len(patch) - 12
    while pos < end:
        value, pos = _bps_number(patch, pos)
        action, length = value & 3, (value >> 2) + 1
        if action == 0:
            out += rom[len(out):len(out) + length]
        elif action == 1:
            out += patch[pos:pos + length]
            pos += length
        else:
            delta, pos = _bps_number(patch, pos)
            delta = -(delta >> 1) if delta & 1 else delta >> 1
            if action == 2:
                source_rel += delta
                out += rom[source_rel:source_rel + length]
                source_rel += length
            else:
                target_rel += delta
                for _ in range(length):
                    out.append(out[target_rel])
                    target_rel += 1
    if len(out) != target_size or zlib.crc32(out) != int.from_bytes(patch[-8:-4], "little"):
        raise ValueError("the BPS patch's result does not match its checksum")
    return bytes(out)


def apply_patch(rom, patch):
    """The ROM with an IPS, IPS32 or BPS patch applied (as runtime/src/rom_patch.c)."""
    if patch[:5] == b"IPS32":
        return _ips(rom, patch, 4, b"EEOF")
    if patch[:5] == b"PATCH":
        return _ips(rom, patch, 3, b"EOF")
    if patch[:4] == b"BPS1":
        return _bps(rom, patch)
    raise ValueError("not an IPS or BPS patch")


def ips(records):
    """A classic IPS patch from (offset, bytes) records."""
    out = bytearray(b"PATCH")
    for offset, data in records:
        out += offset.to_bytes(3, "big") + len(data).to_bytes(2, "big") + data
    return bytes(out + b"EOF")


def titles():
    """patches.toml's titles by file."""
    import tomllib
    manifest = PATCHES / "patches.toml"
    if not manifest.exists():
        return {}
    return {p["file"]: p["title"] for p in tomllib.loads(manifest.read_text(encoding="utf-8")).get("patch", [])}


def state_dir(tmp, name, rom, patch=None, ini=""):
    d = Path(tmp, name)
    d.mkdir(parents=True)
    Path(d, "rom.cfg").write_text(str(Path(rom).resolve()) + "\n", encoding="utf-8")
    Path(d, "shantae.ini").write_text("expanded_view=0\n" + ini)
    if patch:
        Path(d, "runtime_prefs.ini").write_text(f"rom.patch_enabled=1\nrom.patch_file={Path(patch).resolve()}\n",
                                                encoding="utf-8")
    return d


def watch_dialogs(stop, seen, titles=("ROM patch", "Wrong ROM", "Select Game Boy ROM")):
    """Close the runtime's message boxes (and a file picker after them), noting each."""
    if os.name != "nt":
        return
    import ctypes
    user32 = ctypes.windll.user32
    while not stop.is_set():
        for title in titles:
            window = user32.FindWindowW(None, title)
            if window:
                if title not in seen:
                    seen.append(title)
                user32.PostMessageW(window, 0x0010, 0, 0)   # WM_CLOSE
        time.sleep(0.05)


def run_game(exe, state, args, env_extra=None, timeout=3600):
    """(output, exit code, dialogs seen); None for the code on a timeout."""
    env = dict(os.environ, GBRECOMP_STATE_DIR=str(state), GBRECOMP_HIDDEN_WINDOW="1", SDL_AUDIODRIVER="dummy")
    env.update(env_extra or {"GBRECOMP_NO_LAUNCHER": "1"})
    seen, stop = [], threading.Event()
    watcher = threading.Thread(target=watch_dialogs, args=(stop, seen), daemon=True)
    watcher.start()
    try:
        r = subprocess.run([str(exe), *args], cwd=state, env=env, capture_output=True, text=True,
                           errors="replace", timeout=timeout)
        out, code = r.stdout + r.stderr, r.returncode
    except subprocess.TimeoutExpired:
        out, code = "", None
    finally:
        stop.set()
        watcher.join()
    return out, code, seen


def boot_line(output):
    """The runtime's line for the patch at boot, parsed."""
    m = re.search(r"\[ROM patch\] (.*?): (\d+) bytes changed, (\d+) instruction positions, (\d+) compiled, "
                  r"(\d+) without native code in analyzed code, (\d+) elsewhere; (\d+) routines", output)
    if not m:
        return None
    keys = ("name", "changed", "positions", "compiled", "uncompiled_code", "uncompiled_other", "routines")
    return dict(zip(keys, [m.group(1)] + [int(g) for g in m.groups()[1:]]))


def ran(output):
    m = re.search(r"\[ROM patch\] .*?: ran (\d+) patched instructions at (\d+) positions", output)
    return (int(m.group(1)), int(m.group(2))) if m else None


class Game:
    """The game under the debug server (tools/view_probe.py), a patch chosen."""

    def __init__(self, exe, state, port):
        from view_probe import Debug
        env = dict(os.environ, GBRECOMP_STATE_DIR=str(state), GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_DEBUG_PORT=str(port), GBRECOMP_HIDDEN_WINDOW="1", SDL_AUDIODRIVER="dummy")
        self.log = open(Path(state, "run.log"), "w")
        self.proc = subprocess.Popen([str(exe), "--benchmark"], cwd=state, env=env, stdout=self.log,
                                     stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 20
        while True:
            try:
                self.c = Debug(port)
                break
            except OSError:
                if self.proc.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(0.02)
        self.c.command("pause")

    def mem(self, addr, count, **banks):
        return bytes.fromhex(self.c.peek(addr, count, **banks))

    def slot(self):
        s = self.mem(0xCA13, 2)
        return self.mem(s[0] | s[1] << 8, 126, wram_bank=3)

    def shot(self, path):
        self.c.command("screenshot", path=Path(path).resolve().as_posix())

    def close(self):
        try:
            fallback = self.c.command("interp_fallbacks")["total_fallbacks"]
            self.c.command("quit")
            self.proc.wait(timeout=20)
        finally:
            if self.proc.poll() is None:
                self.proc.terminate()
                self.proc.wait(timeout=10)
            self.log.close()
        return fallback


def play(exe, tmp, patch, out, port):
    """Debug mode, scene 3C's text and the shop's menu, pictured; the shop's state for the differential."""
    import make_states
    from view_state_repro import step
    state = state_dir(tmp, "play", ROOT / "roms/shantae.gbc", patch, "smooth_moves=0\neasy_dance=0\ntext_speed=0\n")
    g = Game(exe, state, port)
    try:
        grid = out / "debug-grid.state"
        make_states.debug_grid(g, grid)
        g.shot(out / "grid.png")
        g.c.command("set_input", buttons="-")
        g.c.command("load_state", path=grid.resolve().as_posix())
        step(g.c, 2)
        g.c.command("poke", addr="0xcbfc", hex=f"01{SCENE:02x}")
        step(g.c, 360)
        for _ in range(6):   # the remark, a box at a time: B runs its letters out
            g.c.command("set_input", buttons="B")
            step(g.c, 40)
            g.c.command("set_input", buttons="-")
            step(g.c, 20)
        g.shot(out / "text.png")
        make_states.shop(g, grid, out)
        g.shot(out / "shop.png")
        info = g.c.command("rom_patch_info")
    finally:
        fallbacks = g.close()
    return info, fallbacks, Path(state, "run.log").read_text(errors="replace")


def dance_floor(exe, tmp, patch, out, port):
    """The dance floor at night, as a player gets there, and a round played to its end.

    From the patch's own debug-grid.state (play()): the Zombie Caravan (night),
    out through its gates, the first warp dance (Down Up Left Right) to
    Scuttle Town's wall, Up held at its door (the helper 06:7143 takes Up held
    for 10 frames), the Dance Parlor, its owner talked to with B (Yes, rank
    E), and the round left to run with nothing pressed until its controller
    has called 12:5725. Saves round-end.state 120 frames before that call and
    returns the frame it came in and the WRAM bank 3 bytes D719-D71C after it
    (the fix sets them to 8, 8, 8, 2: the position of the object 58A9 makes)."""
    import make_states
    from make_states import press, scripts
    from view_state_repro import step

    def where():
        s = g.slot()
        return s[0x34] | s[0x35] << 8

    state = state_dir(tmp, "dance", ROOT / "roms/shantae.gbc", patch)
    g = Game(exe, state, port)
    try:
        g.c.command("set_input", buttons="-")
        g.c.command("load_state", path=(out / "debug-grid.state").resolve().as_posix())
        step(g.c, 5)
        g.c.command("poke", addr="0xc389", hex=f"{CARAVAN:02x}")
        press(g, "A", 2)
        press(g, "-", 300)
        g.c.command("poke", addr="0xcc04", hex="00")
        press(g, "U", 2)            # the Caravan Gates, where she starts
        press(g, "-", 300)
        press(g, "T", 2)
        press(g, "-", 30)
        for b in "DULR":            # the first warp, with Easier dancing's quick steps
            press(g, b, 2)
            press(g, "-", 20)
        press(g, "-", 800)
        raw = g.mem(0xD000, 32 * 126, wram_bank=3)   # the door's slot is marked $80, not drawn
        doors = [s[0x34] | s[0x35] << 8 for s in (raw[i * 126:(i + 1) * 126] for i in range(32))
                 if s[0] != 0xFF and (s[4], s[2] | s[3] << 8) == WARP_DOOR]
        assert doors, ("no town door after the warp", sorted(scripts(g)))
        door = doors[0] - 14   # Up takes her in from 29 to 7 pixels left of the door's x, standing
        g.c.command("set_input", buttons="R" if where() < door else "L")
        for _ in range(200):
            if abs(where() - door) <= 2:
                break
            step(g.c, 1)
        press(g, "-", 10)
        g.c.command("set_input", buttons="U")
        for _ in range(60):
            step(g.c, 1)
            if g.mem(0xC9F9, 2).hex() == "679b":
                break
        press(g, "-", 300)
        assert g.mem(0xC9F9, 2).hex() == "679b", ("not in Scuttle Town", g.mem(0xC9F9, 2).hex())
        g.c.command("set_input", buttons="R")
        for _ in range(900):
            step(g.c, 1)
            if g.mem(0xCBD6, 1)[0] == PARLOR:
                break
        press(g, "-", 4)
        assert g.mem(0xCBD6, 1)[0] == PARLOR, "no Dance Parlor door"
        press(g, "U", 12)
        press(g, "-", 240)
        press(g, "R", 400)          # to the owner
        press(g, "-", 30)
        g.shot(out / "parlor.png")
        press(g, "B", 2)
        press(g, "-", 60)
        for _ in range(14):         # "You here to dance?" Yes, rank E, on stage
            press(g, "B", 2)
            press(g, "-", 50)
        assert any(bank == 0x12 for bank, _ in scripts(g)), "no round on the dance floor"
        g.shot(out / "dance-floor.png")
        floor = out / "dance-floor.state"
        g.c.command("save_state", path=floor.resolve().as_posix())
        frame = 0
        while not any(bank == 0x12 and pc in ROUND_OVER for bank, pc in scripts(g)):
            assert frame < 3600, "the round did not end"
            step(g.c, 1)
            frame += 1
        fixed = g.mem(0xD719, 4, wram_bank=3)
        press(g, "-", 600)          # the winnings, and back to the owner
        g.shot(out / "after-round.png")
        back = where()
        g.c.command("load_state", path=floor.resolve().as_posix())
        step(g.c, frame - 120)
        g.c.command("save_state", path=(out / "round-end.state").resolve().as_posix())
    finally:
        fallbacks = g.close()
    return frame, fixed, back, fallbacks


def differential(exe, tmp, name, patch, frames, state=None, script=None):
    d = state_dir(tmp, name, ROOT / "roms/shantae.gbc", patch)
    args = ["--differential", "2000000000", "--differential-frames", str(frames), "--differential-log", "100000000"]
    if state:
        args += ["--differential-state", str(Path(state).resolve())]
    if script:
        args += ["--input", script]
    out, code, _ = run_game(exe, d, args)
    m = re.search(r"Matched generated and interpreter execution for (\d+) steps / (\d+) frames", out)
    mismatch = [l for l in out.splitlines() if "[DIFF]" in l and "Matched" not in l][:6]
    return (int(m.group(1)), int(m.group(2))) if m else None, ran(out), mismatch, code


def check_patch(args, patch, title, port, failures, lock):
    rel = patch.relative_to(PATCHES).as_posix()
    out = ROOT / "logs/patches" / re.sub(r"[^A-Za-z0-9_.-]+", "_", patch.stem)
    out.mkdir(parents=True, exist_ok=True)
    lines = []

    def fail(text):
        with lock:
            failures.append(f"{rel}: {text}")
        lines.append(f"  FAIL {text}")

    with tempfile.TemporaryDirectory(prefix="shantae-patches-", ignore_cleanup_errors=True) as tmp:
        # Boot, the patch chosen.
        output, code, _ = run_game(args.exe, state_dir(tmp, "boot", ROOT / "roms/shantae.gbc", patch),
                                   ["--benchmark", "--limit-frames", "120"])
        b = boot_line(output)
        if not b or "[LIMIT] Reached frame limit 120" not in output:
            fail(f"did not boot with the patch (exit {code})")
        else:
            lines.append(f"  boot: {b['changed']} bytes changed, {b['positions']} positions, all compiled: "
                         f"{b['compiled'] == b['positions']}; {b['routines']} routines step through changes")
            if b["name"] != title:
                fail(f"recognized as {b['name']!r}, not {title!r}")
            if b["compiled"] != b["positions"] or b["uncompiled_code"] or b["uncompiled_other"]:
                fail("positions without native code")

        # The patched ROM as the ROM, no patch on.
        stock = (ROOT / "roms/shantae.gbc").read_bytes()
        image = apply_patch(stock, patch.read_bytes())
        patched_rom = Path(tmp, "patched.gbc")
        patched_rom.write_bytes(image)
        output, code, _ = run_game(args.exe, state_dir(tmp, "prepatched", patched_rom),
                                   ["--benchmark", "--limit-frames", "120"])
        b2 = boot_line(output)
        if not b2 or b2["name"] != title or "[LIMIT] Reached frame limit 120" not in output:
            fail(f"the patched ROM file did not start as the patch (exit {code})")
        else:
            lines.append("  the patched ROM file starts as the patch")

        if not args.no_differential:
            matched, count, mismatch, code = differential(args.exe, tmp, "cold", patch, args.frames)
            if not matched:
                fail(f"cold-boot differential: {' | '.join(mismatch) or f'no verdict (exit {code})'}")
            else:
                lines.append(f"  cold-boot differential: matched {matched[0]} instructions / {matched[1]} frames; "
                             f"patched instructions run: {count[0] if count else 0}")

        if not args.no_play:
            try:
                info, fallbacks, log = play(args.exe, tmp, patch, out, port)
                lines.append(f"  play: debug grid, scene {SCENE:02X}'s text, the shop's menu "
                             f"(pictured in {out.relative_to(ROOT)}); patched instructions run: {info['ran']}")
                if fallbacks:
                    fail(f"{fallbacks} interpreter fallbacks")
                if "[ROM patch]" in log and "has to stop" in log:
                    fail("stopped at patched code")
            except Exception as e:   # noqa: BLE001 -- reported as the patch's failure
                fail(f"play: {e!r}")
            else:
                if not args.no_differential and (out / "shop.state").exists():
                    script = "10:D:2,40:B:2,90:B:2,140:B:2,190:B:2,240:A:2,290:A:2"
                    matched, count, mismatch, code = differential(args.exe, tmp, "shop", patch, args.shop_frames,
                                                                  out / "shop.state", script)
                    if not matched:
                        fail(f"shop differential: {' | '.join(mismatch) or f'no verdict (exit {code})'}")
                    else:
                        lines.append(f"  shop differential: matched {matched[0]} instructions / {matched[1]} "
                                     f"frames; patched instructions run: {count[0] if count else 0}")
                if image[DANCE_FLOOR_FIX:DANCE_FLOOR_FIX + 3] != stock[DANCE_FLOOR_FIX:DANCE_FLOOR_FIX + 3]:
                    try:
                        frame, fixed, back, fallbacks = dance_floor(args.exe, tmp, patch, out, port)
                    except Exception as e:   # noqa: BLE001 -- reported as the patch's failure
                        fail(f"dance floor: {e!r}")
                    else:
                        lines.append(f"  dance floor: the caravan, the warp, the parlor at night, a round over at "
                                     f"frame {frame}; D719-D71C {fixed.hex(' ')} (pictured in "
                                     f"{out.relative_to(ROOT)})")
                        if fixed != b"\x08\x08\x08\x02":
                            fail(f"the dance floor fix did not set D719-D71C: {fixed.hex(' ')}")
                        if fallbacks:
                            fail(f"{fallbacks} interpreter fallbacks on the dance floor")
                        if not args.no_differential:
                            matched, count, mismatch, code = differential(args.exe, tmp, "round-end", patch,
                                                                          args.round_frames, out / "round-end.state")
                            if not matched:
                                fail(f"round-end differential: {' | '.join(mismatch) or f'no verdict (exit {code})'}")
                            elif not count or not count[0]:
                                fail("round-end differential: the fix's code did not run")
                            else:
                                lines.append(f"  round-end differential: matched {matched[0]} instructions / "
                                             f"{matched[1]} frames; patched instructions run: {count[0]}")
    with lock:
        print(f"{rel} ({title})\n" + "\n".join(lines), flush=True)


def analyzed():
    """The ROM bytes of the routines the recompiler analyzed (generated/<prefix>_patch_tables.c)."""
    covered = bytearray((ROOT / "roms/shantae.gbc").stat().st_size)
    text = (ROOT / "generated/shantae_patch_tables.c").read_text()
    for _, offset, length in re.findall(r"\{(\d+),(\d+),(\d+)\}", text.split("body_ranges[]")[1]):
        covered[int(offset):int(offset) + int(length)] = b"\x01" * int(length)
    return covered


def made_patches(tmp):
    """The code, data and size patches made here."""
    rom = (ROOT / "roms/shantae.gbc").read_bytes()
    assert rom[0x3976:0x3978] == b"\x3e\x00", "00:3976 is not LD A,$00"
    covered = analyzed()
    data = 0x200000   # the middle of 64 bytes no analyzed routine reaches into
    while any(covered[data - 32:data + 32]):
        data += 64
    paths = {}
    for name, records in (("code", [(0x3977, b"\x01")]), ("data", [(data, bytes([rom[data] ^ 1]))]),
                          ("size", [(len(rom), b"\x00")])):
        paths[name] = Path(tmp, f"{name}.ips")
        paths[name].write_bytes(ips(records))
    return paths


def check_made(args, failures):
    with tempfile.TemporaryDirectory(prefix="shantae-patches-made-", ignore_cleanup_errors=True) as tmp:
        made = made_patches(tmp)
        output, code, seen = run_game(args.exe, state_dir(tmp, "code", ROOT / "roms/shantae.gbc", made["code"]),
                                      ["--benchmark", "--limit-frames", "120"], timeout=120)
        stopped = "changed the game's code at 00:3976" in output and code == 1
        print(f"code patch (00:3976 LD A,$00 -> LD A,$01): stopped at 00:3976 with its message: {stopped}"
              f" (dialogs {seen}, exit {code})")
        if not stopped or (os.name == "nt" and "ROM patch" not in seen):
            failures.append("the code patch did not stop the game at 00:3976 with its message")
        output, code, _ = run_game(args.exe, state_dir(tmp, "data", ROOT / "roms/shantae.gbc", made["data"]),
                                   ["--benchmark", "--limit-frames", "600"], timeout=300)
        b = boot_line(output)
        runs = "[LIMIT] Reached frame limit 600" in output and b and not b["uncompiled_code"]
        print(f"data patch (a byte no analyzed routine reaches): runs, as data: {bool(runs)}")
        if not runs:
            failures.append("the data patch did not run")
        output, code, seen = run_game(args.exe, state_dir(tmp, "size", ROOT / "roms/shantae.gbc", made["size"]),
                                      ["--benchmark", "--limit-frames", "120"], timeout=120)
        refused = "changes the ROM's size" in output and code == 1
        print(f"size patch (a byte past the end): refused with its message: {refused} (dialogs {seen})")
        if not refused:
            failures.append("the size patch was not refused")


def check_positions(args, patches, failures):
    """whole_rom_check's patches mode: every instruction position each patch changes."""
    exe = args.exe.parent / ("whole_rom_check.exe" if os.name == "nt" else "whole_rom_check")
    if not exe.exists():
        failures.append(f"{exe.name} is not built (ninja -C generated/build whole_rom_check)")
        return
    r = subprocess.run([str(exe), str(ROOT / "roms/shantae.gbc"), "patches", *map(str, patches)],
                       capture_output=True, text=True, errors="replace")
    for line in r.stdout.splitlines():
        if line.startswith(("PASS", "FAIL")):
            print(f"positions: {line}", flush=True)
    if r.returncode:
        failures += [f"positions: {l}" for l in r.stderr.splitlines() if l.startswith("FAIL")][:10] or \
                    [f"positions: whole_rom_check exited {r.returncode}"]


def patch_line(shot):
    """'green', 'amber' or None: the ROM PATCH box's verdict line."""
    from PIL import Image
    green = amber = 0
    for r, g, b in Image.open(shot).convert("RGB").crop(PATCH_LINE).get_flattened_data():
        if g > r + 60 and g > b + 30:
            green += 1
        elif r > g + 30 and g > b + 60:
            amber += 1
    if max(green, amber) < 20:
        return None
    return "green" if green > amber else "amber"


def check_launcher(args, known, failures):
    shots = ROOT / "logs/patches/launcher"
    shots.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="shantae-patches-launcher-", ignore_cleanup_errors=True) as tmp:
        made = made_patches(tmp)
        cases = [("known", known, "green", True), ("data", made["data"], "green", True),
                 ("code", made["code"], "amber", None), ("size", made["size"], "amber", False)]
        for name, patch, colour, starts in cases:
            state = state_dir(tmp, f"launcher-{name}", ROOT / "roms/shantae.gbc", patch)
            shot = shots / f"{name}.png"
            script = (f"size:{SIZE[0]}x{SIZE[1]};wait:30;click:{MODS[0]},{MODS[1]};wait:20;shot:{shot.resolve()};"
                      f"click:{PLAY[0]},{PLAY[1]};wait:60")
            env = {"LNG_SCRIPT": script, "LNG_FORCE_SCALE": "1"}
            output, code, seen = run_game(args.exe, state, ["--limit-frames", "30"], env, timeout=120)
            line = patch_line(shot)
            started = "[LIMIT] Reached frame limit 30" in output
            ok = line == colour and (starts is None or started == starts)
            print(f"launcher, {name} patch: the ROM PATCH line is {line} (want {colour}); PLAY started the game: "
                  f"{started}{'' if starts is None else f' (want {starts})'}")
            if not ok:
                failures.append(f"launcher, {name} patch: line {line}, started {started}")
            if name == "known" and started:
                prefs = Path(state, "runtime_prefs.ini").read_text(encoding="utf-8")
                kept = "rom.patch_enabled=1" in prefs and str(Path(known).resolve()) in prefs
                rom_cfg = Path(state, "rom.cfg").read_text(encoding="utf-8").strip()
                print(f"launcher, known patch: kept for the next start: {kept}; rom.cfg still the stock ROM: "
                      f"{Path(rom_cfg).resolve() == (ROOT / 'roms/shantae.gbc').resolve()}")
                if not kept:
                    failures.append("the launcher did not keep the patch in runtime_prefs.ini")


def main():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--only", default="", help="only the patches whose path contains this")
    ap.add_argument("--frames", type=int, default=300, help="cold-boot differential frames")
    ap.add_argument("--shop-frames", type=int, default=320, help="shop differential frames")
    ap.add_argument("--round-frames", type=int, default=300, help="dance floor round-end differential frames")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--port", type=int, default=14720)
    ap.add_argument("--no-differential", action="store_true")
    ap.add_argument("--no-play", action="store_true")
    ap.add_argument("--no-made", action="store_true", help="skip the made patches and the launcher")
    args = ap.parse_args()
    args.exe = args.exe.resolve()

    names = titles()
    patches = sorted(p for p in PATCHES.rglob("*") if p.suffix.lower() in (".ips", ".ips32", ".bps")
                     and args.only in p.relative_to(PATCHES).as_posix())
    failures, lock = [], threading.Lock()
    if patches and not args.no_differential:
        check_positions(args, patches, failures)
    queue = list(enumerate(patches))

    def worker():
        while True:
            with lock:
                if not queue:
                    return
                i, patch = queue.pop(0)
            rel = patch.relative_to(PATCHES).as_posix()
            check_patch(args, patch, names.get(rel, patch.stem), args.port + i, failures, lock)

    threads = [threading.Thread(target=worker) for _ in range(max(1, args.jobs))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    if not args.no_made:
        check_made(args, failures)
        if patches:
            check_launcher(args, patches[0], failures)
    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        sys.exit(1)
    print(f"PASS: {len(patches)} patches, each recognized with all its changed instructions compiled, "
          "matching the interpreter, playing natively")


if __name__ == "__main__":
    main()
