#!/bin/sh
# Utilities "Reboot" (sudoers 20-power: passwordless). Exits at once and reboots 2 s later, detached:
# shelf records the tool's play when it exits, and being killed mid-write left a hot journal on
# stats.db (changelog 2026-10-12).
setsid -f sh -c 'sleep 2; exec sudo -n systemctl reboot' >/dev/null 2>&1 </dev/null
