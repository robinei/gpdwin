#!/bin/sh
# CPU/GPU boost + RAM + WiFi + volume + battery + clock for swaybar (i3bar JSON so the battery can change colour).
# Updates every 20s to keep wakeups low; SIGUSR1 (sent by osd.sh and turbo.sh)
# redraws at once.
bat=/sys/class/power_supply/max170xx_battery
chg=/sys/class/power_supply/bq24190-charger
text=#cdd6f4 yellow=#f9e2af red=#f38ba8 dim=#7f849c

wifi() {
    info=$(iwctl station wlan0 show 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g')
    ssid=$(printf '%s\n' "$info" | sed -n 's/^ *Connected network *\(.*[^ ]\) *$/\1/p')
    [ -n "$ssid" ] || { echo "wifi off"; return; }
    rssi=$(printf '%s\n' "$info" | sed -n 's/^ *RSSI *\(-\?[0-9]*\) dBm.*/\1/p')
    # -90 dBm -> 0%, -30 dBm -> 100%
    pct=$(( (${rssi:--90} + 90) * 100 / 60 ))
    [ $pct -gt 100 ] && pct=100
    [ $pct -lt 0 ] && pct=0
    echo "$ssid ${pct}%"
}

volume() {
    v=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null) || { echo "vol ?"; return; }
    pct=$(echo "$v" | awk '{print int($2*100+0.5)}')
    case "$v" in *MUTED*) echo "muted ${pct}%" ;; *) echo "vol ${pct}%" ;; esac
}

# Boost state as "cpu+"/"gpu+" (turbo allowed) or a dimmed "cpu-"/"gpu-" (capped by turbo.sh).
boost() { # cpu|gpu -> prints "on" or "off"
    case "$1" in
    cpu) [ "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null)" = 1 ] && echo off || echo on ;;
    gpu) d=$(ls -d /sys/class/drm/card*/gt/gt0 2>/dev/null | head -1)
         [ -n "$d" ] && [ "$(cat $d/rps_max_freq_mhz)" -lt "$(cat $d/rps_RP0_freq_mhz)" ] && echo off || echo on ;;
    esac
}

boostblock() {
    if [ "$(boost $1)" = on ]; then block "$1+" "$text"; else block "$1-" "$dim"; fi
}

# Used/total RAM in GB; "used" = total - available (what apps can't get back without swapping).
ram() {
    awk '/^MemTotal:/{t=$2} /^MemAvailable:/{a=$2} END{printf "ram %.1f/%.1fG", (t-a)/1048576, t/1048576}' /proc/meminfo
}

json() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

block() { # text colour [last]
    sep=18; [ -n "$3" ] && sep=6
    printf '{"full_text":"%s","color":"%s","separator":false,"separator_block_width":%d}' "$(json "$1")" "$2" "$sep"
}

trap ':' USR1
echo '{"version":1}'
echo '['
while :; do
    w=$(wifi)
    wcol=$text; [ "$w" = "wifi off" ] && wcol=$dim
    cap=$(cat "$bat/capacity" 2>/dev/null)
    charging=$(cat "$chg/status" 2>/dev/null)
    mark=""; bcol=$text
    if [ "$charging" = Charging ]; then
        mark="+"
    elif [ "${cap:-100}" -le 15 ]; then
        bcol=$red
    elif [ "${cap:-100}" -le 30 ]; then
        bcol=$yellow
    fi
    vol=$(volume)
    vcol=$text; case "$vol" in muted*) vcol=$dim ;; esac
    printf '[%s,%s,%s,%s,%s,%s,%s],\n' \
        "$(boostblock cpu)" \
        "$(boostblock gpu)" \
        "$(block "$(ram)" "$text")" \
        "$(block "$w" "$wcol")" \
        "$(block "$vol" "$vcol")" \
        "$(block "bat ${cap}%${mark}" "$bcol")" \
        "$(block "$(date +'%a %d %b  %H:%M')" "$text" last)"
    sleep 20 &
    wait $! 2>/dev/null
    kill $! 2>/dev/null
done
