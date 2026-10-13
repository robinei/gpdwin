#!/bin/bash
# GPD side: extra runs on the final builds, queued behind whatever deqprun is active (runall2 also waits).
cd ~/vk13-test || exit 1
M=deqp/mustpass
grep -h -E "^dEQP-VK\.descriptor_indexing\." $M/*.txt $M/*/*.txt > lists-descidx-full.txt
grep -h -E "^dEQP-VK\.binding_model\.shader_access\." $M/*.txt $M/*/*.txt | awk 'NR % 100 == 0' > lists-shaderaccess.txt
grep -h -E "^dEQP-VK\.binding_model\.(descriptorset_random|mutable_descriptor|descriptor_buffer|buffer_device_address)" $M/*.txt $M/*/*.txt | grep -v buffer_device_address | awk 'NR % 100 == 0' > lists-bm-other.txt
wc -l lists-descidx-full.txt lists-shaderaccess.txt lists-bm-other.txt
wait_idle() { while pgrep -x "deqprun.sh" >/dev/null || pgrep -x "retroarch|wine|wine64|wine-preloader|devilutionx|ppsspp|dosbox|scummvm" >/dev/null; do sleep 20; done; }
run() { wait_idle; ./deqprun.sh "$1" "$2" "$3"; }
run shaderaccess-final vk13finaldbg lists-shaderaccess.txt
run shaderaccess-stock stock lists-shaderaccess.txt
hasvk_report_descriptor_indexing=true run descidx-claim-final vk13finaldbg lists-descidx-full.txt
run bm-other-final vk13finaldbg lists-bm-other.txt
run bm-other-stock stock lists-bm-other.txt
echo QUEUE2DONE > logs/QUEUE2DONE
