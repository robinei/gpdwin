#!/bin/bash
# Dark Souls PTDE on-screen test kit for the R9 hasvk patches (mesa/reports/R9-vulkan13.md, section 9).
# Run ON THE GPD from a terminal in sway (foot), by Robin:
#   ~/vk13-test/darksouls-test.sh VARIANT [--dry-run] [--dry-run-sleep N]
# VARIANT
#   stock          today's launcher: DATA/d3d9.dll (DXVK 2.4.1 + gplasync + async readback), installed lib32 hasvk 1.7
#   vk13-24        same DLL, the patched 32-bit driver (~/vk13-test/lib32, softpin, real Vulkan 1.3)
#   vk13-24-reloc  same DLL, the patched driver in relocation mode (HASVK_USE_RELOCATIONS=1): isolates "softpin"
#   vk13-27        DXVK 2.7.1 + gplasync 2.7.1 + ported async readback (~/vk13-test/dxvk271gpd/x32/d3d9.dll) on
#                  the patched driver with hasvk_report_descriptor_indexing=true. Only enabled when the 32-bit
#                  d3d9tex/d3d9rb runs passed (marker ~/vk13-test/vk13-27-ok exists; the script says so otherwise).
# Same settings as the real launcher + shelf's per-game settings for Dark Souls (cpu=on gpu=on tdp=8 pin=on
# fps=30 from ~/.config/pegasus-frontend/shelf-game-settings.txt, MangoHud via MANGOHUD=1 + the shim preload):
# game-wrap applies the power settings and backs the saves up as usual. Neither the launcher nor the game is
# edited; for vk13-27 DATA/d3d9.dll is swapped for the run and restored on any exit (trap on EXIT/INT/TERM/HUP),
# the restore is verified by sha256. ASYNC=0 in the environment turns DXVK_GPD_ASYNC_READBACK off (A/B of the
# readback workaround). Logs: ~/vk13-test/logs/ds-VARIANT-TIME.log (wine stderr) and .../ds-VARIANT-TIME/ (DXVK log).
# Bail out: quit the game (Esc menu), or from another terminal: pkill -x DARKSOULS.exe   (the script restores
# the DLL and exits; `pkill -f darksouls-test` also works). A GPU hang shows as a frozen picture: wait 10 s for
# the reset, then quit; journalctl -k | grep -i "gpu hang" counts them (printed at the end).
set -u
GAME="$HOME/Games/installed/dark-souls-prepare-to-die-edition"
DATA="$GAME/DATA"
T="$HOME/vk13-test"
variant=${1:-}; shift || true
dry=0; dry_sleep=0
while [ $# -gt 0 ]; do
  case $1 in
    --dry-run) dry=1 ;;
    --dry-run-sleep) dry=1; dry_sleep=$2; shift ;;
    *) echo "unknown option $1"; exit 2 ;;
  esac
  shift
done
case $variant in stock|vk13-24|vk13-24-reloc|vk13-27) ;; *) sed -n 2,24p "$0"; exit 2 ;; esac

# --- what to run with
swap_dll=""
export hasvk_report_vk_1_3_version=true
case $variant in
  stock) ;;
  vk13-24) export VK_DRIVER_FILES=$T/icd/vk13lib32.json ;;
  vk13-24-reloc) export VK_DRIVER_FILES=$T/icd/vk13lib32.json HASVK_USE_RELOCATIONS=1 ;;
  vk13-27)
    if [ $dry = 0 ] && [ ! -e "$T/vk13-27-ok" ]; then
      echo "vk13-27 is not enabled: the 32-bit DXVK 2.7.1 test runs (d3d9tex32/d3d9rb32 on the patched driver) have"
      echo "not passed yet (R9 section 9). Marker $T/vk13-27-ok missing."; exit 3
    fi
    export VK_DRIVER_FILES=$T/icd/vk13lib32.json hasvk_report_descriptor_indexing=true
    swap_dll="$T/dxvk271gpd/x32/d3d9.dll" ;;
esac
mkdir -p "$T/icd" "$T/logs"
cat > "$T/icd/vk13lib32.json" <<J
{ "ICD": { "api_version": "1.3.354", "library_arch": "32", "library_path": "$T/lib32/libvulkan_intel_hasvk.so" }, "file_format_version": "1.0.1" }
J
[ -n "${VK_DRIVER_FILES:-}" ] && [ ! -f "$T/lib32/libvulkan_intel_hasvk.so" ] && { echo "missing $T/lib32/libvulkan_intel_hasvk.so"; exit 3; }
[ -n "$swap_dll" ] && [ ! -f "$swap_dll" ] && { echo "missing $swap_dll"; exit 3; }

