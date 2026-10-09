# Genuine InstallService hosting: isolated lab and measured blockers

## Genuine Windows hosting contract

Read-only inspection established:

* Service: InstallService, running on the user's actual Windows system.
* ImagePath: `C:\Windows\System32\svchost.exe -k netsvcs -p`.
* ObjectName: LocalSystem; Type 16 (own process); Start 3; dependency rpcss.
* Parameters: ServiceDll `C:\Windows\system32\InstallService.dll`,
  ServiceMain `ServiceMain`, ServiceDllUnloadOnStop 0.
* Internal class:
  `Windows.Internal.InstallService.Control.InstallServiceControl`,
  ActivationType 1, Server InstallService, TrustLevel 0, ActivateOnHostFlags 1.
* WinRT server: ServerType 2, ServiceName InstallService, IdentityType 1,
  Identity `nt authority\system`, with the genuine binary Permissions descriptor.

The signed DLL exports ServiceMain, DllGetActivationFactory, DllGetClassObject,
and DllCanUnloadNow. It **does not** require a SvchostPushServiceGlobals export.
It imports real RegisterServiceCtrlHandlerExW/SetServiceStatus and query APIs.
No service status or factory registration result may be manufactured.

Exact Windows service DACL:

```text
D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)
```

The binary WinRT server Permissions descriptor and this service policy are
retained in the assigned scratch/lab. They are public security configuration,
not copied account credentials/tokens. No permissions were broadened, no
Microsoft identity was fabricated, and no policy check was bypassed.

## Fresh lab, not a clone

Dedicated CrossOver bottle:
`XodusInstallServiceLab20261006-001`.

Prefix:
`/Users/dragoshont/Library/Application Support/CrossOver/Bottles/XodusInstallServiceLab20261006-001`.

Stage:
`/Users/dragoshont/xodus-runs/package-manager-service-lab-20261006-001`.

It was created from the **fresh win10_64 template**, not copied from the original
experimental bottle or the parent's token-runtime APFS clone. Only the pinned
native code/control/state-provider artifacts were staged. There are no copied
accounts, authentication caches, game data, native KernelBase, or native ntdll.
The original bottle, GUI, parent runtime build and clone remain untouched.

Lab service registration preserves the actual image command (including `-p`),
LocalSystem identity, dependency rpcss, native ServiceMain symbol, and exact
WinRT server/class metadata and Permissions descriptor. ServiceDll points to
the existing single-import-derived InstallService lab client; its one state
import calls the verified narrow provider from `package-manager-state-notes.md`.
The derived lab DLL is explicitly **not vendor-signed**, with its earlier
source/modified hashes and IAT receipt retained. Original signed sources remain
unchanged.

Wine's sc.exe dependency parser initially produced a malformed REG_MULTI_SZ;
the lab dependency was corrected using reg.exe to the genuine single entry
`rpcss`. Unsupported sc.exe security commands were replaced by direct public
SDK calls, not by skipping security.

## Invoked security blocker: no secure service start

The new `package-manager-service-control.cpp` calls the **actual SCM SDK APIs**.
It first requires the real Windows service DACL to round-trip exactly:

```text
REAL_SET_SERVICE_DACL=1 ERROR=0
REAL_QUERY_SERVICE_DACL=1 ERROR=0
ACTUAL_SERVICE_DACL=D:
EXPECTED_SERVICE_DACL=D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)...
GENUINE_SERVICE_DACL_ROUNDTRIP=FAIL
```

The runtime acknowledged SetServiceObjectSecurity but returned an **empty DACL**,
not the requested policy. This is a measured invoked-API failure, not an unused
import warning. A successful API return is not proof the policy is stored or
enforced.

**The genuine service was therefore not started.** Starting it under a
non-equivalent or unverified policy would violate the no-skipped-policy
boundary. QueryServiceStatusEx confirmed:

```text
REAL_SERVICE_STATE=1 PID=0 WIN32_EXIT=1077 SERVICE_EXIT=0 CHECKPOINT=0
```

That is STOPPED, no service PID, never-started status. No ServiceMain execution,
RUNNING status, service-owned factory, or native service activation is claimed.

The control implements real --start/--stop operations for later use once policy
support exists; it does not synthesize SERVICE_RUNNING or service events.

## Invoked WinRT registration/transport blocker

The actual server/class registration was installed in the lab with
ActivationType 1 and ServerType 2, **without an invented in-process DllPath**.
Real RoGetActivationFactory for InstallServiceControl returned `0x80040150`.
The runtime still searches only for DllPath, rather than starting/connecting to
the configured service server.

