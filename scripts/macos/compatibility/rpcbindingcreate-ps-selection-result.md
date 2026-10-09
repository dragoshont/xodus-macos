# Actual PS selection fixed; real factory reaches paired client

Owned stage only: `~/xodus-runs/rpcbindingcreate-20261006-001`.
No parent engine/prefix or global PS class registry replacement.

## Measured failing stage

Actual server logs separated all stages:

* IClassFactory: CoGetPSClsid S_OK -> PS00000320, CoGetClassObject S_OK,
  CreateStub S_OK. Existing COM/RPCSS coverage remained functional.
* IActivationFactory IID00000035: **CoGetPSClsid 80040155** before any
  CoGetClassObject/CreateStub call. marshal_object converted this to 80004002.
* Actual PS factory CreateStub vtable code resides in **rpcrt4.dll** (the
  generic NDR factory implementation), not proof of which proxy list supplied it.
* Runtime ole32 virtual module path is C:\windows\system32\ole32.dll.
  Physical prefix file pin 1c882d54628ba7ce316654ae5e8ed04bd81f76b735cb8377be6fa5a3bd15a27f
  differs from built candidate. It must not be mislabeled as the candidate hash.

Generated activation proxy list contains actual IID35 and delegated
IInspectable methods; both proxies were built in the existing ole32 core
PSFactoryBuffer (candidate SHA5e3d509fcd41c9409436fea50e46dbf9833a96e16962da9c79577898597c2cfc).
Matched builtin ole32 was explicitly selected. The key runtime defect was
missing **interface registration**, not failed factory QI or callback.

## One concrete selection fix

`rpcbindingcreate-activation-proxy.reg` adds ONLY the two missing mappings:

```text
Interface IID00000035 -> native PS CLSID00000320
Interface IIDAF86E2E0 -> native PS CLSID00000320
```

Existing PS CLSID class/InprocServer32 coverage was unchanged. Native registry
identity was independently measured; parent owns the native CoGetPSClsid API
control. Previous interface keys were exported where present; absence was
recorded by export failure. Fix persists only in the owned experimental prefix.

## Actual lazy paired-process gate

After the fix:

```text
server ROT registration S_OK; callback count initially0
distinct client GetObject/QI IClassFactory S_OK
server genuine callback once in MTA -> real IActivationFactory
server genuine factory QI IID35 S_OK
server CoGetPSClsid IID35 S_OK -> PS00000320
server CoGetClassObject S_OK; CreateStub IID35 S_OK
client CreateInstance: S_OK/non-null actual IActivationFactory proxy
client QI IID35: S_OK/same proxy
client safe GetIids: S_OK/count1
client GetRuntimeClassName: 8000000E
same genuine LOCAL factory GetRuntimeClassName: 8000000E
server real Revoke S_OK; callback count remains1
held class-factory proxy after revoke: 800401FD, output NULL
subsequent ROT lookup: 800401E3, output NULL
```

The runtime-class-name HRESULT is the genuine factory's local behavior, not
a newly missing API or fabricated remote value. ActivateInstance was not called
(avoids GUI/activation side effects). No Native-Windows broker ABI equivalence,
native RoRegister success, constructor, render, install or auth success claimed.
RoRegisterActivationFactories remains honest E_NOTIMPL.

This proves the private Wine-to-Wine factory seam works with existing ROT,
generated core proxies and real native callback. Concurrency/adversarial
collision and complete registration-lifecycle integration remain separate gates.

Logs: rpcbindingcreate-ps-selection-server.stderr.log (before),
rpcbindingcreate-ps-selection-fixed-* (first transfer),
rpcbindingcreate-ps-final-{server.stdout,server.stderr,client}.log (method/revoke).
Diagnostic marshal.c changes are owned tracing only.
Clean metadata-only patch remains rpcbindingcreate-metadata-clean-export.patch;
copy attributes/class-registration helpers and native-derived data listed in
rpcbindingcreate-proxy-result.md. No diagnostic hooks are in that clean patch.

## Authoritative native control and live provider comparison

Parent independently completed native Windows CoGetPSClsid IID35 -> S_OK/
CLSID00000320, CoGetClassObject IPSFactoryBuffer -> real nonnull object, and
identified CreateStub code in native **combase.dll**. That supplied API control
now supersedes the earlier registry-only limitation.

A fresh owned Wine trace separately measures factory **data/object** address
and CreateStub **code** address:

```text
ACTIVATION_PS_FACTORY_DATA_MODULE C:\windows\system32\ole32.dll
ACTIVATION_PS_FACTORY_MODULE      C:\windows\system32\rpcrt4.dll
ACTIVATION_PS_CREATESTUB IID35    S_OK/non-null stub
```

This is Wine's existing ole32 generated proxy list using generic RPCRT4 NDR
factory code, not native Windows COMBASE's implementation layout. Module names
are not assumed interchangeable. The same fresh paired run again returns an
actual factory proxy, remote GetIids S_OK/count1, and both revoke errors.
Logs: rpcbindingcreate-ps-provider-{server.stdout,server.stderr,client}.log.
