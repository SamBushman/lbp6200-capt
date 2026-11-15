#!/bin/bash
# usage: sweep.sh name "options" [ps]   -> out/<name>.capt
export PPD=/usr/share/cups/model/CNCUPSLBP6200CAPTK.ppd CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock PATH=/usr/lib/cups/filter:$PATH
mkdir -p out
ps=${3:-$HOME/lbp6200/evidence/capture-002/testpage.ps}
/usr/lib/cups/filter/pstocapt3 99 sam test 1 "$2" "$ps" > out/$1.capt 2> out/$1.err
echo "$1 rc=$? $(stat -c%s out/$1.capt)"
