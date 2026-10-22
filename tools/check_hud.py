"""The transformation buttons' HUD and the day and night clock as players see them.

Each place is shot in a hidden window (GBRECOMP_HIDDEN_WINDOW: the window's
own composition, its HUD pictures and shader chains included) with no shader
preset, an LCD dot matrix, crt-geom and koko-aio's monitor-bloom-bezel drawing
the whole window, the last of which takes the HUD and the clock into the
picture. The places (tools/make_states.py's states):

- corridor: the first labyrinth, the monkey's dance learned;
- town: a town's street (debug scene 04), every dance taught: no
  HUD, and a transformation button does nothing;
- shared: the corridor with the clock in the HUD's corner, below it.

Also checks, from the HUD's own pictures (the debug server's shantae_hud),
that the HUD is drawn in the corridor and not on the town's street, and that
a transformation button there leaves her as she is.

usage: python tools/check_hud.py [--shots logs/hud] [--presets none,lcd,crt,bezel]
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time
from view_probe import Debug, ROOT

PRESETS = {
    "none": "",
    "lcd": "shader.slang=handheld/gameboy-color-dot-matrix.slangp\nshader.slang_fill_window=0\n",
    "crt": "shader.slang=crt/crt-geom.slangp\nshader.slang_fill_window=0\n",
    "bezel": "shader.slang=bezel/koko-aio/Presets-4.1/monitor-bloom-bezel.slangp\nshader.slang_fill_window=1\n",
}
TOWN_SCENE = 0x04
FORM = 0xCB72


class Game:
    def __init__(self, tmp, name, ini, prefs, port):
        state = Path(tmp, name)
        state.mkdir()
        Path(state, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(state, "shantae.ini").write_text("expanded_view=1\n" + ini)
        Path(state, "runtime_prefs.ini").write_text("video.fullscreen=0\nvideo.vsync=0\n" + prefs)
        env = dict(os.environ, GBRECOMP_STATE_DIR=str(state), GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_HIDDEN_WINDOW="1", GBRECOMP_DEBUG_PORT=str(port))
        self.log = open(Path(state, "run.log"), "w")
        self.proc = subprocess.Popen([str(ROOT / "generated/build/shantae.exe")], cwd=state, env=env,
                                     stdout=self.log, stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 30
        while True:
            try:
                self.c = Debug(port)
                break
            except OSError:
                if self.proc.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(0.05)
        self.c.command("pause")
        self.c.command("window", width=1280, height=720, scaling_mode=0)
        self.run_log = Path(state, "run.log")
        self.preset = "shader.slang=" in prefs

    def wait_for_preset(self):
        """A preset is probed and loaded in the background, in wall time."""
        deadline = time.monotonic() + 30
        while self.preset and time.monotonic() < deadline:
            self.log.flush()
            text = self.run_log.read_text(errors="replace")
            if "[SHADER] loaded" in text or "[SHADER] probe" in text and "exit 0x00000000" not in text:
                break
            self.step(2)
            time.sleep(0.2)
        # Presets that keep past frames (koko-aio's bloom and ambient light)
        # settle over a second or two of frames presented.
        for _ in range(self.preset * 30):
            self.step(4)
            time.sleep(0.05)

    def step(self, frames):
        self.c.command("step", count=frames)
        while self.c.read().get("event") != "step_done":
            pass

    def ram(self, addr, n=1):
        return bytes.fromhex(self.c.peek(addr, n))

    def close(self):
        try:
            self.c.command("quit")
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()
        self.log.close()


def teach_dances(g):
    """Every dance's learned bit (the dance table, 0E:4342)."""
    rom = (ROOT / "roms/shantae.gbc").read_bytes()
    for i in range(16):
        e = 0x0E * 0x4000 + 0x4342 - 0x4000 + i * 8
        if not rom[e] | rom[e + 1]:
            break
        addr = 0xCB1B + rom[e + 3]
        g.c.command("poke", addr=hex(addr), hex=f"{g.ram(addr)[0] | rom[e + 2]:02x}")


def to_town(g):
    """Scene 04, a town's street (CBFD the scene, CBFC 1 loads it, as
    check_ui_fit does)."""
    g.c.command("set_input", buttons="-")
    g.c.command("load_state", path=(ROOT / "logs/states/debug-grid.state").as_posix())
    g.step(2)
    g.c.command("poke", addr="0xcbfc", hex=f"01{TOWN_SCENE:02x}")
    g.step(300)
    teach_dances(g)
    g.c.command("set_input", buttons="R")
    g.step(30)
    g.c.command("set_input", buttons="-")
    g.step(10)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shots", type=Path, default=ROOT / "logs/hud")
    ap.add_argument("--presets", default="none,lcd,crt,bezel")
    ap.add_argument("--port", type=int, default=14420)
    args = ap.parse_args()
    args.shots.mkdir(parents=True, exist_ok=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix="shantae-hud-", ignore_cleanup_errors=True) as tmp:
        for k, preset in enumerate(args.presets.split(",")):
            for place in ("corridor", "town", "shared"):
                ini = "clock=1\n" if place == "shared" else ""
                g = Game(tmp, f"{preset}-{place}", ini, PRESETS[preset], args.port + k * 3 + len(place) % 3)
                try:
                    if place == "town":
                        to_town(g)
                    else:
                        g.c.command("load_state", path=(ROOT / "logs/states/corridor.state").as_posix())
                        g.c.command("set_input", buttons="-")
                        g.step(30)
                    g.wait_for_preset()
                    g.step(2)
                    shot = (args.shots / f"{preset}-{place}.png").resolve().as_posix()
                    g.c.command("window_screenshot", path=shot)   # taken at the next frame presented
                    g.step(2)
                    hud = g.c.command("shantae_hud")
                    if place == "town" and g.ram(0xC9F9, 2) != b"\x67\x9b":
                        failures.append(f"{preset}: scene {TOWN_SCENE:02X} is not on a town's map")
                    if place == "town":
                        if hud["drawn"]:
                            failures.append(f"{preset}: the HUD is drawn on a town's street")
                        g.c.command("shantae_transform", form=1)
                        g.step(40)
                        if g.ram(FORM)[0]:
                            failures.append(f"{preset}: a button turned her into form {g.ram(FORM)[0]} on the street")
                    elif not hud["drawn"]:
                        failures.append(f"{preset}: no HUD in the corridor")
                    print(f"{preset} {place}: HUD drawn {hud['drawn']}, {shot}")
                finally:
                    g.close()
    for f in failures:
        print("FAIL", f)
    print("PASS" if not failures else f"{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
