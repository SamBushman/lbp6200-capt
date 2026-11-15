#!/usr/bin/env python3
"""Disassemble a VM range of a 32-bit PPC Mach-O (thin/fat).  usage: ppcdis.py FILE START END"""
import sys, struct
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import macho_str as m
from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN
d = open(sys.argv[1], 'rb').read(); S = m.segs(d)
a, e = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
code = m.read(d, S, a, e - a)
for i in md.disasm(code, a): print('%08x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
