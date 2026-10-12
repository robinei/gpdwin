#!/bin/bash
# GPD side: feature diff, mustpass inventory, quick functional/perf runs of the r5 programs
cd ~/vk13-test || exit 1
V13=VK_DRIVER_FILES=/home/robin/vk13-test/icd/vk13.json
which python3; nproc
echo "== games"; pgrep -a "retroarch|wine|devilutionx|ppsspp|dosbox|scummvm"; echo "(checked)"
mkdir -p logs
./r5/vkq > logs/vkq-stock.txt 2>&1
env $V13 ./r5/vkq > logs/vkq-vk13.txt 2>&1
echo "== vkq diff (stock -> vk13)"; diff logs/vkq-stock.txt logs/vkq-vk13.txt
echo "== mustpass files"; ls deqp/mustpass | head -40; wc -l deqp/mustpass/*.txt | tail -1
for g in "binding_model.buffer_device_address" "api.info" "api.buffer" "memory" "pipeline.monolithic.push_constant" "api.device_init" "api.version_check" "api.feature" "info"; do
  n=$(cat deqp/mustpass/*.txt | grep -c "^dEQP-VK.$g"); echo "$g: $n"
done
echo "== vkb stock"; ./r5/vkb 2>&1 | tail -3
echo "== vkb vk13"; env $V13 ./r5/vkb 2>&1 | tail -3
echo "== vkdepth stock"; ./r5/vkdepth 2>&1 | tail -4
echo "== vkdepth vk13"; env $V13 ./r5/vkdepth 2>&1 | tail -4
echo "== hangs"; journalctl -k --no-pager | grep -ci "gpu hang|Resetting rcs"
