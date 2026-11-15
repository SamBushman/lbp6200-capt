# 12 - How Canon's monitor releases the printer and recovers a stale job (captmoncnabd, static trace Nov 14)

Question: what does Canon do when something fails AFTER the printer was reserved (A2A0 OK) - and when a previous job left the printer reserved?
Source: ghidra/out/captmoncnabd.c (function names from docs/04-engine-method-map.txt / assert strings).  Nothing here was run on hardware.

## Reservation state (C_PrinterReserve @ port+0x58a8)
* C_PrinterReserve::ReservePrinter FUN_080a7c54: A2A0 (EngineReservePrinter FUN_080ad668, type 0x1e).  On success: `state[0]=1`, `state[2]=job`, `state[3]=CAPT job id`
  (copied to job+0x24d8).  On failure: NOTHING is reserved, local state cleared (FUN_080a67f8) and NO release is sent; for reply errors 0x2000008d / 0x200000cd the
  job gets flag 0x200 and a 5000 ms retry timer (job+0x249c/0x24a0).  (0x87 ALREADY_RESERVED is not special-cased here.)
* FUN_080a67f8 clears state[0], state[2], state[3]; it is called by every release path whether or not the E0A9 write worked.

## Release is unconditional once reserved (PrintJob2 FUN_080c4718, lines 78410-78437)
```
reserve = C_Port::ReservePrinter(job, jobid)      # FUN_080a7ddc
if reserve == 0:
    PrintJob(job)                                  # FUN_080c3730: the whole job, errors included
    if port.reserve_state[0] != 0:                 # still reserved, whatever happened in PrintJob
        C_Port::ReleasePrinter()                   # FUN_080a69f4
else:
    (no release; error handling only)
```
There is no "release only on success".  Every error inside PrintJob unwinds to this point and the release is sent.

## C_Port::ReleasePrinter FUN_080a69f4
C_PrinterReserve::ReleasePrinter FUN_080a68ac -> EngineReleasePrinter FUN_080ad758 (E0A9, payload = CAPT job id) -> FUN_080a67f8 (clear state, even if the write
failed) -> poll status (CheckStatus FUN_080cd2cc) every 100 ms for <= 2000 ms until S0 bit0 is SET (printer free); a CheckStatus error returns early.

## Job end inside PrintJob (FUN_080c3730, lines 78100-78190)
* Final E1A1 (FUN_080830b0, state in job+0x24c4) is sent only when `FUN_080c5278` is true = no fatal port flags AND connection open (port+0x5898) AND we still hold the
  reservation (port+0x58a8 != 0) AND reset-request bit (port+0x72e0 & 8) clear.  So on a dead link Canon sends no E1A1 but still tries E0A9.
* State chosen from job flags: 7 if the fatal mask (0x10680) is set; else 6 when job flag 0x11 is set (the normal ending in all our captures); else 3 (flag 0x18),
  4 (flag 0x10 or 0x17) or 5 (none of them).  Meaning of 3/4/5/7 is NOT traced (never seen in captures, docs/09 only has 1, 2, 6).
* When the state is not 6 and the port is still healthy: FUN_080c08c8 = ClearErrorMisprint (E0A2 ClearError when status 0x54e5&0x4e, E0A3 ClearMisprint when
  0x54e8&0x80) before the release.

## Stale job recovery (PrintJob2 scheduler lines 78486-78499, FUN_080c0008 lines 75661-75720)
Trigger: timer (port+0x71cc interval) expired AND status shows the printer FREE (S0 bit0 set) AND the status job number (A0A8 payload+28 = port+0x54f8) != 0,
i.e. a job id is left in the printer although nobody holds the reservation.  Procedure (FUN_080c0008):
1. C_Port::ReservePrinter(job=0, WORD = stale job id)      -> A2A0 with the stale id as requested id (the WORD param)
2. SetJobInfo(page 0, state 5, tz 0/0, JOBTIME zero, job id = stale id, byte 1, empty host/user/title) = E1A1 state 5 (FUN_080ae278 args: 0,5,0,0,&zero,id,1,"","","")
3. C_Port::ReleasePrinter()
If the printer still shows reserved (S0 bit0 clear) Canon does NOT force anything; it just re-arms the timer.

