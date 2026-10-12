#!/bin/bash
# GPD side dEQP-VK runner with crash resume.
#   deqprun.sh NAME DRIVER CASELIST        DRIVER = stock | vk13 | vk13dbg | vk13p2 ...
# Results: ~/vk13-test/logs/NAME.out (deqp stdout), NAME.res (one "testname status" per line), NAME.sum
# dEQP runs the cases in its own tree order (not the list order), so after a crash the remaining list is
# "list minus cases that have a result".
cd ~/vk13-test || exit 1
name=$1; drv=$2; list=$3
case $drv in
  stock) env=() ;;
  *) env=(VK_DRIVER_FILES=/home/robin/vk13-test/icd/$drv.json) ;;
esac
mkdir -p logs
out=logs/$name.out; res=logs/$name.res; : > $out; : > $res
rem=logs/$name.remaining; cp "$list" $rem
crashes=0; t0=$(date +%s)
parse() {
  awk '/^Test case /{sub(/^Test case ./,""); sub(/.\.\.$/,""); t=$0; next}
       /^  (Pass|Fail|NotSupported|QualityWarning|CompatibilityWarning|InternalError|ResourceError|Crash|Timeout|Waiver)/{
          split($1,a," "); if(t!=""){print t, a[1]; t=""}}' $out > $res
}
cd deqp
while [ -s ../$rem ]; do
  env "${env[@]}" ./deqp-vk --deqp-caselist-file=../$rem --deqp-log-filename=/dev/null \
      --deqp-log-images=disable --deqp-log-shader-sources=disable --deqp-log-flush=disable \
      --deqp-surface-type=pbuffer >> ../$out 2>&1
  rc=$?
  cd ..
  parse
  last=$(grep "^Test case " $out | tail -1 | sed "s/^Test case '//; s/'\.\.$//")
  if [ $rc -eq 0 ] && grep -q "^DONE" $out && ! grep -q "ABORTED" $out; then cd deqp; break; fi
  if [ -z "$last" ]; then echo "no test started, rc=$rc" >> $out; cd deqp; break; fi
  if ! grep -q "^$last " $res; then echo "$last Crash(rc=$rc)" >> $res; crashes=$((crashes+1)); fi
  if [ $crashes -ge 40 ]; then echo "too many crashes, stopping" >> $out; cd deqp; break; fi
  # remaining = list minus cases with a result
  awk 'NR==FNR{done[$1]=1; next} !($0 in done)' $res "$list" > $rem.new && mv $rem.new $rem
  echo "=== resumed after abort/crash in $last (rc=$rc), $(wc -l < $rem) remaining" >> $out
  cd deqp
done
cd ..
parse
{
  echo "$name ($drv, $(wc -l < "$list") tests): $(awk '{print $2}' $res | sed 's/(.*//' | sort | uniq -c | sort -rn | tr '\n' ' ')"
  echo "crashes: $crashes, wall time: $(( $(date +%s) - t0 )) s"
  echo "hangs in dmesg: $(journalctl -k --no-pager | grep -ci 'gpu hang|Resetting rcs')"
} > logs/$name.sum
cat logs/$name.sum
