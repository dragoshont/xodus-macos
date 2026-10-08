#!/bin/bash
# Mirror the native Windows InProcHandler32 registrations owned by twinapi.appcore.dll
# (measured on the reference VM: HKLM\SOFTWARE\Classes\CLSID\*\InProcHandler32, no ThreadingModel).
# CoUnmarshalInterface of an OBJREF_HANDLER created by twinapi.appcore's CWrlLightweightHandlerServer
# resolves the handler class through these keys.
# Usage: WINEPREFIX=... register-appcore-handlers.sh <wine-loader>
set -eu
WINE=${1:?wine loader}
for c in '{14DE3806-5D5B-405C-AB89-4AC936BCBF48}' '{3C5EDEBE-16C6-4EE4-906B-436AA1F6EF9E}' '{A1103531-6B1C-425F-A8C9-671616E40FA9}'; do
    "$WINE" reg add "HKLM\\Software\\Classes\\CLSID\\$c\\InProcHandler32" /ve /d 'C:\Windows\System32\twinapi.appcore.dll' /f
done
# IConfigureWindowFactory, remoted to the window-factory server; native ProxyStubClsid32 (OneCoreUAPCommonProxyStub.dll).
"$WINE" reg add 'HKLM\Software\Classes\Interface\{601D51E3-801E-49C9-BBFA-FE29A662AEAD}\ProxyStubClsid32' /ve /d '{95E15D0A-66E6-93D9-C53C-76E6219D3341}' /f
# {99FC44E3-...}, queried on the remoted activated-event args by
# CoreApplication::ApplyViewActivationResults; same native proxy/stub DLL.
"$WINE" reg add 'HKLM\Software\Classes\Interface\{99FC44E3-CE9A-4284-BF04-76331D1B1788}\ProxyStubClsid32' /ve /d '{95E15D0A-66E6-93D9-C53C-76E6219D3341}' /f
# CLSID_UAPSplashScreenEvents, created in-process after CreateSplashScreen (native: CoreUIComponents.dll, Both).
"$WINE" reg add 'HKLM\Software\Classes\CLSID\{3EC569DC-6FB5-45D1-8B46-E122D27962E9}' /ve /d 'CLSID_UAPSplashScreenEvents' /f
"$WINE" reg add 'HKLM\Software\Classes\CLSID\{3EC569DC-6FB5-45D1-8B46-E122D27962E9}\InProcServer32' /ve /d 'C:\Windows\System32\CoreUIComponents.dll' /f
"$WINE" reg add 'HKLM\Software\Classes\CLSID\{3EC569DC-6FB5-45D1-8B46-E122D27962E9}\InProcServer32' /v ThreadingModel /d Both /f
# Every other interface natively served by OneCoreUAPCommonProxyStub.dll (measured list), added only
# where the prefix has no Interface key yet so Wine's own proxy registrations stay untouched.
LIST=$(dirname "$0")/onecoreuap-proxystub-iids.txt
REG=$(mktemp -t onecoreuap-ps).reg
python3 - "$LIST" "$WINEPREFIX/system.reg" "$REG" <<'EOF'
import re, sys
lst, sysreg, out = sys.argv[1:]
have = set(m.upper() for m in re.findall(r'^\[Software\\\\Classes\\\\Interface\\\\(\{[0-9A-Fa-f-]{36}\})\]', open(sysreg, encoding='utf-8', errors='replace').read(), re.M))
iids = [l.strip() for l in open(lst) if l.startswith('{')]
add = [i for i in iids if i.upper() not in have]
with open(out, 'w', encoding='utf-16') as f:
    f.write('Windows Registry Editor Version 5.00\r\n\r\n')
    for i in add:
        f.write('[HKEY_LOCAL_MACHINE\\Software\\Classes\\Interface\\%s\\ProxyStubClsid32]\r\n@="{95E15D0A-66E6-93D9-C53C-76E6219D3341}"\r\n\r\n' % i)
print('onecoreuap proxy/stub: %d measured, %d already present, %d added' % (len(iids), len(iids) - len(add), len(add)))
EOF
WREG=$("$WINE" winepath -w "$REG")
"$WINE" regedit /S "$WREG"
rm -f "$REG"
