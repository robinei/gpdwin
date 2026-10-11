#!/bin/sh
# Keep inputd (src/inputd.c) running for the sway session: it disables input while the lid is
# closed, so a crash must not leave the device without input. Restarted 1 s after it exits (a
# restarted inputd re-applies the lid and screen state); stops once sway is gone (inputd exits
# when its Wayland connection goes). Restart it after an update with `pkill -x inputd`.
while :; do
    ~/.local/bin/inputd
    swaymsg -t get_version >/dev/null 2>&1 || exit 0
    sleep 1
done
