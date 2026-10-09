# Catalog review fixes and bounded registration proof

Owned stage only: `~/xodus-runs/rpcbindingcreate-20261006-001`.
Parent sources/prefix and native installed-package/catalog state are unchanged.
No GUI, authentication, elevation, injection, token replay or fork search.

## Requested narrow catalog corrections

`rpcbindingcreate-server-catalog.c` now:

* preserves HRESULT_FROM_WIN32 for non-missing RegQueryValueEx failures,
  including access/resource/read errors, on both size and data reads;
* retains E_INVALIDARG for the measured missing ActivatableClasses property
  and malformed type/size/termination;
* logs successful actual enumeration with **TRACE**, not ERR.

`rpcbindingcreate-server-catalog-review-fixes.patch` contains only those changes
against the accepted helper (original SHA-256 b23620fdf79d280f62827bf64091ab240af80d23f0794c65469a3be0a4eacee3).
Use the corrected complete helper for a source splice. The registry data and
native ActivationStore source/hash/class order have not changed.

Allocation/error fixture: 500 cycles pass. Unpackaged/native-error fixture and
all three fresh shutdown lifetime cases still pass.
Actual original Xbox retry still enumerates four real classes and reaches
RoRegisterActivationFactories(count=4), then E_NOTIMPL/fail-fast 7/80004001/27c.

## Native apartment/cookie/ownership observations

The console uses only the four verified native class names and callback
delegation to genuine system twinapi.appcore.dll DllGetActivationFactory.
Actual Windows observations:

```text
WITHOUT_APARTMENT hr=800401f0 cookie=NULL callbacks=0
UNPACKAGED_MTA_REGISTER hr=80040154 cookie=NULL callbacks=0
GENUINE_TWIN_FACTORY [all four names] hr=80040111 factory=NULL
```

No successful native registration occurred and no valid cookie needed revocation.
RoRevokeActivationFactories(NULL) completed. These negative controls do not
establish valid-registration callback ownership or post-revoke dispatch parity.
Static native code shows cookie clearing at export +10242c, apartment validation
and temporary class-info allocation/resolution before +102517 registration;
temporary class-info cleanup is reached on error.

The separate `rpcbindingcreate-registration-errors.patch` adds only the measured
apartment requirement and zeroed failure cookie to owned RoRegisterActivationFactories.
It does **not** store callbacks, register a broker, or return successful registration.
Wine controls now match the no-apartment status/cookie and return truthful
E_NOTIMPL with cookie=NULL after MTA initialization.

## Real paired-process result: registration is NOT proven

The control launches a real server console, verifies it remains live, then
launches a distinct client process in the same owned prefix.

```text
server: MTA registration E_NOTIMPL; cookie=NULL; callbacks=0
client: all four real class requests 80040154; factory=NULL
both processes exit normally
```

No callback was invoked by the paired request and no IActivationFactory was
returned. Therefore no registration success is claimed and no callback-only
success-shaped implementation was supplied.

### Concrete existing-COM transport seam

Wine's existing path is:

```text
CoRegisterClassObject(REFCLSID, IUnknown, CLSCTX_LOCAL_SERVER, ...)
  -> apartment_get_local_server_stream
  -> rpc_register_local_server
  -> irpcss_server_register(GUID, flags, marshalled object, cookie)

rpc_get_local_class_object(REFCLSID, REFIID)
  -> irpcss_get_class_object(GUID)
  -> CoUnmarshalInterface(IID_IServiceProvider)
  -> IServiceProvider::QueryService(CLSID, IID)
```

`include/wine/irpcss.idl` has GUID-keyed register/revoke/get operations, not
package-scoped HSTRING activation-class operations. Current RoGetActivationFactory
looks up in-process activation-context/registry DLL paths and has no RPCSS
out-of-process WinRT branch.

The native authoritative records were re-read including **all value names/types**:
each of the four ActivatableClassId keys contains only ActivationType (DWORD)
and Server (SZ), not a CLSID field. Inventing/hash-guessing CLSIDs to fit RPCSS
would not establish native identity. Also, current CoRegisterClassObject returns
S_OK after rpc_register_local_server even if that call failed; a bare success
return cannot prove the transport works. That unrelated path was not refactored.

## Direct genuine app-callback prerequisite

The original app supplies callback twinapi.appcore.dll **RVA 2fc90** for all
four classes. Static code and original import parsing establish its first call:

```text
callback +2fcce -> delayed IAT 242408
api-ms-win-core-winrt-registration-l1-1-0.dll!
RoGetActivatableClassRegistration(HSTRING, registration-object output)
```

An isolated headless console verified the actual callback instruction signature,
then directly invoked that original Microsoft callback with a verified class
name. It actually aborted:

```text
wine: ... unimplemented function combase.dll.RoGetActivatableClassRegistration
```

This is a **diagnostic direct callback**, not an original-app OOP delivery or
a successful paired request. No private registration-object IID/vtable, class
identity, factory or broker state was guessed.

Current pinned upstream eba89375a0515957701928faac0f5007ef638b04 also exports
RoGetActivatableClassRegistration as a stub with no roapi implementation.
RoRegisterActivationFactories remains the known success stub upstream.
There is no usable behavior to backport.

### Bounded stopping point

Minimal reuse is blocked on **package-scoped activation-class resolution and
the native registration metadata-object ABI/client route**, before a genuine
callback can yield IActivationFactory. Existing GUID-only RPCSS is not itself
that resolver/protocol. Building a new broker/string transport or synthesizing
identities would exceed this slice and would not be justified by these controls.
No fake factory or registration was added.

## Persistent artifacts and pins

* `rpcbindingcreate-server-catalog-review-fixes.patch` and corrected helper:
  accepted metadata query plus only the two requested review fixes.
* `rpcbindingcreate-registration-errors.patch`: independent negative-API fix.
* `rpcbindingcreate-registration-native-control.c/.exe`: actual native,
  paired and direct-original-callback controls.
* `rpcbindingcreate-native-registration-metadata-fields.json`: selected
  activation metadata including complete field-name/type checks (owned stage).
* Logs `rpcbindingcreate-registration-{native-control,paired-server,paired-client,
  original-callback,allocation-regression,unpackaged-regression}.log`,
  `rpcbindingcreate-registration-original-app.{stdout,stderr}.log`.
* Native disassemblies: registration, revoke, registration transport/helper
  and original app callback, all in the owned stage.

```text
f8b00ff9d48d151b3216a8dd09b3b81175fc69b5e36ef9b1e6756cc2fd6e359f corrected catalog helper
bfbb0269e6ad6b9e02127f5e47bcef314a6de42b8d3c79e57123526cbaca85e1 owned combase.dll (also includes separate negative-API fix)
```

For parent integration, splice source/data only; do not replace the entire engine.
Final genuine launcher argument remains the **bare** observed server name.
