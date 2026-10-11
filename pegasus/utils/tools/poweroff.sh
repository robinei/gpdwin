#!/bin/sh
# Utilities "Shut Down" (sudoers 20-power: passwordless). Exits at once and powers off 1 s later, detached:
# shelf records the tool's play when it exits, and being killed mid-write left a hot journal on
# stats.db (changelog 2026-10-12).
setsid -f sh -c 'sleep 1; exec sudo -n systemctl poweroff' >/dev/null 2>&1 </dev/null
