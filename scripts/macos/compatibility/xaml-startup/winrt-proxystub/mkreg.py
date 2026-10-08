# Emit a .reg mapping every interface in xboxftps_p.c to the PSFactory CLSID.
import re, sys
p = open('xboxftps_p.c').read(); h = open('xboxftps.h').read()
names = sorted(set(re.findall(r'&IID_(\w+)', p)))
g = {}
for m in re.finditer(r'DEFINE_GUID\(IID_(\w+),\s*0x([0-9a-f]+),\s*0x([0-9a-f]+),\s*0x([0-9a-f]+),\s*((?:0x[0-9a-f]+,?\s*){8})\)', h):
    b = [int(x, 16) for x in re.findall(r'0x([0-9a-f]+)', m.group(5))]
    g[m.group(1)] = '{%08x-%04x-%04x-%02x%02x-%s}' % (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16), b[0], b[1], ''.join('%02x' % x for x in b[2:]))
clsid = '{b70d1129-254c-45dd-be65-2d2124641b0c}'
k = r'HKEY_LOCAL_MACHINE\Software\Classes'
out = ['Windows Registry Editor Version 5.00', '',
       r'[%s\CLSID\%s]' % (k, clsid), '@="PSFactoryBuffer (xodus winmd analogue of MetadataPSFactory)"', '',
       r'[%s\CLSID\%s\InProcServer32]' % (k, clsid), r'@="C:\\windows\\system32\\xboxftps.dll"', '"ThreadingModel"="Both"', '']
for x in names:
    if x in g:
        out += [r'[%s\Interface\%s]' % (k, g[x]), '@="%s"' % x,
                r'[%s\Interface\%s\ProxyStubClsid32]' % (k, g[x]), '@="%s"' % clsid, '']
sys.stdout.write('\r\n'.join(out) + '\r\n')