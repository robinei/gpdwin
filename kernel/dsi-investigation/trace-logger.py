# Read-only DSI underrun logger with i915 tracepoints (run as root on the GPD; reads registers, never writes them).
#   sudo python3 trace-logger.py OUTDIR STOPFILE
# - OUTDIR/events.log: every DPI underrun latch edge (MIPI_INTR_STAT bit 20) and output on/off, plus the rare
#   display events from tracefs instance "gpdrare": non-primary plane updates/disables (cursor, sprites), cxsr
#   on/off, VLV FIFO re-split, watermark writes, pipe enable/disable, CPU FIFO underrun.
# - OUTDIR/underrun-HHMMSS.trace: on each latch edge, the last 20 s of the full trace (instance "gpdfull":
#   every plane update and pipe update incl. vblank evasion), to see what happened right before.
# - A line with event counts every 60 s, so sessions with and without glitches can be compared.
# Stops at STOPFILE or after 3 hours; removes its tracefs instances at exit. A panel reset re-arms the latch.
import mmap, os, struct, subprocess, sys, threading, time
OUT, STOP = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
T = '/sys/kernel/tracing'
RARE = ['i915/intel_plane_update_arm', 'i915/intel_plane_disable_arm', 'i915/intel_memory_cxsr',
        'i915/vlv_fifo_size', 'i915/vlv_wm', 'i915/intel_pipe_enable', 'i915/intel_pipe_disable',
        'i915/intel_cpu_fifo_underrun']
FULL = ['i915/intel_plane_update_arm', 'i915/intel_plane_disable_arm', 'i915/intel_memory_cxsr',
        'i915/vlv_fifo_size', 'i915/vlv_wm', 'i915/intel_pipe_update_start', 'i915/intel_pipe_update_vblank_evaded',
        'i915/intel_pipe_update_end', 'i915/intel_crtc_flip_done', 'i915/intel_cpu_fifo_underrun']

def w(path, val):
    with open(path, 'w') as f: f.write(val)

def setup(name, events, kb, rare):
    d = f'{T}/instances/{name}'
    if os.path.isdir(d): os.rmdir(d)
    os.mkdir(d)
    w(f'{d}/trace_clock', 'mono')
    w(f'{d}/buffer_size_kb', str(kb))
    for e in events:
        if rare and e == 'i915/intel_plane_update_arm':
            w(f'{d}/events/{e}/filter', 'name !~ "primary*"')   # primary flips happen every frame
        w(f'{d}/events/{e}/enable', '1')
    w(f'{d}/tracing_on', '1')
    return d

full = setup('gpdfull', FULL, 4096, False)
rare = setup('gpdrare', RARE, 512, True)
log = open(f'{OUT}/events.log', 'a', buffering=1)
def L(s): log.write(f'{time.strftime("%H:%M:%S")} {s}\n')

counts = {}
lock = threading.Lock()
done = threading.Event()
def pump():
    # non-blocking reads, so the instance can be removed at exit (an open trace_pipe keeps it busy)
    fdp = os.open(f'{rare}/trace_pipe', os.O_RDONLY | os.O_NONBLOCK)
    buf = b''
    while not done.is_set():
        try:
            chunk = os.read(fdp, 65536)
        except BlockingIOError:
            chunk = b''
        if not chunk:
            time.sleep(0.2); continue
        buf += chunk
        *lines, buf = buf.split(b'\n')
        for raw in lines:
            line = raw.decode(errors='replace')
            ev = next((x.rstrip(':') for x in line.split() if x.startswith(('intel_', 'vlv_'))), '?')
            with lock: counts[ev] = counts.get(ev, 0) + 1
            if ev != 'vlv_wm' or counts[ev] <= 200:   # in case watermark writes turn out to be per-frame
                log.write(line + '\n')
    os.close(fdp)
pt = threading.Thread(target=pump, daemon=True)
pt.start()

fd = os.open('/sys/bus/pci/devices/0000:00:02.0/resource0', os.O_RDONLY)
dsi = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x18b000)
def rd(o): return struct.unpack('<I', dsi[o:o+4])[0]
def state():
    u = rd(0x804)
    return None if u == 0xffffffff else (u >> 20) & 1

def snapshot():
    now = time.clock_gettime(time.CLOCK_MONOTONIC)
    name = f'{OUT}/underrun-{time.strftime("%H%M%S")}.trace'
    with open(f'{full}/trace') as src, open(name, 'w') as dst:
        for line in src:
            if line.startswith('#'): continue
            try:
                ts = float(line.split(': ', 1)[0].split()[-1])
            except ValueError:
                continue
            if ts >= now - 20: dst.write(line)
    return name

prev = state(); t0 = last = time.time()
L(f'start latch={prev}')
try:
    while not os.path.exists(STOP) and time.time() - t0 < 3 * 3600:
        s = state()
        if (s is None) != (prev is None):
            L('output ' + ('OFF' if s is None else f'ON latch={s}'))
        elif s == 1 and prev == 0:
            top = subprocess.run(['ps', '-eo', 'pcpu,comm', '--sort=-pcpu'], capture_output=True, text=True).stdout.splitlines()[1:4]
            L(f'UNDERRUN latch 0->1  trace={os.path.basename(snapshot())}  top: ' + ' | '.join(x.strip() for x in top))
        prev = s
        if time.time() - last >= 60:
            with lock: c = dict(counts); counts.clear()
            L('counts/60s ' + (' '.join(f'{k}={v}' for k, v in sorted(c.items())) or 'none'))
            last = time.time()
        time.sleep(0.1)
finally:
    done.set(); pt.join(timeout=5)
    L('stop')
    for d, evs in ((full, FULL), (rare, RARE)):
        try:
            w(f'{d}/tracing_on', '0')
            for e in evs: w(f'{d}/events/{e}/enable', '0')
        except OSError: pass
    time.sleep(0.5)
    for d in (full, rare):
        try: os.rmdir(d)
        except OSError as e: L(f'could not remove {d}: {e}')
