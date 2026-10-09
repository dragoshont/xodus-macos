# Actual first E_NOTIMPL producer with genuine server argument

## Reproduced product context

The owned RPC engine/prefix was used, not the parent stage. It received the
parent's measured GetCurrentPackageFamilyName prerequisite and the current
TwinAPI image plus its required shutdown-delay bridge dependency. RPC changes
and parent QOS fix were preserved. No GUI/input, authentication, factory
fabrication or parent-prefix mutation occurred.

The real registered launcher now uses the manifest-derived **Microsoft.Xbox.AppL**
app id (the earlier Microsoft.Xbox.App argument was incorrect), with:

```text
Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe
Microsoft.Xbox.AppL
C:\windows\system32\package-claims.dll
run
Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca
```

Invocation correction: the existing launcher prepends `-ServerName:` itself.
Earlier worker traces used a prefixed argument, producing a double prefix;
see `rpcbindingcreate-hstring-result.md` for proof and the corrected real retry.
The historical shutdown-delay failure remains valid, but use the bare argument above.

Actual owned-prefix result: catalog query success, registered original image,
genuine activated child claims, launcher checks 13 / failures 0, original child
exit 3. No constructor/render/install success is claimed.

## Exact first producer, not a guessed class-enumeration cause

The owned combase trace captures the first E_NOTIMPL origination and module RVAs.
Its causal native frame is **twinapi.appcore.dll +0x4ae57**.
Disassembly establishes:

```text
twinapi +0x4acea: load real stop-event HANDLE
twinapi +0x4acf1: EDX = 0x7d0 (2000 milliseconds)
twinapi +0x4acf6: indirect call via IAT RVA 0x1ccc20
twinapi +0x4ad02: retain returned HRESULT
twinapi +0x4ad07: failing-HRESULT branch to +0x4ae43
twinapi +0x4ae52: originate/propagate the returned error
twinapi +0x4ae57: recorded causal return frame
```

The exact IAT entry resolves to **combase-server-shutdown-delay.dll ordinal 69**.
The actual loaded bridge exports ordinal 69 at RVA `0x1330`, not a forwarder.
The corresponding bridge implementation is
**CoRegisterServerShutdownDelay(HANDLE stop_event, DWORD milliseconds)**.
For the nonnull event/2000-ms request it intentionally returns E_NOTIMPL:

```c
if ((!stop_event) != (!milliseconds)) return E_INVALIDARG;
if (!stop_event) return S_OK;
return E_NOTIMPL; /* shutdown-delay registration unsupported */
```

Thus the first actual 0x80004001 is the **COM server shutdown-delay registration
request**, before any attempted WinRT factory registration.

The later native instruction at twinapi `+0x4ad1f` calls named
RoRegisterActivationFactories through IAT `0x2423c0`. That instruction is not
reached on this failing branch, explaining why the parent's honest registration
ERR was not seen. RoGetServerActivatableClasses is earlier and still a
success/count-zero stub; it is not the identified first E_NOTIMPL return site.
No fake empty enumeration or guessed manifest implementation was supplied.

## Concrete remaining dependency

A real implementation of the reached shutdown-delay registration requires the
actual COM server lifetime/delay/event delivery semantics, not just a timer or a
successful return. This trace does not infer those undocumented semantics or
replace the bridge's truthful E_NOTIMPL. The bridge remains parent-owned;
this worker did not overlap its implementation.

Diagnostic source patch is retained in the owned stage:
`rpcbindingcreate-server-trace.patch`. It only traces server enumeration entry
and first E_NOTIMPL origination; it is not an integration fix.

Evidence in the owned stage and assigned Windows scratch:

* rpcbindingcreate-server-trace.stdout.log
* rpcbindingcreate-server-trace.stderr.log
* server-trace-twin-disassembly.log
* actual-shutdown-delay-producer.dll (actual loaded bridge for export verification)
* rpcbindingcreate-server-trace.patch

No RoGetBufferMarshaler, factory registration, Vector method, or unrelated
unused import was mislabeled as the reached producer. Next work must address
the observed ordinal-69 call, not assume another SDK/contract file is missing.
