"""Capture reproducible view-mod evidence in an isolated game instance."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


class Debug:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
        self.file = self.sock.makefile("rwb")
        self.seq = 0

    def read(self):
        line = self.file.readline()
        if not line:
            raise RuntimeError("Debug server disconnected")
        return json.loads(line)

    def command(self, cmd, **args):
        self.seq += 1
        self.file.write((json.dumps(dict(cmd=cmd, id=self.seq, **args)) + "\n").encode())
        self.file.flush()
        while True:
            reply = self.read()
            if reply.get("id") == self.seq:
                if not reply.get("ok"):
                    raise RuntimeError(reply)
                return reply

    def peek(self, addr, length, **banks):
        reply = self.command("peek", addr=hex(addr), len=length, **banks)
        data = bytes.fromhex(reply["hex"])
        while len(data) < length:
            reply = self.read()
            if reply.get("id") == self.seq:
                data += bytes.fromhex(reply["hex"])
        return data.hex()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--frames", default="1500,7000,7500,8500")
    ap.add_argument("--out", type=Path, default=ROOT / "logs/view-probe")
    ap.add_argument("--settings", default="")
    ap.add_argument("--input", type=Path, default=ROOT / "logs/pit-route.input")
    ap.add_argument("--port", type=int, default=14370)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="shantae-view-") as state:
        Path(state, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(state, "shantae.ini").write_text(args.settings.replace(",", "\n") + "\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=state, GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_DEBUG_PORT=str(args.port))
        command = [str(ROOT / "generated/build/shantae.exe"), "--benchmark",
                   "--input", args.input.read_text().strip(), "--report-interpreter-hotspots"]
        with (args.out / "run.log").open("w") as log:
            proc = subprocess.Popen(command, cwd=state, env=env, stdout=log,
                                    stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                deadline = time.monotonic() + 20
                while True:
                    try:
                        client = Debug(args.port)
                        break
                    except OSError:
                        if proc.poll() is not None or time.monotonic() >= deadline:
                            raise
                        time.sleep(0.02)
                client.command("pause")
                for frame in map(int, args.frames.split(",")):
                    client.command("run_to_frame", frame=frame)
                    while client.read().get("event") != "run_to_done":
                        pass
                    shot = str((args.out / f"frame-{frame}.png").resolve()).replace("\\", "/")
                    client.command("screenshot", path=shot)
                    snap = dict(frame=frame, ppu=client.command("ppu_state"),
                                hw=client.command("hw_state"), regs=client.command("get_registers"))
                    snap["wram"] = client.peek(0xc000, 4096) + "".join(
                        client.peek(0xd000, 4096, wram_bank=b) for b in range(1, 8))
                    snap["vram"] = "".join(client.peek(0x8000, 8192, vram_bank=b) for b in range(2))
                    snap["hram"] = client.peek(0xff80, 127)
                    snap["oam"] = client.peek(0xfe00, 160)
                    snap["view"] = client.command("shantae_view_info")
                    snap["draws"] = [client.read() for _ in range(snap["view"]["count"])]
                    (args.out / f"frame-{frame}.json").write_text(json.dumps(snap))
                    client.command("save_state", path=str((args.out / f"frame-{frame}.state").resolve()).replace("\\", "/"))
                    print(f"Captured {frame}", flush=True)
                client.command("quit")
                proc.wait(timeout=10)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)


if __name__ == "__main__":
    main()
