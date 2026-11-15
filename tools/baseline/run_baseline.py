#!/usr/bin/env python3
"""Regression baseline for the CAPT CUPS filter (+backend).  Reproduces CUPS' pipeline without CUPS:
   gen_rasters | rastertocapt6200 | backend-lbp6200 | pty mock printer (replies via fd 3 to the filter)
and records every host->printer CAPT frame (excluding timing-dependent status polls A0A8/A0A4/A3AF) as: cmd, size, sha256 of the whole frame.
usage:  run_baseline.py run --bin DIR --out DIR [--only NAME ...]
        run_baseline.py compare GOLDEN_DIR NEW_DIR
        run_baseline.py list
Clock and hostname are pinned with fakeclock.so, TZ=PST8PDT, so E1A1/PC_SET_TIME payloads are reproducible."""
import os, sys, struct, subprocess, threading, time, tty, hashlib, json, argparse, collections

HERE = os.path.dirname(os.path.abspath(__file__))
POLLS = (0xa0a8, 0xa0a4, 0xa3af)
# name -> (paper, bpp, pages, duplex, OPTS)
SCEN = collections.OrderedDict()
for p in ('a4', 'letter', 'legal', 'a5', 'b5', 'exec'): SCEN[p + '_1p_k1'] = (p, 1, 1, 0, '')
SCEN['a4_3p_k1'] = ('a4', 1, 3, 0, '')
SCEN['a4_1p_g8'] = ('a4', 8, 1, 0, '')
SCEN['letter_2p_g8'] = ('letter', 8, 2, 0, '')
SCEN['a4_2p_dxl'] = ('a4', 1, 2, 1, '')
SCEN['a4_3p_dxl'] = ('a4', 1, 3, 1, '')
SCEN['letter_2p_dxl'] = ('letter', 1, 2, 1, '')
SCEN['a4_2p_dxs'] = ('a4', 1, 2, 2, '')
SCEN['a4_4p_dxs_g8'] = ('a4', 8, 4, 2, '')
SCEN['a4_1p_draft'] = ('a4', 1, 1, 0, 'CNDraftMode=True')
SCEN['a4_1p_dens8'] = ('a4', 1, 1, 0, 'CNTonerDensity=8')
SCEN['a4_1p_nosmooth'] = ('a4', 1, 1, 0, 'CNSuperSmooth=False')
SCEN['a4_2p_dx_bindtop'] = ('a4', 1, 2, 1, 'BindEdge=Top')

def run_one(binp, name, spec, emu=None, filter_bin=None):
    paper, bpp, pages, duplex, opts = spec
    m, s = os.openpty(); tty.setraw(m); tty.setraw(s); dev = os.ttyname(s)
    st = dict(buf=b'', starts=0, decs=0, online=False, asleep=True, serial=2)
    frames, errs = [], []
    def rep(c, pl=b''): os.write(m, struct.pack('<HH', c, 4 + len(pl)) + pl)
    def mock():
        while True:
            try: d = os.read(m, 65536)
            except OSError: break
            if not d: break
            st['buf'] += d
            while len(st['buf']) >= 4:
                c, sz = struct.unpack_from('<HH', st['buf'], 0)
                if sz < 4 or (c >> 8) not in (0xa0, 0xa1, 0xa2, 0xa3, 0xc0, 0xd0, 0xe0, 0xe1):
                    errs.append('bad frame cmd=%04x size=%d' % (c, sz)); st['buf'] = b''; break
                if len(st['buf']) < sz: break
                fr = bytes(st['buf'][:sz]); st['buf'] = st['buf'][sz:]
                frames.append((c, sz, hashlib.sha256(fr).hexdigest(), fr[:32].hex()))
                if c == 0xa0a8:
                    x = bytearray(92); struct.pack_into('<HH', x, 0, 0xa0a8, 0x5c)
                    struct.pack_into('<H', x, 4, 0x8c00 if st['online'] else (0x8c12 if st['asleep'] else 0x8c10))
                    struct.pack_into('<H', x, 16, 0x8000 if st['asleep'] else 0); struct.pack_into('<H', x, 12, 1); struct.pack_into('<H', x, 14, 0x0080)
                    struct.pack_into('<4H', x, 18, st['decs'], st['starts'], st['starts'], st['starts']); os.write(m, bytes(x))
                elif c == 0xa2a0: rep(c, struct.pack('<HH', 0, st['serial']))
                elif c == 0xa0a4: rep(c, bytes.fromhex('000c00000000000303010dfe00000000'))
                elif c == 0xa3af: rep(c, bytes(2) + bytes.fromhex('ea070b0e07100403'))
                elif c in (0xc0a0, 0xc0a4, 0xd0a9):
                    if c == 0xc0a4: st['decs'] += 1
                else:
                    if c == 0xe0a7: st['starts'] += 1
                    if c == 0xe0a5: st['online'] = True
                    if c == 0xe0ba: st['asleep'] = False
                    rep(c, b'\0\0')
    threading.Thread(target=mock, daemon=True).start()
    fr_, fw = os.pipe(); br, bw = os.pipe()
    env = dict(os.environ, DEVICE_URI='lbp6200:' + dev, TZ='PST8PDT', LD_PRELOAD=os.path.join(HERE, 'fakeclock.so'), LANG='C')
    fenv = env
    if emu:   # emulated guest: LD_PRELOAD does not apply (static binary has fakeclock_static linked in); keep it away from qemu itself
        fenv = dict(env); fenv.pop('LD_PRELOAD')
    be = subprocess.Popen([os.path.join(binp, 'backend-lbp6200'), '1', 'u', 't', '1', opts], stdin=fr_, env=env,
                          preexec_fn=lambda: os.dup2(bw, 3), close_fds=False, stderr=subprocess.PIPE)
    gen = subprocess.Popen([os.path.join(HERE, 'gen_rasters'), paper, str(bpp), str(pages), str(duplex)], stdout=subprocess.PIPE)
    src = gen.stdout; conv = None
    if emu:   # the emulated filter reads the neutral raster format (no PPC libcups on the host)
        conv = subprocess.Popen([os.path.join(HERE, 'ras2plain')], stdin=gen.stdout, stdout=subprocess.PIPE); gen.stdout.close(); src = conv.stdout
    fcmd = ([emu] if emu else []) + [filter_bin or os.path.join(binp, 'rastertocapt6200'), '1', 'u', 't', '1', opts]
    fl = subprocess.Popen(fcmd, stdin=src, stdout=fw, env=fenv,
                          preexec_fn=lambda: os.dup2(br, 3), close_fds=False, stderr=subprocess.PIPE)
    src.close()
    os.close(fw); os.close(fr_); os.close(bw); os.close(br)
    t0 = time.time(); fe = fl.communicate()[1].decode(errors='replace'); rc = fl.returncode
    try: be.wait(timeout=30)
    except Exception: be.kill(); errs.append('BACKEND DID NOT EXIT')
    be.stderr.read(); gen.wait()
    if conv: conv.wait()
    time.sleep(0.2)
    cmds = [(c, sz, h, p) for (c, sz, h, p) in frames if c not in POLLS]
    # filter stderr: keep INFO/ERROR/PAGE lines (STATE lines depend on poll timing -> only counted)
    keep = [l for l in fe.splitlines() if l.split(':')[0] in ('INFO', 'ERROR', 'PAGE', 'WARNING')]
    return dict(name=name, rc=rc, secs=round(time.time() - t0, 1), nframes=len(cmds), npolls=len(frames) - len(cmds), errs=errs,
                stderr=keep, frames=[list(x) for x in cmds],
                digest=hashlib.sha256(json.dumps([x[:3] for x in cmds]).encode()).hexdigest())

