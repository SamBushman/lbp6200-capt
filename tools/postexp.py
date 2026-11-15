#!/usr/bin/env python3
"""Experiment: after a job, poll A0A8; when ejecting finishes (S1 bit 0x0100 seen / printing seen then idle), run Canon's post-page handshake. Logs timeline."""
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
    b=tx(0xA0A8,q=True)[4:];return [struct.unpack_from('<H',b,o)[0] for o in (0,8,10,12,14,16,18,20)]
L=open(os.path.expanduser('~/lbp6200/evidence/capture-002/job3-frames.txt')).read().split('\n')
f2=bytearray([bytes.fromhex(l.split(' ')[4]) for l in L if ' TX E1A1 ' in l][1][4:])
rx(0.2);t0=time.time();last=None;seen_print=False
while time.time()-t0<60:
    s=st();k=tuple(s[:4])
    if k!=last:print('%5.1f S0 %04x S1 %04x S2 %04x S3 %04x ctr %s'%(time.time()-t0,*s[:4],s[4:]),flush=True);last=k
    if s[1]&0x4000 or s[1]&0x0004: seen_print=True
    if seen_print and not(s[1]&0x4000) and s[3]&0x0080==0: break
    time.sleep(0.25)
print('--- post-page handshake')
for t in range(3):
    tx(0xA0A4,q=True);tx(0xA3AF,q=True);r=tx(0xA2A0,bytes.fromhex('02001e0000000000'))
    if r[4:6]==b'\0\0':break
    time.sleep(0.3)
rid=r[6:8];N=struct.unpack('<H',rid)[0]
if r[4:6]==b'\0\0':
    struct.pack_into('<H',f2,18,N)
    tx(0xE1A2,bytes.fromhex('31000202010000000000080000000000'));tx(0xA3AF)
    tx(0xE1A1,bytes(f2));tx(0xE0A2);tx(0xE0A3);tx(0xE1A1,bytes(f2))
    tx(0xE0A9,rid)
print('final',['%04x'%x for x in st()[:4]])
