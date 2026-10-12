#!/bin/bash
cd ~/vk13-test/logs || exit 1
for f in *.sum; do [ -f "$f" ] && cat "$f" | head -1; done
cur=$(ls -t *.out 2>/dev/null | head -1)
echo "current: $cur $(grep -c '^Test case' "$cur" 2>/dev/null) started, $(grep -c '^  Pass' "$cur" 2>/dev/null) pass, $(grep -c '^  Fail' "$cur" 2>/dev/null) fail, $(grep -c '^  NotSupported' "$cur" 2>/dev/null) notsupp, $(grep -c '=== resumed' "$cur" 2>/dev/null) crashes"
echo "hangs: $(journalctl -k --no-pager | grep -Eci 'gpu hang|Resetting rcs')"
pgrep -a "retroarch|wine|devilutionx|ppsspp|dosbox|scummvm"
[ -f ALLDONE ] && echo ALLDONE
