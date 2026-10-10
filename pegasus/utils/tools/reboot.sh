#!/bin/sh
# Utilities "Reboot" (sudoers 20-power: passwordless)
exec sudo -n systemctl reboot
