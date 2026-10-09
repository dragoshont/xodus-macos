# Null-server stub construction fixed and gated

Owned stage only; no tracing changes in the published source patches.

The normal and delegating RPCRT4 constructors now leave pvServerObject NULL
when no server is supplied, allocate real stubs, retain the PS factory and
construct the real delegated base stub. Failed allocations release a server
reference only when one exists. Existing Connect/Disconnect and base-stub
release remain intact.

Native controls additionally proved IsIIDSupported is NULL while disconnected
and nonnull when connected. The candidate implements that measured state,
including after Disconnect, rather than advertising a disconnected server.

Native Windows and Wine both pass 500 cycles each for IActivationFactory and
IInspectable: NULL CreateStub -> real genuine factory Connect -> Disconnect
twice -> reconnect -> Disconnect -> release. Real factory references remain
2 before/after. Native final Release diagnostic is 1, Wine is 0; this existing
stub-count distinction is explicitly recorded, not claimed as identical.

The parent's unchanged exact control now also returns S_OK/STUB1 and deferred
Connect S_OK on Wine. The original genuine callback factory additionally passes
NULL CreateStub/Connect/Disconnect/release inside the real activated process.

The nonnull paired ROT regression remains successful: real factory proxy reaches
the client, remote GetIids S_OK/count1, real revoke S_OK, held-factory 800401FD,
later lookup 800401E3. Registration still returns honest E_NOTIMPL.

## Clean integration artifacts

* rpcbindingcreate-nullstub-fix.patch: RPCRT4 only; no trace or test hook.
* rpcbindingcreate-core-proxy-build.patch: OLE32 Makefile/header additions only.
* rpcbindingcreate_inspectable.idl: attributed WineHQ MR!8911 reuse.
* rpcbindingcreate_activation.idl / rpcbindingcreate_proxy_iids.c:
  actual SDK activation ABI and existing PS identity.
* rpcbindingcreate-activation-proxy.reg: only IID35/IInspectable -> native PS00000320.
* rpcbindingcreate-metadata-clean-export.patch: separate metadata includes/export,
  no diagnostic callback/ROT/registration wiring.
* rpcbindingcreate-nullstub-lifetime-test.c: genuine-server lifecycle fixture.

Copy the three proxy source files beside ole32 sources, apply the core build
patch, apply RPCRT4 patch, build `make -j3 dlls/ole32/all dlls/rpcrt4/all`.
Import only the scoped interface registration in the intended experimental
prefix. Existing global PS class mapping and other COM coverage are untouched.
Use matching loader/server/DLL environment; test executable requires
`-lole32 -luuid -lruntimeobject`.

```text
0a99fba9d6746dd1f090e5db13f0faa6aaadf5a0c86b206fc28f9b0c2431b358 rpcrt4.dll
5e3d509fcd41c9409436fea50e46dbf9833a96e16962da9c79577898597c2cfc ole32.dll
```

Owned logs: rpcbindingcreate-nullstub-fixed-parent-control.log,
rpcbindingcreate-nullstub-fixed-{server.stdout,server.stderr,client}.log,
rpcbindingcreate-nullstub-lifetime-{native,wine}.log.
Do not integrate the diagnostic combase/ROT instrumentation with these patches.
Actual registration/client-route wiring is deliberately deferred.
