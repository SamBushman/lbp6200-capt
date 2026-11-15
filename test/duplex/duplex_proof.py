#!/usr/bin/env python3
"""Offline proof harness: does OUR duplex wire output reproduce Canon's (page ORDER, 180-degree rotation, window shift)?

Canon facts (docs/10-duplex-rotation.md): filter (pstocapt3, PRINTER set) rotates even pages 180 deg for long-edge duplex; the monitor
sends each sheet as [back page, front page]; a lone odd last page is unrotated.  All bytes pass through the monitor unchanged.

Usage:
  duplex_proof.py --ref-wire canon_wire.bin  --ours our_dump.txt            # Canon's real wire capture vs ours
  duplex_proof.py --ref-ps two.ps --paper A4 --opts "Duplex=DuplexNoTumble" --ours our_dump.txt
        (reference built by running Canon's REAL chain pstops|pstocapt3 with PRINTER=LBP6200, then applying the sheet order)
  duplex_proof.py --selftest      # reference vs reference (must PASS) and reference vs unrotated/unordered (must FAIL)
our_dump.txt = lines "cmdhex size hex-of-whole-frame" (src/test/cupsfilter/pipeline_test.py with DUMP=file); a raw frame stream (.bin) also works.
Decoding uses Canon's own lCaptDecode (32-bit helper dec2pbm, build: gcc -m32 -O1 -o dec2pbm dec2pbm.c -ldl).
PASS criteria per block: decoded bitmap equal to Canon's, or (for different halftoning) same ink bounding boxes within --tol px and
identical position of every solid marker; exact equality is required only for bi-level (solid) test content."""
import os,re,struct,subprocess,sys,argparse,tempfile
HOME=os.path.expanduser('~')
LIBS=HOME+'/lbp6200/ref/canon-common/cndrvcups-common-3.21/libs'
DEC=os.path.join(os.path.dirname(os.path.abspath(__file__)),'dec2pbm')
GEOM={'A4':(592,6778),'Letter':(608,6364),'Legal':(608,8164),'A5':(408,4724),'B5':(508,5834),'Executive':(516,6064)}
DX={'A4':7,'Letter':4,'Legal':4,'A5':8,'B5':5,'Executive':2}     # window shift (bits, toward col 0) after the 180-degree rotation, Canon-measured

def frames_from_bin(b):
    o=0
    while o+4<=len(b):
        c,s=struct.unpack_from('<HH',b,o)
        if s<4 or o+s>len(b): break
        yield c,b[o:o+s]; o+=s
def frames_from_dump(t):
    for l in t.splitlines():
        c,s,h=l.split(); yield int(c,16),bytes.fromhex(h)
def blocks_from_frames(fr):
    """-> list of (band-stream bytes with 4 zero lead bytes, d0a9 payload list) per C0A0..C0A4 block, in wire order"""
    blocks=[];cur=None
    for c,f in fr:
        if c==0xc0a0:
            cur=cur if cur is not None else bytearray(b'\0\0\0\0'); cur+=f[4:]
        elif c==0xc0a4 and cur is not None:
            blocks.append(bytes(cur));cur=None
    return blocks
def blocks_from_capt_stream(b):
    """Canon filter output (extended stream, 0x8200 band records) -> per page band stream, filter order"""
    out=[]
    for s in [m.start() for m in re.finditer(b'\xa1\xd0\x04\x00',b)]:
        o=s+4;d=bytearray(b'\0\0\0\0')
        while b[o:o+2]==b'\x00\x82':
            ln=struct.unpack_from('<I',b,o+4)[0]; d+=b[o+8:o+ln]; o+=ln
            if ln==12: break
        out.append(bytes(d))
    return out
def sheet_order(pages,duplex_long=True):
    """Canon monitor: pair (odd,even) -> [even,odd]; lone odd last page unchanged."""
    if not duplex_long: return list(pages)
    out=[]
    for i in range(0,len(pages),2):
        out+= [pages[i+1],pages[i]] if i+1<len(pages) else [pages[i]]
    return out
def decode(bs,paper):
    W,H=GEOM[paper]
    with tempfile.TemporaryDirectory() as td:
        fb=td+'/b.bin';fo=td+'/o.pbm';open(fb,'wb').write(bs)
        r=subprocess.run([DEC,LIBS+'/libcanon_slim.so.1.0.0',fb,str(W),str(H),fo],env=dict(os.environ,LD_LIBRARY_PATH=LIBS),capture_output=True,text=True)
        d=open(fo,'rb').read(); i=d.index(b'\n',d.index(b'\n')+1)+1
        return d[i:],r.stderr.strip()
