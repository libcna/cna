# Museum-discovered CNA repairs

## Scope and evidence

Owner-requested intake on 2026-10-22. Permanent external inventory:
`../museum-simulator/docs/CNA_FINDINGS.md`, section **Open**. The museum owns that
inventory and moves entries to **Fixed in CNA** only after rebuilding and rerunning
its reproducer. Do not edit the museum or its CNA worktree as part of these repairs.
Do not invent CNA-DEMOS issue IDs for museum-only findings.

Intake CNA: `28046c8bbc48bea787007881e21e9b3ab71bcfd6`, branch `next`.
Museum reference CNA: `7b028ebe8`, an ancestor 36 commits behind. No intervening
committed changes touch `modules/net` or `Sdl3Tray.cpp`.
The shared worktree contains unrelated content/audio ownership work and the
uncommitted CNA-DEMOS-ISSUE-113 occlusion-query candidate. Preserve all of it.
The latter still needs its uint32-result/XNA compatibility review before closure;
its last external-v4 run preserved 132 outcomes (including eight known ISSUE-082
failures) and passed six exact visual references. It is not a completed repair.

## Current task

Publication authority: on 2026-10-22 the owner explicitly authorized ongoing pushes
when appropriate. Publish validated, coherent commits with a normal fast-forward
push; this does not authorize force pushes, merges, or committing another agent's
uncommitted work. Recheck the remote and validation evidence at each checkpoint.
Read-only `git ls-remote origin refs/heads/next` on that date confirmed
aaef018ef92fa57a9c9e2f59c79b90bcf565c233 already published; no push was performed
by this validation step.

### Current task: WaveBank028 retained-file repair (2026-10-23)

025 publication VERIFIED: CNA origin/next79c56330faa5de25c813188ceee1b3482c530bf0,
demos origin/nexte842f7a4414d78e6f78f58ce66c3378ecb21dba3 (fresh ls-remote checks).
028 baseline on this tree: CnaAudioTests775 registered =771PASS/4SKIP,8.638s. The four
skips are the existing empty/misaligned/zero-buffer/invalid-channels SoundEffect cases;
do not report775PASS. Current constructor witness again has9FAIL/5PASS; lazy path8 checks
has2FAIL/6PASS, retained file6 checks has2FAIL/4PASS. Reports repair-wavebank-probes-before-v2;
the first standalone link missed TitleLocation (not a product result), corrected by including
the already-built runtime TitleLocation object with the canonical audio static dependency closure.
Four permanent StreamingWaveBankFileTest regressions fail before code changes (0.003s).
Candidate retains the std::ifstream used to parse metadata until disposal, protected by the
existing xactImplMutex_ for reads/disposal, and preserves lazy audio/non-resident wave data.
An internal parser overload accepts the same open stream; existing filename overload remains.
This is a scoped lifetime sub-fix, NOT closure of028: constructor cwd/header/exception errors
still need repair. Offset/packetSize semantics and Windows share-mode runtime coverage are not
claimed. Do not replace 64-bit file positions with Sharp Stream's current int32 Position/Length
without handling that limitation; do not add a second independently-opened data handle.
Validation COMPLETE for the lifetime sub-fix: full audio779=775PASS/4sameSKIP8.669s,
all775 prior outcomes preserved plus4PASS. Original lazy8/retained6 witnesses now allPASS;
constructor14 retains the exact9FAIL/5PASS. ASan/UBSan/leaks42PASS0.063s, six instrumented
translation units (parser, WaveBank, AudioEngine, SoundEffect, SoundEffectInstance, native fixture),
other dependencies uninstrumented; no findings or changed frozen inputs. Five native C audio
controlsPASS; full consumer buildPASS. No compiler warnings in these builds. SOFTWARE32 and
OPENGLES3 32 each30PASS/1same114FAIL/1manual microphoneNOTRUN; exact status/check-count/marker/
message/assertion multiplicity parity. Fifteen registered XACT/catalog CTestsPASS13.65s.
Catalog719 unchanged;275 final consumer artifact hashes verified after all gates; freeze released.
Evidence demos docs/repair-evidence/content/WAVEBANK_RETAINED_FILE.md and repair-wavebank-* logs.
Source repair committed:3c67874ba54492d0d10258f0742824c1bb5bcec9. Only028 matrix/handoff and
demos028 inventory/evidence remain for this checkpoint; preserve all foreign edits. No028 closure.
Next: publish this root-cause repair and evidence, then repair constructor semantics with
the unchanged genuine14-case preflight oracle. A managed FileStream preflight can read the first
five bytes to check length>4/magic without querying int32 Length or replacing the retained 64-bit
native data stream. Preserve FileNotFound/ArgumentNull ordering, absolute diagnostic filename,
case-insensitive CNA file resolution, and distinguish invalid magic from failed native metadata.

085 publication VERIFIED: CNA origin/next b6d5ca097f2cd2982cc797690227659b73c829cf,
demos origin/next90d98d66e2daecb412ffd77a9d74128559d8ecd9. Source420a4572a is included.
025 native witnesses added in modules/net/tests/CNA/Internal/Net/OfflineLeaderboardCommitTests.cpp;
first build passed. First run failed setup (maxGamers1 is invalid); corrected fixture to4.
Before repair:3FAIL/3PASS0.006s, with original missing Result, retained scalar/stream values lost,
and unchecked final-write failure. Six cases cover rating-before-columns, references/streams
retained before StartGame, both directions of unchanged cached writer alias, read-without-write,
and failed final persistence/recovery. Preserve immediate offline Rating fixture writes and avoid
an unconditional cached-row flush that overwrites another writer's newer data. Investigate a
canonical local-column snapshot, compared at LocalWithLeaderboards EndGame before GameEnded.
No callbacks on each stream byte, arbitrary sleeps, service impersonation or changed online rules.
Candidate implemented: canonical snapshots from local serializer; Rating saves then swaps its
baseline; EndGame flushes only changed offline rows before GameEnded. No online rule changes.
Native validation passed: finalNet565/56548.580s (558 prior outcomes plus7PASS), Gamer737=
736PASS/1sameSKIP75.187s exact085 parity. Seventh control added after initial6-case native run
protects LocalNetworkGamer's own writer; not claimed as separately red. Seven C controlsPASS0.24s.
ASan/UBSan/leaks48PASS0.029s, eleven relevant translation units instrumented, other libraries
uninstrumented, no findings or changed hashes. Initial47-pass sanitizer run retained; v2 adds the
seventh control. Native48 existing nodiscard warnings; C build no warnings. No suppression.
Demo now clears its fixture columns and commits Rating before writing all expected values, so
previous runs cannot hide025. All21 assertions retained, including085 stream/shape checks.
Complete consumer buildPASS without compiler warnings. Final SOFTWARE59/OPENGLES3 59 each
58PASS/1manualNOTRUN, exact085 gamer/018 net comparison preserves every outcome/count/marker/
message/logged assertion. All21 paging assertions pass with Rating now before column writes.
Eleven focused/catalog CTestsPASS26.40s. All275 frozen artifacts verify after all gates; freeze
released. Catalog719 exactly unchanged versus085; generated3197 routes current.
025 source repair8a5e5d8aa50c56599ddc8649b738cb2ca4caab15 committed. Permanent025 status is
RESOLVED/FIXED_VALIDATED:156 historical IDs,129 final/27 without. No new full719/native aggregate,
visual, other-platform or live-service claim; last full checkpoint stays075 with six residuals.
Next: commit closure/evidence and normal fast-forward publish/verify. Then028: reuse the existing
genuine14-case WaveBank constructor oracle and current retained-file/lazy-path witnesses from
demos docs/repair-evidence/content/MEDIA_PATH_TRIAGE.md. Current WaveBank source still resolves
the eager bank through TitleContainer and reopens a streaming filename for each lazy read.
XNA uses cwd/Path.GetFullPath for both constructors and retains an opened FileStream. Do not
implement the disproved historical title-root expectation. Verify current reproductions before
repairing; preserve foreign SoundEffect/Song/content/113 changes. No028 source changes yet.
Owned uncommitted025 work: matrix block and this handoff; demos paging setup,025 inventory block
and LEADERBOARD_COMMIT.md. All implementation/tests/docs are in the source commit above.

Latest publication verified: CNA108fc0e46be3024e815f9f83c3636ac100e3ffe7,
demos1a978376acd795bed3efb3ae2e3cf2b3c75a9a9d.017 closure is published; Sharp unchanged.
085 root causes confirmed in source: offline writers never enable stream creation and the local
JSON codec skips streams. Four focused regression tests are added, before production edits.
The first build caught two test-fixture mistakes involving collection element proxies; corrected
to explicit entry copies/const collection views. The second build passed. Red run:4/4FAIL,0.001s.
085 candidate implements owned stream restoration and bounded local persistence. Full native
737 registered=736PASS/1sameSKIP74.437s; all732 previous outcomes retained plus5newPASS.
ASan/UBSan/leaks:41 writer/reader testsPASS0.018s, no changed frozen inputs. Eight relevant
translation units instrumented (including store, dictionary, writer, reader, entry, MemoryStream,
native fixture and user-data isolation); other static dependencies are not instrumented. First
sanitizer link lacked canonical WebGPU -L path; added it, then v2 link/tests passed. No suppression.
Six native C controlsPASS0.20s; complete consumer buildPASS without warnings. Original demo21
checks PASS on SOFTWARE2.839s/OPENGLES3 1.682s after three exact nine-column shape corrections.
The first focused GLES run correctly caught the repeat-run writer Count assumption: its stored
Replay was already present before the eight-scalar check. Preserve that failure evidence; moving
the check after stream access and requiring9 on both first/repeated runs fixed the demo oracle.
085 metadata removed only after both passes. Final consumer SOFTWARE35/OPENGLES3 35 each
34PASS/1manualNOTRUN; exact017 comparison changes only085 FAILED->PASSED, other34 rows unchanged.
Seven registered focused/catalog CTestsPASS16.65s. Catalog719 valid, only paging metadata changed;
generated3197 routes/57 files current. All275 frozen consumer artifacts verified after validation;
freeze released. Source fix420a4572a721f750978e496c3cc0b3b34e2e26a2 committed.085 is now
RESOLVED/FIXED_VALIDATED:156 historical IDs,128 final dispositions/28 without. No new full719,
native aggregate, visual, other-platform or live-service claim. Last full checkpoint stays075.
Evidence demos LEADERBOARD_STREAMS.md. Closure/evidence are published as recorded above.
Continue025 native red tests for post-rating scalar/retained stream writes
at EndGame. Source confirms offline EndGame has no final column flush. Protect unchanged cached
rows from overwriting another writer and preserve existing immediate Rating fixture persistence.
025 needs a separate commit-boundary repair, including retained stream/reference writes. Do not
pretend a pre-mutation callback fixes it. Preserve all foreign content/audio/113 changes.
085 owned work is fully committed/published; only foreign work and the new025 work recorded
above are uncommitted. No pending approval.

Publication base: CNA2db9a7646bdce9f91c15a53fe23c78db0312745b and demos
0e33c8aa40cce4ad6034c1430c28510019e8e4a4, both origin/next verified. Sharp unchanged2f644cf47.
077/078 source repair `4a756413f779f5f74d5d3e48e92bcb53ecc00b0e` is committed and validated.
Both permanent dispositions are now RESOLVED/FIXED_VALIDATED:156 historical IDs,
126 final dispositions/30 without. Do not call campaign complete.

Genuine XNA Framework4/Wine oracle passes24 checks: read-only ICollection/IList refuses mutation
before argument validation; absent string key uses KeyNotFoundException. Native collection now
matches, and C translates only this missing-key route to INVALID_ARGUMENT. All mutators report
NOT_SUPPORTED through the existing barrier. C scalar output remains unchanged on refusal; handle
outputs are still cleared. No ABI/generator/numeric-index/copy/ownership/factory flag changes.
Native/C docs and the old FNA-based tests explicitly adopt genuine XNA. Both C collection and
C++ award demos needed these reference-backed oracle corrections, with all old controls retained.

Before: native728=709PASS/18newFAIL/1unchangedSKIP93.759s, original C demo29/30 on both renderers. After:
full native727PASS/1unchangedSKIP75.355s, six C gamer/boundary testsPASS0.19s, complete consumer buildsPASS.
Existing nodiscard warnings only (30 before/17 after in unchanged test files); consumer no warnings.
First C run had one incorrect new scalar-output assumption and three unbuilt executables;
corrected test/docs and built missing targets, then all six passed. No production output-policy
change. The bounded debugger was stopped during symbol load without evidence; generated source
identified the scalar-output behavior. Generated3197 routes/57 files current.

C demo33/33PASS on SOFTWARE and OPENGLES3, then077 marker removed. The initial35-scenario run
exposed the C++ award demo's stale FNA collection oracle, which aborted after10 controls. Corrected
only its collection controls and reran the affected demo on both renderers: original14 controls,
same independent017 failure. Latest35 scenarios each renderer=32PASS/2sameFAIL/1sameNOTRUN;
only C collection outcome changes versus075. Other34 statuses/check counts/markers/messages and
logged assertion multiplicities unchanged. Exact comparison is sequential and retains first-run
failures. Only cna-demos relinked for the final oracle correction; other274 artifacts unchanged,
all275 final hashes verified after five registered achievement/catalog CTestsPASS11.48s. Catalog719
valid with only repaired collection metadata changed. No new full719/aggregate/sanitizer/visual/
browser/online-service/platform claim. Full cross-subsystem checkpoint stays075 with six residuals.
Evidence: demos docs/repair-evidence/gamer/ACHIEVEMENTS_PREFLIGHT.md and
AchievementCollectionXnaOracle.cs; ignored reports repair-achievements-*. Artifact freeze released.

Publication VERIFIED: CNA origin/next f3ac0166b4e79f855b28df156e5cb0d4319e74b8,
demos origin/next46b16ee3398e83cddbc981222c62e7e200592319. Normal pushes only.
017 source repair `2ac45d2f06b8f3fe7e8a38d8419919ad9bf0ff16` is committed and validated.
Its permanent disposition is now RESOLVED/FIXED_VALIDATED:156 historical IDs,127 final/29 without. Four focused witnesses
failed before repair (0.002s); candidate defaults synthetic/offline EarnedOnline to false and sets
true only for earned authenticated service records. Public native/C docs state the synthetic
default. Corrected one old native and one C synthetic-fixture assertion; added C offline provenance
control. No ABI, storage serialization, copy or ownership changes. Original award14 assertions
are unchanged and PASS on SOFTWARE/OPENGLES3;017 marker removed only after these passes.
Full native732=731PASS/1unchanged declared rendererSKIP78.538s, all728 prior outcomes retained
and4newPASS. Six C gamer controlsPASS0.20s. Native build17 existing nodiscard warnings in unchanged
expressions, consumer builds no warnings. Generated3197 routes/57 files current.
SOFTWARE35 and OPENGLES3 35 each33PASS/1unchanged085FAIL/1manualNOTRUN. Exact comparison against
077/078 changes only017 FAILED->PASSED and one additional C signed-in provenance assertion;
other33 rows retain all outcomes/assertion multiplicities. Five registered achievement/catalog
CTestsPASS11.38s; award now passes normally, without expected-failure policy. Catalog719 valid,
only017 metadata changed. All275 frozen artifact hashes verified after final tests; freeze released.
No new full719/native aggregate/sanitizer/visual/other-platform/live-service claim. Evidence:
demos docs/repair-evidence/gamer/EARNED_ONLINE.md; ignored reports repair-achievement-origin-*.
Corrections-only publication verified: CNA f61dc24ef5646cc0a759575882f5b633eca275d9 and demos
b190f8f102bf37fa8e4a0f6723873903d4dc477f.017 source and disposition were subsequently published in the
closure commits recorded at the top of this section. Continue offline leaderboard025/085.
No active build/test. Preserve foreign113/content/audio work and the corrected077/078 totals.
Preserve077/078 and compare surrounding gamer results. Evidence demos EARNED_ONLINE.md.
The old Achievement ctor set EarnedOnline=true, including unearned/synthetic/offline values;
that default and the three materialization paths have now been repaired. Offline store contains key/ticks, not an online flag (historical issue
sentence is wrong); service awards are authenticated calls. Genuine Windows XNA getter is a Pro
feature stub, so no runtime online/offline XNA-value claim. Preserve foreign113/content/audio work.
No pending approval. Neither repository is globally clean because foreign
changes remain. No downstream engine/extended/museum edits.

#### Completed ISSUE-018 validation

ISSUE-026 fix5c60a2028f0688fa8b728109156bd6921dc4dbe2 is committed and validated below.
Its permanent matrix/inventory disposition is RESOLVED/FIXED_VALIDATED:156 historical IDs,
123 final dispositions,33 without. Publication VERIFIED: CNA origin/next
49b0884f0c33d101fda1532717a5ba742547b53e; demos origin/next
f43caae62c97b7cfd3032eb2d6864915e3011050. Sharp Runtime unchanged2f644cf47.
Seven native process witnesses are added for018: host/client allocation, discovery allocation,
Find, private replies, EndCreate and EndFind. They exhaust only their own child's descriptors,
retain exact socket headroom, restore resources and prove public Create recovery. The separate
bind role reproduces the original61190 conflict when run alone in the private namespace.
Native witness build passes. Red baseline0PASS/7FAIL at the expected failure points; original
exclusive-bind role also fails the type check while recovery succeeds. Candidate changes only
ENet initialization/host/client/discovery acquisition failures to NetworkNotAvailableException,
and refuses private search socket failure instead of silently using the shared reply port.
Build passes without diagnostics. Bind reproduction PASS; all558 native network tests PASS
in48.369s, original551 statuses unchanged. CApi NetSmoke/GeneratedRoutesSmoke/BoundaryDetail
all PASS0.11s. Original external demo still FAILS on both renderers when61190 is occupied,
using hash-verified preceding026 consumer binaries and their own RUNPATH. This preserves the
external red witness before consumer rebuild. Only its stale comment is updated, not the oracle.
Reports repair-net-errors-*; evidence demos docs/repair-evidence/net/NETWORK_FAILURE_TYPES.md.
Consumer rebuild PASS without diagnostics;275 artifacts frozen. Original negative-path SOFTWARE
now SKIPPED/exit77 with the exact existing no-network diagnostic. First driver stopped on77;
v2 preserves that evidence and explicitly checks expected77/status/message before continuing.
V2 validation PASS: original forced-unavailable case reports exact SKIPPED/exit77 on both
renderers; all24 ordinary C++/C network scenarios PASS on SOFTWARE and all24 PASS on GLES.
Every preceding026 ordinary status/marker/check count/message/assertion multiplicity is unchanged.
Catalog719 identical, zero problems; all275 hashes match. Freeze released; no active scope.
Next commit only018 production/tests and this handoff, close matrix/inventory with its SHA,
then publish/verify. No new full719/aggregate native/visual/sanitizer/platform/service claim.
Read-only next group:017/077/078 achievement semantics. Genuine XNA collections are read-only
(all mutators throw NotSupportedException) and missing keys use Dictionary/KeyNotFoundException;
do not fix077 merely by changing IsReadOnly to false. No next-group source/test change yet.
XNA XML and NetworkSessionErrorHandler distinguish unavailable network from communication
failure; do not translate every arbitrary runtime_error or infer one subtype for all ENet errors.
Existing public Begin/End timing is protected by the new tests. No018 closure yet. No pending
permission; preserve foreign content/graphics/audio work and113.

#### Completed ISSUE-026 validation

ISSUE-075 fix40f286237cdbf7bdc05eb0a2ee9c9ee584eb677f is committed and fully validated.
Matrix075 is RESOLVED/FIXED_VALIDATED; census156 IDs/122 final dispositions/34 without.
Publication VERIFIED: CNA origin/next53806939171df3a7571b5b02e5216f1a33cea3f8 and demos
origin/next6e85316ad8c5efb72547d209fc2518c98b67d6f3. Sharp Runtime unchanged2f644cf47.
The native independent-process026 witness is added in TwoProcessLoopbackTest.cpp and
tools/net/net_two_process_harness.cpp: host-first control, search-first readiness pipe,
single public Find, join and original payload round trip; no port is supplied to the finder.
Native red baseline on unchanged production:2PASS/1FAIL; only search-before-host fails, matching
the historical finding. Both builds pass without diagnostics. Candidate generalizes the existing
private SearchSocket/reply polling across native platforms, keeping Darwin interface broadcasts
local. Wire format, search window and policy unchanged. Low-resource fallback remains for018.
Complete native networking passes551/551 in48.278s; original549 statuses unchanged and exactly
two passing discovery additions. Reports repair-discovery-native.log/xml/native-exact.log.
Full consumer rebuild passes without diagnostics; all24 C++/C network demos PASS on SOFTWARE
and all24 on OPENGLES3, preserving every075 status, marker, check count, message and logged
assertion multiplicity. Native CApi_NetSmoke/GeneratedRoutesSmoke PASS; catalog719 identical,
zero validation problems. All275 frozen hashes match after validation; freeze released.
No active build/test scope. Next commit026 production/test repair and this handoff, close the
permanent matrix/inventory with its SHA, publish/verify, then reproduce018's unavailable-network
exception contract. Details: demos docs/repair-evidence/net/DISCOVERY_PREFLIGHT.md.
No new full719/native aggregate/visual/sanitizer/browser/other-platform/service claim;075 remains
the latest full checkpoint with six unchanged CTest residuals. No pending approval.
ISSUE018 contract preflight is also recorded there; keep it separate until reproduced.
The075 artifact freeze has ended. One compiler/test suite at a time, shared ccache, existing
trees, private display/network, MemoryHigh4GiB/MemoryMax6GiB/no swap. No pending approval.
Preserve inherited content/audio/graphics work, matrix113 and both older campaign handoffs.

#### Last complete checkpoint: ISSUE-075

Publication VERIFIED: CNA origin/next6767bda371bbb1635ff65844bedbd8885b745ebe and demos
origin/nextedc44ee9deecc212abf3d3b85b3924eab724add0. Sharp Runtime remains2f644cf47.
Current owned075 code: modules/c-api/tests/pure_c/NetSmoke.c adds sentinels around offset2/0/4,
reject-without-mutation and empty-queue preservation; native red CApi_NetSmoke fails exactly the
new offset2 boundary assertion before production changes (repair-packet-range-before.log/xml).
CnaCApiNetSessions.cpp now copies only the received byte range at offset; no other production
change. Native target build52146 passes; its21 nodiscard warnings all belong to unchanged
NetworkSessionTests.cpp exception assertions. The strict C adapter/test build has no warnings.
Complete CnaNetTests passes549/549 in46.642s (repair-packet-range-native.log/xml).
CApi_NetSmoke/GeneratedRoutesSmoke pass on SOFTWARE/GLES,0.11s each; packet tests are
renderer-independent, so no GPU/online-service claim. Consumer builds14040/40617 pass without
diagnostics. The untouched external demo passes28/28 on both selections, exact assertion names/
order preserved and one failure removed. Marker075/stale restriction removed only afterwards.
Catalog719/zero problems; only075 metadata differs. All275 artifacts are frozen in
repair-packet-range-full-artifacts.sha256. Full SOFTWARE completed719 IDs:
647PASS/21FAIL/48SKIP/3NOTRUN, only075 changes and40->39 failed assertions; all275 hashes match.
OPENGLES3 completed719 IDs:689PASS/20FAIL/7SKIP/3NOTRUN, exact only075 delta and39->38 failed
assertions. Final serial CTest735registered/732enabled=678PASS/48SKIP/6unchangedFAIL/3disabled;
exact per-test DELTAS[]. All275 hashes match after both sweeps and CTest; freeze released.
No build/test scope remains. Reports repair-packet-range-ctest.log/xml and exact comparison.
Compare against repair-host-cleanup-full/repair-host-cleanup-ctest, expecting only075 and one
removed failure per renderer. Do not rebuild/change executable inputs meanwhile.
Validation is complete (CTest659.97s). Fix40f286237 and075 permanent disposition are recorded;
publish the closure next. Preserve foreign files and matrix113. No new visual/sanitizer/full
native corpus/browser/additional-platform claim.
Read-only follow-on018/026 notes are in demos net/DISCOVERY_PREFLIGHT.md (uncommitted, exclude
from075). Existing549 native tests bypass independent-process discovery by handing over the
host port; SearchSocket remains Apple-only. No new018/026 runtime reproduction or code change.

#### Historical ISSUE-075 preparation and previous checkpoint

ISSUE-051 fix9b0f481f554c2961d6bbe15d1d445b045b49a745 and demos closureedc44ee are committed.
Matrix051 is RESOLVED/FIXED_VALIDATED; census156 IDs/121 final dispositions/35 without.
Next commit only the051 matrix/handoff closure and publish both repositories, verify remotes,
then add the075 native sentinel regression before changing LocalNetworkGamerReceiveDataAt.
Existing NetSmoke only compares payload bytes. Cover prefix/suffix, rejected-buffer preservation,
and exact-end/zero offsets without changing packet-consumption or sender ownership semantics.
No075 production/test change yet; preparatory evidence is demos net/PACKET_RECEIVE_RANGE.md.
Preserve inherited work. No running build/test scope remains after051; freeze is released.

#### Last complete checkpoint and history: ISSUE-051

VALIDATION COMPLETE: full SW719=646PASS/22FAIL/48SKIP/3NOTRUN and
GLES719=688PASS/21FAIL/7SKIP/3NOTRUN. Each exact comparison changes only051 and removes its eight
failed assertions. Final serial CTest735registered/732enabled=678PASS/48SKIP/6unchangedFAIL/
3disabled; per-test DELTAS[]. All275 frozen hashes match after both sweeps and CTest; freeze
released. Next commit051 adapter/test/docs, close matrix/inventory with its SHA, publish/verify,
then begin075's sentinel regression. No new visual/sanitizer/full native corpus/browser claim.

Publication VERIFIED: CNA origin/nexte2074613f1644cecaacf70a51489d972a4c51382 and demos
origin/next77346b73ed0969a6a8c9eea34d2493c270c743ce. Sharp Runtime remains2f644cf47.
Current owned051 files: CnaCApiRuntime.cpp, tests/pure_c/HostDrivenRunSmoke.c,
docs/c-api/CALLBACKS_AND_THREADING.md and demos lifecycle/HOST_FAILURE_COMPLETION.md.
The corrected native red witness (before-v3) has5FAIL/1PASS: missing completion and overwritten
first diagnostics; the first-Unload-only control passes. Earlier test versions forgot to
zero-terminate the byte-copy buffer, fixed before any production edit; all logs retained.
Candidate lets Exiting/EndRun use runAfterFailure, stores the first normalized message in CGame
and republishes it at run/terminal/destroy boundaries. Native Game and ordinary guards unchanged.
Strict build passes. Six native CTests pass SOFTWARE0.76s and OPENGLES313.16s; all six adversarial
cleanup cases pass, including refused nested calls and TLS replacement between host calls.
Full consumer builds26981 and51487 pass without diagnostics. The unchanged external demo passes
all73 child assertions on both renderers; exact names/order preserved and precisely8 failures
removed. Marker051 and the stale restriction sentence were removed only afterwards.
Catalog719 has zero problems and changes only those two051 metadata fields. All275 consumer
artifacts are frozen in repair-host-cleanup-full-artifacts.sha256. Full SOFTWARE completed719:
646PASS/22FAIL/48SKIP/3NOTRUN. Exact comparison changes only051,48->40 failed assertions; all275
hashes match. OPENGLES3 completed719:688PASS/21FAIL/7SKIP/3NOTRUN, only051 changes and47->39
failed assertions. Exact comparisons pass on both renderers; all275 hashes match after both.
Serial full CTest scope cna-host-cleanup-full-ctest/session74294 is running, reports
repair-host-cleanup-ctest.log/xml. Compare per-test status to repair-owner-ctest.xml, expecting
the same six residuals, then verify all275 hashes before releasing the freeze.
Do not rebuild or edit executable inputs until the full comparison and hash checks finish.
No051 closure or publication yet. Use reports repair-host-cleanup-*, bounded one-job builds.
Read-only next-batch preparation:075's C offset receive copies its entire zero-filled temporary
back over the caller buffer; canonical ReceiveData and the plain C receive copy only the payload.
The existing native NetSmoke checks no surrounding sentinels. New preparatory demos evidence
net/PACKET_RECEIVE_RANGE.md belongs to075, not the051 commit. No075 code/test change yet.

#### Last complete checkpoint: ISSUE-050

ISSUE-050 validation is complete. Full SOFTWARE719=645PASS/23FAIL/48SKIP/3NOTRUN;
OPENGLES3719=687PASS/22FAIL/7SKIP/3NOTRUN. Only050 changes relative to019; exactly its two failed
assertions disappear on each renderer. Final serial CTest735registered/732enabled gives
678PASS/48SKIP/6unchangedFAIL/3disabled; per-test status DELTAS[]. All275 frozen artifact hashes
match after both sweeps and CTest; the freeze is released. No full native corpus, new visual,
sanitizer or additional-platform pass is claimed. Native six-test controls pass on both renderers.
ISSUE-050 repair commit12837955407ec964ebce9752b1fae0b3e47b6b14 and demos closure77346b7 are
complete. Matrix050 is RESOLVED/FIXED_VALIDATED; census156 IDs/120 final dispositions/36 without.
Preserve all foreign work, especially113, content/audio ownership and the upstream082 record.
Reports: repair-owner-* in the demos existing linux-full tree. Preparatory051 evidence is a
separate uncommitted HOST_FAILURE_COMPLETION.md and was excluded from the050 commit.

