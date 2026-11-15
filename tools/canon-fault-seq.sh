#!/bin/bash
R=$HOME/lbp6200/tools/canon-fault-run.sh
$R f_e0a5_busy   "MOCK_FAULT=e0a5:status=8b" MAXWAIT=60
$R f_e0a5_silent "MOCK_FAULT=e0a5:silent" MAXWAIT=60
$R f_e0a7_silent "MOCK_FAULT=e0a7:silent" MAXWAIT=60
$R f_cancel      "MOCK_FAULT=e0a7:silent" CANCEL_AFTER=5 MAXWAIT=60
$R f_reserve_busy "MOCK_FAULT=a2a0:status=87:0:2" MAXWAIT=60
$R f_stale       MOCK_STALE=5 NOJOB=1 STOP_ON=e0a9 MAXWAIT=240
echo SEQ-DONE
