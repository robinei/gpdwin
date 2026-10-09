#!/bin/sh
# Freeze (SIGSTOP) / thaw (SIGCONT) Pegasus and everything it started: the running game and its
# whole process tree (launcher, Wine processes), so nothing drains the battery while the screen is
# off. Called by lid.sh (lid closed/opened) and the idle screen-off step (handheld). The frozen pids
# are kept in $XDG_RUNTIME_DIR/frozen-games, so thaw only touches what was frozen, and a second
# freeze (idle screen-off, then lid closed) does nothing. wineserver is not Pegasus' descendant
# (it daemonizes); it just waits while its clients are stopped.
# Usage: freeze-games.sh stop|cont
f="$XDG_RUNTIME_DIR/frozen-games"

case "$1" in
stop)
    [ -f "$f" ] && exit 0
    root=$(pgrep -x pegasus-fe | head -1)
    [ -n "$root" ] || exit 0
    pids="$root" todo=$(pgrep -P "$root")
    while [ -n "$todo" ]; do
        pids="$pids $todo"
        todo=$(pgrep -P "$(echo $todo | tr ' ' ,)")
    done
    echo $pids > "$f"
    kill -STOP $pids 2>/dev/null
    ;;
cont)
    [ -f "$f" ] || exit 0
    kill -CONT $(cat "$f") 2>/dev/null
    rm -f "$f"
    ;;
esac
exit 0
