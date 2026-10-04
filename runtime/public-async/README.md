# Public async and gaming-shim boundary

This is an original, isolated **integration candidate**, not a shipping runtime,
Store authorization or gameplay certification. It closes a concrete public
shim blocker: all 23 XAsync/XTaskQueue methods were stubs, so `XUserAddAsync`
could not reach the existing RPS client.

## Source and licensing

The scheduler is Microsoft's existing implementation, not a new scheduler.
`dependency.json` pins public `microsoft/libHttpClient` commit
`7ead73f6389271c2dc2cb10cdafcc587b899bd8a` and MIT `LICENSE.md` Git blob
`de9d570d38a39d767afb071db3e9fd8fd48358cf`. Only `AsyncLib.cpp`,
`TaskQueue.cpp`, `ThreadPool_win32.cpp` and `WaitTimer_win32.cpp` are compiled,
with their public headers. HTTP and WebSocket providers are excluded.

Original support, bridge, build tools and checks are GPL-3.0-only, under the
repository's `LICENSE`. Retain Microsoft's MIT license and the public shim/Wine
LGPL notices when packaging the combination; do not label the entire result
MIT-only or LGPL-only. Static compiler runtimes also retain their applicable
runtime licensing/exception obligations. This candidate is not a distribution
package, signing/notarization pipeline or supported upstream version pair.

The public shim is pinned to `xodus-gaming/xgameruntime`
`44d97de1e084be61243323062ed49e307d28728c`; it requires the exact existing
`public-rps` overlay first. The async applicator checks the public revision and
these exact input Git blobs before writing:

| Input | Git blob after the existing RPS overlay |
|---|---|
| `xthreading.c` | `6d62fe1084639954311f347785787ffa64aaad42` |
| `xuser.c` | `679127a722557eea6c01e90b62b48e963772576c` |
| `Makefile.in` | `e78342f99f3d8ec7b3d46cf839ad70a89ea2c664` |

Changed revisions/files, aliased roots and existing bridge files are refused;
reapplication is not an update mechanism. Use a new disposable public checkout,
not an app's approved engine or a private runtime.

## Actual integration

The bridge loads only `xodus_async.dll` beside its own actual gaming module,
using an absolute sibling path and restricted DLL search flags. Initialization
checks the ABI version, block/provider sizes, three provider field offsets and
all 23 entry points. Failures are explicit HRESULTs and payload-free diagnostics;
there is no default DLL-search fallback. The successful core reference is retained
for process lifetime, keeping its async state valid across individual COM calls.
This does not promise safe gaming-DLL unload while its own providers are active.
Both checker variants also retain their successfully loaded modules until process
exit. A termination callback and reference-only queue closure do not prove that
a native worker has returned from DLL code. Checks verify the selected module
remains loaded after queue closure; no early-success or error path unloads it.

COM vtable order, padding, reference counting and the three existing
time-sensitive-thread methods are preserved. Windows callbacks and return values
are exercised through the real COM interface, not just matched by header sizes.
The existing user initializer acquires RPS **before** default Xbox endpoint HTTP,
so malformed, expired and missing configured RPS fail before account HTTP.
Acquired tickets follow the cleanup path on subsequent HTTP failure. Coupled
provider fixes release failed user state before completion, transfer successful
handle ownership only on result consumption, and release unconsumed handles;
the user object's final reference now releases its allocation.

The build rejects changed/untracked public source and ignored files capable of
shadowing headers, and verifies the original MIT license. The original support
maps allocation/other exceptions to typed failure HRESULTs without logging
provider context or exception payloads. Both core and original helpers link
compiler runtimes statically; actual imports contain Windows CRT APIs, not an
unshipped `libwinpthread`/`libgcc`/`libstdc++` DLL.

## Checks and bounded evidence

On the owned macOS public Wine candidate, the strict actual Win64 build and:

