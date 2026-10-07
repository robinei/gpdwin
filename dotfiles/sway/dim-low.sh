#!/bin/sh
# Idle dim, second level: 1% brightness (the lowest visible level). Doesn't save the brightness:
# dim.sh saved it at the first level, so resume restores the original.
#   dim-low.sh battery   only when running on battery (on AC this step comes later)
[ "$1" = battery ] && [ "$(cat /sys/class/power_supply/bq24190-charger/online 2>/dev/null)" = 1 ] && exit 0
exec brightnessctl -q set 1
