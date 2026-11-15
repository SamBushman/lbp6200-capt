# 09 - Job start logic and SetJobInfo (E1A1): what Canon's code proves

Source: decompiled `captmoncnabd` (~/lbp6200/ghidra/out/captmoncnabd.c), cross-checked against wire captures
(evidence/capture-003, capture-007).  Rule (user, Nov 14): every byte we send must be explained from code/spec;
a matching trace alone is not an explanation.

## A0A8 status word S0 (reply+4, low byte) - from C_Port::GetStatus (FUN_080c7d..., status.cpp)
| bit | evidence in code | meaning |
|-----|------------------|---------|
| 0x20 | `if (S0&0x20 && !(prev&0x20)) log "CONTROLLER RESET"` | printer controller was reset (power-up) |
| 0x10 | `if (S0&0x10 ...) log "detect OffLine"`; PrintContinuous tests it (below) | printer is OFFLINE (must be brought online with E0A5) |
| 0x02 | DoOnline waits for it to clear before E0A5 (<= 2 s, else error 0x15, no E0A5); LeaveSleep wakes (E0BA) when set | engine SLEEPING / power-save (clears ~160 ms after E0BA) |
| 0x01 | ReservePrinter waits for it to CLEAR after A2A0; ReleasePrinter waits (<=2 s) for it to SET after E0A9; idle test FUN_080b5854 needs it set | printer NOT reserved (free); clear = reserved by us |
| 0x40 | ReservePrinter waits for it clear (mask 0x41); no other use of this bit of Basic status (0x54dc) in the monitor except the Basic=0x%x log | not decoded (never set in any capture); we only wait for it as Canon does |
| 0x04 | NO test of bit 0x04 of Basic status (0x54dc) anywhere in captmoncnabd (the 0x54d0 hits at FUN_080a1008 are a different field) | unused by Canon; we ignore it too |

## Job start (PrintContinuous = FUN_080c1d00, then DoOnline = FUN_080c03c8)
```
if (S0 & 0x10) == 0:   # ONLINE
    nothing (page counters are snapshotted from the status)
else:                  # OFFLINE
    ClearMisprint (E0A3); ClearError (E0A2); DiscardData (E0A4);
    DoOnline: poll status <= 2 s until S0&0x02 == 0; if clear: GoOnline (E0A5, payload magic ee db ea ad + DWORD counter base)
              else error 0x15 (nothing sent)
```
So "init only on the first job after power-up" was a coincidence: the printer goes OFFLINE again after idle
(observed S0 0x8c13/0x8c17 five minutes after a job) and then needs the init again.  My earlier test of job 81 skipped the
init (old rule: S0 bit5) on a printer with S0=0x8c13 -> data accepted, nothing printed.  Fixed: need_init = S0&0x10.

## E0BA (SetPowerDown(1,0)) = C_Port::LeaveSleep (FUN_080a8114) - wake from sleep, RESOLVED
Only caller of SetPowerDown (FUN_080af300); ~20 engine commands call LeaveSleep as a prologue.  Sleeping = S0 bit1 set (and status
payload+12 bit15 set).  Capture-007 timeline: job 79 S0=0x8c31 (bit1 clear) -> NO E0BA; job 80 S0=0x8c13 (bit1 set, p+12=0x8000) -> E0BA(01 00),
then Canon polls A0A8 every ~12 ms until p+12 bit15 clears and S0 bit1 clears (~160 ms; code allows 15 s), then continues with E1A1.
(The old rule "E0BA together with the offline init" was wrong.)  Note capture-002 (first power-up job, S0 had bit1) had E0BA and no second A3AF;
capture-007 sends A3AF (GetTime, query only) after E1A2 - queries are not state-changing, replay test ignores them.

## ReservePrinter (FUN_080a7ddc)
A2A0 (payload 00 00 1e 00 ...; 0x1e = reserve type) -> reply job id; then poll status <= 2 s until S0&0x41 == 0;
if a flag (FUN_080a6930) says the printer clock is unset it also sends A3A9 SetTime (we never send A3A9 yet).

## A3A9 SetTime (C_Port::EngineSetTime FUN_080af404; condition FUN_080a6930) - RESOLVED
Called inside ReservePrinter after the 0x41 wait.  Condition: transport supports it AND (status payload+8 bit0 clear  OR  printer's A3AF reply
byte 15 (= JOBTIME byte 7, the daylight flag) != ours).  Capture-007: the very first poll had p+8=0 -> A3A9 sent at once (monitor start-up probe:
reserve, A3A9, release); every later poll p+8=1 and A3AF byte equal -> no A3A9 in jobs 78-80.  Packet: u16 cmd, u16 size=0xa2, JOBTIME(8) from
FUN_080bf90c, rest zero (memset 0xa2); FUN_080b2644 only byte-swaps it.  The non-zero bytes in the capture (c8 99, c4 ff ff ff, b4 62) are
uninitialised heap padding (FUN_080b2624 allocs without clearing).  Implemented in capt_job_begin.

