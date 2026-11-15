import sys,struct
b=open(sys.argv[1],'rb').read();o=0
while o<len(b):
    c,s=struct.unpack_from('<HH',b,o)
    if c in(1,2,3,4,0xd0a0,0xd0a1,0xd0a2,0xd0a4,0xc0a4,0xc0a0,0xe0a7):
        print('%6d %04x size=%d %s'%(o,c,s,b[o+4:o+min(s,20)].hex()))
        o+=s
    else:
        # data chunk 0x8200?: header 00 82 then u16 len
        print('%6d DATA hdr=%s'%(o,b[o:o+8].hex()));
        # search next known record
        n=min([i for i in (b.find(struct.pack('<H',x)+b'\x04\x00',o+8) for x in (0xc0a4,0xd0a2)) if i>0]);o=n
