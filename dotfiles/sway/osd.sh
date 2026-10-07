#!/bin/sh
# Volume/brightness keys: change the value, then show it in wob
wob="$XDG_RUNTIME_DIR/wob.sock"
sink=@DEFAULT_AUDIO_SINK@
case "$1" in
    vol-up)   wpctl set-volume -l 1.0 $sink 5%+ 2>/dev/null ;;
    vol-down) wpctl set-volume $sink 5%- 2>/dev/null ;;
    mute)     wpctl set-mute $sink toggle 2>/dev/null ;;
    bri-up)   brightnessctl -q set 5%+ ;;
    bri-down) brightnessctl -q --min-value=2 set 5%- ;;
esac
case "$1" in
    vol-*|mute)
        v=$(wpctl get-volume $sink 2>/dev/null)
        case "$v" in *MUTED*) n=0 ;; *) n=$(echo "$v" | awk '{print int($2*100+0.5)}') ;; esac ;;
    bri-*)
        n=$(brightnessctl -m | cut -d, -f4 | tr -d %) ;;
esac
[ -p "$wob" ] && echo "$n" > "$wob"
case "$1" in vol-*|mute) pkill -USR1 -x status.sh ;; esac   # redraw the bar now
