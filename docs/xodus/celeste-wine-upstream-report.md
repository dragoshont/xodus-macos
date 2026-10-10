# Celeste / .NET Native loader compatibility: upstream report drafts

These drafts have not been submitted. The owner selected **report upstream
only; move on to a game type that already works**. Runtime patching has stopped.
Celeste is not playable. The legally acquired payload remains on the authorized
Mac, outside this repository. Neither draft requires distributing that payload.

## WineHQ Bugzilla draft

**Title:** ntdll loader writes a .NET Native security cookie in a read-only
`.rdata` section; synthetic x64 DLL reproduces the access violation

### Environment

- macOS on Apple Silicon, x86-64 Windows code through CrossOver.
- Official CrossOver 26.3.0.39832, build `cxoffice-26.3.0rc2`.
- Reported Wine version: `11.0-8726-g2e2f5fca349`.
- Windows 10 x64 bottle; reproduced with both a template-derived bottle and a
  pristine bottle.
- Actual application: Microsoft Store PC edition of Celeste, version
  `20.9.11.2`, x64, .NET Native. This is not the console package.
- Declared native dependencies: Microsoft.NET.Native.Framework.2.2
  `2.2.29512.0`, Microsoft.NET.Native.Runtime.2.2 `2.2.28604.0`, and x64
  Microsoft.VCLibs.140.00 `14.0.33519.0`.

### Expected behavior

The loader initializes a default security cookie without violating the
protection of the image page, then restores the original protection.
An already initialized cookie should remain unchanged.

This report does not assume that fixing this issue makes the complete UWP/.NET
Native application compatible.

### Actual behavior

Celeste fails before application activation, with process exit 5 and exception
`0xc0000005` on a write:

```text
Vendor ntdll.dll: instruction RVA 0x45216
Instruction: mov [r15], rax
Instruction bytes: 49 89 07
Destination: relocated Celeste.dll security cookie, image RVA 0x2b9718
Destination section: .rdata, not writable
Initial cookie: standard MSVC default 0x2b992ddfa232
```

The destination address matches the PE load-configuration `SecurityCookie`
pointer after relocation. Microsoft SharedLibrary.dll also places its cookie
in read-only `.rdata` (RVA `0x1fb608`). The runtime's mrt100_app.dll instead has
a writable `.data` cookie (RVA `0x2ea88`).

The matching vendor Wine source's `update_load_config()` in
`dlls/ntdll/loader.c` calls `set_security_cookie()` directly, without making
the target page writable.

### Payload-free synthetic reproducer

The following DLL requires no game, Store account, licensed payload, or
Microsoft framework. Save as `cookie-fixture.c`:

```c
#include <windows.h>

#ifndef COOKIE_VALUE
#define COOKIE_VALUE 0x2b992ddfa232ULL
#endif

#ifdef WRITABLE_COOKIE
static ULONG_PTR cookie = COOKIE_VALUE;
#else
static const ULONG_PTR cookie __attribute__((section(".rdata"))) = COOKIE_VALUE;
#endif

const IMAGE_LOAD_CONFIG_DIRECTORY64 _load_config_used = {
    .Size = sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64),
    .SecurityCookie = (ULONGLONG)&cookie,
};

__declspec(dllexport) const ULONG_PTR *get_cookie(void)
{
    return &cookie;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    return TRUE;
}
```

Save the loader as `cookie-load.c`:

```c
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    HMODULE module;
    if (argc != 2) return 2;
    module = LoadLibraryA(argv[1]);
    if (!module) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 3;
    }
    puts("Loaded successfully");
    return !FreeLibrary(module);
}
```

Build with x64 MinGW:

```sh
x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,--entry,DllMain \
  -o cookie-readonly.dll cookie-fixture.c
x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,--entry,DllMain \
  -DWRITABLE_COOKIE -o cookie-writable.dll cookie-fixture.c
x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,--entry,DllMain \
  -DCOOKIE_VALUE=0x123456789abcULL \
  -o cookie-initialized.dll cookie-fixture.c
x86_64-w64-mingw32-gcc -o cookie-load.exe cookie-load.c
```

Check the DLL's PE load-configuration data-directory entry with
`x86_64-w64-mingw32-objdump -p`: it must be nonzero and point at the structure
containing the cookie. The tested MinGW build populated that directory; merely
placing a variable in `.rdata` without a load-configuration entry is not a
reproducer.

