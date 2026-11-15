#!/usr/bin/env python3
"""Compare a raw wire dump (concatenated CAPT frames, as written by harness) with a golden scenario: usage compare_wire.py WIRE GOLDEN.json"""
import sys, struct, hashlib, json
POLLS = {0xa0a8, 0xa0a4, 0xa3af}
w = open(sys.argv[1], 'rb').read(); g = json.load(open(sys.argv[2]))
fr = []; o = 0
while o + 4 <= len(w):
    c, sz = struct.unpack_from('<HH', w, o)
    if sz < 4 or o + sz > len(w): print('bad frame at', o); sys.exit(1)
    if c not in POLLS: fr.append([c, sz, hashlib.sha256(w[o:o + sz]).hexdigest()])
    o += sz
gold = [x[:3] for x in g['frames']]
if fr == gold: print('wire IDENTICAL to golden (%d frames)' % len(fr)); sys.exit(0)
print('DIFFER: %d frames vs golden %d' % (len(fr), len(gold)))
for i, (a, b) in enumerate(zip(fr, gold)):
    if a != b: print(' first difference at frame', i, a[:2], b[:2]); break
sys.exit(1)
