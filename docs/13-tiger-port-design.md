# 13 - Mac OS X 10.4 (Tiger, PPC) port: findings and design

Status: DESIGN FOR REVIEW (Nov 14 2026). Nothing has been installed or changed on the G5; all evidence below comes from read-only
queries and from files copied to `ref/tiger/` (see "Evidence").

## 1. Constraints (from the user)
* Target: G5 running Tiger 10.4.11. Another Claude session may be using it: no kernel panic, no reboot if avoidable, no killall,
  nothing that needs more than userspace without pausing first.  A kext (even codeless) is a PAUSE POINT: wait for the user to say
  the G5 is free.
* Follow Tiger driver conventions as seen in existing drivers/libraries; the stability constraint is about HOW we work, not WHAT we build.
* Tiger only (no Leopard).

## 2. How Tiger really talks to a USB printer (evidence-based)
1. CUPS 1.1.23 (`/usr/include/cups/cups.h`): no `cups_page_header2_t`, no `cupsRasterReadHeader2`, no back-channel (fd 3),
   no `CUPS_CSPACE_SW`.  Filters talk one-way: raster -> filter -> backend.
2. Stock backend `/usr/libexec/cups/backend/usb` (IOKit only, `otool -L`) enumerates `IOUSBInterface` services with
   bInterfaceClass 7 / SubClass 1 (`ghidra/out-tiger/usb.c` FUN_00003ecc), builds `usb://Make/Model?serial=` URIs and prints `direct ...`
   discovery lines (FUN_00002da0).  For each printer it asks the interface's registry entry for the property
   **`"USB Printing Class"`** (CFSTR at 0x6310).  If present, it loads that path as a CFPlugIn of type
   `06047D16-53A2-11D6-9206-003065524592`, factory instance of interface `F1506FC0-53A0-11D6-8939-003065524592`
   (FUN_000036e4).  If loading fails it logs "Unable to load USB class driver, using generic class driver instead" and uses
   `/System/Library/Printers/Libraries/USBGenericTOPrintingClass.plugin`.
