#!/usr/bin/env python3
"""[fault-injection copy of mockprinter_pty.py: env MOCK_FAULT="cmdhex:silent|status=HEX[:skip[:count]];..." MOCK_STALE=<jobid> MOCK_C0A0_DELAY=<s>]
Pty-based mock of the LBP6200 for driving Canon's real captmoncnabd WITHOUT hardware or paper.
Creates a pty, symlinks the slave at argv[1], answers CAPT packets with a state machine derived from the real capture,
and logs every received packet (raw, in order) to argv[2] (binary: u16 cmd,u16 size,payload ...) plus a text log argv[2].txt.
Run until killed."""
import os,pty,sys,time,struct,termios,tty,select
link,logp=sys.argv[1],sys.argv[2]
m,s=pty.openpty();tty.setraw(s);tty.setraw(m)
slave=os.ttyname(s)
if os.path.islink(link) or os.path.exists(link): os.unlink(link)
os.symlink(slave,link);os.chmod(slave,0o666)
st=dict(S0=0x8c33,S1=1,S2=0x80,S3=0x8000,S4=0x56,job=0,n5=1,recv=0,dec=0,prt=0,out=0,inj=0,ev=1,pages=0,t0=None)
FAULTS=[]
for r in os.environ.get('MOCK_FAULT','').split(';'):
    if r:
        p=r.split(':');FAULTS.append(dict(cmd=int(p[0],16),act=p[1],skip=int(p[2]) if len(p)>2 else 0,count=int(p[3]) if len(p)>3 else 999,seen=0))
if os.environ.get('MOCK_STALE'): st['job']=int(os.environ['MOCK_STALE'])
C0D=float(os.environ.get('MOCK_C0A0_DELAY','0'))
raw=open(logp,'wb');txt=open(logp+'.txt','w')
ID=bytes.fromhex('a1a14000000be3090101f0ff000124005d0001031806c0036f086f08e40d0000e40d0000fa023408f604ea0a32323232580258021e0704000010000000000000')
def A0A8():
    t=st['t0']
    if t is not None:
        d=time.time()-t
        if d>2: st['prt']=st['pages']
        if d>3.5: st['S0']=0x8c13;st['S1']=0x4085;st['out']=st['pages']
        if d>6: st['S1']=0x4001
        if d>7: st['S0']=0x8c10;st['S1']=0x0001
    p=bytearray(88)
    if st.get('jam'): pass
    struct.pack_into('<H',p,0,st['S0']);p[2:8]=bytes.fromhex('00000f000000')[0:6] if False else b'\0\0\x0f\0\0\0'
    struct.pack_into('<HHH',p,8,st['S1'],st['S2'],st['S3'])
    struct.pack_into('<HHHH',p,14,st['dec'],st['prt'],st['out'],0)
    struct.pack_into('<HHH',p,24,st['S4'],0,st['job']);p[33]=st['n5'];struct.pack_into('<H',p,34,st['recv'])
    struct.pack_into('<H',p,40,st['inj']);struct.pack_into('<H',p,64,st['ev']);p[32]=0x55
    if st.get('jam'): p[13]|=0x01          # payload+13 bit0 = JAM (capt_status_conditions)
    return struct.pack('<HH',0xa0a8,92)+bytes(p)
def rep(c,pl=b''):
    os.write(m,struct.pack('<HH',c,4+len(pl))+pl)
buf=b'';npk=0
print('mock ready at',link,'->',slave,flush=True)
while True:
    if not select.select([m],[],[],0.5)[0]:
        continue
    try: ch=os.read(m,65536)
    except OSError: time.sleep(0.05);continue
    buf+=ch
    while len(buf)>=4:
        c,sz=struct.unpack_from('<HH',buf,0)
        if sz<4:
            txt.write('BAD size %d at cmd %04x, dropping 1 byte\n'%(sz,c));buf=buf[1:];continue
        if len(buf)<sz:break
        pk=buf[:sz];buf=buf[sz:];raw.write(pk);raw.flush();npk+=1
        txt.write('%.3f %04x %d %s\n'%(time.time(),c,sz,pk[4:40].hex()));txt.flush()
        fa=None
        for f in FAULTS:
            if f['cmd']==c:
                f['seen']+=1
                if f['seen']>f['skip'] and f['seen']<=f['skip']+f['count']: fa=f['act']
        if fa:
            txt.write('   FAULT %s on %04x\n'%(fa,c));txt.flush()
            if fa.startswith('status='): rep(c,struct.pack('<H',int(fa[7:],16)))
            continue
        if c==0xc0a0 and C0D: time.sleep(C0D)
        if c==0xa0a8: os.write(m,A0A8())
        elif c==0xa0a4: rep(c,bytes.fromhex('000c00000000000303010dfe00000000'))
        elif c==0xa3af: rep(c,b'\0\0\0\0'+bytes.fromhex('ea070b0d17062d03'))
        elif c==0xa1a0: rep(c,b'\0\0MFG:Canon;MDL:LBP6200;CMD:CAPT;VER:3.0;CLS:PRINTER;DES:Canon LBP6200')
        elif c==0xa1a1: os.write(m,ID)
        elif c==0xa1a8: rep(c,b'\0\0DCON:04.04\0\0'[0:14])
        elif c==0xa0a1: rep(c,bytes.fromhex('0001c0000000000000009600fd0000000000'))
        elif c==0xa2a1: rep(c,bytes.fromhex('00010400'))
        elif c==0xa2a0:
            rid=struct.unpack_from('<H',pk,4)[0] or 2;st['S0']=0x8c32;rep(c,struct.pack('<HH',0,rid))
        elif c in(0xa0a0,0xa0a2,0xa1aa): rep(c,b'\x80\x00')
        elif c==0xe1a1:
            if st['S0']==0x8c32: st['S0']=0x8c30;st['S3']=0;st['job']=2
            rep(c,b'\0\0')
        elif c==0xe0a5: st['S0']=0x8c00;st['n5']=2;rep(c,b'\0\0')
        elif c==0xe0a7:
            if os.environ.get('MOCK_JAM_ON_E0A7'): st['jam']=1
            st['pages']+=1;st['S1']=0x85;st['t0']=time.time();st['recv']=st['dec']=st['pages'];rep(c,b'\0\0')
        elif c==0xc0a0: st['S1']=0x81;st['recv']=st['dec']=st['pages']+1
        elif c==0xe0a9:
            st.update(S0=0x8c13,S1=1,job=0,inj=0,t0=None);rep(c,b'\0\0')
        elif c>=0xe000: rep(c,b'\0\0')
