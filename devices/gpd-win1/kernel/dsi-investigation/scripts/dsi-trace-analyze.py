#!/usr/bin/env python3
"""Compare the trace before DSI underruns with the trace before random moments (dsi-trace.py output).

  dsi-trace-analyze.py DIR [-v]      DIR = a copy of /var/tmp/dsi-trace (*-ur.txt.zst, *-ctl.txt.zst)

Reference time t0: the first underrun probe event in a -ur dump, the recorded random time in a -ctl
dump. For every event type: how often it occurred within 1, 5, 20 and 100 ms before t0, and the
median time since its last occurrence, underrun dumps against control dumps. An event that comes
before underruns much more often than before random moments is a suspect. Also: frame position of
the underrun and of the last plane update arm on the DSI pipe, and what each CPU was doing at t0.
"""
import collections, glob, os, re, statistics, subprocess, sys

LINE = re.compile(r'^\s*(.+?)-(\d+)\s+\[(\d+)\]\s+\S+\s+([\d.]+):\s+(\S+):\s?(.*)$')
WINDOWS_MS = (1, 5, 20, 100)
VTOTAL, VBLANK_START = 1308, 1280


def read(path):
    if path.endswith('.zst'):
        return subprocess.run(['zstd', '-dcq', path], capture_output=True, text=True).stdout
    return open(path).read()


def field(rest, name):
    m = re.search(r'\b' + name + r'=([^\s,]+)', rest)
    return m.group(1) if m else None


def parse(path):
    text = read(path)
    head = text.split('\n', 1)[0].split()
    meta = dict(zip(head[1::2], head[2::2]))
    events = []
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            comm, pid, cpu, ts, ev, rest = m.groups()
            events.append((float(ts), int(cpu), ev, rest, comm))
    events.sort(key=lambda e: e[0])
    return meta, events


def key(ev, rest):
    """Event type, split by the detail that matters (irq name, i2c adapter, sideband unit...)."""
    if ev == 'irq_handler_entry':
        return 'irq ' + (field(rest, 'name') or '?')
    if ev in ('irq_handler_exit', 'cpu_idle', 'cpu_frequency'):
        return None  # handled separately
    if ev in ('i2c_write', 'i2c_read', 'i2c_reply', 'i2c_result', 'smbus_write', 'smbus_read',
              'smbus_reply', 'smbus_result'):
        m = re.match(r'(i2c-\d+)', rest)
        return ev + ' ' + (m.group(1) if m else '?')
    if ev in ('sb_rd', 'sb_wr'):
        return ev + ' unit ' + (field(rest, 'unit') or '?')
    if ev in ('mbi_rd', 'mbi_wr', 'mbi_mod'):
        return ev + ' port ' + (field(rest, 'port') or '?')
    if ev.startswith('intel_plane') or ev.startswith('intel_pipe_update'):
        m = re.search(r'pipe (\w)', rest)
        return ev + (' pipe ' + m.group(1) if m else '')
    return ev


def main():
    d = sys.argv[1]
    verbose = '-v' in sys.argv
    files = sorted(glob.glob(os.path.join(d, '*-ur.txt*')) + glob.glob(os.path.join(d, '*-ctl.txt*')))
    since = {'ur': collections.defaultdict(list), 'ctl': collections.defaultdict(list)}
    n = collections.Counter()
    idle_at = {'ur': collections.Counter(), 'ctl': collections.Counter()}
    ur_pos, arm_pos = [], {'ur': [], 'ctl': []}
    for path in files:
        meta, events = parse(path)
        kind = meta.get('kind', 'ctl')
        if kind == 'ur':
            urs = [e for e in events if e[2] == 'ur']
            if not urs:
                print('no ur event in', path)
                continue
            t0 = urs[0][0]
            ur_pos.append(int(field(urs[0][3], 'line')))
        else:
            t0 = float(meta['ref'])
        covered = t0 - events[0][0] if events else 0
        if covered < 0.1:
            print('%s: only %.3f s before t0, skipped' % (os.path.basename(path), covered))
            continue
        n[kind] += 1
        last = {}
        idle = {}
        last_arm = None
        for ts, cpu, ev, rest, comm in events:
            if ts >= t0:
                break
            if ev == 'cpu_idle':
                st = int(field(rest, 'state'))
                idle[cpu] = 'run' if st == 4294967295 else 'C%d' % st
                continue
            k = key(ev, rest)
            if k:
                last[k] = ts
            if ev == 'intel_plane_update_arm' and 'pipe B' in rest:
                last_arm = (ts, int(field(rest, 'scanline')), int(field(rest, 'frame')))
        for k, ts in last.items():
            since[kind][k].append((t0 - ts) * 1000)
        for cpu, st in idle.items():
            idle_at[kind][st] += 1
        if last_arm:
            arm_pos[kind].append((round((t0 - last_arm[0]) * 1000, 2), last_arm[1]))
        if verbose:
            print('%-32s t0 %.6f covered %.2f s, last arm %s' % (os.path.basename(path), t0, covered,
                  last_arm and '%.2f ms before, scanline %d' % ((t0 - last_arm[0]) * 1000, last_arm[1])))
    print('dumps: %d underrun, %d control' % (n['ur'], n['ctl']))
    if not n['ur'] or not n['ctl']:
        return
    if ur_pos:
        b = collections.Counter(min(p // 100 * 100, 1300) for p in ur_pos)
        print('\nunderrun scanline (irq-timed):', ' '.join('%d:%d' % kv for kv in sorted(b.items())))
    print('\nCPU state at t0 (per CPU):')
    for kind in ('ur', 'ctl'):
        tot = sum(idle_at[kind].values()) or 1
        print('  %-4s' % kind, '  '.join('%s %.0f%%' % (s, 100 * c / tot) for s, c in sorted(idle_at[kind].items())))
    print('\nlast plane update arm on pipe B before t0 (ms before, its scanline):')
    for kind in ('ur', 'ctl'):
        v = arm_pos[kind]
        if v:
            print('  %-4s median %.2f ms; %s' % (kind, statistics.median(x[0] for x in v),
                  ' '.join('%.1f@%d' % x for x in v[:20])))
    print('\nevent type: share of dumps with it within N ms before t0 (underrun | control), median ms since last')
    hdr = ''.join('%14s' % ('<%d ms' % w) for w in WINDOWS_MS)
    print('%-44s%s %18s' % ('', hdr, 'median since'))
    rows = []
    for k in set(since['ur']) | set(since['ctl']):
        u, c = since['ur'].get(k, []), since['ctl'].get(k, [])
        fu = [sum(x < w for x in u) / n['ur'] for w in WINDOWS_MS]
        fc = [sum(x < w for x in c) / n['ctl'] for w in WINDOWS_MS]
        score = max(a - b for a, b in zip(fu, fc))
        rows.append((score, k, fu, fc, u, c))
    for score, k, fu, fc, u, c in sorted(rows, reverse=True):
        cells = ''.join('%14s' % ('%3.0f%% |%3.0f%%' % (100 * a, 100 * b)) for a, b in zip(fu, fc))
        med = '%7s |%7s' % ('%.1f' % statistics.median(u) if u else '-', '%.1f' % statistics.median(c) if c else '-')
        print('%-44s%s %18s' % (k[:44], cells, med))


if __name__ == '__main__':
    main()
