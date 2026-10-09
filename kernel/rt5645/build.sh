#!/bin/sh
# Build the patched snd-soc-rt5645 for the running kernel (docs/hardware.md "Audio"): fetch the
# exact upstream sources, verify them, apply the patch, build against the installed headers.
# Does NOT load or install anything. Output: build/snd-soc-rt5645.ko
set -e
cd "$(dirname "$0")"
V=${1:-$(uname -r | sed 's/-.*//')}
[ "$V" = 7.2.9 ] || { echo "sources are pinned for 7.2.9, running $V: update the sha256 pins and check the patch applies" >&2; exit 1; }
[ -d /usr/lib/modules/$(uname -r)/build ] || { echo "install linux-headers" >&2; exit 1; }
B=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/sound/soc/codecs
rm -rf build; mkdir -p build/a/sound/soc/codecs; cd build
for f in rt5645.c rt5645.h rl6231.h; do curl -fsSL -o "a/sound/soc/codecs/$f" "$B/$f?h=v$V"; done
sha256sum -c <<'SUMS'
9f7b2349ab4a6571d70bbac557fd467762f0d14edff21944e8ebab06ebe475bc  a/sound/soc/codecs/rt5645.c
484be613886ec5536b427ea58d480b6a618727d14a03e4ce729de72443b0f4c6  a/sound/soc/codecs/rt5645.h
c57abf5ffe4a6f6c80d745d5fa23393aa3eb8fd71c14cfa2f332098750f959dd  a/sound/soc/codecs/rl6231.h
SUMS
P=$(cd .. && pwd)/0001-ASoC-rt5645-restore-codec-after-hibernation.patch
# the patch is written against an a/ and b/ tree: apply it to a copy of the verified sources
mkdir -p b; cp -r a/sound b/
patch -p1 -d b -i "$P"
cp b/sound/soc/codecs/rt5645.c b/sound/soc/codecs/rt5645.h b/sound/soc/codecs/rl6231.h .
printf 'obj-m := snd-soc-rt5645.o\nsnd-soc-rt5645-y := rt5645.o\n' > Makefile
make -C /usr/lib/modules/$(uname -r)/build M=$PWD modules
modinfo ./snd-soc-rt5645.ko | grep -E '^(vermagic|depends)'
echo "built $(pwd)/snd-soc-rt5645.ko"
