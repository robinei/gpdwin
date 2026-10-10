#!/bin/bash
# Stress test for the VLV_IER race (patch 0015): output off/on in a loop while gpuload (scripts/gpuload.c,
# built to /tmp/gpuload) keeps GT interrupts coming, so an irq handler often runs while the display power
# well enable rewrites VLV_IER. Run as the sway user on the GPD; no sudo, no register access.
#   screen-race-test.sh CYCLES [OFF_S] [ON_S] > log
# With 0015, a caught race logs "VLV_IER 0x... after display irq enable, restoring"; without it, the
# display would die ("vblank wait timed out", flip_done timeouts). Stops early at /tmp/race-test-stop.
N=${1:-300} OFF=${2:-1} ON=${3:-1.5}
export SWAYSOCK=$(ls /run/user/1000/sway-ipc.*.sock)
/tmp/gpuload > /tmp/gpuload.log & L=$!
trap 'kill $L 2>/dev/null; swaymsg -q "output DSI-1 power on"' EXIT
since=$(date '+%Y-%m-%d %H:%M:%S')
echo "start $since cycles=$N off=$OFF on=$ON"
for i in $(seq 1 "$N"); do
	[ -e /tmp/race-test-stop ] && break
	swaymsg -q 'output DSI-1 power off'; sleep "$OFF"
	swaymsg -q 'output DSI-1 power on'; sleep "$ON"
	(( i % 25 == 0 )) && echo "$(date +%T) cycle $i"
done
echo "end $(date +%T) after $i cycles; gpuload: $(tail -1 /tmp/gpuload.log)"
journalctl -k --since "$since" --no-pager -o short-monotonic |
	grep -E "VLV_IER|vblank wait timed out|flip_done|commit wait|sample cancelled|WARNING"
echo "restored: $(journalctl -k --since "$since" --no-pager | grep -c 'VLV_IER') timeouts: $(journalctl -k --since "$since" --no-pager | grep -c 'timed out')"