- Four direct-core manual, threadpool, delayed and cancellation cases pass,
  including exact result data, one completion/cleanup/termination, and six
  independent ABI-field mismatches returning `ERROR_REVISION_MISMATCH`.
- The same four cases pass through the actual public gaming DLL's COM vtable,
  with preserved true/false time-sensitive state.
- Three real loader refusals verify absent core, incompatible ABI and missing
  exports. Each returns the exact cached failure on repeated calls, without
  falling back to another DLL. Incomplete fixture DLLs exist only for these
  negative checks and are never runtime candidates.
- Three actual `XUserAddAsync`/`XUserAddResult` malformed, expired and missing
  RPS cases pass with their exact error, one completion and a null user handle.
  The owned peer validates the exact synthetic request, not real credentials.
- Thirteen exact-source/license/overlay guards and nine component/lifetime
  guards pass without executing a game or accessing an account. Two of these
  regressions check the checker has no explicit unload and that its actual
  post-close module-retention assertion is present.

The shared reviewed runner keeps its PID-bound readiness, owned private prefix,
specific-PID graceful shutdown and uncertain-prefix retention. The exact selected
native ntdll mapping check finds no remaining clients. No default broker,
personal credentials, Store login, successful synthetic account tickets, real
game, private runtime, outside pixels or launcher Account flow was used.

Observed unsigned components (hashes identify these artifacts, not reproducible
builds):

| Component | SHA-256 |
|---|---|
| Async core DLL | `36b74899540eca304a2e51b107aa49cef3379b1c0f934e57b908657a304f1fca` |
| Direct-core PE check | `66856a7def3425f3483cbf9fba3c57c132c1b82b43cd71912b1ad7ee9ecb9eaf` |
| Public gaming DLL | `7a062d7a837dc5354ba351304ee19246f8f1803f220c30eb4c4373cea5d92f79` |
| Gaming COM PE check | `22fb5ffd3527ba2be2a458f5815c83abddf1c710e5e7b8de36fd0a0e160e3277` |

## Developer reproduction

Use caller-owned, non-aliased paths. First apply the existing public RPS overlay
to a pristine pinned shim, then:

```sh
python3 -B apply_to_public_shim.py "$shim"             # read-only validation
python3 -B apply_to_public_shim.py "$shim" --apply
python3 -B build_core.py "$public_async_source" "$new_core_output"

# In the already configured public Wine build, after its guarded platform overlay:
make -j2 dlls/xgameruntime/all
python3 -B build_shim_check.py "$shim" "$wine_build" \
  "$new_core_output" "$new_shim_check_output"
python3 -B check_shim.py "$owned_root" "$wine_build/loader/wine" \
  "$wine_build/server/wineserver" "$new_shim_check_output/xodus-async-smoke.exe" \
  --library-directory "$owned_dependency_library_directory"

python3 -B test_source_guards.py --shim-source "$public_shim_fixture" \
  --async-source "$public_async_fixture" -v
python3 -B test_components.py -v
```

Source tests use new disposable local clones of complete pinned public fixtures.
A sparse/promisor checkout missing unrelated objects is not a complete clone
fixture; obtain a bounded full public snapshot instead of weakening the guard.
Hosted checks cover the actual cross-build and native source/component refusals,
not gaming COM execution under Wine.

Retained report 33 identified **R17**, a medium-severity standalone checker
unload race: termination counters and handle closure do not synchronize native
worker return. This was a source-verified interleaving, not an observed crash.
The checker now retains modules through process exit, with actual direct-core
and gaming checks rerun; the core and gaming DLL bytes are unchanged. The
corrective delta is awaiting the same retained reviewer's closure. Older checker
artifacts remain historical observations, not safe-unload proof.

Real Store issuance,
current entitlement, authorized manifests/installations, service/profile/prefix
pairing, supported runtime lifecycle, window presentation, licensed gameplay
and signed distribution remain separate gates. This must not enable **Play** or
turn catalog/marker evidence into ownership or verified installation.
