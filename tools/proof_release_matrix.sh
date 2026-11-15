#!/bin/bash
# Offline proof matrix for the release-on-every-exit change (docs/12).  usage: proof_release_matrix.sh FILTER BACKEND   (the binaries that ship in the RPM)
F=$1; B=$2; T=$HOME/lbp6200/tools/transport_proof.py; E=$HOME/lbp6200/evidence/proof-fix-001
run(){ name=$1; shift; echo "=== $name"; env "$@" 2>&1 | grep -E "rc=|SIGTERM|states|last frames|VIOL|stderr|BACKEND"; }
P="timeout 100 python3 $T lbp $F $B"
run "S1 success, 1 page Letter"            $P $E/l2.ras --out $E
run "S2 success, 3 pages A4"               $P $E/a4x3.ras --out $E
run "F1 printer silent on E1A1 (after reserve)"  DROP=e1a1 $P $E/l1.ras --out $E
run "F2 printer silent on E0A5"            DROP=e0a5 $P $E/l1.ras --out $E
run "F3 E0A5 refused (0x8b)"               ERRST=e0a5:8b $P $E/l1.ras --out $E
run "F4 E0A7 refused (0x8b)"               ERRST=e0a7:8b $P $E/l1.ras --out $E
run "F5 reserve refused (0x87)"            ERRST=a2a0:87 $P $E/l1.ras --out $E
run "F6 raster input ends early"           $P $E/trunc.ras --out $E
run "F7 SIGTERM waiting for E0A7 reply"    DELAY=e0a7:6 $P $E/l1.ras --out $E --term-after 1.5
run "F8 SIGTERM during job start"          DELAY=e1a1:4 $P $E/l1.ras --out $E --term-after 1.5
run "F9 SIGTERM during page data (0.55 s)" DELAY=c0a0:1.0 $P $E/l2.ras --out $E --term-after 0.55
run "F9b SIGTERM during page data (1.0 s)" DELAY=c0a0:1.0 $P $E/l2.ras --out $E --term-after 1.0
echo MATRIX-DONE
