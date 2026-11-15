#!/usr/bin/env python3
"""Decode a strace of Canon's monitor (fd 4 = /dev/usb/lp0) into a CAPT command sequence.
usage: straceseq.py <strace-file> [start-time HH:MM:SS]"""
import re, struct, sys, subprocess, os
names = {}
for l in subprocess.run([os.path.expanduser('~/lbp6200/tools/cmdnames.py'),
        os.path.expanduser('~/lbp6200/ghidra/out/libcncaptnpm.so.2.0.1.c')], capture_output=True, text=True).stdout.splitlines():
    c, n = l.split(None, 1); names[int(c, 16)] = n.strip()
names.update({0xA0A4: 'PC_GET_CARTRIDGE_STATUS', 0xA3AF: 'PC_GET_TIME?', 0xE1A1: 'PCR_SET_JOB_INFO?', 0xE1A2: 'PCR_SET_LED_STATUS',
              0xE2A2: 'PCR_ANNOUNCE_PAGE?', 0xD0A3: 'IC_?D0A3', 0xD0A5: 'IC_CYAN_PLANE', 0xD0A7: 'IC_YELLOW_PLANE', 0xC0A6: 'IC_MAGENTA_END'})
start = sys.argv[2] if len(sys.argv) > 2 else '00:00:00'
ev = []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'([\d:.]+) (write|read)\(4, "(.*)", \d+\)\s+= (-?\d+)', line)
    if not m or m.group(1) < start: continue
    t, op, d, ret = m.groups()
    ev.append((t, op, bytes(int(x, 16) for x in re.findall(r'\\x([0-9a-f]{2})', d))))
out = []; i = 0; poll = 0
def nm(c): return names.get(c, '?')
while i < len(ev):
    t, op, b = ev[i]
    if op == 'write' and len(b) >= 4:
        c, sz = struct.unpack_from('<HH', b)
        # gather reply reads until next write
        rd = b''; j = i + 1
        while j < len(ev) and ev[j][1] == 'read': rd += ev[j][2]; j += 1
        if c == 0xA0A8:
            poll += 1; i = j; continue
        if poll: out.append('   ... %d x PC_GET_EXTENDED_STATUS2 (A0A8) polls' % poll); poll = 0
        pl = b[4:]
        out.append('%s TX %04X %-26s size=%-5d %s' % (t[:12], c, nm(c), len(b), pl[:40].hex() + ('..' if len(pl) > 40 else '')))
        if rd: out.append('%s    RX %-4d %s' % (t[:12], len(rd), rd[:40].hex() + ('..' if len(rd) > 40 else '')))
        i = j
    else: i += 1
if poll: out.append('   ... %d x A0A8 polls' % poll)
print('\n'.join(out))
