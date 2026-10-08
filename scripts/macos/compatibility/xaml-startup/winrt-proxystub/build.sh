#!/bin/bash
# Build xboxftps.dll: NDR proxy/stub DLL for the GamingApp packaged WinRT
# interfaces (XboxPcAppFT/XboxLib/Extensions winmds).  Wine analogue of the
# native combase MetadataPSFactory {00000355-0000-0000-C000-000000000046}
# that the AppxManifest names for these winmds; Wine combase has no
# metadata-driven marshaler, so we compile ordinary widl NDR proxies.
# Requires the widl from widl-winrt-proxy.patch.
# usage: build.sh <S> <dir-with-winmds>
set -e
S=$1; M=$2; W=$S/build/tools/widl/widl; L=$S/build/dlls
export PATH=/opt/homebrew/bin:$PATH
python3 "$(dirname "$0")/winmd2idl.py" xboxftps.idl $M/XboxPcAppFT.winmd $M/XboxLib.winmd $M/Microsoft.Gaming.XboxApp.Extensions.winmd
INCS="-I $S/source/wine/include -I $S/build/include"
$W --winrt -h -o xboxftps.h $INCS xboxftps.idl
$W --winrt -p -o xboxftps_p.c $INCS xboxftps.idl
$W --dlldata-only --dlldata=dlldata.c xboxftps
printf '#define INITGUID\n#include "xboxftps.h"\n' > guids.c
printf 'LIBRARY xboxftps.dll\nEXPORTS\nDllGetClassObject PRIVATE\nDllCanUnloadNow PRIVATE\n' > xboxftps.def
INC="-nostdinc -I. -I$S/build/include -I$S/source/wine/include -I$S/source/wine/include/msvcrt -D_UCRT -DWINE_NO_TRACE_MSGS"
CL='-DPROXY_CLSID_IS={0xb70d1129,0x254c,0x45dd,{0xbe,0x65,0x2d,0x21,0x24,0x64,0x1b,0x0c}}'
for f in xboxftps_p dlldata guids; do
  x86_64-w64-mingw32-gcc -c -O1 $INC -DPROXY_DELEGATION -DENTRY_PREFIX= "$CL" -o $f.o $f.c
done
x86_64-w64-mingw32-gcc -shared -nostdlib -o xboxftps.dll xboxftps_p.o dlldata.o guids.o xboxftps.def -Wl,--entry=0 \
  $L/rpcrt4/x86_64-windows/librpcrt4.a $L/combase/x86_64-windows/libcombase.a \
  $L/ucrtbase/x86_64-windows/libucrtbase.a $L/kernel32/x86_64-windows/libkernel32.a
python3 "$(dirname "$0")/mkreg.py" > xboxftps.reg
# deploy: cp xboxftps.dll $WINEPREFIX/drive_c/windows/system32/; wine reg import xboxftps.reg