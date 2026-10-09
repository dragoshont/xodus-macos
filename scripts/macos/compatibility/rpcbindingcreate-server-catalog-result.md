# Authoritative native server-class query candidate

Follow-up narrow source fixes and registration proof/blocker:
`rpcbindingcreate-registration-result.md`. Its helper pin supersedes the
initial helper pin below; native mapping data and original hive hash are unchanged.

Owned stage only: `~/xodus-runs/rpcbindingcreate-20261006-001`.
No parent engine/prefix, native installed-package state, GUI, authentication,
token replay, injection or elevation was changed.

## Authoritative mapping, not manifest inference

The installed package's activation-only metadata file is:

```text
C:\ProgramData\Microsoft\Windows\AppRepository\Packages\
Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe\ActivationStore.dat
```

It is a registry hive. Read-only native RegLoadAppKey returned access denied (5);
no privilege escalation or writable mount was attempted. A bounded read-only
cell parser inspected its 546 keys without mounting/copying the hive.
The original file hash was rechecked unchanged:

```text
97c56d96cb59ed24b23e054d7135fb98d970d1259ba5de16087a747e41d9f9c0
```

Root `{E5522658-D675-4998-AB02-47EA5D22FF67}\Server\`
`Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca`
contains an actual **REG_MULTI_SZ (type 7) ActivatableClasses** value, ordered:

1. `Microsoft.Xbox.AppL.AppXj1x023cvxecgx8b22mdm03jfv63tazgg.mca`
2. `Microsoft.Xbox.AppL`
3. `Microsoft.Xbox.AppL.AppXabakgp1hk450qzwqpznhnpzk90j2c5zr.mca`
4. `Microsoft.Xbox.AppL.AppX7zsk1ekc4wcd214w25p0bfcpxzgax7qg.mca`

All four corresponding ActivatableClassId records independently have
ActivationType=1 and Server equal to this exact server. The Server record has
ExePath=XboxPcApp.exe and
AppUserModelId=Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.AppL.
XboxPcApp.App is **not substituted as a class list**.
Only these activation/code metadata fields were exported to the owned stage;
no account, dependency-SID or authentication data/hive dump was exported.

Signed native combase.dll SHA-256:
`39a2e2ba1ca6012953c94c10011fff5a18783120a2a34f5b66deb2d374a362c9`.
Its actual RoGetServerActivatableClasses export is RVA d8000. Static tracing
shows resolver access at +d8150, metadata getter vtable slot +78 at +d80ab,
and conversion of a returned MULTI_SZ into an allocated HSTRING array at
+d8240. No arbitrary process memory dump or live process injection was used.

## Bounded implementation and persistent state

`rpcbindingcreate-server-catalog.c` is included by the narrow roapi patch.
It reads real current-process WIN://SYSAPPID token attributes for package-full
name and manifest app id. It additionally queries the existing actual
NtXodusQueryRegisteredPackage catalog and requires the current executable path
to equal that registered image. Missing identity/registration cannot obtain
another application's class metadata.

The data source is a new, explicitly staged metadata-only subtree:

```text
HKLM\Software\Wine\Xodus\ActivationMetadata\
<package-full-name>\<manifest-app-id>\<bare-server-name>
```

Only this owned-prefix key was created. Its actual REG_MULTI_SZ and provenance
hash are derived from the verified native records. No hardcoded Xbox class
list appears in the provider. This is not a native deployment database, license,
installed-package fabrication or factory registration.

The provider validates metadata type/size/termination, allocates the array with
CoTaskMemAlloc, creates real owning HSTRINGs, and unwinds partial failure.
Unpackaged callers return measured CLASSNOTREG; unsupported/malformed states
fail explicitly. Non-not-found underlying token/catalog/key errors are preserved.

## Controls and actual changed product boundary

* Native Windows and Wine: exact four-class order, HSTRING contents and
  create/delete/CoTaskMemFree over **500 cycles**, all pass.
* Decoder negative controls: odd/truncated/zero lengths, missing terminal NUL,
  empty class, NULL data/output; outputs clear and errors explicit.
* Standalone unpackaged API controls retain native 80040154/zero outputs;
  zero-count factory validation retains native 80070057.
* Genuine activated app with missing class property: explicit 80070057,
  not success/empty inventory; original property restored.
* Genuine activated app requesting unknown server: 80040154; no record invented.
* Fresh shutdown-delay deliver/cancel/reacquire regressions still pass.

Restored final genuine Xbox retry resolves **four real classes**, then reaches:

```text
RoRegisterActivationFactories classes=<real allocated array>
callbacks=<real native callback array> count=4 cookie=<native output>
Out-of-process WinRT factory registration transport is unsupported;
no factories registered.
```

The new actual failing method is **named RoRegisterActivationFactories**,
E_NOTIMPL with **valid count=4**, not zero-count validation or server lookup.
Fail-fast parameters **7 / ffffffff80004001 / 27c**; child exit c0000409.
Registered activation/child claims still pass 13 checks. No render, broker,
constructor, installation or authentication success is claimed.

Upstream at already-checked eba89375a0515957701928faac0f5007ef638b04 contains
only a success stub for this reached registration method; it was not backported.
No Office/fork search or broker transport rewrite was attempted.

## Integration artifacts

* `rpcbindingcreate-server-catalog.patch`: against owned pre-mapping fail-closed roapi.
* `rpcbindingcreate-server-catalog.c`: copy beside roapi.c before building.
* `rpcbindingcreate-native-hive-query.py`: original read-only metadata parser.
* `rpcbindingcreate-native-activation-mapping.c`: native read-only mount control.
* `rpcbindingcreate-server-catalog-test.c/.exe`: allocation/property fixture.
* Owned-stage `rpcbindingcreate-native-server-mapping.json`: exact records/types/hash.
* Owned-stage `rpcbindingcreate-authoritative-server-mapping.reg`: only scoped key.
* Owned-stage `rpcbindingcreate-authoritative-classes.bin`: exact allocation fixture.

```bash
own="$HOME/xodus-runs/rpcbindingcreate-20261006-001"
export PATH=/opt/homebrew/bin:$PATH TMPDIR="$own"
cd "$own/build"
touch "$own/source/wine/dlls/combase/roapi.c" # ensure the included helper is recompiled
make -j3 dlls/combase/all
# Matching loader/server/prefix/DLL environment from earlier result files.
"$own/build/loader/wine" reg.exe import \
  'Z:\Users\dragoshont\xodus-runs\rpcbindingcreate-20261006-001\rpcbindingcreate-authoritative-server-mapping.reg'