## What our driver does differently (src/core/capt.c, src/cups/rastertocapt6200.c)
| situation | Canon | ours |
|---|---|---|
| reserve OK, later job-start step fails (E1A2/E1A1/E0A5/wait) | job aborts, E1A1 final state (if link ok), E0A9 | return error, NO E0A9 (fault injection DROP=e1a1/e0a5, evidence/proof-fix-001) |
| failure after pages sent | E1A1 state 3/4/5/7 (+ClearError/Misprint), E0A9 | capt_job_end sends E1A1 state **6** (if started_any) + E0A9 |
| reserve fails | no release, retry later | same (retry x3, no release) |
| stale id left in a free printer | reserve(id)+E1A1 state 5+release | nothing |
| job cancelled (SIGTERM) | monitor is a daemon, unwinds normally, releases | filter and backend both die, no release |

## Results of running Canon's real monitor against the fault mock (Nov 14, evidence/canon-faults/, tools/canon-fault-run.sh + mockprinter_fault.py)
* reserve answered 0x87 twice: Canon re-sends A2A0 after 0.4 s then 1.0 s, NO E0A9 in between, third attempt OK -> job normal (f_reserve_busy).
* E0A5 answered BUSY(0x8b): Canon repeats the whole group E0A3,E0A2,E0A4,E0A5 every 0.5 s (179x in 90 s), holds the reservation, never aborts (g_e0a5_busy).
* E0A7 answered BUSY: Canon re-sends E0A7 every ~0.2 s (h_e0a7_busy).  => printer-reported transient errors are retried, not abandoned.
* jam reported after E0A7 accepted: data was already delivered, so Canon sends E1A1 state 6 then E0A9 (h_jam2).  => state 6 = "all data delivered", not "printed".
* printer silent: nothing further is sent for >= 60 s (f_e0a7_silent, f_e0a5_silent, f_cancel).  Static reason: libcaiousb opens the device O_RDWR (blocking) and
  caiolibRead is a single read(); caiolibControl returns -1 for every command, so the 0x40a transport-type query fails, port type = 0 (not 6), and the
  UsbReadTimeOut branch (10 s / 30 s with flag 0x20) of ReadFromPortSub (FUN_080acdb8) is never used.  Canon blocks forever on a silent printer (EINTR -> -2).
* stale-id cleanup (FUN_080c0008) is a PageTimeOut (default 600 s) watchdog that only runs while one of Canon's jobs waits; it is NOT a job-start recovery.
## Job-end state meaning (job flags at job+0x2448 = std::bitset<32>; FUN_0808bf04 test, FUN_0808c694 set, FUN_0808c704 reset)
* 6 = bit 0x11 = "PrintContinuous: exits TRUE" (line 77251): all job data delivered to the printer.
* 4 = bit 0x10 (set by CancelPrint FUN_08084224 line 37499) or 0x17: job cancelled.   3 = bit 0x18 (pages printed == pages sent).
* 5 = none of those: job did not complete because of an error (same state Canon's own stale cleanup sends).   7 = fatal/communication mask.
## Decision for our driver (derived from the above; every packet is one Canon itself sends in the same situation)
1. After a successful A2A0 reserve, EVERY exit path releases: if the link is healthy and our start E1A1 was sent -> final E1A1 (state 5 error / 4 cancel;
   6 only if all data was delivered), then E0A9.  Link not healthy (timeout/EIO/desync) -> no E1A1 (Canon: FUN_080c5278 false), E0A9 attempted once.
2. Failure before all data delivered must NOT send state 6 (Canon never does).
3. Cancel: CUPS sends SIGTERM to filter+backend and SIGKILLs after JobKillDelay (30 s, scheduler/job.c stop_job): filter handles SIGTERM (unwinds, state 4,
   release), backend ignores SIGTERM and exits when its stdin closes.
4. Not copied: Canon's retry-forever on printer-reported BUSY and its blocking read: our reads keep bounded timeouts (design decision, not Canon behaviour).
5. Not implemented: the 600 s stale-id watchdog (not a start-of-job recovery).
