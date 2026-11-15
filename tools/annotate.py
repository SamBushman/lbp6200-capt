#!/usr/bin/env python3
"""Print one decompiled function with FUN_<addr> callees replaced by recovered C++ names.
usage: annotate.py <dump.c> <addr-hex> [maxlines]"""
import re, subprocess, sys, os
dump, addr = sys.argv[1], sys.argv[2]
maxl = int(sys.argv[3]) if len(sys.argv) > 3 else 400
names = {}
sigs = subprocess.run([os.path.expanduser('~/lbp6200/tools/engsigs.py'), dump], capture_output=True, text=True).stdout
for line in sigs.splitlines():
    m = re.match(r'([0-9a-f]+)\s+(\S+)\s+(?:LONG|BOOL|void|virtual void)\s+(.+?)\(', line)
    if m: names[m.group(1)] = m.group(3).replace('C_Port::', '').replace(' ', '')
src = open(dump, errors='replace').read()
m = re.search(r'(?m)^// ==== \S+ @ %s ====\n(.*?)(?=^// ==== )' % addr, src, re.S)
body = m.group(1)
body = re.sub(r'FUN_0*([0-9a-f]{8})', lambda mm: names.get(mm.group(1), mm.group(0)), body)
print('\n'.join(body.splitlines()[:maxl]))