#### ISSUE-050 investigation and validation history

Publication VERIFIED: CNA origin/next82a93ae97aa6eb6fac70e39bc7de35a37dfa8286 and demos
origin/next4e9f461881aae09db5c86df5108a800c484346bd. ISSUE-150 is closed;119/156 final dispositions.
Current owned050 work: new modules/c-api/tests/cpp/OwnerThreadRuntimeTest.cpp and its CMake target,
generator/spec/generated Runtime routes and generated smoke, docs/c-api/GENERATED_ROUTES.md,
plus generator regression and malformed-policy cases in test_generate_routes.py. The new generator
test failed before production changes (repair-owner-routes-generator-before.log).
Native baseline control passes; the new test has7PASS dispatch off/7FAIL dispatch on, all seven
wrongly queued calls released only by cancellation with STATE instead of THREAD,15.22s.
The native test checks seven owner-only routes with dispatch off/on;
it never pumps a forbidden operation, waits on a condition variable for at most2s and cancels
any wrongly queued call, requiring direct THREAD/categoryTHREAD and intact owner state to pass.
The candidate is explicit boolean group dispatch policy in the route generator, using existing
CallHereWithExceptionBarrier for owner-only groups and keeping ordinary calls dispatchable.
Include queue configuration/pumping, which runtime.h explicitly reserves for the game thread,
alongside run/run_one_frame/run_frame_ext/tick/destroy. Generated body comparison proves those
seven change only their barrier; no other generated function body changes. The formatted-copy
optimization is disabled for owner-only groups so it cannot bypass the policy. Five generator
tests pass, generated-file checks pass, all3205 routes remain covered. Native six-test regression
passes in12.53s, including all14 owner-thread cases, ordinary queued calls, host-driven lifecycle,
runtime game/components and the3197-route generated smoke. Full consumer rebuild is running.
Native six tests also pass SOFTWARE in0.96s. The original standalone demo now passes on both
renderers with all46 child assertions intact (the historical issue's30 is an earlier revision).
Exact assertion-name/order comparison against full019 baseline preserves all46 and removes exactly
two failed assertions. Parent JSON correctly has checks=0 for a standalone; the first focus harness
incorrectly asserted30 there and stopped after the passing SOFTWARE run. That harness failure is
retained; GLES was then run and both child logs independently validated. Marker050 is now removed
after those passes; no assertion or restriction changed. Final consumer build/full regression next.
Reports repair-owner-*. No closure yet; six global residuals remain separate.
Final consumer build60273 passed; catalog719 has only050 marker delta. All275 artifacts are frozen
by repair-owner-full-artifacts.sha256. Full SOFTWARE completed719 IDs:645PASS/23FAIL/48SKIP/3NOTRUN.
Exact comparison with019 changes only050 and removes exactly its two failures (50->48 assertions).
OPENGLES3 completed719 IDs:687PASS/22FAIL/7SKIP/3NOTRUN; only050 changes,49->47 failed assertions.
Exact status/marker/check/message and failed-assertion comparisons pass on both renderers.
All275 artifact hashes still match. Serial full external CTest scope cna-owner-full-ctest,
session29950 is running; report repair-owner-ctest.log/xml. Compare to repair-reset-ctest.xml,
expecting the same six residuals, then recheck hashes before releasing the freeze.
Do not rebuild or change executable inputs while frozen. Matrix050 is now
CONFIRMED_OPEN with the verified root cause; preserve inherited113 row. Owned external evidence
is lifecycle/OWNER_THREAD_DISPATCH.md plus its README link and the single050 metadata line.
Read-only051 preparation: CGame::EndRun and NotifyExit still use Invoke's failure-suppression
guard, unlike UnloadContent's runAfterFailure=true. Public host-frame contract promises both
completion hooks. Any future repair must preserve first callback diagnostics even when cleanup
callbacks fail or call another failing C API; current RecordCallbackFailure overwrites them.
No051 edit/reproduction change during the050 freeze.
The fresh complete owner sweeps reproduce051 identically on both renderers:65PASS/8FAIL out of73
child assertions, with all ten failure-point traces retained. Read-only preparation is recorded in
the new demos lifecycle/HOST_FAILURE_COMPLETION.md; do not include it in the050 closure commit.

ISSUE-150 repair is committed as379bac6d3f76f452ffce7c7760149b2d1df30b95. Matrix and demos inventory
now close150 as UNSUPPORTED_CONTRACT_CORRECTED, preserving its historical link reproducer and
description. Census156 IDs/119 final dispositions/37 without. The separate C smoke correction is
8a21c594d. Demos closure/evidence commit4e9f461881aae09db5c86df5108a800c484346bd is complete.
Pending owned closure work:150 matrix hunk and this handoff. Do not stage the inherited113
matrix/inventory hunk. Next commit the closure records, publish normal fast-forward, verify
remote SHAs and begin050.

C API audit commit079a2fbe2891dae7e144a95a55963fa45adb48de is published and its remote SHA verified.
ISSUE-150 remains OPEN until validation/closure. The issue explicitly permits retiring the
declaration-only facade instead of inventing a new subsystem-lifetime API. A repository-wide
search finds no production includer/caller; the original standalone reproducer still fails all
five link symbols in the pre-repair log. Candidate removes CNA/Misc.hpp, its eleven C inventory
rows and obsolete limitation theme, documents Game/C game lifecycle and preserves the historical
CBIND-037E5 record. CNA::Runtime the CMake module target is unrelated and remains unchanged.
New CApi_RetiredRuntimeFacade checks header absence plus the five exact retired method names;
it intentionally does not reject CNA::Runtime::Testing. Before header removal its new check
failed on the old public header; after removal it passes against the actual runtime archive.
Original reproducer remains unchanged. One-job/6GiB incremental CnaRuntimeTests build is running;
next run native runtime regression, clean/shared C API gates and the external consumer checks.
ISSUE-150 validation is now complete: native248=244PASS/4 unchanged SKIP, full consumer build
passes and275 binaries are byte-identical to the last full019 checkpoint. Catalog719/zero problems.
Fresh Game33 and C game14 cases per SOFTWARE/GLES preserve every status/marker/check count and
the same050/051 failures. Six audit/retirement CTests pass. The two additionally built C runtime
smokes pass on both renderers after the separate fixture correction below. Clean inventory removes
exactly11 obsolete declarations:466headers/8093symbols,7009implemented/15partial/626planned/443N/A;
shared worktree has8102symbols/635planned because of inherited work. Strict clean/shared audits
and33 scope-model tests pass. No sanitizer or new full renderer/visual run is claimed; old full
evidence remains byte-identical. Retained setup failures: nonexistent c_api.runtime category and
initially missing native smoke binaries. The catalog prefix c_api.game was verified before use.
Stage only own150 files and clean snapshot mapping/generated docs. Preserve inherited content,
audio, occlusion, matrix113 and upstream082 changes. The original150 reproducer is unchanged.
Next after the repair/closure commits: ISSUE-050, confirmed again by both C game category runs.
Read-only source diagnosis finds generated Runtime routes always use CallWithExceptionBarrier,
whose foreign-thread retry overrides the owner-only contract. CallHereWithExceptionBarrier already
provides local exception translation; review generator/spec policy and ordinary queued-call controls.
No050 production/test edit yet. ISSUE-051 remains separate.

Additional native C smoke audit exposed an independent stale ISSUE-019 test assumption:
RuntimeGameSmoke reset the clock in every Update and then expected each of two fixed-step calls
to draw. Its fresh run fails exit3 at the draw-count check; RuntimeComponentsSmoke passes.
Genuine XNA clock evidence and the seven native reset tests already establish that a reset step
may execute no Update/Draw. The test now exercises ResetElapsedTime once inside Update, consumes
its pending clock-reset step before the independent SuppressDraw checks, and preserves the reset
success, callback-reentry and exact draw-count assertions. No runtime implementation changed.
Separate test-only fix: strict C build passes; both C runtime smoke tests pass on OPENGLES3
in10.57s and on SOFTWARE in0.35s. Logs repair-runtime-facade-c-smoke-ctest (original failure) and
repair-reset-c-smoke-isolation-* (repair). Fixture-only commit8a21c594d is separate from150.

ISSUE-019 publication is VERIFIED: origin/next CNA70a7a4ef9f01faf071cbda6d3868de75ba5d56ba,
demosfe283d03b2054cbc8ef766358d0a1644609c78e8. Normal fast-forward pushes only; all owned019
source/evidence committed, no foreign changes included. Its runtime fix is f9747ea4964c4aa65cda1d5fa437ec40ab89911a.
The C API inventory repair is validated on the isolated published source snapshot under demos
build-probe/cna-capi-audit-9c068f632 (with the current Game.hpp brief), and separately on the shared
worktree. No runtime or ABI change. Provenance: 974c1ac08 changed the empty HapticDevice signature
to `() noexcept`; b0b0207e6 made three ContentManager helpers private while preserving the C routes
through ContentManagerAccess; 3d254dfa7 removed IsDisposableSharedPtr; d650554a8 added two protected
stock-parameter construction helpers whose observable results have existing C parameter/bone routes.
The mapping now pins the exact constructor, removes three private helper pins and the two deleted
trait pins, and explicitly dispositions just the two protected construction methods. No broad pin
approval, public operation suppression or completed-task reopening was used.

Clean source: 467 headers / 8104 symbols, 7009 implemented / 15 partial / 626 planned / 454 N/A.
Shared worktree: 8113 symbols, 7009 implemented / 15 partial / 635 planned / 454 N/A; the extra
declarations belong to inherited work, not this commit. The clean/shared mapping differs ONLY in
the inherited sound-effect-move-operations pins. Stage the clean source's four generated/mapping
files via the reviewed patch; keep the combined shared documents in the worktree. The independently
confirmed obsolete trait-rule deletion overlaps an inherited hunk but needs no uncommitted source.
Original inherited documents are preserved under demos build-probe/capi-inherited-docs-before-regeneration.

Validation: existing scope-model suite 33/33; all five registered shared CTest gates pass in16.34s
(CoverageMatrix, CoverageScopeModel, Limitations, DocExportCounts, ReleaseGate). Clean release gate
and coverage checks pass while correctly retaining NOT READY and the626 planned bindings. Initial
scope-fixture run32PASS/oneERROR lacked the archived cmake/Tests/ModuleProbes.cmake; adding that
published input made33/33, with both logs retained. Reports are demos build/linux-full/reports/
repair-capi-*. No C++ rebuild, graphics run or sanitizer run is claimed for this mapping/doc batch.
Last complete runtime/renderer checkpoint remains ISSUE-019 below; six external residuals unchanged.
Next: commit/publish this audit only, then resolve ISSUE-150's independently reproduced five-symbol
facade link failure. No150 source change yet; preserve all content ownership and113 work.

### Completed ISSUE-019 checkpoint

CNA runtime repair `f9747ea4964c4aa65cda1d5fa437ec40ab89911a` is committed. Demos closure
`fe283d03b2054cbc8ef766358d0a1644609c78e8` is committed; Sharp remains
`2f644cf4761123275426247b4d582e3392b4a181`. Status-only CNA commit follows this runtime revision.
ISSUE-019 is RESOLVED/FIXED_VALIDATED, matrix156 IDs/118 final dispositions/38 without.
Original demo assertions remain intact. Only its knownIssue marker was removed.
Both next refs are published and verified above; continue the separate coverage audit.
No merges, force pushes, tags, or edits to museum/cna-engine/cna-extended.

Validation is complete. Native runtime244PASS/4 unchanged SKIP of248; all seven new reset
regressions pass. Genuine XNA17 clock checks plus six live control/reset checks pass.
Full719-ID SOFTWARE/GLES sweeps change only019 from FAIL to PASS; original four assertions kept.
Final serial CTest94813 finished713.22s:735 registered/732enabled,678PASS/48SKIP/6FAIL/3disabled.
Exact comparison with the directory checkpoint has DELTAS[]. All275 binary hashes match after
completion; the freeze is RELEASED. No compiler or test process from this batch remains active.
The global gate remains RED on the same six external residuals and the separately diagnosed
pre-existing coverage inventory rules; do not claim full campaign/native-corpus completion.

The runtime commit contains only Game.cpp, Game.hpp, seven tests, own019 upstream-plan row
and this handoff. Demos commit contains only marker/inventory/evidence changes. A first demos
commit attempt was automatically rejected because the review mistook the displayed unstaged113
diff for the index. Direct `git show :docs/CNA_ISSUES.md` comparison proved113 byte-identical to
HEAD in the index, with exactly six authorized staged paths. The same commit was then approved;
no workaround, staging change, or user permission request was used. No approval block remains.
Never stage inherited113 matrix/inventory,082 upstream-plan or content ownership changes.

### ISSUE-019 validation record

Directory publication is finished. Active owned candidate: Game.cpp, Game.hpp and new
ResetElapsedTimeTests.cpp; only019 hunks in matrix and upstream plan, plus this handoff.
Do not stage inherited113 matrix or082 upstream-plan hunks. Demos own new lifecycle directory
contains two genuine XNA reference sources and RESET_ELAPSED_TIME.md. Original assertions unchanged.
Actual XNA4 reference now passes17/17 clock/mutation checks. Fresh-process live Game.Run cases
pass control2/2 (12 catch-up updates) and reset4/4 (one ordinary update, slow=false).
Native pre-repair full runtime baseline:246 tests,238PASS/4SKIP/4FAIL; all four failures are new
ResetElapsedTime regressions, variable-reset control passes. Reports repair-reset-runtime-before.*.
Candidate makes reset apply to both modes, consumes only the next clock interval, keeps prior
debt and planned steps, and clears slow history without changing the currently executing
Update argument. Native builds51133/89090 passed without diagnostics. First candidate run65091:
247 tests,243PASS/4 unchanged SKIP/0FAIL. Exact comparison changes four failing reset witnesses
to PASS and adds one passing fractional-debt test; all old outcomes retained. Six reset tests pass.
Consumer rebuild26240 was deliberately interrupted only in its named scope (exit130): expanding
the header's documentation shifted debug line information and invalidated hundreds of cached
objects. The correct public brief now retains its original single line; no semantic/test edit.
Restart36976 passes (967 planned/966 printed steps), no compiler diagnostics. Original demo now
passes4/4 on SOFTWARE and OPENGLES3 (one update, slow=false), with its marker still retained.
V5 genuine observation of20 retained whole steps leaves Draw slow=false. Seventh regression
RetainedDebtDoesNotImmediatelyRestoreSlowDrawHistory reproduces6PASS/1FAIL (only Draw flag).
The final correction excludes retained reset debt from immediately restoring lag history.
Genuine reference v6 passes17/17. Final native run48764:248 tests,244PASS/4 unchanged SKIP/0FAIL;
all seven reset tests pass. Original demo marker removed after both renderer passes, with all
four assertions unchanged. Consumer build2181 passes (228 planned/227 printed steps), no compiler
diagnostics, one compiler/6GiB. Final timestep category5/5 and C runtime controls1/1 pass on each
renderer. Catalog719/zero problems; all6 visual references pixel-exact. All275 binaries were
frozen by repair-reset-full-artifacts.sha256 through validation. Complete sweep51712
finished: SOFTWARE719=644PASS/24FAIL/48SKIP/3NOTRUN; GLES719=686PASS/23FAIL/7SKIP/3NOTRUN.
Exact comparison with the directory checkpoint changes only019 FAILED->PASSED and removes its
marker/one burst assertion failure. All other statuses, markers, check counts and failed-assertion
multisets unchanged (51->50 SW,50->49 GLES). All275 hashes verified after both sweeps.
Final serial CTest94813 finished713.22s with identical per-test statuses to the directory
checkpoint:678PASS/48SKIP/6FAIL/3disabled. All hashes verified; freeze released. Reports
repair-reset-ctest.{log,xml}, repair-reset-ctest-comparison.log, repair-reset-hashes-after-ctest.log.
C API coverage check69539 FAILED on two stale rules: haptic-device-empty-constructor does not
match noexcept added in974c1ac08; content-manager-resolve-existing-asset-path still describes a
public helper made private inb0b0207e6. Both changes are present in base9c068f632 HEAD, unrelated
to Game's comment/implementation. Preserve failure log repair-reset-capi-coverage.log; no coverage
pass is claimed. These need a separate inventory repair, preserving inherited mapping/doc hunks.
Prose export counts pass (8 claims/3205 exports). The global gate had six existing
CTest residuals at the directory checkpoint; do not hide them or call the campaign complete.
Independent read-only ISSUE-150 preparation: its original public consumer still fails all five
Runtime symbols when linking the current DSO (repair-runtime-facade-before.log). No150 source edit.
Next-batch audit preparation only: ignored demos build-probe/cna-capi-audit-9c068f632 is a116MB
source-only archive of published HEAD (modules/tools/docs/plan/ENet headers), not a new compiler
build tree. Clean coverage baseline22436 fails the same two rules PLUS content-reader-detail-traits.
That third stale rule is already deleted in the inherited uncommitted mapping hunk; do not stage
it as part of019. This proves the published baseline has three stale coverage rules, while the
shared worktree has two. No source/index change was made for this diagnosis. Keep clean and
shared validation distinct; generated coverage docs must not accidentally include inherited work.
Doxygen diagnostic confirms HapticDevice() noexcept = CPP-53709B9BFBB0 and no public
ResolveExistingAssetPath. Its exact new signature is `() noexcept`, not `()noexcept`.
First interrupted consumer log retained.
No closure/commit yet. Older preparation notes below are historical.

Current published revisions: CNA9c068f6324bf06cd43f3de4d035de54bce4fecfa,
demos499a58e5a188671d4dbe7dc40bec7cf1208273db,
Sharp2f644cf4761123275426247b4d582e3392b4a181. All three remote next refs verified;
no force push, merge or tag. Directory checkpoint is complete and its binary freeze released.
The campaign still has156 IDs/117 dispositions/39 without;010 is only partially repaired.

ISSUE-152 disposition/handoff checkpoint is published in CNA `8c162e142af498c5db22f14fe9f3eba2b9a938ed`
and demos `0086bc424e50a023c79698a3107c2e1d7dd0aa41`. Three completed FileShare probe binaries
(sanitizer, Framework sharing executable, strict C smoke) were removed per disk policy;
sources/logs remain and are rebuildable. The full152 binary freeze was verified and released.

Sharp File.Delete repair `d051307b3316903a692846acf78200d74339bbef` is published, full gate
18223/18223/42 executables,zero skips/failures. Strict MinGW/Emscripten checks and boundaries44/109
pass. CNA new DeleteFile tests reproduce50PASS/2FAIL before forwarding;52/52 after. C shared
exception barrier regression reproduces exit31 for UnauthorizedAccessException falling through
to INTERNAL; after mapping it to IO the same boundary test exits0. C StorageSmoke and the demo
gain directory preservation/missing parent/missing leaf assertions; original assertions remain.
No010 closure. Genuine XNA12/12 and actual .NET8 7/7 references are in the demos evidence sources.

Follow-up emulated Windows product validation: actual File/FileStream and existing tests compile
strictly with MinGW C++23/-O1 and pass16/16 under Win64 Wine, including144 share pairs and4 deletion
cases. First -O0 type_info duplicate-symbol linker failure retained, no source/oracle workaround.
Sharp docs-only checkpoint4c7c69bd731b53bbb2fd87cb0f8da233278eb67d records reproduction.
Native Windows/CNA facade and browser runtime remain pending; do not equate Wine with those.

Build session19270 finished warning-free (469 planned/466 printed steps). Both renderer-labelled
storage7/7 and C storage2/2 pass; directory demo7 ->11checks, other focused outcomes unchanged.
Strict C17/Werror StorageSmoke against rebuilt C ABI passes. Catalog719/zero problems,6 exact
visuals and all6 unchanged-budget platform audits pass; C API routes3197/57, coverage3205/3205
and8 prose counts pass. All275 demo/CNA/C API binaries are now frozen by
repair-storage-filedelete-full-artifacts.sha256. Do not rebuild them during runtime sweeps.
Both complete sweeps14464 finished, prefix repair-storage-filedelete-full-v2, explicitly
SDL_AUDIO_DRIVER=dummy (matches CMake baseline). Each719 IDs: SW643/25/48/3,GLES685/24/7/3.
Exact comparison with repair-fileshare-full: statuses/markers/failing-assertion multisets unchanged
(51SW/50GLES); only directory checks7 ->11, all passing. All275 hashes verified unchanged.
First attempt73692 omitted dummy audio and had no-device skips/failures, including VideoPlayer
child abort; preserve its partial full-parts/SOFTWARE logs. Identified shell863182/private scope
stopped, exit143; old queued GLES never started. This is unmatched-environment evidence, not
an accepted regression checkpoint or a hidden knownIssue. All275 hashes still match; no source
edit/rebuild/retry of an assertion was used to correct the environment. No-audio paths need
separate triage. Full serial CTest94083 finished726.18s:735 registered/732enabled,
678PASS/48SKIP/6FAIL/3disabled. Exact ID/status comparison with repair-fileshare-ctest has zero
differences, including every failure and skip. All275 hashes verified after completion;
the binary freeze is released. Reports repair-storage-filedelete-ctest.*. A daemon restart
interrupted the earlier launch before any scope/process/log existed; only one run executed.
The global gate stays RED with the six residuals listed below. No full native CNA corpus,
native Windows/browser runtime or new sanitizer pass is claimed.
This checkpoint commits only the six owned CNA source/test/documentation files, the ISSUE-010
matrix hunk and this handoff. Demos publication follows: directories.cpp, storage/README.md,
FILE_DELETION.md, FILE_SHARE.md, FileDeletionOracle.cs, StorageOperationsOracle.cs and the own010
inventory progress hunk. Everything else remains inherited dirt, including
StorageContainerPathErrorsTests.cpp and the ISSUE-113 matrix/inventory hunks.
Do not stage it. Detailed evidence: demos docs/repair-evidence/storage/FILE_DELETION.md.
Deletion is now published: CNA4d384865ca0b586a4ed52929907589ea32a1656b and
demos27fd84c809b5c9df8a2563828ffeae4c55172d31, both remote refs verified.
Next batch in progress: CreateDirectory(existing file/file parent) and missing-root enumeration.
CNA baseline53PASS/2FAIL of55 includes a passing Unicode name/filter control. Candidate delegates
creation/enumeration to Sharp Directory, retains GlobMatch/empty-pattern behavior, and passes55/55.
Sharp five directory tests reproduce4PASS/1FAIL on Linux AND actual Windows code under Win64 Wine:
embedded NUL creates/enumerates the prefix. Unicode already passed in that MinGW configuration;
do not claim a reproduced Unicode failure. Actual .NET8.0.31 oracle12/12 passes.
Sharp repair2f644cf4761123275426247b4d582e3392b4a181 is published (remote verified): full
18228/18228/42 executables,0skips/failures, warning-free build; Windows/Wine5/5, boundary44/109.
No native Windows/MSVC/browser runtime or sanitizer claim. C smoke's new creation rejection
assertions fail with exit4 against the old external DSO, before the consumer rebuild.
Full demos rebuild90648 passed (234 planned/231 printed steps), no compiler diagnostics.
Both renderer storage7/7 and C storage2/2 pass; directory checks11->18, every old check retained.
Strict C17 smoke now exits0 against the rebuilt DSO, unchanged executable from pre-fix exit4.
Catalog719/zero problems and6 pixel-exact visuals pass. Platform ratchet/nonproduction/hotpath
audits pass with unchanged budgets. All275 binary hashes frozen in
repair-storage-directory-full-artifacts.sha256. Complete sequential SOFTWARE/OPENGLES3 sweep
session96493 finished with explicit SDL_AUDIO_DRIVER=dummy. SOFTWARE643/25/48/3 and
OPENGLES3685/24/7/3, each719 exact IDs; statuses/markers/failing-assertion multisets identical
to the deletion checkpoint (51SW/50GLES), only directory checks11->18. All275 hashes match.
Final serial CTest56448 finished692.56s:735 registered/732enabled,
678PASS/48SKIP/6FAIL/3disabled. Exact ID/status comparison with the deletion checkpoint has
zero differences. All275 hashes match after CTest; the binary freeze is released. Reports
repair-storage-directory-ctest.*. The same six residuals keep the global gate RED.
Owned new CNA work: StorageContainer.cpp, StorageDeviceTests.cpp, StorageSmoke.c and this handoff.
Owned demos: directories.cpp, DIRECTORY_OPERATIONS.md, DirectoryOperationsOracle.cs. Sharp clean.
Reports repair-storage-directory-*. This checkpoint publishes only the three owned storage/C
source/test files, the ISSUE-010 matrix hunk and this handoff. Demos inventory/evidence publication
follows with the CNA commit SHA. ISSUE-010 stays CONFIRMED_OPEN: content guard/remaining filesystem
error audit is not closed. No marker is removed. Next: publish CNA/demos, then complete the019
genuine live reference and deterministic native baseline before changing Game production.
Independent reference preparation for ISSUE-019 has started: ResetElapsedTimeOracle.cs and
RESET_ELAPSED_TIME.md belong to the NEXT batch, not the directory commit. The sample-bin Game.dll and
actual loaded XNA GAC differ (GameClock type not found in the sample-assumed assembly).
First oracle failed with TypeLoadException; only its exact process/debugger were stopped.
The sample's disassembly is not accepted as genuine runtime evidence. The GAC monodis attempt
aborted. V2 passed10/12 and disproved the sample-derived frame-clock assumptions. Corrected v3
passes11/11 against the actual GAC Game/Clock assembly: reset consumes stale time for fixed steps,
preserves previous whole/fractional debt, clears slow history, and zeroes one variable update.
No Game product/demo edit. Actual Game.Run/private-display and deterministic native seams are
still needed before repair; do not infer the exact live clock behavior from the sample assembly.
Current full SOFTWARE game category still reproduces019's original failed catch-up assertion.
The first live XNA oracle shows the no-reset control's12 catch-up updates but stops the reset
run before its post-load witness (4/6 checks pass). No live reset pass is claimed. Its Draw-count
termination is now conditioned on actually observing the post-load update, and the burst check
also refuses zero unobserved updates. Corrected live execution is pending; keep first failed log.
Live v2 still returns from its SECOND Game.Run before the witness despite the corrected draw
termination (3/6). The harness now runs control/reset in separate fresh processes; compilation/
execution of that isolation correction is pending. Do not attribute this host-loop issue to019.
Four deterministic native clock tests are now added in the separate new ResetElapsedTimeTests.cpp,
using the existing platform decorator and a scripted counter; no sleep or Game production edit.
Native-only build58721 of CnaRuntimeTests passed (40 planned/37 printed steps), one compiler/6GiB.
No compiler warnings; existing vendored Draco CMP0148 configure warning remains. It did not
rebuild frozen demo artifacts. Native execution remains pending. The expanded constructor-free
XNA v4 oracle passes15/15 and additionally proves Reset does not mutate the current Update's
GameTime slow flag, while the next already-planned Update sees cleared slow history. Exclude this new
runtime test file and all lifecycle reference preparation from the directory repair commit.

### Previous checkpoint: ISSUE-152 validated on Linux; ISSUE-010 deletion next (2026-10-23)

Published CNA next `169470ee7550471844d01a0789dd5e25770544b6`, Sharp next
`f51bf9acf8374088a425024b40ef1c7b697c5ba5`; demos baseline HEAD
`0fbe936047059b5bac75eefc6e8c1a3630503aed`. FileShare status/marker evidence is published as
demos `0086bc424e50a023c79698a3107c2e1d7dd0aa41`, separately from the next repair.
Permanent152 disposition is FIXED_PLATFORM_VALIDATION_PENDING:
Linux validated, Windows product runtime pending; strict cross-compilation is not a runtime pass.
Sharp's two commits c6984ecef/f51bf9acf and CNA21b4cd64c/169470ee7 are already published.

Final152 regression: both complete719-ID sweeps finished, SOFTWARE643PASS/25FAIL/48SKIP/3NOTRUN,
OPENGLES3685PASS/24FAIL/7SKIP/3NOTRUN; no added failing assertions or PASS-to-FAIL against the
preceding repair-sprite-binding-full checkpoint. Storage alone changes only152, retaining8checks.
All6 visuals pixel-exact;16 tool tests pass;275 binary hashes unchanged after all runs.
Serial CTest735 registered/732enabled completes695.99s:678PASS,48SKIP,6FAIL,3disabled.
Global gate remains RED: unmarked c_api.content.content_manager and
c_api.graphics.resource_lifecycle assertions; stale knownIssue unexpected passes for
content.content_manager.read_asset_ownership/several_managers/unload_disposes and
graphics.occlusion_query.visible_pixels. Preserve these, including the inherited uncommitted
ownership/occlusion changes. Compared with Complete1128, resource_lifecycle is still an
independent historical PASS-to-FAIL regression. No full CNA corpus pass is claimed here.
Full details: demos docs/repair-evidence/storage/FILE_SHARE.md and repair-fileshare-* reports.
Campaign census after152:156 historical IDs,117 final dispositions/39 without, plus DC-R1/DC-R2.

Next task already started without modifying frozen demo binaries: genuine XNA
StorageOperationsOracle.cs passes12/12 filesystem exception/preservation controls. New lower-layer
FileDeletionTests.cpp baseline1PASS/4FAIL exposes wrong directory exception, ignored missing
parent, NUL-prefix deletion and directory-symlink refusal. Candidate File.cpp uses file-only
unlink/DeleteFileW, preserving directories and translating native errors. Initial full IO748/749:
only the old Delete_OnDirectory_ThrowsIOException expectation disagreed. Actual .NET8 oracle7/7
and genuine XNA confirm UnauthorizedAccessException; that test is corrected, not suppressed.
Sharp full gate completed (session63637):18223/18223 across42 executables,zero failures/skips;
warning-free incremental build. No010 production commit or closure yet.
Owned next-batch files: Sharp File.cpp, IOStreamTests.cpp, new FileDeletionTests.cpp; demos
new StorageOperationsOracle.cs and FileDeletionOracle.cs. Preserve all other dirt. After full
Sharp validation, publish the lower layer, add failing CNA DeleteFile preservation regressions,
forward through File::Delete, validate native/storage/C API on both renderers, and continue
CreateDirectory/enumeration errors. ISSUE-010 as a whole also includes content behavior.

### Previous checkpoint: CNA-DEMOS-ISSUE-152 native handle repair and consumer validation (2026-10-23)

The CNA facade/native tests are published as
`21b4cd64cd122353431dafb5793a19ac568956fd` (normal push83014, ls-remote verified).
The lower-layer repair is published in Sharp Runtime as
`c6984ecef98aa6a04634e76145763545a90c68fe` (normal push25211, ls-remote verified).
It uses one native handle, Windows
CreateFileW sharing/POSIX advisory flock, lock-before-truncate, buffered descriptor adoption,
non-destructive write-only opens, handle-based length/resize, append boundaries and explicit
write-error reporting. No second locking handle or pathname reservation table. Short
FileStream constructors default Read sharing, File.Create uses None. Public signatures remain;
new four-argument constructor is additive. **Linux FileStream layout576 ->24, align8 unchanged:
rebuild every consumer, including derived IsolatedStorageFileStream.** Sharp migration note
`docs/Migration-FileStream-NativeSharing.md` and native before/after/ownership pins document SA-3.

Validation so far:
- Full Sharp warning-free build, full gate **18,218/18,218**,42 executables,zero skips/failures.
  Existing actual XNA fixtures and private original Yacht SOAP service; IO744 tests.
- First native IO run740/741 exposed stale read-ahead after resize. Fixed the implementation,
  retained that test;741/741 then passed before adding3 more safety tests. No oracle weakened.
- Actual FileStream.cpp +23 sharing/path/EOF regressions **ASan/UBSan/LSan23/23 PASS**;
  supporting linked archives are not fully instrumented. Disposable binary still exists at
  Sharp build-probe/cna-repair-fileshare-sanitizer; remove only it after recording evidence.
- Final-source MinGW and Emscripten syntax checks, Wall/Wextra/Wpedantic/Werror PASS.
  No Windows/browser runtime sharing pass is claimed; browser VFS locking stays best-effort.
- CNA4 added regressions reproduce all4 failures before facade repair (46/50 pass). Forward
  sharing, exclusive CreateFile/short OpenFile defaults, and Append write-only: **50/50 PASS**.
  New nodiscard warnings in the test baseline were corrected with void casts; final build clean.
- All6 CNA platform/SDL/hot-path audits PASS, budgets unchanged. Sharp boundaries44/109 PASS.

Owned Sharp files: FileStream.hpp/.cpp, File.cpp, new FileStreamSharingTests.cpp, migration note.
Owned CNA files: StorageContainer.hpp/.cpp, StorageDeviceTests.cpp, this handoff. Other dirty
files, especially the inherited StorageContainerPathErrorsTests.cpp and ownership/occlusion
work, are untouched and must not be staged. No cna-demos marker/status change yet.

The full demos build/linux-full target rebuild completed warning-free (session41158,
300 planned steps/297 printed, one compiler,6GiB cap). SOFTWARE and OPENGLES3 storage each
pass7/7: the original FileShare demo now passes **8/8 unchanged checks**. Exact comparison
with repair-museum-storage-delete changes only that ID's FAILED ->PASSED and clears its
exclusive-open failure message; all other statuses/counts/markers/messages are identical.
Both runs exit1 solely for the stale152 marker's unexpected PASS, not a failed assertion.
C API storage2/2 PASS on each renderer (35/38 internal child checks, outer checks0 unchanged).

Additional actual .NET8.0.31 oracle covers all16 legal Delete/Inheritable sharing masks:
2304pairs,2025allowed/279refused,zero native-policy mismatches; append-boundary controls pass.
Source: demos docs/repair-evidence/storage/FileShareFlagsOracle.cs. Genuine Framework4 under
Wine also passes:2304pairs,400allowed/1904refused,zero policy mismatches/append failures.
First reference compile failed on a path spelling (retained); corrected -v2 compile/run passes.

The marker-only demo rebuild is complete. Full SOFTWARE smoke is running in session64878,
scope cna-fileshare-full-software-1023, through the existing complete-category driver.
Artifacts are frozen in repair-fileshare-full-artifacts.sha256 (275 files). Do not rebuild
them during the sweep. Latest full predecessor is repair-sprite-binding-full; its existing
unmarked c_api.content.content_manager/resource_lifecycle failures and stale010/011 passes
are still visible, not accepted or reclassified. Peak observed scope memory <900MiB.

C API follow-up (this checkpoint): storage.h/STORAGE.md no longer claim ignored sharing or
ReadWrite short defaults; StorageSmoke.c adds native conflict->IO/invalid-handle/nontruncation,
shared-reader positive controls and Append/default checks. Strict C17/Wall/Wextra/Wpedantic/
Werror compile and no-window actual shared-library runtime PASS; linkage log records the
rebuilt external C ABI. No adapter implementation/signature or demo oracle changed.

Next: finish SOFTWARE, run OPENGLES3 sequentially, exact visuals and full CTest; compare
failure/assertion/status deltas before final152 disposition. Then commit only owned status
hunks in shared dirty documents. See demos docs/repair-evidence/storage/FILE_SHARE.md.
This checkpoint is NOT a full CNA corpus or full repaired-demo sweep pass claim.
Evidence prefix: demos build/linux-full/reports/repair-fileshare-*.
Campaign remains156 historical IDs/40 pending until the actual issue disposition is updated.

### Previous checkpoint: CNA-DEMOS-ISSUE-152 FileShare lower-layer baseline (2026-10-23)

Published repair checkpoints, all remote SHAs verified:
- CNA lazy root `fffa552ad4b4567c772f2f293302b8b103a5b16f`;
- CNA empty arguments/patterns `17c66a9d3541511881ff8fc9f8f9aaf82b5f326a`;
- CNA deletion errors/file preservation `ac6be8f03ea3b0f37d73c8ac7c7ad1cfbc505cd1`;
- Sharp Runtime lazy policy `31c7c08f1987a284ce1b84bf1c779ce236a6e1c6`;
- demos lazy-contract evidence `0fbe936047059b5bac75eefc6e8c1a3630503aed`.
No consumer/museum source or pin was changed. Museum owns external revalidation/closure.
Native storage46/46 and the renderer/C API evidence below are the last finished checkpoint;
they are NOT a full CNA suite pass or campaign completion. Historical156 IDs /40 pending
dispositions are unchanged by these museum-only batches.

ISSUE-152 is now active engineering, not awaiting policy approval. Owner chose native .NET
behavior per platform on2026-10-20: Windows144-pair matrix119refused/25allowed, Linux
.NET8 matrix63refused/81allowed; exclusive None must refuse conflicts on both. Permanent
reference and detailed differences: demos docs/repair-evidence/storage/README.md §Measured
FileShare platform split, FileSharePlatformOracle.cs, FileShareEdgesOracle.cs,
StorageContractsOracle.cs. Do not use the local .NET11 preview source as unqualified XNA
evidence; compare it with those actual Framework4/.NET8 measurements.

New owned **uncommitted** Sharp file:
`modules/io/tests/System/IO/FileStreamSharingTests.cpp`. No Sharp production edit yet.
Its three pre-fix regressions are intentionally red: missing four-argument constructor,
and destructive write-only Open/OpenOrCreate. Native IO build44674 clean; full IO run52801:
**730run /727PASS /3FAIL**, all failures are the new tests. Prior full Sharp gate at31c7c08f
was18,201/18,201,42executables,0skips. Do not commit the red test file alone under Sharp's
zero-failure commit policy, and do not remove/weaken it. Evidence prefix:
demos build/linux-full/reports/repair-fileshare-current-before*.

Current source has std::fstream as its handle and exposes no share-taking constructor;
CNA OpenFile discards fileShare. GCC14.2/C++23 has no fstream native-handle feature macro.
The write-only truncation comes from std::ios::out without in/app; adding in would demand
read permission and is not a correct write-only repair. Locking a second handle is not a
correct Windows fix because it conflicts with the stream's own handle. A localized native
open/owned-handle backend in FileStream is therefore under consideration, not implemented.
Keep buffering, short-read/EOF/seek behavior, constructor exception cleanup and write errors
correct; avoid unbuffered per-byte syscalls in content loading. Lock BEFORE truncation.
Use the same owned handle for live length/resize, not reopening the path. Preserve ownership
traits explicitly if replacing fstream, whose noncopyability currently provides them.

Fresh layout measurement (existing build-probe,1job,Werror) at31c7c08f:
Linux x86_64 FileStream sizeof576/align8/copy0/move0; Stream sizeof8/align8. Historical
Win64 compile-only size536/align8 is in demos README; it is not a new Windows runtime pass.
Any layout change must satisfy Sharp SA-3: before/after pins, migration/full-consumer-rebuild
note, no unsupported signature/noexcept change, full green gate. Retain current signatures;
the new overload is additive. No native backend layout has been chosen or changed yet.

Exact next task: design the smallest single-handle native FileStream backend, expand tests
for the measured144-pair policy, aliases/cross-process exclusion, pre-truncation refusal,
failed-constructor fd cleanup and EOF controls, then implement and run the full Sharp gate.
Also wire File.Create and CNA CreateFile/OpenFile defaults: FileStream's short constructors
default Read sharing, but File.Open/CNA short OpenFile and File.Create are exclusive; the
2arg Append path chooses Write. Preserve that distinction. Review Emscripten/Windows paths,
cross-build where practical, document unavailable runtime coverage. Publish dependency first,
rebuild consumers after any layout change, then remove ISSUE-152 only on real demo PASS.

No compiler/test/helper remains active at this checkpoint. Single compiler, shared cache,
6GiB cap, existing build dirs only. The disposable layout executable was removed after
transcribing its measurement. Other inherited CNA/demos work remains untouched; Sharp's
only dirty path is the owned new regression file above. No goal was created or changed.

### Previous checkpoint: museum B11 directory deletion errors (2026-10-23)

Previous argument/pattern repair is published as
`17c66a9d3541511881ff8fc9f8f9aaf82b5f326a` (push82218, remote verified).
Confirmed remaining defects: std::filesystem::remove's error escapes as filesystem_error,
missing directory silently succeeds, and a regular file supplied to DeleteDirectory is
actually deleted. The last case was found while minimizing this batch and reproduced by
a new test checking both exception and retained file. XNA delegates to Directory.Delete;
its nonempty/missing cases have the29/29 genuine reference evidence above. Microsoft's
documented regular-file contract is IOException:
https://learn.microsoft.com/en-us/dotnet/api/system.io.directory.delete?view=net-7.0 .

Repair keeps CNA's existing resolved native path (important for Windows UTF-8), rejects
regular-file status, maps missing-directory and IO/access errors to the framework types,
and retains the native filesystem error as an inner diagnostic cause. No recursive delete
or containment-policy changes. This is not a claim of atomic behavior under concurrent
external replacement of directory entries. Sharp Directory currently narrows paths, so
redirecting through it would regress the established Unicode behavior.

Four permanent regressions in existing StorageDeviceTests.cpp: BEFORE3FAIL/1PASS (Unicode
success control). AFTER full native storage **46/46 PASS**, zero skips/failures, including
all seven inherited untracked tests without changing them. One separately seeded-order
run (2310) also46/46PASS to check global-policy/fixture ordering; not a retry for a failure.
Native build71657 clean. Consumer rebuilt only cna_shared in existing demos build/linux-full
(43523,7steps), since no API/ABI or demo source changed; ldd verifies the demo loads that
exact CNA/libcna.so. SOFTWARE/OPENGLES3 each retain6PASS/1knownISSUE-152FAIL, exact
status/check-count/knownIssue/message delta empty. C API storage children2/2PASS on each
(35member +38save checks; outer report checks0 is only the process wrapper). All six
boundary audits PASS, budgets unchanged. One compiler/shared cache/6GiB/private display.
No full CNA, new sanitizer, Windows or Android runtime pass is claimed for this batch.

This checkpoint is the commit containing this entry, based on17c66a9d3. Owned files:
StorageContainer.cpp/.hpp, StorageDeviceTests.cpp and this handoff only. Next: publish,
then resume CNA-DEMOS-ISSUE-152 FileShare at the actual Sharp Runtime FileStream layer.
Current CNA OpenFile ignores its share argument; Sharp Runtime has only the three-argument
fstream constructor, so a caller-only change cannot implement sharing. Owner selected
native .NET behavior per platform (Windows stricter than Linux); preserve the original
FileShare.None oracle on both. Do not emulate Windows sharing using an extra second file
handle that conflicts with the stream's own handle, or silently weaken that oracle.
Evidence prefix: demos build/linux-full/reports/repair-museum-storage-delete-*.

### Previous checkpoint: museum B11 empty storage arguments/patterns (2026-10-23)

Lazy-root checkpoint published: CNA `fffa552ad4b4567c772f2f293302b8b103a5b16f`,
Sharp Runtime `31c7c08f1987a284ce1b84bf1c779ce236a6e1c6`, demos
`0fbe936047059b5bac75eefc6e8c1a3630503aed` (all remote SHAs verified).
Demos dedicated evidence `docs/repair-evidence/storage/LAZY_ROOT.md`; catalog3/3PASS.
The owned disposable Sharp sanitizer binary was removed after recording6/6; sources/logs
remain. Inherited ownership/occlusion/path-error work remains untouched.

Next confirmed root cause: StorageContainer uses ArgumentException for empty file/directory
paths and rejects empty search patterns. Genuine XNA4 ValidateArguments uses
ArgumentNullException with parameter file/directory before stream-option validation;
enumeration delegates to Directory, where empty pattern returns zero matches before IO.
Existing real GAC public-call evidence: demos report repair-storage-path-reference-v1-xna.log,
29/29PASS. Its untracked oracle/test files are inherited and must not be staged here.

Three owned permanent regressions were added to existing StorageDeviceTests.cpp (reusing
its owned-root fixture): nine path-entry cases with exact type/parameter; empty patterns
with both present and missing root; disposal precedence control. BEFORE:2FAIL/1PASS.
Candidate changes only seven empty-path throws and two empty-pattern returns, plus public
contract comments. AFTER full native storage:40PASS/2FAIL in42; remaining failures are
the pre-existing nonempty/missing-directory error-type cases, not suppressed.

Native builds65069/23075 clean. Demos build61151 completed0 in existing build/linux-full,
no warnings/errors. Seven storage demos on each of SOFTWARE/OPENGLES3 remain6PASS/
1expectedISSUE-152FAIL; exact status/check-count/knownIssue/message delta is empty on both.
All six SDL boundary audits pass with unchanged budgets. Native XML comparison improves
exactly three inherited failures to PASS and adds three PASS, with no regression.
One compiler throughout, shared cache,6GiB memory cap, private display. No new sanitizer
or non-Linux runtime claim for this scalar/error-control-flow change.

This checkpoint is the commit containing this entry, based on fffa552ad. Publish only
StorageContainer.cpp/.hpp, StorageDeviceTests.cpp and this handoff. Next: directory
deletion error translation, without losing existing UTF-8 native-path support; its
current std::filesystem::remove lets filesystem_error escape and silently accepts a
missing directory. Sharp Directory::Delete has the types but still uses narrow native
paths, so blindly routing CNA's already-widened path through it regresses Windows UTF-8.
Evidence prefix: demos build/linux-full/reports/repair-museum-storage-args-*.

### Previous checkpoint: museum B11 deferred storage root (2026-10-23)

Baseline CNA/origin `d7ed7592d8cf50f3d3ff33a391839c2744aeebac`, Sharp Runtime
`1dc8785b35284b5cf50f3aec361703bfaf090b72`. B13 publication was verified by ls-remote.
Confirmed eager call in SetAppNameEXT, including the isolated-storage override. Candidate
registers a lazy Sharp Runtime host policy; both CNA and System.IO-first access retain the
same per-game root. Android now asks the lower layer for its unmodified default path
without clearing/re-entering the override. A local lifetime guard unregisters the callback
before CNA unloads. Startup configuration remains explicitly nonconcurrent.

Dependency commit `31c7c08f1987a284ce1b84bf1c779ce236a6e1c6` adds the lazy policy/default-path
APIs, native tests and `docs/MuseumLazyStoragePolicy.md`. Published first, push76123 and
ls-remote verified; Sharp Runtime worktree clean. CNA callers require that dependency.
Sharp baseline build clean; initial test attempt lacked XNA fixture environment (five skips,
not accepted). With existing authentic fixtures and private original Yacht SOAP server:
full18,201/18,201,42executables,0failure/0skip; Storage6/6; IO.IsolatedStorage63/63.
Module boundary audit44modules/109edges unchanged and PASS. Actual StoragePaths.cpp plus
its six tests also pass ASan/UBSan/LSan, no suppressions, in existing Sharp build-probe
(compile99201,6/6). This is not whole-library or CNA instrumentation. No
Windows/Android/browser runtime pass is claimed for this batch.

CNA baseline34tests:29PASS/5FAIL. All five failures belong to inherited untracked
StorageContainerPathErrorsTests.cpp; do not stage, remove, suppress or overwrite that work.
New five lazy-root regressions all FAIL before the production change and all PASS after.
After39tests:34PASS/5sameFAIL, no previous outcome changed. Initial new test compilation
had three ignored-nodiscard warnings; corrected explicit void casts, repaired build clean.
Private display for native tests, one compiler, shared cache,6GiB memory cap/no swap.

Owned CNA files: StorageDevice.cpp, StorageDevice.hpp, StorageDeviceLazyRootTests.cpp and
this handoff only. Existing demos build/linux-full rebuild20190 completed0, clean.
The old selector demo pinned eager configuration failure; its before report shows the
expected new timing disagreement. Update preserves its exact disconnected-exception cause
and serialization checks at GetStorageRootEXT, adding a no-access setter assertion (12->13).
Corrected demo build42850 clean. SOFTWARE and OPENGLES3 each6PASS/1unchangedISSUE-152FAIL;
no knownIssue changed or removed. All six SDL boundary/configure audits pass, budgets
unchanged; native XML comparison preserves all34 previous outcomes and adds5PASS.

This checkpoint is the commit containing this entry, based on d7ed7592d. Next: publish
CNA, commit only the matching selector demo and dedicated evidence note in cna-demos,
then address remaining StorageContainer argument/IO error mappings. Genuine existing
StoragePathErrorsOracle.cs reference has29/29PASS against the XNA4 GAC assembly; its
inherited untracked native test remains untouched/uncommitted. Do not mistake this
subfinding for closure of all museum storage findings or any historical CNA-DEMOS ID.
Evidence prefix: demos build/linux-full/reports/repair-museum-storage-*.

### Previous checkpoint: museum B13 tray-capability contract (2026-10-23)

B14 native flag repair is published as
`aff773b3bd99a02154d0ace2543c9c65bd32f7f8` (push75043, remote verified).
The museum explicitly offered either a watcher probe or an accurate capability contract
for B13. Source audit confirms this getter intentionally lives in SystemTraySupport.cpp
and links no native tray; Sdl3ServiceSupport.cpp reports desktop service availability,
while construction performs actual allocation and propagates PlatformException.
The prior public header described desktop support but omitted the host-dependent failure.
Disposition: DOCUMENTATION_DEFECT corrected, no runtime behavior change. Header and
devices guide now explicitly say the query is not a live StatusNotifier/D-Bus probe and
document constructor failure even when it is true. The museum's supplied no-host failure
is consistent with this contract; no new live tray runtime pass is claimed here.
Existing canned tests cover capability selection, no-service refusal and icon ownership;
they were inspected, not rerun (current multi tree has CNA_DEVICES=OFF). The preceding
platform regression525PASS/12SKIP is not relabeled as public SystemTray coverage.
Only two documentation files and this handoff belong to this batch. This checkpoint is
the commit containing this entry, based onaff773b3b. Next: StorageDevice.SetAppNameEXT
eager root creation, including the Sharp Runtime isolated-storage override; do not fix
only the CNA caller while losing per-game isolated-storage identity.

### Previous checkpoint: museum B14 native dialog keyboard defaults (2026-10-23)

B10 documentation/regression commit is published as
`376eb834b17b2a47fbb92afe50014ede0a78ba08` (push92404).
MessageBox is CNA::Devices, explicitly a CNA extension with no XNA equivalent.
Sdl3Dialogs passes every custom button with flags0, confirming the museum's source
finding. Candidate sets Return on first and Escape on last (both for one button),
and corrects stale MessageBox support/return documentation. No API signature changed.
Native opt-in platform tests plus tools/platform/run_message_box_tests_private.sh
exercise actual keys on a private X11 display, with hard timeout and no desktop input.
No additional SDL test-reference budget was needed; inventory regenerated (959 names),
classification and ratchet pass with unchanged budgets.

Test build1107 completed0, BEFORE production edits. Initial runs v1-v3 and diagnostic
v1/v2 are harness diagnostics, NOT valid behavior reproductions: Zenity chose Wayland,
then xdotool's title search could not decode the native toolkit WM_NAME (UTF-8 atom),
although xwininfo and _NET_WM_NAME saw the visible window. The first timeout's SIGTERM
was ignored; only its confirmed owned PID756455 was killed. All subsequent runs have
timeout -k2s. Current harness uses xev substructure events plus xwininfo for discovery,
then one focused XTest key event. Before-v4 process83054 confirms injected Return
reaches the visible native dialog but selects nothing and hits the hard timeout137.
Build20071 completed0 (61steps,one compiler,6GiB cap). The same harness after repair
passes4/4 with all four injections logged. Full platform run63720 passes525/537;
12 skips consist of the4 opt-in keyboard tests (separately4PASS) and8 hardware/platform
cases. No failures. Production defaults/docs are still uncommitted.
Evidence prefix: demos build/linux-full/reports/repair-museum-dialog-*.

Separate DEPENDENCY_DEFECT: SDL's optional Zenity implementation ignores keyboard-default
flags entirely (third_party/SDL/src/dialog/unix/SDL_zenitymessagebox.c). The native test
deliberately limits the test child's PATH to the build directory to select X11, not
Zenity; it must not be described as Zenity validation. Public docs disclose this residual.
Do not claim all B14 paths fixed or modify the shared prebuilt SDL in place. Need a
separate correct-layer dependency decision/patch and independent validation afterward.
An additional non-production configure audit caught B10's direct SDL_VIDEODRIVER string
in a platform-neutral test. Corrected to CNA_TEST_OFFSCREEN_WINDOW, leaving the native
driver choice only in the launcher; no manifest ceiling change. Actual configure/build
77173 passed all audits. Corrected selection1006 passes51/51, real offscreen1/1.
The two-file B10 correction is a47766886, published separately. This B14 native/doc
repair is the commit containing this entry. No owned test/build process remains live.
Native coverage is Linux X11; no Zenity, real Windows, Apple or sanitizer pass claimed.
Museum B14's CNA flag-forwarding defect is fixed; the optional provider limitation is
explicit, not hidden behind these passes. Next: review the remaining museum platform
contracts (tray-support reporting and storage root laziness), then the content queue.
Preserve all inherited content/audio/occlusion work.

### Previous checkpoint: museum B10 surface-free WebGPU audit (2026-10-23)

FNA3D repair below is published/remote-verified as
`932b04dec06373588088c034ef87c761aeff0b5b` (push60183).
B10's preferred surface-free rendering path is not implemented, but its alternative
explicit fallback ALREADY WORKS. GraphicsDevice::resolveRenderer catches native surface
failure and records InitializationFailed before trying the opted-in next candidate.
The museum's Main.cpp explicitly disables automatic fallback when a renderer is named;
that explains its strict creation failure, not a missing fallback mechanism.
GetAvailable/IsAvailable already document compiled identities, not runtime support.
Disposition: INTENTIONALLY_UNSUPPORTED surface-free WebGPU with DOCUMENTATION_DEFECT
corrected; runtime substitution VERIFIED, no runtime-code change. Museum retains its
own final entry/revalidation decision; its source/pin are untouched.

New native fallback regression uses no debug failure override and confirms the selected
WebGPU/activeSoftware identities, exact failure history and CornflowerBlue pixels.
It passes both actual SDL_VIDEODRIVER=offscreen (missing native window Headless) and
HeadlessEXT (missing platform window id). Initial test build86419 failed on a span
initializer; corrected to the existing vector-to-span convention; build10506 passes.
Evidence: demos build/linux-full/reports/repair-museum-webgpu-offscreen-*.
Broader renderer-selection regression99918 passes51/51. Docs now state the native surface
requirement and show opt-in fallback. Only the new regression, WebGPU docs/plan and
this handoff belong to this batch (commit containing this entry, based on932b04dec).
No owned process is live. No full-corpus or sanitizer claim.
Next: inspect museum B14 MessageBox Return/Escape defaults against the platform/FNA contract.

### Previous checkpoint: FNA3D Single-format verdict (2026-10-23)

Volume repair below is published and remote-verified as
`c105303dc7520df0483f477f43e9545a204cc716` (push23524).
The B8 cross-control revealed a pre-existing CNA FNA3D gate defect: its underlying
storage/transfer/enum mapping supports Single, but ClassifySurfaceFormatEXT returns
Defer and the shared gate refuses SurfaceFormat13. Local FNA Texture2D reference
accepts that format; the XNA HiDef vertex fixture uses a four-channel texture but
its height read uses only red, so the same asserted graph applies to Single.
Three new tests cover vertex sampling, exact float mip/rectangle transfer (including
negative and >1 values), and all six cube faces/three mips. Cube needs explicit
coverage because its default verdict delegates to the 2D verdict. All3 fail before
the production change (before-v2,exit1) with the same format refusal. Test builds
41242/64659 completed0. The production fix is one Supported case; no vendor change.
Affected build73341 completed0. SDL_GPU/Vulkan45181 and OpenGL72212 each pass
114/115 with the unchanged Reach-volume skip. However public-format control21442
FAILS two existing assertions: Single's missing G/B sample as zero, where the XNA
oracle expects one, for both a sprite and environment-map cube. Do NOT publish the
gate-only change. This is precisely why the whole promoted format contract matters.
The follow-up adapter fix uses its existing NormalizedByte2 strategy: FNA3D has no
texture-component swizzle API, so ordinary Single 2D/cube storage expands to native
Vector4, fills missing channels with one and contracts readback without changing red
bits, logical format or transfer sizes. This costs4x native storage for those textures;
it does not quantize and does not alter vendor/native renderer drivers. Transfer byte
expansion is overflow-checked. Build50944 completed0. Final SDL_GPU/Vulkan66278 and
OpenGL3200 each pass114/115 renderer tests (same Reach-volumeSKIP) and19/25 public
format tests (six identical capabilitySKIP, no failures). Both previously failing
missing-channel checks now pass without assertion changes. XML comparison preserves
all112 original renderer outcomes and adds3PASS. All boundary gates/diff checks pass.
This batch is the commit containing this entry, based on c105303dc. No owned process
remains live. No new sanitizer/Windows/Apple/full-CNA-corpus pass is claimed.
Next: reproduce museum B10 (surface-free WebGPU) and inspect availability/fallback
policy before deciding between support and an explicit unavailable result.
Only FNA renderer source/tests, relevant
docs and this handoff belong to this batch. Evidence prefix:
demos build/linux-full/reports/repair-museum-fna-single-*.

### Previous checkpoint: WebGPU deferred volume lifetime (2026-10-23)

Vertex-texture repair below is published as
`e8bfdf2c5584bb0f8f6f4b7c6d84c1c32336fdf9`; push16173 and ls-remote confirm it.
Follow-up source suspicion is now reproduced, not merely speculative:
WebGPUCompiledEffectWgslDrawTest.VolumeTextureOutlivesItsDeferredDraw constructs
a two-slice volume, issues a draw, disposes/destroys the volume and Effect, then
flushes/readbacks. Pre-fix run26818 aborts134 in wgpuDeviceCreateBindGroup with
`TextureView[Id(3,2)] does not exist`. Test build22757 completed0.
Evidence prefix: demos build/linux-full/reports/repair-museum-volume-lifetime-*.
The resolver used a bare SampledView despite an existing owning SampledEXT helper.
The narrow fix returns that helper's texture/view lease; no new ownership system.
Native fix build89313 completed0. Expanded native WebGPU/translator run93013:
78tests,77PASS/1unchanged cube/volumeSKIP, including the new strict disposal test.
Browser page retains all19 earlier checks and adds near/far disposal checks21total.
Browser build77738 completed0 (existing wasm tree, scope cna-volume-browser-build,
one compiler/shared ccache/6GiB cap). Private Chrome run97432 completed0:
21/21PASS,120frames,clean teardown,no uncaptured/validation errors. Both new disposal
checks pass; all19 prior assertions remain and pass. The native regression is now
parameterized over SPIR-V and WGSL (WebGPUShaderRoutes); rebuild80767 completed0.
Final native run87152:79tests,78PASS/1same Reach-volumeSKIP; both new routes pass,
no uncaptured errors. This batch is the commit containing this entry, based on e8bfdf2c5.
No new sanitizer result: the demonstrated failure is a stale WebGPU resource handle,
detected directly by wgpu validation, not a claimed host-heap sanitizer diagnosis.
Only renderer source, native test, browser page and this handoff belong to this batch.
All owned processes are terminal. No museum/engine/demos source or consumer pin changed.
FNA3D baseline76366 completed0 meanwhile:112tests,111PASS/1Reach-volumeSKIP.
Its Single-format promotion audit is next; no FNA production changes yet.

### Previous checkpoint: WebGPU HiDef vertex-texture repair (2026-10-23)

SDL_GPU batch below is committed and published as
`079dfc55eef2c2cbf62efbb99f3dcd154281e39f`; normal push97006 and ls-remote verified.
WebGPU has four independent missing links: the Single/Vector4 ordinary-2D format
verdict, explicit vertex-sampler refusal, absent group0 capture/layout/binding, and
the browser WGSL translator refusing OpImageSampleExplicitLod (TEXLDL).
Six new native/WGSL-route pixel tests use the shared SDL_GPU/FNA oracle and
deferred-disposal case. Test build94724 completed0; webgpu-new-before-v1 records
all6FAIL before repair. The implementation now captures/binds both stages, makes
point samplers explicitly non-filtering (including layout/pipeline/cache identity),
accepts Single/Vector4 ordinary2D storage but preserves the float-cube refusal,
and translates explicit Lod to textureSampleLevel while still rejecting an invalid
Lod operand on implicit sampling. Native build86824 passed; run70138 passed62/63
with the identical existing cube/volume skip. The separate MojoShader sampler-bias
unit suite passes2/2, including both the new valid and preserved invalid operand case.

Browser example now adds the same Single/Vector4 clamp/wrap graph checks and
deferred disposal, using the genuine preloaded fixture and offscreen800x480 targets.
It requests HiDef explicitly. Its CMake target preloads only that972-byte fixture.
Wasm configure12590 passed, enabling only CNA_WEBGPU_COMPILED_EFFECTS in the existing
tree; every configure audit passes. Browser build54840 completed0 (scope
cna-vertex-browser-build,471steps,one compiler,6GiB cap,shared ccache).
Private-display Chrome runtime85908 completed0, explicitly19/19PASS, no uncaptured
or validation errors,120frames and clean teardown. The harness's teardown-only
success check was supplemented by checking the exact page count and every assertion.
Evidence: repair-museum-vertex-texture-browser-{configure,build,runtime}-v1.log.
Native broader run52515 passed75/76, same1SKIP,
with no uncaptured/validation error. XML comparison preserves all57 earlier WebGPU
outcomes; the first6new cases pass. Final native build49547 completed0. Final WebGPU
run78443 passes76/77, same1SKIP, including the new
PointVertexTexturesDoNotRequireFloat32Filterable test. The existing
DebugForceFloat32FeaturesAbsentEXT seam tests CNA policy, not a real feature-less adapter.
SDL_GPU cross-control27611 passes80/81, same shadercrossSKIP. An initial combined
invocation11957 incorrectly selected default OPENGLES3 for renderer-specific tests;
it failed and is retained as combined-final-v1, not counted as product validation.
Corrected separate runs explicitly select WEBGPU and SDL_GPU, without source changes.
The standalone WebGPU-format CTest regex selected no tests in this multi-renderer
configuration: that invocation is NOT coverage. Rebuilt public Graphics test run20957
supplies replacement format/readback validation:25tests,23PASS/2existing capabilitySKIP
(public-formats-final-v1 XML/log). All five platform boundary gates and diff checks pass.
No full CNA corpus, new sanitizer run,
real Windows or Apple result is claimed for this batch. WebGPU edits belong to the
commit containing this entry, based on079dfc55e; SDL_GPU079dfc55e is already published.
Next: audit the deferred volume lifetime observation below and FNA3D Single refusal.

Follow-up audit observation, not a claimed runtime reproduction: the compiled
Texture3D resolver still returns SampledView without a lease even though the volume
renderer already offers SampledEXT with ownership. The new B8 regressions exercise
Texture2D. Reproduce deferred volume disposal before fixing that separate lifetime
path; do not describe the B8 tests as full Texture3D lifetime coverage.

### Previous checkpoint: SDL_GPU HiDef vertex-texture repair validated (2026-10-23)

Avatar lifetime repair is published and remote-verified as
`2b39eb9341791b711008205d0d70f89bb9e4e799`. Sharp Runtime remains1dc8785b,
demos1295899b and museumce761bea. No consumer pin or source changed.

Museum B8 was confirmed: SDL_GPU explicitly rejected reflected vertex samplers,
while WebGPU's ordinary texture-format verdict omits Single/Vector4 despite storage
mappings for them. WebGPU separately rejects reflected vertex samplers as well.
Existing Fna3dCompiledEffectTest.VertexTextureAndSamplerDeviceCollectionsReachTheDraw
uses the genuine XNA vertex-texture fixture and only public graphics APIs. Its
unchanged baseline passed on FNA3D (SDL_GPU/Vulkan driver), failed on WEBGPU with
SurfaceFormat15 refusal and failed on SDL_GPU with explicit vertex-sampler refusal.
The full SDL_GPU compiled-effect baseline completed:59 tests,58PASS/1declaredSKIP
(optional shadercross OFF). Logs/XML use
`repair-museum-vertex-texture-*-before-v1` under the demos reports directory.

Coverage correction discovered during this audit: cmake-build-museum-asan retained
the seven renderers and museum feature switches, but its four optional compiled-
effect switches defaulted OFF, unlike the original museum build-probe (all ON).
Do not describe that avatar run as an identical full configuration. It covers the
stock avatar path/SkinnedEffect and mutex repair, not compiled-effect execution.
cmake-build-multi has WEBGPU/SDL_GPU compiled effects ON and EasyGL/SOFTWARE OFF;
FNA3D supplies the existing working fixture reference. This new shader baseline
must not use the ASan consumer's unsupported compiled-effect path as evidence.

SDL_GPU now resolves both stages through the existing lifetime-owning sampler
capture, but reads the independent current public vertex slots after Apply. The
native vertex binding replaces dummy-only bindings; unreflected sparse slots still
receive valid dummy textures. Pixel-stage behavior is unchanged. No public API,
fixture, demo expectation, HiDef request or knownIssue was weakened.

Native tests add Single/Vector4 geometry checks with fragment slot2 fixed (stage
independence), plus disposal of texture/effect after issuing the last draw and
before readback. Both format checks failed before production changes (new-before-v1
XML/log, exit1). The original unchanged FNA fixture also transitions FAIL to PASS
on SDL_GPU. Final expanded native run40505:82 tests,81PASS/1unchanged shadercross
SKIP, including all SDL_GPU renderer unit tests and the unchanged fixture.
The v1 narrower after-run was62 tests,61PASS/1sameSKIP. Build95012 initially found
a private TextureCollection count accessor; corrected to the existing public
vertex sampler count, builds9592/36129 exit0. Boundary inventory/classification,
renderer SDL audit, ratchet, hot-path lint and diff checks all pass.

Cross-renderer controls6813:3PASS/1FAIL on FNA3D. Vector4, the original fixture and
deferred disposal pass. **New pre-existing gap:** FNA3D refuses ordinary Single
textures (SurfaceFormat13); none of its implementation changed in this batch.
Do not call this control sweep all-green or turn its assertion into a skip. Audit
that format verdict after the two museum-requested renderer repairs. The independent
WebGPU baseline71835:57 tests,56PASS/1existing cube/volumeSKIP (WGSL path included).

The broad incremental CNA build50950 included1790 stale/unbuilt targets. It was
intentionally interrupted at137 (exit130, no source error) to avoid spending this
focused batch rebuilding unrelated subsystems. Its completed cache/artifacts remain.
**No full CNA build/test corpus pass is claimed.** Focused renderer/shared-library
builds passed. External demos rebuild22957 completed0 with compiled effects OFF;
before/after public-binding demo results are exactly unchanged except elapsed time:
4PASS each on SOFTWARE,OPENGLES3,SDL_GPU, no knownIssue. This is a regression control,
not evidence that it executes a vertex shader. Final affected build23741 completed0;
final native run56782 again81PASS/1sameSKIP. XML comparison preserves all59 original
outcomes. SDL_GPU-specific CTest33654 passed10/10. No owned build/test remains live.

All evidence is under demos build/linux-full/reports/repair-museum-vertex-texture-*.
The repair is the commit containing this entry, based on2b39eb934. Stage only the
seven SDL_GPU/shared-test/documentation files, not inherited work. Next: publish
this validated SDL_GPU-only repair with a normal fast-forward, then implement WebGPU vertex
group0 and float-format support with the same oracle. Its float cube transfer code
still assumes4-byte texels, so do not accidentally enable float cubes by changing
the shared 2D verdict. WebGPU sampling layouts must distinguish non-filtering
float32 vertex samplers without requiring Float32Filterable. No sanitizer, browser,
Windows or Apple runtime pass is claimed for this SDL_GPU batch yet. Inherited
content/audio/occlusion work remains untouched. Builds use one compiler, shared
ccache and a6GiB scope limit. The demos156-ID/40-unfinalized census is unchanged.

### Previous checkpoint: avatar load lifetime repair validated (2026-10-23)

GLES float readback repair below is committed and published as
`53292f1472e2681ce9f75e4fe53168ba928a000c`. The current investigation is museum B3,
the single heap-corruption crash reported at a6f9e9b2e. A definite AvatarLoad mutex
lifetime defect was reproduced and the narrow repair is validated below;
its relationship to that historical crash is not established. Museum source remains
clean at ce761beaa9189a68fa23f241acca80da6668675b;
its source, pin and cna-next worktree are not changed.

Persistent CNA-owned consumer tree `cmake-build-museum-asan` configured successfully
from read-only `../museum-simulator`, with CNA_ROOT_DIR pointing to this CNA tree.
It retains the original build-probe's seven renderers (default OPENGLES3), shared
CNA, coverage, C API, networking, inspector, pipeline and automatic video support.
Debug/address,undefined/O0 instruments the museum, CNA and Sharp Runtime: verified
in compile_commands.json for AvatarLab, Main, AvatarRenderer, EasyGLRenderer and
Uri. Vendored prebuilt SDL and system Mesa are not instrumented; do not call this
whole-process coverage. Shared SDL and WebGPU packages are reused.

Configure process13727 and build process74215 completed0. The build used
scope `cna-museum-asan-build`, one compiler, MemoryHigh4G/MemoryMax6G/no swap, shared
CCACHE_DIR=/rv/cnaccache and CCACHE_BASEDIR=/rv. Logs are in demos build/linux-full/reports/
`repair-museum-avatar-asan-{configure,build}-v1.log`. Repaired AvatarRenderer compiled
at step764. Native test/shared-library rebuild52458 also completed0 (six steps).
Inherited content/audio/occlusion edits remain present and must not be staged.

Source audit identified a definite lifetime defect in AvatarRenderer::Poll:
resources_->load.reset() can free the AvatarLoad while lock_guard still owns its
mutex. A cached model completes synchronously, so no worker reference prevents
this. Existing native test ALoadCompletingInsideDrawWaitsUntilTheCallerCanUpdateItsAnimation
confirmed it under gdb: __libc_free of the shared allocation precedes
pthread_mutex_unlock from Poll's lock_guard destructor. Diagnostic10119 exited1,
log `repair-museum-avatar-mutex-before-v1.log` contains the exact stack. This is
concrete CNA UAF evidence, not proof of the historical Mesa crash's cause.
New debugger-only tools/graphics/avatar_load_lifetime.gdb observes actual free and
unlock calls on Linux x86-64/libstdc++; a new cached-completion native regression
is prepared. Refined observer v2 also fails with the same free-before-unlock stack
(process33931 terminal1); it restricts unlocks to the Poll thread and drops the
watched address after unlock, avoiding unrelated worker unlocks/address reuse.
The native pre-repair binary is the red baseline; the rare full museum crash has
not been reproduced. Poll now scopes the lock before resetting its owning result.
This source had not yet compiled in build74215 when edited, so that consumer build
will contain the candidate fix. It is not a full museum before/after comparison.
At that checkpoint no native rebuild or post-repair runtime check had run;
completed validation is recorded below.

Pre-repair full native GamerServices baseline completed:709 tests from88 suites,
708PASS/1 unchanged declaredSKIP, no failures,79.049s. Private OPENGLES3 run17335;
`repair-museum-avatar-native-before-v1.{log,xml}`. The pending new test is not in
that old binary. Existing external C++ render/custom_animation and C avatar_renderer
baselines completed on SOFTWARE/OPENGLES3 before rebuilding demos: render9/9,
custom_animation3/3, C avatar_renderer41/41 each (C outer JSON reports0 checks;
child log carries41). Six results are PASSED with empty knownIssue; process28914
terminal0. Logs/JSON: `repair-museum-avatar-demo-*-before-v1.*`.

Instrumented museum runtime53653 completed0: exact script
`goto:gs-avatar;wait:20;pressall;wait:4;stats;quit`, OPENGLES3 on a private display,
SDL dummy audio, --no-save, ASan/UBSan halt-on-error and leak detection enabled.
No sanitizer report; frame27,27610 triangles,45 textures,zero exhibit/script errors.
Log `repair-museum-avatar-asan-runtime-v1.log`. ldd verifies this uses the new
instrumented CNA/C API libraries and libasan/libubsan, not a different build.
SDL/Mesa remain uninstrumented. This one clean run is not proof that the historical
intermittent crash is eliminated; its actual cause remains unconfirmed.

New native cached-completion regression under debugger99211 passed,232 matching
unlocks before allocation release (`repair-museum-avatar-mutex-after-v1.log`).
The exact pre-repair existing test also passed after repair: observer9312 terminal0,
286 matching unlocks (`repair-museum-avatar-mutex-after-original-v1.log`), a direct
red-to-green comparison. Full native suite78530 completed0:710 tests,709PASS and
the identical one declaredSKIP. XML comparison preserves every original709 result,
no removed/changed outcomes, only the new regression PASS;79.507s. Demos rebuild56611
completed0. Native SOFTWARE avatar subset passed33/34 with the same one declared
skip. All six external after-cases passed (process18368 terminal0): render9/9,
custom_animation3/3,C avatar_renderer41/41 on both renderers. Exact JSON comparison,
excluding only elapsed seconds, preserves all six outcomes/check counts/knownIssue
fields. MinGW implementation and test translation units also compile (33091 exit0,
`repair-museum-avatar-mingw-compile-v1.log`); no Windows runtime claim.

All owned build/test processes are terminal. Build-policy and diff checks pass.
This coherent repair contains only AvatarRenderer.cpp, AvatarRendererTests.cpp,
the debugger observer/README and this handoff. Its base is53292f147; the repair SHA
is the commit containing this entry. Inherited content/audio/occlusion edits are
preserved, not part of the commit. Museum source/pin remains unchanged. No full
CNA corpus, TSan, browser or physical-platform validation is claimed for this batch.
The156-ID demos census remains40 without final disposition: this separate avatar
lifetime repair does not silently close any historical demo issue.

**Exact next task:** reproduce museum B8's HiDef vertex texture fetch on WEBGPU
and SDL_GPU, inspect format support and compiled-effect stage bindings, and repair
the actual missing behavior with pixel regressions. Do not weaken the museum's
vertex-sampler checks or silently downgrade its HiDef request. Keep B3's historical
rare crash unconfirmed unless its actual failure is reproduced; send the owner this
separate lifetime repair for museum revalidation.

### Previous checkpoint: GLES float readback repair validated (2026-10-23)

Candidate now adds one GLES-only branch in EasyGLRenderer.cpp: RGBA/FLOAT transfer
for half or reduced-channel float targets, then existing HalfTypeHelper packing/
channel selection. Desktop GL and full RGBA32F direct read are unchanged. No
capability was disabled and no expected value relaxed.

New permanent `EasyGLFloatReadbackTests.cpp` observes native GL calls through the
existing platform loader seam (not a new production test hook). Its first attempt
observed the executable's separate private meta-gl table, so v1 calls=0 was a test
instrumentation failure, not root-cause evidence. Corrected v2 observes14 calls
and fails only the required RGBA/FLOAT pair on original code. After the repair it
passes all14 reads:7 formats,2D/cube,odd-width subrectangle,negative/>1 values and
sentinel bounds. Unsupported formats are not asserted to exist on other hardware.

Native build PASS. Existing HDR suite16/16 and format-agreement suites12/12 PASS
each on SOFTWARE/OPENGLES3/OPENGL33 (the first filter matched only HDR; separate
format runs cover the actual suite names). Full raw renderer baseline293 has
109PASS/170SKIP/14FAIL: the14 SDL_GPU-specific tests were run under OPENGLES3 and
explicitly refuse that selection. Proper SDL_GPU run59=58PASS/1 declared skip.
Raw after294 has exactly the same293 outcomes plus the new PASS. This is not a
claim that the raw incorrectly-selected run was green. All logs/XML are under the
existing `repair-museum-half-readback-*` reports prefix.

External original HDR demo baseline passed4/4 on SOFTWARE/OPENGLES3/OPENGL33.
Actual changed EasyGL translation unit cross-compiles with NDK29, using the
existing Android consumer's compile metadata read-only. Its object was redirected
to CNA cmake-build-multi/EasyGLRenderer.android-readback.o; no museum file written.
Log: `repair-museum-half-readback-android-compile-v1.log`.

Portable diagnostic passed natively and in the private Android guest:
`repair-museum-half-readback-{native,android}-portable-v1.log`. Original HALF_FLOAT
still prints FAIL; RGBA/FLOAT and existing CNA HalfTypeHelper conversion print
PASS, with exact {-2,0.5,2,1} and untouched sentinels. Default diagnostic mode still
requires both raw transfers; --portable explicitly tests only the portable route,
not a reclassification of the original failure. README documents this boundary.
This is substrate/conversion coverage, not a full Android CNA/museum runtime pass.
The after emulator was stopped normally; session42823 terminal0, peak5370978304
bytes, no live guest. Native observer's final no-GL-platform guard also builds and
passes (`repair-museum-half-readback-focused-final-v1.log`).

External build58597 completed0 (`repair-museum-half-readback-demo-build-v1.log`).
Unchanged HDR demo4/4, C render_target12/12 and C texture_formats6/6 PASS each on
SOFTWARE/OPENGLES3/OPENGL33. Exact HDR JSON comparison excludes timing only and
matches the original baseline. All three800x480 screenshots were inspected: HDR
keeps the three distinct additive levels and labels, while Color saturates as
expected. ImageMagick AE comparison SOFTWARE vs GLES and vs desktop GL is0 pixels
each. These are cross-renderer comparisons, not a full historical visual-reference
sweep. Report prefix remains `repair-museum-half-readback-*`; C wrapper JSON has
zero outer checks, child logs carry12/6 respectively. Diff/policy PASS.

All processes are terminal; no live emulator or build. No full CNA suite, WebGL
browser, sanitizer or physical-device coverage claimed. Museum application
revalidation is pending, not silently counted as a pass. This commit contains only
renderer cpp,new renderer test,driver diagnostic,its README,Android documentation
and this handoff. Inherited content/audio/occlusion work remains untouched.

**Exact next task:** investigate B3's intermittent gs-avatar heap crash with a
targeted ASan reproduction. First inspect museum build rules and existing sanitizer
artifacts read-only; its source/pin/worktree are owned by its agent. Use an isolated
stable CNA-owned consumer build if a new instrumented build is needed, one compiler
and a hard memory limit, and the private display. Do not infer a Mesa defect from
the malloc stack. Existing CNA build-asan is SOFTWARE-only, not an OpenGLES3 proof.
No remaining approval blocker; continue other actionable findings if a necessary
external reproduction is unavailable. The156-ID demos matrix still has40 without
final disposition; this museum-only fix closes none of them.

Profile repair is published and remote-verified as
**ca8633622f237d40a46a3535be1dbd6718e4ea9f**; details below. No consumer pin changed.
New uncommitted source: `tools/graphics/half_float_readback_probe.cpp`, a driver-only
EGL/GLES3 diagnostic using EasyGL's RGBA16F/RGBA/HALF_FLOAT triple. It clears four
distinct exactly representable values and compares half/float reads plus sentinel
bounds. Native private Xwayland control PASS on AMD/Mesa25.0.7; Android NDK29
x86_64 compile PASS after linking GLESv3 (GLESv2 lacks the ES3 symbols).
Artifacts in existing `cmake-build-multi/half-float-readback-probe{,-android}`.
Evidence prefix: demos `build/linux-full/reports/repair-museum-half-readback-*`.

Private read-only Medium_Phone emulator on port5594, user scope
`cna-half-readback-5594.scope`, completed and was shut down normally with the
serial-specific emu kill; tool session85899 is terminal0. No emulator/compile job
remains. It had no window/audio/snapshots, guest GLES,2cores,3072MiB guest memory,
6GiB cgroup hard limit/no swap; peak5371133952 bytes. Shared AVD was not altered.

**Reproduced:** Google SwiftShader4.0.0.1 GLES3.0 reports complete RGBA16F framebuffer
and no allocation/clear errors. Its preferred read type is GL_FLOAT (0x1406).
Reading HALF_FLOAT yields wrong values with GL_NO_ERROR; reading RGBA/FLOAT yields
exactly {-2,0.5,2,1}, with intact bounds sentinels. Native Mesa passes both reads.
Reports: `repair-museum-half-readback-{native,android}-before-v1.log`; emulator
startup log retained separately. These are driver/substrate tests, not an Android
CNA or museum application runtime pass. No production renderer change yet.
Museum source currently uses HdrBlendable plus HalfVector4 readback and a HiDef
project; its historical finding calls the target HalfVector4.

**Exact next task:** implement and validate the GLES-safe float readback route,
using RGBA/FLOAT transfer and selecting/packing the target's own channels/half
elements with existing PackedVector::HalfTypeHelper. Do not blindly copy the upload
pixel type to ReadPixels. Keep desktop GL behavior intentional and cover Single,
Vector2, HalfSingle, HalfVector2, HalfVector4/HdrBlendable, cube/2D, odd widths,
subrectangles, bounds and negative/>1 values. Preserve existing exact assertions.
Official ES3 reference accepts RGBA/FLOAT for float rendering surfaces; HALF_FLOAT
is not universally guaranteed, only the implementation-selected pair is additional:
https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/es3.0/glReadPixels.xml
Use that contract, not an Android-specific driver-name workaround. Native regression
coverage exists in HdrRenderTargetRoundTripTests/RenderTargetFormatAgreementTests.
Require a regression that guards the selected transfer route as well as conversion,
then emulator and consumer evidence before commit. The new diagnostic source and
this handoff are the only current campaign-owned uncommitted files.

### Completed checkpoint: two-account profile repair validated

Base CNA **3cd6028b7ad30a22251349e12db32df9f07d5e88** is published. Sharp
**1dc8785b35284b5cf50f3aec361703bfaf090b72** was rechecked with ls-remote and
contains c26614e8: UP-020 remains resolved by dependency publication. Demos stays
at1295899bbaa2d657348a115b831f31b928d2df6c.

The new production-loopback regression
`ServiceRequestRetryTest.LocalProfileUsesItsOwnAccountAndNeverBorrowsAnotherAfterSignOut`
reproduced the defect in11ms: Bob's healthy read used Alice's token; his read
queued after logout also used Alice instead of failing. The request count proves
the second read reached the loopback service. No external service/credentials used.
Evidence in `../cna-demos/build/linux-full/reports/`:
`repair-museum-profile-actor-red-{build,tests}-v1.log`.

Candidate captures a local SignedInGamer's service identity before queueing and
passes it to the internal profile operation. OnlineBackend no longer falls back
to another slot for that explicit actor. FakeBackend rejects a signed-out explicit
actor too. Remote Gamer lookups (including objects with a service user ID), guests
and offline identities retain the default lookup route. Worker never dereferences
the Gamer. Both production-loopback and fake-backend tests retain the remote-read
control and the separate disposed-object contract.

One-worker memory-limited native rebuild passed, log
`repair-museum-profile-actor-after-build-v1.log`; existing unrelated nodiscard
warnings only. Focused4/4 PASS (`repair-museum-profile-actor-focused-v1.log`). Full
suite **709 =708 PASS/1 unchanged SKIP**,74364ms; exact XML comparison to the707
checkpoint found no old outcome changes/missing tests and exactly two new PASS.
Log/XML: `repair-museum-profile-actor-native-after-v1.{log,xml}`. Native shared
library rebuild also passed (`repair-museum-profile-actor-shared-build-v1.log`).
Windows cross-compilation of the three changed production units passed
(`repair-museum-profile-actor-mingw-v1.log`); no Windows runtime coverage claimed.
External consumer build passed (`repair-museum-profile-actor-demo-build-v1.log`).
Unchanged C++ get_profile is9/9 PASS and C profile_identity is19/19 PASS on both
SOFTWARE and OPENGLES3, matching the prior checkpoint. The C wrapper's JSON has
zero outer checks; its child log contains19. Logs/JSON:
`repair-museum-profile-actor-{demo,c-demo}-after-{SOFTWARE,OPENGLES3}-v1.*`.
Diff/build-performance policy PASS. No public API/test expectations changed.
Only the six gamer-services files and this handoff belong to this commit.
Inherited content/audio/occlusion work remains untouched. No new CNA-DEMOS ID or
disposition changes. No full CNA/sanitizer sweep or live online service validation
claimed. Museum reproduction/revalidation remains its agent's responsibility.

**Exact next task:** investigate B5 Android half-float render-target readback:
verify the museum's actual format/data element types, inspect ReadRenderTargetPixels
and context capabilities, then reproduce before choosing conversion vs capability
refusal. No renderer edits yet. B3's13 non-reproducing avatar reruns remain recorded;
the existing CNA build-asan is SOFTWARE-only (address,undefined/O0), not an
OPENGLES3 sanitizer reproducer. Do not claim it reproduced or ruled out that crash.
All profile-batch processes are terminal. Commit and normal fast-forward publication
are authorized; no unrelated work may be included.

### Previous checkpoint: profile sign-out exception fixed

Notifications are published through **3c61c30ef00801b1ae99cab867ec30c57c9a94d7**,
verified directly on origin/next. No museum repository or pin was changed.

Two native regressions accompany the repair in
`modules/gamer-services/tests/Microsoft/Xna/Framework/GamerServices/ServiceRequestRetryTests.cpp`:
`ProfileQueuedAfterSignOutReportsTheXnaPrivilegeFailureWithoutSending` and
`ProfileAuthenticationRefusalIsMappedButOtherServiceErrorsArePreserved`.
It uses the existing loopback service and the real production worker queue;
logout precedes the profile job deterministically, independent of game-thread
event publication. It checks the exception family and that profile.get was never
sent. This is not online/service validation and uses no extra sleep/retry.
The runtime repair changes only GamerServicesBackend.cpp and GamerRequests.cpp:
tokenFor preserves a typed ServiceOperationError/UNAUTHENTICATED (still an internal
service exception); EndGetProfile maps that specific reason to GamerPrivilegeException.
Other service/transport errors retain their existing family. A second public-API
test covers server authentication refusal and a non-authentication error control.

Source reference: sibling `xna4-decomp/reference/xna4/decompiled/windows/`
`Microsoft.Xna.Framework.GamerServices/Microsoft.Xna.Framework.GamerServices/`
`ErrorHandler.cs` maps ProfileNotSignedIn to **GamerPrivilegeException**. Gamer.cs
separately checks disposal at BeginGetProfile and throws ObjectDisposedException.
These are different states; do not replace all failures with a disposal exception.
Before that repair tokenFor threw a generic GamerServicesNotAvailableException.
The remaining default-account selection was subsequently reproduced above.

The build completed (v1 and public-API refinement v2). The new test reproduces
the exact museum message deterministically in13ms: EndGetProfile throws
GamerServicesNotAvailableException, not XNA's GamerPrivilegeException. The profile
request was not sent. Log: `repair-museum-profile-signout-red-tests-v1.log` under
the reports directory below. The test asserts the XNA contract at public
BeginGetProfile/EndGetProfile, not on the internal backend API.

Baseline completed: **705 tests =704 PASS/1 SKIP**, no failure (73742ms).
Candidate build and full module run passed: **707 =706 PASS/1 unchanged SKIP**
(74284ms). XML comparison confirms all705 old outcomes identical, exactly two new
PASS, no missing tests. Native shared library relink and both changed MinGW
translation units pass; Windows runtime not rerun. Log/XML under
`../cna-demos/build/linux-full/reports/`:
`repair-museum-profile-signout-native-{baseline,after}-v1.{log,xml}`.
Existing unrelated nodiscard test warnings are preserved. The original demos
consumer, before rebuilding, passes get_profile9/9 on SOFTWARE and OPENGLES3.

External consumer build passed (one compiler, shared cache, existing tree); log
`repair-museum-profile-signout-demo-build-v1.log` in that reports directory.
Rebuilt unchanged C++ get_profile is9/9 PASS on SOFTWARE and OPENGLES3, matching
both pre-rebuild results. Unchanged C profile_identity is19/19 PASS on both
selections (the wrapper JSON reports0 outer checks; child logs contain19).
Reports/logs: `repair-museum-profile-signout-{demo,c-demo}-after-{software,gles}-v1.*`.
The C adapter already maps GamerPrivilegeException to INVALID_STATE/STATE rather
than INTERNAL; that is source inspection, not a live online C API failure test.
Diff and build-performance policy PASS. No demo expectations changed; no full
CNA/demos or sanitizer sweep is claimed. All batch processes are terminal.

**Next task at that checkpoint:** reproduce and audit profile requester identity with two
signed-in accounts. The exception fix does not prove that a queued local-profile
read cannot borrow another account after logout: BeginGetProfile still captures
only gamertag and OnlineBackend::profile still calls tokenFor({}). Extend the
loopback fixture with distinct users/tokens and observe the actual outgoing actor
before deciding the smallest correct session/identity repair. Preserve normal
remote-gamer profile reads and guest/offline behavior; avoid reading a mutable
Gamer object from the worker or broadly rewriting all service APIs. The museum
finding remains pending this audit and museum revalidation. No approval blocker.

This batch is based on CNA3c61c30ef, Sharp1dc8785b and demos1295899b; the commit
containing this checkpoint carries the exception fix. Inherited content/audio/
occlusion changes remain uncommitted and untouched. The156-ID demos matrix still
has40 without final disposition, plus DC-R1/DC-R2; none is silently closed here.

### Completed notification checkpoint: museum revalidation pending

Sender repair **3078bb41e6cc50f50ec10caba675e63ebe2a64b0** is published on
origin/next (remote SHA verified). The companion failed-open repair is in the
commit containing this checkpoint, based on that revision. Sharp and demos
revisions remain `1dc8785b35284b5cf50f3aec361703bfaf090b72` and
`1295899bbaa2d657348a115b831f31b928d2df6c` respectively.

`HttpNotificationChannel` now queues ErrorOccurred/ChannelOpenFailed for failed
socket creation, bind/listen, address lookup and Windows event setup. It preserves
the operation's native error across cleanup, publishes no URI on failure, and
dispatches handlers outside the mutex on the caller thread. Queued errors survive
Close. Successful Open/ChannelUriUpdated and existing listener ownership remain
unchanged. The public channel layout changed: rebuild C++ consumers before running.

API names, enum values0..5, read-only property types, private construction, and the
HRESULT meaning of ErrorCode were verified from the official Windows Phone SDK
reference assembly/XML. See `tools/phone/REFERENCE.md` for provenance and hashes.
No SDK installer/service was executed. POSIX uses E_FAIL, Windows uses
HRESULT_FROM_WIN32; ErrorAdditionalData preserves the native errno/Winsock code.

Validation (2026-10-22, same bounded/private build policy as below):

- Before: all four deterministic failed-open CTests fail, because no public error
  event exists. After: **24/24 notification CTests PASS**, including descriptor
  release, preserved diagnostics, caller-thread/reentrant dispatch and successful
  reopening; existing native **phone18/18 PASS**.
- Actual Emscripten/Chrome: raw=false, toast=false; exactly one queued open error,
  no URI/registry entry, correct HRESULT/native diagnostic, no duplicate dispatch,
  exit0/no uncaught exception. No socket interposition in the browser probe.
- Demos consumer rebuilt after the class layout change; unchanged local_push
  **9/9 PASS** for SOFTWARE and OPENGLES3 selections. No assertion/knownIssue change.
- Native shared runtime builds; actual MinGW channel and event-args translation
  units compile. Native/Wasm/MinGW configure audits pass; only the existing Draco
  developer warning remains. Windows runtime and TSan/ASan not rerun in this batch.
- Diff whitespace and build-performance policy checks pass. Full CNA/demos
  sweeps were not rerun; focused results do not replace the previous full checkpoint.

Logs: `../cna-demos/build/linux-full/reports/repair-museum-notifications-open-*`:
red-tests-v1, after-build-v4, after-tests-v1, after-phone-v1,
browser-after-v1.json, mingw-v1, demo-build-v1, demo-{software,gles}-v1.json,
shared-build-v1. Earlier build attempts failed on alias qualification and an
inapplicable EventArgs GetTypeName override; both corrected, not hidden.
No source outside the phone repair and its build/test/docs files is included.
Inherited content/audio/occlusion work remains uncommitted and untouched.

The museum owns final revalidation and its inventory; do not edit that repository.
**Exact next task:** investigate the profile refresh/sign-out finding using
GamerRequests.cpp, ServiceAsyncResult.hpp and OnlineBackend's slot generation and
token lifetime. Current inspection shows tokenFor throws *before* issuing a
request when no token exists (not an unauthenticated request already sent).
BeginGetProfile checks disposal before scheduling, but captures only the gamertag;
OnlineBackend::profile later chooses tokenFor({}). Reproduce the queued-work versus
sign-out ordering deterministically, then establish the correct XNA exception and
actor ownership before repairing. Existing SystemGuideTests cover a call made
after sign-out/disposal, not this queued interval. Genuine XNA Gamer.cs confirms
BeginGetProfile's ObjectDisposedException guard. No gamer-services source was changed.
All batch processes are terminal. Fresh matrix census:156 IDs,40 without final
disposition, plus DC-R1/DC-R2. This museum repair does not close those demos IDs.

### Historical sender checkpoint: channel errors still open at that point

Sender repair prepared against CNA `cb377fa04057b061f1daafb45c906e7aa2b31379`,
Sharp Runtime `1dc8785b35284b5cf50f3aec361703bfaf090b72`, demos
`1295899bbaa2d657348a115b831f31b928d2df6c`. The sender now requires complete
final HTTP 2xx response headers instead of treating a local socket enqueue as
receiver acceptance. It handles short writes and interrupted syscalls, refuses
EOF/incomplete/malformed/non-success responses, and releases the connection on
every return. Public comments describe the existing synchronous bool contract.
No demo assertion, knownIssue or unrelated inherited change was altered.

Validation on 2026-10-22, one compiler at a time, shared ccache, existing build
trees and private test display, MemoryHigh=4G/MemoryMax=6G/MemorySwapMax=0:

- Original sender: deterministic transport suite **2/11 PASS, 9 FAIL**; failed
  responses incorrectly accepted, fragmented acknowledgement not fully read,
  short writes and interrupted calls incorrectly refused.
- Repaired sender: **20/20 CTests PASS** (15 sender cases, 5 existing lifecycle
  cases), including four extra short-status/upgrade/overlong/failed-send guards.
- Existing native `CnaPhoneTests`: **18/18 PASS**, including real raw/toast delivery.
- Real Emscripten/Chrome probe: raw and toast to the museum's unreachable
  endpoint both return false; exit0, no uncaught exception. No socket test seam
  is used in that browser executable.
- Existing demos consumer rebuilt; unchanged `phone.notification.local_push`
  **9/9 checks PASS** with SOFTWARE and OPENGLES3 selected. This console demo
  verifies transport/events, not renderer pixels.
- MinGW actual Winsock sender translation unit compiles. Windows runtime and
  retired Windows Phone service validation are not claimed.
- `git diff --check` and build-performance policy PASS. No new compiler warning;
  existing vendored Draco CMP0148 configure warning remains.

Evidence: `../cna-demos/build/linux-full/reports/repair-museum-notifications-*`:
`sender-red-tests-v1.log`, `sender-final-tests-v1.log`,
`sender-after-phone-v1.log`, `browser-after-v1.json`, `sender-mingw-v1.log`,
`demo-build-v1.log`, `demo-software-v1.json`, `demo-gles-v1.json`.
Permanent fixtures and commands: `tools/phone/README.md`.
This is focused regression evidence, not a new full CNA/demos/sanitizer sweep.

**Do not close the museum finding yet.** `HttpNotificationChannel.ErrorOccurred`
is still missing. Next: verify the Windows Phone error enum values and event
argument contract, add deterministic failed-open tests, implement reporting
without disturbing the repaired listener lifetime, and exercise a real browser
Open. No required external permission is pending. All batch processes are terminal.

### Historical notification intake

UP-001's installed C++ SDK is now published as
`58ebc46fd2613833cfbb273edb50328bae109405` (remote SHA verified). See
CNA_ENGINE_REPAIR_HANDOFF.md for shared/static relocation, native16/16 gates and
the Windows/WebAssembly configure-only limits. All batch processes are terminal.
Do not edit cna-engine, cna-extended, or the museum; their agents own revalidation.

Museum's current Open section was re-read at this checkpoint. Next actionable
batch: `HttpNotificationChannel fails silently where no socket can listen`.
Before the sender repair above, source inspection confirmed:

- `modules/phone/src/HttpNotificationChannel.cpp::Open` returns without a public
  error event on socket/bind/listen/getsockname or Windows event setup failure.
  Its header has ChannelUriUpdated and HttpNotificationReceived only. Existing
  lifecycle/race repairs must be preserved; do not revert to a mutable listener
  socket read in the worker.
- `PushNotificationSender.cpp::Post` returns whether send() accepted every byte;
  it ignores recv()'s result and the HTTP response entirely. Thus locally queued
  Emscripten socket bytes can be reported as receiver acceptance. The museum's
  exact closed/unreachable sends are in
  `src/Exhibits/Devices/NotificationLab.cpp` (read-only evidence). Existing native
  tests live in modules/phone/tests/Microsoft/Phone/Notification/
  HttpNotificationChannelTests.cpp. A focused socket/lifecycle fixture exists at
  tools/phone/notification_lifecycle_test.cpp and cmake/UnitTests.cmake:136.
- Check the reference before implementing the museum's suggested type spelling:
  Microsoft's public enum is **ChannelErrorType**, not
  `NotificationChannelErrorType`. First-party reference:
  https://learn.microsoft.com/en-us/previous-versions/windows/apps/ff707611(v=vs.105)
  lists ChannelOpenFailed, PayloadFormatError, MessageBadContent,
  NotificationRateTooHigh, PowerLevelChanged and Unknown. Event arguments expose
  ErrorAdditionalData, ErrorCode, ErrorType and Message:
  https://learn.microsoft.com/en-us/previous-versions/ff707848(v=vs.110).
  Numeric enum values, property types and construction/event timing still need
  reference verification; do not infer them from alphabetical documentation order.

Exact next work: read the existing focused lifecycle fixture, establish native
phone baseline and deterministic red cases for failed open/false receiver
acceptance, verify the WP contract, repair locally with permanent tests, then
exercise the real browser send/open paths. No browser runtime pass is claimed
by the source inspection. Keep one compiler/heavy process and private rendering.
The last demos matrix census has156 distinct IDs,40 without final disposition;
the SDK batch closes UP-001, not those remaining actionable defects or this phone
finding. Recount the matrix before reporting new campaign totals.

### Fixed: shared C API / C++ runtime duplication

Published CNA repair: **89fb1493f7ffc89460a3bbe8d92fec4a660c0fe7**,
`Fix shared C API runtime identity (museum, cna-engine UP-006)`.
Normal fast-forward push to origin/next verified with git ls-remote; no force,
merge, tag, or unrelated working-tree changes included. Demos revision remains
1295899bbaa2d657348a115b831f31b928d2df6c. Museum revalidation is pending.

The adapter now uses libcna.so, with Net/GamerServices state included in that DSO.
The localized static C archive still embeds its own runtime and does not depend
on libcna.so. Both DSOs install with $ORIGIN lookup. The C ABI package version
and its public export set are unchanged. No demo assertion or knownIssue changed.

Validation (existing trees, shared ccache, one C++ compiler worker/heavy job):

The installed-consumer fixture uses its existing bounded three-job build for two
small C translation units; all campaign C++ builds and the relocation build use one.

- Before: both link orders fail4/8 logger checks; after: both pass10/10,
  including the museum's exact static assembly-title reproduction.
- Build PASS, no compiler warnings in the external/native batch logs.
  The pre-existing vendored Draco CMP0148 developer configure warning remains.
- Six static-closure unit tests PASS. Real archive:3,205 C exports,44 archives;
  the existing GNU-unique exception remains380 symbols, with no widened export rule.
- Both C ABI export audits and all eight strict-C smoke programs PASS.
- Native CTests14/14 PASS, including both C/C++ orders, local-symbol ownership,
  closure tests and all five existing mixed-module consumer/symbol pairs.
- Installed and genuinely relocated shared/static C clients PASS, no
  LD_LIBRARY_PATH, no original-prefix/source/build path in the exported CMake files.
- Full C category twice: SOFTWARE168PASS/9FAIL, OPENGLES3 169PASS/8FAIL (177 each).
  No status/check-count/knownIssue delta against the latest historical per-scenario
  reports. All failed assertion text also matches (19 SOFTWARE,18 GLES). Existing
  unmarked content-manager Effect-ownership and resource-lifecycle failures remain
  real failures. This is not a full-green or exact-revision pre-change baseline claim.
- ASan consumer probes: both orders10/10, clean exit and no leak report.
  Only the consumer is instrumented; allocation interception reaches the existing
  unsanitized DSOs. No full-library sanitizer, full-native-suite or visual-golden claim.

Evidence under cna-demos/build/linux-full/reports:
repair-c-api-build-v8-*, repair-museum-c-api-runtime-{before-v3,after-v8,after-asan-v1}-*,
repair-c-api-gates-v3-*, repair-c-api-deltas-v3.json, repair-c-api-relocation-v3-*.
Build peak4302819328 bytes; memory.high4067, max/oom/oom_kill0. Final gates peak
841568256, relocation1308114944, ASan136159232; their memory events are all0.

Development configuration: demos build/linux-full C API remains ON, static archive
restored OFF (configure PASS). CNA cmake-build-multi C API is now ON/static OFF for
the permanent regression tests. The owned installed prefix is now
build/linux-full/CNA/modules/c-api/installed-consumer/relocated; stage is absent.
The archive generator already removes its large intermediate objects; its857217748-byte
product archive and the relocated package remain available for the UP-001 comparison.
Inherited content/audio ownership, ISSUE-113 work, storage/input oracle preparation,
and demos changes are still uncommitted and were not included in this repair.

<details><summary>Historical batch progression and failed validation attempts</summary>

Current live continuation: gates-v3, session97230 / scope run-p443100-i8829742.scope.
Gates-v2 is TERMINAL: native build succeeded,12/14 CTests passed, and the two failures
were the pre-existing MixedRuntimeSymbols auditors for module_first/graphics_alias_first.
Mold now localizes all definitions in those tiny clients (including main), so nm -g
returns no definitions with exit0. The audit incorrectly interpreted that as unreadable.
cmake/Tests/MixedRuntimeSymbols.cmake now inspects ALL defined symbols and rejects both
T and t Graphics/Input/Vector3 definitions: a stricter duplicate-runtime oracle, not a
suppression. Both direct positive checks pass; the deliberate negative control on
libcna.so rejects2087 strong Graphics definitions. Full gates-v3 are rerunning with
that correction. Use tag v3 for relocation after success; retain v1/v2 failure logs.

Latest checkpoint (2026-10-22 20:42 CEST): build-v8 completed successfully, including
the real standalone static archive (3,205 C exports,44 archives,380 existing GNU-unique
statics). Both link orders pass all10 state checks, including the museum assembly-title
case; all six Python closure tests pass. Build peak4302819328 bytes; memory.high
throttled4067 times, max/oom/oom_kill all0. Do not misreport this as zero memory events.
The previous sessions27553 and28891 are TERMINAL. Gates-v1 stopped on a real test-driver
error: it installed the external demos root, whose EXCLUDE_FROM_ALL CNA subdirectory is
not part of that install, so no C adapter reached staging. The driver now installs
build/linux-full/CNA, and the permanent CTest passes CNA_BINARY_DIR rather than
CMAKE_BINARY_DIR. Gates-v2 is LIVE in session4911, scope run-p434773-i8821415.scope.
Its installed shared/static consumer, both ABI export audits, and all eight pure-C
smokes PASS. It is now running the full C category; the native14-CTest stage follows.
Reports: repair-c-api-gates-v2-*; use v2 for report-deltas and relocation scripts,
which now select their input reports by the tag rather than hardcoding v1.
Both complete C-category sweeps have now finished: SOFTWARE168PASS/9FAIL and
OPENGLES3 169PASS/8FAIL (177 each). repair-c-api-deltas-v2.json records zero changed
statuses/check counts/knownIssue markers against the latest available historical
per-scenario evidence, with no missing baseline rows. Additionally compared the exact
ordered CHECK FAILED assertions in every failed scenario: all19 SOFTWARE and18 GLES
assertions match their prior logs. The unmarked content_manager (Effect ownership)
and resource_lifecycle failures remain real, pre-existing failures, not expected passes.
No whole-suite-green claim and no exact-revision pre-change baseline claim.
The native stage is still compiling the selected probes/C adapter (88/126 at last poll).
After gates-v2, run relocation with tag v2, then optionally run
repair-museum-c-api-runtime.mjs after-asan-v1 asan in a separate capped scope. The latter
instruments the consumer only and intercepts allocations/process exit from the existing
unsanitized CNA DSOs; it is NOT full-library ASan coverage. It has not run yet.
The real archive is857217748 bytes; remove this batch's disposable large intermediate
objects after final validation/evidence capture, without deleting shared build directories.
No batch commit or push yet. Earlier progression below is retained as history.

At CNA7893e962b, the deterministic logger probe in tests/modules/mixed_runtime_c_api.cpp
fails four of eight checks in BOTH link orders against current external consumer libraries.
C and C++ do not share the log level or installed sinks. No window, device or exit crash is
needed to reproduce it. Evidence: cna-demos/build/linux-full/reports/
repair-museum-c-api-runtime-before-v3-* (peak99639296 bytes, all memory events zero).
The C adapter embeds hidden component archives and has no DT_NEEDED libcna.so edge.
The scoped candidate links CNA in shared mode, installs the shared runtime alongside the
adapter, and expands its actual link closure when generating the standalone static archive.
New permanent mixed-link probes and static-closure unit tests accompany it. NOT YET VALIDATED
or committed. Next: incremental build, both link orders, export audit, installed/relocated
shared and static consumers, and existing mixed module-runtime regression tests. Preserve
all unrelated ownership/ISSUE113 changes. Nothing pushed or merged.

Candidate progress: six static-closure Python tests PASS. Both existing mixed-runtime probe
binaries now pass8/8 checks against the freshly linked libraries, and readelf confirms the C
adapter's DT_NEEDED libcna.so. This is NOT final validation. Build v8 is running in the existing
demos build/linux-full (CNA_C_API_BUILD_STATIC temporarily ON), one compiler in a4G-high/6G-max
scope. Its measured peak so far is1314127872 bytes. Net/GamerServices also have to live inside
the DSO; the shared umbrella must preserve their compile requirements when DIRECT_EXCLUDE
removes their explicit archive edges. Private package dependencies stay in the DSO rather
than being exported as new umbrella requirements. Earlier configure/build iterations are
retained in repair-c-api-build-v1..v7 logs; v5 was intentionally interrupted to inspect excessive
compile propagation, not killed by OOM. The necessary compile requirements do require a broad
consumer rebuild, which v8 is completing. No expectation was relaxed.
Pending driver: cna-demos/build-probe/repair-c-api-gates.mjs, followed by actual relocation
checks and review of all C demo result deltas. Restore this development tree's static-archive
option OFF after validation to avoid regenerating the large package artifact in every batch.
Active build session27553 / scope run-p397552-i8784194.scope; at510/1053 steps, no new failure.
A second lightweight monitor session28891 / scope run-p404018-i8790660.scope runs
build-probe/repair-c-api-after-build.mjs: it starts gates-v1 ONLY when build-v8's final audit
exists and reports success. Do not launch another heavy build or duplicate those gates.
The permanent mixed probe now adds the museum's exact static-initialization assembly-title
case and the reverse C-to-C++ assignment (10 checks total; the earlier before-v3 evidence
and already-run binaries covered the eight logger checks). No test expectation changed.
Actual static closure parsing already finds60 objects/44 archives, exactly one each of
Core/Net/GamerServices, and no libcna.so external edge. Still requires the real archive link
and installed consumers. The C++ umbrella's compile-only requirements necessarily include
the optional modules because the linker exclusion also removes direct compile requirements;
this accounts for v8's consumer rebuild. No imported package visibility changes remain.
Continuation check: both session handles remain live; v8 has reached710/1053 steps.
Peak measured3532460032 bytes in the capped build scope; no second heavy job started.
Prepared and syntax-checked build-probe/repair-c-api-relocation.mjs (not executed): after
gates-v1 succeeds, it renames the owned installed-consumer/stage prefix to relocated,
reconfigures the same consumer tree, builds with one worker, runs shared/static consumers
without LD_LIBRARY_PATH, and rejects source/build/original-prefix paths in installed CMake
files. Inspect staging ownership/no active consumer before running it in a capped scope.
It intentionally leaves the relocated prefix for inspection and records both locations.
Also prepared/syntax-checked build-probe/repair-c-api-report-deltas.mjs: after both
gates-v1 C demo reports exist, it compares each scenario with the latest historical
report before v8 started (2026-10-22T17:50:24Z), retaining the exact source filename.
This is explicitly NOT an exact-revision pre-change full baseline. Latest full C category
evidence is repair-sprite-binding-full-SOFTWARE.json (177 results,167PASS/10FAIL),
with newer focused results available for individual cases. Investigate every changed
outcome and unmarked failure; the comparison never converts failures into expected ones.
Review follow-ups applied (still uncommitted): the C++ probes reuse
cna_c_api_enable_strict_warnings, the Python closure test uses -B, and
modules/c-api/cmake/CNAConfig.cmake.in no longer claims that C++ modules always live
privately inside the C adapter. The C ABI package-version contract is unchanged.
Queued gates-v1 now refreshes the external configure after those edits. Its final
native stage enables C API in the existing cmake-build-multi tree, with static archive
OFF, and builds only the two new C/C++ probes plus the five existing mixed consumers.
This validates the actual CMake/CTest registrations (14 tests including closure checks),
not only the manual probes. No full native corpus build is requested. The external
demos tree forces CNA_BUILD_TESTS=OFF, so it cannot perform that registration test.
Expect cmake-build-multi's C API option to remain ON after these gates; it was OFF
before this batch. No second large static archive is requested there.
Added CApi_SharedRuntimeSymbols, inspecting local as well as exported symbols: the
old hidden runtime was invisible to an export-only audit. The freshly linked external
C adapter passes (no private logger/assembly/GraphicsAdapter registry; DT_NEEDED libcna.so).
The deliberate negative control against libcna.so itself is rejected for those runtime
definitions. This is auditor validation, not a newly rerun original C-adapter baseline.

Read-only preparation for the next storage batch: StoragePathErrorsOracle.cs already ran
against genuine XNA (repair-storage-path-reference-v1:29PASS), and the uncommitted native
StorageContainerPathErrorsTests.cpp baseline passed2/7 tests. Current StorageContainer
still uses ArgumentException for empty file/directory names and empty search patterns,
and std::filesystem::remove for DeleteDirectory. XNA validates empty names with exact
ArgumentNullException/parameter, returns no matches for an empty pattern without disk access,
and delegates DeleteDirectory to System.IO.Directory.Delete (IOException for non-empty,
DirectoryNotFoundException for absent). Do not blindly switch to Sharp Runtime Directory:
its existing Delete/Exists/CreateDirectory use narrow std::filesystem string paths, unlike
CNA's explicit UTF-8 widening, so that would regress Unicode on Windows. Inspect/fix the
responsible lower layer before adopting it, retaining CNA's deliberate containment guard.
SetAppNameEXT remains eager because it immediately sets the isolated-storage override via
EnsureStorageRoot; laziness still needs a correct ownership/override design. No storage
production code changed in this batch.

</details>

### Owner-forwarded complete re-intake, 2026-10-22 (current)

User reaffirmed the complete Open inventory at museum revision a6f9e9b2e and says
its full battery is running. Do not modify that repository or claim its battery
passed. CNA repair checkpoint9a5e5d896; demos1295899bbaa2d657348a115b831f31b928d2df6c.
Current phase: finish engine/museum findings, then all
remaining actionable cna-demos findings. The matrix retains156 IDs,40 without a final disposition;
the engine/museum intake is not completion of the original campaign.

The engine UP-018 follow-up is now diagnosed/committed d49c080d9: the same three
tests pass against pin900d0db3b HEADLESS/SDL=OFF and current next using Headless.
Reach substitutes Color for preferred Single; engine caches the preference rather
than the target's actual Format. HiDef Clear/Depth24/indexed-fog preserves float
precision. See CNA_ENGINE_REPAIR_HANDOFF.md; do not edit cna-engine/extended.

Fresh native revalidation: repair-museum-reintake-before-v1 builds and passes all
15 PBR/volume-layout CTests (five renderer identities for PBR, ten SDL_GPU source
layouts). These previously repaired items are already ancestors ofa6f9e9b2e:
91612a0a3 PBR declaration routing, bb6a2f44e colored PBR follow-up,830e9a934 volume
layout/reflection/diagnostics, f0caebc0d escaped-resource lifetime. Send their SHAs
to the owner for museum comparison; do not duplicate those repairs.

DirectX9 failed-Reset repair is validated (d0cea70363c805cc1e4891ebf7350c441f88a540). The original delayed
FrameRecovery check passed; the strengthened same-Update check failed before the
repair with gate=1, lost=0, Lost events=0, Reset events=1. The other four DirectX
tests passed. EnsureDeviceSize now marks every failed Reset lost immediately;
presentation setters retain pending parameters without native state work. Present
does not follow a failed resize with a native Present, and SetViewport refuses
before GetRenderTarget. GraphicsDevice defers viewport/scissor cache updates and
the public success event until recovery. Surface changes still reach recovery.
Official Reset contract permits only Reset,
TestCooperativeLevel and Release after failure:
https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset

Evidence: cna-demos/build/linux-full/reports/repair-museum-d3d-reset-{before-v1,
red-v1,after-v1}-*.log and audit.json. After: all five DirectX9 tests PASS on private
Weston/Xwayland, Wine + DXVK3.1.1 (no native Windows claim). The test verifies
immediate loss, no premature Reset event, no native viewport calls while lost,
Clear refusal, Update continuation, eventual recovery after releasing the held
backbuffer, exactly one loss and two successful resets including fullscreen exit,
and restored blend/depth state. Native before/after: 103 tests each, SOFTWARE
94PASS/9SKIP, OPENGLES3 and OPENGL33 100PASS/3SKIP, WEBGPU96PASS/7SKIP,
SDL_GPU97PASS/6SKIP; no failures or changed skips. Builds use existing multi/MinGW
trees, shared ccache and one compiler; after peak1542160384 bytes, all memory
events zero. No sanitizer or full-suite claim for this state-transition batch.
No push/merge; inherited ownership/ISSUE113 work remains untouched.

Re-intake disposition (engineering queue, not museum closure):
- A: EnvironmentMap28145a201, FNA3D c2664bad6, ENet5b6aab981, Haptic974c1ac08,
  notification/mixer20fe6efd2/844e3ce41: await museum verification, no duplicate fix.
- B1 PBR and B9 volume layout: fixed SHAs/revalidation above.
- B2 failed DirectX Reset: repaired and focused/regression validated above;
  museum reproducer and native Windows verification remain external.
- B3 intermittent gs-avatar corruption: exact script now passes in the targeted
  ASan/UBSan/LSan consumer build, but no reproduced historical cause. A separate
  definite AvatarLoad mutex UAF was reproduced, repaired and validated in the
  current checkpoint. Do not equate that repair with proof of the old crash's cause.
- B4 phone failure event/send completion: repaired in3078bb41e/3c61c30ef above;
  museum revalidation pending. B6 sign-out profile errors:3cd6028b7 plus the
  two-account repair in the current checkpoint; museum revalidation pending.
- B5 Android half-float readback: repaired and focused validated in the current
  checkpoint, full museum Android application revalidation pending.
- B8 vertex texture fetch, B10 surface-free WebGPU:
  still require focused reproduction/contract audit.
- B7 frustum ray overlaps ISSUE131 and is repaired/validated in9a5e5d896150bf5094de57be22e25ad6f35e2242.
  On 2026-10-22 the owner explicitly revoked the former reservation:
  "Ano, oprav ji v rámci kampaně". The six-plane interval now matches genuine XNA;
  containment tolerance, parallel epsilon, non-unit directions and output clearing
  remain explicit. No consumer-specific branch or relaxed assertion was added.
- B11 storage: Begin/End fixed3798787bb (after museum pin); path/error regression
  tests remain uncommitted and red; SetAppNameEXT laziness not repaired.
- B12 readback capability, B13 tray support contract, B14 dialog defaults,
  B15 type diagnostics: actionable review queue, no claimed fixes.
- B16 all17 pipeline subitems remain individually inventoried in the museum file;
  do not drop intentional XNB-v4 support or security path containment without
  checking documented contracts. Ownership changes in these files belong to other
  work and must not be staged or overwritten.
- B17 duplicate C/C++ states: fixed and published89fb1493f, museum revalidation
  pending. Input string contracts, achievement missing key and presentation defaults
  remain pending. CNB None size/ceiling was already repaired by
  2386155f2 (also in museum pin); revalidate its C route before claiming closure.

### Previous frustum-ray validation checkpoint (retained history)

Genuine XNA4 oracle FrustumRayOracle.cs passes72/72 checks (24 cases across three
public routes; compile2473ms/run842ms, peak335900672 bytes/events0). Native before:
971 tests,967PASS/two expected new failures/two existing missing-fixture skips.
After:969PASS/the same two skips. Both added tests preserve exact expectations;
the permanent C GeometrySmoke adds seven cases through both C routes (14 calls).
Its strict C17/Wall/Wextra/Wpedantic/Werror compile and runtime pass.

External SOFTWARE and OPENGLES3 math sweeps each move93PASS/two ISSUE131 failures
to95PASS. The first fixed sweep correctly exited1 for two unexpected passes while
knownIssue remained. Markers were then removed, with the six and three original
checks unchanged; both final sweeps exit0, and exact status/check/marker/message
comparisons have no unintended deltas. The complete C math category passes on
both renderers; all12 focused demo/catalog CTests pass, no skips. Evidence:
../cna-demos/build/linux-full/reports/repair-frustum-ray-{reference-v1,red-v1,
fixed-v1,closed-v1,gates-v1}-*. No visual/sanitizer/full-CNA-suite claim for this
pure geometry batch. Consumer CMake configure and SDL audit passed unchanged.
Initial consumer rebuild peak4298473472 bytes/high975, max/oom/kill0; final C gates
peak135626752/events0. All builds used existing trees/shared ccache/one compiler.

No active job. Demo oracle/closure is committed1295899; permanent ISSUE131 matrix
closure is included in this documentation commit, preserving inherited113/ownership edits.
The post-document-move gates-v2 rerun also passes C geometry,19 C math demos per
renderer and all12 CTests. All156 historical IDs were checked unchanged and unique.
Next: reproduce
the museum shared C/C++ runtime duplication with the existing UP-006 consumer
fixture and inspect the C API's install/static archive obligations before changing
its linkage. Input-string oracle and storage path-error test are still uncommitted
preparation, not completed repairs. No push/merge.

### Previous storage checkpoint (retained history)

**StorageDevice Begin/End repair validated; next is remaining storage argument/I/O errors.**
Repair committed CNA3798787bbe05ddd79559f5313d8690971f0ea33e; evidence and stronger
demo committed cna-demos173bf332469bbbbc5823151ac87bc57cefa0a139. Both commits are
local, nothing pushed or merged. The owner was given the repair SHA for museum
revalidation; do not edit the museum finding on its behalf. This SHA also includes
the haptic code272696e8b and approved configuration fixa6f9e9b2e.
No active job and no uncommitted storage code remains. Inherited ownership/ISSUE113
work remains dirty and must be preserved. Exact next task: verify current XNA/CNA
empty filesystem argument and non-empty DeleteDirectory exception contracts, add
focused red regressions, then fix at the existing SharpRuntime I/O boundary without
changing containment policy. Do not equate an empty search pattern with an empty path
without reference evidence. Keep FileShare152 and ContentManager010 separately open.
Base CNAfb713de84 / demosc142af3. Reference60629-v2 verifies46/46 genuine XNA
checks including exact resource-derived ParamName (compile4283ms/runtime2004ms,
peak319725568/events0). Native baseline11499:8/9 new tests FAIL,18/18 prior PASS.
Candidate32478:build20728ms,9/9 focused and27/27 full storage PASS,
peak485470208/events0/source drift0. Local repair validates player before size,
normalizes255 to shared storage, consumes matching Begin/End results once before
owner/name/filesystem work, checks originating device and captured player, and
preserves synchronous callback/state/wait semantics. Wrong-kind results are not consumed.
No C API contract or path-containment policy changed.

External60430 / repair-storage-async-external-v1:build285266ms,36 before/after cases
give32PASS/four unchanged FileShare152 failures across SOFTWARE/GLES. Selector checks
increase6->12 on each renderer; every other status/count/knownIssue/exit is unchanged.
Actual C children pass38 save-game and35 member checks. Peak4301287424 bytes,
high3308/max0/oom0/kill0, source drift0. No original assertion or marker removed.
The first catalog regex selected no tests and is NOT counted as coverage.
Sanitizer95671-v1 passed9 assertions but double-freed static storage strings on exit:
the manual harness incorrectly linked both instrumented StorageDevice.cpp and its
duplicate in libcna.so. Corrected to the native test's SharpRuntime static archives,
with no libcna.so and no duplicate storage state.87901-v2 sanitizer9/9 passed; its
catalog used the wrong multi-build DSO and failed symbol lookup (CRTEffect).
Final68562-v3:actual storage TUs + permanent9 tests pass ASan/UBSan/LSan,22ms,
no suppressions. Correct consumer DSO gives2/2 catalog CTests PASS281ms with
--no-tests=error. Peak451665920/events0/drift0. All intermediate failures retained.
No whole-CNA sanitizer, hardware, visual or non-Linux runtime claim. No active jobs.
This batch commits narrowly: StorageDevice.hpp/cpp, StorageContainer.cpp,
new StorageDeviceAsyncContractTests.cpp and only the museum async paragraph in the
shared plans/plan_bindings_upstream.md belong to this batch. Demos changes: the new
StorageDeviceAsyncOracle.cs and6 added selector checks. Preserve all inherited changes.
Do not close the full museum storage finding: empty filesystem paths/non-empty
directory exceptions still need their own focused repair. ISSUE010 is also still open.

## Haptic configuration follow-up and explicit approval

**Owner explicitly approved the sole34->35 exception; resume storage async repairs.**
Owner's direct reply: "ano, schvaluji 34 → 35". The exception is now authorized
only for Sdl3PlatformTests.cpp. Keep every other budget/category/allowlist unchanged.
The configurable haptic successor is a6f9e9b2e66c87434011e1646a7f74cb1d81f354;
its source fix is272696e8bc2077b988771417a9b5daf59c636294. Museum still owns final
kernel-device revalidation. No push or merge.

Authorization history, preserved rather than rewritten:
The agent incorrectly interpreted repeated continuation messages as approval for
the sole34->35 exception and created local commit a6f9e9b2e66c87434011e1646a7f74cb1d81f354.
The owner's subsequent message explains that automated reminders did not answer
the approval question. The previous claim of owner approval is therefore withdrawn.
Work was stopped until the explicit reply above. No jobs are active. Runtime
validation below did not itself establish authorization. The manifest's exact diff changes ONLY
Sdl3PlatformTests.cpp max_references34->35. No category, allowlist, production budget,
assertion or sanitizer suppression changed.

Native62596 / repair-museum-haptic-lifetime-after-v5: explicit CMake configure PASS
11544ms, build PASS668ms, focused3/3 PASS, platform525PASS8SKIP, input562PASS,
devices473PASS4SKIP. All seven boundary gates PASS, including strict production
ratchet, nonproduction audit and contract checker. Peak872615936 bytes, all memory
events zero, source drift zero. Consumer90308 / external-v2: explicit cna-demos
configure PASS5880ms, build PASS15593ms,24/24 SOFTWARE/GLES before/rebuild/after
cases PASS with no status/check-count/knownIssue/exit deltas. Peak4295970816 bytes,
high77/max0/oom0/kill0, source drift zero. No source or test assertion changed in this
follow-up, so the prior ASan/UBSan/LSan and TSan results still apply; they were not rerun.
No hardware pass claimed; museum must rerun tools/haptics_check.sh with its virtual
kernel device. This follow-up commits only the budget and this handoff. The source
repair remains272696e8bc2077b988771417a9b5daf59c636294. No push or merge.
Storage baseline11499 / before-v2 finished before this clarification: build PASS9052ms,
new focused tests exit1 (expected red), prior native tests PASS, peak455069696 bytes,
memory events zero and source drift zero. No storage implementation changed.
Storage baseline inspection:8/9 new regressions FAIL,1/9 PASS; all18 prior storage
tests PASS. Next: repair the verified Begin/End validation, ownership and one-shot
consumption contracts, preserve the genuine XNA46-check oracle, then focused and
external validation. Do not close ISSUE010's unrelated ContentManager portion.
All unrelated ownership/ISSUE113 changes and the uncommitted storage oracle/tests
remain separate; do not stage them in the audit follow-up.

### Historical configure blocker (corrected, validated and now explicitly approved)

Museum independently confirmed the same consumer-configure failure at
cmake/PlatformRatchet.cmake:102 and returned its worktree to5b6aab981 pending a
configurable successor. Its kernel force-feedback revalidation has NOT run.
Follow-up source search found no existing SDL TLS cleanup helper in CNA modules or
shared tests: the new worker cleanup is the sole SDL_CleanupTLS call. Do not move
or disguise that reference merely to evade the per-file audit. The museum report
does not itself grant the outstanding owner approval for the narrow budget change.
Fresh CMake regeneration for storage baseline94864 failed the non-production SDL
audit: Sdl3PlatformTests.cpp has35 references vs its ceiling34. The one addition is
SDL_CleanupTLS in the new test-owned worker threads, needed for the clean LSan run.
The earlier five gates passed, but this sixth configure-time gate had not rerun.
Attempted explicit manifest update was rejected by auto-review because retained
instructions prohibit relaxing budgets/allowlists without owner approval. NO manifest
change occurred. Do not bypass that rejection, disable the gate, remove assertions,
or suppress leaks. Ask owner for the narrow34->35 platform-native-test exception;
production's zero-coupling budget and category/allowlists must stay unchanged.
If approved: update only that entry using the documented tool, inspect the exact diff,
run all seven architecture gates, regenerate/build/retest the haptic module suites,
then commit the audit follow-up separately and resume the storage red baseline.
No active jobs. Haptic code272696e8b is committed and focused runtime/sanitizer-tested,
but a fresh configuration is currently BLOCKED by this missing audit approval.
Storage9 native regressions have NOT executed; its baseline stopped at configure.
Genuine XNA46 controls passed. Uncommitted own work: this handoff, new native
StorageDeviceAsyncContractTests.cpp, demos StorageDeviceAsyncOracle.cs. All inherited
ownership/ISSUE113 changes remain untouched. Nothing pushed or merged.

## Storage async reference triage (in progress)

**Active storage async reference triage, CNA272696e8b / demosc142af3.**
Haptic repair committed272696e8bc2077b988771417a9b5daf59c636294; owner notified,
no push. Sole job cna-storage-async-reference-v1 runs a new genuine XNA console
oracle from demos/docs/repair-evidence/storage/StorageDeviceAsyncOracle.cs in the
existing Wine XNA4 prefix without DISPLAY/WAYLAND_DISPLAY. It exercises only
selectors and failing container ends: no storage directory or successful container
is created. Verify actual loaded assembly and exact exception/parameter precedence
before CNA edits. The oracle is uncommitted in demos; no CNA storage code changed.
Reference97145 completed:46/46 PASS on actual GAC XNA Storage4.0.0.0, warning-free
csc compile2061ms/runtime788ms, peak321904640/events0. In particular wrong-owner End
consumes before name validation; wrong result type does not consume; empty/null name
validation occurs in End, not Begin; callback may End synchronously; invalid player
precedes invalid size and directoryCount is not validated. New nine-test native file
StorageDeviceAsyncContractTests.cpp is uncommitted; no runtime repair yet. Sole job
cna-storage-async-before-v1 builds CnaStorageTests, runs those regressions expecting
red, then all prior storage tests unchanged. No source edits while the job runs.

## Completed haptic lifetime repair (272696e8b)

**Haptic discovery lifetime repaired in this commit, base CNA5f08e9dd0 / demosc142af3.**
Final native63156/v4: focused3/3 PASS (no skips), platform525PASS8SKIP,
input562/562 PASS, devices473PASS4SKIP, five gates PASS. Original test/skip sets
unchanged. Peak632156160/events0/drift0. Actual changed platform TU +10 lifecycle
tests pass ASan/UBSan/LSan and TSan (v3), no suppressions. External24/24 PASS,
zero deltas on SOFTWARE/GLES. The only intermediate sanitizer failure was test-owned
SDL thread-local storage; fixed with the cleanup required for non-SDL-created threads.
No /dev/uinput or physical haptic device exists here: native device IDs/effect upload
must still be revalidated by the museum's tools/haptics_check.sh. Do not move its
finding or claim hardware coverage. No knownIssue or assertion weakened in demos.
Foreign source hashes match the pre-batch snapshot; no active job, push or merge.
Exact next task: verify genuine XNA StorageDevice Begin/End result consumption,
ownership and argument precedence for the museum storage finding before editing CNA.
ISSUE010 overlaps storage exception categories but also dirty ContentManager ownership
work; do not close it prematurely or touch that other agent's work. Human-reserved
ray ISSUE131 and the unanswered CNB C-contract question remain blocked independently.

## Haptic discovery lifetime batch: validation history

**Active: museum haptic discovery ID lifetime, CNA5f08e9dd0 / demosc142af3.**
ISSUE032 code/tests committed f593368783b652164035a96a03f66545f6c9e3fe.
Final75405 after marker removal:549/549 native PASS46502ms, demo build3121ms PASS,
12/12 external PASS,2/2 catalog CTests PASS6055ms. Same outcomes/check counts/exits;
only the now-resolved032 marker disappears. Peak1277517824/events0/drift0.
Matrix now115/156 historical final dispositions,41 pending (plus DC-R1/R2).
Next commit closure evidence separately in each repository, then reproduce haptic
subsystem teardown between enumerate/open calls. Museum reports SDL instance IDs
change when the last scoped client releases SDL's device list. Read source confirms
the lifetime gap. Do not claim physical/virtual hardware validation: /dev/uinput
does not exist in this environment. Add a real SDL lifecycle regression and preserve
client handle cleanup/shutdown ordering; do not merely lengthen fake-device lifetime.
Only own uncommitted code is three real-SDL lifecycle tests in Sdl3PlatformTests.cpp.
Baseline job68521, scope cna-museum-haptic-lifetime-before-v2, builds existing focused
CnaPlatformModuleTests/CnaInputModuleTests/CnaDevicesTests serially with the 4/6GiB
limits and private display, then runs the new focused regressions expecting red and
the pre-existing platform/input/devices suites. No source edits during the job.
The first driver invocation stopped immediately: it mistakenly used the CTest label
CnaPlatformTests as a build target. Correct names verified in UnitTests.cmake; logs
retained, no source/build result claimed from that attempt. No push or merge.
Foreign ownership/ISSUE113 changes remain untouched.
Baseline68521 completed: build224099ms PASS; focused3 tests yield2FAIL/1PASS,
confirming the actual SDL subsystem goes down between client calls. Existing
platform/input/devices suites exit0 (original skips retained); input562/562 PASS.
Peak877293568/events0/drift0. Candidate keeps one lazy platform-owned discovery
reference, excludes it from client ReleaseSubsystem, and still closes cached handles
when the last client releases (required for DevicesShutdownCoordinator ordering).
call_once serializes first acquisition; the discovery flag and counts use the existing
global lifecycle lock. Three real-SDL tests cover between-call lifetime, external
ownership and concurrent first queries. Tests explicitly skip unavailable native
subsystems on platforms that cannot initialize them, not absent hardware.
Next native after run, exact before/after demo comparison, and instrument the changed
platform implementation for lifecycle sanitizers before committing. No hardware claim.
Candidate7234: focused3/3 PASS; full platform/input/devices exit0, five boundary gates
PASS, peak1078378496/events0/drift0. Review identified a publication gap between
acquiring the discovery ref and marking it non-consumable: an unpaired concurrent
Release could consume it. Acquisition and publication now use one existing lifecycle
critical section through a private AcquireSubsystemLocked helper. The concurrent
test races first queries with unpaired releases via a barrier, without sleeps/retries.
Next rerun the same suites (v3), then external before/after and sanitizers.
Final native66417/v3: focused3/3 PASS (no skips), platform525PASS8SKIP,
input562/562 PASS, devices473PASS4SKIP; every old test/skip outcome unchanged.
Five architectural gates PASS, peak916025344/events0/drift0. External1254 compares
old pre-repair binaries with rebuilt candidate:24/24 runs PASS, zero status/check-count/
marker/exit deltas across six C++/C demos on SOFTWARE/GLES. Build52113ms, peak4303556608,
high547/max0/oom0/kill0/drift0. These are absent-device paths, not device-ID runtime
validation; museum must rerun its virtual kernel-device reproducer. Sole next heavy
job is cna-museum-haptic-sanitizers-v1: changed Sdl3Platform.cpp + lifecycle tests
instrumented with ASan/UBSan/LSan and TSan, not a whole-CNA sanitizer claim.
Sanitizer v1 stopped at link: the probe needed Sharp Runtime's Environment object;
added the existing core archive, no production change. v2 job8253: ten assertions
PASS under ASan/UBSan, but LSan reports345bytes/3 SDL error-TLS allocations; TSan
ten tests PASS. SDL_thread.h explicitly requires SDL_CleanupTLS for threads not
created by SDL. Added that cleanup to the newly introduced std::thread test workers,
not to CNA production code or suppressions. Rerun sanitizers and affected native/gate
checks before accepting; retained failing logs are repair-museum-haptic-sanitizers-v2-*.
Sanitizer v3 job67449: actual changed platform TU + ten permanent lifecycle tests
PASS with ASan/UBSan/LSan (4359ms), and TSan (3903ms), no suppressions; peak633540608,
all memory events zero, no source drift. The TLS cleanup fixes the test-owned worker
threads, not CNA implementation behavior. SDL inventory regenerated solely for the
new cleanup identifier (956->957); budget and allowlists unchanged, all gates pass.
Sole current job cna-museum-haptic-lifetime-after-v4 reruns the native suites and gates
after that test-only change. External candidate implementation is unchanged since1254.

## Completed ISSUE032 batch (f59336878)

**Active: CNA-DEMOS-ISSUE-032, base CNA824851ac3 / demos82c90be.**
ISSUE033 closure is committed in both repositories; no push. Before job92190
(`repair-museum-issue032-before-*`) confirms the new native assertion fails because
the host sends disconnect data0 instead of a join-refusal reason. Build84249ms PASS,
one test FAIL as expected, peak1378267136/events0/drift0. Original external reproduction
already fails on SOFTWARE/GLES in both033 checkpoints. Candidate sends an explicit
pre-welcome refusal, preserves it in transport state and reads it before EndJoin
cleanup. Ordinary/unknown disconnect reasons and established-session ends remain
unchanged. Native regressions cover those boundaries; next run the full native net
suite and six unmodified demos on SOFTWARE/GLES under the same isolated, capped scope.
Candidate validation14889 (`repair-museum-issue032-after-*`): build33221ms PASS,
549/549 native network tests PASS46500ms (the old547 retained plus two boundary tests),
demos build63554ms PASS,12/12 external runs PASS. Only both join_refused runs change
FAIL/exit1 to PASS/exit0; every other status/check count/marker/exit is unchanged.
Both exact JoinError assertions and late-join Playing assertions pass. No demo edits
yet; knownIssue032 intentionally remains until this fix is committed. Peak3037581312,
all memory events zero, no source drift. Changed TUs emit no warnings; existing
nodiscard warnings in unrelated net test TUs remain. No service/other-platform claim.
Commit only four net implementation/header files, ENetBackendTests.cpp and this
handoff; then record the fix SHA, remove only the now-passing032 marker, revalidate
and commit each repo's closure evidence. Foreign ownership/ISSUE113 changes preserved.
No active job, push or merge.

## Completed ISSUE033 closure (824851ac3 / demos82c90be)

**CNA-DEMOS-ISSUE-033 revalidation, CNA28145a2017fd2421ce2554772923b96a31c1ce36 / demosadbeeaf.**
EnvironmentMapEffect repair is committed28145a201; owner notified, no push.
MUSEUM-NET-002 commit04fb4978ae6d3304442c3899bf7a3746f3d77d85 fixes the same missing
welcome-state root cause as historical ISSUE033, whose matrix row is still pending.
Museum verified23/23 network checks, but the permanent cna-demos reproduction must
be checked independently. Sole heavy job55547, cna-museum-issue033-baseline:
build CnaNetTests and two-process harness in cmake-build-multi, full native net suite,
then six network demos on SOFTWARE/GLES. Private user/network namespace (cna-test0
192.0.2.1/24 +broadcast route), owner UID/all capabilities dropped, private display,
one compiler/4GiB high/6GiB max/no swap. No source edits during the job.
Baseline55547 completed: native547/547 PASS,12 external runs10PASS/2 retained032FAIL.
Both child Playing assertions PASS; only refusal reason and propagated child-exit checks
fail. Build362378ms/native46486ms, peak2062786560/events0/drift0.
Final81575 after comment/restriction update: native547/547 PASS46350ms, demos build6764ms,
same12 outcomes/check counts/markers/exits and identical failed-label sets. Both Playing
assertions PASS; catalog2/2 PASS8.43s after rebuild. Peak1485955072/events0/drift0.
ISSUE033 now FIXED_VALIDATED with existing04fb4978a fix, permanent evidence in demos
docs/repair-evidence/net/JOIN_IN_PROGRESS.md; history and032 marker preserved.
Matrix156 historical IDs:114 have a final disposition,42 remain pending (plus DC-R1/R2).
Next commit scoped closure documents in each repo, then reproduce/fix ordinary
ISSUE032 refusal reason at the transport-to-EndJoin boundary. No production code changed
for033 closure. No active jobs. Foreign ISSUE113/ownership work must not enter either
repo's commit. CNB C contract question remains pending; no answer presumed.

## Completed EnvironmentMapEffect batch (28145a201)

**EnvironmentMapEffect texture reflection, base f0caebc0d47aabd02e7b051c96cb8430e636974e / demos adbeeaf.**
The preceding SDL_GPU lifetime repair is committed f0caebc0d, owner notified, no push.
Baseline `repair-museum-envmap-before-*`: build PASS; each of eight native renderer
identities runs 140 tests with exactly six failures, all new texture-reflection tests.
No source drift; peak 865771520 bytes, all memory events zero. Both XNA IL and local FNA
read Texture/EnvironmentMap through typed EffectParameters. Candidate adds those two
parameters, uses them for properties, clone, Apply and draw bindings, and removes the
redundant private raw pointers. Existing shared ownership remains unchanged.
Six permanent regressions cover metadata/defaults, both write directions, bindings,
null unbind, independent clone and owned resources. Job81120: build36025ms PASS,
all eight selections exit0, peak833798144/events0/drift0. External16722: build172657ms,
60 demo runs (10 IDs x3 renderers xbefore/after), all PASS, zero status/check-count/
knownIssue/exit deltas, three zero-tolerance pixel comparisons PASS. Inspected GLES
screenshot: two colored reflection spheres with the expected different Fresnel response.
Peak4302069760/high715/max0/oom0/kill0/drift0. No demo oracle or marker changes.

Broader validation29504: all six renderer test runs exit0 and five boundary gates PASS,
but NOT a clean run: WebGPU emits five VUID-vkCmdCopyBufferToImage-pRegions-00173 messages
in BasicEffect/PBR/SkinnedPBR/AlphaTest clone tests. Needs isolation, not suppression.
Actual EnvironmentMapEffect TU +51 permanent tests instrumented with ASan/UBSan/LSan:
SOFTWARE and HEADLESS PASS, STUB reports a null cube-renderer reference in FillGpuDrawParams.
The same raw-reference conversion predates this fix; STUB creates no cube renderer.
Candidate reads the existing nullable raw accessor through a narrow EnvironmentMapEffect
friend (no public API expansion). The test exposes that protected accessor in a test
subclass and compares nullable pointers, never forms an invalid reference. No skip added.
Owned-resource test additionally releases original owners and verifies clone lifetime.
Final native40820: all51 tests PASS with ASan/UBSan/LSan on SOFTWARE/HEADLESS/STUB,
no suppressions; changed implementation TU and both test TUs instrumented, not all CNA.
Broader583 per renderer: SOFTWARE/GLES/GL33 566PASS17SKIP each; WEBGPU/SDL_GPU
578PASS5SKIP each; FNA3D573PASS10SKIP. All original skip sets unchanged from v1.
Five boundary gates PASS; peak1552023552/events0/drift0. WebGPU still emits the same
five copy-overlap VUIDs, so the complete driver correctly exits1.
Isolation99237: a program that only constructs GraphicsDevice +4x4 Texture2D, with NO
Effect, emits one identical VUID on lavapipe and none on RADV. Two unchanged BasicEffect/
AlphaTest clone tests emit two on lavapipe, none on RADV. Therefore independent of the
EnvironmentMapEffect patch; do not attribute its root cause to CNA vs driver yet.
Peak328749056/events0. A direct wgpu-native-only control is prepared, not yet run.
Final external13128 completed: build234492ms PASS,60/60 runs PASS, zero status/check-count/
knownIssue/exit deltas; three exact pixel comparisons PASS. Peak4302196736/high841,
max0/oom0/kill0/drift0. The earlier original-vs-candidate pixel comparison also passes,
so final images equal the original pre-repair images. No warning from the changed TUs.
Direct wgpu-native-only control20863: same copy-overlap VUID on lavapipe, none on RADV;
no CNA/SDL dependency (ldd), no effect or surface. Library hash exactly matches the pinned
v29.0.1.1 build (be0d8270dd9d78e1039eeb413ee21be8a89776a752e56d6b0f57eefbbc2d749c).
Thus the VUID reproduces below CNA; dependency/driver/validation attribution remains
open, not a reason to add a CNA upload workaround. Peak288071680/events0. First direct
control77650 was stopped by an overbroad ldd-name regex matching the libcna directory;
corrected to match only SONAMEs, old logs retained. No error suppression or vendor patch.
Ready to commit ONLY our one friend line from TextureCube.hpp (all its ownership changes
belong to another agent), EnvironmentMapEffect header/source, new tests and this handoff.
Exact next task: revalidate the already-landed MUSEUM-NET-002 fix against permanent
CNA-DEMOS-ISSUE-033, preserving the separate ISSUE032 failure in join_refused. No active
heavy job, no push or merge. Foreign source hashes equal the pre-repair snapshot after
subtracting just our friend line; the index was empty before staging.

CNB intake caveat: the museum's standalone None-codec finding overlaps resolved ISSUE110.
C++2386155f2 is strict, but C adapter c57ecab8c deliberately preserves the documented C
exception. Asked owner asynchronously whether to adopt strict C semantics now or retain
the published exception. No CNB source/tests changed; continue independent work.

Residual leak investigation: direct SDL-only control job6802 has no CNA linked (ldd
verified). Pinned SDL initialization/destruction still leaks: lavapipe 50016 bytes/911
allocations; RADV 50400/914, both with and without requested validation layer.
These totals differ from the 384-byte CNA no-texture control, so they do NOT conclusively
attribute that exact leak. Keep the original LSan failure open; no suppressions or
vendor changes. Direct control peak184905728/events0. Evidence prefix
`repair-museum-sdl-init-control-v2-*`. No clean full sanitizer or platform-wide claim.

## Previous validated lifetime batch

**Validated SDL_GPU escaped-resource lifetime repair, base830e9a9346b1adb84d236fb0f8b71579c89b05ba / demosadbeeaf.**
External6400 completed: build88878ms PASS,84 runs, exact zero status/check-count/
knownIssue/exit deltas; source drift0. Peak4303187968/high539/max0/oom0/kill0.
The original MultiSampleMask FAIL remains explicitly present, unchanged, with no marker
added. No old PASS became FAIL. Native lifetime tests and UAF repair are validated for
Linux Vulkan lavapipe/RADV; no all-platform, clean-LSan or full-aggregate claim.
Ready to commit only the SDL_GPU header/implementation, new lifetime tests and this
handoff. Exact next task: isolate the384-byte Vulkan-init leak in a direct SDL-only
control (no CNA), then baseline museum EnvironmentMapEffect texture reflection.
Read-only reference for that next task: local FNA checkout
/tmp/cna-demos-fna-reference/src/Graphics/Effect/StockEffects/EnvironmentMapEffect.cs,
and ../xna4-decomp/reference/xna4/decompiled/windows/Microsoft.Xna.Framework.Graphics/
Microsoft.Xna.Framework.Graphics/EnvironmentMapEffect.cs. Both expose Texture and
EnvironmentMap through parameters; CNA omits them and uses private backing pointers.
No EnvironmentMapEffect source or tests changed yet. No push or merge.

The volume-source fix below is committed830e9a934; owner notified, no push.
The separate eight cubemap teardown errors prompted a five-kind sanitizer reproduction.
Ignored driver build-probe/repair-museum-sdlgpu-escape.mjs instruments the actual
SdlGpuRenderer.cpp TU and links the matching shared CNA build; it does not emulate
ownership. Before35311 compiled the TU but missed the versioned shaderc library at link;
before-v2 corrected that, reproduced four UAFs, but its volume used Reach and was refused.
Before-v3 job15559 corrected only the harness to HiDef: all five (2D, volume, cube,
2D target, cube target) report ASan heap-use-after-free in the sampled-state or target
wrapper destructors. Vulkan errors4/2/2/7/10 respectively. Peak419569664/events0/drift0.
Candidate50201: native build10540ms, instrumented TU28657ms, all five UAFs and Vulkan
errors gone. LSan still reports384 bytes/3 allocations with an unloaded-module frame
in every case; do NOT claim a clean full sanitizer run. Peak1603846144/events0/drift0.
No-texture LSan controls56082 reproduce the same384 bytes in both old demos and current
native CNA, through Vulkan instance initialization. They use allocation interception,
not a mismatched instrumented renderer overlay: first control46278 rejected that mix
at link because demos compiled-effects are off (failure retained). Slow unwinding
exposed that these standalone probes selected /usr/local/lib/libSDL3.so.0, while native
CTest/demos select the project's pinned SDL. Both paths/hashes are recorded; subsequent
drivers explicitly pin LD_LIBRARY_PATH to the project's SDL. Repeat ASan with that
dependency before finalizing. Do not attribute these leaks to the texture registry.
Eight native lifetime tests added, including retained states and failed constructors.
Native v1 hit the nonproduction audit on the test's unnecessary SDL_GPUTexture return
type; changed it to a boolean handle-presence predicate, preserving the same assertions,
without adding an allowlist or changing audit budgets. Active v2 job81541 builds/runs
native tests and original RT/deferred/bound lifetime fixtures with pinned SDL, private
display and6GiB. Initial lavapipe: renderer77PASS/1SKIP, graphics405PASS/24SKIP (429),
ten volume-layout CTests and all five legacy fixtures PASS, zero Vulkan errors.
Prepared external driver repair-museum-sdlgpu-escape-external.mjs compares84 runs;
the before library is still830e9a934 (no lifetime fix). Do not rebuild demos first.
Native81541 completed on BOTH lavapipe/RADV: renderer77PASS/1SKIP, graphics405PASS/24SKIP,
ten source-layout CTests and all five legacy fixtures PASS; zero Vulkan validation
errors. Includes the unchanged previously-leaking cube escape test and original120-frame
RT churn, deferred-source and bound-target suites. All70/352 previous renderer/graphics
outcomes unchanged; eight new renderer tests and77 newly selected graphics tests.
Offline mapping ASan11PASS, all five boundary gates PASS, peak1072603136/events0/drift0.
Pinned-SDL sanitizer final51978: five escaped-resource cases and all eight permanent
tests complete with no UAF/UB/Vulkan reports. LSan remains nonzero:384 bytes/3 allocations
per device, also the no-texture control,3072/24 for eight tests. Logs identify pinned
SDL's Vulkan instance initialization; no leak suppression, no clean-LSan claim.
Peak1320493056/events0/drift0. Both renderer code and permanent tests were instrumented.
Current sole heavy job6400, cna-museum-sdlgpu-escape-external-v1:42 before runs captured,
incremental demos rebuild running,42 after runs pending. Baseline includes the existing
SDL_GPU render_target.multisampling FAIL at MultiSampleMask0 (known limitation in
handoff.md §5; demo has no knownIssue marker). It must not be hidden or relaxed.
Other initial baseline outcomes: SOFTWARE12PASS/2SKIP, GLES14PASS,
SDL_GPU11PASS/2SKIP/1FAIL. Compare all statuses/checks/markers/exits exactly.
Next: compare a no-texture control, identify the residual allocation, run eight new
native lifetime tests, existing deferred/bound-target lifetime fixtures and broader
SDL_GPU graphics regressions on lavapipe/RADV; external before/after demos thereafter.
Candidate registers each live native texture state, releases/detaches remaining states
before device destruction, and preserves normal frame-deferred release. No public API
change, vendor edit, or inherited content ownership edit. All lifetime work uncommitted.

**Validated SDL_GPU volume-source repair, base CNA b0b0207e68b635b3fc1b97f05c8d9fc995514e52 / demos adbeeaf.**
External job82770 completed: build264820ms PASS;72 runs compare36 before/after outcomes
with zero status/check-count/knownIssue/exit deltas. Each phase: GLES12PASS;
SOFTWARE2PASS/10SKIP and SDL_GPU2PASS/10SKIP (existing dialect/capability gates unchanged,
not claimed as native source-layout coverage). Native ten-layout CTests above provide
the actual SDL_GPU regression, including4096 exact pixels per render case.
External peak4297715712/high1344/max0/oom0/kill0, source drift0.
XML v3/v4 comparison confirms all59 old renderer and352 graphics outcomes unchanged
on each driver; only eleven added mapping tests appear. No test or demo weakened.
Ready for a coherent source-layout repair commit; museum owns final tex-volshader
revalidation. Linux Vulkan lavapipe/RADV validated, not Metal/D3D12/mobile or a full
CNA aggregate. The separate pre-existing cube-escape teardown errors stay OPEN.
Exact next task after committing: run prepared ignored driver
build-probe/repair-museum-sdlgpu-escape.mjs before in a private6GiB/one-worker scope.
It instruments the actual SDL_GPU renderer TU and tests escaped2D/volume/cube/2D-target/
cube-target renderers; reproduce memory safety before changing lifetime ownership.
Preserve all inherited content/ISSUE-113 changes. No push or merge.

Expanded validation v3 completed (job97142): ten source-layout CTests PASS on each
lavapipe/RADV; existing native and portable two-volume fixtures PASS on both;
renderer58PASS/1SKIP each, graphics338PASS/14SKIP each. Offline ASan/UBSan/LSan10PASS;
all five boundary gates PASS, peak538288128/events0/source drift0.
NOT an overall green result: each graphics run emitted eight device-teardown Vulkan
errors in unchanged TextureCubeLifetimeTest.RenderTargetCubeRendererSurvivesATextureCubeBaseMove.
Review job61919 compares this lifetime against the still-old demos library before any
demos rebuild; evidence repair-museum-volume-review-v1-*. No source shader is involved.
The new offline test guard was too narrow for multi-renderer builds (standalone sanitizer
did execute all10). Corrected it to include CNA_RENDERER_PRESENT_SDL_GPU; native suite
must be rerun. Review also adds a16/17 sampler boundary regression: source layouts can
combine more than SDL's16 slots; SDL_CreateGPUShader asserts on overflow in debug mode.
Do not commit until this boundary and the Vulkan delta are resolved/documented.
Earlier expanded attempts retained: v1 configure audit mistook the test's printf tag
SDL_GPU_VOLUME_LAYOUT for a native API use (renamed only the tag, no audit bypass);
v2 build passed but standalone fixture missed -lSDL3 (link invocation corrected).
Review61919 completed: the isolated unchanged cube escape reproduces exactly eight
VUID-vkDestroyDevice-device-05137 errors on BOTH old demos and candidate libraries,
on lavapipe and RADV. LD_DEBUG proves the distinct loaded libcna paths; hashes recorded.
It is a pre-existing escaped-resource lifetime defect, not a shader regression, and
remains OPEN for the next batch (raw owner pointers outlive the SDL_GPU device).
New sampler-boundary test first failed10PASS/1FAIL, then the local16-slot guard repaired it.
Final native v4 job89186: build7826ms; ten layout CTests and both legacy fixtures PASS
on each driver; renderer69PASS/1unchangedSKIP (eleven new offline tests really enabled);
graphics338PASS/14SKIP and the same eight separately reproduced teardown errors each.
ASan/UBSan/LSan11PASS, five boundary gates PASS, peak1295589376/events0/source drift0.
Driver retains exit1 for the eight Vulkan errors; no suppression or all-green claim.
Current sole heavy job cna-museum-volume-external-v1, driver
build-probe/repair-museum-volume-external.mjs v1: captures36 before outcomes, rebuilds
the existing demos tree with one worker, runs36 after outcomes on SOFTWARE/GLES/SDL_GPU.
Do not change C++ sources during this job. No demo oracle or marker edits. No push/merge.

Visibility repair committed b0b0207e6; owner notified, foreign ownership edits
verified unchanged after commit, no push. Scope cna-museum-volume-before rebuilds
cmake-build-multi/libcna.so with one compiler, then runs the five isolated shader
layouts on lavapipe only with Vulkan validation, core dumps disabled and a60-second
per-case timeout. Driver build-probe/repair-museum-volume.mjs before; evidence
repair-museum-volume-before-*. Do not change sources while the baseline runs.
Baseline73354 completed: build126224ms/compile2641ms, native2dPASS4096pixels;
native_volumeFAIL4096/4096 with VUID07752 (2D view bound to sampler3D);
portable_volume and portable_uniforms bothSIGSEGV with descriptor-stage07988 and
unwritten-descriptor08114 validation errors; invalid_volume wrongly accepted.
Peak1399189504/events0, source/library drift0. No RADV baseline crash was attempted.
Candidate unifies GLSL/SPIR-V descriptor reflection, retains image dimensions and
logical units, preserves source GLSL Y-up versus portable bytecode's Y correction,
rejects unmappable sampler descriptors before GPU use and missing cube/volume
bindings before queueing. Empty resource snapshots stay explicitly empty.
Candidate34717 caught a missing InvalidOperationException include (build failed,
no runtime tests); corrected include only. Candidate-v2 job19487: build10292ms,
all5 original lavapipe casesPASS, zero Validation Error/VUID lines, exact4096pixel
matches, peak827277312/events0 and no drift. This is focused evidence, NOT closure.
Added10 permanent source-layout CTests (also multi-renderer) and10 offline mapping
tests, plus docs/sdlgpu-shader-effect-layout.md. No vendor or museum files changed.
Next: compile/run expanded tests, existing native and portable ShaderEffect fixtures,
affected renderer/graphics suites, offline ASan/UBSan/LSan, boundary gates and focused
external demos. Validate corrected cases on lavapipe before RADV; commit only after
regression evidence is reviewed. All SDL_GPU changes remain uncommitted.

**Validated DEF_PROP visibility repair, base CNA bb6a2f44ef64bef812a82941781d2454c00f1705 / demos adbeeaf.**
PBR follow-up below is committed bb6a2f44e; owner notified, no push. Visibility baseline:
new test exposes eleven helpers in the shared tree (nine in committed HEAD plus two
from the preserved, uncommitted ownership work); RootDirectory/Viewport controls pass.
Pure-C ContentSmoke before-v2 passes SOFTWARE and GLES (4634/4701ms), peak140267520,
events0. First before run used a different CWD from TitleLocation and could not find
its fixture; retained that harness failure, then matched the original CTest layout.
Candidate restores explicit public property/private helper sections; three path/key
methods are reachable only via CNA::Internal::ContentManagerAccess for existing C
adapter/media consumers. No path logic duplicated, no SharpRuntime macro change.
Private-header native/content job13768 completed, scope cna-museum-visibility-after:
build428414ms, visibility3PASS, existing content2053PASS/4SKIP in111972ms. All2057
existing outcomes exactly match pbr-final baseline. Peak3039920128/events0.
C API after76446: build190433ms, strict C compilationPASS, original ContentSmoke
SOFTWARE/GLES bothPASS5002/5158ms, peak2055286784/events0. No runtime expectation edits.
Focused demos job87409 (cna-museum-visibility-demos) hit the driver's20-minute build
timeout at662/958 steps, SIGTERM, no compile error and no tests run. Peak4063199232,
events0 including oom/kill0. Verified the scope inactive and no orphan ninja/compiler.
Resumed incremental build with a one-hour build timeout under scope
cna-museum-visibility-demos-v2, new evidence suffix demos-v2; original logs retained.
It rebuilds the complete demo binary
and checks C manager inspection, several managers, compiled Song and Video on both
SOFTWARE/GLES. Historical matching reports are captured separately, not mislabeled
as a fresh baseline. Evidence repair-museum-visibility-{after,capi-after,demos}*.
Job49071 completed: incremental build1244346ms, all8 demo runsPASS, peak2783576064,
events0. Against historical reports, six outcomes/check counts/markers are unchanged;
both several_managers runs changeFAIL->PASS because the preserved uncommitted
ownership work already changes Load-after-Dispose to ObjectDisposedException.
Do NOT attribute ISSUE-010 to this visibility batch or remove its marker.
Final layout correction moved the new helper to src/Internal/ContentManagerAccess.cpp
(same implementation). Job64839, scope cna-museum-visibility-final, rebuilds and reruns
native content, strict-C SOFTWARE/GLES and all8 focused demos in sequence. Evidence
suffix final; no source changes during this final validation.
Job64839 completed successfully: native build24018ms, visibility3PASS53ms,
content2053PASS/4SKIP142991ms; exact XML comparison covers all2057 existing cases
with zero outcome deltas against pbr-final. C API build11420ms, strict-C compile363ms,
SOFTWARE/GLES smokePASS5715/5445ms. Demo rebuild55072ms and all8 checksPASS again;
statuses/check counts/knownIssue markers match demos-v2 exactly. Combined scope
peak4299747328, high1961, max0/oom0/kill0 (memory.high throttling, not OOM).
Only nine access/call-site hunks are staged in the two shared files; their remaining
unstaged added/removed lines exactly equal the saved pre-repair ownership diff.
The new helper header/source, media call-site and three visibility regressions belong
to this batch. No demo oracle or marker changed; no full CNA aggregate claim and no
new sanitizer claim for this access-only repair. No push or merge.
Next task: commit this validated batch, then reproduce the SDL_GPU volume-source
layout failure below against a freshly rebuilt multi-renderer library.

Read-only preparation for the next memory-safety batch: museum SDL_GPU `tex-volshader`.
GLSL CompileProgram currently bypasses the SPIR-V resource remapper, hardcodes one
fragment sampler/one uniform per stage and never populates reflected resource snapshots.
The bytecode route already remaps descriptors and captures their original bindings.
This is a source-level root-cause candidate, not a newly reproduced runtime claim yet.
Important: the remapper also flips vertex Y for portable bytecode; source GLSL is
documented as compiled as written. Do not accidentally double-flip existing native
source shaders when unifying resource handling. Native set2 volume bindings also
need the actual sampler dimension, not an unconditional SpriteBatch 2D texture.
Prepared but NOT compiled/run: demos build-probe/museum-sdlgpu-volume-layout.cpp,
five isolated modes native2d/native_volume/portable_volume/portable_uniforms/invalid_volume.
It checks4096 exact pixels and uses asymmetric volume colors to detect orientation.
Prepared driver build-probe/repair-museum-volume.mjs runs each case under prlimit
core=0, a60-second timeout and Vulkan validation. Baseline/candidate phases use
lavapipe only; add RADV after the corrected lavapipe cases pass.
Do not launch it concurrently with the active demos rebuild. Its compile/source/
library hashes and every failing case remain in the baseline audit, not suppressed.
Further source review: preserve portable set0/binding0 SpriteBatch inputs as well
as set1 typed units; native set2 needs dimension-aware routing. Empty reflected
uniform/sampler lists must remain distinguishable from the old unreflected GLSL
fallback (otherwise a shader with no uniforms still receives nonexistent slot0).
The current 2D-white fallback is invalid for a missing cube/volume binding; do not
introduce another dimension mismatch while fixing the bound-volume reproduction.
Next baseline should run each mode separately with core dumps disabled, private
display and memory cap, first lavapipe (/usr/share/vulkan/icd.d/lvp_icd.json).
Official contracts reviewed: https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader and
https://wiki.libsdl.org/SDL3/SDL_BindGPUFragmentSamplers (dense per-stage bindings and
matching sampled texture dimensions). No SDL_GPU production changes in this batch.

