#!/bin/sh
# Idle step 2: turn the display off, but only on battery. On AC the screen just stays dimmed.
[ "$(cat /sys/class/power_supply/bq24190-charger/online 2>/dev/null)" = 1 ] && exit 0
exec swaymsg "output * power off"
