import sys,glob,os
from dump import hdr
def flat(f):
    R,dh,n,t=hdr(f);d={}
    for i,(c,p) in enumerate(R): d[(i,c)]=p
    d['dh']=dh[:4];d['dh2']=dh[4:];d['tail']=t[-16:]
    return d
base=flat('out/base.capt')
for f in sorted(glob.glob('out/*.capt')):
    n=os.path.basename(f)[:-5]
    if n.startswith('base'):continue
    try:d=flat(f)
    except Exception as e: print(n,'ERR',e);continue
    out=[]
    for k in base:
        a,b=base[k],d.get(k,b'')
        if a!=b:
            diffs=[ '%d:%02x->%02x'%(i,x,y) for i,(x,y) in enumerate(zip(a,b)) if x!=y]
            out.append('%s[%s]'%(k if isinstance(k,str) else '%04x'%k[1],' '.join(diffs[:14])))
    print(n,' '.join(out) or 'HDR-IDENTICAL')
