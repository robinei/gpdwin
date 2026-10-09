#!/bin/sh
# Build a patched i915.ko for the GPD's exact kernel (7.2.9-arch1-1). Runs on the DESKTOP, not on the
# GPD (i915 is ~300 files; ~1 min here, far too slow on the Atom). Inputs are pinned in sources.sha256:
# the kernel.org tarball, Arch's patch for that release (it touches i915/display/intel_dp.c, so it is
# applied too, to differ from the stock module only by our patches) and Arch's linux-headers package
# (.config, Module.symvers, build scripts). Needs the same gcc major as Arch used (see build/info).
# Output: out/i915.ko.zst (+ out/info). Copy it to the GPD and run install-test.sh there.
#   ./build.sh                 all NNNN-*.patch in this directory
#   ./build.sh 0002 [0001..]   only the patches with these numbers (one experiment at a time)
#   ./build.sh none            no patches of ours (stock-equivalent module, to test the recipe itself)
set -e
cd "$(dirname "$0")"
HERE=$PWD
V=7.2.9
KV=7.2.9-arch1-1
mkdir -p cache
cd cache
[ -f linux-$V.tar.xz ] || curl -fsSLO https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-$V.tar.xz
[ -f linux-v$V-arch1.patch.zst ] ||
    curl -fsSLO https://github.com/archlinux/linux/releases/download/v$V-arch1/linux-v$V-arch1.patch.zst
[ -f linux-headers-$V.arch1-1-x86_64.pkg.tar.zst ] ||
    curl -fsSLO https://archive.archlinux.org/packages/l/linux-headers/linux-headers-$V.arch1-1-x86_64.pkg.tar.zst
sha256sum -c "$HERE/sources.sha256"
cd "$HERE"
rm -rf build out
mkdir -p build/hdr build/src/inc/a out
tar -xf cache/linux-headers-$V.arch1-1-x86_64.pkg.tar.zst -C build/hdr
# i915 plus the one header it includes from outside its directory
tar -xf cache/linux-$V.tar.xz -C build/src --strip-components=1 \
    linux-$V/drivers/gpu/drm/i915 linux-$V/drivers/platform/x86/intel_ips.h
# Arch's patch, i915 part only
zstd -dcq cache/linux-v$V-arch1.patch.zst |
    awk '/^diff --git/{p = ($3 ~ /^a\/drivers\/gpu\/drm\/i915\//)} p' > build/arch-i915.patch
patch -p1 -d build/src -s < build/arch-i915.patch
applied=
if [ "$1" != none ]; then
    for p in "$HERE"/[0-9][0-9][0-9][0-9]-*.patch; do
        [ -e "$p" ] || continue
        n=$(basename "$p" | cut -c1-4)
        [ $# -eq 0 ] || echo " $* " | grep -q " $n " || continue
        echo "applying $(basename "$p")"
        patch -p1 -d build/src -s < "$p"
        applied="$applied $(basename "$p")"
    done
fi
# The tracepoint headers include "../../drivers/gpu/drm/i915/...": give gcc a directory two levels
# below the tree root to resolve that from.
make -C build/hdr/usr/lib/modules/$KV/build M="$HERE/build/src/drivers/gpu/drm/i915" \
    KCFLAGS="-I$HERE/build/src/inc/a" -j"$(nproc)" modules
ko=build/src/drivers/gpu/drm/i915/i915.ko
modinfo "$ko" | grep -q "^vermagic: *$KV " || { echo "vermagic mismatch"; exit 1; }
zstd -q -19 "$ko" -o out/i915.ko.zst
{
    echo "kernel $KV"
    echo "patches:${applied:- none}"
    echo "srcversion $(modinfo -F srcversion "$ko")"
    echo "gcc $(gcc -dumpfullversion), Arch used: $(grep CONFIG_CC_VERSION_TEXT build/hdr/usr/lib/modules/$KV/build/.config)"
    echo "repo $(git -C "$HERE" describe --always --dirty)"
    (cd out && sha256sum i915.ko.zst)
} > out/info
cat out/info