A separate same-process registration conformance control passed a callback that
delegates **unchanged to the genuine Microsoft DLL's real
DllGetActivationFactory** for its public AppInstallManager class. The direct
genuine factory returned S_OK. There is no mock factory/object in this test.

Actual SDK results:

```text
DIRECT_GENUINE_FACTORY_HRESULT=0x00000000
REAL_RO_REGISTER_HRESULT=0x00000000 COOKIE_CHANGED=0
REAL_REGISTERED_FACTORY_LOOKUP=0x80040154 GENUINE_CALLBACK_CALLS=0 OBJECT=absent
```

RoRegisterActivationFactories returned S_OK without creating a registration
cookie, invoking the genuine callback, or making the factory discoverable.
The control reports failure, not registration success.

Read-only CodeWeavers Wine 11 source inspection corroborates these measured
limitations:

* `dlls/combase/roapi.c`: registration function is a S_OK-returning stub.
* RoGetActivationFactory/get_library_for_classid searches activation context or
  DllPath; no server/SCM out-of-process dispatch path.
* `programs/svchost/svchost.c` genuinely loads ServiceDll with altered search path,
  resolves ServiceMain, and uses StartServiceCtrlDispatcherW. It does not
  implement the Windows `-p` option; the option was not silently removed.
* Wine 11 SCM RPC Set/QueryServiceObjectSecurity source endpoints are
  unimplemented. The running CrossOver build's policy mismatch was separately
  measured, not inferred from the source version.

Implementing a fake Microsoft factory, accepting the runtime's no-op S_OK,
loosening ACLs, removing policy flags, or injecting an in-process DllPath would
not constitute genuine service hosting. None was done.

## Builds, controls and evidence

Build the native SCM/WinRT control in the existing code stage:

```bash
export TMPDIR=/Users/dragoshont/xodus-runs/package-manager-service-lab-20261006-001
/opt/homebrew/bin/x86_64-w64-mingw32-g++ -std=c++11 -Wall -Wextra -Werror \
  -Wno-cast-function-type -O2 -static -municode package-manager-service-control.cpp \
  -o "$TMPDIR/code/package-manager-service-control-x64.exe" \
  -lole32 -lruntimeobject -luuid -ladvapi32
```

Run **only with the explicit lab bottle**, headless:

```bash
lab=/Users/dragoshont/xodus-runs/package-manager-service-lab-20261006-001
cd "$lab"
win_lab="Z:${PWD//\//\\}"
CX_ROOT=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver
run() {
  "$CX_ROOT/bin/wine" --bottle XodusInstallServiceLab20261006-001 --no-gui \
    --dll 'winemac.drv=;umpdc=n;rmclient=n;api-ms-win-ham-apphistory-l1-1-0=n' \
    --debugmsg '-all,err+all,trace+service,trace+combase' "$@"
}
run "$win_lab\code\package-manager-service-control-x64.exe" --security \
  'D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)'
run "$win_lab\code\package-manager-service-control-x64.exe" --query
run "$win_lab\code\package-manager-service-control-x64.exe" --activate
run "$win_lab\code\package-manager-service-control-x64.exe" --register-native \
  "$win_lab\code\package-manager-InstallService-state-bridge-x64.dll"
# DO NOT use --start until policy round-trip/enforcement is genuinely implemented.
```

`package-manager-service-host-test.py` validates the actual failure evidence,
real genuine callback provenance/control result, never-started service status,
and state-provider pin. Its pass means **blocked hosting was reproduced**, not
service/factory activation or installation success.

Evidence logs:
`package-manager-service-lab-policy-test.log`,
`package-manager-service-lab-activation.log`,
`package-manager-service-lab-registration-test.log`,
`package-manager-service-lab-status-before.log`.
The original native registrations, service DACL and binary server policy are
retained alongside them. No broker purchase/install/sign-in request was made.

Next real prerequisite: service-object security persistence/enforcement plus
actual WinRT registration and out-of-process service activation transport.
This is not another missing SDK/WinMD contract, and no list of merely unused
native imports is offered as the reached blocker.

Verified control SHA256:
`1fcc85b4093e4e108740df2b61d82a3775aededc264e36ce8b3d941536dc3e66`.
Verified state helper SHA256:
`17172e2eb646fd630d7f6df7d127dbf0995ff765fa908e8dc80fef84962dda71`.
