#!/bin/sh
exec foot --app-id=pegasus-tool fish -c 'echo "Reboot once with the stock display driver (boot entry \"Arch Linux (stock)\")."; echo "The boot after that uses the patched driver again."; read -P "Press Enter to reboot (Ctrl+C to cancel) " ans; and sudo systemctl reboot --boot-loader-entry=arch-stock.conf'