**Completed PBR follow-up, historical evidence. Base CNA91612a0a3 / demosadbeeaf; commit bb6a2f44e.**
PBR batch committed91612a0a3a39605b594ec9c54a61eaa9ec50de9c; owner notified, no push.
Read-only audit: DEF_PROP and its DDATA/DGETTER wrappers occur in only ContentManager
and Viewport. Viewport already puts all macros in public scope. ContentManager's
RootDirectory macro changes access from private to public and leaks11 loader/lifetime
helpers until the next private label. New ContentManagerVisibilityTests.cpp reproduces
all11 exposures; property accessibility controls pass. Current baseline job26047,
scope cna-museum-visibility-before, driver demos build-probe/repair-museum-visibility.mjs
before: build14749ms PASS, visibility3tests=2PASS/1expectedFAIL; existing full content
suite finished2056tests=2050PASS/4SKIP/2FAIL. Both failures are PBR source-structure
audits still pinning the old WebGPU stride gate, removed in91612a0a3. No product
access fix yet. Active follow-up updates those audits for declaration normalization
and adds exact colored PBR pixel coverage, retaining the behavioral expectations.
Follow-up content job61391 passed2053/2057, same4SKIP,110490ms, peak1492205568/events0;
evidence prefix repair-museum-visibility-pbr-followup. Expanded pixel regression
exposed a genuine EasyGL bug: declared COLOR0 without tangent (stride36) is ignored,
while canonical60 produces tinted pixels. GLES/GL33 each fail3colored comparisons;
SOFTWARE/WEBGPU/SDL_GPU pass25. Native color-followup-v2 records this before state;
first color-followup build caught the fixture's incorrect use of polymorphic Color
as packed data, fixed to uint32 without changing pixel expectations (logs retained).
EasyGL candidate selects matching extended PBR shader/validation/binding inputs when
VertexColorEnabledEXT is true, not just stride60; shared local predicate, no shader
changes. New pixel control also proves disabling the color flag restores uncolored
output. Native job36906 color-followup-v3 completed: build9221ms and all five renderer
pixel CTests PASS (25comparisons each). Renderer tests WEBGPU66PASS/1SKIP,
SDL_GPU58PASS/1SKIP, GLES28PASS/1SKIP, GL33 22PASS/7SKIP. Graphics/PBR/vertex499 each:
WEBGPU479PASS/20SKIP, SDL_GPU467PASS/32SKIP, GLES497PASS/2SKIP, GL33 495PASS/4SKIP.
All six existing rigid/skinned/instanced PBR fixture runs PASS; helper ASan/UBSan/LSan
5PASS with no reports. Peak1426870272/events0/source drift0.
Browser build99568 PASS36332ms, peak461877248/events0/source drift0. Chrome40670:
25/25 with tier1 present and25/25 masked, zero page/uncaptured/validation errors,
peak468709376/events0/artifact drift0. Evidence repair-museum-webgpu-pbr-color-followup*.
External46958:54 demo runs, exact zero deltas (status/checks/knownIssue/exit): eachphase
GLES9PASS, SDL_GPU7PASS/2SKIP, SOFTWARE9SKIP. No oracle/metadata change. Build64718ms,
peak4302499840/high1887/max0/oom0/kill0. Five boundary gates PASS.
Final content job98717 (repair-museum-visibility-pbr-final): build9887ms PASS;
2053PASS/4unchangedSKIP,133442ms,peak3459633152/events0. PBR follow-up is fully validated
within the stated native/browser scope; no full aggregate or museum rerun claim.
At that checkpoint the next task was the pure-C baseline and visibility repair above;
the then-failing visibility test stayed outside the PBR commit. Unrelated dirty work
remained preserved.
Exact planned local repair: explicit public RootDirectory/private helper boundary;
keep C API asset-path/normalized-key routes and built-in media loader resolution via
a narrow internal friend accessor, not duplicated path logic. Current C API directly
calls BuildAssetPath/NormalizeKey (four calls), ContentMediaLoaders free helper calls
BuildAssetPath/ResolveExistingAssetPath. Other helper users already have the intended
ContentReader/BuiltinContent/ModelResourceRecorder friendship. Preserve unrelated
ContentManager/CnaCApiContent ownership edits; stage only new access hunks when done.

