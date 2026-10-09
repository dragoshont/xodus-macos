# Minimal observed RpcBindingCreateW prerequisite

The extra association/connection-policy rewrite was removed from the owned
source. Existing RPC behavior is reused: an active call owns its connection;
another concurrent/asynchronous call acquires another available connection or
creates a real new one. There is no per-binding serialization queue in this
path. NONCAUSAL is retained as the binding property, copied, and available through
SDK option 9 set/inquiry; no invented wire flag or new causal-context framework
was added.

The observed local V1 options Flags=1, ComTimeout=5, CallTimeout=0 now succeed.
Other unsupported flags/timeouts/security remain explicit failures.
Parent QOS inquiry changes are preserved. Strict actual controls report:

```text
DEFAULT_AUTH_STATUS=0 LEVEL=6 SVC=10 QOS=1,0,0,3
CREATE_CASE=6 STATUS=0 HANDLE=1
RPCBINDINGCREATE_TEST_PASS failures=0
```

Combined runnable patch: `rpcbindingcreate-minimal.patch`, relative to the matched
`cw-xbox-com-policy-20261006` source. It includes the parent QOS fix, the bounded
CreateW slice and minimal NONCAUSAL property handling. Unlike the old patch,
there are **no rpc_assoc.c/h changes**.

Owned engine/prefix:
`/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001/{build,prefix}`.

```bash
own=/Users/dragoshont/xodus-runs/rpcbindingcreate-20261006-001
cd "$own/build"
export PATH=/opt/homebrew/bin:$PATH TMPDIR="$own"
make -j3 dlls/rpcrt4/all
export WINEPREFIX="$own/prefix" WINESERVER="$own/build/server/wineserver"
export WINEDLLOVERRIDES='winemac.drv=;rpcrt4=b;rmclient=n'
export WINEDEBUG='-all,err+all,warn+rpc,trace+rpc'
export WINEDLLPATH="$own/build/dlls:/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows"
export DYLD_FALLBACK_LIBRARY_PATH=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64
"$own/build/loader/wine" "$own/parent-rpcbindingcreate-test.exe"
```

The genuine catalog-backed activation launcher was retried twice in the owned
prefix, including explicit matched WINESERVER selection. It stopped before
process creation:

```text
CATALOG_QUERY status=c0000225 bytes=8192
GENUINE_RETRY_EXIT=3
```

Therefore this follow-up does **not** claim that the actual RMCLIENT invocation
or original app constructor succeeded or reached a changed later boundary.
The observed flags prerequisite is fixed in actual binding controls; the current
genuine-retry gap is the owned catalog query failure. No next-class mapping,
original parent stage/prefix, GUI, generic infrastructure or auth policy changed.

Current pins:

```text
74ea2133d6f3e8f053c2fddcf4861025cb76437a47a09f7c1ffe210136a6eca4  isolated rpcrt4.dll
2e5680c89997c52988270583d9c73eec3306f510c50de3cc5244f67eeb3fa1b5  rpcbindingcreate-minimal.patch
```

Logs in the owned stage: rpcbindingcreate-minimal-build.log,
rpcbindingcreate-minimal-unit.log, rpcbindingcreate-minimal-app.stdout.log,
rpcbindingcreate-minimal-app.stderr.log.
