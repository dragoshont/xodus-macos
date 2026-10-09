# Bounded generated activation proxy candidate: still blocked

Owned stage only; no parent engine/prefix or RoRegister success change.

Native registry confirms both IActivationFactory IID00000035 and IInspectable
IIDAF86E2E0 use **PS CLSID00000320**, the existing core PSFactoryBuffer.
A direct native CoGetPSClsid API control was not completed; registry evidence
must not be mislabeled as that API control.

Upstream-first evidence:

* Master pinned eba89375a0515957701928faac0f5007ef638b04 has activation.idl,
  inspectable.idl and real HSTRING_UserSize/Marshal/Unmarshal/Free in combase,
  but no generated core proxies in the checked ole32 Makefile.
* Public WineHQ MR!8911 supplies a generated IInspectable proxy integration.
  Full diff is retained. Its LGPL attribution is preserved.
* Staging tree2395d93338d6b75d44c4c68fec38213f2398a5a9 has no matching
  inspectable/activation/HSTRING patch-path result in the scoped check.
* Proton proton_10.0 ole32 Makefile likewise has no matching proxy source line.
  No claim that this proves absence across every fork/drop was made.

The candidate reuses MR!8911 plus existing activation.idl through WIDL.
Two proxy IDLs are added to ole32's existing PSFactoryBuffer, preserving
native CLSID00000320. PROXY_DELEGATION is enabled for inherited interfaces;
only the actual existing IActivationFactory IID definition is supplied.
Generated HSTRING wire routines import the existing combase implementations.
No new broker, transport, Windows class identity or engine replacement.

Build succeeded:
`5e3d509fcd41c9409436fea50e46dbf9833a96e16962da9c79577898597c2cfc`
owned ole32.dll.

Actual paired ROT result remains blocked:

```text
ROT object lookup and IClassFactory QI: S_OK
lazy genuine callback: S_OK/non-null actual factory, once, MTA
actual factory QI IID35: S_OK
StdMarshalImpl_MarshalInterface: Failed to create ifstub 80004002
client receives NULL IActivationFactory
real revoke S_OK; held factory 800401FD; later lookup 800401E3
```

Thus generating/linking the proxies alone has not fixed runtime PS factory
selection/stub creation. No successful cross-process factory transfer or
registration is claimed. The exact runtime selection/registration seam still
needs diagnosis; no further subsystem was opened at this ceiling.

Files: rpcbindingcreate_inspectable.idl, rpcbindingcreate_activation.idl,
rpcbindingcreate_proxy_iids.c; owned ole32 Makefile/compobj_private header changes.
Logs: rpcbindingcreate-activation-proxy-{build,server.stdout,server.stderr,client}.log.
The candidate is experimental/not integration-ready as a marshal fix.

## Clean metadata handoff (independent from proxy experiment)

`rpcbindingcreate-metadata-clean-export.patch` adds only attributes/class-
registration includes and the named export. It contains no diagnostic callback,
ROT or registration patch. Copy these beside roapi.c:

* rpcbindingcreate-server-catalog.c (already independently gated)
* rpcbindingcreate-attributes.c
* rpcbindingcreate-class-registration.c

Data in owned stage: rpcbindingcreate-authoritative-server-mapping.reg,
rpcbindingcreate-class-metadata.reg, rpcbindingcreate-attributes-data.reg.
They contain only native-derived scoped metadata, not authentication/state.
Use the existing matching engine build (`touch roapi.c; make dlls/combase/all`).
Map fixtures are rpcbindingcreate-attributes-test.c with the generated
windows.foundation.h header isolated in test-abi; both native/Wine entry-point
variants passed 500 cycles. Genuine direct callback returns real factory.
Remaining secondary metadata interfaces/scope/trust stay explicitly unsupported.
