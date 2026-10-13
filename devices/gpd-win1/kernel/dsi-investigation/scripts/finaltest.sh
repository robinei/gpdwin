#!/bin/bash
# Final Mesa (1.2, scoped no-snoop upload staging) from /tmp/mesa-final, test programs only.
export WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/1000
M=/tmp/mesa-final
export LD_LIBRARY_PATH=$M/usr/lib __EGL_VENDOR_LIBRARY_FILENAMES=$M/usr/share/glvnd/egl_vendor.d/50_mesa.json
cd /tmp
log() { echo "$(date +%T) $*" >> /tmp/finaltest.log; }
while pgrep -x 'devilutionx|retroarch|ppsspp|zelda3' >/dev/null || pgrep -f wine >/dev/null; do log "game running, waiting"; sleep 60; done
log "start; $(./colorcheck | grep -E '^renderer|PASS|FAIL' | tr '\n' ' ')"
./readtime | tail -5 >> /tmp/finaltest.log
s=$(date +%T)
timeout 700 ./burst3 -m upload -d 10 -t 600 -l /tmp/ft.times | grep -v fps | head -1 >> /tmp/finaltest.log
e=$(date +%T)
n=$(journalctl -k -b --no-pager --since "$s" --until "$e" | grep classified | grep -vc echo)
up=$(awk '{d=($2-$1)*1000; s+=d; n++; if(d>5)b++} END{printf "upload mean %.2f ms, >5 ms in %.1f%%, frames %d", s/n, 100*b/n, n}' /tmp/ft.times)
log "final upload $s-$e: $n events; $up"
s=$(date +%T)
timeout 400 ./burst3 -m full -d 10 -t 300 -l /tmp/ft-full.times | grep -v fps | head -1 >> /tmp/finaltest.log
e=$(date +%T)
n=$(journalctl -k -b --no-pager --since "$s" --until "$e" | grep classified | grep -vc echo)
log "final full (upload+draw, 5 min) $s-$e: $n events"
log "done"
