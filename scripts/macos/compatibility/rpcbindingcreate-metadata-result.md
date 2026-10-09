# Grounded metadata ABI and bounded partial resolver

Owned stage: `~/xodus-runs/rpcbindingcreate-20261006-001`.
This is an experimental **partial base-interface candidate**, not full native
metadata parity, successful factory registration or OOP delivery.

## Actual private ABI evidence

Matching signed native combase.dll:
39a2e2ba1ca6012953c94c10011fff5a18783120a2a34f5b66deb2d374a362c9.
CodeView identifies Microsoft public `combase.pdb`,
**DA1DC0C4F35CEE58F672CBB20460D11D1**. Exact public PDB was downloaded from
Microsoft's symbol server and inspected with native DbgHelp, not guessed ordinals.

PDB signature:
`HRESULT RoGetActivatableClassRegistration(HSTRING, Windows::Foundation::IActivatableClassRegistration **)`.

IActivatableClassRegistration inherits IInspectable, then:

| Slot | Method |
| --- | --- |
| 6 | get_ActivatableClassId(HSTRING*) |
| 7 | get_ActivationType(enum*) |
| 8 | get_RegistrationScope(enum*) |
| 9 | get_RegisteredTrustLevel(enum*) |
| 10 | get_Attributes(IMapView<HSTRING,IInspectable>**) |

Live native global PropertySet and Uri metadata queries both return S_OK.
Returned primary vtable slots match PDB getter RVAs 213500 / 14ae00 / 13a2a0 /
2137e0 / 63220. Native GetIids lists:

```text
9BBCAE23-3DD6-49C3-B63C-1C587E7A6A67
00000038-0000-0000-C000-000000000046
C8AA04F6-66C6-46A3-8FE6-F56BE7DDC091
```

The candidate supports the observed base metadata IID plus standard IUnknown/
IInspectable only. Weak-reference and secondary server interfaces are not
claimed. Native global controls return class ids faithfully and numeric
activation/scope/trust values 0; these global values are not copied to package state.

## Newly authoritative custom attributes

The original package ActivationStore class subtrees have CustomAttributes:

* `AppObject.Aliased`: actual REG_DWORD 1, all four classes.
* `AppObject.EntryPoint`: actual REG_SZ XboxPcApp.App for Microsoft.Xbox.AppL
  and AppX7zsk...; Windows.ApplicationModel.Core.DefaultInProcBackgroundTask
  for AppXabak... and AppXj1x....

The original TwinAPI callback at RVA 2fc90 calls metadata slot 10, then
IMapView::Lookup for the exact key **AppObject.Aliased**. This is real registered
attribute data, not a manifest-entry-point-as-class-list guess.

## Implemented and actually executed

`rpcbindingcreate-class-registration.c` resolves a class against the existing
identity/image-bound authoritative class catalog. A new metadata-only Classes
subkey stores actual ActivationType from native records; no hardcoded class
object or identity is used.

The owning COM object implements real QI/AddRef/Release/GetIids, class-id string
ownership and the actual ActivationType. Scope/trust/runtime-name, attributes,
weak references and secondary server interfaces fail explicitly as unsupported.
This limited object is **not ready to splice as full metadata support**.

The named stub was replaced only in the owned combase spec. An explicitly
opt-in `XODUS_METADATA_DIAGNOSTIC=1` path directly invokes the genuine callback
inside the original, legitimately activated Xbox process before returning the
unchanged honest factory-registration E_NOTIMPL. It neither registers factories
nor routes an OOP request.

Actual headless result:

```text
Resolved actual class metadata Microsoft.Xbox.AppL.AppXj1x... activation=1
get_Attributes: native IMapView<HSTRING,IInspectable> not implemented
DIRECT_PACKAGE_CONTEXT_CALLBACK_DIAGNOSTIC hr=80004001 factory=NULL
```

The former unimplemented-import abort is replaced by a real metadata object and
an explicit **get_Attributes E_NOTIMPL**. Original app still fails registration
with 7/80004001/27c; no factory, rendering, install or auth success is claimed.

## Exact bounded gap

The remaining metadata slice must return an owning, immutable native-compatible
IMapView with correctly typed IInspectable property values from those verified
REG_DWORD/REG_SZ attributes. Scalar property-value conversion, generic-interface
QI/iteration/lifetime and native package-scope/server secondary interfaces were
not fully grounded/tested within this time ceiling. No fake empty map, guessed
IID or default scope/trust was supplied. Broker transport remains untouched.

## Persistent artifacts

* `rpcbindingcreate-metadata-symbol-control.c/.exe`: exact PDB/type inspection.
* `rpcbindingcreate-metadata-native-control.c/.exe`: global native ABI controls.
* `rpcbindingcreate-class-registration.c`: partial resolver candidate.
* Owned source roapi.c/spec include/export and opt-in diagnostic changes.
* Assigned Windows scratch: matching public PDB and logs
  `rpcbindingcreate-native-metadata-{symbols,abi,global}.log`.
* Owned stage: `rpcbindingcreate-class-metadata.reg`,
  `rpcbindingcreate-metadata-{build,import,diagnostic.stdout,diagnostic.stderr}.log`.
* Selected activation-only custom-attribute records:
  `rpcbindingcreate-native-metadata-attributes.json`.

Candidate DLL:
`a94feb280d91b195027845a102db12ebaa1cb72d8254334c442e9b595799abb1`.
Parent's independently gated catalog/lifecycle implementation is untouched.
Do not integrate this partial candidate as complete metadata or registration.