Phone browser follow-up remains read-only pending exact reference contract: missing
ErrorOccurred confirmed; Open silently returns on socket/listen/getsockname failures.
PushNotificationSender Post reports only send byte count and ignores recv result,
so browser socket queuing can report acceptance without a connected receiver.
Official Microsoft archived ff707848/ff707611 and property pagesff707802/ff708179/
ff707322/ff707957 (vs.110) confirm NotificationChannelErrorEventArgs properties:
ErrorAdditionalData Int32, ErrorCode Int32, ErrorType ChannelErrorType, Message String.
The correct enum name is ChannelErrorType, not museum's NotificationChannelErrorType.
Six names documented; numeric values not verified. No local Microsoft.Phone.dll found
in deps, repository references, NuGet cache or the two Wine prefixes; no guessed API
or Phone production edit made. Direct curl can read these Learn pages though web open
reports cache miss. Next Phone step: authentic enum metadata and browser baseline,
then failure-event and sender-completion fixes at the proper HTTP abstraction layer.

**Museum PbrEffect FIXED_NATIVE_BROWSER_VALIDATED, following CNA f8d8f06d0 / demos adbeeaf.**
HapticDevice implementation is committed974c1ac08; its matrix closuref8d8f06d0 and
demo workaround removaladbeeaf are complete. No push.
PBR minimized native baseline before-v3: GLES/GL33/SOFTWARE five comparisons PASS;
WEBGPU uses unlit fallback (metallic0/1 both204 vs canonical148/255), SDL_GPU rejects
the tangent-free declaration. Initial before/before-v2 fixture mistakes (state setter
spelling and unpacked polymorphic tangent vertex) were corrected, logs retained.
Candidate shares a declaration-driven rigid-PBR normalization helper; missing tangent
uses GL neutral(0,0,0,1), canonical48/60 streams retain resident GPU storage. No shader
or expected-value change. Both affected renderers route PBR by effect, not byte stride.
Native after-v2:20 exact comparisons each on GLES/GL33/SOFTWARE/WEBGPU/SDL_GPU PASS,
covering user, padded/reordered, buffered and indexed/nonzero-offset vertices, metallic,
roughness, emission and disabled-light controls. Peak791625728/events0; build18482ms.
Separate residual: FNA3D with SDLGPU driver rejects missingTangent0 explicitly both
before and after (exit2); not repaired or suppressed. Museum's FNA3D result may use GL.
Full driver exits remain1 because of this recorded residual, not falsely called green.
Final native job55522 completed:5 registered renderer CTests PASS; WEBGPU renderer67
=66PASS/1SKIP; SDL_GPU59=58PASS/1SKIP. Graphics/PBR/vertex suite499 per renderer:
WEBGPU479PASS/20SKIP, SDL_GPU467PASS/32SKIP. Existing rigid/skinned/instanced fixtures
all six executions PASS. Five helper tests ASan/UBSan/LSan PASS, no reports.
Peak1054932992/events0/source drift0. Earlier90844/79419 retained include-path and SDL
link-line harness failures; no product assertions were changed to pass them.
Browser build39133:50489ms, two preexisting warnings (32-bit hash constant, enum switch)
matching earlier webgpu-after-build evidence; peak495022080/events0/source drift0.
Chrome job84962:20/20 comparisons each with tier1 available and masked, zero page,
uncaptured or validation errors; peak459329536/events0/artifact drift0.
External job51000: nine PBR demos each SOFTWARE/GLES/SDL_GPU before/after (54 runs).
GLES9PASS unchanged; SOFTWARE9 historical SKIP unchanged; SDL_GPU6PASS/1FAIL/2SKIP
becomes7PASS/2SKIP. The sole behavior delta is metallic_roughness refusal->PASS,
checks0->1, exit1->0. The audit intentionally exits1 on any delta; reviewed changes
are exactly this intended repair. No oracle, tolerance, knownIssue or restriction edits.
Build67824ms, peak4307984384, high1846 but max/oom/kill0. SOFTWARE native pixel tests
above genuinely run; skipped external demos are NOT counted as passes. Those older
SOFTWARE restrictions need separate validation, not blanket removal in this repair.
Five platform boundary gates PASS. One compiler/private GPU/MemoryMax6GiB throughout.
No full aggregate, museum rerun, or new Windows/macOS claim; museum owns revalidation.
Evidence prefixes repair-museum-pbr-* and repair-museum-webgpu-pbr-* in demos reports.
No heavy job remains; commit only this PBR batch, retain all inherited dirty work.
Next independent item: browser HttpNotificationChannel ErrorOccurred contract and
failure event. ISSUE-131 remains owner-reserved by handoff.md section6; asked owner
again2026-10-22 whether the new museum request lifts that explicit reservation. Do not
implement/generate the method while that choice remains unanswered.

