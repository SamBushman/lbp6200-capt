#!/usr/bin/env python3
"""Resolve VM addresses to C strings / bytes in a thin or fat PPC Mach-O.  usage: macho_str.py FILE ADDR [ADDR...]"""
import struct, sys
def segs(d):
    off = 0
    if d[:4] == b'\xca\xfe\xba\xbe':
        n = struct.unpack('>I', d[4:8])[0]
        for i in range(n):
            ct, cs, o, sz, al = struct.unpack('>5I', d[8+20*i:28+20*i])
            if ct == 18: off = o; break      # CPU_TYPE_POWERPC
    assert d[off:off+4] == b'\xfe\xed\xfa\xce', 'not 32-bit BE Mach-O'
    ncmds = struct.unpack('>I', d[off+16:off+20])[0]
    p = off + 28; out = []
    for _ in range(ncmds):
        cmd, size = struct.unpack('>II', d[p:p+8])
        if cmd == 1:
            name = d[p+8:p+24].rstrip(b'\0').decode(); vm, vsz, fo, fsz = struct.unpack('>4I', d[p+24:p+40])
            out.append((name, vm, vsz, fo + off, fsz))
        p += size
    return out
def read(d, S, a, n=200):
    for name, vm, vsz, fo, fsz in S:
        if vm <= a < vm + vsz and fsz: return d[fo + a - vm: fo + a - vm + n]
if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read(); S = segs(d)
    for a in sys.argv[2:]:
        b = read(d, S, int(a, 16))
        print(a, repr(b.split(b'\0')[0]) if b else None)
