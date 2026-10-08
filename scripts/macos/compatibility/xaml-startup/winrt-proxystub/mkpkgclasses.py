#!/usr/bin/env python3
"""Generate package-scoped WinRT activatable-class rows from an AppxManifest.

Native layout (measured, Windows 26100/26300):
HKLM\\SOFTWARE\\Classes\\ActivatableClasses\\Package\\<PackageFullName>\\ActivatableClassId\\<class>
  ActivationType=0, DllPath=<absolute dll path>, Threading (both=0, STA=1, MTA=2).
Wine combase (br147 patch) reads this key for GetCurrentPackageFullName().

usage: mkpkgclasses.py AppxManifest.xml <PackageFullName> <package dir as Windows path> > out.reg
"""
import re, sys

man, full, root = sys.argv[1:4]
root = root.rstrip('\\') + '\\'
x = open(man, encoding='utf-8-sig').read()
tm = {'both': 0, 'STA': 1, 'MTA': 2}
out = ['Windows Registry Editor Version 5.00', '']
n = 0
for blk in re.findall(r'<InProcessServer>(.*?)</InProcessServer>', x, re.S):
    path = re.search(r'<Path>(.*?)</Path>', blk).group(1)
    dll = (root + path).replace('\\', '\\\\')
    for cls, thr in re.findall(r'<ActivatableClass ActivatableClassId="([^"]+)" ThreadingModel="([^"]+)"', blk):
        out += ['[HKEY_LOCAL_MACHINE\\Software\\Classes\\ActivatableClasses\\Package\\%s\\ActivatableClassId\\%s]' % (full, cls),
                '"ActivationType"=dword:00000000', '"DllPath"="%s"' % dll, '"Threading"=dword:%08x' % tm[thr], '']
        n += 1
sys.stdout.write('\r\n'.join(out) + '\r\n')
sys.stderr.write('classes %d\n' % n)