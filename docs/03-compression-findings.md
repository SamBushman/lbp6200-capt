# Compression findings (Nov 13, static analysis of Canon Linux libcaptfilter.so.1.0.0)

* The band encoder is `caslim*` (caslimNew/caslimEncoding/caslimGetData/caslimGetSize).
  It takes a parameter block `{b0,b1,L3,?,L0,L2,u16 L4}`-style whose layout matches the Hi-SCoA
  D0A4 parameters documented in captdriver SPECS (L3,L5,01,01,L0,L2,int16 L4).
* End-of-band: emit 8-bit END code + 2-bit end type, then pad to a 4-byte boundary
  (caslimGetData -> FUN_00033954 / FUN_000339c0): matches Hi-SCoA.
* Bit sink FUN_000338bc XORs every output byte with **0xBC** (= ~0x43). captdriver XORs
  0x43 on a stream whose bit polarity is the inverse; same wire bytes expected. VERIFY by
  differential test vs libcaptfilter (needs a 32-bit runtime) or by a first real page.
* libcnaccm.so/cnaccm32.dll is the colour-management/halftone/calibration library
  (CMS/CT/HT/IPTCalib*), NOT the codec.
* Pipeline in libcaptfilter: CAPT_CompData -> GetRaster2 -> CAPT_RasterData2 ->
  EncodeLine3_Common (StoreRasterData, Retouch_* = "Automatic Image Refinement")
  -> SelectEncodeLine3 -> EncodeLine3[_dylib] -> caslimEncoding.
* Filter->daemon "extended" commands (buftool, little-endian): JC BeginJob {1,0x28,0xfadedace,..},
  JC PageInfo_30 {2,0x80,...}. Printer-level job sequencing lives in the per-model monitor
  (captmoncnabd / Windows cnabdemk), NOT in ccpd (ccpd only manages queues).

## Differential test result (Nov 13)  — evidence/difftest-001-hiscoa.txt, scratch/difftest/
Canon's codec library is **libcanon_slim.so.1.0.0** (cndrvcups-common 3.21): exports
lCaptComp / lCaptCompEx / lCaptCompEx2 / lCaptDecode. libcaptfilter's caslim*/EncodeLine3 call it.
  lCaptComp(src, dst, linesize, nlines, dstcap, 0, &lines_done, params8) -> bytes written
  lCaptDecode(&srcptr, dstaddr, linesize, nlines, &srclen, 0, params8)   -> lines decoded
  params8 = {L3, L5, 1, 1, L0, L2, L4lo, L4hi} == captdriver hiscoa_format_params / D0A4 payload.
  Literal bytes are written as byte^0x43; pad bytes 0xBC; bands need blank pre-roll lines.
36/36 cases (2 param sets x 2 line sizes x 9 image types, incl. random/sparse/correlated):
captdriver's hiscoa_compress_band output decodes bit-exactly through Canon's own decoder;
Canon->Canon round trip also OK (validates the harness). Simple patterns: byte-identical streams;
noisy images: both valid, sizes within a few %.
CAVEAT: proves codec compatibility with Canon's host library. Printer-firmware acceptance and the
LBP6200's actual params/band size still need confirmation against real traffic / a first page.
