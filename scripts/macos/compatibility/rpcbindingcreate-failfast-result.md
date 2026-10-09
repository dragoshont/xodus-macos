# Exact post-shutdown-delay fail-fast boundary

Follow-up: `rpcbindingcreate-hstring-result.md` establishes that the earlier
prefixed enumeration input came from a double-prefixed launcher invocation,
not a HSTRING bug. The corrected bare-name query remains unsupported.

Owned stage only: `~/xodus-runs/rpcbindingcreate-20261006-001`.
Parent engine/prefix, GUI, authentication and shutdown-delay source are unchanged.

## Identified producer

The genuine original TwinAPI imports **named functions**, not Wine ordinals:

| Original delayed IAT RVA | Contract | Name |
| --- | --- | --- |
| `242410` | api-ms-win-core-winrt-registration-l1-1-0.dll | RoGetServerActivatableClasses |
| `2423c0` | api-ms-win-core-winrt-l1-1-0.dll | RoRegisterActivationFactories |

Direct PE delay-import parsing independently confirmed these names. Native
TwinAPI SHA-256:
`d360b4926165bc68d8a8225e4345e1454e2d386de0ced29d4cae8c70d845ef7c`.

Actual order and arguments:

1. TwinAPI `+4ac5b` calls RoGetServerActivatableClasses with HSTRING
   `-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca`.
   The inherited implementation returns S_OK/count=0 and no class array.
2. Real shutdown-delay registration succeeds for the genuine stop event/2000 ms.
3. TwinAPI `+4ad1f` calls RoRegisterActivationFactories:
   classes=NULL, callbacks=`00000000007BB820`, count=0,
   cookie output=`00000000007BAF98`.
4. Registration returns **E_INVALIDARG (`80070057`)**, before its unsupported
   transport branch. Return frame is TwinAPI `+4ad26`.
5. TwinAPI's failure branch reaches `+4b001` (return frame `+4b006`) and
   RaiseFailFastException with code `c0000409`, parameters
   **7 / ffffffff80070057 / 27c**.

Recorded native stack includes TwinAPI `+b37f4`, `+a6187`, `+46a56`, `+989ef`,
`+a00f8`, `+a00bf`, `+bcf90`, `+4b006`, `+4a956`, `+4a4e8`, `+49b76`,
`+49859`, `+4932c`, `+fc43`, then genuine XAML/Xbox entry frames.
GetRestrictedErrorInfo is called during failure reporting; its stub is not
the producer of the recorded 80070057.

## Upstream check and decision

Current wine-mirror/wine master was fetched and resolved to
**eba89375a0515957701928faac0f5007ef638b04**, committed 2026-10-05.
At that exact revision:

* `dlls/combase/roapi.c`: server enumeration still returns S_OK/count=0;
  factory registration still returns S_OK without registering anything.
* `dlls/combase/combase.spec`: both named exports exist.
* `dlls/combase/tests/roapi.c`: no tests for either function.

There is **no working upstream implementation to backport**. The upstream
success stubs were not imported.

## Bounded fail-closed change and real retry

`rpcbindingcreate-server-enum-failclosed.patch` removes inherited false empty
enumeration success. It clears outputs, uses the existing real package-family
token query, returns CLASSNOTREG for an unpackaged caller and explicitly
E_NOTIMPL for the unsupported package-scoped catalog query. It does not
invent classes, factories, broker registrations or package state.
Registration's honest validation/unsupported transport remains unchanged.

Native Windows unpackaged controls, with and without `-ServerName:` prefix,
return **80040154 / classes=NULL / count=0**. Zero-count registration returns
**80070057**, cookie=NULL. The same standalone Wine controls match.
This does not establish valid packaged server enumeration behavior.

The verified manifest declares AppL executable XboxPcApp.exe and entry point
XboxPcApp.App, but the current developer catalog does not provide the generated
server-to-activatable-class mapping. Guessing that mapping or treating the entry
point as a complete native class list would not establish equivalent behavior.
Actual Windows package-context server-class records and broker registration
transport remain the concrete implementation dependency.

Genuine app retry after fail-closed enumeration still exits **c0000409**, now
with failure parameters **7 / ffffffff80004001 / 2b5**, from the actual unsupported
RoGetServerActivatableClasses request. RoRegisterActivationFactories is no
longer reached. Catalog activation/child claims still pass all 13 checks.
This is a truthful earlier boundary, **not startup success or a completed broker**.

## Artifacts and commands

* `rpcbindingcreate-failfast-stack.patch`: diagnostic-only kernelbase stack/
  exception-parameter logging; no change to exception behavior.
* `rpcbindingcreate-server-enum-failclosed.patch`: narrow enumeration boundary
  and factory-entry diagnostics; no shutdown-delay edits.
* `rpcbindingcreate-factory-boundary-test.c/.exe`: native/Wine standalone control.
* Owned logs: `rpcbindingcreate-failfast-{producer,exact}.{stdout,stderr}.log`,
  `rpcbindingcreate-server-enum-failclosed.{stdout,stderr}.log`,
  `rpcbindingcreate-factory-boundary-{native,wine}.log`,
  `rpcbindingcreate-failfast-twin-callsite.log`.
* Pinned upstream source snapshots: `rpcbindingcreate-upstream-eba89375-*`.

```bash
own="$HOME/xodus-runs/rpcbindingcreate-20261006-001"
export PATH=/opt/homebrew/bin:$PATH TMPDIR="$own"
cd "$own/build"
make -j3 dlls/combase/all dlls/kernelbase/all
# Matching prefix/server/DLL environment: rpcbindingcreate-shutdown-delay-result.md.
"$own/build/loader/wine" "$own/rpcbindingcreate-factory-boundary-test.exe"
```

Candidate DLL pins:

```text
0f68d8e14fd2a8fbf80064393cc2f30e34e6f2d634a444b196138c4c0c9d4549 combase.dll (includes fail-closed boundary)
6a39021c50ad00cf92fdffd01305901044617973cfa431d586db512328b89c46 kernelbase.dll (diagnostic-only change)
656a2fc6747b93ff8ea12623341c962b897f70dc695240cc1e5404cdfc122d01 rpcbindingcreate-failfast-stack.patch
0d719d65569cff847b8e1c42e78caa3ad3725fad748bcbe861c9cc27de52fb77 rpcbindingcreate-server-enum-failclosed.patch
```

Both patches pass reverse dry-run against the owned final sources.
Fresh deliver/cancel/reacquire shutdown controls were rerun after these changes
and still pass. Standalone native and Wine boundary controls both report
`FACTORY_BOUNDARY_PASS failures=0`.

The previously accepted shutdown-delay patch and its integration pins remain
separate. Do not replace the parent's complete engine with this diagnostic stage.
