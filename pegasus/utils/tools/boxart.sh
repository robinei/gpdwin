#!/bin/sh
exec foot --app-id=shelf-tool fish -c '~/Games/fetch-boxart.py; echo; read -P "Press Enter to return " ans'
