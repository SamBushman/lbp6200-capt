#!/usr/bin/env python3
"""List functions in a Ghidra decompile dump that call the CAPT transact primitive."""
import re, sys
src = open(sys.argv[1], errors='replace').read()
prim = sys.argv[2] if len(sys.argv) > 2 else 'FUN_10008e10'
funcs = re.split(r'(?m)^// ==== (\S+) @ (\S+) ====$', src)
# funcs: [pre, name, addr, body, name, addr, body...]
rows = []
for i in range(1, len(funcs), 3):
    name, addr, body = funcs[i], funcs[i+1], funcs[i+2]
    if prim + '(' not in body: continue
    cmds = set(re.findall(r'=\s*(0x[0-9a-fA-F]{1,2}[0-9a-fA-F]{2})\b(?=;)', body))
    cmds = {c for c in cmds if re.match(r'0x[a-fA-F][0-9a-fA-F]{3}$', c)}
    m = re.search(prim + r'\(\w+,[^,]+,(0x[0-9a-f]+|\w+),', body)
    rows.append((addr, name, sorted(cmds), m.group(1) if m else '?', len(body.splitlines())))
for r in sorted(rows): print('%s %-14s cmds=%-28s len=%-6s lines=%d' % (r[0], r[1], ','.join(r[2]), r[3], r[4]))
print(len(rows), 'callers', file=sys.stderr)
