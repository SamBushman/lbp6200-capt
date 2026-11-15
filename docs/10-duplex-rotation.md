# 10 - Duplex: page order and back-side rotation (Canon behaviour, proven offline)  [Nov 14 2026]

Cause of job 85's wrong page 2 (user: "wrong direction, horizontally offset"): we sent page 2 unrotated and in front-first order.
Canon sends, per duplex sheet, **[back page rotated 180 deg] then [front page]**.  A lone odd last page is unrotated.

## Where it happens (and where it does NOT)
| step | who | evidence |
|---|---|---|
| rotate even pages 180 deg (long-edge duplex only) | Canon's FILTER chain (pstocapt3 -> gs/opvp libcanonc3pl -> libcaptfilter) | filter output page 2 == wire block A **byte for byte** (A4 full capture, 23376 B; Letter job 79 first 8188 B and job 80 page 2) |
| reorder to [page 2, page 1] | MONITOR (captmoncnabd C_SendQueue::QueueSheet FUN_080a405c; C_Send::QueuePage FUN_080a0e84: type-4 node takes the REVERSE page's D0A0 (sheet+0x10), type-3 node the FRONT's (sheet+0xc)) | wire block 1 == filter page 2, block 2 == filter page 1 (A4 duplexL.bin, Letter jobs 79/80); the image-node order itself was observed, the code line that puts the reverse image first was not read |
| band data | monitor passes it through unchanged (0x8200 record -> C0A0 chunks <= 0xff00, captmoncnabd.c ~46849); no decoder is linked into captmoncnabd | byte equality above |
Angle rule = libcaptfilter `GetDataRotate` @0x000201fc: duplex mode (param+0xa0) 2 (long edge): angle = 0xb4 (180) if page number (param+0xc4) is even else 0
(90 / +90 only for paper-table flag [5]==1 = rotated-feed papers, none of A4/Letter/Legal/A5/B5/Executive); mode 3 (short edge): 0 (no rotation) for these papers;
CNRotatePrint adds +180.  Margins/offsets per parity: `SetPageInfo` @0x00014efc (`iStack_110 = page % 2`).
**Trap:** my standalone oracle runs (pstocapt3 without env PRINTER) did NOT rotate/offset page 2 - with `PRINTER=LBP6200` set (as cupsd does) the output is rotated.  Always set PRINTER when using the oracle.

## Which pages / modes (measured with the real filter, identical test pages, ~/lbp6200/src/test/duplex/*.py)
* long edge (`Duplex=DuplexNoTumble`, also `DuplexTumble` WITHOUT BindEdge=Top; PageInfo[92] = 0x02 front / 0x4a back): even pages rotated, wire order [even, odd].
* short edge (`BindEdge=Top`, PageInfo[92] = 0x22 / 0x62): page 2 bytes identical to page 1 (NO rotation); wire order still [page 2, page 1] (duplexS.bin: block 1 = filter page 2 + 4 trailing pad bytes, decoded equal; block 2 = page 1).
* 3 pages (job 80): [p2 rotated, p1] then lone p3 unrotated (D0A9[5]=0).  4+ pages: pairs (2,1),(4,3) by the same per-sheet rule (inferred from QueueSheet, not captured).

