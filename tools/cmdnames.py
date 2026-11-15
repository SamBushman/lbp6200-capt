#!/usr/bin/env python3
"""Extract (code,name) pairs from Ghidra's decompile of cmdCode2str."""
import re, sys
src = open(sys.argv[1], errors='replace').read()
m = re.search(r'(?m)^// ==== cmdCode2str @ \S+ ====\n(.*?)(?=^// ====)', src, re.S)
body = m.group(1)
pairs = {}
# pattern A: if (v == 0xXXXX) { return "NAME"; }
for c, n in re.findall(r'==\s*(0x[0-9a-fA-F]+)\)\s*\{\s*return\s+"([A-Z0-9_]+)"', body):
    pairs[int(c, 16)] = n
# pattern B: pcVar = "NAME"; bVar5 = v == 0xXXXX;
for n, c in re.findall(r'=\s*"([A-Z0-9_]+)";\s*bVar\d\s*=\s*\w+\s*==\s*(0x[0-9a-fA-F]+)', body):
    pairs[int(c, 16)] = n
for c in sorted(pairs): print('0x%04X  %s' % (c, pairs[c]))
