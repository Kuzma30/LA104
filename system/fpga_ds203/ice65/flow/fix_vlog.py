#!/usr/bin/env python3
"""icebox_vlog writes an ANSI-style port list and then declares every port
again as wire/reg, which strict simulators (iverilog) reject. Rewrite the
header in non-ANSI style, where the later wire/reg declarations are legal.
usage: fix_vlog.py in.v > out.v"""
import re, sys
src = open(sys.argv[1]).read()
m = re.search(r'module\s+(\w+)\s*\((.*?)\)\s*;', src, re.S)
ports = re.findall(r'(input|output|inout)\s+(\\\S+ |\w+)', m.group(2))
names = [n if not n.startswith('\\') else n.rstrip() + ' ' for _, n in ports]
hdr = 'module %s (%s);\n' % (m.group(1), ', '.join(names))
hdr += ''.join('%s %s;\n' % (d, n) for (d, _), n in zip(ports, names))
sys.stdout.write(src[:m.start()] + hdr + src[m.end():])
