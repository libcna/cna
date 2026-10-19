# CNA demos repair campaign

## Campaign contract

Repair every actionable defect inventoried in `../cna-demos/docs/CNA_ISSUES.md`.
Preserve historical IDs and assertions; verify current behavior and XNA evidence
before changing production code. Work in coherent subsystem/root-cause commits.
Do not push or merge. Unavailable hardware/services must be stated explicitly.

## Current checkpoint (2026-10-19)

**Latest validated batch:** Curve 002/036/097/137/138 is closed FIXED_VALIDATED by
CNA `d753d0997`. All 155 historical IDs are retained: five fixed runtime findings,
23 documentation corrections, seven already-fixed findings, one demonstrated demo
expectation error, 119 pending final disposition. Demo closure is about to be committed
after demo HEAD `81bc0e2`; this status supersedes all earlier pending-Curve notes below.
All 732 enabled demo CTests pass (412.58s, 48 skipped/three disabled unchanged).
SOFTWARE and private-display OPENGLES3 math sweeps each have 95 demos, 51 passes and
44 unrelated expected failures. Exact comparisons show only five Curve repairs plus
earlier 116 oracle correction; every unrelated failed assertion/check count is unchanged.
Native math 886 passed/two existing fixture skips; aggregate Curve/readers 92/92,
registered Curve CTests 74/74; native C17/Werror ABI smoke and extended 17-check C demo
pass. Targeted collection production-TU ASan/UBSan alias probe passes with leak checking.
Five knownIssue markers removed only after actual passes; all assertions preserved.

Phase C next batch: ISSUE-117 perspective. Matrix production is still unchanged.
Three new native MatrixPerspectiveCompatibilityTests and the native C API MatrixSmoke
Pi boundary regression are uncommitted. Their two-job native rebuild is running in
`repair-117-native-before-build.log`; capture expected pre-fix failures next. Genuine
XNA PerspectiveOracle is running (`repair-117-xna{,-build,.stderr}.log`) using the
existing private Wine environment. Do not confuse these new tests with the completed
Curve checkpoint. No other production change, no push/merge. Current dirty work is
these next-batch tests/probe plus closure evidence/status pending their coherent commits.
Last complete renderer smoke and six exact visuals remain the original baseline;
this math batch adds both full math sweeps and the full SOFTWARE CTest checkpoint.

## Earlier Phase C progress (superseded by the checkpoint above)

**Authoritative latest state:** Phase B is complete; Phase C Curve repair is in progress.
Latest continuation overrides the pending-build statements below: Curve runtime/tests
are committed as `d753d0997`; documentation status commit is `8991423cd`, matching demo
closure `81bc0e2`. Demo rebuild and marker-only rebuild passed (232 and 13 scheduled
steps, no warnings). All five original Curve reproductions pass with their full
6/17/20/4/7 checks under both renderer labels; external C API workflow passes all 17
raw checks, and the native C17/Werror CurveSmoke passes against that exact rebuilt
demo library. The five knownIssue markers are now removed, assertions retained.
The complete 95-demo math category on SOFTWARE has 51 passes / 44 expected failures.
An OPENGLES3 category attempt without a display exposed that math.color.named_colors
is a Game demo: its missing-video-device failure is preserved in
`repair-curve-math-OPENGLES3-no-display.*`, NOT accepted as a regression pass. Rerun
the category with the private display. Full demo CTest is running with three jobs:
`repair-curve-demo-ctest.log`. Do not exceed the three-job campaign cap by starting
another test/compiler until it finishes. Then run private OPENGLES3 math, compare
all math assertions to the baseline, close the five Curve IDs with d753d0997 and
commit the demo evidence/markers. Documentation checkpoint skip count is unchanged:
48 skipped / three disabled. Curve primary inventory/matrix closure is still pending.
All 95 SOFTWARE math results and exact failed-assertion multiplicities now compare
successfully: only the five Curve fixes and earlier Matrix oracle 116 change status;
097 restores 6 -> 20 executed controls. Report `repair-curve-math-SOFTWARE-comparison.json`.
Ignored comparison helper: demo `build-probe/repair-curve-compare.mjs` (run for GLES
after its private-display rerun). Next-batch preparation only: untracked demo
`docs/repair-evidence/issue-117/PerspectiveOracle.cs` has NOT been compiled/run yet.
It checks exact XNA exception type/ParamName, refusal order, unchanged out matrices,
Pi boundary and intentional IEEE dimension/aspect behavior. Do not include this
unvalidated probe in the Curve evidence commit. Matrix production is unchanged. Three
new native tests in MatrixPerspectiveCompatibilityTests.cpp and a native C API
MatrixSmoke Pi/output-preservation check are now applied but NOT built/run. Build
these against the unchanged Matrix implementation first and preserve the failures.
Then run the genuine XNA probe, correct the stale std exception catches in the demo,
and repair only the proven boundary/exception contracts. Keep this next-batch work
out of the Curve closure commit.

