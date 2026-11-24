#!/usr/bin/env python3
# Checks that every symbol an application imports is exported by the OS of
# the DS203 (os_host/source/main/Shared.h with DS203, DISABLE_USB, no
# LA104, no USE_FLASHING_SUPPORT). Otherwise the OS stops the start with
# "Symbol ... not found". usage: check_imports.py app.elf ...
import re, subprocess, sys, os
here = os.path.dirname(os.path.abspath(__file__))
shared = open(os.path.join(here, '../../os_host/source/main/Shared.h')).read().split('\n')
ok, stack = set(), []
for l in shared:
    if l.startswith('#if'):
        stack.append('defined(DS203)' in l)
    elif l.startswith('#endif'):
        stack.pop()
    m = re.search(r'case (0x[0-9a-f]+):', l)
    if m and all(stack):
        ok.add(int(m.group(1), 16))
def h(s):
    x = 5381
    for c in s.encode(): x = (x * 37 + c) & 0xffffffff
    return x & 0xffff
bad = 0
for app in sys.argv[1:]:
    out = subprocess.run(['arm-none-eabi-readelf', '-r', '--wide', app], capture_output=True, text=True).stdout
    syms = sorted(set(re.findall(r'R_ARM_(?:JUMP_SLOT|GLOB_DAT)\s+\S+\s+(\S+)', out)))
    miss = [s for s in syms if not s.startswith('_address') and h(s) not in ok]
    print('%s: %d imports%s' % (app, len(syms), (', NOT EXPORTED: ' + ' '.join(miss)) if miss else ', ok'))
    bad += len(miss)
sys.exit(1 if bad else 0)
