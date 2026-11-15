import sys,struct
def hdr(f):
    b=open(f,'rb').read()
    # records until d0a1
    o=0;R=[]
    while True:
        c,s=struct.unpack_from('<HH',b,o)
        R.append((c,b[o+4:o+s]));o+=s
        if c==0xd0a1:break
    return R,b[o:o+8],len(b),b[-24:]
if __name__=='__main__':
    for f in sys.argv[1:]:
        R,dh,n,t=hdr(f);print(f,n,dh.hex(),t.hex())
