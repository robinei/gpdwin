#!/bin/sh
# Create the DKMS source tree of one of our module packages in dkms-tree/<package>-<version>/: the pinned
# upstream sources of kernel $V, our patches applied, plus Kbuild Makefiles and dkms.conf. DKMS compiles that
# tree for each installed kernel (scripts/sync installs it: manifest "dkms" entries), so after a kernel update
# it is still $V's driver code until re-pinned (/maintain does that on every kernel update):
#   ./prepare.sh gpd-audio        rt5645 codec + Intel atom SST: sound after hibernation (docs/hardware.md "Audio")
#   ./prepare.sh gpd-usbpd        chtwc_int33fe: ask USB-PD chargers for 5 V only (docs/power.md "Charging voltage")
#   ./prepare.sh --repin 7.3.1    pin the sources of another kernel version (rewrites V below and every
#                                 sources.sha256), then build both trees
# The patches must apply exactly (no fuzz): a patch that only fits loosely fails here, to be redone
# by hand rather than landing somewhere else. Loads/installs nothing.
set -e
cd "$(dirname "$0")"
HERE=$PWD
V=7.2.9
B=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain
LISTS="rt5645/sources.sha256 intel-sst/sources.sha256 usbpd/sources.sha256"
fetch() { curl -fsSL --retry 5 --retry-delay 3 --retry-all-errors -o "$1" "$2"; } # kernel.org rate-limits (503)
if [ "${1:-}" = --repin ]; then
    NEW=${2:?usage: prepare.sh --repin VERSION}
    for list in $LISTS; do
        tmp=$(mktemp -d)
        while read -r sum path; do
            fetch "$tmp/f" "$B/${path#./}?h=v$NEW" || { rm -rf "$tmp" "$list.new"; exit 1; }
            echo "$(sha256sum "$tmp/f" | cut -d' ' -f1)  $path"
        done < "$list" > "$list.new" || exit 1
        rm -rf "$tmp"
        mv "$list.new" "$list"
    done
    sed -i "s/^V=.*/V=$NEW/" "$HERE/prepare.sh"
    echo "pinned to $NEW (see git diff devices/gpd-win1/kernel/); building the trees"
    "$HERE/prepare.sh" gpd-audio
    exec "$HERE/prepare.sh" gpd-usbpd
fi

# sources LIST...: fetch the files a sources.sha256 names into $T and check them
sources() {
    for list in "$@"; do
        while read -r sum path; do
            mkdir -p "$T/$(dirname "$path")"
            fetch "$T/$path" "$B/${path#./}?h=v$V"
        done < "$list"
        (cd "$T" && sha256sum -c --quiet "$HERE/$list")
    done
}

case ${1:-} in
gpd-audio)
    T=dkms-tree/gpd-audio-1.0
    rm -rf "$T"; mkdir -p "$T"
    sources rt5645/sources.sha256 intel-sst/sources.sha256
    patch -p1 --fuzz=0 -s -d "$T" -i "$HERE/rt5645/0001-ASoC-rt5645-restore-codec-after-hibernation.patch"
    patch -p1 --fuzz=0 -s -d "$T" -i "$HERE/intel-sst/0001-ASoC-Intel-atom-sst-hibernation-pm-callbacks.patch"
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
    ;;
gpd-usbpd)
    T=dkms-tree/gpd-usbpd-1.0
    rm -rf "$T"; mkdir -p "$T"
    sources usbpd/sources.sha256
    patch -p1 --fuzz=0 -s -d "$T" -i "$HERE/usbpd/0001-chtwc_int33fe-GPD-Win-5V-only.patch"
    printf 'obj-m += drivers/platform/x86/intel/\n' > "$T/Makefile"
    printf 'obj-m += intel-chtwc_int33fe.o\nintel-chtwc_int33fe-y := chtwc_int33fe.o\n' > "$T/drivers/platform/x86/intel/Makefile"
    cat > "$T/dkms.conf" <<'CONF'
PACKAGE_NAME="gpd-usbpd"
PACKAGE_VERSION="1.0"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"
BUILT_MODULE_NAME[0]="intel-chtwc_int33fe"
BUILT_MODULE_LOCATION[0]="drivers/platform/x86/intel"
DEST_MODULE_LOCATION[0]="/updates"
AUTOINSTALL="yes"
CONF
    ;;
*)
    echo "usage: $0 gpd-audio|gpd-usbpd | --repin VERSION" >&2; exit 2 ;;
esac
echo "tree ready: $PWD/$T"
