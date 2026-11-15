# 07 - Status decoding (A0A8 / A0A4 / A0A1): what Canon's code proves

Source: decompiled `captmoncnabd` (`~/lbp6200/ghidra/out/captmoncnabd.c`, Canon's x86 monitor for the LBP6200), message tables
`ref/canon-capt/cndrvcups-capt-2.71/libs/captemon/msgtablecnabd.xml`, checked against 1911 real A0A8 replies
(`evidence/capture-003/-006/-007`, `evidence/capture-002/a0a8-distinct.txt`, `evidence/probe-001-status.txt`).
Extraction script: `scratch/a0a8_scan.py` (output `scratch/a0a8_all.txt`); UI-code table dump: `scratch/status_table.txt`.
Rule: every claim names its code location.  **PROVEN** = the whole chain status byte -> error bit -> message id -> UI code/key was read in code.
**OBSERVED** = also seen in real data.  **UNPROVEN** = anything else.  No capture contains a paper-out, jam, cover-open, service-call or
toner state, so none of those conditions has been OBSERVED on the wire (the printer was never put into them).

## 1. How Canon turns the status into messages (the chain)
1. `C_Port::GetStatus` (FUN_080c7dbc) sends A0A8 and copies the 0x5c-byte reply to `port+0x54d8`.  **payload offset = port offset - 0x54dc**
   (reply offset - 4).  S0 = port+0x54dc, S4 = 0x54e4, S5 = 0x54e6, S6 = 0x54e8, job id = 0x54f8.
2. `C_Port::CheckStatusSub` (FUN_080c9448, lines ~80900-81520 of the decompile) runs `SetBit` (FUN_0806809c = `bitset<53>::set`, aborts for index > 0x34)
   on the **error bitset at port+0x7290** for the status bits in section 2; `CheckCartridgeStatus` (FUN_080c79a0 + per-entry FUN_080c5508) adds the cartridge bits;
   `CheckPaperStatus` (FUN_080c6040) fills the tray masks 0x7228/0x722c/0x7230 and event word 0x72e8; the query handlers GetJamStatus (FUN_080b08bc),
   GetServiceCallStatus (FUN_080b090c), GetMisprintStatus (FUN_080b086c), GetWaitingStatus (FUN_080b0a9c), GetIllegalConnectionStatus (FUN_080b0a4c) fetch details.
3. The decision tree `FUN_080bbccc` (lines 72147-72810) reads the bitset in priority order and writes **{class, message id}** (event dwords 0/1).
4. `ConvEmonStatusToID` (FUN_0804c61c, lines 2330-3288) turns {class, id} into a **UI code** (e.g. 0x2e); `FindStatusEntry` (FUN_0804c034) looks the
   code up in the 16-byte-entry table at 0x080f68e0 (dumped in `scratch/status_table.txt`): code -> state name + msgtable key.
5. LBP6200's own `msgtablecnabd.xml` only contains these keys: s_warn_toner_mem, s_printing, s_sleep, s_canceled, s_cleaning, s_ready,
   s_servernotready, s_invalid_port, s_check_status, s_adapter, s_cant_comm, s_incompatible, s_disk_full(2), s_jam2(+_info/_top/_rear/_top_sub/_rear_sub),
   s_xfer_error(2), s_change_size(_sub), s_change_paper(_sub), s_no_paper2_5, s_no_memory, s_waiting(_2), s_wt_init, s_wt_paper, s_ip_block,
   s_missprint(2,3), s_no_cartridge, s_door_open, s_firm_update, s_service_call, s_size_*.  There are **no toner-level keys** (no s_warn_toner1,
   s_tonerout*): the LBP6200 monitor never shows a toner-low/empty message; the only toner-related texts are "no toner cartridge" and
   "possible connection problem with the toner cartridge".

## 2. A0A8 payload bytes (offsets into the 88-byte payload; reply = 4-byte header + payload)
Layout of the words is the CAPT v2 layout of captdriver SPECS 1.3 (`ref/agalakhov/SPECS`): STATUS0 +0, STATUS1 +8, STATUS2 +10, STATUS3 +12, pages
decoding/printing/out/completed +14..+20, STATUS4 +24, job# +28, STATUS5 +30, #E0A5 +33, pages received +34, STATUS6 +38, pages in job +40,
pages printed +44, event counter +64 (this is what `capt_get_status` fills; `st[1]`=+8, `st[2]`=+10, `st[3]`=+12).  Canon's S1..S3 words (+2/+4/+6) are copied
but **never used by any decision** (port+0x54de is only compared for change detection, line 80557; +4 = 0x000f and +6 = 0 in every sample).

| payload byte.bit | port address | Canon use (code) | meaning | status |
|---|---|---|---|---|
| +0 bit0 (0x01) | 54dc | ReservePrinter polls until `(S0&0x41)==0` (FUN_080a7ddc ~59584); 78489: `(S0&1)==0 \|\| jobid==0` | printer free / no reservation held | PROVEN (docs/09) |
| +0 bit1 (0x02) | 54dc | LeaveSleep FUN_080a8114; DoOnline waits for clear | engine sleeping (power save) | PROVEN, OBSERVED (8e33 asleep) |
| +0 bit4 (0x10) | 54dc | "detect OffLine"; PrintContinuous | printer offline (needs E0A5) | PROVEN (docs/09) |
| +0 bit5 (0x20) | 54dc | "CONTROLLER RESET" edge (line 80527) | controller reset / power-up | PROVEN (docs/09) |
| +0 bit2 (0x04) | **54d0** (saved S0 dword copy, NOT 54dc) | `C_Send::SendDataQueue` FUN_080a1008: test-and-set data-block interlock, see section 8 | printer busy receiving/decoding page data; Canon sends the next D0A9 block only when it is clear | PROVEN used (Linux+Windows); meaning OBSERVED (8c04 while page data decodes) |
| +0 bit6 (0x40) | 54dc | only inside the 0x41 mask of ReservePrinter (FUN_080a7ddc) | unknown condition; must clear after reserve | PROVEN only that use; never set in 1911 samples |
| +0 bit3 (0x08), bit7 (0x80) | 54dc | never read (Linux, Windows) | - | never read; never set in samples |
| +1 (S0 high byte 0x8c/0x8e/0x0c) | 54dd | never read (0 references) | 0x8e = 0x8c + bit 9, seen only in the first reply after daemon start (5 of 1911) | UNPROVEN, no decision depends on it |
| +8 low byte | 54e4 | bit4 (0x10) + model flag -> msg 0x385 = **sleep** (s_sleep, code 0x57, lines 72564-72610); bit2 (0x04) clears the misprint byte (81337) | 0x01 idle, 0x81 receiving, 0x85 printing | 0x01/0x81/0x85 OBSERVED; bit4 meaning PROVEN by code only |
| +9 bit7 | 54e5 | `<0` -> SetBit 7 (CheckStatusSub 81104-81108) | **service call** (see below) | PROVEN chain, never observed |
| +9 bit0 | 54e5 | SetBit 0x14 (0x100000) -> msg 0x136 -> UI code 0x107 | code 0x107 is not in the status table, no text | UNPROVEN meaning |
| +9 bits1-3 | 54e5 | `&0xe` -> job flag bit 0x26 (81379-81390) | job-level event, meaning not traced | UNPROVEN |
| +10 low byte (S5 low, Canon log field "Paper") | 54e6 | bit `(0x80 >> slot)` = paper present in input slot (FUN_080c156c 76655: set -> change-size/paper checks, **clear -> JE_ADDPAPER**; `54e6==0` -> idle msg 0x387 = code 0x3d "no paper") | **paper present in slot 0** (LBP6200 has one slot) | PROVEN chain; OBSERVED 0x80 in all 1911 samples (paper was always loaded); the clear state was never captured |
| +11 bit7 | 54e7 | SetBit 5 (0x20) -> msg 0x97 -> code 0x95 (not in table) | - | UNPROVEN meaning |
| +11 bit1 | 54e7 | SetBit 0xd (0x2000) -> msg 0x135 -> code 0x107 | - | UNPROVEN meaning |
| +11 bit3 (0x08) | 54e7 | SetBit 0xf (0x8000) -> msg 0x2be, class 1 | "waiting" (s_waiting) | PROVEN chain |
| +12 bit1 (0x02) | 54e8 | SetBit 7 (0x80) -> msg 0xc9 (class 8) -> UI code 0x8004 = **s_service_call** | **service call** | PROVEN chain, never observed |
| +12 bit2 (0x04) | 54e8 | SetBit 0xe (0x4000) -> class 0xc ids 0x321-0x323 -> codes 0x29/0x26/0x2b = s_printing | engine printing | PROVEN chain |
| +12 bit3 (0x08) | 54e8 | SetBit 0x11 (0x20000) + GetIllegalConnectionStatus -> ids 0x12e/0x130/0x131/0x134/0x137 (codes 0x54/0x55/0x100 no-fuser/0x101 no-itb) | illegal/missing unit connection | UNPROVEN which unit exists on the LBP6200 |
| +12 bit7 (0x80) | 54e8 | triggers GetMisprintStatus (81327-81335) -> `wMisprint` word -> job bits 0x1b/0x23 | misprint report available | OBSERVED (S6 0x0080 in capture-006); message mapping PROVEN only for ids 0x1f5-0x1f8 |
| +13 bit7 (S6 bit15) | 54e9 | `(short)S6<0` -> SetBit 0x10 (0x10000); not read by the decision tree | engine asleep (S6 = 0x8000 while asleep, docs/09) | OBSERVED (310 samples) |
| +13 bit4 (0x10) | 54e9 | SetBit 0xf + GetWaitingStatus | waiting | PROVEN chain |
| +13 bit6 (0x40, S6 bit14) | 54e9 | `&0x40` -> SetBit 9 (0x200) (81202) -> tree msg 0x12f, class 3 (72204) -> code 0x2e = **s_door_open** (CNCoverOpen) | **cover open** | PROVEN chain, never observed |
| +13 bit0 (0x01, S6 bit8) | 54e9 | `&1` -> SetBit 8 (0x100) + GetJamStatus (81133-81165) -> tree class 2 msg 0x12d (72191-72770) -> code 0x3b/0x3c = **s_jam2** | **paper jam** | PROVEN chain, never observed |
| +14 | 54ea | pages decoded (E0A7 gate, docs/09) | counter | OBSERVED |
| +23 (Canon "Ews") | 54f3 | feeds mask 0x7260 and the "EMJ_OFFLINEREQUEST" test (bits 0x40/0x04 with the cartridge masks) | - | UNPROVEN |
| +28 | 54f8 | job id compared with `job+0x24d8` | job id | PROVEN |

Canon's GetStatus log "Basic/Aux/Cont/Eng/Ews/Paper/Es/User" = S0 low / S1 low (54dc/54de), S4 low + 0x552b + 0x5529 (54e4), S4 high + S5 high + 0x5526
(54e5/54e7), S6 word (54e8), 54f3, S5 low (54e6), 54f2, 54f7 (lines 80556-80575).

Corrections of the old text of this file: "S0 bit1 = paper out" and "S0 bit5 = uninitialised" were wrong (bit1 = sleeping, bit5 = reset); the "idle/no-paper probe
0x8e33" is the *asleep* state with payload+10 = 0x0080 (paper present).  The old `capt_status_summary` treated **payload+10 bit7 as "not error"**:
it is the paper-present bit, so a tray without paper would have been reported as a generic error.  (captdriver's SPECS name this bit nERROR; Canon's code
shows it is a per-slot paper bit.)

Error-bit priority in the tree (7290 bit -> id, first match wins, lines 72172-72770): bits 0-4 (fatal transport/identity; bit4 -> class 0xb id 7
"incompatible printer"); 0x200000, 0x20, 0x400000, 0x800000, 0x1000000, 0x2000000 (class 0xf ids 0x97-0x9c, codes 0x95-0xa0, no table entry);
0x80 service (0xc9); 0x100 jam (0x12d); 0x2000/0x100000 (0x135/0x136); 0x20000 illegal connection (0x12e..0x137); 0x200 door open (0x12f);
cartridge ids 0x191-0x19f (section 3); job-level ids 0x1f5-0x1fa/0x227-0x229/0x259-0x260; then busy/ready (0x2bd-0x2bf, 0x321-0x323, 0x385-0x389).

## 3. A0A4 cartridge reply (GetCartridgeStatus FUN_080b0b8c, decoded by FUN_080c79a0 / FUN_080c5508)
Reply = 8-byte header (cmd, size, 4 more bytes; header byte 6 (= payload+2) selects 7274: 0x10->1, 0x20->2, 0x40->4, 0x80->8 - a slot/colour hint, never non-zero
seen) followed by `(size-8)/12` entries (max 16).  Entry: `[0]=flag`, `[1]=id`, `[10]=flag2` (bytes 2-9, 11 unknown).
Id -> mask bit: 0..3 -> `1<<id` (toner slots), 0x10..0x13 -> `0x100<<n` (drums), 0x1f/0x20/0x30/0x31 -> 0x8000/0x10000/0x20000/0x40000.
The flag is decoded in this priority order (FUN_080c5508 lines 78836-78869); the matching mask gets the id bit:

| flag test | mask (port+) | error bit set by CheckCartridgeStatus | tree id -> UI code (key) |
|---|---|---|---|
| `flag & 0x80` | 7250 | 0x400 | ids 0x10..0x13: 0x191 -> code 0xed (no drum); **ids 0..3: 0x192 -> code 0x3e = s_no_cartridge** |
| else `flag & 0x20` | 725c | 0x40 | 0x194 -> code 0x5f / 0x19d -> 0xa8..0xab (no table entry) |
| else `flag & 0x01` | 724c | 0x40000 (ids >3) | 0x195 -> 0xf1 (drum out), 0x196 -> 0x5a (tonerout); ids 0..3 feed ready-warning code 4 |
| else `flag & 0x08` | 7248 + 7238 | - | 0x198/0x199 -> 0xf0 / 0x59 |
| else `flag2 & 0x08` | 7278 | - | 0x19f -> 0x5b |
| else `flag2 & 0x40` | 727c | - | ready-warning 0x18 (s_w_overtoner) |
| else `flag & 0x10` | 7238 | - | ready-warning code 3 (s_warn_toner2) |
| else `flag & 0x02` | 7234 | - | ready-warning **code 2 = s_warn_toner1 "toner needs replacing soon"** (msg 0x388, lines 72367-72398) |
| (independent) `flag & 0x40` | 7240 | - | ready-warning **code 5 = s_warn_toner_mem "connection problem with the toner cartridge"** (lines 72413-72416; key present in the cnabd xml) |
| (independent) `flag2 & 0x80 / 0x10 / 0x20` | 7244 / 7254 / 7258 | 0x80000 / 0x800 / 0x1000 | ids 0x19e (code 0xa1), 0x19b (0x106), 0x19c (0x56 ill_cartridge) |

Status: the chains for 0x80 -> s_no_cartridge, 0x40 -> s_warn_toner_mem and 0x02 -> code 2 are PROVEN in code; the *text* for code 2 does not exist in the
LBP6200 table (Canon never displays it).  The other rows are UNPROVEN for this printer (the model vtable +0x58 mask decides which slots count).
OBSERVED: the idle reply `a4a0 1400 000c 0000 | 00 00 00 03 03 01 0d fe 00 00 00 00` = one entry, flag 0, id 0, flag2 0 (probe-001, test mocks): no condition.
Entry bytes 3-7 (`03 03 01 0d fe`) are unexplained.

## 4. A0A1 (GetInputStatus FUN_080b0aec, decoded by CheckPaperStatus FUN_080c6040) - 6-byte header, 16-byte entries at `port+0x55ba + slot*0x10`
`[0]=flg`: 0x80 entry active, 0x40 **paper present** (edge events 4/8 in 0x72e8), 0x04 empty (inverted edge events), 0x08 size changed; 0x20 is set on a loaded
Letter tray (flg 0xe0) but not used by the status logic; `[1]=input id`, `[2]=num`, `[3]=size code` (0x0d Letter), `[4..5]=w`, `[6..7]=h` (300 dpi
units), `[11]=type`, `[12]=flg2` (bit 0x10 -> `72e0 |= 0x10`, the "lift-up"/paper-not-available path of FUN_080c156c).  The job-level decisions
(`CheckPaperError` FUN_080c156c, lines 76528-76825) combine it with the S5 low byte: slot paper bit clear (and flg2 bit4 clear) -> `JE_ADDPAPER` (job+0x2454 |= 0x20) ->
tree msg 0x25b -> code 0x4c = **s_no_paper2_5** ("Out of paper or paper could not be fed"); slot bit set but size mismatch -> `JE_CHANGEPAPER`
(|= 0x10, msg 0x25a -> code 0x4d s_change_paper) or `JE_CHECKPAPER` (|= 0x04, msg 0x25e); size change msg 0x259 -> code 0x41 s_change_size.
These are *per-page job events*, evaluated when a page is sent, not a free-running status bit.  The probe's idle A0A1 (`... 00 01 | c0 00 ...`) has one entry, flg 0xc0.

## 5. What the software reports (PROVEN chains only)
`capt_status_conditions()` / `capt_cartridge_conditions()` (core/capt.c) -> `report()` (cups/rastertocapt6200.c) -> CUPS `STATE:` keywords:

| condition | source | CUPS reason | UI key (Canon) |
|---|---|---|---|
| no paper | payload+10 bit7 clear | `media-empty-error` | s_no_paper2_5 |
| jam | payload+13 bit0 | `media-jam-error` | s_jam2 |
| cover open | payload+13 bit6 | `door-open-error` | s_door_open |
| service call | payload+12 bit1 or payload+9 bit7 | `other-error` | s_service_call |
| no toner cartridge | A0A4 flag 0x80, id 0..3 (snapshot taken by capt_job_begin from the A0A4 it already sends before the reserve; no extra wire traffic) | `toner-empty-error` | s_no_cartridge |
| toner memory/connection | A0A4 flag 0x40 | `other-warning` | s_warn_toner_mem |
| toner low (never observed) | A0A4 flag 0x02 (lowest-priority flag) | `toner-low-warning` | (code 2, no LBP6200 text) |

Not reported (unproven): the +9/+11 bit-0/1/7 codes, class 0xf codes, illegal-connection, size-mismatch events, output-tray-full.
`capt_status_summary()` returns ERROR for door/jam/service, NO_PAPER for the paper bit, else the busy/printing/ready states; sleep (S0 bit1, S6 bit15) and offline
(S0 bit4) are normal idle states, not errors.  Tests: `src/test/status_test.c` (real payload words must decode to "no condition", each bit alone gives exactly
its condition) and `replay_test` (16 frames, 0 mismatches).  Validation over all 1911 real A0A8 replies: 0 no-paper, 0 jam, 0 door, 0 service
(payload+10 = 0x0080 in all; S6 in {0x0000, 0x0080, 0x8000}; S4 in {0x0000, 0x0001, 0x0005, 0x0080, 0x0081, 0x0085}).

## 6. Other queries (unchanged)
A1A1 printer info (PrinterInfo, 64 bytes), A1A8 engine version ("DCON:04.04"), A3AF time; jam/service/misprint/waiting/illegal-connection detail queries are listed in 1.2.

## 7. Still unproven (flag before any live test)
1. Whether payload+10 bit7 really clears on an empty tray on hardware (code says so; an earlier hardware observation, S0=0x8c13 with the tray loaded and empty, saved no A0A8 payload).
2. Whether the jam/cover/service bits read as above on the wire (never provoked).  Do not provoke them with raw commands; observe a real fault with the capture running.
3. S0 bits 2/3/6 and the S0 high byte; S1/S2/S3; payload+23; A0A4 header bytes 4-7 and entry bytes 2-9, 11; the cartridge rows marked UNPROVEN.
4. `capt_wait_ready_for_data` still waits on S0 bit2 ("buffer full", unproven).

## 8. S0 bits 0x04 / 0x40 / 0x08 / 0x80: full consumer audit, Linux AND Windows (static, no printer access)
**Layout of the saved status** (FUN_080c7dbc GetStatus tail, lines 80707-80711): the 0x5c-byte reply is copied to port+0x54d8 (S0 low byte = 0x54dc), and
the first three reply dwords are ALSO stored at port+0x54cc (reply+0 header), **0x54d0 (reply+4..7 = S0 dword)**, 0x54d4 (reply+8..11 = word 0x000f).  The 0x54d0..0x54d7
copy is a working copy that the send code modifies (test-and-set); the next GetStatus overwrites it with the printer's real value.
**Windows equivalent**: cnabdemk.dll.c is the same C_Port/C_Send source.  S0 low byte = port+0x180 (GetStatus FUN_10034480), working copy = port+0x174
(`*(uint*)(port+0x174)=S0`, line 44428), credit byte = port+0x177.  The other Windows binaries (cnabdm/cnabdswk/cnabdun/cnabdpmk/cnabdrdk/cnap2*) contain no A0A8/status
code at all (only cnabdemk.dll has it).

### Every reader of S0 low byte (54dc / Windows 0x180) - complete list from the decompile
| bit | readers (Linux function: line) | behaviour |
|---|---|---|
| 0x01 | ReservePrinter FUN_080a7ddc 0x41 mask (59584/59634); ReleasePrinter FUN_080a69f4 (58756: waits until SET); FUN_080b5854 (69603); FUN_080c4718 (78489); FUN_080ca748 (81955/81959) | printer reservation state: set = no reservation held |
| 0x02 | FUN_080a6420 (58442), DoOnline FUN_080c03c8 (75902/75945), FUN_080c3260 (77607), FUN_080c531c (78726/78756), FUN_080c9448 CheckStatusSub (81033), GetStatus (the 0x58a8 block) | engine sleeping |
| 0x10 | FUN_080a1008 (55900), FUN_080a3654 (57227), FUN_080c1d00 PrintContinuous (77006/77376 mask 0x12/77447), FUN_080c3260 (77517), FUN_080c3730 (77797), FUN_080c531c (78729), GetStatus "detect OffLine" | offline |
| 0x20 | GetStatus (80527) | controller reset edge |
| 0x40 | **only** ReservePrinter's `(S0 & 0x41)==0` poll loop (FUN_080a7ddc, lines 59584 and 59634) | after A2A0 the printer must have 0x01 AND 0x40 clear (<= 2 s) before the monitor continues (then SetTime check); no other reader, never set in 1911 real replies |
| 0x08, 0x80 | **never read** | - |
| 0x04 | **not via 54dc**, but via the working copy 0x54d0: FUN_080a1008 lines 55931-55936 | see below |
Windows (cnabdemk.dll.c, port+0x180): tests only 0x02 (26777, 26932, 27028, 27437, 27459, 27557, 41136, 41708/41714, 42381, 45606), 0x10 (26780, 27464, 42062, 42294, 42357, 42468), 0x12 (42267),
0x01 (35085, 46204), 0x41 (29033/29054 ReservePrinter FUN_10021e90), 0x20 (44272).  **Windows: bit 0x40 only in that 0x41 mask; 0x08/0x80 never read; 0x04 only in the SendDataQueue test-and-set** - identical to Linux.
The bitset at port+0x7290 (FUN_080cda10/FUN_0806809c) is NOT filled from S0 low-byte bits: CheckStatusSub (FUN_080c9448) derives its bits from other payload bytes
(S4/S5/S6, cartridge, paper), so no bitset index maps to S0 0x04/0x40/0x08.

### Bit 0x04 = data-block interlock (C_Send::SendDataQueue FUN_080a1008, Windows FUN_1001f590)
```
branch A (taken iff cap0==0 || port+0x70ec not in {3,8}):   if (copy54d0 & 0x04) return 0 (not now);  copy54d0 |= 0x04;
branch B (taken iff cap0==0 || port+0x70ec in {3,8}):      if (byte 0x54d3 == 0) return 0;          0x54d3--;      // credit byte = S0 dword byte 3
```
`cap0` = bit0 of the engine capability word (`FUN_0808bf04(obj+4,0)`).  The credit byte (reply offset 7) is 0 in every real reply (1911 samples), yet Canon sent data,
so branch B cannot be active for the LBP6200; with branch B excluded cap0 must be set and the mode not 3/8, so **branch A is the LBP6200's rule**:
a D0A9 block (the SendDataQueue node = D0A9 + its page data) is started only if the last polled S0 had bit 0x04 clear, and the bit is set locally right after
starting one, so at most ONE D0A9 block is started per A0A8 poll.  OBSERVED: all 5 D0A9 blocks of capture-007 (jobs 79/80) are each preceded by an A0A8 poll
since the previous D0A9 (violations 0); the printer's own bit 0x04 is seen as S0=0x8c04 (14 samples, only capture-007: right after E0A7(1) while page data is being decoded,
and for the ~2.5 s 13-poll window of job 80 while page 3 is decoded).  Also in the same function (cap0 set): a block with page serial N is only started if
`port+0x9784(pages completed, "page safe") + max(2, byte port+0x588e) >= N` (FUN_080a1008 55920-55928; Windows 26830-26842), i.e. the host stays at most
~2 pages ahead of the printer's completed counter (exact numeric value of byte 0x588e / serial numbering NOT traced).
**Consequence for our code**: `capt_wait_ready_for_data` (waits S0&4 clear) was a correct idea but is unused; Canon polls A0A8 before every D0A9 block and
holds it while S0 bit 0x04 is set (pair = 2 blocks => poll in between).  Sending page 2/3 blocks without that poll+check is NOT what Canon does.

