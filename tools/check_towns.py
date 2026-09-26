"""Every town with the expanded view against the original, frame for frame.

Towns keep the original picture, object activation and object table
(docs/expanded-view.md), so with the view on they must play exactly as without
it. From a state saved on the debug scene grid with the cursor on row N (its
first five columns are Scuttle Town, Water Town, Oasis Town, the Zombie Caravan
and Bandit Town), each town is loaded and its loop is walked once without the
view to find the doors: where a door's label shows (CBD6, with the player's x
at CBDC). Then an expanded and an original instance run each route side by
side:

- the whole loop both ways, walking and running (A held);
- for every door, walking there from the town's start, Up, 240 frames inside,
  and walking left, the way out of the Firefly Shrine and others (a building
  whose way out is elsewhere is left inside), then 240 frames more.

Until the town is left, the presented picture, OAM, HRAM, WRAM C000-CEFF (the
stack below CFFF keeps stale frames) and the live object slots must be
identical at every frame, and the town must never be presented or activated
expanded, nor have more than 32 slots once its map is loaded. A shop or house
is a room the view does present, with the grown table: the frame each leaves
the town must be the same, and from there the rooms and cameras each goes
through (a load there may end a frame apart) and where each ends, door label
included; back in the town, it must still be native. Remove slowdown is off in
both: with the view a screen load may hold for more frames (extras.c), which
only moves the frame a load ends on.
Isolated settings and saves; the grid state is not modified.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from view_probe import Debug, ROOT

TOWN_MAP = "679b"   # C9F9/C9FA
SLOT_SIZE = 0x7E


def step(client, frames):
    client.command("step", count=frames)
    while client.read().get("event") != "step_done":
        pass


def town_route(town):
    """From the grid: the cursor to column `town` of row N, A, and the fade in."""
    return ["-:5"] + ["R:2", "-:4"] * town + ["A:2", "-:180"]


def run(args, route, expanded, port, shot, full=True):
    """Play `route` (parts "buttons:frames") from the grid state; one record a frame."""
    records = []
    with tempfile.TemporaryDirectory(prefix="shantae-towns-", ignore_cleanup_errors=True) as state:
        Path(state, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(state, "shantae.ini").write_text(
            f"expanded_view={int(expanded)}\nview_width={args.width}\nview_height={args.height}\n"
            f"room_zoom={args.room_zoom}\nremove_slowdown=0\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=state, GBRECOMP_NO_LAUNCHER="1", GBRECOMP_DEBUG_PORT=str(port))
        with open(Path(state, "run.log"), "w") as log:
            proc = subprocess.Popen([str(ROOT / "generated/build/shantae.exe"), "--benchmark"], cwd=state, env=env,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                deadline = time.monotonic() + 20
                while True:
                    try:
                        c = Debug(port)
                        break
                    except OSError:
                        if proc.poll() is not None or time.monotonic() > deadline:
                            raise
                        time.sleep(0.02)
                c.command("pause")
                size, _, mode = args.window.partition(":")
                width, height = map(int, size.split("x"))
                c.command("window", width=width, height=height, scaling_mode=int(mode or 0))
                c.command("load_state", path=args.grid.resolve().as_posix())
                n = 0
                for part in route:
                    buttons, frames = part.split(":")
                    c.command("set_input", buttons=buttons)
                    for _ in range(int(frames)):
                        step(c, 1)
                        n += 1
                        hram = bytes.fromhex(c.peek(0xFF80, 127))
                        wram = bytes.fromhex(c.peek(0xC000, 4096))
                        r = dict(n=n, map=wram[0x9F9:0x9FB].hex(), camera=(hram[0x61] | hram[0x62] << 8,
                                                                             hram[0x63] | hram[0x64] << 8),
                                 label=wram[0xBD6], player_x=wram[0xBDC] | wram[0xBDD] << 8)
                        if full:
                            c.command("screenshot", path=shot)
                            table = bytes.fromhex(c.peek(0xD000, 32 * SLOT_SIZE, wram_bank=3))
                            view = c.command("shantae_view_info")
                            for _ in range(view["count"]):
                                c.read()
                            r.update(picture=Path(shot).read_bytes(), oam=bytes.fromhex(c.peek(0xFE00, 160)),
                                     hram=hram, wram=wram[:0xF00], view=view,
                                     slots=b"".join(table[i * SLOT_SIZE:(i + 1) * SLOT_SIZE - 2]
                                                    for i in range(32) if table[i * SLOT_SIZE] != 0xFF))
                        records.append(r)
                c.command("quit")
                proc.wait(timeout=10)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)
    return records


def doors(args, town, port):
    """Label -> [first, last] player x (mod 640) and the frame of walking Right it is centred on."""
    base = town_route(town)
    start = sum(int(p.split(":")[1]) for p in base)
    walk = run(args, base + ["R:700"], False, port, None, full=False)[start:]
    found = {}
    for r in walk:
        if r["label"] and r["map"] == TOWN_MAP:
            found.setdefault(r["label"], []).append((r["player_x"] % 640, r["n"] - start))
    return walk[0]["camera"][1], {k: ([min(x for x, _ in v), max(x for x, _ in v)], v[len(v) // 2][1])
                                  for k, v in sorted(found.items())}


def rooms_of(records):
    """The rooms a run goes through, as [(map, camera, town), frames] in order,
    leaving out values held for less than three frames: a frame that ends in the
    middle of a load or of the camera routine shows a camera half set."""
    rooms = []
    for r in records:
        key = (r["map"], r["camera"], r["view"]["town"])
        if rooms and rooms[-1][0] == key:
            rooms[-1][1] += 1
        else:
            rooms.append([key, 1])
    held = []
    for key, frames in rooms:
        if frames < 3:
            continue
        if held and held[-1][0] == key:
            held[-1][1] += frames
        else:
            held.append([key, frames])
    return held


def compare(args, tag, route, port):
    """Everything identical until the town is left, which must happen on the
    same frame. A shop or house is a room the view presents, with the grown
    table, so its frames take different time (and different random numbers):
    from there the two must go through the same rooms at the same cameras, a
    load may end a frame apart, and they must end in the same room, at the
    same camera, with the same door label. The town is native throughout."""
    shots = [str(args.out / f"{tag}-{name}.png") for name in ("original", "expanded")]
    result = {}
    threads = [threading.Thread(target=lambda e: result.__setitem__(e, run(args, route, e, port + e, shots[e])),
                                args=(e,)) for e in (0, 1)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    if len(result) != 2:
        return f"{tag}: FAILED, an instance stopped"
    entered = False
    left = None
    # Frames in the town in a row: the picture the view reports is the last one
    # presented, a frame behind, and the room is set up before its map load
    # builds the table (three frames before, coming out of a shop).
    in_town = 0
    for a, b in zip(result[0], result[1]):
        view = b["view"]
        in_town = in_town + 1 if view["town"] else 0
        entered = entered or (a["view"]["town"] and view["town"])
        if entered and left is None and not (a["view"]["town"] and view["town"]):
            left = a["n"]
        if in_town and (view["activated"] or (in_town > 1 and view["expanded"]) or (in_town > 30 and view["slots"] > 32)):
            differ = ["town presented, activated or given slots expanded"]
        elif left is None:
            differ = [k for k in ("picture", "oam", "hram", "wram", "slots") if a[k] != b[k]]
        elif left == a["n"] and a["view"]["town"] != view["town"]:
            differ = ["left the town on different frames"]
        else:
            differ = []
        if differ:
            (args.out / f"{tag}-{a['n']:05d}-original.png").write_bytes(a["picture"])
            (args.out / f"{tag}-{a['n']:05d}-expanded.png").write_bytes(b["picture"])
            return f"{tag}: FAILED, frame {a['n']} differs ({', '.join(differ)})"
    for shot in shots:
        Path(shot).unlink(missing_ok=True)
    if not entered:
        return f"{tag}: FAILED, never reached the town"
    if left is None:
        return f"{tag}: {len(result[0])} frames identical"
    original = rooms_of(result[0][left - 1:])
    expanded = rooms_of(result[1][left - 1:])
    if [r[0] for r in original] != [r[0] for r in expanded]:
        return f"{tag}: FAILED, after leaving the town at frame {left} the rooms differ: {original} / {expanded}"
    a, b = result[0][-1], result[1][-1]
    if (a["map"], a["camera"], a["label"]) != (b["map"], b["camera"], b["label"]):
        return f"{tag}: FAILED, the ends differ: {a['map']} {a['camera']} label {a['label']} / {b['map']} {b['camera']} label {b['label']}"
    inside = max((r for r in original if not r[0][2]), key=lambda r: r[1])[0]
    back = f", then back in town at {a['camera']} with label {a['label']}" if a["view"]["town"] else ""
    return (f"{tag}: identical to frame {left - 1}; both leave the town at frame {left} for map {inside[0]} at "
            f"{inside[1]}{back}")


def check_town(args, town):
    port = args.port + 10 * town
    camera_y, labels = doors(args, town, port + 2)
    lines = [f"town {town} (camera y {camera_y}): " +
             ", ".join(f"door {k} at x {v[0][0]}-{v[0][1]}" for k, v in labels.items())]
    base = town_route(town)
    routes = [(f"town{town}-walk", base + ["R:700", "L:700"]), (f"town{town}-run", base + ["RA:360", "LA:360"])]
    routes += [(f"town{town}-door{k}", base + [f"R:{frame}", "U:2", "-:240", "L:150", "-:240"])
               for k, (_, frame) in labels.items()]
    lines += [compare(args, tag, route, port) for tag, route in routes]
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--grid", type=Path, default=ROOT / "logs/states/debug-grid.state",
                    help="a state on the debug scene grid, cursor on row N")
    ap.add_argument("--towns", default="0,1,2,3,4", help="columns of row N")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--room-zoom", type=int, default=1)
    ap.add_argument("--window", default="1920x1080:1", help="WxH[:scaling mode]")
    ap.add_argument("--jobs", type=int, default=5, help="towns checked at once (two instances each)")
    ap.add_argument("--out", type=Path, default=ROOT / "logs/check-towns")
    ap.add_argument("--port", type=int, default=14500)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    towns = [int(t) for t in args.towns.split(",")]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        reports = list(pool.map(lambda t: check_town(args, t), towns))
    failed = 0
    for lines in reports:
        for line in lines:
            print(line)
            failed += "FAILED" in line
    print(f"Towns: {'FAILED' if failed else 'identical'} ({failed} routes differ)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
