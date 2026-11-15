#!/bin/sh
# Runs on the G5 (Tiger), userspace only: runs every golden scenario's LBPC stream through the plug-in bundle + mock interface and writes the wire dumps.
# usage: run_g5.sh BUNDLE_DIR   (cwd = the step-2 tree)
B="$1"; mkdir -p out/wire; fail=0
for s in test/tiger/out/streams/*.lbpc; do
	n=`basename $s .lbpc`
	nice -n 15 ./out/harness "$B" $s out/wire/$n.wire > out/wire/$n.log 2>&1 || { echo "FAIL $n"; tail -3 out/wire/$n.log; fail=1; }
done
echo "ran `ls out/wire/*.wire | wc -l` scenarios, fail=$fail"
