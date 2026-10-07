#!/bin/sh
# Keep Pegasus fullscreen. A workspace has one fullscreen window, so a game going fullscreen takes
# it from Pegasus, and sway doesn't give it back when the game closes. Whenever Pegasus gets focus
# again without being fullscreen, make it fullscreen. Sleeps in read() between window events.
swaymsg -r -m -t subscribe '["window"]' | while read -r event; do
    case "$event" in *'"change": "focus"'*'"app_id": "org.pegasus-frontend.pegasus-fe"'*)
        case "$event" in *'"fullscreen_mode": 0'*)
            swaymsg '[app_id="org.pegasus-frontend.pegasus-fe"] fullscreen enable' >/dev/null ;;
        esac ;;
    esac
done
