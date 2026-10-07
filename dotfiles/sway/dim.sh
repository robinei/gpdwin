#!/bin/sh
# Idle dim: remember brightness, drop to a third (never below 2%)
brightnessctl -q -s
cur=$(brightnessctl -m | cut -d, -f4 | tr -d %)
brightnessctl -q set "$(( cur / 3 > 1 ? cur / 3 : 1 ))%"
