#!/usr/bin/env python3
"""Non-printing: reserve (retry), E1A2, E0BA, then E1A1 with the u32 at payload[4] tried as several candidates; release."""
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
rx(0.2)
frame=bytes.fromhex(open(os.path.expanduser('~/lbp6200/evidence/capture-002/job3-frames.txt')).read().split('\n')[3].split(' ')[4])[4:]
for t in range(4):
    tx(0xA0A4,q=True);tx(0xA3AF,q=True)
    r=tx(0xA2A0,bytes.fromhex('00001e0000000000'))
    if r[4:6]==b'\0\0':break
    time.sleep(0.3)
rid=r[6:8];N=struct.unpack('<H',rid)[0];print('id',N)
tx(0xE1A2,bytes([0x13])+bytes(15));tx(0xE0BA,bytes([1,0]))
for c in [1,2,3,0x56,0x57,0x58,0x100,7,8]:
    f=bytearray(frame);struct.pack_into('<I',f,4,c)
    r=tx(0xE1A1,bytes(f));print(' cand',c)
    if r[4:6]==b'\0\0':break
tx(0xE0A9,rid)
