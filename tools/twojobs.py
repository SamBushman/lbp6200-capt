#!/usr/bin/env python3
"""Run N jobs of a raster file through our filter back-to-back (no resets), waiting for the printer to report the page completed."""
import subprocess,sys,time,os,struct
ras=sys.argv[1];n=int(sys.argv[2])
flt=os.path.expanduser('~/lbp6200/src/cups/rastertocapt6200');st=os.path.expanduser('~/lbp6200/src/status/lbp6200-status')
def status():
    return subprocess.run([st,'-d','/dev/usb/lp0'],capture_output=True,text=True).stdout
for i in range(n):
    t=time.time()
    r=subprocess.run('%s 1 u t 1 "" < %s 3<>/dev/usb/lp0 >&3'%(flt,ras),shell=True,executable='/bin/bash',capture_output=True,text=True)
    print('job',i+1,'rc',r.returncode,r.stderr.strip().replace('\n',' | ')[:150],flush=True)
    if r.returncode: break
    for k in range(60):
        time.sleep(1);s=status()
        if 'busy' not in s and 'printing' not in s and k>5: break
    print('  %.0fs'%(time.time()-t),[l for l in s.split('\n') if l.startswith(('Status','Pages'))],flush=True)
