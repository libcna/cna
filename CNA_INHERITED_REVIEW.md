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

## Review completion

The inherited ownership and disposal work is accepted in CNA `39ef8ea2f` and
`65e69cc5e`. ISSUE-011 and ISSUE-082 now have final dispositions; the matrix is
156 historical IDs / 133 final dispositions / 23 pending. ISSUE-010 remains open,
but its ContentManager post-Dispose subcase is repaired and the obsolete demo
marker is removed. The independent StorageContainer constructor/filesystem audit
is deliberately left for the successor.

Native validation passes: Core119; Content2057 PASS/4 unchanged SKIP;
Graphics3051 PASS/115 unchanged SKIP; Media355; Audio775 PASS/4 SKIP. Selected
SpriteBatch/event regressions pass52 cases on each of SOFTWARE, OPENGLES3,
OPENGL33, WEBGPU, SDL_GPU and FNA3D. The first asset-dependent module driver used
the wrong working directory; its Content102/Media234 setup failures are retained,
and unchanged binaries pass from the source directory required by UnitTests.cmake.

Integrated ownership sanitizers pass62/62 with actual affected implementation
and129 Sharp Runtime translation units instrumented. SpriteBatch/event ASan,
UBSan, float-cast-overflow and leak validation passes106/106. The first event
sanitizer audit had an overly strict repeated-warning multiplicity assumption;
the v2 driver proves the exact prior/new diagnostic multiset and no source drift.

The final external consumer rebuild completes191 PASS / one unchanged capability
SKIP. Ten intended rows change FAIL-to-PASS: C content manager on two renderers,
SpriteBatch begin/end on four and C resource lifecycle on four. Independent C
cube/sound transfer lifetime passes SOFTWARE and OPENGLES3; six exact visual
references pass. Frozen source/artifact hashes are unchanged. Peak memory is
4,301,119,488 bytes with high-limit throttling only and zero max/OOM events.

Genuine XNA post-Dispose observation ran in six fresh Wine/DXVK processes. Four
reference processes crash at EffectParameter.SetValue for deferred/native-effect
paths; two Immediate cases complete. CNA preserves measured pair/queue/accounting
semantics but refuses at the safe SpriteEffect boundary instead of reproducing a
use-after-free. These are observations, not six passing XNA tests.

C API coverage was regenerated after explicit review. The nine declarations
introduced by this ownership work have audited mappings; the existing broad
GraphicsResource rule also now explicitly owns seven previously planned protected
implementation declarations. Current inventory:8103 symbols,7018 implemented,
15 partial,620 planned,450 not applicable. Coverage, limitations and release-gate
checks all pass; the release gate remains honestly not ready because620 unrelated
planned symbols remain.

No validation job remains active. Final marker/catalog closure, the English root
handoff and normal pushes are the only work after this document. The ordinary
remaining campaign belongs to the successor and is not claimed complete here.
