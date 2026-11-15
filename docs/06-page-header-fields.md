# Page-header fields (offline oracle sweep, no printing)
Method: `tools/sweep.sh name "media=A4 <opt>=<val>" [file.ps]` runs Canon's pstocapt3 by hand (deterministic: two identical runs are byte-identical),
then `tools/diffhdr.py` diffs header records vs baseline. Streams in evidence/sweep-003/. Offsets below are PAYLOAD offsets (after the 4-byte u16 cmd,u16 size header).

## Stream structure (filter -> monitor "extended" stream), little-endian
Job:  0001 BeginJob(40) , then per page: 0002 PageInfo(128), D0A0(72), D0A4(12), D0A1(4), data (hdr `00 82 00 01 <u32 len?>`, Hi-SCoA band), C0A4(4), D0A2(4), 0004 EndPage(8); finally 0003 EndJob(4).
(Multi-page confirmed with a 3-page job: BeginJob once, page group repeated, EndJob once. Page count not in BeginJob.)

## 0001 BeginJob
[0:4]=cedadefa (0xfadedace) ; [8] = sides: 1 simplex, 2 duplex long-edge(left), 3 duplex short-edge (BindEdge=Top). (DuplexNoTumble vs Tumble alone does NOT differ; BindEdge does.)
## 0002 PageInfo_30
[0]  paper code: A4=9 Letter=1 Legal=5 Exec=7 A5=0xb B5=0xd
[84] media type: Plain=1 PlainL=0x24 Thick=2 Rough1=0x0c Rough2=0x0d Labels=0x14 OHP=0x13 Envelope=0x1c ; [85]=0xf0 for ROUGH1/2
[92] flags: bit1(0x02)=duplex, bit5(0x20)=short-edge bind, bit6(0x40)=this page is the back side (even pages)
[96:100] f9 08 fc 00 : [98] = signed toner density = (CNTonerDensity-8) ; (ohp also shifts it: ohp [98]=f9)
## D0A0 page setup (72 B)
[4] paper code2: A4=2 Letter=0x0d Legal=0x0c A5=3 B5=7 Exec=0x0a ; [36] media type (same as PageInfo[84] except rough=9)
[8] toner density = (CNTonerDensity-1)*4 (default PPD density 4 -> 0x0c; ohp -> 0)
[18] SuperSmooth (Image Refinement): 2 on, 0 off
[26:28] printable width in BYTES (A4 592=4736px; Letter 608=4864px), [28:30] printable height in lines (A4 6778, Letter 6364)
[30:32] paper width px @600dpi (A4 4960, Letter 5100) ; [32:34] paper height px (A4 7014, Letter 6600)
[34] CNSpecialPrintingModeB Settings2 -> 4 ; [54] bits: 0x20 AdjustmentA Mode1, 0x40 AdjustmentB Mode1, 0x08 SpecialModeA Settings1 ; [55] wrinkle correction L1=8, L2=4
## D0A4 (12 B) `01 04 01 01 00 f0 00 00`
[0]: 1 normal, 3 draft/toner-save (also [1]=0x0c,[5]=0xf1), 2 halftone=None(solid) ([1]=0x0c)
## 0001 [16] (BeginJob) = 6 when CNDetectPaperSize=False
## Data header after D0A1: `00 82 00 01 <u32>` ; u32 = band size field (A4 0x32c)? other sizes: A5 0x294 B5 0x25c Letter... (tied to printable size)
## Options with NO header effect: CNHalftone Gradation/Manual slot/PrintImageAdjustment/SkipBlank/RotatePrint (rot changes band data only)/SpecialModeB Settings1 (band data only, 60056 B).
## Resolution: Resolution=1200 is ignored by Canon's filter (identical output). 1200 dpi mode must come from the Windows side (tModeSpeedTbl_Hook_1200x1200) - still TODO.

## 1200 dpi: CONCLUSION
Windows lb6200au.xpd lists ONLY "600 dpi" (DMRES_HIGH 600); A1A1 reports 600x600 twice; Canon's Linux PPD offers 600 only. The LBP6200 has no native 1200 dpi mode.
(The earlier "tModeSpeedTbl_Hook_1200x1200" is a Linux libcaptfilter model/speed table, not an LBP6200 mode.) "1200-class" quality = Image Refinement (CNSuperSmooth, D0A0[18]) + halftone HighResolution. Scope item "1200 dpi" therefore = expose those, not a new wire mode.

## Page geometry @600 dpi (measured with edge bars through the oracle; scratch/sweep/edgeall.sh)
paper  page px     printable (bytes x lines)
A4     4960x7014   592 x 6778   (4736 px wide)
Letter 5100x6600   608 x 6364
Legal  5100x8400   608 x 8164
A5     3496x4960   408 x 4724
B5     4298x6070   508 x 5834
Exec   4350x6300   516 x 6064
Printable window starts 120 px from the left edge and ~118-120 px from the top (A4 120). Band format: 256-line bands, 16 blank pre-roll lines, params {1,4,1,1,0,0xf0,0,0}.
