# CNA demos repair campaign

## Campaign contract

Repair every actionable defect inventoried in `../cna-demos/docs/CNA_ISSUES.md`.
Preserve historical IDs and assertions; verify current behavior and XNA evidence
before changing production code. Work in coherent subsystem/root-cause commits.
Do not push or merge. Unavailable hardware/services must be stated explicitly.

## Current checkpoint (2026-10-20)

Phase C completion / Phase D repair. CNA runtime031693a9f(components141–143),
b086814f9(launch144), external frozen1ad0a3916(Color132); last complete checkpoint5ab1c7729
plus99d790167(containment). Demo HEADe7eafac(Color focused/markers); containment
closure45549e0, marker74622b4. All155 IDs retained:57 FINAL/98 PENDING:
25 runtime findings,one verified duplicate,23 documentation corrections,seven
already fixed before campaign,one demonstrated demo-expectation defect.

LAST COMPLETE REGRESSION: containment127/129/133 now FIXED_VALIDATED. Native963PASS/
two unchanged skips;registered247/247;consumers1408PASS/four unchanged skips;
actual four-TU ASan/UBSan/float-cast-overflow/leak13/13. Strict actual C ABI Geometry/
Vertex smokes pass; focused17 per renderer15PASS/two unchanged reserved131 failures.
Both95math91PASS/four expectedFAIL. Full719SW603/65/48/3,GLES642/67/7/3,exact46
cumulative expected changes,differences[]. Metadata719/0,tools7+4+5,six exact visuals.
Isolated full CTest732accepted/zero failures/48skips/three disabled,1233.07s;
executable and both DSOs SHA256 reverified after completion. Permanent evidence
../cna-demos/docs/repair-evidence/geometry/CONTAINMENT_CHECKPOINT.md.
Complete1128 demo-status delta48; current-HEAD campaign delta46. External freeze
released. Owner-reserved131 remains open; ray method/exception untouched.

ACTIVE NEXT: Color132 FIXED_PENDING_REGRESSION, runtime1ad0a3916. External build
and marker rebuild COMPLETE warning-free; strict actual C ABI ColorSmoke and all6
focused scenarios per renderer PASS. Two markers removed, original7/9 checks remain.
Color executable/C++/C ABI frozen with SHA256/ldd repair-color-frozen*. DO NOT rebuild
external until complete checkpoint. Full driver79621 COMPLETE: both math sweeps
and both full719-ID sweeps exact48 changes/differences[]; SOFTWARE605/63/48/3,
OPENGLES3 644/65/7/3; tools and six exact visuals pass. Frozen hashes reverified.
Full isolated CTest session55689 RUNNING(one job), repair-color-demo-ctest.log;
do not overlap SystemLink suites. Lifecycle production is native-validated
separately in native tree. Final hook companion build96698 COMPLETE warning-free;
77/77 focusedPASS(4.770s), actual five-TU ASan/UBSan/leak58/58PASS(1.771s).
Repeated broad native consumer job76785 COMPLETE: exact1412consumers1408PASS/four
unchanged skips256.700s; all33 runtime suites234total231PASS/three same skips15.440s.
Native executable/DSO repair-lifecycle-hook-native-frozen.sha256 verified unchanged;
native freeze released. External Color freeze remains in force.
Color native already complete: genuine/native1782/1782; math966/two skips;
registered70/70; actual Color.cpp ASan/UBSan/float-cast-overflow/leak64/64;
exact1412 consumers1408PASS/four same skips(282.277s). No external pass/closure yet.
Prepared drivers build-probe/repair-color-{c-smoke,focused,math,regression}.mjs;
exact comparators admit only48 cumulative changes(two Color demos additional).

COMMITTED CNA: component root-cause batch031693a9f and launch batchb086814f9,
including matching regressions and C contracts/smokes. All four matrix rows now
FIXED_PENDING_REGRESSION; no lifecycle external validation/closure yet.
Status commitc01e04275; lifecycle evidence8b38d2a, storage sharing edges2129f3f.
UNCOMMITTED: latest145/152 evidence and matrix/handoff; no production changes.
Pre-fix standalone57total39PASS/18 intendedFAIL/zero skips907ms.
First repaired native76/76 including19 Game controls; initial three warnings exactly
match baseline GameTests ignored Remove results, then strengthened assertions build
warning-free. Actual five production-TU ASan/UBSan/leak57/57PASS(2.981s). First
sanitizer harness added Wall/Wextra beyond Game.cpp native policy and failed on
existing renderer unused parameters/reorder/sign comparisons; retained. Corrected
Game.cpp policy is native flags plus Werror and instrumentation, other TUs keep
extra Wall/Wextra/Werror. Native broad consumer checkpoint COMPLETE:1412total,
1408PASS/four same skips284.362s; all33 runtime-source suites233total230PASS/three
capability skips10.789s. Native binary/DSO hashes reverified, that freeze released.
Actual process launch probe8checks: old verified external DSO five failures, repaired
native8/8PASS. Changed actual C adapter and both strict C17 smokes compile separately
without touching frozen external DSOs; C runtime still pending.
NEW companion: owned property callback can remove the last reference before the
derived hook returns. New all-four-properties test fails while prior57pass
(repair-property-hook-ownership-before*,1.377s). Local private ownership helper now
pins through all four setters and Dispose; no public layout/vtable change. Final
77-test focus and58-test sanitizer pass; final broad consumers also pass as above.
Genuine lifecycle34+8+3+6 checks pass; current actual C ABI disposal11checks/seven passes/
four failures. Existing Game19/19 controls pass. Derived explicit disposal always
invokes UnloadContent before event, even without Initialize; false path silent.
Collection Clear uses live-index iteration, not snapshot/fail-fast. Game.Dispose
requires an owning original snapshot. Keep owned references alive across the entire
virtual component Dispose, nested removal, exceptions and re-registration.
See ../cna-demos/docs/repair-evidence/lifecycle/ and prepared sanitizer builder.
Important adjacent demo-oracle correction pending for lifecycle: drawable_game_component
currently expects one UnloadContent after two explicit Dispose calls. Genuine derived
disposal and IL require two. Preserve check count, change exact oracle with evidence
only when preparing that external batch; current frozen Color binary retains old
runtime/oracle. Do not accidentally include lifecycle edits in a Color rebuild.
Second adjacent demo-oracle correction pending: standalone/c_api.game.launch_parameters/main.c
currently expects bare/equals-only flags dropped and duplicate Add SUCCESS. Genuine
retains both as empty values and duplicate maps INVALID_ARGUMENT; sorted name/count
expectations must expand from3 to5 initially (6 after Alpha), preserving checks and
UTF8/guard/atomicity controls. Native RuntimeGameSmoke's corresponding old count/name
expectations are now corrected too; latest strict compile passes, actual C runtime pending.
Third adjacent correction: c_api.game.component_members expects one callback/event
after two explicit disposals. Genuine requires two; preserve the existing assertion
and single drawable-disposal check. No external demo-source changes yet.

