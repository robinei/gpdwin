#!/bin/sh
# Idle suspend only on battery
[ "$(cat /sys/class/power_supply/bq24190-charger/online 2>/dev/null)" = 1 ] && exit 0
exec sudo -n systemctl suspend-then-hibernate
