#!/bin/sh
# Build the patched snd-intel-sst-core for the running kernel (docs/hardware.md "Audio"): fetch the
# exact upstream sources, verify them, apply the patch, build against the installed headers.
# Does NOT load or install anything. Output: build/sound/soc/intel/atom/sst/snd-intel-sst-core.ko
set -e
cd "$(dirname "$0")"
HERE=$PWD
V=${1:-$(uname -r | sed 's/-.*//')}
[ "$V" = 7.2.9 ] || { echo "sources are pinned for 7.2.9, running $V: update sources.sha256 and check the patch applies" >&2; exit 1; }
[ -d /usr/lib/modules/$(uname -r)/build ] || { echo "install linux-headers" >&2; exit 1; }
B=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain
rm -rf build
mkdir build
cd build
while read -r sum path; do
    mkdir -p "$(dirname "$path")"
    curl -fsSL -o "$path" "$B/${path#./}?h=v$V"
done < "$HERE/sources.sha256"
sha256sum -c "$HERE/sources.sha256"
# the patch is written for an a/ -> b/ pair of trees: apply it inside a copy of the verified sources
mkdir tmp
cp -r sound tmp/
patch -p1 -d tmp -i "$HERE/0001-ASoC-Intel-atom-sst-hibernation-pm-callbacks.patch"
cp tmp/sound/soc/intel/atom/sst/sst.c sound/soc/intel/atom/sst/sst.c
cd sound/soc/intel/atom/sst
printf 'snd-intel-sst-core-y := sst.o sst_ipc.o sst_stream.o sst_drv_interface.o sst_loader.o sst_pvt.o\nobj-m += snd-intel-sst-core.o\n' > Makefile
make -C /usr/lib/modules/$(uname -r)/build M=$PWD modules
modinfo ./snd-intel-sst-core.ko | grep -E '^(vermagic|depends)'
echo "built $PWD/snd-intel-sst-core.ko"
