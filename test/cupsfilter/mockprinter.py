#!/usr/bin/env python3
"""Mock CAPT printer for the CUPS filter: runs the filter, parses its stdout packets, answers on fd 3.
usage: mockprinter.py out_prefix filter args... < raster"""
import os,sys,struct,subprocess,select
pre=sys.argv[1];cmd=sys.argv[2:]
r3,w3=os.pipe()
p=subprocess.Popen(cmd,stdin=sys.stdin,stdout=subprocess.PIPE,stderr=subprocess.PIPE,pass_fds=(r3,),
    preexec_fn=lambda:os.dup2(r3,3) if r3!=3 else None)
os.close(r3)
buf=b'';log=[];data=b'';starts=0;pages_data=[]
def status():
    s=bytearray(92);struct.pack_into('<HH',s,0,0xa0a8,0x5c)
    struct.pack_into('<H',s,4,0x0000);                       # STATUS0 ready
    struct.pack_into('<H',s,4+10,0x0080)                     # payload+10 bit7 = paper present in slot 0 (real captures: always 0x0080 with paper loaded)
    struct.pack_into('<HHH',s,4+14,starts,starts,starts);struct.pack_into('<H',s,4+20,starts)
    return bytes(s)
def reply(c,pl=b''):
    os.write(w3,struct.pack('<HH',c,4+len(pl))+pl)
import threading
err=[]
threading.Thread(target=lambda:err.append(p.stderr.read()),daemon=True).start()
while True:
    ch=os.read(p.stdout.fileno(),65536)
    if not ch:break
    buf+=ch
    while len(buf)>=4:
        c,s=struct.unpack_from('<HH',buf,0)
        if len(buf)<s:break
        pk=buf[:s];buf=buf[s:];log.append((c,s))
        if c==0xc0a0: data+=pk[4:]
        elif c==0xc0a4: pages_data.append(data);data=b''
        elif c==0xa0a8: os.write(w3,status())
        elif c==0xa2a0: reply(0xa2a0,struct.pack('<HH',0,2))
        elif c==0xa0a4: reply(0xa0a4,bytes.fromhex('000c00000000000303010dfe00000000')[0:16])
        elif c>=0xe000:
            if c==0xe0a7: starts+=1
            reply(c,b'\0\0')
rc=p.wait()
for i,d in enumerate(pages_data): open('%s.page%d.band'%(pre,i+1),'wb').write(b'\0\0\0\0'+d)
open(pre+'.log','w').write('\n'.join('%04x %d'%x for x in log))
sys.stderr.write(b''.join(err).decode(errors='replace'))
print('filter rc=%d, %d packets, %d pages of band data (%s)'%(rc,len(log),len(pages_data),[len(d) for d in pages_data]))
