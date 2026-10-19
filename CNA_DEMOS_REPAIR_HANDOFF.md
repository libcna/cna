# CNA demos repair campaign

## Campaign contract

Repair every actionable defect inventoried in `../cna-demos/docs/CNA_ISSUES.md`.
Preserve historical IDs and assertions; verify current behavior and XNA evidence
before changing production code. Work in coherent subsystem/root-cause commits.
Do not push or merge. Unavailable hardware/services must be stated explicitly.

## Current checkpoint (2026-10-19)

Baseline capture is complete. CNA source baseline is `688f3f23e`; campaign status
commit before repairs is `56aaa6cff`, demo evidence commit is `2552912a5`.
All six separate Wine/reference tests passed (667.70 seconds), including genuine
XNA compiler acceptance and runtime interoperability. Log:
`../cna-demos/build/linux-full/reports/repair-baseline-native-xna.log`.

NATIVE-BASELINE-MSAA-2X is a demonstrated native test-oracle defect, not missing
multisampling. A direct GL query on the same Radeon 780M/Mesa driver reports actual
2x samples at (0.25,0.25)/(0.75,0.75), both on the fixture's exact y=x edge. The 4x
pattern straddles it. Evidence: `repair-baseline-msaa-sample-positions.log`; probe
source and diagnosis are in `../cna-demos/build-probe/shared-runtime-source/`.
The fixture now bisects a texel with a vertical edge; every coverage threshold,
single-sample control and interleaved-target check is unchanged. The formerly
failing isolated native test passes on private OPENGLES3. No renderer code changed.

ISSUE-116's corrected demo passes all seven checks under SOFTWARE and OPENGLES3
labels, empty stderr, no GPU use. Its invalid knownIssue marker is removed.
All 19 selected demo Matrix/catalog/validation CTests pass. Native value/out/alias
infinity/NaN coverage is added and all 91 selected Matrix tests pass, together
with MSAA: 92/92, 4.98 seconds (`repair-oracles-native-ctest.log`). The focused
native build passes with one existing unrelated nodiscard warning, no new warning.
Demo evidence: `repair-116-closed-*`, `repair-116-ctest.log`.

Committed native oracle corrections: `461064b90` (MSAA), `62e3f9c56` (Matrix).
Demo `997f2d8` closes 116 as DEMO_EXPECTATION_BUG, retaining historical evidence;
the matrix records its final disposition. Demo worktree is clean. Seven IDs were
already fixed; one demo expectation is corrected; zero CNA runtime repairs yet.

Phase B: nine C++ documentation corrections and fourteen C API documentation
corrections are applied but not yet committed/closed. Public API shape and runtime
behavior are unchanged. Native full rebuild is running in cmake-build-debug,
log `repair-docs-native-build.log`; C API route generation check passes unchanged.
Pending: build demos, run focused native/demo contract checks, update coverage
inventory if required, resolve the documentation IDs with actual evidence, commit
the coherent batches separately. Then Curve root causes (002/036/097/137/138).
Uncommitted CNA work: documentation headers/comments, digest regression, PacketReader
example assertions, matrix/handoff. No push or merge. Historical sections below preserve
baseline progress; this checkpoint supersedes their pending-run statements.

## Starting state (2026-10-19)

- Phase A: repository inspection and baseline; no repairs made yet.
- CNA: branch `next`, `688f3f23e79f1f8294f38985b1beda5ecc13862b`, clean.
- cna-demos: branch `next`, `a50a1f426be7e16e565fc481ff677dbb556c4037`, clean.
- Sharp Runtime: `f2aecfefeac171b331787e5cdc4849507363ef0e`, clean.
- EasyGL: `f4d6afaf16cdc7e7e73130950bc45e6d865b41bd`.
- MetaGL: `dd3b30055d55fdc42240de7af248259338256992`.
- CNA HEAD has 221 commits after the Complete1128 CNA revision
  `d23e139de9e5c15511b6be745de290896a69ee37`.
- Complete1128 demo revision: `042fea04f53cc051291b9585782ed71a1a724bda`.
- Historical inventory: IDs 001–155, including five entries under Resolved.
  Current dispositions require verification; an old Open label is not proof.
- Prior migration already recorded ISSUE-083 and ISSUE-134 as repaired externally.
  Historical transient build issues 044, 052, 053 are also under Resolved.

## Validation and evidence

