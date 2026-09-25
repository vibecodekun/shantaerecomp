"""Check expanded-view captures from view_state_repro.py (no user-save writes)."""
import argparse
import json
from pathlib import Path
import struct


def check(path, width, height, enemy, background, one_screen, offset, room_zoom=False):
    timeline = json.loads((path / "timeline.json").read_text())
    matched = offsets = 0
    zooms = set()
    for frame in timeline:
        info = frame["view"]
        # The surround's scroll must be the one the native frame was drawn
        # with, including lag frames (no seam offset at the native boundary).
        assert (info["scroll_x"] & 255, info["scroll_y"] & 255) == (info["scx"], info["scy"]), \
            (path, frame["elapsed"], "surround scroll differs from the native scroll")
        png = (path / f"frame-{frame['elapsed']:04d}.png").read_bytes()
        # The original view is presented on its own (native_scaling's default),
        # so its captures are 160 x 144; the view's are the requested size, or
        # with room zoom the part of it the zoom keeps.
        size = struct.unpack(">II", png[16:24])
        if one_screen:
            assert size == (160, 144), (path, frame["elapsed"], "capture size", size)
        elif room_zoom:
            assert size == (info["width"], info["height"]) and size[0] <= width and size[1] <= height, \
                (path, frame["elapsed"], "zoomed capture size", size)
            zooms.add(round(info["zoom"], 3))
        else:
            assert size == (width, height), (path, frame["elapsed"], "capture size", size)
        if offset:
            # Bosses move the background by an offset in the scroll; sprites
            # stay placed by the camera alone.
            assert info["oam_checked"] == info["oam_matched"], (path, frame["elapsed"], "sprites differ from OAM")
            offsets += (info["scroll_x"], info["scroll_y"]) != (info["camera_x"], info["camera_y"])
        if one_screen:
            # A room pinned to one screen keeps the native picture and activation.
            assert not info["expanded"] and not info["widened"], (path, frame["elapsed"], "one-screen room expanded")
            continue
        assert info["expanded"], (path, frame["elapsed"], "native fallback")
        assert info["background_checked"] == info["background_matched"], (path, frame["elapsed"], "background differs from hardware")
        matched += info["background_checked"]
    if offset:
        assert offsets, (path, "the background offset was never exercised")
    if one_screen:
        print(f"{path}: {len(timeline)} native frames, {offsets} with a background offset")
        return
    if background and not enemy:
        assert matched > 0, (path, "no background overlap was validated")
    if enemy:
        # The state1 platform enemy at x=760 was absent until the GB camera
        # reached it. It must now be active while ahead of the native view.
        assert any(d["bank"] == 0x7d and d["x"] > frame["view"]["camera_x"] + 160
                   for frame in timeline for d in frame["draws"]), "enemy still waits for native viewport"
    print(f"{path}: {len(timeline)} expanded frames, {matched} matching live background tiles, {offsets} with a background offset"
          + (f", zoom {sorted(zooms)}" if room_zoom else ""))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("captures", type=Path, nargs="+")
    ap.add_argument("--width", type=int, default=256)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--enemy", action="store_true", help="Use only with the state1 enemy route")
    ap.add_argument("--no-background-objects", dest="background", action="store_false",
                    help="The room has no live background objects to compare")
    ap.add_argument("--one-screen", action="store_true",
                    help="A room pinned to one screen (the first boss's arena)")
    ap.add_argument("--offset", action="store_true",
                    help="A boss moves the background offset; sprites must match OAM")
    ap.add_argument("--room-zoom", action="store_true",
                    help="Captured with room zoom on: pictures may be smaller than the view")
    args = ap.parse_args()
    for path in args.captures:
        check(path, args.width, args.height, args.enemy, args.background, args.one_screen, args.offset,
              args.room_zoom)