## Geometry rule (back page = exact 180 deg rotation of the 1-bit window bitmap, then a left shift)
For a back page with window W bytes x Lines rows (bits NB = 8*W): let F be the window bitmap our filter would send for that page as a FRONT page
(rows r = 0..Lines-1, columns c = 0..NB-1).  Canon's back window B is
    B[r'][c'] = F[Lines-1-r'][ (NB-1-dx) - c' ]      (c' = 0..NB-1-dx, bits beyond that are 0)
i.e. rotate 180 deg, then shift dx bits toward column 0 (zeros enter at the right end).  dy = 0.
| paper | W bytes x Lines | dx (bits) | evidence |
|---|---|---|---|
| A4 | 592 x 6778 | 7 | 502/502 ink rows exact (identical pages), plus A4 duplexL wire |
| Letter | 608 x 6364 | 4 | exact; Letter wire first 2560 lines (capture-007) |
| Legal | 608 x 8164 | 4 | exact |
| A5 | 408 x 4724 | 8 | exact |
| B5 | 508 x 5834 | 5 | exact |
| Executive | 516 x 6064 | 2 | exact |
(effective printable width = NB - dx: 4729, 4860, 4860, 3256, 4059, 4126 px; no formula found - table-driven.  PPD ImageableArea does not predict it.)
Cross-check with page coordinates (marker rectangles, centre-based): front window offset Lf = 120.5 px for all papers, back Lb = 110.5 A4, 119.5 Letter/Legal/A5, 118.5 B5, 103.5 Executive.
**Halftoning:** for bi-level content the rotate-the-bitmap rule is exact; for gray/AA content 20% of rows differ (A4 test: 4017/5025 rows equal) - Canon's screen is not rotation-symmetric,
i.e. Canon rotates the raster BEFORE halftoning.  Implement: rotate the 8-bit source (row r' <- window row Lines-1-r', column c' <- source column per the shift above) and then halftone with the normal
screen; geometry then agrees with Canon to the pixel, only dither phase differs (expected, ours is not Canon's screen anyway).

## What our code must do (for the implementer)
1. Duplex long edge: hold the odd page (front) data; when the even page (back) arrives, rotate it as above, send  D0A9(front,[5]=4)  <back page C0A0.. C0A4>  <front page C0A0.. C0A4>  D0A9(back,[5]=2).
   (D0A9 type 4 carries the REVERSE page's D0A0, type 3 the front's: identical bytes except [5] when both pages have the same paper.)
2. Lone odd last page: D0A9[5]=0, data unrotated, as now.
3. Short edge: same ordering, no rotation.
4. Note: front page must be buffered (as compressed frames or raster) until the back page has been rendered - Canon's monitor does the same.

## Offline proof harness
`~/lbp6200/src/test/duplex/duplex_proof.py` (needs `gcc -m32 -O1 -o dec2pbm dec2pbm.c -ldl` in that dir, done).  Decodes every C0A0..C0A4 block of Canon's reference and of our output with Canon's own lCaptDecode and compares in wire order.
* `--selftest` : reference vs reference passes, swapped order detected.
* `--ref-wire ~/lbp6200/evidence/mockwire-clean/duplexL.bin --ours <dump>` or `--ref-ps <ps> --paper A4 --opts "Duplex=DuplexNoTumble" --ours <dump>` (reference regenerated by Canon's real chain with PRINTER set).
* verified: Canon's real wire vs regenerated reference = EXACT both blocks; current (unrotated, front-first) output = FAIL both blocks.
Use bi-level test PS (e.g. ~/lbp6200/src/test/duplex/mk.ps) for the exact check; gray content only compares within --tol.
Helper scripts: measure.py (per-paper marker geometry via real filter), fit.py (exact dx fit), shortedge.py (option variants).  Data: ~/lbp6200/scratch/oracle/papers/.

## Open doubts
* Why the filter output depends on env PRINTER (it connects to 127.0.0.1:59787, refused offline, and then rotates) - the code-level switch is not traced; the real chain always has PRINTER set and its output equals Canon's wire (proven).
* Code line giving reverse-first image order not read; order proven empirically on 3 captures.
* 4-page ordering, Legal/A5/B5/Executive wire not captured from the real monitor (filter-level only).
* Gray-level pixel equality vs Canon impossible (different screen); geometry within 1 px.

## IMPLEMENTED + OFFLINE-PROVEN (Nov 14 18:05)
* core/capt.c: duplex pair wire = D0A9[5]=4 (built from the back page's D0A0 with back_side cleared), back page data, held front page data (its own D0A9 dropped), D0A9[5]=2;
  lone odd page unchanged (held D0A9+data patched to [5]=0).  Every D0A9 block is gated on a status poll with S0 bit 0x04 clear (Canon SendDataQueue, docs/07 sec 8).
* core/capt.h/c: capt_geom.back_dx per paper (A4 7, Letter 4, Legal 4, A5 8, B5 5, Executive 2).
* cups/rastertocapt6200.c: long-edge even pages are read whole and rotated 180 deg (window, shifted dx) BEFORE halftoning; short edge: no rotation (order still back, front).
* Also fixed a halftone defect found by the proof: `(v>>2) < bayer` dropped 1 dot in 64 of solid black (bayer==0); now `v < bayer*4+2` (black always ink, white never).
* Proof method (renderer-independent): src/test/duplex/mkras_from_canon.py builds a CUPS raster from Canon's own unrotated front-window bitmap; our filter output through pipeline_test.py
  (DUMP=file, OPTS env) is compared by duplex_proof.py with Canon's real chain (PRINTER=LBP6200) decoded with Canon's lCaptDecode.  Results, all EXACT every row:
  A4 long-edge 2 pages; Letter long-edge 2 pages; A4 short-edge (BindEdge=Top) 2 pages; A4 long-edge 3 pages (blocks [p2 rotated, p1, p3] and D0A9[5] = 4,2,0).
* NOT proven offline: Legal/A5/B5/Executive wire (filter-level numbers only), 4+ page ordering, Canon's page-ahead window (completed+max(2,N) >= serial) not implemented.

## Live result job 90 (Nov 14 18:07, 2-page Letter long-edge): orientation of both pages correct (user).  Page 1 (the SECOND data block on the wire) printed SHEARED
(progressive horizontal skew; left border wrapped to the right edge) - photo ~/Downloads/1000011157.jpg.  Same symptom as job 85's page 2 (also the second block).
Found by comparing our blocks byte-wise with Canon's: Canon terminates EVERY page's band stream with a 4-byte `bd 3c bc bc` (= empty band, eob type LAST; present in all 40
filter streams + both duplex wire captures); we never sent it.  Hypothesis (not yet proven on hardware): without it the printer's decoder is not reset, so the next page
block decodes with stale state.  Fixed in capt_job_page_lines (terminator appended, same bytes as Canon's); offline: tails now equal Canon's, bitmaps EXACT (A4 long, 3 pages).
Remaining byte-level difference: our compressor chooses different (equivalent) codes in places (total 8 bytes shorter per page) - decodes identically with Canon's decoder.

## Job 91 live result (2026-11-14 18:42) - page terminator confirmed
Same job as 90 but with Canon's 4-byte page terminator (bd 3c bc bc, empty band eob LAST) appended to every page's band stream: printer counters end dec=2 prn=2 out=2 done=2,
the filter's wait for page 2 completes, and both sides print correctly (orientation, centering, no wrap).  Without the terminator (jobs 85/90) the second block of the sheet was
printed with its left border wrapped to the right edge (constant offset) and the page never completed (out=1 done=1).

## Job 92 live result (2026-11-14 18:47) - 4-page long-edge duplex with page-ahead window
Letter, Duplex=DuplexNoTumble, 4 pages (scratch/realtest/four.ps): job completed cleanly in ~20 s, printer counters 4/4/4/4, both sheets correct on all four sides
(orientation, centering, no wrap; user confirmed).  Window W=3 (docs/07 8b) did not stall.  Still unproven on hardware: Legal/A5/B5/Executive, short-edge, jobs > 4 pages.
