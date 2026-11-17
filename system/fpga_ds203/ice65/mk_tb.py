#!/usr/bin/env python3
"""Make variants of ../tb/tb_scope.v for the iCE65L04 build.
  mk_tb.py ram  in.v out.v   RTL with DP_RAM_l04: the block RAM models are 0 after
                             configuration, drop the initialisation of dut.U2
  mk_tb.py gate in.v out.v   netlist (after synthesis or recovered from the
                             bitstream): no hierarchical access at all; flip-flops
                             are 0 after configuration; the test that forces the
                             counter near the 32 bit wrap is not checked
"""
import re, sys
mode, src, dst = sys.argv[1:4]
lines = open(src).read().split('\n')
out = []
i = 0
while i < len(lines):
    l = lines[i]
    if mode == 'gate' and 'check("wrap of 32 bits"' in l:
        # the counter is not forced near the wrap here (the lines with dut. are
        # dropped below); the reads stay, so that the timing of the tests
        # that follow is the same as on the sources
        out.append('        $display("     (wrap of 32 bits: not checked, needs access to the counter)");')
        i += 1
        continue
    if mode == 'gate' and l.strip() == 'dut.U1.Full = 0;':
        out.append('        #1;')
        out.append('`include "gate_init.vh"')
        # the edge model of the testbench counted the same x -> 0 edges at time 0
        out.append('`ifdef COUNTERS')
        out.append('`ifndef NO_EDGE_CNT')
        out.append('        m_cr = 0; m_cf = 0; m_dr = 0; m_df = 0;')
        out.append('`endif')
        out.append('`endif')
        out.append('        // ' + l.strip())
        i += 1
        continue
    hit = 'dut.U2.' in l if mode == 'ram' else re.search(r'\bdut\.', l) and 'FIFO dut' not in l
    if hit:
        # a "for (...) dut.x = 0;" on one line is dropped as a whole
        out.append('        // ' + l.strip())
    else:
        out.append(l)
    i += 1
open(dst, 'w').write('\n'.join(out))
