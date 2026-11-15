import sys,struct
def recs(b):
    o=0;out=[]
    while o+4<=len(b):
        c,s=struct.unpack_from('<HH',b,o)
        if c==0x8200:
            out.append((c,o,s,b[o+4:o+4+s]));o+=4+s;continue
        if s<4 or o+s>len(b): out.append(('??',o,b[o:o+16].hex()));break
        out.append((c,o,s,b[o+4:o+s]));o+=s
    return out
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read()
    for r in recs(b):
        if r[0]=='??': print(r);break
        c,o,s,p=r
        print('%04x @%d size=%d %s'%(c,o,s,p[:128].hex() if s<400 else p[:24].hex()+'...'))
