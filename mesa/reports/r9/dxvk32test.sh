#!/bin/bash
# GPD side: 32-bit Wine (~/.wine32 prefix, ~/Games/tools/wine32, like the Dark Souls launcher) + a DXVK d3d9.dll
# + a hasvk driver, running an R9/R5 test exe. Nothing in the prefix or the game is changed: the DLL is loaded
# from the exe's directory (WINEDLLOVERRIDES d3d9=n).
#   dxvk32test.sh DLL DRIVER EXE [args...]
#     DLL    = path to a 32-bit d3d9.dll (dxvk-test/d3d9-2.4.dll, ~/vk13-test/dxvk271gpd/x32/d3d9.dll, ...)
#     DRIVER = stock      installed lib32 hasvk 1.7 (relocations)
#              vk13       ~/vk13-test/lib32 (patches 1-4, softpin)
#              vk13reloc  same with HASVK_USE_RELOCATIONS=1 (relocation mode + patches 3/4)
#     EXE    = d3d9rb32.exe | d3d9tex32.exe (in ~/vk13-test), args passed on (frames ...)
# Env: ASYNC=1 sets DXVK_GPD_ASYNC_READBACK=1. Never run while the CTS queue is active (shared GPU).
# The claim options are set for every DXVK >= 2.x (2.0-2.6 need the 1.3 claim; 2.7 the descriptor indexing one).
cd ~/vk13-test || exit 1
dll=$1; drv=$2; exe=$3; shift 3
mkdir -p icd run32 logs
cat > icd/vk13lib32.json <<J
{ "ICD": { "api_version": "1.3.354", "library_arch": "32", "library_path": "/home/robin/vk13-test/lib32/libvulkan_intel_hasvk.so" }, "file_format_version": "1.0.1" }
J
cp -f "$dll" run32/d3d9.dll && cp -f "$exe" run32/
export WINEPREFIX="$HOME/.wine32" WINEARCH=win32 WINEDEBUG=-all DISPLAY=${DISPLAY:-:0}
export WINEDLLOVERRIDES="mscoree,mshtml,winegstreamer=d;d3d9=n"
export hasvk_report_vk_1_3_version=true hasvk_report_descriptor_indexing=true
export DXVK_LOG_LEVEL=warn DXVK_LOG_PATH="$HOME/vk13-test/logs"
case $drv in
  stock) ;;
  vk13) export VK_DRIVER_FILES=/home/robin/vk13-test/icd/vk13lib32.json ;;
  vk13reloc) export VK_DRIVER_FILES=/home/robin/vk13-test/icd/vk13lib32.json HASVK_USE_RELOCATIONS=1 ;;
  *) echo "bad driver $drv"; exit 1 ;;
esac
[ "$ASYNC" = 1 ] && export DXVK_GPD_ASYNC_READBACK=1
pgrep -a "retroarch|devilutionx|ppsspp|dosbox|scummvm" && { echo "game running, not testing"; exit 1; }
h0=$(journalctl -k --no-pager | grep -Eci 'gpu hang|Resetting rcs')
base=${exe##*/}; base=${base%.exe}
rm -f "logs/${base}_d3d9.log"
echo "== $(basename "$dll") on $drv, $base $*, async=${ASYNC:-0}"
( cd run32 && timeout 600 "$HOME/Games/tools/wine32/bin/wine" "./$(basename "$exe")" "$@" 2>&1 | tail -12 )
rc=$?
"$HOME/Games/tools/wine32/bin/wineserver" -k 2>/dev/null
h1=$(journalctl -k --no-pager | grep -Eci 'gpu hang|Resetting rcs')
echo "rc=$rc hangs before/after: $h0/$h1"
grep -E "GPD|Skipping|Device does not|error|warn:.*(hang|lost|Timeout)" "logs/${base}_d3d9.log" 2>/dev/null | grep -v "warn:  DXVK: No adapters" | head -12
[ "$h1" != "$h0" ] && journalctl -k --no-pager | grep -Ei "gpu hang|Resetting rcs" | tail -3
