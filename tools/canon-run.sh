#!/bin/bash
# Run Canon's ccpd under strace against the real printer (wire capture). usage: canon-run.sh <evidence-dir>
E=$1
export CUPS_SERVER=$HOME/lbp6200/cups/run/cups.sock
strace -E LD_PRELOAD=$HOME/lbp6200/scratch/fakeroot/fakeeuid.so -f -ff -tt -x -s 8192 -e trace=read,write,execve,openat,ioctl,pipe,pipe2,socketpair,connect,dup2,dup3 -o $E/ccpd  /usr/sbin/ccpd > $E/ccpd.stdout 2>&1
