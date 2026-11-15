# LBP6200d: real job sequence (captured Nov 13 2026 from Canon's own Linux driver)

Method: Canon cndrvcups-capt 2.71 + common 3.21 built/installed in the dev container, private CUPS
(~/lbp6200/cups), ccpd run as the normal user via an LD_PRELOAD geteuid shim, whole tree under
`strace -f` (fd 4 = /dev/usb/lp0). One A4 simplex 600 dpi PostScript page; the page printed.
Evidence: evidence/capture-002/{job3-sequence.txt,job3-frames.txt,print1/t.*}; filter output
(extended stream, produced OFFLINE, no printer): evidence/capture-002/manual/out.capt.
Tools: tools/straceseq.py (decode), tools/cmdnames.py (names).

## Printer-level sequence for one page (all little-endian u16 cmd,u16 size; replies in 6-byte hdr + rest)
 poll   A0A8 (status2, 92 B reply), A0A4 (cartridge, 20 B), A3AF (get time, 16 B)  -- before and between steps
 1  A2A0 RESERVE_UNIT       00 00 1e 00 00 00 00 00        -> reply 8 B, id in last u16 (here 2)
 2  E1A2 SET_LED_STATUS     13 00 + 14 zero bytes (size 0x14)
 3  E0BA SET_POWER_DOWN     01 00
 4  E1A1 SET_JOB_INFO       size 0x76: job#, 0x0e/0x06/0x16 fields, date/time (ea07 0b 0d 17 06 2e 03 = 2026-11-13 23:06:46),
                            UTF-16LE names (job/doc/user strings)  [layout still to be mapped]
 5  A0A4 cartridge status ; E0A3 CLEAR_MISPRINT ; E0A2 (unnamed, size 4) ; E0A4 DISCARD_DATA
 6  E0A5 GO_ONLINE          ee db ea ad + 12 zero bytes (size 0x14)
 7  E1A6 SET_PRINTED_PAGES  00 00 00 00
 8  D0A9 IC_COMPOUND_DATA (size 96) = D0A0 (0x48 B page params) + D0A4 (0x0c B Hi-SCoA params: L3=1 L5=4 1 1 L0=0 L2=0xf0(-16) L4=0)
                            + D0A1 (4 B) + D0A2 (4 B)
 9  C0A0 IC_VIDEO_DATA      u16 size 0x9634 (38452) then the Hi-SCoA band stream (no reply)   [size field u32 form for >64K: see cnabdemk]
 10 C0A4 IC_BLACK_END       (4 B, no reply)
 11 E0A7 START_PRINT        01 00
 12 E1A1 SET_JOB_INFO again x2 (page-state updates; byte 8 = 01, field 0x0e.. = 02 then 06)
 13 E0A9 RELEASE_UNIT       id from step 1 (02 00)
 then idle polling (A0A8/A0A4/A3AF ~ every 50 ms during job, ~1 s idle).
Replies to E-commands are 6 bytes `<cmd> 06 00 00 00`; status word 0000 = ok.

## Filter -> monitor extended stream (offline from pstocapt3; same data, different framing)
 JC BeginJob {1,0x28,0xfadedace..}, JC PageInfo_30 {2,0x80,..}, D0A0, D0A4, D0A1,
 <data hdr 00 82 00 01 2c 03 00 00> band stream, C0A4, D0A2, JC EndPage(3)/EndJob(4).
 The monitor re-wraps D0A0/D0A4/D0A1/D0A2 into D0A9 and sends the band as C0A0.

## Offline oracle
`PPD=... pstocapt3 job user title copies "options" file.ps > out.capt` runs Canon's real filter chain
without a printer, so page-header fields for any option (paper, duplex, toner save, density, ...) can be
decoded by diffing out.capt across option changes. No printing needed.

## Open items
* Full field maps: D0A0 (0x48), E1A1 (0x76), E1A2, E0A5; status records A0A8/A0A4/A3AF.
* Names for A3AF and E0A2; large-band C0A0 size form; multi-page + duplex sequence; 1200 dpi.
* Replay test: reproduce this exact sequence with our own code and print the same page.
