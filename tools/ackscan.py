#!/usr/bin/env python3
"""Non-printing: find the job id the printer wants acknowledged (A2A0 payload[0]=k), then run Canon's post-page handshake and release."""
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
def tx(cmd,pl=b'',q=False):
    os.write(fd,struct.pack('<HH',cmd,4+len(pl))+pl);r=rx()
    if not q:print('%04X'%cmd,pl[:6].hex(),'->',r[4:8].hex(),flush=True)
    return r
def s0():
    r=tx(0xA0A8,q=True);return struct.unpack_from('<H',r,4)[0]
rx(0.2)
L=open(os.path.expanduser('~/lbp6200/evidence/capture-002/job3-frames.txt')).read().split('\n')
f2=[bytes.fromhex(l.split(' ')[4]) for l in L if ' TX E1A1 ' in l][1][4:]
print('S0 %04x'%s0())
for k in range(0,24):
    tx(0xA0A4,q=True);tx(0xA3AF,q=True)
    r=tx(0xA2A0,struct.pack('<H',k)+bytes.fromhex('1e0000000000'),q=True)
    st=struct.unpack_from('<H',r,4)[0]
    print('k',k,'status %02x'%st,'id',r[6:8].hex(),'S0 %04x'%s0())
    if st==0:
        rid=r[6:8]
        if k:
            tx(0xE1A2,bytes.fromhex('31000202010000000000080000000000'));tx(0xA3AF)
            tx(0xE1A1,f2);tx(0xE0A2);tx(0xE0A3);tx(0xE1A1,f2)
        tx(0xE0A9,rid);print('S0 %04x'%s0())
        if k:break
