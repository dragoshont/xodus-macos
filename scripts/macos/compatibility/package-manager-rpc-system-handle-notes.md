# Xbox broker: measured RPC system-handle gap

## Actual failure

The original, unmodified `XboxPcAppCE.exe`, with its extracted original
framework dependencies, was launched directly in stock CrossOver 26.3.0.39832
in the separate `XboxAppDirectPackage20261006-001` bottle. The Store installer
was not used. Its entry returned zero, but no Xbox library was observed.

The genuine `Windows.System.BrokeredLauncher` client called the genuine
Microsoft-signed `usermgrcli.dll` through its measured Windows API-set mapping.
The client reached local RPC object
`B18FBAB6-56F8-4702-84E0-41053293A869`, NDR32 transfer syntax version 2.0,
procedure 10. The actual trace then showed:

```text
NdrpClientCall2 proc num: 10
client_do_args param[0]: ... type 3c MustSize MustFree IsIn IsByValue
dispatch_exception unknown exception (code=6f7) raised
```

The exception is `RPC_X_BAD_STUB_DATA`. It occurs during argument sizing,
before an observed call to the target service endpoint. Neither service
unavailability nor Xbox installation success can be inferred from this trace.

## Exact descriptor, not a guessed type

The provider was resolved on genuine Windows by loading
`ext-ms-win-session-usermgr-l1-1-0.dll`, resolving `UMgrQueryUserContext`, and
finding the owning module. The owner is `usermgrcli.dll`, Microsoft-signed,
SHA-256:

```text
8f36df633756eecef02a2c606a7c5ab7ba032bbb164709c4977a8b26b0232b89
```

Read-only decoding of that exact PE image found:

| Item | Value |
| --- | --- |
| Procedure 10 format RVA | `0xe780` |
| MIDL stub descriptor RVA | `0xbb10` |
| Type-format base RVA | `0xee52` |
| First argument type-format offset | `0x0c` |
| Exact type descriptor bytes | `3c 05 08 00 00 00` |

`0x3c` is `FC_SYSTEM_HANDLE`; resource byte `5` is an access token, and the
little-endian access mask is `0x00000008`, `TOKEN_QUERY`. The argument therefore
requires real local-RPC transfer of an access-token handle with query access,
not transmission of a raw integer or fabrication of a user context.

The type and descriptor layout were cross-checked against these public decoder
definitions, rather than an AI search summary:

