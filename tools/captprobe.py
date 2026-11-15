#!/usr/bin/env python3 -I
"""Send ONE non-printing CAPT query to /dev/usb/lp0 and hex-dump the reply.
usage: captprobe.py <cmd-hex> [payload-hex]   e.g. captprobe.py A0A8
Refuses anything not in the read-only allowlist."""
import os, sys, select, struct, time
ALLOW = {0xA0A0,0xA0A1,0xA0A2,0xA0A3,0xA0A4,0xA0A5,0xA0A7,0xA0A8,0xA1A1,0xA1A2,0xA1A3,0xA1A4,
         0xA1A5,0xA1A8,0xA1AA,0xA2A1,0xA2A2,0xA2A3,0xA2A4,0xA2A7,0xA3A0,0xA3AA,0xA3AB,0xA3AC,0xE0A0}
cmd = int(sys.argv[1], 16)
payload = bytes.fromhex(sys.argv[2]) if len(sys.argv) > 2 else b''
if cmd not in ALLOW: sys.exit('command 0x%04X not in read-only allowlist' % cmd)
pkt = struct.pack('<HH', cmd, 4 + len(payload)) + payload
fd = os.open('/dev/usb/lp0', os.O_RDWR | os.O_NONBLOCK)
# drain any stale input first
while select.select([fd], [], [], 0.2)[0]:
    try: stale = os.read(fd, 4096)
    except BlockingIOError: break
    if not stale: break
    print('stale:', stale.hex())
print('TX', pkt.hex())
os.write(fd, pkt)
buf = b''; t0 = time.time()
while time.time() - t0 < 3:
    if select.select([fd], [], [], 0.3)[0]:
        try: d = os.read(fd, 4096)
        except BlockingIOError: continue
        if not d: break
        buf += d
        if len(buf) >= 6 and len(buf) >= struct.unpack_from('<H', buf, 2)[0] and time.time() - t0 > 0.3: break
print('RX', len(buf), 'bytes:', buf.hex())
os.close(fd)
