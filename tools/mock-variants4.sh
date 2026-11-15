#!/bin/bash
# deterministic: oracle filter by hand -> ext stream -> ccp backend by hand -> ccpd -> Canon monitor -> pty mock
E=$HOME/lbp6200/evidence/mockwire-008; mkdir -p $E
M=$HOME/lbp6200/scratch/mock; export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
export PPD=/usr/share/cups/model/CNCUPSLBP6200CAPTK.ppd PATH=/usr/lib/cups/filter:$PATH
P1=$HOME/lbp6200/evidence/capture-002/testpage.ps; P2=$HOME/lbp6200/scratch/sweep/two.ps; P3=$HOME/lbp6200/scratch/sweep/three.ps
for spec in "$@"; do
  IFS='|' read -r name file opts <<< "$spec"
  for q in pstocapt3 c3pldrv ccp gs captfilter; do for p in $(pgrep -x $q); do kill $p 2>/dev/null; done; done
  $HOME/lbp6200/tools/mock-restart.sh w_$name >/dev/null 2>&1
  /usr/lib/cups/filter/pstocapt3 99 sam "$name" 1 "$opts" ${!file} > $E/$name.ext 2>/dev/null
  ( export DEVICE_URI=ccp://localhost:59687 PRINTER=LBP6200; exec -a ccp /usr/lib/cups/backend/ccp 99 sam "$name" 1 "$opts" $E/$name.ext > $E/$name.backend.out 2>&1 )
  for i in $(seq 1 150); do sleep 1; grep -q ' e0a9 ' $M/w_$name.bin.txt && break; done
  sleep 1; cp $M/w_$name.bin $E/$name.bin
  echo "$name: ext=$(stat -c%s $E/$name.ext) rx=$(stat -c%s $E/$name.bin) releases=$(grep -c ' e0a9 ' $M/w_$name.bin.txt)"
done
echo DONE
