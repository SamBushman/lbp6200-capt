#!/bin/bash
# (re)start the pty mock printer + Canon ccpd against it. usage: mock-restart.sh <rxname>
M=$HOME/lbp6200/scratch/mock; cd $M
for p in $(pgrep -x ccpd) $(pgrep -x captmoncnabd); do kill $p 2>/dev/null; done
for p in $(pgrep -f 'python3 .*mockprinter_pty'); do [ $p != $$ ] && kill $p 2>/dev/null; done
sleep 1
export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
nohup python3 $HOME/lbp6200/tools/mockprinter_pty.py $M/lp0 $M/${1:-rx}.bin > mock.out 2>&1 &
sleep 1.5
LD_PRELOAD=$HOME/lbp6200/scratch/fakeroot/fakeeuid.so nohup /usr/sbin/ccpd > ccpd.out 2>&1 &
sleep 8
awk '{print $2,$3}' $M/${1:-rx}.bin.txt | uniq -c | head -${2:-20}
