#!/usr/bin/env python3
"""decode a mock-printer rx*.bin (raw TX packets from Canon's monitor): print one job (up to first E0A9) without polls"""
import sys,struct
b=open(sys.argv[1],'rb').read();o=0;n=0;skip={0xa0a8,0xa0a4,0xa3af}
full=len(sys.argv)>2
while o+4<=len(b):
    c,s=struct.unpack_from('<HH',b,o);p=b[o+4:o+s];o+=s
    if c in skip: continue
    print('%04x %5d %s'%(c,s,p[:(200 if full else 24)].hex()))
    if c==0xe0a9: break