3. The property is injected by a **codeless kext**: `/System/Library/Extensions/Xerox USB Printing.kext` has only an Info.plist with
   `IOKitPersonalities` of class `AppleUSBMergeNub` (Apple's own kext), `IOProviderClass IOUSBInterface`,
   `IOProviderMergeProperties {"USB Printing Class" = "/Library/Printers/Xerox/Xerox USB ClassDriver.plugin"}`, matched on
   idVendor/idProduct/bConfigurationValue/bInterfaceNumber.  Xerox's plugin (`Library/Printers/Xerox/...`) uses the same plug-in type as Apple's.
   => This is the Tiger-native, vendor-supported way to own the USB transport of a printer.  No kernel code is added.
4. Class-driver interface (COM-style, 0x60-byte block; derived from `USBGenericPrintingClass` and the backend's call sites):
   | vtbl | method | notes |
   |---|---|---|
   | +04/08/0c | QueryInterface / AddRef / Release | |
   | +28 (data) | `IOUSBInterfaceInterface **` | INJECTED by the backend before Open (usb.c:1036) |
   | +2c/+2d (data) | bulk-out / bulk-in pipeRefs | filled by Open |
   | +30 | control request(self, IOUSBDevRequest*, timeout) | |
   | +34 | GetString(idx, langid, timeout, CFString*) | |
   | +38 | SOFT_RESET (class req 0x21/2) | |
   | +3c | GET_PORT_STATUS (0xA1/1) | |
   | +40 | GetDeviceID(CFString*, timeout) (0xA1/0) | |
   | +44 | ReadPipe(self, buf, UInt32 *len) | |
   | +48 | WritePipe(self, buf, UInt32 *len, 0) | |
   | +4c | Open(self, locationID, protocol) | backend tries protocol 2 (bidi) then 1 |
   | +50 | Abort | called on write error |
   | +54 | Close | |
   | +58/+5c | initialise / release cached device-ID string | |
5. Backend data flow (`usb.c` FUN_00002718): reads the job from stdin in **2048-byte** chunks and calls `WritePipe` on each; a
   second thread polls `ReadPipe` (511-byte buffer, every 0.25 s) and writes anything it returns to the backend's **stdout**
   (never to the filter); it stops on a 0x04 byte.  For a stdin job the backend does `sigset(SIGTERM, SIG_IGN)` (0x2a2c) - same as our
   Linux backend, so the plug-in is never killed mid-job.  `STATE:/INFO:` lines on stderr reach cupsd as usual.
6. Discovery / PPD match: Printer Setup Utility and PrintCore load PBMs, run the backend's discovery, and auto-select PPDs from
   `/Library/Printers/PPDs/Contents/Resources/` (+`en.lproj/`, `/Network/...`) with `CPLAutoSelectMatchPPDs` / `CPLCreateMatchPPDByProduct`
   (strings in PrintCore, libPPDLib) using `*Product`, `*ModelName`, `*NickName`, `*1284DeviceID`.
7. Third-party filter convention (514 shipped PPDs analysed, `ref/tiger/ppds`): raster filters live in `/Library/Printers/<Vendor>/Filter/`
   and are referenced by ABSOLUTE path in `*cupsFilter` (Brother: `application/vnd.cups-raster 0 /Library/Printers/Brother/Filter/rastertobrothermode9`);
   PPDs are gzip files in `Contents/Resources/en.lproj/`; Brother PPD identification: `*Product: "(HL-2060)"`, `*1284DeviceID: "MFG: Brother;MDL: HL-2060"`,
   `*cupsVersion: 1.1`, K colourspace is `cupsColorSpace 3`.
8. The printer on the G5 (`ref/tiger/ioreg-service-g5.txt`): `IOUSBDevice "Canon CAPT USB Device"` 04a9:271b, serial 0000A1DATDXT,
   config 1, `IOUSBInterface@0`: class 7, subclass 1, **protocol 2 (bidirectional)**, alt 0, 2 endpoints; only `IOUSBCompositeDriver`
   is attached (no printer kext).  Canon's BJ plugins (BJUSBIOM etc.) use a Canon-private plugin framework (different CFPlugIn type UUIDs
   than Apple's) - NOT a convention to copy.

## 3. Why the Linux architecture cannot be copied, and what replaces it
* The Linux filter converses with the printer through a back-channel; Tiger's CUPS 1.1 has none, and Tiger's stock `usb` backend would
  merge commands (it hands the class driver arbitrary 2 KiB chunks).  CAPT needs both one-command-per-USB-message framing and
  request/reply flow control.
* The only place on Tiger that sits next to the device, sees both directions and is vendor-extensible is the **USB class-driver plug-in**
  loaded inside Apple's own `usb` backend.  So: filter = pure transform (like every Tiger filter), plug-in = transport + CAPT job engine.

## 4. Design
```
app -> PDF -> cupsd -> pstocupsraster -> rastertocapt6200 (filter) -> Apple usb backend (stock!) -> LBP6200 class-driver plug-in -> IOUSBInterface -> printer
                                         halftone, Hi-SCoA, duplex rotate         unchanged            CAPT engine + framing + status + release
```
| piece | location | notes |
|---|---|---|
| `rastertocapt6200` | `/Library/Printers/lbp6200/Filter/` | ported to CUPS 1.1 API; emits a private record stream (below) on stdout; no reading from the printer; `STATE:/INFO:` only for filter-side events |
| class-driver plug-in `LBP6200 USB ClassDriver.plugin` | `/Library/Printers/lbp6200/` | Mach-O PPC CFPlugIn, type 06047D16-..., new factory UUID; implements the table in 2.4; hosts `core/capt.c` over an IOKit `capt_io`; writes `STATE:/INFO:` to stderr |
| merge-nub personality kext `LBP6200 USB Printing.kext` | `/System/Library/Extensions/` | codeless, copy of the Xerox pattern for 0x04a9/0x271b, config 1, iface 0  **(PAUSE POINT)** |
| PPD `Canon LBP6200.gz` | `/Library/Printers/PPDs/Contents/Resources/en.lproj/` | `*Product "(LBP6200)"`, `*1284DeviceID`, absolute `*cupsFilter`, K colourspace 3, `cupsVersion 1.1` |
| status tool | `/Library/Printers/lbp6200/Utilities/lbp6200-status` | optional; cannot open the device while a job runs (exclusive); fails cleanly if busy; same IOKit code |
| package | `.pkg` | later |

Filter -> plug-in record stream (`LBPC` container; plug-in reassembles records across the backend's 2 KiB chunks):
`JOB_BEGIN{user,title,host,tz,time,duplex,total_pages}`, `PAGE{opts, n bands, band lengths + data}` (bands already Hi-SCoA compressed,
duplex back page already rotated), `JOB_END{state}`, `CANCEL`.  Everything the Linux engine needed replies for (ready-for-data, page window,
start-print, release) runs inside the plug-in's `WritePipe`, so CUPS backpressure works naturally (the backend blocks in WritePipe).
* `ReadPipe` must never touch the device and must never return data (it would go to cupsd's stdout): return an error/timeout at once;
  the backend then sleeps 0.25 s.
* Cancel: cupsd SIGTERMs filter+backend; backend ignores it; the filter writes `CANCEL`, closes its output; the plug-in does the
  Canon release sequence (E1A1 state 4 + E0A9).  `Close` without `JOB_END` is the safety net (always release if reserved, docs/12).
* Thread safety: `WritePipe` (main thread) and `ReadPipe` (reader thread) run concurrently; plug-in state is guarded.
* IOKit transport: `IOUSBInterfaceInterface` `WritePipe`/`ReadPipe` (+ `ReadPipeTO`/`WritePipeTO` if the 10.4.11 header has them), one frame per
  write; timeouts as in `capt_io`.  Header check is part of step 2.

## 5. Testing strategy (G5-safe by construction)
1. Host (Linux): compile the portable core + record parser + engine natively and run it against the existing mock-printer tools
   (`tools/mockprinter*.py`, `docs/08`).  Compare Hi-SCoA/CAPT output byte-for-byte filter-side.
2. G5, userspace only, no printer, no kext: build filter and plug-in under `nice` in a private directory; load the plug-in with a
   test harness that calls the factory and injects a MOCK `IOUSBInterfaceInterface` (the backend injects it at +0x28, so the plug-in is
   designed to accept any); drive it with the same record streams and compare wire bytes with the Linux run (endianness proof).
3. PAUSE for the user (kext = codeless but system-visible): install personality kext, load without reboot (`kextload`/kextd scan - to be
   confirmed statically first), still no job.
4. Real printer, staged: discovery only (`usb` backend with no args must list `usb://Canon/LBP6200?serial=...`) -> device ID read ->
   read-only status -> one test page -> duplex.  Each step re-checks uptime/ps and only adds files; nothing broad.

## 6. Open items that need evidence before coding
* Exact `IOUSBInterfaceInterface` version/slots in 10.4u SDK (`ReadPipeTO`, `ControlRequestTO`, pipe-ref API) - copy
  `/Developer/SDKs/MacOSX10.4u.sdk/.../IOKit.framework/Headers/usb/*.h` (read-only).
* The generic plug-in's Open() (FUN_00001930) behaviour on alt settings/protocol 2 vs 1 so ours matches; and how the backend uses `+0x58`.
* Which user cupsd runs backends/filters as on Tiger, and how backend stdout is wired in CUPS 1.1.23 (we return no data, so low risk).
* How to make the personality take effect without a reboot (kextload of a codeless kext / kextd rescan) and the ownership/permissions
  kextd requires; look at Xerox kext file modes and at `/System/Library/Extensions.mkext` handling.
* PSU auto-match details (`CPLAutoSelectMatchPPDs`): what a PPD must contain so the printer is detected and selected automatically.
* Endianness audit result (step 2 proves it).
* The A1A0 device-ID reply vs the USB 1284 device ID (PPD `*1284DeviceID` must equal what the backend reports).

## 7. Evidence
`ref/tiger/` (31 MB copy of G5 printing files), `ghidra/out-tiger/{usb,USBGenericPrintingClass,USBGenericTOPrintingClass,Xerox USB ClassDriver}.c`,
`ghidra/out-tiger/USBGenericPrintingClass.at.c` (vtable methods), `tools/tiger/{macho_str,ppcdis}.py`, `ref/tiger/ioreg-*.txt`, `ref/tiger/ppds/`.

## 8. Step 0 result (Nov 14 2026): one source tree, Linux unchanged, PPC emulation green
* Refactor (all in `src/`): `core/capt.[ch]` gains `capt_compress_page()` + `struct capt_page_sink` (begin/data/end; chunk boundaries of 0xff00 are
  part of the wire format and preserved); `capt_job_page_lines()` is now a thin wrapper with a job sink.  New `cups/rasterpage.[ch]`
  (the "page producer": raster reading, halftone, back-page rotation, paper/option mapping) and `cups/cups_compat.h` (CUPS 1.1 vs 1.2+
  raster API).  `cups/rastertocapt6200.c` keeps only Linux I/O (stdout / fd 3), job conversation and cancel handling.
* Incidental fix: the old inner chunk loop in `capt_job_page_lines` did not stop at a failed `capt_page_data`, so a later success could overwrite the
  error; the compressor now stops at the first failure.  Success-path frames are unchanged.
* Proof: `baseline/golden` (18 scenarios, pinned clock, polls excluded, reproducible, mutation-tested) reproduced IDENTICAL after each part;
  `replay_test` (16 frames vs Canon's real capture) and `status_test` pass; a clean-export build with the RPM spec's flags builds all three binaries.
* Endianness: the unmodified filter + shared producer + core + Hi-SCoA, built for big-endian PPC32 against Tiger's own CUPS 1.1.23 headers (+ a libcups
  shim, `src/test/ppc/`), run under `qemu-ppc`, reproduces all 18 golden scenarios IDENTICALLY (`make -C src/test/ppc check-ppc`).
  Not covered by emulation: Mach-O/Apple ABI, CoreFoundation/IOKit glue, real libcups raster reading on Tiger.
* Tools: `tools/baseline/{run_baseline.py,gen_rasters.c,ras2plain.c,fakeclock*.c}`, snapshot `baseline/src-linux-pre-tiger.tar.gz`.

## 9. Step 1 result: LBPC records, filter front end, driver library (host only)

Code (all verified without touching the G5):

| Piece | Path | Role |
|---|---|---|
| Record format | `lbpc/lbpc.h` | JOB_BEGIN / PAGE_BEGIN / PAGE_DATA / PAGE_END / JOB_END, little-endian, versioned stream header |
| Writer | `lbpc/lbpc_write.c` | `capt_compress_page()` sink that emits records; chunk boundaries (0xff00) are preserved because they are wire format |
| Tiger filter | `tiger/rastertolbpc.c` | CUPS 1.1 raster in, records out; no USB, no CAPT conversation; SIGTERM = finish page, JOB_END(cancelled) |
| Driver library | `lbpc/lbpc_drive.c` | push parser (`lbpc_drive_feed` takes any piece size, the usb backend hands over 2048 bytes) that replays records into `capt_job_*`; `lbpc_drive_end` waits for the last page and emits `PAGE:`; `lbpc_drive_cancel` only raises the engine's cancel flag |
| Host stand-in for plug-in+backend | `test/lbpc/lbpc_feed.c` | same library behind a pty |

Status lines (`STATE:`, `INFO:`, `PAGE:`, `ERROR:`) now come from the driver library through a `log` callback; the plug-in writes them to stderr, which Apple's usb backend shares with cupsd's back-channel reader.

Results:
* `make -C test/lbpc check`: filter -> records -> driver -> pty mock printer, all 18 golden scenarios have identical wire frames and identical INFO/ERROR/PAGE lines.
* `make -C test/ppc check-ppc-lbpc`: the same with BOTH the filter and the driver library built as static big-endian PPC32 and run under qemu-ppc (filter against Tiger's CUPS 1.1.23 headers): IDENTICAL.
* `test/lbpc/lbpc_fuzz` (ASan+UBSan, stub printer): the intact stream is accepted at feed sizes 1..65536; ~3000 mutated/truncated/garbage streams: no crash, no leak, no hang. Every stream that is not the original is rejected unless the mutation only touched opaque compressed data.

Decisions made while building it:
* A stream that ends without JOB_END aborts the job (release, E1A1 only if the link is healthy); an empty stream is not a job.
* JOB_END(2, filter failed) maps to `capt_job_abort(CAPT_EARG)`; (4)/(5) map to `capt_job_finish(4/5)` exactly like the Linux filter.
* `total_pages` is in the record but the filter sends 0 (CUPS 1.1 raster does not announce the page count), same as Linux.

Not covered by emulation (step 2 on the G5): Mach-O/Apple ABI build, CoreFoundation/IOKit glue of the class-driver plug-in, real libcups raster reader.

## 10. Step 2 result: class-driver plug-in on Tiger against a MOCK USB interface (no printer, no kext)

Built and run on the G5 inside a private directory (`~/lbp6200-step2`), under `nice`, userspace only; nothing was installed anywhere else and no
kext/USB device was touched.  `test/tiger/check-g5.sh` repeats the whole thing from the host in ~7 s.

* `tiger/plugin/lbp_class.c` + `Info.plist` -> `LBP6200 USB ClassDriver.plugin` (Mach-O PPC bundle, Xcode gcc 4.0.1, 10.4u SDK, `-mmacosx-version-min=10.4`).
  CFPlugIn type 06047D16-53A2-11D6-9206-003065524592, factory `USBPrinterClassFactory`, our factory UUID F6DF49CB-1DE2-4F50-879D-A498429CFAF4.
* Object layout (compile-time asserted): outer object {block*, factoryID, refcount, private*}; 0x60-byte block with the method table and the data the backend
  writes (+28 interface, +2c/+2d pipeRefs).  Resolved while reading Apple's generic driver:
  * the interface UUID to QueryInterface is 03346D74-53A3-11D6-9EA1-763065524592 (IUnknown is accepted too);
  * `+58` is the block initialiser (the backend calls it with NULL right after loading; it also clears +28), `+5c` releases the cached device ID;
  * the backend writes the `IOUSBInterfaceInterface**` straight into block+0x28; if none is there, Open() finds the class-7/subclass-1 interface by locationID (same search as the generic driver);
  * `Close()` closes the interface AND releases the reference the backend handed over;
  * the backend's read thread treats "no data" (or any error) as "sleep 0.25 s, poll again" -> our `ReadPipe()` always reports 0 bytes, the replies stay inside the CAPT engine.
* Data path: `WritePipe()` feeds the LBPC parser; the CAPT engine talks over `ReadPipeTO`/`WritePipe` of the interface (queried as IOUSBInterfaceInterface182), keeps an
  RX buffer (the engine reads header and payload separately, a bulk-in transfer returns whole messages) and splits multi-frame writes into one USB transfer per CAPT frame.
* `test/tiger/harness.c` does what the usb backend does (CFPlugIn load, QueryInterface, Init(NULL), poke +0x28, Open(loc, 2), 2048-byte WritePipe pieces with a concurrent
  ReadPipe poller, Close, release) against a mock IOUSBInterfaceInterface182 backed by `test/lbpc/mockprinter.h`.

Results (G5, Tiger 10.4.11): all 18 golden scenarios: every frame the plug-in put on the mock's bulk-out pipe is byte-identical to the Linux capture (compare_wire.py, sha256 per frame);
harness self-checks pass (pipeRefs, device-ID class request incl. wIndex, no data from ReadPipe, interface open/close balanced, reference counts 0 at the end);
failure mode (the interface starts failing mid-job) -> WritePipe error, Abort, Close, release attempted, no hang, no stray processes; G5 load stayed ~1.1 throughout.

Still open before the PAUSE (step 3): activation of the codeless personality without a reboot (kextload/kextd, permissions), PPD auto-match details, what user the
usb backend runs as, the printer's real USB 1284 ID (for `*1284DeviceID`), real-hardware behaviour of ReadPipeTO with this printer.