Later 118/119 triage caveat: genuine XNA decompiled methods declare float squared
intermediates, yet historical x86 runtime probes report wider finite ranges. Do not
infer their actual JIT rounding/range solely from that C# spelling. The official C#
specification permits wider intermediate floating-point precision/range:
https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/types#837-floating-point-types
Confirm fresh genuine runtime results (and ordinary/zero/NaN/Infinity controls) before
choosing a local numerical repair. No 118/119 production change is made.

CNA HEAD `60b6b747f`; documentation fixes are `66918da04` (nine C++ findings) and
`60b6b747f` (fourteen C API findings). Demo HEAD before closure commit: `997f2d8`.
The permanent inventory and matrix now retain all 155 IDs: 23 DOCUMENTATION_CORRECTED,
seven ALREADY_FIXED, one DEMO_EXPECTATION_BUG (116), 124 awaiting final disposition.
The 23 resolved documentation IDs are 037/040/042/049/059/060/061/063/064/065/068/069/
071/072/074/076/079/080/084/107/153/154/155. No runtime failure marker was suppressed.

Last full demo regression checkpoint: all 732 enabled CTests passed, three disabled,
391.13 seconds (`repair-docs-ctest.log`). Metadata 719/0, warning-free full demo build;
both focused SOFTWARE/OPENGLES3 runs exactly matched all 28 baseline results, including
check counts and failed-assertion multiplicities. Native documentation checks: 266/266;
packet example 9/9. Hardware/service/DirectX9/WebGPU claims remain source-only where unavailable.
The last complete two-renderer smoke and six exact visuals remain the initial baseline;
the documentation checkpoint is not a new full smoke/visual claim.

Uncommitted runtime work: Curve.cpp, CurveKey.cpp, CurveKeyCollection.cpp and matching
native math/C API regressions. Confirmed-open Curve IDs 002/036/097/137/138 are repaired
locally but not closed. Native build passes; math 886 passed/two unchanged fixture skips;
aggregate Curve* 92/92; registered Curve CTests 74/74 (14.20s); targeted production-TU
ASan/UBSan aliasing probe passes, replacing its pre-fix invalid-vptr failure.
The separate full demo rebuild with Curve is now running with three jobs, log
`repair-curve-demo-build.log`. C API demo gains real Step/post-loop controls, all original
checks retained. Its two assertions and native C API oracle still require this rebuilt
demo library validation. Five C++ knownIssue markers remain until actual passes.

Exact next task: finish Curve demo build; run all five focused C++ witnesses under both
renderer labels and native/external C API Curve tests; then remove only the five proven
markers, run full math demos and regression checkpoint, commit Curve with tests, close
IDs with commit evidence and immediately continue Math/Core. ISSUE-001 remains confirmed
open with an unapplied build-support candidate. No push/merge. Remaining dirty state:
campaign handoff/matrix, Curve sources/tests, demo exception catches/C API controls,
genuine-XNA evidence and documentation closure records. Earlier notes below are historical.

## Earlier checkpoint details (superseded where noted above)

Latest continuation: full demo rebuild passed (1,409 scheduled steps), log
`repair-docs-demo-build.log`. Documentation-focused SOFTWARE rerun: all 28 demos
match baseline status, knownIssue, check count and exact failed-assertion multiplicities;
`repair-docs-focused-SOFTWARE/comparison.json` has no differences. OPENGLES3 equivalent
and full demo CTest suite are running (`repair-docs-focused-OPENGLES3-driver.log`,
`repair-docs-ctest.log`). No production Curve patch is applied yet. Current CNA HEAD
is `82b1bf60c`, demo HEAD `997f2d8`; dirty work below remains intentionally uncommitted
until this checkpoint finishes. Historical running statements below are superseded
by later evidence in this document.

Subsequent checkpoint: both focused renderer runs completed with 28/28 exact baseline
matches. C++ documentation commit `66918da04` covers 037/040/042/049/084/107/153/154/155;
C API documentation commit `60b6b747f` covers 059/060/061/063/064/065/068/069/071/072/074/076/079/080.
All 266 native contract tests and the 9-row packet example passed before these commits.
Full demo CTest is still running against the completed documentation-only demo build;
no documentation ID is closed in the inventory until that run is audited.
The repaired exception catches restore all 20 key-collection demo checks under both
renderer labels and reveal exactly the known nearby-position ordering failure (097).
Evidence: `repair-curve-key-collection-before-{SOFTWARE,OPENGLES3}.{json,log,stderr.log}`.

