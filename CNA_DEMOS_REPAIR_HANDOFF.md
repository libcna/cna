# CNA demos repair campaign

## Campaign contract

Repair every actionable defect inventoried in `../cna-demos/docs/CNA_ISSUES.md`.
Preserve historical IDs and assertions; verify current behavior and XNA evidence
before changing production code. Work in coherent subsystem/root-cause commits.
Do not push or merge. Unavailable hardware/services must be stated explicitly.

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

Full native build is running in `cmake-build-debug` (2,829 scheduled steps,
OPENGLES3, tests enabled); log is `repair-baseline-native-build.log` in that
same demo reports directory. No native-test result is yet claimed.
Its existing Draco dependency emits CMake CMP0148 and deprecated implicit-`this`
capture warnings; native tests also emit existing ignored-nodiscard warnings.
These are retained, not suppressed or counted as repair regressions.
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
No source repair has begun. Complete the
all-ID repair matrix, inspect the remaining issue evidence, finish current CNA
and demo builds, run native tests and all demo CTests plus partitioned SOFTWARE
and OPENGLES3 smoke, and compare statuses and individual failed assertions with
Complete1128. Record setup failures separately from behavioral failures.

Then start documentation-only contradictions before coherent math/core batches.
Retain the demo oracle. Record fix commits and native/focused/regression results
for each batch, update this file and the matrix, commit, and immediately continue.

Campaign files: this handoff and `CNA_DEMOS_REPAIR_MATRIX.json`; baseline evidence
is ignored build output. No production or demo assertions have been changed.
Initial inventory/handoff commit: `13275d0df`. Current demo documentation revision:
`53288781957556f29e6cc987b7566faed45adaca` (021/041 status reconciliation only;
runtime source/binaries remain those of the starting baseline). CLI metadata and
catalog/inventory CTests both pass after this documentation update (719 IDs).
This handoff and matrix update are committed together as a status checkpoint;
no other uncommitted work is intended. Native baseline build remains running.
No push or merge.
