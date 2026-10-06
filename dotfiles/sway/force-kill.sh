#!/bin/sh
# Force-close the focused window when `kill` (Mod+Backspace) is ignored: SIGTERM its process,
# then SIGKILL if it is still there after a second. Never touches sway itself.
pid=$(swaymsg -t get_tree | python3 -c '
import json, sys
def walk(n):
    if n.get("focused") and n.get("pid"):
        return n["pid"]
    for c in n.get("nodes", []) + n.get("floating_nodes", []):
        p = walk(c)
        if p:
            return p
print(walk(json.load(sys.stdin)) or "")')
[ -n "$pid" ] || exit 0
[ "$pid" = "$(pgrep -x sway)" ] && exit 0
kill -TERM "$pid" 2>/dev/null || exit 0
sleep 1
kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
exit 0
