# Private Swift native authentication host, version 1

This is a private app/engine pairing contract, not the management protocol.
`management-v1.schema.json`, its C95 hash and the 21 public consent diagnostics
are unchanged. A Swift AppKit/WebKit helper owns the managed login window;
Rust still owns checked device preparation, SOAP/proof/Store issuance and the
existing parent-only atomic management credential commit. The standalone CLI
continues to use its existing Wry UI. There is no managed Wry fallback.

Canonical definitions: `contracts/native-auth-host-v1.schema.json`, Rust
`xodus-management::native_auth`, and sanitized
`fixtures/native-auth-host-v1.json`. Fixtures are artificial issuer stand-ins,
not provider observations or authentication/capability certification.

## Framing and ordering

The Rust credential worker spawns exactly one explicitly pinned Swift helper
with an anonymous socketpair inherited as **duplex FD 0**. stdout/stderr are
null. Each frame is a four-byte big-endian unsigned byte count followed by one
UTF-8 JSON object. The entire JSON object, including envelope overhead, must be
1..262144 bytes. Duplicate/unknown fields, unknown operations, nonstring issuer
fields and incompatible versions fail closed. No provider-bearing argv,
management frame, named socket, file, dump or application log is permitted.
Application code does not print error descriptions or payloads. Core dumps
are disabled where supported; complete suppression of operating-system or
crash logging is **not** promised.

Both directions start `sequence` at 1 and increment it exactly once per frame.
All integers are bounded by 9007199254740991. `version` is exactly 1, `flowID`
is the engine's canonical lowercase hyphenated UUID, and `sessionID` is exactly
1 for this single helper/view session. Results additionally carry `replyTo`,
equal to the most recent worker command sequence. Unknown, duplicate, late,
foreign-flow/session, out-of-order or incorrectly correlated results fail.
Only one control command and one issuer exchange may be outstanding.

| Worker message | Exact additional fields | Helper response |
| --- | --- | --- |
| `open` | `remainingMillis`, `url`, `headers`, `userAgent` | `ready`, then at most one `da` |
| `navigate` | `url` | `ready`, then at most one `da` |
| `close` | `disposition`: completed/cancelled/failed | exactly one matching `closed`, then exit |

`ready` means the owned view accepted the command, not that a provider page
loaded or authentication succeeded. `da` contains only `property`, the seven
existing string fields: `sDAToken`, `sDASessionKey`, `sDAStartTime`, `sDAExpires`,
`sSTSInlineFlowToken`, `sSigninName`, `K`. They remain unchanged in memory.
Unrelated ServerData properties are not forwarded. `sDASessionKey` is required
by the existing model but is not used as a substitute for checked device proof.
The worker may issue `navigate` only after a trusted SOAP inline-auth fault,
at most four times. It uses the same view and nonpersistent cookie store.

The helper can send a one-shot `cancelled` or `failed` terminal at any active
stage. `failed.reason` is a private closed enum in the canonical schema; all
ordinary host failures map to existing public NativeSignIn/pipelineFailed,
not new public reasons. Deadline and cancellation retain the existing expiry
and cancellation semantics. No terminal may be followed by another frame.
Helper exit: 0 only after `closed/completed`; 2 for cancellation; 1 otherwise.
An exit without the corresponding terminal, a contradictory exit, or a crash
cannot promote credentials. The worker requires successful matching close
acknowledgement **and clean helper exit** before its StoreCompleted handoff.

## Initial input and native callbacks

Initial URL is exactly the existing InlineLogin.srf URL with id=80604, scid=3,
mkt=en-US, Platform=Windows10, clientid=000000004424da1f and hosted=1. Initial
headers and UA are preserved from `webview::login_request`; the fixture pins
their exact lowercased header keys and values, except the per-flow correlation
UUID. No arbitrary script, client, platform, header or market is accepted.

