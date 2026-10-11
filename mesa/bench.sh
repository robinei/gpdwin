#!/bin/bash
# Run ON THE GPD (no root): Mesa benchmark for comparing builds. Builds the test programs from
# kernel/dsi-investigation/scripts into /tmp/mesabench (colorcheck, readtime, burst). Takes over the screen ~2.5 min.
#   mesa/bench.sh LABEL [MESA_PREFIX]   MESA_PREFIX: an unpacked package (e.g. /tmp/mesa-slim) instead of the system Mesa
# Output: /tmp/bench-LABEL.txt
label=$1 prefix=$2
out=/tmp/bench-$label.txt
export WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/1000
if [ -n "$prefix" ]; then
    export LD_LIBRARY_PATH=$prefix/usr/lib __EGL_VENDOR_LIBRARY_FILENAMES=$prefix/usr/share/glvnd/egl_vendor.d/50_mesa.json
    mkdir -p /tmp/mesabench
    sed "s|/usr/lib/|$prefix/usr/lib/|" $prefix/usr/share/vulkan/icd.d/intel_hasvk_icd.json > /tmp/mesabench/hasvk_icd.json
    export VK_DRIVER_FILES=/tmp/mesabench/hasvk_icd.json LIBGL_DRIVERS_PATH=$prefix/usr/lib/dri
fi
S=$(cd "$(dirname "$0")/../kernel/dsi-investigation/scripts" && pwd)
mkdir -p /tmp/mesabench && cd /tmp/mesabench
[ -x colorcheck ] || cc -O2 -o colorcheck "$S/colorcheck.c" -lEGL -lGL || exit 1
[ -x readtime ] || cc -O2 -o readtime "$S/readtime.c" -lEGL -lGL || exit 1
[ -x burst3 ] || cc -O2 -o burst3 "$S/burst.c" $(pkg-config --cflags --libs sdl3) || exit 1
if pgrep -x 'devilutionx|retroarch|ppsspp|zelda3' >/dev/null || pgrep -f wine >/dev/null; then echo "a game is running"; exit 1; fi
{
echo "== $label $(date -Is) $(./colorcheck | head -1)"
echo "vulkan driver: ${VK_DRIVER_FILES:-system}"
python3 - <<'PY'
import subprocess, time, os, statistics
def runs(n, env=None):
    t = []
    for _ in range(n):
        t0 = time.perf_counter(); subprocess.run(['./colorcheck'], stdout=subprocess.DEVNULL, env=env); t.append((time.perf_counter() - t0) * 1000)
    return t
runs(3)
w = runs(20)
e = dict(os.environ, MESA_SHADER_CACHE_DISABLE='true')
c = runs(8, e)
print('colorcheck process (start, GL init, draw, exit): warm cache median %.0f ms (min %.0f); no shader cache median %.0f ms' % (statistics.median(w), min(w), statistics.median(c)))
PY
# memory of a running GL program
./burst3 -m full -t 12 >/dev/null 2>&1 & p=$!; sleep 8
awk '/^Rss|^Pss|^Private_Dirty/ {printf "%s %s kB  ", $1, $2} END {print ""}' /proc/$p/smaps_rollup | sed 's/^/burst memory: /'
echo "burst mapped libraries: $(awk '$6 ~ /\.so/ {print $6}' /proc/$p/maps | sort -u | wc -l), $(awk '$6 ~ /\.so/ {print $6}' /proc/$p/maps | sort -u | grep -cE 'LLVM|SPIRV|sensors|elf') of them LLVM/SPIRV/sensors/elf"
wait $p
for m in "upload" "full" "static"; do
    f=$(timeout 60 ./burst3 -m $m -n -t 20 2>/dev/null | tail -1 | awk '{print $1}')
    echo "burst -m $m -n (no vsync, 20 s, opengl): $f frames = $((f / 20)) fps"
done
f=$(timeout 60 ./burst3 -m full -n -t 20 -r vulkan 2>/dev/null | tail -1 | awk '{print $1}')
echo "burst -m full -n -r vulkan (hasvk, 20 s): $f frames = $((f / 20)) fps"
./readtime | sed -n 2,5p
} > $out 2>&1
cat $out
