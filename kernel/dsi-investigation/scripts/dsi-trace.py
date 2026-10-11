#!/usr/bin/env python3
"""Capture what the system did around each DSI DPI FIFO underrun (runs ON THE GPD, as root).

Needs the i915 module with patch 0018 booted with i915.vlv_dsi_ur_irq=1: its interrupt handler calls
vlv_dsi_ur_irq_event(frame, scanline, stat), which a kprobe here turns into a trace event with a
microsecond timestamp. Read-only towards the hardware: it only uses ftrace.

Two tracefs instances:
  dsi    rolling buffer (overwrite) with flips/plane updates, cxSR/watermark changes, GPU requests,
         vblank events, all interrupts, CPU idle/frequency, I2C/SMBus transfers (the PMIC bus), i915
         Punit sideband and iosf_mbi accesses (incl. the PMIC bus semaphore), and the underrun probe.
  dsiur  only the underrun probe; its trace_pipe wakes this script.
On an underrun: wait 150 ms (the second underrun of a pair, the stall), stop the dsi buffer, save it
to OUT/<time>-ur.txt.zst, clear it, restart. At random times (mean CONTROL_S) the same is done
without an underrun (-ctl), as the baseline for the comparison (scripts/dsi-trace-analyze.py).
All timestamps are CLOCK_MONOTONIC (= the kernel's ktime_get(), = journalctl -o short-monotonic).

  sudo systemd-run --unit=dsi-trace python3 ~/gpd/kernel/dsi-investigation/scripts/dsi-trace.py
  journalctl -u dsi-trace -f          progress
  sudo systemctl stop dsi-trace       stop (removes the instances and probes)
"""
import glob, os, random, select, signal, subprocess, sys, time

T = '/sys/kernel/tracing'
INST, DET = T + '/instances/dsi', T + '/instances/dsiur'
OUT = '/var/tmp/dsi-trace'
BUF_KB = int(os.environ.get('BUF_KB', '4096'))  # per CPU
AFTER_S = 0.15
CONTROL_S = float(os.environ.get('CONTROL_S', '90'))
MIN_GAP_S = 2.0
MAX_MB = 1500

KPROBES = [
    # patch 0018 hook (module function)
    'p:dsi/ur i915:vlv_dsi_ur_irq_event frame=$arg1:u32 line=$arg2:u32 stat=$arg3:x32',
    # i915 IOSF sideband (Punit, CCK, DPIO...): unit, register
    'p:dsi/sb_rd i915:vlv_iosf_sb_read unit=$arg2:u32 addr=$arg3:x32',
    'p:dsi/sb_wr i915:vlv_iosf_sb_write unit=$arg2:u32 addr=$arg3:x32 val=$arg4:x32',
    # iosf_mbi (built in): any message bus access, and the PMIC I2C bus semaphore
    'p:dsi/mbi_rd iosf_mbi_read port=$arg1:u8 op=$arg2:u8 off=$arg3:x32',
    'p:dsi/mbi_wr iosf_mbi_write port=$arg1:u8 op=$arg2:u8 off=$arg3:x32 val=$arg4:x32',
    'p:dsi/mbi_mod iosf_mbi_modify port=$arg1:u8 op=$arg2:u8 off=$arg3:x32 val=$arg4:x32 mask=$arg5:x32',
    'p:dsi/punit_i2c_block iosf_mbi_block_punit_i2c_access',
    'p:dsi/punit_i2c_unblock iosf_mbi_unblock_punit_i2c_access',
    'p:dsi/punit_acquire iosf_mbi_punit_acquire',
    'p:dsi/punit_release iosf_mbi_punit_release',
]
EVENTS = [
    'dsi',  # all the probes above
    'i915/intel_plane_update_arm', 'i915/intel_plane_update_noarm', 'i915/intel_plane_disable_arm',
    'i915/intel_pipe_update_start', 'i915/intel_pipe_update_vblank_evaded', 'i915/intel_pipe_update_end',
    'i915/intel_crtc_flip_done', 'i915/intel_crtc_vblank_work_start', 'i915/intel_crtc_vblank_work_end',
    'i915/intel_memory_cxsr', 'i915/vlv_wm', 'i915/vlv_fifo_size', 'i915/intel_cpu_fifo_underrun',
    'i915/intel_pipe_enable', 'i915/intel_pipe_disable',
    'i915/i915_request_add', 'i915/i915_request_retire',
    'i915/i915_request_wait_begin', 'i915/i915_request_wait_end',
    'drm/drm_vblank_event',
    'irq/irq_handler_entry', 'irq/irq_handler_exit',
    'power/cpu_idle', 'power/cpu_frequency',
    'i2c', 'smbus',
]


def w(path, val, append=False):
    with open(path, 'a' if append else 'w') as f:
        f.write(val + '\n')


def log(*a):
    print(*a, flush=True)