**Completed CNA-DEMOS-ISSUE-009 / museum HapticDevice:** repair follows `c2664bad6`.
Fresh public-header test fails before the patch with unique_ptr's sizeof(incomplete
IPlatformHapticDevice); its preexisting noexcept trait passes. Native baseline36
haptics tests PASS23177ms (before-v2). First baseline tried a not-yet-built focused
input binary and returned ENOENT; retained as before, not claimed as a test pass.
Candidate moves the default constructor into HapticDevice.cpp and explicitly retains
noexcept. New input tests deliberately never complete the platform type, cover local
and member construction, move and idempotent disposal. No hardware is needed.
Completed job30700, scope `cna-museum-haptic-after`, driver demos
`build-probe/repair-museum-haptic.mjs after`: public-header Werror compile PASS1303ms,
native build16477ms, public consumer compile/link1031ms and runtime36ms PASS; full
input suite562 PASS24689ms, all560 preexisting cases retain their previous outcomes.
Two new public-header tests pass. Peak1007407104, no memory-limit events.
Consumer jobs59317/52602: both haptics demos on SOFTWARE/GLES pass before and after;
rumble checks1->2 and effects12->13 add default-member construction without removing
hardware restrictions or behavior assertions. Rebuild71030ms, peak3890540544/events0.
Evidence: demos build/linux-full/reports/repair-museum-haptic-*. No hardware claim.
One compiler, private display,6GiB cap. CNA974c1ac08, matrixf8d8f06d0, demosadbeeaf.
Preserve all inherited ISSUE113 and content/audio edits, including dirty docs.

