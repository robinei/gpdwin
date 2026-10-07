#!/bin/sh
# Workspace 1 as a stack of fullscreen windows: Pegasus, and on top whatever it starts. A
# workspace has only one fullscreen window, so a new one takes fullscreen from the one below, and
# sway doesn't give it back when the top window closes. So whenever a tiled window on workspace 1
# gets focus without being fullscreen, make it fullscreen. Sleeps in read() between window events.
#
# Old X11 games with a fixed window size (native Psychonauts) are floated by sway right after they
# open (a "new" event as tiled, then a "floating" event), which drops the fullscreen from the
# tiling rule in autostart, and the fullscreen Pegasus then hides them. So X11 windows on workspace 1
# that become floating are made fullscreen too, unless they are dialogs (a parent window, or a
# dialog/menu/tooltip window type).
swaymsg -r -m -t subscribe '["window"]' | while read -r event; do
    case "$event" in
    *'"change": "focus"'*'"fullscreen_mode": 0'*)
        swaymsg '[workspace="^1$" con_id=__focused__ tiling] fullscreen enable' >/dev/null 2>&1 ;;
    *'"change": "floating"'*'"type": "floating_con"'*'"transient_for": null'*)
        case "$event" in
        *'"window_type": "dialog"'* | *'"window_type": "utility"'* | *'"window_type": "splash"'* | \
        *'"window_type": "menu"'* | *'"window_type": "popup_menu"'* | *'"window_type": "dropdown_menu"'* | \
        *'"window_type": "tooltip"'* | *'"window_type": "notification"'*) ;;
        *)
            id=$(printf '%s\n' "$event" | sed -n 's/^{ "change": "floating", "container": { "id": \([0-9]*\),.*/\1/p')
            [ -n "$id" ] && swaymsg "[con_id=$id workspace=\"^1$\"] fullscreen enable" >/dev/null 2>&1 ;;
        esac ;;
    esac
done
