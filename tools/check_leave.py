"""Leaving the game from its menus: the first confirmed Quit / Return to Launcher ends it.

Each case starts the game in an 800x720 window (hidden) with its own state
folder, lets it run a moment, and leaves it:

- menu: Escape's menu, System, "Return to launcher" pressed twice (its press
  and the confirming press) must end the game at once with exit code 64 and
  start the launcher, which is told (LNG_SCRIPT) to take a screenshot, kept
  in logs/leave/, and close; "Quit game" pressed twice must end it with exit
  code 0 and start nothing.
- settings: the same from the settings window's footer buttons and their
  confirmation popups.
- debug: the debug server's `menu leave=launcher` and then `leave=quit` while
  it holds the game: the last one asked for decides, so it quits.

Before the fix the confirmed press pushed an SDL_QUIT that the menu's hold
caught and pushed back every frame: SDL_PollEvent stops at the poll sentinel
queued before it, so the main loop never saw it, and the game ran on behind
the open menu, unpaced, until a second leave queued a second quit. The first
Return to Launcher had also registered the relaunch for the program's exit,
so a Quit after it started the launcher too.

Never touches user settings or saves.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from view_probe import Debug, ROOT
from view_state_repro import step

SIZE = (800, 720)
SYSTEM = (93, 264)                   # the menu's System section at SIZE
MENU_ROWS = {"launcher": (348, 572), "quit": (330, 621)}
FOOTER = {"launcher": (496, 671), "quit": (690, 671)}
POPUP = {"launcher": (335, 387), "quit": (280, 387)}   # the popup's confirm button
EXIT_CODES = {"launcher": 64, "quit": 0}


def processes():
    """{pid: parent pid} of every process (Windows; elsewhere empty, and the
    launcher's screenshot tells whether it opened)."""
    if os.name != "nt":
        return {}
    import ctypes
    from ctypes import wintypes

    class Entry(ctypes.Structure):   # PROCESSENTRY32W
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD),
                    ("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
                    ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", ctypes.c_long),
                    ("flags", wintypes.DWORD), ("exe", ctypes.c_wchar * 260)]

    k32 = ctypes.windll.kernel32
    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    snap = k32.CreateToolhelp32Snapshot(2, 0)   # TH32CS_SNAPPROCESS
    entry, found = Entry(), {}
    entry.size = ctypes.sizeof(Entry)
    ok = k32.Process32FirstW(snap, ctypes.byref(entry))
    while ok:
        found[entry.pid] = entry.parent
        ok = k32.Process32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return found


def children(pid):
    return [p for p, parent in processes().items() if parent == pid]


def alive(pid):
    return pid in processes()


class Game:
    def __init__(self, exe, tmp, shot, port):
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=str(tmp), GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_DEBUG_PORT=str(port), GBRECOMP_HIDDEN_WINDOW="1",
                   SDL_AUDIODRIVER="dummy", LNG_FORCE_SCALE="1",
                   LNG_SCRIPT=f"wait:30;shot:{shot.resolve().as_posix()};quit")
        self.log = open(Path(tmp, "run.log"), "w")
        self.proc = subprocess.Popen([str(exe)], cwd=tmp, env=env, stdout=self.log,
                                     stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 20
        while True:
            try:
                self.c = Debug(port)
                break
            except OSError:
                if self.proc.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(0.05)
        self.c.command("window", width=SIZE[0], height=SIZE[1])
        step(self.c, 60)
        self.c.command("continue")

    def frame(self):
        return self.c.command("history").get("newest")

    def click(self, at):
        self.c.command("ui_event", x=at[0], y=at[1], button=1, down=1)
        time.sleep(0.05)
        self.c.command("ui_event", x=at[0], y=at[1], button=1, down=0)
        time.sleep(0.3)

    def leave(self, how, to):
        """Asks to leave; returns the frame the game had reached."""
        if how == "menu":
            self.c.command("menu", open="main")
            self.click(SYSTEM)
            self.click(MENU_ROWS[to])          # "Press again ..."
            frame = self.frame()
            self.click(MENU_ROWS[to])
        elif how == "settings":
            self.c.command("menu", open="settings")
            self.click(FOOTER[to])
            frame = self.frame()
            self.click(POPUP[to])
        else:
            self.c.command("pause")
            step(self.c, 1)
            frame = self.frame()
            try:
                self.c.command("menu", leave="launcher")
                self.c.command("menu", leave="quit")
                self.c.command("continue")
            except (OSError, RuntimeError):
                pass   # gone at the first leave (the build before the fix)
        return frame

    def outcome(self, frame, timeout=5.0):
        """(exit code or None, what happened), waiting up to timeout for the exit."""
        try:
            return self.proc.wait(timeout=timeout), ""
        except subprocess.TimeoutExpired:
            try:
                m = self.c.command("menu")
                later = self.frame()
                return None, (f"still running {timeout:.0f} s later, frames {frame} -> {later} "
                              f"with the menu {'holding the game' if m['game_held'] else 'open'}")
            except (OSError, RuntimeError) as e:
                return None, f"still running {timeout:.0f} s later ({e})"

    def close(self):
        if self.proc.poll() is None:
            try:
                self.c.command("quit")
                self.proc.wait(timeout=10)
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                self.proc.kill()
                self.proc.wait(timeout=10)
        self.log.close()


def run_case(exe, how, to, shot, port):
    """Returns (passed, line)."""
    shot.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="shantae-leave-", ignore_cleanup_errors=True) as tmp:
        game = Game(exe, tmp, shot, port)
        try:
            frame = game.leave(how, to)
            code, detail = game.outcome(frame)
        finally:
            game.close()
        # A relaunch happens as the program exits; give the launcher time for
        # its screenshot and its own quit, and close it if it lingers.
        kids = children(game.proc.pid)
        deadline = time.monotonic() + 20
        while kids and any(alive(k) for k in kids) and time.monotonic() < deadline:
            time.sleep(0.25)
        if os.name != "nt":
            time.sleep(5)
        for k in kids:
            if alive(k):
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(k)], capture_output=True)
        launcher = shot.exists() or bool(kids)

    want_code = 0 if how == "debug" else EXIT_CODES[to]
    want_launcher = how != "debug" and to == "launcher"
    if code is None:
        got = detail
    else:
        got = f"exit code {code}, " + ("the launcher opened" if launcher else "no launcher")
        if detail:
            got += f" ({detail})"
    passed = code == want_code and launcher == want_launcher
    label = "debug: leave launcher, then quit" if how == "debug" else f"{how}: {to}"
    return passed, f"{'PASS' if passed else 'FAIL'} {label}: {got}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    parser.add_argument("--port", type=int, default=14373)
    args = parser.parse_args()

    shots = ROOT / "logs/leave"
    shots.mkdir(parents=True, exist_ok=True)
    cases = [("menu", "launcher"), ("menu", "quit"),
             ("settings", "launcher"), ("settings", "quit"), ("debug", "quit")]
    failed = 0
    for how, to in cases:
        passed, line = run_case(args.exe.resolve(), how, to, shots / f"{how}-{to}.png", args.port)
        failed |= not passed
        print(line, flush=True)
    print(f"launcher screenshots in {shots}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
