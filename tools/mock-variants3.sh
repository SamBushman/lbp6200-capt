#!/bin/bash
E=$HOME/lbp6200/evidence/mockwire-010; mkdir -p $E
M=$HOME/lbp6200/scratch/mock; export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
P1=$HOME/lbp6200/evidence/capture-002/testpage.ps; P2=$HOME/lbp6200/scratch/sweep/two.ps; P3=$HOME/lbp6200/scratch/sweep/three.ps
run() { name=$1; file=$2; shift 2
  cupsenable LBP6200 2>/dev/null; cancel -a LBP6200 2>/dev/null; for q in pstocapt3 c3pldrv ccp gs captfilter; do for p in $(pgrep -x $q); do kill $p 2>/dev/null; done; done; sleep 3
  $HOME/lbp6200/tools/mock-restart.sh v_$name >/dev/null 2>&1
  lp -d LBP6200 -t "$name" "$@" $file >/dev/null 2>&1
  for i in $(seq 1 170); do sleep 1; grep -q " e0a9 " $M/v_$name.bin.txt && break; done
  sleep 1; cancel -a LBP6200 2>/dev/null; for q in pstocapt3 c3pldrv ccp gs captfilter; do for p in $(pgrep -x $q); do kill $p 2>/dev/null; done; done
  cp $M/v_$name.bin $E/$name.bin
  python3 - $name $E/$name.bin <<'PY'
import sys,struct
n,f=sys.argv[1:];b=open(f,'rb').read();o=0;ok=False;t=n.encode('utf-16le')[:8]
while o+4<=len(b):
    c,s=struct.unpack_from('<HH',b,o);p=b[o:o+s];o+=s
    if c==0xe1a1 and n[:4].encode('utf-16le') in p: ok=True
print(n,'job-title-verified' if ok else 'TITLE NOT FOUND (contaminated or no job)')
PY
}
for spec in "$@"; do
  IFS='|' read -r name file opts <<< "$spec"
  run $name ${!file} $opts
done
echo DONE