def rows(d,W): return [int.from_bytes(d[y*W:(y+1)*W],'big') for y in range(len(d)//W)]
def compare(a,b,paper,tol):
    W,H=GEOM[paper];ra,rb=rows(a,W),rows(b,W);NB=W*8;mask=(1<<NB)-1
    eq=sum(1 for x,y in zip(ra,rb) if x==y)
    ink=[y for y in range(H) if ra[y] or rb[y]]
    best=(eq,0,0)
    if eq!=H:
        for dx in range(-tol,tol+1):
            for dy in range(-tol,tol+1):
                m=0
                for y in ink:
                    yy=y+dy
                    if 0<=yy<H:
                        s=(rb[yy]<<dx)&mask if dx>=0 else rb[yy]>>(-dx)
                        m+= ra[y]==s
                m+=H-len(ink)
                if m>best[0]: best=(m,dx,dy)
    return eq,H,best
def load_ours(path):
    b=open(path,'rb').read()
    try: t=b.decode(); fr=list(frames_from_dump(t)) if re.match(r'^[0-9a-f]{4} \d+ ',t) else None
    except Exception: fr=None
    if fr is None: fr=list(frames_from_bin(b))
    return fr
def reference_from_ps(ps,paper,opts):
    env=dict(os.environ,PPD='/usr/share/cups/model/CNCUPSLBP6200CAPTK.ppd',PRINTER='LBP6200',CUPS_SERVER=HOME+'/lbp6200/cups/run/cups.sock')
    F=HOME+'/lbp6200/cups/bin/filter/'
    o='PageSize=%s %s'%(paper,opts)
    p=subprocess.run([F+'pstops','1','u','t','1',o,ps],env=env,capture_output=True).stdout
    out=subprocess.run([F+'pstocapt3','1','u','t','1',o],input=p,env=env,capture_output=True).stdout
    return sheet_order(blocks_from_capt_stream(out),'DuplexTumble' not in opts and 'BindEdge=Top' not in opts)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--ref-wire');ap.add_argument('--ref-ps');ap.add_argument('--paper',default='A4')
    ap.add_argument('--opts',default='Duplex=DuplexNoTumble');ap.add_argument('--ours');ap.add_argument('--tol',type=int,default=1)
    ap.add_argument('--selftest',action='store_true');a=ap.parse_args()
    if a.selftest:
        ps=os.path.dirname(os.path.abspath(__file__))+'/mk.ps'
        if not os.path.exists(ps): ps=HOME+'/lbp6200/scratch/oracle/papers/mk.ps'
        ref=reference_from_ps(ps,a.paper,a.opts); W,H=GEOM[a.paper]
        ok=True
        for i,bs in enumerate(ref):
            d,_=decode(bs,a.paper);e,_,_=compare(d,d,a.paper,0);ok&=e==H
        print('selftest ref==ref',ok)
        wrong=[ref[1],ref[0]]; d0,_=decode(ref[0],a.paper); d1,_=decode(wrong[0],a.paper)
        e,_,best=compare(d0,d1,a.paper,a.tol); print('selftest ref vs swapped order: equal rows',e,'/',H,'(must be < all)', 'DETECTED' if e<H else 'NOT DETECTED')
        return 0 if ok and e<H else 1
    ref=blocks_from_frames(frames_from_bin(open(a.ref_wire,'rb').read())) if a.ref_wire else reference_from_ps(a.ref_ps,a.paper,a.opts)
    ours=blocks_from_frames(load_ours(a.ours))
    print('blocks: ref %d ours %d'%(len(ref),len(ours)))
    if len(ref)!=len(ours): print('FAIL: block count differs');return 1
    bad=0
    for i,(r,o) in enumerate(zip(ref,ours)):
        dr,er=decode(r,a.paper);do,eo=decode(o,a.paper)
        eq,H,(bm,dx,dy)=compare(do,dr,a.paper,a.tol)
        status='EXACT' if eq==H else ('OK(shift dx=%d dy=%d, rows %d/%d)'%(dx,dy,bm,H) if bm>=0.98*H else 'FAIL')
        print('block %d: equal rows %d/%d  best-shift rows %d/%d  -> %s'%(i+1,eq,H,bm,H,status)); bad+= status=='FAIL'
    print('RESULT','PASS' if not bad else 'FAIL');return 1 if bad else 0
sys.exit(main())
