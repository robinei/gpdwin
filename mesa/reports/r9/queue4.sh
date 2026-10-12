#!/bin/bash
# GPD side: the runs that never started (deqprun.sh had lost its exec bit after an rsync: robust-stock, ssbo,
# texture, all of queue2.sh and queue3.sh) plus repeats of the draw group's failing cases on both drivers.
cd ~/vk13-test || exit 1
M=deqp/mustpass
grep -h " Fail" logs/draw-stock.res logs/draw-vk13p2dbg.res | cut -d" " -f1 | sort -u > lists-drawfail.txt
grep -h -E "^dEQP-VK\.draw\.(renderpass|dynamic_rendering\.primary_cmd_buff)\.indexed_draw\." $M/*.txt $M/*/*.txt | awk 'NR % 3 == 0' > lists-indexed.txt
wc -l lists-drawfail.txt lists-indexed.txt
wait_idle() { while pgrep -x "deqprun.sh|run32matrix.sh" >/dev/null || pgrep -x "retroarch|wine|wine64|wine-preloader|devilutionx|ppsspp|dosbox|scummvm|DARKSOULS.exe" >/dev/null; do sleep 20; done; }
run() { wait_idle; ./deqprun.sh "$1" "$2" "$3"; }
run drawfail1-stock stock lists-drawfail.txt
run drawfail1-final vk13finaldbg lists-drawfail.txt
run drawfail2-stock stock lists-drawfail.txt
run drawfail2-final vk13finaldbg lists-drawfail.txt
run indexed-stock stock lists-indexed.txt
run indexed-final vk13finaldbg lists-indexed.txt
run draw-final vk13finaldbg lists-draw.txt
run pipeline-final vk13finaldbg lists-pipeline.txt
run push-final vk13finaldbg lists-push.txt
run bda5-final vk13finaldbg lists-bda5.txt
run devinit-final vk13finaldbg lists-devinit.txt
run devinit-stock stock lists-devinit.txt
hasvk_report_descriptor_indexing=true run descidx-claim-final vk13finaldbg lists-descidx-full.txt
run robust-stock stock lists-robust.txt
run ssbo-final vk13finaldbg lists-ssbo.txt
run ssbo-stock stock lists-ssbo.txt
run texture-final vk13finaldbg lists-texture.txt
run texture-stock stock lists-texture.txt
run shaderaccess-final vk13finaldbg lists-shaderaccess.txt
run shaderaccess-stock stock lists-shaderaccess.txt
run compute-final vk13finaldbg lists-compute.txt
run glsl-final vk13finaldbg lists-glsl.txt
run memory-final vk13finaldbg lists-memory.txt
run bm-other-final vk13finaldbg lists-bm-other.txt
run bm-other-stock stock lists-bm-other.txt
echo QUEUE4DONE > logs/QUEUE4DONE
