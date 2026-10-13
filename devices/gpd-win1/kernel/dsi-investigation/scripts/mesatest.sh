#!/bin/bash
# Overnight Mesa test on the GPD (no install): patched Mesa from /tmp/mesa-test for the test programs only.
export WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/1000
M=/tmp/mesa-test
export LD_LIBRARY_PATH=$M/usr/lib __EGL_VENDOR_LIBRARY_FILENAMES=$M/usr/share/glvnd/egl_vendor.d/50_mesa.json
cd /tmp
log() { echo "$(date +%T) $*" >> /tmp/mesatest.log; }
game() { pgrep -x 'devilutionx|retroarch|ppsspp|zelda3' >/dev/null || pgrep -f 'wine' >/dev/null; }
run() { # name env...
  name=$1; shift
  while game; do log "$name: a game is running, waiting"; sleep 60; done
  log "$name colorcheck: $(env "$@" ./colorcheck | grep -E '^renderer|PASS|FAIL' | tr '\n' ' ')"
  s=$(date +%T)
  env "$@" timeout 700 ./burst3 -m upload -d 10 -t 600 -l /tmp/mt-$name.times | grep -v fps | head -1 >> /tmp/mesatest.log
  e=$(date +%T)
  n=$(journalctl -k -b --no-pager --since "$s" --until "$e" | grep classified | grep -vc echo)
  up=$(awk '{d=($2-$1)*1000; s+=d; n++; if(d>5)b++} END{printf "upload mean %.2f ms, >5 ms in %.1f%%, frames %d", s/n, 100*b/n, n}' /tmp/mt-$name.times)
  log "$name $s-$e: $n events; $up"
  sleep 5
}
log "start"
run V0  DUMMY=1
run V2  MESA_ST_MATCH_SIZED=1
run V1a CROCUS_STAGING_NOSNOOP=1
run V1b CROCUS_SNOOP_UC_MOCS=1
run V2a MESA_ST_MATCH_SIZED=1 CROCUS_STAGING_NOSNOOP=1
last=$(journalctl -k -b --no-pager | grep classified | grep -v echo | tail -1)
log "last event: ${last##*classified: }"
log "done"
