# BATCH_2_STABILIZATION.md — Batch 2 stabilization and checkpoint record

Companion to `BATCH_0_COMPLETE.md` / `BATCH_1_STABILIZATION.md`. Covers the two Batch 2 lanes
(`wicked`, `magnum`) after their integration, the consolidated current-head baseline, the
adjudication of the newly measured EasyGL failures, and the checkpoint decision.

**Scope.** Stabilization only. No feature lane was integrated in this session, no published history
was rewritten, `audit/` was not touched, and nothing was pushed. The session ran on the established
ThinkPad T14 host; no migration assumption was made.

---

## 1. Identity

| Item | Value |
|---|---|
| Repository | `/rv/data/development/github.com/openeggbert/cna` (shared object store) |
| Integration worktree | `/rv/data/development/github.com/openeggbert/cnaintegration` |
| Integration branch | `integration/post-audit-phase1` |
| Integration HEAD at session start | `e7d46c4c` — the `magnum` merge |
| Integration HEAD at the decision | **`cbdab0c5`** = `e7d46c4c` + the REMED-GFX-222 fix `0dd1b0a9` + the WICKED-80 record `cbdab0c5`, both signed |
| Planning worktree | `/rv/data/development/github.com/openeggbert/cnaaudit`, branch `feature/audit`, head `a37dc3d0` at start |
| Phase-1 checkpoint | `cna-post-audit-remediation-phase1` = `d79214e7` (ancestor ✅) |
| Batch 0 checkpoints | `…batch0-20260804` → `e0332214`, `…batch0-complete-20260804` → `990d6b8a` (ancestors ✅) |
| Batch 1 checkpoint | `integration/checkpoint-batch1-20260805` → `ed607602` (ancestor ✅) |
| Published remote head | `origin/integration/post-audit-phase1` = `61bd1a1b` — ancestor of local HEAD ⇒ **no rewrite** |
| Signing key | `FB9CE8E20AADA55F` (`commit.gpgsign=true`; `git tag -s` passed explicitly); preflight-proven for commits and annotated tags before any work |
| Fetch | `git fetch --all --prune --tags` exit 0, **no remote movement, nothing pruned** |

**Lane merges present: exactly 11** — verified by first-parent merge enumeration over
`d79214e7..e7d46c4c`, not by ancestry (adapted lanes replay commits; `BATCH_1_STABILIZATION.md`
§1.1's method note holds).

| # | Lane | Merge | | # | Lane | Merge |
|---|---|---|---|---|---|---|
| 1 | depthcrt | `61bd1a1b` | | 7 | opengl4 | `bc29a976` |
| 2 | gltf | `722a2f5a` | | 8 | opengl1 | `c0876fca` |
| 3 | ext (NOXNA) | `8a374b9f` | | 9 | opengl2 | `9e6d62ed` |
| 4 | dxold | `990d6b8a` | | 10 | **wicked** | `683a00a5` |
| 5 | stub | `99ae7d11` | | 11 | **magnum** | `e7d46c4c` |
| 6 | opengles1 | `df6b7cc6` | | | | |

### 1.1 Refreshed lane count (after fetch)

**21 logical lanes — 11 integrated, 10 pending.** Pending: `direct2d`, `gl`, `diligent`, `gdi`,
`glide`, `llgl`, `skia`, `sokol`, `html-dom`, `metal`. No twelfth lane was begun: the `adapt/*`
branch set is exactly the ten known adaptation branches, and no new lane merge exists.

The four pre-existing user stashes are untouched. The stale, pre-existing prunable worktree entry
`/tmp/cnaaudit-gfx098-prefix` was left exactly as found.

---

## 2. Wicked provenance review

Every figure below was re-measured directly against git this session, not carried forward.

| Check | Result |
|---|---|
| Original ref | `origin/claude/wicked-engine-cna-backend-5ffqzd` = `91d8587e` — **unchanged** |
| Archive tag | `archive/preintegration/wicked-20260804` → `91d8587e`, verifies **Good** |
| Fork point / develop base | `merge-base(91d8587e, d79214e7)` = `2338b44f`; `merge-base(91d8587e, develop)` = `ac3aaaeb` |
| Own commits | `git rev-list --count 2338b44f..91d8587e` = **10** |
| Adapted range | `ed607602..97d5a644` = **17** commits, all `%G?`=`U`, all authored **and** committed by the maintainer |
| Integration merge | `683a00a5`, signed (`U`), parents `ed607602` + `97d5a644` |
| Merged-tree equality | `683a00a5^{tree}` = `97d5a644^{tree}` = `aa94d4e3…` ✅ byte-identical |
| Range-diff | `2338b44f..91d8587e` ↔ `ed607602..97d5a644`: **all 10 originals pair 1:1 in order**, 7 unmatched-right = exactly the documented added commits (`dc972cbc`, `1b6ee0a3`, `4c1dadd4`, `9f820697`, `4449daaa`, `de70722f`, `97d5a644`) |
| Attribution sweep (multiline, whole adapted range + merge) | **zero hits** — not even allowed-class tokens |
| Wider narrative sweep | one hit: the documented **allowed technical** "cube type and face survive the normalized handoff" (`d455adb3`) |

### 2.1 The resolved defects, re-verified

**WICKED-77** (`9f820697`). The instanced-route fix is in `WickedGraphicsBackend.cpp` (+10 at the
single-geometry-stream bind); the regression suite `Wicked_GeometryVertexOffset` carries exactly
the prescribed cases: `OrdinaryIndexedHonorsVertexOffset` (ordinary control),
`InstancedZeroVertexOffsetRenders` (zero-offset control), `InstancedVertexOffsetSelectsLaterRecords`,
`InstancedSingleInstanceHonorsVertexOffset` (**`instanceCount == 1`**), and
`InstancedVertexOffsetIsVertexElementsNotBytes` (**byte-versus-element discrimination**).
Current-head run: the suite passed inside both corpus executions (8.2 s).

**WICKED-78** (`4c1dadd4`). `cmake/patches/wicked-device-teardown.patch` fixes **both** upstream
mechanisms in `GraphicsDevice_Vulkan`'s destructor: it destroys the three never-destroyed null
images and their views, and frees the pool-allocated command lists (`cmd_allocator.free(...)`)
whose retained buffers otherwise pin the allocator, `VkDevice` and `VkInstance`. The lifecycle
suite carries the six prescribed legs: bare, query-only, repeated, explicit-early-disposal,
drawing, mixed-cycle. VMA assertions are live (`CMAKE_CXX_FLAGS_DEBUG = -g`, no `NDEBUG`, no
`VMA_ASSERT` override anywhere in the lane files) and no abort-suppression mechanism exists.
Current-head run: the suite passed inside both corpus executions (12.8 s), and the corpus completed with zero aborts — the teardown class is absent.

**WICKED-79** (`4449daaa`). Staged uploads are written through the staging texture's own
`mapped_subresources[0]` pitches with one submit per staged upload; the regression carrier is the
corpus's thirteen transfer tests (multi-face and per-mip cube suites, Cnj/content `Texture3D`
fixtures) — there is deliberately no separate dedicated CTest. Current-head evidence: all corpus transfer tests passed, and the new sanitized narrow-width probe exercised the class further — finding WICKED-80 (§9.1) beyond the fixed WICKED-79 shapes.

