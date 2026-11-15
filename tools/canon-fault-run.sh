#!/bin/bash
# Run Canon's REAL ccpd/captmoncnabd against the fault-injection mock (never the real printer) and save the wire log.
# usage: canon-fault-run.sh NAME [env VAR=val ...]  env: MOCK_FAULT MOCK_STALE MOCK_C0A0_DELAY CANCEL_AFTER(s) MAXWAIT(s) STOP_ON(hex cmd to stop after seeing, default e0a9) FILE(ps)
NAME=$1; shift
for kv in "$@"; do export "$kv"; done
M=$HOME/lbp6200/scratch/mock; E=$HOME/lbp6200/evidence/canon-faults; mkdir -p $E $M
export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
FILE=${FILE:-$HOME/lbp6200/evidence/capture-002/testpage.ps}
cleanup(){ for p in ccpd captmoncnabd; do pkill -x $p 2>/dev/null; done; [ -f $M/mock.pid ] && kill $(cat $M/mock.pid) 2>/dev/null; sudo cp $M/ccpd.conf.orig /etc/ccpd.conf; }
trap cleanup EXIT
[ -f $M/ccpd.conf.orig ] || sudo cp /etc/ccpd.conf $M/ccpd.conf.orig
sudo sed "s,^\(\s*DevicePath\s\+\).*,\1$M/lp0," $M/ccpd.conf.orig | sudo tee /etc/ccpd.conf >/dev/null
grep DevicePath /etc/ccpd.conf
pgrep -f "cups/etc/cupsd.conf" >/dev/null || { nohup /usr/sbin/cupsd -c $HOME/lbp6200/cups/etc/cupsd.conf > $M/cupsd.out 2>&1 & sleep 3; }
cancel -a LBP6200 2>/dev/null; cupsenable LBP6200 2>/dev/null
for p in ccpd captmoncnabd pstocapt3 c3pldrv ccp captfilter; do pkill -x $p 2>/dev/null; done
[ -f $M/mock.pid ] && kill $(cat $M/mock.pid) 2>/dev/null; sleep 1
python3 $HOME/lbp6200/tools/mockprinter_fault.py $M/lp0 $M/$NAME.bin > $M/mock.out 2>&1 &
echo $! > $M/mock.pid; sleep 1.5
LD_PRELOAD=$HOME/lbp6200/scratch/fakeroot/fakeeuid.so nohup /usr/sbin/ccpd > $M/ccpd.out 2>&1 &
sleep 8
# SAFETY: no Canon process may have the real printer open
if ls -l /proc/*/fd 2>/dev/null | grep -q "/dev/usb/lp"; then echo "ABORT: something holds /dev/usb/lp*"; ls -l /proc/*/fd 2>/dev/null | grep "/dev/usb/lp"; exit 9; fi
echo "safety ok: mock only"; for p in $(pgrep -x captmoncnabd); do ls -l /proc/$p/fd 2>/dev/null | grep -E "lp0|pts" | head -2; done
[ -n "$NOJOB" ] || lp -d LBP6200 -t "$NAME" $FILE >/dev/null 2>&1
T0=$(date +%s); CANCELLED=0
while :; do
  sleep 1; N=$(( $(date +%s) - T0 ))
  if [ -n "$CANCEL_AFTER" ] && [ $CANCELLED = 0 ] && [ $N -ge $CANCEL_AFTER ]; then cancel -a LBP6200; CANCELLED=1; echo "cancelled at ${N}s"; fi
  grep -q " ${STOP_ON:-e0a9} " $M/$NAME.bin.txt 2>/dev/null && [ -z "$CANCEL_AFTER" -o $CANCELLED = 1 ] && { sleep 4; break; }
  [ $N -ge ${MAXWAIT:-120} ] && { echo "maxwait"; break; }
done
cp $M/$NAME.bin $M/$NAME.bin.txt $E/ 2>/dev/null
echo "== wire summary ($NAME)"; awk '{print $2,$3}' $E/$NAME.bin.txt | grep -v "^a0a8\|^a0a4\|^a3af\|^FAULT\|^c0a0" | uniq -c | head -60