Navigation retains the existing HTTPS three-host allowlist: login.live.com,
account.live.com, login.microsoftonline.com. No userinfo or nondefault port;
explicit HTTPS port 443 is canonicalized/accepted by the existing Rust URL
parser. Bridge origin is only login.live.com. Finish path is exactly
/ppsecure/post.srf, not a prefix match. The Swift helper checks actual
WKScriptMessage frame origin, main-frame identity, owned view and navigation
generation, not just the current view URL. It preserves the fixed getContext
callback shape and uses JSON-safe values, not interpolated provider script.

The native main thread owns NSWindow/WKWebView. Background private IPC and
asynchronous Rust SOAP work must not block it. Fixed JavaScript completion,
main-navigation errors, renderer termination and unsupported popup requests
have explicit static failure outcomes; no silent None/pending fallback.
WKUIDelegate must handle popup requests explicitly without expanding origin
permissions. A known deliberately superseded navigation cancellation is not
confused with a current-page failure. Callback/close/expiry races are fenced.

## Ownership and deadlines

The original engine deadline is 600 seconds, monotonic, and is not reset after
device preparation. Bootstrap carries the remaining original budget.
The worker independently watches engine private-channel EOF throughout
issuance and handoff; the helper watches its worker channel and its remaining
deadline. Unexpected post-bootstrap engine bytes fail closed. Engine or worker
death, explicit half-close, cancellation and deadline prevent late success and
close/reap only owned children/windows. `kill_on_drop` alone is insufficient.

The engine keeps its bootstrap write-half open while awaiting the framed
result: existing `adapter::spawn_consent` writes bounded length+bytes and
moves the whole stream into `read_consent`. EOF is not a bootstrap delimiter.
Shared guardian cancellation state fences SOAP completion, helper close/ack/
reap and handoff publication. Normal engine closure after reading a complete
terminal result must not be mistaken for a pre-terminal parent failure.

The helper executable path/version/SHA and paired engine identity are explicit
nonsecret launch bindings, verified before spawning. No PATH discovery,
external saved helper, shared credential import or implicit fallback.
Binding fields are exactly `version` (1), `executable` (absolute owned regular
executable path) and `sha256` (64 lowercase hexadecimal characters).
Management launch options are `--native-auth-host`, `--native-auth-host-sha256`
and `--native-auth-host-version`; all three are supplied together. These are
nonsecret trusted packaging configuration, not a public management operation.
The worker receives this binding in its private bounded bootstrap.

The fixed source-backed context request is
`{type:"invoke",value:{name:"CloudExperienceHost.getContext",context:STRING}}`.
The opaque provider string is echoed unchanged in the fixed callback object:
`{type:"callback",value:{name:"CloudExperienceHost.getContext",
args:["CloudExperienceHost","TokenBroker","TokenBroker",CAPABILITIES_STRING],
context:STRING}}`. Dispatch uses the existing page's
`window["CloudExperienceHost.Bridge.dispatchMessage"]` function. No new
promise/callback, context interpretation or Windows Hello method is invented.
The document-start main-frame script exposes `window.external.notify`.
Both direct seven-field notification and the exact finish-path extraction of
provider-defined global `ServerData` are supported, with one-shot forwarding
and checked JavaScript completion.

## Capability limits and validation

Swift ownership is not a demonstrated live biometric fix. The old Wry host
already uses system WKWebView. Its inherited Windows UA/PasswordlessConnect
declaration does not establish implemented Windows Hello capability.
Ordinary WKWebView passkeys require RP association; the macOS browser exception
requires an approved managed browser capability. The current ad-hoc signed
pair has no embedded entitlements. No guessed entitlement, UA/header change,
OAuth token substitution or ASWebAuthenticationSession callback is included.
Current inline ServerData/Passport proof is not a URL-return OAuth contract.

Validate exact codecs and boundaries, callback/origin/generation/one-shot
ordering, native neutral/offscreen callbacks, renderer/JS/navigation/popup
failures, real child EOF/cancel/crash, original budget and engine-death races.
Retain the existing checked SOAP/device/proof negatives and parent atomic
commit tests. No Microsoft requests, fake TLS, relaxed production origin,
credentials or visible fixtures without separate GUI safety authorization.
Source review and separately authorized immutable pairing/deployment remain
required before a live retry.