**Museum external revalidation received before the PBR batch above.**
Owner's museum revalidation2026-10-22 at5b6aab981: NET-001/002 net23PASS0FAIL;
TRAY-00113PASS twice; WEBGPU-001/002 browser320rooms, none skipped, zero destroyed
texture/uncaptured errors, exit0. App-device workaround removed. NET-003 remains
source-inspected only in museum; no museum UBSan rerun. Museum owns its inventory.

**2026-10-22 MUSEUM-FNA3D-001 FIXED_NATIVE_VALIDATED:** follows HEAD `594d7d328`,
local next, no push. Its commit contains this checkpoint. Museum revalidation pending.
The sporadic museum SDL_GPU sampler crash now has a deterministic minimized CNA
reproducer on physical RADV PHOENIX. Draw/destroy a textured BasicEffect triangle,
then draw an untextured punctual-light BasicEffect: slot0 is a hole in the latter
shader's register layout, but FNA3D binds the full slot range and reuses the released
texture. ELF observation of the real SDL bind reports the stale pointer before a UAF.
Baseline before: exit90, second binding, released fragment slot0; peak382513152/events0.
Extended before-v3: all11 cases fail as intended (draw90, five texture kinds each
pixel93/vertex94). Before-v2 only had a test enum compile typo; retained separately.
Candidate-v2: all11 PASS using actual repaired Fna3dResources.cpp linked ahead of the
existing multi library; peak483000320/events0. Native drivers/dependencies are real,
not stubs. The candidate centralizes wrapper release in Fna3dDeviceState::ReleaseTexture:
clear matching native and CNA cached slots before disposal, preserve live slots,
honor retired device tokens. Covers2D/3D/cube/RT2D/RTcube; no FNA3D dependency edit.
Permanent fixture `modules/renderers/fna3d/examples/fna3d_sampler_lifetime_test.cpp`,
11 Linux/shared-FNA3D CTests. Native API observer is narrowly recorded in the existing
nonproduction SDL manifest (19 references), no widened audit rule.
Native job33681 captured successful before suites on SDLGPU/OpenGL, then configure
correctly rejected the not-yet-catalogued native test. After adding its manifest row,
job84679 completed under scope `cna-museum-fna3d-native-v2`, driver demos
`build-probe/repair-museum-fna3d-native.mjs v2`: before suites, incremental single-job
multi build,11 CTests, after suites, both-driver release controls. MemoryHigh4GiB,
MemoryMax6GiB, no swap, private display. Build84083ms,11 CTests PASS3270ms;
each driver112 renderer cases=111PASS/1unchangedSKIP and68 graphics cases PASS.
Exact before/after outcomes match by case. Twenty extra release controls (ten per
driver) PASS. Peak1840414720/events0/no source drift. Fifteen preexisting enum-compare
warnings in unchanged Fna3dCompiledEffectTests; no new source warning.