## Other bytes of the job start
* E1A2 payload (20 bytes; FUN_080aeb64 packs the C_LEDSTATUS struct; UpdateLED FUN_080c65fc fills it): byte0 = four 2-bit LED modes
  d5 | d6<<2 | d7<<4 | d8<<6 (0 off, 1 on, 3 fast-blink as written by the monitor).  In a job with no error/cover/paper state UpdateLED sets
  d5=3, d6=0, d7=1, d8=0 -> 0x13 (the captured value); the remaining bytes are the cartridge/aux LED fields, all 0 in that state.  Error states
  would change them; we send only the normal-printing value, so 0x13 + zeros is exactly what Canon's code produces for a healthy job.
* E1A2 SetLEDStatus (FUN_080aeb64; packet 0x14 bytes, payload = LED struct bytes 4..13): UpdateLED (FUN_080c65fc, status.cpp) packs 2-bit LED-mode fields:
  p[0] = d5 | d6<<2 | d7<<4 | d8<<6, p[1] = f7d | f7e<<2 | f7f<<4 | f80<<6, p[2] = d9<<2, p[3] = state code (0x09/0x0f/...), p[4..] extra.
  Port-state bit5 clear (no error) and bit7 clear and (port+0x5526 & 8)==0  =>  d5=3, d6=0, d7=1, d8=0  =>  p[0] = 3 | 1<<4 = 0x13 (unless bit 8 /
  job flag 0x1f is set: d5=1,d8=3).  So 0x13 is the normal "ready, no error" LED pattern; all other bytes 0 there.  It only drives panel LEDs
  (mode values 0 off / 1 on / 3 blink); sending the constant 0x13 for a healthy printer is exactly what Canon computes.  Error patterns (other
  d/f values, p[3] codes) are not needed since we never set error LEDs ourselves.
* E0A5 GoOnline(DWORD): magic ee db ea ad then the page-counter base (DoOnline param, 0 for a fresh job); the monitor then resets its own counters to that base.
* E1A6 SetPrintedPages(DWORD) (FUN_080afc24): sent in PrintContinuous when the transport flag (vtable+0x34 & 0x10000) is set; value = job+0x2484 (pages already
  printed for a continued job) = 0 for a new job.
* E0A3 ClearMisprint, E0A2 ClearError, E0A4 DiscardData: no payload (FUN_080add4c/add98/add00).

## E1A1 SetJobInfo (C_Port::SetJobInfo @080ae278), packet = u16 cmd, u16 size, then
| payload off | field | notes |
|---|---|---|
| p[0..3] | 0 | |
| p[4..7] | param_2 DWORD "page" (0 if job flag 0x14 set, i.e. on the first E1A1) | job+0x2474 (= job+0x2470 at PrintJob entry); Canon logs "SetJobInfo %d page %d" (state, page). Captures: 0 on state 1; total/sent page count afterwards (3-page job: 3 on its first state-2). EXACT meaning not yet traced |
| p[8..9] | byte length of string 1 (<=0x1e) | host/computer name, UTF-16LE |
| p[10..11] | byte length of string 2 (<=0x28) | user name |
| p[12..13] | byte length of string 3 (<=0x100) | document title (NOT a duplex flag: 0x1a = 13 chars) |
| p[14..15] | 0 | |
| p[16] | state (BYTE param_3 = job+0x24c4) | 1 = job start, 2 = page(s) sent, 6 = finished |
| p[17] | param_8 = byte at winjob+0x18 | always 1 in captures |
| p[18..19] | param_7 = CAPT job id (job+0x24d8) | = A2A0 reply; HW-verified (wrong -> 0x90) |
| p[20..21] | param_4 = short job+0x24c8 | time-zone bias in minutes west of UTC = Bias+StandardBias, written ONLY by FUN_080bf90c (a GetTimeZoneInformation wrapper) |
| p[22..23] | param_5 = short job+0x24cc | = Bias+DaylightBias (same function). The captured values 0x304c / 0xc04c / 0x104c differ between runs on one machine => junk from Canon's Linux time-zone emulation, NOT a duplex flag. We send the real zone biases (std, std-60) |
| p[24..31] | JOBTIME (u16 year, mon, day, hour, min, sec, byte7) | byte7 = (TZ_ID==DAYLIGHT)+2 per FUN_080bf90c: 2 standard / 3 daylight; Canon's Linux build sent 3 even in standard time (emulation) - we keep 3 |
| p[72..]   | strings 1,2,3 concatenated UTF-16LE | packet size = 0x4c + len1 + len2 + len3 |
Our builder (capt_build_job_info) now implements this layout exactly; the replay test (Canon capture) still matches 16/16 frames.

