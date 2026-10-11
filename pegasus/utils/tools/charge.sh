#!/bin/sh
# Utilities "Full Charge": toggle the battery charge limit (docs/power.md, "Charge limit") between
# 4.144 V (~80%, the default from /usr/local/bin/charge-limit) and 4.384 V (100%, the stock value).
# wheel can write the file (charge-limit). Back to the limit on the next boot. Shown in wob:
# bar at 80 = limited, full bar = charging to 100%.
f=/sys/class/power_supply/bq24190-charger/constant_charge_voltage
limit=4144000 full=4384000
if [ "$(cat $f)" -ge $full ]; then echo $limit > $f; else echo $full > $f; fi
[ "$(cat $f)" -ge $full ] && pct=100 || pct=80
wob="$XDG_RUNTIME_DIR/wob.sock"
[ -p "$wob" ] && echo "$pct charge" > "$wob"
pkill -USR1 -x statusbar
