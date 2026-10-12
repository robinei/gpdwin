#!/bin/bash
# Runs inside the r7 chroot: assert-enabled (b_ndebug=false) build of hasvk in /build/mesa/src/build-dbg
set -e
cd /build/mesa/src
export PATH=/build/mesa/src/build-clc/src/compiler/clc:/build/mesa/src/build-clc/src/compiler/spirv:$PATH
if [ ! -f build-dbg/build.ninja ]; then
  meson setup build-dbg mesa-26.2.4 --buildtype=plain --optimization 2 -Db_ndebug=false \
    -Dandroid-libbacktrace=disabled -Dplatforms=x11,wayland -Dgallium-drivers=crocus,softpipe \
    -Dvulkan-drivers=intel_hasvk -Dvulkan-layers= -Dgallium-extra-hud=false -Dgallium-va=disabled \
    -Dgallium-mediafoundation=disabled -Dgallium-rusticl=false -Dvideo-codecs= -Dmesa-clc=system \
    -Dllvm=disabled -Dspirv-tools=disabled -Dlmsensors=disabled -Dlibunwind=disabled -Dvalgrind=disabled \
    -Dhtml-docs=disabled -Dintel-rt=disabled -Dmicrosoft-clc=disabled -Dgles1=disabled \
    -Dvulkan-loader-rpath= -Dvulkan-manifest-per-architecture=false -Dprefix=/usr -Dlibdir=lib 2>&1 | tail -3
fi
nice -n 10 ninja -C build-dbg -j16 src/intel/vulkan_hasvk/libvulkan_intel_hasvk.so 2>&1 | grep -i -E "warning|error|Linking target" | tail -40
ls -la build-dbg/src/intel/vulkan_hasvk/libvulkan_intel_hasvk.so
