"""Compare paired probe outputs. Usage: compare-probe-pairs.py <native dir> <wine dir> <report>

Pointer-sized hex values (16 digits), the probe self-hash line and the EXIT
formatting are normalised; everything else must match line for line."""
import difflib, os, re, sys

NATIVE, WINE, REPORT = sys.argv[1:4]
PTR = re.compile(r'\b(?:0x)?[0-9A-Fa-f]{16}\b')
SELF = re.compile(r'^audit-.*\.exe [0-9A-F]{64}$')

def norm(path):
    out = []
    for line in open(path, encoding='utf-8', errors='replace').read().splitlines():
        if SELF.match(line):
            continue
        m = re.match(r'^EXIT=(?:0x)?([0-9a-fA-F]+|timeout)$', line)
        if m:
            v = m.group(1)
            # Wine's unix exit status keeps only the low byte of the Windows code
            line = 'EXIT=' + (v if v == 'timeout' else '%d' % (int(v, 16 if len(v) == 8 else 10) & 0xff))
        out.append(PTR.sub('PTR', line))
    return out

names = sorted(os.listdir(NATIVE))
same = []
rows = []
for n in names:
    a, b = norm(os.path.join(NATIVE, n)), norm(os.path.join(WINE, n))
    if a == b:
        same.append(n)
        continue
    diff = [l for l in difflib.unified_diff(a, b, 'native', 'wine', n=0, lineterm='') if l[:1] in '+-' and l[:3] not in ('+++', '---')]
    rows.append((n, diff))

with open(REPORT, 'w') as f:
    f.write('# Paired probe parity (native vs Wine)\n\n')
    f.write('match %d / %d after pointer normalisation\n\n' % (len(same), len(names)))
    f.write('## Matching\n\n' + ''.join('- %s\n' % n for n in same) + '\n## Differing\n\n')
    for n, d in rows:
        f.write('### %s (%d lines)\n\n```diff\n%s\n```\n\n' % (n, len(d), '\n'.join(d[:12]) + ('\n...' if len(d) > 12 else '')))
print('match %d / %d' % (len(same), len(names)))
