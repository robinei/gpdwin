#!/bin/sh
# Toggle CPU or GPU turbo and show it in wob (full bar = on, empty = off).
#   CPU: intel_pstate no_turbo (0 = turbo allowed)
#   GPU: i915 max/boost frequency, RP0 (turbo) <-> RP1 (efficient)
# Needs the sysfs files to be writable by the wheel group (see /etc/tmpfiles.d/turbo.conf).
wob="$XDG_RUNTIME_DIR/wob.sock"

case "$1" in
cpu)
    f=/sys/devices/system/cpu/intel_pstate/no_turbo
    if [ "$(cat $f)" = 0 ]; then new=1; else new=0; fi
    echo $new > $f 2>/dev/null
    [ "$(cat $f)" = 0 ] && on=1 || on=0
    ;;
gpu)
    d=$(ls -d /sys/class/drm/card*/gt/gt0 2>/dev/null | head -1)
    [ -n "$d" ] || exit 1
    rp0=$(cat $d/rps_RP0_freq_mhz); rp1=$(cat $d/rps_RP1_freq_mhz)
    if [ "$(cat $d/rps_max_freq_mhz)" -ge "$rp0" ]; then
        echo $rp1 > $d/rps_boost_freq_mhz 2>/dev/null; echo $rp1 > $d/rps_max_freq_mhz 2>/dev/null
    else
        echo $rp0 > $d/rps_max_freq_mhz 2>/dev/null; echo $rp0 > $d/rps_boost_freq_mhz 2>/dev/null
    fi
    [ "$(cat $d/rps_max_freq_mhz)" -ge "$rp0" ] && on=1 || on=0
    ;;
*) echo "usage: $0 cpu|gpu" >&2; exit 2 ;;
esac

# Show the state read back from the hardware, so a failed write never shows a wrong bar.
[ -p "$wob" ] && echo "$((on * 100)) $1" > "$wob"
