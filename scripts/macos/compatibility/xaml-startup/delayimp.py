#!/usr/bin/env python3
"""List delay imports of a PE for given DLL-name substrings, report whether the
Wine builtin spec exports them, and find call sites (call [rip+X]) to the IAT slots
with the preceding argument-register setup."""
import sys, pefile, re, subprocess

pe_path, spec_path, dllsub = sys.argv[1], sys.argv[2], sys.argv[3].lower()
want = set(sys.argv[4:])
pe = pefile.PE(pe_path, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
spec = open(spec_path).read()
base = pe.OPTIONAL_HEADER.ImageBase
slots = {}
for d in getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', []):
    name = d.dll.decode().lower()
    if dllsub not in name:
        continue
    for imp in d.imports:
        n = imp.name.decode() if imp.name else '#%d' % imp.ordinal
        exported = bool(re.search(r'\b%s\(' % re.escape(n), spec)) and not re.search(r'stub\s+%s\b' % re.escape(n), spec)
        print('%-45s %-45s wine=%s iat=%x' % (name, n, 'yes' if exported else 'NO', imp.address))
        slots[imp.address] = n

if want:
    text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
    data = text.get_data()
    va0 = base + text.VirtualAddress
    for i in range(len(data) - 6):
        if data[i] == 0xff and data[i + 1] == 0x15:
            disp = int.from_bytes(data[i + 2:i + 6], 'little', signed=True)
            tgt = va0 + i + 6 + disp
            if tgt in slots and slots[tgt] in want:
                start = va0 + i - 48
                out = subprocess.run(['x86_64-w64-mingw32-objdump', '-d', '--no-show-raw-insn', '--start-address=%#x' % start,
                                      '--stop-address=%#x' % (va0 + i + 6), pe_path], capture_output=True, text=True).stdout
                lines = [l for l in out.splitlines() if re.match(r'\s+[0-9a-f]+:', l)]
                print('--- %s call at %x' % (slots[tgt], va0 + i))
                print('\n'.join(lines[-10:]))
