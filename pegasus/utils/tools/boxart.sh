#!/bin/sh
exec foot --app-id=pegasus-tool fish -c '~/Games/fetch-boxart.py; echo; echo "Restart Pegasus to see new box art."; read -P "Press Enter to return " ans'
