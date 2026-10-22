# CNA demos repair campaign

## Campaign contract

Repair every actionable defect inventoried in `../cna-demos/docs/CNA_ISSUES.md`.
Preserve historical IDs and assertions; verify current behavior and XNA evidence
before changing production code. Work in coherent subsystem/root-cause commits.
Do not push or merge. Unavailable hardware/services must be stated explicitly.

## Current checkpoint (2026-10-21)

**2026-10-22 ISSUE-027 committed766d62fbd; validation accepted.**
Focused SW/GLES/GL33:14PASS each, including compiled/MRT paths. Default native
11041=10923PASS/118SKIP, exact old11027 parity. External47085 terminated2 with
parityAccepted:true:132 cases=123PASS/8existingFAIL/1SKIP, no deltas; eight082
failures remain unaccepted. Original HDR demo4PASS/checks4; six exact visuals PASS
with zero pixel difference. All ten screenshots individually inspected: HDR
preserves distinct additive levels and text, font/clear/checkerboard references match.
Final CTest57592 accepted5PASS/noSKIP24422ms, no input drift/events; peak148176896.
External peak193499136/events0; native peak4056010752/events0. No heavy job live.
Only numerical range handling changed; no new ownership/lifetime behavior, and no
new sanitizer run claimed for this batch. Prior full719 smoke remains historical;
final all-issue full campaign is still required. This batch did not remove any
knownIssue marker (027 had none). Product SHA766d62fbd560b8356f43b169c3ebfdf2ccad3a7e
contains only the range fix,14 regressions and handoff.027 is FIXED_VALIDATED;
matrix156 IDs now has107 dispositions (including2 platform-pending),49 remaining.
Exact next task: reproduce054 using unchanged DDS stream demo, then protect
short-read/position/truncation/ownership boundaries; see CUBE_STREAM_TRIAGE.md.
066 awaits the explicit toggle-contract choice; do not infer an answer. Preserve
inherited011/082 edits, no push/merge.

**2026-10-22 ISSUE-027 candidate; sole external47085 running.**
CNA HEAD30c872915 / demos2031ddd.003 closure is committed, Windows runtime pending.
Current027 baseline: SOFTWARE12FAIL/2PASS versus GLES/GL3314PASS; eight compiled-off
SDL skips explicit. Narrow SoftwareRenderer.cpp range helper plus two new native
regression files fix all14 SOFTWARE cases; focused after15346 passes14 each
SW/GLES/GL33, SDL6PASS/8SKIP. Existing066 C failures remain unchanged on SW/SDL.
No source drift or OOM; focused peak902090752 bytes. No027 commit/closure yet.
Native69993 completed0: build30717ms/no diagnostics, graphics3026=2936PASS/90SKIP,
full11041=10923PASS/118SKIP in1157090ms; exact old11027 outcomes unchanged plus
6PASS/8compiled-offSKIP. No source drift/events, peak4056010752 bytes. Scope dead.
Sole external47085 now runs repair-blend-range-external.mjs: original132 outcomes
with previously accepted003 checks9, original HDR demo/screenshots on four renderers,
six exact visual references. Private GPU/6GiB/no swap. Collect before any new heavy
job, inspect screenshots, then commit027 and update permanent status. Separate
existing082 failures remain failures, not a green full campaign. No027 closure yet.
See demos graphics/BLEND_STATE_RANGE_TRIAGE.md for fixture corrections, exact logs
and contract evidence.066 awaits an explicit legacy-toggle factor choice; no answer
presumed and it does not block027. Inherited011/082 work remains separate. No push.

**2026-10-22 ISSUE-003 product and metadata validation complete.**
CNA repair `3b6a2eaf3bc51a7cf44cf645b5cfa66494e0a6bb` contains only003
code/tests; inherited011 ownership and082 lifecycle changes remain uncommitted.
Metadata gate92221 completed0: incremental consumer build3894ms/no diagnostics,
registry719/0 problems, both original texture demos PASS on SOFTWARE/OPENGLES3/
OPENGL33/SDL_GPU (eight passes, checks1/9), five enabled focused/catalog CTests
PASS/no skips in17.01s. Catalog comparison proves only the obsolete003 restriction
was removed; all other metadata and frozen inputs unchanged. Peak1267585024 bytes,
all memory events0. The initial preflight used the older focus manifest and stopped
before building on the already-validated D3D9/multi-library changes; the successful
run uses the later accepted84921 manifest. No failure was suppressed.
Matrix:156 historical IDs,106 with dispositions (including003/101 requiring final
platform validation),50 without final disposition.003 still requires native
Windows/D3D9 execution; compile-only coverage is not a runtime pass. The last full
native/full-smoke checkpoints below remain historical, with separate011/082
failures unaccepted; focused passes do not replace that final campaign gate.
Exact next task: run the six prepared BlendSourceRangeTests and original066 C draw
scenario on the current linked libraries; investigate target-range and blend-toggle
contracts before the027/066 repair. No heavy validation remains live. No push/merge.


**2026-10-22 accepted compiled-on checkpoint84921.** Configuration12627ms0;
libraries95586ms0 (exact16 unchanged/reviewed Draco warnings, no suppression),
strict fixtures9453ms0. SDL_GPU22PASS2962ms, FNA3D22PASS3329ms, WebGPU22PASS2186ms;
no skips or source drift, peak2133086208 bytes/all memory events0. Existing
MojoShader/FNA3D patch helpers explicitly skipped; no dependency reset/fetch.
All six available Linux renderer identities now execute the22 binding witnesses
with compiled effects where supported; external SDL compiled-off refusal controls
also remain verified. D3D9 source/owner compile validation is recorded separately;
native Windows execution still pending. ISSUE-003 code/test batch is ready for its
coherent commit; Texture2D's sprite-only index patch excludes all011 ownership
changes, leaving those working-tree edits intact. Next record the repair SHA,
remove the obsolete demo restriction, refresh permanent status and continue the
next unresolved batch. No global campaign completion or push/merge claimed.

**Earlier checkpoint2026-10-22: final focus passed; SDL compiled-on84921 was live.**
CNA89cd0b202 / demosfbab2d9. Separate C Effect documentation/expectation correction
is committed and validated: SW/GLES34PASS each, SDL compiled-off27PASS, native11PASS.
ISSUE-003 product remains uncommitted/open; inherited011/082 changes remain untouched.
Final focus46333 completed0: consumer build73819ms/no diagnostics; registry719;
stronger22 allPASS on SW/GLES/GL33, SDL13PASS/9 compiled-offSKIP; original two
texture demos pass on allfour, rectangle checks7->9. No drift, peak1310486528/events0.
All eight screenshots individually inspected. Paint differences exactly match
seeded Update sequences (SW compatible685..687, GLES89, GL3388, SDL86); every
pixel matches the original palette/cell layout with zero tolerance. This is
scheduling-state reconstruction, not an exact cross-renderer reference claim.
See demos graphics/SPRITE_BINDING_FINAL_FOCUS.md for full evidence and limitations.
Final review found the same missing Immediate hook in D3D9. Narrow mode/flush
repair added; MinGW17526 five relevant units compile0/no diagnostics, including
allocating DirectX9Renderer, no drift, peak648994816/events0. No Windows runtime pass.
SDL compiled effects require no unavailable hardware here: existing patched
MojoShader suffices. Sole84921 now runs repair-sprite-binding-sdl-compiled.mjs:
existing cmake-build-multi, enables SDL compiled effects while retaining WebGPU ON,
strict shared-dependency no-op guards, one worker/shared cache/private GPU/6GiB/no swap.
Requires22PASS/noSKIP each on SDL_GPU/FNA3D/WebGPU. External compiled-off config
is untouched. All product/source inputs frozen; no simultaneous heavy job.
Exact next task: collect84921, investigate any real failure, then stage only003
code/tests (split Texture2D sprite identity hunks from inherited011 ownership),
commit the repair, remove the old rectangle restriction only after validation,
record repair SHA and permanent disposition; continue027/066 or next actionable batch.
Do not close003 from compiled-off skips or infer native Windows coverage. No push/merge.

**Earlier2026-10-22 continuation: multi-renderer observations collected; WebGPU Immediate defect confirmed.**
The former33697 build is terminal: configure0, libraries0 (712365ms), peak
2169597952 bytes, no memory events. Its strict diagnostic gate remains failed;
exactly16 existing Draco warnings were separately reviewed against unchanged
vendor hashes. Observer42676 did not rebuild: strict fixtures12036ms0,
FNA3D22PASS/noSKIP3970ms, WebGPU21PASS/1FAIL/noSKIP2706ms. No input drift;
peak469401600 bytes/no memory events. Both old scopes verified inactive/dead.
The failing `FailedImmediateDrawIsNotReplayedByEnd` observes no OnApply at Draw:
WebGPU does not implement SetImmediateMode, so compiled sprites wait until End.
Genuine XNA InternalDraw calls RenderBatch immediately, and RenderBatch applies
passes then publishes Textures[0]. Two additional pre-End assertions now protect
successful Draw timing without removing any previous assertion. Baseline35351
completed with FNA3D22PASS and WebGPU20PASS/2FAIL; build9194ms, runs2750/2109ms,
peak455569408 bytes/no events. Original assertions remain; both new timing checks
fail as intended. The renderer now implements the existing SetImmediateMode hook
and flushes staged compiled sprites before Draw returns. Driver19040 completed:
incremental library build23165ms0/no diagnostics, fixture build12102ms0,
FNA3D22PASS4237ms and WebGPU22PASS2841ms, no skips/drift; peak954060800/events0.
Driver: build-probe/repair-sprite-binding-immediate.mjs (before/after).
MinGW84891 also completed: all four D3D9/common translation units compile with
existing Windows flags and no diagnostics (5695/3404/5227/1570ms), no source
drift; peak669827072/events0. This is compile-only, not a Windows runtime pass.
Native38691 build completed0 in569153ms; the diagnostic guard stopped before
execution on two unchanged MorphTargetEXTTests EXPECT_THROW/nodiscard warnings
(lines146/147), not on a production failure. Both test and declaration match HEAD;
review retained in repair-sprite-binding-webgpu-native-observed-diagnostic-review.json.
No suppression/rebuild/source change. Observer58197 inventory25ms0, run5482ms0:
189 Sprite cases =169PASS/20SKIP/0FAIL, all22 new binding tests actually pass.
All20 skips are existing WebGPU-excluding renderer gates:11 rasterization,
five depth/projection, four Immediate sampler. They are not runtime validation.
No source drift; original build peak1123647488/events0, observer187244544/events0.
UnitTests.cmake places renderer-owned tests in CnaRendererTests, not the graphics
target. Build12544 completed0 in56918ms;15 unchanged FNA3D static_assert comparisons
between neutral and MojoShader enums produce GCC enum-compare warnings. Exact
source/line review retained, no suppression. Observer36123 ran actual WebGPUCompiled
suites in8234ms:57=56PASS/1SKIP/0FAIL, no drift, peak158949376/events0; build
peak907333632/events0. The one skip is SharedCubeAndVolumeSamplerContract: its
fixture constructs a default Reach device then asks for Texture3D. Not a hardware
or platform pass, and no claim of volume validation from this skipped fixture.
The two source-proven C demo expectations are now corrected without runtime
changes: bytecode Effect has no ShaderEffect source program; non-current pass is
INVALID_STATE with exact System.InvalidOperationException/STATE diagnostics.
C effects.h and docs/c-api/EFFECTS.md clarify both contracts. Original34 checks
and all other controls retained; source evidence remains in COMPILED_EFFECT_C_EXPECTATIONS.md.
Validation25303 completed0: actual C demo build224911ms/no diagnostics,
SOFTWARE34PASS71ms, GLES34PASS257ms, SDL_GPU27PASS249ms (compiled-off refusal),
and all11 native EffectApply checks PASS1103ms. Exact InvalidOperationException/
STATE diagnostics observed on both compiled-on renderers. No input drift;
peak1394274304 bytes/all memory events0. This corrects two demonstrated demo
expectation errors, not native runtime behavior; full-smoke reports remain historical.
C documentation committed separately as89cd0b202; demos correction/evidence as
fbab2d9. Two direct post-SpriteBatch checks are now added to the original rectangle
demo: Textures[0] equals the drawn canvas, and a valid one-pixel SetData is refused
with InvalidOperationException until explicit unbinding. Original seven checks
remain, so nine are expected. The old003 restriction is deliberately still present
until validation and the CNA repair commit are recorded.
Sole46333 now runs build-probe/repair-sprite-binding-final-focus.mjs: existing
cna-demos target/registry, strengthened22 on SW/GLES/GL33/SDL_GPU (only SDL's
nine compiled-off skips permitted), both original texture demos on each renderer,
and eight screenshots. One worker/shared cache/private GPU/6GiB/no swap. All
source inputs frozen during this run; generated319 consumer replacements audited.
Next collect46333, inspect every screenshot, then stage only003 code/test hunks
(Texture2D also contains inherited011 work), commit the product repair, record SHA,
remove the obsolete restriction only after passes, and update permanent issue/matrix.
Other011/082 failures remain unaccepted. No003 closure or push/merge yet.
ISSUE-003 stays open;011/082 changes and failures remain separate. No push/merge.

**Historical checkpoint2026-10-22: full smoke collected; multi-renderer33697 was live.**
Sanitizer83125 terminated0: configure6395ms, instrumented libraries123701ms,
strict original22 compile12762ms, run3422ms, inspect18ms. All22 PASS/noSKIP
under ASan/UBSan/float-cast/leaks, including actual SOFTWARE compiled effects.
Six relevant CNA translation units and129 Sharp Runtime units verified
instrumented; third-party libraries are not claimed instrumented. No build
diagnostics, source drift or memory events; peak1378291712 bytes.
Native68394 acceptance below remains valid (11027,10917PASS/110SKIP, exact
old11005 parity). Product003 is still uncommitted/open.

External consumer build1277 terminated0: full existing build45973ms without
diagnostics, registry46ms reports719 demos/0 problems. All319 listed consumer
artifacts frozen after rebuild. No source drift, peak4095909888/all events0.
A transient island-simulator build was observed and allowed to finish before
launch; no foreign process was stopped. Driver repair-sprite-binding-external-build.mjs
requires accepted native/sanitizer and records the deliberate generated-library
replacement separately from immutable source hashes.

External62600 terminated2: original132 cases on SW/GLES/GL33/SDL_GPU completed
with123PASS/8FAIL/1SKIP. Direct comparison with the preceding sprite-depth132
observations retains every identity, status, check count, knownIssue, failed
assertion and assertion label. No new delta, no source drift; peak168407040 bytes
and all memory events0. All eight existing082 callback failures remain
unaccepted; this is not a green suite. The comparator recognizes only the exact
removal of previously failing C++ labels because DemoContext::Check prints
failures only; CApiDemo prints both. Check counts, metadata, new failures and
all other label changes stay strict. Original depth audit remains unmodified.
Safety review initially rejected an ambiguous dry-run wrapper; it executed no
regression. The replacement has an explicit no-import --check-plan branch,
then passed host syntax/transform checks before this actual authorized launch.

Full66351 TERMINAL0 as a complete observation, explicitly accepted:false.
SOFTWARE761459ms:635PASS/33FAIL/48SKIP/3NOT RUN; OPENGLES3263603ms:
675PASS/34FAIL/7SKIP/3NOT RUN. Exact719 identities each; no frozen-input drift,
peak372277248 bytes/all memory events0. Six exact visual references PASS,
zero difference/tolerance, all screenshots individually inspected. Scope verified
inactive/dead before the next launch. All16 deltas per renderer audited against
the previous full staging-lifecycle checkpoint in demos
graphics/SPRITE_BINDING_FULL_SMOKE_AUDIT.md: SOFTWARE tenFAIL->PASS/threePASS->FAIL;
GLES eleven/three. Its only different delta is the accepted055 clone repair.
The three failed C scenarios and082 inner failure remain explicitly unaccepted.

Sole heavy33697: scope cna-repair-sprite-binding-multi, driver
build-probe/repair-sprite-binding-multi.mjs, existing cmake-build-multi.
One worker/shared cache/private GPU/4GiB high/6GiB max/no swap. Only three
runtime renderer gates broadened to Fna3d/WebGPU/DirectX9 in native/evidence
copies; every original assertion is byte-identical. Driver verifies that exact
change against the full-smoke frozen manifest. Configuration13966ms exit0 enables
actual compiled WebGPU; FNA3D already supports compiled effects. Both shared
dependency helpers explicitly report already applied/skipping; no vendor reset.
One Draco CMake CMP0148 developer warning is retained, not suppressed; do not
claim warning-free configuration. Shared-library build is live; no new renderer
test result yet. Inputs are frozen again; no concurrent heavy work/source edit.
Next collect33697, review diagnostics, then require22PASS/noSKIP on each
FNA3D/WebGPU. D3D9 cross-build and C expectation correction remain afterward.
No003 closure or marker change.
Continuation:33697 is still live. Both changed FNA3D/WebGPU translation units
compiled;16 Draco deprecated-capture warnings exactly match the original
diagnostic review and both unchanged vendor header hashes. The strict driver
will stop before tests after build success; do not call it warning-free.
Prepared repair-sprite-binding-multi-observed.mjs passes host syntax/transform
checks, UNRUN. It requires the terminal original build0 and exactly those16
diagnostics, keeps the failed gate, performs no configure/rebuild, and runs the
original22 fixtures on each FNA3D/WebGPU without allowing skips. Any different
diagnostic must be reviewed, not waived. No second heavy job launched.
Prepared repair-sprite-binding-d3d9-compile.mjs is syntax checked, UNRUN, and
requires that future accepted observed audit. It reuses existing MinGW flags
for four changed D3D9/common translation units, no configure/download/link or
native-Windows runtime claim. Product inputs remain frozen until33697 ends.

The full-SOFTWARE audit covers every719 demo.
Its C effect_core now exercises the enabled compiled branch:32PASS/2FAIL versus
the previous compiled-off27PASS refusal branch. Source-backed review identifies
two incorrect expectations, not yet corrected: source-program presence for a
bytecode Effect, and INVALID_ARGUMENT instead of XNA InvalidOperationException's
INVALID_STATE for a non-current pass. Permanent evidence/required follow-up is
docs/repair-evidence/graphics/COMPILED_EFFECT_C_EXPECTATIONS.md in cna-demos.
Keep original failures; correct/validate this demo and clarify C documentation
after the frozen validation, before claiming external closure. Existing011 C
Effect lifetime and082 callback failures remain independent and unaccepted.
Preserve011/082 work and owner decisions. All156 IDs retained,105final/51pending.
No push/merge. Prepared027/066 witnesses remain uncompiled/unrun.


**Latest checkpoint2026-10-22: native68394 ACCEPTED; sanitizer83125 LIVE.**
CNA status4f3ed89e5/demos35d2a3d; product003 remains uncommitted/open.
Native-unbound: build11472ms/no diagnostics, focused22=13PASS/9 compiledSKIP
2667ms, corrected DXT refillPASS342ms, graphics3012=2930PASS/82SKIP43532ms.
Full11027 completed1174074ms:10917PASS/110SKIP, no failures. Exact compare112ms
retains every original11005 identity/outcome, adds only13PASS/9 compiledSKIP;
no missing/changed old outcome. Inputs unchanged, peak3681579008 bytes, all
memory events0. Original47617 DXT failure preserved; fixture precondition repair
and its two added guard assertions are still own uncommitted003 work.

Old native scope inactive/dead, no native child remains. A transient host
ninja/cc1plus was observed before sanitizer launch and confirmed absent on the
next process check; nothing was killed and no second heavy job launched.
Sole heavy83125 scope cna-repair-sprite-binding-sanitizer, demos driver
build-probe/repair-sprite-binding-sanitizer.mjs, existing build-asan.
Native accepted audit is its hard prerequisite. Private GPU, one compiler,
shared cache, MemoryHigh4GiB/Max6GiB/no swap. Configuration6395ms exit0 enables
SOFTWARE compiled effects after exact shared-dependency safety checks; instrumented
library closure build now running. It requires original22 PASS/noSKIP under
ASan/UBSan/float-cast/leaks. No sanitizer result yet. Production/dependency
inputs frozen again; do not start another heavy job or edit frozen sources.

Next collect83125 and inspect diagnostics, all22 outcomes, memory and source
stability. Then external C API/demo ABI-consumer rebuild, focused/full external
regression deltas and visual comparisons, and available unbuilt renderer checks.
The separate082 C callback regressions remain unaccepted and owner choices
remain pending. Prepared027/066 source triage/six uncompiled witnesses are in
demos3e74913, outside the current frozen set; finish003 before that repair batch.
All156 IDs retained,105final/51pending. No product commit/closure/push/merge.


Continuation preparation while68394 remains live: read-only027/066 blend audit
is recorded in demos graphics/BLEND_STATE_RANGE_TRIAGE.md. Six new
BlendSourceRangeTests.cpp witnesses are UNCOMPILED/UNRUN, outside the frozen
native input set; no native registration, production edit or second heavy job.
Four Color-target cases and two exact HDR controls prevent an unconditional
clamp repair.066 also has differing legacy factor behavior across renderer
families; do not silently normalize the public CNAEXT contract. Both IDs remain
pending runtime verification. Finish003 validation before starting this batch.


**Latest continuation: native68394 LIVE; older live notes below are historical.**
CNA0d1a9bdfb/demosc07f1bd verified; old campaign scope inactive/dead.
84495 terminal: build0/897240ms, then diagnostic guard1; no tests ran.
All45 warnings (44 nodiscard, one volatile) exactly match the original baseline
and13 HEAD-identical files; no suppression or warning-free claim.
Reviewed47617 terminal1: focus13PASS/9 compiledSKIP/906ms;
graphics3012=2929PASS/82SKIP/1FAIL/40304ms. Exact old2990 comparison has only
ClassicTextureFormat.DxtAlphaBlocksSampleTheirOwnEncoding PASS->FAIL.
No old case missing;22 additions13PASS/9SKIP. No full-native run was reached.
Frozen inputs unchanged; peak672497664 bytes/all memory events0.

Genuine local XNA SpriteBatch.RenderBatch leaves Textures[0] bound, and
Texture2D.CopyData refuses SetData while bound. The DXT fixture wrongly refilled
the sampled texture without unbinding. Added identity/refusal assertions and
explicit unbind before refill. All original RGB/alpha assertions, bytes and
tolerances remain unchanged; original failing XML retained. No product guard
weakened. This fixture is own003 work, not inherited011.

Sole heavy68394, scope cna-repair-sprite-binding-native-unbound, demos driver
build-probe/repair-sprite-binding-native-unbound.mjs. One worker/shared cache/
existing debug tree/private GPU+network/caps0/6GiB/no swap. Exact fixture-only
diff verified and other inputs frozen. Incremental build11472ms exit0 without
diagnostics; original22 focus2667ms exit0; corrected refill342ms PASS.
Graphics3012 completed43532ms exit0:2930PASS/82SKIP, no failure. Full11027
and exact old11005 comparison are still running/pending. Host scope verified
active/running, CnaTests PID2417036, peak2646577152 bytes under6GiB.
Inventory audit:156 unique IDs exactly match all156 issue headings; no missing
or extra entry. Final dispositions:71 fixed,23 documentation,8 already fixed,
one platform-validation pending, one demo correction, one duplicate;51 pending.
Continuation2026-10-22:68394 still verified live; the long differential build
case PASS543003ms and the run has reached effect-compiler tests. Host peak
3176636416 bytes under6GiB. Prepared sanitizer prerequisite now points to
native-unbound-audit.json, with an unchanged hard requirement for successful
final compare/no drift; host syntax/transform checkPASS, actual run still UNRUN.
Next collect68394 and inspect the complete audit before launching sanitizer.
Sanitizer/external ABI rebuild+regression/visuals/unbuilt renderer checks remain.
Preserve011/082 and owner choices.003 open,105final/51pending; no push/merge.


Native84495 still live, build passed251/347;13 pre-existing Guide nodiscard
warnings now observed, all exactly present in original campaign build log and
unchanged HEAD test source. Strict diagnostic guard will stop before tests; no
warning-free claim. Prepared demos
`build-probe/repair-sprite-binding-native-observe.mjs` is syntax/transform checked,
UNRUN. After84495 terminal/build0 only, it verifies exact13 diagnostics and source
hashes, records review, runs native22/graphics3012/full11027 without rebuilding.
Initial wrapper syntax-only quoting error corrected before execution; no product
test ran. Sanitizer prerequisite now names future observed native audit. If more
diagnostics appear, inspect them; never bypass the exact check. No second heavy
job or production edit. All sources remain frozen while84495 lives.

Follow-up verified wait:84495 still live, native build passed146/347 without
observed diagnostics. Exact88 focused observations reconcile70PASS/9FAIL/9SKIP
to79PASS/9SKIP, only nine intended FAIL->PASS transitions. Five platform gates
PASS: inventory,956/16 classification, exact fna3d+sdl-gpu allowlist, ratchet0/0,
hot-path181/1769. Prepared demos driver
`build-probe/repair-sprite-binding-sanitizer.mjs` passes host syntax/transform
checks, **UNRUN**; requires accepted84495, then safely enables SOFTWARE compiled
effects in existing build-asan after shared dependency/stamp verification.
Actual CNA/Runtime ASan/UBSan/float-cast/leaks must give22PASS/noSKIP. No second
heavy job, sanitizer result, closure, push or merge. Source freeze still applies.

**ISSUE-003 retry focus ACCEPTED45982; native84495 LIVE.** Shared runtime
build148080ms, strict original22 compilation10008ms. SW1452/GLES832/GL33814ms
each22PASS, SDL2267ms13PASS/9 compiledSKIP. All original assertions/pixel equality
unchanged; source inputs stable; peak1653633024/all memory events0. This is
focused acceptance only, not003 closure or product commit.
Sole heavy84495 scope `cna-repair-sprite-binding-native`, demos driver
`build-probe/repair-sprite-binding-native.mjs`: existing debug build, one worker/
shared cache/private GPU+network, owner UID/all caps0,6GiB/no swap. Native22/
graphics3012/full11027 will compare every prior11005 outcome. Default native
compiled support remains OFF, expecting new13PASS/9 explicit compiledSKIP;
enabled compiled routes were separately runtime-tested above. Sources frozen.
Next collect84495; lifetime sanitizer, external C API/demo ABI rebuild and full
external/visual deltas, FNA3D/WebGPU/available cross-build checks remain. Preserve
011/082 and owner-pending082/131. No product commit/closure/marker change/push.

**Retry correction validating45982; demos evidence2fdf525.** Candidate adds
internal FlushPendingSpritesEXT/DiscardPendingSpritesEXT to distinguish failed
pass submission from renderer End; common deferred queue/Begin survives failure,
failed Immediate staging is discarded without replay. Six renderer families
delegate to existing flushes and clear only unsubmitted geometry. All original22
assertions unchanged; original pixel test registered natively byte-identically.
Sole heavy45982 scope `cna-repair-sprite-binding-retry-candidate`, demos driver
`build-probe/repair-sprite-binding-retry-candidate.mjs`: existing external target
`cna_shared`, one worker/shared cache/private GPU/6GiB/no swap, then22/four renderers.
No result yet. C API/demo ABI consumers are NOT rebuilt by this focused gate;
rebuild before external regression. Sources frozen; do not edit until terminal.
No003 product commit/closure/marker change. Next collect45982; native regression,
full external/visual/lifetime sanitizer and unbuilt renderer paths remain. Preserve
011/082 dirty changes and owner-pending decisions. No push/merge.

**Compiled baseline captured; all heavy jobs terminal.**48754 build721899ms
exit0, diagnostic gate exit1 on16 pre-existing Draco warnings; each matches
original campaign baseline. No suppression/vendor edit; no clean-submodule claim
because Draco nested Git metadata is absent (9598 preflight failure retained).
Corrected82479 strict6437ms: SW/GLES/GL33 each19PASS/2FAIL, SDL13PASS/8 compiledSKIP.
Frozen unchanged/peak472776704/events0. End cleanup repeats Apply, and failed
Immediate Draw is replayed at End. Pixel44928 strict2073ms: SW/GLES/GL33FAIL,
nontransparent controlsPASS, SDL capabilitySKIP; peak446980096/events0/unchanged.
All original22 assertions retained. No003 product commit/closure/marker change.
Next implement pending-submit/discard seams, preserve deferred front-end queue/
Begin on failure, prevent Immediate replay. Register original pixel witness
byte-identically. Preserve old native renderer-End recovery and texture lifetime
controls. Then22 focused/four renderer, native/full external/visual/sanitizer.
Detailed reports in demos SPRITE_TEXTURE_BINDING_TRIAGE.md. Preserve011/082 work.

Continuation:48754 remains verified live (tool plus host scope active/running;
current4263153664/peak4301078528 bytes,6GiB hard cap; OOM/kill/max0).
Existing external build passed1524/1878. No production/dependency inputs changed
and no second heavy job. Demos now has an additional **UNCOMPILED/UNRUN**
`SpriteCompiledRetryPixelTests.cpp`, outside48754's frozen21 and not yet native
registered. Its syntax-checked driver
`build-probe/repair-sprite-binding-retry-pixels.mjs` must run only after48754 is
terminal and its complete unchanged-source audit has been inspected. It compares
exact successful/retried pixels to detect duplicate queued geometry after a
recoverable Apply exception. No new runtime pass, issue closure or marker change.
Next remains: collect48754, baseline the pixel witness, diagnose/fix actual
compiled failures, then native/full external/visual/sanitizer acceptance.

**ISSUE-003 first focused gate ACCEPTED; compiled gate LIVE48754.**78015
terminated0: external build1678789ms, strict original16 witness build4948ms;
each SW/GLES/GL33/SDL_GPU gives13PASS/3 compiledSKIP (runs106/678/598/1662ms).
All original44 failing test cases (11 per renderer) become passing tests;
original eight passing controls remain passing. No frozen
input drift. Peak4298645504 bytes; high1262, max/OOM/kill0. This is focused
acceptance only, not003 closure or full regression/sanitizer acceptance.
FNA3D identifier collision correction is now APPLIED, with missing parameter
documentation; no FNA3D build claim. Five additional compiled lifetime/retry cases
are now native-registered byte-identically, still UNCOMPILED/UNRUN at launch.
Sole heavy48754: `cna-repair-sprite-binding-compiled-enabled`, driver demos
`build-probe/repair-sprite-binding-compiled-enabled.mjs`. Configuration7428ms exit0
enables SOFTWARE/EasyGL compiled effects in the existing external build. Both
shared dependency patch helpers explicitly reported already-applied/skipping;
no dependency reset or new clone. Build now running, one worker/shared cache/
private GPU/6GiB/no swap. It will record21 cases on each renderer, preserving
every failure; SDL_GPU compiled support remains OFF/eight explicit skips.
Poll48754; do not edit frozen production or shared dependency inputs. Diagnose
any new compiled error-path failures, then native/full external/visual/sanitizer
gates. All156 IDs retained,105final/51pending; inherited011/082 untouched by staging.
No product commit, push or merge. Report prefix `repair-sprite-binding-compiled-enabled-*`.

**ISSUE-003 candidate IN VALIDATION:** sole heavy tool78015, scope
`cna-repair-sprite-binding-candidate`, driver demos
`build-probe/repair-sprite-binding-candidate.mjs`. Existing external incremental
build, one worker, shared cache, private display,6GiB/no swap. Source inputs
frozen in `repair-sprite-binding-candidate-inputs.json`; do not edit production
while it runs. It recompiles all16 original witnesses because Texture2D and the
renderer interface layout changed, then records SW/GLES/GL33/SDL_GPU separately.
No pass claimed yet; compiled12SKIP cannot establish compiled-route coverage.
Candidate files: new internal SpriteTextureBinding header/source; SpriteBatch
header/source; Texture2D own spriteIdentity hunks (mixed with inherited011);
IGraphicsRenderer interface; narrow software/easygl/SDL_GPU/FNA3D/D3D9/WebGPU
sprite binding seams; three native byte-identical witness copies. No product
commit. Preserve all other011/082 dirty work. Explicit remaining audit: compiled
End failure/retry timing, enabled compiled runtime, extra lifetime/copy-assignment
and renderer-control tests, native/full external regression and ASan/UBSan. Do not
close003 on the first13 focused passes. No push/merge.
Prepared separately in demos: `SpriteCompiledBindingLifetimeTests.cpp`, five
UNCOMPILED/UNRUN cases, not in78015's frozen manifest. XNA End's Begin-pair reset
follows successful Flush; current renderer-End recovery may wrongly clear it on
a compiled binding failure. First reproduce with compiled effects enabled; do not
weaken the retry oracle. Existing multi-tree uses shared FNA3D source
`/home/robertvokac/deps/FNA3D`, reusable if enabling SOFTWARE/EasyGL support later.
Last host check confirms78015 scope active/running,917716992current/970432512peak
bytes under6GiB. No competing campaign job. Wait for terminal status, inspect the
candidate audit, then continue the outstanding gates; no product acceptance yet.
Static review while78015 runs found an unbuilt FNA3D compile-name collision:
DrawSpriteRunEXT's new `binding` parameter shadows its existing local vertex
binding. Apply demos `build-probe/repair-sprite-binding-fna3d-name.patch` AFTER
78015 terminates (two identifier-only edits), before compiling that renderer.
This patch is prepared, UNAPPLIED; do not claim FNA3D currently builds.
Next prepared serial driver: demos
`build-probe/repair-sprite-binding-compiled-enabled.mjs`, syntax/transform checked
on host, UNRUN. Requires accepted78015, FNA3D identifier correction, native
byte-identical registration of the five new compiled-lifetime/error witnesses.
It enables SOFTWARE/EasyGL compiled effects in the existing external tree and
records21 cases per renderer; only SDL_GPU compiled cases may skip. Read-only
preflight confirms shared133-patch MojoShader stamp matches tracked diff exactly,
and FNA3D final patch reverse-check passes. The driver refuses mismatch BEFORE
configure because the existing MojoShader helper has a destructive checkout
fallback; do not erase dependency changes. Full hashes/source evidence in demos
SPRITE_TEXTURE_BINDING_TRIAGE.md. No additional heavy job has been launched.

**ISSUE-003 supplemental checkpoint:** demos `82ee978` preserves seven lifetime/
stock-pass witnesses and three compiled-effect witnesses. Lifetime baseline:
sixFAIL/one retained-renderer destruction controlPASS on each SW/GLES/GL33/SDL_GPU;
compiled v2: strict buildPASS but all12 runtime observations SKIP because configured
compiled-effect support is OFF, reproduced=false. Original strict compiler failure
is retained. No003 runtime repair or closure, original six-case oracle unchanged.
Tool28520 is missing; its host scope is inactive/dead; no campaign heavy job lives.
Reports/limits are in demos graphics/SPRITE_TEXTURE_BINDING_TRIAGE.md. Next implement
lifetime-safe public binding and correctly publish after stock/compiled effect
passes; compiled skips are a validation gap, not success. Preserve011/082 work and
pending082/131 owner choices. No push/merge.

**Safe continuation checkpoint:** CNA product7097e1684/status2f6eac0f1;
demos closure23bbbaf,003 baseline d8ed678,082 evidence c07e948. All156 IDs retained,
105final/51pending. No heavy job is live:98308 terminal0,70430 terminal2 with
eight openly unresolved082 deltas,61003 terminal0 capturing20FAIL/4PASS under003.
This handoff-only checkpoint does not include product/oracle changes.
Next concrete task: inspect/extend003 lifetime and custom-effect ordering
regressions, then implement the smallest safe public TextureCollection binding
repair. Original six-case witness must remain unchanged. The existing queue's
retained renderer lifetime must survive wrapper destruction/moves. Preserve
inherited011 changes and pending082/131 authority choices. No push or merge.

**156 CLOSED;003 reproduced.** Product CNA7097e1684 fixes only SDL_GPU depth,
with its five native tests/bias fixture/reference row. Native98308 accepted
10904PASS/101sameSKIP (all11000 old outcomes +5PASS); external70430 completed132
with123PASS/8FAIL/1sameSKIP and exit2. All nine delta rows reviewed: one156 fix,
eight separately pending082 callback regressions;123 rows unchanged. The raw
comparator additionally flags the156 improvement because C++ successful checks
are not logged; the original audit remains unmodified/unaccepted. No marker or
oracle weakening. Matrix156IDs=105final/51pending; original155 IDs preserved.
Next actual baseline61003 TERMINAL0 confirms003: five failures/one empty-batch
controlPASS on EACH SW/GLES/GL33/SDL_GPU, strict build2210ms, runs78/387/338/909ms,
frozenunchanged, peak415399936/all memory events0. No003 product fix yet.
No heavy job remains. Next: design/test lifetime-safe public texture binding,
including custom-effect ordering and destroyed/moved wrappers, before editing
SpriteBatch/Texture2D. Preserve inherited011 and owner-pending082/131 choices.
No push/merge; documentation closure commits are recorded above.

**Native156 ACCEPTED; external70430 LIVE.**98308 terminated0: full11005 in
1323219ms,10904PASS/101unchangedSKIP. Exact comparison134ms retains all11000
old outcomes and adds only the five depthPASS; graphics2917PASS/73sameSKIP.
Frozen inputs unchanged; peak4301795328bytes, max/OOM/kill0. Sole heavy70430 now
executes the original132 external observations on four private-GPU renderers,
6GiB/no swap, no rebuild/oracle edits. It must expose pending082 C callback
regressions rather than accept them. Poll70430, inspect every delta, then commit
the independent156 repair if its scope is clean;082 remains separately open.
Next baseline003 is prepared but UNRUN. CNA2af199aad/demos16fb075; no push/merge.

**Evidence checkpoint demos16fb075:** committed only the already-run five original
ISSUE-156 pixel witnesses and projection triage (no product code or closure).
All five read-only platform gates passed: inventory, classification956/16,
ratchet0/0, renderer SDL allowlist exactly fna3d/sdl-gpu, and hot-path181 methods/
1767 sources. Native98308 remains the sole heavy job; source inputs unchanged.
Prepared003 fixture/driver remain uncompiled/unrun and uncommitted. Pending082
C callback policy is not bypassed. No push/merge. Poll98308 then run132 comparison.

**Continuation checkpoint:** sole heavy98308 is verified live (systemd scope
active/running, current4264685568bytes/peak4301795328,6GiB cap). Build21145ms,
original depth5PASS389ms, graphics2990 exit0/42391ms; full11005 still running,
currently XnaDifferentialBuildTest. Do not restart or claim full acceptance.
Prepared demos `build-probe/repair-sprite-depth-external-regression.mjs` syntax/plan
checks pass:132 original observations (116 sampler/effect +16 lifecycle), preserving
the final004 metadata corrections. UNRUN, requires accepted native audit first;
reports every status/check/assertion/marker delta and exits2 on regressions,
including the four already observed C callback failures. No oracle normalization.
Demos audio evidence now committed `0f3420e`; CNA remains `2af199aad`.
Read-only003 audit and six uncompiled/unrun public-state witnesses are in demos
graphics/SPRITE_TEXTURE_BINDING_TRIAGE.md and SpriteTextureBindingTests.cpp.
No003 production change; preserve queue lifetime safety and inherited011 before
designing public texture-slot retention. No second heavy job, push or merge.
Follow-up verified wait:98308 remains active/running, its CnaTests child is in
the existing XNA differential corpus with an actual fxc/Wine child. No restart,
timeout inference or second Wine use. Prepared003 demos driver
`build-probe/repair-sprite-texture-binding-before.mjs` passes syntax/transform
checks but is UNRUN; it records six witnesses on four renderers after the132-case
external observation completes (without accepting the pending C failures).
Next remains: poll98308 to terminal, inspect exact11005 delta, then run132 gate
serially under the same6GiB/private GPU policy. Preserve all failed reports.

**Latest:156 actual focused/bias/visual gates ACCEPTED; native98308 LIVE.**
External96584 build98700ms, original five depth tests PASS on each SW/GLES/GL33/
SDL_GPU (828/493/451/1517ms); frozenunchanged/OOM0, only three expected output
artifacts changed (cna-content/libcna/libcna_c_api). Corrected bias61930 retains
exactly the original72 PASS labels/outcomes (strict4564ms/run668ms). Visual98930
passes all original seven checks on four renderers; every RGBA pixel equals the
original correct SOFTWARE frame. All four repaired images actually viewed.
No SDL_GPU source commit or156 closure yet. Sole heavy98308 validates native
focus5/graphics2990/full11000+5 exact old outcomes, one worker/private GPU+network/
owner UID/caps0/shared cache/6GiB/no swap. Poll it; all inputs frozen again.
IMPORTANT: lifecycle-after72013 completed observations, NOT a passing regression:
12 C++ PASS, four C resource_lifecycle FAIL (one of30 checks each), exactly the
documented callback-before-disposed expectation. No knownIssue marker added;
owner C callback decision remains pending. C++ SpriteBatch guard choice and131
reservation also remain pending. Do not call the overall external suite green.
Next after native acceptance: broader original SpriteBatch/effect/external deltas,
resolve those explicit contract choices, then coherent graphics commits/closure.
Committed audio repair2af199aad remains validated; inherited011 untouched.

**Latest: ISSUE-156 candidate focused validation96584 LIVE.** HEAD2af199aad is
the committed audio-test repair. Native88430 accepted10899PASS/101sameSKIP;
audio sanitizer75848 accepted2PASS; event sanitizer83569 accepted6PASS/no findings
(build481ms/strict6825ms/run128ms/inspect52ms, frozenunchanged/OOM0). Original
external lifecycle17557 captured16PASS on SW/GLES/GL33/SDL_GPU; each C child has30
checks even though its outer demo report counts0. Before any external rebuild,
156 baseline49609 strict3141ms:five PASS on each SW/GLES/GL33, five FAIL on SDL_GPU.
Original seven-check visuals16525 reproduced only SDL_GPU depth failure; all four
images actually inspected, three controls pixel-identical and SDL differs only in
16000 pixels at x560..759,y20..99 (blue instead of red). Original depth-bias fixture
27937/v2 passed72/72 (strict4566ms/run1154ms). Initial27588 missing Game.hpp include
was a preserved harness failure, not a product failure; v2 adds actual consumer
includes/compatible ABI definitions without changing the original fixture.
Now candidate changes ONLY SDL_GPU CPU sprite Z/W projection and sets the existing
bias fixture's default layerDepth to0.5 to preserve coplanarity with depth0.5
geometry. Five original witnesses copied unchanged into native graphics tests
(not yet built there); XNA decision row added. No custom-depth policy change.
96584 is the sole heavy job: one external build, original20 depth witnesses on
four actual renderers. Next: corrected bias72, original four demo images/exact
pixels, native regression and external lifecycle/C deltas.082/C callback and
SpriteBatch post-disposal guard choices remain pending; no full external closure
may be inferred from156 focused success. Preserve011; no push/merge.

**Latest: audio test repair committed2af199aad; event sanitizer83569 LIVE.**
Audio sanitizer75848 accepted2PASS/no findings: build18679ms/strict8665ms/run398ms/
inspect29ms, frozenunchanged/peak3717730304/all events0. With native-v2 full10899PASS/
101unchangedSKIP and20/20 focus, committed ONLY DynamicSoundEffectInstanceTests.cpp
as2af199aad4d11053a7549bee01f9efff272cd9e0. Original failed30524 preserved;
no production audio change or historical issue closure. Sole heavy83569 now runs
original6 event/lifetime witnesses with actual graphics/Runtime instrumentation.
Next after acceptance: original external16, then156 five-case/four-renderer baseline
before any external rebuild. Pending C callback/SpriteBatch guard choices and
owner-reserved131 remain explicit. Inherited011 untouched; no push/merge.

**Latest: native88430 ACCEPTED; audio sanitizer75848 LIVE.** Native-v2 full11000
completed1508686ms:10899PASS/101unchangedSKIP. Exact comparison292ms retains all
10994 old outcomes and adds only the six expected eventPASS; audio775 outcomes
also match the full run. Frozen inputs unchanged; peak4297629696bytes/maxOOM0.
The original failed30524 audit remains preserved. Sole heavy75848 now runs actual
audio/Runtime ASan+UBSan+float-cast/leaks on both dynamic stress tests, one worker,
private GPU/shared cache/6GiB/no swap. Poll75848; then event sanitizer6, original
external16 lifecycle capture, and original156 depth20 baseline BEFORE rebuilding
external artifacts. Audio test-only commit awaits sanitizer acceptance; graphics
candidate and inherited011 must remain separated. No issue closure or push/merge.

**Issue-specific authority boundary revalidated:** handoff.md§6 explicitly reserves
ISSUE-131's real ray/frustum implementation for the human author and forbids
generating it/removing the exception. Matrix131 now records BLOCKED with no final
disposition instead of generic PENDING_VERIFICATION. Asked owner whether that
reservation is lifted for this campaign; no code/oracle change or new runtime
claim. Existing boundary-start improvement from containment remains preserved.
This does not block native88430 or the other51 pending issues.

**Latest: original native30524 TERMINAL1; repaired test validating in88430.**
Full11000 completed1442390ms:10898PASS/101unchangedSKIP/one audio coverageFAIL.
Exactly six new event tests PASS; only old-outcome delta is the recorded
`callsThrown==0` stress assertion. Frozen sources unchanged, peak4297887744bytes,
max/OOM/kill0. Original failed XML/log/audit retained, never accepted as green.
After termination, changed only DynamicSoundEffectInstanceTests.cpp:16 rendezvous
cycles require opposite-format rejection (eight each direction), exact count and
Playing-state checks; all original3000 free-running submit/Play/Stop cycles remain.
No audio production change, sleep, retry or weakened assertion. Sole heavy88430
(`cna-repair-resource-event-native-v2`) now builds one worker then requires20/20
focused iterations, audio module, original6 event tests, graphics2985 and full11000
exact parity against004. Same private GPU/network/caps0/shared cache/6GiB/no swap.
Poll88430; inputs frozen again. Only after full acceptance may the prepared event
sanitizer use this v2 audit, followed by original external lifecycle baselines.
See demos docs/repair-evidence/audio/DYNAMIC_STRESS_SCHEDULING.md. Test candidate
remains uncommitted; isolate it from graphics/011 in its eventual coherent commit.
88430 progress: build11796ms;20/20 stressPASS804ms;audio771PASS/4unchangedSKIP9070ms;
focus6PASS218ms;graphics2912PASS/73SKIP41221ms;full11000 LIVE. The repaired stress
also passed inside that full run, not a full acceptance claim. Exact next after
full acceptance: prepared audio stress sanitizer2, then event sanitizer6, then
old external16 capture. All three drivers syntax/transform checked, UNRUN.
During the verified native-v2 wait, added source-backed156 triage in demos
docs/repair-evidence/graphics/SPRITE_DEPTH_PROJECTION_TRIAGE.md: genuine XNA
M33=1/no Z translation, SDL_GPU compiled sprite path already correct, and the
existing depth-bias fixture's hidden layerDepth0-to-depth0.5 assumption. Prepared
demos repair-sprite-depth-before.mjs (syntax/plan checked, UNRUN) must capture the
original five pixel cases on four renderers AFTER external16 but BEFORE any
external rebuild. It preserves all outcomes, not assumed control passes.
Separately noted custom ShaderEffect depth-disabled source concern; not reproduced
or a new issue yet. No production/registered-test edits during this frozen run.

**Continuation audit: native30524 remains live, but is NOT green.** The full
suite failed `DynamicSoundEffectInstanceTest.StressSubmitFloatBufferEXTAgainstRepeatedPlayCyclesNeverCorruptsLiveStream`
at line1130 (`callsThrown == 0`,52ms). Original log is retained; no blind retry,
assertion removal or sanitizer-prerequisite bypass is permitted. Source inspection
shows the producer and3000 Play/Stop cycles have no rendezvous guaranteeing the
required live opposite-format submission. The same unchanged test passed in both
previous055/004 full XMLs (28/21ms); that does not invalidate this failure.
Keep original30524 running and collect all outcomes before modifying frozen inputs.
Next: establish deterministic live-format coverage while preserving concurrent
stress, validate that test change independently, then restore the full native gate.
No audio product change or test change has been made at this checkpoint.

**Current: ISSUE-082 base-resource event candidate, native30524 LIVE.**
Current status7ad1f8470/product0049359a26b6; demos ecf2ed46e6e1138a3d6ea0c5a2907f6227f5e950
(004 closure99f4069; new156 inventory1f016d0). Actual XNA reference97591
accepted8PASS; original native before65198 accepted5FAIL/1finalizerPASS on each
SW/GLES/GL33 (build6750ms/runs191/446/401ms, frozenunchanged/peak3603423232/events0).
Local candidate edits only old non-shared GraphicsResource::Dispose: flag/diagnostic
state before callbacks; exception-safe device registration retirement; preserve
normal callback order and original exception. Inherited011 metadata/shared identity
untouched. Six witnesses copied unchanged into native graphics; measured decision
row added to plans/plan_bindings_upstream.md. These are UNCOMMITTED candidate edits.
30524 sole heavy job validates focused6/graphics2985/full10994+6; inputs frozen,
one worker/private GPU/network/owner UID/caps0/shared cache/6GiB/no swap. Poll it;
after full acceptance run actual lifetime sanitizers6 then external regressions.
Do not stage the whole inherited011 GraphicsResource.cpp when committing: isolate
only this disposal hunk.082's marker/oracle unchanged, pending owner decision on
SpriteBatch post-disposal guard compatibility. SDL_GPU depth residual remains
separate/unhidden, now permanently ISSUE-156: original155+1new=156/104final/52pending.
30524 build55531ms/focused6PASS350ms/graphics2912PASS+73SKIP54259ms accepted;
full11000 confirmed live, no full result. After acceptance run prepared
repair-resource-event-sanitizer.mjs (syntax/transform checked, UNRUN, original6
SOFTWARE callbacks/registration, actual production/Runtime ASan/UBSan/float-cast/leaks).
Additional owner decision pending: C GRAPHICS_RESOURCES.md and strict-C/demo
fixtures explicitly expect is_disposed=false inside Disposing, contrary to XNA.
Do not silently change that contract or suppress the assertions. No C edits yet.
Continuation re-polled30524 and verified original native process/scope live in the
long XNA differential corpus. Prepared demos SpriteDepthProjectionTests.cpp is an
unregistered/UNCOMPILED/UNRUN five-case156 witness: depth order, no-depth control,
numeric interval against geometry, Z translation/scale. No renderer source change.
Next heavy task remains082 sanitizer after native acceptance, not a parallel build.
30524 subsequently passed the genuine XNA differential build test and advanced
to XnaErrorParityTest. After sanitizer acceptance run prepared demos driver
repair-resource-event-external-before.mjs:16 original lifecycle/C cases on four
renderers, unchanged004 external artifacts, before any rebuild/oracle correction.
Driver syntax/plan/key-inventory checked only (319 frozen keys present), UNRUN.
No push/merge. Original155 IDs and their dispositions retained.

**Accepted checkpoint: ISSUE-004 product9359a26b6 committed.** Product
`9359a26b61ded101eff35ee4008fbee3a490e9dc` contains only common sampler forwarding
and four native pixel regressions. Native10893PASS/101sameSKIP, graphics2906PASS/
73sameSKIP, actual SOFTWARE sanitizer4PASS/no findings. External116 only004 improves
on four renderers; old082 and SDL_GPU depth residual remain visible. Actual images
inspected; first sprite and full blend framebuffer unchanged. Final58657 accepted0:
exact marker-only build/catalog delta, eight Immediate runs and five CTests PASS,
frozenunchanged/OOM0. Original evaluator error retained and source-backed read-only
83834 accepted; no test weakening. Demos IMMEDIATE_SAMPLER_REPAIR.md has evidence.
155 historical IDs retained/104final/51withoutfinal. Demos closure commit
`99f4069bebbbb8a42749a1c25a438a5d20b1a4ac`. Genuine managed event reference97591
completed0:8PASS, original XNA assemblies/MVID retained, no GPU teardown claim;
build6685ms/run1026ms, peak807870464bytes/high303/max0/OOM0, frozenunchanged.
No live heavy job at this checkpoint. Next execute prepared native six-case event
baseline before any repair;
inherited011 GraphicsResource metadata is separate and must not be staged with it.
Also retain/reproduce the unmarked SDL_GPU layer_depth_sorting residual for its
own root-cause batch. No push/merge. All older004 LIVE notes below are historical.

**Latest: ISSUE-004 sanitizer93821 ACCEPTED; external86378 LIVE.** Original4
SOFTWARE pixels PASS under production/Runtime ASan+UBSan+float-cast+leaks,
build20836ms/strict fixture6387ms/run298ms/inspect35ms, no findings/frozen drift,
peak4,297,469,952bytes/max0/OOM0. Native42945 accepted as below. Sole heavy job86378
captures29 old SDL_GPU cases, then one external rebuild and exact116 comparisons,
plus original4 pixels on SW/GLES/GL33. Only004 may improve;082 stays visible.
Pre-rebuild SDL_GPU also fails the unmarked layer_depth_sorting depth-test
assertion (other controls pass). Not a004 regression; preserve and independently
investigate after004, no skip/expected-failure conversion. See demos triage notes.
Poll86378; inspect actual images, run prepared marker-only final gate, then commit
only004's source/native regression and corresponding demos evidence/history.
No marker/closure yet. Preserve011/082/003 work; counts155/103final/52withoutfinal.

**Latest verified: ISSUE-004 native42945 ACCEPTED; sanitizer93821 LIVE.**
Full10,893PASS/101unchangedSKIP in1,655,437ms; exact old10,990 outcomes retained,
exactly four new pixelPASS. Graphics2906PASS/73SKIP, focused4PASS; frozenunchanged,
peak4,299,186,176bytes/high8179/max0/OOM0. Native terminal0: do not restart it.
Sole heavy job93821 now executes original4 SOFTWARE pixels with actual production
and Runtime ASan/UBSan/float-cast/leak instrumentation, one worker/private GPU/
shared cache/6GiB/no swap. Poll93821; then prepared external116 exact comparisons,
actual screenshot review, prepared marker-only final gate/five CTests and commits.
Dedicated demos evidence IMMEDIATE_SAMPLER_REPAIR.md. Original004 marker/assertions
intact, no closure/push/merge; counts155/103final/52withoutfinal. Preserve inherited011.
All earlier LIVE notes below are historical and superseded by this paragraph.

**Current live task: ISSUE-004 native42945, sole heavy job.** HiDef baseline75809/v3
accepted3 real pixelFAIL/1DeferredPASS, strict build3069ms/run383ms/inspect31ms,
frozen unchanged/peak609665024/events0. Candidate only adds18 common SpriteBatch
lines to pass all seven live device sampler properties to Immediate Draw's private
renderer channel; no Begin-state reset, backend hack or Deferred-path change.
Original4 pixel witnesses registered unchanged in native tests.42945 builds/tests
focus4, graphics2979, full10990+4 with exact previous outcomes; one worker/private
GPU+network/owner UID/all capabilities0/6GiB/no swap. Poll42945; inputs frozen.
Then external87 parity and actual multi-renderer pixel witnesses, repaired images,
marker-only closure and coherent commits. No004 closure yet.082 is provisionally
DEMO_EXPECTATION_BUG with independently measured accounting, but related lifecycle
controls still need audit;003 intersects inherited011 ownership, preserved untouched.
42945 update: native buildPASS42759ms, focused4PASS331ms, graphics2979=
2906PASS/73SKIP55342ms. Full10994 now running; exact old10990 outcome comparison
pending. Poll42945; no full acceptance/closure yet and no second heavy job.
Continuation verified42945 live via process handle AND systemd scope; CnaTests
is executing the genuine XNA fxc differential corpus. MemoryCurrent4208435200,
peak4299186176 under6GiB. No failed-test line observed; not a final result.
Prepared syntax/plan-checked `build-probe/repair-sprite-sampler-external.mjs`
(in demos) requires accepted native audit, captures29 SDL_GPU baseline cases
before one external rebuild, then exact116 before/after comparisons and original
four pixel witnesses on SOFTWARE/OPENGLES3/OPENGL33. Run serially after42945;
inspect real repaired screenshots before marker removal. No external run yet.
Separate UNRUN/nonregistered GraphicsResourceEventOrderTests.cpp prepares082's
event-order/reentrant/throwing-callback audit; do not include it in004's commit.
Further082 preparation: separate UNRUN GraphicsResourceEventOrderOracle.cs has
eight managed genuine-reference checks; do not run beside the active Wine corpus.
Source review warns against a naive flag-before-event change: a throwing callback
would bypass device unregistration, and the destructor then sees disposed state
and returns. Future repair needs exception-safe retirement and sanitizer/device
regressions, preserving inherited011 metadata hunks. Details in SPRITE_BATCH_TRIAGE.
Latest verified progression:42945 passed the genuine XNA differential corpus
(763587ms) and error parity (130170ms), and is still running later tests.
After full acceptance, run prepared `repair-sprite-sampler-sanitizer.mjs` FIRST:
actual SpriteBatch/Texture2D/GraphicsResource/runtime instrumentation, original4
SOFTWARE pixel tests, ASan/UBSan/float-cast/leaks, one worker/private GPU/6GiB.
Driver is syntax/plan checked only. External116 now requires that accepted audit
and permits only its declared build-asan artifact updates in frozen comparisons.
No sanitizer/external pass is claimed yet. Same native session42945 remains live.

**Latest work: SpriteBatch baseline accepted,004 minimized probe75809 LIVE.**
CNA d2bef7020/status,1ba26ed18/product; demos a445241;055 remains closed.
Baseline94606 original87 observations=80PASS/6FAIL/1GLES capabilitySKIP; only004
and082 fail on SOFTWARE/OPENGLES3/OPENGL33, unchanged frozen inputs. Genuine082
reference91995=13PASS: retained counters match XNA, old demo accounting expectation
wrong; separate event/exception controls need audit (inherited011 preserved).
004 screenshots inspected on all3: second sprite remains point-filtered. Four
prepared native witnesses are not registered yet. Original18808 forced unavailable
SOFTWARE;36001 used Reach where readback is refused. Both are preserved harness
errors, not product defects.75809/v3 now runs correct OPENGLES3/HiDef before any
production change. Poll it, then smallest common sampler repair, native+external87
regressions, and closure. Counts155/103final/52withoutfinal unchanged; no push.

**Latest accepted: ISSUE-055 closed and committed.** Product CNA
`1ba26ed18b520b3469a8250a8a6fe032e664c84e`; demos closure
`a445241795fc9bcb971043d0536657b89cbdcb89`; Runtimec26614e85 (clean).
Matrix155historical/103final/52withoutfinal;101 remains explicit native-Windows
codec-validation pending. Four-file product commit contains only ShaderEffect
implementation/header, four native regressions and strict C EffectSmoke extension.
Inherited011 and its C metadata remain uncommitted, unchanged by this repair.

Accepted: native10889PASS/101sameSKIP, graphics2902PASS/73sameSKIP, sanitizer5/no
findings, external39 exact comparisons, original C smoke on three renderers,
structural4 on six renderers, actual GL before/after images, seven C metadata gates.
Final15342 accepted0: originalGL9 each, SOFTWARE honestSKIP77, catalog3PASS/1SKIP;
only055 marker removed, frozen sources/catalog otherwise unchanged. Final61523
build succeeded0/7961ms; verifier failed on relative/absolute executable alias.
Canonical-path resume reused the pinned build without rebuilding; original failed
log retained. No new compiler warning, original native nodiscard review retained.
Last complete native checkpoint is73916; last full external campaign sweep remains
the earlier recorded checkpoint, not this39-case subset. No push or merge.

No heavy job live at this checkpoint. Exact next task: SpriteBatch003/004/082.
Genuine082 managed reference91995 completed13PASS (build3504ms/run644ms,
peak751284224/events0), confirming retained accounting and disposed-before-event;
constructor-free seam, NOT GPU resource destruction coverage. Review existing
SOFTWARE-351 native tests/ledger and current CNA contract, reproduce current demos,
then correct only demonstrably wrong expectations or actual compatibility defects.
Separate SpriteBatch prep remains uncommitted in demos. All older LIVE notes below
are historical; do not restart completed jobs or stage inherited011 accidentally.

**Latest: ISSUE-055 final marker gate61523 running; older LIVE notes are historical.**
Inventory74105 accepted0, seven original gates PASS; classifications unchanged
(467headers/8122symbols/7011implemented/15partial/643planned/453N/A).
Regenerated full inventory changes only six ShaderEffect source line numbers.
External79676 accepted39 paired cases; only exact three055 GL failures improve.
Same strict C regression fails old World retention and passes on three repaired
renderers; structural4 pass on six. Both repaired GL images inspected and exactly
RGBA-identical. Native10889PASS/101sameSKIP and sanitizer5 remain accepted.
Final before97106 accepted; only knownIssue055 line removed.61523 runs final build,
GL9 each, honest SOFTWARE skip and focused/catalog4. Poll61523 before any heavy
job or frozen-input edit. Then commit four ShaderEffect/native/C files only,
demos evidence/closure and CNA status separately; preserve inherited011. No push.
Matrix155/102final/53withoutfinal until closure. Next SpriteBatch003/004/082,
with genuine082 managed reference first; never blindly impose its old demo oracle.

**Current: sanitizer43739 accepted; external79676 LIVE.** Actual SAN5PASS,
no findings/suppressions;build12,713ms/strict fixtures6,128ms/run105ms/inspect25ms;
frozen unchanged,peak833,327,104/events0. Native73916 accepted below. Extended C
EffectSmoke strictly compiled284ms and exact clone-World failure reproduced on old
SOFTWARE C library (exit1/63ms/stage3). All39 old shader/C captures complete;
79676 now sole heavy incremental external build,one worker/private GPU/6GiB/no swap.
Poll79676; do not edit frozen inputs. Then GL image inspection/inventory7/final
marker-only gates. No055 closure/commit or marker removal. Inherited011 preserved.

**Current: native73916 accepted; sanitizer43739 LIVE.** Full10,889PASS /
101sameSKIP1,688,536ms; exact old10,986 outcomes plus4newPASS, compare135ms.
Focus4PASS440ms; graphics2,902PASS/73SKIP59,418ms; frozen unchanged.
Peak2,705,068,032/all memory events0. Original60373 warning review retained.
After native completion added C EffectSmoke.c's exact three-matrix clone retention
and independent World mutation checks; otherwise original C assertions untouched,
new C checks UNRUN.43739 sole heavy job: actual SAN5/one worker/private GPU/6GiB
no swap. Poll43739; then external39 + old/new C regression, GL images, inventory7,
marker-only final gates and coherent055 commit. No closure/marker removal yet.
Preserve inherited011 and exclude SpriteBatch preparation. Older LIVE notes historical.

**Current: ISSUE-055 native resume73916 LIVE;60373 terminal before tests.**
Original build PASS146,150ms; three non-fatal nodiscard warnings from unchanged
HEAD CnjEffect/CnjStockEffect EXPECT_THROW tests triggered diagnostic review.
Draco CMP0148 dev warning retained too. Exact source/log/binary hashes and review
are in demos reports/repair-shader-clone-diagnostics-review.json. No warning or
test suppression, no product/fixture edit and no warning-free claim.73916 reuses
the successful build under the same one-job/private GPU/net/caps0/6GiB/no-swap
limits: focused4 PASS440ms; graphics2975/full10986+4 still pending. Poll73916;
do not restart60373 or edit frozen inputs. Then sanitizer/external GL/C gates.
Graphics completed59,418ms:2,902PASS/73SKIP, no failures; full native still LIVE.
Prepared syntax-checked UNRUN shader-clone-sanitizer5 and external39 paired shader/C
demos+6x4 structural+original C EffectSmoke3. Run serially only after prerequisites,
then inspect actual screenshots and C metadata gates. See demos055 triage.
No055 closure/marker removal/commit. Earlier LIVE/no-source-edit notes superseded.
Prepared `repair-shader-clone-inventory.mjs` is syntax-checked/UNRUN, requires
accepted external39 and runs the seven original C metadata gates under2GiB/no swap.
Inherited011 mapping/generated summaries remain excluded from055's commit.
Read-only next SpriteBatch triage found genuine XNA source also retains active
accounting on Dispose: verify082's expectation before any repair, not blindly
decrement to make the demo green. See demos SPRITE_BATCH_TRIAGE.md; no SpriteBatch
code/oracle/status changed, and that preparation is not part of055's commit.
Prepared055 final marker driver requires external39/inventory7 and actual GL image
review, is syntax-checked/UNRUN; no marker removed. Prepared082 genuine managed
counter/event probe13 is UNCOMPILED/UNRUN; never run its Wine prefix concurrently
with73916. See demos SpriteBatchDisposeAccountingOracle.cs and triage.
Continuation19:17:73916 is authoritatively LIVE, past differential/error/effect
compiler/public sample tests; no failed-test line yet, not final acceptance.
After completion add exact3-matrix retention/independent World mutation checks to
clean C EffectSmoke.c's existing ShaderEffect clone block (NOT edited yet). The
revised UNRUN external driver requires that regression and proves its exact old
C-library World-copy failure before the rebuild, then same C binary PASS on3
renderers. This joins055's test commit, not inherited011. Sanitizer5 precedes it.

**ISSUE-055 native60373 LIVE.** Video is committed below (CNA productabbd8a4ea,
status61baeb155; demosb325b6a). Shader baseline45813 reproduces3FAIL/1PASS in four
unchanged structural cases; original GL demo9 has same3failures on OPENGLES3 and
OPENGL33; SOFTWARE honestSKIP77. Both screenshots inspected. Candidate only adds
existing disposal guard+three matrix copies and public documentation, registers
unchanged4. Sole heavy job60373 now builds/runs focus4, graphics2975, full10986+4
with private GPU/network/caps0/one worker/6GiB/no swap. Poll60373; never restart
for silence or modify its frozen inputs. After acceptance: sanitizer, actual GL
pixel/program-lifetime regressions and C metadata/ABI checks; no055 closure yet.
The source-baseline snapshot's sandbox EPERM and exact HEAD-based recovery are
documented honestly in demos SHADER_CLONE_STATE_TRIAGE.md. Inherited011 preserved.

**Video batch committed; next ISSUE-055.** Product CNA
`abbd8a4ea7ae3f511e0f3aac3faaba411f6de0a4`; demos evidence/marker closure
`b325b6a6120aded1f7962e1d31e41fc0a99a29f6`; Runtimec26614e85.
ISSUE-099/100 FIXED_VALIDATED;101 FIXED_PLATFORM_VALIDATION_PENDING (local URI/
missing-file fixes validated; genuine native Windows codec/error timing still
requires functional WMV control, see VIDEO_CODEC_REFERENCE_LIMITATION.md).
Matrix155 historical/102final dispositions/53withoutfinal; one of the102 explicitly
awaits platform validation. No ordinary local video defect is hidden by that status.

Native53/355/full10885PASS+101sameSKIP; sanitizer53/no findings; external53 and
exact112 comparisons; pure-C real frame; seven metadata gates; strict C17/C++23
ABI/error/bool; final10 renderer/8CTest all accepted. SOFTWARE/OPENGLES3 actually
ran. Three knownIssue markers removed with original assertions retained.
See demos VIDEO_C_CONTRACT_ALIGNMENT.md and repair-video-final-audit.json.
Two native test EOF-only whitespace fixes have byte-exact proof in
repair-video-commit-whitespace.json; use its frozen hashes for the next baseline.
No heavy process remains live at this checkpoint. No push/merge.

Exact next task: reproduce055 with the prepared four structural witnesses and
unchanged graphics.shader_effect.clone_state real GL demo, then minimal guard/
three-matrix copy repair and focused/regression validation. No055 source edit yet.
Preserve inherited011 content/core/graphics/audio/Song changes. Remaining dirty
coverage_mappings hunks are only obsolete trait removal plus SoundEffect pins;
COVERAGE/LIMITATIONS/RELEASE_GATE summaries depend on011 and are not in video
commit. Demos055 prep and provisional UNRUN codec witness are not runtime proof.
Global docs/COVERAGE.md remains the historical full baseline; no partial112 run
is substituted for a new full campaign report.

### Earlier checkpoints (historical)

**External62468 LIVE after accepted native/inventory/SAN:** native62083 full
10,885PASS/101sameSKIP with exact prior outcomes; inventory-v3 session22151
seven original gatesPASS26,436ms, decoder policyPASS, peak965,607,424/events0.
29275 and33140 failed only sequentially stale generated LIMITATIONS/RELEASE_GATE;
their logs remain. Original generators refreshed derived summaries, no criteria
changed; release still honestly NOT READY(643planned). Three summaries and the
SoundEffect pin hunk depend on inherited011 and must not enter the video commit.
Sanitizer63740 all53PASS/no findings: libraries17,506ms,fixture39,002ms,run962ms,
inspect35ms;peak1,726,898,176/events0,frozen unchanged. Initial sanitizer wrapper
failed before build (exact replacement string), corrected driver now transforms
the original base directly; product/tests unchanged. External62468 is the only
heavy job: existing linux-full build, unchanged112 oracles,53actual witnesses,
pure-C actual decoded frame. Collect it, then run extra C ABI requiring accepted
loop external and inventory-v3 audits. No video closure/marker removal/commit/push.

**Native62083 completed0, inventory29275 LIVE:** full10,885PASS/101sameSKIP in
1,869,465ms; exact old10,982 outcomes preserved plus4loop-boundaryPASS (compare
159ms).53focused/355mediaPASS; frozen inputs unchanged. Peak4,299,919,360 bytes,
high5228/max0/OOM0. Now running repair-video-inventory.mjs in sole heavy scope
cna-repair-video-inventory,1GiB high/2GiB max/no swap. Collect29275 before another
job. If inventory passes, run loop-sanitizer53 then loop-external112 then extra
C ABI; all three remain UNRUN. Do not mark video resolved yet. Earlier LIVE
native observations below are historical, not instructions to restart62083.

Continuation18:10 CEST: existing native62083 authoritatively live; differential
build/error and effect-compiler phases passed, public sample content build has
advanced to device tests. No duplicate heavy job or production edits. Scope
peak4,299,919,360 bytes, current about3.7GB. Matrix rechecked155/99final/56pending.
The additional C ABI driver is now syntax-checked against the future accepted
loop-external audit AND actual inventory audit; the older instruction below to
change its prerequisite is superseded. Still UNRUN. Next: collect62083, then run
repair-video-inventory.mjs under1GiB high/2GiB max/no swap; review any diagnostics.
Then loop-sanitizer53, loop-external112 and extra C ABI serially under established
private GPU/6GiB/no-swap scopes. No markers removed, commits, push or merge.

**Inventory metadata review while62083 runs (not yet validated):** removed only
the orphan content-reader-detail-traits rule (declarations removed by committed
3d254dfa7); repinned only the two existing SoundEffect move operations from
defaulted signatures to the inherited011 out-of-line signatures, with no pattern/
status/test expansion. Corrected two video mapping descriptions for owner-approved
XNA contracts, keeping all video approved-symbol IDs unchanged. No content/audio
source or coverage generator changed. See demos VIDEO_C_INVENTORY_REVIEW.md for
exact old/new pins and commit boundaries: keep the SoundEffect pin hunk with
ownership, not the video commit; derived summary is NOT regenerated yet. After
62083, run actual inventory and preserve any new failures rather than blanket
approving. Original five C gates still stand at3PASS/2FAIL until rerun. Mapping
JSON was not part of runtime frozen inputs; native tests do not reference it.
The unchanged lightweight test_coverage_scope.py passes33/33 in0.800s; exact
source/mapping hashes are in VIDEO_C_INVENTORY_REVIEW.md. This does not replace
the pending actual inventory/release rerun.62083 is still live in the long
XnaDifferentialBuildTest; poll that existing handle, never restart for silence.

62083 update: build71,620ms/zero diagnostics,53focusedPASS1,350ms and
355mediaPASS12,721ms. Full native10986 is LIVE; exact old10982 outcome parity
plus4newPASS is still pending. Prepared repair-video-loop-sanitizer.mjs and
repair-video-loop-external.mjs are syntax-checked only, UNRUN; they require
accepted full native and sanitizer53 respectively. External repeats the exact
112 oracles from failed84138, allowing only the remaining C frame failure to
become PASS; no further label/predicate changes. repair-video-xna-c-contract.mjs
is also UNRUN and still names the failed XNA external audit; update its prerequisite
to a future accepted loop external audit before use. It adds original ABI C17/
C++23, error and generated bool tests against the actual external C ABI. Full
C inventory diagnostic remains pending (obsolete committed trait rule plus
inherited011 SoundEffect move pins); no mapping approval changed. A focused
Video/VideoPlayer inventory diagnostic was added to repair-video-coverage-diagnose.py,
but that revised script has not run. It does not replace the failed full gate.

**62083 LIVE — deterministic loop-frame repair:** external84138 completed1,
NOT accepted: build157,279ms/zero diagnostics; actual external49PASS; all112
statuses/assertion labels match authorized changes except the same remaining
C player Stop-frame assertion on both renderers. Pure-C VideoSmoke nowPASS366ms
with real decoded-frame coverage. Peak4,301,926,400/high6577/max0/OOM0; frozen
unchanged. Root cause: EOF looping resets lastFramePts_ to-1 for scheduling while
the texture/generation still describe the last decoded frame. The public PTS
getter wrongly exposes that scheduling sentinel. Do not weaken the original C
retained-frame assertion. New deterministic native4 baseline74000 gives2FAIL/
2PASS (strict build5562ms/run545ms/audit39ms), peak3,626,180,608/events0, unchanged
hashes; only test-friend clock seam added, no sleeps. Identical4 registered with
decoder-off exclusion. Minimal candidate separates framePresentationTime_ from
lastFramePts_: update only after upload, clear on decoder close, retain over seek.
Native53 focused/355media/full10982+4 now running with private GPU/net/caps0,
one worker/6GiB/no swap. Do not edit frozen sources during62083. After acceptance:
sanitizer53 and external112/pure-C with NO further C assertion edits, then C ABI/
coverage gates. C inventory failures below remain unresolved; inherited011
untouched. No video closure/product commit/marker removal/push/merge.

**15652 sanitizer complete; external84138 LIVE:** all49 reference-corrected and
original guards/lifetime/association/tracks/URI cases PASS under actual
ASan/UBSan/float-cast/leak instrumentation, no findings/suppressions. Library
build15,639ms, strict fixture37,028ms, run756ms, inspection36ms. Frozen hashes
unchanged; peak685,629,440/all memory events0. External driver now runs serially
under6GiB/no swap/private GPU/shared cache: existing linux-full incremental build,
actual external49,112 scenarios with authorized-only C assertion deltas, and
pure-C smoke requiring real decoded frame. Source frozen while84138 runs.
Runtime checkpoint collected successfully; C inventory failures below remain
unaccepted. No video closure, product commit, marker removal, push or merge.

**90148 completed; C inventory gate FAILED (not suppressed):** native full
10881PASS/101sameSKIP in1,653,247ms, exact old10976 outcomes plus6PASS; compare260ms.
49focused/351mediaPASS, frozen hashes unchanged, peak4,297,699,328/high5540/max0/OOM0.
Post-review20536: actual decoder-policy TRUE/FALSE checks PASS; build23,490ms,
zero diagnostics, CnaTests/CnaMediaTests/libcna.so byte-identical to full checkpoint.
C API CTests3PASS/2FAIL: CoverageMatrix and ReleaseGate fail on the obsolete
content-reader-detail-traits rule. Commit3d254dfa7 removed both declarations;
read-only Doxygen diagnostic also finds two SoundEffect move pins stale against
inherited uncommitted011 declarations. No content/audio source or mapping changed.
Keep failed logs and do NOT claim full C API acceptance or video closure.
repair-video-xna-runtime-checkpoint.mjs captures independent runtime acceptance
with contractAccepted=false/closureBlocked=true; use it only to continue required
sanitizer49/external112/pure-C diagnostics, then settle inventory/ownership gates.
The two post-review edits are a decoder-off fixture selector and C header comment
placement/output semantics; no public declaration change. No second full run needed
while binary identity holds. No video commit/marker removal/push/merge.

**Video XNA alignment now validating:** owner approvals received for099 and latest
101 (ArgumentException/INVALID_ARGUMENT supersedes earlier IO proposal). New
baseline85836 reproduces5wrong-exceptionFAIL/1real-decodePASS,7,651/554/55ms,
peak3,427,909,632/events0; final070 inputs unchanged. Minimal Play guard corrected;
six exact native regression cases registered. C docs/smoke/demos deliberately
corrected against XNA with original11files captured before edits and explicit
predicate/label mapping in demos VIDEO_C_CONTRACT_ALIGNMENT.md. No adapter hack,
invalid-codec speculation or marker removal. Native driver repair-video-xna-native.mjs
now runs49focused/351media/full10976+6 in private GPU/network, zero capabilities,
one worker/6GiB/no swap. Inputs frozen; do not edit until finished. Then sanitizer,
external112 with authorized-only deltas, real pure-C smoke and C contract gates.
Preserve inherited011. No video closure/product commit/push/merge.

90148 update: native build40,919ms/zero diagnostics,49focusedPASS1,324ms,
351mediaPASS13,223ms; full still live (XNA differential/error parity stages).
No concurrent heavy task. Generated3197 C routes/57files check passes. Matrix
duplicate101 current_validation key removed, historical facts retained in
validation; strict duplicate-key parser verifies155 unique IDs/99final.
Before final video acceptance, add the new decoder-dependent
VideoMissingFileXnaTests.cpp to the existing !CNA_FFMPEG_AVAILABLE test filter;
do not change this frozen file during90148. Verify enabled test inventory
unchanged and disabled policy retains guard/backend-availability coverage.
Also finish C header documentation placement/output-clearing wording after
the frozen gate. Prepared XNA sanitizer/external wrappers are UNRUN. External
must normalize frozen artifact paths and apply only the documented six-label
mapping/six new C asset labels; pure-C smoke must report real frame exercise.

**070 closed:** product `1c8bbb6b83bab2fc3b15bf2370b426226fd87204`, demos based on
676f36d plus closure evidence. Marker gate44634 completed0: build6,929ms/zero
diagnostics; original C070 both renderers PASS; four focused/catalog CTests
PASS13,767ms; exact catalog delta only070, frozen hashes unchanged. Peak
3,006,173,184/events0. Matrix155historical/99final/56pending. No full external
CTest/smoke claim. Next owner-approved099/100/101: preserve genuine32 reference,
correct missing-file Play to exact ArgumentException/INVALID_ARGUMENT; align C
docs/smoke/demo with XNA fresh-frame refusal/Volume refusal/retained Stop Video
and frame, preserving all unrelated assertions and explicit change mapping.
Do not infer arbitrary invalid-codec exceptions from platform-limited Wine.
Capture current final070 inputs before changes, run focused before/after then
native/media/full, sanitizer and external gates.055 preparation is deferred.
Inherited011 and video candidates remain uncommitted/unaccepted; no push/merge.

**070 repair accepted for scoped product commit:** eight assignments in two PBR
copy constructors and eight native regression tests; no header/C/backend change.
Native10875PASS/101sameSKIP, graphics2898PASS/73SKIP, sanitizer35PASS/no findings,
six actual renderers8PASS each. External112 exact preceding outcomes (existing
C video regressions still unaccepted). Stock66 improves only070 on both renderers:
54PASS/12sameSKIP; all original assertion labels retained. Four shadow images
800x480 exactly unchanged, zero differing pixels; OPENGLES3 pair inspected and
OPENGL33 byte-identical. No clone/BRDF image claim. Frozen hashes unchanged.
External peak4,301,479,936/high812/max0/OOM0; stock192,016,384/events0;
visual194,105,344/events0. Catalog-before capture completed. Commit only the two
PBR implementations, native fixture and this handoff. Then remove only070 marker/
restriction, rebuild incrementally, assert exact catalog delta and two original
C passes/four focused CTests; only then close070 and record repair SHA. Next
resume owner-approved099/100/101. Preserve inherited changes; no push/merge.

**Owner decisions received:** C VideoPlayer099 must align with verified XNA/C++;
update public docs/tests with history. Latest101 answer supersedes the earlier
IO proposal: missing-file Play throws ArgumentException, C INVALID_ARGUMENT;
correct URI handling too. Existing-invalid-codec failure remains unproven on
Windows; preserve the platform limitation. Finish070, then resume099/100/101,
not055. No new push/merge. External070 finished112 exact prior outcomes and48
renderer checks, errors0/frozen unchanged. Stock66 now54PASS/12sameSKIP, exactly
two070 scenarios improve with original assertions. Visual/marker gates pending.

**LIVE38861 PBR external:** sanitizer35/35PASS/no findings,14,068/9,239/172/22ms,
peak816,594,944/events0, frozen hashes unchanged. Existing external build now
runs one worker/6GiB/no swap, then48 six-renderer clone witnesses/112 parity.
Next stock66 with only070 improvements/four exact shadows/marker-only gate.
Codec observer19914 is PLATFORM-LIMITED, NOT PASS: valid silent WMV aborted
at Wine MFCreateVideoMediaType; only the inspected owned scope was stopped,
observer130, lingering winedevice killed within that scope. See demos
VIDEO_CODEC_REFERENCE_LIMITATION.md. No video exception or C contract change.

**72714 completed0:** full10875PASS/101sameSKIP1,245,153ms, exact previous10968
outcomes plus8PASS, comparison127ms; graphics2898PASS/73SKIP. Frozen hashes
unchanged,peak4,296,978,432/high1602/max0/OOM0. Real decoder observer now runs
alone under1GiB/no swap. Next PBR sanitizer35/external48 plus112/stock66 with
only070 improvements/four exact shadows/marker gate. No070 closure or product
commit yet; do not rerun the accepted native full. Preserve inherited candidates.

**LIVE72714, ISSUE-070 candidate:** CNA cbe9b9388, demos03ed687. Original C demo
still fails exactly four properties on both SOFTWARE/OPENGLES3. New safe C++
baseline67425 confirms6FAIL/2PASS across PbrEffect/SkinnedPbrEffect;2,836/363/23ms,
peak447,148,032/events0,frozen hashes unchanged. Eight assignments now copy four
omitted fields in the two constructors. Original8 test bodies registered;
preparation-comment-only change is explicitly checked against the original hash.
Native graphics/full10968+8 parity RUNNING, private GPU/network/caps dropped,
one worker/6GiB/no swap. Source frozen. Next sanitizer and external six-renderer
clone witnesses/C070 improvement plus unchanged stock/video/visual regression,
then scoped commit and marker update only after acceptance.070 remains open.
Preserve inherited011 and video work. No new push/merge; C video choices pending.

72714 update: build22,793ms/zero diagnostics; focused8PASS320ms; graphics module
passed44,459ms. Full suite advanced through compiler differential/error parity
and is running original content integration projects. Do not edit frozen sources.
After completion: optionally run prepared repair-video-codec-reference.mjs serially
under1GiB/no swap (real normally constructed decoder/proxy, valid silent WMV
positive control;77 is unavailable, not PASS), then required PBR sanitizer35,
external8x6/112, stock66 with only070 improvements, four unchanged shadow images.
Prepared marker-only before/after driver repair-pbr-clone-final.mjs verifies exact
catalog delta, both original C assertions and four focused/catalog CTests only
after all those gates. All these drivers are UNRUN. Shader055 structural4 fixture/
triage exists only in demos, uncompiled/unrun; no055 product source changed.

**Committed checkpoint:** CNA repair `ce88b5f9bf4a6771d4650d182c014944837452a0`
closes102 with all gates detailed below. Demos at9593e86 before the matching
closure evidence commit; Runtime c26614e85dffff4cadc5c4803b00d78953706b21.
Matrix155historical/98final/57pending; all historical IDs retained. No knownIssue
marker existed for102, so none removed. Last native full10867PASS/101sameSKIP.
Next run repair-pbr-clone-before.mjs (8 prepared tests; original C070 already
reproduced four failures on both renderers), then repair only the omitted four
fields in each PBR copy constructor. Inherited011/video work remains dirty and
unaccepted, excluded from102 commit; both C video choices pending. No push/merge.

**ISSUE-102 validation complete, ready for scoped product commit:** native full
10867PASS/101sameSKIP (20newPASS, all old outcomes unchanged), graphics2890PASS/
73SKIP, sanitizer27PASS/no findings. External build458,110ms/zero diagnostics;
original20 witnesses PASS on each SOFTWARE/OPENGLES3/OPENGL33/SDL_GPU(Vulkan)/
HEADLESS/STUB. Actual renderer identity checked.112 original audio/video cases
exactly unchanged; existing video C regressions and smoke exit2 remain globally
unaccepted. Stock66 before/after exact52PASS/12rendererSKIP/2known070FAIL.
All four original OPENGLES3/OPENGL33 images800x480 have zero differing pixels;
two OPENGLES3 captures visually inspected, GL33 counterparts byte-identical.
Not an approved-reference or SOFTWARE shadow-image claim. External peak
4,302,151,680/high1113/max0/OOM0; stock-after193,142,784/events0;
visual-after197,160,960/events0. Every gate's frozen hashes unchanged.
Commit only four receiver guards, six public headers, original20 native tests
and this handoff; then record the resulting SHA in matrix/demos issue history.
Next immediately reproduce prepared070 C++8 against the repaired library,
then minimal two-copy-constructor repair and all focused/regression gates.
Preserve inherited011/video work. No push/merge; video decisions still pending.

**Live45102 external shadow gate:** sanitizer27/27PASS, actual four effects/
runtime instrumented, no findings,30,047/6,386/136/19ms,peak851,980,288/events0,
frozen hashes unchanged. Stock66 BEFORE52PASS/12rendererSKIP/2known070FAIL.
External incremental335 tasks now running, one worker/6GiB/no swap. First
wrapper ambiguity stopped before building; fix selects only C++ flags, C17
flags unchanged. Next stock66 AFTER/four exact images, then review/commit102
only. Corrected101 owner question now explicitly proposes ArgumentException/
INVALID_ARGUMENT from genuine XNA, superseding prior IO suggestion. Decisions
pending; no C contract/expectation edit. Prepared070 baseline remains UNRUN.

**Current continuation after native45760:** focused20PASS; graphics2890PASS/
73SKIP; full10867PASS/101sameSKIP in1,259,563ms, exact old10948 outcomes plus
20newPASS, comparison177ms. Frozen hashes unchanged, peak1,357,451,264/events0.
Genuine XNA missing-file oracle32/32PASS confirms pre-decoder E_INVALIDARG
maps to exact ArgumentException/null ParamName, not FileNotFoundException.
This is actual guard/translator coverage, not native decoding/playback. Earlier
C IO proposal must be corrected before any contract decision or101 closure.
Stock-effect66 BEFORE now runs against old external binaries; next sanitizer27,
external120 across six renderers plus112 parity, stock66 AFTER/four exact images.
One heavy job/one compiler/6GiB/no swap.102 still open; no product commit/push.
Preserve inherited011 and video candidates. Earlier running states are historical.

**Reference caution,15:40 CEST:** genuine Framework IL's missing-file video
guard returns E_INVALIDARG, mapped to ArgumentException, not FNA/demo
FileNotFoundException. Installed Framework MVID matches inspected dump. New
VideoFileFailureOracle.cs/driver in demos are prepared/unrun; run serially after
active45760 and settle XNA precedence before101 closure/expectation correction
or adopting the earlier C IO proposal. No video production/demo edits now.
070 PBR clone witness/triage prepared/unrun outside registered native sources.
45760 full still runs its normal long XNA differential compiler test; only heavy job.

**LIVE45760,15:30 CEST:** original20 shadow witnesses PASS498ms after no-op
build460ms; full graphics module RUNNING, then exact full10968 parity.93255
first build succeeded364819ms but stopped before tests to review five old
nodiscard/EXPECT_THROW warnings in unchanged ModelContentTypeReaderTests,
CnjModelTests and CnjStockEffectTests. Failed gate/log retained. V2 requires
all first-build source hashes unchanged and explicitly reuses its owned rebuilt
artifacts; no content edits or native warning-policy changes. Sources frozen.
Next stock-effect66 before against still-old external binaries, sanitizer27,
external20x6 actual-renderer witnesses/112 exact outcome parity, stock66 after,
four original exact visual comparisons. These gates are pending, no closure.
Same one worker/shared cache/private GPU/network/4GiB high/6GiB hard/no swap.

**LIVE93255,15:24 CEST:**102 source candidate is now only four pre-assignment
count guards/docs plus identical20 native witnesses. Native graphics/full
parity RUNNING with source hashes frozen, one worker/shared cache/private GPU
and network/4GiB high/6GiB hard/no swap. Safe before20=12PASS/8FAIL; original
visual16+6 checks PASS on each OPENGLES3/OPENGL33, two inspected images with
byte-identical counterparts on the other GL profile. Next sanitizer27, actual
external renderer/public regression, exact before/after images. No product
commit/closure. Preserve unrelated011/099/100/101 candidates; C choices pending.

**Authoritative continuation,15:20 CEST:** external68581 finished1 (unaccepted):
build2,576,494ms/zero diagnostics; actual external43PASS; each SOFTWARE/OPENGLES3
50PASS/5FAIL/1manual.099 C++27,100 pixels9,101 URI9 pass; four unrelated failures
unchanged, same five C VideoPlayer assertions fail on both, pure-C smoke exits2.
Frozen hashes unchanged,peak4,302,946,304/high3924/max0/OOM0. No C edits or closure.
Safe102 baseline93365 completed:20=12PASS/8 boundaryFAIL, exact four receivers,
no invalid count drawn;3873/679/30ms,peak473071616/events0,unchanged hashes.
Original compile failure under added Wextra preserved; native targets do not
enable that unused-parameter policy, corrected probe retains Wall/Werror.
45372 original shadow visual baseline RUNNING, private OPENGLES3/OPENGL33.
Next verify it, add only the four count guards/docs and unchanged20 native tests,
then native graphics/full parity, sanitizers, external renderer and visual gates.
099/100/101 remain open; both C choices unanswered;101 actual decoder-open
failure follow-up also remains. Preserve inherited011.155IDs/97final/58pending.
No new product commit/push/merge. Earlier live-state paragraphs are historical.

**LIVE68581 EXTERNAL-v2:** sanitizer97309 TERMINAL0: all43PASS, actual Video/
VideoPlayer/Sharp Runtime and CMake TitleLocation object instrumented; libraries
57,823ms, strict witnesses22,209ms, run501ms/audit27ms; no findings/suppressions,
unchanged hashes,peak920498176/events0. External43227 stopped before build because
native audit retained old ASan archive hashes; v2 takes the successful sanitizer's
new ASan hashes while requiring all native sources/artifacts unchanged. No test
failure hidden.68581 now rebuilds demos linux-full then43 original witnesses,
112 SOFTWARE/OPENGLES3 scenarios and pure-C smoke. Same single-worker/private
GPU/6GiB/no swap. C failures remain failures;099/101 decisions still unanswered.
No product closure/push. Native59093 accepted full10847PASS/101unchangedSKIP.

Resume detail (15:06 CEST): external build807/1022, sole heavy job. Prepared
safe ISSUE-102 witness has20 cases acrossfour effects; it never draws an
accepted-invalid count. Drivers in demos/build-probe cover before reproduction,
native full parity, sanitizer27 (20new+7existing), and original OPENGLES3/
OPENGL33 shadow images. All shadow drivers remain UNRUN; no production edit.
A separate uncompiled VideoDeferredOpenFailureTests.cpp witness documents why
the101 File.Exists precheck is not proof of actual decoder-open failure
propagation. Resolve that root-cause follow-up and the pending C decisions
before101 closure. No new C API source/assertion changes or remote writes.

### Earlier checkpoints (historical; the live state above takes precedence)

**CURRENT97309 SANITIZER RUNNING:** native59093 TERMINAL0, full10,948=
10,847PASS/101sameSKIP in1,341,053ms; exact old10,934 parity and14newPASS,
compare136ms. Frozen hashes unchanged; peak2,626,609,152 bytes/all memory events0.
Runtime c26614e8 committed/clean, CNA c04f80d65 / demos377a5b0 plus candidates.
97309 uses existing build-asan/Media archives and the actual instrumented
CMake TitleLocation.cpp.o (not a new umbrella build),43 witnesses, private GPU,
one worker/shared cache/6GiB/no swap. No other heavy job. Sources frozen.
Exact next: sanitizer audit, external43/112 and pure-C smoke; retain rejected C
regressions until explicit099 AND101 contract decisions. Both choices have been
asked asynchronously; generic continuation is not authorization. Afterwards
safe ISSUE-102 boundary baseline is prepared/unrun in demos graphics evidence.
No new CNA production commit, closure, knownIssue removal, push or merge.

**LIVE59093 (native-v3):** incremental build18,503ms/zero diagnostics;
focused43/43PASS1,037ms, media345/345PASS12,715ms; full10948 now RUNNING.
14068 exited before build because the guard detected libcna.so rebuilt at step52
of the first failed build. This artifact is now explicitly recorded/rehashed;
all unrelated source hashes remain pinned. Earlier failed logs retained. No test
assertion changed. Only59093 is active, same one worker/private GPU/network/6GiB.
Runtime dependency c26614e8 is committed/clean/not pushed. Next full parity,
then prepared sanitizer43 and external112 gates, with unchanged C regressions
still rejecting acceptance. No CNA production commit/issue closure.

**LATEST:** runtime repair committed locally as c26614e85dffff4cadc5c4803b00d78953706b21;
runtime worktree clean, no push. Native18544 TERMINAL1 after324516ms build:
new URI fixture called TitleLocation from isolated CnaMediaTests without linking
runtime. Corrected fixture placement to tests/MediaVideoUriContractTests.cpp in
the existing cross-module group, preserving all9 assertions/public calls and
production module boundaries. Old failure log retained. Native-v2 job14068 now
RUNNING, same limits/isolation, expected43focus/345media/10948full. All sources
frozen again. Prepared sanitizer now uses canonical CnaTests link closure for
its public TitleLocation witness. No CNA issue closure or C expectation change.

**CURRENT (supersedes historical notes below):** ISSUE-101 URI/file-path candidate
is now implemented locally, not committed or accepted. Dependency gates are green:
Sharp Runtime baseline18,158/18,158; candidate18,193/18,193 across41 executables,
zero failures/skips/diagnostics, URI311→345. Latest runtime audit
repair-uri-runtime-after-v2: peak830410752 bytes, all memory events0, frozen hashes
unchanged. Genuine Framework4/.NET8 URI34-input oracle records their actual
differences; candidate follows modern.NET invalid-UTF8 escaping. Native baseline
URI9 was5FAIL/4PASS; new file-path boundary5 allFAIL against the old CNA library.
No expectation weakened. URI fixture now uses PathToGenericUtf8 (same Linux bytes).
Native job18544 is RUNNING in cna-repair-video-uri-native.scope: one worker,
shared cache, existing cmake-build-debug, private GPU/network,4GiB high/6GiB max,
no swap. Planned focused43/media354/full10948 with exact previous10934 parity.
Do not edit candidate sources while it runs. Driver/reports in demos build-probe
and build/linux-full/reports under repair-video-uri-native prefix.
Exact next: inspect this gate, document/commit validated runtime dependency,
refresh sanitizer and external renderer gates; preserve099 C API failures until
owner contract choice is explicit. C API source/docs/tests unchanged. Preserve011.
CNA ba56ddf5a / demos ca6310b / Sharp Runtime97713eb2 plus pending changes.
Matrix155/97final/58pending unchanged. No new product commit, closure, push/merge.

**CURRENT: no running job.** Native-v2 completed10,833PASS/101unchangedSKIP;
sanitizer29PASS. External37654 finished unaccepted: original099 C++27/27 and100
public-pixel9/9 PASS on SOFTWARE and OPENGLES3, but five unchanged C VideoPlayer
assertions fail on each renderer and pure-C VideoSmoke exits2. Each renderer now
49PASS/6FAIL/1manual, versus48/7/1 before (two improvements, one C regression).
Four apparent C++ label differences were a log-parser error: DemoContext prints
failures only. Separate source-pinned reanalysis retains all three real C failures
and the failed gate; original audit unchanged. External build129390ms/zero
diagnostics, original29PASS; peak4302004224/high1192/max0/OOM0, hashes unchanged.
URI baseline94692 now confirms101: fiveFAIL/fourPASS,2332/351/23ms,
peak475160576/events0, hashes unchanged; no101 production change.
Exact next: obtain the unanswered099 C contract choice, implement its scoped
adapter/explicit contract correction, repeat affected gates, then coherent commits
and closure metadata. Do not weaken assertions or accept the current C regression.
Continue101 through the URI dependency layer after complete dependency guidance;
its C Play failure contract is separate. Preserve all011 owner changes.
Permanent evidence: demos docs/repair-evidence/media/VIDEO_REPAIR_VALIDATION_CHECKPOINT.md.
CNA d1ed3d9ae / demos ca6310b plus this evidence checkpoint and uncommitted099/100,
011. Matrix155/97final/58pending,099/100/101 CONFIRMED_OPEN. No product commit,
knownIssue removal, push or merge. Earlier RUNNING entries below are history.

**Latest:** native-v2 session86932 finished successfully: full10,934 =10,833PASS/
101unchangedSKIP (1,328,138ms), exact original10,905 outcome parity with the one
reference-corrected Volume rename and29 newPASS; comparison121ms. Media340/340PASS.
Frozen hashes unchanged; peak3,127,988,224 bytes/all memory events0. External
session37654 (`cna-repair-video-external.scope`) is now RUNNING: one worker,
private GPU,6GiB/no swap, existing demos linux-full tree. It rebuilds and runs
29 original witnesses,112 unchanged renderer scenarios and pure-C VideoSmoke.
The C ABI embeds CNA static module libraries: linkage checks require the actual
external libcna_c_api.so, not a nonexistent libcna.so dynamic dependency.
No expectation weakening, marker removal, product commit or issue closure.
CNA d1ed3d9ae / demos c847549 plus uncommitted099/100 and preserved011; keep sources
frozen. Exact next: inspect external audit, resolve the unanswered099 C contract
choice, then registered/final metadata gates. No new push/merge.

**CURRENT LIVE JOB86932:** native-v2 full10934 RUNNING after clean45964ms build
and media340/340PASS11990ms.92340 after-v2 TERMINAL0: all29 focusedPASS, build61433ms/
zero diagnostics, unchanged hashes,peak1226051584/events0.4221 sanitizer-v2 TERMINAL0:
29PASS, libraries12500ms/strict fixtures14043ms/run399ms/XML23ms, actual Video/runtime
ASan/UBSan/float-cast/leaks, no findings/suppressions, unchanged hashes,
peak629182464/events0. One worker/private GPU/network/6GiB/no swap. Sources frozen.
Next prepared demos repair-video-external.mjs: original29 against actual external
library and112 renderer scenarios. C API regressions deliberately FAIL its gate;
do not turn them into known issues. C contract owner choice still unanswered.
CNA product base ab97df323 / demos c847549 (permanent evidence) plus pending099/100,
preserved011. No new source commit,
marker removal, issue closure, push or merge. Full10934/external acceptance pending.

**Latest, superseding running states below:**15439 TERMINAL0 full10929=
10828PASS/101sameSKIP1416421ms, compare123ms; exact old10905 outcomes plus24PASS,
one reference-justified Volume rename. Hashes unchanged;peak4295897088/high61/
max0/OOM0.63091 then reproduced the missed C++ destruction assertion1FAIL and
ISSUE-100 tracks3FAIL/1PASS, strict3302/342/20ms,peak459255808/events0.
Local follow-up preserves null frame after independent Video destruction (separate
from fresh/failed-play guards), applies per-Video track overrides before output
setup without replacing player defaults, corrects misplaced width Doxygen, and
registers real-decoder fixtures only in enabled profiles. Pure guards stay enabled;
OFF-profile test now requires the same fresh refusal (OFF runtime not tested).
92340 after-v2 focused29 RUNNING, sole heavy job/one worker/private GPU/6GiB/no swap;
sources frozen. Exact next: focused29, updated sanitizer29/full10934 parity, then
external112 and C routes. Owner decision about099 C contracts still pending;
no C API edits, no new closure/source commit/push/merge. Preserve011 changes.

**Latest099 candidate:** CNA ab97df323 / demos7b94712 plus uncommitted four
Video/VideoPlayer production files, two existing test files and three new native
fixtures.25907 focused TERMINAL0: build80477ms/zero diagnostics, original9guards,
original6lifetime and9new association cases all PASS. Hashes unchanged,
peak1191071744/all memory events0.41878 sanitizer TERMINAL0:24PASS, library17306ms/
strict fixture12219ms/run472ms/XML30ms, peak1118015488/events0 and unchanged hashes;
actual Video/VideoPlayer/runtime instrumented, no suppressions/findings.15439 native
RUNNING: build79272ms/zero diagnostics, media335/335PASS14598ms, full10929 ongoing.
One worker/private GPU/network/6GiB/no swap. Sources frozen. New demos fixture
VideoDestroyedFrameTests.cpp is UNRUN: protects the existing pause_and_loop
after-Video-destruction GetTexture==null contract, missed by the original24. Run it
after15439; do not mistake the current focused/media passes for full acceptance.
Pending owner choice: C API promises SUCCESS/no frame before Play, but genuine XNA
and corrected C++ throw InvalidOperationException. Asked whether C should map to
INVALID_STATE with explicit docs/tests correction or keep an intentional C-only
extension. No answer or C API edits yet. Next sanitizer, native media/full parity,
contract resolution, external and registered gates. No099 closure/commit/marker
removal. Preserve inherited unaccepted011 owner changes. No new push/merge.
Earlier entries are chronological history, not current running state.

15439 continuation: XnaDifferentialBuild parity PASS696715ms; full still RUNNING.
Immediately afterward run demos build-probe/repair-video-followup-before.mjs:
one existing C++ destruction-contract case plus four ISSUE-100 track preference
cases, currently UNRUN. See VIDEO_EXTENSIONS_TRIAGE.md (current official FNA docs
and local b35512475ed7980169574d2c40927381c1764d5a). C owner question expanded to
include documented Volume clamping and Stop-clears-video/frame demo expectations;
no answer yet. New real-decoder fixtures need the existing UnitTests.cmake OFF-
profile exclusion, while pure guards stay enabled; OFF-profile fresh GetTexture
assertion needs the same reference correction. No100/101 production edit/closure.

**Product checkpoint:** CNA7af2cd42b;073 repair514a6a8c2 and038 repair85a23569c,
CNJ warning cleanup a52d6f0ce, no-hardware harness7af2cd42b. Matrix155 permanentIDs:
97 final (89RESOLVED/7ALREADY_FIXED/1DEMO_EXPECTATION_BUG),58pending. Demos base
7486b2b plus validated closure metadata/docs; separate evidence commit follows.
038/073 are FIXED_VALIDATED, details in demos AUDIO_INSTANCE_GUARDS_CHECKPOINT.md.
No push/merge. Only011 ownership production/test changes remain uncommitted,
including owner ContentReader/ContentTypeReader files; do not stage/stash/rewrite.

**No active heavy job. Next task:099 VideoPlayer guard and lifetime repair.**
95880 completed028 sparse-large-offset9/9PASS (1911ms build/128ms run, peak365604864)
and099 guards9=5FAIL/4PASS (2231/47/25ms,peak420233216). Its lifetime fixture failed
Werror dangling-else compile; source/log retained. Braces-only69241v2 confirms
lifetime6=4FAIL/2PASS (2180/302/23ms,peak445419520). All hashes unchanged and memory
events0. These are pre-repair defect baselines, not product passes. Real decoder
fixtures confirm retained-identity failures and old-video destruction stopping the
replacement playback. Repair the private relationships safely, including copy/
multiple-player lifetimes; do not simply retain a raw pointer. No028/029/099 source
edits yet. Keep028's now-measured >2GiB lazy streaming capability when repairing IO.
Older entries below are chronological history, not current RUNNING state.

038/073 VALIDATION COMPLETE, commits/issue ledgers being finalized.69886 external-v2
TERMINAL0 (build7019ms/zero diagnostics), original23 PASS,112 exact comparisons:
each renderer48PASS/7 unchanged historicalFAIL/1manualNOTRUN.22724 metadata-final
TERMINAL0 (build9836ms/zero diagnostics), same23/112 outcomes, only038/073 markers
removed,318 other binaries byte-identical. Both keep all counts and failed labels;
the one measured Apply3D oracle correction is explicitly retained, not hidden.
23775 registered32native PASS11393ms and19demo PASS23532ms; hashes unchanged,
peak228753408/events0. v2 peak1996042240/events0;final peak1265840128/events0.
No new full719 sweep/visual/hardware claim; prior011 dirty ownership work remains
unaccepted and excluded from these commits. No push/merge. Next: record exact
073/038 commits, then run028 large-offset9 and099 guards9/lifetime6 baselines
serially before choosing the next repair; do not stop at this batch.

37318 original external TERMINAL1: strict112-case comparison rejected exactly
the SOFTWARE/OPENGLES3 Apply3D demo's old assertion requiring a refused Pan write
to mutate-0.5 to0.75. Both original038/073 demos and the original23 fixtures PASS
against the actual external library. Build2680774ms/zero diagnostics, frozen
sources unchanged;peak4303581184/high2623/max0/OOM0. The old demo expectation
(introduced9a6234b, retained7486b2b) contradicts measured XNA and073's C oracle.
Correct only that strict expected value/comment/label, retaining all18 assertions;
first failed logs/audit stay intact.69886 external-v2 RUNNING, same112 comparisons
and exact oracle-diff proof; source repairs stay frozen and markers remain.
Final driver requires v2, and registered gate now includes the corrected demo:
32native/19demo CTests. No closure/push/merge. Follow up with prepared028 large-
offset9 and099 guard9 plus new real-decoder lifetime6 baselines, all currently
UNRUN. Details and uncommitted lifetime fixture in demos HANDOFF/triage.

08:45 UTC:76943 integrated sanitizer TERMINAL0,27/27PASS (original23 plus4
existing disposal/cleanup controls), actual audio and Sharp Runtime ASan/UBSan/
float-cast-overflow/leak instrumentation, no suppressions/findings. Library build
32465ms/zero diagnostics, strict fixtures15999ms/run223ms/XML22ms; hashes unchanged,
peak1258749952/events0.37318 external original gate RUNNING, sole heavy job:
cna-repair-audio-instance-guards-external.scope, existing linux-full/1002 tasks,
ONE worker/6GiB/no swap/private GPU. Original23 fixtures and112 renderer cases follow.
Keep038/073 markers until their actual demos pass; metadata-only final112 and
32native/18demo CTests remain. No closure/push/merge. Sources frozen.
55931 also completed028 baselines: retained-file6=4PASS/2FAIL, compile1449ms/run113ms,
peak365621248/events0; ctor14=5PASS/9FAIL, compile2704ms/run45ms/XML21ms,
peak476516352/events0. Hashes unchanged. Absolute paths alone do not fix lost
file identity; constructor path/refusal defects also reproduced. No028 repair yet.

08:42 UTC:16641 native-v4 TERMINAL0. Full10905=10804PASS/101sameSKIP,
1202840ms; all10882 prior outcomes preserved with the two documented XNA helper
renames and23newPASS. Wayland case PASS88ms. Compare115ms, source hashes unchanged,
peak1140305920/all memory events0. Audio775=771PASS/4sameSKIP8726ms, focus175PASS2468ms.
No audio closure yet: sanitizer27, external112/original markers, final metadata-only
external112, then registered32native/18demo gates remain. Sources stay frozen.
56438 genuine VideoPlayer reference completed0 AFTER the full suite:41/41 managed
guards/retention checks PASS, compile2246ms/run445ms,peak295788544/events0. No real
decoder/playback validation. The two prepared028 baseline probes now run serially
in cna-repair-wave-bank-followup-before.scope; no production028 change.

08:37 UTC: native-v4 still running; XnaDifferentialBuild parity PASS552480ms.
Additional static platform gates: classification, renderer boundary, ratchet and
hot-path lint pass. Inventory check found two stale descriptive counts in
plans/plan_platform.md (131/81 versus132/82). Comparing to its last committed
generated block identifies exactly the already committed058 MediaQueueReplayTests.cpp
with SDL_AUDIODRIVER; no new production coupling (50 production files unchanged),
no changed identifier set (956), no raised budget. Corrected only those two counts;
fresh five-gate validation39347 PASS (1332/1243/1287/1230/1081ms), reports
repair-audio-instance-guards-platform-v2-*. Retain the first failed-check report.
Demos79fa7c1 preserves prepared028 constructor14/029 WAV-format19 cases, UNRUN.

LATEST 08:21 UTC:16641 native-v4 RUNNING, sole heavy campaign job.60977 finished
full10905=10803PASS/102SKIP in1298418ms but failed strict parity: exactly the
formerly PASS Wayland recentering case became SKIP. No failed runtime tests;
hashes unchanged, peak2213658624/all memory events0. The launch had incorrectly
forced SDL_VIDEO_DRIVER=x11 and removed private WAYLAND_DISPLAY. Fresh same-binary
controls reproduce forced-X11 SKIP and private-Wayland PASS93ms. No oracle/source
change. v4 retains private Wayland, unsets both global video pins and modern audio
override, uses overridable legacy SDL_AUDIODRIVER=dummy. Same full/parity assertions,
only new report prefix; preserve v3 failure. Build429ms/zero diagnostics and three
driver controls41/44/44ms PASS. Final audio/full audit pending.6GiB/no swap,1worker,
private GPU/network/owner UID/no caps. Downstream scripts require native-v4 success.

Prepared after60977 (NOT run): demos build-probe/repair-audio-instance-guards-sanitizer.mjs
reuses existing build-asan canonical CnaAudioTests library closure, instruments both
changed TUs and Sharp Runtime, runs original23 cases with ASan/UBSan/float-cast and
leaks enabled. Then repair-audio-instance-guards-external.mjs: actual external
library/original23,112 scenarios, only038/073 improve and markers remain. After
that passes remove exactly the two marker lines and073 obsolete restriction;
external-final driver proves exact metadata-only changes against demos HEAD,
requires sanitizer acceptance and318 unchanged non-browser artifacts, reruns112.
Do not commit demo marker edits before external-final. Finally ctest driver expects
32 native and18 demo cases. Run serially under same6GiB private scopes.
Separate validated commits for073,038, four nodiscard casts, missing-device harness,
and ledger/evidence; preserve011. New028 retained-file rename/replacement six-check
probe/driver is prepared but UNRUN, see demos MEDIA_PATH_TRIAGE.md. No new closure.

### Latest:60977 native-v3 FULL10905 RUNNING; audio/focus green

60977 cna-repair-audio-instance-guards-native-v3.scope is the ONLY live heavy job.
Build9521ms/zero diagnostics PASS, harness modern/legacy/both driver-env controls
PASS55/54/58ms; audio775=771PASS/4sameSKIP8923ms; focused175PASS2601ms. Full10905
is running, final parity/hash/memory audit pending. Same one-worker/sharedcache/
private GPU and cna-test0 namespace, owner UID/no capabilities,6GiB max/no swap.

12090 TERMINAL1 after audio9063ms, before full: exactly two failures retained.
The no-hardware harness only overrode legacy SDL_AUDIODRIVER; SDL3's current
SDL_AUDIO_DRIVER=dummy inherited from the runner took precedence (confirmed in
third_party/SDL/src/SDL_hints.c GetHintEnvironmentVariable). Set both to the same
nonexistent name; keep expected NoAudioHardwareException. The stress test's final
disposed PendingBufferCount call now throws; preserve its SAME1000 queue-size
bound via the read-only seam and additionally assert public refusal. This is not
a changed tolerance/new skip. No final v2 hash/memory audit was emitted; logs kept.

v3 verifies the exact v2 failed-ID set, the exact four original diagnostic lines,
and the source-only stress correction against its prior hash. Expected rebuilt
CnaTests artifact is explicitly recorded. Frozen set includes the harness and
four-cast CnjCapabilityMatrixTests change. Keep those two infrastructure fixes
separate from audio commits. Prepared sanitizer/external drivers now require
native-v3 audit. Next sanitizer23 original cases, external112 parity, then marker
updates/registered CTests and isolated commits. No038/073 closure or push yet.

### Latest:12090 native-v2 RUNNING after four diagnosed legacy test warnings

21670 TERMINAL1 before tests: successful222655ms build, but strict diagnostic
review found four nodiscard warnings in clean HEAD CnjCapabilityMatrixTests.cpp
lines253/265/277/288. ContentManager::Load was already nodiscard in HEAD. Original
log/progress retained; no final hash/memory audit. Corrected only those four
EXPECT_THROW expressions with explicit(void), preserving inputs/exception types
and case names; commit separately from audio fixes after validation.

12090 RUNNING cna-repair-audio-instance-guards-native-v2.scope. Driver verifies
the exact initial diagnostic set and unchanged inputs, permits only the expected
rebuilt CnaTests hash, then runs the normal strict build/audio/focus/full gates.
Incremental build8290ms/zero diagnostics PASS; audio module now running. Same
private GPU/network, owner UID/no capabilities, one worker/sharedcache/6GiB/no swap.
Only12090 is live; sources frozen until final audit. Prepared sanitizer/external
drivers require native-v2 audit, not the incomplete original. No issue closure.

### Latest:21670 native audio/full regression RUNNING; focused23/23 PASS

41722 TERMINAL0: build116708ms/zero diagnostics; original18 pan cases PASS
(strict compile3448ms/run131ms/XML32ms), original5 dynamic-disposal cases PASS
(compile2702ms/run53ms/XML29ms). Hashes unchanged;peak1260371968/all events0.
21670 now RUNNING cna-repair-audio-instance-guards-native.scope: existing
CnaTests/CnaAudioTests rebuild, module/focus/full, exact prior015 full comparison.
Expected10905 cases, only the two documented old-helper name/contract corrections
and23 newPASS; every other prior outcome must remain. Private GPU/network
cna-test0, owner UID/all capabilities zero, one worker/sharedcache/6GiB max/no swap.
No second heavy job; freeze native sources until final audit. External original
probes/112 demo comparisons and registered CTests remain pending. Markers038/073
are still unchanged; no closure/commit yet. Driver/reports repair-audio-instance-guards-native-*.

### Latest:41722 audio-instance focused candidate RUNNING

Current CNA94423036b plus uncommitted038/073 candidate and preserved011 changes.
Demosaf5a986 contains the accepted015 evidence. New baselines:0288353 gives exactly
6PASS/2lazy-pathFAIL (compile1680ms/run114ms,peak357634048/events0);07375902 gives
8PASS/10FAIL across18 cases (compile2456ms/run110ms,peak454631424/events0);03834449
gives1setterPASS/4FAIL (compile1968ms/run45ms,peak410947584/events0). All hashes
unchanged.073's first compile59404 failed two dangling-else warnings in the new
fixture; braces fixed, Werror retained, original50d0c97/log preserved. No cases ran
in that failed setup.028 is now CONFIRMED_OPEN for lazy retained-path identity,
NOT its disproved title-root expectation. No028 production repair yet.

41722 runs cna-repair-audio-instance-guards-after.scope with ONE worker/private
GPU/sharedcache/existing native tree/6GiB max/no swap. Driver in demos build-probe;
reports repair-audio-instance-guards-after-*. Builds cna_shared then original
unchanged18+5 fixtures. Freeze sources until final hash audit. Candidate changes
SoundEffectInstance pan ordering/mutation and DynamicSoundEffectInstance's four
disposal guards, matching header comments, two new native fixture files and the
bindings divergence ledger. Original queue-cleanup assertion is preserved through
a narrow read-only DynamicSoundEffectInstanceTestAccess friend. Two old wrongly
FNA-derived *AfterDisposeDoesNotThrow tests are deliberately corrected/renamed to
*AfterDisposeThrowsObjectDisposed; full parity must explicitly map those two IDs
and add exactly23 cases (expected total10905). No closure/commit before focused,
native/full and external regression, marker updates and CTests are validated.

Matrix95FINAL/60pending:87RESOLVED/7ALREADY_FIXED/1demo correction,
51PENDING_VERIFICATION/9CONFIRMED_OPEN. Next after41722: native audio/full gates,
external original fixtures and112 audio/media/C comparisons (only038/073 expected
improvements), registered CTests, isolated commits, then028/029.011 remains OPEN;
all its ContentReader/ownership changes stay unstaged. No push/merge.

### Latest: ISSUE-015 CLOSED; continue audio reproductions

CNA fix `9639707991b589914d8db8224b6d79f3867d60d0` contains only title-relative
Song resolution/comments and native tests. All pending011 ownership hunks,
including those in Song.cpp/Song.hpp and the owner's content reader files, remain
uncommitted and untouched. Demos validation source is50d0c97 plus the now-tested
play_song relative-path/identity assertion; matching evidence commit follows.

79454 TERMINAL0: external-v2 incremental build917304ms/zero diagnostics, original
eight cases PASS and both cold-title overloads PASS;112 audio/media/C comparisons
preserve all previous statuses/markers/failure labels. play_song alone adds one
passing file-identity assertion (six checks); each renderer46PASS/9historicalFAIL/
1manual NOT RUN. Sources/artifacts unchanged;peak2254340096/all memory events0.
Original23738 timeout remains explicitly retained, not counted as a product pass.
49056 TERMINAL0:9 native CTests3621ms+16 demo CTests11881ms, exact JUnit counts,
hashes unchanged;peak211771392/events0. Full native checkpoint10882=10781PASS/
101sameSKIP retains every10874 predecessor outcome plus eightPASS. See demos
docs/repair-evidence/content/MEDIA_PATH_TRIAGE.md for all reports and limitations.

Matrix95FINAL/60pending:87RESOLVED/7ALREADY_FIXED/1DEMO_EXPECTATION_BUG;
52PENDING_VERIFICATION/8CONFIRMED_OPEN. Linux SOFTWARE/OPENGLES3 runtime only;
no new sanitizer/visual/full719-demo/global acceptance, and011 remains OPEN with
the C Effect lifetime regression. No live heavy process at this checkpoint.
Exact next task: run prepared028 lazy-path baseline, then07318-case pan baseline
serially under existing6GiB/no-swap/private GPU scopes.029/038 native witnesses
remain unrun. No push/merge. All older RUNNING entries below are historical.

### Latest:79454 external-v2 RUNNING after explicit build-timeout continuation

23738 TERMINAL1 at07:16UTC: build1800106ms reached the driver's30-minute limit,
SIGTERM/ETIMEDOUT after683/1001 steps, zero compiler warning/error diagnostics.
No runtime case ran; no final memory/hash audit was emitted. Original logs and
progress preserved. Host inspection verified its scope inactive/dead and no
remaining build processes before continuation. This is not a source/test failure
or a product pass.79454 now RUNNING cna-repair-song-uri-path-external-v2.scope:
same existing build/one worker/sharedcache/private GPU/4GiB high/6GiB max/no swap,
90-minute build allowance,324 remaining steps (38 done07:19UTC). Source freeze
unchanged, no second heavy job. Driver requires the recorded timeout explicitly;
all assertions and058 comparison unchanged. Next actual external probes/112 cases,
then repair-song-uri-path-ctest-v2.mjs (native9 and demo CLI/play_song16), isolated
015-only commit and status update. Preserve011 ownership hunks and all historical
failures. CNA a9d74c023; demos ee116cf plus play_song/handoff changes. No push.
Prepared028 lazy path and native029/038 witnesses remain unrun. Genuine029(25+71),
038(24) and corrected073(33) measured controls are complete; initial073 wrong-order
oracle4FAIL remains preserved. Matrix94FINAL/61pending (52PENDING/9CONFIRMED).

###073 genuine refusal order measured;015 external build still running

69388 first draft TERMINAL1:10PASS/4FAIL disproved its range-before-live3D
assumption. Original source/logs retained. Actual XNA set_Pan IL0x1ac10 orders
disposal, prepacket latch reset, live3D refusal, range validation, native call,
then public mutation. Corrected67026 gives33/33PASS, C# build4003ms/run837ms,
peak262078464/events0. Managed refusal fixture only, not native voice/playback.
No CNA source changes.073 CONFIRMED_OPEN;94FINAL/61pending now52PENDING/9CONFIRMED.
External23738 still RUNNING (~559/1001 steps07:08UTC), no diagnostics. Its driver
has a30-minute build timeout; if reached, retain that setup failure and continue
incrementally only once its owned process tree has exited. Do not weaken tests or
launch another heavy job. No015 closure or source commit yet.

### Small genuine audio references completed beside the sole external C++ build

52326 TERMINAL0:029 extended71/71 managed WAV checks PASS, C# build2708ms/run585ms,
peak285597696/events0.18009 TERMINAL0:038 genuine24/24 disposal-guard controls
PASS, build3248ms/run780ms,peak268726272/events0. Each used a separate1GiB/private
display/no-swap scope; they ran serially and have both exited. No second heavy
C++ build or CNA source mutation. Sources and actual Framework hash/MVID intact.
No successful native sound creation, real XNA disposal or hardware output claim.
038 is CONFIRMED_OPEN, including the two conversion helpers' shared missing guard;
matrix94FINAL/61pending, now53PENDING_VERIFICATION/8CONFIRMED_OPEN. Native new038/
029 witnesses and028 lazy-path probe remain UNRUN. Demos evidence checkpoint2da9d23
plus these reference results.23738 remains the only live job, around485/1001 build
steps at07:03UTC, zero diagnostics so far. No015 closure, source commit or push.

### Latest:015 full native PASS;23738 external renderer regression RUNNING

31828 TERMINAL0: full10882=10781PASS/101sameSKIP, all10874 old outcomes unchanged
plus eightPASS. Full1228182ms/compare124ms, frozen inputs unchanged; peak4297228288,
high943/max/OOM0. Media311PASS and focused247 cases have no failures. Cold-title
string/typed controls PASS. No015 closure yet; matrix94FINAL/61pending.

23738 RUNNING cna-repair-song-uri-path-external.scope, driver in demos build-probe.
Existing linux-full incremental build, unchanged eight-case/cold-title probes
linked to its actual shared library, then112 audio/media/C renderer comparisons.
The play_song demo uses a relative URI and adds one title-file identity assertion;
all five original playback checks stay. All other statuses/checks/markers must
match058 external baseline. Private GPU/one worker/sharedcache/4GiB high/6GiB max/
no swap. Freeze CNA and demo source; no concurrent heavy job. Next registered
CTests then isolated015 commit, preserving all011 hunks. Prepared next evidence:
028 lazy-path eight-check probe and029 genuine71 WAV chunk controls (both UNRUN).
Demos also contains prepared029 nine-case native stream fixture, UNCOMPILED.
No new push/merge. The sandbox ctest -N attempt failed read-only discovery writes,
not a runtime test; rerun real CTest with proper permissions after this gate.

### Resume:31828 full native RUNNING; media and cold-title controls PASS

2026-10-21 06:29 UTC: build147276ms PASS, strict cold-title probe build1643ms,
string44ms/typed43ms both resolved=1 in fresh processes. Media311PASS11821ms;
broader focused gate PASS2064ms. Full native suite still runs; final10874-to10882
parity, memory and hash audit not yet available. No concurrent heavy job/source
changes. Cold-title probe is committed in demos dc12e06; older prepared/not-run
notes below are historical. Next full parity, then external relative-path demo
and SOFTWARE/OPENGLES3 regression. No015 closure or new push/merge.

### Latest:015 focused8PASS;31828 native/default-title/full regression RUNNING

23666 TERMINAL0: incremental cna_shared build103408ms/zero diagnostics; unchanged
original eight-case fixture now8PASS (build2858ms/run43ms/inspect22ms).
All frozen sources unchanged; expected rebuilt libraries recorded;peak1353715712,
all memory events0. No015 closure yet. Matrix94FINAL/61pending,015 CONFIRMED_OPEN.

31828 RUNNING cna-repair-song-uri-path-native.scope. Rebuild CnaTests/CnaMediaTests,
strict fresh-process default-title probe (string and typed), media, focused/full
native regression. Compare every10874 preceding outcome and exactly eight newPASS.
The moved existing relative-URI test must retain its full-suite ID/outcome (focused
media count becomes311 because that one test now belongs to integration).
Driver ../cna-demos/build-probe/repair-song-uri-path-native.mjs; reports same prefix.
Private GPU and user/network namespace, capabilities dropped, one worker/shared
cache/4GiB high/6GiB max/no swap. Freeze CNA production/native tests, no overlapping
heavy job. New cold-title probe source in demos not yet committed/run. Next external
demo path assertion and SOFTWARE/OPENGLES3 regression after native gate.011 separate
ownership work remains uncommitted; preserve its hunks when eventually staging015.

### Current:015 reproduced/local repair;029 genuine25PASS

11469 TERMINAL0 confirms OPEN015: strict eight-case baseline gives six relative
title/cwd/decoy failures and two absolutePASS;build2196ms/run35ms/audit20ms,
peak867409920/events0/hashes unchanged.95566 TERMINAL0: genuine XNA managed WAV
and public pre-native FromStream oracle25/25PASS, MVID34d977ae-c3ec-4d07-aa6d-
6fed6d8e3864,peak317890560/events0. No native audio/playback claim for that oracle.

23666 RUNNING: repair-song-uri-path-after.mjs rebuilds existing cna_shared then
reruns unchanged015 eight-case fixture. Single worker/sharedcache/private display/
6GiB max/no swap. Source/native tests frozen. Local015 uses core TitlePath bridge
and platform base path; no new runtime dependency. Eight reproduced tests copied
to tests/MediaSongTitlePathTests.cpp. Existing typed-relative SongTest keeps its
ID but moves there for public TitleLocation setup and correct title-rooted handle;
two plain-path controls use existing absolute fixture. Full comparison must retain
all10874 outcomes and add eightPASS. No015 closure/commit yet.
IMPORTANT: Song.cpp/Song.hpp contain BOTH pending011 and015 hunks. Stage only015
hunks when validated, never sweep in the unaccepted011 ownership work.
CNA a9d74c023, demos aa4551b plus current evidence updates.058 remains closed;
011 C effect/model contract choice still pending. No push/merge. Next after015
focused gate: native media/integration/full and external path/media regression.

### Latest closure:058 fixed in 380c32153c8063c38f6354dc9e37a3c23c2dd0f0

All targeted sanitizer/native/external/CTest gates below have completed. Matrix
now94FINAL/61pending (86RESOLVED/7ALREADY_FIXED/1DEMO_EXPECTATION_BUG;
55PENDING_VERIFICATION/6CONFIRMED_OPEN). No historical ID removed. CNA source
commit contains only058 fix, tests and narrow test-audit registration;011 remains
uncommitted and unaccepted. Demos evidence at c206224 plus closure notes pending.
No live job; immediately run repair-song-uri-path-before.mjs, then
repair-sound-effect-stream-reference.mjs, serially under the same6GiB/private
display/shared-cache policy. Prepared sources are committed, not yet executed.
No push or merge. No full719/visual/735-CTest green claim for the ownership batch.

### Current:058 validated and ready for its isolated source commit

31597 TERMINAL0: external incremental build49748ms/zero diagnostics; original
unchanged replay probe links the exact external library and passes caller109ms/
queue110ms.112 audio/media/C runs preserve all prior statuses/checks/markers and
failed assertions on SOFTWARE/OPENGLES3 (each46PASS/9historicalFAIL/1manualNOT RUN).
Frozen319 artifacts/native inputs unchanged;peak4306530304/high924/max/OOM0.
73297 TERMINAL0: all six registered replay CTests PASS,2.10s; logs/XML retained.
Together with integrated sanitizer and full10874 native parity,058 can be closed.
Only MediaPlayer.cpp, six native regressions and their exact one-reference SDL
test-fixture classification belong to the source commit. Preserve every011 file.
No process remains running. Next record repair SHA/status in both repositories,
then run prepared015 native decoy baseline and029 genuine managed stream oracle.
011 remains open/uncommitted; no full campaign acceptance, push or merge.

### Current:058 full native PASS;31597 external regression RUNNING

90349 TERMINAL0: full10874=10773PASS/101sameSKIP, all10868 predecessor outcomes
unchanged and exactly six newPASS. Full1235135ms, compare96ms; frozen hashes
unchanged;peak4297822208/high2020/max/OOM0. Build18594ms zero diagnostics;
media312/312PASS12263ms, wider625PASS/4SKIP6143ms. Sanitizer after-v3 remains green.

31597 RUNNING cna-repair-media-queue-replay-external.scope. Existing linux-full
incremental build, unchanged original probe linked to that exact external shared
library, then56 audio/media/C scenarios on each SOFTWARE/OPENGLES3 with exact
status/check/knownIssue/failed-assertion parity. Historical failures and manual
NOT RUN are retained, not counted as product passes. Driver in demos build-probe;
reports repair-media-queue-replay-external-*. Serial/shared cache/private display/
4GiB high/6GiB max/no swap. Freeze production/native sources. No overlapping job.
Next finish058 external gate, commit only its source/test/audit changes, update
permanent issue status. Then run prepared015 and029 evidence (demos c206224).
The011 ownership batch remains uncommitted/open with the recorded C Effect
lifetime regression; owner C contract question unanswered. No push/merge.

### Current:058 sanitizer PASS;90349 full native regression RUNNING

98405 TERMINAL0 (after-v3): unchanged original caller AND queue probe PASS;
all six unchanged native regressions PASS under ASan/leaks/UBSan/float-cast-overflow.
Media archive rebuild6516ms, original probe1581ms, six-test fixture2458ms, zero
diagnostics. Expected MediaPlayer/media archive delta only; all other frozen
inputs unchanged. Peak654491648/all memory events0. First two configure failures
remain preserved: new dummy-driver test lacked platform-audit classification;
it now has exactly one audio-native-fixture entry, scanner rules unchanged.

90349 RUNNING, cna-repair-media-queue-replay-native.scope: build18594ms PASS;
CnaMediaTests312/312PASS12263ms; wider focus625PASS/4SKIP6143ms. Full CnaTests
running, must preserve all10868 previous ownership-checkpoint outcomes plus six
newPASS. Driver ../cna-demos/build-probe/repair-media-queue-replay-native.mjs,
reports repair-media-queue-replay-native-*. Private GPU/network cna-test0, owner
UID/all capabilities dropped, one worker/shared cache/4GiB high/6GiB max/no swap.
Freeze production/native tests; no overlapping heavy job. Next full comparison,
then external media reproduction and renderer regression. No closure/commit yet.
011 C effect/model contract decision pending; no broad acceptance claimed.
Matrix93FINAL/62pending;058 now CONFIRMED_OPEN. No push/merge.

### Latest:058 reproduced; local queue lifetime repair awaiting validation

16171 TERMINAL0: external build2864576ms/zero diagnostics; same independent-C
fixture passes SOFTWARE125ms/OPENGLES3 282ms.60398 TERMINAL1: focused54 runs
give52PASS/2FAIL, only C content_manager (Effect after Unload/destruction on each
renderer). Original011 ownership/disposal demos now PASS on both with unchanged
assertions. Frozen inputs unchanged; build peak4301131776/high2903/OOM0;
focused peak1311191040/events0. C effect/model owner choice remains pending.

6576 TERMINAL0 confirms OPEN058 in fully instrumented existing build-asan:
unchanged historical probe caller0/queue ASan UAF; six new strict-compiled tests
give two caller controls PASS/four queue-alias UAFs at LoadSong after Clear.
All frozen hashes unchanged;peak3494117376/events0. Reports in demos
build/linux-full/reports/repair-media-queue-replay-before-*.
MediaPlayer.cpp now locally retains the old queue through both Play overloads;
the exact six baseline tests are copied to native media tests. NOT validated yet.
Next rebuild sanitizer media archive, relink unchanged probes, then native media
and full regression. No heavy job currently running. CNA8d73b19c0 plus separate
uncommitted011 ownership batch and this058 candidate; demos9de99c3 plus evidence.
No issue closure, marker removal, push or merge. Older RUNNING notes are history.

### Latest diagnostic:53232 C effect regression;16171 still RUNNING

Candidate C shared library linked successfully during16171. Early pure-C
content_manager diagnostic53232 TERMINAL1: SOFTWARE and OPENGLES3 each fail
exactly2/67 checks (Effect use after Unload/manager Destroy); other65 pass.
Renderer identities and all artifact/source hashes verified;peak158732288/events0.
Reports repair-content-c-contract-early-*. No stale C++ executable run. Separate
512MiB scope for this brief no-compile diagnostic;16171 remains the only heavy
job. Do not call the ownership batch accepted or hide the two failures.
Owner now has the measured C effect/model lifetime contract choice; no answer
presumed. Preserve assertions until the intended public contract is settled.

Prepared, NOT run: build-probe/repair-content-ownership-c-focused.mjs (24 C
content scenarios plus three original ownership/cache/unload demos,54 cases on
both renderers), and
build-probe/repair-media-queue-replay-before.mjs (unchanged058 probe against
existing integrated build-asan, caller control and actual UAF diagnostic, then
six new prepared native-style alias/control tests in separate processes).
Run after16171 finishes, serial heavy work. Source-only058 collection-alias
audit and proposed move-retainer fix are in demos evidence, not implemented.
Demos commits b4f3a38,868f684,3935305,4a0533d preserve C baseline/native results,
058 original probe and020 relocation constraints; newest runtime notes pending
commit. CNA matrix current_validation updated for011, status stays open.

### Current:26923 native PASS;6476 C baseline PASS;16171 external build RUNNING

26923 TERMINAL0: full10868=10767PASS/101sameSKIP, all10807 original057 outcomes
unchanged plus61 newPASS. Wider focus1524=1519PASS/5SKIP. Build857043ms,
focus65764ms, full1152852ms, compare118ms;peak4296716288,high308,max/OOM0.
All frozen source/artifact hashes unchanged;66 build warnings exactly match the
previous native location/message multiset. Integrated sanitizer61PASS still valid.

6476 TERMINAL0: permanent demos ContentIndependentHandleLifetimeProbe.c compiled
C17/Wall/Wextra/Werror and passed against original057 C ABI on SOFTWARE/OPENGLES3.
Actual96 exact cube pixels, existing looped playback and new playable instance
at all three boundaries (before Unload, after Unload, after manager Destroy).
Build261ms, SOFTWARE126ms, OPENGLES3 280ms;peak278118400/events0;hashes unchanged.
Reports ../cna-demos/build/linux-full/reports/repair-content-independent-c-before-*.

16171 RUNNING in cna-repair-content-ownership-external.scope: existing external
linux-full incremental rebuild, then SAME compiled C fixture with candidate C
library on both renderers. Driver ../cna-demos/build-probe/repair-content-ownership-external.mjs;
reports repair-content-ownership-external-*. One worker/shared cache/private GPU,
4GiB high/6GiB max/no swap. Freeze production/native sources; no overlapping build.
Next inspect candidate actual C use, run existing C content scenarios and complete
SW/GLES/CTest/visual parity. C effect/model lifetime choice still pending; preserve
their post-Unload assertions.93FINAL/62PENDING,011 open. CNA8d73b19c0 ownership
batch uncommitted. Demos has new C fixture plus read-only020 triage and preserved
historical058 probe; neither020 nor058 has a new runtime result. No push/merge.
Older RUNNING sections below are chronology, not active process status.

### Current:90369 sanitizer61PASS;26923 native regression RUNNING

90369 TERMINAL0. Reused sanitizer libraries160ms; fixtures compile35927ms;
diagnostic comparison20ms verifies exactly145 unchanged renderer-interface
location/message warnings, with no other diagnostic. Actual61 native tests from
11 suites allPASS, process462ms (GTest238ms), XML audit21ms. ASan/leak/UBSan/
float-cast-overflow enabled; no sanitizer report or suppression. SOFTWARE;
affected CNA and129 enabled Sharp Runtime TUs instrumented, vendors not.
Peak1215119360,all memory events0; all source/artifact hashes unchanged.
Original13324 failed compile and35869 preflight remain preserved evidence.

26923 RUNNING, cna-repair-ownership-native-current-v3.scope. Driver
../cna-demos/build-probe/repair-content-ownership-native-current.mjs; reports
repair-content-ownership-native-current-v3-*. Sanitizer audit/hash prerequisite
passed; canonical CnaTests is rebuilding after public-layout changes, followed
by wider focused tests and full suite. Exact057 comparison requires all10807
original outcomes unchanged plus61 newPASS (expected10868 total). Private display
and private network, owner UID/all capabilities dropped, cna-test0 limited
broadcast route verified by driver. One worker/shared cache/4GiB high/6GiB max/
no swap. Freeze production/native tests; poll26923, no concurrent heavy job.

CNA8d73b19c0 still uncommitted; demos checkpoint b465720 is committed/clean.93FINAL/
62PENDING;011 open, no knownIssue removal. After native gate: rebuild external
C API/demos, validate actual independent C-handle use and both renderers/CTest/
visuals. C model/effect lifetime decision remains pending. No push/merge.
Read-only C coverage audit: standalone/c_api.content.content_manager/main.c already
checks texture pixels and font measurement after Unload/Destroy and subsequently
draws them. Reuse it. additional_loaders currently destroys sound/cube before
Unload; add focused real-C sound-instance/cube-pixel lifetime coverage after the
current source-frozen native gate. Do not mistake native C-style detachment tests
for actual ABI validation. No new C fixture/source has been added yet.

### Prior:90369 sanitizer continuation started;13324 TERMINAL1

13324 libraries-build0/1262919ms, then strict native-fixture compile1/37387ms.
All145 errors are unused parameters in unchanged HEAD IGraphicsRenderer.hpp,
required by the cube weak-renderer-expiry assertion. No tests ran; do not call
this a sanitizer runtime failure/pass. Library build has16 existing Draco warnings;
configuration has the unused FetchContent flag warning. Peak4297129984,
high1707,max/OOM0; all frozen source/artifact hashes unchanged.

90369 reuses the exact existing build-asan/cache and verifies original hashes.
35869 stopped before any compile: cache guard expected literal ccache, but CNA
uses /usr/bin/cmake;-E;env;CCACHE_BASEDIR=/rv;/usr/bin/ccache. Verified that exact
existing wrapper and corrected the guard; no configuration or source changed.
Driver same path, report prefix repair-content-ownership-integrated-asan-v2.
It retains Wall/Wextra/Werror, demotes only unused-parameter errors to visible
warnings and requires the exact145 original location/message diagnostics, with
no new/changed diagnostics, before running all61 unmodified native tests. No
production/header/test/assertion edits, suppression, reconfigure or clean rebuild.
One worker, private display, shared cache,4GiB high/6GiB max/no swap as before.
Poll90369; no overlapping heavy job. Native-current-v3 is prepared, NOT launched,
and requires v2 sanitizer completion/hashes before rebuilding/running native tests.
93FINAL/62PENDING;011 remains open. Older RUNNING notes below are chronology.

### Prior:39 focused PASS;13324 integrated sanitizer build started

Demos checkpoint committed efb6e2d; worktree clean. CNA remains8d73b19c0 with
the current ownership/race/metadata batch uncommitted.13324 remains authoritative
live job, last observed603/725 library build steps. Do not start another build.
The host has about24GiB available RAM at the latest read-only check. Resumed CLI
PID3439242 RSS380272KiB and tool-serving Codex PID10934 RSS793936KiB are separate
processes; do not describe either number as total Codex memory. No process stopped.
Scope peak2918453248 bytes so far. Driver71806 has no inherited ASAN/UBSAN/LSAN/TSAN
options; its children explicitly enable ASan/leak/UBSan halt-on-error checks.
Prepared (NOT launched) native-current-v3 driver requires the completed sanitizer
audit and matching source/artifact hashes, then rebuilds CnaTests and compares the
full suite to057 with exactly61 newPASS and no changed/missing previous outcomes.

57618 TERMINAL0: all canonical link-closure archives and shared libcna.so rebuilt
in353534ms with zero diagnostics; strict39-case fixture compiles20631ms with zero
diagnostics, runs747ms/all39PASS. Exact XML comparison21ms: previous30PASS retained,
two metadata baselineFAIL->PASS, other two metadata controls unchanged, five new
boundaryPASS; no missing cases. All source/fixture/library hashes unchanged;
peak2356383744,all memory events0. The four original metadata tests, genuine oracle
and concurrency reproduction remain byte-identical to e32db28. Both new native
metadata files match their permanent external originals exactly.

13324 RUNNING: cna-repair-ownership-integrated-asan.scope, private display,
one worker/shared ccache,4GiB high/6GiB max/no swap. Driver
../cna-demos/build-probe/repair-content-ownership-integrated-asan.mjs. Existing
build-asan/cmake-build-asan and devices sanitizer variants were absent; created
only the standard persistent CNA build-asan for this genuinely different config.
Debug, SOFTWARE, SDL3 platform/audio, CNA_SANITIZE=address,undefined (GCC adds
float-cast-overflow), O0,FULL debug; examples/C API off, tests enabled, existing
SDL prebuilt reused. No /tmp build/new dependency clone/private ccache.

Configure0/7530ms, with one honest CMake warning: unused
FETCHCONTENT_UPDATES_DISCONNECTED (no FetchContent operation in this config).
Instrumentation audit verifies nine affected CNA implementations and129 enabled
Sharp Runtime translation units have actual sanitizer compile flags. BuildPerformance
explicitly instruments the enabled Sharp Runtime closure; earlier speculation that
Sharp Runtime could not be instrumented was incorrect. Vendored libraries remain
uninstrumented per project policy; do not claim otherwise. Current725-step library
build is active; last scope~547MB current/~578MB peak. No sanitizer runtime result
yet. It will strictly compile/run the actual61 native ownership/core/metadata
tests against the canonical CnaContentTests link closure, not mixed old artifacts.

Reports repair-content-ownership-integrated-asan-*; poll13324, do not restart solely
on a timeout. Its driver intentionally refuses overwriting an existing sanitizer
cache: if a future run is needed, inspect/reuse this same tree, do not create a
second variant. Production/native/fixture source and canonical native libraries
are frozen throughout. CnaTests and external C API/demos executables are still
STALE after the GraphicsResource layout change and must be rebuilt before use.
Next after terminal: investigate sanitizer findings, then current native/full and
external C API/demos/renderer/visual gates. C model/effect ownership decision still
pending.93FINAL/62PENDING;011 not closed, no knownIssue removal, no production
commit/push/merge. Demos boundary evidence/status checkpoint is being committed.

### Live metadata candidate:57618 RUNNING (2026-10-21)

CNA8d73b19c0 retains the uncommitted ownership batch plus the proven core race
repair. New metadata patch adds opt-in shared Name/Tag storage at GraphicsResource,
activated by Texture2D/TextureCube content recording before capturing the stable
representative. Copy/move construction and assignment retain/replace that scope.
Ordinary unrecorded copies keep snapshots; disposal/events/device registration
are not coupled to SharedIdentity. C-boundary DetachContentOwnership first clones
the metadata snapshot, then removes manager ownership, including const wrappers.
Texture2D/TextureCube public copy comments describe the distinction. No broad
renderer-specific change or ContentManager-special-case property setter.

The four measured metadata cases from e32db28 remain unchanged and are copied
exactly into native content tests. New demos ContentTextureMetadataBoundaryTests.cpp
adds five controls: cube copies plus explicit independent disposal, detached const
2D snapshot/pixels after unload, detached cube metadata/pixels, assigning an
unrecorded wrapper, and move assignment between manager scopes. All five also
have exact native copies, but are NOT yet claimed passing. New native total is
expected10868 (old05710807+61 additions), not the previous10857; audit actual XML.

57618 RUNNING: cna-repair-ownership-metadata-candidate.scope, private GPU runner,
one worker/shared ccache,4GiB high/6GiB max/no swap. Driver remains
../cna-demos/build-probe/repair-content-ownership-residuals.mjs but report prefix
is repair-content-ownership-metadata-candidate. Rebuilds the canonical CnaTests
link closure's relative archives AND shared libcna.so, then strict39-case external
fixture (previous30, original4 metadata, new5 boundaries). Comparator requires all
oldPASS unchanged, exactly the two original metadata FAIL->PASS and five addedPASS.
Records source/fixture/archive/shared-library hashes; freeze these files until
terminal. Build reconfigured only because native test globs grew;193 tasks.
CnaTests itself and external C API/demos executables remain STALE until rebuilt.

Demos HEAD e32db28; new boundary fixture uncommitted. A mechanical native-copy
attempt via a shell pipe failed because apply_patch is a provided command wrapper,
not a shell executable; it made no changes. The direct apply_patch invocation then
added both exact native files successfully. No expectation/knownIssue changes.
Next: inspect57618 compile/runtime/compare outcome, fix any failure, then rebuild
native executables and external C API/demos and run integrated sanitizers and full
native/renderers/visual regressions. C model/effect policy remains pending;93FINAL/
62PENDING,011 open, no production commit/push/merge. Older RUNNING notes superseded.

### Current: native checkpoint green; candidate race repaired; metadata reproduced

Demos evidence committed e32db28; worktree clean. CNA remains8d73b19c0 with
the ownership candidate/core synchronization/test changes uncommitted. No heavy
job live; all job handles in this checkpoint are terminal. Continue metadata
repair before the next integrated rebuild, retaining the measured four-case oracle.

80015 TERMINAL0: incremental build8175ms; focus1513=1508PASS/5SKIP in62690ms;
full10857=10756PASS/101sameSKIP in1239549ms. Independent XML compare0/143ms
preserves every old057 outcome, adds exactly50PASS, no missing cases. All frozen
source/executable/libcna.so hashes unchanged. Owner UID1000, zero capabilities,
private cna-test0 broadcast/display verified; peak2008776704, all memory events0.
This is a real pre-followup native checkpoint, NOT a current pass after the core
helper edits below; no external/C API/renderer-sweep/integrated sanitizer claim.

28128 TERMINAL1 (expected observed defect, not a passing gate): strict actual
ContentOwnership.cpp TSan probe build0/2050ms, run66/56ms, reports a real race
between Attach line86 and Unlink line97 on another worker's link. Four threads
only copy/move/destroy DISTINCT wrappers from a stable recorded source; owning
disposal runs after join. Peak169361408/events0. This defect was introduced by
the uncommitted ownership candidate, not claimed as a new historical issue.
Original report repair-content-ownership-concurrency-before-* retained.

Corrected the core helper with a per-group recursive mutex for membership and
state. Disposal removes a member under lock but calls user Dispose outside the
lock, so callbacks may join copying workers. Same-wrapper mutation/destruction
still needs caller synchronization.1170 TERMINAL0: identical4000-copy TSan
reproduction passes27ms, strict build1676ms,peak169459712/events0. Added two native
core tests: concurrent distinct wrappers and callback joining a copy worker
(10-second fail-fast deadlock guard, no detached dangling thread).99336 TERMINAL0:
actual helper and all13 native core tests pass ASan/UBSan/leak, strict build5189ms,
run24ms,peak360095744/events0. Integrated library binaries are now stale and need
rebuilding after the next source batch; no test suppression or runtime workaround.

Metadata baseline94947: genuine XNA15/15PASS, but first new C++ fixture compilation
failed because System::Object is abstract. Added only a concrete MetadataTag with
GetTypeName; unchanged Name/Tag assertions.95660 TERMINAL0 records the expected
native exit1: strict C++ build3674ms/zero diagnostics, four cases2PASS/2FAIL with
12 failed assertions,242ms. Genuine XNA repeats15/15PASS (445ms); native frozen
hashes unchanged; peak494956544/events0. It uses real ContentManager/custom XNB
reader and real unbound BlendState for reference metadata, NOT a reference GPU
Texture2D runtime. Separate-manager/reload and unrecorded-copy controls pass;
cached metadata and its value-copy/base-reference propagation fail. Original
compile failure plus corrected reports repair-content-metadata-before{-v2}-*
retained. Both metadata source files and concurrency probe are permanent demos
evidence, with a documentation checkpoint being prepared from2e0a4f4.

NO heavy job remains live. Next: repair content-acquired Texture2D/TextureCube
metadata sharing at the common GraphicsResource metadata layer, preserving
ordinary unrecorded copies, explicit independent disposal and the independent
C-handle boundary. Do not reuse SharedIdentity wholesale (it also couples device/
disposal/events). Preserve the four-case failing fixture; add native coverage.
Then rebuild current binaries, rerun original ownership/metadata/race controls,
integrated sanitizer and native/external/C API/renderer/visual regressions. C
model/effect policy still awaits owner choice.93FINAL/62PENDING;011 open, no marker
change or production commit/push/merge. Older RUNNING entries are chronology.

### Resume checkpoint:80015 RUNNING;42874 terminal, not a passing gate

Continuation audit (source-only while80015 remains live): original011 explicitly
mentions Name/Tag divergence between cached Texture2D results. GraphicsResource
copying snapshots these fields; the current ownership link only coordinates
disposal. Do not close that related observation as fixed by the disposal work.
Its existing ShareResourceIdentityWith seam also shares disposal/events, so it
cannot be substituted blindly for metadata sharing. Ordinary unrecorded C++
copies have documented independent-wrapper behavior and must retain it.

Demos has two NEW UNCOMMITTED, UNRUN evidence files:
docs/repair-evidence/content/ContentTextureMetadataTests.cpp (four cases: cached
bidirectional metadata, content copies/moves through base refs, independent
managers/reload, ordinary unrecorded-copy control) and ContentMetadataOracle.cs
(15 intended checks using genuine ContentManager/custom XNB reader plus ordinary
unbound BlendState to examine GraphicsResource metadata without a GPU). The
reference is NOT a Texture2D/GPU runtime claim. Neither file is in the frozen
native test suite. Prepared ignored driver build-probe/repair-content-metadata-
before.mjs passes node --check; it refuses execution until80015's full compare
gate succeeds and preserves source/binary hashes, then sequentially runs genuine
XNA and strictly compiles/runs the four-case CNA baseline. Expected native2PASS/
2FAIL is an explicit hypothesis, not yet an observed result. Run only after80015
actually terminates and passes; otherwise investigate its failures first.

Additional source-review risk to investigate before ownership closure: the new
ContentOwnershipLink intrusive Group list is unguarded. Parallel copying and
destruction of distinct wrappers from one stable recorded source may race even
without concurrent ContentManager use. No race has been experimentally confirmed
yet; add a focused actual-helper concurrency/TSan check rather than assuming the
eleven single-threaded ASan/UBSan controls establish thread safety.

Prepared third uncommitted demos fixture ContentOwnershipConcurrencyProbe.cpp
and ignored repair-content-ownership-concurrency.mjs (node --check PASS), NOT
built/run yet. It compiles the actual core helper under TSan and copies/moves/
destroys4000 distinct wrappers on four workers from one stable source; source
mutation and manager/group disposal occur only after joining all workers. Any
TSan runtime/setup failure must not be called a race or a pass. After80015 is
terminal, prioritize this small lifetime-safety probe, then the metadata baseline
against unchanged artifacts, before editing production. Run serially under the
same6GiB/no-swap scope discipline, never alongside the current heavy job.
Latest authoritative scope check:80015 active10min, genuine differential-build
test has live fxc/Wine children,~1.2GiB current/~1.8GiB peak. No restart warranted.

Demos checkpoint committed2e0a4f4, worktree clean.80015 wider focus is now
TERMINAL0/62690ms:1513=1508PASS/5SKIP, no failures/errors. The unchanged structural
count passes with the corrected call spelling. Full native is RUNNING under the
verified private network/display. Scope MemoryCurrent1068859392,peak1975140352,
hard maximum6442450944 at transition. No full-native result yet; poll80015.

Read-only host checks after the reported OOM found resumed Codex3439242 at
360804 then366460KiB RSS (~352-358MiB); host had23689MiB available, no swap.
No process was stopped or configuration changed. The build scope does not limit
Codex itself. Keep tool output bounded; do not read the entire enormous demos
HANDOFF merely to obtain its current checkpoint.

42874 TERMINAL1: native build0/834467ms; focus1513 gives1507PASS/5SKIP/1FAIL
in72359ms. The sole failure is the pre-existing source-structure assertion
CnbSharedModelBuilderTest.TheSharedBuilderIsTheOneEveryModelFrontEndCalls: it
still searches BuildModelMeshPartGeometryEXT(device, geometry), whereas all
three reviewed callers now pass the owning cm. Updated only that exact search
string to (device, cm, geometry), retaining the expected count3 and all other
structural assertions. Runtime expectations unchanged. Source/binary freeze
passed; peak4298309632 bytes, high1225,max/OOM0. Full suite never started because
focus failed. Original reports repair-content-ownership-native-current-* retained.

The original launcher also omitted the baseline's private network namespace.
It is not accepted as a full comparable checkpoint. Corrected driver now asserts
repository-owner UID, all five capability sets zero, and the private cna-test0
limited-broadcast route before building/testing. The launcher uses
unshare --user --map-current-user --keep-caps --net, loopback plus dummy
cna-test0/192.0.2.1/24 and explicit broadcast255.255.255.255 route, followed by
setpriv --bounding-set=-all --inh-caps=-all --ambient-caps=-all. No host network
configuration changes. XML comparison additionally checks total counts, error
elements, all old057 outcomes and exactly50 newPASS.

80015 RUNNING: cna-repair-ownership-native-current-v2.scope, private GPU runner,
one worker/shared cache,4GiB high/6GiB max/no swap. Incremental CnaTests build
passes0/8175ms; wider focus running, then full native if green. Driver remains
../cna-demos/build-probe/repair-content-ownership-native-current.mjs; reports now
repair-content-ownership-native-current-v2-*. Poll80015, do not restart it or
overlap another heavy job. Production/native tests frozen again. The temporary
unrun isolated-only driver was removed after merging its guards into this driver.
Next: investigate every delta, then continue the C API/demos/integrated sanitizer
and Name/Tag audit listed below.93FINAL/62PENDING;011 remains open. No production
commit, push, merge or knownIssue change. Older RUNNING notes are chronology.

### Current job:42874 native rebuild/regression (2026-10-21)

Demos evidence/status is committed asf881dfb; its worktree is clean. Read-only
host check during this job found resumed Codex3439242 at369804KiB RSS (~361MiB),
host MemAvailable23733MiB, no swap. Nothing was stopped or reconfigured. Build
scope limits do not constrain Codex itself. The OpenAI docs describe /compact
as context/token compaction, not a guaranteed RSS remedy; no such claim is made.

Child controls are complete:36218 compile failed on ambiguous Google Test macro
syntax, fixed by braces only.8582 strict build0/4447ms,4PASS/1FAIL: the newly authored
CNB-v2 fixture incorrectly used default0 skin weights; codec correctly refused
that input. Setting that fixture field to valid4 (no assertion/production change)
yields93419 TERMINAL0: strict build6156ms/zero diagnostics,5PASS/324ms,
peak526106624/events0. Four other results unchanged. Native copy of all five
controls added. Current candidate therefore has25+5 externalPASS, plus actual core
helper11 ASan/UBSan/leakPASS; no wider-current-native or integrated sanitizer claim.

42874 RUNNING: cna-repair-ownership-native-current.scope; one worker/sharedcache,
private display,4GiB high/6GiB max/no swap. Driver
../cna-demos/build-probe/repair-content-ownership-native-current.mjs rebuilds
CnaTests, runs the wider Content/Cnj/RuntimeGltf/resource focus, then full native
suite and compares all old057 outcomes. Records changed-source plus executable/
libcna.so hashes and refuses changes during validation. Poll this actual session.
Reports repair-content-ownership-native-current-*. Freeze production/native tests.

Next after terminal: investigate every regression; rebuild external C API/demos,
test independent C texture/font/sound with actual use, resolve pending C model/
effect policy from owner response. Standard integrated sanitizer build still
needed; neither cmake-build-debug nor cmake-build-multi is instrumented, and no
existing build-asan/cmake-build-asan tree was found. Do not substitute a mixed-ABI
or stale executable. Audit the original011 related Name/Tag observation explicitly
before final disposition; the scoped disposal repair has not changed metadata
sharing. Then full SOFTWARE/OPENGLES3/visual/CTest regression.93FINAL/62PENDING;
no closure/knownIssue change/production commit/push/merge. Demos child evidence and
status are being checkpointed from1ca1f78; CNA source remains uncommitted on8d73b19c0.

### Latest residual checkpoint:25PASS; child controls running

58839 TERMINAL1: archive build0/376998ms and strict25-case fixture build0/19796ms
had no diagnostics, but fixture SIGSEGV after10 managerPASS. Peak2182914048/events0;
NOT OOM and NOT a pass. Debugger76318 reproduced an invalid virtual dispatch at
ContentOwnershipAccess::Record<Texture2D>. Root cause was the validation driver:
it rebuilt relative `.a` targets but omitted the canonical link line's `libcna.so`.
The executable therefore loaded the old shared ABI alongside new headers. No
production workaround was made. The original crash log/backtrace remain intact.

Driver corrected to build all relative `.a` AND `.so` link targets, explicitly
requiring libcna.so.44058 TERMINAL0: shared relink0/673ms; strict unchanged25-case
build0/13569ms/zero diagnostics;25PASS/487ms. All18 previous cases stayPASS and all
six measured residual failures improve. Peak848592896,all events0. Reports
repair-content-ownership-residuals-v2-*; no all-renderer/full-native/C-API pass.
CnaTests and other pre-layout-change executables remain stale until rebuilt.

New demos-only ContentModelChildOwnershipTests.cpp has five prepared controls:
CNB-v2's five stock effects/shared buffers, completed-child retention after failed
CNJ model load, one disposal record for a reused cached external effect, embedded
glTF texture, generated white SkinnedEffect texture. Its private bounded driver
is RUNNING as36218, with source/native libraries frozen until it
finishes. Reports repair-content-ownership-children-candidate-*. These are new
candidate controls, not a claimed pre-fix red/green measurement. Demos has only
this new untracked source on1ca1f78. No production commit/closure/push/merge.

### Active residual-path repair (uncommitted; 58839 RUNNING)

Demos evidence/status committed as1ca1f78, its worktree clean. CNA still based on
8d73b19c0 with the ownership batch uncommitted. Added internal polymorphic
ContentOwnedResource capability to the four participating resource types, so an
Object/IDisposable reference preserves the same concrete disposal record. Const
shared-pointer wrappers now use that const-safe capability; arbitrary const custom
IDisposable pointers are not cast to mutable. Added a core erased-const regression;
copied all seven permanent model/polymorphic cases into native content tests.

Non-XNB model loaders now record completed buffers/effects and generated/embedded
textures at materialization, including CNB-v2 and variant buffers. Model-owned
buffers share ownership with the manager instead of being unique-only; a narrow
private content-module recorder reaches the existing registry. External cached
custom effects are not recorded again, and cached/generated texture reuse is not
duplicated. No graph scan, no deep clone or C model/effect policy change.

39862 TERMINAL0: strict actual core helper+11 tests with ASan/UBSan/leak,
build4441ms/run19ms;11PASS;peak335880192;all memory events0. This does not certify
the integrated graphics/audio implementations under sanitizers.

58839 RUNNING: cna-repair-ownership-residuals-candidate.scope, one worker/shared
ccache/private display,4GiB high/6GiB max/no swap. Driver
../cna-demos/build-probe/repair-content-ownership-residuals.mjs rebuilds the exact
in-tree archive targets in the canonical CnaTests link closure (not the test
objects), then strictly compiles/runs the unchanged25 external cases. Reports
repair-content-ownership-residuals-candidate-*. Poll this session; freeze CNA
production/native test sources while it runs. CnaTests and other executables are
now STALE after changed public layouts; do not run them as a current validation.

Next: inspect58839 compile/runtime outcome, then add focused coverage for CNB-v2,
generated/embedded model textures, partial model reads and reused external effects.
Rebuild native executables before wider tests. Rebuild external C API/demos and
verify actual independent C texture/font/sound use; owner C model/effect policy
answer remains nonblocking. Full native/renderers/visuals and integrated lifetime
sanitizer coverage are still required.93FINAL/62PENDING; no011 closure/marker change,
no production commit/push/merge. Historical RUNNING notes below are superseded.

### Latest ISSUE-011 integrated checkpoint and measured residuals (2026-10-21)

94457 TERMINAL0: CnaTests builds0/1291420ms; broader native focus1414 gives
1409PASS/5SKIP in54862ms. Independent XML comparison against057 preserves all1377
previous outcomes exactly (including those5 skips), plus37 newPASS. Strict external
fixtures compile0/9617ms/zero diagnostics; all18PASS in365ms. Both original C++
fixtures and genuine108-check C# oracle remain byte-identical to005899e9. Native
build emitted66 warnings, all in unchanged test files; not diagnostic-free.
Scope peak4300394496,high2551,max0,OOM0. No full-suite/C-API/all-renderer pass yet.

37046 TERMINAL1 (honest reproduced failures, not a crash): additional permanent
demos ContentModelOwnershipTests.cpp and ContentPolymorphicOwnershipTests.cpp
compile strictly0/7240ms/zero diagnostics; seven cases1PASS/6FAIL in384ms,20 failed
assertions. XNB model is the passing control. CNB/CNJ/glTF each leave vertex/index
buffers and effects undisposed. Object-typed texture disposes the original but
not its later value copy; const Object/Texture shared pointers record neither.
All initial loads/operations succeed. Scope peak507011072,all memory events0.
Reports repair-content-ownership-model-formats-candidate-*; original assertions
are retained, not reclassified. These are further paths of011, not new issue IDs.

No heavy job remains; production freeze for94457/37046 is lifted. Next implement
polymorphic/const value-resource recording and record each materialized non-XNB
model child at its owning loader. Preserve partial-read retention, custom-effect
cache hits, and ordinary independent disposal. Audit generated/embedded textures
and CNB-v2 as well as the measured formats. Rebuild affected libraries and run
the unchanged25 external cases before another wider build. C model/effect policy
still awaits the owner's nonblocking answer. CNA production/helper/native tests
remain uncommitted on8d73b19c0; demos evidence/status are being checkpointed from
3f5bf7e.93FINAL/62PENDING; no knownIssue change/closure/push/merge.

The RUNNING descriptions below are superseded launch chronology; do not restart
completed exclusive-report drivers.

### Active ISSUE-011 value-resource integration (uncommitted, not closed)

Previous turn made concrete progress: registry implementation,101 passing native
focus tests, eight original failure-to-pass changes and demos evidence3f5bf7e.
Current turn implements ContentOwnershipLink in core, with intrusive live-wrapper
membership and a retained concrete representative. Copies/moves preserve the
scope; ordinary explicit wrapper disposal stays independent; the representative
keeps the real resource alive after an uncached returned wrapper dies. The
disposer retains the concrete resource type, rather than returning a type-erasing
IDisposable surrogate. Callback destruction/movement removes or relocates members
without stale pointer snapshots. Representative links are weak to avoid cycles.

Integrated into Texture2D, TextureCube, move-only SoundEffect and copyable Song.
Resource disposal state/native reference fields that an owner can invalidate
through a const handle are mutable. Existing GraphicsResource shared identity was
NOT repurposed. Direct XNB font atlas now records once; non-XNB root load routes
record results; CNB font records its atlas. C-issued 2D/cube/font wrappers detach
from native manager scope. The unique uncached C SoundEffect transfers its owner
without letting the manager cascade into C-owned instances. C model/effect policy
still awaits the nonblocking owner answer; no clone or contract change applied.

33874 TERMINAL0: actual new core helper +8 tests, strict ASan/UBSan/leak build,
all8PASS,peak341397504,events0.32375 TERMINAL0 extends this to10 tests, including
exclusive transfer/refusal; strict build3790ms,run16ms,10PASS,peak337207296,events0.
These are helper tests, NOT yet an integrated resource/sanitizer pass.

94457 RUNNING: cna-repair-ownership-values-candidate.scope,6GiB max/4GiB high,
no swap,one worker/shared cache/private display. Revalidate/poll this actual
session. Driver ../cna-demos/build-probe/repair-content-ownership-values.mjs builds
canonical CnaTests, runs Content/Texture/GraphicsResource/SoundEffect/Song/
SpriteFont/Cnb/model-reader focus, then strictly builds/runs the unchanged14
original external cases plus the four committed nested cases. Reports prefix
repair-content-ownership-values-candidate-. Freeze production and native tests
until this run is terminal; no integrated result claimed yet.

Added native tests include the original4 texture and4 nested cases, concrete
explicit-recorder ownership, resource retention after uncached wrapper destruction,
const/copy invalidation, callback deletion, C-style pixel-preserving detach,
cube release, independent C-style sound lifetime and Song-manager isolation.
Core tests10,manager tests11 remain separate. All original demos assertions are
unchanged. Demos worktree remains clean at3f5bf7e; CNA has this handoff plus the
uncommitted production/helper/tests from this coherent ownership batch. No commit,
closure, push or merge.93FINAL/62PENDING; goal active;131 owner-reserved.

Next: finish94457, inspect every new/changed test outcome and warnings. Then build
the external C API/demos against the candidate and verify independent texture/
font/sound lifetime with actual use. Resolve the C model/effect policy with the
owner response; audit non-XNB model graph/partial-read ownership and polymorphic
reference routes before claiming011 complete. Full native/renderer/visual and
integrated memory-lifetime regressions still required. Do not treat helper10PASS
or a passing18-case focused probe as full closure.

### Active ISSUE-011 registry candidate (uncommitted, not closed)

Permanent demos evidence is now committed as3f5bf7e (nested/native fixture,
exploratory C lifetime probe, triage and handoff). Demos worktree is clean.
Only the CNA production candidate/native tests/status below remain uncommitted;
no CNA repair commit, issue closure, push or merge has occurred.

LATEST:40668 TERMINAL exit1 (intentional honest aggregate of an incomplete
candidate, not a process crash). Canonical CnaTests build exits0 in709535ms;
101 native focus tests PASS in1274ms: all90 prior outcomes exactly match057's
full XML, plus11 new ownership regressions PASS. Strict external-fixture build
exits0/5761ms/zero diagnostics; original14 now11PASS/3FAIL in265ms. Exactly eight
formerly failing manager cases improve; all15 remaining texture assertion
messages are byte-identical to the prior baseline. Both original C++ fixture
files and the genuine108-check C# oracle match005899e9 byte-for-byte. Native
incremental build reports51 warnings in unchanged tests (volatile/nodiscard);
do not call it diagnostic-free. Scope peak4302471168,high2858,max0,OOM0.

5983 is also TERMINAL exit1: new four-case nested fixture builds strictly with
zero diagnostics and produces onePASS (model buffers/effect) / threeFAIL (font
atlas, loose Texture2D, moved SoundEffect and its instance), nine assertions.
All pre-unload operations succeed. Reports repair-content-ownership-nested-candidate-*.
Before that, the unchanged native C model smoke and exploratory C model lifetime
probe both compile strictly and pass against the hash-verified unchanged057
external libcna_c_api.so (9730dd4a990b011cce60659211a063a89605a997349f0d2dc778ce0ab23d1782).
Thus pre-repair C model drawing/Apply after manager unload/destroy is real baseline
behavior, not an assumed pass; intended model/effect contract remains awaiting
the owner's answer. No claim these C probes pass the new native candidate.
Nested/C scope peak539287552,all high/max/OOM0. No heavy process remains alive.

Exact next engineering step: implement ownership tracking for by-value resource
wrappers (copies/moves/destruction/manual Dispose vs manager Dispose), preserving
renderer release and SoundEffect instance cascading. Register direct XNB font
atlas and non-XNB resources; use the new four-case fixture alongside the unchanged
14 cases. Keep an actual disposable object/resource alive until manager unload,
not merely a flag or raw-address list. Ensure callbacks that destroy/move another
wrapper cannot leave stale addresses. Ordinary explicit texture disposal must
remain independent; Dispose(false) drops records without invalidating wrappers.
The C model/effect policy question is nonblocking for this native work. No final
ownership helper architecture has yet been applied. All production changes and
the native11-case test remain uncommitted; no full-suite/sanitizer/renderer-wide
pass, no marker changes, no closure.93FINAL/62PENDING; goal active.

The RUNNING descriptions below are retained launch chronology, superseded by the
terminal results above. Do not restart either driver (reports use exclusive names).

After resume/OOM inspection, both repositories were clean at CNA8d73b19c0 /
demos9e62467. The previous turn verified host memory/checkpoint only; this turn
starts production engineering. ContentManager now has ordered per-materialization
disposal records, ContentReader's default fallback, exact disposed Load/Unload
exceptions and exceptional Unload/Dispose finalization. Enumeration detects a
callback registering more assets rather than keeping invalid vector iterators.
Four production files are modified; native ContentOwnershipTests.cpp adds the ten
unchanged behavioral cases plus a callback-mutation regression. Original external
14-case sources and the genuine oracle remain untouched. This is a partial
candidate: value wrappers, direct font atlases, non-XNB roots and C boundaries
remain to be implemented/validated. Do not close011 or change knownIssue markers.

Build/test driver session40668 is RUNNING as
cna-repair-ownership-registry-candidate.scope (6GiB max,4GiB high,no swap,
one compiler/shared cache/private display). Revalidate/poll this actual handle;
do not restart from a historical RUNNING label. Driver:
../cna-demos/build-probe/repair-content-ownership-candidate.mjs.
It builds canonical CnaTests, runs ContentOwnershipCompatibilityTest plus existing
ContentReader/ContentManager tests, then compiles/runs the original14 external
cases. All exits/logs are retained, including anticipated still-failing texture
cases; it does NOT claim partial improvement is a complete green repair.
Reports prefix repair-content-ownership-registry-candidate-. Freeze the four
production files/native test until this build is terminal.

Additional demos-only ContentNestedOwnershipTests.cpp stages direct font-atlas
and model-buffer/effect checks (not compiled yet). ContentModelLifetimeProbe.c is
an exploratory C post-Unload/post-Destroy use probe, NOT an approved oracle.
The actual native C smoke source was restored unchanged. A nonblocking owner
question asks whether C model/effect resources follow native XNA Unload or need
independent ownership; only texture independence is explicit in CONTENT.md,
whereas models.h explicitly promises sharing the cached model graph. Do not
silently use shallow copies, deep clones or a changed lifetime oracle to decide
this ambiguity. Texture/font independence remains required either way.

No commit, push or closure yet;93FINAL/62PENDING. No agent/process was stopped.
Next: finish this candidate's build/focused evidence, validate added nested probes,
then continue value-resource ownership and the C boundary under the chosen
contract. No full-suite or sanitizer pass is claimed for this candidate.

### Active ISSUE-011 ownership baseline (no production repair yet)

CNA baseline31a580d2eb51794824499638609e113a95670901; demos baselineaf858e41d4c8756096fc00874913802714413c0f.
Permanent refreshed demos evidence revision9e62467d027bb99d0bc49ebe0124817b7f12bfcf.
Both worktrees/indexes were clean before this next baseline.42827 COMPLETE0:
strict current-library build5384ms,zero diagnostics; native14=3PASS/11FAIL,242ms,
normal exit1 retained. Every one of50 failed assertion messages exactly matches
the retained ten-manager/four-texture XML baselines. Original test sources and
genuine oracle byte-identical to005899e9; all frozen native/affected-source hashes
verified before/after. Peak503,635,968 bytes,zero high/max/OOM. No heavy job live.
Reports repair-content-ownership-after057-before-*; no ownership patch or closure.
93FINAL/62PENDING unchanged. The completed057 checkpoint below remains accepted.

Next exact task: implement the manager-owned disposal repair with matching native
coverage, starting from the preserved14 cases and genuine108-check reference.
Retain ordered per-materialization records (no dedup), partial-read ownership,
caller-recorder ownership, exceptional Unload/Dispose finalization and exact
disposed exceptions. Shared-pointer fallback alone cannot close011: by-value
texture copies/moves must be invalidated by the manager and release actual renderer
references while ordinary explicit wrapper disposal stays independent. C-issued
resources must retain documented independent lifetime. Audit direct XNB font-atlas,
model graph and non-XNB resource creation; preserve actual pixel C tests and add
missing nested/C-resource controls. RUNTIME_OWNERSHIP_TRIAGE.md maps these sources.
Do not adopt shared GraphicsResource identity as a blanket fix; it neither releases
all wrapper renderer references nor preserves ordinary independent disposal.
This handoff/matrix are committed together after demos9e62467; no source edits or
other uncommitted work are intended. Preserve unrelated changes if they appear.
Keep builds serial/one worker/shared cache/bounded6GiB; use private GPU runner.
No push/merge;131 stays owner-reserved,152 remains actionable. Goal active.

### Last complete checkpoint: ISSUE-057

CNA source revision83e611fe1e1196e63ccc708d2aada80e8addf512 fixes staging lifecycle
coordination, with three native regressions and public documentation in the same
commit. Demos closure revisionaf858e41d4c8756096fc00874913802714413c0f separately
commits permanent fixtures/evidence/issue history. Its parentf1140cc and6d4659d
are read-only011 integration audits, not part of the057 runtime repair.
All057 validation is complete.
Inventory155:93FINAL/62PENDING (60FIXED_VALIDATED,23DOCUMENTATION_CORRECTED,
8ALREADY_FIXED,oneDEMO_EXPECTATION_BUG,oneDUPLICATE_ROOT_CAUSE). Only057 closes;
all other154 matrix records and all historical issue descriptions are retained.

Full native60712 COMPLETE0:10807=10706PASS/101unchangedSKIP, independent XML and
source/artifact comparisons exact. Canonical build12564 and external build17134
have zero diagnostics. Focus53714:133PASS/one sameSKIP,context11/host6;
external focus12011:146 cases exact. Full external29991 COMPLETE0:all18 stages,
both719 sweeps unchanged versus047 (SW628PASS/40knownFAIL/48SKIP/three manual;
GLES667PASS/42knownFAIL/7SKIP/three manual),both95 math,six pixel-exact visuals,
CTest684PASS/48unchangedSKIP/three disabled (732 enabled,714.32s),319 hashes exact.
External peak359,165,952 bytes,zero high/max/OOM. No057 runtime marker existed;
known failures stay40/42,71 improvements versus initial baseline/73 Complete1128.

Actual-header ASan/UBSan/leak passes the unchanged1000-owner concurrent probe;
seven unprivileged permission controls and Linux15/MinGW32-Wine14 available
process controls pass. Sequential81749 COMPLETE0 adds1000 exact safe-marker-only
cleanups while preserving all original active/recent/closed checks; historical
sequential source remains unchanged,not claimed passing the new housekeeping
contract. Sequential peak168,828,928 bytes,zero high/max/OOM. Closure15171 COMPLETE0
independently verifies10807 native outcomes and352 unique files,including319
external artifacts,all fixtures/current binaries and six exact visuals;errors[].
Wine native symlink creation is unavailable(Function not implemented),not passed;
no native Windows/MSVC/macOS/NFS/SMB or all-renderer claim. No live heavy job.
Post-closure catalog/validation CTests pass3/3 in7.30s. Full historical057 body
and all155 unique issue headings remain; other154 matrix and issue records unchanged.

Exact next task: after this CNA handoff/matrix status commit, verify clean
indexes/worktrees, then run the prepared
build-probe/repair-content-ownership-after057-before.mjs in demos on the bounded
private native GPU runner. It refreshes14 unchanged ownership cases against this
checkpoint (expected3PASS/11FAIL),including exact XML failure messages. Do not
weaken them. Continue011 manager-owned lifetime at the correct layer, preserving
independent C resources and non-XNB/nested-resource routes documented in
RUNTIME_OWNERSHIP_TRIAGE.md.152 remains actionable;131 remains owner-reserved.
This handoff and matrix are committed together as the057 status checkpoint.
Production code and demos evidence are already committed; no other uncommitted
work is intended. Preserve unrelated changes if they appear. No heavy job live.
No push or merge. Goal remains active;62 historical IDs still need final states.

### Retained ISSUE-057 pre-closure chronology (superseded)

The candidate/running/uncommitted statements below record earlier checkpoints;
the completed checkpoint above is authoritative. Do not restart finished jobs.

CNA HEADfd97482067a289669699a90df60402a437a45e5f; demos HEADf1140ccdeee7da3f2a503c53ce6f303d796cf01b.
Demos6d4659d is a separate read-only ISSUE-011 integration audit in
docs/repair-evidence/content/RUNTIME_OWNERSHIP_TRIAGE.md, committed while29991
runs. It does not alter057 sources/oracles or close011. It maps ordered recording,
exceptional finalization, non-XNB acquisition routes, value-wrapper/device lifetime
and independently owned C handles. All011 production code remains unchanged.
057's candidate/baseline was prepared on demos16931a6; frozen runtime hashes are
unchanged by the separate documentation commit.
Demosf1140cc extends that same read-only011 audit: the XNB font decoder creates
its atlas directly outside nested ReadObject, Model C handles borrow graph GPU
resources, and existing pure-C ContentSmoke already verifies actual texture pixels
after unload/destruction. No011 production edits; both documentation commits are
separate from057 and leave every frozen runtime hash unchanged.
Candidate adds an empty, persistent `.cna-staging.lock` parent identity and a short
blocking OS lock around reservation initialization, scavenging and teardown only.
It does not serialize builds or remove malformed-metadata diagnostics. The entire
existing scavenger body is unchanged ignoring whitespace (audit SHA256
44f764c3e002252c570ebd1703ff3773881ff257e934a0bc272b2b001d206efa). Windows uses
synchronous LockFileEx; POSIX uses flock with only EINTR syscall resumption.
Unsafe/nonempty/multiply-linked parent markers refuse scanning/reservation;
failed destructor coordination preserves an intact abandoned tree for later cleanup.
The new persistent housekeeping identity is documented, never deleted by production.
Private test harnesses remove it only after all their own child processes exit,
and only after checking the exact empty/regular/owner/single-link/no-other-entry shape.

Native process v2 gate46615 COMPLETE0:15 checks, two independent1000-owner processes
plus scanner, parent aliases, OS release without destructors, active-old retention,
abandoned-old collection, malformed parent markers, native empty-target symlink and
hardlink refusal, exact cleanup. Wine v2 gate98626 COMPLETE0:14 available checks;
native symlink creation returns77 / Function not implemented. That Win32-specific
control is explicitly unavailable, not passed. Earlier v1 native/Wine14-check runs
remain retained; their host-created nonempty-target link did not independently
prove native Win32 symlink handling. No native Windows/MSVC/NFS/SMB/macOS claim.

Canonical build12564 COMPLETE0:12 tasks,18,585ms,zero diagnostics/errors,one worker,
shared cache. Peak4,298,215,424 bytes,high1196,max/oom0. Focus53714 COMPLETE0:
134 native=133PASS/one unchangedSKIP (StreamingFileHashAcceptsAFileLargerThanTwoGiB),
three added cases; all prior selected results unchanged. Context11/11 and real
compiler-host6/6 unchanged tests pass. Strict probe builds clean; frozen hashes
exact; private fixtures cleaned. Focus peak1,439,477,760 bytes,zero OOM/high/max.
Auxiliary54210 COMPLETE0: original unchanged1000-owner concurrent probe now has
2030 scans/zero owner errors/zero diagnostics under actual-header ASan/UBSan/leak
instrumentation. Its parent_empty=0 is precisely the one verified persistent
coordination file, not a leaked reservation. Seven permission controls pass as
non-root owner UID with zero capabilities. Peak159,485,952 bytes,zero OOM/high/max.

Full native60712 COMPLETE0:10807=10706PASS/101unchangedSKIP in1,224,981ms.
Every10804 previous outcome is unchanged plus three new PASS; independent XML and
frozen hashes match. Private display/network,owner UID,zero capabilities;
peak1,117,966,336 bytes,zero high/max/OOM events.
External build17134 COMPLETE0:10,073ms,zero diagnostics/errors,one worker,
peak1,605,029,888 bytes,zero high/max/OOM. Focus12011 COMPLETE0:146 content demos
(73 per renderer),zero status/knownIssue/check/failed-assertion differences versus047;
all36 standalone labels retained,peak208,023,552 bytes. RUNNING29991:full external
18-stage gate alone in bounded6GiB/private-display scope. Poll29991; do not launch
another heavy job until it finishes and its audit is accepted.
29991 partial checkpoint: all719 SOFTWARE outcomes accepted,628PASS/40knownFAIL/
48SKIP/three manual,651,572ms. Exact047 status/knownIssue/check/failed-assertion
comparison errors[],all36 standalone labels retained; original-baseline audit
also passes. Six visuals already pixel-exact and both95 math sweeps pass.
OPENGLES3 full719 also accepted:667PASS/42knownFAIL/7SKIP/three manual,
284,357ms,exact047 and original-baseline comparisons errors[]. Both319-artifact
hash checks after full sweeps and before CTest pass.29991 now runs final CTest
serially;18-stage summary and independent closure still pending. No057 closure.
Read-only inventory audit confirms155 unique permanent headings and matrix IDs,
92FINAL/63PENDING; only057's matrix record differs from HEAD,other154 unchanged.
Source and artifact freeze is active, including docs/content-pipeline.md.
Full driver: demos/build-probe/repair-staging-native-full.mjs. It requires the
accepted focused and auxiliary gates and independently compares native XML.
The full external regression runs against047. No knownIssue-count change is
expected for057; all719
statuses/checks/failures must remain unchanged on SOFTWARE and OPENGLES3. Follow
with math95 each,six exact visuals,all735 registered CTests and independent closure.
Do not reuse047 drivers unchanged: their hashes/baselines and private-parent
cleanup contract are now historical. Never ignore arbitrary leftover files.
Drivers: demos/build-probe/repair-staging-external-build.mjs (complete17134)
and repair-staging-demo-focused.mjs (complete12011). The former requires native10807/accepted XML,
freezes native/auxiliary sources and checks all319 previous047 external artifacts
before building. The latter compares all146 content runs exactly against047,
including unchanged knownIssue/check counts/failed assertions and36 internal labels.
Run each serially in its own <=6GiB/4GiB-high/no-swap scope on the private GPU
runner with no heavy overlap. Full external gate driver
repair-staging-full-gates.mjs is live29991; independent repair-staging-closure-audit.mjs
is prepared and syntax-checked, NOT RUN. They require accepted predecessor
gates, exact status/check/knownIssue parity with047, unchanged original probe and
scanner, explicit Wine limitations and current normalized artifact/source hashes.
The native/full and external/build audits are accepted; do not rerun them merely
because the preceding historical progress notes describe live jobs.
Read-only host measurement: Codex PID3439242 RSS320,856KiB; host22GiB available.
Native scope current477,769,728/peak1,117,966,336 bytes;MemoryHigh4GiB,
MemoryMax6GiB,MemorySwapMax0 verified. These limits do not constrain Codex itself.
No existing process stopped and no duplicate heavy job launched.
Exact live command (demos cwd): systemd-run --user --scope
--unit=cna-repair-staging-full-gates -p MemoryHigh=4G -p MemoryMax=6G
-p MemorySwapMax=0 env -u SDL_AUDIODRIVER -u SDL_VIDEODRIVER
../cna/tools/platform/run_gpu_tests_private.sh --exec env SDL_AUDIO_DRIVER=dummy
SDL_VIDEO_DRIVER=x11 WAYLAND_DISPLAY= node build-probe/repair-staging-full-gates.mjs.
After29991 COMPLETE0, inspect all18 stages, run the new sequential control below,
then run the independent closure auditor. Only after accepted closure commit the CNA helper/tests/public docs,
separately commit demos fixtures/evidence, then close057 with actual repair SHA
and update totals to93/62. Preserve all155 issue histories and other154 matrix
records. Next ordinary work includes011/152;131 remains owner-reserved. No push.
Prepared and syntax-checked, NOT RUN: demos/build-probe/repair-content-ownership-after057-before.mjs.
After057 closure/commit/status reconciliation and no other heavy job, run it on
the private native GPU runner in a <=6GiB/no-swap scope. It refuses absent057
closure or repair SHA, compiles the two unchanged permanent ownership test sources
against current canonical native libraries using one worker/shared cache, then
compares all14 cases and exact XML failure messages with the retained ten-manager/
four-texture baselines (expected3PASS/11FAIL). Original fixtures and XNA oracle must
match demos005899e9; current native frozen hashes are checked before/after.
This is a prepared failing-baseline refresh, not new runtime evidence or closure.

Additional057 closure requirement (prepared, NOT RUN): permanent
docs/repair-evidence/content/ContentStagingSequentialLifecycleProbe.cpp retains
the original1000-owner active/recent/closed-scan controls, replacing the obsolete
empty-parent check with exact single empty regular singly-linked owned coordination
file checks after EVERY lifecycle. The historical ContentStagingSequentialControl.cpp
remains byte-identical; do not claim it passes the new housekeeping contract.
Driver demos/build-probe/repair-staging-sequential.mjs requires accepted18-stage
external gate, builds the actual-header new control with strict Clang/ASan/UBSan/
leak checks, verifies1000 exact cleanups and only removes its exact owned marker
after the synchronous fixture exits. Run serially after29991 as non-root owner
with CapEff0 in <=6GiB/no-swap scope (no GPU needed), using existing build-probe.
The syntax-checked independent closure auditor now REQUIRES this sequential audit
and hashes the new fixture/binary plus unchanged original control. No production
or already-frozen fixture changed;29991 remains the sole live heavy job.

Uncommitted CNA: tools/common/CnaContentStaging.hpp, three regression cases in
modules/content/tests/CNA/Content/Pipeline/ContentPipelineCliTests.cpp, public
docs/content-pipeline.md, this handoff/status evidence. New demos fixtures:
ContentStagingProcessProbe.cpp, staging_process_tests.py, staging_permission_tests.py
and ContentStagingSequentialLifecycleProbe.cpp in docs/repair-evidence/content.
Original concurrent/sequential probes untouched.
Reports prefix repair-staging-lifecycle-*; exact gate suffixes are -native-v2-audit,
-win32-wine-v2-audit, -native-canonical-build-audit, -focused-audit, -auxiliary-audit,
plus -native-full and -external-build-audit. No057 closure or source commit;92FINAL/63PENDING.
No push or merge. Keep the full campaign goal active.

### Last complete checkpoint: ISSUE-047

Latest completed gate (2026-10-21): external63473 COMPLETE0, all18 stages.
CTest684PASS/48unchangedSKIP/three previously disabled,763.03s; both719 renderer
sweeps,math95 each,six pixel-exact references and all comparisons accepted.
External scope peak1,132,859,392 bytes,zero high/max/OOM events. Independent
closure59379 COMPLETE0:10804 native outcomes independently rechecked,341 current
file hashes verified (including319 external artifacts),six exact visuals,errors[].
Report: demos/build/linux-full/reports/repair-generated-output-closure-audit.json.
No heavy job remains live. ISSUE-047 is RESOLVED/FIXED_VALIDATED by CNA source
revision a56383319578256459c95ff990fd6b7826fa2226 (branch next), with parent fixes
23a34f986219d8ad61c24f31ce902b0ceeacf5cb (old-cache reporting) and
32cb1a4ed31b7c0c5a1b58c4341faf0df481e37c (atomic-copy source-handle lifetime).
Demos closure revision is1abd9ec6b14e44f6e2e77739e90a926b8dd4aec2 (branch next);
the preceding057 design-only note iscd07666. Permanent issue history and four earlier matrix
validation entries are retained. Inventory155:92FINAL/63PENDING, comprising
59FIXED_VALIDATED/23DOCUMENTATION_CORRECTED/8ALREADY_FIXED/oneDEMO_EXPECTATION_BUG/
oneDUPLICATE_ROOT_CAUSE. Only047's knownIssue/expected failure is removed; one
new PASS on each renderer,71 improvements versus the original campaign baseline.
Remaining known failures40SOFTWARE/42OPENGLES3 are not hidden or closed.
Validation platforms: Linux native, SOFTWARE/OPENGLES3 runtime; actual atomic-copy
helper additionally MinGW32/Wine and ASan/UBSan/leak detection. No native Windows,
MSVC, unavailable hardware/services or all-renderer coverage claim. No new push.

Next exact task: ISSUE-057 staging lifecycle. The refreshed unchanged concurrent/
sequential probes now confirm the defect on this checkpoint; prove a cross-process and
cross-thread lifecycle synchronization repair without suppressing malformed-state,
ownership, symlink, lease, age or bounded-scan diagnostics. See demos
docs/repair-evidence/content/CONTENT_STAGING_TRIAGE.md; its read-only design note
is a separate commit, not part of047. No staging runtime edit has been made.
ISSUE-011 ownership and ISSUE-152 FileShare remain ordinary actionable work;
ISSUE-131 remains owner-reserved. Goal unchanged and active.
Post-closure catalog CTests pass3/3 (10.56s). Audit confirms all155 unique IDs,
unchanged complete047 historical body and unchanged other154 matrix records.
This metadata checkpoint commits the handoff/matrix after the separate demos
closure. Demos worktree is clean; no uncommitted CNA production changes remain.
Preserve unrelated changes if they appear. No push or merge was performed.

057 refresh66458 COMPLETE0: strict Clang builds, unchanged permanent probes,
1000 concurrent owners/5640 scans;3093 missing-owner warnings plus64 unreadable,
22 incomplete,39 invalid-directory diagnostics,zero owner errors/removals. Normal
concurrent exit1 retained; sequential1000 exits0 with zero diagnostics/errors.
Private parents end empty and are removed non-recursively. Source hashes exact;
scope peak141,725,696 bytes,zero high/max/OOM. Report prefix in demos:
repair-content-staging-after047-before; baseline CNAee92c1eaa/demos1abd9ec.
No057 production/test-oracle edit or live heavy job. Continue with lifecycle
synchronization and independent-process/thread safety regressions; do not rerun
this identical baseline or treat the warning as harmless. A persistent parent
coordination file, if selected, requires explicit exact housekeeping-file checks
and documentation, not arbitrary leftover suppression. Inventory remains92/63.

### Retained pre-closure validation chronology

The following running/candidate statements describe earlier checkpoints, not live
jobs or current issue status; the completed checkpoint above supersedes them.

Cache-policy build61683 is COMPLETE0 (configure0/build0,6,024,361ms), shared
ccache base `/rv`, one compiler worker, four resulting binary hashes recorded,
three ISSUE-109 source hashes unchanged, audit errors empty. Final warning audit:
138 diagnostics,122 pre-campaign owned-test call sites,16 vendor,zero errors or
newer owned call sites; not a warning-free build. Reports use
repair-model-encoder-native-cache and ...-warning-provenance-final prefixes.

Final native gate5396 COMPLETE0: focused54/54; full10798 gives10697PASS and
101unchangedSKIP, zero failures in1,148,296ms. Every10791 predecessor outcome is
unchanged, plus seven new passes. Independent XML comparison agrees. The scope
peaked at2,598,830,080 bytes, with zero high/max/OOM events. Private display,
owner UID/zero capabilities and limited-broadcast route were actually verified.
External18-stage checkpoint remains complete. Final independent closure audit
58861 COMPLETE0 verifies330 current file hashes, including319 external artifacts,
all four native binaries and the four instrumented sanitizer translation units.
Actual encoder/decoder ASan/UBSan/leak coverage is61/61 PASS; dependencies were
not fully instrumented. No earlier failed gate is deleted or reclassified.

ISSUE-109 is RESOLVED by68e0acb67732efdb8a0b737ab6f43181444dee65; matrix and permanent
issue now retain its history and record FIXED_VALIDATED. Totals91FINAL/64PENDING.
No knownIssue/expected-failure count change: this was a passing diagnostic demo,
whose assertions now require encoder rejection. Source/test repair is committed.
Closure metadata committed in CNA33dad9dc9 and demos90b9ba3; post-closure catalog
CTests pass3/3. No push or merge.

047 real-host baseline92605 COMPLETE0: strict warning-free fixture build, five
expected root-authorization failures and one passing invalid-input control.
The Python test exit remains1; the audit accepts only that exact reproduced
baseline and verifies unchanged canonical binaries/109 sources. Initial51013
audit failure retained: it mistook the deliberately retained empty shared staging
root for a leaked reservation. Corrected auditor removes only the owned, empty,
non-symlink root; all other leftover content still fails. No test/assertion change.
Both private temporary parents are cleaned. Validated047 fixtures are committed
in demos3b18b54; corresponding matrix checkpoint is CNA91490f2f1.

047 candidate is UNCOMMITTED and requires full regression: five production/header files
authorize configured-output files without fake inputs, preserve XNA nested output
paths and persist/fingerprint generatedOutput in manifest version10. Input-root
capabilities and authored-output guards remain. Two existing native test files
add four cases; docs/content-pipeline.md describes the new provenance and safe
incompatible-manifest rebuild. No assertion or knownIssue marker is removed.
Initial build20174 COMPLETE1 after102/687 tasks: cna-content safely rebuilt its
old cache, but BuildContent then parsed the saved old manifest and reported an
unsupported version. The original log/audit remains; no cache was deleted to
hide it. Additional local fix in BuildContent.cpp keeps current-manifest parsing
strict but warns/uses empty previous state for unreadable old caches. A fifth
new native regression in XnaBuildTaskTests.cpp covers old/corrupt/future caches,
accurate rebuilt outputs, intact obsolete files and a subsequent no-op build.
Recovery build52660 COMPLETE0, two warnings identical to the preceding full build,
zero OOM. Focus56969 COMPLETE1: native115=113PASS/one unchangedSKIP/oneFAIL; all five
new cases pass. The sole failure was a literal manifest-version9 expectation in
RoundTripsEveryStableFieldDeterministically. It now explicitly expects10 and the
generatedOutput:false field; EarlierVersions adds9 to its rejection list.
Unchanged context fixture11/11 and unchanged real-host fixture6/6 PASS, strict
probe builds clean, frozen hashes exact. First failing focus remains retained.

Publication portability probe23944 confirms a second integration defect: actual
CnaToolAtomicWrite.hpp passes5/5 on Linux but MinGW32/Wine self-copy gives three
Access denied failures (different destination and no-temp-leak controls pass).
The reader survived into destination replacement. Local helper fix closes/checks
the reader before publication; unchanged probe56854 now PASS5/5 on both platforms,
strict clean builds, no leaks/OOM. This is Wine/MinGW coverage, not native Windows
or MSVC validation. A sixth native regression covers same-file/lexical-alias copy.

Build76628 COMPLETE0:20 tasks,33,254ms,zero diagnostics/errors. Final focused
gate13383 COMPLETE0:131 native cases=130PASS/one unchangedSKIP, all six additions
pass, all previous selected outcomes unchanged. Unchanged context11/11 and real
compiler-host6/6 PASS; strict fixture builds clean, frozen hashes exact. Scope
peak1,212,870,656 bytes,zero high/max/OOM events; private temporary parents cleaned.
Reports: repair-generated-output-native-final-build-audit.json and
repair-generated-output-final-focused-audit.json in demos/build/linux-full/reports.

External build94535 COMPLETE0:347 tasks,609,391ms,one expected BuildContent warning
about safely rebuilt incompatible previous manifest; no compiler errors/audit
errors. Existing build/linux-full/shared cache,one worker. Peak4,299,345,920 bytes,
high2539,max/oom/oom_kill0: the4GiB soft limit applied,6GiB hard limit not reached.
Source/native-artifact/fixture/demo hashes unchanged. Report:
repair-generated-output-external-initial-build-audit.json.
Initial external focus76950 COMPLETE1 after16 SOFTWARE cases:15PASS/oneFAIL in
content.custom.build_manifest, whose explicit version9 assertion is stale after
the deliberate manifest-v10 change. Original audit/logs retained. The demo now
explicitly pins10 and generatedOutput:false, preserves the previous version8
rejection and also checks9; all14 checks and other assertions retained. This
additional demos source edit is uncommitted; no047 marker has been removed.
Native source/artifact hashes remain unchanged. Sanitizer57508 COMPLETE0: actual
atomic-copy helper and identical fixture5/5 PASS under ASan/UBSan/leak detection,
strict clean build,source hashes exact,private fixtures cleaned,peak272,592,896
bytes,zero OOM. Report repair-atomic-deployment-copy-sanitizer-audit.json.
External format-v10 build94348 COMPLETE0:11,205ms,zero diagnostics/errors,
peak2,669,649,920 bytes,zero high/max/OOM; frozen hashes exact. Format-v10 focus
85104 COMPLETE0:146 cases, each renderer70PASS/three unchanged knownFAIL, only047
improves; manifest demo retains14 checks. Separate label comparison confirms
all36 internal standalone checks unchanged on both renderers. Peak2,929,029,120
bytes,zero high/max/OOM. The047 marker and stale048 restriction have now been
removed in uncommitted metadata only; runtime standalone source is unchanged.
Metadata build6798 COMPLETE0:8,742ms,zero diagnostics/errors,peak3,544,891,392,
zero high/max/OOM. Metadata focus96907 COMPLETE0:146 cases,only047 improves,
all36 internal labels explicitly preserved per renderer; frozen hashes exact.
Peak4,295,487,488 bytes,high1450,max/oom/oom_kill0. Original failures retained.
Native56713 COMPLETE0:10804=10703PASS/101unchangedSKIP,1,281,742ms. Independent
XML comparison preserves every10798 predecessor result plus six new passes;
all frozen native/source hashes exact. Private Xwayland/owner UID1000/all
capabilities zero/private broadcast route verified. Peak2,546,384,896 bytes,
zero high/max/OOM. Reports repair-generated-output-native-full-audit.json/log/XML.
Current63473 runs full external gates alone on private Xwayland,<=6GiB scope.
The first fourteen stages pass: frozen hashes, both math95 sweeps, catalog,three
automation-tool suites,six exact visuals and both full719 renderer sweeps.
SOFTWARE628PASS/40knownFAIL/48SKIP/3manual; OPENGLES3667PASS/42knownFAIL/7SKIP/3manual.
Only047 improves against109; all36 internal standalone labels and unrelated
statuses/checks/failed assertions remain unchanged. Both original-baseline audits
find71 improvements and zero unexpected differences. CTests are running (420/735
started at the latest observation); no second heavy campaign job was launched.
Native/external sources and319 artifacts remain frozen. Poll63473; inspect
repair-generated-output-final-gates-summary.json and all comparison/CTest audits.
It requires metadata-focus hashes,36 internal labels,sanitizer5/5 hashes and
native XML audit, then compares every unrelated status/check/failed assertion
against109 and the original baseline. No new commit/issue closure yet.
Prepared repair-generated-output-closure-audit.mjs is syntax-checked,NOT RUN;
run it only after63473 completes. It independently rechecks native XML, all
retained before/after fixtures, renderer/math/CTest/visual results and hashes.
Only then create coherent code/test commits (atomic copy, old-cache reporting,
generated-output provenance), update permanent047 disposition/matrix/handoffs,
and commit demos evidence/metadata separately. Inventory155 unique IDs remains
91final/64pending until that closure. Last observed resumed Codex309,424KiB RSS.
Additional uncommitted demos change: CONTENT_STAGING_TRIAGE.md contains only
read-only057 synchronization constraints with primary OS references. No staging
implementation/oracle change or new runtime pass. Keep this separate from the047
source/closure commit. It explains why process-local locking, missing-metadata
suppression, and unverified directory-only locks are insufficient alternatives.
Full native expectation10804=10703PASS/101unchangedSKIP; full external regression
still required. AtomicDeploymentCopy.cpp is an untracked validated demos fixture.
No047 closure; totals91FINAL/64PENDING. No other campaign heavy job is running.
Latest host observation: resumed Codex PID3439242 RSS302,004KiB,22GiB available;
no process stopped. Workload scopes do not limit the Codex application itself.
Resume verification:63473 is still live; host Codex PID3439242 RSS309,172KiB,
22GiB available. The external scope has peaked at1,121,337,344 bytes so far,
zero high/max/OOM events. No duplicate build/test job or process termination.
Read-only path review rejects the hypothesized containment/relative-path mismatch:
ValidateContainedNativePath itself computes weakly_canonical plus lexically_relative,
identically to the new bridge branch. It does not perform a separate case-insensitive
acceptance that could admit an escaping lexical result. No new Windows runtime
claim or source change follows from this review; general filesystem case policy
remains outside this narrow generated-output repair.
Follow-up observation during94535: Codex302,760KiB; build current1,135,243,264 bytes,
peak2,802,225,152, MemoryHigh4GiB/Max6GiB/Swap0 verified; host22GiB available.

Continuation drivers in demos/build-probe (completed/running results above):
- repair-generated-output-demo-focused.mjs: format-v10 completed146 content cases
  against109,only047 becomes PASS. Final metadata stage also completed successfully.
- repair-atomic-deployment-copy-sanitizer.mjs: actual helper plus identical five
  probe cases, ASan/UBSan/leak detection; Linux5/5 complete in its own bounded scope.
- The047 knownIssue and stale048 restriction are removed locally after successful
  format-v10 focus;048 already fixed by3a1437725. Metadata build passed; its final
  focus passed. No issue closure/commit before full regression.
- repair-generated-output-native-full.mjs: full native10804, exact predecessor
  outcomes plus six additions, independent XML comparison. Requires private GPU
  runner and --map-current-user network namespace, cna-test0 dummy192.0.2.1/24,
  explicit limited-broadcast255.255.255.255 route, then drop all capabilities.
- repair-generated-output-full-gates.mjs requires accepted metadata focus and
  full native gate; freezes319 external artifacts plus native/source evidence,
  runs math95 each, six visuals, both719 sweeps,735 CTests, and exact previous/
  original comparisons (only one new improvement,71 since campaign baseline).
All heavy stages SERIAL in separate bounded scopes; never overlap63473. Use the
private demos display environment documented in the109 checkpoint. No timeout,
retry or behavior assertion has been weakened. Format-version pin and047/048
metadata changed as recorded. Native full passed; external full is running,
not a completed checkpoint. CNA remains91490f2f1 and
demos3b18b54; no new commit/push.

### Earlier checkpoint (superseded for live jobs and next actions)

Native86497 COMPLETE1:10798=10688PASS/105SKIP/fiveFAIL. All outcomes retained.
Root capabilities caused one permission failure/four extra skips; loopback-only
private network lacked limited-broadcast routing, causing four two-process
discovery/migration failures. Corrected unchanged-binary control83742 PASS26/26:
13 discovery+eight two-process+five permission cases; four binaries/three source
hashes exact. No code/assertion/timeout changes. Both surviving full jobs ended.
Cache-policy build61683 now RUNNING alone,one compiler worker,existing native
tree,verified systemd scope cna-repair-model-cache-policy MemoryHigh4GiB/Max6GiB/
Swap0. Wait for its audit, then final full usernet gate with private display,
owner UID/all capabilities zero,cna-test0 dummy192.0.2.1/24 and explicit limited
broadcast255.255.255.255 route. Driver now checks route and cache-rebuilt hashes.
Three109 source/test files remain uncommitted;90/65 unchanged. Scope limits apply
to new workloads,not the Codex application. See MODEL_ENCODER_VALIDATION.md.

047 next-host fixture preparation is uncommitted in demos:
docs/repair-evidence/content/GeneratedOutputCompiler.cpp plus
generated_output_cli_tests.py; six Python cases parse, C++/runtime NOT RUN.
Nested-output ownership is supported by genuine XNA static IL (eight methods,
hashes in GENERATED_OUTPUT_TRIAGE.md), not a runtime pass. CNA's unconditional
basename mapping must not lose the output identity after root authorization.
Use existing custom-compiler target flags/link libraries after current serial
work; see GENERATED_OUTPUT_TRIAGE.md. Do not conflate prepared assertions with
the measured11-case context/manifest baseline. No047 CNA source changes.
Prepared demos build-probe/repair-content-generated-output-host-before.mjs passes
syntax check only; requires completed cache rebuild and hashes canonical artifacts.
Ongoing61683 retains vendor/test diagnostics. Provenance snapshot86:70 owned-test
call sites predating688f3f23e,16 vendor,zero errors/newer owned sites. See demos
repair-model-encoder-native-cache-warning-provenance-interim.json. Re-run the
read-only build-probe/repair-native-warning-provenance.mjs with a fresh final
report prefix after build completion; this does not change flags/test policy.
Preserve the exact final diagnostics in the audit; no vendor
edit and no warning-free broad-build claim. Native model sources compiled.
Prepared final-native/047-host drivers additionally inspect their actual cgroup
memory.max<=6GiB and swap.max0 before any workload. Guard-only checks PASS2 bounded
acceptances/2 unbounded refusals; no compiler or CNA suite launched. Use a new
bounded systemd scope for the final private-display/unprivileged-broadcast gate.
The final-native driver also explicitly exports shared ccache `/rv/cnaccache`,
base `/rv`, and CMAKE_BUILD_PARALLEL_LEVEL/CTEST_PARALLEL_LEVEL=1 to test children.
It stores complete error lists in audit JSON while bounding console output to
counts/first ten errors plus the full audit path. Expected outcomes are unchanged;
driver syntax passes, final native workload is still NOT RUN.
It additionally records start/end memory counters per stage and refuses a new
OOM-killed child. Guard/snapshot-only bounded-scope check passed (no CNA workload).

The completed external checkpoint remains valid: job8261 exited0; all18 stages,
CTest684PASS/48unchangedSKIP/three disabled, both719-demo renderer sweeps,
95 math demos per renderer, six exact visual references and146 focused checks.
The independent audit has no errors and verifies319 artifacts/three sources.
Only61683 is the current heavy campaign job. Do not follow obsolete launch or
hash-adaptation instructions in the historical recovery notes below.

### Historical recovery notes (superseded by the current checkpoint above)

The following notes preserve observations at earlier times. Statements that
86497 or8261 were live, or that the prepared usernet driver still expected old
binary hashes, are historical and are not instructions for the next run.

External8261 COMPLETE0:18 stages,CTest684PASS/48sameSKIP/three disabled964.43s;
735 outcomes exact against XML,319 frozen artifacts/three sources exact.
Both719/math95/six zero-pixel visuals/146 focus complete; independent
repair-model-encoder-external-checkpoint-audit.json errors[]. External freeze
released. Native86497 still live,already known root-capability permission failure;
do not start new heavy work until it ends. Next correct ONLY native cached ccache
base/launchers to /rv (external cache already correct),rebuild one-worker,
record resulting binary hashes,then full unprivileged usernet gate10798.
Prepared usernet driver expects old binary hashes: adapt only after validated
cache rebuild,keep exact source and outcome assertions. Three109 source/test
edits still uncommitted;90/65,no closure. Post-resume memory~16GiB available,
Codex~373MiB RSS at latest check;no process stopped/restarted.

OOM recovery at19:59 local: kernel killed Codex PID12082 with8,349,408KiB anonymous
RSS (global OOM; cna-street requested the allocation). This identifies the victim
and triggering allocation, not the entire causal memory breakdown. Resumed Codex
PID3439242 initially~543MiB RSS; host~16GiB available of30GiB,no swap.
Both original handles86497(native)/8261(external) re-polled LIVE after resume;
native PID3401964 remains in genuine XNA differential build; CTest PID3401360
progressed past551/735. No job restarted or stopped. Do not start new heavy jobs
until these surviving jobs finish; subsequently run new heavy jobs serially,
use one compiler worker, read bounded log summaries and check host memory before
launch. Both artifact/source freezes still hold. Three109 source/test changes
remain uncommitted; do not mark109 fixed. Preserve the known root-capability
failure in86497 and use the corrected unprivileged usernet gate after it ends.

86497 verified live but already fails one publication permission test: its
map-root-user namespace retains capabilities that bypass the fixture's removed
write permission. Do not accept this as a green gate. Corrected isolation46590
preserves UID/GID1000 and drops all capability sets AFTER private loopback setup;
unchanged native binary passes original permission test+13 discovery cases14/14.
After86497 actually terminates, run repair-model-encoder-usernet-native-gates.mjs
under --map-current-user --keep-caps, then setpriv --bounding-set=-all
--inh-caps=-all --ambient-caps=-all, plus usual private graphics display.
New driver asserts owner UID/zero capabilities, exact four binary/three source
hashes and all10798 outcomes. Do not overlap native full runs sharing XNA build
directories; no process was stopped.8261 finalCTest still live; both freezes hold.
047 extended baseline52851 COMPLETE0(test expected1):11 cases=8PASS/3FAIL,
including independent manifest rejection of the legitimate output-root report.
Input fingerprint/security controls pass; initial strict aggregate-init failure
retained then corrected warning-free build. See GENERATED_OUTPUT_TRIAGE.md.
Only the three109 source/test edits remain in CNA;90/65 unchanged.

109 extended sanitizer52903 COMPLETE0:61/61 PASS, actual encoder+decoder and both
test TUs instrumented, zero warnings/findings; four frozen native artifacts/three
source hashes verified. Full native86497/external8261 remain running. No109 closure.

109 corrected native build61320 COMPLETE0,zero warnings. New gate86497 runs full
10798 on private display AND private Linux network namespace after focused54/54
passed; freeze four native binaries/three sources incl.CnbHardeningTests.cpp.
Actual encoder+decoder/both test TU sanitizer52903 RUNNING(one compiler).
External8261 both719 sweeps exact SW627/41/48/3,GLES666/43/7/3 with original/local
audits errors[]; serial finalCTest RUNNING,319-artifact/three-source freeze intact.
Do not rebuild either frozen tree. Initial failed95074/compile12167 logs retained;
no109 runtime commit/closure yet.90/65. Native cache inspection found the existing
CMake compiler launcher overrides exported CCACHE_BASEDIR with the repository
parent; before NEXT build (after freezes), explicitly configure CNA_CCACHE_BASEDIR
and both C/CXX compiler launchers to /rv. Shared CCACHE_DIR=/rv/cnaccache is used.
Do not claim the old cached launcher used /rv merely from the shell environment.

109 first native95074 COMPLETE1:10798=10690PASS/101sameSKIP/sevenFAIL;four native
binary/two source hashes exact. One decoder fixture relied on encoder accepting
invalid data; six61190 discovery failures overlap external net (wrong host port).
Unchanged binary discovery13/13 passes in private network namespace74752.
Both16/32-bit decoder fixtures now corrupt valid encoded chunks and separately
check encoder refusal, preserving exact decoder diagnostics/high-byte coverage.
Third uncommitted CNA source:CnbHardeningTests.cpp. Corrected native build61320
RUNNING after initial retained missing-Parse-filename error12167. Next full
10798 gate must use private network plus private display. External8261 continues
SW719627/41/48/3 exact original/local audits; GLES/CTest pending. External319/three
source freeze unchanged. No109 closure;90/65. See MODEL_ENCODER_VALIDATION.md.

047 isolated native baseline79356 COMPLETE0 (test process expected1):two
legitimate separate-output registrations FAIL,five source/security controls PASS.
Strict warning-free build;four frozen native binaries/two109 sources exact.
Private fixtures removed. See demos GENERATED_OUTPUT_TRIAGE.md and permanent
ContentGeneratedOutputTests.cpp. Matrix047 now CONFIRMED_OPEN; no production
change. A correct repair must carry generated-output provenance through manifest,
hash/cache/publication/clean, not only widen AddDeploymentFile containment.
109 gates95074/8261 verified live; native has passed the genuine XNA differential
stage and is in avatar service tests, external SOFTWARE sweep continues.
Only two CNA109 source/test edits remain; both freezes active.90/65 unchanged.

109 demo/evidence committed86e7a89 after both initial and metadata146 controls
pass exactly (69PASS/four sameFAIL per renderer,model29/29). Canonical metadata
build is clean; full8261 has completed both95 math/six exact visual references
and is running whole719 sweeps. Native95074 remains live in genuine XNA compile
comparison. Both artifact/source freezes are active. Only two CNA109 runtime/test
files remain uncommitted; no demo source edits pending.90FINAL/65PENDING unchanged.

109 next-gate state:95074 full native RUNNING (canonical35 focus passed);8261
post-metadata146 focus then full both719/math/visual/CTest RUNNING. Native542/
external229 tasks and metadata rebuild pass without warnings. Initial146 exact
69PASS/four knownFAIL per renderer; model29/29. Exact source mapping preserves
all29 controls/five malformed inputs, changing only the one diagnostic oracle
to exact encoder refusal.109 restriction sentence removed after canonical passes.
Four native binaries/two source files frozen by95074; external full gate will
freeze319 artifacts plus three sources. No further builds/code edits until gates
finish. Only109 runtime/test and corresponding demo/evidence edits are pending;
closure039/043/045/103/148 already committed CNAfe59ea861/demosd566696.90/65.

XML/content final74592 COMPLETE0:CTest684PASS/48 unchangedSKIP/three disabled,
887.44s;735 registered/732 enabled and319 frozen hashes exact. Both719 sweeps,
both95 math,six zero-pixel references,native10791 and Sharp18158 gates complete.
039/043/045/103/148 now FIXED_VALIDATED:90FINAL/65PENDING of155. All150 unrelated
historical bodies unchanged and five original resolved bodies preserved.
Evidence closure committed demos d566696; see XML_CONTENT_FINAL_CHECKPOINT.md.
Previous external freeze released.
New109 canonical build88321 COMPLETE0,542 printed tasks,zero warnings/source
changes. Canonical35 codec cases pass; full10798 native95074 RUNNING with four
binaries and two source files frozen. The uncommitted109 production/test changes
are NOT covered by the completed XML checkpoint. Next:commit closure evidence
separately,correct109 diagnostic demo only to stronger exact encoder-refusal
oracle,build external against109,then focused and full regressions. No push.

New coherent content batch109: uncommitted CnbModelCodec.cpp and native test file.
Seven additions reproduce seven failures while all28 previous codec cases pass;
actual-TU candidate passes35/35 normally and ASan/UBSan/leak, strict warning-free
builds and exact before/after comparison. Decoder/schema/valid write path unchanged.
Canonical native build88321 RUNNING(two workers); external XML/C-root finalCTest
74592 RUNNING(one),still319 frozen external artifacts. The external RUNPATH uses
its own build/linux-full/CNA/libcna.so, not the rebuilding native shared library.
No external rebuild or109 demo edit yet. MODEL_ENCODER_VALIDATION.md records proof
and next gates. Current counts85/70; no closure or push. Earlier native XML10791
gate is complete and its native freeze was released before this new build.

Current CNA dbfe6b4c1 includes validated strict XML corpus/docs. Both external719
sweeps complete:SW627/41/48/3,GLES666/43/7/3; original70-change and localtwo-change
audits errors[],319 hashes verified before serial finalCTest.74592 is now running
CTest; freeze319 continues. Native37179 COMPLETE0 as below; no issue closure yet.
Next final closure audit must include039/043/045/103/148, not just the latestthree.
057 is newly CONFIRMED_OPEN from current-header fixed1000-owner concurrent probe:
3241 missing-owner warnings,zero owner errors/removals,paired sequential1000 clean.
Clang strict builds pass; initial GCC strict optional-handle warning retained.
See demos CONTENT_STAGING_TRIAGE.md. No057 or011 production change. No new push.

Native final37179 COMPLETE0:10791 cases=10690PASS/101 unchangedSKIP,1647.295s;
exact case-by-case comparison with the frustum publication baseline errors[],
all three frozen native hashes unchanged. Strict corpus9/9 also passes with
original goldens/diagnostic comparisons intact and both PI/BOM exemptions removed.
Native artifact freeze released. External74592 remains RUNNING on319 frozen
artifacts; SOFTWARE719 exact627/41/48/3, OPENGLES3 and finalCTest still pending.
Commit the native corpus/docs now; do not close any ID on this partial final gate.
Audit also identified043/045 still legitimately pending the same original C-root
conflict; include their existing focused/native/sanitizer evidence in final closure
review alongside039/103/148. Counts85FINAL/70PENDING unchanged. No new push.

Latest verified continuation: full SOFTWARE719 completes627PASS/41FAIL/48SKIP/
three manual; both original-baseline and predecessor audits pass with no residual
C-root discrepancy. Exactly two improvements since016,70 since initial baseline.
OPENGLES3/CTest74592 and native37179 still running; no artifact freeze released.
Independent ownership probe62694 COMPLETE0 (test process expected1):four actual
Texture2D cases give onePASS/threeFAIL,15 precise missing-disposal assertions.
All pixel/copy/move/independent-manager/Dispose(false) controls pass; strict build
and three native frozen hashes verified. Permanent regression/evidence committed
in demos005899e. No011 production change; counts85/70 remain unchanged.

Active final checkpoints:37179 native full suite (in genuine XNA differential
build comparison),74592 external full both719/math/visual/CTest. FREEZE native
three test binaries and external319 artifacts until these jobs finish. No builds
currently running. External27083 COMPLETE0(476 printed/479 steps),no compiler
warnings; marker build68007 COMPLETE0(9/10 steps). Canonical focused21825 and
post-marker82771 both COMPLETE0:each renderer XML36/36 exact retained labels,
C ContentManager67/67 exact approved candidate labels,C++ cache5/5. Only103/148
knownIssue/restriction metadata removed; source assertions unchanged.
Prepared exact original-baseline comparator expects70 demo status improvements
(two more than016),with no C-root residual allowed. Local comparator permits
only these two FAIL->PASS transitions; all other status/marker/check/failure
multisets must remain identical. Do not close039/103/148 before full audit.
Current source changes: CNA corpus exemptions/docs plus handoff/matrix; demos
two metadata files,HANDOFF,XML_SUBSTRATE.md. Sharp97713eb2 clean/unpushed.
Read-only152 review confirms fstream cannot enforce requested sharing before
truncating; a same-native-handle lower-layer design is required. No152 code edit.

Current active jobs:37179 native CNA full suite on private display;27083 canonical
demos build (two compilation workers). No other active campaign build/test jobs.
Sharp commits d326d37b test scheduling,16993ac6 PI parser,97713eb2 character BOM
are local/unpushed; Sharp worktree clean. Whole dependency gate18158/41 executables
passes zeroFAIL/SKIP;574XML/352XML.Linq,actual Reader/Document ASan/UBSan/leak5/5,
parser4/4 sanitizer,44modules/109edges boundary check,zero build warnings.
Native CNA build39216 COMPLETE0,108 printed/111 tasks; both corpus exemptions
removed in uncommitted test/docs candidate. Fresh genuine corpus9/9 passes with
goldens and exact diagnostic comparisons unchanged. Full10791 outcomes pending.
Current CNA revision92625c32b,demos6f307f4,Sharp97713eb2. Counts85FINAL/70PENDING.
Next:finish native37179 and external27083; actual148/C-root focused runs; only
then remove passing markers,commit corpus/evidence and full both719/CTest/visual
checkpoint. Do not rebuild frozen native CnaTests/CnaContentTests/PipelineTests
during37179. PI/BOM permanent evidence:demos XML_SUBSTRATE.md (uncommitted).
Other malformed-XML eager factory timing remains explicitly documented, not
claimed fully streaming-compatible. No process stopped for memory; no new push.

016 evidence/demo closure committed6f307f4;155 historical bodies audited intact
(154 unchanged,016 original body retained under resolution note). Counts85/70.
Sharp full gate39469 COMPLETE0:18151PASS across41 executables,zeroFAIL/zeroSKIP,
unchanged original Yacht service on private loopback fixture. No service fake.
Next separate Sharp commits for Diagnostics test synchronization and148 PI parser;
148 remains open for character-BOM/timing and downstream canonical validation.

ISSUE-016 now FIXED_VALIDATED:85FINAL/70PENDING of155. Permanent evidence and
historical inventory updated; both demo-local workarounds removed, assertions
unchanged. Full38847 local comparisons/CTest complete as recorded below; the
original C-root residual remains visible and is not reclassified as a pass.
Sharp full41-component gate39469 is RUNNING after test-only four-reader latch
repair; previous failure archived as repair-xml-pi-failed-components.log.
Focused569 XML/350 XML.Linq/whole Diagnostics and100 fixed stress iterations pass.
Next:commit016 closure separately; finish Sharp gate,commit independent test/parser
repairs,then complete148 character-BOM/timing and canonical C-root gates. No push.

Latest verified continuation:016 full driver38847 is COMPLETE(exit1 solely the
two retained original C RootDirectory audits). Both719 sweeps exactly match the
preceding roots checkpoint: SOFTWARE625/43/48/3,OPENGLES3 664/45/7/3; local errors[].
Serial CTest completed684PASS/48SKIP/three disabled,735 inventory/732 enabled,
990.96s,all319 frozen artifact hashes verified. Final summary and CTest audit:
demos build/linux-full/reports/repair-mixed-runtime-{final-gates-summary,ctest-final-audit}.json.
Artifact freeze is released.016 is ready for documented closure, not yet changed
in the matrix;84FINAL/71PENDING remains the recorded count.

Sharp Runtime ISSUE148 PI candidate remains uncommitted. Full-component attempt
87202 failed in the pre-existing Diagnostics concurrency test (zero reader
snapshots); the fixed100-run diagnostic reproduced one such failure. Narrow
TEST-only latch now requires all four readers' first snapshots before stopping;
no Process implementation change, no sleeps/retries/oracle relaxation. Incremental
full build3684 passes; focused98557 passes fixed100 repetitions,whole Diagnostics,
Xml and Xml.Linq. Preserve original failed component log before rerunning the
full41 executable gate. PI sanitizer already passes4/4. BOM/timing part of148 is
still open. Exact next task:close016 with permanent evidence; run full Sharp gate
and commit Diagnostics test and approved PI repair separately,then148 string BOM.
No new pushes. Owner's memory query:read-only inspection found Codex PID12082
RSS8,807,404KiB/PSS8,807,384KiB,mostly anonymous private memory; cwd cna-demos.
No process was stopped. The cause inside Codex is not established by this sample.

15:58 UTC: C RootDirectory contract/test correction committed CNA124281bff and
demosdab9d4f, separately from016. No runtime changes or new pushes. Focused strict
native C and actual demo67/67 both renderers pass; all54 original labels retained
under two explicit owner-authorized mappings,13 additions. Canonical external
rebuild waits for frozen38847;103 marker remains and039/103 are not closed.
016 SOFTWARE719 completed625PASS/43FAIL/48SKIP/three manual, exact predecessor
comparison errors[] and only the original C-root residual. OPENGLES3 running.

Approved C RootDirectory follow-up now has an uncommitted candidate in
docs/c-api/CONTENT.md, modules/c-api/include/CNA/C/content.h and native ContentSmoke.
No runtime change:583af2a5e/68a3c7b1f already implement the intended contracts.
Native strict-C baseline22075 fails2 on both renderers; corrected15627 passes both,
including exact InvalidOperationException and cache retention with source removed.
Actual separately named demo47927 passes67/67 both renderers; all54 original check
labels preserved with only two owner-authorized semantic mappings,13 additions.
Demos source/metadata changes are uncommitted; marker remains until canonical gates.
See demos docs/repair-evidence/content/C_ROOT_CONTRACT.md. No frozen artifact replaced.

016 sanitizer97588 completes1 due an incorrect double-free prediction in the
baseline audit: actual old graph reports2070 leaked bytes/34 allocations, repaired
graph exits0/no ASan/UBSan/leak diagnostics. Instrumented consumer only; CNA archives/
DSO normal. Original failed audit retained, observed-evidence read-only audit passes.
Full38847 remains running (SOFTWARE input); counts84/71 unchanged.

016 repair committed `470b12a5d`; no campaign push. Publication900d0db3b remains
the verified remote tip. External full38847 is still RUNNING (SOFTWARE categories).
FBX delta86754 COMPLETE0: removing only the scoped needed-state wrapper produces
the exact previous binary hash; the repaired executable adds only libcna.so to
DT_NEEDED and runs successfully. No unexplained artifact delta remains.
Permanent pending evidence: demos docs/repair-evidence/build/MIXED_RUNTIME_LINKAGE.md.
Counts84/71 remain unchanged until full sweeps/CTest complete. Next: close016 on
complete local regression, then apply the approved XNA C RootDirectory contract
to documentation/demo/native C coverage (039/103); preserve all ownership checks.

15:42 UTC:016 root repair ready for coherent commit; full external regression38847
RUNNING on frozen319 artifacts. Native31/31:24 mixed runtime/symbol/state cases,
six rebuilt/requested static footprint consumers and existing shared-mode fixture.
All21 module/footprint link closures remain static. Eleven actual consumer graphs
pass on each of GNU ld.bfd,LLD,Mold (33 links/runs/symbol audits). Initial draft
FAILED on opaque ld.bfd --as-needed (SIGABRT/1200 strong Graphics definitions);
retained evidence led to a scoped needed-library feature, not global --no-as-needed.
Permanent opaque BFD regression also checks CNA retained/unrelated unused DSO omitted.
Four real CMake versions16 scenarios still pass; Clang addsfour passes. No new
compiler warnings; only existing Draco CMP0148 configure warning.

Native CnaTests,CnaMathTests,libcna.so are byte-identical to the immediately preceding
publication full10791/math969 validation. Reuse that exact-binary evidence; do NOT
claim a second full native run. External12110 COMPLETE0 (three preludes+47/50 tasks),
both demo link workarounds removed without changing assertions.318/319 artifacts
are identical to roots checkpoint; FBX standalone differs (needed-state control
86754 running). Fresh both95 math and six exact visuals pass; both719/CTest pending.
Counts84/71 unchanged. Source/issue closure follows only completed evidence. No push.

15:25 UTC: campaign resumed after the owner-requested frustum publication task.
Explicit one-task push authorization was used: origin/next is verified at
900d0db3b52d76d3115ebda0a0e04a25fefad1a5, fast-forward from688f3f23e; it contains
5ab1c7729. Commit900d0db3b adds ONLY the requested orthographic point regression.
Publication gates used the committed shared-runtime helper without overwriting
the unrelated six-file016 draft: math967PASS/twoSKIP; complete CnaTests10690PASS/
101SKIP,zero failures,1622.817s. Three frozen binaries and all six draft hashes
unchanged. Temporary CMake preload removed; publication evidence is demos reports
publish-frustum-completed.json. Sharp Runtime99c2f79c was used, NOT pushed.
No additional campaign pushes are authorized by that one-task publication request.

016 draft now exists (uncommitted): shared-target direct DSO injection and direct
owned-module exclusion. Eight native link orders/aliases pass16 runtime/symbol
checks; baseline explicit shared/Graphics-first variants abort and define1200
strong Graphics functions. Full publication suite did NOT validate this draft.
Next: cover opaque STATIC consumers with --as-needed, verify module-only static
footprints, then actual external browser/pipeline graph without demo workarounds.
No jobs active at resume; counts84 FINAL/71 PENDING unchanged. Preserve failures
and generic non-reproducing models. Owner policy decisions below remain binding.

Owner decisions received after the 14:38 checkpoint (explicit replies, not defaults):
- ISSUE-152: preserve native .NET FileShare behavior on each platform and document
  Windows/Linux differences. The original exclusive FileShare.None demo must pass
  on both. This resolves the policy question, not implementation/validation.
- ISSUE-148: a narrow, documented vendored tinyxml2 processing-instruction parser
  repair is authorized, with regression tests and corrected vendor provenance.
- RootDirectory C API conflict (039/103): unify with XNA/C++; explicitly correct
  the C documentation and demo contract. Preserve cache/disposal assertions; add
  refusal/exception evidence instead of hiding the previously observed failure.
These three policy blockers are removed. None is newly claimed fixed by approval.

016 minimized successfully to one actual-CNA translation unit: strict-built probe
constructs no Game/device/service and only retains an uncalled GamerServices function.
Control exits0 with zero strong Graphics definitions; without the demo's first-DSO
workaround it aborts at exit and contains1213 strong Graphics definitions. Evidence:
docs/repair-evidence/build/MixedRuntimeLifetimeProbe.cpp and ignored reports
repair-mixed-runtime-minimal-before-*. Next: repair umbrella link ordering, verify
actual mixed consumers and keep module-only consumers static. No production patch yet.
Closure commits now CNA8f0af7a8c / demosb71f57c;84 FINAL/71 PENDING unchanged.

14:38 UTC: ISSUE-001 FIXED_VALIDATED; native/external CMake policy gates complete.
External57606 COMPLETE0,50 printed steps/only prior Draco CMP0148 dev warning,
all319 artifacts byte-identical to the completed roots checkpoint;11 sources exact.
Fresh content1462930 exact68PASS/five unchangedFAIL each;catalog53901 threePASS6.60s.
No second full sweep claimed; original both719/math/visual/CTest proof applies to
identical runtime artifacts. All155 historical bodies verified intact.84 FINAL/71
PENDING:51 FIXED_VALIDATED,23 DOC,eight ALREADY_FIXED,one DEMO,one DUPLICATE.
Closure files pending immediate commit. Six-ID closure commits CNAa48c4c84f /
demos0b6478b. No running jobs; all frozen runtime artifacts remain unchanged.

016 --version confirms control exit0 versus isolated relink SIGABRT/double free,
without assets, game construction, GPU operations or services. Three more tiny
model variants (cycle, old policy, independent XML-like predecessor) also do NOT
reproduce; do not mistake them for full-graph clearance. Next minimize using a
small actual-CNA consumer with the real link closure, then repair the shared
umbrella's transitive ordering while preserving static-only module consumers.
No016 production change yet. Generic negative fixture remains uncommitted in demos.

14:28 UTC: combined24685 COMPLETE1 solely the original C-root residual. CTest
COMPLETE0:735 outcomes,684PASS/48 unchangedSKIP/three disabled,599.13s;319 hashes
exact. Six IDs013/014/105/106/108/110 now FIXED_VALIDATED; inventory/matrix audit
verifies every155 historical body preserved and83 FINAL/72 PENDING. Breakdown50
FIXED_VALIDATED,23 DOCUMENTATION_CORRECTED,eightALREADY_FIXED,oneDEMO_EXPECTATION_BUG,
oneDUPLICATE_ROOT_CAUSE. See ROOTS_BOUNDARIES_CHECKPOINT.md. Last globally accepted
checkpoint still048; no assertion/marker hides C-root conflict. Closure commits
pending immediately below;001 remains open pending external reconfiguration/build.

016: first generic small link model did NOT reproduce (all four runs pass); retained
as incomplete minimization, not ALREADY_FIXED. Actual current720-object demo closure
relinked to a separate ignored executable, removing ONLY first explicit libcna.so
token, reproduces6461 strong duplicate symbols (1725 Graphics) versus controlzero,
and SIGABRT/double-free at exit. Original metadata invocation also sees expected
missing assets from relocated title directory, so a --version-only control is running
to isolate shutdown from asset lookup. Initial nm capture hit the finite output buffer;
file-backed resumed76723 COMPLETE0,319 external hashes exact. No016 source repair.
Next: commit six-ID closure, release external freeze, configure/build001 in demos,
then continue016 with actual target topology, preserving static module footprint.

14:19 UTC: both719 full sweeps complete, exact local comparison passes. SOFTWARE
625/43/48/3,GLES664/45/7/3,68 cumulative improvements vs initialHEAD/70Complete1128;
only original C-root contract residual remains.24685 now serial CTest(~342/735).
All319 hashes exact before CTest. Six IDs still await final CTest/closure.

Build residual001 repair committed b35fbc933:shared-mode minimum3.30 (static overall
minimum unchanged),actual production policy/direct-helper guard,docs,native cyclic
fixture. Four real CMake versions16 scenarios pass, including four expected early
refusals on3.28/29 and12 build/runs. Both real old-version top-level ON calls refuse
with version/remedy. First audit line-wrapping mismatch retained, no compiler failure
waived. Native27369 COMPLETE0:three preludes/six tasks,only prior Draco CMP0148 dev
warning; all four old native hashes and319 external hashes unchanged. New registered
CTest5779/repeated33118 pass, final four-version93677 passes. Eleven source files
frozen. ONLY24685 running. After its final freeze check/closure, external rebuild
must validate the new configuration before001 closure. No ordinary source edits.
Counts77/78 unchanged. No push/merge. Demos build evidence pending commit.

14:08 UTC: next ownership baseline expanded, not repaired. Genuine36470 COMPLETE0:
108/108 (91 original+17 lifecycle); native37987 COMPLETE0 as baseline audit, actual
test exit1:10=2PASS/eightFAIL, all old seven outcomes exact and three new failures.
Strict builds/four native hashes pass. Records demonstrate Dispose failure's finally
cleanup and disposed-operation ordering; see RUNTIME_OWNERSHIP_TRIAGE.md. No new
production edits.24685 full combined external RUNNING(1), SOFTWARE graphics underway;
no other jobs. Prepared guarded repair-content-roots-close.mjs is NOT RUN and refuses
closure without final full/CTest/focus evidence. Counts77/78 unchanged.

14:01 UTC: corrected content78820 COMPLETE0:146 cases,68PASS/five unchangedFAIL
per renderer, exact audit errors[]. All319 post-focus hashes match. Four additive
manifest controls committed demos1f8ae67; no original assertion changed. Full
combined24685 RUNNING(1):both95 math and six exact visuals already pass; both719
sweeps/serial735 inventory CTest pending.319 external artifacts remain frozen.
No other jobs. Ordinary ownership011 investigation continues read-only: native
Texture2D copies intentionally survive original.Dispose and C API content handles
explicitly survive manager unload. Any manager ownership repair must preserve those
documented independent-handle contracts, not globally change all copy disposal.
Next finish full gates and close013/014/105/106/108/110 only on complete local proof.

13:59 UTC: external41603 COMPLETE0,1183 steps/zero warnings; three core, four C API
and one demo source hashes match.319 external artifacts frozen for focus. C API37932
COMPLETE0:177 per renderer, SOFTWARE167PASS/10 unchangedFAIL, GLES168PASS/nine
unchangedFAIL, original C foundations25 labels pass; exact audit errors[]. External
strict probes12884 COMPLETE0:manifest16/16,cold-root12/12 against rebuilt libcna.so.
Content focus8404 stopped at an AUDIT defect: ordinary DemoContext does not print
passing labels, unlike standalone child tests. Original logs retained. Corrected
audit proves entire original manifest source preserved after stripping the four-check
addition and comment correction; first correction had a one-newline boundary error,
also retained. Corrected78820 RUNNING(1), no product/assertion edits or retries of a
product failure. Next audit146, hash319, commit demo addition, full content-roots
regression. Counts77FINAL/78PENDING; original C-root contract decision still pending.

13:51 UTC: native root pipeline85718 COMPLETE0:562=561PASS/one unchangedSDK-fixture
SKIP,858.214s; exact audit/four hashes match. All native gates pass. Root batch
013/014 committed bbe2bda7b (three files, no code edits since freeze). External41603
RUNNING(2),~923/1183, includes bbe2bda7b plus C adapterc57ecab8c. No external focus
or full final pass yet. One free worker; no other active test jobs.

028 triage: genuine WaveBank path14/14PASS (93820), native streaming6/6PASS (87543),
strict warning-free/four native hashes unchanged. Two reference compile setup failures
retained (internal TitleLocation, then GameTime's separate assembly); final reflection
uses Framework Curve and unchanged expectations. Initial cwd lookup matches genuine
XNA; historical title-root expectation is wrong. No028 closure: lazy relative path
reopen after cwd change is a separate source-only concern not yet measured; no media
runtime patch. Permanent MEDIA_PATH_TRIAGE.md and issue028 progress retain boundaries.
Next immediate: finish/audit external41603, run roots146 content/354 C API focus and
external manifest16/cold12, then full frozen gates. All compiled sources remain fixed.

13:44 UTC: root module21730 COMPLETE0:1993=1989PASS/four unchangedSKIP, three new
focus pass, exact audit/hashes. Consumers58114 COMPLETE0:1412=1408/4, runtime241=
238/3, XML4/2 and original6/6; exact audit/hashes. Six link checks81512 allPASS.
Pipeline85718 RUNNING(1), near final public-sample build cases. External41603
RUNNING(2),1183 scheduled steps (~705); final319 boundary hashes checked before
release. Native three-source freeze plus external C API/demo source hashes intact.
Only these two jobs/three workers. No compiled source edits until both finish.

Matrix six current rows now record native proof, existing fix commits and110's C
adapterc57ecab8c; all remain CONFIRMED_OPEN. No closure helper run. Next finish
native pipeline/build audits, content146 roots focus, C API177 per renderer and
external16 manifest/12 cold-root probes, then full regression. No new marker edits.
Prepared, NOT RUN media triage in demos MEDIA_PATH_TRIAGE.md: installed XNA IL
confirms015 relative Song uses title;028 both genuine WaveBank constructors instead
use cwd. Framework monodis134 partial dump retained, target method/MVID complete;
Xact dump0. Genuine14 pre-native-path oracle and native6 streaming probe prepared.
Do not close028 yet: actual reference/native runs and a source-only lazy-path/cwd
lifetime concern remain. No media production changes.77 FINAL/78 PENDING.

13:32 UTC: C adapter compatibility COMMITTED c57ecab8c after strict native CnbSmoke
normal/sanitizer passes and the unchanged demo's exact25 labels pass in both modes.
Native REQUIRE count203->207 (four additive assertions). All319 external hashes match;
no C assertion relaxed. Original boundary full failure remains; no new closure.
Root canonical12705 COMPLETE0:1141 printed steps,45 exact prior warnings/zero new,
three source hashes exact. Four native artifacts frozen under repair-content-roots.
21730 module1993 RUNNING(1), three focused regressions alreadyPASS;85718 pipeline562
RUNNING(1);58114 consumers/runtime/XML RUNNING(1). No other SystemLink suite.
New uncommitted demo manifest adds four checks13->17, preserves all originals and
now asserts exact reserved-name exclusions plus legitimate hidden/prefix controls.
Prepared external phase content-roots compares against last completed012, includes
only105 status improvement and those four additive checks, and does NOT waive the
original C-root residual or C-foundations regression. Native/source edits frozen.
Next: finish native gates/link checks; once module/consumers pass, external rebuild
may share two workers with the remaining pipeline gate. Include committed C adapter;
run146 content focus, both177 C API categories, cold-root external witness, then
full719/math/visual/CTest/hashes. Boundary close helper is superseded/unexecuted.

13:25 UTC IMPORTANT: external boundary92630 COMPLETE1 with a NEW regression,
not just the original C-root residual. SOFTWARE719=624PASS/44FAIL/48SKIP/3manual:
105 improves but c_api.content.cnb_foundations regresses at its ORIGINAL None-size
assertion. Full driver stops before GLES/CTest; predecessor audit consequently
fails. Preserve all reports; no boundary closure, no successful final summary.
The C docs/native CnbSmoke explicitly require None to ignore declared sizes. Unlike
the C++ helper, this established C ABI is preserved: candidate adapter supplies
storedByteCount as both helper sizes for None only. C header/docs clarify difference;
original demo unchanged, two additive native C smoke input cases. Four C-only files
uncommitted; native build excludes C API so its three-source freeze is unaffected.
Before native CnbSmoke exits8 as expected; candidate normal67136 COMPLETE0,
strict compilation/linked identity/319 external hashes exact. Sanitizer70975
RUNNING(1). Root native12705 RUNNING(2),~416/1141. THREE workers total.
See CNB_C_API_COMPATIBILITY.md. Next native root gates, C sanitizer, then canonical
combined external rebuild/focus/full with original C ABI assertion preserved.
77 FINAL/78 PENDING;048 remains last globally accepted checkpoint. Closure helper
repair-content-boundaries-close.mjs is only PREPARED and must not run on failed data.

13:16 UTC: boundary external243-step and marker11-step builds COMPLETE0/zero
warnings; native/demo source hashes match. Initial146 and marker146 exact focus
both68PASS/five unchangedFAIL per renderer;37/32/23 controls retained. Demo changes
committed34e530e. Final boundary92630 RUNNING(1),319 FROZEN external binaries:
both95 math/six zero-pixel visuals pass, full719 sweeps underway. No boundary
closure yet,77 FINAL/78 PENDING. Do not rebuild external tree during this gate.

Next013/014 candidates UNCOMMITTED in three CNA files: ContentManager.cpp/.hpp
and ContentManagerManifestTests.cpp. Exact bookkeeping exclusion/two native cases;
lazy relative-root resolution/one fresh-process native case. See demos ROOT_REPAIRS.md.
Actual TU normal/sanitizer:01316 public+8 native pass, including combined candidate;
01412 public+1 fresh native pass; four native boundary hashes exact. No suppression.
Final boundary-native hashes checked before release. Native build12705 RUNNING(2),
1141 scheduled steps; three source hashes frozen. THREE workers total, no source
edits. Prepared roots native helpers expect1993 module/562 pipeline unchanged;
consumers wait for external SystemLink/CTest to finish. No013/014 demo changes.
Next: finish/audit native build, run module/pipeline/link gates; preserve original
C-root blocker throughout. Uncommitted handoff/evidence separate from candidates.

13:06 UTC: closure/status committed CNA183328865/demos9b8bdf4. Four separately
validated native fixes committed:1102386155f2,1051721b4182,1080d63ac853,
1062ce326907 (current CNA HEAD). No boundary closure or marker removal yet.
Final319 external012 hashes match; its freeze released. Three diagnostic demo
assertions now require the repaired contracts, preserving37/23/32 controls and all
positive witnesses. These three demo files are uncommitted during validation.
External boundary build25644 RUNNING(3),243 scheduled steps/private GPU. No other
jobs; freeze all compiled sources until complete. Next audit warning/task/source
accounting, initial146 focus, then105 marker/106108 restrictions only on proof.

13:03 UTC: ISSUE-012 RESOLVED/FIXED_VALIDATED; runtime d7ce398d8, demos marker
dc9bfac. Current heads before closure commits CNA46c9d5654/demosdb663fa.
77 FINAL/78 PENDING of155:44 fixed,23 documentation,8 already fixed,1 demo
expectation,1 duplicate. Complete proof: demos STREAM_ROUTING_CHECKPOINT.md.
Full719 SW624/44/48/3, GLES663/46/7/3; only two012 predecessor improvements;
67 improvements versus initialHEAD/69 Complete1128. Both95 math and six exact
visuals pass. Final CTest684PASS/48unchangedSKIP/three disabled,658.42s; all735
outcomes and319 hashes exact. Overall driver1 solely original C API RootDirectory
contract conflict; globallyAccepted=false, last accepted048.039/045/043/103 open.
Nine resolved inventory blocks moved under Resolved without changing any of155
body hashes. No IDs removed. No pushes/merges or new hardware/service claims.

Boundary105/106/108/110 canonical gates COMPLETE: module1990=1986PASS/4sameSKIP;
pipeline562=561PASS/1sameSKIP,830.126s; consumers1412=1408PASS/4sameSKIP;
runtime241=238PASS/3sameSKIP; XML4/2 and original6/6; six link checks allPASS.
All exact audits errors[], four native artifact hashes unchanged. Actual-TU normal
and ASan/UBSan/leak proof already recorded below. No active jobs. Nine boundary
source/test files uncommitted; closure matrix/handoff are separate changes.
Next: commit each of four root causes separately with matching native tests;
verify319 external012 hashes before release, correct historical diagnostic
observations110/108/106 without reducing controls, build boundary external tree,
run prepared146 initial focus BEFORE changing105 marker/106/108 restrictions.
Then marker rebuild/focus/full719/math/visual/CTest/hashes; no boundary closure yet.
Owner C-root choice remains unanswered; unrelated actionable work continues.

12:51 UTC: boundary module62368 COMPLETE0,1990=1986PASS/four unchangedSKIP;
five new focus PASS, exact audit/four hashes match. Six link-footprint checks98103
COMPLETE0, all pass/no skips/four hashes match.30922 pipeline562 still RUNNING(1).
External01278862 finished both719 sweeps:SW624/44/48/three manual,GLES663/46/7/three;
predecessor audit differences[] on both, only two012 improvements. Original C-root
residual unchanged, globallyAccepted=false retained.319 hashes match; final serial
CTest RUNNING(1), ~340/735. No source/metadata edits/rebuilds during either freeze.
Independent014 probe88540 COMPLETE0 audit:12=9PASS/threeFAIL, strict warning-free,
four boundary native hashes match. Fresh Linux cwd/title decoy proves root changes
after TitleLocation initialization; absolute root/cache controls pass. Permanent
ColdContentRootProbe.cpp/PUBLIC_BOUNDARY_TRIAGE.md, matrix CONFIRMED_OPEN. No014 fix.
Prepared boundary focus helper requires73 cases per renderer, only105 improves,
retains37 CNB controls and exact32 compiler/23 font child labels except deliberate
contract-corrected historical observations. Not run; no demo edits yet. Counts76/79.

12:45 UTC: all four boundary candidates pass focused normal/sanitized gates;
10624736/49964 COMPLETE0:21 public+three selected native. Canonical83644 COMPLETE0,
84 printed steps/zero warnings/nine source hashes exact, audit errors[]. Old native
012 four hashes checked immediately before freeze release. New four artifacts
FROZEN at repair-content-boundaries-native-frozen.sha256; nine compiled sources
FROZEN at *-source.sha256. No source/test edits until native gates finish.
62368 module RUNNING(1):five new focus then1990 exact cases.30922 pipeline RUNNING(1):
three new focus then562 exact cases.78862 external012 RUNNING(1):SW719 complete
624PASS/44FAIL/48SKIP/three manual; only retained original C root assertion delta,
67 intended improvements. GLES underway; original global audit remains strict.
THREE workers. Next free slot: six native link-footprint checks (no SystemLink).
Consumers/runtime/XML must wait until external012 full/CTest finishes. Boundary
runtime/tests/docs uncommitted; native build/gates use that frozen candidate. No
boundary demo changes or closure. Exact next after gates: separate root-cause
commits, external boundary build/focus after releasing012 external freeze, update
only measured historical diagnostic expectations and105 marker, full comparison.

12:41 UTC:012 marker96444 COMPLETE0 (same67PASS/six unchangedFAIL each renderer),
marker/evidence commit demosdc9bfac. Full external78862 RUNNING(1), phase
content-stream-routing,319 artifacts FROZEN; both95 math and six exact visuals
PASS, SOFTWARE719 sweep currently in net. No external rebuild until all full/
CTest/hash audits finish. Native012 artifacts remain unchanged for actual-TU probes.

Next coherent content-boundary batch is UNCOMMITTED in CNA:110 source+three native
regressions,105 shared resolver+two regressions,108 range guard+two regressions,
106 diagnostic/public comments+one regression. Before witnesses preserved.110
23256/60899 COMPLETE0:11 public+15 native normal/ASan/UBSan/leak.10541746/27986
first exit1 at legacy native fixture's stronger-Werror compile, after public25 PASS;
old missing-field/range-loop diagnostics retained. Canonical-policy fixture80998/
49461 COMPLETE0:25 public+23 native both modes, no suppressions.10886920/43501
COMPLETE0:12 public+8 native both modes. Four native012 hashes match every probe.
10624736 normal/49964 sanitizer RUNNING(1 each), actual content.cpp +21 public/three
selected native tests; old native fixture uses canonical flags, public staysWerror.
THREE workers including78862. No four-boundary demo assertions/markers changed.
See demos docs/repair-evidence/content/BOUNDARY_REPAIRS.md. Next after106 proof:
release native012 freeze only after final hash check, build canonical native-j2
(external78862 stays1), then1990 content +562 pipeline exact gates. Defer native
SystemLink consumers until external full/CTest ends. Counts76/79;012 still pending.

12:31 UTC:012 runtime COMMITTED d7ce398d8 after all native gates and initial146
focus passed. External75857 COMPLETE0, resumed484+original566 printed steps,
1040 unique body tasks/six repeated asset tasks, zero warnings/nine source hashes
exact.86484 initial focus COMPLETE0:67PASS/six unchangedFAIL each renderer; only
two012 improvements, resource checks2->4 existing success branch. Two012 markers
removed afterward, no assertion/ownProcess changes.48811 marker build COMPLETE0,
10 steps/zero warnings.96444 marker146 focus RUNNING(1); full gates next on319
frozen artifacts. Counts76/79 unchanged until full closure; global C root residual
still strict. Independent106 probe28937 COMPLETE0 audit21=16PASS/fiveFAIL, strict
warning-free/four native hashes match; permanent source and triage doc updated.
No106 runtime repair. Native012 artifacts remain frozen, no other active jobs.

12:21 UTC scheduling: all native gates/probes finished, so external79065 was
deliberately interrupted once with SIGINT to its verified Ninja2640985 (exit2,
no compiler error). Original log retained:566 printed steps/563 body tasks,51%
done. Nine source hashes still match. Resume75857 RUNNING(3),484 scheduled steps,
report *-external-build-rescheduled.log. This is a parallelism change, not a
failure retry. Auditor now reconciles both contiguous segments and exactly six
repeated independent copy/content tasks; requires1040 unique original body tasks,
zero warnings, completed final segment and unchanged nine sources. No other jobs,
no compiled source/metadata changes. HEADs CNA48de2373a/demos6ce3745.

12:18 UTC: native012 pipeline94252 COMPLETE0,559=558PASS/one unchangedSDK-fixture
SKIP; exact predecessor audit errors[], four posthashes match. All prepared native
012 gates now pass. External79065 is the only active worker (~514/1043); no source
or metadata edits until it finishes.012 runtime still uncommitted pending external
focus, no closure. Independent105/108 before probes also finish: named-routing25
=19PASS/sixFAIL; font-regions12=8PASS/fourFAIL, strict warning-free/four frozen hashes
match. Permanent sources and PUBLIC_BOUNDARY_TRIAGE.md retain failures and controls.
Next: audit external build,146 initial focus, commit validated012 runtime, remove
only the two012 markers on actual proof, marker rebuild/focus, then full gates.

12:14 UTC: consumers7409 COMPLETE0:1412=1408PASS/four unchangedSKIP,
runtime241=238PASS/three unchangedSKIP, XML4/2 and original fixtures6/6; exact
audit errors[]. Six link-footprint CTests COMPLETE0 (3.24s), no skips; both
four-artifact pre/post hashes match. Only pipeline94252 RUNNING(1) and external
build79065 RUNNING(1), ~440/1043. No source/metadata edits. Next: finish559 pipeline
audit and external zero-warning/source audit, then146 initial renderer focus.
Native012 source/runtime remains uncommitted, markers unchanged, counts76/79.

12:07 UTC: native module4214 COMPLETE0,1985=1981PASS/four unchangedSKIP. Canonical
20focus PASS, exact old1965 (+one measured oracle rename)+20 new cases audit errors[],
four posthashes match. Consumers7409 RUNNING(1), pipeline94252 RUNNING(1), external
79065 RUNNING(1). No overlapping SystemLink; external is a build only. No source or
metadata changes. Next free slot: six link-footprint CTests via *-native-gates.mjs
link-footprint. External build auditor prepared *-external-build-audit.mjs: require
all steps accounted, zero warnings and nine source hashes unchanged before146 focus.

12:04 UTC: canonical native01243298 COMPLETE0,1205 printed steps,51 prior warnings
and no new diagnostics; build accounting/provenance/nine source hashes audit errors[].
Four artifacts FROZEN under repair-content-stream-routing-native-frozen.sha256.
Module4214 RUNNING(1): canonical20 focus then1985 exact cases; pipeline94252 RUNNING(1):
559 exact predecessor cases. External79065 RUNNING(1),1043 scheduled steps. THREE
workers, no source/fixture/metadata edits. Reports *module-driver.log/*pipeline-driver.log.
Next when4214 finishes: consumers/runtime/XML then six link-footprint CTests in the
free slot (external is building, not SystemLink testing). After external build verifies
zero-warning/source hashes, run146 initial focus; remove012 markers only on proof.
Current HEADs CNAaa005dae6/demosdb2716f; CNA012 runtime/tests/docs remain uncommitted.
Prepared full external phase content-stream-routing expects67 cumulative improvements;
global C root conflict still retained. Counts76 FINAL/79 PENDING, no new closure.

12:03 UTC scheduling update: native43298 RUNNING(2), ~636/1205,51 prior warnings/no
new diagnostics in partial audit. Start external012 build RUNNING(1) under private GPU,
report repair-content-stream-routing-external-build.log. This intentionally advances
the earlier sequential schedule: focused actual-TU20/sanitizers already prove the
candidate, both trees now build the SAME frozen sources concurrently; closure/commit
still require all native/external gates. Nine source hashes and all319 old external
hashes reverified immediately before external freeze release. THREE workers total.
No compiled source/metadata/native-test edits while either build runs. Final external
full helpers now accept phase content-stream-routing with67 cumulative improvements,
two012 predecessor changes and the unchanged strict C root residual; not run yet.

11:59 UTC: closure commits CNA57bd989a0/demos840c1e5;76 FINAL/79 PENDING unchanged.
Only43298 native012 build RUNNING(2),1205 scheduled steps. All compiled source/test
hashes remain frozen at *source-platform.sha256; no subsequent product edits.
Idle third slot used for independent before probes against unchanged external089
library:0138738 COMPLETE0 audit16=8PASS/8FAIL;1104263 COMPLETE0 audit11=4PASS/7FAIL,
strict warning-free, no large allocations. Permanent probes and PUBLIC_BOUNDARY_TRIAGE.md
record exact failures and positive controls.105/108 confirmed from current focused
output + public contract/source; no runtime changes for013/105/108/110. No active probes.
Prepared012 gates remain next. Private loadsThroughOpenStream_ field comment is stale
after012; correct in a later header-edit batch, not during this source freeze.

11:52 UTC:089 CLOSED FIXED_VALIDATED, runtime5d32ca58b/marker4c03e74;76 FINAL/79
PENDING of155 (43 fixed,23 documentation,8 already-fixed,one expectation correction,
one duplicate). External67911 COMPLETE1 solely retained original C root-contract
delta. CTest COMPLETE0:684PASS/48 unchangedSKIP/three disabled650.59s,735 outcomes
and319 hashes exact; both719 predecessor audits allow only089 improvement, six exact
visuals and both95 math PASS. Permanent demos INITIALIZATION_CHECKPOINT.md records
explicit unique_ptr failure/reconstruction extension boundary and original residual.
Global048 and pending039/045/043/103 unchanged. No pushes/merges.

Current demos93ead6a (012 evidence), CNA HEAD56dca0b43 before closure/status commit.
Only active job43298 native012 build RUNNING(2),1205 scheduled steps, report suffix
native-build-platform. Source freeze still applies. External089 freeze released ONLY
after final audits; do not build external012 until canonical native gates prove repair.
Prepared helpers: repair-content-stream-routing-freeze.mjs (build accounting/warning
provenance/nine source hashes/four-artifact freeze), *-native-gates.mjs module|pipeline|
consumers|link-footprint, *-focused.mjs initial|markers. Focus allows only two012
improvements; ResourceContentManager must rise2->4 checks (existing success branch).
Next: finish native build and1985/559/consumer/XML/link gates; commit012 only after
validation, rebuild external/focus before markers. Uncommitted CNA012 runtime/tests/
docs retained; closure commit stages only handoff/matrix.013 additionally confirmed
from current manifest output/source, no code/test edit during012 freeze.

11:47 UTC correction: native13213 exited1 during CMake regeneration BEFORE compiling:
strict platform audit correctly rejects native SDL cleanup calls in content tests.
Retained native-build.log. No ratchet/budget relaxation: permanent native concurrency
case now uses existing PlatformFactory Headless + ScopedCurrentPlatform, preserving
all synchronization/exception/cache assertions. Demos fixture retains the independently
validated default-SDL integration + documented TLS cleanup. Native fixture23730 actual-
five-TU sanitizer20/20PASS, no diagnostics. Nonproduction audit unchanged81 files/1050
references PASS. New nine-source manifest *-source-platform.sha256; restarted43298
RUNNING(2) uses *-native-build-platform.log. Native freeze release remains valid;
no build had touched the four old binaries before this restart. External67911 stays
frozen/running serialCTest (~380/735). Next native gate plan below unchanged.

11:45 UTC:012 canonical native build13213 RUNNING(2), report repair-content-stream-
routing-native-build.log. Native089 four hashes verified immediately before release;
nine012 source hashes captured in repair-content-stream-routing-source.sha256. Do
not edit compiled sources/tests until build and frozen native gates finish. Added
20 native cases (identical to permanent demos fixture); corrected exactly one old
no-Game-registration oracle, now AStandaloneFamilyRegistersWithoutDeclaringOtherFamilies,
using genuine ResourceContentColdOracle14/14. Per-family linkage/withdrawal controls
remain strict. No012 marker removed or closure claimed.

Candidate80098/43448 COMPLETE0:16/16 normal and actual-five-TU ASan/UBSan/leak.
Expanded95620 normal20/20;97433 sanitizer20 behavioral passes but EXIT1/528-byte
SDL TLS leak in fixture-owned foreign threads. Original report retained. Installed
SDL_thread.h explicitly requires SDL_CleanupTLS before non-SDL thread exit; fixture
now owns that cleanup conditionally for SDL3. Final13459 sanitizer/74086 normal
COMPLETE0:20/20, no diagnostics/suppressions, errors[]. Native libraries were not all
instrumented. Pending external08967911 continues serial CTest, both719 sweeps and
predecessor audits done:SW622/46/48/3,GLES661/48/7/3; only089 improves from087, C root
residual retained/globallyAccepted=false.319 hashes match after full sweeps.
THREE workers (nativebuild2, externalCTest1). No native SystemLink until67911 ends.
Next: verify complete native build/warning provenance/nine source hashes, freeze
four native artifacts, run1985 module (1965 +20 +one measured rename),559 pipeline,
then consumers/runtime/XML and link-footprint checks.089 closure waits finalCTest.
Uncommitted CNA:012 runtime/tests/docs plus handoff/matrix; demos:012 evidence/handoff.

11:40 UTC:012 candidate now changes five production TUs plus ContentManager.hpp,
UNCOMMITTED. Expanded cold before88082 COMPLETE0 audit (2PASS/sixFAIL); first89871
compile failed before any test because the witness omitted BuiltinContent::Register's
manager argument. Both original failed build log and corrected baseline retained.
Cold actual-TU42333/15563 COMPLETE0 normal/ASan+UBSan+leak: sixPASS/two unchanged
ordinary-routing FAIL, exact audit errors[]. Linked libraries are not fully instrumented.
Boundary before64070 COMPLETE0 audit: eight cases, twoPASS/sixFAIL, including
SoundEffect's specialization and untyped external references bypassing OpenStream.

Combined actual-five-TU candidate80098 normal/43448 sanitizer RUNNING(1 each),16
controls. No compiled source/fixture edits until these finish.67911 external089
still RUNNING(1), SOFTWARE719=622PASS/46FAIL/48SKIP/three manual; only retained C
root assertion delta,65 intended improvements. Six exact visuals and metadata/tools
pass, GLES full underway. THREE workers total. Both canonical trees remain frozen.
Next: audit candidate16, extend reentrant/thread/exception-identity boundary coverage,
correct the obsolete native no-Game-registration expectation using genuine14-control
reference, then canonical native build/gates.012 not closed;089 final CTest still ahead.

11:34 UTC: CNA HEAD56dca0b43, demos HEAD4c03e74. Runtime0895d32ca58b and
marker are committed; marker focus55039 COMPLETE0, exact65PASS/eight unchangedFAIL
per renderer, standalone20/20 ordered controls. Final external67911 RUNNING(1),
driver repair-intermediate-initialization-final-gates-driver.log; pre319 hashes
and both95 math gates pass. External artifacts FROZEN: do not rebuild until full
smoke, visuals, serial CTest and posthash audits finish. Native artifacts remain
unchanged; all native089 gates completed as recorded below. No global C root waiver.

012 expanded before witness89871 RUNNING(1): eight cases, including cold family
closure, first explicit factory precedence, withdrawal across lazy all-family
installation, explicit restoration and new-Game restoration. Source edits only in
demos evidence/helper; no012 production edit yet. Candidate plan: allow standalone
family installation without declaring all families, preserve withdrawal during lazy
installation while allowing explicit registration/new Game to restore it. Ordinary
virtual OpenStream routing is a separate remaining cause. Genuine cold resource14/14
and original four-case native1PASS/threeFAIL retained. TWO active workers, no agents.
Next: verify expanded before audit, implement/test cold registration locally using
actual production TUs against frozen native libraries; finish089 full audit when ready.
Uncommitted: expanded012 evidence/helper and these handoff updates only.

11:25 UTC:089 runtime COMMITTED5d32ca58b, full regression pending. Native559
pipeline15936 COMPLETE0 (558PASS/one sameSKIP818.582s); consumers93704 COMPLETE0
(1412=1408PASS/fourSKIP129.299s,runtime238PASS/threeSKIP9.367s,XML4/2 and6/6PASS),
all exact audits/four hashes match. External58110 COMPLETE0,105 printed steps,
zero warnings,nine source hashes match. Initial focus31609 actual cases65PASS/eight
unchangedFAIL eachrenderer; helperexit1 ONLY stale historical18 child count (actual
predecessor/current both20). Read-only *initial-label-audit.json proves same20
ordered labels/only089 improvement; first failed audit preserved, no rerun.
087 closure remains75/80; no global C root waiver.

Marker089/restriction removed after20/20 proof, build88124 COMPLETE0 nine steps
warning-free; marker focus55039 RUNNING(1). No other active jobs. After it passes,
freeze319 via repair-content-dispatch-final-gates.mjs intermediate-initialization
under private GPU. Shared full/predecessor helpers extended for65 cumulative
improvements and only089 predecessor delta; original C root residual still strict.
Then serial CTest/hashes, no overlapping SystemLink. Do not rebuild external.

012 genuine ResourceContentColdOracle now14/14PASS, strict C#4/x86; no Game/GPU
service. Native four-case baseline1PASS/threeFAIL is committed demos7e63c20. No012
production change yet. Ordinary stream routing and cold builtin-family registration
are separate causes. Preserve per-family linkage, first explicit registration,
withdrawal across later family materialization, and restoration by a subsequent
Game; do not merely flip the initial declared flag (provisional factory precedence).
Current uncommitted demos:089 marker and genuine012 source/evidence; CNA status
updates only. Runtime089 committed; native artifacts still frozen pending release.

11:16 UTC:087 CLOSED FIXED_VALIDATED;75 FINAL/80 PENDING of155. Final external24965
COMPLETE1 solely for retained original-baseline C root-contract delta. CTest itself
COMPLETE0:735 inventory/732 enabled,684PASS/48 unchangedSKIP/three disabled670.99s,
all735 outcomes and319 pre/post hashes exact. Both719 predecessor comparisons allow
only087 improvement and explicitly globallyAccepted=false; full SW621/47/48/3,
GLES660/49/7/3, both95 math and six zero-pixel references PASS. No C contract failure
waived; global048 checkpoint and pending039/045/043/103 unchanged. Permanent demos
DISPATCH_CHECKPOINT.md records local closure and original residual side by side.

Native089 module31942 COMPLETE0,1965=1961PASS/four unchangedSKIP, exact errors[]
and four post-hashes match.15936 pipeline RUNNING(1), canonical7 focus PASS,559
cases.93704 consumers/runtime/XML RUNNING(1), started only AFTER external CTest
ended.012 baseline35158 COMPLETE0 helper:four native cases onePASS/threeFAIL,
strict warning-free; no012 runtime edit. New file ContentStreamRoutingTests.cpp
preserves direct ReadAsset positive, ordinary virtual Load/cache, override exception,
and cold resource/withdrawal controls. Cold resource genuine oracle is next.

External087 freeze RELEASED only after all final audits.
New external089 build58110 RUNNING(1), report repair-intermediate-
initialization-external-build.log, against uncommitted089 candidate. THREE workers
total (15936/93704/58110), native four artifacts stay frozen. No compiled CNA/demo
source changes during external build; no new012 runtime or native canonical tests.
Runtime089 still uncommitted pending native gates and focused external results.

11:07 UTC clarification:20915 module launch stopped before any test, with a136940-
byte single filter argument. Read-only --gtest_list_tests reproduction confirms
E2BIG, statusnull/stdout0; diagnostic JSON retained. Native helper now uses compact
suite patterns (as preceding module helper did), but still requires every exact
1965 identity/status.31942 RUNNING(1), report suffix content-module-compact and
module-compact-audit; fresh compact pre-hashes pass.15936 RUNNING(1) canonical7
focus PASS,559 pipeline at genuine differential build.24965 RUNNING(1) external
CTest (~524/735). No test failure retried; all failed setup logs retained. THREE
workers remain; do not start native SystemLink consumers yet.

11:05 UTC: canonical089 build9887 COMPLETE0,79 printed steps, nine source hashes
match. Two material-test nodiscard warnings exactly match the last build of those
TUs (repair-effective-parameters-native-build.log). Initial auditor incorrectly
compared to087's51 warnings, which did NOT recompile the material tests; failed
audit retained. Both first gate launches stopped at missing pre-freeze manifest,
BEFORE ANY TEST. Corrected warning provenance/accounting audit errors[]; four
native artifacts now FROZEN under repair-intermediate-initialization-native-frozen.
sha256. Resumed15936 RUNNING(1): canonical7 focus then559 pipeline;20915 RUNNING(1):
1965 module exact comparison. Fresh pre-hash names preserve both setup failures.
External24965 RUNNING(1): serial final CTest still in progress,319 artifacts frozen
at087. THREE workers total, no native consumers/SystemLink until external ends.
No compiled source changes while these gates run; native089 remains uncommitted.

11:00 UTC:089 actual-TU candidate normal59761/sanitizer49008 COMPLETE0:21basic,
11cycle,6failure,two isolation cases PASS. Expanded37339/48274 COMPLETE0: same38
controls plus FOUR isolation cases (late registration and simultaneous first use).
Native fixture wrappers35100/70983 COMPLETE0:7/7 native GTests plus fresh-process
cold-start1/1, normal andASan/UBSan/leak, no diagnostics/warnings. Three permanent
CNA test files now installed (seven cases); failed initialization uses threadsafe
death-test re-exec to avoid poisoning the other tests. Nine source hashes captured
in repair-intermediate-initialization-source.sha256.

Canonical native build9887 RUNNING(2),79 scheduled steps. Do not edit compiled
089 sources/tests while it builds.087 native freeze released after final hash
verification; no old087 native hash is claimed after this rebuild. External24965
RUNNING(1) stays FROZEN at087: BOTH719 sweeps complete,SW621/47/48/3 and GLES660/
49/7/3. Original-baseline comparison has64 intended improvements and the identical
C root assertion residual; additional predecessor audit has only087 improvement,
errors[] and explicitly globallyAccepted=false. Both95 math and six exact visuals
PASS. All319 post-full/pre-CTest hashes match; final serial CTest is running.
No concurrent SystemLink tests. Keep global048 checkpoint and74/81 unchanged.

Prepared repair-intermediate-initialization-native-gates.mjs accepts module
(1965 exact), pipeline (previous552+seven=559, includes canonical7 focus), consumers
(1412/runtime241/default/original XML). After9887, audit warning deltas and nine
source hashes, freeze four artifacts under the089 prefix, then run module/pipeline
with at most three total workers. Defer consumers/SystemLink until24965 finishes
all external CTests. Native candidate is uncommitted; no089 full-regression pass.

10:52 UTC: marker focus15417 COMPLETE0,146 exact results,64PASS/nine unchangedFAIL
per renderer. Demos8b69456 commits corrected oracle/validated087 marker; CNA status
6a8e77bf4 follows runtime3d254dfa7. External24965 RUNNING(1):319 artifacts FROZEN,
both95 math exact and six pixel references0diff PASS; SOFTWARE full currently runs
graphics. The final-gates helper will run GLES, retain original-baseline C root
delta, perform a separate predecessor audit, then serial CTest/hashes. Do not
rebuild external or overlap SystemLink suites.

Independent089 candidate is now UNCOMMITTED in four production files: intermediate
Serializer/ContentTypeSerializer .cpp plus Core/ContentTypeSerializer headers.
Registry-owned callback owner, scoped thread-local per-operation state, published
one-time initialization outside the registry lock, waiting concurrent initializers,
and reentrant/complete built-in publication replace temporary callback owners.
Explicit unique_ptr registrations cannot implement XNA fresh-instance recovery;
candidate documentation states cached initialization failure without retry/copy,
while raw lookup and retained failed owner remain usable. Genuine15 recovery
evidence is NOT removed or claimed identical; review this CNAEXT ownership boundary
before closing089. No factory/clone API added, no089 native regression installed yet.
59761 normal(1) and49008 sanitizer(1) compile FOUR actual implementation TUs and
the permanent21 basic,11 cycle,6 failure and two owner-isolation cases, then run
all in fresh processes. Source frozen until both candidate builds finish. Exact
helper repair-intermediate-initialization-candidate.mjs [sanitizer], reports same
prefix. Canonical native libraries still087 and four hashes checked before work;
no native rebuild yet. THREE workers total, no spare compiler/test slot.

10:46 UTC:087 runtime COMMITTED3d254dfa7, FIXED_PENDING_REGRESSION;74/81 unchanged.
All native gates below complete. External48985 COMPLETE0,1043 printed steps, zero
warnings. First audit rejected terminal gap3 vs prior1; retained. Exact accounting
shows three printed prelude steps plus1040 contiguous body steps, whereas prior
had one plus971. Corrected native/external *build-accounting-audit.json errors[].
Original focus90968 COMPLETE1,146 cases: only existing_objects old null-fallback
oracle fails5/13 on both renderers;087 shared_resources17/17 PASS both. Corrected
demo retains13 checks (null result AND unchanged offered object); oracle build76338
COMPLETE0 warning-free9 steps, oracle focus72580 COMPLETE0,64PASS/nine unchangedFAIL
per renderer, exact errors[]. Marker087 removed afterward, build55301 COMPLETE0,
nine steps warning-free; marker focus15417 RUNNING(1). No other active gate.
Next freeze319 external artifacts and launch repair-content-dispatch-final-gates.mjs
under private GPU. It runs both95 math, both719 smoke/six visuals, original-baseline
strict comparison (C root residual remains), ADDITIONAL predecessor-only comparison,
then serial735/732 CTest with319 pre/post hashes. No concurrent SystemLink suite.
No089 runtime edits yet; new17 edge controls are demos6d45c49. CNA uncommitted only
handoff/matrix; demos oracle/marker/evidence updates await coherent commit.

10:35 UTC: native31779 COMPLETE0; pipeline552=551PASS/one unchangedSDK-fixtureSKIP,
932.000s, exact audit errors[] and four post-hashes match. All087 native gates now
complete, but runtime remains uncommitted pending external focus. External48985
still builds1043 scheduled steps. Evidence demos6d45c49 includes native checkpoint
and new089 fresh-process edge probe: cycles11=onePASS/tenFAIL; failure6=onePASS/
fiveFAIL. Strict build warning-free, exact audits errors[], actual exits1. No089
runtime changes. Its explicit unique_ptr registration lacks factories for XNA's
fresh-instance retry; that boundary must not be silently replaced with same-object
retry. Genuine15 retains the full reference behavior.

Prepared additional repair-content-dispatch-predecessor-compare.mjs (syntax checked,
not executed) compares every719 status/check/marker/failure multiset against the
immediately preceding043 run, allowing only087's measured callback improvement.
It also requires the ORIGINAL-baseline residual to remain identical and reports
globallyAccepted=false when that C root-contract regression remains. This is NOT
a waiver or replacement of the mandatory original-baseline comparison. Keep039/
045/043/103 pending; a local batch pass alone cannot make the global campaign green.
Next: complete external build, audit warnings, run initial146 focus and retain the
known demonstrably wrong zero-reference fallback expectation, then correct only
that expectation with reference evidence and rerun all146 before087 marker removal.

10:30 UTC: native module/consumers complete with exact comparisons; pipeline31779
and external build48985 still running. Post-rebuild actual-TU sanitizer rerun36/36
PASS9ms, no ASan/UBSan/leak diagnostics, four frozen hashes match. Independent089
owner-isolation probe42827 complete: both cases fail with exactly10 owner/init
assertions; nested/concurrent relocation, alias and XML controls otherwise pass.
No089 runtime changes. Current CNA81ab8a7d5/demoscaa9c1b;74 FINAL/81 PENDING.
The failed matrix-update shell command made no changes; refreshed matrix follows.
No new accepted global checkpoint: C API root-contract owner choice still pending.

10:21 UTC: consumer60610 COMPLETE0.1412=1408PASS/four unchangedSKIP155.254s;
runtime241=238PASS/three unchangedSKIP9.448s; default XML4PASS/two unchangedfixture
SKIP1ms, original fixtures6/6PASS12ms. Exact four audits errors[], all four frozen
hashes match. Native pipeline31779 continues552 cases (genuine differential build
is the long-running gate); external48985 continues. Third worker42827 runs an
independent demos-only089 owner-isolation probe (two cases, nested and concurrent
contexts). No089 runtime changes or canonical test additions. Native087 remains
frozen/uncommitted, source nine hashes verified mid-build; dependencies freshly
verified clean/unchanged: Sharp99c2f79c, EasyGLf4d6afa, MetaGLdd3b300.

10:16 UTC: canonical087 module COMPLETE0,1965=1961PASS/four unchangedSKIP,
111.347s. All previous1929 outcomes (one measured test rename) and36 additions
match, differences[]. Post-module four hashes match.31779 now runs552 pipeline;
60610 runs consumers/runtime/XML after the retained setup-only log collision.
External48985 continues1043-step build. Native freeze remains in force. Demos
evidence4961222 records this module result; runtime087 is still uncommitted.

LATEST NATIVE087 GATES (10:13 UTC): corrected whole build10522 COMPLETE0.
1204 scheduled steps, terminal1201/1204 gap matches prior native3; all51 warnings
match preceding51 by normalized diagnostic/multiplicity, build audit errors[].
All nine candidate source hashes match. Four native artifacts now FROZEN in
repair-content-dispatch-native-frozen.sha256; no native rebuild/source edits during
validation.31779 RUNNING(1):36 canonical focus,1965 module exact audit against1929
plus36/one measured rename, hashes, then552 pipeline exact comparison.
3501 RUNNING(1):1412 consumers,241 runtime, default/original XML6, exact comparisons
and pre/post hashes. Only this consumer job runs campaign SystemLink tests.
External48985 RUNNING(1),1043 scheduled steps. Three campaign workers total.
Next inspect all module failures (none may be waived); after external completion,
run prepared repair-content-dispatch-focused.mjs initial on both renderers. It
deliberately reports the old existing_objects null-fallback expectation as a delta;
retain that failure before the reference-backed demo correction. After successful
oracle focus, remove087 marker and rerun markers focus before full64-delta sweeps.
Current CNA62594002e/demosd2adfc8; runtime087 uncommitted,74 FINAL/81 PENDING.

10:14 UTC correction: canonical focus36/36PASS3ms;31779 module continues.
First consumer driver3501 stopped1 BEFORE launching a test: its child log name
collided with the outer redirected driver log (EEXIST from deliberate wx guards).
All four pre-hashes had passed. Failure log retained; only helper output naming
was corrected.60610 RUNNING(1) resumes consumers with a fresh pre-hash report;
no retry of any failed test and no assertion change. External48985(1) continues.

LATEST LIVE STATE (09:51 UTC):14774 COMPLETE0,36/36 normal AND actual-TU ASan/
UBSan/leak controls, exact errors[].089 baseline50866 COMPLETE0 helper:21 checks,
fourPASS/seventeen expectedFAIL, strict build warning-free; no089 runtime edits.
Corrected native whole build10522 RUNNING(2), native-corrected-build.log; complete
external build just started RUNNING(1), repair-content-dispatch-external-build.log.
External043 freeze RELEASED only after full CTest/audit and319 hashes matched;
that failed strict checkpoint is still not accepted globally because of the C
root-contract delta. Current builds use corrected087 source, unlike prior043/103
external results. NO compiled CNA/demo source edits during either build. Three
campaign workers total. After native completion freeze four artifacts; run prepared
repair-content-dispatch-module.mjs (1965, previous1929+36, one measured rename),
then pipeline/consumers via repair-content-dispatch-native-regression.mjs. After
external completion run focused content comparisons before087 marker removal.
Uncommitted runtime/test/deviation remain in CNA; evidence update/nullable probes
and separate089 probe are ready for logically separate demos commits.74/81 unchanged.

Source-audit warning for the upcoming external focus: existing_objects.cpp has a
demonstrably incorrect additional oracle, independent of repaired043 lookup. It
expects ReadObject(existing) with serialized index0 to return its offered fallback,
while genuine53 and native dispatch explicitly prove null. The corrected runtime
will make its current "all three slots populated" guard fail early. Do NOT undo
the runtime null fix or label this an unexplained regression. Retain the first
focused failure, then correct that demo with the measured contract, preserving all
other13 controls and explicitly checking the supplied fallback remains unmodified.
No demo code changed while48985(1) builds. Current native10522(2); no source edits.
Evidence commits: demos49f9f33 nullable and0548d55 separate089 baseline. Additional
owner-lifetime analysis is read-only; no089 implementation choice made yet.

LATEST087 BOUNDARY (09:49 UTC): genuine expanded value/nullable oracle34/34PASS,
all original18 controls unchanged; reference53 retained. Actual-TU candidate36
gives32PASS/fourFAIL, precisely the newly exposed null-existing Nullable cases;
all prior28 remain passes. This is a candidate regression, not waived. Native
whole build77714 deliberately interrupted130 after491/1203 steps (only verified
own Ninja2371373 signaled), original log retained. No mixed build/native checkpoint
is claimed. The candidate now normalizes empty optional payloads to "new instance"
before invocation, matching managed Nullable boxing.36-case normal and sanitizer
job14774 RUNNING(1); native nullable regression installed and full module helper
updated to176 files/1965 cases, previous1929 plus36 and one explicit value-oracle
test-name mapping. No native full rebuild until these controls pass.
Independent089 baseline probe50866 RUNNING(1),21 initialization/owner/cache controls,
no089 runtime edits. Other user-owned island-simulator/cna-engine builds were seen
and were NOT signaled or modified. No concurrent campaign CTest remains;71529 passed.

LATEST EXTERNAL043 CHECKPOINT (09:44 UTC):71529 COMPLETE0.735-test inventory,
732 enabled,684PASS/48 unchangedSKIP/3 disabled,936.58s. Exact735 status comparison
against048 and all319 pre/post hashes pass, errors[]. The separate strict719
sweeps still retain the C API root assertion delta, so048 remains the last accepted
full checkpoint;039/045/043/103 are NOT closed by CTest's expected-failure status.
External freeze can be released for the next validated repair build; no external
build has started. Native087 build77714 RUNNING(2), compiled source stays frozen.
The third worker is now free for genuine nullable34 and actual-TU native36 controls;
those additional controls are not yet runtime results. No overlapping SystemLink
suite remains. CNAad34fe3ee/demosbe3c94d plus documented uncommitted087 work.

LATEST087 CANDIDATE (09:33 UTC): actual ContentReader TU plus28 native regression
cases PASS28/28, normal and ASan/UBSan/leak instrumentation, exact audits errors[].
Before10PASS/18FAIL; no prior passing control regressed. The full candidate unifies
typed/raw invocation, exact existing-reference identity/value rejection, null and
explicit-reader dispatch; preserves concrete disposable capability through erasure.
Serialized reference metadata is shared with collection element dispatch, not
Sharp's stub Type.IsValueType. Native copies of both probe files are installed.
One old native value-struct expectation is corrected from success to exact
InvalidOperation after payload consumption using measured genuine18 evidence.
Existing C++ by-value reference wrapper semantics remain explicit in
plans/plan_bindings_upstream.md;011 ownership is not claimed fixed.
Canonical whole build77714 RUNNING(2), repair-content-dispatch-native-build.log.
DO NOT edit compiled native source during this build. External CTest71529 RUNNING(1)
on frozen043/103 binaries; no overlapping SystemLink suite. All runtime/tests remain
uncommitted until canonical validation; demos evidence commit4ff4fd4 plus a small
sanitizer/timestamp documentation follow-up. Next freeze native outputs after77714,
run28 focus and the full module/consumer gates with the one explicit old-test-name
mapping, and finish the independent external CTest/hash audit.74/81 unchanged.

Additional boundary work while77714 builds: prepared16 genuine nullable controls
in ContentValueDispatchOracle.cs (original18 retained) and eight native cases in
the demos-only ContentNullableDispatchTests.cpp. Neither has executed yet. Source
review suggests an outer optional containing an empty Nullable value needs the
same "new instance" interpretation as null shared_ptr; current28 don't cover it.
Do not claim that boundary fixed. Prepared reference/helper syntax checks pass;
run only after a worker is free, then measure before changing native source after
the in-flight build finishes. Native source remains frozen during77714.

LATEST STATE (09:28 UTC): CNA572458ec6, demos1a139aa;74 FINAL/81 PENDING.
Full043 external job8777 COMPLETE1: SOFTWARE620PASS/48FAIL/48SKIP/3manual;
OPENGLES3659PASS/50FAIL/7SKIP/3manual. Exactly63 intended status improvements
against campaign start, but both retain the C API RootDirectory assertion delta
below. It is NOT waived. Both95 math, metadata/tools and six exact visuals passed.
Serial external CTest71529 RUNNING(1), with319 frozen-artifact hashes before/after
and an exact735-outcome audit against048. Last accepted full checkpoint stays048.
External artifacts remain FROZEN; new087 source is not in these binaries.

Independent087 work: new permanent ContentValueDispatchTests.cpp runs nine native
value controls against the existing genuine18-check oracle:fivePASS/fourFAIL.
Combined28 native controls give10PASS/18FAIL, all previous19 outcomes unchanged
(repair-content-dispatch-values-audit.json errors[]). Three uncommitted production
files now preserve shared-pointer IDisposable capability through type erasure;
this is only a PARTIAL candidate, not a fix/closure of087. Manager fallback011,
existing-instance identity and explicit/value dispatch are not repaired yet.
Initial focused compilation applied test-only Wextra/Werror to the production TU
and stopped on pre-existing unused renderer parameters. Failed log is retained.
Corrected helper compiles production with its exact compile_commands policy and
new regression TUs with Wall/Wextra/Werror; job72799 COMPLETE0, exact28-outcome
comparison accepts ONLY the two erased/shared callback improvements. No canonical
build or sanitizer result for087 yet. Next finish087 invocation semantics with
genuine53+18 evidence, preserve C API ownership, and keep the running external
checkpoint isolated from the new source. No pushes/merges.

LATEST EXTERNAL STATE (09:08 UTC; supersedes prior build/focus jobs): full build
54284 COMPLETE0,972 scheduled steps, warning-free. Focus99460 COMPLETE0:73 demos
per renderer,63PASS/ten unchangedFAIL each; exact146 comparisons errors[]. Original
043 demo passes all13 controls on both, precisely its two lookup failures removed.
Only then removed043 marker; incremental14100 COMPLETE0, nine scheduled steps,
warning-free; marker focus84481 COMPLETE0 with the same exact146 results.
All319 external artifacts are now FROZEN in repair-reader-lookup-binaries.sha256.
Full job8777 RUNNING(1):both95 math, metadata/tools, six exact visuals, both719
renderer sweeps with63 intended cumulative status improvements against startHEAD.
The unresolved039 C root-contract regression is deliberately NOT allowed/waived
by the full comparator; expect it to remain a reported failure until owner decision.
No final full checkpoint or043/103 closure yet. After full run, retain every delta,
check hashes, run isolated serial CTest/audit as appropriate without claiming an
accepted global checkpoint while this contract remains unresolved.

103 additional static gates:3197 generated routes in57 files current; generator
unit tests4/4PASS2.604s (34284 COMPLETE0), reports repair-content-exception-generated-
current.log and repair-content-exception-generator-tests.log. No generated edits.
CNA HEAD1249a46bf (runtime043252e9bca1,10368a3c7b1f), demos HEAD97d2279 plus
uncommitted043 marker/main handoff updates. All155 IDs retained,74 FINAL/81 PENDING.
Native043 freeze was released after its final audits; external frozen binaries
must not be rebuilt during8777/following audits. No087 production edits yet.
Next independent source work:087 ownership/dispatch, preserving genuine53+18,
native19 pre-fix outcomes, and the documented C API independent resource ownership.

LATEST043 NATIVE CHECKPOINT (08:53 UTC):12643 COMPLETE0.
Runtime/test/deviation now committed as252e9bca1; demos evidence HEAD9576c6a.
Consumers1412 give1408PASS/four unchangedSKIP183.079s; runtime241 gives238PASS/three unchangedSKIP
10.238s. Default XML4PASS/two unchangedfixtureSKIP; original fixtures6/6PASS12ms.
All four exact comparisons have errors[]; all four final frozen native hashes
match. With content1929, pipeline552 and actual-TU sanitizer11/11, the043 native
batch is validated and ready for its coherent runtime/test/deviation commit.
Status FIXED_PENDING_REGRESSION, not closed. Native freeze RELEASED after final
audit, but DO NOT edit compiled content sources while external54284 still builds.
Only external54284 RUNNING(2) remains. Next run the prepared146 focused comparisons
when that build finishes. Docs/evidence HEAD9576c6a includes measured087 boundaries.
039/045/103 remain pending the separate C root-contract question/full checkpoint.

LATEST LIVE STATE (08:50 UTC; overrides all prior running-job descriptions):
CNA3e27412c1 (103 runtime68a3c7b1f), demos4df894d.74 FINAL/81 PENDING unchanged.
043 pipeline11324 COMPLETE0:552=551PASS/one unchangedXACT SKIP969.769s, exact
comparison against039/045 errors[], all four frozen native hashes match.
Remaining native job12643 RUNNING(1):1412 consumers, runtime241, default XML,
original-fixture XML6, exact comparisons and final four hashes. External full
rebuild54284 RUNNING(2),972 scheduled steps. Native freeze remains in force.

Prepared087 boundary probes are now executed: genuine53/53PASS (all39 original
labels unchanged +14 boundary controls), native19=5PASS/14FAIL (all17 original
outcomes unchanged +two failing grouped cases), audit errors[]. Source-only
bracing warning initially stopped the Werror compile; corrected braces preserve
every assertion. Initial failed build/driver retained. Prefix
repair-content-dispatch-boundaries; native result suffix native-before-braced.
The first remaining-native driver15098 stopped on that compile failure before
consumers;12643 resumes only after the corrected independent probe/audit succeeds.
No087 production patch/design selected; no native frozen artifact changed.

Next: finish12643 and hash gate, commit043 runtime/test/deviation separately;
finish54284 then prepared repair-reader-lookup-focused.mjs (73 content demos
each renderer, exact prior039/045 focused outcomes with only043's two failures
removed). Only remove043 marker after its actual demo passes. No accepted full
checkpoint/039045103 closure while the C root contract remains unanswered.
Sharp99c2f79c, EasyGLf4d6afa, MetaGLdd3b300 freshly verified clean/unchanged.
Uncommitted:043 production/native test/deviation;087 permanent reference/native
probe additions and evidence, plus this handoff/matrix. Nothing pushed/merged.

LATEST103 UPDATE (08:37 UTC): external target67664 COMPLETE0, warning-free.
Runtime/test commit68a3c7b1f is now recorded; evidence demos7fd2618 (new results
pending documentation commit). Full external rebuild54284 RUNNING(2) under
repair-reader-lookup-external-build.log, alongside native pipeline11324(1).
Both native and evidence probes pass44/44 on SOFTWARE and OPENGLES3; each retains
all original controls and exactly eight canonical-type failures disappear.
Existing ErrorSmoke, CoreExtSmoke, GeneratedRoutesSmoke and ContentReaderSmoke
also pass both renderers; strict C17/Werror builds pass. ContentModelEffectSmoke
passes actual successful model/effect loading on OPENGLES3 (not just missing files).
Initial helper12026 exits1 because it wrongly requires the native summary spelling
from the evidence harness; actual executables all exit0. Original logs/audit are
retained. Corrected read-only audit checks both exact formats, all44 original
assertion labels and exactly eight improvements per renderer, errors[]. Reports
repair-content-exception-{after-*,corrected-audit.json,model-effect-*}.
103 runtime/test/CMake/fixture is ready for its own coherent commit; status remains
FIXED_PENDING_REGRESSION, original demo marker retained, root-contract question
unanswered. Native043 pipeline11324 continues, frozen four artifacts unchanged.
Do not report these manual native C runs as registered CTest executions.

LATEST AUTHORITATIVE LIVE UPDATE (08:31 UTC; supersedes running-job text below):
CNA HEAD ba30a215b, demos HEAD99f6b18. Still74 FINAL/81 PENDING; goal unchanged.
039/045 full job19438 COMPLETE1: both719 sweeps retain the C root-contract conflict
described below (SW619/49/48/3, GLES658/51/7/3). No final CTest/closure claimed.
All319 external hashes match after the sweeps AND isolated before-fix103 probes.
Failed external freeze is now RELEASED, without accepting that checkpoint.
Owner root-contract question remains unanswered; no C API root behavior/docs/demo
were changed. Last accepted complete checkpoint remains048.

043 canonical whole build75383 COMPLETE0:51 warnings, all already present in the
preceding52-warning baseline. New four artifacts are FROZEN in
repair-reader-lookup-native-frozen.sha256. Module76364 COMPLETE0:1929 cases,
1925PASS/four unchangedSKIP; all1918 previous outcomes plus11 new lookup passes
match exactly, differences[]. All four hashes match afterward. Full552 pipeline
11324 RUNNING(1), repair-reader-lookup-native-after.log; do not rebuild native.
Actual-TU sanitizer11/11PASS remains separate from canonical module coverage.

103 is independently reproduced without loaded assets/root changes: all eight
load routes return IO, clear outputs, retain message/category, but lose canonical
ContentLoadException identity. Four-byte XNB fixture; before36PASS/eightFAIL of44
on BOTH SOFTWARE and OPENGLES3. CNA-native C17/Werror copy also reproduces44/eight.
Permanent demo probe: docs/repair-evidence/content/ContentExceptionIdentityProbe.c.
Local candidate lets the common exception barrier translate the existing native
exception (eight catch arms in four C API files), preserving IO and metadata.
Native regression added at modules/c-api/tests/pure_c/ContentExceptionSmoke.c,
fixture tests/assets/content-exception/invalid.xnb, CMake test registration.
External cna_c_api target build67664 RUNNING(2); reuses the existing tree/shared
cache and also incorporates043 dependencies. Old external aggregate is no longer
an immutable039/045 checkpoint. No after-fix103 result or commit yet.
Initial missing-file probe separately found SoundEffect NotSupported instead of
ContentLoadException; preserve its logs and investigate the native loose-audio
boundary later, not by broadening103's expected canonical type.

EXACT NEXT: finish native552, exact audit,1412/runtime241/XML and four hashes;
finish cna_c_api build and run strict44-check103 probes on both renderers, plus
existing C diagnostic regressions. C API tests are OFF in the native tree and CNA
tests OFF in the external tree, so manual native-source runs are NOT a CTest claim.
Commit043/103 separately with tests after their gates; update permanent evidence
and matrix. Neither issue is closed. Keep root-contract conflict pending owner
decision. Maximum THREE compiler/test workers; private GPU only; no concurrent
SystemLink suites. Uncommitted source:043 registry/test/deviation,103 C API/test/
fixture/CMake; documentation:handoff and runtime triage; demo103 probe untracked.

AUTHORITATIVE UPDATE (supersedes all older checkpoint text below):
Phase F. All155 historical IDs retained:74 FINAL/81 PENDING. Totals:41 fixed runtime
findings,23 documentation corrections,eight pre-campaign fixes,one duplicate and
one demo-expectation defect. Goal unchanged. Nothing pushed or merged.
CNA runtime583af2a5e (status HEAD47ac55163), demos9c320f3 (048 closure6633f80),
Sharp99c2f79c.039/045 runtime and eight new native cases are committed, with
evidence-backed existing test corrections; both remain FIXED_PENDING_REGRESSION.
They were NOT part of the completed048 checkpoint binary set.

LATEST LIVE UPDATE (supersedes older running-job descriptions below):039/045 native
validation is complete, including552 and1412/runtime241/XML, exact audits and all
four hashes. External build6996 and marker/oracle build96000 COMPLETE0, warning-free.
The initial146 focus11347 FAILED and is retained: ownership demo expected rawEOF
instead of genuine ContentLoadException; the helper also miscounted failure-only
printed lines as all assertions. Only the catch type is corrected,011 preserved;
JSON total check counts remain exact and only the two repaired printed failures
may disappear. Focus61606 COMPLETE0:62PASS/11 unchangedFAIL each renderer,146 exact
comparisons errors[].039/045 markers removed only after both original demos passed.
Cache/root screenshots show expected text/checkerboard and match byte-for-byte
across renderers.319 external artifacts FROZEN in repair-content-runtime-binaries.sha256;
initial sandbox hash EPERM wrote no manifest, authorized repeat93065 completed.
Job19438 full external checkpoint RUNNING(1):both95 math exact48, metadata/tool
tests and all six zero-difference visuals PASS; both719 sweeps/finalCTest pending.
DO NOT rebuild external tree or alter its319 frozen artifacts before final audit.

Next043 local candidate is now present in ContentTypeReaderManager.cpp/.hpp plus
new ContentTypeReaderTargetLookupTests.cpp and plans/plan_bindings_upstream.md.
Target names associate with actual returned factory registrations; stale callbacks
cannot publish after removal/replacement, and clearing withdraws associations.
Lazy factories, callable copies, fresh unique_ptr and explicit/same-name aliases
are preserved. Actual-TU11/11PASS (before4PASS/sevenFAIL); ASan/UBSan/leak11/11PASS2ms,
job19899 COMPLETE0. Canonical whole native build75383 RUNNING(2), log
repair-reader-lookup-native-build.log. Native freeze was released after039/045's
final hash audit. Do not confuse this uncommitted043 source with frozen039/045 demos.
Maximum THREE campaign compiler/test workers total; private GPU only; no concurrent
SystemLink suites. No other campaign jobs remain active.

EXACT NEXT: finish19438 and strict62-improvement audits, then isolated serial final
735-inventory/732-enabled CTest with319 pre/post hashes and prepared
repair-content-runtime-ctest-audit.mjs. Only then close039/045. Meanwhile finish
75383, audit warnings, run canonical lookup/existing contracts and complete1929
content-module tests (old1918 unchanged +eleven lookup additions), followed by
pipeline/consumers/sanitizer gates before a043 runtime commit. External043 must
wait until039/045 external freeze is released.043 is NOT closed.
Uncommitted CNA work:the043 runtime/header/native test/deviation record and this
handoff; demos:three039/045 marker/oracle files plus primary/evidence documentation.

NEW REGRESSION / OWNER CONTRACT QUESTION (2026-10-20): full SOFTWARE719 has the
expected619PASS/49FAIL/48SKIP/three manual, BUT its strict comparison FAILS for
c_api.content.content_manager. The C demo expects live root mutation to succeed;
039 now refuses it. Its conditional branch skips later checks, including103's
exception-type witness:103 is NOT fixed or verified by this run. Raw child checks
fall from the original54 to43, although the parent remains an expected failure.
Report repair-content-runtime-full-SOFTWARE-comparison.json preserves the delta.
The C header content.h:73-81 and docs/c-api/CONTENT.md:36 explicitly promise cache-
preserving root changes (original commit ef6a72525, CBIND-028); native ContentSmoke
also relies on it. The C route directly forwards to the native setter. This is a
public-contract conflict, NOT yet a demonstrated demo expectation bug. No C API
demo/source/docs have been changed or assertions waived. An asynchronous owner
question is pending: unify C API with measured XNA/C++ and explicitly correct its
contract/tests (recommended), OR preserve the documented C-only behavior as an
intentional extension. Do not infer an answer from the preselected option.
19438 continues the independent GLES sweep; retain its result and verify frozen
hashes before releasing a failed checkpoint. Do NOT run/claim a successful final
039/045 checkpoint or close those IDs until this conflict is resolved. The
independent043 native build75383 continues. Prepared/syntax-checked helper
repair-reader-lookup-module.mjs pins173 files/1929 cases, all1918 prior outcomes
unchanged plus11 new lookup passes. No canonical module result yet.

LAST COMPLETE CLOSED CHECKPOINT:048, runtime3a1437725, unchanged demo behavior
snapshot3d1f833 (later evidence commits only). Native251/552+1412/runtime241/XML
and both719 renderers/math/visuals pass as below. Final CTest59450 COMPLETE0:
732accepted/zero unexpected failures/48sameSKIP/three disabled1427.54s; all735
statuses match previous and319 hashes pass before/after, errors[]. External freeze
RELEASED. Permanent ../cna-demos/docs/repair-evidence/content/EFFECTIVE_PARAMETERS.md.

PREVIOUS CLOSED CHECKPOINT:088/090/111/046/030, runtimef421948fe and external
marker snapshot3d1f833. Permanent ../cna-demos/docs/repair-evidence/content/
SECTIONS_CHECKPOINT.md. Native552=551PASS/one same XACT SKIP973.298s, all517 old
outcomes unchanged+35newPASS. Consumers1412=1408PASS/four sameSKIP187.433s;
runtime241=238PASS/three sameSKIP; original XML6/6PASS. Four native hashes verified.
Strict external719:SW617PASS/51FAIL/48SKIP/three manual, GLES656/53/7/3;
exact60 demo improvements vs initialHEAD/62vsComplete1128, differences[]. Math95
exact48 improvements; metadata/tool gates and six zero-difference visuals pass.
Final isolated serial CTest9154 COMPLETE0:732accepted/zero unexpected failures/
48sameSKIP/three disabled980.07s; all319 hashes match before AND after. External
freeze RELEASED only after this final audit. These are expected-failure-aware
results, not a claim that all719 demos individually pass.

CLOSED root causes:
-088 80b8e45b2 atomic XML shortcut registration and correct alias/name priority;
 genuine collision11/11 and priority12/12, native16/16, actual-TU sanitizer5/5.
-090 7864dd5b1 Initialize outside mutex with pre/post duplicate/frozen checks;
 genuine8/8, bounded native9/9, four callback regressions.
-111 6823af99a explicit environment platform/profile, compression/default overload
 preserved, component version2 invalidates wrong cached output; four regressions.
-046 f421948fe scoped external target/fixup identity resolution, including generic
 targets without serializers; genuine15/15, native17 plus full focus58/58.
 Actual Reader/Serializer/corpus+sections+external ASan/UBSan/leak44/44PASS565ms.
-030 ALREADY_FIXED by01534c2e7 before baseline. Genuine simulator27/27 proves sender-
 side Reliable-exempt normal latency. Demo0b4d2ce corrects invalid receiver/minimum
 assumptions, preserving exact packet counts and all six assertions. Rebuilt
 aggregate6/6 each renderer; native simulator/ENet11/11. No retries, added sleeps
 or online-service claims. Markersce04195/3d1f833 follow focused passes.
Prior registry GLES strict030-delta failure remains archived, not whitelisted.
Partial148 e0318ab52/de1b6e259 fixes empty sections and section-local fixup order;
PI/BOM remain open, marker retained. No vendor changes or permission inferred.

CLOSED048 FIXED_VALIDATED:
-Commit3a1437725 returns the actual effective processor parameters; appended optional
 requestedParameters preserves original manifest/cache identity, with legacy
 fallback for manual results. Manifest schema/fingerprint algorithm unchanged.
-Minimized before onePASS/oneFAIL4ms; initial exploratory stronger-warning failure
 retained. Four new regressions cover effective metadata, exact bytes/warnings,
 request preservation, distinct requested fingerprints and manual-result fallback.
-Native whole build89679 COMPLETE0,1164steps, only the same two material nodiscard
 warnings. Canonical pipeline33745 COMPLETE0:251cases,250PASS/one unchanged large-
 file SKIP60.971s; all246 original outcomes unchanged+five later additionsPASS.
 Original11161-case CTest log used; baseline LastTest.log is empty, not evidence.
-Native four artifacts recorded in repair-effective-parameters-native-frozen.sha256.
 Full552 XNA pipeline8475 COMPLETE0:551PASS/one unchanged XACT SKIP1008.149s,
 every prior outcome unchanged,differences[]. Four post-full hashes match.
 Broader job54092 COMPLETE0:1412=1408PASS/four sameSKIP203.003s;
 runtime241=238PASS/three sameSKIP11.121s;defaultXML4PASS/two same fixtureSKIP;
 original XML6/6PASS14ms. Exact consumer comparisons differences[] and all four
 final hashes match. Native freeze RELEASED only afterward for runtime build below.
-External048 rebuild5276 COMPLETE0,346steps,warning-free. Strict56 focus37051
 COMPLETE0:25PASS/three remaining FAIL each,errors[]; ONLY048 failure removed,
 all36 child assertions preserved,047 still fails. No marker change required.
 External319 artifacts recorded in repair-effective-parameters-binaries.sha256.
 Full math/visual/sweep job95510 COMPLETE0. Both95 math exact48 and six exact visual
 references pass; GLES font screenshot visually inspected. Full719 SOFTWARE
 617PASS/51FAIL/48SKIP/three manual and GLES656/53/7/3, exact60 improvements vsHEAD
 (62vsComplete1128), differences[]. Final isolated serial CTest59450 COMPLETE0;
 all319 pre/post hashes and unchanged735-status/48-skip audit pass.048 now CLOSED.
Maximum THREE campaign compiler/test workers, Sharp at mostTWO; no concurrent
SystemLink suites; private GPU wrapper only. Pipeline66736 COMPLETE0:552 cases,
551PASS/one unchanged XACT SKIP1212.106s. Exact552 outcome audit against048 has
errors[] and all four corrected native hashes match after the pipeline. Current
native consumers80119 COMPLETE0:1412=1408PASS/four sameSKIP234.190s,
runtime241=238PASS/three sameSKIP11.223s, defaultXML4PASS/two sameSKIP;
original-fixture XML6/6PASS4ms. Exact four consumer audits errors[] and all four
final hashes match. Native freeze RELEASED; external build6996(2) still reads
the source, so defer production edits until it finishes.

NEXT RUNTIME BATCH EVIDENCE:
ContentOwnershipOracle.cs35/35 actual XNA4 strict C#4/x86, no GPU/services. Pins
inline/shared callbacks, no identity deduplication, partial-load retention,
exceptional-Unload finally cleanup, virtual streams, root/cache and IOException
boundaries. FNA differs in deduplication/root/cleanup; measured XNA wins and future
implementation must record the departure in plans/plan_bindings_upstream.md.
Root039 native before probe4887 COMPLETE1 expected:twoPASS/oneFAIL3ms. Both setter
forms wrongly allow same/different root with cached content; failed Load and raw
ReadAsset correctly permit root changes. Initial compile14883 failed on existing
nodiscard warnings; those are corrected in reused test fixture without weakening
assertions (one initial-value assertion strengthened). Both logs retained:
repair-content-root-native-before-build.log and repair-content-root-werror-native-before.log.
Expanded genuine envelope oracle90059 COMPLETE0:89/89, including all header lengths
0..9, stream closure, failed-load root mutability and reader exception boundaries.
Native envelope88078 COMPLETE1 expected:fourFAIL10ms, leaking header/body I/O.
Actual-TU sanitizer28664 COMPLETE1 confirms four-byte heap overread at the end of
a ten-byte compressed external XNB, ContentManagerUntyped.cpp:154. Evidence retained.
Committed039/045 repair583af2a5e: shared private ReadXnbHeader validates both paths and
wraps I/O; ContentReader typed/erased ReadAsset wraps IOException only; both root
setters guard nonempty cache. Low-level header parser contract remains unchanged.
Actual ContentManager.cpp+Untyped.cpp and whole fixture ASan/UBSan/leak72209
COMPLETE0:23/23PASS29ms. One old SoundEffect ReadAsset test now expects the genuine
ContentLoadException instead of rawEOF; its malformed payload/allocation assertion
is unchanged. Whole-container and SoundEffect property fuzz catches are tightened
to reject leakedEOF; low-level parser/decompressor contracts remain unchanged.
After048 freeze release, native canonical20911 COMPLETE0,1194steps,
repair-content-runtime-native-build.log. All52 warnings match the original152
baseline by normalized file/diagnostic; no new warnings. Four native artifacts
recorded in repair-content-runtime-native-frozen.sha256. Corrected manager focus
23/23PASS; whole1918 module81669 COMPLETE1:1912PASS/four sameSKIP/two FAIL262.712s.
Song end-to-end test expects unwrapped FileNotFoundException, contradicted by
genuine91/91 expanded reference (81817 COMPLETE0). Direct SongReader contract must
remain FileNotFoundException. Writer fuzz's negative-sphere seed throws in its
constructor after the earlier math fix before reaching the writer; preserve the
same invalid payload using public field assignment, not by relaxing its writer oracle.
Both regressions require fixture corrections and revalidation, not suppression.
All four failed-gate hashes match. Native freeze RELEASED for these corrections;
this is NOT a successful content checkpoint. Initial70373 guard failed because
filter omitted two constructor tests (21/21PASS); corrected filter adds them,
not reduces expectation. Both logs retained. External039/045 has not been built;
external048 binaries still contain old runtime. Other011/012/043/087 remain open.
Fixture corrections are now applied: public sphere fields retain the identical
invalid writer input/expected Written result; Song end-to-end expectation becomes
ContentLoadException, with explicit renamed-test mapping in the audit. Direct
SongReader FileNotFoundException contract is unchanged. Eight-step incremental
build28508 COMPLETE0, warning-free. Combined manager/writer/song33/33PASS146ms,
corpus48=32refused/16written, mutations400=204refused/196written. Corrected1918
job71175 COMPLETE0:1914PASS/four sameSKIP188.735s, all1905 previous outcomes and13
additions match, differences[]. Four post-module hashes pass. Writer-only fixture
correction1ec77a734, runtime583af2a5e. Native artifacts remain FROZEN under
repair-content-runtime-corrected for full552 pipeline66736 and following consumers.
External build6996 parallel2 RUNNING against583af2a5e; no external repair pass yet.
011 includes value-handle Texture2D ownership; a shared_ptr-only fix is insufficient.
012 cold builtin registration depends on Game declaration; preserve family link
boundaries and explicit reader withdrawal.043 must preserve fresh unique_ptr API.
New ContentDispatchOracle.cs genuine39/39, strict C#4/x86 build/run exit0:
nine typed/erased/raw modes, all four existing-instance replacement rejections,
null dispatch and null reader result. Raw new objects DO record; existing objects
do NOT; replacement throws InvalidOperationException; null index returns null.
Evidence repair-content-dispatch-xna*. No011/087 production change yet.
Prepared but NOT compiled/run: demos docs/repair-evidence/content/
ContentDispatchTests.cpp (17) and ReaderTargetLookupTests.cpp (eight). Kept outside
native source glob while039/045 builds. Run bounded before probes once a worker
is free and the native library closure is stable; preserve failures, then port
tests into native coverage with the next repair. No087/043 pass claimed.
Further prepared ContentOwnershipTests.cpp adds seven011 shared_ptr lifetime
cases from measured XNA; opt-in stream routing isolates012 and does not repair it.
Combined32-case source build-probe/repair-content-dispatch-lookup-tests.cpp.
Combined baseline45949 COMPLETE1 as expected:32tests,8PASS/24FAIL8ms; dispatch5/12,
lookup1/7, ownership2/5. Wall/Wextra/Werror compilation passes after correcting
anonymous-namespace linkage of generated test classes; initial failed build log
retained. Prefix repair-content-dispatch-lookup-werror-native-before. No runtime
repair yet, and value-handle Texture2D still requires separate work.
Genuine ContentValueDispatchOracle1740 COMPLETE0:18/18. Explicit value-reader calls
bypass index, IDisposable value types never record, all four existing-value overloads
throw InvalidOperationException due boxed identity. Do not apply reference-type
dispatch rules to value targets or blindly preserve existing incorrect native expectations.

EXACT NEXT: finish external build6996,
run prepared repair-content-runtime-focused.mjs (73 content demos each renderer,
only039/045 assertion changes allowed; preserve all other failures/counts), then
remove those markers only after focused passes. Run full62-improvement checkpoint,
math95/visuals/finalCTest/hash audit before closing039/045. Native freeze stays.
048 is closed. Preserve047/089/148 markers.
Prepared helpers (syntax checked, not run yet): repair-content-runtime-native-audit.mjs
accepts pipeline or consumers and compares exact outcomes with048; consumers include
the original-fixture XML6/6 run using
XNA_SAMPLES_ROOT=/rv/data/development/github.com/libcna/sharp-runtime/build-probe/cna-repair-xna-samples
(existing links only). repair-content-runtime-{math,math-compare,regression,full-compare}.mjs
require exactly62 improvements vs initialHEAD/64vsComplete1128, retaining048's
child-only improvement and all previous deltas. Focus script defaults to old markers;
its markers argument expects only039/045 markers removed after verified passes.
External6996 schedules1037steps; native66736 differential corpus has passed its long
accept/refuse comparison (802.611s suite), error-parity and remaining cases continue.
Sharp99c2f79c, EasyGLf4d6afa and MetaGLdd3b300 were freshly checked clean.
Read-only demo review found a likely additional oracle correction: content_manager/
read_asset_ownership.cpp catches raw EndOfStreamException around ReadAsset's
truncated_tiles input. Genuine91/body evidence requires ContentLoadException at
that boundary; direct ReadBytesExactOrThrow must retain EOF. Do NOT edit the demo
while6996 compiles. First retain the completed build's strict focused failure,
then change only the exact catch type if confirmed, preserving reader-name,
zero-record, opened/destroyed stream checks and count.011 must remain open.
Expanded043 lookup before probe82991 COMPLETE1 as expected:eleven cases,
fourPASS/sevenFAIL0ms; strict Wall/Wextra/Werror compile passes. Original eight
unchanged, three compatibility controls added (same-name/manual associations and
throwing factory publication). Reports repair-reader-lookup-before*. Separate
five-check genuine XNA lookup oracle54615 COMPLETE0:5/5, strict C#4/x86/Werror.
Distinct target/reader names resolve during Initialize; implicit raw read yields73
without an index. Genuine same-reader identity is measured separately; preserve
CNA's documented fresh unique_ptr ownership. Reports repair-reader-lookup-xna*.
Uncommitted runtime work: none. Current evidence/status updates are committed
separately. All failed setup/regression logs remain; corrected native gate passes.
Owner-reserved131 untouched;152 sharing policy and148
vendor policy remain unanswered. Ordinary actionable issues remain: campaign NOT complete.

### Earlier same-day checkpoint (superseded by authoritative update above)

AUTHORITATIVE CURRENT STATE: Phase F content/serialization. All155 historical IDs
retained:68 FINAL/87 PENDING (36 runtime findings,one duplicate,23 documentation,
seven pre-campaign fixes,one demonstrated demo-expectation defect). No push/merge.
CNA source HEAD e0318ab52 (partial148); demos81d6837; Sharp99c2f79c. Goal unchanged.
Current frozen checkpoint binaries are STILL6823af99a/ce04195, without partial148.

LAST COMPLETE REGRESSION:146/147/149 FIXED_VALIDATED. CNA7c4431fdf/9dd0826bf,
Sharpaf1b0bea through99c2f79c, demo markers71e6992, closure demoa604560/CNA50127b2f7.
Native503PASS/one same XACT SKIP of504 (exact six expected improvements), broader
1412/runtime241 unchanged, original-fixture XML6/6, actual-TU ASan/UBSan/leak9/9,
Sharp18146/18146 across41 with zero failures/skips. Both719 strict comparisons:
exact57 demo improvements vs campaign HEAD (59 vs Complete1128), plus two149
assertion improvements; differences[]. SW614/54/48/3, GLES653/56/7/3; six exact
visuals. Final isolated serial CTest15390:732accepted/zero unexpected failures/
48same skips/three disabled,747.99s. All319 hashes verified before/after.
Permanent demo docs/repair-evidence/content/CHECKPOINT.md. Previous storage
checkpoint remains docs/repair-evidence/storage/CHECKPOINT.md. Both old freezes
were released ONLY after their final audit.

ACTIVE CURRENT BATCH088/090/111 — NOT CLOSED:
- 090 committed7864dd5b1: Initialize outside compiler mutex, duplicate/frozen
  checks before callback and before publication. Genuine8/8, bounded native9/9,
  focused10/10; native full508 gives507PASS/one same XACT SKIP,859.192s, all504
  previous outcomes unchanged and four new PASS. Broader1408PASS/four same SKIP
  of1412, runtime238PASS/three same SKIP of241; hashes verified.
- 088 committed80b8e45b2: atomic alias validation, separate private full-name/
  alias maps, alias-first XML lookup and full-name-first canonical lookup with
  array-alias fallback. No public API/layout change. Genuine duplicate-alias11/11
  and priority12/12; original standalone16/16 (before6PASS/tenFAIL). Actual
  IntermediateSerializer.cpp plus five native regressions ASan/UBSan/leak5/5.
  Native focus34/34 includes whole intermediate corpus,705ms.
- 111 committed6823af99a: explicit environment platform/profile forwarded;
  no-environment overload retains registration defaults; compression retained.
  Component version1->2 invalidates old incorrect cached output. Demo's exact
  identity assertion updated to2 without relaxing/count changes. New four-test
  baselineonePASS/threeFAIL18ms; combined focus52PASS/one existing opt-in >2GiB
  SKIP of53,1007ms, including existing manifest fingerprint tests. This is a
  public CNA extension contract, NOT Windows Phone/Xbox runtime validation.
- First strict56 external focus removed only088/090 failures. Final strict56
  after111:24PASS/four unchanged FAIL per renderer, errors[] everywhere,
  exact parent/child assertion counts and failure multisets preserved.
  XML-to-XNB33/33. Markersce04195:088->089;090/111 removed only after passes.
- Registry external build36112 and111 builds92716/56489 and marker59195 complete.
  External and111 native builds warning-free; registry native52242 retains only
  the same two pre-existing material nodiscard warnings (418/420).

RUNNING JOBS / FREEZES:
1. Native full517,119 suites:50856 RUNNING via
   node build-probe/repair-content-native.mjs after repair-registry.
   Logs repair-registry-native-after*. Native CnaTests/libcna/static content
   archive/cna-content FROZEN in repair-registry-native-frozen.sha256 (four hashes).
   DO NOT rebuild native until full run and subsequent consumer gate/hash audit.
2. External full checkpoint11178 RUNNING. Both95math sweeps exact48 improvements,
   metadata/tools and six zero-pixel-difference visuals already pass. Full
   SOFTWARE then OPENGLES3 underway; strict cumulative expected58 demo improvements
   (60 vs Complete1128), plus two149 and one088 assertion improvements; remaining
   089 marker stays visible. EXPECTED counts only:SW615/53/48/3,GLES654/55/7/3.
   These full results are NOT yet claimed.319 executables/DSOs/content tool FROZEN
   in repair-registry-binaries.sha256; DO NOT rebuild external until both sweeps,
   final isolated serial CTest and before/after319-hash audit finish.
   Drivers repair-registry-{math,regression,full-compare}.mjs; reports
   repair-registry-regression-driver.log and repair-registry-full-*.
Maximum THREE campaign compiler/test workers (Sharp at mostTWO). No concurrent
campaign SystemLink suites. Never use the user's desktop; private GPU wrapper only.
Run native corpus tests from CNA ROOT, not cna-demos cwd.

IMPORTANT RETAINED EVIDENCE:
- An all-name collision rejection candidate passed five sanitizer tests but was
  REJECTED on genuine XNA evidence: XML shortcuts may overlap full type names in
  either order. Old source/tests/sanitizer retained as repair-intermediate-registry-
  overstrict-*. Two new rejection expectations were corrected to genuine-backed
  positive round trips; unchanged native library still fails four of five tests.
- Initial registry focus from demo cwd had24PASS/ten missing-fixture failures;
  retained. Correct CNA-root run34/34 passes. Standalone probe initially used
  wrong XML factory/ownership signatures; corrected strict C++23/Werror build.
- 089 genuine initialization21/21 and edges15/15: process-wide long-lived owner,
  once initialization before callbacks; self/mutual lookup works. Discovered
  custom serializers initialize eagerly in singleton construction; failure
  prevents singleton publication, captured failed owner retains its instance,
  next successful owner owns fresh instances. The first lazy-init setup assumption
  failed; preserved. Native monodis crashed; targeted genuine reflection/IL worked.
  No089 runtime repair chosen. Do not give a globally cached serializer a temporary
  Initialize context. Permanent evidence demos887d950/66f720d/33b7e32 and README.
- Sharp watcher full gate is green but two separate stress-only rename findings
  remain documented, not claimed fixed.
- 148 genuine envelope16/16, reader61/61 on Framework4/.NET8. Tinyxml2 PI-position
  vendor-policy question remains UNANSWERED; no vendor edits. String-BOM rejection
  must respect deferred Read exception timing (not throw prematurely at creation).
  CNA's own self-closing Resources/ExternalReferences early returns are separately
  actionable; no148 production edits yet.
- 152 platform-sharing-policy question remains UNANSWERED; no inferred approval.
  131 remains OWNER-RESERVED; do not modify outside-entry ray/frustum behavior.

EXACT NEXT:
Finish517 audit against prior508 (nine added PASS expected, old outcomes unchanged),
then prior1412/runtime241 consumers and XML fixture check, with native hash audit.
Finish both external strict full comparisons, then isolated serial CTest and319
hashes before closing088/090/111. Preserve every ID; final count would71 only AFTER
those gates. Do not whitelist030 assertion drift or any other unexpected delta.
While frozen jobs run, prepare owned-CNA148 self-closing-section reference/native
reproductions; vendor PI and152 policy questions must not stall independent work.
Partial148 committed e0318ab52, demo evidence81d6837: genuine section oracle13/13;
native frozen-library baseline sixPASS/sevenFAIL2ms; modified actual Reader TU with
13 native cases ASan/UBSan/leak13/13,8ms. Only two early returns removed; three old
corpus exemptions removed, golden inputs/messages retained. No148 closure or marker
change: PI/BOM still open. Isolated sanitizer corpus extension now RUNNING (session
record in tool history), log repair-intermediate-sections-sanitizer-corpus.log.
Exploratory Wall/Wextra build hit an unchanged parentheses warning at Reader.cpp57;
retained failed log. Passing sanitizer uses prior project/Werror policy, not a
claimed stronger-warning pass. GLES registry font capture visually inspected; exact
six-image automated gate already passes. Full517 now in XnaErrorParity after the
differential-build suite completed621s without failure. SOFTWARE sweep in graphics.
IMPORTANT NEW REGRESSION: full isolated22-case section+corpus ASan gate37341 FAILS:
21PASS/one corpus FAIL with two changed missing-reference diagnostics, plus792 bytes
in11 leaked cyclic Node fixture allocations.13 focused cases still pass, but that
was insufficient. Genuine expanded section oracle18/18 proves shared fixups run
after the resource section and before external parsing, external fixups before the
final envelope check; section-end errors still precede fixups. Production correction
splits private ApplyFixups accordingly (UNCOMMITTED). Test-only NodeGraphCleanup
breaks strong cycles AFTER serialization/assertions, including exceptions/ASSERTs;
no graph assertion/expected XML is weakened. Neither correction validated yet.
Native baseline extensions initially omitted Object::GetTypeName overrides, making
fixtures abstract; retained setup logs. Corrected baseline compilation now running.
046 genuine external-alias oracle12/12: root/local aliases, shadowing, generic
arguments, assembly-qualified identities and targets without serializers; surrounding
TargetType whitespace is REFUSED (initial contrary expectation retained11/12).
Native046 tests prepared, no046 production change yet. Demos uncommitted oracle files.

088/090/111 full native517 COMPLETE516PASS/one sameXACT SKIP958.663s; exact508 old
outcomes unchanged, nine added PASS; four hashes verified. Consumer40129 RUNNING.
SOFTWARE full719 strict58 improvements/differences[],615PASS/53FAIL/48SKIP/3manual;
OPENGLES3 now654PASS/55FAIL/7SKIP/3manual, BUT STRICT COMPARATOR FAILED:030 outage
assertion unexpectedly passes (latency assertion still fails). Original result retained,
no whitelist/retry. Other718 outcomes and all intended changes match. Final isolated
serial CTest51475 RUNNING independently;319 hashes verified before it. Do not close
088/090/111 or rebuild external yet. Current permanent pending evidence:
demos docs/repair-evidence/content/REGISTRY_CHECKPOINT.md, demoHEAD23dc47c.

Native consumers40129 COMPLETE:1408PASS/4sameSKIP of1412,172.778s; runtime238PASS/
3sameSKIP of241,10.175s; exact old outcomes unchanged. XML original fixtures14857
6/6PASS4ms; default invocation4/2fixtureSKIP retained. Four native hashes audited
after all gates. Native checkpoint freeze can release AFTER current isolated sanitizer
56243 finishes linking/running (it still consumes these archives). That sanitizer
includes corrected section ordering and RAII corpus cycle teardown, not yet a pass.

030 investigation: genuine managed simulator27/27 (no network/services), native
deterministic7/7. Source fix01534c2e7 is an ancestor of campaign baseline688f3f23e:
simulation moved to SENDER; Reliable exempt; latency normal(meanL,sdL/4,clamp0..2L).
xna4-spec NetworkSession.xml and actual decompiled/genuine DLL agree. Demo sets
receiver and wrongly asserts all delays>=mean. Prepared corrected sender setup/
recovery marker and statistically justified sample-mean check (L/2,9.8 standard
errors for24 samples), preserving reliable/unreliable counts and native exact tests.
No030 closure/marker removal yet. Separate demo build41625 RUNNING into build-probe,
NOT frozen bin; do not run it while CTest/SystemLink executes. Demos uncommitted
NetworkSimulatorOracle.cs and network main.cpp. Sharp clean. No push or merge.

LATEST: sanitizer56243 COMPLETE0,27/27 in1567ms with NO leak/UB/address report.
CNAde1b6e259 commits corrected148 ordering + tests/teardown. The earlier22-case
failure remains evidence; no golden/input/assertion deletion. Native freeze RELEASED
after its full/consumer/fixture/hash gates and sanitizer completion. Native rebuild
21039 NOW RUNNING (72 steps,parallel1) for partial148 plus uncommitted046 identity
resolver; actual-TU sanitizer23734 COMPILE-ONLY RUNNING with corpus,18 section/order
and14 external identity/roundtrip cases (41 expected). LINK ONLY after21039 finishes.
046 reuses namespace/generic resolver with private known-identity mode, rejects
TargetType outer whitespace, canonicalizes both table and fixup identities, no
global alias/public API. Two writer roundtrip tests added to12 genuine-matched reads.
Network separate build41625 COMPLETE0 with Werror, binary build-probe/repair-network-demo;
do not execute before CTest51475 ends. Native simulator7/7 confirms unchanged net
source (git diff against688f3f23e is empty). DemoHEAD76e8bb4 saves evidence; only demo
network main.cpp remains uncommitted. CNA046 code/tests and this status are dirty.
Next: finish native build+41 sanitizer, focus content and full549 content inventory;
finish old serial CTest+319hash audit, then run corrected standalone network onboth
renderers and sender/receiver native integration without overlapping SystemLink.
External old freeze still active; registry strictGLES remains FAILED on030 variance.
Do NOT call either full checkpoint green or close088/090/111/030 yet.

### Chronological work log (superseded by authoritative state above)

UPDATE: SOFTWARE strict comparison is exact55 improvements/differences[].
Sharp full final gate91010 COMPLETE18146/18146 across41,zero failures/skips;
af1b0bea commits XML/LINQ repair, e0601cd2 separately commits watcher TEST barrier.
The earlier invocation59482 omitted XNA_SAMPLES_ROOT and ended with five fixture
skips; retained *-components-missing-fixtures.log. Final invocation uses supported
XNA_SAMPLES_ROOT=/rv/data/development/github.com/libcna/sharp-runtime/build-probe/cna-repair-xna-samples.
Final full summary retained Sharp build-probe/cna-repair-xml-linq-components-final.log.
Both genuineFramework4 and.NET8 LINQ probes74/74. Demo evidence commit859b878.
Sharp worktree clean; two independent watcher stress findings remain unresolved.
Native content build2419 still RUNNING; corpus NUL/null-write checks strengthened
from ANY_THROW to their measured exception categories. All external binaries stay
frozen at the earlier storage revision until final CTest/hash audit. No push/merge.

UPDATE2: both full storage renderer comparators now pass exact55/differences[]:
SW612/56/48/3, GLES651/58/7/3. Regression3249 COMPLETE0. Final serial private
CTest32815 RUNNING, three frozen hashes verified immediately beforehand; no
overlapping SystemLink suite. Permanent pending checkpoint is demo5a3d75b,
docs/repair-evidence/storage/CHECKPOINT.md. Do not close or rebuild before final
CTest and hash audit. Current native build2419 has two pre-existing nodiscard
warnings in unchanged XnaMaterialContentTests.cpp:418/420; source and declarations
are identical to campaign baseline. Record these, do not call this build warning-free.
Actual three-TU content ASan/UBSan compile43959 RUNNING separately, one compiler;
first harness51032 failed because whitespace splitting corrupted quoted compile
database defines, not product compilation. Original log retained; corrected script
uses shell-aware shlex and compiles only until the native library finishes.
Sources/tests are existing product TUs, not emulated replacements. Link/run later.

148 vendor-policy clarification asked asynchronously: permit a narrow documented
tinyxml2 PI-position fix versus keep vendor untouched and use an owned adapter.
No response yet; no vendor edits. Prepared untracked evidence
docs/repair-evidence/content/XmlReaderEnvelopeOracle.cs is NOT run yet (worker cap).
Do not infer its expected checks passed. Other work continues. Next content batch
090/088/089 source inspection confirms callback under non-recursive compiler lock,
uninitialized intermediate serializer registrations and unchecked alias insertion;
no new code/test changes for those IDs, no current standalone reproduction yet.

UPDATE3: native build2419 COMPLETE0 (the two unchanged material-test warnings
above). Focused19 cases initially18PASS/one FAIL: the now-correct null-root WRITE
raises existing modern Sharp text, while the Framework4 corpus uses a parameter
line. Do not modify runtime to manufacture a read failure or rewrite goldens.
Corrected corpus pins both exact diagnostic forms plus type/parameter/timing;
message-only rebuild8525 COMPLETE0 warning-free, final19/19PASS735ms, including
dictionary, whole intermediate corpus and hardening. Native CnaTests/libcna now
FROZEN in repair-content-native-frozen.sha256. Full504 post-fix regression31697
RUNNING (compiler-backed XnaDifferentialBuildTest); no native rebuild until done.
Sharp/native147 NUL and null-write tests are strengthened from ANY_THROW.
Matrix146/147/149 now FIXED_PENDING_REGRESSION, no final disposition/closure.
Actual three-TU sanitizer43959 still compiling its large corpus TU; not stalled
(cc1plus active), max three campaign workers including native full and external
CTest32815. Sanitizer must link after compilation and run before a pass is claimed.
Demo standalone/content.custom.intermediate_serializer/main.cpp now has the two
genuine-backed corrected null-root read checks, unchanged total and independent
write refusal; NOT rebuilt/externally validated yet. Existing148 marker remains.
Uncommitted demo evidence XmlReaderEnvelopeOracle.cs remains unrun.

Native complete content baseline66506 COMPLETE exit1:504 tests/117 suites,
497PASS/one existing XACT fixture SKIP/exactly six expected newly added failures,
1306.990s. Four146 dictionary tests and two149 direct-null tests fail; no other
failure. Reports repair-content-native-before*. Native freeze RELEASED. Prepared
146/149 runtime patches now APPLIED with native tests and public contract docs;
the historical golden manifest is unchanged, but its round-trip test now distinguishes
NullReferenceException(value member) from ArgumentNullException(null root WRITE).
Initial combined146 patch failed context ordering without changing files; applying
its reviewed independent hunks succeeded. Native build2419 RUNNING, parallel1,
repair-content-native-after-build.log. Do not rebuild the external demo tree.

Sharp147 writer focus49/49 passes. First full gate failed an unchanged watcher
barrier assertion; its test-only awaitEvent correction passes20/20 repetitions.
The COMPLETE34-test watcher stress still fails:678PASS/two FAIL of680, other tests
TheDefaultFilterIsCompletelyUNAFFECTED and
Decision5b_AppliesToDeletedAndToBothHalvesOfARename missed Renamed callbacks.
These are uncorrected separate findings, not a green stress claim. Gate2 IO726/726
passes but LINQ347/349 fails because two old NUL expectations pinned XmlException.
Both failed gates and stress logs remain in Sharp build-probe/.

Fresh genuine XLinqNulOracle74/74 establishes content-value ArgumentException;
earlier71/75 disproved the proposed standalone-attribute namespace expectation
(genuine omits URI, this port emits it; URI guard deliberately unchanged).
Reference-backed pre-fix LINQ19 tests7PASS/twelve FAIL. Shared character-data
guard and seven direct serializers now repaired; XML names/metadata keep separate
guards. Post-fix20/20 PASS1ms, warning-free full Sharp build. Initial added namespace
test used reversed XName constructor arguments and failed19/20; corrected to the
documented XName::Get(local,namespace), retained both logs. Full Sharp gate59482
RUNNING with original isolated Yacht fixture. .NET8 LINQ reference83258 RUNNING.
Uncommitted Sharp changes: writer/helper/seven LINQ callers, tests/docs, plus
separate watcher TEST-only synchronization change. Commit separately after gates.
Next: audit Sharp full gate, native focused/content regression, then storage full
GLES/CTest and hashes. No issue closure or inferred152 policy approval yet.

Phase E external validation. CNA runtimecbb77e3c8(Title145),5865ae938(storage151), Sharp58c6cba1 (including
EOF1c0fc9b5,directory551ced42,URI82b57541,NUL3304ecd6); completed external031693a9f/b086814f9(lifecycle141–144),
demo markers61d344e, latest closure804caa1. All155 IDs retained:62 FINAL/93 PENDING:
30 runtime findings,one verified duplicate,23 documentation corrections,seven
already fixed before campaign,one demonstrated demo-expectation defect.

LAST COMPLETE REGRESSION: lifecycle141–144 FIXED_VALIDATED. Native77/77, actual
five-TU sanitizer58/58, consumers1408PASS/four skips, genuine and actual C ABI pass.
Both95math93PASS/two reserved131FAIL. Full719SW609/59/48/3 exact52 changes/differences[];
GLES648/61/7/3 same52 status improvements but strict comparator retains one030
unreliable-outage assertion fluctuation, exit1. Same frozen CTest independently
reproduces both030 failures with24/24 unreliable deliveries versus0/24 in GLES.
Simulator/demo source blobs unchanged since baseline. No comparator relaxation or
network closure; details docs/repair-evidence/lifecycle/CHECKPOINT.md. Metadata/tools/
six exact visuals pass, representative GLES image inspected. Isolated full CTest
35298 COMPLETE:732accepted/zero unexpected failures,48same skips/three disabled,
911.46s. Three hashes reverified. External freeze RELEASED after closure commits.
Complete1128 demo-status delta54, campaign HEAD delta52. No push/merge.

ACTIVE: Title145 committedcbb77e3c8, matrixFIXED_PENDING_REGRESSION. Complete external
storage/title build63979 COMPLETE0, warning-free, repair-storage-demo-build.log.
Focused16529 COMPLETE0: ten scenarios per renderer,9PASS/one unchanged152FAIL.
Title10/lifetime14 checks retained; actual C save child38/38 on both renderers.
Both DSOs still match the earlier actual C ABI hash manifest. Only then removed
056/145/151 markers (no assertion edits) and corrected stale Disposing comment.
Marker rebuild65070 COMPLETE0 warning-free; demo marker commitde6bcde. External
executable and both DSOs FROZEN in repair-storage-frozen.sha256; C ABI DSO hashes
still match earlier validation. Three registered CTests verified without WILL_FAIL.
Full regression3249 RUNNING: both95math exact48 improvements/differences[], metadata/
tools PASS, all six visual references pixel-exact; representative GLES texture inspected.
Full SOFTWARE started; GLES follows independently even if strict comparison finds
a delta. After both smokes finish, run full serial isolated CTest with no SystemLink
overlap, then reverify all three hashes before closure. Do not rebuild external tree.
Actual C ABI69669 COMPLETE: strict C17/Werror RuntimeGameSmoke, StorageSmoke and
TitleCValidationProbe PASS (independent title13/13 versus pre-fix8/13). ldd proves
the actual external C ABI DSO; both libraries' hashes unchanged during execution.
Reports repair-storage-c-after-*, repair-storage-c-libraries*. Fresh isolated run
directory build-probe/repair-storage-c-work retained. Next verify marker build,
freeze executable/both DSOs and run full math/comparison/visual/isolated CTest gates.

Native Title145 final repaired checkpoint COMPLETE. Sharp status repair58c6cba1 built warning-free and full gate95127
passes18144/18144 across41 executables,zero failures/skips. Native build1322
COMPLETE warning-free; focused17/17PASS53ms. Actual TitleContainer.cpp+FileStream.cpp
and13 regression tests pass ASan/UBSan/leak52ms. Corrected sanitizer62682 links
the real required Sharp archives. Android branch syntax-only check2420 passes;
not an NDK cross-build or Android runtime claim. Broad5832 COMPLETE: exact1412
consumers1408PASS/four same skips168.769s; runtime241total238PASS/three same skips
12.101s. Both native hashes reverified; native freeze RELEASED. C API post-fix
execution now passes above; pre-fix independent13checks/five failures retained.
145 runtime/tests/contracts committedcbb77e3c8, matrixFIXED_PENDING_REGRESSION.
No external storage closure yet. Content146 preparation found key-validation ordering; the
first fresh pipeline oracle failed to load its assembly, not a behavior result.
Its owned probe1349449/debugger1349541 were identified and terminated; wrapper
39023 exits143 and logs remain. Do not shadow all other probes with framework DLLs.
Corrected isolated staging run94075 passes14/14 genuine XNA4 checks. Three symlinked
original SDK assemblies live ONLY in build-probe/repair-content-reference; no shared
probe-root DLL shadowing. Permanent OpaqueDictionaryOracle.cs is uncommitted in
demo docs/repair-evidence/content/. Native test-only build6392 completes; three new
OpaqueDictionaryCompatibilityTest cases give1PASS/two expectedFAIL against unchanged
production (3ms): null-value exact type and empty-key precedence; positive insertion/
replacement ordering passes. Reports repair-opaque-native-before{,-build}.log.
New native test file untracked. Expanded hook oracle7/7 genuine, native expanded
five tests1PASS/four expectedFAIL7ms (build/run1861 COMPLETE). Prepared runtime patch
demo build-probe/repair-opaque-runtime.patch UNAPPLIED. Issue146 CONFIRMED_OPEN.
147 default XML NUL oracle21/21 on Framework4 and.NET8; optional CheckCharacters=false
diagnostics explicitly separate. Sharp test-only90856 COMPLETE:49tests46PASS/three
expectedFAIL2ms, zero build warnings. Production unchanged. Issue147 DEPENDENCY_DEFECT/
CONFIRMED_OPEN. Preserve default rejection and useful diagnostic; do not claim the
optional false mode works through tinyxml2. Reports repair-xml-nul-{xna,net8}* and
Sharp build-probe/cna-repair-xml-nul-before*. Both new witnesses are included in
docs/repair-evidence/content/README.md. No inferred152 policy decision.

148/149 fresh genuine IntermediateEnvelopeOracle16/16 confirms envelope contracts
but CORRECTS the historical149 root-read expectation: Deserialize returns null;
only subsequent Serialize throws ArgumentNullException(value). Old corpus Accept<T>
combines both calls in one catch; its rejected manifest describes a round trip,
not a failed read. Primary issue inventory now retains history with this correction.
Value-null genuinely throws NullReferenceException. Native test-only34744 COMPLETE:
both new direct149 tests FAIL before repair (15ms); positive Null=false control passes.
Reports repair-intermediate-envelope-{direct,asserted}-xna*,
repair-intermediate-null-native-before*. First genuine setup used invalid empty
relocation (ArgumentNull path), retained as repair-intermediate-envelope-xna* exit143;
only owned probe1396356/debugger1396535 stopped. Corrected reference uses null relocation
and a setup exception boundary. No production content edits, no golden regeneration.
Additional uncommitted native file: XnaIntermediateSerializerTests.cpp (two tests/includes).
Sharp uncommitted XmlWriterValidationTests.cpp contains evidence-backed test changes
and one new pre-write-preservation test; actual writer unchanged. Both prepared runtime
patches repair-opaque-runtime.patch and repair-xml-nul-runtime.patch remain UNAPPLIED.
Do not alter production until external storage marker rebuild finishes and is frozen.

ACTIVE native content baseline66506 runs ALL suites discovered from content-pipeline
test sources in the existing CnaTests, private display; reports repair-content-native-
before{,-driver,-inventory.json,.xml}. It includes the slow compiler-backed
XnaDifferentialBuildTest (~12min historically). Six newly added pre-fix failures are
expected (four146, two149); do not hide any other failure. Total504 tests from117 suites.
Native CnaTests/libcna binaries and content dictionary/serializer headers MUST remain
unchanged until this run ends. After external freeze, narrow Sharp147 NUL exception
repair APPLIED; no signature/layout change. Its XmlWriter.hpp edit documents the
existing unconditional-NUL limitation and new exception only, no executable header
change. Actual CNA native/external binaries still embed pre-147 Sharp code.
Sharp full build/focused91375 COMPLETE0 warning-free:49/49 tests3ms (before46/49).
Full Sharp component gate43386 FAILED in unchanged IO watcher test:725PASS/one FAIL
of726. Original preserved cna-repair-xml-nul-components-first-failed.log. Fixed20-run
diagnostic of unchanged single test gives18PASS/two FAIL, both immediate Deleted
lookup after sentinel. Source proves pending rename Deleted is dispatched AFTER
the batch, so sentinel is not a callback barrier. Test-only correction awaits the
actual named Deleted/Created through existing condition variable/deadline, preserving
all types/names/negative assertions, no sleeps/retries. Production watcher unchanged.
Build/20 complete34-test suite repetitions66559 RUNNING, one worker; then rerun full
normal gate after this real test repair, retaining first failure. New sibling files
IONamespaceReviewTests.cpp and docs/Testing-WatcherRenameBarrier.md must be committed
separately from147 after successful full gate. Other jobs native baseline66506 and
external regression3249; three campaign workers total. XML repair not committed yet.
Sharp files: XmlWriter.cpp, XmlWriter.hpp, XmlWriterValidationTests.cpp,
docs/Migration-XmlWriter-NulException.md; all uncommitted until warning-free full gate.
Opaque runtime patch still UNAPPLIED until native content baseline completes.
Prepared repair-intermediate-null-runtime.patch is also UNAPPLIED; review and add
the exact corpus-exception branches/docs with149, preserving original golden files.

Historical intermediate NUL/status failures (now repaired above): review exposed
actual wrong-file access/truncation in FileStream. Sharp3304ecd6 now rejects NUL
and empty input before filesystem effects; genuine54/54 and dependency18143/18143
pass. Genuine title witness expands to30/30. CNA now opens through FileStream
without a masking existence check (Android falls back only for genuinely missing
paths). Native93477 build COMPLETE warning-free, but focused17 has16PASS/one
length-categoryFAIL: FileStream discards stat errors and reports overlong paths
as missing. Broad80437 retains that failure; do not call this checkpoint green.
Expanded sanitizer40208 failed to link File helpers, NOT a runtime sanitizer run;
corrected builder now links the actual required Sharp archives and awaits rerun.
Sharp status-error repair is UNCOMMITTED: preserve errno-derived categories, new
18-case regression fails before repair. Build83227 RUNNING. Full gate still needed.
Genuine long-path hypothesis initially71/72; actual Framework4 Append/ReadWrite
checks full-path length before its late append-access guard. Explicit versioned
Framework4 mode72/72 and actual.NET8.0.31 mode72/72 record that difference; Sharp
keeps current .NET argument precedence. Original failed logs retained. No152 policy
decision is inferred. Next finish Sharp gate/commit, rebuild native, rerun17 focus,
two-TU13-test sanitizer and exact consumers/runtime before145 commit/external build.

Previous pre-NUL native Title145: initial68183 and final37740 target builds COMPLETE
warning-free; final16/16 title/storage focus55ms. Actual TitleContainer.cpp ASan/
UBSan/leak6/6PASS36ms; strict C smoke syntax compilePASS. Broader10391 COMPLETE:
exact1412consumers1408PASS/four same skips; all240runtime237PASS/three same skips
11.599s (previous three Title failures now pass, one new length test passes).
Native hashes reverified, native freeze RELEASED. Product/C adapter/docs/tests
UNCOMMITTED pending review and commit; external C ABI probe13checks/five pre-fix
failures retained. Genuine28/28; actual GAC IL corrects the decompiled source's
incorrect ArgumentException missing-file branch. OldFramework4 length65520 UTF16
guard is now included and pinned for ASCII/astral input; Sharp modern Uri stays
unbounded. Next: review embedded-NUL lower-path handling, commit coherent145 repair,
then rebuild actual external C ABI/demos for056/145/151 focused/full regression.
FileShare152 policy question still pending; no implementation assumed.

PREVIOUS COMPLETE REGRESSION: Color132 FIXED_VALIDATED, runtime1ad0a3916.
Native966PASS/two same skips;registered70/70;exact1412consumers1408PASS/four skips;
actual Color.cpp ASan/UBSan/float-cast-overflow/leak64/64. Actual C ABI ColorSmoke6
stages pass; all6 focused demos per rendererPASS, original7/9 checks retained.
Both95math93PASS/two reserved131FAIL. Full719SW605/63/48/3,GLES644/65/7/3,exact48
cumulative intended changes,differences[]. Metadata719/0,tools7+4+5,six exact visuals;
representative GLES capture inspected. Isolated full CTest732accepted/zero failures/
48skips/three disabled983.90s; executable/both DSOs SHA256 reverified afterward.
Evidence ../cna-demos/docs/repair-evidence/issue-132/CHECKPOINT.md. Complete1128
demo-status delta50,current-HEAD campaign delta48. External Color freeze RELEASED.
Owner-reserved131 remains open; outside-entry ray method/exception untouched.

Historical lifecycle build/run details (superseded by completed checkpoint above):
full external regression22718 COMPLETE with exit1, binaries then FROZEN after
warning-free initial89883 and marker36933 builds. Demos61d344e preserves genuine
adjacent oracles with4/20/15 assertion calls. All20 focused scenarios per renderer
give18PASS/two unchanged050/051FAIL; repaired four retain5/8/11/8 checks.
Only then four knownIssue/restriction markers removed. Actual C ABI smokes allPASS
(independent disposal11/11), library hashes unchanged through marker rebuild.
Manifest ../cna-demos/build/linux-full/reports/repair-lifecycle-frozen.sha256 pins
the executable and both DSOs. Both95 math sweeps exact48 changes/differences[];
metadata/tools/six pixel-exact visuals pass; GLES texture image also inspected.
Full719SW609/59/48/3 has exact52 intended changes/differences[]. GLES648/61/7/3
has the same52 status improvements but one retained strict-comparator difference:
net.network_session.simulated_conditions no longer fails its unreliable-outage
assertion in this run (latency still fails). Original comparator/logs retain exit1;
do not relabel the sweep exact or close030. The receiver-side demo configuration
and non-guaranteed unreliable delivery were already identified before this batch;
finish the bounded delta investigation separately, without relaxing assertions.
Isolated serial full CTest35298 RUNNING, repair-lifecycle-demo-ctest.log. No other
SystemLink run may overlap. Then reverify hashes before closing141–144. Do NOT rebuild
build/linux-full until that checkpoint finishes. No SystemLink overlap.

AFTER FREEZE: Sharp056 minimal Read EOF-state fix committed1c0fc9b5 with three tests
and migration note; full existing build70708 COMPLETE warning-free. Post-fix full
component gate16272 COMPLETE:18140/18140 across41 executables,zero failures/skips;
private Yacht fixture stopped/removed and original sample assertions unchanged.
CNA151 shared disposed guard now native-validated in5865ae938, with four tests and corrected
genuine-backed C StorageSmoke event-access oracle (backing-file preservation kept).
Full build29581 deliberately interrupted at212/1051 to prioritize the affected
target;17 warnings all exactly in original152-warning baseline,none new. Target
CnaTests build66318 COMPLETE warning-free. Native18/18 storagePASS(10ms); actual
rebuilt-library EOF13/13; production-TU ASan/UBSan/leakEOF3/3 andstorage4/4PASS.
Broad53372 COMPLETE: exact1412consumers1408PASS/four same skips272.553s; all239
runtime tests233PASS/three same skips/three still-open Title145FAIL11.092s. The
driver retains exit1 and those failures; no new regression or suppressed test.
Native executable/DSO hashes reverified unchanged; native freeze released.
Actual external C ABI storage validation still pending; modified C smoke compiles
strict C17/Werror. Title145 has
five prepared tests,2PASS/three intendedFAIL; do not include those expected failures
in an asserted-green storage gate or close145. FileShare152 policy question pending.

Completed Color build details: runtime1ad0a3916. External build
and marker rebuild COMPLETE warning-free; strict actual C ABI ColorSmoke and all6
focused scenarios per renderer PASS. Two markers removed, original7/9 checks remain.
Color executable/C++/C ABI hashes recorded repair-color-frozen*, now freeze released.
Full driver79621 COMPLETE: both math sweeps
and both full719-ID sweeps exact48 changes/differences[]; SOFTWARE605/63/48/3,
OPENGLES3 644/65/7/3; tools and six exact visuals pass. Frozen hashes reverified.
Full isolated CTest55689 COMPLETE, repair-color-demo-ctest.log,983.90s.
Do not overlap SystemLink suites. Lifecycle production is native-validated
separately in native tree. Final hook companion build96698 COMPLETE warning-free;
77/77 focusedPASS(4.770s), actual five-TU ASan/UBSan/leak58/58PASS(1.771s).
Repeated broad native consumer job76785 COMPLETE: exact1412consumers1408PASS/four
unchanged skips256.700s; all33 runtime suites234total231PASS/three same skips15.440s.
Native executable/DSO repair-lifecycle-hook-native-frozen.sha256 verified unchanged;
native freeze released. External Color freeze also released after full checkpoint.
Color native already complete: genuine/native1782/1782; math966/two skips;
registered70/70; actual Color.cpp ASan/UBSan/float-cast-overflow/leak64/64;
exact1412 consumers1408PASS/four same skips(282.277s). External closure now complete.
Prepared drivers build-probe/repair-color-{c-smoke,focused,math,regression}.mjs;
exact comparators admit only48 cumulative changes(two Color demos additional).

COMMITTED CNA: component root-cause batch031693a9f and launch batchb086814f9,
including matching regressions and C contracts/smokes. All four matrix rows now
FIXED_PENDING_REGRESSION; actual C ABI smokes pass, full external closure pending.
Status commitc01e04275; lifecycle evidence8b38d2a, storage sharing edges2129f3f.
Latest storage evidence1d0f897, lifecycle demos61d344e. Repair5865ae938 commits its
production guard/header/C StorageSmoke oracle, four native tests and measured
compatibility decision. Sharp056 committed1c0fc9b5. Remaining UNCOMMITTED:
TitleContainerPathTests.cpp (five pre-fix tests only), current matrix/handoff updates.
Sharp directory prerequisite for145 committed551ced42: minimal directory
classification, one18-case regression, migration and changelog. Genuine sharing
oracle36/36PASS; native pre-fixFAIL retained. Build33779 warning-free and full gate
50969 passes18141/18141 across41 executables,zero failures/skips. URI prerequisite
committed82b57541: empty/whitespace relative references accepted under allowed
kinds,OriginalString preserved. Genuine36-outcome diagnostic; new matrix fails
against committed pre-fix Uri.cpp compiled into separate object. Initial test-only
compiler failures retained, not claimed as runtime failures. Build80011 warning-free;
fullgate68746 passes18142/18142,zero failures/skips. Neither changes public layout.
Title145 production/C adapter/docs/new tests now UNCOMMITTED; native target build
68183 RUNNING. Path cleaning faithfully precedes validation; private Android loader
and case-insensitive lookup retained. Native absolute-path acceptance oracle corrected
against genuine evidence, not weakened. External binaries still frozen. Need native
focus/broad/sanitizer and actual C ABI/external gates before commit/closure. New
genuine diagnostics identify old Framework4 relative-URI length limit65520 UTF16
units and an ArgumentException-inner translation discrepancy with decompiled source;
investigate before claiming full145 compatibility. Current Title implementation has
no old Framework4 length-limit guard yet. FileShare152 policy still pending.
Native storage before repair: one live control passes,
three disposed/event groups fail. Copied151 candidate passes all4. Native CnaTests
rebuild/focused/broad validation now complete as above; actual external storage
validation must await lifecycle closure.
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
without touching then-frozen external DSOs; actual C runtime now passes as above.
NEW companion: owned property callback can remove the last reference before the
derived hook returns. New all-four-properties test fails while prior57pass
(repair-property-hook-ownership-before*,1.377s). Local private ownership helper now
pins through all four setters and Dispose; no public layout/vtable change. Final
77-test focus and58-test sanitizer pass; final broad consumers also pass as above.
Genuine lifecycle34+8+3+6 checks pass; pre-fix actual C ABI disposal11checks/seven passes/
four failures now becomes11/11PASS. Existing Game19/19 controls pass. Derived explicit disposal always
invokes UnloadContent before event, even without Initialize; false path silent.
Collection Clear uses live-index iteration, not snapshot/fail-fast. Game.Dispose
requires an owning original snapshot. Keep owned references alive across the entire
virtual component Dispose, nested removal, exceptions and re-registration.
See ../cna-demos/docs/repair-evidence/lifecycle/ and prepared sanitizer builder.
Three adjacent demo oracles are now corrected against genuine evidence, awaiting
the focused/full external run. drawable_game_component requires two UnloadContent
calls after two explicit Dispose calls (four assertions retained).
standalone/c_api.game.launch_parameters/main.c retains bare/equals-only flags as
empty values and requires duplicate Add INVALID_ARGUMENT; sorted names/counts
expand from3 to5 initially (6 after Alpha). All15 assertions and UTF8/guard/atomicity
controls remain. Native RuntimeGameSmoke now passes the actual C ABI too.
c_api.game.component_members requires two callbacks/events after two disposals;
all20 assertions and the single drawable-disposal check remain. No primary
knownIssue marker has yet been removed.

Pre-fix storage056/151/152 confirmed at correct lower layer; subsequent056/151
production changes are recorded in ACTIVE above. Title145 has five native tests:
strict warning-free build, two controls pass/three argument/directory/normalization
groups fail(117ms); title production unchanged. Genuine32-path diagnostic retained.
Actual FileShare platform policy question sent asynchronously: Windows reference
25 opens/119 refusals versus Unix81/63. No default or oracle relaxation applied;
continue independent repairs while awaiting that compatibility decision.
Genuine EOF13/13 versus seven native failures; genuine storage172/172 versus
native170checks29PASS/141FAIL (two FileStream share checks cannot compile yet).
Native disproves share enforcement and disposed guards, including real mutations
of isolated fixtures. Container shorter defaults are exclusive,Append write-only;
direct FileStream defaults read sharing. New genuine sharing-edge15/15 checks prove
denied Create/Truncate preserves bytes and exclusive locks reach a child process.
Native direct write-only Open/OpenOrCreate loses existing data:4/4FAIL; preserve
these controls in the same lower-layer open repair. Evidence2129f3f.
Sharp baseline build8883 COMPLETE warning-free, existing build/Debug,--parallel2,
11 reported links. Initial full gate46798 FAILED solely on five absent default-root
XML serialization XNA sample fixtures; no test failures. Original samples exist.
Initial log preserved build-probe/cna-repair-storage-baseline-components-missing-fixtures.log.
Supported XNA_SAMPLES_ROOT now points at build-probe/cna-repair-xna-samples, containing
only links to original ShipGame/Spacewar sources; no test/sample edits or skip allowance.
Corrected full component gate82082 COMPLETE:18137/18137 across41 executables,zero
failures/zero skips. Original Yacht SOAP fixture on private loopback stopped and
temporary executable/config removed. Sharp HEAD remainsf2aecfef. AFTER baseline,
uncommitted native FileStreamEofTests.cpp added: warning-free IO-target build19156,
all three expectedFAIL on unchanged production. No dependency runtime edits.
Linux FileStream pre-layout576/8,Stream8/8,copy/move bothfalse; MinGW Win64 static
layout536/8,Stream8/8,warning-free. Existing32-bit Wine prefix rejected Win64 EXE;
failure retained, no Win64 runtime claim/new prefix. Isolated EOF candidate now
passes3/3 new and723/723 total IO tests(2.233s); corrected static linkage uses the
actual IO target dependencies. Initial missing File symbols/missing compile database
harness failures retained. Candidate clears only benign EOF/fail state, preserving
badbit/non-EOF failures. Initial isolated151 shared-guard candidate passes3/3 new
and17/17 native storage controls(10ms), then expanded4/4 with genuine7/7 event
contracts(2ms), no API/layout change. These are pre-production candidate results;
actual056/151 files are now changed AFTER lifecycle freeze. Color complete.
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
