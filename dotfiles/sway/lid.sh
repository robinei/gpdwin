#!/bin/sh
# Lid closed/opened (called by inputd, which also disables/enables input, grabs the pad and turns
# the screen off/on via screen.sh): CPU turbo off and the GPU limited to RPn while closed. Needs the
# sysfs files writable by wheel (see /etc/tmpfiles.d/turbo.conf). Saved values live in
# $XDG_RUNTIME_DIR/lid-saved; both steps are safe to repeat. Exits non-zero if a write failed
# (inputd then retries): the values are saved to lid-saved.new first and the file renamed once all
# writes succeeded, so a retry still has the original values.
nt=/sys/devices/system/cpu/intel_pstate/no_turbo
g=$(ls -d /sys/class/drm/card*/gt/gt0 2>/dev/null | head -1)
s="$XDG_RUNTIME_DIR/lid-saved"

case "$1" in
close)
    [ -f "$s" ] && exit 0   # already closed
    [ -f "$s.new" ] || echo "$(cat $nt) $(cat $g/rps_max_freq_mhz) $(cat $g/rps_boost_freq_mhz)" > "$s.new"
    rpn=$(cat $g/rps_RPn_freq_mhz)
    echo 1 > $nt && echo $rpn > $g/rps_boost_freq_mhz && echo $rpn > $g/rps_max_freq_mhz || exit 1
    mv "$s.new" "$s"
    ;;
open)
    f="$s"; [ -f "$f" ] || f="$s.new"   # .new: a close that failed half way
    [ -f "$f" ] || exit 0
    read -r t max boost < "$f"
    echo $t > $nt && echo $max > $g/rps_max_freq_mhz && echo $boost > $g/rps_boost_freq_mhz || exit 1
    rm -f "$s" "$s.new"
    ;;
esac