ASan/UBSan actual resource TU + permanent fixture: every lifetime assertion PASS,
no UAF/UB reported, but **sanitizer process exit1 due to unsuppressed dependency leaks**.
Do NOT call the sanitizer suite green. Final job51401 (`repair-museum-fna3d-asan-final-*`)
includes a device-only control with no application textures or calls to the new release
path: it already leaks8792bytes/513allocations. Ten compiled release cases leak
8896bytes/516allocations; draw9464bytes/517allocations. Logs identify MojoShader
`spv_add_attrib_fixup` allocations (profiles/mojoshader_profile_spirv.c:849/850) through
stock effect compilation, plus native SDL allocations in the draw. The independent
shader/native cleanup leak remains OPEN; FNA3D and its MojoShader AGENTS prohibit
AI-authored dependency fixes, so it requires human-authored dependency repair.
No dependency modified, no leak suppressions or disabled leak detection. Dependencies
are normal binaries, not comprehensively instrumented. Final sanitizer peak539738112,
events0/no source drift. Original asan logs retained too.

Consumer job92494 (`repair-museum-fna3d-external-final-*`) completed: final fixture
rebuild4540ms and11 CTests PASS4090ms; six demos each SOFTWARE/GLES before/after,
all24 executions PASS with identical status/check/knownIssue/assertions. These demos'
linux-full configuration does not compile FNA3D: this is cross-renderer nonregression,
not a museum/FNA3D consumer claim. Rebuild27012ms, peak4296372224, memory.high90,
max/oom/oom_kill0; no source drift. Native FNA3D validation above exercises real
RADV Vulkan and OpenGL. Five boundary gates PASS after regenerating only the platform
inventory's test-file counts; the narrow nonproduction manifest also passes.
All older content/audio/ISSUE113 dirt remains untouched. No full aggregate, museum
rerun, Windows/macOS runtime, or leak-free claim. No heavy job remains.
Next: reproduce the service sign-out/profile timing finding. Read-only prep:
GamerServicesBackend::signOut clears the authenticated slot on its worker before
the SignedOut event reaches the main-thread pump; tokenFor then throws locally.
The quoted error therefore does not itself prove an unauthenticated network request.
Inspect GamerRequests/dispatcher lifecycle and test the real service backend, not
only the synchronous fake, before deciding the repair or XNA exception contract.

