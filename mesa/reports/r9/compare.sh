#!/bin/bash
# GPD side: summary of all finished dEQP groups and per-test comparison of every vk13 run against its stock run
cd ~/vk13-test/logs || exit 1
echo "== summaries"
for s in *.sum; do
  n=${s%.sum}
  printf "%-26s %s | %s\n" "$n" "$(head -1 $s | sed 's/^[^:]*: *//')" "$(sed -n 2p $s)"
done
echo "== vk13 vs stock per test (same case list)"
for r in *-stock.res; do
  g=${r%-stock.res}
  for v in $g-vk13p2dbg.res $g-vk13p2.res $g-vk13dbg.res $g-final.res; do
    [ -f $v ] || continue
    d=$(diff <(sort $r) <(sort $v))
    if [ -z "$d" ]; then echo "$g: $v identical to stock ($(wc -l < $v) results)"; else echo "$g: $v DIFFERS:"; echo "$d" | head -20; fi
  done
done
echo "== non-pass results in vk13 runs (excluding NotSupported)"
grep -v " Pass\| NotSupported" *vk13*.res *final*.res 2>/dev/null | sort -u | head -40
echo "== hangs: $(journalctl -k --no-pager | grep -ci 'gpu hang|Resetting rcs')"