Run `cookie-load.exe cookie-readonly.dll` in an isolated x64 test bottle.
The tested DLL reproduced a write access violation at the same vendor ntdll
RVA `0x45216`. Use a bounded run and shut down only the test bottle if Wine's
debugger remains open. The writable and preinitialized variants are controls.
The abbreviated `cookie-load.c` above is a reporting harness; the experiment
used a fuller verifier that also checked cookie value and page protection.

### Evaluated repair direction

A temporary, matching-source ntdll experiment used `NtProtectVirtualMemory`
to make the cookie range writable, called `set_security_cookie()`, and restored
the original protection. It included an image-bounds check and explicit errors
for protection failures. It did not modify the application executable, strip
the load-configuration directory, or disable licensing/integrity checks.

Observed results of that experimental component:

- Default read-only cookie was initialized and returned to `PAGE_READONLY`.
- Default writable cookie was initialized and retained writable protection.
- Preinitialized read-only cookie retained its original value and protection.
- The actual Celeste application passed the original cookie-writing fault.

This is a candidate upstream direction, not a production-ready patch.
Upstream review should address executable page protections, a range spanning
pages with different protections, and failure handling. It has been removed
from the host. There is no installed custom Wine component now.

### Subsequent .NET Native failure

After the cookie repair, the native application loaded its graphics/audio and
framework dependencies, but mrt100_app.dll failed during process attach:

```text
ucrtbase.dll RVA 0x39104: read access violation
Invalid string pointer: 0xa848
Caller: mrt100_app.dll RVA 0x7fc6b
Intermediate: ntdll LdrResolveDelayLoadedAPI
mrt100_app.dll failed to initialize, status c0000005
```

The callback receives a named import. The inspected Wine implementation fills
`DELAYLOAD_INFO.TargetApiDescriptor.Description.Ordinal` with the low word of
the import-name RVA even when `ImportDescribedByName` is true. The .NET Native
callback treats that union as a string pointer and calls `_strnicmp`.

A separate synthetic descriptor/callback test verified that assigning
`Description.Name` to the relocated `IMAGE_IMPORT_BY_NAME.Name` repairs that
specific callback-data defect without changing ordinal imports. The actual
application then reached a different failure: a delayed import of `mrt100.dll`
was missing and the resolved function pointer remained null.

The runtime callback has separate handlers for reason 3 (DLL-load failure)
and reason 4 (procedure-resolution failure). The inspected Wine implementation
calls reason 4 for either failure. DLL-recovery behavior is a next investigation,
not a verified application repair: that experiment was stopped before its
new recovery candidate was validated. No working WinRT activation, game window,
input, or gameplay was observed.

## CodeWeavers support note draft

I am testing the legitimately acquired Microsoft Store PC version of Celeste
`20.9.11.2` with official CrossOver `26.3.0.39832` on Apple Silicon macOS.
Both a pristine Windows 10 x64 bottle and a template-derived bottle fail during
native DLL loading.

The first blocker is an ntdll security-cookie initialization write into a
read-only image section: vendor ntdll RVA `0x45216`, `mov [r15], rax`, targets
Celeste.dll's load-configuration cookie at RVA `0x2b9718` in `.rdata`.
A small MinGW-built DLL reproduces this without the game or Store account;
source and instructions appear above.

A temporary ntdll built from the matching vendor source confirmed that
making the cookie page writable just for initialization and restoring its
protection removes this first fault. Read-only, writable, and preinitialized
cookie controls passed. That was a diagnostic experiment, not a claim that
Celeste is supported.

The next failure is mrt100_app.dll process attach: a named delayed-import
callback receives the import-name RVA as an ordinal instead of a relocated
name pointer, causing `_strnicmp` to read from `0xa848`. Correcting that field
reaches another delayed-import recovery issue involving `mrt100.dll`.
Could these loader and .NET Native callback behaviors be investigated upstream?

I have stopped runtime patching and restored the entire official CrossOver
bundle. Deep/strict signature verification passes, Gatekeeper accepts its
notarized Developer ID, and the signer is CodeWeavers Inc.,
team `9C6B7X7Z8E`, not a local identity. An existing Lara bottle's Windows
command bootstrap returned exit 0 with the restored vendor engine.
That check is not a full Lara gameplay test.

No keys, delivery addresses, credentials, or application payload are attached.