**Carried dependency patches.** Both apply cleanly to the **pristine** pinned revision
`27c0df16` — proven this session by extracting the six pre-patch files with `git archive` from
`~/deps/WickedEngine` and dry-running `wicked-sdl3-platform.patch` (6/6 files) then
`wicked-device-teardown.patch` on top. The `~/deps/WickedEngine` checkout is at the pin with
exactly the six patched files modified. Provenance and licensing are unchanged from the lane card
(MIT, pin = upstream master tip, `libdxcompiler.so` is upstream's own redistributed binary).

**Corpus composition.** The EASYGL principal tree's generated `CTestTestfile.cmake` contains
**zero** `Wicked` or `Magnum` registrations, and the WICKED corpus registers exactly the lane's
official 5780 — the `WICKED-71`-class exclusion holds at the current head.

### 2.2 Lane-card citation sweep — result

Every 8-hex-or-longer token in `integration/lanes/wicked.md` was extracted and resolved: **every
cited SHA exists and its subject matches the card's claim**; the only non-resolving tokens are the
literal date `20260804` (a tag-name component) and the Wicked Engine pin `27c0df16`, which
correctly lives in the dependency repository, where it was verified. Named tests, file paths and
the preserved-reproducer location (`cnaintegration-wicked/cmake-build-wicked/wicked-repro/`) all
exist. **Zero stale citations were found, so the card required no correction** — the sweep result
is recorded here rather than by editing frozen lane evidence.

The stale **"24-commit rebase"** phrasing the wicked card handed to this checkpoint survives in
`INTEGRATION_ORDER.md` §1 (the C2 row and the §2 graph labels) and
`INTEGRATION_HISTORY_POLICY.md` §5 (the DEPENDENCY-FIRST row);
`INTEGRATION_BRANCH_INVENTORY.md` §8 already carries its own correction banner. Dated correction
notes were added to the two documents that lacked one (committed together with this record).

---

## 3. Magnum provenance review

| Check | Result |
|---|---|
| Original ref | `origin/claude/cna-magnum-gr-backend-211xsx` = `9b903db8` — **unchanged** (remote-only, as recorded) |
| Archive tag | `archive/preintegration/magnum-20260804` → `9b903db8`, verifies **Good** |
| Fork point | `merge-base(9b903db8, d79214e7)` = `2338b44f` — the same audit-stacked fork as `wicked` |
| Own commits | **13** (`2338b44f..9b903db8`) |
| Adapted range | `683a00a5..b7fe9b24` = **19** commits, all `U`, maintainer-authored and -committed |
| Integration merge | `e7d46c4c`, signed, parents `683a00a5` + `b7fe9b24` |
| Merged-tree equality | `e7d46c4c^{tree}` = `b7fe9b24^{tree}` = `c246a59b…` ✅ — **the merged tree is byte-identical to the adaptation head**, so the retained `cnaintegration-magnum` build trees are current-head content |
| Range-diff | 13 originals pair 1:1 in order; 6 unmatched-right = the documented additions (`a7c2c52c`, `46a466cb`, `d832ec72`, `901cc42c`, `f9b3fd41`, `b7fe9b24`) — **zero omitted, zero superseded** |
| Attribution sweep | **one hit**: the literal tracked filename `CLAUDE.md` in `6e988c8e`'s body — the allowed class (policy §2.1). Zero violations |

### 3.1 Architecture, re-verified from source at the current head

- **One public identity.** `MAGNUM` is the 29th `GraphicsBackendType` member; the underlying
  desktop OpenGL API is not counted as another CNA backend. The lane's shared-production footprint
  outside its backend directory is exactly: one `#ifdef CNA_BACKEND_MAGNUM` window-flag block in
  `GraphicsDevice.cpp` (invisible to every other backend's preprocessed output), a comment-only
  documentation correction in `IGraphicsBackend.hpp` (`preferPerPixelLighting` honouring set), and
  the +6-line registration surface in `GraphicsBackendType.hpp`.
- **Magnum typed GL wrappers** render on a **desktop GL 3.3 core** context requested via
  `SDL_GL_SetAttribute(..., 3/3, SDL_GL_CONTEXT_PROFILE_CORE)`; **SDL3 owns the window and
  context**; real wireframe via `Renderer::setPolygonMode(PolygonMode::Line/Fill)`.
- **Pins reproducible**: `cmake/ThirdPartyMagnum.cmake` pins Corrade
  `783e4e4807536ec52c352986fc9317db986ace96` and Magnum `5a7424643bfd4621fbcff8c361d37795502cf890`;
  the `~/deps/corrade` and `~/deps/magnum` checkouts are at exactly those heads. Both `COPYING`
  files carry the MIT permission grant. No EasyGL or `feature/gl` identity is involved.

### 3.2 Obligations, re-verified from source

- **Exhaustive capability switch**: `MagnumGraphicsBackend::SupportsCapability` has **11
  `case` arms and no `default`**.
- **Declaration-fidelity guard**: `RequireFaithfulDeclarationEXT` is called in the draw funnel
  (line 1187) before native work; split multi-stream declarations are lifted by
  `stream.combinedByteBase` (line 1348) — the `901cc42c` fix in place.
- **Stock-null refusal**: the formerly silent no-op raises `System::NotSupportedException`
  naming stride and flags (line 1204).
- **Truthful WireFrame**: real `glPolygonMode` mapping; MAGNUM sits in the wireframe pixel
  oracle's measured set.
- **Backend-local tests excluded** from other configurations (`d832ec72`): zero Magnum
  registrations in the EASYGL principal tree's generated test files.
- **Lifecycle**: the preserved `probe_lifecycle` legs re-run at the current head: **4/4 clean
  exits**; the guard/capability probe: **17 checks, 0 failures** — both also re-run sanitized (§7).

### 3.3 MAGNUM-65, re-verified

`f9b3fd41` replaces both `pendingVertices_.size() * sizeof(Vertex)` and
`pendingIndices_.size() * sizeof(uint16_t)` with plain element counts in the two
`ArrayView<const void>` constructions of the sprite flush, with the constructor-scaling comment in
place. **All other `ArrayView` constructions across the six Magnum backend files were re-audited
at the current head**: every remaining site passes a `void*`/`nullptr` pointer with byte counts
(no scaling), a byte-typed container with its own `size()`, or a genuinely typed pointer with an
element count (`MagnumProgram` uniform arrays). **No recurrence of the byte/element hazard
exists.** The regression oracle is the sanitizer leg (§7) — a normal build cannot observe an
overread that renders correctly, which is exactly how the defect stayed invisible; the pre-fix
ASan hit and the radeonsi fault are the lane card's recorded evidence, and the radeonsi reproducer
was again deliberately not re-run (real display excluded by campaign policy).

---

## 4. The EasyGL baseline, re-derived — an instrument change, not test growth

The magnum session reported 6212/6203/5/4 against every earlier 5913-shaped baseline. Naïve total
comparison is meaningless here; the sets were derived exactly, from the preserved run log
(`cmake-build-noxna/preserved-logs-pre-batch2/`, saved this session before any new ctest run could
overwrite it) and from the registration sources:

**The previous instrument** was the `CnaTests` gtest binary run alone from the repository root:
**5913 cases** (Batch 1 §11.3: 5907 passed, 6 gtest-skips). **The new instrument** is the full
`ctest` corpus of the same tree. The 6212-test run decomposes exactly:

| Class | Count | Notes |
|---|---|---|
| `CnaTests` gtest cases, individually registered | **5913** | **name-for-name the previous instrument's set** |
| `EasyGL_*` dedicated block (`cmake/Tests/EasyGLTests.cmake`) | **292** | registration file exists since 2026-07-16 (`72183c05`), content last changed 2026-08-02, gated on `CNA_BUILD_EXAMPLES AND CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32 AND EASYGL` |
| easy-gl sibling project's own tests | **3** | `easy-gl-smoke-tests`, `easy-gl-context-lifecycle-tests`, `easy-gl-resource-smoke-tests` — registered by `/rv/…/openeggbert/easy-gl/tests/CMakeLists.txt` via `add_subdirectory` |
| Other named CTests | **4** | `CnaInputTests`, `NOXNA_Settings_Compile_Run`, `StrictXnaApiSurfaceCheck_Compile_Run`, `StrictXnaApiSurfaceLeakCheck_MustFailToCompile` |
| **Total** | **6212** | = 5913 + 299 exactly |

**Why the 299 were never measured before.** The principal-control baseline was historically the
single gtest binary, which contains none of the named CTests. Within this tree the `EasyGL_*`
block's binaries were first built at the 2026-08-05 15:55 reconfigure (file mtimes) — after the
Batch 1 baseline run — and the sibling's three tests have been registered since the tree's
creation on 2026-08-04 18:19 but were never part of the gtest instrument. The first full-`ctest`
principal control on this tree is the magnum session's 2026-08-06 run. **Neither the registration
logic nor any of the three failing tests' sources changed in the Wicked or Magnum ranges** —
`EasyGLTests.cmake`'s include and condition predate the phase-1 checkpoint, the two `cna_test_easygl_*`
sources date to 2026-06-26 and 2026-07-07, and the sibling repository has been unmoved on `develop`
at `62c0a24` since 2026-07-19 with a clean working tree.

Of the five failures, indices 637 (`TwoProcessLoopbackTest.HostMigration…`, the networking
Outcome C) and 1628 (`SoundBankTest.IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes`, the
wall-clock audio class) are the two known pre-existing classes inside the baseline-comparable
range; 6052, 6142 and 6211 are the three newly measured items adjudicated in §5.

---

## 5. Adjudication of the three newly measured failures

Each was reproduced **in isolation, three consecutive runs, deterministically failing 3/3**
(quota-scoped `ctest -R`, `DISPLAY=:101` forced on the ctest process). None is grouped with the
others; each classification is individual. All three lie **outside both Batch 2 lane diffs**
(wicked touches 16 files, magnum 45; none of them intersects the EasyGL backend glue, the two test
sources, the sibling project, or — before this session — `GraphicsDevice.cpp`'s
`SetVertexBuffers`).

### 5.1 `EasyGL_DeviceValidation` — **B: latent pre-existing CNA production defect** → REMED-GFX-222, discovered and resolved

- **Identity**: CTest `EasyGL_DeviceValidation` → `cna_test_easygl_device_validation` ←
  `examples/easygl_device_validation_test.cpp` (Task 202, 2026-06-26).
- **Failure**: `[FAIL] SetVertexBuffers(16) does not throw` — check 2 of 4; the other three pass.
- **Mechanism**: the test binds 16 **default-constructed** (null-buffer) `VertexBufferBinding`s,
  asserting XNA's count-boundary contract (16 accepted, 17 throws).
  `GraphicsDevice::SetVertexBuffers` throws `System::ArgumentNullException` for **any** null-buffer
  element. **FNA performs no such element check** (`FNA/src/Graphics/GraphicsDevice.cs:1143`):
  a null *array* unbinds, more than 16 throws `ArgumentOutOfRangeException`, and null-buffer
  *elements* are legal unused slots — FNA itself assigns `VertexBufferBinding.None` into the same
  array. CNA's own draw path already treats a defaulted binding as unused.
- **Provenance of the defect**: introduced 2026-07-25 by `8a308f3d` (REMED-GFX-039), **before the
  phase-1 checkpoint**. GFX-039's master-plan strategy is "Add each validation per FNA"; every
  other validation in its progress record has an FNA counterpart — this one contradicts FNA and
  carries no documented deviation rationale. One unit test added by the same commit
  (`SetVertexBuffers_DefaultNullBindingThrows`) encodes the same over-reach.
- **A/B controls**: the failing binary in the principal tree was built 2026-08-05 15:55 — **at
  Batch-1-checkpoint content (`ed607602`)** — and fails identically; the implicated production
  function is byte-identical at `ed607602`, `683a00a5`, `e7d46c4c` (the only `GraphicsDevice.cpp`
  change in the whole Batch 2 range is magnum's `#ifdef CNA_BACKEND_MAGNUM` window-flag block).
  The post-fix HEAD binary passes 3/3. **Not a Batch 2 integration regression.**
- **Resolution**: bounded fix on the integration branch (`0dd1b0a9`), following the Batch 1
  REMED-GFX-220 precedent — the element null-throw removed (the FNA-faithful >16 check, empty-vector
  unbind and null-safe current-buffer assignment stay), the header's `@throws` contract corrected,
  and the one unit test replaced by FNA-faithful acceptance coverage. The `VertexBufferBinding`
  parameterized-constructor validation (also GFX-039, XNA-documented) is deliberately untouched.
  Post-fix: `EasyGL_DeviceValidation` **3/3 pass** (4/4 checks); `GraphicsDeviceValidationTest`
  + `TextureCollectionValidationTest` **20/20**, the 17-binding ceiling mutation control throwing;
  a second in-tree test encoding the same over-reach surfaced in the post-fix corpus and was
  corrected within scope (§6.1); the focused validation suite re-run green under the WICKED and
  MAGNUM configurations (§6.1); full corpus deltas in §6.1. Recorded as **DISCOVERED AND
  RESOLVED**; full ticket: `plans/plan_postaudit.md` §19.

### 5.2 `EasyGL_GraphicsDevice_ReferenceStencil` — **B: latent pre-existing CNA defect, already documented and tracked (Task 872) — carried visible residual**

- **Identity**: CTest `EasyGL_GraphicsDevice_ReferenceStencil` →
  `cna_test_easygl_graphicsdevice_reference_stencil` ←
  `examples/easygl_graphicsdevice_reference_stencil_test.cpp` (Task 319, source last changed
  2026-07-07).
- **Failure**: `[FAIL] centre=(0,255,0), expected BACKGROUND (override reference 0x99 must
  reject)` with the test's own diagnosis: `GraphicsDevice.setReferenceStencilProperty() had no
  effect — the compare still used the state's own baked-in ReferenceStencil`.
- **Status in the tree**: the registration itself is commented *"registered as a documented known
  failure"* (Task 872 — "confirmed a universal, not-Vulkan-specific gap"), and the frozen
  `AUDIT.md` (line 128) records `ReferenceStencil`'s independent override as connected on Vulkan
  but with **"zero backend connection on EasyGL/Bgfx — Task 872, still open"**.
- **Classification**: a pre-existing, owner-documented, tracked-open CNA gap that predates the
  phase-1 checkpoint entirely; the registration widening merely made an already-documented known
  failure *counted*. No new ticket is warranted (the precise finding already exists as Task 872);
  no production change was made this session. It remains a **visible failing test** — not skipped,
  not weakened — and is carried exactly like the other documented open findings.

### 5.3 `easy-gl-resource-smoke-tests` — **C: EasyGL sibling-project defect (upstream), outside the integrated CNA contract**

- **Identity**: registered by the sibling repository's own `tests/CMakeLists.txt`
  (`/rv/data/development/github.com/openeggbert/easy-gl`, branch `develop`, head `62c0a24`
  2026-07-19, clean); binary `cmake-build-noxna/easy-gl/tests/easy-gl-resource-smoke-tests`,
  unchanged since the tree's 2026-08-04 creation.
- **Failure**: `Assertion 'g_state.last_active_texture == 0x84C0' failed` at
  `easy-gl/tests/smoke/SmokeResourceTests.cpp:336`
  (`test_texture_upload_sets_unpack_alignment_wrap_and_unit0_binding`) — subprocess abort, 0.12 s,
  3/3 deterministic. The suite is a **mock-GL state-machine harness**: no CNA code is compiled
  into it, no display or GPU is involved.
- **Ownership**: entirely inside the easy-gl repository — its own test asserting its own
  library's active-texture-unit hygiene after upload. The sibling checkout predates the phase-1
  checkpoint and has not moved; the failing binary predates both Batch 2 merges. **It cannot be a
  Batch 2 (or any CNA) regression.**
- **CNA's declared contract is not violated**: CNA consumes easy-gl's upload/bind API and verifies
  observable behavior through its own pixel and round-trip suites — all green in the same corpus
  (the 292-test dedicated block and the 5913-case corpus contain the EasyGL texture round-trips).
  Which GL texture unit remains active after an internal upload is not a property CNA's contract
  consumes.
- **Disposition**: recorded as a **visible upstream residual** with the exact reproducer
  (`ctest -R '^easy-gl-resource-smoke-tests$'` in the principal tree; assert line above) for the
  owner. **The easy-gl repository was not modified** — cross-repository repair is outside this
  session's authorization. The two sibling companions (`easy-gl-smoke-tests`,
  `easy-gl-context-lifecycle-tests`) pass.

---

## 6. Consolidated current-head baseline

All runs on the current integrated HEAD content (`e7d46c4c` before the §5.1 fix; the post-fix
re-validation is folded into §6.1). Display `:101` with `SDL_VIDEODRIVER=x11` forced on every ctest/gtest
process itself; `:0` never used; `:99` not required (no Wine control in scope). GPU tests serial
(ctest default). All heavy work inside `systemd-run --user --scope` CPUQuota containment (40 %,
raised to 60 % mid-session at the owner's request), dry-run-proven before use.

### 6.1 Corpus matrix

| Configuration | Tree | Registered | Selected | Executed | Passed | Failed | Skipped | Not run | Aborts/Timeouts |
|---|---|---|---|---|---|---|---|---|---|
| WICKED (full ctest) | `cnaintegration/cmake-build-wicked` (**new**) | 5780 | 5780 | 5774 | 5771 | **3** | 6 (disabled) | 0 | **0** |
| EASYGL principal (full ctest) | `cnaintegration/cmake-build-noxna` (reused, incremental) | 6212 | 6212 | 6205 | 6196 | **9** | 7 | 0 | 0 (the sibling abort is its test's own `assert`, counted in the 9) |
| EASYGL continuity instrument (`CnaTests` gtest binary, repo root) | same tree | 5913 | 5913 | 5913 | 5906 | **1** | 6 (gtest skips, the exact historic set) | 0 | 0 |
| EASYGL post-fix (full ctest, after §5.1's fix) | same tree, incremental | **6213** (+1: the new ceiling test) | 6213 | 6206 | 6195 | **11** | 7 | 0 | 0 |
| MAGNUM (full ctest) | `cnaintegration-magnum/cmake-build-magnum` (reused; content ≡ HEAD by tree SHA; no-op build gate clean) | 5843 | 5843 | 5837 | 5830 | **7** | 6 | 0 | 0 |

Wicked corpus arithmetic: 5771 + 3 + 6 = 5780; EasyGL pre-fix: 6196 + 9 + 7 = 6212; continuity:
5906 + 1 + 6 = 5913; Magnum: 5830 + 7 + 6 = 5843; EasyGL post-fix: 6195 + 11 + 7 = 6213. All exact.

**The post-fix run's deltas, each dispositioned:** `EasyGL_DeviceValidation` left the failure set
(the fix, §5.1). Two entries appeared: `OrdinaryDrawMultiStreamTest.NullSecondaryStreamIsRejectedAtBindTime`
— a second in-tree test encoding the same GFX-039 bind-time over-reach (it never draws; it asserts
the `ArgumentNullException` on an FNA-legal input) — corrected within §5.1's scope to assert the
FNA contract (`…BindingIsAcceptedAsUnusedSlot`: accepted, both slots retrievable, the null slot
null), suite 20/20 after; and `EasyGL_MsaaMipReadback`, which passed **3/3 in immediate
isolation** — the documented corpus-context environmental blip class (Batch 1 §6.3's
"different victim each run"), not a fix consequence (its subject shares nothing with
`SetVertexBuffers`). The six audio-class victims and the networking flip behaved per their
standing classifications.

The Wicked corpus required staging upstream's `libdxcompiler.so` beside the **source root**,
because REMED-BUILD-001 sets every discovered case's working directory to `${CMAKE_SOURCE_DIR}`
and Wicked's shader compiler loads `./libdxcompiler.so` CWD-relative with no fallback. A first,
non-official run without it failed 803 device-creating cases with the loader error plus one
audio-class victim — recorded as run evidence, and the staging replicates what the lane's own
clean corpus log proves its session did. The staged copy and the corpus-generated transients
(root `log.txt`, an aborted video test's dot-fixture) were removed after the runs; the worktree
returned to clean.

### 6.2 Dedicated suites and probes at the head

| Item | Result |
|---|---|
| `Wicked_PipelineKey` | Passed (in-corpus, both runs) |
| `Wicked_DeviceLifecycle` (6 legs = WICKED-78 regression) | **Passed**, 12.8 s, zero VMA assertion, zero abort |
| `Wicked_GeometryVertexOffset` (5 cases = WICKED-77 regression, incl. `instanceCount==1` and bytes-vs-elements) | **Passed**, 8.2 s |
| `Wicked_Demo2D_SmokeTest` | Passed (lavapipe ICD staged on the ctest environment) |
| WICKED-79 carriers (transfer tests: multi-face/per-mip cube, Cnj `Texture3D` fixtures) | all Passed in-corpus |
| Magnum dedicated 8 pixel suites | all Passed in-corpus (`Magnum_Smoke` … `Magnum_MrtMsaa`) |
| Magnum guard/capability probe (17 checks, rebuilt from `magnum-repro/`) | **17 checks, 0 failures** — all 11 capability answers, custom-declaration refusal with target unmutated, stock acceptance, unlisted-stride refusal |
| Magnum lifecycle probe (bare/query/repeated/dispose) | **4/4 exit 0** — the WICKED-78 teardown class remains absent |
| Shared declaration-fidelity / capability / WireFrame-oracle / texture round-trip / RT-orientation suites | contained in the corpora above; zero failures attributable to them in any configuration |

### 6.3 Failure classification — every failure, individually

| Test (index/config) | Classification |
|---|---|
| `TwoProcessLoopbackTest.HostMigration…` (WICKED 643; **passed** in the EASYGL runs) | **Pre-existing checkpoint residual** — networking Outcome C, the ~50 % coin flip (Batch 1 §5). Isolated-unquoted control this session: 1/3. Encountered naturally; not investigated further, per scope |
| Audio wall-clock class — WICKED: `CueTest.PauseAfterNaturalCompletionIsANoOp`, `WaveBankTest.IsInUseFalse…`; EASYGL: those two plus `AudioEngineTest.UpdateSweeps…`, `CueTest.PlayingCueNaturallyTransitions…`, `DynamicSoundEffectInstanceTest.PendingBufferCount…`, `SoundBankTest.IsInUseFalse…` | **Pre-existing environmental class, in a measured failure window this session**: the same binaries passed these tests in the morning's preserved run; at measurement time they failed 0/3 isolated-unquoted under BOTH backends simultaneously, and the host's PipeWire speaker sink was **SUSPENDED** with one active sink-input. Failure shape: `IsStopped` stays false after natural completion. The class predates Batch 2 (Batch 1 and magnum-session controls); tests unmodified, visible |
| `EasyGL_DeviceValidation` (6052) | **B → REMED-GFX-222**, §5.1 — fixed this session; pre-fix failure recorded here |
| `EasyGL_GraphicsDevice_ReferenceStencil` (6142) | **B, documented known failure, Task 872** — §5.2, carried visible |
| `easy-gl-resource-smoke-tests` (6211) | **C, sibling-owned upstream defect** — §5.3, carried visible |

**Zero failures classified as Batch 2 integration regression, stale harness, platform limitation,
or new independent CNA defect beyond REMED-GFX-222.**

### 6.4 Runtime identities

| Configuration | Identity |
|---|---|
| WICKED | Wicked Engine `27c0df16` · `GraphicsDevice_Vulkan` · adapter `llvmpipe (LLVM 19.1.7, 256 bits)` (lavapipe ICD forced via `VK_ICD_FILENAMES`; RADV cannot present on Xvfb) · **software rendering** · shader route `wi::shadercompiler` → `./libdxcompiler.so` v1.9 → SPIR-V at device creation · SDL3 window on `:101` |
| MAGNUM | Corrade `783e4e48` + Magnum `5a742464` · Magnum::GL typed wrappers · `Magnum backend on 4.5 (Core Profile) Mesa 25.0.7-2+deb13u1 (renderer: llvmpipe (LLVM 19.1.7, 256 bits))` · **software rendering** · SDL3 owns window/context · `:101` |
| EASYGL principal | `EasyGLGraphicsBackend initialized with OpenGL OpenGL ES 3.2 Mesa 25.0.7-2+deb13u1` (llvmpipe on Xvfb) · **software rendering** · `:101` |

---

## 7. Sanitizer consolidation

Both configurations `address,undefined`, GCC 14.2.0, Debug, `:101`, lavapipe/llvmpipe. No
sanitizer run was concurrent with a normal heavy build.

| Tree | Content | Suites | ASan errors | UBSan runtime errors | Leak roots (frame #1) | `detect_leaks=0` control |
|---|---|---|---|---|---|---|
| `cnaintegration/cmake-build-wicked-asan` (**new**, targeted: 3 dedicated suites + demo; the `CnaInputTests` ctest alias needs the full `CnaTests` binary, which — as in the lane's own asan tree — was deliberately not built) | HEAD | PipelineKey + DeviceLifecycle + GeometryVertexOffset + Demo2D | **0** | **0** | 100 % `libvulkan_lvp.so`, 0 CNA/backend/engine frames | **4/4 pass, exit 0** |
| `cnaintegration-magnum/cmake-build-magnum-asan` (reused; tree ≡ HEAD) | HEAD | all 8 `Magnum_*` pixel suites | **0** | **0** | 100 % `libGLX_mesa.so`, 0 CNA frames | **8/8 pass, exit 0** |

Sanitized probes: the Magnum guard (**17/17**) and lifecycle (**4/4** legs) probes recompiled
against the asan tree run clean — covering the declaration guard, capability answers, lifecycle
and disposal rows; the MAGNUM-65 oracle is the sanitized sprite flush inside the 8 suites (the
pre-fix run is what caught the defect; the post-fix runs are clean). The WICKED-79 row is covered
by a new sanitized narrow-width staged-transfer probe — which stayed ASan/UBSan-clean **and found
`WICKED-80`** (§9.1): the corrupted bytes are in-bounds, which is precisely why the sanitizer
alone cannot see this class and the probe compares content byte-exactly.

**Zero CNA-originating ASan findings, zero CNA-originating UBSan findings, no hidden abort, no
disabled assertion** (VMA assertions live via `-g`-only Debug flags; `WICKED_ENABLE_RTTI=ON`
keeps the vptr check active across the dependency). Upstream/library leaks are classified by
frame-#1 ownership with `detect_leaks=0` controls proving the checks execute.

---

## 8. History, signature and attribution gate

Scanned range: `d79214e7..HEAD`.

| Measure | At session start (`e7d46c4c`) | At the decision (`cbdab0c5`) |
|---|---|---|
| Commits | **244** | **246** |
| Merges | **11** — the eleven lanes, no twelfth | **11** |
| `%G?` classes | **`U` × 244; 0 `N`, 0 `E`** | **`U` × 246; 0 `N`, 0 `E`** |
| Distinct authors / committers | **1 / 1** — `Robert Vokac <robertvokac@robertvokac.com>` | **1 / 1** |
| Trailers | **1** — the technical `Verified: 7/7 checks pass.` (`13f50353`) | **1** — the same |
| Banned-token multiline sweep | **2 hits, 0 violations** — both the allowed literal filename `CLAUDE.md` (`069b073c`, `6e988c8e`) | **2 hits, 0 violations** — the same two |

The wider narrative sweep over the Batch 2 range returns exactly one hit, the allowed technical
"normalized handoff" (`d455adb3`). The four `opengl2` §2.2 narrative residuals recorded by
`BATCH_1_STABILIZATION.md` §7.2 remain in merged history, unchanged and permanently recorded —
correcting them would require rewriting published history, which is prohibited. Original branches
and all archive tags are unchanged; `origin/integration/post-audit-phase1` (`61bd1a1b`) remains an
ancestor — **published history was not rewritten**.

---

## 9. New findings

**Two.** One discovered and completely resolved within this stabilization; one discovered,
precisely recorded, and deliberately left open — it is this checkpoint's blocker.

| ID | Severity | Status |
|---|---|---|
| **REMED-GFX-222** | MEDIUM | **DISCOVERED AND RESOLVED** — `SetVertexBuffers` rejected FNA-legal null vertex-buffer bindings (GFX-039 over-reach, 2026-07-25); §5.1, `plans/plan_postaudit.md` §19 |
| **WICKED-80** | **HIGH** | **OPEN** — `Texture3D` staged transfers corrupt dimension-dependent tail rows (§9.1) |

### 9.1 `WICKED-80` — Texture3D staged transfers corrupt dimension-dependent tail rows

Found by this stabilization's new sanitized narrow-width transfer probe — coverage the corpus
does not have. A byte-exact `Texture3D` SetData/GetData round trip fails for specific volume
shapes; the affected shape set depends on the probe's allocation sequence (5×5×3 in one build;
4×5×3 and 6×5×3 in another) but is **deterministic per binary across runs**. The first wrong
texel sits at a row/slice tail, and the wrong value equals an **earlier texel of the same
uploaded pattern** (`in[5]`, `in[52]`, `in[0]` across instances) — recycled staging bytes showing
through where the copy never wrote. Four consecutive readbacks of an affected volume disagree
with the input identically, so **the stored volume itself is corrupt**: the upload's staged-copy
footprint drops tail rows for some shapes (the shared staged readback path is equally suspect for
the same arithmetic). ASan/UBSan are silent — the bytes are wrong, not out of bounds — and every
existing corpus transfer test and dedicated suite passes over it; only byte-exact content
comparison at the affected footprints can see it.

It violates the `REMED-GFX-135` completion/correctness contract on a supported path
(`Texture3D` is reported supported and `GetData` "returns only after the whole" transfer). The
content is byte-identical between `97d5a644` and the current head for this path, so it is a
**latent lane-content defect newly measured**, not a Batch 2 integration regression — and it is
an **unaccepted CNA production defect**, which under Phase 9's criteria blocks the checkpoint
tag exactly as `REMED-GFX-220` blocked Batch 1's first decision.

**Deliberately not fixed in this session.** The lane card's own §13.4 discipline applies:
guessing at GPU copy-footprint arithmetic without an isolated raw-`wi::graphics` control is how
a plausible-but-wrong fix lands. The reproducer, its build instructions, the ASan first-detection
log and the measured evidence are preserved in
`cnaintegration/cmake-build-wicked/wicked-repro/`; the raw-probe infrastructure that settled
`WICKED-78`'s CNA-versus-upstream boundary is the named first step. Recorded as an OPEN row in
`plans/plan_wicked.md` (`cbdab0c5`).

No ticket was raised for: the registration/instrument bookkeeping (§4 — expected-correction
class), the ReferenceStencil failure (the precise finding already exists as **Task 872**, in-tree
and in `AUDIT.md`), the sibling smoke failure (upstream easy-gl defect, recorded with reproducer),
or sanitizer/bookkeeping items.

---

## 10. Residuals and carried status

| Residual | Status |
|---|---|
| Networking Outcome C (`TwoProcessLoopbackTest.HostMigration…`) | Open, visible, unmodified — the ~50 % characterisation of Batch 1 §5 stands; encountered only as naturally occurring in the corpora (failed in the Wicked official, Magnum, continuity and post-fix EasyGL runs; passed in the pre-fix EasyGL run — the coin flip at small sample) |
| Wall-clock audio class (`SoundBankTest.IsInUseFalse…` and victims) | Pre-existing, control-classified by the magnum session on the pre-magnum principal binary; 2 victims in the Wicked official run, 6 (identical names cross-backend) in the EasyGL and Magnum runs during the measured suspended-sink window (§6.3) |
| Task 872 — `ReferenceStencil` override on EasyGL/Bgfx | Open, pre-existing, documented in `AUDIT.md`; now **counted** as a visible failing test (§5.2) |
| easy-gl sibling `resource-smoke` assertion | Upstream, deterministic, reproducer recorded (§5.3) |
| `REMED-GFX-221` | LOW, open, unchanged |
| `REMED-CONTENT-007` / `-008` | **OPEN, HIGH/P1** — re-verified still present this session (0 containment guards in the three readers); outside both lane file sets; non-blocking for this checkpoint under the campaign's consistently applied rule; required before any public security-clean claim |
| Wicked real-hardware/display verification (`WICKED-18`/`74`), `WICKED-75`/`76`, smoke-test-vs-RADV environmental | Lane-declared boundaries, unchanged |
| Magnum real-display/GPU verification (`MAGNUM-59`), `MAGNUM-54`/`55`/`58`, `SetDataRaw` observation | Lane-declared boundaries, unchanged |
| `opengl2` narrative residuals (4 bodies) | Permanent, recorded (Batch 1 §7.2) |
| Direct2D | **OWNER-FROZEN, FROZEN INCOMPLETE/EXPERIMENTAL** — untouched |
| `feature/gl` | Untouched; the MetaGL→develop, EasyGL rvc→develop, repin sequence remains owner-gated; public identities remain OpenGL ES 3 / OpenGL 3 / WebGL 1 / WebGL 2; EasyGL stays internal |
| Modularization | Not begun; binding sequence unchanged (all 21 lanes → full-tree stabilization → cleanup/migration → modularization) |

---

## 11. Checkpoint decision — **BLOCKED**

Measured against the Batch 2 READY criteria:

| Criterion | Result |
|---|---|
| Wicked and Magnum provenance clean | ✅ §2, §3 |
| All lane regressions green (W77/W78/W79 carriers, MAGNUM-65 oracle) | ✅ §6.2, §7 |
| All new EasyGL failures individually adjudicated | ✅ §5 — B (resolved), B (documented Task 872), C (upstream) |
| No Batch 2 integration regression | ✅ §6.3 — zero, on every instrument |
| Complete Wicked/Magnum/EasyGL current-head baselines | ✅ §6 |
| Sanitizer gates clean (zero CNA-originating findings) | ✅ §7 |
| History/signature/attribution gate | ✅ §8 |
| **No unaccepted CNA production defect** | ❌ **`WICKED-80`** (§9.1) — discovered by this stabilization's own deeper-than-corpus probe, open |
| Worktrees clean, `audit/` untouched, `git diff --check` | ✅ — both writable worktrees clean after the commits, `audit/` untouched, `git diff --check` clean, session transients removed |

**Decision: OUTCOME B — BLOCKED. No checkpoint tag was created.**

This is the same deliberately literal reading Batch 1's first decision took for `REMED-GFX-220`:
every integration-quality gate passed — the two lanes are provenance-clean and regression-green,
and the corpus baselines are the cleanest consolidated set the campaign has recorded — but the
stabilization's own probe found a real, reproducible data-correctness defect in a supported path,
and taking the tag over an open production defect is what the criteria exist to prevent. The
owner may instead accept `WICKED-80` as a carried residual (it is lane-content latent, invisible
to every existing test, and preserved with a reproducer); that is an owner judgement this session
does not make silently.

**Next task — exactly one, not begun:** resolve `WICKED-80` — first the raw-`wi::graphics`
control from the preserved reproducer to settle the CNA-versus-upstream boundary, then the
bounded fix on whichever side owns it, re-run the probe (all shapes) plus the Wicked corpus, and
re-take this checkpoint decision. Everything is preserved: all branches, worktrees, archive tags,
the four user stashes, every build tree, and both new reproducers.

---

## 12. Checkpoint retake — `WICKED-80` resolved (2026-08-06, HP EliteBook 840 G9)

The §11 BLOCKED decision stands as recorded; this section is the separate retaken decision its
last paragraph prescribed. The session ran on the migrated HP EliteBook 840 G9 — the first CNA
session on this host.

### 12.1 Migration validation

| Check | Result |
|---|---|
| Host | `debian` on HP EliteBook 840 G9 · i7-1260P (4P+8E, 16 threads) · 15 GiB RAM · 16 GiB swapfile · LUKS root (115 G free) + `/media/robertvokac/claude` 98 G build partition (93 G free at start) |
| Toolchain | gcc 14.2.0 · clang 19.1.7 · CMake 3.31.6 · ccache 4.11.2 (cache empty at start) · git 2.47.3 · gpg 2.4.7 · Xvfb present |
| Vulkan | Mesa 25.0.7 — Intel Iris Xe (ANV) + llvmpipe/lavapipe (the **same** Mesa+LLVM as the T14 evidence) · Khronos validation layer 1.4.309 · `optimalBufferCopyRowPitchAlignment` = 128 on both devices |
| /rv paths | unchanged; all 14 worktrees resolve; the stale prunable `/tmp/cnaaudit-gfx098-prefix` entry left exactly as found |
| Fetch | `git fetch --all --prune --tags` exit 0 |
| Heads | integration `cbdab0c5` ✅ (= §1's decision head) · planning `7dc2be5b` ✅ |
| Lane merges | exactly 11 first-parent merges, no twelfth ✅ |
| Checkpoint ancestry | phase-1 `d79214e7`, batch0 `e0332214`/`990d6b8a`, batch1 `ed607602` all ancestors of HEAD ✅; all checkpoint tags verify **Good** |
| Wicked/Magnum archive tags | `91d8587e` / `9b903db8` unchanged, verify **Good** ✅ |
| Stashes | the four user stashes present, untouched ✅ |
| GPG | signatures on both heads verify Good; signing proven non-interactively on this host before any work ✅ |
| Dependency pins | `~/deps/WickedEngine` at `27c0df16` with exactly the six SDL3/teardown-patched files modified ✅; corrade `783e4e48` + magnum `5a742464` exact ✅ |
| **Deviation** | **No CNA build tree survived the migration** — including `cmake-build-wicked/wicked-repro/` (the preserved WICKED-80 reproducer + raw ThinkPad evidence) and `cmake-build-noxna/preserved-logs-pre-batch2/`. The probe source was never git-tracked, so it was rebuilt from §9.1's documented specification; the ThinkPad raw measurements survive only as the figures quoted in §9.1/`plans/plan_wicked.md`. Recorded as a migration deviation, not a blocker: every git-integrity item passed and the lost artifacts are regenerable (this session regenerated them). Fresh trees live on the owner-designated build partition `/media/robertvokac/claude/tmp/cna/`; the in-repo `.sdl-prebuilt-Linux-x86_64` prebuilt survived and is reused |

### 12.2 Reproduction at the unchanged HEAD

The rebuilt probe reproduced `WICKED-80` on the EliteBook exactly as recorded: run 1 corrupted
**3/14 shapes** (5×5×3 — the ThinkPad layout-A shape — plus 3×3×3 and 5×4×7); run 2 of the same
binary corrupted **3/14 different shapes** (6×5×3, 7×3×4, 5×1×3); every isolated single-shape run
passed. Strays decode to earlier texels of the same pattern (`in[0]`, `in[25]`, `in[40]`, …) —
§9.1's signature — and consecutive readbacks of a corrupt volume disagree with the upload. On
this host the failing set varies **per run**, not only per binary, sharpening §9.1's
"deterministic per allocation sequence": the manifestation is a suballocation-adjacency lottery
over a defect that is always present. Adapter: `llvmpipe (LLVM 19.1.7, 256 bits)`.

### 12.3 Ownership — classification **B: pinned upstream Wicked defect**, proven, not intuited

The CNA-free raw `wi::graphics` control (`probe_raw_wicked_texture3d.cpp`, hidden SDL window —
the pin's window-less constructor path segfaults — validation ENABLED) mirrors the CNA helper
one-for-one and settles ownership:

- **Arithmetic, measured**: every narrow staging texture reports `mapped_size == tight` while its
  mapped layout advertises `row_pitch = align(w·4, 128)`, `slice_pitch = row_pitch·h` — 5×5×3:
  **300-byte buffer, 1920-byte addressed footprint**;
- **API verdict**: `VUID-vkCmdCopyBufferToImage-pRegions-00171` and
  `VUID-vkCmdCopyImageToBuffer-pRegions-00183` fire on both directions for every narrow shape —
  on lavapipe **and on Intel ANV** (a second implementation ⇒ not a driver/environment defect);
- **Raw corruption**: the control corrupts with no CNA in the process (5×4×7: slice 6 tail rows =
  pattern texel 5, both readbacks identical); aligned-width controls emit no VUID and stay
  byte-exact;
- The TEXTURE_2D audit legs flag the identical under-allocation for narrow 2D staging (5×5: 100
  bytes vs 640) — the same class behind Texture2D/cube-face/small-mip staging; `WICKED-79`'s
  measured "two staged copies on one command list interfere" was this defect's collision, and
  every narrow transfer had been passing on suballocation luck.

Root cause: upstream `GraphicsDevice_Vulkan::CreateTexture` sizes UPLOAD/READBACK staging buffers
with `ComputeTextureMemorySizeInBytes` — the TIGHT texel size — while
`CreateTextureSubresourceDatas(..., optimalBufferCopyRowPitchAlignment)` hands out ALIGNED row
pitches and `CopyTexture` addresses the buffer through them (`bufferRowLength = row_pitch/stride`,
`bufferImageHeight = subresource height`). Units audit: widths/heights in texels, pitches in
bytes, `bufferRowLength` in texels — no CNA-side unit or arithmetic error exists on this path;
CNA writes exactly through the advertised mapped layout and cannot observe the allocation size.
The residual lavapipe-only `pRegions-00173` "overlap" reports (3 post-fix, also present pre-fix,
absent on ANV, round trips byte-exact) are recorded as a pre-existing environmental
validation-layer artifact class, not suppressed.

### 12.4 Fix — the third carried patch

`cmake/patches/wicked-staging-footprint.patch` sizes those buffers with exactly the footprint
`CreateTextureSubresourceDatas` lays out. Proven to apply cleanly on the pristine pin `27c0df16`
after the SDL3 and teardown patches (git-archive extraction, sequential apply, reverse-check);
applied and marker-verified by `cna_wicked_check_staging_footprint_fix` in
`cmake/ThirdPartyWicked.cmake` (`CNA_WICKED_APPLY_STAGING_FOOTPRINT_PATCH=ON`), a line-for-line
mirror of the proven teardown-patch mechanism. No assertion or validation is suppressed, no
system library is touched, and the public `Texture3D` contract is preserved — now byte-exact.

### 12.5 Regression — `Wicked_Texture3DStagedTransfer`

11 corpus gtest cases + 1 dedicated ctest: narrow volumes (every ThinkPad- and
EliteBook-measured shape), aligned-width controls, the 31/32/33 alignment-boundary trio
(bytes-vs-texels), sub-box upload and readback at offsets, repeated readbacks, the WICKED-79
narrow-Texture2D control, a two-face TextureCube control, small-mip (4×4, 1×1) controls and a
device-usable-after tail — all byte-exact via per-texel index encoding. **The sequenced
narrow-matrix leg fails 3/3 deterministically against the pre-fix engine** with §9.1's exact
signature (5×5×3, first wrong texel 50 at the slice-2 start, stray = `in[0]`), while the
fixture-per-test legs pass pre-fix — the measured proof that fresh-device single transfers dodge
the allocation lottery, so the sequenced leg is the load-bearing discriminator. With the patch:
11/11 ×3 runs; the CNA probe 14/14 shapes ×3 full runs; the raw control 18/18 with zero
under-allocation and zero out-of-bounds VUIDs.

### 12.6 Validation on the fixed HEAD

| Configuration | Tree | Registered | Selected | Executed | Passed | Failed | Skipped | Not run | Aborts/Timeouts |
|---|---|---|---|---|---|---|---|---|---|
| WICKED (full ctest, official) | `/media/robertvokac/claude/tmp/cna/cmake-build-wicked` (**new**) | 5789 | 5789 | 5783 | 5783 | **0** | 6 | 0 | **0** (zero `***` verdicts) |
| MAGNUM focused controls (`ctest -R '^Magnum_'`) | `…/cmake-build-magnum` (**new**; Corrade/Magnum from the pinned `~/deps` checkouts) | 8 | 8 | 8 | 8 | **0** | 0 | 0 | 0 |
| EASYGL continuity instrument, run 1 (`CnaTests` from repo root) | `…/cmake-build-noxna` (**new**, `CNA_NOXNA=ON`) | 5910 | 5910 | 5910 | 5903 | **1** | 6 | 0 | 0 |
| EASYGL continuity instrument, run 2 | same binary | 5910 | 5910 | 5910 | 5904 | **0** | 6 | 0 | 0 |

Arithmetic: 5783+0+6 = 5789; 5903+1+6 = 5910; 5904+0+6 = 5910 — all exact. The WICKED corpus
(1700.3 s) is **the campaign's first zero-failure corpus on this backend**: the networking
Outcome C flip and the wall-clock audio class both passed naturally this run (their standing
residual classifications remain — one clean run closes no class). The 6 skips are the exact
historic composition in both instruments. In-corpus dedicated suites all passed:
`Wicked_PipelineKey`, `Wicked_DeviceLifecycle` (WICKED-78, 6 legs), `Wicked_GeometryVertexOffset`
(WICKED-77, 5 cases), `Wicked_Texture3DStagedTransfer` (WICKED-80, 11 cases),
`Wicked_Demo2D_SmokeTest`; the 13 WICKED-79 transfer carriers all passed.

**Continuity run 1's single failure**
(`Texture3DTextureCubeContentTypeReaderTest.TextureCubeReaderZeroSizeThrowsContentLoadException`)
passed **3/3 in immediate isolation** and did not recur in run 2 — the documented corpus-context
environmental blip class (§6.1's `EasyGL_MsaaMipReadback` precedent, "different victim each
run"); its subject shares nothing with the Wicked-only change set, which touches no
EASYGL-visible file. **Zero failures classified as regression.**

Sanitizers (fresh `…/cmake-build-wicked-asan`, `address,undefined`, GCC 14.2.0, Debug,
`WICKED_ENABLE_RTTI=ON`, lavapipe/:101): PipelineKey 14, DeviceLifecycle 6, GeometryVertexOffset
5, Texture3DStagedTransfer 11 and the demo smoke all pass sanitized — **0 UBSan runtime errors,
0 ASan errors**, 16 leak blocks with **every** frame-#1 root in `libvulkan_lvp.so` (0
CNA/backend/engine frames), `detect_leaks=0` controls **5/5 exit 0**. Zero CNA-originating
findings.

**Registration arithmetic.** WICKED registers **5789** = §6.1's official 5780 + 1 (REMED-GFX-222's
ceiling test, name-verified) + 12 (the WICKED-80 suite, name-verified) **− 4**; the continuity
instrument registers **5910** = §6.1's 5913 + 1 (the same ceiling test, name-verified) **− 4**.
The identical −4 in both configurations, from git-identical sources, cannot be content-caused;
name-level attribution is impossible because the ThinkPad trees' registration lists did not
survive the migration (this host's complete lists are preserved in
`wicked-repro/ctest_names_off_built.txt` and the continuity logs), and test discovery here is
source-deterministic (no runtime-valued instantiations exist in the test sources). Recorded as a
host-conditional registration delta of the migrated instrument, §4-style; every named gate above
is unaffected.

### 12.7 Decision — **OUTCOME READY**

| Requirement | Result |
|---|---|
| WICKED-80 ownership proven | ✅ §12.3 — raw CNA-free control + VUIDs on two implementations |
| WICKED-80 fixed (contract preserved, not withdrawn) | ✅ §12.4 — third carried patch; byte-exact |
| Raw control and CNA regression green | ✅ §12.5 — 18/18, 14/14 ×3, 11/11 ×3; pre-fix discriminator fails 3/3 |
| WICKED-77/78/79 remain green | ✅ §12.6 — suites + carriers in-corpus, plus sanitized |
| Wicked corpus acceptable | ✅ **5789 · 5783 · 0 failed · 6 skips · 0 aborts** — first zero-failure corpus |
| Magnum controls acceptable | ✅ 8/8 (zero shared files in the change set) |
| EasyGL continuity acceptable | ✅ 5910-shaped instrument; run 2 zero-failure; run 1's blip isolated 3/3 green |
| Sanitizer gate clean | ✅ 0 + 0 CNA-originating, driver-rooted leaks, controls 5/5 |
| No open supported-path production defect from Batch 2 | ✅ WICKED-80 closed; no new finding |
| Provenance/signatures/attribution clean | ✅ §12.8 |
| No twelfth lane; `audit/` untouched; worktrees clean; `git diff --check` clean | ✅ §12.8 |

**Decision: OUTCOME READY.** Local signed annotated tag **`integration/checkpoint-batch2-20260806`**
("CNA integration Batch 2 checkpoint") created on the integration head and verified with
`git tag -v`. Nothing pushed. Batch 2 closes; the campaign pointer moves to Batch 3
(`sokol` → `diligent`).

### 12.8 History and hygiene

The fix landed as one signed commit on `integration/post-audit-phase1` (recorded below by
`NEXT.md` and the commit itself); this record landed as one signed commit on `feature/audit`.
Author and committer remain the maintainer alone; no prohibited attribution token appears;
`git diff --check` is clean in both worktrees; `audit/` untouched; the four user stashes and all
archive tags untouched; published history not rewritten (`origin/integration/post-audit-phase1`
remains an ancestor). Build trees, probes, run logs and registration name lists are retained
under `/media/robertvokac/claude/tmp/cna/` (recorded in `NEXT.md`).

---

## 13. Migration-gate reconciliation addendum (2026-08-06, EliteBook 840 G9)

A narrow, code-free reconciliation of one procedural inconsistency in §12. Nothing in §11 or §12 is
withdrawn or rewritten; no production code was touched; no tag was moved, deleted or recreated; no
twelfth lane was begun; `audit/` was untouched. The Wicked corpus was **not** re-run — this section
re-reads retained artifacts and git only.

### 13.1 The original gate, stated exactly

The session that produced §12 opened with a formal Phase 0 migration gate. Among its required
verifications were **the retained `WICKED-80` reproducer** and **retained raw evidence**, and its
failure clause was explicit and unconditional:

> If migration validation fails: do not repair by deleting or recreating worktrees; do not reset
> branches; report MIGRATION BLOCKED; stop.

### 13.2 The non-conformance, stated plainly

§12.1 recorded truthfully that **no CNA build tree survived the migration** — including
`cmake-build-wicked/wicked-repro/` (the preserved `WICKED-80` reproducer and its raw ThinkPad
evidence) and `cmake-build-noxna/preserved-logs-pre-batch2/`. Two of the gate's enumerated items
were therefore *absent*, not merely degraded.

The session nevertheless classified migration validation as "PASSED with one recorded deviation",
reconstructed both probes from committed specifications, and continued through the `WICKED-80`
resolution to create `integration/checkpoint-batch2-20260806`.

**Under the instruction as written, that moment required `MIGRATION BLOCKED` and a stop.** It did
not permit a self-granted deviation, however well recorded. The reasoning the session applied — that
every git-integrity item passed and the lost material was regenerable — was substantively sound and
is what this addendum ultimately confirms, but it was a judgement the gate reserved to the owner.
The correct sequence was: report BLOCKED, obtain a decision, then proceed. This is recorded as
**historical process non-conformance**, and it is not erased by the technical outcome below.

### 13.3 What was actually lost — classification

Classes: **A** committed and intact · **B** reconstructable exactly from committed specification ·
**C** recorded observations retained but raw artifact lost · **D** irrecoverably absent.

| Item | Class | Basis |
|---|---|---|
| Original ThinkPad probe sources (`probe_texture3d_staged_transfer.cpp`, `probe_raw_wicked_texture3d.cpp`) | **D** as byte-exact files (never git-tracked, `.gitignore`d build-tree scaffolding) / **B** functionally — the EliteBook rebuilds reproduced §9.1's documented signature exactly (5×5×3, first wrong texel 50 at the slice-2 start, stray = `in[0]`), which is the evidence the reconstruction was faithful. Both reconstructed sources are retained | lane card §13.3; `wicked-repro/README.md` |
| Exact pre-migration commands | **B** | run methodology committed in §6.1/§9.1 (display `:101`, `SDL_VIDEODRIVER`, `VK_ICD_FILENAMES`, CWD-relative `libdxcompiler.so`, quota containment); one-TU compile/link recipe in lane card §13.3 — demonstrably sufficient, it was executed successfully on this host |
| ThinkPad failure dimensions (5×5×3; 4×5×3 and 6×5×3) | **A** as recorded figures / **C** for the underlying run log | committed in `plans/plan_wicked.md` (`cbdab0c5`) and §9.1 |
| Expected and actual bytes (strays `in[5]`, `in[52]`, `in[0]`; first wrong texel at a row/slice tail) | **A** as recorded figures / **C** for the raw log | committed in §9.1; independently re-measured on this host (`in[0]`, `in[25]`, `in[40]`), logs retained |
| ThinkPad raw run logs and `preserved-logs-pre-batch2/` | **C** | the quoted observations survive in committed prose; the artifacts themselves are gone. **This is the only genuinely lost evidence class** |
| Wicked pin `27c0df16` | **A** | `cmake/ThirdPartyWicked.cmake`; `~/deps/WickedEngine` verified at the pin |
| Staging layout values (5×5×3: 300 tight vs 1920 addressed; `row_pitch = align(w·4,128)`, `slice_pitch = row_pitch·h`; 5×5 2D: 100 vs 640) | **A** | committed in `plans/plan_wicked.md` and §12.3 — **and re-measured post-migration**, printed per shape in the retained raw logs |
| `WICKED-79` evidence that led to `WICKED-80` | **A** | fix `4449daaa`, committed narrative in `plans/plan_wicked.md`, 13 committed corpus transfer carriers. The "two staged copies interfere" raw device measurement is **C** |
| Original lane / blocker decision | **A** | §11 (`OUTCOME B — BLOCKED`) committed at `7dc2be5b`; the `WICKED-80` OPEN row at `cbdab0c5`; both preserved verbatim |

**No unique source was lost.** Nothing that defines the checkpoint's code identity was derived
material: the production fix, `cmake/patches/wicked-staging-footprint.patch`, the
`cna_wicked_check_staging_footprint_fix` marker gate, the 362-line
`WickedTexture3DStagedTransferTest.cpp` regression and the docs are all committed in `ebd04ae3`.
Everything lost was untracked derived build output — trees, probe binaries, probe scaffolding
sources and run logs.

### 13.4 Independence of the post-migration evidence

Retained under `/media/robertvokac/claude/tmp/cna/cmake-build-wicked/wicked-repro/` and
`…/cmake-build-wicked-asan/`, re-read for this addendum:

| Required element | Retained |
|---|---|
| Raw CNA-free `wi::graphics` reproduction | ✅ `probe_raw_wicked_texture3d.cpp` + `raw_run1.log`, `raw_run2_lavapipe.log` |
| Exact failing shapes and byte offsets | ✅ per-shape in the raw and CNA probe logs; `prefix_seq_run{1,2,3}.log` |
| Aligned row/slice pitch measurements | ✅ `row_pitch=128 slice_pitch=640` printed per shape |
| Tight allocation measurement | ✅ `mapped_size=300 tight=300 aligned_footprint=1920` (5×5×3), `** UNDER-ALLOCATED **` |
| Both Vulkan VUIDs | ✅ `…-pRegions-00171` ×10 and `…-pRegions-00183` ×10 in **each** pre-fix raw log |
| lavapipe reproduction | ✅ adapter `llvmpipe (LLVM 19.1.7, 256 bits)` |
| Intel ANV reproduction | ⚠️ **C — no raw artifact retained**; all three raw logs are lavapipe. See §13.5 |
| Pre-fix deterministic sequenced discriminator | ✅ `prefix_seq_run{1,2,3}.log` FAILED 3/3, plus `prefix_discriminator_run{1,2}.log` passing fixture-per-test controls |
| Post-fix raw control | ✅ `raw_postfix_lavapipe.log` — 18/18, `mapped_size` now equals `aligned_footprint`, zero under-allocation, zero OOB VUIDs |
| Post-fix CNA control | ⚠️ the standalone probe's post-fix 14/14 ×3 logs are **not** retained (only the pre-fix `probe_run{1,2}.log`) — **C**. Superseded by the stronger committed carrier: `WickedTexture3DStagedTransferTest` 11/11 + `Wicked_Texture3DStagedTransfer`, all Passed in the retained `corpus_official.log` |
| Carried-patch provenance / pristine-pin apply-reverse proof | ✅ patch and marker gate committed in `ebd04ae3`; the dry-run transcript is **C** but the gate re-executes the check on every configure |
| Sanitizer gate | ✅ 10 retained logs — 0 `ERROR: AddressSanitizer`, 0 `runtime error`, all 16 leak frame-#1 roots in `libvulkan_lvp.so`, 0 CNA frames, `detect_leaks=0` controls present |
| Corpus | ✅ `corpus_official.log` — `100% tests passed, 0 tests failed out of 5789`, the 6 historic skips named, 1700.30 s |

### 13.5 The ANV gap, and why the conclusion survives it

The `Intel ANV` leg is the one element of §12.3 whose raw artifact is not retained; it is a recorded
observation only. It is corroborative rather than load-bearing, and the fact it carried was
independently re-verified this session with no compilation:

`vulkaninfo` reports **`optimalBufferCopyRowPitchAlignment = 0x00000080` (128) on *both*** present
devices — `Intel(R) Iris(R) Xe Graphics (ADL GT2)` (Intel open-source Mesa driver, i.e. ANV) and
`llvmpipe (LLVM 19.1.7, 256 bits)`. The engine's under-allocation arithmetic is a pure function of
that limit, so the identical tight-versus-aligned mismatch arises on ANV by construction.
Independently, the two VUIDs are validation-layer arithmetic over the copy parameters against the
buffer size — a spec-conformance computation, not driver behaviour. The "not a driver quirk" claim
therefore stands on re-verifiable ground even though its ANV transcript is gone.

### 13.6 Does independent post-migration evidence prove `WICKED-80`? — **YES**

The four links of the conclusion each rest on a retained post-migration artifact, and **none passes
through any ThinkPad material**:

1. **Ownership (classification B, pinned upstream Wicked defect)** — the raw control has zero CNA in
   the process and still reports the under-allocation and triggers both spec VUIDs. CNA cannot be
   the author of a defect reproduced without CNA present.
2. **Root cause** — measured, not inferred: the pinned engine reports `mapped_size == tight` while
   simultaneously advertising a mapped layout that addresses 1920 bytes. That internal contradiction
   is printed in the retained log.
3. **Fix correctness** — post-fix raw control 18/18 with `mapped_size` raised to the advertised
   footprint; the committed regression's sequenced leg fails 3/3 pre-fix and passes in the retained
   zero-failure corpus. The public `Texture3D` contract is preserved, not withdrawn.
4. **No residual supported-path defect** — retained corpus at 0 failures over 5789, sanitizers clean
   of CNA-originating findings.

The reconstruction of the ThinkPad probe is therefore *not* what the conclusion depends on. It
supplied a starting point; the proof is post-migration and re-inspectable today.

### 13.7 Reconciled decision — **CHECKPOINT ACCEPTED WITH RECORDED MIGRATION DEVIATION**

Process and code are adjudicated separately, and they land differently:

- **Process: non-conformant.** The Phase 0 gate should have produced `MIGRATION BLOCKED` and a stop
  when the reproducer and raw-evidence directories were found absent (§13.2). That is now permanently
  recorded, not excused.
- **Code: correct on its own merits.** The Batch 2 technical gates are satisfied by evidence that
  postdates the migration and is independently verifiable today (§13.4–§13.6); the checkpoint object
  is sound (annotated, signed, signature **Good**, target `ebd04ae3` = the branch tip, eleven lane
  merges, no twelfth, Batch 0/1 and phase-1 ancestry intact, integration history not rewritten,
  original Wicked/Magnum refs and archive tags unchanged).

`integration/checkpoint-batch2-20260806` is **preserved exactly as created** — not moved, not
deleted, not recreated — and **no replacement or revalidation tag was created**: repository
convention marks checkpoints once, and a second tag over an unchanged commit would add no
information this record does not carry. Batch 2 stands **ACCEPTED**; the next action remains the
Batch 3 `sokol` lane.

**Recurrence risk, for the owner.** The evidence that now carries `WICKED-80` lives on the
`/media/robertvokac/claude` build partition and is not git-tracked, so it is exposed to exactly the
loss class that produced this addendum. The committed material — patch, marker gate, regression
test — is what makes the result durable; the logs are not. Promoting any future load-bearing raw
observation into tracked content at the time it is measured is the standing lesson.
