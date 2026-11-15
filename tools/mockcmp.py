import sys,struct
skip={0xa0a8,0xa0a4,0xa3af,0xa1a0,0xa1a1,0xa0a1}
def job(f):
    b=open(f,'rb').read();o=0;fr=[];on=False
    while o+4<=len(b):
        c,s=struct.unpack_from('<HH',b,o);p=b[o:o+s];o+=s
        if c==0xa2a0:on=True
        if not on or c in skip: continue
        fr.append((c,p))
        if c==0xe0a9:break
    return fr
def bycmd(fr):
    d={}
    for c,p in fr: d.setdefault(c,[]).append(p)
    return d
if __name__=='__main__':
    base=bycmd(job('base.bin'))
    for n in sys.argv[1:]:
        fr=job(n+'.bin')
        print('==',n,len(fr),'frames:',' '.join('%04x'%c for c,p in fr))
        for c,ps in bycmd(fr).items():
            bs=base.get(c,[])
            for i,p in enumerate(ps):
                if c==0xc0a0:
                    print('  c0a0 #%d size %d'%(i,len(p)));continue
                if i<len(bs) and bs[i]==p: continue
                if i<len(bs) and len(bs[i])==len(p):
                    d=[(j,bs[i][j],p[j]) for j in range(len(p)) if p[j]!=bs[i][j]]
                    print('  %04x #%d diff:'%(c,i),' '.join('%d:%02x->%02x'%x for x in d[:16]))
                else: print('  %04x #%d (no same-size base) size %d: %s'%(c,i,len(p),p[:48].hex()))
