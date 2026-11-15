#!/usr/bin/env python3
"""Run Canon's REAL filter chain (pstops|pstocapt3, PRINTER set) for a 2-page duplex job per paper size,
decode both pages with Canon's decoder, and measure where a known page-coordinate marker lands in the window."""
import os,subprocess,struct,re,sys
H=os.path.expanduser('~')
env=dict(os.environ,PPD='/usr/share/cups/model/CNCUPSLBP6200CAPTK.ppd',PRINTER='LBP6200',CUPS_SERVER=H+'/lbp6200/cups/run/cups.sock')
F=H+'/lbp6200/cups/bin/filter/'
L=H+'/lbp6200/ref/canon-common/cndrvcups-common-3.21/libs'
geom={'A4':(595,842,4960,7014,592,6778),'Letter':(612,792,5100,6600,608,6364),'Legal':(612,1008,5100,8400,608,8164),
      'A5':(420,595,3496,4960,408,4724),'B5':(516,729,4298,6070,508,5834),'Executive':(522,756,4350,6300,516,6064)}
ps='%!PS\n/m{newpath moveto 0 exch rlineto exch 0 rlineto neg 0 rlineto closepath fill}def\n'+''.join(
 '0 setgray 72 600 m 60 40 rectfill 0 setgray 300 100 30 20 rectfill showpage\n' for _ in range(2))
ps='%!PS\n'+''.join('0 setgray 72 400 60 40 rectfill 300 100 30 20 rectfill showpage\n' for _ in range(2))
open('mk.ps','w').write(ps)
def pages(b):
    out=[]
    for s in [m.start() for m in re.finditer(b'\xa1\xd0\x04\x00',b)]:
        o=s+4;d=bytearray(b'\0\0\0\0')
        while b[o:o+2]==b'\x00\x82':
            ln=struct.unpack_from('<I',b,o+4)[0]; d+=b[o+8:o+ln]; o+=ln
            if ln==12:break
        out.append(d)
    return out
def bbox(path,W,H_):
    b=open(path,'rb').read();i=b.index(b'\n',b.index(b'\n')+1)+1;d=b[i:]
    ys=[y for y in range(H_) if any(d[y*W:(y+1)*W])]
    xs=set()
    for y in ys[::5]:
        r=d[y*W:(y+1)*W]
        for x in range(W):
            if r[x]:
                for k in range(8):
                    if r[x]>>(7-k)&1: xs.add(x*8+k)
    # second marker is small: split rows into two groups
    groups=[];cur=[ys[0]]
    for y in ys[1:]:
        if y-cur[-1]>50: groups.append(cur);cur=[y]
        else: cur.append(y)
    groups.append(cur)
    res=[]
    for g in groups:
        xs=[]
        for y in g[::3]:
            r=d[y*W:(y+1)*W]
            xs+= [x*8+k for x in range(W) if r[x] for k in range(8) if r[x]>>(7-k)&1]
        res.append((min(xs),max(xs),g[0],g[-1]))
    return res
sel=sys.argv[1:] or list(geom)
for p in sel:
    wpt,hpt,P,Hh,Wb,Lines=geom[p]
    cap=subprocess.run([F+'pstops','1','sam','t','1','PageSize='+p+' Duplex=DuplexNoTumble',open('mk.ps').name],env=env,capture_output=True).stdout
    out=subprocess.run([F+'pstocapt3','1','sam','t','1','PageSize='+p+' Duplex=DuplexNoTumble'],input=cap,env=env,capture_output=True).stdout
    pg=pages(out)
    print(p,'pages',len(pg),[len(x) for x in pg])
    for i,d in enumerate(pg):
        open('%s_%d.bin'%(p,i+1),'wb').write(d)
        r=subprocess.run([H+'/lbp6200/scratch/oracle/dec2pbm',L+'/libcanon_slim.so.1.0.0','%s_%d.bin'%(p,i+1),str(Wb),str(Lines),'%s_%d.pbm'%(p,i+1)],env=dict(os.environ,LD_LIBRARY_PATH=L),capture_output=True,text=True)
        bb=bbox('%s_%d.pbm'%(p,i+1),Wb,Lines)
        # expected page coords of markers (rect1 x72..132pt,y600..640 ; rect2 x300..330,y100..120) in px, y from top
        s=600/72
        for (x0,x1,y0,y1),(px0,px1,py0,py1) in zip(sorted(bb,key=lambda t:t[2]),sorted([(72*s,132*s-1,(hpt-440)*s,(hpt-400)*s-1),(300*s,330*s-1,(hpt-120)*s,(hpt-100)*s-1)],key=lambda t:t[2])):
            print('  page',i+1,'win x',x0,x1,'y',y0,y1,'| unrot expect page x %.1f..%.1f y %.1f..%.1f'%(px0,px1,py0,py1),
                  '| rot expect page x %.1f..%.1f y %.1f..%.1f'%(P-1-px1,P-1-px0,Hh-1-py1,Hh-1-py0))