## E0A7 StartPrint flow control (C_Send::SendStartPrint FUN_080a0c38, gate FUN_080a0bfc) - VERIFIED on capture-007 status timeline
Gate: E0A7(n) is sent only when (a) E0A7(n-1) has been sent (strict order) and (b) the printer's status counter
"pages decoded" (A0A8 payload +14; monitor keeps the max in port+0x9768) is >= n.  Page data is NOT held back: Canon sent page 3's
C0A0 while E0A7(2) still waited.  Evidence (capture-007, A0A8 polls every ~200 ms):
```
job 79 (2-page duplex):  C0A4,C0A4 -> D0A9(back) ; poll dec=1 -> E0A7(1) ; E1A1(s2) ; poll dec=2 prn=1 -> E0A7(2) ; E1A1(s6) ; E0A9
job 80 (3 pages):        ... D0A9(back) ; dec=1 -> E0A7(1) ; E1A1(s2) ; D0A9(front) C0A0 C0A4 (page 3) ; dec=2 -> E0A7(2) ;
                         dec=3 -> E0A7(3) ; E1A1(s6) ; E0A9
```
E1A1 states are JOB-level: state 2 after the first E0A7, state 6 after the last E0A7, then E0A9 (pages keep printing after release:
out/done counters rise afterwards).  Duplex page order on the wire: D0A9(front) C0A0.. C0A4 (front) C0A0.. C0A4 (back) D0A9(back) ; the D0A9 of the back
side is sent after its data (observed in both captures; reason not traced).
Core now implements exactly this (pump_start_print, capt_job_end).

## E1A1 p[4] = "TOTALPAGESENT"
job+0x2470 = total pages of the job (sum of sheet pages x copies); when the whole job has been handed to the monitor Canon logs
"Job: TOTALPAGESENT set" and p[4] = that total (job+0x2474).  Before that (first E1A1) p[4] = 0.  A streaming filter does not know the total:
state 2 carries total_pages if the caller knows it, else 0; state 6 carries the real total.  (Printer-side use of p[4] untested.)

## D0A9 placement (C_SendQueue::QueueSheet FUN_080a4*** emsend.cpp:0x369, C_Send::QueuePage FUN_080a0e84, dispatcher FUN_080a3654) - RESOLVED
Canon queues SHEETS, not pages.  A duplex sheet (m_apReverse != null) becomes a chain of send objects  A, img(page1), img(page2), B  :
A = QueuePage(type 4): D0A0 payload[5] |= 4 (&= ~2) taken from the sheet's reverse-page header; B = QueuePage(type 3): payload[5] |= 2 (&= ~4)
from the front-page header.  The dispatcher walks the chain in order: A's non-image block (SendDataQueue FUN_080a1008 -> D0A9), the image nodes'
C0A0 data (SendImage FUN_080a12d8) with C0A4 per page, then B's D0A9.  That is the observed order D0A9(4) data1 C0A4 data2 C0A4 D0A9(2).
A sheet with no reverse page (odd last page, or simplex) is QueuePage type 2: payload[5] bits 1 and 2 both cleared -> D0A9 payload[5]=0, sent
BEFORE its data (job 80 page 3: `0d 00`, all other bytes identical to the duplex D0A9).  Core now holds the duplex front page (D0A9+data) until
the back page begins (-> [5]=4 pair) or the job ends (-> patch [5]=0 lone), exactly like Canon's sheet pairing.  Pipeline tests reproduce
job 79/80 command order byte-for-byte in the D0A9 fields.

## Still unexplained / unproven (flag before any live test)
1. (RESOLVED) p[22..23] = time-zone daylight bias, not a flag. E1A1 has no duplex-specific field.
2. (RESOLVED in code, printer-side effect untested) p[4] = total pages / 0 while unknown.
3. (RESOLVED) E0A7 deferral = decode-counter gate; D0A9 placement resolved (section below).
4. (RESOLVED) E0BA = wake iff S0 bit1; A3A9 SetTime condition resolved and implemented.
5. (RESOLVED to the extent Canon uses them) S0 bits 0x01/0x04/0x40 - see table.  Paper-out / toner / error bit mapping is status REPORTING only (no bytes sent depend on it); the monitor's CheckStatusSub (FUN_080c*) is not yet transcribed into capt_status_summary.
