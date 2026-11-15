#!/bin/bash
# one persistent mock + ccpd; per variant: mark log offset, submit, wait for e0a9, cancel, save the packet slice.
E=$HOME/lbp6200/evidence/mockwire-005; mkdir -p $E
M=$HOME/lbp6200/scratch/mock; export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
T=$HOME/lbp6200/evidence/capture-002/testpage.ps; P3=$HOME/lbp6200/scratch/sweep/three.ps; P2=$HOME/lbp6200/scratch/sweep/two.ps
$HOME/lbp6200/tools/mock-restart.sh all >/dev/null 2>&1
run() { name=$1; file=$2; shift 2
  off=$(stat -c%s $M/all.bin)
  lp -d LBP6200 -t "$name" "$@" $file >/dev/null 2>&1
  for i in $(seq 1 90); do sleep 1; tail -c +$((off+1)) $M/all.bin > $E/$name.bin; python3 - $E/$name.bin <<'PY' && break
import struct,sys
b=open(sys.argv[1],'rb').read();o=0;seen=False
while o+4<=len(b):
    c,s=struct.unpack_from('<HH',b,o);o+=s
    if c==0xe0a9: seen=True
sys.exit(0 if seen else 1)
PY
  done
  cancel -a LBP6200 2>/dev/null; sleep 6
  echo "$name: $(stat -c%s $E/$name.bin) bytes"; }
run base $T
run duplexL $P2 -o Duplex=DuplexNoTumble
run duplexS $P2 -o Duplex=DuplexTumble -o BindEdge=Top
run three $P3
run letter $T -o media=Letter
run a5 $T -o media=A5
run thick $T -o MediaType=ThickPaper
run draft $T -o CNDraftMode=True
run td1 $T -o CNTonerDensity=1
run td16 $T -o CNTonerDensity=16
run hoff $T -o CNHalftone=Off
run smoothoff $T -o CNSuperSmooth=False
echo DONE
