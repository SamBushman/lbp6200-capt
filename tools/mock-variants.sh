#!/bin/bash
# run Canon's real filter+monitor against the pty mock for each variant; saves rx files to evidence/mockwire-004
E=$HOME/lbp6200/evidence/mockwire-004; mkdir -p $E
export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
T=$HOME/lbp6200/evidence/capture-002/testpage.ps; P3=$HOME/lbp6200/scratch/sweep/three.ps
run() { name=$1; file=$2; shift 2
  $HOME/lbp6200/tools/mock-restart.sh mv_$name >/dev/null 2>&1
  lp -d LBP6200 -t $name "$@" $file >/dev/null 2>&1
  for i in $(seq 1 40); do sleep 1; grep -q ' e0a9 ' $HOME/lbp6200/scratch/mock/mv_$name.bin.txt 2>/dev/null && break; done
  sleep 2; cancel -a LBP6200 2>/dev/null
  cp $HOME/lbp6200/scratch/mock/mv_$name.bin $E/$name.bin
  echo "$name: $(python3 $HOME/lbp6200/tools/mockwire.py $E/$name.bin | grep -c .) frames"; }
run base $T
run duplexL $P3 -o Duplex=DuplexNoTumble
run duplexS $P3 -o Duplex=DuplexTumble -o BindEdge=Top
run letter $T -o media=Letter
run a5 $T -o media=A5
run thick $T -o MediaType=ThickPaper
run draft $T -o CNDraftMode=True
run td1 $T -o CNTonerDensity=1
run td16 $T -o CNTonerDensity=16
run hoff $T -o CNHalftone=Off
run smoothoff $T -o CNSuperSmooth=False
run three $P3
echo DONE
