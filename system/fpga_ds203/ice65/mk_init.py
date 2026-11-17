#!/usr/bin/env python3
"""Flip-flops are 0 after the configuration and the pins do not make an edge
then. In a simulation the inputs go x -> 0 at time 0, which clocks the
flip-flops of the pin-clocked counters with x. This writes statements that
put every flip-flop of a netlist back to 0 (included by the gate testbench
1 ps after the start).
  mk_init.py rec <recovered.v> <hier>     regs of the icebox_vlog netlist
  mk_init.py syn <syn.v> <hier>           SB_DFF* instances of the yosys netlist"""
import re, sys
mode, src, hier = sys.argv[1:4]
txt = open(src).read()
if mode == 'rec':
    names = []
    for line in re.findall(r'^reg (.*);', txt, re.M):
        for d in line.split(','):
            names.append(d.split('=')[0].strip())
    for n in names:
        print('%s.%s = 0;' % (hier, n + ' ' if n.startswith('\\') else n))
else:
    for m in re.finditer(r'^\s*SB_DFF\w*\s+(?:#\(.*?\)\s*)?(\\\S+|\w+)\s*\(', txt, re.M | re.S):
        n = m.group(1)
        print('%s.%s.Q = 0;' % (hier, n + ' ' if n.startswith('\\') else n))
