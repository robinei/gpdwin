#!/bin/sh
# Create the DKMS source tree dkms-tree/gpd-audio-1.0/ : the pinned upstream sources of kernel $V
# (sound/soc/codecs/rt5645.c and the Intel atom SST driver), our two patches applied, plus the Kbuild
# Makefiles and dkms.conf. The sources stay the ones the patches were written against, whatever
# kernel is running; DKMS compiles them for each installed kernel. Loads/installs nothing.
set -e
cd "$(dirname "$0")"
HERE=$PWD
V=7.2.9
B=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain
T=dkms-tree/gpd-audio-1.0
rm -rf dkms-tree
mkdir -p "$T"
for list in rt5645/sources.sha256 intel-sst/sources.sha256; do
    while read -r sum path; do
        mkdir -p "$T/$(dirname "$path")"
        curl -fsSL -o "$T/$path" "$B/${path#./}?h=v$V"
    done < "$list"
    (cd "$T" && sha256sum -c "$HERE/$list")
done
patch -p1 -d "$T" -i "$HERE/rt5645/0001-ASoC-rt5645-restore-codec-after-hibernation.patch"
patch -p1 -d "$T" -i "$HERE/intel-sst/0001-ASoC-Intel-atom-sst-hibernation-pm-callbacks.patch"
printf 'obj-m += sound/soc/codecs/\nobj-m += sound/soc/intel/atom/sst/\n' > "$T/Makefile"
printf 'obj-m += snd-soc-rt5645.o\nsnd-soc-rt5645-y := rt5645.o\n' > "$T/sound/soc/codecs/Makefile"
printf 'snd-intel-sst-core-y := sst.o sst_ipc.o sst_stream.o sst_drv_interface.o sst_loader.o sst_pvt.o\nobj-m += snd-intel-sst-core.o\n' > "$T/sound/soc/intel/atom/sst/Makefile"
cat > "$T/dkms.conf" <<'CONF'
PACKAGE_NAME="gpd-audio"
PACKAGE_VERSION="1.0"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"
BUILT_MODULE_NAME[0]="snd-soc-rt5645"
BUILT_MODULE_LOCATION[0]="sound/soc/codecs"
DEST_MODULE_LOCATION[0]="/updates"
BUILT_MODULE_NAME[1]="snd-intel-sst-core"
BUILT_MODULE_LOCATION[1]="sound/soc/intel/atom/sst"
DEST_MODULE_LOCATION[1]="/updates"
AUTOINSTALL="yes"
CONF
echo "tree ready: $PWD/$T"
