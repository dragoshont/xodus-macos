# Full MR!8895 comparison: reuse already active

Runtime-component correction: `rpcbindingcreate-genuine-base-reuse.md` proves
these actual factory/view calls are served by genuine xbox_wintypes_base.dll.
The map.c equality below is source comparison, not runtime module attribution.

The latest final original callback result remains S_OK with a real nonnull
IActivationFactory. No runtime/source replacement was made during this comparison.

The complete public MR!8895 diff was retrieved, not just its first 6 KB:
79,079 bytes. Its new 836-line map.c was reconstructed and its Git blob hash
independently verified against the actual diff:

```text
b2dd866a590d4f9fb90be2b727519244a458e414  MR map.c Git blob
d40ca6f88af806574d6decf6dbc25796140ad822c4f7f2d8de5eefdde0917fde  MR map.c SHA-256
f34ac320ac6c8809d720d0d5aa7698aa8ec500eacaa8dcf2e95c44a2fed328ed  complete MR diff SHA-256
```

The already-used engine map.c is **byte-identical to current upstream** at
eba89375a0515957701928faac0f5007ef638b04:

```text
83e6019fc998734c1020a334663aefb3a70fc7bd665416d2b55f6ac16b60ce04
1,083 lines / 35,360 bytes
```

This verifies present implementation reuse, not the MR's GitLab merge/draft
status or exact ancestry. The separate researcher owns that status/commit
investigation; no duplicate fork/status research was performed.

Parent's completed independent GitLab API validation reports CLOSED,
merged_at=NULL, failed CI, head e89fdb920392c88154938482b33e95089b65717d,
base 4b1e53ce..., author Vibhav Pant/reviewer Rémi Bernon. This is recorded
as supplied validation, not a second status lookup. Closed/unmerged does not
prove other implementations are absent or that the approach is abandoned.
No wholesale MR vendor is warranted; actual genuine-component reuse is already
tested and passes. The original callback was retried again at the requested
deadline: S_OK/nonnull actual factory, followed by unchanged honest factory-
registration E_NOTIMPL, 13 activation checks passing, child C0000409.

Full helper comparison:

* Both implementations use owned HSTRING/IInspectable entries, sorted ordinal
  lookup, reference-counted pairs/views/iterators, and serial invalidation.
* MR view Lookup/HasKey delegate to the full backing map; current code uses
  the actual view's entries/size, supporting split ranges.
* Both GetIids bodies are explicitly E_NOTIMPL, not a completed helper.
* MR Split returns S_OK/NULL/NULL. This is a native-permitted no-split result,
  measured independently; it is not treated as missing data or a fake partition.
* Current Split creates actual ranges. The narrow wrapper handles small-view
  semantics and native no-split outcomes without exposing a mutable map.
* Iterable First follows the same real GetView/QI/First ownership path.
* The MR includes tests and PropertySet integration, not only storage. Its
  factory/property boxing is not an additional replacement for the already
  working existing PropertyValue implementation.

Decision: **do not regress to the older helper or blindly backport its stubs**.
The candidate already reuses the newer current-upstream collection/property
code for storage, boxing and iteration. Its bespoke portion is only the bounded
immutable view adapter with supported GetIids, canonical identity/output cleanup
and native Split behavior, plus authoritative metadata parsing.

The native/Wine 500-cycle controls for both actual entry points and the genuine
callback result remain the gates; no new implementation or engine swap is needed.
Persistent full sources/diff/comparison are in the owned stage:
rpcbindingcreate-mr8895.diff, rpcbindingcreate-mr8895-map.c,
rpcbindingcreate-mr8895-map-comparison.diff.
