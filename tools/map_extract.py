"""Shantae's world from the ROM: rooms, maps and what is placed in them.

The research side of the map (map.c). Everything here is read from
roms/shantae.gbc as the game reads it, and checked against vgmaps.de's maps
(--vgmaps): qwert_vs_qwerty's day maps of each area are pixel crops of the
same 256 x 256 sectors.

Rooms. A room is loaded by 03:7920, reached by a far call (CALL 00:0540,
then 03:7920 inline) with its 17-byte header inline after it: the camera's
least x and y (C9E9-C9EA, C9EC-C9ED, also C9DB-C9DC and C9DF-C9E0), the
camera range's width and height (C9F4-C9F7; the greatest x and y, C9DD-C9DE
and C9E1-C9E2, are least + size - 1), C9F8, the map directory's page and bank
(C9F9-C9FA), FFDD, FFDE, the metatile bank FFDF, C399 and the spawn record
directory's page and bank (C39A-C39B). The room is the camera's range plus
the screen (160 x 144). 112 such calls are in the ROM.

Maps. A map is 32 x 32 sectors of 256 x 256 pixels. Its directory is a page
in the map bank, two bytes a sector (row-major): the sector's cell page and
cell bank, a page of 16 x 16 cells of 16 x 16 pixels, each two bytes: the low
nibble of the first a metatile bank offset from FFDF, the rest the metatile's
address. A metatile is 16 bytes: 4 tiles, 4 attributes, 4 collision bytes and
4 property bytes, for its four 8 x 8 quarters.

Spawn records. The record directory has a pointer and a bank for each
sector (4 bytes a sector, two rows of sectors a page); each list is a count
and 17-byte records: the box's right, left, bottom and top (+0, +2, +4, +6),
the flag (+8: the bit in CB1B+ that says it is defeated or collected), the
object (+10 address, +12 bank: a 4-byte stub in the object table 0A:4000,
"op 40" to its script) and four bytes of parameters (+13).

usage: python tools/map_extract.py rooms
       python tools/map_extract.py records [--map 74AB]
       python tools/map_extract.py types
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM = (ROOT / "roms/shantae.gbc").read_bytes()

ROOM_LOADER = bytes.fromhex("cd400520 7903".replace(" ", ""))   # CALL 0540, 03:7920 inline
OBJECT_TABLE = (0x0A, 0x4000)


def r8(bank, addr):
    if addr < 0x4000:
        return ROM[addr]
    o = bank * 0x4000 + addr - 0x4000
    return ROM[o] if 0 <= o < len(ROM) else 0xFF


def r16(bank, addr):
    return r8(bank, addr) | r8(bank, addr + 1) << 8


def addr_of(offset):
    """(bank, address) of a ROM offset, as a CALL in that bank sees it."""
    return (0, offset) if offset < 0x4000 else (offset // 0x4000, 0x4000 + offset % 0x4000)


class Room:
    def __init__(self, bank, at, raw):
        self.at = (bank, at)
        self.x, self.y = raw[0] | raw[1] << 8, raw[2] | raw[3] << 8
        self.w, self.h = raw[4] | raw[5] << 8, raw[6] | raw[7] << 8
        self.c9f8 = raw[8]
        self.map = (raw[9], raw[10])            # directory page, bank
        self.ffdd, self.ffde, self.ffdf = raw[11], raw[12], raw[13]
        self.c399 = raw[14]
        self.records = (raw[15], raw[16])       # record directory page, bank

    @property
    def box(self):
        """The room in world pixels: x0, y0, x1, y1 (exclusive)."""
        w = self.w if self.w < 0x8000 else 0
        h = self.h if self.h < 0x8000 else 0
        return self.x, self.y, self.x + max(w, 1) - 1 + 160, self.y + max(h, 1) - 1 + 144

    def key(self):
        return "%02X%02X" % self.map

    def __repr__(self):
        return "Room(%02X:%04X map %s box %s ffdf %02X rec %02X%02X)" % (
            self.at + (self.key(), self.box, self.ffdf) + self.records)


def rooms():
    """Every room header in the ROM."""
    out = []
    start = 0
    while True:
        i = ROM.find(ROOM_LOADER, start)
        if i < 0:
            break
        start = i + 1
        bank, at = addr_of(i)
        raw = ROM[i + len(ROOM_LOADER):i + len(ROOM_LOADER) + 17]
        if raw[9] == 0 and raw[10] == 0:
            continue   # a header filled in at run time (C2xx), not a room of its own
        out.append(Room(bank, at, raw))
    return out


class Map:
    def __init__(self, page, bank, ffdf, rec_page, rec_bank):
        self.page, self.bank, self.ffdf = page, bank, ffdf
        self.rec_page, self.rec_bank = rec_page, rec_bank

    @classmethod
    def of(cls, room):
        return cls(room.map[0], room.map[1], room.ffdf, room.records[0], room.records[1])

    def sector(self, sx, sy):
        e = (self.page << 8) + sy * 64 + sx * 2
        return r8(self.bank, e), r8(self.bank, e + 1)

    def drawn(self, sx, sy):
        p, b = self.sector(sx, sy)
        return 0x40 <= p < 0x80 and (b + 1) * 0x4000 <= len(ROM)

    def metatile(self, x, y):
        p, b = self.sector(x >> 8, y >> 8)
        if not (0x40 <= p < 0x80):
            return None
        c = (p << 8) + ((y & 255) >> 4) * 32 + ((x & 255) >> 4) * 2
        lo, hi = r8(b, c), r8(b, c + 1)
        return self.ffdf + (lo & 15), (hi << 8) | (lo & 0xF0)

    @staticmethod
    def quarter(x, y):
        return ((y & 8) >> 2) + ((x & 8) >> 3)

    def collision(self, x, y):
        m = self.metatile(x, y)
        return None if m is None else r8(m[0], m[1] + 8 + self.quarter(x, y))

    def records(self, sx, sy):
        e = ((self.rec_page + (sy >> 1)) & 0xFF) << 8 | ((sy & 1) << 7) + sx * 4
        ptr, bank = r16(self.rec_bank, e), r8(self.rec_bank, e + 2)
        if not ptr:
            return []
        out = []
        for i in range(r8(bank, ptr)):
            a = ptr + 1 + i * 17
            raw = bytes(r8(bank, a + k) for k in range(17))
            out.append(dict(at="%02X:%04X" % (bank, a),
                            x0=raw[2] | raw[3] << 8, x1=raw[0] | raw[1] << 8,
                            y0=raw[6] | raw[7] << 8, y1=raw[4] | raw[5] << 8,
                            flag=raw[8] | raw[9] << 8, object="%02X:%04X" % (raw[12], raw[10] | raw[11] << 8),
                            params=raw[13:].hex()))
        return out

    def all_records(self):
        """Every record of the map once (a record listed by several sectors is one)."""
        seen = {}
        for sy in range(32):
            for sx in range(32):
                for rec in self.records(sx, sy):
                    seen.setdefault(rec["at"], dict(rec, sector=[sx, sy]))
        return list(seen.values())


def object_script(obj):
    """The script an object table stub goes to (op 40: address, bank), or None."""
    bank, addr = int(obj[:2], 16), int(obj[3:], 16)
    if r8(bank, addr) != 0x40:
        return None
    return "%02X:%04X" % (r8(bank, addr + 3), r16(bank, addr + 1))


def maps_of_rooms(all_rooms):
    """One Map for each map directory the rooms use (the first room's record
    directory and metatile bank)."""
    out = {}
    for r in all_rooms:
        out.setdefault(r.key(), (Map.of(r), r))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["rooms", "records", "types"])
    ap.add_argument("--map", help="only this map directory, e.g. 74AB")
    ap.add_argument("--json", type=Path, help="write the result here")
    args = ap.parse_args()
    all_rooms = rooms()
    if args.what == "rooms":
        for r in all_rooms:
            print(r)
        print(len(all_rooms), "rooms in", len({r.key() for r in all_rooms}), "maps")
        if args.json:
            args.json.write_text(json.dumps([dict(at="%02X:%04X" % r.at, map=r.key(), box=r.box, ffdf=r.ffdf,
                                                  records="%02X%02X" % r.records, c9f8=r.c9f8, c399=r.c399)
                                             for r in all_rooms], indent=1))
        return
    maps = maps_of_rooms(all_rooms)
    out = []
    for key, (m, room) in sorted(maps.items()):
        if args.map and key != args.map.upper():
            continue
        for rec in m.all_records():
            out.append(dict(rec, map=key, script=object_script(rec["object"])))
    if args.what == "records":
        for rec in out:
            print(rec["map"], rec["at"], rec["object"], rec["script"], rec["x0"], rec["y0"], rec["flag"], rec["params"])
    else:
        by = defaultdict(list)
        for rec in out:
            by[rec["object"]].append(rec)
        for obj, lst in sorted(by.items(), key=lambda kv: -len(kv[1])):
            print(obj, object_script(obj), len(lst), sorted({r["map"] for r in lst}))
    print(len(out), "records")
    if args.json:
        args.json.write_text(json.dumps(out, indent=0))


if __name__ == "__main__":
    main()
