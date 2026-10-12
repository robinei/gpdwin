#!/bin/bash
# GPD side: DXVK 2.7.1 (64-bit) smoke test with the given hasvk build. The DLL is loaded from the exe's directory
# (WINEDLLOVERRIDES=d3d9=n), nothing in the Wine prefix is changed.
#   dxvktest.sh vk13p2 [frames]
cd ~/vk13-test/dxvk271 || exit 1
drv=$1; frames=${2:-300}
mkdir -p run64 && cp -f x64/d3d9.dll run64/ && cp -f ../d3d9tex.exe run64/
export WINEDEBUG=-all WINEDLLOVERRIDES="d3d9=n" DXVK_LOG_LEVEL=info DXVK_LOG_PATH=/home/robin/vk13-test/logs
case $drv in stock) ;; *) export VK_DRIVER_FILES=/home/robin/vk13-test/icd/$drv.json ;; esac
echo "== games"; pgrep -a "retroarch|devilutionx|ppsspp|dosbox|scummvm"; echo "(checked)"
rm -f logs/d3d9tex_d3d9.log
echo "== hangs before: $(journalctl -k --no-pager | grep -ci 'gpu hang|Resetting rcs')"
( cd run64 && timeout 120 wine ./d3d9tex.exe $frames 2>&1 | tail -8 )
echo "== dxvk log"; cat ../logs/d3d9tex_d3d9.log 2>/dev/null | head -40
wineserver -k 2>/dev/null
echo "== hangs after: $(journalctl -k --no-pager | grep -ci 'gpu hang|Resetting rcs')"
