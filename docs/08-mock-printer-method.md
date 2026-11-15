# Mock-printer method (Nov 14): run Canon's REAL monitor with no hardware/paper
tools/mockprinter_pty.py creates a pty, symlinks the slave as scratch/mock/lp0 and answers CAPT packets with a state machine derived from the
real capture (evidence/capture-002/a0a8-distinct.txt). /etc/ccpd.conf DevicePath -> that symlink (RESTORED to /dev/usb/lp0 afterwards).
tools/mock-restart.sh restarts mock + ccpd (run as sam via fakeeuid shim); tools/mock-variants3.sh submits CUPS jobs with options and saves the
packet log (evidence/mockwire-0xx; mockwire-clean = title-verified runs). Compare with tools/mockcmp.py.
Gotchas: jobs lag/alternate (every 2nd submit lost) - verify the job title inside E1A1 text; never `pkill -f`/`pgrep -f` with the script name inside
the same command line (kills your own shell; use script files); ccp backend by hand needs argv[0]=ccp, PRINTER=LBP6200 and a real CUPS job id.
## Validated (single page, wire level)
* Our core reproduces the real simplex capture byte-for-byte (16/16 frames).
* wire D0A0 payload = ext-stream D0A0 with [12]=media class (plain 1, thick 4, labels 0x28) and [0]=mode (1 normal, 2 draft, 0 halftone-off);
  [4] paper, [8] density, [26..33] geometry, [36] media code as in docs/06.
* D0A4: normal 01 04 01 01 00 f0..; draft 03 0c .. f1; halftone off 02 0c ..
* Bands >64K are split into C0A0 packets of 0xff00 payload (draft page: 65284 + 4004).
* E1A1 size depends on name length; name = host+user+title UTF-16LE at payload offset 72.
## NOT validated (mock does not do flow control): multi-page and duplex
Canon's monitor in a 2-page duplex job: D0A0[0]=0, D0A0[5]=4 (front)/2 (back), E0A7 payload 04, next page's data sent before E0A7. Needs real hardware.
