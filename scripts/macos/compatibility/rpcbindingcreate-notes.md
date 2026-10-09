# Bounded fast local RpcBindingCreateW candidate

This is an isolated Wine-source candidate, **not an integration-ready result**.
The time ceiling stopped work before genuine Xbox/RMCLIENT retesting. No server,
HAM/UserManager endpoint, GUI rendering, authentication, or installation success
is claimed.

## Isolation and patch

APFS copies of the exact matched parent source/build and requested product prefix:

`/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001/{source,build,prefix}`

Parent:
`/Users/dragoshont/xodus-runs/cw-xbox-com-policy-20261006`

Only the copied source was edited. The patch touches rpcdce.h, rpc_binding.c/h,
rpc_transport.c and rpcrt4.spec. It does not alter the parent's engine/prefix or
the existing true-peer sh_token/TOKEN_QUERY/DuplicateHandle implementation.

`rpcbindingcreate-local.patch` is relative to this exact matched parent source,
not a generic clean Wine release. It adds the SDK V1 layouts and Unicode export,
real existing RPC string-binding/association creation, auth/QOS forwarding,
owned static thread-token snapshots, connection-open impersonation/restoration,
effective-only pipe SQOS, copy/free ownership, and fast-static reset preservation.
Classic resets still remove the endpoint; dynamic fast resets also clear a
resolved endpoint.

Defaults use privacy/WINNT, static identity and impersonation, matching native
Windows inquiry. Unsupported non-local protocols, nonempty network addresses,
unknown template/security versions/flags, advanced QOS, explicit credentials,
non-default timeout or options flags fail explicitly. Existing option/timeout
helpers are success-returning stubs, so they were **not** used to silently accept
unimplemented behavior. This bounded candidate does not support those overrides.

## Actual comparisons and remaining verification gap

`rpcbindingcreate-test.c` compiles against SDK-compatible local V1 layouts and
queries the real RpcBindingCreateW export.

Native Windows observed:

* Protocol 3: success; 0/1/2/4/5: status 1764.
* Invalid template/security/options versions, reserved member or unknown flags:
  87.
* Default authentication inquiry: level 6, service 10, QOS
  Version 1 / Capabilities 0 / Static 0 / Impersonate 3.
* Native accepts noncausal options and nonzero call timeout; the candidate
  deliberately returns 1764 because existing Wine helpers do not implement them.

Isolated Wine build passes valid/negative creation, static fast copy/reset/free,
dynamic creation/reset/free, and unchanged classic reset controls:
`RPCBINDINGCREATE_TEST_PASS failures=0`.

**Not fully verified:** the default-auth inquiry with a QOS output pointer
actually returns 1702 in the matched Wine implementation because
RpcBindingInqAuthInfoExW explicitly lacks QOS inquiry. The control records that
failure rather than asserting native equivalence. The binding carries the QOS
and auth objects, but inquiry and live token/security transport conformance
remain verification gaps. The genuine RMCLIENT/Xbox follow-up was not run
before the hard time ceiling. Do not describe this as verified product progress.

## Reproduction

```bash
own=/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001
cd "$own/build"
export PATH=/opt/homebrew/bin:$PATH TMPDIR="$own"
make -j3 dlls/rpcrt4/all

export WINEPREFIX="$own/prefix"
export WINEDLLOVERRIDES='winemac.drv=;rpcrt4=b'
export WINEDEBUG='-all,err+all,warn+rpc,trace+rpc'
export WINEDLLPATH="$own/build/dlls:/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows"
export DYLD_FALLBACK_LIBRARY_PATH=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64
"$own/build/loader/wine" "$own/rpcbindingcreate-test.exe"
```

Native comparison executable and logs are retained in the assigned Windows
session scratch `files\rpcbindingcreate`; use `--compare` for observational
native cases without requiring unsupported Wine options.

Pins:

```text
788e1ebfe5fb6399bdd02a971cc49ea2cb834fcb328172f43706089dd57bb4d8  isolated rpcrt4.dll
e2da0838dc688a15f6f260135e615f3c94137dec15f1e0e2d70ea4aa86147c92  rpcbindingcreate-local.patch
```

Evidence: rpcbindingcreate-native.log, rpcbindingcreate-wine.log,
rpcbindingcreate-build.log. No broad dependency loop, new worker, public push,
release, registry-policy copying, or parent integration was performed.
