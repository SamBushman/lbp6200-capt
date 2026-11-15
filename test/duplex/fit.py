import sys
geom={'A4':(592,6778),'Letter':(608,6364),'Legal':(608,8164),'A5':(408,4724),'B5':(508,5834),'Executive':(516,6064)}
def load(n):
    b=open(n,'rb').read(); i=b.index(b'\n',b.index(b'\n')+1)+1; return b[i:]
for p,(W,H) in geom.items():
    NB=W*8; f=load(p+'_1.pbm'); k=load(p+'_2.pbm')
    ti=lambda r:int.from_bytes(r,'big'); rev=lambda x:int(format(x,'0%db'%NB)[::-1],2); mask=(1<<NB)-1
    rf=[ti(f[y*W:(y+1)*W]) for y in range(H)]; rk=[ti(k[y*W:(y+1)*W]) for y in range(H)]
    inkrows=[y for y in range(H) if rk[y]]
    best=[]
    for dx in range(-30,31):
        for dy in range(-4,5):
            m=0;n=0
            for y in inkrows:
                yy=H-1-y+dy
                if not 0<=yy<H: continue
                s=rev(rf[yy]); s=(s<<dx)&mask if dx>=0 else s>>(-dx)
                m+= s==rk[y]; n+=1
            best.append((m/n,dx,dy,n))
    best.sort(reverse=True)
    print(p,'best',best[:2],'S=(W*8-1)-dx =',NB-1-best[0][1])
