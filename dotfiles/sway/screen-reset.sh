#!/bin/sh
# Resync the DSI panel. A game's fullscreen switch sometimes leaves the picture shifted by half a
# frame (bottom half shown at the top); powering the output off and on fixes it.
swaymsg output DSI-1 power off
sleep 2
swaymsg output DSI-1 power on
