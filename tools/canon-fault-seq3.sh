#!/bin/bash
R=$HOME/lbp6200/tools/canon-fault-run.sh
$R h_e0a7_busy "MOCK_FAULT=e0a7:status=8b" MAXWAIT=60
$R h_jam       MOCK_JAM_ON_E0A7=1 MAXWAIT=75
echo SEQ3-DONE
