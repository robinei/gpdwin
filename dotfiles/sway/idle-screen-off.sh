#!/bin/sh
# Idle step 2: turn the display off, but only on battery. On AC the screen just stays dimmed.
[ "$(cat /sys/class/power_supply/bq24190-charger/online 2>/dev/null)" = 1 ] && exit 0
pkill -STOP -x statusbar   # nothing to show while the screen is off; resume sends SIGCONT
~/.config/sway/freeze-games.sh stop   # a game left running pauses too; resume thaws it
exec swaymsg "output * power off"
