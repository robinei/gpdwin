#!/bin/bash
# mkicd.sh NAME...  -> ~/vk13-test/icd/vk13NAME.json pointing at lib/libvulkan_intel_hasvk_NAME.so
cd ~/vk13-test || exit 1
for v in "$@"; do
  printf '{ "ICD": { "api_version": "1.3.354", "library_arch": "64", "library_path": "/home/robin/vk13-test/lib/libvulkan_intel_hasvk_%s.so" }, "file_format_version": "1.0.1" }\n' "$v" > icd/vk13$v.json
  echo "icd/vk13$v.json: $(VK_DRIVER_FILES=/home/robin/vk13-test/icd/vk13$v.json ./r5/vkq | head -1)"
done
