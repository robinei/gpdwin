#!/bin/sh
# Temporary (2026-10-12): Dark Souls diagnostic runs for the DXVK 2.7.1 crash on save load (docs/steam-library.md,
# mesa/reports/R9-vulkan13.md section 9). Runs the R9 kit with logs, a memory sampler and the DXVK memory overlay;
# the installed game and launcher are not changed (the kit swaps DATA/d3d9.dll for 2.7.1 runs and restores it).
exec foot --app-id=shelf-tool bash -c '
kit=$HOME/gpd/mesa/reports/r9/darksouls-test.sh
echo "Dark Souls test runs (logs in ~/vk13-test/logs; load the same save each time)"
echo
echo "  1  DXVK 2.7.1 on the patched driver   (the run that crashed; now with logs)"
echo "  2  DXVK 2.4.1 on the patched driver   (is the driver alone fine in the real game?)"
echo "  3  DXVK 2.4.1 on the normal driver    (baseline, same logging)"
echo
read -p "Choose 1-3 (anything else: back) " c
case $c in 1) v=vk13-27 ;; 2) v=vk13-24 ;; 3) v=stock ;; *) exit 0 ;; esac
echo
"$kit" $v
echo
echo "If the game crashed: press Close on the Wine dialog first (or Save As... to ~/vk13-test/logs)."
read -p "Press Enter to return " x
'
