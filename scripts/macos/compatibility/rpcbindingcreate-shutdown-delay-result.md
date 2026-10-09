# Refcount-linked COM shutdown-delay candidate

Owned source/build only:
`/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001`.
No parent engine/prefix was changed.

The named CoRegisterServerShutdownDelay export is implemented in owned
combase.c using the **existing com_server_process_refcount and
registered_classes_cs**. Registration alone does not start a timer. Actual
CoReleaseServerProcess transition to zero arms delayed delivery; CoAddRefServerProcess
cancels pending idle delivery. NULL/0 detaches the registration, wakes and joins
its worker outside the COM lock, and closes real owned handles. Generation checks
prevent stale waits from signaling a later registration.

Events are validated by NtQueryEvent and independently duplicated. Each
registration owns its wake event, worker and DLL reference. SetEvent is real and
guarded against new references/cancellation. There are no user callbacks under
the lock or borrowed-handle delivery. Invalid handles/types/argument pairings
fail rather than returning fabricated success.

## Actual controls and gap

The owned Wine controls passed initial-no-timer, held-reference suppression,
release-to-zero delivery, immediate cancel, new-ref cancellation, replacement,
closed-original-handle ownership and 100 cleanup cycles:
`SHUTDOWN_DELAY_TEST_PASS failures=0 native=0`.

**Cleanup caveat:** Wine's GetProcessHandleCount returned 0/0, so this does not
independently prove kernel handle-count parity; it is not reported as a true
handle inventory. Cleanup paths are explicit in the source.

Independent genuine Windows controls confirmed initial registration and held
release delivery, but revealed a further terminal-lifetime condition:
after the first delivered idle shutdown, subsequent registration returns
`CO_E_SERVER_STOPPING` (`0x80080008`). The corrected candidate latches this
process-wide stopping state only after real successful SetEvent delivery.
NULL/0 remains successful cleanup and does not clear the terminal state.
Fresh native and Wine delivery, cancel and reacquire processes now all pass,
including terminal re-registration rejection and no later event delivery.
These are measured lifecycle controls, not a claim of full COM parity.

## Genuine product retry

Only after the Wine controls passed, the owned prefix's ordinal-69 bridge was
backed up to `combase-server-shutdown-delay-before-candidate.dll`, then replaced
with a no-code forwarder:

```def
LIBRARY "combase-server-shutdown-delay"
EXPORTS
    CoRegisterServerShutdownDelay=combase.CoRegisterServerShutdownDelay @69 NONAME
```

The parent cleanup-only bridge remains untouched.

With the genuine observed server argument, original catalog activation and
child claims again pass (13 checks, zero failures). The actual product log
previously reached:

```text
CoRegisterServerShutdownDelay: Shutdown delay registered, interval 2000; no initial timer.
Unhandled exception code c0000409
```

The corrected lifecycle candidate was retried headlessly with the same genuine
server argument. Catalog activation/child claims again pass all 13 checks;
original child exit remains `3221226505` (`0xc0000409`). The ordinal-69 E_NOTIMPL
is gone on this observed initial-registration path. The exact new fail-fast
producer was not resolved before the time ceiling. No render, constructor,
factory-registration, install or auth success is claimed. RoRegisterActivationFactories
was not rewritten.

## Runnable artifacts and pins

* `rpcbindingcreate-shutdown-delay.patch`: owned-stage delta against matched
  parent COM files; not an unrelated COM refactor.
* `rpcbindingcreate-shutdown-delay-test.c`: source in this directory.
* `rpcbindingcreate-shutdown-delay-test.exe`: owned stage and assigned scratch.
* `rpcbindingcreate-shutdown-delay-forward.def/.dll`: owned stage/scratch.

```bash
own=/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001
cd "$own/build"
export PATH=/opt/homebrew/bin:$PATH TMPDIR="$own"
make -j3 dlls/combase/all
# Use the matching loader/server/PE fallback environment documented in the
# server trace result; run the test in the owned prefix only.
"$own/build/loader/wine" "$own/rpcbindingcreate-shutdown-delay-test.exe"
```

Pins:

```text
ab8b54d27e954b3d7918b25212435591947b6a451d9d4f7ebdc5eb0c4bc20152  corrected lifecycle combase.dll
f5bed8c8447d72119243d1e530449e8f0d0c934a79f16dd14463c76c779df31d  ordinal-69 forwarder
ea133dccc768fcd7a93f818bbabc80df24038eeeeb125997a1a7c622cb9de3f6  corrected shutdown-delay patch
```

## Fresh-process lifecycle correction

The earlier one-shot interpretation is superseded: the parent's timeout
observation was contaminated by a previously delivered terminal shutdown.
Reacquisition cancels pending delivery; a later release-to-zero reschedules
until actual delivery makes the process terminal. Cancellation/generation
ownership is unchanged. Each scenario runs in its own fresh process.

```text
                                  Wine        genuine Windows
deliver RELEASED_WAIT             0           0
deliver ELAPSED                   150 ms      157 ms
cancel CANCEL_WAIT                258         258
reacquire EARLY_WAIT               258         258
reacquire REACQUIRED_WAIT          258         258
reacquire SECOND_RELEASE_WAIT      0           0
post-delivery TERMINAL_REGISTER    80080008    80080008
post-delivery TERMINAL_WAIT        258         258
```

All six fresh native/Wine runs pass. The extended Wine fixture also passes
replacement, error/type/ownership and 100 cancel/cleanup cycles before its final
delivered shutdown. It no longer incorrectly expects fresh lifecycle semantics
after terminal delivery. The historical fixture filename contains "oneshot";
its assertions now require correct rescheduling and terminal-only one-shot delivery.

Run separate processes:

```bash
for scenario in deliver cancel reacquire; do
  "$own/build/loader/wine" "$own/rpcbindingcreate-shutdown-oneshot-test.exe" "$scenario"
done
# Genuine Windows: same executable, each scenario separately, append --native.
```

Logs:
`rpcbindingcreate-shutdown-reschedule-{deliver,cancel,reacquire}-wine.log`,
`rpcbindingcreate-shutdown-reschedule-{deliver,cancel,reacquire}-native.log`,
`rpcbindingcreate-shutdown-reschedule-extended-wine.log`,
`rpcbindingcreate-shutdown-reschedule-app.{stdout,stderr}.log`,
`rpcbindingcreate-shutdown-reschedule-build.log`.

Evidence: rpcbindingcreate-shutdown-delay-build.log,
rpcbindingcreate-shutdown-delay-wine.log,
rpcbindingcreate-shutdown-delay-native.log,
rpcbindingcreate-shutdown-delay-app.stdout.log,
rpcbindingcreate-shutdown-delay-app.stderr.log.