Production Curve patch is NOW APPLIED in three math implementation files, with the
previously proven native regressions. Native-only rebuild is running with one job:
`repair-curve-native-after-build.log`, targets CnaTests/CnaMathTests/cna_shared in
cmake-build-debug. This does not rebuild or replace the separate demo build's libraries;
the concurrent documentation CTest retains its original runtime provenance. Wait for
that CTest before rebuilding demos with Curve. Next: native math/Curve, C API and
targeted sanitizer verification, then focused demos, remove only proven knownIssue
markers and commit the root-cause batch. No Curve issue is closed or repair committed yet.

Native Curve after-repair results: build passed (17 scheduled steps, no warnings),
all 888 native math cases ran: 886 passed, the same two missing ShipGame fixture skips,
zero failures (`repair-curve-all-math-after.log`). Direct aggregate CnaTests Curve*
selection also passed all 92 cases, including content-reader suites
(`repair-curve-aggregate-after.log`). Targeted production-TU ASan/UBSan rebuild and
unchanged aliasing probe passed with leak detection enabled and no diagnostic;
`repair-curve-sanitizer-after{,-build}.log`. This replaces the pre-fix invalid-vptr
failure, not a whole-runtime sanitizer claim. Demo/C API validation remains pending.

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
behavior are unchanged. The all-target incremental rebuild was deliberately interrupted
(exit 130, `repair-docs-native-build.log`) after compiling production/test objects, to
avoid rebuilding unrelated examples for this documentation batch. It is not a passed full
build. Targeted rebuild is running in cmake-build-debug: cna_shared, CnaTests and affected
Audio/Content/Design/Devices/Graphics/Media/Net test executables plus the changed packet
example; log `repair-docs-native-target-build.log`. C API route generation check passes unchanged.
Pending: build demos, run focused native/demo contract checks, update coverage
inventory if required, resolve the documentation IDs with actual evidence, commit
the coherent batches separately. Then Curve root causes (002/036/097/137/138).
Uncommitted CNA work: documentation headers/comments, digest regression, PacketReader
example assertions, matrix/handoff. No push or merge. Historical sections below preserve
baseline progress; this checkpoint supersedes their pending-run statements.

Continuation details while the documentation rebuild runs:
- Native test-routing correction: CTest invokes aggregate `CnaTests`, not `CnaMathTests`.
  Building only the module executable did not update CTest's discovered list. The first
  Curve CTest control therefore ran 66 old tests (all passed); archived as
  `repair-curve-native-before-stale-registration.log`, NOT accepted as new regression coverage.
  Direct current module execution then runs 73 Curve tests and fails six as expected; the
  nearby-key case initially used std epsilon instead of CNA's half-ULP epsilon, so that test
  is corrected to the exact original witness and aggregate relink/CTest rerun is active.
  Logs: `repair-curve-native-before.log`, `repair-curve-native-before-exact.log`.
  Matrix closure received the same audit: the original 91 CTests covered old cases only.
  Fresh `CnaMathTests` direct run from CNA root passes ALL 92 Matrix tests, explicitly including
  the new singular-affine case (`repair-116-native-direct.log`). The preceding wrong-cwd run
  missed the existing oracle corpus, failed two corpus guards, and remains preserved as
  `repair-116-native-direct-wrong-cwd.log`; it is not accepted as a code regression or pass.
  Rebuild CnaTests when adding tests before relying on CTest registration.
- Exact pre-fix results are now captured: 73 native Curve CTests, 66 passed / seven failed
  (exit 8, 12.03 seconds). Failures are nearby replacement, aliasing replacement, NaN CompareTo,
  post-loop tangent, shifted/non-unit Step, duplicate/tiny spans and NaN evaluation. The corrected
  native C API CurveSmoke fails stage 2 (exit 2) on its new NaN comparison expectation. This
  contrasts with the original C API test's pass and proves the old native oracle was wrong.
  Genuine XNA exact witness rerun passes all 28 checks. Permanent source/evidence are now
  pending in demo `docs/repair-evidence/curve/`; no Curve production fix is applied yet.
- Targeted ASan/UBSan instrumentation of actual `CurveKeyCollection.cpp` confirms a related
  setter lifetime defect: replacing from the last element reads a destroyed CurveKey after
  erase (invalid vptr at Add line 88, setter line 59). Exit 1, logs
  `repair-curve-sanitizer-before{,-build}.log`. Other runtime units are not instrumented; do not
  call this whole-engine sanitizer coverage. Probe/build driver are in demo build-probe:
  `repair-curve-collection-asan.cpp`, `repair-curve-sanitizer-build.mjs`. Rerun unchanged after
  applying the prepared runtime patch, which copies the replacement before erase. A matching
  native last-element regression is now added (74 Curve cases after the next rebuild).
