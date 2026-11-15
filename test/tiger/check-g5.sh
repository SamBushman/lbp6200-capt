#!/bin/sh
# Step-2 check, driven from the host: copies the sources to a PRIVATE directory on a Tiger PPC Mac (default ssh host G5, dir ~/lbp6200-step2), builds the plug-in
# bundle and the mock-interface harness there under `nice` (userspace only: no kext, no printer, nothing installed outside that directory), runs every golden
# scenario's LBPC stream through the real bundle via CFPlugIn, and compares each frame sent to the mock printer with the Linux golden capture.
# usage: test/tiger/check-g5.sh [ssh-host]       needs: tools/baseline built (make -C test/lbpc check does that) and tiger/rastertolbpc
set -e
H=${1:-G5}; D='~/lbp6200-step2'; cd "$(dirname "$0")/../.."
tools/baseline/run_baseline.py streams --filter-bin "$PWD/tiger/rastertolbpc" --out test/tiger/out/streams
tar cf - --exclude='*.o' core hiscoa lbpc tiger/plugin test/lbpc/mockprinter.h test/tiger/harness.c test/tiger/run_g5.sh test/tiger/out/streams | ssh $H "mkdir -p $D && cd $D && tar xf -"
ssh $H "cd $D && mkdir -p out && (cd tiger/plugin && nice -n 15 make) && nice -n 15 gcc -arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -mmacosx-version-min=10.4 -std=gnu99 -O1 -w -o out/harness test/tiger/harness.c core/capt.c hiscoa/hiscoa-compress.c hiscoa/hiscoa-common.c -framework CoreFoundation -framework IOKit -lpthread && sh test/tiger/run_g5.sh \"\$PWD/tiger/plugin/build/LBP6200 USB ClassDriver.plugin\""
rm -rf test/tiger/out/wire; mkdir -p test/tiger/out/wire
ssh $H "cd $D/out/wire && tar cf - ." | tar xf - -C test/tiger/out/wire
bad=0
for f in test/tiger/out/wire/*.wire; do n=$(basename "$f" .wire); test/tiger/compare_wire.py "$f" test/golden/$n.json > /dev/null || { echo "DIFFER $n"; bad=1; }; done
[ $bad = 0 ] && echo "RESULT: IDENTICAL (plug-in on Tiger vs Linux golden, $(ls test/tiger/out/wire/*.wire | wc -l) scenarios)" || { echo "RESULT: DIFFERENCES"; exit 1; }
