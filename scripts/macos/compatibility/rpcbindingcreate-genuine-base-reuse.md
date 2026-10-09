# Existing genuine PropertySet reuse: measured component correction

The existing construction path is already:
PropertySet ActivateInstance -> IMap Insert -> GetView,
plus real PropertyValue statics UInt32/String/Boolean boxing.
No MR helper was vendored and no runtime component changed for this check.

Actual component ownership was measured from live interface-vtable addresses,
not inferred from Wine source availability or the name wintypes.dll:

```text
PropertyValueStatics   C:\windows\system32\xbox_wintypes_base.dll
BooleanValue           C:\windows\system32\xbox_wintypes_base.dll
PropertySet GetView    C:\windows\system32\xbox_wintypes_base.dll
```

The actual filename has the **xbox_** prefix. It exists in both the tested owned
prefix and the original experimental bottle, size 1,501,432 bytes, identical pin:

```text
5a94b019c7190d9ec47f98e0bc28dced70036f1f97e9a84ef9d75340e364158a
```

The 113,724-byte wintypes.dll dispatch shim is not evidence of Wine-backed map
storage. Earlier wording that attributed the runtime storage to current Wine
map.c was an inference and is **superseded by this direct component trace**.
The upstream/MR source comparison remains accurate as a source comparison,
but does not identify the component actually serving these factory calls.

The owning immutable-view adapter keeps the real GetView alive, rejects
IMap (mutable interface) with E_NOINTERFACE, and keeps canonical IUnknown
identity through its IIterable. Values and iterators remain usable after the
view is released. Native no-split outcomes are supported.

Strengthened owned-prefix fixture:
**500 cycles / zero failures**, including actual component names, Boolean
Type=11/value=1, exact UInt32/String values, mutable-QI rejection, canonical
identity, split, iteration and lifetime.

One fresh original callback retry:

```text
DIRECT_PACKAGE_CONTEXT_CALLBACK_DIAGNOSTIC hr=0 factory=<real nonnull factory>
RoRegisterActivationFactories: honest E_NOTIMPL
fail-fast parameters 7/80004001/27c
```

No OOP delivery, complete registration, render, install or auth success claimed.
No engine update, native DLL replacement or metadata-data change was performed.

Evidence in the owned stage:
rpcbindingcreate-genuine-base-component-test.log,
rpcbindingcreate-genuine-base-callback.{stdout,stderr}.log.
The updated rpcbindingcreate-attributes-test.c records live component ownership.
