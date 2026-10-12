#!/bin/bash
# GPD side: first smoke test of the vk13 hasvk build (nothing installed; VK_DRIVER_FILES points at ~/vk13-test)
cd ~/vk13-test || exit 1
mkdir -p icd
cat > icd/vk13.json <<J
{ "ICD": { "api_version": "1.3.354", "library_arch": "64", "library_path": "/home/robin/vk13-test/lib/libvulkan_intel_hasvk.so" }, "file_format_version": "1.0.1" }
J
cat > icd/vk13dbg.json <<J
{ "ICD": { "api_version": "1.3.354", "library_arch": "64", "library_path": "/home/robin/vk13-test/lib/libvulkan_intel_hasvk_dbg.so" }, "file_format_version": "1.0.1" }
J
echo "== system icd"; ls /usr/share/vulkan/icd.d/
cd r5
for p in vkq vkb vkdepth; do [ -x $p ] || gcc -O2 -o $p $p.c -lvulkan -lm 2>&1 | tail -2; done
ls
echo "== games"; pgrep -a "retroarch|wine|devilutionx|ppsspp|dosbox|scummvm"; echo "(checked)"
echo "== hangs before"; journalctl -k --no-pager | grep -ci "gpu hang|Resetting rcs"
echo "== stock vkq"; ./vkq 2>&1 | head -4
echo "== vk13 vkq"; VK_DRIVER_FILES=/home/robin/vk13-test/icd/vk13.json ./vkq 2>&1 | head -4
echo "== vk13 vulkaninfo summary"; VK_DRIVER_FILES=/home/robin/vk13-test/icd/vk13.json ../vulkaninfo --summary 2>&1 | grep -E "apiVersion|driverVersion|deviceName|conformance|driverInfo" | head
echo "== hangs after"; journalctl -k --no-pager | grep -ci "gpu hang|Resetting rcs"