def mono():
    return time.clock_gettime(time.CLOCK_MONOTONIC)


def teardown():
    for inst in (INST, DET):
        if os.path.isdir(inst):
            try:
                w(inst + '/tracing_on', '0')
                w(inst + '/events/enable', '0')
            except OSError:
                pass
            try:
                os.rmdir(inst)
            except OSError as e:
                log('rmdir', inst, e)
    for p in KPROBES:
        name = p.split()[0].split(':', 1)[1]
        try:
            w(T + '/kprobe_events', '-:' + name, append=True)
        except OSError:
            pass


def setup():
    teardown()
    os.mkdir(INST)
    os.mkdir(DET)
    ok = []
    for p in KPROBES:
        try:
            w(T + '/kprobe_events', p, append=True)
            ok.append(p.split()[0][2:])
        except OSError as e:
            log('kprobe not available:', p.split()[1], e)
    if 'dsi/ur' not in ok:
        log('WARNING: no underrun probe (patch 0018 module not loaded?): control dumps only')
    for inst, kb in ((INST, BUF_KB), (DET, 64)):
        w(inst + '/trace_clock', 'mono')
        w(inst + '/buffer_size_kb', str(kb))
        w(inst + '/tracing_on', '0')
    for ev in EVENTS:
        try:
            w(INST + '/events/' + ev + '/enable', '1')
        except OSError as e:
            log('event not available:', ev, e)
    if 'dsi/ur' in ok:
        w(DET + '/events/dsi/ur/enable', '1')
    w(INST + '/tracing_on', '1')
    w(DET + '/tracing_on', '1')


def oldest_ts():
    """Oldest event still in the dsi buffer, over all CPUs (= how far back a dump reaches)."""
    best = 0.0
    for st in glob.glob(INST + '/per_cpu/cpu*/stats'):
        for line in open(st):
            if line.startswith('oldest event ts:'):
                best = max(best, float(line.split(':', 1)[1]))
    return best


def out_mb():
    return sum(os.path.getsize(f) for f in glob.glob(OUT + '/*')) / 1e6


zstd = None


def dump(kind, ref):
    global zstd
    w(INST + '/tracing_on', '0')
    stop = mono()
    covered = stop - oldest_ts()
    name = time.strftime('%Y%m%d-%H%M%S') + '-' + kind
    raw = OUT + '/' + name + '.txt'
    with open(raw, 'w') as f:
        f.write('# kind %s ref %.6f stop %.6f covered_s %.3f\n' % (kind, ref, stop, covered))
        with open(INST + '/trace') as t:
            while True:
                b = t.read(1 << 20)
                if not b:
                    break
                f.write(b)
    w(INST + '/trace', '')
    w(INST + '/tracing_on', '1')
    if zstd:
        zstd.wait()
    os.chmod(raw, 0o644)
    zstd = subprocess.Popen(['zstd', '-q', '--rm', '-3', raw])
    log('%s ref %.6f: dumped %.2f s back, %.1f s to save, out %.0f MB'
        % (kind, ref, covered, mono() - stop, out_mb()))


def main():
    os.makedirs(OUT, exist_ok=True)
    os.chmod(OUT, 0o755)
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
    setup()
    log('tracing; buffer %d KB/CPU; out %s' % (BUF_KB, OUT))
    pipe = os.open(DET + '/trace_pipe', os.O_RDONLY | os.O_NONBLOCK)
    buf = b''
    next_ctl = mono() + random.expovariate(1 / CONTROL_S)
    last_dump = 0.0
    try:
        while True:
            if out_mb() > MAX_MB:
                log('output over %d MB, stopping' % MAX_MB)
                break
            r, _, _ = select.select([pipe], [], [], max(0.0, next_ctl - mono()))
            if r:
                try:
                    buf += os.read(pipe, 65536)
                except BlockingIOError:
                    pass
                lines = buf.split(b'\n')
                buf = lines.pop()
                urs = []
                for line in lines:
                    # "  <idle>-0  [001] d.h1.  1234.567890: ur: (vlv_dsi_ur_irq_event+0x4/0x10 [i915]) frame=..."
                    if b' ur: ' in line:
                        try:
                            urs.append(float(line.split(b': ur: ')[0].split()[-1]))
                        except ValueError:
                            pass
                        log('underrun', line.split(b') ', 1)[-1].decode(errors='replace'))
                if urs and urs[0] > last_dump:
                    time.sleep(AFTER_S)
                    if mono() - last_dump >= MIN_GAP_S:
                        dump('ur', urs[0])
                        last_dump = mono()
                continue
            if mono() >= next_ctl:
                if mono() - last_dump >= MIN_GAP_S:
                    dump('ctl', mono() - AFTER_S)
                    last_dump = mono()
                next_ctl = mono() + random.expovariate(1 / CONTROL_S)
    finally:
        if zstd:
            zstd.wait()
        teardown()


if __name__ == '__main__':
    main()
