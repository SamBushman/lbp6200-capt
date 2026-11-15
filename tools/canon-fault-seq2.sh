#!/bin/bash
R=$HOME/lbp6200/tools/canon-fault-run.sh
$R g_e0a5_busy   "MOCK_FAULT=e0a5:status=8b" MAXWAIT=90
$R g_e0a7_silent "MOCK_FAULT=e0a7:silent" MAXWAIT=330
$R g_cancel      "MOCK_FAULT=e0a7:silent" CANCEL_AFTER=5 MAXWAIT=330
$R g_stale_job   MOCK_STALE=5 MAXWAIT=120
echo SEQ2-DONE
