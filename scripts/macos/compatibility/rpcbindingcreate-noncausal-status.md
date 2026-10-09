# NONCAUSAL follow-up candidate: not integration-ready

The parent's actual QOS inquiry fix was fetched from the current owned source
before editing and preserved. The isolated build succeeded and the parent's
strict test still reports:

```text
DEFAULT_AUTH_STATUS=0 LEVEL=6 SVC=10 QOS=1,0,0,3
CREATE_CASE=6 STATUS=0 HANDLE=1
CREATE_CASE=7 STATUS=1764 HANDLE=0
RPCBINDINGCREATE_TEST_PASS failures=0
```

Case 6 is NONCAUSAL; case 7 is unsupported nonzero call timeout.

Candidate source remains under:
`/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001/source/wine`.
Only that owned source/build was changed, never the parent original engine.
Current Windows edit mirrors are retained in the assigned session scratch
`files\rpcbindingcreate`.

The candidate adds a retained/copied binding NONCAUSAL property, option 9 set/
inquiry, and passes that property to the actual association connection path.
Independent calls bypass idle-connection reuse and create their own real
authenticated transport connection. Completed independent connections retain
one association lifetime pin; additional completed connections are released.
Timeout flags outside this bounded subset and unsupported security still fail
explicitly. Existing true-peer token code is untouched.

Official semantics:
<https://learn.microsoft.com/en-us/windows/win32/rpc/causal-ordering-of-asynchronous-calls>
and <https://learn.microsoft.com/en-us/windows/win32/rpc/binding-option-constants>.

**Unfinished verification:** no actual concurrent-dispatch/context-handle
regression was completed, and the genuine RMCLIENT/Xbox follow-up was not run.
Creation success alone is not proof of full NONCAUSAL transport equivalence.
The association lifetime/correlation changes therefore remain an unreviewed
candidate; do not integrate on the strength of the existing creation test.

The actual activation launcher was found at
`/Users/dragoshont/xodus-runs/cw-package-activation-20261006/package-activation-launch.exe`,
but exact registered package arguments were not resolved before the bounded
session ended. Subsequent SSH attempts timed out, preventing final patch
export/pinning and product retesting. No changed product boundary is claimed.

Evidence in the owned stage:
`rpcbindingcreate-noncausal-build.log`,
`rpcbindingcreate-noncausal-unit.log`.

The previous `rpcbindingcreate-local.patch` predates this follow-up and does not
contain these changes or the parent's QOS fix. Do not use it as the follow-up
patch. No GUI, new/nested worker, public push, release or auth downgrade occurred.
