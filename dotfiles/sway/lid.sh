#!/bin/sh
# Lid closed/opened (called by inputd, which also disables/enables input and grabs the pad): screen
# off/on via screen.sh (also freezes shelf and the game), plus CPU turbo off and the GPU limited to
# RPn while closed. Needs the sysfs files writable by wheel (see /etc/tmpfiles.d/turbo.conf). Saved
# values live in $XDG_RUNTIME_DIR/lid-saved.
nt=/sys/devices/system/cpu/intel_pstate/no_turbo
g=$(ls -d /sys/class/drm/card*/gt/gt0 2>/dev/null | head -1)
s="$XDG_RUNTIME_DIR/lid-saved"

case "$1" in
close)
    ~/.config/sway/screen.sh off
    [ -f "$s" ] && exit 0   # already closed
    echo "$(cat $nt) $(cat $g/rps_max_freq_mhz) $(cat $g/rps_boost_freq_mhz)" > "$s"
    echo 1 > $nt
    rpn=$(cat $g/rps_RPn_freq_mhz)
    echo $rpn > $g/rps_boost_freq_mhz; echo $rpn > $g/rps_max_freq_mhz
    ;;
open)
    ~/.config/sway/screen.sh on
    [ -f "$s" ] || exit 0
    read -r t max boost < "$s"
    echo $t > $nt
    echo $max > $g/rps_max_freq_mhz; echo $boost > $g/rps_boost_freq_mhz
    rm -f "$s"
    ;;
esac
