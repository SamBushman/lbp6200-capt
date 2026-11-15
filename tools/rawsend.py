#!/usr/bin/env python3
"""Send a sequence of raw CAPT packets (non-printing commands only; caller's responsibility) and dump replies.
usage: rawsend.py CMD[:payloadhex] ...   (e.g. A0A4 A3AF E0A9:0200)"""
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
rx(0.2)
for a in sys.argv[1:]:
    c,_,p=a.partition(':');cmd=int(c,16);pl=bytes.fromhex(p)
    os.write(fd,struct.pack('<HH',cmd,4+len(pl))+pl);print('TX',a,'RX',rx().hex())
