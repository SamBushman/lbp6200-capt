#!/usr/bin/env python3
"""Offline proof harness: CUPS raster -> rastertocapt6200 -> {stock-usb model | real lbp6200 backend} -> mock LBP6200.

Printer rule (hardware-proven, evidence: memory 'CUPS queue wedge ROOT CAUSE', backend-lbp6200.c header): the printer treats one USB message
(bulk transfer(s) ended by a short packet, i.e. a write whose length is not a multiple of 512) as ONE command.  A message that holds
more than one CAPT frame (or a partial one) is not answered and the printer wedges until power-cycled.

mode stock : model of CUPS' built-in usb backend: read(stdin, 8192) -> one libusb bulk write per read, no framing.  The mock applies the rule
             above live and goes silent on a violation, exactly like the printer.  --xfer-ms = time one bulk write takes (the filter keeps
             writing meanwhile, which is what makes pipe reads coalesce).
mode lbp   : the REAL installed backend binary, device = pty.  strace records the write() sizes it issues to the device; the same rule is
             applied post-hoc (a pty cannot show message boundaries).
usage: transport_proof.py stock|lbp FILTER BACKEND RAS [--xfer-ms N] [--out DIR] [--opts STR]"""
import fcntl,os,sys,struct,subprocess,threading,time,tty,re,argparse,tempfile
ap=argparse.ArgumentParser();ap.add_argument('mode');ap.add_argument('filt');ap.add_argument('backend');ap.add_argument('ras')
ap.add_argument('--xfer-ms','--xfer_ms',dest='xfer_ms',type=float,default=1.0);ap.add_argument('--opts',default='');ap.add_argument('--out',default=None);ap.add_argument('--chunk',type=int,default=8192);ap.add_argument('--term-after',type=float,default=0)
a=ap.parse_args()
MSG_MULT=512

class Printer:
    """CAPT answers copied from src/test/cupsfilter/pipeline_test.py (state machine good enough for a full simplex job)."""
    def __init__(s,send):
        s.send=send;s.starts=0;s.decs=0;s.online=False;s.asleep=True;s.serial=2;s.wedged=False;s.log=[];s.viol=[];s.e1a1=[];s.errst=dict(x.split(':') for x in os.environ.get('ERRST','').split(',') if x);s.delay=dict(x.split(':') for x in os.environ.get('DELAY','').split(',') if x);s.t0=time.time();s.times=[]
    def rep(s,c,pl=b''): s.send(struct.pack('<HH',c,4+len(pl))+pl)
    def frame(s,pk):
        c,sz=struct.unpack_from('<HH',pk,0);s.log.append((c,sz));s.times.append(time.time()-s.t0)
        if c==0xe1a1 and len(pk)>20: s.e1a1.append(pk[4+16])
        if '%04x'%c in s.delay: time.sleep(float(s.delay['%04x'%c]))
        if '%04x'%c in s.errst and not s.wedged: s.rep(c,struct.pack('<H',int(s.errst['%04x'%c],16)));return
        if s.wedged: return
        if '%04x'%c in os.environ.get('DROP','').split(','): return      # fault injection: printer never answers this command
        if c==0xa0a8:
            st=bytearray(92);struct.pack_into('<HH',st,0,0xa0a8,0x5c);struct.pack_into('<H',st,4,0x8c00 if s.online else (0x8c12 if s.asleep else 0x8c10))
            struct.pack_into('<H',st,16,0x8000 if s.asleep else 0);struct.pack_into('<H',st,12,1);struct.pack_into('<H',st,14,0x0080)
            struct.pack_into('<4H',st,18,s.decs,s.starts,s.starts,s.starts);s.send(bytes(st))
        elif c==0xa2a0: s.rep(c,struct.pack('<HH',0,s.serial))
        elif c==0xa0a4: s.rep(c,bytes.fromhex('000c00000000000303010dfe00000000'))
        elif c==0xa3af: s.rep(c,bytes(2)+bytes.fromhex('ea070b0e07100403'))
        elif c in(0xc0a0,0xd0a9): pass
        elif c==0xc0a4: s.decs+=1
        else:
            if c==0xe0a7: s.starts+=1
            if c==0xe0a5: s.online=True
            if c==0xe0ba: s.asleep=False
            s.rep(c,b'\0\0')
    def message(s,m,desc=''):
        """one USB message: must be exactly one frame"""
        ok=len(m)>=4 and struct.unpack_from('<H',m,2)[0]==len(m)
        if ok: s.frame(m);return
        # parse what is inside for the report
        inside=[];i=0
        while i+4<=len(m):
            c,sz=struct.unpack_from('<HH',m,i)
            if sz<4: break
            inside.append('%04x/%d'%(c,sz));i+=sz
        s.viol.append('message of %d B holds [%s]%s'%(len(m),' '.join(inside),desc))
        s.wedged=True

def hp():
    r,w=os.pipe();R=fcntl.fcntl(r,fcntl.F_DUPFD_CLOEXEC,20);W=fcntl.fcntl(w,fcntl.F_DUPFD_CLOEXEC,20);os.close(r);os.close(w);return R,W