Next storage056/151/152 confirmed at correct lower layer, no production edits.
Genuine EOF13/13 versus seven native failures; genuine storage172/172 versus
native170checks29PASS/141FAIL (two FileStream share checks cannot compile yet).
Native disproves share enforcement and disposed guards, including real mutations
of isolated fixtures. Container shorter defaults are exclusive,Append write-only;
direct FileStream defaults read sharing. New genuine sharing-edge15/15 checks prove
denied Create/Truncate preserves bytes and exclusive locks reach a child process.
Native direct write-only Open/OpenOrCreate loses existing data:4/4FAIL; preserve
these controls in the same lower-layer open repair. Evidence2129f3f.
Sharp baseline build session8883 RUNNING, existing build/Debug tree,--parallel2;
at most three campaign jobs including Color CTest. No Sharp production edits.
Next: verify warning-free build, then full component gate with original Yacht SOAP
fixture /rv/tmp/samples/SAMPLE-071-Yacht_4_0/xna4-build/bin on private loopback port.
Never overlap the campaign's SystemLink suites. Sharp Runtime rules/StandingApprovals
read; at most TWO Sharp jobs, layout/migration/full-gate requirements apply.
145 newly CONFIRMED_OPEN: genuine title-path20/20; native6PASS/14FAIL. Historical
missing-file exception subdefect already fixed; existing demo now fails only two
of ten checks. Empty/rooted/invalid-name and directory categories remain wrong.
No title runtime edits; preserve case-insensitive lookup and Android assets.
009/020/029/150 independently confirmed despite associated passing demos.
Linux x86_64/Radeon780M/Mesa only; private displays only. At most THREE campaign
jobs overall. No push/merge. Continue automatically through remaining batches.

## Superseded containment preparation (2026-10-20)

Phase C. Current CNA product1ad0a3916 (Color132); external frozen containment5ab1c7729
plus C regression companion99d790167. Demo evidencea0b2fb6, containment74622b4,
latest final closure2cea947.
All155 historical IDs retained:54 final/101 pending (22 runtime,
one verified duplicate,23 documentation,seven already fixed,one demo expectation).
Latest final closure024/128/130/135 plus126 duplicate024, runtimee1377c437/a8a60bf03.
LAST COMPLETE REGRESSION: geometry values/text frozena8a60bf03; native950/two skips,
consumers1393/four skips,registered341/341,strict actual C ABI passes. Both719-ID
sweeps exact42 cumulative changes:SW599/69/48/3,GLES638/71/7/3. Six exact visuals.
Full isolated CTest732accepted/zero failures/48skips/three disabled1248.07s. Original
first-run same-gamertag network-discovery failure remains retained; no code/assertion
change before rerun. Do not overlap SystemLink/discovery suites. All external
frozen SHA256 values verified unchanged. External freeze now released.

