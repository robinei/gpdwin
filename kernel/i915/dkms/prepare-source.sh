#!/bin/sh
# DKMS PRE_BUILD for gpd-i915 (runs as root in the DKMS build directory): fetch the i915 source of
# the kernel being built for, apply Arch's i915 changes for that release and our patches (patches/).
# Any failure stops the build, DKMS installs nothing and the stock i915 stays in use.
#   prepare-source.sh 7.2.9-arch1-1
set -eu
KV=$1
V=${KV%%-*}                     # 7.2.9
REST=${KV#*-}; AREL=${REST%-*}  # arch1
MAJ=${V%%.*}
CACHE=${GPD_I915_CACHE:-/var/cache/gpd-i915}
mkdir -p "$CACHE"
TB=linux-$V.tar.xz
AP=linux-v$V-$AREL.patch.zst
if [ ! -f "$CACHE/$TB" ]; then
    echo "gpd-i915: downloading $TB (~150 MB)"
    curl -fsSL --retry 3 -o "$CACHE/$TB.part" "https://cdn.kernel.org/pub/linux/kernel/v$MAJ.x/$TB"
    curl -fsSL --retry 3 -o "$CACHE/sha256sums.asc" "https://cdn.kernel.org/pub/linux/kernel/v$MAJ.x/sha256sums.asc"
    want=$(awk -v f="$TB" '$2 == f {print $1}' "$CACHE/sha256sums.asc")
    got=$(sha256sum "$CACHE/$TB.part" | cut -d' ' -f1)
    [ -n "$want" ] && [ "$want" = "$got" ] || { echo "gpd-i915: checksum mismatch for $TB"; exit 1; }
    mv "$CACHE/$TB.part" "$CACHE/$TB"
fi
if [ ! -f "$CACHE/$AP" ]; then
    curl -fsSL --retry 3 -o "$CACHE/$AP.part" \
        "https://github.com/archlinux/linux/releases/download/v$V-$AREL/$AP"
    mv "$CACHE/$AP.part" "$CACHE/$AP"
fi
# keep only the sources of this version in the cache
find "$CACHE" -maxdepth 1 -name 'linux-*' ! -name "$TB" ! -name "$AP" -delete
rm -rf src
mkdir -p src/inc/a   # the tracepoint headers include "../../drivers/gpu/drm/i915/..."
tar -xf "$CACHE/$TB" -C src --strip-components=1 \
    "linux-$V/drivers/gpu/drm/i915" "linux-$V/drivers/platform/x86/intel_ips.h"
zstd -dcq "$CACHE/$AP" | awk '/^diff --git/{p = ($3 ~ /^a\/drivers\/gpu\/drm\/i915\//)} p' > arch-i915.patch
[ -s arch-i915.patch ] && patch -p1 -d src -s --forward < arch-i915.patch
for p in patches/*.patch; do
    echo "gpd-i915: applying $(basename "$p")"
    patch -p1 -d src -s --forward < "$p"
done