- C API route generation, refreshed coverage check, public-header audit and release-gate
  consistency check all pass (`repair-docs-c-api-*`, `repair-docs-coverage-current.log`).
  Release remains intentionally not-ready under its existing owner decisions; no decision,
  approval pin or release policy was changed by this campaign.
- Complete native math pre-fix sweep: 888 tests, 879 passed / two pre-existing missing ShipGame
  fixture skips / exactly the seven new Curve failures, exit 1. No unrelated math failure.
  Logs `repair-curve-all-math-before{,-build}.log`. This includes the last-element alias test.
  Prepared external C API demo extension `build-probe/repair-curve-c-api-demo.patch` is UNAPPLIED;
  apply only after the shared runtime fix passes, as it adds real Step/distinct-tangent checks
  and describes the issues as resolved. Existing checks remain intact. After fixing, require
  all 886 runnable math tests, both C/C++ focused demo paths and the targeted sanitizer to pass.
- A candidate build-support policy patch for 001 is also prepared, UNAPPLIED, in demo
  `build-probe/repair-001-support.patch`. It raises only the shared-runtime floor to CMake 3.30
  (static older toolchains stay supported) and adds a small policy regression. Review before
  applying; actual 3.28/3.29/3.30 fixture evidence remains the deciding contract evidence.
- Targeted native documentation build passed (28 scheduled steps, no warnings); all 266
  selected native contract CTests passed in 47.64 seconds, no skips. Packet example passes
  9/9 rows, including its now-real ReadColor assertion. Logs: `repair-docs-native-target-build.log`,
  `repair-docs-native-ctest.log`, `repair-docs-packet-example.log`. Full demo build is running
  with two jobs, log `repair-docs-demo-build.log`; no documentation issue is closed yet.
- Genuine XNA Curve probe now passes all 28 checks, including tiny spans, NaN/Step behavior,
  comparison and nearby replacement. Logs: `repair-curve-xna{,-build,.stderr}.log` (actual stderr
  filename is `repair-curve-xna.stderr.log`); stderr contains private display startup/shutdown
  messages, not a claimed empty stream. Native original C17/Werror CurveSmoke compiled and
  passed with empty stdout/stderr against the demo C API library: `repair-curve-c-api-before*`.
  New native C++ and C API regression patches are NOW APPLIED, production Curve code remains
  UNCHANGED. One-job native C++ build and failing-witness run are active, logs
  `repair-curve-native-tests-build.log` and `repair-curve-native-before.log`. Do not mistake
  intentional pre-fix regression failures for regressions caused by the documentation batch.
- `generate_coverage_inventory.py --check` correctly detected moved declaration locations.
  Normal `--write` (not approval regeneration) changes only the inventory SHA-256; all 467
  headers/8,109 symbols and status counts are unchanged. First sandboxed write was refused;
  authorized rerun succeeds. Logs: `repair-docs-coverage-{check,write}.log`.
- Prepared next-batch patches live in demo `build-probe/repair-curve-{runtime,tests,
  c-api-tests}.patch`; only runtime remains UNAPPLIED. Read files, not stale in-memory drafts.
  Native Curve post-loop and
  C API CurveSmoke NaN expectations themselves incorrectly preserve old CNA behavior; the
  patches correct those from genuine XNA source and add regressions. The tests are running
  against the old implementation first; apply runtime repair only after the witnesses fail.
  `repair-curve-oracle.cs` is the genuine-XNA probe that passed all 28 checks above.
- Native debug and multi profiles both have C API tests OFF. Do not claim native C API runtime
  coverage from either. The pure C CurveSmoke has no external fixtures and can be compiled with
  its exact C17/Wall/Wextra/Wpedantic/Werror/Threads policy against the demo's actual
  `build/linux-full/CNA/modules/c-api/libcna_c_api.so` in the existing demo build-probe directory.
  Preserve a pre-repair pass and the evidence-backed changed expectation before the Curve fix.
- Demo `math.curve.key_collection` now has an uncommitted expectation correction for the
  pre-campaign System exception migration: ArgumentOutOfRangeException for indices,
  ArgumentException for insufficient CopyTo destination. Its 20 assertions and ISSUE-097 marker
  remain unchanged. Build/focused-run pending; require the actual ordering witness to fail
  before applying its production repair. Demo HANDOFF records this overlapping preparation.

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
