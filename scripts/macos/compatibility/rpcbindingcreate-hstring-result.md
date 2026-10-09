# Actual server-prefix parser control

Owned stage: `~/xodus-runs/rpcbindingcreate-20261006-001`.
No Wine API, HSTRING layout, catalog, parent launcher, shutdown-delay or
GUI/auth changes were made during this slice.

## Root cause of the remaining prefix

The existing launcher's optional final argument is the **bare server name**.
Its source formats the child command line as:

```c
server_name ? L"\"%ls\" -ServerName:%ls" : L"\"%ls\""
```

Earlier worker retries incorrectly supplied `-ServerName:` in that argument.
The actual child therefore received two prefixes. Wine tracing proves:

```text
WindowsCreateStringReference("-ServerName:", 12, header, out) -> reference
WindowsGetStringLen(prefix reference) -> 12
input: "-ServerName:-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca"
input length: 84
WindowsSubstring(input, 12, out)
output: "-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca"
output length: 72
```

This is not a substring failure. TwinAPI genuinely strips one prefix:
callsite `+4aacd` constructs the 12-character reference, `+4abfa` obtains its
length, and `+4ac13` passes that length as the substring start.

## Independent native/Wine controls

`rpcbindingcreate-hstring-parser-test.c/.exe` calls the actual named APIs,
then server enumeration. Both Windows and Wine report:

```text
PREFIX_LENGTH=12 HEADER_SIZE=24 HANDLE_IS_HEADER=1
single prefix: start=12 input_length=72 output_length=60
output=Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca
double prefix: start=12 input_length=84 output_length=72
output=-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca
both unpackaged server queries: 80040154, classes=NULL, count=0
HSTRING_PARSER_PASS failures=0
```

The native reference header contains flags=1 at offset 0, length=12 at offset
4, and a buffer pointer at offset 16. The handle points to the 24-byte caller
header in both implementations. Native zeros reserved bytes 8..15; Wine leaves
the test's A5 initializer there. Those reserved bytes are not used by any
observed parser operation. No opaque-header copying or padding-based speculative
fix was introduced.

There is **no confirmed missing HSTRING behavior** requiring an upstream
backport or API implementation. The source remains unchanged.

## Corrected genuine retry and remaining dependency

Correct optional launcher argument:

```text
Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca
```

Do **not** prepend `-ServerName:` when calling this launcher.

The corrected real child now creates a 72-character input, strips 12,
and passes the exact 60-character bare server name to
RoGetServerActivatableClasses. Catalog activation and child claims again pass
all 13 checks. Its actual unsupported package-scoped query still returns
80004001; fail-fast records **7 / ffffffff80004001 / 2b5**, child exit c0000409.
No class list or factory registration success is fabricated.

Read-only native Windows metadata confirms the exact installed package
Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe and a per-user repository
record for Microsoft.Xbox.AppL. That record exposes no server/class/entry-point/
executable value and only a Capabilities child. The legacy
ActivatableClasses\Package key is absent in both 64-bit HKLM and HKCU views.
Therefore these observations do not provide an authoritative server-to-class
mapping. The bare server name must be resolved against real package-scoped
activation metadata; the manifest entry point is not substituted as a class list.
Parent-owned catalog-schema investigation remains independent and untouched.

The prior pinned upstream result remains applicable to this actual remaining
gap: eba89375a0515957701928faac0f5007ef638b04 has only success stubs for
RoGetServerActivatableClasses/RoRegisterActivationFactories, not a backport.

Logs in the owned stage and assigned scratch:

* `rpcbindingcreate-hstring-actual.{stdout,stderr}.log`: double-prefix proof.
* `rpcbindingcreate-hstring-parser-{native,wine}.log`: independent controls.
* `rpcbindingcreate-hstring-corrected-launch.{stdout,stderr}.log`: corrected retry.

Run the control as a separate process:

```bash
own="$HOME/xodus-runs/rpcbindingcreate-20261006-001"
"$own/build/loader/wine" "$own/rpcbindingcreate-hstring-parser-test.exe"
# Genuine Windows: same executable, no arguments.
```

Use the matching prefix/server/DLL environment from the earlier owned-stage
results. The existing loader/launcher binary is unchanged; only its invocation
argument was corrected.