env=dict(os.environ)
filt_args=['1','u','t','1',a.opts]
if a.mode=='stock':
    br,bw=hp()                       # printer replies -> filter fd 3
    fr,fw=hp()                       # filter stdout -> 'backend'
    P=Printer(lambda b:os.write(bw,b))
    fl=subprocess.Popen([a.filt]+filt_args,stdin=open(a.ras,'rb'),stdout=fw,preexec_fn=lambda:os.dup2(br,3),pass_fds=(3,),stderr=subprocess.PIPE)
    os.close(fw);os.close(br)
    chunks=[]
    def backend():
        acc=b''
        while True:
            ch=os.read(fr,a.chunk)
            if not ch:break
            chunks.append(len(ch))
            time.sleep(a.xfer_ms/1000.0)
            acc+=ch
            if len(ch)%MSG_MULT:          # short packet ends the USB message
                P.message(acc,'');acc=b''
        if acc: P.message(acc,' (unterminated)')
    t=threading.Thread(target=backend);t.start()
    t0=time.time();fe=fl.communicate()[1].decode(errors='replace');rc=fl.returncode;os.close(bw)
    t.join(10)
    print('MODE stock-usb-model  xfer=%.1fms  filter rc=%d  %.1fs'%(a.xfer_ms,rc,time.time()-t0))
    print('  bulk reads (bytes):',' '.join(map(str,chunks[:6])),'...',' '.join(map(str,chunks[-4:])) if len(chunks)>10 else '')
    print('  filter stderr:',' | '.join(l for l in fe.splitlines() if 'ERROR' in l or 'failed' in l) or '(no errors)')
else:
    sl_m,sl_s=os.openpty();tty.setraw(sl_m);tty.setraw(sl_s);name=os.ttyname(sl_s)
    stream=bytearray();P=Printer(lambda b:os.write(sl_m,b));pbuf=bytearray()
    def mock():
        while True:
            try:d=os.read(sl_m,65536)
            except OSError:break
            if not d:break
            stream.extend(d);pbuf.extend(d)
            while len(pbuf)>=4:
                c,sz=struct.unpack_from('<HH',pbuf,0)
                if sz<4 or len(pbuf)<sz:break
                P.frame(bytes(pbuf[:sz]));del pbuf[:sz]
    threading.Thread(target=mock,daemon=True).start()
    br,bw=hp();fr,fw=hp()
    stf=tempfile.mktemp(prefix='strace-',dir=a.out or '.')
    benv=dict(env,DEVICE_URI='lbp6200:'+name)
    be=subprocess.Popen(['strace','-f','-e','trace=write,openat','-s','0','-o',stf,a.backend,'1','u','t','1',a.opts],stdin=fr,env=benv,preexec_fn=lambda:os.dup2(bw,3),pass_fds=(3,),stderr=subprocess.PIPE)
    fl=subprocess.Popen([a.filt]+filt_args,stdin=open(a.ras,'rb'),stdout=fw,preexec_fn=lambda:os.dup2(br,3),pass_fds=(3,),stderr=subprocess.PIPE)
    os.close(fw);os.close(fr);os.close(bw);os.close(br)
    t0=time.time()
    if a.term_after:
        def term():
            time.sleep(a.term_after)
            kids=subprocess.run(['pgrep','-P',str(be.pid)],capture_output=True,text=True).stdout.split()
            for p in [fl.pid]+[int(k) for k in kids]:
                try:os.kill(p,15)
                except OSError:pass
            print('  SIGTERM sent to filter and backend at %.1fs'%(time.time()-t0))
        threading.Thread(target=term,daemon=True).start()
    fe=fl.communicate()[1].decode(errors='replace');rc=fl.returncode
    try:be.wait(timeout=15)
    except Exception:be.kill()
    time.sleep(0.3)
    txt=open(stf).read()
    dev=None;sizes=[]
    for l in txt.splitlines():
        l=re.sub(r'^\d+\s+','',l)
        m=re.match(r'openat\(.*"(/dev/pts/\d+)".*\)\s*=\s*(\d+)',l)
        if m:dev=int(m.group(2))
        m=re.match(r'write\((\d+), ".*"\.*, (\d+)\)\s*=\s*(-?\d+)',l)
        if m and dev is not None and int(m.group(1))==dev and int(m.group(3))>0:sizes.append(int(m.group(3)))
    # post-hoc rule on the real write sizes
    Q=Printer(lambda b:None);pos=0;acc=b''
    for n in sizes:
        acc+=bytes(stream[pos:pos+n]);pos+=n
        if n%MSG_MULT:Q.message(acc);acc=b''
    if acc:Q.message(acc,' (unterminated)')
    print('MODE real-lbp6200-backend (strace)  filter rc=%d  %.1fs'%(rc,time.time()-t0))
    print('  device writes: %d, messages checked: %d; bytes %d of stream %d'%(len(sizes),len(Q.log)+len(Q.viol),pos,len(stream)))
    print('  filter stderr:',' | '.join(l for l in fe.splitlines() if 'ERROR' in l or 'failed' in l) or '(no errors)')
    P=Q
    if a.out:os.rename(stf,os.path.join(a.out,'strace-backend.txt'))
    else:os.unlink(stf)
import collections
print('  frames seen by printer:',len(P.log),' pages decoded(C0A4):',sum(1 for c,_ in P.log if c==0xc0a4),' start-print(E0A7):',sum(1 for c,_ in P.log if c==0xe0a7),' release(E0A9):',sum(1 for c,_ in P.log if c==0xe0a9))
print('  final E1A1 states sent:',P.e1a1,' E0A9 sent:',sum(1 for c,_ in P.log if c==0xe0a9));print('  last frames:',' '.join('%04x'%c for c,_ in P.log if c not in(0xa0a8,0xa0a4,0xa3af))[-110:]);print('  VIOLATIONS:',len(P.viol));[print('   -',v) for v in P.viol[:3]]
sys.exit(0 if (rc==0 and not P.viol) else 1)
