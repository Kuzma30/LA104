#!/usr/bin/env python3
"""Write a module FIFO (the ports of the testbench) around a netlist of the
iCE65 build:  mk_shim.py syn FIFO_L04 > shim.v          (netlist after synthesis)
              mk_shim.py rec <recovered.v> <module> > shim.v
(netlist recovered from the bitstream by icebox_vlog, scalar ports)"""
import re, sys
print("module FIFO( ClrW, MCI, nRD, CE, H_L, C_D, SCK, SDA, Din, nPD, CKA, CKB, DB);")
print("  input ClrW, MCI, nRD, CE, H_L, C_D, SCK, SDA; input [17:0] Din;")
print("  output nPD, CKA, CKB; inout [15:0] DB;")
if sys.argv[1] == 'syn':
    print("  %s U(.ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE), .H_L(H_L), .C_D(C_D), .SCK(SCK), .SDA(SDA),"
          " .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB));" % sys.argv[2])
else:
    src = open(sys.argv[2]).read()
    m = re.search(r'module\s+%s\s*\((.*?)\)\s*;' % sys.argv[3], src, re.S)
    ports = [p.strip() for p in re.findall(r'(?:input|output|inout)\s+(\\\S+ |\w+)', m.group(1))]
    conns = []
    for p in ports:
        q = p.lstrip('\\')
        conns.append('.%s (%s)' % (p + ' ' if p.startswith('\\') else p, q))
    print("  %s U(%s);" % (sys.argv[3], ', '.join(conns)))
print("endmodule")
