#!/usr/bin/env python3
"""Find direct call rel32 sites to a function containing ADDR (start found via
.pdata) and print the preceding instructions (to recover constant arguments)."""
import sys, pefile, re, subprocess

pe_path, addr = sys.argv[1], int(sys.argv[2], 16)
pe = pefile.PE(pe_path, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
base = pe.OPTIONAL_HEADER.ImageBase
start = None
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    b, en = base + e.struct.BeginAddress, base + e.struct.EndAddress
    if b <= addr < en:
        start = b
        # follow chained unwind back to primary function start
        break
print('function containing %x starts at %x' % (addr, start))
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
data = text.get_data()
va0 = base + text.VirtualAddress
def dis(a, b):
    out = subprocess.run(['x86_64-w64-mingw32-objdump', '-d', '--no-show-raw-insn', '--start-address=%#x' % a,
                          '--stop-address=%#x' % b, pe_path], capture_output=True, text=True).stdout
    return [l for l in out.splitlines() if re.match(r'\s+[0-9a-f]+:', l)]
print('\n'.join(dis(start, addr + 0x30)))
n = 0
for i in range(len(data) - 5):
    if data[i] in (0xe8, 0xe9):
        tgt = va0 + i + 5 + int.from_bytes(data[i + 1:i + 5], 'little', signed=True)
        if tgt == start:
            n += 1
            print('--- %s at %x' % ('call' if data[i] == 0xe8 else 'jmp', va0 + i))
            print('\n'.join(dis(va0 + i - 40, va0 + i + 5)[-6:]))
print('total', n)