Historical Complete1128: 719 demos; 732 enabled CTests passed, three disabled.
SOFTWARE: 555 pass / 113 expected fail / 48 skip / 3 manual, 204 failed assertions.
OPENGLES3: 594 / 115 / 7 / 3, 209 failed assertions. Six visual references exact.
Artifacts remain in `../cna-demos/build/linux-full/reports/checkpoint-1128-*`.

The later 2026-10-17 demo handoff records all 732 enabled CTests passing against
CNA `6ee757c2a`; no complete renderer smoke was performed at that revision.
This is not substituted for a fresh current baseline.

Reuse `../cna-demos/build/linux-full`, already configured against this CNA tree
with SOFTWARE, OPENGLES3, OPENGL33, SDL_GPU, HEADLESS and STUB. Native test trees
`cmake-build-debug` and `cmake-build-multi` exist; inspect/reuse their configuration.
Compile with at most three jobs for this campaign, shared `CCACHE_DIR=/rv/cnaccache`
and `CCACHE_BASEDIR=/rv`. All display tests must use a private display and writable
isolated XDG storage; never the owner's desktop.

Initial sandboxed build failed solely because shared ccache is read-only there.
The same build passed with sandbox escalation (254 scheduled steps, no warnings); log:
`../cna-demos/build/linux-full/reports/repair-baseline-build.log`.
Metadata: 719 demos, zero problems. Demo CTest: all 732 enabled tests passed in
259.65 seconds, including expected-failure contracts; three manual tests disabled.
Display: private Weston/Xwayland via CNA's runner; isolated XDG data, dummy audio.
Log: `../cna-demos/build/linux-full/reports/repair-baseline-ctest.log`.
Validator/comparator/visual-tool self-tests passed 7 + 4 + 5 tests.

Full native build passed in `cmake-build-debug` (2,829 scheduled steps,
OPENGLES3, tests enabled); log is `repair-baseline-native-build.log` in that
same demo reports directory. This native profile has CNA_BUILD_C_API,
CNA_CNAEXT and CNA_DEVICES OFF; the full demo configuration separately enables
those surfaces. Native C API validation is still required for later C API batches.
Its existing Draco dependency emits CMake CMP0148 and deprecated implicit-`this`
capture warnings; native tests also emit existing ignored-nodiscard warnings.
These are retained, not suppressed or counted as repair regressions.
The native CTest baseline has 11,161 selected tests, excluding the separately
hosted Wine XNA tests. The first attempt was intentionally interrupted (exit 130)
after identifying an environment conflict: global SDL_AUDIO_DRIVER=dummy and
SDL_VIDEO_DRIVER=x11 override the older per-test SDL_AUDIODRIVER/SDL_VIDEODRIVER
settings. The no-physical-hardware audio test failed because the dummy device
still opened; one diagnostic run unsetting SDL_AUDIO_DRIVER passes that exact
test (82 ms), proving the setup cause. Source review also found native dummy,
X11 and Wayland test selections, so the initial run is not accepted as coverage.
Preserved evidence: `repair-baseline-native-ctest-env-conflict.log` and
`repair-baseline-native-audio-env-control.log`. No test/source was changed.
The complete restarted native run finished in 1,294.71 seconds: 11,054 passed,
106 skipped, one failed, out of 11,161 selected tests (CTest exit 8). Modern
audio/video overrides were unset, with no global SDL_VIDEODRIVER pin and legacy
SDL_AUDIODRIVER=dummy so fixtures could override it. The checkout guard remained
clean. Logs: `repair-baseline-native-ctest.log` and the preserved full
`repair-baseline-native-LastTest.log`.

The one pre-repair failure is `EasyGL_Conformance_render_target_own_msaa`, leg D:
requested/applied 2x but zero partially covered centre-row texels; its 4x and
single-sample controls pass. An isolated run reproduces exactly the same failure
(`repair-baseline-native-msaa-control.log`, exit 8, 0.69 seconds). Keep this visible
as NATIVE-BASELINE-MSAA-2X until investigated, not as a campaign regression. The
test uses one exact diagonal; a valid 2-sample layout may put both samples on
that diagonal. This is only a test-oracle hypothesis, not a disposition or fix.
Inspect sample positions and rasterization before changing either renderer or test.

