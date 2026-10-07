#!/bin/sh
# Idle dim, first level: remember brightness, drop to a third (never below 1%).
# The second level (handheld) drops to 1% without saving, so resume restores the original.
brightnessctl -q -s
cur=$(brightnessctl -m | cut -d, -f4 | tr -d %)
brightnessctl -q set "$(( cur / 3 > 1 ? cur / 3 : 1 ))%"
