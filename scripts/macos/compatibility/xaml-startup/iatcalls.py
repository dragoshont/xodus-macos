#!/usr/bin/env python3
"""Disassemble call sites of an imported function (static or delay IAT)."""
import sys, pefile, subprocess, re
path, func = sys.argv[1], sys.argv[2].encode()
pe = pefile.PE(path, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY[d] for d in
    ('IMAGE_DIRECTORY_ENTRY_IMPORT', 'IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT')])
base = pe.OPTIONAL_HEADER.ImageBase
iat = [i.address for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []) + getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', [])
       for i in e.imports if i.name == func]
print('iat', [hex(a) for a in iat])
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
data = text.get_data(); va0 = base + text.VirtualAddress
for i in range(len(data) - 7):
    if data[i] == 0x48 and data[i+1] == 0xff and data[i+2] == 0x15: off, ln = 3, 7
    elif data[i] == 0xff and data[i+1] == 0x15: off, ln = 2, 6
    else: continue
    tgt = va0 + i + ln + int.from_bytes(data[i+off:i+off+4], 'little', signed=True)
    if tgt in iat:
        a = va0 + i
        out = subprocess.run(['x86_64-w64-mingw32-objdump', '-d', '--no-show-raw-insn', '--start-address=%#x' % (a - 0x60),
                              '--stop-address=%#x' % (a + 0x40), path], capture_output=True, text=True).stdout
        print('--- call at %x' % a)
        print('\n'.join(l for l in out.splitlines() if re.match(r'\s+[0-9a-f]+:', l)))
