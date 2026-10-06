#!/usr/bin/env python3
"""Guide (Xbox) button -> start or focus Pegasus, unless a game launched from it is running."""
import array, fcntl, glob, os, struct, subprocess, time

PAD = "Microsoft X-Box 360 pad"
BTN_MODE = 316
EVENT = struct.Struct("llHHi")


def find_pad():
    for path in sorted(glob.glob("/dev/input/event*")):
        try:
            fd = os.open(path, os.O_RDONLY)
        except OSError:
            continue
        name = array.array("B", [0] * 256)
        fcntl.ioctl(fd, 0x80ff4506, name)  # EVIOCGNAME(255)
        if name.tobytes().split(b"\0")[0].decode() == PAD:
            return fd
        os.close(fd)
    return None


def pgrep(*args):
    r = subprocess.run(["pgrep", *args], capture_output=True, text=True)
    return r.stdout.split()


def on_guide():
    pegasus = pgrep("-x", "pegasus-fe")
    if pegasus and pgrep("-P", pegasus[0]):
        return  # a game is running; leave the button to it
    if pegasus:
        subprocess.run(["swaymsg", "workspace", "number", "1"], capture_output=True)
    else:
        subprocess.run(["swaymsg", "workspace number 1; exec ~/.config/pegasus-frontend/run"], capture_output=True)


while True:
    fd = find_pad()
    if fd is None:
        time.sleep(5)
        continue
    try:
        while True:
            _, _, typ, code, value = EVENT.unpack(os.read(fd, EVENT.size))
            if typ == 1 and code == BTN_MODE and value == 1:
                on_guide()
    except OSError:
        pass  # pad went away (e.g. resume); reopen
    finally:
        os.close(fd)
        time.sleep(1)
