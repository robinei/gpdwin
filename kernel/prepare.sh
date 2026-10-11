#!/bin/sh
# Create the DKMS source tree dkms-tree/gpd-audio-1.0/ : the pinned upstream sources of kernel $V
# (sound/soc/codecs/rt5645.c and the Intel atom SST driver), our two patches applied, plus the Kbuild
# Makefiles and dkms.conf. DKMS compiles that tree for each installed kernel, so after a kernel
# update it is still $V's driver code until re-pinned (/maintain does that on every kernel update):
#   ./prepare.sh --repin 7.3.1   pin the sources of another kernel version (rewrites V below and
#                                both sources.sha256), then build the tree as usual
# The patches must apply exactly (no fuzz): a patch that only fits loosely fails here, to be redone
# by hand rather than landing somewhere else. Loads/installs nothing.
set -e
cd "$(dirname "$0")"
HERE=$PWD
V=7.2.9
B=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain
if [ "${1:-}" = --repin ]; then
    NEW=${2:?usage: prepare.sh --repin VERSION}
    for list in rt5645/sources.sha256 intel-sst/sources.sha256; do
        tmp=$(mktemp -d)
        while read -r sum path; do
            curl -fsSL -o "$tmp/f" "$B/${path#./}?h=v$NEW"
            echo "$(sha256sum "$tmp/f" | cut -d' ' -f1)  $path"
        done < "$list" > "$list.new"
        rm -rf "$tmp"
        mv "$list.new" "$list"
    done
    sed -i "s/^V=.*/V=$NEW/" "$0"
    echo "pinned to $NEW (see git diff kernel/); building the tree"
    exec "$0"
fi
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
patch -p1 --fuzz=0 -d "$T" -i "$HERE/rt5645/0001-ASoC-rt5645-restore-codec-after-hibernation.patch"
patch -p1 --fuzz=0 -d "$T" -i "$HERE/intel-sst/0001-ASoC-Intel-atom-sst-hibernation-pm-callbacks.patch"
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
