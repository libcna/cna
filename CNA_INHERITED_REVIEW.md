# Inherited work review — 2026-10-23

The owner authorized review, correction and coherent commits of all inherited
working-tree changes before preparing the final English `handoff.md`. This is
not a campaign-completion claim. No goal was created or changed.

## Starting state

CNA: `6b083bb13aef6cc0e36b9c48a720b0e0d9d71d53`.
cna-demos: `252e1272fc19cdf13a339305e015876858dbaf5e`.
Both revisions were published before this review. Snapshot and binary diffs of
all 48 CNA and 14 cna-demos inherited files are retained in cna-demos
`build-probe/repair-inherited-review/`; hashes are recorded in
`build/linux-full/reports/repair-inherited-review-start.json`.

The existing `cmake-build-multi` module targets build successfully with one
compiler, shared ccache, MemoryHigh=4 GiB, MemoryMax=6 GiB and no swap.
The first build was interrupted after linking; the incremental continuation
completed. Existing test warnings remain visible in the build log.

Baseline on a private display with dummy audio:

- Core: 119 cases, no failures.
- Content: 2,060 cases, no failures.
- Media: 355 cases, no failures.
- Storage: 55 cases, no failures.
- Graphics, configured OPENGLES3: 3,160 cases, four failures, all in the
  inherited occlusion coordinator's signed/wrapping-count boundary tests.

Counts include any declared skips; exact individual outcomes are in the JSON
reports named `repair-inherited-before-*`. The initial full graphics run forced
SOFTWARE and had 13 additional configuration-dependent failures in tests that
assert the compiled default renderer/window behavior. Repeating the unchanged
binary with its configured OPENGLES3 leaves exactly the four count failures.
That failed run is preserved, not reported as a passing SOFTWARE full suite.

## Reviewed batches

### Storage path regression fixture — accepted

The seven inherited `StorageContainerPathErrorsTest` cases are legitimate:
empty path argument exception types and parameter names, argument/disposal
ordering, empty patterns before filesystem access, nonempty/missing-directory
errors, preserved contents, and UTF-8 empty-directory deletion. The corresponding
production fixes already landed in `17c66a9d3` and `ac6be8f03` (with later
directory/file error fixes). No second runtime patch is necessary.

All seven pass in the current 55-case storage module run. Their genuine XNA
reference is `cna-demos/docs/repair-evidence/storage/StoragePathErrorsOracle.cs`:
29 checks passed in the retained `repair-storage-path-reference-v1-xna.log`.
That constructor-free reference covers public path methods, not real device
selection. The historical 7-case CNA baseline had five failures and two passes.
This commits missing regression coverage; it does not close all of ISSUE-010.

### Occlusion intervals and count arithmetic — accepted runtime repair

The inherited overlapping-query design is appropriate and matches genuine XNA.
Review fixed its saturating/signed-overflow summation and boolean fallback after
wrap. Source audit also found saturation in the native SOFTWARE accumulator and
WebGPU readback; a shared wrapping DWORD counter now covers all three paths.
The genuine reference and history are retained in demos' graphics evidence.

Current native graphics: 3,163 = 3,048 PASS / 115 unchanged SKIP, with exactly the
four inherited count failures repaired and three counter cases added. Seven
renderer/driver combinations have no focused failure (declared skips preserved).
The 17-case actual coordinator/counter sanitizer probe passes; its dependencies
are not instrumented. The original coordinator UBSan failure remains recorded.

Final external v3 rebuild and regression are accepted: 132 identical prior rows,
including eight explicit ISSUE-082 failures, all C++18/C24 query controls on
SOFTWARE/GLES/GL33, declared SDL_GPU refusal, a fault-injected negative control,
and six exact visual references. No source/artifact drift or OOM event. Build
385,284 ms; memory peak 4,302,282,752 bytes, high-limit throttling only. Full
details: demos `docs/repair-evidence/graphics/OCCLUSION_REPAIR_REVIEW.md` and
`repair-inherited-occlusion-external-v3-*` reports. Final marker/issue closure
follows the product commit and a focused unmarked rerun.

## Remaining review work

Owner decision received during this review: loaded C Effect/Model lifetime must
match XNA/C++, with explicit C documentation and test corrections. The old
independent-effect/model contract is superseded; do not introduce deep cloning
or suppress native disposal to preserve it. Independently transferred texture,
font and sound handles were not part of that question. Preserve their existing
contract and regression checks.

The owner also answered the two ISSUE-082 questions: C callback timing must
match XNA/C++, and SpriteBatch behavior after Dispose must also match XNA
(do not retain the former blanket ObjectDisposedException guards as an
intentional extension). The existing genuine 13-case managed accounting oracle
does not cover native Draw/End. A fresh-process real-device observation fixture
`SpriteBatchDisposedGpuObservation.cs` is prepared to resolve those paths before
implementation. Preparation alone is not runtime evidence.

- ISSUE-113: replace the disproved saturating tally with defined 32-bit counting,
  validate native/renderer/C/demo behavior, then commit the interval repair.
- ISSUE-011: finish reviewing resource ownership and metadata changes; implement
  the owner's C Effect/Model contract decision with exact disposal diagnostics.
- GraphicsResource event ordering: validate and commit separately from content
  metadata where possible.
- Review reference-only input evidence and regenerate affected C API inventories.
- Reconcile inherited chronological handoffs, then prepare the final English
  `handoff.md` for Opus 5.5 xhigh, commit and push. Ordinary remaining campaign
  issues belong in that handoff; do not claim they were all fixed.
