#!/bin/bash
# GPD side: reduced sequential dEQP-VK plan (replaces runall.sh after the bda group). Waits for a running
# deqprun to finish first; pauses between groups while a game is running.
cd ~/vk13-test || exit 1
M=deqp/mustpass
mk() { # name, pattern, step
  grep -h -E "^dEQP-VK\.$2" $M/*.txt $M/*/*.txt 2>/dev/null | awk -v s="$3" 'NR % s == 0' > lists-$1.txt
  echo "lists-$1.txt: $(wc -l < lists-$1.txt)"
}
mk info 'api\.info|info\.' 1
mk apibuf 'api\.buffer' 1
mk cmdbuf 'api\.command_buffers' 1
mk devinit 'api\.device_init|api\.version_check|api\.object_management' 1
mk push 'pipeline\.monolithic\.push_constant|pipeline\.monolithic\.push_descriptor' 1
mk memory 'memory\.' 5
mk copy 'api\.copy_and_blit' 200
mk query 'query_pool' 10
mk sync 'synchronization\.' 60
mk draw 'draw\.' 25
mk compute 'compute\.' 50
mk pipeline 'pipeline\.monolithic' 200
mk glsl 'glsl\.' 50
mk dynstate 'dynamic_state' 10
mk robust 'robustness\.' 60
mk texture 'texture\.' 200
mk ssbo 'ssbo\.|ubo\.' 40
mk descidx 'descriptor_indexing\.' 20
wait_idle() {
  while pgrep -x "deqprun.sh" >/dev/null || pgrep -x "retroarch|wine|wine64|wine-preloader|devilutionx|ppsspp|dosbox|scummvm" >/dev/null; do sleep 30; done
}
run() { wait_idle; ./deqprun.sh "$1" "$2" "$3"; }
run info-vk13p2 vk13p2 lists-info.txt
run push-vk13p2dbg vk13p2dbg lists-push.txt
run push-stock stock lists-push.txt
run apibuf-vk13p2dbg vk13p2dbg lists-apibuf.txt
run cmdbuf-vk13p2dbg vk13p2dbg lists-cmdbuf.txt
run cmdbuf-stock stock lists-cmdbuf.txt
run devinit-vk13p2dbg vk13p2dbg lists-devinit.txt
run memory-vk13p2dbg vk13p2dbg lists-memory.txt
run memory-stock stock lists-memory.txt
run draw-vk13p2dbg vk13p2dbg lists-draw.txt
run draw-stock stock lists-draw.txt
run pipeline-vk13p2dbg vk13p2dbg lists-pipeline.txt
run pipeline-stock stock lists-pipeline.txt
run compute-vk13p2dbg vk13p2dbg lists-compute.txt
run compute-stock stock lists-compute.txt
run query-vk13p2dbg vk13p2dbg lists-query.txt
run query-stock stock lists-query.txt
run copy-vk13p2dbg vk13p2dbg lists-copy.txt
run copy-stock stock lists-copy.txt
run sync-vk13p2dbg vk13p2dbg lists-sync.txt
run sync-stock stock lists-sync.txt
run glsl-vk13p2dbg vk13p2dbg lists-glsl.txt
run glsl-stock stock lists-glsl.txt
run dynstate-vk13p2dbg vk13p2dbg lists-dynstate.txt
run dynstate-stock stock lists-dynstate.txt
run robust-vk13p2dbg vk13p2dbg lists-robust.txt
run robust-stock stock lists-robust.txt
run ssbo-vk13p2dbg vk13p2dbg lists-ssbo.txt
run ssbo-stock stock lists-ssbo.txt
run texture-vk13p2dbg vk13p2dbg lists-texture.txt
run texture-stock stock lists-texture.txt
echo ALLDONE > logs/ALLDONE
