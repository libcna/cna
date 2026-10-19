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
capture warnings; these are retained, not suppressed or counted as repair regressions.
SOFTWARE category-partitioned smoke is running with explicit SDL X11 on the
private runner. `/tmp/cna-repair-smoke.mjs` validates each category and exact
719-ID coverage before writing the merged report. Next run OPENGLES3 serially.

The complete historical issue inventory has been read. The machine-readable
matrix is `CNA_DEMOS_REPAIR_MATRIX.json`, containing exactly IDs 001–155, source
locations, associated demos and provisional subsystem/classification fields.
PENDING_VERIFICATION is deliberately not a final disposition. ISSUE-021 is
historically described as fixed despite remaining beneath the Open heading;
verify it and reconcile the section rather than treating the heading as truth.
ISSUE-041 has a source-confirmed pre-campaign fix (`a83673fc5`), but its demo also
contains ISSUE-098 failures; whole-demo WILL_FAIL cannot prove either independently.
The transient compile failures 044, 052 and 053 are verified ALREADY_FIXED by
the current successful build. The other classifications are provisional until
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

## Issue state and next task

No issues repaired by this campaign; three verified pre-existing fixes are recorded.
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
