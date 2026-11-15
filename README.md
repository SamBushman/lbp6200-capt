# lbp6200-capt

Open-source CUPS driver for the Canon LBP6200 / LBP6200d (CAPT 3.0) laser printer, written from the protocol (no Canon code), with the aim of
building from one source tree for **Linux** (working, tested on hardware: simplex and long-edge duplex) and **Mac OS X 10.4 Tiger on PowerPC**
(in progress, see `docs/13-tiger-port-design.md`).

    core/     platform-neutral CAPT protocol engine (capt.c) - talks through struct capt_io callbacks only
    hiscoa/   Hi-SCoA band compressor (from captdriver, GPL-3)
    cups/     CUPS raster filter (page producer in rasterpage.c, Linux front end rastertocapt6200.c), Linux backend for /dev/usb/lpN
    status/   read-only status / toner / problem tool
    ppd/      PPD            packaging/  rpm spec
    test/     unit and pipeline tests, golden wire captures (test/golden), big-endian PPC emulation test (test/ppc)
    docs/     protocol reverse-engineering notes, status decode, job sequence, install notes, Tiger port design
    tools/    capture analysis, mock printer, regression baseline harness (tools/baseline)

Build (Linux): `make -C cups && make -C status`; tests: `make -C test check`.  Install notes: `doc/INSTALL-bazzite.md`.
Licence: GPL-3 (see COPYING).
