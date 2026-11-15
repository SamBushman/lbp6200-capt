#!/usr/bin/env python3
"""Compare two LBPC streams record by record, ignoring JOB_BEGIN (time/host/user differ between machines): cmp_records.py A B"""
import sys, struct
def recs(p):
    d = open(p, 'rb').read(); assert d[:4] == b'LBPC'; o = 8; r = []
    while o < len(d):
        t, _, n = struct.unpack_from('<HHI', d, o); r.append((t, d[o + 8:o + 8 + n])); o += 8 + n
    return r
a, b = recs(sys.argv[1]), recs(sys.argv[2])
ia = [x for x in a if x[0] != 1]; ib = [x for x in b if x[0] != 1]
if ia == ib: print('records IDENTICAL (%d records, %d bytes of page data)' % (len(ia), sum(len(x[1]) for x in ia if x[0] == 3))); sys.exit(0)
print('DIFFER', len(ia), len(ib)); sys.exit(1)
