#!/usr/bin/env python3
"""Non-printing: replay Canon's SECOND (post-page) handshake from capture-002 and release, to clear a dangling job."""
import os,sys,select,struct,time
fd=os.open('/dev/usb/lp0',os.O_RDWR|os.O_NONBLOCK)
def rx(t=2.0):
    buf=b'';t0=time.time()
    while time.time()-t0<t:
        if select.select([fd],[],[],0.2)[0]:
            try:d=os.read(fd,4096)
            except BlockingIOError:continue
            if not d:break
            buf+=d
            if len(buf)>=4 and len(buf)>=struct.unpack_from('<H',buf,2)[0]:break
    return buf
def tx(cmd,pl=b''):
    os.write(fd,struct.pack('<HH',cmd,4+len(pl))+pl);r=rx();print('%04X'%cmd,pl[:6].hex(),'->',r[4:8].hex(),flush=True);return r
rx(0.2)
L=open(os.path.expanduser('~/lbp6200/evidence/capture-002/job3-frames.txt')).read().split('\n')
f2=[bytes.fromhex(l.split(' ')[4]) for l in L if ' TX E1A1 ' in l][1]
tx(0xA0A4);tx(0xA3AF)
r=tx(0xA2A0,bytes.fromhex('02001e0000000000')); rid=r[6:8]
tx(0xE1A2,bytes.fromhex('31000202010000000000080000000000'));tx(0xA3AF)
tx(0xE1A1,f2[4:]);tx(0xE0A2);tx(0xE0A3);tx(0xE1A1,f2[4:])
tx(0xA0A8);tx(0xE0A9,rid);tx(0xA0A8)
