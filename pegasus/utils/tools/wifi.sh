#!/bin/sh
exec foot --app-id=pegasus-tool fish -c 'echo "iwctl: station wlan0 scan / get-networks / connect NAME, then exit"; iwctl'
