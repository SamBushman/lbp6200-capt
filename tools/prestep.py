#!/usr/bin/env python3
"""Non-printing pre-job step: reserve(retry), E0A4 discard, E0A1 reset engine, release; print status before/after."""
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
    os.write(fd,struct.pack('<HH',cmd,4+len(pl))+pl);return rx()
def st():
    b=tx(0xA0A8)[4:];return 'S0 %04x S1 %04x ctr %s'%(struct.unpack_from('<H',b,0)[0],struct.unpack_from('<H',b,8)[0],[struct.unpack_from('<H',b,o)[0] for o in (14,16,18,20)])
rx(0.2);print('before',st())
for t in range(4):
    tx(0xA0A4);tx(0xA3AF);r=tx(0xA2A0,bytes.fromhex('00001e0000000000'))
    if r[4:6]==b'\0\0':break
    time.sleep(0.3)
rid=r[6:8]
for c in (0xE0A4,0xE0A1):
    r=tx(c);print('%04X'%c,r[4:6].hex())
time.sleep(1);tx(0xE0A9,rid);time.sleep(1);print('after',st())
