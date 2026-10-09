#!/usr/bin/env python3
"""Make a GameMaker game start in fullscreen: set the "Fullscreen" bit in the GEN8 header's info flags.

  gm-start-fullscreen.py [--undo] DATAFILE      (game.unx on Linux, data.win on Windows)

Undertale (Linux, assets/game.unx) does not remember F4; with this bit it starts fullscreen (F4 still
toggles). Same idea as the Windows exe/data.win mods. 4 bytes change; the original is kept as
DATAFILE.orig on the first run. Idempotent. Re-run after the game is reinstalled or updated.
"""
import shutil, struct, sys

FULLSCREEN = 0x1
args = [a for a in sys.argv[1:] if not a.startswith("--")]
undo = "--undo" in sys.argv
if len(args) != 1:
    sys.exit(__doc__)
path = args[0]
with open(path, "r+b") as f:
    head = f.read(16 + 72)
    if head[:4] != b"FORM" or head[8:12] != b"GEN8":
        sys.exit("not a GameMaker data file (no FORM/GEN8)")
    g = head[16:]                       # GEN8 chunk data
    bytecode = g[1]
    width, height, info = struct.unpack_from("<III", g, 60)
    if not (200 <= width <= 8192 and 200 <= height <= 8192):
        sys.exit(f"unexpected window size {width}x{height}: layout differs, refusing to patch")
    off = 16 + 68
    print(f"{path}: bytecode {bytecode}, window {width}x{height}, info flags 0x{info:x}")
    new = info & ~FULLSCREEN if undo else info | FULLSCREEN
    if new == info:
        sys.exit("nothing to do (already " + ("off" if undo else "on") + ")")
    if not undo:
        try:
            with open(path + ".orig", "xb") as bak:     # first run only
                f.seek(0)
                shutil.copyfileobj(f, bak)
            print("backup:", path + ".orig")
        except FileExistsError:
            pass
    f.seek(off)
    f.write(struct.pack("<I", new))
print(f"info flags 0x{info:x} -> 0x{new:x} (fullscreen {'off' if undo else 'on'})")
