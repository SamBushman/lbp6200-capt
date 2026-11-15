import os,subprocess,re,struct,sys
H=os.path.expanduser('~')
env=dict(os.environ,PPD='/usr/share/cups/model/CNCUPSLBP6200CAPTK.ppd',PRINTER='LBP6200',CUPS_SERVER=H+'/lbp6200/cups/run/cups.sock')
F=H+'/lbp6200/cups/bin/filter/';L=H+'/lbp6200/ref/canon-common/cndrvcups-common-3.21/libs'
geom={'A4':(592,6778),'Letter':(608,6364)}
ps='%!PS\n'+''.join('/Helvetica findfont 40 scalefont setfont 0 setgray 72 600 moveto (Page ABC) show 0.5 setgray 300 300 120 90 rectfill 0.8 setgray 100 150 200 40 rectfill 0 setgray 3 setlinewidth 50 50 400 600 rectstroke showpage\n' for _ in range(int(sys.argv[3]) if len(sys.argv)>3 else 2))
open('mk2.ps','w').write(ps)
def pages(b):
    out=[]
    for s in [m.start() for m in re.finditer(b'\xa1\xd0\x04\x00',b)]:
        o=s+4;d=bytearray(b'\0\0\0\0')
        while b[o:o+2]==b'\x00\x82':
            ln=struct.unpack_from('<I',b,o+4)[0]; d+=b[o+8:o+ln]; o+=ln
            if ln==12:break
        out.append(d)
    return out
p=sys.argv[1];opts=sys.argv[2]
W,Hh=geom[p]
ps_=subprocess.run([F+'pstops','1','sam','t','1','PageSize='+p+' '+opts,'mk2.ps'],env=env,capture_output=True).stdout
out=subprocess.run([F+'pstocapt3','1','sam','t','1','PageSize='+p+' '+opts],input=ps_,env=env,capture_output=True).stdout
tag=p+'_'+opts.replace(' ','_').replace('=','')
open(tag+'.capt','wb').write(out)
pg=pages(out); print(tag,[len(x) for x in pg], 'pageinfo flags',[hex(out[m.start()+4+92]) for m in re.finditer(b'\x02\x00\x80\x00',out)])
for i,d in enumerate(pg):
    open('%s_%d.bin'%(tag,i+1),'wb').write(d)
    subprocess.run([H+'/lbp6200/scratch/oracle/dec2pbm',L+'/libcanon_slim.so.1.0.0','%s_%d.bin'%(tag,i+1),str(W),str(Hh),'%s_%d.pbm'%(tag,i+1)],env=dict(os.environ,LD_LIBRARY_PATH=L),capture_output=True)
