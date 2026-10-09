# Read-only: log every change of bit 31 of 0x1e1708 and 0x1e170c, and of the underrun latch 0x18b804 bit 20,
# with timestamps and the busiest process names. Stops at the stop file or after 3 hours.
import mmap, os, struct, subprocess, sys, time
LOG, STOP = sys.argv[1], sys.argv[2]
fd = os.open('/sys/bus/pci/devices/0000:00:02.0/resource0', os.O_RDONLY)
pg = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x1e1000)
dsi = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x18b000)
def rd(m, o): return struct.unpack('<I', m[o:o+4])[0]
def state():
    a, b, u, t = rd(pg, 0x708), rd(pg, 0x70c), rd(dsi, 0x804), rd(dsi, 0x810)
    if a == 0xffffffff: return None            # output off
    return ((a >> 31) & 1, (b >> 31) & 1, (u >> 20) & 1, t, a, b, u)
def ctx():
    try:
        out = subprocess.run(['ps', '-eo', 'pcpu,comm', '--sort=-pcpu'], capture_output=True, text=True).stdout.splitlines()[1:4]
        return ' | '.join(o.strip() for o in out)
    except Exception: return ''
log = open(LOG, 'a', buffering=1)
prev = state(); t0 = time.time()
log.write('start %s state=%s\n' % (time.strftime('%H:%M:%S'), prev and tuple(hex(x) for x in prev[:4])))
while not os.path.exists(STOP) and time.time() - t0 < 3 * 3600:
    s = state()
    if (s is None) != (prev is None):
        log.write('%s output %s\n' % (time.strftime('%H:%M:%S'), 'OFF' if s is None else 'ON ' + str(s[:3])))
    elif s and prev and s[:4] != prev[:4]:
        log.write('%s CHANGE hs_timeout %06x->%06x  underrun %d->%d  b31(1708) %d->%d  (1708=%08x 170c=%08x)  top: %s\n' % (
            time.strftime('%H:%M:%S'), prev[3], s[3], prev[2], s[2], prev[0], s[0], s[4], s[5], ctx()))
    prev = s
    time.sleep(0.1)
log.write('stop %s\n' % time.strftime('%H:%M:%S'))
