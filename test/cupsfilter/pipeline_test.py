#!/usr/bin/env python3
"""Reproduce CUPS' pipeline without CUPS: raster -> rastertocapt6200 -> pipe -> backend-lbp6200 -> pty 'printer'; replies back via pipe to filter fd 3.
The pty printer checks CAPT framing strictly and answers like the real printer (slowly, optional).  usage: pipeline_test.py file.ras [accept_bytes_per_s] [e0a7_delay_s]"""
import os,sys,struct,subprocess,threading,time,tty,termios
ras=sys.argv[1];rate=float(sys.argv[2]) if len(sys.argv)>2 else 0;d7=float(sys.argv[3]) if len(sys.argv)>3 else 0
here=os.path.dirname(os.path.abspath(__file__));cups=os.path.join(here,'..','..','cups')
m,s=os.openpty();tty.setraw(m);tty.setraw(s);name=os.ttyname(s)
frames=[];log=[];errs=[];buf=b'';starts=0;serial=2;decs=0;online=False;asleep=True
def rep(c,pl=b''):os.write(m,struct.pack('<HH',c,4+len(pl))+pl)
def mock():
    global buf,starts,decs,online,asleep
    while True:
        try:d=os.read(m,4096)
        except OSError:break
        if not d:break
        if rate:time.sleep(len(d)/rate)
        buf+=d
        while len(buf)>=4:
            c,sz=struct.unpack_from('<HH',buf,0)
            if sz<4 or (c>>8) not in (0xa0,0xa1,0xa2,0xa3,0xc0,0xd0,0xe0,0xe1): errs.append('bad frame cmd=%04x size=%d'%(c,sz));buf=b'';break
            if len(buf)<sz:break
            log.append((c,sz));frames.append((c,sz,bytes(buf[:sz if os.environ.get("DUMP") else min(sz,200)])));buf=buf[sz:]
            if c==0xa0a8:
                st=bytearray(92);struct.pack_into('<HH',st,0,0xa0a8,0x5c);struct.pack_into('<H',st,4,0x8c00 if online else (0x8c12 if asleep else 0x8c10));struct.pack_into('<H',st,16,0x8000 if asleep else 0);struct.pack_into('<H',st,12,1);struct.pack_into('<H',st,14,0x0080);struct.pack_into('<4H',st,18,decs,starts,starts,starts);os.write(m,bytes(st))
            elif c==0xa2a0:rep(c,struct.pack('<HH',0,serial))
            elif c==0xa0a4:rep(c,bytes.fromhex('000c00000000000303010dfe00000000'))
            elif c==0xa3af:rep(c,bytes(2)+bytes.fromhex('ea070b0e07100403'))
            elif c in(0xc0a0,0xc0a4,0xd0a9):
                if c==0xc0a4:decs+=1
            else:
                if c==0xe0a7:time.sleep(d7);starts+=1
                if c==0xe0a5:online=True
                if c==0xe0ba:asleep=False
                rep(c,b'\0\0')
threading.Thread(target=mock,daemon=True).start()
fr,fw=os.pipe()   # filter stdout -> backend stdin
br,bw=os.pipe()   # backend fd3 -> filter fd3
env=dict(os.environ,DEVICE_URI='lbp6200:'+name)
be=subprocess.Popen([os.path.join(cups,'backend-lbp6200'),'1','u','t','1',os.environ.get('OPTS','')],stdin=fr,env=env,preexec_fn=lambda:os.dup2(bw,3),close_fds=False,stderr=subprocess.PIPE)
fl=subprocess.Popen([os.path.join(cups,'rastertocapt6200'),'1','u','t','1',os.environ.get('OPTS','')],stdin=open(ras,'rb'),stdout=fw,preexec_fn=lambda:os.dup2(br,3),close_fds=False,stderr=subprocess.PIPE)
os.close(fw);os.close(fr);os.close(bw);os.close(br)
t0=time.time();fe=fl.communicate()[1].decode();rc=fl.returncode;
try: be.wait(timeout=15)
except Exception: be.kill();errs.append('BACKEND DID NOT EXIT')
beerr=be.stderr.read().decode()
print('filter rc',rc,'%.1fs'%(time.time()-t0));print(fe[-400:]);print(beerr[-600:]);print('frames',len(log),'errors',errs)
import collections;print(collections.Counter('%04x'%c for c,_ in log).most_common(6));print('cmds',' '.join('%04x'%c for c,_ in log if c not in(0xa0a8,0xa0a4,0xa3af)))

if os.environ.get('DUMP'):
    open(os.environ['DUMP'],'w').write(''.join('%04x %d %s\n'%(c,sz,b.hex()) for c,sz,b in frames))
