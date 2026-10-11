#!/bin/sh
# Volume/brightness keys: change the value, then show it in wob
wob="$XDG_RUNTIME_DIR/wob.sock"
sink=@DEFAULT_AUDIO_SINK@
# A step takes ~165 ms (two wpctl runs), but a held key repeats every 40 ms: overlapping copies
# competed for the CPU and finished out of order, so the bar stuttered. Repeats that arrive while a
# step runs are dropped instead (one step at a time, ~6 per second). Mute is never dropped.
case "$1" in
    mute) ;;
    *) exec 9>"$XDG_RUNTIME_DIR/osd.lock"; flock -n 9 || exit 0 ;;
esac
case "$1" in
    vol-up)   wpctl set-volume -l 1.0 $sink 5%+ 2>/dev/null ;;
    vol-down) wpctl set-volume $sink 5%- 2>/dev/null ;;
    mute)     wpctl set-mute $sink toggle 2>/dev/null ;;
    bri-up)   brightnessctl -q set 5%+ ;;
    bri-down) brightnessctl -q --min-value=1 set 5%- ;;
esac
case "$1" in
    vol-*|mute)
        v=$(wpctl get-volume $sink 2>/dev/null)
        case "$v" in *MUTED*) n=0 ;; *) n=$(echo "$v" | awk '{print int($2*100+0.5)}') ;; esac ;;
    bri-*)
        n=$(brightnessctl -m | cut -d, -f4 | tr -d %) ;;
esac
[ -p "$wob" ] && echo "$n" > "$wob"
exec 9>&-   # done with the lock; the bar signal below needn't hold it
case "$1" in vol-*|mute) pkill -USR1 -x statusbar ;; esac   # update the bar now
