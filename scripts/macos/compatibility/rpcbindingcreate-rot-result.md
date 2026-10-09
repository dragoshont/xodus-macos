# Completed attribute controls and real ROT factory seam

Owned stage only: `~/xodus-runs/rpcbindingcreate-20261006-001`.
Parent engine/prefix unchanged. No native broker ABI equivalence is claimed.

## Attribute slice validation completed

Native Windows and Wine now both pass **500 cycles for each actual entry point**:
XboxPcApp.App and Windows.ApplicationModel.Core.DefaultInProcBackgroundTask.
Controls cover UInt32 Aliased=1, exact String EntryPoint, Boolean boxing
Type=11/value=1, missing-key E_BOUNDS, Size/HasKey, canonical view/iterable QI,
Split and iterator/value survival after view release.

Native global PropertySet registration get_Attributes returned an empty
machine-metadata view; its actual IIDs include:

```text
BB78502A-F79D-54FA-92C9-90C5039FDF7E  IMapView<HSTRING,IInspectable>
00000038-0000-0000-C000-000000000046  native weak-reference source
FE2F3D47-5D47-5499-8374-430C7CDA0204  iterable of key/value pairs
```

SDK-generated generic IIDs match native QI. The wrapper does not advertise
unsupported weak-reference/agility/runtime-name/trust ABI.
Additional class attributes are rejected rather than silently dropped.
Only the two verified values are read; no default/empty package map is returned.

## Existing ROT transport correction accepted and executed

The earlier GUID-only assessment applies to Irpcss class lookup, not Irot.
No new daemon, socket protocol or fabricated Windows CLSID was introduced.

A private diagnostic IClassFactory is registered with the existing ROT using
a string item moniker containing package-full name, manifest app id and the
actual class id. Registration first validates real token identity, registered
image and class membership through the actual metadata resolver. Delimiter
injection is rejected; preexisting/colliding monikers are rejected and an
unexpected duplicate registration cookie is revoked. Collision behavior is
source-checked, not claimed as an adversarial concurrent collision test.

The wrapper owns its class HSTRING, callback module reference and atomic COM
lifetime. It does not eagerly invoke the callback. Existing COM apartment
dispatch and CoWaitForMultipleHandles(0,...) are reused.
Registration/revoke are diagnostic only; RoRegisterActivationFactories still
returns honest E_NOTIMPL.

## Actual distinct-process proof and precise remaining failure

```text
server PID216: ROT Register S_OK cookie2 callbacks0
client PID208: ROT GetObject S_OK; QI(IClassFactory) S_OK
server callback: PID216 TID296 apartment1 (MTA), real callback S_OK
                real native factory nonnull; callbacks1
server QI(IActivationFactory): S_OK, actual interface nonnull
client CreateInstance(IActivationFactory): 80004002, output NULL
server Revoke: S_OK, callback count remains1
held client factory after revoke: 800401FD / output NULL
client GetObject after revoke: 800401E3 / output NULL
both processes finish; no helper remains awaiting input
```

The exact failure is **after actual callback and actual factory QI**:

```text
StdMarshalImpl_MarshalInterface: Failed to create ifstub, hr 80004002
CoMarshalInterface: Failed to marshal interface
{00000035-0000-0000-C000-000000000046}, hr 80004002
```

Thus existing ROT delivers the standard IClassFactory request lazily and in an
actual initialized apartment, but this engine cannot marshal the returned
IActivationFactory. It is not a callback-lookup, synthetic identity, or ROT
transport failure. The client has not received IActivationFactory; no completed
registration or app startup success is claimed.

This is the bounded next seam: actual IActivationFactory proxy/stub support in
the existing COM marshaler. No new transport architecture was written.

## Artifacts

* rpcbindingcreate-attributes.c/test.c and updated class-registration.c.
* rpcbindingcreate-rot-factory.c: isolated lazy ROT diagnostic wrapper.
* rpcbindingcreate-rot-factory-client.c/.exe: distinct client request/revoke test.
* Opt-in owned source flag: XODUS_ROT_FACTORY_DIAGNOSTIC=1.
  XODUS_METADATA_DIAGNOSTIC remains a separate direct-call diagnostic.
* Logs rpcbindingcreate-rot-factory-server.{stdout,stderr}.log and client.log.
* Native/Wine completed attribute logs: rpcbindingcreate-attributes-final-*.

Final owned combase DLL:
35b2d17ff718450caf799518634c36c28b907c830384921775113b81c75283d7.
Metadata source/data remains authoritative and factory registration remains
unsupported until a real paired client receives the actual factory.