def cmd_run(a):
    os.makedirs(a.out, exist_ok=True)
    names = a.only or list(SCEN)
    idx = {}
    for n in names:
        r = run_one(a.bin, n, SCEN[n], a.emu, a.filter_bin)
        json.dump(r, open(os.path.join(a.out, n + '.json'), 'w'), indent=0)
        seq = ' '.join('%04x' % c for c, _, _, _ in [tuple(f) for f in r['frames']][:12])
        print('%-18s rc=%d %5.1fs frames=%-4d polls=%-4d errs=%s digest=%s' % (n, r['rc'], r['secs'], r['nframes'], r['npolls'], r['errs'], r['digest'][:12]), flush=True)
        idx[n] = r['digest']
    json.dump(idx, open(os.path.join(a.out, 'INDEX.json'), 'w'), indent=1)

def cmd_compare(a):
    g, n = a.golden, a.new; bad = 0
    for f in sorted(os.listdir(g)):
        if not f.endswith('.json') or f == 'INDEX.json': continue
        if not os.path.exists(os.path.join(n, f)): print('MISSING', f); bad += 1; continue
        A, B = json.load(open(os.path.join(g, f))), json.load(open(os.path.join(n, f)))
        if A['digest'] == B['digest'] and A['rc'] == B['rc'] and A['stderr'] == B['stderr']: print('same    ', A['name'], A['nframes'], 'frames'); continue
        bad += 1; print('DIFFER  ', A['name'], 'rc', A['rc'], B['rc'], 'frames', A['nframes'], B['nframes'])
        for i, (x, y) in enumerate(zip(A['frames'], B['frames'])):
            if x[:3] != y[:3]: print('   first differing frame #%d: golden %04x/%d  new %04x/%d' % (i, x[0], x[1], y[0], y[1])); break
        if A['stderr'] != B['stderr']: print('   stderr differs:', A['stderr'][:3], B['stderr'][:3])
    print('RESULT:', 'IDENTICAL' if not bad else '%d scenario(s) differ' % bad); return 1 if bad else 0

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); sp = ap.add_subparsers(dest='c', required=True)
    r = sp.add_parser('run'); r.add_argument('--bin', required=True); r.add_argument('--out', required=True); r.add_argument('--only', nargs='*'); r.add_argument('--emu', help='run the filter under this emulator, e.g. qemu-ppc'); r.add_argument('--filter-bin', help='filter binary (default BIN/rastertocapt6200)')
    c = sp.add_parser('compare'); c.add_argument('golden'); c.add_argument('new'); sp.add_parser('list')
    a = ap.parse_args()
    if a.c == 'list': print('\n'.join('%-18s %s' % (k, v) for k, v in SCEN.items()))
    elif a.c == 'run': cmd_run(a)
    else: sys.exit(cmd_compare(a))
