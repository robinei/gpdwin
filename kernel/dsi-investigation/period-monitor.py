# Read-only: log the display frame period (pipe B frame counter + scanline) every 2 s, to test whether the
# split state can be told from the good state (with i915 patch 0005: split = exactly link-paced, 16.5520 ms;
# good = slightly longer). Run as root on the GPD:  sudo python3 period-monitor.py LOG STOPFILE
import mmap, os, struct, sys, time
LOG, STOP = sys.argv[1], sys.argv[2]
fd = os.open('/sys/bus/pci/devices/0000:00:02.0/resource0', os.O_RDONLY)
p = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x1f1000)
d = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x18b000)
def r(m, o): return struct.unpack('<I', m[o:o+4])[0]
def sample():
    for _ in range(1000):
        f = r(p, 0x40); t = time.clock_gettime(time.CLOCK_MONOTONIC); s = r(p, 0) & 0x1fff; f2 = r(p, 0x40)
        if f == 0xffffffff: return None                # output off
        if f == f2 and 10 < s < 1250: return t, f, s
        time.sleep(0.001)
    return None
log = open(LOG, 'a', buffering=1)
log.write(f'{time.strftime("%H:%M:%S")} start\n')
prev = sample(); t0 = time.time()
while not os.path.exists(STOP) and time.time() - t0 < 3 * 3600:
    time.sleep(2)
    cur = sample()
    if cur and prev and cur[1] > prev[1]:
        fr = (cur[1] - prev[1]) + (cur[2] - prev[2]) / 1308
        latch = (r(d, 0x804) >> 20) & 1
        log.write(f'{time.strftime("%H:%M:%S")} {(cur[0] - prev[0]) / fr * 1e3:.4f} latch={latch}\n')
    elif cur is None:
        log.write(f'{time.strftime("%H:%M:%S")} off\n')
    prev = cur
log.write(f'{time.strftime("%H:%M:%S")} stop\n')
