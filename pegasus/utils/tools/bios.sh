#!/bin/sh
exec foot --app-id=shelf-tool fish -c 'echo "Reboot into the BIOS / UEFI setup."; read -P "Press Enter to reboot (Ctrl+C to cancel) " ans; and sudo systemctl reboot --firmware-setup'
