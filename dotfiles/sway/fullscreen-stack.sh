#!/bin/sh
# Workspace 1 as a stack of fullscreen windows: Pegasus, and on top whatever it starts. A
# workspace has only one fullscreen window, so a new one takes fullscreen from the one below, and
# sway doesn't give it back when the top window closes. So whenever a tiled window on workspace 1
# gets focus without being fullscreen, make it fullscreen. Sleeps in read() between window events.
swaymsg -r -m -t subscribe '["window"]' | while read -r event; do
    case "$event" in *'"change": "focus"'*'"fullscreen_mode": 0'*)
        swaymsg '[workspace="^1$" con_id=__focused__ tiling] fullscreen enable' >/dev/null 2>&1 ;;
    esac
done