Mixer publication repair committed as `844e3ce41eff298e3cc17011706b3de87e06a13f`,
local next, not pushed. **MUSEUM-AUDIO-002 FIXED_NATIVE_SANITIZER_VALIDATED:** pending-pan static
teardown. The SDL3 mixer has no process-exit hook; its selected audio device may
still run when SoundEffect's static cleanup drains queued pan states. CnaMixer's
separate engine already registers a first-use exit hook to close its device before
preexisting statics. A lock in the pan destructor alone does not stop future enqueue.
Baseline55798 completed: active and recreated mixer cases fail deterministically
because generation/stopped callbacks show no shutdown before static cleanup;
explicit-close control passes. This reproduces ordering, not the museum's actual
TSan delete/read trace. Candidate registers DestroyMixer on first initialization,
stopping/joining the device and retiring all tracks before earlier statics destruct.
After53523: all three cases PASS plain and TSan, peak351772672/events0/no drift.
Final job83680 completed, scope `cna-museum-mixer-exit-final`, drivers demos
`build-probe/repair-museum-mixer-exit.mjs final` then `repair-museum-mixer-exit-native.mjs final`.
Instruments actual AudioMixer.cpp
and SoundEffect.cpp (preserving inherited ownership work), with a public/native
fixture checking mixer generation invalidation and stopped callbacks before static
cleanup, real long/short fire-and-forget pan sounds, first-callback synchronization,
and active/recreated/explicitly-closed mixer cases. No arbitrary sleep. Permanent
fixture is `tools/audio/mixer_exit_order_test.cpp`, three CTests. Final job adds
ASan/UBSan/leaks: all three cases PASS under each of plain/TSan/ASan+UBSan+LSan,
peak454717440/events0/no findings or source drift. Native rebuild13488ms; seven
publication/exit CTests PASS2930ms; audio775=771PASS/4unchangedSKIP8736ms,
media355/355PASS12239ms; both exactly match prior publication checkpoint by case.
MinGW actual AudioMixer TU compile1428ms; peak837533696/events0/no drift. Five
boundary gates PASS. No native Windows/macOS runtime claim.
Consumer job19835 completed, scope `cna-museum-mixer-exit-external`, existing
11-demo SOFTWARE/GLES consumer driver with suffix `exit-final`:all44 executions PASS,
no status/check/knownIssue/assertion delta or source drift. Rebuild59330ms,
peak3862011904/events0. No heavy job remains. SoundEffect.cpp has not been modified
by this repair; its inherited ownership work remains uncommitted. No full aggregate,
museum run, native Windows/macOS audio runtime or physical listening claim.
Next investigation: FNA3D SDL_GPU deferred sampler lifetime. Read-only source evidence:
CNA caches raw native textures in boundPixelTextures_/boundVertexTextures_, but its
resource destructors do not invalidate them; Fna3dCompiledEffect::ApplyNativeSampler
can reuse these caches. FNA3D FreeTextureHandle also releases the SDL texture without
clearing native sampler arrays. Neither observation yet proves the museum crash's
exact cause. Need a focused reproducer before selecting the repair layer.
Dependency source is `/home/robertvokac/deps/FNA3D/src/FNA3D_Driver_SDL.c` (not
the old _SDLGPU.c spelling); configured by cmake-build-multi. Its AGENTS.md/CLAUDE.md
explicitly prohibit AI-authored code contributions and allow issue identification.
Do not edit FNA3D/MojoShader sources; investigate CNA ownership and report any
dependency-only repair requiring a human author. No vendor changes made.

Phone repair committed as `20fe6efd26661ef1f19651d4c1eeeeac30ae8cee`, local next,
not pushed. Completed batch **MUSEUM-AUDIO-001 FIXED_NATIVE_TSAN_VALIDATED**,
callback publication in the SDL3
mixer adapter. Pending-pan static teardown remains a separate open root cause.
Baseline fixture calls actual adapter TU and real SDL: while mixer is locked,
control updates prematurely replace callback/userdata; deterministic mix and stopped
checks both fail (first0/second2). First TSan attempt caught a test-fixture lock-order
mistake (resource creation while holding mixer); moved setup outside the lock.
Before-v3 real audio-thread stress then reproduces the museum's exact data race,
MixCallback:245 read vs SetMixerTrackMixCallback:559 write, exit66. No suppression.
Fix publishes both tuples under the owning native mixer lock and makes explicit
Stop follow mixer->track order, preserving callback-side replacement without inversion.
Permanent fixture `tools/audio/mixer_callback_publication_test.cpp`, four CTests:
mix, stopped, fixed-workload real audio stress, reentrant stopped-callback replacement.
Final76516: all four PASS plain and TSan, peak346636288/events0/no drift.
Native20876: build13767ms, four CTests PASS2465ms, full audio8692ms/media12245ms,
MinGW actual adapter compile1131ms. Peak1170313216/events0/no drift. Existing
audio outcomes775=771PASS/4unchangedSKIP and media355/355PASS match the previous
full native checkpoint exactly, case by case.
Five platform boundary gates PASS. No native Windows/macOS audio runtime claim;
real SDL audio thread uses dummy output, not a physical speaker/listening check.
Consumer job90674 completed successfully, scope `cna-museum-mixer-external-final`,
driver demos `build-probe/repair-museum-mixer-external.mjs final`:11 demos on SOFTWARE
and GLES before/after rebuild, including standalone C sound-effect client. All44
executions PASS with identical status/check count/knownIssue outcomes; no assertion
or source drift. Rebuild59025ms, peak4109045760 bytes/events0. Reports
`repair-museum-mixer-external-final-*`. No full CNA aggregate or museum runtime
claim. Preserve inherited audio ownership changes in SoundEffect.cpp. No push.
No heavy job remains. Next: reproduce process-exit device/callback shutdown ordering
behind the pending-pan static teardown race, using the existing CnaMixer exit hook
as design evidence; do not just add a lock to a destructor while callbacks can
still enqueue after that destructor returns.

Latest checkpoint: **MUSEUM-PHONE-001 FIXED_LINUX_TSAN_AND_WINE_VALIDATED**.
Repair follows5b6aab981 on local next; its commit contains this checkpoint.
Native Windows/macOS and museum repro validation remain unclaimed. The
external job35138 completed successfully: both phone demos preserve all outcomes
(9+2 checks) on SOFTWARE and OPENGLES3. Rebuild142706ms, peak4305600512 bytes;
memory.high2358, max/oom/oom_kill0, no source drift. Reports
`repair-museum-phone-external-*`. This replaces the historical "current job" below.
Windows control `repair-museum-winsock-control.log` measured shutdown=-1/10057 and
accept still waiting; closesocket then releases it. An initial event-wait listener
candidate passed complete100 and idle2000 under Wine, but partial body still hung
in blocking recv (bounded45s test termination, report `phone-windows-after-*`).
Candidate now uses cancellable Windows FD_READ/FD_CLOSE waits as well, preserves
socket ownership until worker exit, and explicitly unregisters socket events before
releasing their handles. Same fixture has real Winsock import observers; it waits
for the first receive continuation, not an arbitrary delay.
Job69392 (`phone-windows-after-v2`) completed: all five cases PASS against actual
channel TU and real SharpRuntime/core archives; compile7320ms/link67ms, complete1899ms,
idle2800ms, partial386ms, partial-header296ms, truncated495ms. Peak598634496/events0.
Linux final job94627: all five cases plain and TSan PASS; five native CTests PASS;
all18 existing phone tests PASS, exact outcome parity with previous full native
aggregate `repair-occlusion-native-full.xml`. Build19067ms, CTest3382ms, phone907ms,
MinGW actual-TU compile3721ms. Peak974692352/events0/no source drift. No sanitizers
were claimed for uninstrumented SharpRuntime/SDL dependencies.
Final external job19798 rebuilt against the final source and preserved both phone
demo outcomes (9+2 checks) on SOFTWARE and OPENGLES3, no knownIssue changes.
Build24649ms, peak3663585280/events0/no drift. Reports `phone-external-final-*`.
Tests retain real recv/WSA waits; partial requests cancel, EOF-truncated bodies are
rejected, complete queued bodies survive Close, registry/URI disappear and repeated
Close does not reclaim a reused handle. New CTests cover native Linux and MinGW64;
no fake native Windows claim. Final CMake-only adjustment limits MinGW wrappers to
64-bit import spelling; no source behavior changed. No full CNA aggregate rerun.
Next independent task: SDL3 mixer callback/userdata races, then pending-pan teardown.

Museum-owned inventory now confirms the browser tour at5b6aab981:320 rooms,
zero destroyed-texture/uncaptured errors; the unrelated PbrEffect finding remains.
It also records successful museum net/tray revalidation. These are consumer-reported
results, not additional runs performed by this agent.

**2026-10-22 MUSEUM-WEBGPU-001: FIXED_BROWSER_AND_NATIVE_VALIDATED**, awaiting museum revalidation.
This repair follows CNA `93baa9363` on `next`; its commit contains this checkpoint.
The existing museum browser artifacts are not modified. CNA had no configured browser
tree, so `cmake-build-wasm-webgpu` is now its persistent Emscripten WEBGPU variant,
reusing the existing `.sdl-prebuilt-emscripten` and shared ccache. Configure succeeded.
Baseline build30563 completed0 in1025714ms, no source drift, peak529199104/events0.
Baseline browser72828 reproduced the exact rgba16unorm/tier1 JavaScript TypeError in
both runs (adapter feature available/hidden), with no reached success summary. This
was a successful reproduction of failure, not a passing product test. Peak436781056.
Candidate requests tier1 in browsers, keeps native TextureFormat16bitNorm, and gates
all six16-bit normalized probe formats before creating a texture. Exact renderer
factories refuse unsupported formats with NotSupportedException. The public XNA
preferred-format fallback is preserved, as required by shipped RenderTarget2D's
GraphicsAdapter.QueryFormat call and existing RenderTargetFormatAgreement tests.
Fixture now checks that distinction explicitly rather than requiring the public
preferred-format constructor to throw. Its original baseline never reached those
assertions because the capability probe already terminated the page.
After build56744:0/48084ms/peak401289216. Browser18939:10/10 PASS available and10/10 PASS
with feature hidden, actual device feature enabled/disabled respectively. Complete
capability report, exact2D/cube factories, public preferred-format selection and all16
readback pixels checked; no JavaScript/uncaptured GPU errors. Peak422395904/events0,
no source/artifact drift. Actual Chrome/private Vulkan runs, not Node-only Wasm runs.
Reports are demos `build/linux-full/reports/repair-museum-webgpu-{before,after}-*`.
Reusable regression driver added as `scripts/run-webgpu-optional-format-test.mjs`
(Node22+, existing browser build and optional report directory as arguments). Linux
must use the private GPU runner. No museum executable/reproducer run is claimed.

Native job47658 completed0: existing multi-renderer library/graphics/renderer targets
built in656309ms; native fixture9PASS; WebGPU renderer suite67=66PASS/1SKIP; format and
capability checks28PASS on each of WEBGPU/OPENGLES3/SOFTWARE. The57 previously recorded
WebGPU effect cases have identical outcomes, including the Reach volume-texture skip;
the10 reflection cases also pass. All28 format/capability outcomes match the previous
full native aggregate. No full aggregate or museum browser tour rerun is claimed.
Native peak2134278144 bytes, no memory events or source drift. Seventeen warnings are
in unchanged HEAD test sources (two MorphTargetEXT nodiscard calls and fifteen FNA3D
enum comparisons); none in changed source. Browser builds before/after have the same
six existing warnings (32-bit size_t constant truncation and unhandled draw enum cases).
Reports: demos `build/linux-full/reports/repair-museum-webgpu-native-*`.
The committed browser driver itself also ran successfully (job10085):10/10 available,
10/10 absent; no JavaScript/GPU errors or artifact drift. Its retained reports are in
`build/linux-full/reports/repair-museum-webgpu-portable/`.
Optional-format repair commit: `b6fc8e441`, local `next`, not pushed.

**MUSEUM-WEBGPU-002: FIXED_BROWSER_AND_NATIVE_VALIDATED**, awaiting museum revalidation.
This repair follows `b6fc8e441` on local `next`; its commit contains this checkpoint.
Before browser fixture:7checks/3FAIL,
three destroyed-canvas-texture submission errors. Before native fixture:SIGABRT from
wgpuSurfaceConfigure, `SurfaceOutput must be dropped before a new Surface is made`.
Reports: demos `build/linux-full/reports/repair-museum-canvas-before-*` and
`repair-museum-canvas-native-before-*`; source/artifact drift empty, memory events0.
The repair uses cached owned browser backbuffer storage and only acquires/copies/submits
the canvas at Present. Surface reconfiguration first drops native acquired output.
Expanded14-check fixture covers partial-draw preservation, resize and MSAA as well.
First candidate browser build65662 passed in44370ms, peak402931712, no events/drift.
Browser job62634 was the expected failing baseline; job48287 the native abort baseline.
Intermediate browser71049 passed14/14. Source review then corrected Present's copy to
AcquireFrameEncoderEXT, not BeginReplayEXT (the latter may map occlusion results/yield),
and added a pending-query frame. Final build28474 passed0/45551ms/peak397864960/events0.
Final browser27103 passed16/16 and exact screenshot pixels2880/2880; no JS/GPU errors or
artifact drift. Actual MSAA4 was applied. Screenshot is under demos reports
`repair-museum-canvas-final-browser/`, viewed and independently pixel-checked.
Native23052 completed0: build64863ms, fixture16PASS, existing clear/readback10PASS,
MSAA7PASS, RenderTarget2D13PASS, cube18PASS, occlusion3PASS. WebGPU suite66PASS/1SKIP and
format/capability28PASS each on WEBGPU/GLES/SOFTWARE. No build/fixture compiler diagnostics;
peak1478066176/events0/source drift0. Only private-header comments changed afterwards.
Browser regression build86381 completed0/280539ms/peak398274560/events0/no drift.
Seven existing warnings repeat the same two known messages (32-bit hash truncation,
unhandled draw enum cases); the coverage page adds one more inclusion of that header.
Regression job60945 passed: final canvas16/16 plus **all3840/3840 screenshot pixels**,
optional formats10/10 enabled and10/10 disabled, browser coverage9/9, EnvironmentMap4/4,
Skinned9/9, DualTexture4/4, AlphaTest4/4. No JS/uncaptured GPU errors or artifact drift;
peak479354880/events0. Native67 renderer and28 format cases per renderer have exact
outcome parity with the preceding optional-format checkpoint. Reports:
`build/linux-full/reports/repair-museum-canvas-regression-*` in demos.
No heavy job remains. No museum tour, full native aggregate, compiled browser Effects,
Firefox, native Windows/macOS or sanitizer run is claimed for this GPU-lifetime batch.
GPU validation and exact pixels cover the actual invalid-texture defect.
Current SDK header exposes TextureFormatsTier1, and Chrome's primary feature reference
identifies rgba16unorm as tier1, not the tier2 claimed by CNA's stale comments:
https://developer.chrome.com/blog/new-in-webgpu-142 . Native's extension remains distinct.
The pre-fix browser RequestDeviceOnlyEXT omitted tier1 and ProbeRenderTargetFormatEXT
created RGBA16Unorm without a feature guard. Canvas lifetime is a separate root cause:
frame encoding survives async capability/shader waits, not just backbuffer readback.
The two repairs remain separate commits and separate regression fixtures.

MUSEUM-NET-001: FIXED_NATIVE_VALIDATED (`cd9868526`), awaiting museum revalidation. Migration
now excludes the departed host's whole shared NetworkMachine. Client peer maps
cannot do this correctly because the star topology relays other machines through
the same upstream connection. No new retries or timing changes in production.
Before repair: two minimized real-ENet cases FAIL (both target DepartingGuest);
all 538 pre-existing network tests PASS. After repair: all 541 network tests PASS,
including both minimized cases and the new three-process/two-host-local case,
which proves promotion, rediscovery/reconnect and a post-migration payload echo.
The original one-local-host multiprocess case remains unchanged and passes.
The first expanded harness attempt failed because it read ACKs only from local
gamer zero; the actual Host identity can be the other local gamer. The harness now
reads both host inboxes and still requires two distinct survivors' exact ACKs.
Its pre-existing duplicate delete on the timeout path was also removed.
Final build 5558 ms, full net 45846 ms; peak 642084864 bytes, no memory events,
no source drift or build diagnostics. No full aggregate/GPU/sanitizer claim for
this networking-only change. Museum `tools/net_check.sh` was not run here.
Reports: `../cna-demos/build/linux-full/reports/repair-museum-net-before-*`,
`repair-museum-net-migration-after-*` (retained harness failure),
`repair-museum-net-migration-v2-after-*` (accepted).
Runner: `../cna-demos/build-probe/repair-museum-net.mjs`.

MUSEUM-NET-002: FIXED_NATIVE_VALIDATED (`04fb4978a`), awaiting museum revalidation. Public Join
in two real processes reproduced Lobby/events=0 before repair. The welcome now
carries Playing in an optional tagged suffix after guest flags; Lobby's exact old
bytes and the strict online-relay envelope stay unchanged. An old welcome defaults
to Lobby. Unknown suffixes remain ignored; truncated/invalid state is rejected.
The normal owner-thread state event is pumped before native EndJoin returns.
Shipped XNA `NetworkSession.GameStarted.add` invokes each new handler when Playing
(reference file under `../xna4-decomp/reference/xna4/decompiled/windows/`); CNA's
existing replay hook therefore delivers exactly one immediate notification after
Join, with no second notification on the next Update. The museum text's statement
that XNA does not replay is contradicted by that accessor; museum confirmation is
still required. No genuine Xbox/live-service reference run is claimed.
Six new tests PASS (codec, real ENet state/event timing and public two-process Join).
All 547 native network tests PASS; all previous 541 outcomes unchanged. Build
130821 ms, focused13 2103 ms, full net46543 ms, peak1182662656 bytes, no memory
events or source drift. The rebuild reports31 existing nodiscard warnings in the
unchanged NetworkSessionTests.cpp/NetworkSessionTypePolicyTests.cpp; no diagnostic
in a changed source. Reports: `repair-museum-net-welcome-before-*` (expected1FAIL),
`repair-museum-net-welcome-after-*` (accepted), under the report directory above.

MUSEUM-TRAY-001: FIXED_NATIVE_VALIDATED (`93baa9363`), awaiting museum revalidation. Sdl3Tray now
owns a heap-stable string per entry; all allocations precede native insertion,
replacement keeps the old label alive through the native update, and label storage
outlives SDL_DestroyTray. No vendor changes or public API changes.
Before: the actual Sdl3Tray TU against a retaining-SDL seam gives3FAIL/1PASS.
After:4PASS, including short/long labels, replacement,128 entries with vector
growth, callbacks and failed insertion. Dedicated CTest CnaTrayLabelLifetimeTests
passes and keeps the fake SDL exports out of every other test executable.
Real SDL on private Wayland and private D-Bus also passes:42 exact menu labels
after caller storage expired,42 after replacement, two dbusmenu callbacks and
clean exit. This uses the committed tools/platform/tray_dbus_harness.cpp and
tray_dbus_check.py, not the museum executable or the owner's panel/bus.
Platform suite530 =522PASS/8SKIP, exact case-by-case parity with the prior full
aggregate checkpoint repair-occlusion-native-full.xml. ASan/UBSan/float-cast/leaks
on the actual adapter TU:4PASS, no findings. All five platform boundary gates PASS
(inventory, classify, renderer audit, ratchet, hot-path lint).
Normal build23448ms, D-Bus522ms, platform8612ms, CTest3924ms;
sanitizer build21536ms/test37ms. Peaks635338752/538132480 bytes; no memory events
or source drift. Reports: `repair-museum-tray-before-*`, `-after-*`, `-sanitizer-*`.
Private portal processes log shutdown warnings when their private bus ends; the
tray harness itself exits0 and all assertions pass. No native Windows/macOS claim.

Reproduce the real integration check from CNA (no live desktop):
`tools/platform/run_gpu_tests_private.sh --exec dbus-run-session -- /usr/bin/python3 tools/platform/tray_dbus_check.py cmake-build-debug/cna_tray_dbus_harness`.

Canvas repair commit: `31a3d54b8`, local `next`, not pushed.

MUSEUM-NET-003: FIXED_UBSAN_AND_NATIVE_VALIDATED, awaiting museum revalidation.
This repair follows `31a3d54b8` on local `next`; its commit contains this checkpoint.
CNA-native C fixture calls the actual protocol TU and real queue/receive path, covering
every bit, duplicates, rollover and stale packets. Initial test registration was absent
because CNA's root project only enabled CXX; enabling C in the test's native Linux scope
fixed that harness error (no production change). Baseline78874 then reproduced UBSan
`protocol.c:492:53: left shift of 1 by 31 places cannot be represented in type int`.
Both literals changed to1u; after29501 passes26630 checks, build921ms/test566ms,
peak157011968/events0/no source drift. Narrow vendor change documented in
`third_party/enet/CNA_PATCHES.md`. First full-net driver had a JavaScript syntax error
and never built or ran tests; fixed and syntax-checked before relaunch.
Full regression completed in scope `cna-museum-enet-net-after-v2`, session89881, driver demos
`build-probe/repair-museum-enet-net.mjs net-after`: existing debug build/net harness,
UBSan CTest and full native network suite, one worker/6GiB/private GPU/private network,
owner UID and zero capabilities. Build2679ms, UBSan CTest494ms, full net46462ms:547/547PASS,
exact case-by-case parity with the preceding welcome repair. Peak1505333248/events0,
no source drift or build diagnostics. Reports `repair-museum-enet-*` in demos.
No heavy job remains. No museum net_check run, full CNA aggregate, other platform or
extra renderer run claimed for this transport-only, wire-format-preserving repair.

Next independent work: notification/mixer lifetime races; browser ErrorOccurred API.
ENet repair commit: `5b6aab981`, local `next`, not pushed.

Notification baseline in progress: actual HttpNotificationChannel.cpp plus standalone
public-API fixture linked against existing debug libcna. Plain100 complete-request
open/close cycles pass; the same TSan run reports no race (not proof of absence).
The deterministic partial-request case observes the listener entering its second real
recv, then calls Close: it hangs and its own bounded process is terminated after10s.
Report `repair-museum-notification-before-*`; no source drift, peak265203712/events0.
Before-v2 session59902 completed: both TSan complete-request and2000 idle cycles reproduce
the exact Close:223 write/Listen:180 read race, exit66; partial Close still times out.
Candidate passes all five cases plain and TSan (complete100, idle2000, partial body,
partial header, truncated100). Job27856 peak272269312/events0/no drift. Listener captures
the native-width socket handle; Close shuts down accepted work under the same mutex as
its close, joins, then releases the listener. Incomplete bodies no longer become queued
successes. Queued complete notifications are retained after Close. Fixture is now
`tools/phone/notification_lifecycle_test.cpp`; five standalone CTests registered.
The recv wrapper only observes entry; it calls the real syscall and parser.
Native35451 completed: build16918ms, five CTests PASS, all18 phone tests PASS, MinGW
actual-TU cross-compile PASS; peak1022484480/events0/no drift. Not Windows runtime proof.
Current sole heavy job:session35138, scope `cna-museum-phone-external`, driver demos
`build-probe/repair-museum-phone-external.mjs`. Captures old phone demo outcomes on
SOFTWARE/GLES, rebuilds cna-demos against current CNA, then compares both phone scenarios
exactly. One worker/private GPU/private network/6GiB. Do not edit C++ during this job.
**Before committing, audit Windows cancellation:** shutdown on a listening Winsock
socket is not a documented accept-cancellation mechanism; delaying closesocket until
join may change the old Windows behavior. Need a focused Windows/Wine control (or
event-based cancellation if required), not just the successful cross-compile. No
Windows runtime pass or notification repair closure is claimed yet.
Current HttpNotificationChannel source and tests read. Close writes listenSocket_ while
Listen reads it, and closes the fd before join; accepted connections are not shut down,
so a partial request can strand Close in join. Reads can also deliver truncated bodies,
contrary to the source's own stated contract. Need deterministic cancellation tests and
actual-TU TSan reproduction before repair. Do not infer a thread-safe public Open/Close
contract: Microsoft's instance methods are not guaranteed thread-safe, but the class
must synchronize its own listener. Browser ErrorOccurred is a separate public contract
repair; Microsoft Learn names the enum **ChannelErrorType**, not the museum's suggested
NotificationChannelErrorType. Reference:
https://learn.microsoft.com/en-us/previous-versions/ff707848(v=vs.110) .

Resource discipline: one heavy job and one compiler worker, shared ccache
`/rv/cnaccache` with base `/rv`, existing `cmake-build-debug`, MemoryHigh 4 GiB,
MemoryMax 6 GiB, no swap, private display and network namespace. No push or merge.

## Intake queue (not closure claims)

All entries below require reproduction/contract verification, except where an
explicit current result is recorded above. Source headings, not this shorthand,
identify the permanent museum evidence. Additional entries in the current file
take precedence over the shorter request summary.

1. Blocking: MUSEUM-NET-001 host migration; MUSEUM-NET-002 join in progress;
   tray entry-label lifetime abort; browser WebGPU tier-1 texture feature gating;
   browser WebGPU destroyed canvas texture submission.
2. Safety: ENet signed unsequenced-window shifts; notification Close/listener race;
   mixer callback race; pending-pan static teardown race; FNA3D SDL_GPU deferred
   sampler crash; post-sign-out profile race; Android half-float capability/readback;
   missing notification ErrorOccurred; DEF_PROP private/protected access leakage.
3. Runtime/graphics: frustum outside-ray clipping; WebGPU PBR lighting;
   SDL_GPU PBR no-tangent declarations; SDL_GPU volume shader layout/crash;
   WebGPU/SDL_GPU vertex texture fetch; HapticDevice incomplete-type constructor
   (overlaps CNA-DEMOS-ISSUE-009); demangled vertex-type diagnostics;
   tray support reporting; message-box default buttons; EnvironmentMapEffect texture
   reflection; render-target readback capability suggestion; surface-free WebGPU.
4. Storage: async End-once and owner checks, size/name/path argument types,
   directory IO exception mapping; SetAppNameEXT eager root resolution.
5. Pipeline: generated nested-name scope; TextureProcessor alias result type;
   Convert exceptions; AddDependency policy/exceptions; EffectMaterial validation;
   ReadRawObject<Int32>; truncated XNB wrapping; intentionally accepted XNB v4;
   Color/Rectangle XML reflection; zero bone weights; scanner changed-list result;
   serializer-attribute equality; embedded glTF images deployment; WAV comments;
   TaskItem equality; compiler-task library linkage; clean lock-file lifetime.
6. C surface: duplicate process CNA state; uncompressed CNB declared-size validation;
   MouseState/GamePadState ToString; achievement missing-key result;
   PresentationParameters defaults; devices.h desktop/Emulator wording.

Suggestions and documented intentional differences are not automatically defects.

Read-only preparation while the browser baseline compiles (no additional runtime claim):
`HttpNotificationChannel::Listen` reads the plain `listenSocket_` concurrently with
Close's replacement. Capturing the listener handle is necessary, but preserve its
ownership until the listener exits (closing before join also permits descriptor reuse).
The listener can also block in ReadRequestBody on an accepted connection; merely
shutting down the listening socket does not wake that recv. Cover partial requests and
Close before calling this lifecycle repaired. Header uses int for SOCKET and socket
failure tests use `< 0`; inspect Windows handle width/INVALID_SOCKET in the same audit.
`SetMixerTrackMixCallback` writes callback/userdata before the locking SDL setter, while
MixCallback reads them. The stopped-callback pair has the analogous pattern. SDL_mixer
offers MIX_LockMixer; existing CNA MixerLock is already used as a callback barrier by
PendingPanStateCleanup::DrainAfterMixerBarrier, but its destructor calls DrainClaimed
without that barrier. Audio files carry inherited ownership work: do not overwrite it.

The museum's already-fixed missing-asset diagnostic and disposed StorageContainer
entries remain historical evidence, not new repairs. No platform/service runtime
coverage is inferred from source review.
