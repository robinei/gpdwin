#!/bin/bash
# r8-cycle.sh LABEL CYCLES [OFF_S] [ON_S]   env: GPULOAD=1 runs /tmp/gpuload; MESA=/tmp/mesa-stock uses Arch's Mesa for it;
# NOSCREEN=1 skips the off/on (pure GPU load + PMIC polling); POLL=1 reads the charger 5x/s.
L=$1 N=${2:-60} OFF=${3:-1} ON=${4:-1.5}
export SWAYSOCK=$(ls /run/user/1000/sway-ipc.*.sock)
log=/tmp/r8-$L.log; since=$(date '+%Y-%m-%d %H:%M:%S'); G= P=
if [ "$GPULOAD" = 1 ]; then
  if [ -n "$MESA" ]; then
    LD_LIBRARY_PATH=$MESA/usr/lib __EGL_VENDOR_LIBRARY_FILENAMES=$MESA/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    LIBGL_DRIVERS_PATH=$MESA/usr/lib/dri /tmp/gpuload > /tmp/r8-$L-gpuload.log 2>&1 & G=$!
  else /tmp/gpuload > /tmp/r8-$L-gpuload.log 2>&1 & G=$!; fi
fi
if [ "$POLL" = 1 ]; then ( while :; do cat /sys/class/power_supply/bq24190-charger/status \
  /sys/class/power_supply/max170xx_battery/capacity >/dev/null 2>&1; sleep 0.2; done ) & P=$!; fi
cleanup() { [ -n "$G" ] && kill $G 2>/dev/null; [ -n "$P" ] && kill $P 2>/dev/null; G= P=; }
trap cleanup EXIT
bad() { journalctl -k --since "$since" --no-pager 2>/dev/null | grep -E "controller timed out|mipi_exec_pmic|semaphore|couldn't acquire|VLV_IER did not settle|vblank wait timed out|flip_done" | head -3; }
echo "start $since label=$L cycles=$N off=$OFF on=$ON gpuload=${GPULOAD:-0} mesa=${MESA:-system} noscreen=${NOSCREEN:-0} poll=${POLL:-0}" > $log
for i in $(seq 1 "$N"); do
  [ -e /tmp/r8-stop ] && { echo "stopped at cycle $i" >> $log; break; }
  if [ "$NOSCREEN" = 1 ]; then sleep 2.5; else
    swaymsg -q 'output DSI-1 power off'; sleep "$OFF"; swaymsg -q 'output DSI-1 power on'; sleep "$ON"; fi
  b=$(bad); if [ -n "$b" ]; then echo "ABORT at cycle $i $(date +%T): $b" >> $log; cleanup; swaymsg -q 'output DSI-1 power on'; break; fi
  (( i % 25 == 0 )) && echo "$(date +%T) cycle $i gt_cur=$(cat /sys/class/drm/card1/gt_cur_freq_mhz) rc6_ms=$(cat /sys/class/drm/card1/gt/gt0/rc6_residency_ms)" >> $log
done
cleanup
echo "end $(date +%T) after $i cycles; gpuload: $(tail -1 /tmp/r8-$L-gpuload.log 2>/dev/null)" >> $log
journalctl -k --since "$since" --no-pager -o short-monotonic | grep -E "VLV_IER|timed out|mipi_exec|flip_done|DSI resync" >> $log