- [NdrFormatCharacter](https://github.com/googleprojectzero/sandbox-attacksurface-analysis-tools/blob/main/NtCoreLib/Ndr/Dce/NdrFormatCharacter.cs)
- [NdrSystemHandleTypeReference](https://github.com/googleprojectzero/sandbox-attacksurface-analysis-tools/blob/main/NtCoreLib/Ndr/Dce/NdrSystemHandleTypeReference.cs)
- [NdrSystemHandleResource](https://github.com/googleprojectzero/sandbox-attacksurface-analysis-tools/blob/main/NtCoreLib/Ndr/Dce/NdrSystemHandleResource.cs)

## Matched Wine source and required implementation

The tested coherent CodeWeavers Wine 11.0 source still labels `0x3c` as
`FC_UNUSED4` in `include/ndrtypes.h`. Its
`dlls/rpcrt4/ndr_stubless.c` sizing dispatcher looks up the type in
`NdrBufferSizer` and raises `RPC_X_BAD_STUB_DATA` when no implementation exists.
The same concrete failure was independently observed in the stock commercial
CrossOver bottle; this is not merely a warning from the custom runtime.

A correct fix must implement the descriptor's sizing, marshalling,
unmarshalling, and cleanup together with local-RPC handle transport. It must
duplicate the actual token into the receiving process, honor `TOKEN_QUERY`,
validate the resource/type and transport, and preserve ownership rules.
Wine already supports server-mediated handle duplication; this finding does
not assert that all handle duplication under Wine is impossible.

[Microsoft's system_handle contract](https://learn.microsoft.com/en-us/windows/win32/midl/system-handle)
requires same-machine calls. An input handle's receiver-side duplicate is
valid for the call and freed afterward unless the receiver explicitly
duplicates it. Output handles transfer ownership to the caller.

Adding a buffer-size handler alone, treating the token as a scalar, or returning
success without a duplicated token would conceal the failure without providing
the required behavior.
Package registration/identity is a separate prerequisite and does not itself
implement this marshalling type or the genuine user-manager service.

## Bounded implementation and actual changed result

`wine-rpc-token-handle.patch` adds experimental, input-only `sh_token` handling
with exactly `TOKEN_QUERY` access to a separate coherent Wine source/build.
It implements sizing, marshal/unmarshal, memory sizing and cleanup. Other
resources, other access masks, output transfer and non-local transport remain
explicitly unsupported.

**The transport is Wine-to-Wine ncalrpc, not Microsoft's ALPC wire ABI.**
An opaque sender handle reference is carried in a checked, tagged local
message. The receiver derives the sender PID from its actual connected pipe,
not a caller-supplied PID, and calls `DuplicateHandle` from that peer into its
own process with `TOKEN_QUERY`. It verifies that the received object is a
token, closes the duplicate on failure, and closes the call-lifetime input
duplicate during NDR cleanup. The numeric reference is never exposed to the
server routine as a supposedly valid receiver-side token.

Real cross-process controls in `rpc-token-tests/` passed:

- Sender PID 216 and receiver PID 32 queried the same token identity.
- The receiver could query its actual duplicate but could not `DuplicateToken`
  through that query-only handle.
- The sender's original remained usable, and the receiver's last call
  duplicate was closed before the next non-token RPC method.
- An event supplied instead of a token raised `ERROR_INVALID_HANDLE`.
- The original Xbox broker actually supplies a null token. A genuine Windows
  client/server control proved that this nullable handle is valid; the Wine
  control then reproduced the same null input without inventing a token.

The fixture IDL describes the method shape; its saved WIDL client/server
stubs deliberately replace the scalar argument's format with the measured
`FC_SYSTEM_HANDLE` descriptor, because this WIDL version does not emit that
modern descriptor itself. Build separate client/server executables with
MinGW, linking `rpcrt4` and `advapi32`; define `TOKEN_PROBE_SERVER` for the
server. `--null` exercises the native-matching null case, and
`--invalid-object` must fail with exception 6 rather than return success.

The rebuilt runtime and controls are under
`~/xodus-runs/cw-rpc-token-20261006/`; only a separate snapshot of the direct
Xbox bottle was used. Stock CrossOver binaries and the original bottles
were not replaced.

The genuine Xbox CE entry now passes the former `0x6f7` sizing failure and
reaches `RpcEpResolveBinding`. Its next measured failure is
`0x6d9` (`EPT_S_NOT_REGISTERED`): the UserManager interface
`B18FBAB6-56F8-4702-84E0-41053293A869` has no registered endpoint. An initial
endpoint-mapper connection also reported `0x6ba`; later mapper calls succeeded
but still found no target endpoint. Neither a genuine UserManager service nor
the Xbox library has been made to work by this marshalling patch.

The verified controls, exact rebuilt DLL hash and original-entry trace are in
`verified-token-rpc-results.json` and
`~/xodus-runs/xbox-app-direct-package-rpc-20261006-001/verified-rpc-original-ce.stderr.log`.

## Evidence

The stock-CrossOver direct-launch receipt and bounded trace are retained under:

```text
~/xodus-runs/xbox-app-direct-package-crossover-20261006-001/
  result.json
  actual-direct-ce.stdout.log
  actual-direct-ce.stderr.log
```

No account caches or credentials were transferred. The original experimental
bottle, working games, and separate Exodus UI lane were not modified.
