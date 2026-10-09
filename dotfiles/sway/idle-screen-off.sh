#!/bin/sh
# Idle step 2: turn the display off, but only on battery. On AC the screen just stays dimmed.
# swayidle's resume runs `screen.sh on` (handheld).
[ "$(cat /sys/class/power_supply/bq24190-charger/online 2>/dev/null)" = 1 ] && exit 0
exec ~/.config/sway/screen.sh off