ACTIVE NEXT: containment127/129/133 FIXED_PENDING_REGRESSION in commit5ab1c7729.
Native math963PASS/two skips;registered247/247;consumers1408/four skips;four-TU
ASan/UBSan/float-cast-overflow/leak13/13. Final formatted build warning-free and
math963/two skips again. Actual external C ABI/demos now rebuilt; no closure yet.
Owner-reserved131 ray body/exception untouched; point containment naturally fixes
its boundary branch, but outside-origin entry remains unimplemented.
NEXT TASK: containment full CTest NOW RUNNING alone on network,ONE job,session9033,
repair-containment-demo-ctest.log. Both full smokes already completed successfully;
do not overlap SystemLink/discovery suites. External binaries frozen with recorded
SHA256/ldd (repair-containment-frozen*); DO NOT rebuild external until checkpoint done.
Strict C17/Werror GeometrySmoke/VertexValueSmoke pass actual rebuilt ABI; focused17
per renderer have15PASS/two unchanged expected131 failures. Three additional old C
boundary oracles contradicted genuine XNA and were corrected, all counts retained;
original failures preserved. Four repaired markers removed only after focusedpass.
Both95 math91PASS/four expectedFAIL,exact46 cumulative changes,differences[].
Six exact visuals pass; representative GLES image inspected. Full719 SOFTWARE
COMPLETE603PASS/65expectedFAIL/48SKIP/3manual; exact46 changes,differences[].
Full719 OPENGLES3 COMPLETE642PASS/67expectedFAIL/7SKIP/3manual; exact46changes,
differences[]. SHA256 frozen executable and both DSOs verified unchanged after smoke.
Color132 committed1ad0a3916, FIXED_PENDING_REGRESSION. New native tests first fail
three (968total,963pass/two skips/three failures); pre C smoke fails two new stages.
Color fixed-point repair now passes native966/two skips, standalone1782/1782,
actual Color.cpp ASan/UBSan/float-cast-overflow/leak64/64. One first post-native run
used wrong cwd and could not load8 checked-in fixtures; original retained, rerun
from CNA root passes all966/two skips. RegisteredColor/packing70 tests running
session46419 COMPLETE70/70pass11.07s. Additional packed/color/vertex/avatar CPU-oriented
consumer selection156PASS/one unavailable effect-fixture skip(157total,7.739s);
not a full renderer suite. No Color external rebuild/closure yet. Exact1412-test native
consumer inventory COMPLETE1408PASS/four same skips(1412total,282.277s),
repair-color-native-consumers.log; private display/separate XDG. Native freeze now
released. Runtime additions below were compiled standalone and did not relink
that frozen native executable. External containment freeze still applies.
UNCOMMITTED: lifecycle native test additions in LaunchParametersTests.cpp,
GameComponentCollectionTests.cpp and new ComponentLifecycleCompatibilityTests.cpp;
handoff and matching demo evidence. No lifecycle production edits. Native standalone
values44total39PASS/five intendedFAIL; real-Game lifecycle6/6 intendedFAIL,zero skips,
private display1.014s. Genuine29+8passes. Standalone Game strict build uses Wall/Wextra/
Werror (not extra Wpedantic, which rejects Decimal's supported __int128); original
build and missing-executable launch failures retained. Native CnaTests now also
integrates all eleven new runtime tests: warning-free13-step build, private-display
50tests39PASS/eleven intendedFAIL/zero skips1.063s,repair-lifecycle-native-before*.
Runtime production is still unchanged (Color1ad0a3916).
Five further collection ownership/mutation tests now fail in the standalone values
suite49total39PASS/ten intendedFAIL; they are NOT yet in the integrated native binary.
Genuine Clear mutation oracle3/3 establishes live-index iteration, NOT snapshot or
fail-fast behavior; no mutation-version field/ABI change needed. Game.Dispose still
requires its distinct original owning snapshot. Existing GameTest controls19/19PASS.
Reports repair-collection-ownership-before*,repair-collection-mutation-xna*,
repair-lifecycle-game-controls-before.log. C forwarding disposal currently relies on
incorrect canonical one-shot behavior; review adapter/comment/smoke with runtime fix.
Keep external Color build free of lifecycle production changes until
it is frozen for its own checkpoint. Linux x86_64,private display,Radeon780M/Mesa only;
no all-platform claim. No push/merge. Continue automatically after each batch.

Prepared validation drivers (ignored build-probe): repair-containment-{focused,
c-smokes,math,regression}.mjs; comparison phase containment admits exactly four
additional repaired demo statuses (46 cumulative). Focused driver includes17
scenarios per renderer, retaining the two131 expected failures. Full smoke and
full CTest must run sequentially to isolate SystemLink discovery. No blanket
network exception has been added to either exact comparator.
Read-only future storage audit confirms151 missing disposed guards,152 discarded
FileShare and missing lower-layer constructor,056 EOF/fail state reaches tellg.
Sharp Runtime current AGENTS/CLAUDE rules inspected (excluding historical count
ledger): consumer-requested dependency fixes authorized, zero-warning gate and
at most TWO build/compiler/test jobs there, fixed reusable build directories;
docs/StandingApprovals.md now read completely (661lines), including SA-3/SA-15.3
layout/base-class gates and SA-5 evidence/migration/full-gate requirements. Its automatic-push
rule is overridden by this campaign's explicit no-push instruction. No Sharp
Runtime modification, build or closure claimed. Local .NET reference is a .NET11
preview snapshot, not XNA/.NET Framework behavior by default.
Fresh genuine .NET Framework4 EOF probe13/13PASS versus direct current native7/13
failures: EOF state also loses a following write, not just position. Fresh genuine
Storage probe172/172PASS (isolated private-root injection, NOT construction/device
validation) proves disposed guards,144 share combinations, and correct shorter
defaults. Historical152 suggestion to retain ReadWrite default is wrong: genuine
Storage OpenFile2/3args is exclusive; 2argAppend choosesWrite; FileStream3args itself
defaultsRead sharing. Future repair must distinguish those contracts. Permanent
../cna-demos/docs/repair-evidence/storage/. All probes remove only their new fixtures.
Passing-demo audit freshly CONFIRMS_OPEN009/020/029/150 via independent public-header,
actual-link and compiled-song-root probes; no already-fixed inference from their
associated passing demos. Permanent ../cna-demos/docs/repair-evidence/api-shape/.
Matrix056/141/142/143/144/151/152 now explicitly CONFIRMED_OPEN after fresh genuine/
native witnesses; final count unchanged54/101. C component disposal actual frozen ABI
probe11checks/seven passes/four failures confirms self-removal and repeated dispatch;
cleanup control independently passes. Native storage real acquisition/isolated XDG
probe170checks29PASS/141FAIL:119 incompatible share pairs admitted,25 compatible pass,
two shorter exclusive defaults fail,Append wrong-access exception,18 disposed/precedence
failures and actual forbidden fixture mutations. First unhandled-Append probe failure
and fixture retained; corrected probe records failure then completes independent checks.
Reports repair-storage-contracts-native-corrected-before*,repair-component-c-disposal-corrected-before*.
No Sharp/runtime production changes. Native runtime test additions remain uncommitted
pending coherent repair. Prepared ignored Color exact-comparison drivers admit exactly
48 cumulative demo changes; containment comparisons rechecked unchanged46/differences[].
Derived disposal companion: genuine DrawableDisposalOracle4/4; two new native tests
fail because explicit Dispose must always invoke UnloadContent even before Initialize,
before base event; false path silent, throwing unload does not suppress later attempt.
Standalone combined runtime suite57tests39PASS/18intendedFAIL/zero skips907ms,
repair-drawable-lifecycle-native-before*. Native integrated binary still only earlier
50-test version. Include derived disposal in141 root-cause batch; do not assume
initialized_ is a valid one-shot guard for managed hooks. Separate device-service
initialization/event hookup is not validated by this no-Initialize oracle.

## Superseded chronological checkpoint notes

Phase C. Latest closed runtime785bf2d2a:118/123/140 FIXED_VALIDATED. All155 IDs
retained,49 final/106 pending:18 runtime findings,23 documentation corrections,
seven already fixed,one demonstrated demo expectation defect. Demo closure commit
6797295; prior committed closurea30f4e1. CNA closure status73dc23836.
No push/merge. Linux x86_64/Radeon780M/Mesa; no all-platform claim.

LAST COMPLETE REGRESSION: magnitude/products/planes, frozen785bf2d2a. Native math
944pass/two unchanged skips; consumer1086pass/four unchanged skips; C17/Werror
VectorSmoke/QuaternionSmoke/GeometrySmoke pass actual rebuilt C ABI. All732 enabled
CTest accepted,zero failures,48skip/three disabled,1114.49s. First CLI metadata
expectation failure remains retained; exact expected repaired row corrected and
entire suite rerun. Both95 math81pass/14expectedfail. Full719SW593/75/48/3 and
GLES632/77/7/3; both exact comparisons accept36 cumulative changes,differences[].
Metadata719/0,tools7+4+5,six exact visuals,representative capture inspected.
Permanent ../cna-demos/docs/repair-evidence/issue-118/REGRESSION_CHECKPOINT.md.
No previous checkpoint jobs remain; external binary freeze released.

ACTIVE GEOMETRY024/126/128/130/135: genuine1338/1338; native first post repair
950pass/two unchanged skips, replacing six exact pre-fix failures. Seven negative
zero frustum plane fields are correct XNA data; only text changes. Constructor
rejects negative radius; C marshal copies mutable fields without constructor.
Ray-box interval/threshold repaired; owner-reserved131 untouched. Genuine additional
six converter/standard-vertex text checks pass; allfour pre-fix vertex probes fail.
Standard vertex companion/source/native/C oracles now locally corrected; three
CNA-only extended vertex formats unchanged. Native whole math950pass/two skips;
registered geometry/converter/vertex341/341 pass69.51s. Exact vertex probe4/4 pass.
The first local edit missed separate-line closing braces; exact probe failed4/4,
preserved repair-vertex-text-after.log; corrected output passes. Broad native build
53 warnings all exactly present in original152-warning baseline,none new; subsequent
four-source correction build warning-free. Consumer run COMPLETE:1393pass/four
unchanged skips(1397 total,223.580s),repair-geometry-native-consumers.log. Runtime
commits e1377c437 (geometry/math/C marshal),a8a60bf03 (stock vertex text companion).
External demo/C API and marker builds COMPLETE without new warnings.
Initial one-job build was deliberately interrupted with SIGINT after native suites
finished to use the released capacity; original log retained,not a compile failure.
1165 initial steps due shared header/marshaller dependencies. Strict actual C ABI
GeometrySmoke/VertexValueSmoke PASS; all13 focused demos PASS both labels. Six math
witnesses retain13/5/12/16/15/10 checks; converter9/stock vertices8; C vertex7
internal checks. Six knownIssue markers removed only afterward. Both95 math sweeps
87PASS/8expectedFAIL; exact42 cumulative intended changes,differences[].
Both full renderer sweeps COMPLETE: SOFTWARE719 is599PASS/69expectedFAIL/48SKIP/
3manual; OPENGLES3719 is638PASS/71expectedFAIL/7SKIP/3manual. Each accepts exact42
cumulative changes with differences[]. Six exact visual references remain unchanged.
First full CTest completed1200.53s with731accepted/one failure (48 skips,three
disabled). Failed698 net.network_session.find_and_join selected a listing with
two gamers/two public slots instead of this test's one gamer/one public slot;
source selects by shared HostGamertag alone. Concurrent smoke uses the same
network despite isolated XDG profiles; cross-suite discovery is the leading
evidence-backed cause, not yet an isolated validation result. Preserve original
repair-geometry-demo-ctest.log; run the full CTest serially AFTER smoke completes.
Do not suppress assertions or accept this failed run as a checkpoint. Full CTest
isolated rerun NOW RUNNING alone on network,repair-geometry-demo-ctest-isolated.log;
no other network suites until it completes. Native build remains independent.
FREEZE EXTERNAL BINARIES at productiona8a60bf03 while full regression runs:
repair-geometry-regression-driver.log (ONE job,metadata/tools/visual/fullSW/fullGLES)
and repair-geometry-demo-ctest.log (ONE job). No external rebuild or demo edits until
both finish. Native next-batch tests may proceed independently with ONE spare job.
Do not edit native sources or relink a live consumer while validation runs.

UNCOMMITTED: six exact demo text corrections and separate demo geometry evidence;
new native GeometryContainmentCompatibilityTests.cpp (NEXT batch): before959total,
951pass/two unchanged skips/six failures. FrustumSeparationOracle.cs captured12
compact genuine grids;27216 overload parity checks pass. Separate issue-132 Color
fixed-point oracle1782/1782 PASS. Expanded native separation fixture12 rows/14328
cases plus three tests reproduce three more failures: before962total,951pass/two
skips/nine failures. Native containment production NOW UNCOMMITTED: private local
Gjk.hpp and BoundingBox/BoundingSphere/BoundingFrustum repairs. No public API or
ray/frustum method/exception change. First post math962:957pass/two skips/three
failures. All original six new failures and full box/frustum-pair grids now pass.
First-run remaining: two historical native oracles pin FNA instead of XNA; sphere grid has
18 differing Contains(sphere) cases (both overloads), while ALL actual GJK
intersection and reverse-containment fields match genuine. Suspect premature
public Dot rounding in inline frustum plane classification; targeted genuine
FrustumSpherePlaneOracle proves1944/1944 wide inline plane classifications,18
rounded intermediary mismatches. Corrected inline Contains(sphere) and two exact
native oracles now pass FULL native math960/two unchanged skips(962 total).
One old nodiscard warning in the touched Sphere test is now explicitly discarded,
without changing its exception assertion. ASan/UBSan actual three production TUs
plus both new regression test files PASS all10 tests with leak detection,
repair-containment-sanitizer{,-build}.log. Registered159/159 PASS50.50s. Formatting
only new files/changed hunks, then native rebuild warning-free and full960pass/two
skips again. No external containment changes yet. Additional genuine sphere
overflow/mutable-radius probe: initial direct Distance caller repeated118's known
spill-dependent range difference and failed; original report preserved. Corrected
model uses actual reflected Distance/DistanceSquared method-entry outputs:
12800 checks PASS,18 differences if radius sums/differences narrow first.
New native test reproduces one failure (963total,960pass/two skips/one failure),
repair-sphere-radius-native-before*. Local Contains(sphere) now keeps double
radius comparisons; Gjk max uses the reference MathHelper NaN contract.
Full native math961PASS/two unchanged skips(963 total); latest ASan/UBSan/leak
run11/11PASS592ms, both builds warning-free,repair-sphere-radius-native-after*
and repair-containment-sanitizer-final*. Final registered160/160PASS31.08s,
repair-containment-native-final-registered.log. Owner-reserved ray method body verified
textually identical to HEAD; point containment naturally changes its boundary
branch, so the old C boundary-refusal oracle/docs need a genuine-backed update
in the NEXT external containment batch,not the frozen geometry checkpoint.
Native consumer still pending while external renderer regression runs.
Final native rebuild after comment-only public-header and Gjk formatting edits
is running with two compiler jobs:repair-containment-final-native-build.log.
Together with the single external smoke job this exhausts the three-job limit.
Genuine FrustumBoundaryRayOracle96/96PASS; corrected native C boundary oracle and
public comment,plus new sphere/separation stages. Strict C17/Werror against frozen
pre-containment actual C ABI fails exactly three stages,repair-containment-c-before.log.
No next-batch C ABI rebuild/external demo edit yet. ColorFixedPointProbe.cpp is only
prepared evidence; no Color production/native-test change or compilation yet.
Focused geometry demos and permanent genuine oracles are committed separately
as cna-demos c3f3bbd; final five-ID closure still pending isolated full CTest.
Read-only lifecycle141–144 source audit and a fresh oracle source are prepared
under docs/repair-evidence/lifecycle,not executed or repaired. Dispose ownership
and Game's forward teardown loop need care before adding genuine self-removal.
LATEST: final native rebuild completed161 steps; one unchanged baseline
BoundingBoxTests nodiscard warning. Rebuilt math961PASS/two unchanged skips.
Consumer deliberately still pending: additional point-radius parity hypothesis
FAILED on genuine XNA (1012 of2048 value/output differences), original source/log
retained as SpherePointRadiusParityHypothesis.cs and repair-sphere-point-radius-precision-xna.log.
Diagnostic forces a real float serialization boundary: value form keeps wide
distance and radius products; output form stores squared distance as Single before
wide radius comparison. SphereSquaredArithmeticOracle.cs confirms19200/19200
over varied3D coordinates and scales, also proving Vector3 DistanceSquared and
LengthSquared need wide accumulation before their public Single return/output.
Two new native tests are capturing pre-fix failures,repair-sphere-squared-native-before*.
Do not close129 or start a frozen consumer before this measured cause is repaired.
Independent next-batch lifecycle genuine29/29PASS; Color native witness fails162
of1782 checks with warning-free build, while genuine1782/1782passes. No Color or
lifecycle production change. Full external isolated CTest remains running alone
on network; frozen binaries SHA256 all verified unchanged.
Squared arithmetic pre-test965total:961PASS/two skips/two failures. Local shared
private Vector3Arithmetic.hpp now backs LengthSquared/DistanceSquared and existing
Length/Distance; public outputs remain Single. Sphere point value keeps wide
distance/radius products; output explicitly stores squared distance. Full post
math963PASS/two unchanged skips. Native consumers NOW RUNNING,repair-containment-native-consumers.log;
FREEZE native CnaTests/libcna.so until complete (no relink). Updated four-TU
sanitizers ONE job,repair-containment-squared-sanitizer{,-build}.log. Together with
external isolated CTest these occupy all three campaign jobs. Consumer subset
has no SystemLink/discovery scenarios (avatar service fixtures use their own local
endpoints); no simultaneous full GPU renderer sweep is running.
New C squared-arithmetic stage still awaits pre/post actual C ABI verification.
Latest four-TU sanitizer13/13PASS696ms,no diagnostics/warnings; registered geometry
and Vector3 tests247/247PASS63.64s. Strict pre-fix C ABI now fails the original
three stages plus the new squared-vector stage,repair-containment-squared-c-before.log.
Native consumer COMPLETE:1408PASS/four unchanged skips(1412total,272.475s),
repair-containment-native-consumers.log. Native binary freeze released. New private
helpers/test files only are being formatted before commit; warning-free final
rebuild/math confirmation pending in repair-containment-commit-{build,math}.log.
Live genuine Game disposal companion8/8PASS (self-removal, snapshot ordering,
reentrant/throwing event controls); no lifecycle production/native test change.
Containment product/native/C regressions and decisions committed5ab1c7729.
Final formatting-only build warning-free; math963PASS/two unchanged skips again.
Matrix127/129/133 now FIXED_PENDING_REGRESSION with this commit,not final.
Native consumer/sanitizer/registered jobs all finished. Three separate Color132
native tests are added (no Color production change): pre968total,963PASS/two
unchanged skips/three failures,repair-color-native-before*. They remain uncommitted
and deliberately failing until the Color batch; do not confuse them with a
containment regression. External geometry CTest still frozen at a8a60bf03; its
previously failing698 find_and_join now PASSES in the isolated run. Await final
suite totals before five-ID closure, then rebuild external containment from5ab1c7729.
Logs repair-containment-native-after* preserve the original three failures.
Product/test repairs committed e1377c437/a8a60bf03. Demo118/123/140
closure/status committed separately as6797295/73dc23836. Six genuine-disproved
exact demo text expectations corrected,all assertion counts retained. Geometry
markers now removed after passes,final issue closure still PENDING complete sweeps.
EXACT NEXT: audit external full results and close024/126/128/130/135 only if clean;
prepare native127/129/133 convex-separation oracle regression and root repair.
Max three compiler/test jobs,currently two external jobs plus one native spare.
Continue127/129/133 containment next;131 reserved in handoff.md section6.
Next-batch genuine containment oracle509/509 confirms point-surface exclusion is
different from including whole-volume boundary corners; Contains(sphere) plane
classification is conservative while Intersects(sphere) truly separates corner
misses. Creation demo must test geometric enclosure,not point Contains,when129
lands. No external containment demo change while prior geometry is frozen.
See geometry/README.md; next fix measured precision cause and exact native oracles,
run full native math/registered/consumers and preserve external geometry checkpoint.
Ordinary actionable defects remain; campaign NOT complete.

## Historical checkpoint (2026-10-19; superseded above)

Phase C. CNA committed runtime `785bf2d2a`; latest demo closure `a30f4e1`.
All155 historical IDs retained:46 final/109 pending (15 runtime findings,23
documentation corrections,seven already fixed,one proven demo expectation error).
No push/merge. Linux x86_64,Radeon780M/Mesa; private displays/dummy audio.
Native graphics OPENGLES3; demos also SOFTWARE,OPENGL33,SDL_GPU,HEADLESS,STUB.
No all-platform/hardware/service claim. Max three compiler/test jobs.

LATEST ZERO-DELTA COMPLETE REGRESSION: integer/transpose, frozen runtime0b03b7b21.
124/125 fixed5a44d2c50;139 fixed0b03b7b21; closure demoa30f4e1.
Genuine integer4510/4510,matrix residual192/192. Native math925 passes/two unchanged
fixture skips; integer registered57/57,Matrix/Plane141/141; consumer1082 passes/four
unchanged skips. Strict C17/Werror MathValuesSmoke/GeometrySmoke pass actual rebuilt
C API DSO. Original focused Point10/Rectangle13/Union10/Transpose4 checks and
strengthened C demos pass both labels; four markers removed only after passes.
All732 enabled CTests accepted,zero failures,48 skips/three disabled (486.96s).
Metadata719/0; tools7+4+5; six visual references exact0 pixels/channels, representative
GLES capture inspected. Both95 math sweeps72pass/23expectedfail. Both full719:
SOFTWARE584pass/84expectedfail/48skip/3manual; OPENGLES3 623/86/7/3.
Both exact comparisons accept27 cumulative intended changes only,differences[].
Reports repair-integer-transpose-*. No checkpoint process remains.
Permanent shared evidence: ../cna-demos/docs/repair-evidence/integer-values/REGRESSION_CHECKPOINT.md.

PRECEDING VALUES CLOSURE120/121/122/136: demo62a496e, CNA status57250017a.
120bf209dcbe canonicalizes zero hashes, preserves NaN payloads, uses CurveKey wrapped
32-bit sum; genuine212. 122/136eef23e6f8 ordered vector bounds/scalar first NaN,
genuine6401.1219786d4c93 wide range endpoints and exact access exceptions,genuine200.
Native registered8/316/247 pass; actual vector production-TU UBSan old overflow now
passes for allthree TUs. Full732CTest passes542.11s;six exact visuals;95math68/27.
Full719SW580/88/48/3 exact23changes; GLES619/90/7/3 has preserved030 assertiondelta.
030's incorrect receiver-side outage oracle vacuously passed once; it configures
receiver though modern CNA/genuine simulate sender. Delay is Gaussian,not minimum.
Historical reliable-loss bug fixed pre-campaign01534c2e7. Simulator7/7,ENet4/4;
unchanged focused follow-up again gets4/24 unreliable packets and both old failures.
No comparator whitelist/retry-until-green. No relevant networking production edits.
Permanent investigation ../cna-demos/docs/repair-evidence/bounds/REGRESSION_CHECKPOINT.md.
030 remains pending its own evidence-backed demo contract repair. Later clean
integer/transpose comparison does not erase that earlier failed report.

NEXT NATIVE BATCH140 committed e4ddea6e0: public Plane.Normalize strict near-unit
shortcut,wide squared sum,Single sqrt,wide reciprocal. Both Matrix Shadow/Reflection
reuse correct layer; duplicate removed. Genuine3405/3405; initial all-double model
fails324 and retained; seven-model experiment distinguishes rounding. Native before
925pass/2skip/4fail;after929/2skip; registered294/294. New C normalization/shadow
stages fail against pre-fix ABI. No external marker removal/closure yet.

123 committed0e7be08e0: wide Dot(Vector2/3/4),Cross(Vector3), XNA Y subtraction order.
Genuine8748; native before929/2skip/4fail,after933/2skip;registered566/566.
New C product stage fails against pre-fix ABI. First consumer run1090 tests:
1085pass/4skip/1avatar golden failure, preserved repair-140-123-native-consumers.log.
Field audit952 raw differences,max2.384185791015625e-7;two quantization crossings.
Companion32e470ed4 preserves explicit binary32 dot arithmetic inside versioned
catalog recipe, without modifying public corrected math or golden fingerprints.
All550524 captured fields exactly match baseline; allfour golden descriptions pass.
Permanent evidence ../cna-demos/docs/repair-evidence/issue-123/AVATAR_REGRESSION.md.
Intermediate corrected consumer run invalidated by our relink of its live self-
spawning CnaTests executable (binary replaced23:25:01 during23:22:14–23:25:24 run).
Sharp Environment uses /proc/self/exe; deleted-inode path plausibly prevents child
launch. Do not call that run successful or suppress its failure. Original log retained.
Fresh frozen self-spawn test passes4815ms; frozen full consumer1090 tests:
1086pass/four identical baseline skips182.763s. Logs repair-avatar-process-frozen-native
and repair-140-123-native-consumers-frozen. No consumer job remains.
NEVER relink a live consumer executable OR its dynamic runtime; freeze both.

CURRENT118: genuine reflection method-entry probes Distance1176/1176 and
Length/Normalize1184/1184 pass. Earlier conflicting caller probes remain preserved;
JIT spills/inlining explanation remains inference,not disassembly proof.
Permanent probes ../cna-demos/docs/repair-evidence/issue-118/.
Native pre-fix946tests:933pass/two unchanged skips/11 new failures. Repairbd46b6ffe
now passes944/two unchanged skips; registered955 accepted,zero failures/two existing
skips(67.83s). Squared API range unchanged; Normalize rounds sqrt not reciprocal.
Unchanged four-avatar golden test initially fails allfour; representative field
audit32556 raw changes/56 quantization crossings among550524 records. Companion
785bf2d2a preserves private catalog lengths/distances/normalization/imported normals.
Allfour golden descriptions pass and all550524 fields match baseline exactly.
Frozen native consumer COMPLETE:1086 passes/four unchanged skips(1090 total,185.284s),
repair-118-native-consumers.log. External build and marker rebuild complete without
new warnings at785bf2d2a. Allnine original focused demos pass both labels with
5/6/8/5/11/18/14/8/14 assertions retained; adjacent C demos pass. Actual rebuilt
C17/Werror VectorSmoke/QuaternionSmoke/GeometrySmoke ALL PASS. Nine marker/restriction
pairs removed only after focused passes. Both95 math sweeps81 pass/14 expectedfail,
exactly36 cumulative intended changes,differences[].
C17/Werror magnitude stages in VectorSmoke/QuaternionSmoke fail on old ABI as
expected; VectorSmoke also retains pending123 product failure. Logs repair-118-c-before-*.
First full demoCTest finished408.90s:731 accepted/one failure, CLI list_ids still
expected118's removed marker. Original report preserved. cmake/CnaDemosTests.cmake
now checks the exact repaired row with '-' knownIssue; no runtime assertion changed.
Configure-only refreshed CTest definitions without relinking frozen binaries.
ACTIVE: corrected full demoCTest ONE job,repair-magnitude-products-demo-ctest-corrected.log;
regression driver ONE job,repair-magnitude-products-regression-driver.log. Metadata719/0,
tools7+4+5 and six exact visuals PASS; representative GLES texture inspected.
Full SOFTWARE/OPENGLES3 still in progress. Third job is native geometry build below.
FREEZE demo binaries785bf2d2a until all these finish. Native binaries have no running
consumer now. Exact next: check full results, investigate every delta, close118/123/140
only when validated. Next audit: geometry127/128/129/130/133 and text024/126/135.
Genuine frustum point threshold1e-5 matters. The earlier deduction that decompiled
subtraction-from-zero implies positive-zero plane bits was DISPROVED; see below.
No118/123/140 closure yet.
C API embeds static CNA: LD_LIBRARY_PATH replacement alone
does NOT test repaired C ABI. Native CnaTests does dynamically import public math.

CURRENT GEOMETRY VALUES024/126/128/130/135: genuine final oracle1338/1338.
Initial positive-zero plane-bit hypothesis fails7; diagnostic and a subsequent
C#-literal attempt also fail7 and are retained. Bit-constructed expected zeros
prove genuineXNA has the SAME seven negative-zero plane fields as CNA, but prints0.
Do NOT canonicalize stored/derived planes. Shared private MathDisplay helper changes
only public text operands for seven float-valued types; Box/Ray literal braces fixed.
128 empty-point exception already fixed pre-campaigncd211420e3; negative-radius
constructor rejection pending here. C marshal must copy public fields instead of
calling that constructor from noexcept; mutated negative-radius values remain valid
fieldwise representations. Ray-box uses nonnegative interval and genuine strict1e-6
parallel threshold; no owner-reserved ray/frustum code touched.
Native exact pre:952 tests944pass/two unchanged skips/six failures. Initial nodiscard
warning corrected explicitly; exact pre-build warning-free. C17 pre:three text
stages/radius constructor/stationary-ray fail, independent mutated-radius and old
plane/shadow stages pass. All logs repair-geometry-values-*.
UNCOMMITTED production/tests for this geometry batch; post-build ONE job ACTIVE,
repair-geometry-values-native-after-build.log then whole native math. No geometry
external marker removed. Do not rebuild demos during the prior frozen checkpoint.
Permanent new probes ../cna-demos/docs/repair-evidence/geometry/; README tracks failed
hypotheses. Next after native: inspect failures, registered/consumer tests, strict
actual ABI and focused demos after frozen checkpoint completes, then full regression.

UNCOMMITTED: CNA geometry production/C marshal/tests plus this status/matrix; demo
nine marker removals, CLI exact metadata expectation and permanent118/123/geometry
evidence. No124/125/139 closure work remains. CNA latest documentation commit21c1df311
records required XNA reference decisions in plans/plan_bindings_upstream.md; frozen
external runtime remains785bf2d2a.
Owner-reserved BoundingFrustum::Intersects(Ray) in handoff.md§6 must not be implemented
without resolving that permission boundary; keep131 explicit,continue other work.
Ordinary actionable work remains extensive; campaign NOT complete.
Older notes below are historical
snapshots, superseded by this checkpoint; original baseline evidence remains intact.

Latest completed repair: Quaternion 119 (`df9234202`), demo `105fc63`. Genuine XNA
41/41; native math 893 passes/two unchanged fixture skips; aggregate and registered
Quaternion tests 109/109; original demos 11/24 checks pass under both labels; native
C17/Werror smoke and extended C demo 17 checks pass. Both 95-demo math sweeps have
54 passes/41 unrelated expected failures, exactly nine cumulative expected status
changes and zero unrelated assertion/check delta. All 732 enabled demo CTests pass
(408.88s), 48 skips/three disabled unchanged; six visual references match exactly.
The 119 inventory/matrix is closed. Logs repair-119-*; permanent oracle in demo
docs/repair-evidence/issue-119/. Current next task is Matrix decomposition 115 below.

Previous completed repair: perspective 117 (`2a245b91f`) uses public Pi and exact
ArgumentOutOfRangeException/ParamName for all three factories, preserving output
atomicity, validation order and IEEE controls. Genuine XNA 96/96; native math 889
passes/two unchanged missing ShipGame XML fixture skips; Matrix CTests 98/98 (21.43s).
Native C17/Werror MatrixSmoke passes; both focused labels pass 12 original controls;
external C factory demo passes 14 checks. Both full 95-demo math sweeps have 52 passes
and 43 unrelated expected failures: seven cumulative expected status changes only
(five Curve fixes, oracle 116, perspective 117), no unrelated failed assertion/check
count changes. Full demo CTest: all 732 enabled pass in 557.06s, 48 skips and three
disabled unchanged. Six visual references are exact (zero differing pixels/channels);
representative GLES texture and SOFTWARE font captures were inspected. Logs:
`../cna-demos/build/linux-full/reports/repair-117-*`. Permanent genuine oracle:
`../cna-demos/docs/repair-evidence/issue-117/`. Marker removed only after passes;
original assertions preserved. No pending test processes from this batch.

Previous completed Curve repair `d753d0997`, demo closure `c6f839b`: IDs
002/036/097/137/138 fixed. Native math 886 passes/two skips; aggregate Curve/readers
92/92; registered Curve CTests 74/74; genuine XNA 28/28; C17/Werror smoke and 17-check
C demo pass. Targeted production collection-TU ASan/UBSan alias probe passes with
leak detection, replacing a proven invalid-vptr failure. Full CTest 732 passes;
both math sweeps 51 passes/44 expected failures. Earlier notes below are historical.
Last complete 719-demo SOFTWARE/GLES smoke remains the original baseline; the latest
batch adds complete math sweeps, complete enabled SOFTWARE CTest and six exact visuals.
Only SOFTWARE/OPENGLES3 have this batch's runtime labels; no all-platform claim.

Current 119 runtime/tests are committed as `df9234202`: four QuaternionRangeTests and native C17 smoke
regression reproduce three numerical failures while the IEEE control passes. Whole
math before repair: 890 passes/two unchanged fixture skips/three failures (895 total).
Native C smoke fails only its new extreme-range stage. Minimal Inverse/Divide patch
now keeps intermediates double until final component stores. Both native/demo builds
pass without warnings. Native math: 893 passes/two unchanged skips; aggregate and
registered Quaternion tests 109/109 (registered 25.99s). Both original demos pass
11/24 checks under both labels and the exact C17/Werror smoke passes. Two markers
removed only after those passes. External C demo gains two extreme-range controls.
Both math sweeps pass with exactly nine cumulative expected changes and no other
status/check/failed-assertion delta (54 passes/41 unrelated expected failures).
Extended C demo passes 17 checks under both labels; all six exact visual references
pass with zero differing pixels/channels. Full 732-test checkpoint now passes and
119 is closed. Matrix 115 runtime/tests are committed as `705ea2324`, with full demo
CTest at two jobs and sequential SOFTWARE/OPENGLES3 complete smoke at one job.
Exact next task: compare all 719 outcomes/assertions using
`build-probe/repair-full-compare.mjs RENDERER 115`, confirm CTest, then close 115.
Do not rebuild demos or start another compiler/test while all three slots are busy.
Genuine dedicated
XNA oracle `build-probe/repair-119-oracle.cs` passes 41/41, including value/out/input
aliases, five extreme magnitudes, self division and a mixed numerator/divisor where
prematurely rounding an inverse component loses a finite final contribution. Logs
`repair-119-xna{,-build,.stderr}.log`. Preserve zero,
NaN, infinity, signed-zero and ordinary Hamilton-order controls; do not change
LengthSquared or conflate 118. Build at most three campaign jobs, private displays only.

118 oracle audit: `repair-range-xna.log` has 12 checks/two failed universal finite
Distance assertions. All Length/Normalize/Quaternion Inverse/Divide extreme outputs
remain as expected, but some grouped Distance calls overflow/underflow. The controlled
`repair-distance-oracle.cs` (NoInlining wrappers plus grouped calls in a smaller
caller) produces finite results for all six Distance forms. Same genuine XNA assembly
and inputs, different caller context. Likely x87 JIT spill/inlining effects are not yet
proven by disassembly. Both sources and bit outputs in ignored demo build-probe/reports
must be retained; do NOT blindly widen Distance or weaken its oracle. C# specification
8.3.7 permits wider intermediates; source float locals alone do not settle runtime
behavior. No 118 production change or final disposition; this does not block 119.

Perspective demo closure is committed as `dac8228`, CNA status `5524a76df`.
119 runtime/native tests are committed as df9234202, demo evidence/closure as 105fc63.
Current uncommitted work: 115 demo marker/C factory controls/oracle; 120 native
MathValueHashTests.cpp, C MathValueHashSmoke.c and its CMake registration, permanent
120 oracle/readme; CNA matrix/handoff. No 120 production edits or marker changes.

Current 115 oracle: permanent `docs/repair-evidence/issue-115/DecomposeOracle.cs`
passes 90 checks against genuine XNA (logs repair-115-xna-extended*), extending the
original 48-check probe with explicit boundary/classification/alias assertions and
printing 28 case classifications. Largest-axis reflection with X/Y/Z tie precedence
is confirmed; all seven zero-axis combinations return true plus NaN quaternion.
Tiny 1e-5 axis returns false/Identity; exact 1e-4 succeeds. Shear 0.1 is accepted,
0.2 is refused; nonaffine fourth column is ignored; NaN/Inf produce true/NaN. Current
CNA's former native reflection test incorrectly called the inherited FNA heuristic XNA's.
Read genuine decompiled Matrix.Decompose and local FNA. Five new native cases plus
the corrected existing reflection oracle yield five pre-fix failures, 893 passes,
two unchanged skips (900 total). Native C MatrixSmoke's corrected true/NaN zero-matrix
expectation fails against the unchanged demo library. Minimal local Decompose repair
is now applied: rank/normalize basis rows, preserve XNA degenerate-row path, flip
largest axis for a reflection, apply normalized-determinant refusal, retain output
order. Native/demo builds pass without warnings; all 898 math tests pass with two
unchanged fixture skips. Native Matrix/glTF/avatar consumers: 1022 passes/four skips
(153.68s). All four skips match original baseline: ChronographWatch, two optional
large glTF fixtures, and a GPU-failure path requiring a non-3D renderer.
An additional attempt to exercise that upload-failure test with STUB was a setup
failure: this native build contains only OPENGLES3, unlike the multi-renderer demo
build. `repair-115-native-avatar-stub.log` is retained, NOT counted as a runtime pass
or a decomposition regression. No backend/configuration was changed for that probe.
Registered Matrix tests 106/106 (16.68s). Native C17 MatrixSmoke passes. Original demo passes
all nine controls under both labels; marker removed only afterward. Extended C
factory demo passes 17 checks. Both math sweeps: 55 passes/40 unrelated expected
failures, exactly ten cumulative expected deltas and no unexplained changes. Six
visual references remain exact. Runtime/native tests committed as 705ea2324; 115
closure awaits full CTest and both full smoke comparisons (repair-115-full-*).

Next 120: genuine signed-zero hash oracle passes 206 checks, including every Matrix
field and all zero-sign combinations, exact unit/ordinary controls and NaN payloads.
.NET 4 preserves nonzero NaN hash bits, unlike modern Sharp Runtime Single hash;
canonicalize only zero in the six existing helpers, not NaNs or unrelated hashing.
Permanent oracle docs/repair-evidence/issue-120/. Seven new native tests and one
registered C17 smoke are written. The pre-fix native build passes; whole native math
now has 899 passes/two unchanged skips/six new hash failures (907 total). Exact
C17/Werror smoke fails only its signed-zero group; nonzero controls pass. Production
remains unchanged: delay helper edits until the corrected 115 demo rebuild finishes.

Further queued bounds batch 122/136: source/demos reviewed, no production edits.
Ignored `build-probe/repair-bounds-oracle.cs` passes 6401 genuine XNA bit-level
Clamp/Min/Max value/out/alias checks and scalar NaN precedence checks (repair-bounds-xna*).
It CONFIRMS the Vector4 purported ordinary control's oracle error: Z bounds min=8,
max=6 must produce 8, not its current 6. The mixed-case genuine result is (1,5,8,-6).
Correct that expectation with permanent evidence in the bounds batch, not beforehand.
Keep scalar MathHelper's first-NaN propagation distinct from vector Min/Max's
second-operand fallthrough. No 122/136 native tests or source changes yet.

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
