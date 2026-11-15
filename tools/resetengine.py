#!/usr/bin/env python3
"""Non-printing: reserve (retry), E0A3 clear misprint, E0A4 discard data, release; show status before/after."""
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
def st():
    r=tx(0xA0A8,q=True);b=r[4:];print('S0 %04x S1 %04x S2 %04x S3 %04x ctr'%tuple(struct.unpack_from('<H',b,o)[0] for o in (0,8,10,12)),[struct.unpack_from('<H',b,o)[0] for o in (14,16,18,20)])
rx(0.2);st()
for t in range(4):
    tx(0xA0A4,q=True);tx(0xA3AF,q=True);r=tx(0xA2A0,bytes.fromhex('00001e0000000000'))
    if r[4:6]==b'\0\0':break
    time.sleep(0.3)
rid=r[6:8]
tx(0xE0A1);time.sleep(3);st();tx(0xE0A9,rid);st()
