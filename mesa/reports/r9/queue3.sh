#!/bin/bash
# GPD side: confirmation runs on the final build (patches 1-4), queued behind other deqpruns.
cd ~/vk13-test || exit 1
awk 'NR % 5 == 0' lists-bda.txt > lists-bda5.txt
wc -l lists-bda5.txt
wait_idle() { while pgrep -x "deqprun.sh" >/dev/null || pgrep -x "retroarch|wine|wine64|wine-preloader|devilutionx|ppsspp|dosbox|scummvm" >/dev/null; do sleep 20; done; }
run() { wait_idle; ./deqprun.sh "$1" "$2" "$3"; }
run draw-final vk13finaldbg lists-draw.txt
run pipeline-final vk13finaldbg lists-pipeline.txt
run compute-final vk13finaldbg lists-compute.txt
run glsl-final vk13finaldbg lists-glsl.txt
run push-final vk13finaldbg lists-push.txt
run bda5-final vk13finaldbg lists-bda5.txt
run memory-final vk13finaldbg lists-memory.txt
run devinit-final vk13finaldbg lists-devinit.txt
echo QUEUE3DONE > logs/QUEUE3DONE
