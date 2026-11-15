#!/usr/bin/env python3
"""Build a CUPS raster (8-bit gray, A4/Letter etc. taken from a template raster's headers) whose pages are Canon's own UNROTATED window bitmaps
(Canon's filter run WITHOUT env PRINTER = no duplex rotation).  Feeding it to our filter must reproduce Canon's real duplex wire exactly
(renderer-independent proof of order + rotation + shift).  usage: mkras_from_canon.py template.ras out.ras file.ps paper opts"""
import sys,os,struct,subprocess
src=open(os.path.dirname(os.path.abspath(__file__))+'/duplex_proof.py').read().replace('sys.exit(main())','')
ns={'__file__':os.path.dirname(os.path.abspath(__file__))+'/duplex_proof.py','__name__':'dp'};exec(compile(src,'dp','exec'),ns)
tmpl,out,ps,paper,opts=sys.argv[1:6]
W,H=ns['GEOM'][paper];NB=W*8
env=dict(os.environ,PPD='/usr/share/cups/model/CNCUPSLBP6200CAPTK.ppd',CUPS_SERVER=ns['HOME']+'/lbp6200/cups/run/cups.sock');env.pop('PRINTER',None)
F=ns['HOME']+'/lbp6200/cups/bin/filter/'; o='PageSize=%s %s'%(paper,opts)
p=subprocess.run([F+'pstops','1','u','t','1',o,ps],env=env,capture_output=True).stdout
c=subprocess.run([F+'pstocapt3','1','u','t','1',o],input=p,env=env,capture_output=True).stdout
pages=ns['blocks_from_capt_stream'](c)
pages=[pages[0]]*len(pages)   # identical test pages: use the FRONT-window bitmap for every page (Canon's unrotated back page carries an extra +8 column margin shift, docs/10 geometry)
t=open(tmpl,'rb').read();U=lambda h,x:struct.unpack_from('<I',h,x)[0]
hdr=bytearray(t[4:4+1796]);w,h,bpl=U(hdr,372),U(hdr,376),U(hdr,392)
geo={'A4':120,'Letter':118,'Legal':118,'A5':118,'B5':118,'Executive':118}[paper]
XOFF=120
res=bytearray(b'3SaR')
for pg in pages:
    d,_=ns['decode'](pg,paper); rows=ns['rows'](d,W)
    img=bytearray(b'\xff')*(bpl*h)
    for r,v in enumerate(rows):
        if not v: continue
        row=bytearray(b'\xff')*bpl
        for cidx in range(NB):
            if (v>>(NB-1-cidx))&1: row[XOFF+cidx]=0
        img[(geo+r)*bpl:(geo+r+1)*bpl]=row
    res+=hdr+img
open(out,'wb').write(res); print('pages',len(pages),'size',len(res),'w,h,bpl',w,h,bpl)
