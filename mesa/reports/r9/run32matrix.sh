#!/bin/bash
# GPD side: the 32-bit DXVK x hasvk matrix with dxvk32test.sh (R9 section 9.3). Run only when no CTS run and no
# game is active. A line stops at the first GPU hang (the kernel's count grows) and the matrix goes on with the next.
cd ~/vk13-test || exit 1
DT=~/Games/installed/dark-souls-prepare-to-die-edition/dxvk-test
frames=${FRAMES:-2400}
out=logs/matrix32-$(date +%H%M%S).txt
hangs() { journalctl -k --no-pager | grep -Eci 'gpu hang|Resetting rcs'; }
line() { # name dll driver exe args...
  local name=$1 dll=$2 drv=$3 exe=$4; shift 4
  local h0=$(hangs)
  echo "### $name" | tee -a $out
  ./dxvk32test.sh "$dll" "$drv" "$exe" "$@" 2>&1 | tee -a $out | tail -4
  [ "$(hangs)" != "$h0" ] && echo "!!! GPU hang in $name, line stopped" | tee -a $out
}
for drv in stock vk13reloc vk13; do
  line "tex 2.4.1gpd $drv"  $DT/d3d9-gpd-2.4.1.dll        $drv d3d9tex32.exe $frames
  line "tex 2.6.2 $drv"     $DT/d3d9-2.6.2.dll            $drv d3d9tex32.exe $frames
  line "tex 2.7.1gpd $drv"  dxvk271gpd/x32/d3d9.dll       $drv d3d9tex32.exe $frames
done
for drv in stock vk13reloc vk13; do
  for dll in $DT/d3d9-gpd-2.4.1.dll dxvk271gpd/x32/d3d9.dll; do
    n=$(basename $(dirname $dll))-$(basename $dll)
    ASYNC=0 line "rb $n $drv async=0" $dll $drv d3d9rb32.exe 600 20 100
    ASYNC=1 line "rb $n $drv async=1" $dll $drv d3d9rb32.exe 600 20 100
  done
done
echo "matrix done, hangs: $(hangs)" | tee -a $out
