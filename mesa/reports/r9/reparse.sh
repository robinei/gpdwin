#!/bin/bash
# re-parse .out files whose .res is empty (runs made with the broken runner); summarize all finished groups
cd ~/vk13-test/logs || exit 1
for o in *.out; do
  n=${o%.out}
  [ -f $n.sum ] || continue
  if [ ! -s $n.res ]; then
    awk '/^Test case /{sub(/^Test case ./,""); sub(/.\.\.$/,""); t=$0; next}
         /^  (Pass|Fail|NotSupported|QualityWarning|CompatibilityWarning|InternalError|ResourceError|Crash|Timeout|Waiver)/{
            split($1,a," "); if(t!=""){print t, a[1]; t=""}}' $o | sort -u > $n.res
  fi
  echo "$n: $(awk '{print $2}' $n.res | sed 's/(.*//' | sort | uniq -c | sort -rn | tr '\n' ' ')"
done
echo "== push diff (stock vs vk13p2dbg)"; diff <(sort push-stock.res) <(sort push-vk13p2dbg.res)
echo "== non-pass in vk13 runs (excluding NotSupported)"; grep -v " Pass\| NotSupported" *vk13*.res | head -20
