#!/usr/bin/env python3
"""Non-printing: replay Canon's pre-job handshake (reserve .. E1A1) with a configurable number of A0A8 polls before E1A1, then release."""
import os,sys,select,struct,time
polls=int(sys.argv[1]); delay=float(sys.argv[2])
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
frame=bytes.fromhex(open(os.path.expanduser('~/lbp6200/evidence/capture-002/job3-frames.txt')).read().split('\n')[3].split(' ')[4])
rid=bytes.fromhex(sys.argv[3])
tx(0xE1A2,bytes([0x13])+bytes(15)); tx(0xE0BA,bytes([1,0]))
for i in range(polls): tx(0xA0A8); time.sleep(delay)
tx(0xE1A1,frame[4:])
tx(0xE0A9,rid)
