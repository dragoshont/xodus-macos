#!/usr/bin/env python3
"""Give Wine's combase.spec the native Windows export ordinals.

Native binaries (Windows.UI, twinapi.appcore, ...) import private combase
functions by ordinal.  Wine auto-numbers its exports, so those imports silently
resolve to unrelated functions.  Ordinals are taken from the native export table
(named exports) and, for NONAME exports, from the matching Microsoft public PDB
symbol at the ordinal's RVA, only when it equals the Wine export name exactly.
Wine-only exports are numbered above the native maximum so they can never
occupy a native ordinal.  Unmatched native ordinals stay unexported.
"""
import re, sys, pefile

native_dll, syms_file, spec_in, spec_out = sys.argv[1:5]

syms = {}
for line in open(syms_file):
    a = line.split()
    if len(a) >= 2:
        try:
            syms.setdefault(int(a[0], 16), a[1])
        except ValueError:
            pass

pe = pefile.PE(native_dll)
ex = pe.DIRECTORY_ENTRY_EXPORT
by_name, by_pdb = {}, {}
for e in ex.symbols:
    if e.name:
        by_name[e.name.decode()] = e.ordinal
    elif e.address in syms:
        by_pdb.setdefault(syms[e.address], e.ordinal)
native_max = max(e.ordinal for e in ex.symbols)

entry_re = re.compile(r'^@(\s+)(\S+)(.*)$')
out, used, report = [], {}, {'named': 0, 'pdb': 0, 'wine-only': 0}
pending = []
for line in open(spec_in).read().splitlines():
    m = entry_re.match(line)
    if not m:
        out.append(line)
        continue
    rest = m.group(3).split()
    name = next(t for t in rest if not t.startswith('-'))
    name = name.split('(')[0]
    if name in by_name:
        o, kind = by_name[name], 'named'
    elif name in by_pdb:
        o, kind = by_pdb[name], 'pdb'
    else:
        o, kind = None, 'wine-only'
    if o is not None and o in used:
        sys.exit('ordinal %d collision: %s and %s' % (o, used[o], name))
    if o is not None:
        used[o] = name
    report[kind] += 1
    idx = len(out)
    out.append(None)
    pending.append((idx, o, m.group(2), m.group(3), name, kind))

next_ord = native_max + 1
for idx, o, ftype, rest, name, kind in pending:
    if o is None:
        o = next_ord
        next_ord += 1
    out[idx] = '%d %s%s' % (o, ftype, rest)
    if kind == 'pdb':
        print('noname #%d = %s' % (o, name))

open(spec_out, 'w').write('\n'.join(out) + '\n')
print('native max', native_max, report)
