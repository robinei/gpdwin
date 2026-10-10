#!/bin/sh
# Utilities "Shut Down" (sudoers 20-power: passwordless)
exec sudo -n systemctl poweroff
