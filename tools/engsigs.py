#!/usr/bin/env python3
"""List C++ method signatures recovered from __assert_fail strings, with command words used."""
import re, sys
src = open(sys.argv[1], errors='replace').read()
fs = re.split(r'(?m)^// ==== (\S+) @ (\S+) ====$', src)
rows = {}
for i in range(1, len(fs), 3):
    body = fs[i+2]
    for m in re.finditer(r'__assert_fail\(\s*"(?:[^"\\]|\\.)*",\s*"([^"]+)",\s*(?:0x[0-9a-f]+|\d+),\s*"([^"]+)"\s*\)', body):
        sig = m.group(2)
        cmds = sorted(set(c.lower() for c in re.findall(r'=\s*(0x[a-fA-F][0-9a-fA-F]{3});', body)))
        rows.setdefault(sig, (fs[i+1], m.group(1), cmds))
for sig, (a, f, c) in sorted(rows.items(), key=lambda kv: (kv[1][1], kv[1][0])):
    print('%s %-14s %s%s' % (a, f.lstrip('./'), sig, ('  cmds=' + ','.join(c)) if c else ''))
print(len(rows), 'signatures', file=sys.stderr)