## 9. S0 bit 0x40 - behavioural deduction (Nov 14, Linux captmoncnabd; Windows cnabdemk twin identical)
Only reader: C_Port::ReservePrinter (FUN_080a7ddc lines ~59584/59634): after the A2A0 reserve command, poll status every 100 ms for up to 2 s until
(S0 & 0x41)==0.  On timeout the function STILL returns success (iVar2=0); the only difference is that A3A9 SetTime (FUN_080a6930/080af404) is skipped.
Companion bit 0x01: ReleasePrinter (FUN_080a6b**, line ~58756) waits until S0&1 is SET (= printer free/unreserved); the idle monitor loop (line ~81955)
reserves automatically when S0&1 is set; FUN_080a69f4/line 69603 treat S0&1 as "no reservation held".  In capture-007 the reserve flips S0 0x8c31 -> 0x8c30
(bit0 cleared by a successful reserve).  Conclusion: bit 0x01 = printer NOT reserved (free); bit 0x40 = a transient "reservation not (yet) settled / held
by another host" ownership flag - the code only waits for it to go away, never errors on it, never maps it to a UI status, never sets/uses it elsewhere.
Never seen set in 1911 real replies.  Safe handling = what we do (wait <=2 s after A2A0 for 0x41 clear, continue on timeout).
NOTE: (local_70 & 0x40) in GetStatus is the AUX status byte, a different field - not S0 bit 0x40.

