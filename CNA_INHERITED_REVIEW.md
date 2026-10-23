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

## Remaining review work

- ISSUE-113: replace the disproved saturating tally with defined 32-bit counting,
  validate native/renderer/C/demo behavior, then commit the interval repair.
- ISSUE-011: finish reviewing resource ownership and metadata changes; preserve
  the documented independent C handle lifetime, including effects and models.
- GraphicsResource event ordering: validate and commit separately from content
  metadata where possible.
- Review reference-only input evidence and regenerate affected C API inventories.
- Reconcile inherited chronological handoffs, then prepare the final English
  `handoff.md` for Opus 5.5 xhigh, commit and push. Ordinary remaining campaign
  issues belong in that handoff; do not claim they were all fixed.