# --- the launcher's environment (gpd-launch.sh) + shelf's per-game settings
export WINEPREFIX="$HOME/.wine32" WINEARCH=win32 WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml,winegstreamer=d;d3d9=n;dinput8=n,b"
export DISPLAY=${DISPLAY:-:0}
if [ "${ASYNC:-1}" = 1 ]; then export DXVK_GPD_ASYNC_READBACK=1; else unset DXVK_GPD_ASYNC_READBACK; fi
stamp=$(date +%H%M%S)
logdir="$T/logs/ds-$variant-$stamp"; log="$T/logs/ds-$variant-$stamp.log"; mkdir -p "$logdir"
export DXVK_LOG_LEVEL=info DXVK_LOG_PATH="$logdir"
settings=$(grep -F "$GAME/gpd-launch.sh" "$HOME/.config/pegasus-frontend/shelf-game-settings.txt" | cut -f2)
for kv in $settings; do
  case $kv in
    cpu=*) export GPD_CPU_TURBO=${kv#cpu=} ;;
    gpu=*) export GPD_GPU_TURBO=${kv#gpu=} ;;
    tdp=*) export GPD_TDP_W=${kv#tdp=} ;;
    pin=on) export GPD_GPU_PIN=1 ;;
    fps=*) export MANGOHUD_CONFIG=read_cfg,fps_limit=${kv#fps=} ;;
  esac
done
export MANGOHUD=1
[ -e /usr/lib/mangohud/libMangoHud_shim.so ] && export LD_PRELOAD="/usr/\$LIB/mangohud/libMangoHud_shim.so"

# --- report
orig_sha=$(sha256sum "$DATA/d3d9.dll" | cut -c1-16)
echo "== Dark Souls test: variant $variant $([ $dry = 1 ] && echo '(DRY RUN, game not started)')"
echo "driver:  $([ -n "${VK_DRIVER_FILES:-}" ] && echo "patched lib32 hasvk (vk13, $(stat -c %y "$T/lib32/libvulkan_intel_hasvk.so" | cut -c1-16)) ${HASVK_USE_RELOCATIONS:+relocation mode}" || pacman -Q lib32-vulkan-intel-gpd)"
echo "d3d9:    $([ -n "$swap_dll" ] && echo "DXVK 2.7.1-gpd (gplasync + async readback port) $(sha256sum "$swap_dll" | cut -c1-16), swapped in for the run" || echo "DATA/d3d9.dll as installed (DXVK 2.4.1-gpd) $orig_sha")"
echo "env:     ASYNC_READBACK=${DXVK_GPD_ASYNC_READBACK:-0} claim13=$hasvk_report_vk_1_3_version descidx=${hasvk_report_descriptor_indexing:-0} shelf: $settings"
echo "log:     $log (+ $logdir/)"
echo "hangs so far: $(journalctl -k --no-pager | grep -Eci 'gpu hang|Resetting rcs')"
pgrep -x DARKSOULS.exe >/dev/null && { echo "DARKSOULS.exe is already running"; exit 3; }

# --- DLL swap with restore on any exit
backup=""
restore() {
  trap - EXIT INT TERM HUP
  if [ -n "$backup" ]; then
    cp -f "$backup" "$DATA/d3d9.dll"
    now=$(sha256sum "$DATA/d3d9.dll" | cut -c1-16)
    if [ "$now" = "$orig_sha" ]; then echo "DATA/d3d9.dll restored (sha256 $now, verified)"; rm -f "$backup"
    else echo "!! DATA/d3d9.dll NOT restored (sha256 $now, want $orig_sha); original kept at $backup"; fi
  fi
  echo "hangs now: $(journalctl -k --no-pager | grep -Eci 'gpu hang|Resetting rcs')"
}
child=""
on_signal() { [ -n "$child" ] && kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; restore; exit 130; }
trap restore EXIT
trap on_signal INT TERM HUP
if [ -n "$swap_dll" ]; then
  backup="$T/d3d9.dll.orig-$orig_sha"
  cp -f "$DATA/d3d9.dll" "$backup" && cp -f "$swap_dll" "$DATA/d3d9.dll" || { echo "swap failed"; exit 3; }
  echo "DATA/d3d9.dll swapped: now $(sha256sum "$DATA/d3d9.dll" | cut -c1-16), original saved as $backup"
fi

cd "$DATA" || exit 3
if [ $dry = 1 ]; then
  echo "(dry run: would run game-wrap $HOME/Games/tools/wine32/bin/wine ./DARKSOULS.exe; sleeping $dry_sleep s)"
  sleep "$dry_sleep" & child=$!; wait "$child"; child=""
else
  "$HOME/gpd/scripts/game-wrap" "$HOME/Games/tools/wine32/bin/wine" "./DARKSOULS.exe" >"$log" 2>&1 &
  child=$!; wait "$child"; rc=$?; child=""
  "$HOME/Games/tools/wine32/bin/wineserver" -k 2>/dev/null
  echo "game ended rc=$rc"
  grep -h -E "GPD|Skipping|Device does not|Vulkan 1\.|Found device|D3D9: VK_|error" "$logdir"/*.log 2>/dev/null | head -15
fi
