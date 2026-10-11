#!/bin/sh
# Screen off/on: the one place for what happens when the display goes off and comes back. Called
# only by inputd (src/inputd.c): lid closed, and the idle screen-off step. Both can apply in one off
# period (idle, then lid closed), so every step is safe to repeat.
#   off: freeze shelf and everything it started (the game's whole process tree, Wine included)
#        with SIGSTOP, pause the status bar, power the output off.
#   on:  the reverse.
# Frozen pids are kept in $XDG_RUNTIME_DIR/frozen, so `on` only thaws what `off` froze and a second
# `off` freezes nothing new. wineserver is not shelf's descendant (it daemonizes); it just waits
# while its clients are stopped. Games started outside shelf are not frozen.
# Exits non-zero if the output couldn't be switched (inputd then retries).
f="$XDG_RUNTIME_DIR/frozen"
rc=0

case "$1" in
off)
    root=$(pgrep -x shelf | head -1)
    if [ -n "$root" ] && [ ! -f "$f" ]; then
        pids="$root" todo=$(pgrep -P "$root")
        while [ -n "$todo" ]; do
            pids="$pids $todo"
            todo=$(pgrep -P "$(echo $todo | tr ' ' ,)")
        done
        echo $pids > "$f"
        kill -STOP $pids 2>/dev/null
    fi
    pkill -STOP -x statusbar
    swaymsg -q "output * power off" || rc=1
    ;;
on)
    swaymsg -q "output * power on" || rc=1
    pkill -CONT -x statusbar
    if [ -f "$f" ]; then
        kill -CONT $(cat "$f") 2>/dev/null
        rm -f "$f"
    fi
    ;;
esac
exit $rc