The six excluded reference tests remain to run separately and serially. Actual
current `run_gpu_tests_private.sh` and `tests/interop/xna40/run-interop-harness.sh`
explicitly support private Xwayland, Wine null ALSA audio and disabled modal
winedbg. The older main `handoff.md` advice to use external Xvfb :99 is stale;
do not follow it onto another display. XnaDifferentialBuildTest's compiler-backed
case is documented as taking about 12 minutes. Prerequisite DLLs/fxc and the
existing Wine prefix are present; the scratch paths contain only generated XNB
artifacts or do not yet exist. Do not run another suite concurrently.
SOFTWARE category-partitioned smoke completed with explicit SDL X11 on the
private runner: 719 IDs, 557 pass / 111 expected fail / 48 skip / 3 manual,
199 failed assertions. OPENGLES3 also completed: 719 IDs, 596 pass / 113 expected
fail / 7 skip / 3 manual, 204 failed assertions, with exactly the same reviewed
deltas. All six approved visual references compare exactly (zero differing
pixels); representative font/texture captures were also inspected. Logs/captures:
`repair-baseline-visual.log` and `repair-baseline-visual/`. A focused OPENGL33
cube-orientation run passes 13 controls (434 reflected pixels above, zero below).
The private display is accelerated AMD Radeon 780M, Mesa 25.0.7, GLES 3.2 / GL 4.6,
as recorded in `repair-baseline-gl-environment.log`; do not call it llvmpipe.
The driver
`/tmp/cna-repair-smoke.mjs` validates each category and exact 719-ID coverage.
Complete results/raw logs: `repair-baseline-SOFTWARE.{json,log,stderr.log}`;
comparison: `repair-baseline-compare-SOFTWARE.log`, all in the demo reports directory.
Do not interpret the lower failure count as five repairs: the reviewed deltas are:

- 083 and 134 now PASS with the same check counts (pre-campaign fixes).
- 041's short-read and 145's missing-file checks now pass inside still-failing demos.
- `math.curve.key_collection` falls from 20 to 6 checks and `math.matrix.perspective`
  from 12 to 4: stale `std::out_of_range` (collection) and `std::invalid_argument`
  (perspective) catches let the pre-campaign System exception migration abort
  these demos before the original 097/117 witnesses. Correct expectations from
  XNA evidence, not merely current behavior. In particular, all three genuine
  XNA perspective constructors throw ArgumentOutOfRangeException with the named
  invalid parameter, while current CNA throws the broader ArgumentException.
  Repair that contract together with 117's Pi endpoint; update tests to the exact
  framework type and preserve every control. Collection negative indices use
  ArgumentOutOfRangeException, short CopyTo destinations use ArgumentException.
- 030's reliable-loss failure disappears, but the latency-minimum and complete
  unreliable-outage checks now fail. CNA commit `01534c2e7f33d67191efe26c4434c874a244bfa3`
  moved simulation to the **sender**, matching genuine XNA NetworkSimulator,
  exempts reliable packets and uses Gaussian latency (mean L, clamped 0..2L).
  The demo still sets simulation on the receiver and assumes a fixed minimum L.
  Review the local XNA Net decompilation and native deterministic simulator tests;
  adapt the incorrect demo contract and revalidate before closing 030.

The complete historical issue inventory has been read. The machine-readable
matrix is `CNA_DEMOS_REPAIR_MATRIX.json`, containing exactly IDs 001–155, source
locations, associated demos and provisional subsystem/classification fields.
PENDING_VERIFICATION is deliberately not a final disposition. ISSUE-021 is
historically described as fixed despite remaining beneath the Open heading;
verify it and reconcile the section rather than treating the heading as truth.
ISSUE-041 has a source-confirmed pre-campaign fix (`a83673fc5`), but its demo also
contains ISSUE-098 failures; whole-demo WILL_FAIL cannot prove either independently.
The transient compile failures 044, 052 and 053 are verified ALREADY_FIXED by
the current successful build. Current runtime evidence additionally verifies
021, 041, 083 and 134 (seven ALREADY_FIXED IDs total). The primary demo inventory
now moves 021/041 to Resolved while retaining their history and distinct 098 failures.
The other classifications are provisional until
their own source/contract/reproduction review; they do not constitute closure.

Read-only source review confirms documentation contradictions 153–155 persist.
Their existing native tests/implementations agree with the issue's intended
contracts. No runtime/header fixes have been applied during baseline capture.
For the next math batch, the local genuine Windows XNA decompilation in
`../xna4-decomp/reference/xna4/decompiled/windows/Microsoft.Xna.Framework/`
`Microsoft.Xna.Framework/Curve.cs` confirms issues 002/036/138. The existing
native `CurveTest.EvaluatePostLoopLinear` incorrectly expects the first key's
outgoing tangent, so it needs an evidence-backed correction alongside new
distinct-tangent, shifted-Step and duplicate/NaN regressions, not blind preservation
of that incorrect native oracle. No test or runtime edit has yet been made.