"$own/build/loader/wine" "$own/rpcbindingcreate-server-catalog-test.exe" \
  'Z:\Users\dragoshont\xodus-runs\rpcbindingcreate-20261006-001\rpcbindingcreate-authoritative-classes.bin'
```

Original launcher's final argument remains the **bare** server name.
Logs: `rpcbindingcreate-server-catalog-{final,missing,unknown}.{stdout,stderr}.log`,
`rpcbindingcreate-server-catalog-allocation-{native,wine}.log`,
`rpcbindingcreate-server-catalog-unpackaged.log`,
`rpcbindingcreate-server-catalog-shutdown-*.log`.
This stage includes earlier diagnostic kernelbase code; integrate the bounded
source/data candidate, not an engine-wide replacement.

Final verified pins (also in owned `rpcbindingcreate-server-catalog-pins.txt`):

```text
1c1af5e596710cee5243a7e8c40be3a286f45852c0710f90703e77917becd714 combase.dll
b23620fdf79d280f62827bf64091ab240af80d23f0794c65469a3be0a4eacee3 rpcbindingcreate-server-catalog.c
fb594e83dff985a8c58555d63fa8dc6054a3135270832f4f0ec20d2a0a4d8b02 rpcbindingcreate-server-catalog.patch
ec79cfc741a1578d292ade0aa34807f32b760f33cb38ba35668cc39ba583306e authoritative metadata .reg
```
