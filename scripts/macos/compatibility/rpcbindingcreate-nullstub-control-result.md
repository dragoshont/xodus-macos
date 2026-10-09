# Unchanged native-control executable on owned Wine: NULL-server divergence

The parent's exact native-activation-ps-control.exe was executed unchanged in
the owned prefix with matched builtin combase/ole32/rpcrt4 and no GUI driver.
Its output label NATIVE is not treated as a platform assertion.

It did **not** reproduce native NULL-server CreateStub success. The console
aborted with a NULL read in:

```text
rpcrt4!CStdStubBuffer_Delegating_Construct
pUnkServer=NULL
rpcrt4/cstub.c:156
called by CStdPSFactory_CreateStub, cpsf.c:130
```

Exact source operation:
`IUnknown_QueryInterface(pUnkServer, riid, &pvServer)` unconditionally dereferences
the NULL server. Matching pinned upstream
eba89375a0515957701928faac0f5007ef638b04 has the same operation/body.
This is a concrete constructor NULL-server mismatch, not a PS CLSID or
factory-provider selection failure. Buffered initial probe stdout did not
survive the abort; no missing four-output transcript is invented.

Parent's independent native control correctly returns S_OK/non-null stub for
the same NULL-server call. The earlier real paired Wine test uses a real,
nonnull genuine server and still proves actual factory transfer, QI/GetIids
and revoke. That passing case does not imply NULL-server parity.

No fallback success or constructor patch was added during this diagnostic.
Fixing this must preserve deferred Connect/Disconnect, delegated base-stub
construction, ownership and later connection semantics—not just skip QI.

Evidence: owned rpcbindingcreate-exact-parent-ps-control.log,
rpcbindingcreate-nullstub-upstream.c; parent's original exe/source are unchanged.