ISSUE-116 is a proven **DEMO_EXPECTATION_BUG**, not a CNA inversion defect. The old
probe `/tmp/checkpoint-1028-matrix-invert-xna/xna_invert_probe.cs` printed `{R}`
floats in the current culture. Fresh genuine XNA 4.0 under the existing Wine prefix
reports culture en-US, console CP1252, and PositiveInfinitySymbol U+221E. That
symbol is best-fit encoded as ASCII `8`, so the old `8`/`-8` transcript was infinity,
not finite numbers. The new probe explicitly verifies both overloads:
M22 bits `7F800000`, M42 `FF800000`, IsPositive/NegativeInfinity true, comparisons
with 8/-8 false; the same original-format output still prints 8/-8. The remaining
14 components are NaN (`FFC00000`). CNA already returns the correct classification.
Source: `../cna-demos/build-probe/repair-matrix-oracle.cs`; genuine-runtime evidence:
`repair-baseline-xna-matrix-bits.log`, empty corresponding stderr, exit 0. Preserve
this evidence, correct the demo to signed infinities after baseline capture, remove
only its now-invalid knownIssue marker, add native coverage and record the permanent
disposition. Do **not** change CNA to synthesize finite 8/-8 values.

## Issue state and next task

No CNA runtime issues repaired by this campaign; seven verified pre-existing fixes
are recorded, and one proven demo-expectation correction (116) is pending implementation.
No production source repair has begun. Both builds, all enabled demo CTests, full
SOFTWARE/OPENGLES3 smoke, exact visuals and the main native suite are now captured.
The all-ID matrix exists; individual investigation continues, with all pending
dispositions explicit. Run the six separate Wine/reference CTests on the private
runner, then begin the prepared documentation batch. Preserve the native MSAA
baseline failure and investigate it in the graphics/test-oracle batch.

ISSUE-001 is now minimized and confirmed on actual CMake 3.28.6 and 3.29.9; the same
cyclic two-library fixture passes with 3.30.9 and 3.31.6. The current CNA helper is
used unchanged. The old claim that 3.29 is sufficient is disproved: WHOLE_ARCHIVE
feature deduplication added in 3.30 is decisive, even with CMP0156 OLD. Official
download checksums were verified; isolated tools and fixture are in the existing
ignored `../cna-demos/build-probe`, not installed globally. Logs use
`repair-baseline-shared-runtime-*`. Repair either the admitted shared-library
version range or archive construction with this regression; do not arbitrarily
change the default on the already-working current toolchain.

Prepared but UNAPPLIED Phase B patches are in
`../cna-demos/build-probe/repair-docs-{comments,tests,example}.patch`:
037/040/042/049/084/107/153/154/155 public comment corrections, a digest-example
test, and a PacketReader example that actually asserts ReadColor round-trip
instead of counting every outcome as a passing historical quirk. Review/apply,
build and focused-validate before recording closure. Existing runtime behavior is
unchanged. Then correct 116's demo and add native value/out/alias classification
coverage, followed by the coherent Curve batch (002/036/097/137/138).

Then start documentation-only contradictions before coherent math/core batches.
Retain the demo oracle. Record fix commits and native/focused/regression results
for each batch, update this file and the matrix, commit, and immediately continue.

Campaign files: this handoff and `CNA_DEMOS_REPAIR_MATRIX.json`; baseline evidence
is ignored build output. No production or demo assertions have been changed.
Initial inventory/handoff commit: `13275d0df`. Current demo documentation revision:
`2552912a5adcdbe5129f2ab1649a28062036a349` (baseline/handoff and permanent 116
oracle evidence; the previous 5328878 moved 021/041 to Resolved). Demo runtime
source/binaries remain those of the starting baseline. CLI validation is 719/0
and all 155 unique issue headings remain. Catalog/inventory CTests passed after
the previous status reconciliation.
This handoff and matrix update are committed together as a status checkpoint;
no other uncommitted work is intended. Main native baseline is finished with the
one documented MSAA failure; the separate Wine/reference subset remains pending.
No push or merge.
