# Actual immutable custom-attribute map candidate

Live component trace in `rpcbindingcreate-genuine-base-reuse.md` supersedes
earlier Wine-storage attribution: the reused boxes/GetView are actually served
by genuine xbox_wintypes_base.dll through the existing dispatch shim.

Follow-up: `rpcbindingcreate-rot-result.md` completes the previously outstanding
Boolean/global-IID/background-entry-point controls and records the real lazy
ROT callback plus subsequent IActivationFactory marshaling failure.

Owned stage `~/xodus-runs/rpcbindingcreate-20261006-001`; parent untouched.

Implemented owning IMapView<HSTRING,IInspectable> and IIterable over the actual
per-class ActivationStore CustomAttributes. No empty/default map, fabricated
class list or invented scope/trust. Aliased and EntryPoint values are read from
the exported typed registry metadata; each class retains its actual entry point.

Pinned upstream eba89375a0515957701928faac0f5007ef638b04 map.c/main.c were inspected.
Existing Wine PropertySet/map/value implementations are reused for owning
storage, boxing and iteration. A narrow view wrapper supplies supported GetIids,
canonical iterable/IUnknown identity, missing-output clearing and correct
small-view Split behavior without exposing the mutable backing map.
Upstream/runtime sources are in `rpcbindingcreate-upstream-attributes-*`.

Generic IIDs and vtables come directly from the SDK/WIDL-generated
windows.foundation.h, not guessed constants. The callback's original
IPropertyValue getter at slot 12 (+60) is GetUInt32: Aliased is boxed UInt32
based on that actual use, not a blanket REG_DWORD conversion assumption.
EntryPoint is boxed String from native REG_SZ records.

Native Windows and Wine both pass 500 cycles covering Type=UInt32/value=1,
Type=String/exact entry point, Size/HasKey/Lookup E_BOUNDS, supported IIDs,
Split, iteration and value/iterator survival after map release.
Native backing Split can return S_OK with NULL/NULL even for two entries;
this permitted native behavior was measured and preserved rather than treated
as two mandatory partitions. Runtime name/trust/agility remain explicitly
unsupported secondary ABI.

The actual opt-in original, legitimately activated Xbox callback now returns:

```text
DIRECT_PACKAGE_CONTEXT_CALLBACK_DIAGNOSTIC hr=0 factory=<real nonnull IActivationFactory>
```

This is direct diagnostic callback execution, NOT OOP delivery or registration.
RoRegisterActivationFactories still returns honest E_NOTIMPL; original app
still fail-fasts 7/80004001/27c. No render/install/auth success is claimed.

Remaining validation limits: native global metadata-map enumeration and an
independent Boolean factory control were not completed in this time ceiling;
the required actual attributes use UInt32/String. The 500-cycle fixture was
completed for XboxPcApp.App; background-task entry-point data is staged from
the authoritative records but not separately loop-tested yet.

Artifacts: rpcbindingcreate-attributes.c, rpcbindingcreate-class-registration.c,
rpcbindingcreate-attributes-test.c/.exe, owned rpcbindingcreate-attributes-data.reg.
Logs: rpcbindingcreate-attributes-test-{native,wine}.log,
rpcbindingcreate-attributes-callback.{stdout,stderr}.log.

Final combase DLL:
6fded8f13a2c20428360d8c88567766885067bd11d1a852a5e79f4a73aa46d60.
Source is experimental pending independent gating and those remaining controls.
Do not claim OOP registration from the returned direct diagnostic factory.