### 8b. Page-ahead window (implemented Nov 14, capt.c gate_block / CAPT_PAGE_WINDOW)
Rule (FUN_080a1008): block serial N is sent only if `completed + max(2, byte[0x588e]) >= N`; completed = port+0x9784 = "page safe" = A0A8 payload +20 (80615-80640);
byte 0x588e = A1A1 reply byte 0x36 (ours: 4) minus a per-model virtual term (vtable+0x48 of the model profile; not traced).  Canon job 80 (capture-007) sent page 3's D0A9 at
done=0 => W >= 3.  We use W=3 (proven lower bound: never further ahead than Canon).  Serial = j->page_serial at block start (duplex: the back page's serial).
Offline: pipeline_test 3/4/5-page duplex jobs give Canon's job-80 command order, no deadlock; replay_test 16/16, duplex_proof selftest OK.  Not exercised on hardware yet.

### 8c. Reporting (Nov 14)
* Filter: STATE reasons (media-empty/jam/door-open/other-error, toner-empty-error, toner-low-warning, other-warning) from capt_status_conditions + capt_cartridge_conditions on every status poll;
  the A0A4 cartridge snapshot is refreshed every 5 s while a job runs (was: once at job start); all reasons are cleared (`-`) when the job ends.
* `lbp6200-status` (src/status) now prints a `Problems:` line with the decoded conditions and the CUPS reason each maps to; this is the between-jobs monitor (CUPS has no idle poll for a backend).
* Verified: status_test (real payloads + one decoded bit at a time), replay_test, pipeline test.  Hardware-observed: only "none" (idle/printing).  Paper-out/jam/door/service/toner mappings are
  decoded from Canon's code paths but NOT yet observed on the printer (toner-low never seen).
