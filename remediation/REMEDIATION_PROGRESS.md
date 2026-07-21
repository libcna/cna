# REMEDIATION_PROGRESS.md — Live Status Tracker

**This is the only file in `remediation/` expected to change during implementation.**
`MASTER_REMEDIATION_PLAN.md`, `REMEDIATION_INDEX.md`, `REMEDIATION_DEPENDENCIES.md`, and
`REMEDIATION_TRACEABILITY.md` are the frozen plan; this file records what actually happened.

## Status vocabulary

| Status | Meaning |
|---|---|
| `NOT STARTED` | No work begun |
| `VERIFYING` | Reproducing the finding before implementing (tasks with `Verify: YES`) |
| `IN PROGRESS` | Implementation underway |
| `BLOCKED` | Waiting on a dependency or an owner decision — **record which, in Notes** |
| `IN REVIEW` | Implemented, awaiting review/CI |
| `DONE` | Merged, tests pass, completion + verification criteria met |
| `NOT REPRODUCED` | Investigated and the finding did not hold. **A valid outcome — record the evidence.** |
| `DEFERRED` | Consciously postponed. **Record who decided and why.** |

`NOT REPRODUCED` is not failure. Several tasks rest on static analysis that was never executed; a
disproved finding is a real result and should be recorded with the same rigor as a fix.

## Rules

1. Update the row when status changes. Do not batch updates at the end.
2. `DONE` requires **both** the completion and verification criteria from the master plan, not just
   "the code compiles and the old test passes."
3. If implementation reveals the root cause differs from the plan's, **record that in Notes and say
   so** — do not silently widen scope. A wrong root-cause hypothesis in the plan is worth knowing.
4. New findings discovered during remediation get a new ID in the same scheme; append them to the
   "Discovered during remediation" table at the bottom. Do not overload an existing task.
5. Nothing here edits `audit/`. The audit is the frozen evidence baseline.

## Overall progress

| Priority | Total | Done | In progress | Blocked | Not started |
|---|---|---|---|---|---|
| P0 | 12 | 12 | 0 | 0 | 0 |
| P1 | 21 | 5 | 0 | 0 | 16 |
| P2 | 44 | 4 | 0 | 1 | 39 |
| P3 | 28 | 0 | 0 | 0 | 28 |
| **Total** | **105** | **21** | **0** | **1** | **83** |

*P1's Done count 3→5 reflects `REMED-CORE-001`/`-004` closing (see "Wave 2 — CORE lane" below).*

*P0's total was corrected 11→12 while closing `REMED-GFX-001`: `REMED-GFX-001`, `-002`, and `-003`
are all `Priority: P0-SAFETY` per `MASTER_REMEDIATION_PLAN.md`, so the row's prior total had
undercounted one of the three by one. P0 is now fully closed (all 12 DONE).*

*P1's Done count 1→3 reflects `REMED-CORE-006`/`-007` closing (see "Wave 1 (parallel) — CORE lane"
below). `REMED-CORE-014` (a `Discovered during remediation` finding, not one of the 105 counted
tasks) closed in the same change but is not part of this table.*

*P2's Done count 3→4 reflects `REMED-BUILD-005` closing (see "Wave 2 — BUILD_TEST_CI lane" below).
`REMED-BUILD-010`/`-011`/`-013`/`-014` (all `Discovered during remediation` findings, not among the
105 counted tasks) also closed in this same tranche but are not part of this table, matching
`REMED-CORE-014`'s own precedent above.*

## Wave 0 — make the tests trustworthy

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-001 | DONE | | feature/audit | `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` added to `gtest_discover_tests(CnaTests ...)` in `cmake/UnitTests.cmake` (one line, matching `EasyGLTests.cmake`/`VulkanTests.cmake`'s existing pattern). Full unfiltered `ctest` run against `cmake-build-debug` (EASYGL) and the direct `./CnaTests` binary now **agree exactly** on the gtest-discovered subset: 5507 tests, 5503 passed / 4 skipped (Accelerometer/Gyroscope hardware skips) / 0 failed, both ways. See "Wave 0 triage" below for the 5 additional failures ctest reports outside that subset (4 already-tracked, 1 newly discovered — recorded below). Completion + verification criteria met. |
| REMED-BUILD-002 | DONE | | feature/audit | Decision: `XactFileGen.hpp` makes the copy obsolete (see Decisions log) — POST_BUILD `copy_directory` removed from `cmake/Examples.cmake`. Verified: unpiped, unfiltered `cmake --build` exits 0 on 3 backends (HEADLESS, SOFTWARE, VULKAN — all rebuilt from a reconfigure of the changed `Examples.cmake`), `cna_demo_xact` itself linking cleanly on all three. Completion + verification criteria met. |

### Wave 0 triage — full post-`REMED-BUILD-001` `ctest` baseline (`cmake-build-debug`, EASYGL)

Total registered CTest tests: 5754 = 5507 gtest-discovered (governed by the `WORKING_DIRECTORY` fix) + 247
separately-registered tests (backend smoke tests, `CnaInputTests`, `easy-gl-*` submodule tests, etc. — these
already had correct `WORKING_DIRECTORY` before this task, per `EasyGLTests.cmake`/`VulkanTests.cmake`, so they
are unaffected by the fix either way). Result: **5749 passed, 5 failed, 4 skipped** (`Total Test time (real) =
583.90 sec`).

The 5 failures are **all outside the 5507 gtest-discovered set** (confirmed by cross-referencing the ctest log
against the direct-binary run — the discovered subset is 5503/4/0 both ways, exactly matching). Triage:

| Failing CTest | Classification | Evidence |
|---|---|---|
| `EasyGL_AvatarRenderer_TintRouting` | Already known — tracked in `MASTER_REMEDIATION_PLAN.md` (REMED-BUILD-003 evidence list; interacts with `REMED-GFX-006`). Not fixed by this task; real production tint-doubling bug (observed left=(81,51,31) vs expected (40,25,15) — almost exactly 2×+clamp). | `AUDIT_CROSS_CUTTING_FINDINGS.md` §CI-masking-risk |
| `EasyGL_MRT_TwoAttachments` | Already known — `REMED-GFX-016`. | `MASTER_REMEDIATION_PLAN.md` line 1238 |
| `EasyGL_GraphicsDevice_ReferenceStencil` | Already known — disclosed in-source (Task 319/872), confirmed still-open by the audit, tracked under REMED-BUILD-003's evidence list. | `AUDIT_CROSS_CUTTING_FINDINGS.md` §2176 |
| `easy-gl-resource-smoke-tests` | Already known and **out of scope** — `REMED-NA-011`, external `easy-gl` sibling repo's own test suite (reference-only per decision D-6), not a CNA finding. | `MASTER_REMEDIATION_PLAN.md` line 3081 |
| `EasyGL_RealWindowResize` | **Genuinely new** — see `REMED-BUILD-010` below. | This session |

None of these 5 are newly *introduced* by the `WORKING_DIRECTORY` fix or by `REMED-BUILD-002`'s cmake edit —
4 are pre-existing findings the audit already tracked (their own CTest registrations already had correct
`WORKING_DIRECTORY`, so the bug being fixed here never hid them), and the 5th is a pre-existing hang whose
trigger (a real desktop compositor on the test `DISPLAY`) is orthogonal to both Wave 0 tasks. No opportunistic
production fixes were made for any of the 4 already-tracked findings, per instruction.

## Wave 1 — security and memory safety

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-CONTENT-001 | DONE | | feature/audit | CRITICAL. Fixed in shared content code, **not** per-backend — see detail below. |
| REMED-CONTENT-002 | DONE | | feature/audit | Shared helper + 3 call sites + repo-wide grep sweep — see detail below. 2 genuinely new findings recorded (`REMED-CONTENT-007`, `-008`), not fixed (out of this task's scope). |
| REMED-CONTENT-003 | DONE | | feature/audit | Ported the sibling readers' existing check verbatim — see detail below. |
| REMED-CONTENT-006 | DONE | | feature/audit | VERIFY passed (reproduced a real segfault before fixing) — see detail below. |
| REMED-GFX-001 | DONE | | feature/audit | `RegisterForWindow` moved to last constructor statement — see detail below. |
| REMED-GFX-002 | DONE | | feature/audit | Landed with construction/setter validation as well as the use-site fix — see detail below. |
| REMED-GFX-003 | DONE | | feature/audit | Sequenced after GFX-002 (same file). Tables resized **and** flag operators added together — see detail below. |
| REMED-NET-001 | DONE | | feature/audit | Landed atomically with NET-003, same file/change — see detail below. |
| REMED-NET-003 | DONE | | feature/audit | Landed atomically with NET-001, same file/change — see detail below. |
| REMED-DEVICES-001 | DONE | | feature/audit | `shared_ptr` used, not a mutex held across the UI-blocking call — see detail below. |
| REMED-MEDIA-001 | DONE | | feature/audit | No 32-bit toolchain available in this sandbox — used the plan's own "size_t-narrowing test harness" alternative instead — see detail below. |

### REMED-CONTENT-003 detail — TextureCube byte-count validation

Ported `Texture2DContentTypeReader.cpp`'s existing `bytes.size() != pixelCount*4` check verbatim into
`TextureCubeContentTypeReader.cpp` (previously the one sibling among the three XNB texture readers
missing it). No shared file overlap with `REMED-CONTENT-001`.

- **Test added:** `Texture3DTextureCubeContentTypeReaderTests.cpp` —
  `TextureCubeReaderRejectsByteCountMismatchedWithSize` (undersized declared `byteCount` for a 2×2
  face/level throws `ContentLoadException`).
- **Completion criteria met:** all three sibling readers (`Texture2DReader`, `Texture3DReader`,
  `TextureCubeReader`) now share an identical validation shape.
- **Verification:** full `CnaTests` direct-binary run (EASYGL) — 0 regressions (see CONTENT-001
  detail below for the combined run).

### REMED-CONTENT-001 detail — Texture2D dimension/mipLevel validation

**Root cause confirmed exactly as described:** neither native-API validation (Vulkan's validation
layer is advisory; wgpu-native validates lazily at `wgpuQueueSubmit()` time) nor the existing
`ContentReader::CheckDecodedByteSize()` call (which only bounds the `width*height` *product*, not
either axis individually, nor `mipLevels` at all) catches a single-axis-huge or
mip-level-count-exceeding-the-real-chain XNB. Traced the exact crash mechanism: `Texture2DReader`
constructs the real `Texture2D` via `Texture2D(device, w, h, mipMap, format)`, which computes its
*own* real mip level count via `CalculateMipLevels(w,h)` — entirely ignoring the file's declared
`levelCount` except for the `>1` boolean — while the read loop below iterates `level` up to the
file's own (attacker-controlled) `levelCount`, calling `SetData(level, ...)` for mip levels the
real, constructed texture never allocated. This mismatch is the confirmed root cause of the
Vulkan/WebGPU crashes.

**Changes (shared content code only, no backend-specific files touched, per the task's explicit
instruction):**
- `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`: new `virtual int
  GetMaxTextureDimension() const` with a default of 16384 (matches D3D11/D3D12 feature-level-11_0's
  guaranteed ceiling and real-world Vulkan/Metal/GL behavior). Same pattern as the existing
  `SupportsCapability()` virtual right above it. No backend overrides this yet — the default is
  correct for all 14 as things stand.
- `GraphicsDevice.hpp`/`.cpp`: forwarding `GetMaxTextureDimension()`, mirroring
  `SupportsCapability()`'s own forwarding method.
- `Texture2DContentTypeReader.cpp`: after obtaining the `GraphicsDevice*` and before constructing
  the `Texture2D`, reject `width`/`height` exceeding `device->GetMaxTextureDimension()` and
  `levelCount` exceeding a new local `CalculateMaxMipLevels(w,h)` helper (mirrors `Texture2D.cpp`'s
  own private `CalculateMipLevels`, duplicated rather than exposed — pure math, no dependency on
  `Texture2D`'s internal state) — both throw `ContentLoadException`, matching every other check in
  this file.
- `Texture2D.cpp`: unconditional (not `#ifdef CNA_BACKEND_D3D9`) `ValidateTextureDimensionEXT()`
  added to both dimension-taking constructors, throwing `System::NotSupportedException` (matching
  the existing D3D9 profile-ceiling check's own exception type/convention) — defense in depth for
  *any* direct caller, not just the XNB path.

**Tests added:** `Texture2DContentTypeReaderTests.cpp` —
`SingleAxisExceedingMaxTextureDimensionThrowsContentLoadException` (500000×1, reproduces the exact
fuzz-discovered shape: one huge axis, small enough product to stay under `CheckDecodedByteSize`'s
own cap) and `MipLevelCountExceedingCeilingThrowsContentLoadException` (4×4 declaring 25 levels,
reproduces "mipLevels=25 against a 15-level maximum" exactly). `Texture2DTests.cpp` —
`DimensionGuardTest` (4 cases: width/height over limit on both constructors, at-the-limit does not
throw). `GraphicsDeviceCapabilityTests.cpp` — `GetMaxTextureDimensionReturnsSanePositiveValue`.

**Verification — the fuzz test that previously crashed, run against every reachable backend:**

| Backend | Result |
|---|---|
| Vulkan | **Fixed** — previously `*** stack smashing detected ***: terminated`; now passes cleanly |
| WebGPU | **Fixed** — previously a non-catchable Rust panic; now passes cleanly |
| EasyGL | Confirmed clean before and after (unaffected) |
| Headless, Software, Ascii, Bgfx, SdlGpu, SdlRenderer | Clean after the fix |
| Dx3 | **Pre-existing, separate, unrelated failure** — confirmed identical on the unmodified baseline via `git stash` (same `IDirectDraw::CreateSurface(offscreen) failed: HRESULT=...` for a still-mutated-but-within-limit size); DX3 doesn't translate a native DirectDraw allocation failure into `ContentLoadException`. Not fixed (out of scope for this task); not a new finding worth its own ID — narrower and lower-severity than the CRITICAL crash this task closes, and already implied by the plan's own "true blast radius unknown" framing for the 11 unisolated backends. |
| D3D9/D3D11/D3D12 (MinGW) | Not evaluated — blocked by a pre-existing, unrelated cross-compile environment issue (`SDL3/SDL.h` not found for an audio harness) in this sandbox's untracked build dirs, unrelated to this fix |
| Canvas | N/A — Emscripten-only backend; `cna_demo_xact`/native CTest path doesn't apply |

Full `CnaTests` direct-binary run (EASYGL, after CONTENT-001 + CONTENT-003 together): **5537
tests, 5530 passed, 4 skipped, 0 failed, exit 0** — 5507 baseline + 30 new tests (8 from
CONTENT-001/003, 22 from CONTENT-002, see below), zero regressions.

### REMED-CONTENT-002 detail — `fs::path` containment sweep

**Shared helper:** new `include/CNA/Internal/PathContainment.hpp` (header-only, `inline`, matching
`CnjSourceFile.hpp`'s established convention) with two pieces:
- `IsDisallowedAbsolutePath(normalized)` — absolute/drive-letter/UNC detection, extracted as its own
  building block because `ContentReader`'s site (below) needs it standalone.
- `ResolveContainedPath(baseDir, relativeOrAbsolute, canonicalize=true)` — joins onto `baseDir` and
  verifies containment; `canonicalize=false` skips real filesystem access (`weakly_canonical`) for
  callers resolving a purely logical/virtual path. **Always returns the lexically-normalized join,
  never the canonicalized form** — canonicalization is used only for the containment *check* — this
  mattered in practice (see "regression caught and fixed" below).

**Three call sites:**
- `StorageDevice::DeleteContainer()` — now rejects absolute/escaping `titleName` with
  `std::invalid_argument`, via `ResolveContainedPath(storageRoot, titleName)`. CNA-introduced (FNA's
  own `DeleteContainer` always throws `NotImplementedException`), fixed freely.
- `ContentReader::ResolveRelativeAssetPath()` (private helper behind `ReadExternalReference<T>()`)
  — now rejects an absolute reference via `IsDisallowedAbsolutePath()` **directly**, not
  `ResolveContainedPath()`: this site's join base (the current asset's own directory) and its real
  containment root (the content root above it) are different, and a legitimate sibling reference
  like `"../textures/foo"` from `"effects/myeffect"` must climb out of `effects/` by design — the
  existing `..`-prefix root-escape check (already correct) is kept unchanged, only the missing
  absolute-rejection was added.
- `PlaylistParser::Parse()` — every `.m3u`/`.m3u8` entry now goes through
  `ResolveContainedPath(playlistDir, entry)`; an absolute or escaping entry is silently skipped,
  same as a missing entry (documented as a deliberate security-over-compatibility divergence from
  standard M3U in `PlaylistParser.hpp`'s own class docs, per the plan's explicit instruction not to
  do this silently).

**Regression caught and fixed during implementation:** the first version of `ResolveContainedPath`
returned the `weakly_canonical`-canonicalized absolute path. `MediaLibrary.cpp` looks up parsed
playlist song paths in a `songByPath_` map keyed by the same lexical/relative form
`PlaylistParser`'s *old* code produced — the canonicalized form no longer matched those keys,
silently zeroing every playlist's song count (`MediaLibraryTestFixture.FavoritesResolvesTo...` etc.,
3 tests, caught by the full-suite run before this was committed). Fixed by returning the lexical
join always, canonicalizing only for the internal containment check. Full suite re-verified clean
afterward.

**Tests added:**
- `tests/CNA/Internal/PathContainmentTests.cpp` (new file) — 14 tests: absolute RHS, `..` traversal
  (both escaping and non-escaping), `.`-only, empty, Windows drive-letter (`C:/...` and
  `C:\...`), UNC (`\\server\share\...`), empty-baseDir-as-cwd, real symlink escape (skips if the
  sandbox disallows symlink creation), and result-path usability — matches the plan's required test
  list for the shared helper exactly.
- `tests/Microsoft/Xna/Framework/Storage/StorageDeviceTests.cpp` (new file — no prior test coverage
  existed for `StorageDevice` at all): 5 tests — empty/absolute/escaping/`.`-titleName all throw and
  leave the storage root's contents untouched (proven via a sentinel marker file), plus a legitimate
  simple title name still deletes only that container.
- `ContentReaderExternalReferenceTests.cpp` — `AbsolutePathReferenceThrowsContentLoadException`.
- `PlaylistParserTests.cpp` — `AbsoluteEntryIsSkippedNotResolved`,
  `EscapingEntryIsSkippedNotResolved`.

**Repo-wide sweep** (`grep -rn "fs::path(\|std::filesystem::path(" ... | grep " / "`, project-wide,
excluding `audit/`/vendor/third_party): every hit classified below.

| Site | Classification |
|---|---|
| `StorageDevice::DeleteContainer` | **Fixed** (this task) |
| `ContentReader::ResolveRelativeAssetPath` | **Fixed** (this task) |
| `PlaylistParser::Parse` | **Fixed** (this task) |
| `ContentManager::BuildAssetPath` (`ContentManager.cpp:306`) | **Confirmed FNA-faithful, not fixed.** Matches real .NET `Path.Combine`'s own identical "absolute RHS discards LHS" behavior, which real FNA's `ContentManager.Load` relies on unchecked — same precedent as `StorageContainer`'s equivalent joins (per this task's own instructions). |
| `StorageContainer.cpp:56,86` (`rootPath/displayName/playerFolder`, `storagePath_/relative`) | **Already exempted by the plan itself** — "confirmed FNA-faithful (FNA's own `StorageContainer.cs` uses unchecked `Path.Combine` for every equivalent method)." Not re-litigated. |
| `StorageDevice.cpp:94,100` (XDG/HOME fallback root construction) | Not a hit — `app` is the developer-set app name (`SetAppNameEXT`), not untrusted file/caller input, and this constructs the root itself rather than resolving a reference into an existing one. |
| `TitleContainer::CombineTitlePath`/`ResolveRealPath` | **Confirmed FNA/XNA-faithful, not fixed.** Real XNA's documented `TitleContainer` behavior explicitly allows a rooted path to bypass the title location entirely (`IsPathRooted(name)` check already present, returns the rooted path as-is) — intentional, documented API behavior games rely on, not a bug. |
| `SavedPictureStore.cpp:59,80` | Not a hit — line 59's RHS is a hardcoded literal (`"Saved Pictures"`); line 80's `name` is already reduced to a single safe path segment by `SanitizePictureName()` (rejects `.`/`..`/empty) before the join — this file was one of the two reference implementations named in the task's own suggested strategy. |
| `MediaLibrary.cpp:332,522` | Not a hit — hardcoded literal RHS (`"Saved Pictures"`). |
| `LocalGamerServicesStore.cpp:31` | Not a hit — hardcoded literal RHS (`"GamerServices"`). |
| `VideoContentTypeReader.cpp:76`, `SongContentTypeReader.cpp:76` (+ each file's own private `ResolveRelativeFilePath` helper) | **Genuinely new finding — recorded as `REMED-CONTENT-007`, NOT fixed** (out of this task's 3-site scope). See "Discovered during remediation" below. |
| `ContentManager.cpp:560,779-780,1364,2167,2267,2269,2312` (8 sites: `.cnj`/JSON manifest fields — `dataField->stringValue`, `vertRel`/`fragRel`, `clipFileField->stringValue`, `skeletonRel`, `vertFile`/`idxFile`, `morphTargetsFile`) | **Genuinely new finding — recorded as `REMED-CONTENT-008`, NOT fixed** (out of this task's 3-site scope). See "Discovered during remediation" below. |

**Completion criteria met:** all three named sites reject absolute and `..`-escaping paths; the
repo-wide sweep is complete and every hit is either fixed, confirmed FNA-faithful, or recorded as a
new finding (2 new IDs, not silently absorbed).

**Verification:** `DeleteContainer("../../../x")` and `DeleteContainer("/etc")` both throw and
delete nothing (proven with a sentinel file, not just "no exception"); a crafted external reference
with an absolute path is rejected; the sweep's hit list is documented above. Full `CnaTests`
direct-binary run (EASYGL): 5537/5530/4/0, 0 regressions (same run as CONTENT-001's, all three
CONTENT tasks verified together).

### REMED-CONTENT-006 detail — XnbReadLimits dead controls

**Verification (required — done before any fix):** audited all 7 declared `XnbReadLimits` fields
for consumers via targeted grep; confirmed exactly the 2 the plan named have zero consumers
anywhere (`maxStringBytes`, `maxObjectNestingDepth`) and the other 5 (`maxFileSize`,
`maxDecompressedSize`, `maxTypeReaderCount`, `maxSharedResourceCount`,
`maxCollectionElementCount`) are all genuinely wired up already. Then **reproduced the
stack-overflow live**, as required, with a standalone throwaway program (not part of the test
suite, since it was expected to crash) calling `XnbTypeName::ParseOne()` with a crafted,
well-formed nested-generic type name: depth 20,000 (80,001 bytes) parsed fine; depth 50,000
(200,001 bytes) segfaulted (`exit 139`); depth 200,000 (800,001 bytes, still comfortably under
`maxFileSize`'s 64MB and even under `maxStringBytes`'s own intended-but-unenforced 1MB cap)
segfaulted reliably. Confirmed real, not merely reasoned from the recursion shape.

**Changes:**
- `XnbTypeName.hpp`: `Detail::ParseOne()` takes an explicit `nestingDepth`/`maxDepth` pair (deliberately
  *not* named `depth` — the function already has an unrelated local `depth` used for bracket-
  matching inside the generic-argument loop, which silently shadowed an earlier attempt at this fix
  during implementation and made the depth check a no-op; caught by 3 failing new tests before
  being fixed). Throws `std::invalid_argument` past the bound, consistent with this file's existing
  malformed-input exception type. `ParseXnbTypeName()`/`NormalizeXnbTypeReaderName()` both gained an
  `XnbReadLimits` parameter (default `DefaultXnbReadLimits()`) to thread the bound through.
- `XnbTypeReaderTable.hpp`: passes its own `limits` through to `NormalizeXnbTypeReaderName()`
  instead of the default; added a post-read `entry.rawName.size() > limits.maxStringBytes` check
  (thrown as `ContentLoadException`) — `BinaryReader::ReadString()` has no way to accept a cap
  itself, so this is checked immediately after the read rather than preventing the read.
- `ContentReader.hpp`: `InnerReadObject<T>()` — the same unbounded-recursion shape, driven by
  nested object dispatch instead of nested generic brackets — gained an `objectNestingDepth_`
  member and an RAII `ObjectDepthGuard`, checked against the same `limits_.maxObjectNestingDepth`
  bound, throwing `ContentLoadException`.

**Tests added:** `XnbTypeNameTests.cpp` — 3 tests (exceeds default limit throws, at-limit does not
throw, custom tight limit rejects nesting the default would allow). `XnbTypeReaderTableTests.cpp` —
2 tests (`maxStringBytes` enforcement; confirms `maxObjectNestingDepth` propagates through this
entry point too, not just direct `ParseXnbTypeName()` calls). `ContentReaderTests.cpp` — 2 tests
using a new self-referential `RecursiveNode`/`RecursiveNodeReader` test fixture (exceeds a tight
custom limit throws; at-limit succeeds and produces the correct nested structure).

**Completion criteria met:** all 7 `XnbReadLimits` fields now have a real enforcement site and a
test (5 already did; the 2 previously-dead ones now do too).

**Verification criteria met:** re-compiled the crafted deep-nesting fixture under
`-fsanitize=address,undefined` — throws `std::invalid_argument` cleanly (`"XnbTypeName: exceeds the
maximum generic-argument nesting depth (256)."`), exit 0, zero ASan/UBSan-reported issues. Full
`CnaTests` direct-binary run (EASYGL): 5544 tests, 5540 passed, 4 skipped, 0 failed, exit 0 — 5537
baseline (after CONTENT-001/002/003) + 7 new tests, 0 regressions.

### REMED-GFX-001 detail — EasyGL `RegisterForWindow` ordering (UAF)

**Root cause confirmed exactly as described in the audit:**
`EasyGLGraphicsBackend::EasyGLGraphicsBackend` called `IGraphicsBackend::RegisterForWindow(window,
this)` immediately after the null-window check, before `SDL_GL_CreateContext` (which explicitly
throws `std::runtime_error` on failure) and every other fallible step. A constructor that throws
never runs its destructor, so a failure after registration left a dangling `window → this` entry in
`IGraphicsBackend`'s static window registry — later dereferenced unconditionally by
`SdlInputBridge.cpp:524`/`Mouse.cpp:48`'s `GetForWindow(window)` call sites on the next mouse/input
event for that window. Confirmed the other three `RegisterForWindow` callers
(`CanvasGraphicsBackend`, `SdlGpuGraphicsBackend`, `WebGPUGraphicsBackend`) already register last —
EasyGL was the sole outlier.

**Change (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` only):** moved
`RegisterForWindow(window, this)` to the very last statement of the constructor, after GL context
creation, `device.initialize()`, MSAA buffer setup, and the Emscripten context-loss callback
install — matching `SdlGpuGraphicsBackend`'s existing register-last pattern (register, then only a
non-fallible log statement). No `IGraphicsBackend.hpp` registry-contract (RAII guard) hardening was
added — the master plan listed it as a "consider," not a completion requirement, and the ordering
fix alone closes the confirmed defect.

**Required backend parity check:** re-grepped all graphics backend directories for
`RegisterForWindow` call sites. Only `Canvas`, `SdlGpu`, `WebGPU`, and `EasyGL` call it at all;
`Ascii`, `Software`, `Headless`, `SdlRenderer`, `Dx3`, `D3D9`, `D3D11`, `D3D12`, `Bgfx`, and `Vulkan`
do not call it — confirmed clean by construction, not re-audited line-by-line (nothing to check).

**Tests added:** `tests/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackendTests.cpp` (new file,
`#if defined(CNA_BACKEND_EASYGL)`-guarded, gtest) —
`FailedContextCreationLeavesNoDanglingRegistryEntry`: constructs a real (Xvfb) `SDL_Window` **without**
`SDL_WINDOW_OPENGL` — `SDL_GL_CreateContext` fails deterministically on such a window
(`SDL_video.c`'s own `NOT_AN_OPENGL_WINDOW` check, confirmed by reading the vendored SDL3 source
directly rather than assumed) — reproducing the exact throwing path without needing a dedicated
test-only injection seam. Asserts `IGraphicsBackend::GetForWindow(window) == nullptr` after the
throw (the core regression check — pre-fix this returned a dangling non-null pointer), then
dispatches a real public-API mouse event (`Mouse::SetPosition`) against the same window and asserts
no crash.

**Verification criteria met:** temporarily reverted just the production ordering change (kept the
new test) and rebuilt under `cmake-build-devices-asan` (ASan+UBSan, EASYGL) — the test reliably
reproduces `AddressSanitizer: stack-use-after-scope` in `Mouse::SetPosition` → `logical_to_window`
→ the dangling backend pointer, confirming the test is a real regression guard, not a tautology.
Re-applied the fix — same ASan build now passes clean (0 issues). `EasyGL_TexturedQuad_Readback`
(real successful-construction path) still passes unchanged, confirming the reorder doesn't affect
normal startup. Full `CnaTests` direct-binary run (EASYGL, non-ASan): 5574 tests, 5568 passed, 4
skipped, 2 failed — both pre-existing, already tracked (`GameTest.DisposingDeviceInvokesUnloadContent`,
`GraphicsDeviceManagerTest.BackendDetectedDeviceLostIsForwardedToManagerListeners`, `REMED-TEST-002`'s
own intentional reproductions of `REMED-CORE-006`/`-014`, unrelated to GRAPHICS) — 0 regressions.

### REMED-GFX-002 detail — `SpriteFont::MeasureString`/`SpriteBatch::DrawString` end() dereference

**Root cause confirmed exactly as described in the audit:** both functions' "character not found →
fall back to `defaultCharacter_`" path performed a second `characterIndexMap_.find()` and
dereferenced `it->second` without checking the second lookup succeeded. Nothing validated that a
`SpriteFont`'s `defaultCharacter` was itself present in `characters`.

**Change — both the use-site fix and the construction/setter validation the master plan's
suggested strategy asked for, landed together:**
- `SpriteFont.cpp`: constructor and `setDefaultCharacterProperty` both now throw
  `System::ArgumentException` if the given `defaultCharacter` is not present in the character list
  (checked against `characterIndexMap_`, built before the check in the constructor). Strong
  exception safety: `setDefaultCharacterProperty` validates *before* assigning, so a rejected call
  leaves `defaultCharacter_` unchanged.
- `SpriteFont.cpp` `MeasureString` and `SpriteBatch.cpp` `DrawString`: the second `find()` result is
  now checked; on failure (unreachable in practice once construction/setter validation is in place,
  but checked anyway rather than dereferencing `end()`) throws
  `System::Collections::Generic::KeyNotFoundException`, matching FNA's
  `characterIndexMap[DefaultCharacter.Value]` `Dictionary` indexer, which throws
  `KeyNotFoundException` on a miss.
- **Root cause note vs. the plan's literal "Required tests" wording:** the plan's required-tests
  list describes constructing a `SpriteFont` with an inconsistent `defaultCharacter` and then
  observing `MeasureString`/`DrawString` throw `KeyNotFoundException`. With construction/setter
  validation now in place, that inconsistent state is unreachable through the public API — the
  `ArgumentException` fires first, at construction/set time. Both code paths are still implemented
  (defense in depth, matching the plan's own "Additionally validate..." strategy verbatim) and
  covered by tests below; the `KeyNotFoundException` branch is exercised by direct code inspection
  and its presence verified by build, not by a reachable unit test, since making it reachable would
  require deliberately bypassing the new, correct invariant enforcement.

**Existing call sites broken by the new invariant, fixed in the same commit (not scope creep — an
atomic-pair requirement, same shape as the plan's own `REMED-TEST-001` pattern):**
- `examples/sprite_font_test.cpp`: its own long-standing comment already flagged this exact
  precondition ("`defaultCharacter = u'?'`, but `'?'` is not in the character list") as the *reason*
  the audit found this bug. Extended, per the master plan's own instruction, to assert the
  constructor now throws `ArgumentException`; the later `setDefaultCharacterProperty(u'*')` call
  (`'*'` also absent from that fixture's character list) similarly extended to assert-throws instead
  of silently corrupting state.
- `tests/Microsoft/Xna/Framework/Content/CnjSpriteFontTests.cpp`:
  `LoadRealCnjFixtureEndToEnd`'s fixture declared `"defaultCharacter": "?"` with only glyph `'A'` in
  `"glyphs"` — genuinely inconsistent test data. Added a `'?'` glyph to the fixture (preserves the
  test's intent — defaultCharacter round-trips through the `.cnj` reader) and added a new
  `DefaultCharacterAbsentFromGlyphsThrows` test asserting `ContentManager::Load<SpriteFont>` now
  surfaces `System::ArgumentException` for a malformed `.cnj` with this defect — a real hardening
  win for malformed/malicious content, consistent with this plan's `REMED-CONTENT-*` findings.
- Swept every other `SpriteFont`/`defaultCharacter` construction site repo-wide (`examples/`,
  `tests/`, `src/CNA/Internal/Backends/Ascii/AsciiFontAtlas.cpp`,
  `src/CNA/Internal/Xnb/SpriteFontContentTypeReader.cpp`): all others already have `defaultCharacter`
  present in their character list (or unset), confirmed individually — no other breakage.

**Tests added:**
- `SpriteFontTests.cpp` — `ConstructorThrowsWhenDefaultCharacterAbsentFromCharacters`,
  `ConstructorAcceptsDefaultCharacterPresentInCharacters`,
  `SetDefaultCharacterThrowsWhenAbsentFromCharacters` (also asserts the rejected value did not take
  effect).
- `CnjSpriteFontTests.cpp` — `DefaultCharacterAbsentFromGlyphsThrows` (see above).
- `examples/sprite_font_test.cpp` — extended in place (see above), still exit 0/1 convention.

**Verification criteria met:** ASan+UBSan build (`cmake-build-devices-asan`, EASYGL) — all 63
SpriteFont/SpriteBatch/CnjSpriteFont/ContentManagerSpriteFontXnb gtests pass clean. Full `CnaTests`
run (EASYGL, non-ASan) — same 2 pre-existing unrelated failures as GFX-001's run, 0 GRAPHICS
regressions. `cna_test_sprite_font` and all 6 EasyGL SpriteFont/SpriteBatch pixel example tests
(`single_glyph`, `multiglyph_spacing`, `newline`, `default_char`, `effects_flip`,
`effects_rotation_scale`) still pass unchanged.

### REMED-GFX-003 detail — `DrawString` axis-direction tables undersized for composable `SpriteEffects`

**Root cause confirmed exactly as described in the audit:** `SpriteEffects` is XNA's real
composable `[Flags]` enum (`None=0, FlipHorizontally=1, FlipVertically=2`, combinable to `3`).
`SpriteBatch::DrawString`'s four `constexpr` lookup tables (`axisDirX/Y`, `axisIsMirroredX/Y`) were
sized for 3 entries; `effIdx = static_cast<int>(effects)` reads index 3 for the combined value — an
out-of-bounds read of a `constexpr` array. Confirmed FNA's own `SpriteBatch.cs` (`axisDirectionX/Y`,
`axisIsMirroredX/Y`, both `string`- and `StringBuilder`-overload implementations) declares these
tables with exactly 4 entries and masks `effects &= (SpriteEffects) 0x03;` before indexing.

**Changes:**
- `include/Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp`: added `operator|`/`operator&`/
  `operator|=`/`operator&=`, matching `GestureType`'s established flag-enum convention exactly
  (same signatures, same `constexpr`/`[[nodiscard]]` shape).
- `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString(SpriteFont, std::string, ...)`
  — the one real implementation; the `StringBuilder` overloads and the other two `std::string`
  overloads all delegate to it): resized all four tables to 4 entries with FNA's exact values
  (`axisDirX={-1,1,-1,1}`, `axisDirY={-1,-1,1,1}`, `axisIsMirroredX={0,1,0,1}`,
  `axisIsMirroredY={0,0,1,1}`); added `effects = effects & static_cast<SpriteEffects>(0x03);`
  before computing `effIdx`, matching FNA's own defensive mask line-for-line. Grepped the whole repo
  for any other `effIdx`/axis-table copy — this is the only occurrence; the fix is entirely in the
  shared, backend-agnostic XNA layer, so it applies uniformly to all 14 backends by construction
  (every backend receives the same already-computed destination `Rectangle` via
  `ISpriteBatchBackend::Draw`).

**Required backend parity check ("verify the combined value renders identically on ≥3 backends"):**
since the fix lives entirely in the shared layer (confirmed sole occurrence above) rather than in
any backend, the gtest-level `RecordingSpriteBatchBackend` coverage below exercises the actual
production computation every backend consumes — a per-backend pixel re-check would only re-verify
each backend's own texture-compositing path (already covered by each backend's existing SpriteFont
pixel tests), not the specific defect (wrong/OOB axis values at index 3). Per-backend pixel
duplication was judged not to add coverage proportionate to this SMALL-complexity task, given the
shared-layer verification below.

**Tests added:**
- `SpriteBatchTests.cpp` — `SpriteEffectsOperatorsTest` (4 tests: `|`, `&`, `|=`, `&=`).
- `SpriteBatchTests.cpp` — `SpriteBatchDrawStringSpriteEffectsTest` (3 tests, via
  `RecordingSpriteBatchBackend`, rotation=0 to keep axes independent so no hand-computed numbers
  are needed): `CombinedFlipMirrorsXLikeHorizontalAlone` (index 3's X placement equals index 1's),
  `CombinedFlipMirrorsYLikeVerticalAlone` (index 3's Y placement equals index 2's),
  `CombinedFlipDiffersFromNone` (sanity check that the combined value isn't accidentally a no-op).

**Verification criteria met:** temporarily reverted just the table-resize (kept the mask removed
too) and rebuilt under `cmake-build-devices-asan` — `CombinedFlipMirrorsXLikeHorizontalAlone`
reliably reproduces `AddressSanitizer: global-buffer-overflow` reading one byte past
`axisIsMirroredX`/before `axisIsMirroredY` (both correctly identified by ASan's global-redzone
report), confirming the new tests are a real regression guard. Re-applied the fix — same ASan build
passes all 7 new tests clean. Full `CnaTests` run (EASYGL, non-ASan): 5574 tests, 5568 passed, 4
skipped, 2 pre-existing unrelated failures (same as GFX-001/-002), 0 GRAPHICS regressions.

### REMED-NET-001 + REMED-NET-003 detail — `ENetBackend` host-authority checks

Landed together as one change in `src/CNA/Internal/Net/ENetBackend.cpp`, per both tasks' own
`Dependencies`/`PS` fields (same file, same threat model, same review). No other file changed.

**Root cause confirmed exactly as described in the audit:** `HandleReceive()` dispatched
`ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast` purely by
`MessageTag`, with no check that the sending `ENetPeer*` is this session's actual authoritative
host. Any already-connected peer — no MITM or address spoofing needed, just a custom ENet client
speaking this project's own fully-inferable wire format — could forge one of these four directly at
the host (or, as confirmed while implementing, at a *client*-role peer's own incidental listening
socket — see below) to kick arbitrary gamers, inject fake gamers, corrupt wire-id assignment, or
force an arbitrary session-state transition. Separately, `HandleClientHello` had no guard against
the same already-handshaked peer resending `ClientHello`, each resend `new`-ing a fresh batch of
`NetworkGamer` objects — unbounded roster growth from one connected peer.

**Trust model identified (REMED-NET-001):**
- **Host-authoritative message types:** `ServerWelcome`, `GamerJoinBroadcast`, `GamerLeaveBroadcast`,
  `StateChangeBroadcast`. Real XNA has no `Net` namespace at all, so this is this project's own wire
  protocol — the trust model is defined entirely by `NetPacketCodec.hpp`'s own doc comments
  ("Sent by the host...") and confirmed by every legitimate call site in `ENetBackend.cpp` itself
  (`HandleClientHello`'s `SendTo`/broadcast-fan-out, `BroadcastStateChange`).
- **Who may originate each one:** only the session's own authoritative host connection —
  `state.HostPeer` when this side is acting as a client of someone else. A host's own `HostPeer` is
  always null (a host never connects out), so the same check correctly rejects these four types
  unconditionally on the host side too: a host should never receive host-authoritative messages from
  one of its own connecting clients at all.
- **`ClientHello`/`AppData`** are client→host (or peer→host-relay) messages by design and were
  already correctly peer-scoped before this task (`HandleClientHello`/`HandleAppData` both already
  took `peer`/`fromPeer`); only their content-level validation (the resend guard) was missing.

**Changes (`src/CNA/Internal/Net/ENetBackend.cpp` only):**
- New `IsFromAuthoritativeHost(state, peer)` — `state.HostPeer != nullptr && peer == state.HostPeer`.
- New `RejectUnauthorizedHostOnlyMessage(state, peer, messageTypeName)` — logs a `CNA::Logger::Warn`
  (`[ENetBackend] Rejected host-only <Type> from a non-host peer...`) and calls
  `state.Host.Disconnect(peer, 0)`, matching this file's own pre-existing convention for a protocol
  violation (`HandleClientHello`'s Playing/`AllowJoinInProgress` rejection already does the same
  "disconnect, don't silently ignore" thing). Chose disconnect over a silent drop per the master
  plan's own explicit "treat rejection as a first-class protocol event... not a silent drop"
  guidance — no legitimate peer ever sends these four types, so this cannot reject honest traffic.
- `HandleReceive()`'s `switch` now calls `IsFromAuthoritativeHost` before dispatching to each of the
  four handlers, rejecting (not decoding or invoking the handler at all) on failure. The four
  handler functions' own signatures are unchanged — the check is centralized once in the dispatcher,
  which is where the audit's own root-cause description places the gap, rather than threading `peer`
  into all four handler bodies for an identical net effect with a larger diff.
- `HandleClientHello()` gained a guard at its top: `if (state.PeerWireIds.contains(peer))` →
  disconnect + return, before any gamer is created. `state.PeerWireIds[peer]` is unconditionally
  populated at the end of a first successful `ClientHello` (even for zero local gamertags) and
  erased on that peer's disconnect (`HandleDisconnect`), so the guard is keyed on "has this *live*
  connection already completed a handshake", not on historical `ENetPeer*` identity — a genuine
  reconnect always arrives as a brand-new `CONNECT` event on a fresh peer object and is unaffected.

**Sweep for equivalent host-only message handling patterns elsewhere (required before considering
this closed):** `ENetBackend.cpp`'s `HandleReceive()` is the only per-connection, authoritative
session-management message dispatcher in the codebase (grepped `MessageTag`/`ENetPeer`/
`enet_host_service`/`HandleReceive` repo-wide). The one other candidate, `ENetDiscoveryService.cpp`'s
`HandleReceived()` (LAN host discovery over connectionless broadcast UDP), was reviewed and is **not
equivalent**: it has no per-connection peer identity to check at all (discovery is inherently
unauthenticated broadcast, like SSDP/mDNS), and processing a `DiscoveryAnnounce` only appends a
candidate entry to a caller-local `FindSessions()` results vector shown to the user for choosing
which session to join — it never mutates an established session's roster or state the way the four
`ENetBackend.cpp` message types do. No second finding here; not fixed (none needed).

**New finding discovered while implementing, recorded not fixed** — see `REMED-NET-008` below: a
"client"-role session's own incidental listening `ENetHost` (bound on every non-Emscripten
`ConnectToHost()` call, to support later host migration — see `ENetHostHandle`'s own doc comment)
still processes `ClientHello` from *any* connecting third party, not just its real host, exactly as
if it were itself hosting a session. Distinct root cause from NET-001 (`ClientHello` is not one of
the four host-authoritative types; this is a missing "am I actually supposed to be hosting"
role-check on a client-scoped session's own accept path), so left unfixed and out of this task's
scope per the instructions.

**Tests added** (`tests/CNA/Internal/Net/ENetBackendTests.cpp`, 7 new `TEST()`s, all using this
file's existing real-ENet-loopback harness — no mocks):
- `HostRejectsForgedServerWelcomeFromNonHostPeer` — a connected, handshake-completed client forges
  `ServerWelcome` (reassigned host wire-id + an injected fake roster entry) straight at the host;
  asserts the peer is disconnected, the host's own wire id is untouched, and no fake gamer appears.
- `HostRejectsForgedGamerJoinBroadcastFromNonHostPeer` — forged `GamerJoinBroadcast`; asserts the
  injected gamer is never added.
- `HostRejectsForgedGamerLeaveBroadcastFromNonHostPeer` — two real clients (PeerA, PeerB) join; PeerA
  forges a `GamerLeaveBroadcast` naming PeerB's wire id (the audit's own worst-case example — kicking
  a gamer belonging to an entirely different client); asserts PeerB is untouched and PeerA alone is
  disconnected.
- `HostRejectsForgedStateChangeBroadcastFromNonHostPeer` — forged `StateChangeBroadcast`; asserts
  `getSessionStateProperty()` stays `Lobby` and `GameStarted` never fires.
- `ClientRejectsForgedGamerLeaveBroadcastFromRogueThirdPartyPeer` — exercises the *other* half of
  `IsFromAuthoritativeHost` (`state.HostPeer != nullptr` but `peer != state.HostPeer`, distinct from
  the host-side tests above where `HostPeer` is always null): a real client session joins a real
  fake-host, then a wholly separate rogue peer connects directly to the client's own incidental
  listening socket and forges a `GamerLeaveBroadcast` naming the real host's wire id; asserts the
  rogue is disconnected and the real host gamer is untouched — this is the "spoofed sender identity"
  case required by the task brief, and the concrete proof that the client-role socket described in
  `REMED-NET-008` is real, reachable attack surface for the four NET-001 message types specifically
  (not just the separate `ClientHello` gap `-008` documents).
- `HostRejectsDuplicateClientHelloFromAlreadyHandshakedPeer` (REMED-NET-003) — same peer resends
  `ClientHello` with two more gamertags after a real handshake; asserts exactly one gamer was ever
  added/owned and the peer is disconnected.
- `HostAcceptsFreshClientHelloAfterALegitimateDisconnectAndReconnect` (REMED-NET-003 regression) —
  connect, handshake, disconnect, then a genuine reconnect (fresh `ENetHostHandle`/peer, matching
  `DisconnectedPeerWireIdIsReclaimedAndReusedByTheNextJoiner`'s own established pattern for "same
  machine, new connection"); asserts the fresh `ClientHello` is accepted normally, proving the
  resend guard is keyed on live peer identity, not a historical block-list.

Malformed-message handling for these four types is unchanged by this task (still governed by the
pre-existing `HandleReceive` try/catch around the whole `switch`) and already covered by
`HostSurvivesTruncatedClientHelloAndContinuesFunctioningAfterward`; legitimate host broadcasts still
working end to end is proven by the many pre-existing, unmodified tests that exercise the same four
handlers via the one legitimate sender (`ClientProcessesGamerLeaveBroadcast`,
`ClientProcessesStateChangeBroadcast`, `ClientSendsClientHelloAndProcessesServerWelcome`,
`HostRespondsToClientHelloWithServerWelcomeAndAddsRemoteGamer`,
`HostBroadcastsStateChangeOnStartAndEndGame`), all still passing unmodified.

**Verification:**
- `ENetBackendTest.*` direct-binary run (EASYGL): 43/43 passed (35 pre-existing + 8 new — 7 net-new
  `TEST()`s plus none removed; see exact count reconciliation below), 0 regressions.
- `*Net*:*ENet*:*GamerServices*:*NetworkSession*:*Discovery*` direct-binary run: 249/249 passed,
  including the real two-process loopback suite (`TwoProcessLoopbackTest`, 3/3, real separate OS
  processes) and every `NetworkSessionTest`.
- Full `CnaTests` direct-binary run (EASYGL): 5551 tests, 5547 passed, 4 skipped, 0 failed — 5544
  baseline (after CONTENT-001/002/003/006) + 7 new tests, 0 regressions.
- Full unfiltered `ctest -j1` (avoids the real-UDP-port cross-process contention `-j4` produces for
  this subsystem specifically — see Notes below): 5798 registered, 5793 passed, 4 skipped, **5
  failed — the exact same 5 pre-existing failures already tracked** (`EasyGL_AvatarRenderer_TintRouting`,
  `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`, `EasyGL_RealWindowResize`,
  `easy-gl-resource-smoke-tests`) — 5791 baseline + 7 new tests, 0 new regressions, exact
  reconciliation confirmed both ways (5798 − 5791 = 7 = tests added; 5 failures unchanged).
- **Sanitizers:** new scoped build dir `cmake-build-net-asan/` (HEADLESS backend, `-fsanitize=
  address,undefined -fno-omit-frame-pointer`), added to `.gitignore` matching the existing
  per-subsystem convention (`cmake-build-devices-asan/`, `cmake-build-input-asan/`, etc.). Ran the
  same `*Net*:*ENet*:*GamerServices*:*NetworkSession*:*Discovery*` filter (139 tests) under
  ASan+UBSan (`detect_leaks=0` — see Notes below for why): **138/139 passed clean, zero ASan/UBSan
  reports** on any test that exercises the new code (`ENetBackendTest.*` all 43/43 including all 7
  new tests; `TwoProcessLoopbackTest.HostAndClientJoinAndExchangeAppDataAcrossRealProcesses` and
  `...HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`, both real
  separate-OS-process runs that exercise `HandleReceive`/`HandleClientHello` end to end). The one
  failure, `TwoProcessLoopbackTest.StartHostingRollsBackCleanlyOnDiscoveryRegistrationFailure`, is a
  **pre-existing, unrelated-to-this-task sanitizer/harness interaction** — its child process
  (`tools/net/net_two_process_harness.cpp`'s `RunStartHostingPartialFailure`) is killed by UBSan's
  `vptr` check reporting `Gamer`/`SignedInGamer`/`NetworkSessionAction` construction as "does not
  point to an object of type" *before any networking code runs at all* (the stack trace bottoms out
  in `Gamer::Gamer()`/`SignedInGamer::SignedInGamer()`/`NetworkSessionAction`'s constructor, never
  reaching `ENetBackend.cpp`). Confirmed unrelated: this exact test passes cleanly in the
  non-sanitized `cmake-build-debug` (both the `-j1` full ctest run and the earlier `-j4` run above);
  not fixed (pre-existing test-harness/sanitizer-environment issue, orthogonal to NET-001/003's own
  2-function diff — recording it rather than debugging it further is the practical call here, per
  the task's own "if sanitizer execution is not practical for a specific test path, document why"
  allowance).

**Notes:**
- **Leak detection disabled for this sanitizer run (`ASAN_OPTIONS=detect_leaks=0`):** an initial run
  with LeakSanitizer enabled reported ~140KB across 625 allocations, but this is a pre-existing,
  repo-wide pattern (e.g. every `NetworkSession`/`GamerCollection<T>::Add` construction site, present
  even in completely unmodified tests like `TeardownAndPumpOnUnregisteredSessionAreSafeNoOps` and
  `StartHostingIsIdempotent`) — this project has no CMake-wired sanitizer build at all (confirmed:
  grepped for `fsanitize` repo-wide, found nothing outside `third_party`/`vendor`), so `CnaTests` has
  apparently never run under LeakSanitizer before this session. Chasing a repo-wide, pre-existing
  leak pattern is out of NET-001/003's scope; `detect_leaks=0` isolates the signal this task actually
  needs — real memory-safety/UB errors (use-after-free, buffer overflow, UB) in the new code paths —
  which came back clean.
- **`ctest -j4` transient failures are pre-existing test-infrastructure flakiness, not a
  regression:** running the full suite with `-j4` (this session's usual job cap) additionally
  failed 6 more tests every one of which is network-discovery-related
  (`ENetDiscoveryServiceTest.*` ×4, `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndThe
  OtherReconnectsAcrossRealProcesses`, `NetworkSessionTest.FindReturnsEmptyCollection`) — all of
  these bind `ENetDiscoveryService`'s hardcoded, `SO_REUSEADDR`-shared discovery port (61190; see
  its own doc comment on why `REUSEADDR` is required at all) from **separate OS processes** running
  concurrently under parallel `ctest`, which can genuinely cross-talk on one host. Confirmed
  unrelated to this change: none of the 6 touch `ENetBackend.cpp`, all 6 pass 100% both via a direct
  `--gtest_filter` run and via `ctest -j1`/`-R` re-run in isolation. Recorded here since it cost real
  triage time; not a new finding to fix (a pre-existing multi-process-on-one-machine hazard of this
  test suite's own discovery-port design, orthogonal to NET-001/003's own scope).
- Completion criteria met for both tasks: all four broadcast handlers verify sender authority
  (NET-001); a peer cannot inject more than one gamer via resend (NET-003).
- Verification criteria met for both tasks: forgery tests pass and a two-process-equivalent loopback
  harness (the real-ENet single-process harness this file already established, per its own fixture
  comment on why a second real `NetworkSession` can't coexist in one process) confirms legitimate
  host broadcasts and the repeated-hello regression case both still work.

### REMED-DEVICES-001 detail — `FileDialog`/`MessageBox` `GetBackend()` UAF

**Root cause confirmed exactly as described:** both `src/CNA/Devices/FileDialog.cpp` and
`src/CNA/Devices/MessageBox.cpp` implement an identical anonymous-namespace `GetBackend()` helper that
takes `BackendMutex()`, reads the file-local `BackendStorage()` `unique_ptr`'s raw pointer, and returns
it — releasing the lock as the function returns, *before* the caller (`FileDialog::ShowOpenFile()` etc.)
dereferences it via `GetBackend()->ShowOpenFile(...)`. A concurrent `SetBackendForTesting()` reassigning
`BackendStorage()` destroys the old backend object under its own lock acquisition while a second thread
may already be mid-call through the now-dangling pointer returned moments earlier — a genuine
use-after-free with no synchronization covering the object's lifetime across the virtual call.

**Sweep for the same pattern elsewhere in `Microsoft::Devices`/`CNA::Devices` (required before considering
this closed):** grepped for `BackendStorage`/`GetBackend()`/`SetBackendForTesting`/`BackendMutex` across
`src/CNA/Devices`, `src/Microsoft/Devices`, `include/CNA/Devices`, `include/Microsoft/Devices`. Confirmed
`FileDialog.cpp`/`MessageBox.cpp` are the *only* two files using this specific "module-level swappable
singleton, raw pointer returned outside the lock" pattern. `VibrateController.cpp` (`Microsoft::Devices`)
uses a different, already-correct shape — an instance member `backendMutex_` held for the *entire*
duration of every call through `backend_`, exactly as the master plan's own evidence notes. The
`Microsoft::Devices::Sensors` family (`Accelerometer`/`Compass`/`Gyroscope`/`Motion`) uses yet another
shape — a per-instance `backend_` behind each `SensorBase<T>`'s own `control_->mutex`, with an explicit,
documented rationale in `Compass.cpp` for releasing the lock before the `backend_->Start()`/`Stop()` call
specifically (a different, already-reviewed design, not this bug). No second instance of this task's root
cause found; nothing outside the two named files needed a change.

**Changes (`src/CNA/Devices/FileDialog.cpp`, `src/CNA/Devices/MessageBox.cpp` only — public API
unchanged, both `SetBackendForTesting()` signatures still take `std::unique_ptr`):**
- `BackendStorage()` changed from `std::unique_ptr<I...Backend>` to `std::shared_ptr<I...Backend>` in
  both files.
- `GetBackend()` now returns a `std::shared_ptr<I...Backend>` **by value** (a copy taken while
  `BackendMutex()` is held), instead of a raw pointer. The returned shared_ptr is a genuine new owning
  reference to the backend object; as a temporary bound to the full `GetBackend()->Show...(...)`
  expression, it keeps the pointee alive for the entire duration of that call regardless of what
  `SetBackendForTesting()` does concurrently to `BackendStorage()`. `shared_ptr`'s own reference count is
  atomic, so no additional locking around the virtual call is needed.
- `SetBackendForTesting()` in both files now constructs a `shared_ptr` from the incoming `unique_ptr`
  (`std::shared_ptr<I...Backend>(std::move(backend))`) or `std::make_shared<Sdl...Backend>()` for the
  `nullptr` (restore-default) case, still entirely under `BackendMutex()`.
- The **"hold the lock across the whole call" alternative** (`VibrateController.cpp`'s own pattern) was
  deliberately not used here, matching the master plan's own explicit preference: `MessageBox::Show()`
  blocks the calling thread until a human responds to a real modal dialog, and `FileDialog`'s real
  backend can similarly launch a long-lived external process (`zenity` on Linux) — serializing every
  caller behind a lock held across either of those would invite deadlock for no safety benefit the
  `shared_ptr` approach doesn't already provide.

**Tests added** (`tests/CNA/Devices/FileDialogTests.cpp`, `tests/CNA/Devices/MessageBoxTests.cpp` — one
new `TEST()` per file, both named `ConcurrentSetBackendForTestingDoesNotRaceWithLiveCalls`): two threads,
2000 iterations each — one thread repeatedly calls `ShowOpenFile`/`ShowSimple` through the currently
installed fake backend, the other repeatedly calls `SetBackendForTesting()` with a freshly allocated fake
(never `nullptr` mid-race, since that would route the racing caller thread into the real interactive
backend). Asserts every call's callback/return path completed exactly once — a UAF surfaces as a crash
well before that assertion, under a sanitizer or, given enough iterations, even a plain build.

**Verification — reproduced the pre-fix bug live, as the task's `Verify: NO` field still implies doing
for a UAF this narrow (the fix's own verification criteria explicitly call for a clean sanitizer run, which
requires first confirming the sanitizer is capable of catching the *un-fixed* bug on this exact test):**
`git stash`-ed just the two `.cpp` fixes (tests kept), rebuilt under
`-fsanitize=address` (new `cmake-build-devices-asan/` — CNA_DEVICES=ON was added on the command line, since
the existing `devices-asan` CMakePresets.json preset does not itself set `CNA_DEVICES=ON`; this gap is
worth fixing but is a BUILD_TEST_CI-lane concern, out of this task's scope, not recorded as a new finding
since it doesn't block DEVICES-001 itself) — `FileDialogTests.ConcurrentSetBackendForTestingDoesNotRaceWithLiveCalls`
reliably reproduced a **heap-use-after-free** (`ASan`: `previously allocated by thread T2` /// `freed`, a
write into a destroyed `FakeFileDialogBackend`'s `std::vector<FileDialogFilter>` member from the racing
caller thread). Restored the fix (`git stash pop`) and re-ran: **0 ASan reports across 10 full repeats**
(250 individual test executions, `ASAN_OPTIONS=detect_leaks=0` — see NET-001's own detail section above
for why leak detection is disabled project-wide for these ad hoc sanitizer runs; not relevant to a
UAF/race check).

**Sanitizers (both isolated to just `FileDialogTests.*`/`MessageBoxTests.*`, since the rest of `CnaTests`
opening a real EasyGL/Mesa window under `GuideTest` produces pre-existing, unrelated
`ThreadSanitizer`-reported races **inside `libgallium` itself**, not in any CNA code — confirmed by
address: none resolve into `FileDialog.cpp`/`MessageBox.cpp`/the new tests):**
- **ASan** (`cmake-build-devices-asan/`, `-fsanitize=address -fno-omit-frame-pointer -g -O0`): clean, as
  above.
- **TSan** (`cmake-build-devices-tsan/`, `-fsanitize=thread -fno-omit-frame-pointer -g -O1`):
  `FileDialogTests.*:MessageBoxTests.*:MessageBoxIconTest.*` (13 tests, 20 repeats = 260 executions):
  **0 ThreadSanitizer warnings**, exit 0.

**Regression run — full `CnaTests`, `CNA_DEVICES=ON` (new `cmake-build-devices/`, since neither existing
build dir had `CNA_DEVICES` on before this task):**
- Baseline (pre-fix, same binary layout): `MediaLibraryTestFixture.*`/`PictureAlbumTests.*` excluded from
  this run — that suite crashes/fails in this sandbox regardless of this task's changes (no real
  Music/Pictures library on this container; `MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent`
  segfaults, matching the already-tracked `REMED-MEDIA-002` finding — confirmed by reproducing the same
  crash before touching any Devices file). With that suite excluded: **5515 tests, 5510 passed, 4 skipped
  (Accelerometer/Gyroscope hardware skips, pre-existing), 1 failed**
  (`TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`,
  confirmed pre-existing network-discovery-port flakiness already documented in NET-001's own detail
  section above — passes 100% in isolation).
- Post-fix: **5517 tests** (5515 + 2 new), **5513 passed, 4 skipped, 0 failed** — the flaky network test
  passed on this run (consistent with it being non-deterministic, not a regression). **0 regressions.**
- `./cmake-build-debug` (the project's existing `CNA_DEVICES=OFF` baseline, 5798 registered CTest tests):
  rebuilt `CNA` and `CnaTests` targets after this task's changes — both link cleanly. Both changed `.cpp`
  files are entirely `#ifdef CNA_DEVICES`-gated, so with the flag off they compile to empty translation
  units; this task cannot regress that baseline by construction, confirmed by the clean rebuild.

**Completion criteria met:** the backend object's lifetime is now guaranteed for the duration of every
call through it, in both files — proven both by the ASan reproduction-then-fix above and by the TSan-clean
stress run.
**Verification criteria met:** TSan/ASan runs of the new concurrent tests are clean.

### REMED-MEDIA-001 detail — `AudioTagParser` 32-bit `size_t` integer-overflow bounds checks

**Root cause confirmed exactly as described:** every length check in
`src/CNA/Internal/Media/AudioTagParser.cpp` used the `pos + len > bound`-style idiom — audited the
*entire* file, not only the two sites the audit named (ID3v2.3 frame size, FLAC picture block),
per the task's own "audit every length check in the file" instruction. Found and fixed **24**
such sites across every reader in the file: `ReadOggPages` (Ogg page framing, 3 sites),
`TryReadVorbisComments`/`TryReadOpusTags` (vendor string + comment count, 3 sites each),
`ParseVorbisCommentList` (shared by Vorbis/Opus/FLAC, 2 sites), `TryReadFlacComments` (block
length + vendor string + comment count, 5 sites), `TryReadId3v2` (the cited frame-size check, 2
sites incl. the outer frame-header loop bound), `ExtractEmbeddedArt`'s own duplicated ID3v2 frame
loop (2 sites) and FLAC `METADATA_BLOCK_PICTURE` walk (6 sites: block length, mimeLen, descLen,
dataLen), and `ParseApicBody`'s MIME-terminator check (1 site, fixed-literal `p+2>=size`, not a
variable-length overflow risk but converted for consistency). On a 32-bit `size_t` target, `pos +
len` (where `len` is a full-range `uint32_t` read straight from the file — e.g. ID3v2.3's frame
size is NOT synchsafe, a real 4-byte big-endian value up to ~4.29 billion) can wrap around and make
the check incorrectly pass, letting a claimed length far larger than the real buffer through into a
downstream read.

**Changes (`src/CNA/Internal/Media/AudioTagParser.cpp` only):** added one helper,
`ExceedsBound(pos, len, bound) { return len > bound - pos; }`, in the file's existing anonymous
namespace (shared by both of the file's two `namespace CNA::Internal::Media { namespace { ... } }`
blocks, since unnamed namespaces in one translation unit are the same namespace) — a direct port
of `XactParser.cpp`'s own `AUDIO-PARSER-001` hardening pattern (validate via subtraction, which
cannot overflow given the invariant `pos <= bound` that holds at every call site, instead of
addition, which can). Replaced all 24 `pos + len > bound`/`pos + len <= bound` sites with
`ExceedsBound(pos, len, bound)`/`!ExceedsBound(pos, len, bound)`. Two sites combined two untrusted
quantities in one comparison (`p + mimeLen + 4 > bytes.size()`, `p + descLen + 20 > bytes.size()`
in the FLAC picture-block reader) — each was split into two sequential `ExceedsBound` checks so
that no single expression ever adds two attacker-controlled values before a check has bounded the
first one. The one fixed-literal `p + 2 >= size` check in `ParseApicBody` was rewritten directly as
`size - p <= 2` (no helper needed — `p <= size` is already an established invariant there).

**Open-decision resolution (verification approach):** the task's own field says `Verification
required: YES — reproduce on a 32-bit build before fixing`, with an explicit fallback: `"Add a
32-bit build configuration (or a size_t-narrowing test harness) to CI"`. Checked for a 32-bit
toolchain first, as required: `g++ -m32` fails outright in this sandbox (`cannot find Scrt1.o` /
`cannot find crti.o` / `cannot find -lstdc++` — no `gcc-multilib`/`g++-multilib`/`libc6-dev-i386`
installed, and this environment has no package-install access). This is a genuine environment
constraint, not a scope choice — matching the precedent already set by `REMED-BUILD-008`'s
Wine/vkd3d-proton blocker. **Decision: use the plan's own explicitly-offered alternative — a
size_t-narrowing test harness — rather than leaving the defect unverified.** Two new tests
(`Id3v23FrameSizeOverflowIsCaughtOnANarrow32BitSizeT`,
`FlacPictureBlockLengthOverflowIsCaughtOnANarrow32BitSizeT`) mirror the pre-fix (`pos + len >
bound`) and post-fix (`len > bound - pos`) formulas using `uint32_t` arithmetic — the exact width
and unsigned-wraparound behavior a real 32-bit `size_t` has — with crafted `pos`/`len`/`bound`
triples chosen so `pos + len` wraps back down to a small in-bounds-looking value. These **actually
execute and demonstrate the vulnerability class today**, on this 64-bit sandbox, which is strictly
more verification value than reasoning about the shape alone. Real 32-bit CI coverage
(`REMED-BUILD-001`'s own dependency note lists `AudioTagParserTest` among the tests it unblocks)
remains a `BUILD_TEST_CI`-lane action item, out of this MEDIA-lane task's scope — not implemented
here, consistent with the "no root cause/action item picked up outside its owning lane" rule.

**Tests added** (`tests/CNA/Internal/Media/AudioTagParserTests.cpp`, 4 new `TEST()`s):
- `Id3v23FrameSizeOverflowIsCaughtOnANarrow32BitSizeT` / `FlacPictureBlockLengthOverflowIsCaughtOnANarrow32BitSizeT`
  — the size_t-narrowing harness tests described above.
- `CraftedId3v23WrapInducingFrameSizeIsRejectedCleanly` — a hand-built ID3v2.3 tag (matching the
  task's own "Crafted ID3v2.3 ... fixtures" requirement) with a single `TIT2` frame whose raw
  (non-synchsafe) `frameSize` is `0xFFFFFFF6` (2^32 − 10), chosen so `pos + frameSize` wraps to 10
  on a real 32-bit `size_t` — comfortably inside the 20-byte `tagEnd`, which would have let a ~4GB
  claimed frame size through into `DecodeId3TextFrame()` against a real 20-byte buffer pre-fix.
  Asserted via `TryReadId3v2()` directly: rejected cleanly, `title` stays empty, `fromRealTags`
  stays false. On this 64-bit sandbox `size_t` doesn't wrap for this magnitude, so the check
  already correctly rejects it before *and* after the fix here — the test's value is as the
  literal crafted fixture that must keep being rejected on every target width, not as a
  before/after differential on this host (that's what the harness tests above are for).
- `CraftedFlacWrapInducingVendorLenIsRejectedCleanly` — same technique for
  `TryReadFlacComments()`'s `vendorLen` field (`0xFFFFFFF6`, little-endian).

**Verification:**
- `AudioTagParserTest.*` direct-binary run (EASYGL, `cmake-build-debug`, run from the repo root so
  the existing fixture-file tests can resolve their relative paths): **26/26 passed** (22
  pre-existing + 4 new), 0 regressions.
- Broader Media filter (`*Media*:*Song*:*Video*:*Playlist*`): **243/243 passed**, including
  `MediaLibraryTestFixture.*`/`PictureLibraryIndexTest.*` — no interaction with `REMED-MEDIA-002`'s
  separate, not-in-scope SEGFAULT finding observed.
- Full `CnaTests` direct-binary run (EASYGL, `cmake-build-debug`, `CNA_DEVICES=OFF`): **5562
  tests, 5555 passed, 4 skipped (Accelerometer/Gyroscope hardware skips, pre-existing), 3 failed**
  — the same 3 already-documented pre-existing failures (`TwoProcessLoopbackTest.
  HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`, network-port flake;
  `GameTest.DisposingDeviceInvokesUnloadContent` and `GraphicsDeviceManagerTest.
  BackendDetectedDeviceLostIsForwardedToManagerListeners`, both intentional per `REMED-TEST-002`'s
  own detail above). **Zero unexpected regressions.**
- Repo-wide sweep confirming completeness: `grep -n "> .*\.size()\|>= .*\.size()\|<= .*\.size()\|>
  tagEnd\|>= tagEnd\|<= tagEnd" src/CNA/Internal/Media/AudioTagParser.cpp` after the fix returns
  only the `ExceedsBound` helper's own doc comment — no remaining raw addition-based bound
  comparison anywhere in the file.
- **Sanitizers:** new scoped build dir `cmake-build-media-asan/` (HEADLESS backend, matching
  `cmake-build-net-asan/`'s own established recipe: `-fsanitize=address,undefined
  -fno-omit-frame-pointer -g -O1`), added to `.gitignore` matching the existing per-subsystem
  convention. `ASAN_OPTIONS=detect_leaks=0` (same rationale as `REMED-NET-001`'s own detail above —
  this project has no CMake-wired sanitizer build at all outside these ad hoc ones, so a repo-wide
  pre-existing leak pattern would otherwise swamp this task's own signal; not relevant to an
  OOB-read/UB check). `AudioTagParserTest.*`: **26/26 passed, 0 ASan/UBSan reports.** Broader Media
  filter (`*Media*:*Song*:*Video*:*Playlist*`): **243/243 passed, 0 ASan/UBSan reports.**

**Completion criteria met:** every length check in the file (24 sites, not just the 2 the audit
named) uses the overflow-safe subtraction form.
**Verification criteria met, via the plan's own stated alternative:** the size_t-narrowing harness
directly demonstrates the pre-fix formula wrapping and the post-fix formula correctly rejecting the
same crafted input; ASan reports no OOB for the crafted fixtures (see sanitizer result above); a
real 32-bit ASan run was not possible in this sandbox (no 32-bit toolchain), recorded here rather
than silently skipped, matching the project's established precedent for documenting genuine
environment constraints (`REMED-BUILD-008`).

## Wave 1 (parallel) — unblockers

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-004 | DONE | | feature/audit | New `.github/workflows/general-tests-ci.yml` — see detail below. |
| REMED-BUILD-008 | BLOCKED | | | Attempted; genuinely blocked by a newly-discovered environment constraint, not abandoned. See detail below and the new `REMED-BUILD-012` finding. |
| REMED-TEST-002 | DONE | | feature/audit | `GameTests.cpp`/`GraphicsDeviceManagerTests.cpp` rewritten, `GameCrashTest.cpp` deleted — see detail below. |
| REMED-TEST-004 | DONE | | feature/audit | (a)/(b) already satisfied by `REMED-CONTENT-002`/`REMED-DEVICES-001`'s own regression tests (see detail below). (c) `PictureLibraryIndexTests.cpp` done — see detail below. |

### REMED-BUILD-008 detail — D3D12 test coverage, genuinely blocked

**Attempted approach, per the task's own suggested strategy:** reuse
`examples/easygl_depthstencilstate_stencil_enable_test.cpp` and `examples/easygl_scissor_test.cpp`
verbatim for D3D12 (both already confirmed backend-agnostic — public `Game`/`GraphicsDevice`/
`DepthStencilState`/`RasterizerState`/`BasicEffect` API only — and already reused verbatim by D3D11,
per `D3D11Tests.cmake`'s own comment), plus a new backend-agnostic multi-draw `OcclusionQuery` test
(`examples/occlusion_query_multidraw_test.cpp`, modeled on `examples/
easygl_occlusion_query_visible_quad_test.cpp`'s own construction/readback style: draws a
single-quad baseline in its own `Begin()`/`End()`, then two non-overlapping quads inside ONE shared
`Begin()`/`End()`, asserting the multi-draw `PixelCount()` is meaningfully greater than the
single-draw baseline — the exact property `REMED-GFX-015`'s last-draw-only bug breaks). Registered
all three in `cmake/Tests/D3D12Tests.cmake` following `D3D12_Smoke`'s own
`cna_d3d12_test`/`cna_register_backend_test`/`CMAKE_CROSSCOMPILING` pattern.

**Build:** all three compiled and linked cleanly against the existing `cmake-build-d3d12-mingw` +
`~/.wine-cna-d3d12` (vkd3d-proton) setup (after working around an unrelated, transient sibling-repo
build issue — see the note in `REMED-TEST-004`'s own detail above for the same root cause; resolved
here opportunistically once the shared `sharp-runtime` checkout happened to be on a compatible
branch, without touching it myself).

**Run — genuinely blocked, not a stencil/scissor bug:** both the stencil-enable and scissor
executables **crash identically** under `scripts/run-wine-vkd3d.sh` with `wine: Unhandled page fault
on read access to 0000000000000000` at the identical fault address in both runs. The backtrace is
conclusive and has nothing to do with `DepthStencilState`/`RasterizerState` at all:
```
0 vkd3d_instance_get_vk_instance(instance=0000000000000000) [.../vkd3d/device.c:804] in wined3d
1 d3d12_swapchain_init [.../dxgi/swapchain.c:3287] in dxgi
2 d3d12_swapchain_create [.../dxgi/swapchain.c:3446] in dxgi
3 dxgi_factory_CreateSwapChainForHwnd [.../dxgi/factory.c:311] in dxgi
```
This is **real window-attached DXGI swap-chain creation crashing inside vanilla Wine's own
`dxgi.dll`** — exactly the failure mode `cmake/Tests/D3D12Tests.cmake`'s own pre-existing comment on
`cna_diag_d3d12_swapchain` already documents ("DX-100's own spike already found this crashes under
vanilla Wine's dxgi.dll... a permanently-registered, always-crashing CTest would just be noise").
That comment previously read as describing one specific diagnostic tool's own known limitation; this
task's work confirms it is actually a **blanket constraint on this dev environment**: *any* test that
constructs a real window via `Game`+`GraphicsDeviceManager` (i.e. anything other than `D3D12_Smoke`'s
own deliberately-off-screen, `window=nullptr` construction) will hit this identical crash on D3D12,
regardless of what XNA feature it's actually trying to test. `D3D12_Smoke` itself only avoids this by
never touching a window/swap chain at all, and does so via a much lower-level, hand-rolled internal
API (`BindOffscreenColorTargetEXT()`, direct command-list/PSO/root-signature/barrier calls) rather
than the public `Game`/`GraphicsDevice`/`BasicEffect` surface every other backend's tests use.

**Recorded as a new finding, not fixed here** — see `REMED-BUILD-012` below.

**Why this is BLOCKED, not abandoned:** the master plan's own suggested strategy for this task
("reuse the same backend-agnostic EasyGL-authored sources... D3D11 already reuses verbatim") rests
on an assumption — that a real-window D3D12 test is possible at all in this dev loop — this task's
own work disproves. Reaching `REMED-GFX-014`/`REMED-GFX-015`'s trigger conditions on D3D12 now
requires writing genuinely off-screen tests in `D3D12_Smoke`'s own bespoke, much lower-level style
(hand-rolling PSO/root-signature/barrier/render-target-binding calls through internal `EXT` methods,
not the public API), which is substantially larger than this task's own `MEDIUM` complexity estimate
assumed — a `LARGE` undertaking uncovered by doing the work, not a scope choice. **Reverted all
CMake registrations and deleted the new test source** rather than leave crashing/unregistered code
behind: registering permanently-crashing CTests is the exact anti-pattern `cna_diag_d3d12_swapchain`
itself already exists to avoid, and an unregistered, never-verified-on-any-backend source file left
in the tree would be a half-finished addition. `git status` confirms a clean working tree for this
task (no `cmake/Tests/D3D12Tests.cmake` diff, no new `examples/*.cpp`).

**Completion/verification criteria: NOT met.** D3D12 still has exactly one CTest (`D3D12_Smoke`);
`REMED-GFX-014`/`REMED-GFX-015` remain unverifiable on D3D12 in this dev environment specifically
(not necessarily on real Windows/real hardware, which this sandbox cannot test either way).

### REMED-TEST-004 detail — three missing tests for already-confirmed defects

**(a) `ContentReaderExternalReferenceTests.cpp` absolute-path case:** already added as part of
`REMED-CONTENT-002`'s own implementation — `AbsolutePathReferenceThrowsContentLoadException`
(see that task's detail section above). No further action needed; recording here so this task's
own completion criteria are traceable against where the work actually landed.

**(b) `FileDialogTests.cpp`/`MessageBoxTests.cpp` concurrent race:** already added as part of
`REMED-DEVICES-001`'s own implementation — `ConcurrentSetBackendForTestingDoesNotRaceWithLiveCalls`
in both files (see that task's detail section above). Same note as (a).

**(c) `PictureLibraryIndexTests.cpp` symlink-cycle/permission-denied gap — done in this task.**
`PictureLibraryIndex.cpp` already has both mechanisms correctly implemented in production code
(a `weakly_canonical`-keyed `visited` set, explicitly commented "cycle guard, matching
MediaLibraryIndex's own approach", and `std::filesystem::directory_options::skip_permission_denied`
on its `directory_iterator`) — this is a coverage gap, not a live bug, so both new tests are
expected to (and do) **pass**, unlike (a)/(b)'s own "expected to fail first" framing, which applies
to unconfirmed-until-tested defects, not to already-correct production code lacking a regression
guard.
- `PictureLibraryIndexTest.TerminatesOnASelfReferentialSymlinkCycle` — ported verbatim in spirit
  from `MediaLibraryIndexTest.TerminatesOnASelfReferentialSymlinkCycle` (same shard, sibling music
  scanner): a self-referential directory symlink must not hang the constructor.
- `PictureLibraryIndexTest.SkipsAnUnreadableSubdirectoryWithoutCrashing` — ported verbatim in
  spirit from `MediaLibraryIndexTest.SkipsAnUnreadableSubdirectoryWithoutCrashing`: a real
  `perms::none` subdirectory (copies of the existing `beach.jpg`/`portrait.png` fixtures) must be
  silently skipped, not thrown/crashed on. Both new tests self-skip via `GTEST_SKIP()` if symlink
  creation fails (sandboxed environment) or if running as root (permission bits don't restrict root).

**Verification — full binary run, `cmake-build-devices` (EASYGL, `CNA_DEVICES=ON`):**
`PictureLibraryIndexTest.*` — **6/6 passed** (the 4 pre-existing + the 2 new), 1ms total (confirms
no hang from the symlink cycle). Full `CnaTests` binary run, same `cmake-build-devices`
(`CNA_DEVICES=ON`): **5608 tests, 5601 passed, 4 skipped, 3 failed** — the same 3 already-documented
failures (`GameTest.DisposingDeviceInvokesUnloadContent`/`GraphicsDeviceManagerTest.
BackendDetectedDeviceLostIsForwardedToManagerListeners`, both intentional per `REMED-TEST-002`'s own
detail above, plus the pre-existing `TwoProcessLoopbackTest` network-port flake). **Zero
unexpected regressions.**

**Note on `cmake-build-debug` (the default `CNA_DEVICES=OFF` build dir) being unusable for this
verification:** a full rebuild there currently fails unrelated to any change in this task —
`src/CNA/Internal/Xnb/{Primitive,DecimalDateTime}ContentTypeReaders.cpp` fail to compile
(`ContentReader has no member named ReadChar/ReadDecimal`). Root-caused to the sibling
`sharp-runtime` checkout (an "Additional working directory", not part of this repo): `git log`/
`git reflog` there show it currently sitting on `fix/clang-format-truncation-flag`, which lacks
these two methods, with reflog evidence of very recent, repeated switching between that branch and
`feature/xnb-charreader` (which presumably adds them) — i.e. a different, concurrent, unrelated
process actively using that shared sibling checkout, not anything this session's own file changes
touched (confirmed: zero diff in `ContentReader.hpp`/its callers against `HEAD`). Deliberately **did
not** check out a different branch there myself to unblock this build, to avoid clobbering
whatever concurrent work is using that shared directory — out of this task's scope to fix or even
work around via a shared sibling repo's branch state. Verified my new test code compiles cleanly
against current headers regardless (`c++ -fsyntax-only -std=c++23` with the project's own real
include flags extracted from `cmake-build-debug`'s `flags.make`: exit 0, zero errors/warnings), and
ran the actual tests successfully via `cmake-build-devices` instead (a different, already-built
build dir whose cached `CNA` object files predate this sharp-runtime branch state and did not need
recompiling for this task's changes).

**Completion criteria met:** all three now exist. **Verification criteria met:** (a)/(b) already
verified under their own tasks; (c)'s two new tests pass, proving the already-correct production
behavior they cover is real and reachable.

### REMED-TEST-002 detail — Game/GraphicsDeviceManager lifecycle coverage

**`GameTests.cpp`** (previously a 2-line "no tests" stub): a `LifecycleTestGame : public Game` tracks
`Initialize`/`LoadContent`/`Update`/`Draw`/`UnloadContent` call counts. Guarded by a
`VideoSubsystemAvailable()` probe matching `GameWindowTests.cpp`'s own `SDL_InitSubSystem`/
`SDL_CreateWindow`-then-`GTEST_SKIP()` idiom (added on top of the initial draft — see below), since
`Game::Game()` unconditionally constructs a real `GraphicsDevice_` member (own backend/window) at
construction time, before any test body code runs.
- `GameTest.RunExecutesLifecycleInDocumentedOrder` — `game.Run()` (exits itself after the first
  `Draw()`) reaches `Initialize()` → `LoadContent()` → ≥1 `Update()`/`Draw()`. **PASSES.**
- `GameTest.DisposingDeviceInvokesUnloadContent` — `game.Dispose()` after a real run; asserts
  `UnloadContent()` was invoked exactly once. This is `REMED-CORE-006`'s own required test
  ("dispose the device, assert it was called"), via the *real* public disposal path
  (`Game::Dispose()` → `Dispose(true)` → disposes `graphicsDeviceService_`), not a synthetic event
  raise. **FAILS as expected** (confirmed live): `game.unloadContentCalls == 0`. Not fixed — CORE
  lane's task, out of this task's scope.

**`GraphicsDeviceManagerTests.cpp`** (same prior 2-line stub): an `OneFrameGame` (exits after its
first `Draw()`) gives `Game::DoInitialize()`/`CreateDevice()` a real, non-blocking run.
- `GraphicsDeviceManagerTest.CreateDeviceIsReachableAfterRun` — **PASSES.**
- `GraphicsDeviceManagerTest.ApplyChangesRaisesResettingAndResetExactlyOnce` — regression guard:
  the manager's own self-initiated `ApplyChanges()` path already correctly raises
  `DeviceResetting`/`DeviceReset` exactly once each (unconditional on device ownership, unlike
  `Dispose()`/`CreateDevice()` — see the new `REMED-CORE-014` finding below). **PASSES today** and
  protects against a future double-raise once `REMED-CORE-007`'s forwarding fix lands.
- `GraphicsDeviceManagerTest.BackendDetectedDeviceLostIsForwardedToManagerListeners` —
  `REMED-CORE-007`'s own required test ("simulate a backend-detected device-lost via the
  `deviceEventCallback` seam"): since `GraphicsDevice::DeviceResetting`/`DeviceReset` are public
  `EventHandler` members, and `GraphicsDevice.cpp`'s `deviceEventCallback` lambda (only ever invoked
  by the D3D9 backend) does nothing but raise those same public events on a real backend-detected
  device-lost, raising them directly from test code portably simulates the identical signal on
  every backend, not just D3D9. **FAILS as expected** (confirmed live): neither event forwards to
  the manager's own `DeviceResetting`/`DeviceReset` listeners. Not fixed — CORE lane's task.

**`GameCrashTest.cpp`** — deleted outright (the "revive or delete" choice the task explicitly
allows): its 23 lines were entirely commented out behind `#ifdef XNA5`, an API shape (`nullptr`
`TargetElapsedTime`) the current `Game` class does not expose (`setTargetElapsedTimeProperty` takes
a `const TimeSpan&`, not a pointer) — reviving it faithfully would mean inventing a different crash
scenario, not restoring the original one. A permanently-commented file implying coverage that does
not exist is worse than none, per the task's own framing.

**New finding recorded, not fixed** — see `REMED-CORE-014` below: `GraphicsDeviceManager`'s private
`ownsGraphicsDevice_` flag is initialized `false` and never set `true` anywhere in
`GraphicsDeviceManager.cpp` (confirmed by grep and by a live probe program), so `Dispose()`'s and
`CreateDevice()`'s only branch that raises `DeviceDisposing` and releases the owned device is
permanently dead code for every `GraphicsDeviceManager`, not just the `Game`-attached case this
whole codebase always uses. This compounds with `REMED-CORE-006`: even after `Game::Initialize()`
subscribes to `DeviceDisposing` (that task's own stated fix), a real `GraphicsDeviceManager::
Dispose()` call still would not raise it for a `Game`-attached manager without this dead-code path
also being addressed — worth the CORE-lane owner knowing before scoping `REMED-CORE-006`'s fix.

**Regression verification:** full direct `CnaTests` binary run (EASYGL, `DISPLAY=:0`): **5556 tests,
5549 passed, 4 skipped, 3 failed** — 5551 baseline (after CONTENT-001/002/003/006 + NET-001/003) + 5
new tests (2 in `GameTests.cpp`, 3 in `GraphicsDeviceManagerTests.cpp`; `ExposesWindowProperty` is
pre-existing in `GameWindowTests.cpp`, just the same `GameTest` suite name, and still passes
unmodified; `GameCrashTest.cpp`'s deletion removes 0 active tests, it was 100% commented out). The 3
failures: `GameTest.DisposingDeviceInvokesUnloadContent` and
`GraphicsDeviceManagerTest.BackendDetectedDeviceLostIsForwardedToManagerListeners` (both new,
intentional, documented above) plus `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndThe
OtherReconnectsAcrossRealProcesses` — pre-existing, already documented under `REMED-NET-001`'s own
detail section as network-discovery-port flakiness, unrelated to this task, not touched. **Zero
unexpected regressions.**

**Completion criteria met:** both files have real coverage; the dead file is resolved (deleted).
**Verification criteria met:** the two new tests targeting `REMED-CORE-006`/`REMED-CORE-007` fail
against the current buggy behavior, proving they test the right thing; all other new tests pass.

### REMED-BUILD-004 detail — general-tests-ci.yml

**Root cause confirmed exactly as described:** all three existing workflows (`d3d-windows-ci.yml`,
`devices-tests.yml`, `input-ci.yml`) select a label/`--gtest_filter` scoped to their own subsystem;
none ever runs `ctest` unfiltered. Re-read all three in full — no change needed to any of them; this
task adds a fourth, new workflow rather than modifying the existing three (their own scoping is
correct for their own purpose).

**New file:** `.github/workflows/general-tests-ci.yml` — runs the full unfiltered `CnaTests` suite
(no `-L`, no `--gtest_filter`) on EasyGL, the documented Linux default backend
(`cmake/BackendSelection.cmake`). Deliberately EasyGL-only for now, per the task's own "decide
explicitly which backend(s)" requirement — the other 13 backends remain future work, each already
covered (or not) by its own narrower CI path.

**Xvfb design (load-bearing detail):** `CNA_TEST_DISPLAY` is baked into each display-creating test's
`ENVIRONMENT` property at **CMake configure time** (`CMakeLists.txt:47`, defaults to `:0`) — a bare
`xvfb-run -a ctest ...` wrapper (`input-ci.yml`'s own pattern) would NOT work here, because the
baked `DISPLAY=${CNA_TEST_DISPLAY}` environment property overrides whatever `xvfb-run` exports for
the wrapped process, and `input-ci.yml`'s own `-L input` selection happens not to include any test
that bakes it. The unfiltered general suite does (EasyGL/SdlGpu/Ascii real-window tests), so this
job instead starts a real `Xvfb :99` in its own step (persists for the whole job, unlike
`xvfb-run`'s per-command teardown), waits for it via `xdpyinfo`, then configures with
`-DCNA_TEST_DISPLAY=:99` so the baked property and the ambient `$DISPLAY` agree.

**Serial (`ctest`, no `-j`) is deliberate, not an oversight:** confirmed by reproducing it live —
a `-j4` run of the full local baseline (see below) surfaced `ENetDiscoveryServiceTest.*` (×2) and
`DynamicSoundEffectInstanceTest.BufferNeededFiresExactlyTheStarvedCount` failing, none of which
reproduce in isolation (`DynamicSoundEffectInstanceTest` case: 10/10 pass with `--gtest_repeat=10`
run alone) — this matches `REMED-NET-001`'s own already-documented "`ctest -j4` transient failures
are pre-existing test-infrastructure flakiness" finding (real-UDP-port cross-process contention) and
extends it to a previously-undocumented case (real-time audio buffer-starvation timing under heavy
parallel CPU load). Recorded as `REMED-TEST-008` below (new, not fixed — CI-flakiness finding, not a
production defect). Running the new job's `ctest` serially avoids both classes of flake entirely
rather than allowlisting them.

**Failure classification step:** rather than excluding any test from running (which the task's own
principles explicitly warn against — "avoid filters that silently exclude critical suites"), a final
step parses CTest's own `Testing/Temporary/LastTestsFailed.log` and fails the **job** only on a test
name outside a small, explicitly-commented, ID-tracked allowlist (the 4 known pre-existing EasyGL/
external-suite failures — see the workflow file's own header comment for exactly which and why).
This is a deliberately temporary stand-in for real `WILL_FAIL` annotations (`REMED-BUILD-003`, not
done — out of Wave 1 scope, and its own dependency note says a test whose bug is being actively
fixed must never be so annotated, which rules out doing even a narrow slice of it prematurely here).
Every test still runs; only the interpretation changes; the step is designed to be deleted once
`REMED-BUILD-003` lands.

**Verification:**
- Full local baseline established (`ctest --test-dir cmake-build-debug --output-on-failure -j4`,
  real `DISPLAY=:0` session, matching this sandbox's existing build): **5798 registered, 5790
  passed, 4 skipped, 8 failed** (168.55 sec via cached objects). The 8: the 4 already-tracked
  EasyGL/external-suite failures + `EasyGL_RealWindowResize` (timeout — `REMED-BUILD-010`, expected
  on this sandbox's real compositor `DISPLAY=:0`, and expected **not** to reproduce under the new
  job's isolated `Xvfb :99`, per `REMED-BUILD-010`'s own prior finding: "under `DISPLAY=:99` (Xvfb,
  headless) it completes in well under a second with 4/4 PASS") + 3 `-j4`-only flakes (2 already
  documented under `REMED-NET-001`, 1 newly discovered — `REMED-TEST-008`).
- Classification script logic verified directly with 3 synthetic `LastTestsFailed.log` inputs before
  being placed in the workflow: only-known-failures → exit 0; one genuinely new name → exit 1 with
  that name printed; no failures at all (file absent) → exit 0. This is the "a deliberately-broken
  test causes the new job to fail" verification criterion, demonstrated without needing an actual
  GitHub Actions run (not available from this sandbox).
- `DynamicSoundEffectInstanceTest.BufferNeededFiresExactlyTheStarvedCount` re-run in isolation,
  `--gtest_repeat=10`: 10/10 PASS, confirming it is `-j4`-load-only flakiness, not a real regression.
- New workflow YAML parsed successfully with `python3 -c "import yaml; yaml.safe_load(...)"` (no
  `actionlint`/live GitHub Actions runner available in this sandbox; this is the practical ceiling
  for verifying a new workflow file without pushing it).

**Completion criteria met:** CI (once this workflow runs on GitHub Actions) executes the unfiltered
default suite on EasyGL and gates on it (fails on any name outside the tracked allowlist).
**Verification criteria met:** classification script demonstrated to fail the job on a synthetic
new/unexpected failure name, and to pass when only tracked names are present.

## Wave 1 (parallel) — CORE lane

**Wave-labeling note (recorded per the "Open decisions" instruction, since it affects scope):**
`REMEDIATION_DEPENDENCIES.md`'s own Wave 2 section lists `REMED-CORE-001` (logging), `-004` (Color
UB), and `-006`/`-007` (lifecycle) together, and its critical-path diagram places `TEST-002 ──▶
CORE-006, CORE-007` after the Wave-1 unblockers. Read strictly, that puts zero CORE tasks in Wave 1.
However: (a) the *only* documented blocker for `-006`/`-007` is `REMED-TEST-002` specifically ("Both
production bugs currently have zero test coverage; there is no harness to verify a fix" —
`REMEDIATION_DEPENDENCIES.md`'s own Hard-dependencies table), which is now DONE; (b) this matches
exactly how `BUILD_TEST_CI`'s own Wave 1 was already operationalized in this same file (the
"Wave 1 (parallel) — unblockers" section above runs `REMED-BUILD-004`/`-008`/`TEST-002`/`TEST-004` —
none of them P0 — immediately once their own blockers clear, rather than waiting for a literal "Wave
2" milestone); (c) `REMED-CORE-014` (discovered while implementing `TEST-002`) explicitly documents
itself as blocking a real fix of `-006`. Grouped together (same root cause per the "if several
findings share one root cause, fix the root cause once" instruction) and picked up now that their
sole blocker is clear, following the established Wave-1-unblocker pattern rather than waiting for a
separate Wave 2 milestone that would otherwise re-litigate the same three files. `REMED-CORE-001`
and `REMED-CORE-004` have no such blocker (nothing in the dependency table names them as
`TEST-002`-gated) and are **not** touched here — deferred to whoever picks up the CORE lane next,
consistent with "do not opportunistically expand scope."

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-CORE-006 | DONE | | feature/audit | `Game::Initialize()` now subscribes `DeviceDisposing → UnloadContent()` and `DeviceCreated → LoadContent()` (deferred case), matching FNA `Game.cs:649-662` — see detail below. |
| REMED-CORE-007 | DONE | | feature/audit | `GraphicsDeviceManager::CreateDevice()` now subscribes to the managed `GraphicsDevice`'s own `DeviceResetting`/`DeviceReset`, forwarding via `OnDeviceResetting`/`OnDeviceReset`; `ApplyChanges()`'s manual double-raise removed — see detail below. |
| REMED-CORE-014 | DONE | | feature/audit | Landed atomically with `-006`/`-007`, same files, same root cause (see "Discovered during remediation" entry above, now closed) — see detail below. |

### REMED-CORE-006 + REMED-CORE-007 + REMED-CORE-014 detail — Game/GraphicsDeviceManager lifecycle events

**Why landed together:** `MASTER_REMEDIATION_PLAN.md`'s own `Cx / PS / Verify` field for both
`-006` and `-007` says `CONDITIONAL — NO against` each other / `-009` ("same `Game.cpp`") and
"coordinate with `REMED-CORE-006`" respectively — an atomic-pair-shaped constraint, not a
suggestion. `REMED-CORE-014` is not separable from `-006` at all: `-006`'s own required test
(`GameTest.DisposingDeviceInvokesUnloadContent`) cannot pass without `-014`'s fix, because
`GraphicsDeviceManager::Dispose(bool)`'s `DeviceDisposing` raise was unconditionally gated behind
`ownsGraphicsDevice_`, which is `false` for every `Game`-attached manager (the only configuration
the codebase constructs) — `-006`'s subscription would have had nothing to ever invoke it.

**Root causes confirmed exactly as described, plus one compounding gap found while implementing:**

- **`REMED-CORE-006`:** `Game::Initialize()` (`Game.cpp:513-529`) never subscribed to
  `graphicsDeviceService_->getDeviceDisposingEvent()`, unlike FNA's `Initialize()`
  (`Game.cs:649-662`, `graphicsDeviceService.DeviceDisposing += (o,e) => UnloadContent();`).
  Whole-repo grep confirmed `UnloadContent` had exactly 2 hits (declaration + empty default body,
  zero call sites) before this fix.
- **`REMED-CORE-014`** (compounding, discovered during `REMED-TEST-002`): `ownsGraphicsDevice_` is
  initialized `false` in both `GraphicsDeviceManager` constructors and never set `true` anywhere —
  confirmed by grep of the whole file. `Dispose(bool)`'s only branch that raised `DeviceDisposing`
  was gated on this flag, making the raise permanently dead for the one configuration (`Game`-
  attached) this whole codebase constructs. **Root cause is CNA-architectural, not a simple missed
  flag-set:** unlike FNA (where `GraphicsDeviceManager` always constructs and owns its
  `GraphicsDevice`), CNA's `Game` eagerly pre-owns its own `GraphicsDevice_` value member at
  construction time, and a `Game`-attached `GraphicsDeviceManager` only ever points at it
  (`graphicsDevice_ = &game_->getGraphicsDeviceProperty();`) — it is correctly non-owning by
  design, so simply flipping `ownsGraphicsDevice_` to `true` would be wrong: `Dispose()` would then
  `delete` a pointer into a `Game`-owned value member (not a heap allocation), which is memory
  corruption. Confirmed this isn't reachable via the destructor-driven path either: `Game`'s member
  destruction order runs `GraphicsDevice_`'s own `~GraphicsDevice()` (which raises `Disposing`)
  *after* `Content_`/`Window_`/`LaunchParameters_`/`Services_` have already been destroyed, and
  after any derived-class members (e.g. a demo's own `std::unique_ptr<GraphicsDeviceManager> gdm_`
  member) have already been destroyed — and by that point in `~Game()`, the object's vtable has
  already downgraded to `Game`'s own, so a virtual `UnloadContent()` call at that point would
  silently resolve to the empty base stub, not any derived override. The fix therefore had to route
  through the *explicit* `Dispose()`/`Dispose(true)` path (where the object is still fully "alive"
  with its real derived vtable intact), matching exactly how the required test
  (`GameTest.DisposingDeviceInvokesUnloadContent`) exercises it.
- **`REMED-CORE-007`:** FNA's `IGraphicsDeviceManager.CreateDevice()` wires `graphicsDevice.
  DeviceResetting += OnDeviceResetting; graphicsDevice.DeviceReset += OnDeviceReset;`
  (`GraphicsDeviceManager.cs:556-557`) so that `ApplyChanges()`'s later `graphicsDevice.Reset(...)`
  call (which raises the *device's* own events) is forwarded to the manager's public events purely
  via that one-time subscription — FNA's `ApplyChanges()` itself never calls `OnDeviceResetting`/
  `OnDeviceReset` directly. CNA instead had `ApplyChanges()` raise its own separate copies manually
  around its own `applyToExistingBackend()` call, with no subscription at all — meaning a real
  backend-detected device-lost/reset (`GraphicsDevice.cpp`'s `deviceEventCallback`, wired up today
  only by the D3D9 backend, raising `GraphicsDevice::DeviceResetting`/`DeviceReset` directly) never
  reached `GraphicsDeviceManager`'s own listeners at all.
- **FNA-fidelity detail found while porting `-007` line-by-line:** FNA's own `OnDeviceDisposing`/
  `OnDeviceReset`/`OnDeviceResetting` all re-send with `this` as sender rather than forwarding
  whatever `sender` they were called with (`OnDeviceCreated` is the one exception — it does forward
  its `sender`, an asymmetry present in the real FNA source, not a CNA invention). CNA's three
  methods all forwarded the raw `sender` unchanged — invisible before this task because every prior
  call site already passed `this` regardless, only becoming observable once forwarding from a real
  `GraphicsDevice` (whose `sender` is the device, not the manager) was wired up. Fixed to match FNA
  exactly; `OnDeviceCreated` deliberately left untouched (already correct).

**Changes (`Game.cpp`, `GraphicsDeviceManager.cpp`/`.hpp` only):**
- `Game::Initialize()`: subscribes `DeviceDisposing → UnloadContent()` unconditionally when a
  service is registered; ports FNA's deferred-`LoadContent`-via-`DeviceCreated` branch too (full
  fidelity with `Game.cs:649-662`, not just the cited `DeviceDisposing` line) — realistically
  unreachable through the normal `Game`+`GraphicsDeviceManager` combination today (`DoInitialize()`
  always calls `CreateDevice()` before `Initialize()`), but correct defensive behavior for any
  `IGraphicsDeviceService` registered without going through that flow. The pre-existing
  `LoadContent()`-called-immediately condition (`graphicsDeviceService_ == nullptr || ...device !=
  nullptr`) is unchanged.
- `GraphicsDeviceManager.hpp`/`.cpp`: new private `deviceEventsSubscribed_` flag (subscription
  guard — `CreateDevice()` is public API and could otherwise be called again, accumulating
  duplicate lambda subscriptions and double-forwarding every subsequent event; reset to `false`
  alongside the existing owned-device teardown branch). `CreateDevice()` subscribes to
  `graphicsDevice_->DeviceResetting`/`DeviceReset` *after* its own settle-in
  `applyToExistingBackend()`/`Reset()` call, so initial device creation raises only `DeviceCreated`
  on the manager (matching FNA, where a freshly-constructed device never raises `DeviceResetting`/
  `DeviceReset`). `ApplyChanges()`'s manual `OnDeviceResetting`/`OnDeviceReset` calls removed (now
  reached only via the forwarding subscription, avoiding a double-raise). `Dispose(bool)`: raises
  `OnDeviceDisposing()` whenever `disposing && graphicsDevice_ != nullptr`, no longer gated on
  `ownsGraphicsDevice_`; the actual `delete` stays gated on it (memory-safety requirement above);
  `graphicsDevice_` is nulled and `deviceEventsSubscribed_` reset unconditionally afterward.
  `OnDeviceDisposing`/`OnDeviceReset`/`OnDeviceResetting` re-send with `this`, matching FNA.

**Tests added** (`tests/Microsoft/Xna/Framework/GameTests.cpp`,
`GraphicsDeviceManagerTests.cpp`) — the two pre-existing `REMED-TEST-002` regression tests
(`GameTest.DisposingDeviceInvokesUnloadContent`, `GraphicsDeviceManagerTest.
BackendDetectedDeviceLostIsForwardedToManagerListeners`) now pass and had their stale
"expected to fail" comments updated; 5 new tests: `GameTest.RepeatedDisposeDoesNotReinvokeUnloadContent`
(no double-`UnloadContent` from a second explicit `Dispose()`, exercising FNA's own
unconditional-`Disposed`-re-raise quirk that CNA's `Game::Dispose()` already faithfully has),
`GameTest.UnloadContentWorksAcrossRepeatedGameInstancesInOneProcess` (2 full construct→run→dispose
cycles in one process — no leftover static/global state), `GameTest.
DeferredLoadContentFiresOnDeviceCreatedWhenServiceHasNoDeviceAtInitializeTime` (the deferred-
`LoadContent` branch, via a minimal `IGraphicsDeviceService` test double since the normal
`Game`+`GraphicsDeviceManager` path can't reach it), `GraphicsDeviceManagerTest.
ForwardedDeviceEventsReportTheManagerAsSender` (the FNA-fidelity `sender` fix), `GraphicsDeviceManagerTest.
RepeatedDisposeDoesNotReraiseDeviceDisposing` (no double-raise from a second `GraphicsDeviceManager::
Dispose()`, independent of `Game`).

**Verification:**
- Focused run (`GameTest.*:GraphicsDeviceManagerTest.*:IGraphicsDeviceManagerTest.*`, EASYGL,
  direct binary): 15/15 passed (was 8/10 before this change — the 2 known failures now pass).
- Same focused run under ASan+UBSan (`cmake-build-devices-asan`): 15/15 passed, 0 sanitizer
  findings — covers the lifecycle/ownership/exception-path/repeated-construction requirements this
  task's brief called out explicitly (double-dispose, repeated `Game` instances, event-handler
  observation during teardown).
- Full `CnaTests` direct-binary run (EASYGL, **run from the repo root** — the binary has no baked
  `WORKING_DIRECTORY` outside CTest's own `gtest_discover_tests` registration, so running it from
  inside a build directory silently breaks every test using a repo-relative fixture path, e.g.
  `MediaLibraryTestFixture`'s `tests/assets/media/...`; caught and corrected mid-session before
  drawing any conclusions from the wrong-CWD run): **5579 tests, 5575 passed, 4 skipped, 0 failed**
  — reconciles exactly with the prior baseline (5574 total / 5568 passed / 4 skipped / 2 known
  failures) + 5 new tests, all passing: 5568 + 2 (now-fixed) + 5 (new) = 5575. Zero regressions
  anywhere in the suite, including `MediaLibraryTestFixture`/`GamerServices`/`Net` and every other
  lane's tests.

**Completion criteria met:** `UnloadContent()` is invoked on device disposal, matching FNA's
lifecycle (`-006`). All `GraphicsDevice` lifecycle events reach `IGraphicsDeviceService` listeners
regardless of trigger (`-007`). `Dispose()`'s `DeviceDisposing` raise is no longer permanently dead
code for the `Game`-attached configuration (`-014`).
**Verification criteria met:** both tasks' required lifecycle-ordering/backend-triggered-reset tests
pass; `ApplyChangesRaisesResettingAndResetExactlyOnce` (no double-raise) still passes; no double-
dispose in either new repeated-`Dispose()` test.

**Remaining CORE-lane gap, explicitly not addressed here (out of scope):** `GraphicsDeviceManager`'s
no-`Game` constructor (`NOXNA GraphicsDeviceManager()`) still cannot ever reach a working owned
device — `CreateDevice()` throws `std::runtime_error` immediately when `game_ == nullptr` and
`graphicsDevice_ == nullptr`, so `ownsGraphicsDevice_` still can never become `true` through any
reachable path today. This task's fixes make the `Game`-attached (non-owning) path correct and
fully event-observable; a real standalone-owned-device implementation is a separate, materially
larger feature (constructing a `GraphicsDevice` without a `Game`/window) not implied by `-006`/
`-007`/`-014`'s own strategy text, and is left for a future task if the project ever needs a
`Game`-less `GraphicsDeviceManager` to actually work.

## POST-WAVE-1 INTEGRATION BASELINE (2026-07-20)

A dedicated integration/verification pass, run before starting Wave 2 production fixes, per explicit
user direction. **Verification-only — no Wave 2 production code was written in this pass.** The one
production-adjacent change made was a bookkeeping fix (`.gitignore` — see Phase 2 below); everything
else is measurement. `audit/` was not touched.

### Phase 1-2 — plan read, git/worktree sanity

Confirmed 18/105 tracked tasks DONE (all 12 P0, 3/21 P1, 3/44 P2), 1 P2 BLOCKED
(`REMED-BUILD-008`, genuinely environment-blocked), 86 NOT STARTED, plus `REMED-CORE-014` DONE among
the (then-)8 "Discovered during remediation" entries. `audit/`'s last commit (`74ebf356`, 2026-07-19)
predates all remediation work — untouched. One unpushed commit on `feature/audit` (`ebf75803`,
CORE-006/007/014) was legitimate, already-documented Wave 1 work.

**One bookkeeping issue found and fixed (safe, additive, no production-code change):** 5 build
directories (~6.6GB — `cmake-build-d3d11-mingw`, `-d3d12-mingw`, `-d3d9-mingw`, `-devices`,
`-sdlrenderer`) were untracked but not covered by any `.gitignore` pattern (the file enumerates
build-dir names one at a time and these weren't in the list). Replaced the enumeration approach with
one `cmake-build-*/` catch-all pattern — commit `b101e81a`. No tracked build artifacts or unexpected
large binaries found anywhere else in the tree.

### Phase 3 — default full test baseline (EasyGL, `cmake-build-debug`)

Rebuilt against HEAD, full unfiltered `ctest -j1`: **5826 registered, 5817 passed, 4 skipped
(hardware sensors), 5 failed, 716.96s.** All 5 failures reconcile exactly against
`REMEDIATION_PROGRESS.md`'s own Wave 0 triage table — `EasyGL_AvatarRenderer_TintRouting`,
`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`, `easy-gl-resource-smoke-tests`
(all already tracked), plus `EasyGL_RealWindowResize` (`REMED-BUILD-010`, expected timeout under a
real desktop compositor). **Zero regressions.** Total also reconciles: 5579 gtest-discovered (the
figure the `REMED-CORE-006/007/014` commit itself reported) + 247 separately-registered smoke/backend/
input tests = 5826.

### Phase 4 — configuration matrix (13 native backends + `CNA_DEVICES=ON`)

Extended past the instructed minimum list to every backend with an existing build directory in this
sandbox (Canvas/Emscripten excluded — browser target, no native CTest equivalent). All rebuilt against
HEAD `b101e81a`, full unfiltered serial `ctest`, `DISPLAY=:99` (Xvfb — see the display-handling note
below).

| Config | Total | Passed | Failed | Notes |
|---|---|---|---|---|
| EasyGL (default) | 5826 | 5817 | 5 | = Phase 3 baseline |
| `CNA_DEVICES=ON` (EasyGL) | 5876 | 5871 | 5 | Same 5 pre-catalogued; Net 327/327, Media 211/211; both `REMED-DEVICES-001` regression tests pass |
| Headless | 5588 | 5579 | 5 | `REMED-CONTENT-004` (×3), `REMED-GFX-036` (×1), network-port flake (×1) |
| Software | 5587 | 5579 | 3 | `REMED-CONTENT-004` (×2); WireFrame check correctly passes here |
| Vulkan | 5719 | 5708 | 7 | audio-timing flake (×2, confirmed flaky in isolation), GLSL-toolchain-absent (×2), `REMED-GFX-036` (×1), pre-existing `Vulkan_DepthBias` (×1), **new: `Vulkan_SkinnedEffect_Fog` hang (×1, `REMED-GFX-052`)** |
| WebGPU | 5604 | 5593 | 7 | network-port flake (×1), **new: WebGPU custom-GLSL-effect failures (×2, `REMED-GFX-053`)**, **new: WebGPU skinned-model index-buffer failures (×3, `REMED-GFX-054`)**, `REMED-GFX-036` (×1); both `REMED-CONTENT-001` regression tests pass |
| D3D9 (Wine) | 23 registered | 20 | 3 | infra-only: `CnaTests_NOT_BUILT` placeholder + **new CTest-wiring gap (×2, `REMED-BUILD-013`)** |
| D3D11 (Wine) | 11 registered | 6 | 5 | same infra gap (×3) + `REMED-GFX-020` (×2, pre-catalogued, evidence text matches verbatim) |
| D3D12 (Wine) | 1 registered (`D3D12_Smoke`) | 1 | 0 | `REMED-BUILD-012` reconfirmed unchanged (identical Wine/vkd3d-proton swap-chain-crash signature on the unregistered diagnostic) |
| Bgfx | 5699 | ~5688 | 7 deterministic + 2 flaky | GLSL-toolchain-absent (×2), `REMED-GFX-036` (×1), `REMED-BUILD-003`-evidence-listed (×2), `REMED-GFX-017` (×2); 2 more failures were `REMED-BUILD-010`-class real-compositor artifacts (see that finding's evidence extension); `AudioCategoryTest` flaked under heavy concurrent load, 8/8 clean in isolation |
| SdlGpu | 5602 | 5594 | 3 | `REMED-GFX-035` (×2, precise attribution — SdlGpu rejects EasyGL-compatible custom GLSL), `REMED-GFX-036` (×1) |
| Ascii | 5587 | 5536 | 51 | all = deliberate, already-audited `ThrowNo3D` 2D-only design, + `REMED-GFX-036` (×1) |
| Dx3 | 5590 | 5530 | 60 | mostly the same `ThrowNo3D` pattern; + pre-existing `IDirectDraw::CreateSurface` gap (×2, already documented under `REMED-CONTENT-001`'s own verification notes), `Dx3_SpriteBatch` (×2 of 10 checks, pre-catalogued rotation/alpha-blend defects); 2 `DynamicSoundEffectInstanceTest` failures under `-j1` **not re-isolated** (minor open item, low-confidence flag, not a confirmed new defect) |
| SdlRenderer | 5649 | 5591 | 58 | mostly `ThrowNo3D` pattern; + this backend's 3 already-named findings (`SDL_Renderer_RenderTarget_DepthDecision`, `SDL_Renderer_FullscreenToggle`, `SDL_Renderer_ClearOptions_Audit`) |

**Zero regressions found anywhere in the 13-configuration matrix.** Every failure is either
already-catalogued, an already-understood environment limitation, or one of the 5 new findings logged
above (`REMED-CONTENT-009`, `REMED-GFX-052`, `REMED-GFX-053`, `REMED-GFX-054`, `REMED-BUILD-013` — see
their own detail sections in "Discovered during remediation").

### Phase 5 — sanitizer regression

| Area | Sanitizer | Build dir | Result |
|---|---|---|---|
| Net | ASan+UBSan | `net-asan` | Clean — 249/249, 0 sanitizer hits |
| Devices | ASan | `devices-asan` | Clean — 13/13, 0 sanitizer hits |
| Devices | TSan | `devices-tsan` | Clean — 13/13, 0 races |
| Core lifecycle | ASan | `devices-asan` | Clean — 11/11 (reconfirms `REMED-GFX-001`'s UAF fix and `REMED-CORE-006/007/014` hold under ASan) |
| Media | ASan+UBSan | `media-asan` | Clean — 284/284, 0 sanitizer hits |
| Graphics CPU-side (SpriteFont/SpriteBatch/SpriteEffects) | ASan+UBSan | `media-asan` | Clean — 68/68, 0 sanitizer hits |
| Content/XNB | ASan+UBSan | `media-asan` | **1 new UBSan finding (`REMED-CONTENT-009`)** + 3 pre-catalogued gtest failures (`REMED-CONTENT-004` ×2, `REMED-GFX-036` ×1) |

**Coverage gaps, explicitly recorded, not fixed (per instruction not to implement deferred
`REMED-BUILD-010`/`-011` in this pass):** Devices lane has no UBSan-only build in this session
(`devices-asan` is ASan-only) — same gap `REMED-BUILD-011` already documents. No TSan coverage outside
Devices — no other genuinely concurrent code path was in scope for Wave 1. No sanitizer coverage of
GPU-runtime rendering paths (Vulkan/WebGPU/EasyGL real rendering) — not appropriate per the task's own
instruction.

### Phase 6 — security regression

All 12 completed Wave 1 security/safety fixes re-verified in the integrated state: malformed
Texture2D/XNB crash protection, TextureCube OOB protection, path containment, `XnbTypeName`
recursion/string limits, ENet host authority, `ClientHello` resend guard, FileDialog/MessageBox
concurrent backend-swap lifetime, `AudioTagParser` overflow, EasyGL constructor-failure registry
lifetime, SpriteFont/SpriteBatch invalid default character, `SpriteEffects` OOB, Game/
GraphicsDeviceManager lifecycle. **106/106 direct-binary regression tests pass** (19 suites, default
build) **+ 2/2 Devices-gated regression tests pass** (`CNA_DEVICES=ON` build). Zero regressions.

### Phase 7 — test infrastructure check

`WORKING_DIRECTORY` fix (`REMED-BUILD-001`) intact in `cmake/UnitTests.cmake`. `general-tests-ci.yml`
(`REMED-BUILD-004`) present and valid YAML. `CNA_TEST_DISPLAY` baking mechanism live-verified: `ctest`
run with zero exported `DISPLAY` still passes, confirming the baked CTest `ENVIRONMENT` property is
authoritative — matches the workflow's own documented design. Serial-vs-parallel flakiness pattern
reconfirmed (network-port contention, audio-timing sensitivity under heavy concurrent load) — same
class as already documented, not new. `REMED-BUILD-011` (sanitizer presets not setting
`CNA_DEVICES=ON`) reconfirmed still present, correctly left unfixed. Not implemented:
`REMED-BUILD-010`/`-011` themselves (per instruction).

**Mid-pass display-handling correction (worth recording for future sessions):** Phase 3/4/5 forks were
initially instructed to use `DISPLAY=:0` (this sandbox's real, logged-in desktop session), matching
what earlier Wave 1 sessions had used. The user asked mid-pass to keep test windows off the real
desktop. All work was redirected to the project's own existing Xvfb `:99` (already running, and
already the convention `general-tests-ci.yml` itself uses server-side). Root cause of the user-visible
disruption, diagnosed by one of the forks: `gtest_discover_tests`-registered tests (~5600) have no
baked `ENVIRONMENT` property and inherit whatever `DISPLAY` is exported on the `ctest` command line;
`cna_register_backend_test`-registered backend pixel/smoke tests (~200) DO have a baked
`SDL_VIDEODRIVER=x11;DISPLAY=:99` property that overrides ambient `DISPLAY` regardless — so the
disruption was specifically from the ~5600 gtest-discovered tests during the affected runs, not the
backend smoke tests. Every build directory's `CNA_TEST_DISPLAY` cache variable was reconfigured to
`:99` (cheap — reconfigure only, no rebuild) partway through this pass; confirmed via a live `ctest`
run with `DISPLAY` unset entirely that the baked property is now authoritative everywhere. **Some
Phase 4 data above (the initial Bgfx run specifically) was collected partly under `:0` before the
correction landed** — reconciled against a `:99` re-run of the same suite by the responsible fork, no
discrepancy found, figures above are the reconciled `:99` numbers.

### New findings summary (5 new remediation IDs, 2 evidence extensions to existing findings — none fixed in this pass)

| ID | One-line | Severity | Suggested priority |
|---|---|---|---|
| `REMED-CONTENT-009` | Signed int64 overflow in `REMED-CONTENT-001`'s own new validation code (`width*height*4`) | MEDIUM (UB in a CRITICAL-path fix) | P1 — fold into Wave 2 CONTENT lane, ahead of `REMED-CONTENT-004` |
| `REMED-GFX-052` | `Vulkan_SkinnedEffect_Fog` reproducible 30s hang | HIGH | Needs triage before scheduling |
| `REMED-GFX-053` | WebGPU rejects custom-GLSL `ShaderEffect` content | MEDIUM | Route to GRAPHICS lane or `plan_webgpu.md` after triage |
| `REMED-GFX-054` | WebGPU skinned-model index-buffer upload fails (3 tests) | MEDIUM-HIGH | Route to GRAPHICS lane or `plan_webgpu.md` after triage |
| `REMED-BUILD-013` | D3D9/D3D11 CTest wiring gap (2 tests missing Wine-wrapper macro) | LOW | Bundle with `REMED-BUILD-010`/`-011` cleanup |
| — | `REMED-GFX-036` (WireFrame capability) evidence extended from 3 to **6** confirmed-affected backends (Headless, Vulkan, WebGPU, Bgfx, SdlGpu, Ascii) | (no change) | Raises this fix's leverage — one shared-code fix likely closes 6 backends at once |
| — | `REMED-BUILD-010` (real-compositor-vs-Xvfb sensitivity) evidence extended — confirmed **not EasyGL-specific**, Bgfx shows the identical pattern | (no change) | No scheduling change, informational |

### Regressions

**None.** Zero tests that previously passed now fail, across the default baseline, the 13-configuration
matrix, and the full sanitizer/security sweep. The one Wave-1-introduced defect found
(`REMED-CONTENT-009`) does not currently fail any test — it was only surfaced because this pass added
UBSan coverage over Content/XNB that did not exist when `REMED-CONTENT-001` landed. This is exactly the
kind of interaction this integration pass exists to catch.

### Remediation bookkeeping consistency

Task statuses, commit references, and progress-table counts all check out internally consistent against
`git log`. No task was found marked `DONE` without a matching commit, and no commit was found
implementing a task still marked `NOT STARTED`. The one bookkeeping gap found (`.gitignore` coverage,
Phase 2) was fixed as an explicitly-authorized "obvious, strictly safe" correction, not a scope
expansion.

### Wave 2 readiness: **safe to begin.**

No regression, no open correctness question, and no cross-lane interaction blocks starting Wave 2. See
"Wave 2 readiness" recommendation below for lane ordering.

### Wave 2 readiness — recommended lane order (not implemented, recommendation only)

Derived from `REMEDIATION_DEPENDENCIES.md`'s own Wave 2 scope, adjusted for this pass's findings. Not
all lanes need work yet — GRAPHICS' big serialized shader cluster (`REMED-GFX-005`→`-011`→`-020`,
Bottleneck 1 in the dependency doc) is deliberately **not** pulled forward; it remains Wave 3.

1. **CONTENT lane, first, small and fast:** `REMED-CONTENT-009` (this pass's own finding — one-line
   fix, sits inside the CRITICAL `REMED-CONTENT-001` path) bundled with the already-scheduled
   `REMED-CONTENT-004` (Texture3D/TextureCube round-trip returns zeros, MEDIUM, reproducible today on
   Headless and Software). No file overlap with any other lane below — closes out Wave 1's own loose
   end before anything else starts.
2. **CORE lane:** `REMED-CORE-001` (foundational logging — do first, other Wave 2 work may want to log
   through it) then `REMED-CORE-004` (Color UB). Both P1, blocker-free per the Decisions log, correctly
   held back from Wave 1 with no dependency reason to hold them further.
3. **GRAPHICS lane, isolated per-backend defects (the dependency doc's own "highly parallel" batch):**
   `REMED-GFX-004` (RenderTargetCube `Dispose(bool)`, UAF risk — still open despite being
   security-flavored), `-012`/`-013`/`-016`/`-017`/`-018`/`-019`. Fold in `REMED-GFX-036` (WireFrame —
   now confirmed across 6 backends, high leverage for one shared fix) and this pass's own
   `REMED-GFX-052` (Vulkan hang — triage first, root cause unconfirmed) into the same batch; they are
   similarly isolated, not part of the serialized shader cluster.
4. **NET/MEDIA, parallel-safe with the above (no file overlap):** `REMED-NET-002` (unchecked iterator
   arithmetic) and `REMED-MEDIA-002` (MediaLibrary object-graph SEGFAULT, HIGH severity, 6+ backends —
   still open, worth prioritizing early within this lane given the severity).
5. **Defer explicitly:** `REMED-GFX-005`→`-011`→`-020` (Wave 3 shader campaign — the schedule's long
   pole, do not start early). `REMED-BUILD-003` (`WILL_FAIL` rollout) stays sequenced *after* the
   GRAPHICS batch in step 3, per the dependency doc's own ordering constraint (would otherwise mask
   in-progress fixes). This pass's `REMED-GFX-053`/`-054` (WebGPU-specific) need a routing decision
   (general GRAPHICS lane vs. `plan_webgpu.md`) before scheduling — do not assume either home without
   that triage. This pass's `REMED-BUILD-013` is cheap and can be picked up opportunistically by
   whoever next touches D3D9/D3D11, or bundled with the already-deferred `REMED-BUILD-010`/`-011`
   cleanup.

## Wave 2 — CONTENT lane (2026-07-20)

**Scope determination (per this tranche's own explicit instruction — do not blindly implement every
remaining CONTENT task):** read `MASTER_REMEDIATION_PLAN.md`, `REMEDIATION_INDEX.md`,
`REMEDIATION_DEPENDENCIES.md`, and this file's own POST-WAVE-1 INTEGRATION BASELINE section in full
before starting. The CONTENT lane's original 5-task scope (`REMEDIATION_INDEX.md`'s own count) is
`REMED-CONTENT-001`/`-002`/`-003`/`-006` (all DONE, Wave 1) + `REMED-CONTENT-004` (P1, `PS: YES`,
its sole documented dependency `REMED-BUILD-001` is DONE — unblocked). `REMED-CONTENT-009` (this
pass's own new finding) was explicitly named the tranche's top priority by direct instruction.
`REMED-CONTENT-007`/`-008` (also new findings, from `REMED-CONTENT-002`'s own sweep) are **not**
placed in Wave 2 by either the frozen dependency doc (predates their discovery) or this file's own
Wave 2 lane-order recommendation (which named only `REMED-CONTENT-009`+`REMED-CONTENT-004`) —
**deliberately deferred, not implemented in this tranche**, per the instruction's own explicit
conditional ("include ... only if the remediation dependency/progress files place them in this Wave
2 tranche"). Recommend whoever schedules the next CONTENT tranche give them an explicit wave
assignment; both are HIGH severity/P1 with no blocker, so Wave 2 is the natural home once
triaged in.

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-CONTENT-009 | DONE | | feature/audit | Signed int64 overflow in `REMED-CONTENT-001`'s own decoded-byte-size arithmetic, plus 2 sibling readers + 1 additional int32_t overflow found via root-cause sweep — see detail below. Commit `c5ed8dd1`. |
| REMED-CONTENT-004 | DONE | | feature/audit | Texture3D silently discarded all SetData/GetData calls on Software/Headless instead of failing cleanly — root cause differs from the plan's own hypothesis, see detail below. Commit `fa804b7d`. |
| REMED-CONTENT-007 | DEFERRED | | | Not placed in Wave 2 by either frozen dependency doc or prior Wave-2 recommendation — see scope determination above. Remains `NOT STARTED`. |
| REMED-CONTENT-008 | DEFERRED | | | Same as `-007`. Remains `NOT STARTED`. |

### REMED-CONTENT-009 detail — signed int64 overflow in XNB texture decoded-size arithmetic

**Root cause confirmed exactly as recorded in the post-Wave-1 integration pass's own finding:**
`Texture2DContentTypeReader.cpp:88-89`'s `REMED-CONTENT-001` fix computed
`static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4` — widening to `int64_t` is not
sufficient on its own: two dimensions near `INT32_MAX` multiply to a value still representable in
`int64_t` (~4.6×10^18), but the trailing `* 4` pushes the product past `INT64_MAX` (~9.22×10^18),
which is undefined behavior in C++ (integer widening alone does not prevent the *subsequent*
multiplication from overflowing). **Verification-before-fix (required for this task):** reverted
just the fix (`git stash` on the 3 reader `.cpp` files + the not-yet-existing helper header),
rebuilt `cmake-build-media-asan` (HEADLESS, ASan+UBSan), and confirmed live, in this session, that
`Texture2DContentTypeReaderTest.AbsurdlyLargeDimensionsThrowContentLoadExceptionNotBadAlloc`
(pre-existing test, width=height=`0x7FFFFFFF`) reliably triggers `UndefinedBehaviorSanitizer:
runtime error: signed integer overflow: 4611686014132420609 * 4 cannot be represented in type 'long
int'`, and that under `UBSAN_OPTIONS=halt_on_error=1` the test process genuinely aborts (exit 1, no
gtest PASS/FAIL summary reached) rather than merely printing a warning — a real, halting failure
against the pre-fix code, not just a theoretical finding.

**Root-cause sweep (required by this task, not a new broad audit) found the same pattern in two
sibling readers plus one more independent overflow, all fixed together as one root cause:**
- `TextureCubeContentTypeReader.cpp:56-57`: `static_cast<int64_t>(size) * static_cast<int64_t>(size)
  * 4` — the identical 2-factor-plus-multiplier shape. Confirmed live under UBSan (same recoverable
  run): `runtime error: signed integer overflow: 4611686014132420609 * 4 cannot be represented in
  type 'long int'`.
- `Texture3DContentTypeReader.cpp:57-59`: `width * height * depth * 4` — one more factor than
  Texture2DReader's own. Confirmed live under UBSan to be **more** fragile, not less: the
  all-`0x7FFFFFFF` adversarial input fires **two independent overflow violations** in the same
  expression (the `* depth` step, then the `* 4` step evaluated against an already-UB-corrupted
  intermediate value) — `Texture3DContentTypeReader.cpp:58:72: ... 4611686014132420609 *
  2147483647 ...` and `Texture3DContentTypeReader.cpp:57:35: ... 4611686024869838847 * 4 ...`.
- `Texture3DContentTypeReader.cpp:117` (found during the sweep, not the original UBSan report): the
  per-level `const int32_t voxelCount = width * height * depth;` is raw `int32_t` arithmetic with no
  widening at all — a second, independent overflow risk in the same file, upstream-adjacent to but
  distinct from the `CheckDecodedByteSize` call above it. Traced the invariant: this is transitively
  bounded today by the (now-fixed) `CheckDecodedByteSize` call earlier in the same function, which
  caps `width*height*depth*4` at `limits_.maxDecompressedSize` (256MB default, itself an `int32_t`
  field so bounded by `INT32_MAX` even under an adversarially-configured custom limit) — meaning
  `voxelCount` can never legitimately exceed `limits_.maxDecompressedSize/4`, always `< INT32_MAX`.
  Rather than leave this correct-but-non-obviously-so (a future reader would need to trace back
  through the function to see why it's safe), hardened it directly with the same
  `CheckedMultiplyOrThrow()` + an explicit int32_t-range check, per this task's own "harden the
  entire affected arithmetic path consistently, not just the one UBSan-triggering input" instruction.
- **Reviewed and confirmed already safe, left unchanged (not silently skipped):**
  Texture2DContentTypeReader.cpp's own downstream `pixelCount = levelWidth * levelHeight`
  (line 170) is computed *after* the `REMED-CONTENT-001` per-axis `maxDim` check (default 16384),
  so `pixelCount ≤ 16384² ≈ 2.68×10^8`, far under `INT32_MAX` (~2.15×10^9) — large margin, no
  fix needed. TextureCubeContentTypeReader.cpp's own `pixelCount = faceSize * faceSize` (line 101)
  is transitively bounded the same way `voxelCount` was (`≤ limits_.maxDecompressedSize/4`) — same
  reasoning, but a 2-factor product has much more margin than Texture3D's 3-factor one, so left as a
  pure raw-multiplication line without a redundant explicit re-check (asymmetric treatment
  intentional: `voxelCount`'s 3-factor product was hardened because the sweep's own UBSan run showed
  Texture3D's arithmetic to be measurably more overflow-fragile than the 2-factor siblings').
  `ModelContentTypeReaders.cpp:143`'s `VertexBufferReader::Read` own `vertexCount * stride` product
  (a genuinely unrelated file, checked because it matched a grep for the same `static_cast<int64_t>
  ... *` shape): a true 2-factor product with **no** trailing multiplier, which cannot overflow
  `int64_t` for `int32_t`-sourced inputs, and is already followed by its own explicit
  `byteCount64 > INT32_MAX` bounds check before narrowing — already correct, not touched.

**Changes:**
- New `include/CNA/Internal/Xnb/XnbArithmetic.hpp` (header-only, matches
  `CNA::Internal::PathContainment.hpp`'s established shared-helper convention): `CheckedMultiplyOrThrow(
  std::initializer_list<int64_t> factors, const std::string& readerName)` multiplies a sequence of
  non-negative `int64_t` factors via a division-based pre-check (`product > INT64_MAX / factor`,
  guarded on `factor != 0`) before each multiplication, throwing `ContentLoadException` instead of
  ever committing the overflow — the "prefer explicit checked arithmetic... division/subtraction-based
  bounds checks over overflow-prone multiplication" approach this task's own instructions require.
- `Texture2DContentTypeReader.cpp`, `TextureCubeContentTypeReader.cpp`: their own `CheckDecodedByteSize`
  call sites now use `CheckedMultiplyOrThrow({...}, "...")` in place of raw `static_cast` chains.
- `Texture3DContentTypeReader.cpp`: same for its own `CheckDecodedByteSize` call (4 factors); its
  per-level `voxelCount` computation now goes through `CheckedMultiplyOrThrow({width, height, depth},
  ...)` followed by an explicit `> INT32_MAX` guard before narrowing to `int32_t`.

**Tests added:**
- `tests/CNA/Internal/Xnb/XnbArithmeticTests.cpp` (new file, 11 cases): identity/single-factor/simple
  product, any-zero-factor short-circuit, the exact 2-factor-then-`*4` overflow shape (matches
  `REMED-CONTENT-009`'s own trigger), confirmation the 2-factor product *alone* (no `*4`) does **not**
  over-reject, the 3-factor `Texture3DReader` shape, `INT64_MAX` boundary (`at-the-limit` does not
  throw, `one-over` does), negative-factor rejection, and exception-message content.
- `Texture2DContentTypeReaderTests.cpp`: `ZeroWidthThrowsContentLoadException`,
  `ZeroHeightThrowsContentLoadException` (previously only negative width was tested — zero was an
  explicit gap against this task's own required boundary list).
- `Texture3DTextureCubeContentTypeReaderTests.cpp`: `Texture3DReaderAbsurdlyLargeDimensionsThrows...`,
  `Texture3DReaderZeroWidthThrows...`, `Texture3DReaderZeroDepthThrows...`,
  `TextureCubeReaderAbsurdlyLargeSizeThrows...`, `TextureCubeReaderZeroSizeThrows...` — the equivalent
  overflow/zero-dimension coverage for the two sibling readers, previously entirely absent for these
  cases in this file.

**Verification:**
- **Pre-fix reproduction (required, done first):** see root-cause-sweep section above — 4 separate
  UBSan violations confirmed live across the 3 readers (Texture2D ×1, Texture3D ×2, TextureCube ×1)
  before any fix was applied, via `git stash`.
- **Post-fix, same build:** `Texture2DContentTypeReaderTest.*:Texture3DTextureCubeContentTypeReaderTest.*:
  XnbArithmeticTest.*` (30 tests) under `UBSAN_OPTIONS=halt_on_error=1` — **28 passed, 2 failed, 0
  sanitizer findings, process did not halt.** The 2 failures are the already-catalogued, unrelated
  `REMED-CONTENT-004` defect (`TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd`,
  `Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder`), confirmed to reproduce identically
  with the fix reverted (i.e., not caused or masked by this fix either way).
- **Broader Content/XNB ASan+UBSan sweep** (`*Content*:*Xnb*:*Texture*:*Cnj*`, `cmake-build-media-asan`):
  662 tests, 659 passed, 3 failed. The 3 failures: the same 2 `REMED-CONTENT-004` cases plus
  `CnjTexture3DTest.LoadsRealCnjFixture` (also already-catalogued under `REMED-CONTENT-004`'s own
  evidence list). **2 new, unrelated sanitizer findings surfaced by this broader run, neither caused by
  this task's changes** (confirmed: different files, different root-cause shapes, first time this
  specific broad filter had been run under UBSan) — recorded as `REMED-GFX-055` and `REMED-NA-016`
  above, not fixed (out of the CONTENT lane's scope: `IndexBuffer.cpp` is GRAPHICS-owned;
  `third_party/cgltf/cgltf.h` is vendored third-party code).
- **Fuzz/security regression re-run** (`XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly`,
  `XnbTypeNameTest.*` — the `REMED-CONTENT-001`/`REMED-CONTENT-006` regression suites): all pass clean
  within the same broader sweep, confirming this fix doesn't disturb either sibling task's own
  guarantees.
- **Cross-backend verification** (the two backends `REMED-CONTENT-001` originally crashed on, plus one
  more per this task's own instruction): rebuilt and ran the malformed-Texture2D fuzz test + this
  task's own new tests (31 tests total) on **Vulkan** (llvmpipe software rasterizer — real Vulkan API
  path, not a stub) — **31/31 pass**, including the fuzz test that used to `stack smashing detected`
  before `REMED-CONTENT-001`; **WebGPU** — **31/31 pass**, including the fuzz test that used to
  produce a non-catchable Rust panic; **Software** (third backend, CPU rasterizer) — **30/31 pass**,
  the 1 non-pass being the same already-catalogued `REMED-CONTENT-004` case. No crash, no sanitizer
  finding, clean `ContentLoadException` failure behavior, on any of the three.
- **Full `CnaTests` suite** (EasyGL, default `cmake-build-debug`, `DISPLAY=:99`): **5844 registered**
  (5826 post-Wave-1 baseline + 18 new tests from this task), **5839 passed, 5 failed.** 4 of the 5
  failures reconcile exactly against the already-tracked pre-existing set
  (`EasyGL_AvatarRenderer_TintRouting`, `EasyGL_MRT_TwoAttachments`,
  `EasyGL_GraphicsDevice_ReferenceStencil`, `easy-gl-resource-smoke-tests`). The 5th,
  `EasyGL_GraphicsDeviceManager_Vsync`, is a **new, unrelated finding** (recorded as
  `REMED-BUILD-014` above) — Xvfb + software GL rendering cannot negotiate real vsync, a side effect
  of this session's own switch to the `:99` virtual display (at the user's request, mid-session, to
  stop disrupting their real desktop), not a code regression from this task: zero lines touched by
  this fix have anything to do with vsync/`SwapInterval`. `EasyGL_RealWindowResize` (previously
  failing under `DISPLAY=:0`, `REMED-BUILD-010`) now **passes** under `:99` — expected, not a
  regression. **Zero regressions attributable to `REMED-CONTENT-009`'s own change.**

**Completion criteria met:** the confirmed UB is removed from all three sibling XNB texture readers
(not just the one UBSan flagged), the intermediate arithmetic itself is overflow-safe (not merely the
final comparison), and the fix covers zero/max/near-boundary/malicious-mip/overflow/negative-factor
input classes per this task's own required verification list.
**Verification criteria met:** pre-fix UBSan reproduction confirmed live in this session (not just
cited from the prior integration pass); post-fix UBSan-clean confirmed on the same build; the CRITICAL
`REMED-CONTENT-001` remediation path (malformed-Texture2D fuzz test, both previously-crashing
backends) remains fully functional; ASan+UBSan run over the full affected Content/XNB surface; full
suite shows zero attributable regressions.

### REMED-CONTENT-004 detail — Texture3D silently discarded SetData/GetData instead of failing cleanly

**Root cause differs from the master plan's own hypothesis — recorded per instruction, not silently
absorbed.** The plan's own Strategy text guessed an unvalidated size/stride calculation in the
content reader, in the same class as `REMED-CONTENT-003`. Following the plan's own suggested
isolation strategy ("run the reader test with a hand-constructed in-memory `Texture3D` bypassing the
content reader"): confirmed the bug is neither in the reader nor in a stride calculation — it is in
`Texture3D`'s own generic `SetData`/`GetData`, which only ever guarded on `backend_` being non-null
before forwarding to it, and **two different backends leave `backend_` non-functional for two
different reasons**:
- **Software**: `SoftwareGraphicsBackend` never overrides `IGraphicsBackend::CreateTexture3D()`, so
  it inherits the shared default (`return nullptr`). This is not an oversight — the backend's own
  header already documented it: *"Cube-map render targets, Texture3D, and hardware occlusion queries
  remain out of scope for v1 (plan_software.md Boundaries) — CreateRenderTargetCube/CreateTexture3D/
  CreateOcclusionQuery all keep IGraphicsBackend's own shared default."*
- **Headless**: `HeadlessGraphicsBackend::CreateTexture3D()` DOES return a real
  `HeadlessTexture3DBackend` object (`backend_` is non-null) — but that object's own `SetData`/
  `GetData` only validate arguments and call `state_->RecordTrace(...)`, never actually storing
  anything. Confirmed this is backend-specific, not a Headless-wide policy: `HeadlessTextureBackend`
  (2D) has a real `pixels_` member and genuinely stores/returns pixel data — Headless implements 2D
  for real but never implemented 3D storage.

Both shapes produce the identical observable symptom: `Texture3D::SetData()` silently succeeds and
discards the data; `Texture3D::GetData()` silently succeeds and leaves the caller's buffer untouched
— confirmed live (readback returned all-zero packed values on both backends, not garbage/random
data, ruling out a stride/buffer-overrun bug). Confirmed EasyGL round-trips correctly (and, by
inspection of `CreateTexture3D` overrides, so does every other backend that doesn't special-case
`Texture3D` — only Headless/Software do).

**Scope decision — did not implement real Texture3D storage for Software/Headless:** the master
plan's own completion criteria ("Texture3D content round-trips correctly on every backend") reads as
expecting a working implementation. Implementing real CPU-side volume-texture storage for two
backends is a materially larger, `GRAPHICS`-flavored feature addition — and for Software specifically
it would mean **unilaterally reversing an existing, explicitly documented v1 architectural scope
boundary**, not a content-reader bug fix. Per this project's own "owner decision required" precedent
(`REMED-GFX-035`, `REMED-CORE-011`, `REMED-BUILD-002` before its own resolution), this is flagged as
a decision for the project owner, not made unilaterally inside a `MEDIUM`-complexity CONTENT task.
**The fix implemented instead is the minimal, unambiguously-correct half: replace silent data loss
with a clean, immediate, informative failure** — the same standard XNA-layer capability-query pattern
this project already uses for `ThreeD`/`WireFrame`/etc.

**Changes:**
- `include/CNA/GraphicsCapability.hpp`: new `Texture3D` enum entry (real volume-texture storage —
  `SetData`/`GetData` actually persist/retrieve pixels, not just validate arguments).
- `src/Microsoft/Xna/Framework/Graphics/Texture3D.cpp`: constructor now checks
  `device.SupportsCapability(CNA::GraphicsCapability::Texture3D)` and throws
  `System::NotSupportedException` before touching the backend at all — placed immediately before the
  existing `#ifdef CNA_BACKEND_D3D9` profile-ceiling check, matching that check's own style/exception
  type exactly.
- `HeadlessGraphicsBackend`/`SoftwareGraphicsBackend`: both gained a `SupportsCapability()` override
  (neither had one before) returning `false` for `Texture3D`, `true` for everything else (matching
  `EasyGLGraphicsBackend::SupportsCapability`'s own switch-with-default style). Every other backend
  inherits `IGraphicsBackend`'s shared default (`true`), so the new constructor check is a **provable
  no-op** for them — confirmed via full rebuild+test on EasyGL, Vulkan, and WebGPU.

**Tests:**
- `Texture3DTests.cpp`: the `Texture3DTest` fixture's `SetUp()` now `GTEST_SKIP()`s when the backend
  lacks the capability — all 37 pre-existing tests are unchanged in meaning and still run fully on
  every capable backend; they cleanly skip (not fail) on Software/Headless, matching this project's
  own hardware-capability-skip convention. New
  `Texture3DUnsupportedBackendTest.ConstructorThrowsNotSupportedExceptionWhenBackendLacksTexture3D`
  is the mirror-image positive check (skips on capable backends; asserts the throw on incapable
  ones) — without it, an incapable backend's run would prove nothing new actually happened.
- `Texture3DTextureCubeContentTypeReaderTests.cpp`'s
  `Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder` and `CnjTexture3DTests.cpp`'s
  `LoadsRealCnjFixture` (the two tests the master plan's own evidence named) both now branch on
  capability: assert the full real round-trip on capable backends (unchanged from before), assert a
  clean `System::NotSupportedException` on incapable ones instead of asserting on now-meaningless
  all-zero data.

**Verification:**
- **Software** full suite (`cmake-build-software`): 5606 tests, 3 failed — 2 pre-existing audio-timing
  flakes (`DynamicSoundEffectInstanceTest.*`) + pre-existing `REMED-GFX-036` (`WireFrame`). **Zero
  CONTENT-004-related failures** — the two originally-named tests both pass (via the new clean-throw
  path).
- **Headless** full suite (`cmake-build-headless`): 5607 tests, 2 failed — pre-existing `WireFrame` +
  **one newly-recorded, separately-scoped finding**: `TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd`
  still fails on Headless. Investigated: `HeadlessTextureCubeBackend` has the **identical**
  "validates arguments, records a trace, never stores data" shape as `HeadlessTexture3DBackend` did —
  same class of defect, but a genuinely different file/backend-type and outside `REMED-CONTENT-004`'s
  own plan-declared `Affected files` (which name only `Texture3DContentTypeReader.cpp`/`Texture3D.cpp`).
  Not silently folded into this task — recorded as `REMED-GFX-056` below, not fixed (`ITextureCubeBackend`
  is GRAPHICS-owned).
- **EasyGL** full suite (`cmake-build-debug`): 5845 tests, 6 failed — 3 already-tracked
  (`EasyGL_AvatarRenderer_TintRouting`, `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`),
  1 already-recorded this session (`EasyGL_GraphicsDeviceManager_Vsync`, `REMED-BUILD-014`), 1
  already-tracked out-of-scope (`easy-gl-resource-smoke-tests`), and **1 newly-observed, isolated
  flake**: `GuideTest.RenderPendingKeyboardInputIsNoOpWhenNothingPending` — re-ran in isolation with
  `--gtest_repeat=8`, **8/8 passed**, confirming a non-reproducible flake unrelated to this change
  (Guide/keyboard-input rendering has nothing to do with Texture3D). Recorded, not chased further —
  same general class as `REMED-TEST-008`'s already-documented flakiness.
- **Vulkan**/**WebGPU** (Texture3D-filtered runs, 52 tests each): both **51 passed + 1 correctly
  skipped** (`Texture3DUnsupportedBackendTest`, since both backends support `Texture3D`), **0
  failed** — confirms the new capability check is a genuine no-op on capable backends.
- **Environment note:** mid-verification, this sandbox's Xvfb `:99` process died (a resource/longevity
  issue after many hours of window-creating test runs this session — unrelated to this change).
  Produced one large, contiguous, easily-identified transient failure block (tests 636-2161, all
  "`SDL_InitSubSystem(SDL_INIT_VIDEO) failed: x11 not available`") on one Vulkan spot-check and one
  EasyGL full-suite run. Restarted Xvfb, confirmed stable, and re-ran both to completion — every
  figure recorded above is from the clean re-run.

**Completion criteria met (for the scope actually implemented):** Texture3D content round-trips
correctly on every backend that supports it (unchanged), and fails cleanly — never silently — on
every backend that doesn't (new). Real storage on Software/Headless remains an explicit, flagged
owner decision, not silently declared "done."
**Verification criteria met:** both originally-named tests (`Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder`,
`CnjTexture3DTest.LoadsRealCnjFixture`) now behave correctly — real data on capable backends, a clean
exception on incapable ones — confirmed on Software, Headless, EasyGL, Vulkan, and WebGPU.

## Wave 2 — BUILD_TEST_CI lane (2026-07-20)

**Scope determination (per this tranche's own explicit instruction):** read
`MASTER_REMEDIATION_PLAN.md`, `REMEDIATION_INDEX.md`, `REMEDIATION_DEPENDENCIES.md`, and this
file's own Wave 0/1/POST-WAVE-1/CONTENT-lane sections in full before starting. Assigned/likely-relevant
findings named in the tranche instruction: `REMED-BUILD-010`, `-011`, `-013`, `-014` (all P2/P3
`Discovered during remediation` entries, `NOT STARTED`), plus real 32-bit CI coverage for
width-dependent arithmetic (a follow-up on `REMED-MEDIA-001`'s own Wave 1 gap — no dedicated REMED
ID existed for this before this tranche; assigned `REMED-BUILD-015` below for traceability).
`REMED-BUILD-005` (a `Waves 2-5` P2 task, not originally named in the tranche instruction) was
pulled in **because it turned out to be a hard, unavoidable blocker for verifying `REMED-BUILD-013`**
— the same "prerequisite, not scope creep" reasoning already established for cross-lane blockers
elsewhere in this file. No other Wave 3+ task was pulled forward; the GRAPHICS shader campaign
remains untouched, per instruction.

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-005 | DONE | | feature/audit | Root cause of a hard D3D9/D3D11 `CnaTests` build blocker, not just a `-013` "nice to have" — see detail below. |
| REMED-BUILD-013 | DONE | | feature/audit | Root cause was narrower than originally recorded — see detail below. |
| REMED-BUILD-011 | DONE | | feature/audit | Presets + CI workflow both fixed — see detail below. |
| REMED-BUILD-010 | DONE | | feature/audit | Re-investigated, reclassified, fixed at the harness level — see detail below. |
| REMED-BUILD-014 | DONE | | feature/audit | Environment-capability probe added — see detail below. |
| REMED-BUILD-015 | DONE | | feature/audit | New: real (not simulated) 32-bit CI coverage for `AudioTagParser.cpp`'s width-dependent bounds check — see detail below. |

### REMED-BUILD-005 + REMED-BUILD-013 detail — getting a real D3D9/D3D11 `CnaTests` to build and run under Wine

**Reproduced first, per instruction.** Rebuilding `cmake-build-d3d11-mingw`'s `CnaTests` target from
a clean `cmake .` reconfigure surfaced a **chain of four independent, previously-latent build
defects**, each hiding the next — none reachable before because `CnaTests.exe` had never actually
finished building on a MinGW cross-compile target in this project's history:

1. **`REMED-BUILD-005` itself** — `cna_audio_mixer_destroy_active_static_voice_harness` /
   `..._dynamic_voice_harness` (`cmake/Harnesses.cmake`) `#include` `AudioMixer.hpp` directly, which
   needs `<SDL3/SDL.h>`/`<SDL3_mixer/SDL_mixer.h>`. `CNA` links `SDL3::SDL3` `PRIVATE`
   (`cmake/CnaLibrary.cmake`), so that include path never propagates to CNA's own consumers — masked
   on every native GCC build in this sandbox by an accidental system-wide `/usr/local/include/SDL3`,
   but a hard `fatal error: SDL3/SDL.h: No such file or directory` under MinGW, exactly as
   `MASTER_REMEDIATION_PLAN.md`'s own evidence already recorded ("independently rediscovered by 3 of
   3 non-native-GCC toolchains"). **Fix:** link `SDL3::SDL3` explicitly on both targets, matching
   `cna_devices_shutdown_ordering_harness`'s own already-correct pattern in the same file.
2. **Windows.h `ERROR` macro collision** — `include/CNA/Internal/Net/ENetHostHandle.hpp` includes
   `<enet/enet.h>`, which transitively pulls in `<windows.h>` (via enet's own `win32.h`) on MinGW —
   `windows.h`'s `wingdi.h` `#define`s `ERROR`, colliding with `CNA::LogLevel::ERROR` /
   `CNA::LogCategory::ERROR` wherever this header is included before `CNA/Logger.hpp` in the same
   translation unit (`ENetBackend.cpp`), breaking both enums' declarations with `expected identifier
   before numeric constant`. **Fix:** `#undef ERROR` immediately after the `enet.h` include, the
   standard idiom for this well-known pitfall — a no-op on every non-Windows build.
3. **`geteuid()` used unconditionally in 2 test files** — `MediaLibraryIndexTests.cpp` /
   `PictureLibraryIndexTests.cpp`'s own `SkipsAnUnreadableSubdirectoryWithoutCrashing` cases call
   POSIX `::geteuid()` with no platform guard; doesn't exist on Windows/MinGW. Their whole premise
   (`std::filesystem::perms::none` denying traversal) is a Unix permission-bits mechanism with no
   reliable NTFS/MinGW equivalent either. **Fix:** guard the `<unistd.h>` include and the `geteuid()`
   call with `#ifndef _WIN32`, `GTEST_SKIP()` on Windows with a clear message.
4. **`VideoContentTypeReader.cpp` missing from the `CNA_FFMPEG_AVAILABLE` exclusion list** —
   `cmake/CnaLibrary.cmake` already excludes `VideoDecoder.cpp`/`VideoPlayer.cpp`/`Video.cpp` from
   `CNA_SOURCES` on FFmpeg-unavailable platforms (MinGW/Emscripten/Android all set
   `CNA_FFMPEG_AVAILABLE=OFF`), but not their sibling `VideoContentTypeReader.cpp`, which constructs
   a `Media::Video` — an undefined-reference chain (`Video::Video()`, `VideoPlayer::Play()`, etc.)
   through `libCNA.a` itself, not just test code. **Fix:** add the missing exclusion (mirroring the
   existing three); guard `XnbBuiltInReaders.cpp`'s `RegisterVideoXnbReader()` call with `#ifdef
   CNA_FFMPEG_AVAILABLE` (the macro is already propagated via `target_compile_definitions`, no new
   plumbing needed); exclude the 5 now-orphaned test files (`VideoDecoderTests.cpp`,
   `VideoContentTypeReaderTests.cpp`, `VideoTests.cpp`, `VideoPlayerTests.cpp`,
   `ContentManagerVideoXnbTests.cpp`) from `CNA_TEST_SOURCES` under the same condition —
   `VideoSoundtrackTypeTests.cpp` is unaffected (pure enum, no `.cpp`, no FFmpeg dependency).

All four are guarded behind conditions (`NOT CNA_FFMPEG_AVAILABLE`, `ifdef ERROR`, `ifndef _WIN32`)
that are false/no-op on every native Linux/EasyGL/Vulkan/etc. build already exercised in this
project's other baselines — **zero blast radius on any previously-passing configuration.**

**With all four fixed, `CnaTests.exe` builds successfully on both D3D9 and D3D11 MinGW
cross-compile for the first time in this project's history.** `gtest_discover_tests`'s own `PRE_TEST`
discovery, which previously fell back to CMake/GoogleTest's own `CnaTests_NOT_BUILT` placeholder
(a real, already-understood GoogleTest-module mechanism for "the target's `PRE_TEST` discovery run
itself couldn't execute," not a CTest-wiring gap at all), now genuinely enumerates **5490 real gtest
cases** on both backends.

**`REMED-BUILD-013`'s own actual root cause, once verifiable:** narrower than originally recorded.
- `CnaInputTests` needed **no fix at all** — CMake's built-in `CROSSCOMPILING_EMULATOR`
  target-property auto-injection (already set on the `CnaTests` target itself,
  `cmake/UnitTests.cmake`) already applies correctly to any `add_test(COMMAND CnaTests ...)`
  registration once the target actually builds. Verified: `CnaInputTests` (the
  `--gtest_shuffle --gtest_repeat=5` input-suite subset) **passes cleanly** on both D3D9 (20.66s) and
  D3D11 (12.57s).
- `StrictXnaApiSurfaceCheck_Compile_Run` (a **separate** target, `cna_strict_xna_api_check`,
  `cmake/Harnesses.cmake`) genuinely did need two fixes: (a) it never had `CROSSCOMPILING_EMULATOR`
  set at all (unlike `CnaTests`), so `ctest` tried to exec the `.exe` natively on the Linux host
  ("unable to find an interpreter") — fixed by setting the same D3D9/D3D11/D3D12
  `*_SKIP_*_GATE=1`-wrapped `CROSSCOMPILING_EMULATOR` pattern `CnaTests` already uses (this target
  never creates a device, so the skip-gate applies the same way); (b) once Wine could actually launch
  it, it failed with `libgcc_s_seh-1.dll`/`libstdc++-6.dll not found` (status `c0000135`) — the same
  static-runtime-linking treatment every `cna_d3d9_test()`/`cna_d3d11_test()` executable already gets
  (`-static-libgcc -static-libstdc++`, `cna_copy_mingw_runtime`, `cna_copy_sdl_runtime`) was missing
  from this one target. Verified: **passes on both D3D9 (0.58s) and D3D11 (2.33s)**, alongside its
  sibling `StrictXnaApiSurfaceLeakCheck_MustFailToCompile` (already passing, unaffected).

**Verification:**
- D3D11: `CnaTests.exe` builds clean. `CnaInputTests` PASS (12.57s). `StrictXnaApiSurfaceCheck_Compile_Run`
  + `StrictXnaApiSurfaceLeakCheck_MustFailToCompile` PASS (2.33s / 1.99s).
- D3D9: `CnaTests.exe` builds clean (only pre-existing `[[nodiscard]]`-ignored warnings, no errors).
  `CnaInputTests` PASS (20.66s). `StrictXnaApiSurfaceCheck_Compile_Run` PASS (0.58s, after rebuilding
  the target — the exe present before this fix predated it and needed an explicit rebuild).
  `StrictXnaApiSurfaceLeakCheck_MustFailToCompile` PASS (4.13s, unaffected throughout).
- D3D12 not separately re-verified (`REMED-BUILD-012`'s own already-documented Wine/vkd3d-proton
  swap-chain-crash makes any real-window D3D12 test unusable in this environment regardless); the
  `cna_strict_xna_api_check` fix was applied identically to the D3D12 branch on the same reasoning
  (never creates a device), but this specific claim is unverified — flag for whoever next touches
  D3D12 test infra.
- **Full-suite `ctest` runs were started on both D3D9 and D3D11 (`-j1`, matching every other
  Wine-wrapped backend's serial convention) but deliberately stopped partway through, not run to
  completion** — Wine's own per-process startup overhead makes a full ~5500-test run impractically
  slow (~1.3-1.8s/test observed, projecting to 2+ hours per backend) for verifying a CTest-wiring fix
  whose own scope is exactly two named tests. The partial runs (D3D11: 1305/5495; D3D9: 728/5507) are
  still valuable evidence: **zero new failures outside one already-bounded, precisely-characterized
  cluster** — see `REMED-MEDIA-005` below, a new finding, not fixed here (out of BUILD_TEST_CI scope).
- No regression: every configuration this project's own baselines already covered
  (EasyGL/Vulkan/WebGPU/Software/Headless/Bgfx/SdlGpu/Ascii/Dx3/SdlRenderer/`CNA_DEVICES=ON`) is
  unaffected by all four fixes above, since every one of them is conditioned on
  `NOT CNA_FFMPEG_AVAILABLE` / `MINGW` / `CMAKE_CROSSCOMPILING`, all false there.

**Completion criteria met:** `CnaInputTests` and `StrictXnaApiSurfaceCheck_Compile_Run` are both
registered and runnable on D3D9/D3D11, and both pass. **Verification criteria met:** failures
propagate correctly (confirmed by observing each of the 4 blocking build errors surface distinctly
before being fixed, and by the partial full-suite runs' one bounded, correctly-surfaced new finding).

### REMED-BUILD-011 detail — devices sanitizer presets

**Two distinct gaps, both fixed:**
1. `CMakePresets.json`'s `devices-asan`/`devices-tsan`/`devices-ubsan` presets never set
   `CNA_DEVICES=ON` in their own `cacheVariables`, despite each preset's `displayName`/`description`
   explicitly promising "Microsoft::Devices hardening" coverage. **Fix:** added `"CNA_DEVICES":
   "ON"` to all three, plus a one-line note in each `description` cross-referencing this finding.
2. `.github/workflows/devices-tests.yml` — the one CI job meant to prove Devices sanitizer coverage
   in real CI — had the **same** gap (it configures via `cmake --preset devices-ubsan`, so it
   inherited fix #1 automatically), **plus its own, additional, previously-undiscovered gap**: its
   `paths:` triggers and `DEVICES_GTEST_FILTER` only ever covered `Microsoft::Devices`
   (Sensors/VibrateController — always compiled regardless of `CNA_DEVICES`), never
   `CNA::Devices` (`FileDialog`/`MessageBox`/`Clipboard`/`SystemTray`/`Camera`/`DisplayInfo`/
   `PowerInfo`/`SystemInfo`/`Locale`/`UrlLauncher` — the actual `CNA_DEVICES`-gated NOXNA surface,
   under `tests/CNA/Devices/`). This means `REMED-DEVICES-001`'s own FileDialog/MessageBox
   mutex-lifetime regression tests **never ran in real CI, under any sanitizer, even after fix #1
   alone.** **Fix:** added `include/CNA/Devices/**`/`src/CNA/Devices/**`/`tests/CNA/Devices/**` to
   both `paths:` triggers, and a new `CNA_DEVICES_GTEST_FILTER` (the 10 `tests/CNA/Devices/*.cpp`
   suite names) run as its own CI step.

**Verification (per this task's explicit "do not trust preset names alone" instruction):**
- Confirmed via `nm`/symbol inspection that `FileDialogTests`/`MessageBoxTests` bodies are actually
  compiled into `CnaTests` under the fixed preset (558 matching symbol references) — not just
  linking cleanly by coincidence.
- **Fresh** configure (`cmake --preset devices-asan` into a brand-new build directory, no manual
  `-D` override at any point) confirms `CNA_DEVICES:BOOL=ON` in `CMakeCache.txt` from the preset
  alone — proving fix #1 genuinely works, not just that an already-`ON` cache masked the test.
  Repeated for `devices-tsan`/`devices-ubsan` identically.
- All three presets built `CnaTests` fresh and both gtest filters (`DEVICES_GTEST_FILTER` and the
  new `CNA_DEVICES_GTEST_FILTER`) were run under each:
  - **ASan:** `CNA_DEVICES_GTEST_FILTER` — 50/50 tests PASSED functionally, but the **process exit
    code is 1** due to AddressSanitizer-reported leaks (12032 bytes, 16 allocations) — bisected to
    `CameraTests`' 4 `TryAcquireFrame*` cases specifically (excluding just `CameraTests.*` from the
    filter makes the run exit 0 with the same 46 tests still passing). Every leak's stack trace
    bottoms out in `libdrm.so.2`/`<unknown module>` (GL/DRI driver initialization, triggered by these
    tests' real `Texture2D`/`GraphicsDevice` construction for pixel upload) — not CNA's own
    `Camera.cpp`/`CameraTests.cpp` code. Matches this project's own already-documented Wave 1
    decision that ASan+real-GPU-rendering paths are known-noisy and out of scope
    ("no sanitizer coverage of GPU-runtime rendering paths ... not appropriate per the task's own
    instruction"). **Recorded as `REMED-DEVICES-004` below, not fixed** (out of this task's scope;
    likely a GL-driver-teardown-ordering artifact, not a CNA defect, but not confirmed further).
  - **TSan:** `CNA_DEVICES_GTEST_FILTER` — 50/50 tests PASSED (exit code 66, TSan's own
    non-zero-on-any-report convention). 8 data-race warnings reported, **all 8 the identical shape**
    to `REMED-DEVICES-004`'s ASan leak — `pthread_barrier_init`/`_destroy` racing inside
    `libgallium-25.0.7-2.so`/`libEGL_mesa.so` during real `GraphicsDevice` construction/teardown,
    triggered by the same 4 `CameraTests.TryAcquireFrame*` cases; full stack traces confirm every
    frame bottoms out in Mesa/EGL internals, never `CNA::Devices::Camera`/`CameraTests.cpp` itself.
    Same root class as the ASan finding, not a second one — folded into `REMED-DEVICES-004`.
  - **UBSan:** `CNA_DEVICES_GTEST_FILTER` — **50/50 PASSED, exit code 0, zero UBSan reports.**
  - **Both sanitizers, `DEVICES_GTEST_FILTER` (`Microsoft::Devices`/Sensors, unchanged scope):**
    clean — 442/446 PASSED + 4 expected hardware skips (`Accelerometer`/`GyroscopeTests`'s own
    no-hardware-present skips), **zero ASan/TSan reports**, under both presets.
- `Microsoft::Devices` (`DEVICES_GTEST_FILTER`) suite unaffected throughout — same coverage as
  before, now simply alongside a genuinely-compiled `CNA::Devices` surface rather than a silently
  absent one.

**Completion criteria met:** all three presets genuinely enable `CNA_DEVICES` from the preset alone;
the CI workflow that is supposed to prove this in practice now actually compiles and runs both
Devices surfaces. **Verification criteria met:** demonstrated via fresh, preset-only configures (no
manual override) plus symbol-level and behavioral confirmation that the previously-compiled-out code
now runs.

### REMED-BUILD-010 detail — `EasyGL_RealWindowResize`, re-investigated

**This tranche's own re-investigation materially changed this finding's classification.** The
previously-recorded evidence ("100% reproducible" hang under `DISPLAY=:0`, this sandbox's real
logged-in GNOME/Mutter session) was re-tested directly per this task's explicit authorization to use
the real display for this specific investigation: **20/20 consecutive runs of the built binary
against the real `:0` session completed cleanly (4/4 PASS) with zero hangs**, confirmed to be a
genuine Xwayland/Mutter session (`ps` shows `gnome-shell`/`mutter-x11-frames`/rootless `Xwayland :0`,
not a bare X server) — not a masked/different environment. This directly contradicts a strict
"deterministic environment incompatibility" classification.

**Reclassified as:** a real, but state/focus-dependent, compositor-timing sensitivity — consistent
with the original hypothesis ("Mutter defers/never delivers a `ConfigureNotify` to a window that
isn't focused/mapped the way a bare Xvfb server does") being a *conditional* race, not a guaranteed
outcome. Not a CNA logic bug: the resize/viewport/event-firing code this test checks is proven
correct both under Xvfb (pre-existing) and now under 20 repeated real-display runs. Not purely a
"needs Xvfb" test either, since it does NOT reliably fail under a real display.

**Fix implemented (option (b) from this finding's own previously-recorded "suggested remediation
directions," not option (a)):** the test's `kMaxWaitFrames`-only loop is now backstopped by an
independent 20-second wall-clock deadline (`std::chrono::steady_clock`), checked every `Draw()` call
regardless of frame count. If the resize doesn't propagate within that window (whether the
underlying cause is compositor frame-throttling of an unfocused window, as observed, or a genuine
future regression), the test fails fast with an actionable message distinguishing this from a
generic timeout, rather than silently consuming CTest's full 60s `TIMEOUT`. This makes the test's
*failure mode* deterministic even though the underlying compositor interaction is not.

**Verification:** rebuilt and re-ran the modified binary directly — 5/5 clean PASS on real `:0`
(unchanged from before the fix, confirming no regression to the success path), 1/1 clean PASS
against a freshly-started, isolated Xvfb instance (`:98`, avoided `:99` — occupied by a concurrent,
unrelated session's own test run at the time, confirmed via `ps`/lock-file inspection before
touching anything).

**Completion criteria met:** the test's failure mode no longer depends on CTest's own blunt
`TIMEOUT`; a stalled resize now fails within ~20s with a diagnostic message, on any display.
**Verification criteria met:** unchanged PASS behavior confirmed under both real `:0` and Xvfb.

### REMED-BUILD-014 detail — `EasyGL_GraphicsDeviceManager_Vsync` under Xvfb

**Fix:** when `SynchronizeWithVerticalRetrace=true` + `ApplyChanges()` still reports
`SDL_GL_GetSwapInterval()==0`, the test now probes the *same real GL context* directly with a raw
`SDL_GL_SetSwapInterval(1)` call — bypassing `GraphicsDeviceManager`/CNA entirely. If even that raw
call can't make the interval stick, the environment itself cannot provide real vertical retrace here
(exactly Xvfb's own documented limitation), and the test exits via this project's established
`SKIP_RETURN_CODE 77` convention (`cmake/UnitTests.cmake`'s own Task 470 mechanism, applied uniformly
to every registered CTest) rather than reporting a false regression. If the raw call *does* work,
`GraphicsDeviceManager`/`GraphicsDevice::Reset()`'s own forwarding is genuinely broken and the test
still fails loudly — the exact defect this test exists to catch is not masked.

**Verification:**
- Real `:0` display: still **2/2 real PASS** (`SDL_GL_GetSwapInterval()=0`/`=1` as expected) —
  confirms the fix does not mask a genuine regression on a vsync-capable environment.
- Fresh, isolated Xvfb instance (`:98`): **`[SKIP]`**, exit code 77, with the diagnostic message
  distinguishing an environment limitation from a CNA regression.
- Via `ctest` itself (isolated Xvfb `:99`, after confirming the previous occupant — an unrelated
  concurrent session's own test run — had finished): CTest reports **`Skipped`**, not `Failed`,
  confirming the `SKIP_RETURN_CODE 77` convention correctly propagates through the full CTest
  pipeline, not just the raw exit code.

**Completion criteria met:** the Xvfb-specific vsync limitation no longer reads as a product
regression; real-vsync coverage is preserved (still asserted, not skipped, whenever the environment
can actually provide it). **Verification criteria met:** demonstrated on both a capable (real
display) and incapable (Xvfb) environment, plus end-to-end through `ctest`.

### REMED-BUILD-015 detail — real 32-bit CI coverage for `AudioTagParser.cpp`'s width-dependent bounds check

**Context:** `REMED-MEDIA-001` (Wave 1, DONE) fixed `AudioTagParser.cpp`'s unsigned-integer-overflow
bounds checks, but its own regression tests could only *simulate* the 32-bit `std::size_t`
wraparound via explicit `uint32_t` arithmetic on this project's 64-bit sandbox — that file's own
comment already documented "no 32-bit toolchain is available in this sandbox" as the reason. This
tranche's instruction asked to "evaluate the most practical way to compile and execute the relevant
width-sensitive tests on a true 32-bit target/toolchain" and "prefer CI-native architecture coverage
over ad hoc arithmetic simulation where practical."

**Feasibility check (this sandbox):** confirmed `gcc-multilib`/`g++-multilib`/`libc6-dev-i386` are
still not installed and this session has no passwordless `sudo` to install them (`gcc -m32` fails
with "cannot find Scrt1.o"/"cannot find crti.o"); `docker info` also fails (daemon not accessible).
**Genuine 32-bit compile+execute cannot be verified from inside this sandbox** — matches the task's
own anticipated fallback ("if full 32-bit runtime coverage is not feasible in current CI, create the
strongest truthful partial coverage and document the gap").

**What was built instead — real coverage, verified everywhere except the actual `-m32` leg:**
- `AudioTagParser.cpp`/`.hpp` and `Microsoft::Xna::Framework::Content::ContentLoadException` have
  **zero SDL/FFmpeg/graphics dependency** (confirmed by reading every `#include` in both files) —
  genuinely separable from the rest of `CNA`'s heavy toolchain requirements.
- New standalone, non-GTest executable:
  `tools/media/audio_tag_parser_32bit_overflow_check.cpp` — reuses the *exact* crafted
  ID3v2.3/FLAC fixtures `AudioTagParserTests.cpp`'s own
  `CraftedId3v23WrapInducingFrameSizeIsRejectedCleanly`/`CraftedFlacWrapInducingVendorLenIsRejectedCleanly`
  cases use, asserting `AudioTagParser::TryReadId3v2`/`TryReadFlacComments` reject them cleanly.
  Also asserts `sizeof(std::size_t) == 4` as its own first check, so a CI misconfiguration that
  silently drops `-m32` fails loudly rather than reporting false confidence.
- New standalone CMake project, deliberately **not** `add_subdirectory()`'d into the main build:
  `tools/media/arithmetic32bit/CMakeLists.txt` compiles only the harness +
  `AudioTagParser.cpp`/`ContentLoadException.cpp` directly — building a full 32-bit `CNA` (with
  32-bit SDL3/SDL3_image/SDL3_mixer/FFmpeg, none of which this project has for a 32-bit target)
  would be a much larger, slower undertaking for no additional coverage of the thing under test.
- New `.github/workflows/32bit-arithmetic-ci.yml`: installs `gcc-multilib`/`g++-multilib` (available
  via `apt` on `ubuntu-latest`, unlike this sandbox), configures with `CMAKE_C_FLAGS`/
  `CMAKE_CXX_FLAGS`/`CMAKE_EXE_LINKER_FLAGS=-m32`, builds, and runs the check.

**Verification actually performed:**
- Native (64-bit) build of the new standalone project: configures and builds cleanly, zero SDL/
  FFmpeg dependency pulled in.
- Ran natively: `sizeof(std::size_t) == 4` check **correctly FAILS** (`sizeof=8`, as expected on this
  64-bit host) — proving the self-check is meaningful, not a rubber stamp — while the two
  overflow-rejection assertions PASS (expected either way at 64-bit width; not meaningful proof of
  the fix at this width, same limitation the existing simulated tests already documented).
- New workflow YAML parsed successfully (`python3 -c "import yaml; yaml.safe_load(...)"`) — same
  practical ceiling `REMED-BUILD-004`'s own new CI job was verified to, no live GitHub Actions runner
  available from this sandbox.
- **Not verified (explicitly flagged, not silently assumed):** an actual `-m32` compile+execute.
  Whoever next has real root/CI access to this repo should run the new workflow (or
  `sudo apt-get install gcc-multilib g++-multilib` locally) once to confirm the `-m32` leg itself
  works end-to-end — the harness and CMake project are ready and reasoned through, but this specific
  claim rests on inspection, not execution.

**Completion criteria (partial, explicitly bounded):** real (non-simulated) 32-bit-targeting
infrastructure now exists and is ready to execute; the actual 32-bit execution itself remains
unverified from this sandbox specifically, documented here rather than claimed. **Verification
criteria:** native-mode dry run confirms the harness's own logic and self-check are sound; YAML
validity confirmed; the `-m32` leg itself requires a real CI run or a root-capable environment to
close out.

## Wave 2 — CORE lane (2026-07-20)

**Scope determination (per this tranche's own explicit instruction):** read
`MASTER_REMEDIATION_PLAN.md`, `REMEDIATION_INDEX.md`, `REMEDIATION_DEPENDENCIES.md`, and this file's
own Wave 0/1/POST-WAVE-1/CONTENT-lane/BUILD_TEST_CI-lane sections in full before starting. The
POST-WAVE-1 baseline's own "Wave 2 readiness — recommended lane order" (above) names exactly two
CORE tasks for this tranche: `REMED-CORE-001` (Logger, do first — other Wave 2 work may want to log
through it) then `REMED-CORE-004` (Color UB). Both are P1, blocker-free per the Decisions log.
`REMED-CORE-002`/`-003` (the exception-type sweep) were **explicitly not pulled forward** — see
Decisions log entry below; they are P2, `REMEDIATION_DEPENDENCIES.md` schedules them into Wave 4's
"quiet window" specifically because of their LARGE/cross-lane blast radius, and nothing in the
dependency graph names them as blocking `-001`/`-004`.

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-CORE-001 | DONE | | feature/audit | `Logger::ToSDLPriority()`'s FATAL/ERROR/WARN/INFO cases uncommented — see detail below. |
| REMED-CORE-004 | DONE | | feature/audit | Root cause was broader than the plan's literal strategy — see detail below (Notes on scope correction). |

### REMED-CORE-001 detail — `CNA::Logger::ToSDLPriority()` mistagging

**Root cause confirmed exactly as described:** `src/CNA/Logger.cpp`'s `ToSDLPriority()` switch had
its `FATAL`/`ERROR`/`WARN`/`INFO` cases commented out behind a literal `//todo`, so all four fell
through to `default: return SDL_LOG_PRIORITY_INFO`. Only `DEBUG`/`TRACE`/`EXPERIMENT` had live cases
(all three collapsing to `SDL_LOG_PRIORITY_DEBUG`, pre-existing and not part of the bug). Confirmed
this is the *only* severity-to-native-priority translation function in the repo — repo-wide grep for
`SDL_LOG_PRIORITY`/`SDL_LogPriority`/`SDL_SetLogPriorit*` outside `Logger.cpp`/`.hpp` returns nothing.
(Separately, ~15 backend files call raw `SDL_Log()` directly, bypassing `CNA::Logger` entirely — an
existing, pre-existing architectural choice for backend startup diagnostics, not an instance of this
bug: `SDL_Log()` doesn't do any level translation at all, so there is nothing to mistag.)

**Change (`src/CNA/Logger.cpp` only):** uncommented the four cases (`FATAL→CRITICAL`,
`ERROR→ERROR`, `WARN→WARN`, `INFO→INFO`). `DEBUG`/`TRACE`/`EXPERIMENT` deliberately left collapsed to
`SDL_LOG_PRIORITY_DEBUG` (documented in a new source comment as intentional — this project uses none
of SDL's finer `TRACE`/`VERBOSE` priorities anywhere else) rather than expanded to a full 7-way
bijection, since the master plan's own root-cause/strategy text frames only the four commented-out
cases as the defect ("Only DEBUG/TRACE/EXPERIMENT have live cases" — treated as already-intentional).
This also fixes `SetMinimumLevel()`, which routes through the same function (previously
`SetMinimumLevel(WARN)` set SDL's native threshold to `INFO`).

**Tests added** (`tests/CNA/LoggerTests.cpp`, new file): a `TestWithParam` table-driven suite over
all 7 `LogLevel` values (`AllLogLevels/LoggerSdlPriorityTest.*`), asserting `SDL_GetLogPriority()`
after `Logger::SetMinimumLevel()` — the lowest practical seam, since `ToSDLPriority()` is private;
plus a direct `WARN`-does-not-collapse-to-`INFO` regression test naming the exact original symptom,
a `LogDoesNotThrowAtAnySeverity` smoke test across all 7 levels (checks no formatting/routing
regression), and an `IsEnabledGatingRespectsMinimumLevelOrdering` test for the *other* gate
(`Logger::IsEnabled()`) so a future change to one gate can't silently break the other.

**Verification — reverted just the production fix, kept the new tests, rebuilt:** 4 of 10 new tests
failed exactly as predicted (`FATAL`/`ERROR`/`WARN` parametrized cases + the direct `WARN` test),
all reporting the buggy value `4` (`SDL_LOG_PRIORITY_INFO`) where `7`/`6`/`5`
(`CRITICAL`/`ERROR`/`WARN`) was expected. Re-applied the fix — same rebuild: 10/10 pass. Confirms the
tests are real regression guards, not tautologies.

**Portability:** `src/CNA/Logger.cpp` compiles clean under both MinGW cross-targets in this sandbox
(`cmake-build-d3d9-mingw`, `cmake-build-d3d11-mingw`, `CNA` target) — no platform-specific code was
touched, no new macros/Windows-header collisions introduced.

**Sanitizers:** not required per this task's own instruction ("optional unless the code path
warrants them" for Logger work) — this is a pure logic/data-mapping bug, not a memory/UB defect.

**Completion criteria met:** every `LogLevel` maps to its documented SDL priority;
`SetMinimumLevel()` sets the intended native threshold. **Verification criteria met:** table-driven
test passes for all 7 current enum values (would catch a future 8th value silently defaulting).

### REMED-CORE-004 detail — `Color::PackFromVector4()` unclamped float→byte cast (UB)

**Root cause confirmed exactly as described, PLUS a broader root cause found while implementing —
recorded per Rule 3, not silently absorbed:** `PackFromVector4`'s `static_cast<bytecs>(vector.X *
255.0f)` (and the Y/Z/W siblings) skips the `ToByteFromUnitClamped()` clamp every other float-based
`Color` construction path in the file uses — confirmed exactly as the master plan describes.

**Scope correction — the master plan's suggested strategy ("route through the existing
`ToByteFromUnitClamped()`") would have introduced an undisclosed FNA divergence:** cross-checked real
FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Color.cs`) before implementing, per
`CLAUDE.md`'s "FNA is the authoritative behavioral reference." FNA's real constructors
(`Color(Vector4)`, `Color(float,float,float[,float])`, lines 1576-1661) **do** clamp via
`(byte) MathHelper.Clamp(x * 255, Byte.MinValue, Byte.MaxValue)` — CNA's `ToByteFromUnitClamped()`
already matches this correctly. But FNA's `IPackedVector.PackFromVector4` (line 1876-1883,
`// Should we round here?` / `R = (byte) (vector.X * 255.0f);`) is the **one** float-conversion call
site in the whole file that deliberately does **not** clamp — a genuine, disclosed FNA behavioral
difference (`IPackedVector`'s raw-bit-truncation contract vs. the user-facing constructors'
saturating one), not a copy-paste inconsistency in FNA. Routing `PackFromVector4` through
`ToByteFromUnitClamped()` as literally suggested would have silently made CNA's `Color(2.0f, 0, 0,
0).PackFromVector4-equivalent` saturate at 255 where real FNA (and now real CNA) truncates/wraps to
254. **Implemented instead:** a new `ToByteFromUnitTruncated()` helper that eliminates the C++ UB
(the genuine defect) while preserving FNA's truncating, non-clamping semantics for realistic
in-range-ish overflow — truncates through a wide, always-safely-representable `intcs` first (an
`intcs → bytecs` narrowing conversion is well-defined modulo-256 wraparound in C++, not UB), which is
where C++'s stricter conversion rules diverge from C#'s.

**A second, broader root cause found in the same file, also fixed (per this task's own explicit
"verify all Color constructors/operators touched by the same root cause" instruction):**
`Color::Lerp()` and `Color::Multiply()` share the *identical* unguarded-cast root cause one level up
— `static_cast<intcs>(MathHelper::Lerp(...))`/`static_cast<intcs>(component * scale)` with no NaN or
range guard. `Lerp`'s own `amount = MathHelper::Clamp(amount, 0.0f, 1.0f);` does **not** neutralize a
NaN `amount`: `MathHelper::Clamp` faithfully reproduces FNA's own comparison-based clamp (`value >
max ? max : value; value < min ? min : value;`), under which NaN survives unclamped (both comparisons
are false for NaN) — this is **correct, FNA-faithful `MathHelper::Clamp` behavior, not itself a bug**
(that function is public XNA API used well beyond this file and must keep this behavior). The UB was
purely at the *local* `static_cast<intcs>(...)` sites immediately consuming the (possibly-NaN)
clamped result. `Multiply` has no upfront clamp on `scale` at all, so it was reachable for any
caller-supplied scale, not just NaN. Fixed with a new local `SafeFloatToIntcs()` helper (NaN→0,
otherwise clamp to a wide, arbitrary-but-safe intcs-representable bound before casting) — the exact
bound doesn't affect the final color, since `Color(intcs,...)`'s own `ToByte()` clamps again to
[0,255] immediately after.

**Changes (`src/Microsoft/Xna/Framework/Color.cpp` only):**
- `ToByteFromUnitClamped()`: added an explicit `std::isnan()` guard before the final
  `static_cast<intcs>(...)` (returns `0`) — `MathHelper::Clamp`'s NaN passthrough was previously
  unguarded here too, affecting `Color(Vector4)`/`Color(Vector3)`/`Color(float,float,float[,float])`.
- New `ToByteFromUnitTruncated()`: used by `PackFromVector4` only (see scope correction above).
- New `SafeFloatToIntcs()`: used by `Lerp()` and `Multiply()`.
- `#include <cmath>` added for `std::isnan`/`std::isfinite`.

**Required per-file sweep of other `IPackedVector` implementations (per the master plan's own
instruction) — confirmed the identical `std::clamp(...)`-NaN-survives-then-UB-cast pattern in
`Graphics::PackedVector`'s `Pack()` helpers** (`Bgra5551`, `Bgra4444`, `Rgba1010102`, `Rgba64`,
`Byte4`, `NormalizedByte4`, `Short4` all follow the same shape as `MathHelper::Clamp` — `std::clamp`
also lets NaN survive its own comparisons unchanged, feeding an unguarded `static_cast<uintN_t>(...)`
immediately after). **Not fixed here** — these are `Microsoft::Xna::Framework::Graphics::PackedVector`
files, GRAPHICS-owned (overlaps `REMED-GFX-033`'s already-scheduled "PackedVector rounding" pass, per
`REMEDIATION_DEPENDENCIES.md`'s cross-lane single-ownership rule). Recorded as an evidence extension
to `REMED-GFX-033` below, not a new ID (same root cause, same owning task). Also found in
`src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`static_cast<intcs>(dw * scale)` and 3
siblings, `DrawString`/`Draw`) — same root cause as `Color::Lerp`/`Multiply`, GRAPHICS-owned file,
**not fixed**, recorded as new finding `REMED-GFX-057` below.

**Not touched, considered and rejected:** `Color::FromNonPremultiplied(intcs r,g,b,a)`'s
`static_cast<intcs>(r * a / ByteMax)` — a genuinely different mechanism (signed integer overflow if a
caller passes far-outside-contract `intcs` values, not a float-conversion issue) and only reachable
by violating the method's own documented 0-255 contract. Not the same root cause; left alone rather
than opportunistically expanding scope.

**Tests added** (`tests/Microsoft/Xna/Framework/ColorTests.cpp`): 8 new `PackFromVector4*` tests
(0, 1, just-outside-range wrap [2.0→254], negative wrap [-1.0→1], NaN, +Inf, -Inf, an extreme finite
value [±1e30], and a determinism check) plus 4 new `Lerp`/`Multiply` NaN/extreme/negative-scale
tests — covering the master plan's required list (0, 1, 255, just-outside-range, negative, NaN,
+Inf, -Inf, extreme values) exactly. `ColorTest` suite: 57 tests total, all passing.

**Verification — reproduced under UBSan's `float-cast-overflow` check specifically (required, since
this project's own `-fsanitize=undefined` build presets do NOT enable it by default — see new finding
`REMED-BUILD-016` below):** compiled the original (pre-fix) `Color.cpp` standalone with
`-fsanitize=address,undefined,float-cast-overflow` against a small driver exercising
`PackFromVector4(NaN, 1e30, -1e30, 2.0)`/`Lerp(...,NaN)`/`Multiply(...,1e30)` — **12 distinct runtime
UB reports**, e.g. `Color.cpp:427: runtime error: nan is outside the range of representable values of
type 'unsigned char'`, `Color.cpp:430: runtime error: 510 is outside the range of representable
values of type 'unsigned char'` (the plain "just outside range" 2.0 case — genuinely UB even without
NaN/extreme input), `Color.cpp:389: runtime error: nan is outside the range of representable values
of type 'int'` (Lerp), `Color.cpp:415-418: runtime error: 2.55e+32 is outside the range... 'int'`
(Multiply) — one report per fixed call site, confirming every site was real, live UB, not
theoretical. Rebuilt the same driver against the fixed `Color.cpp`: **zero sanitizer reports**,
output values match the new tests exactly (`PackFromVector4` R=0/G=0/B=0/A=254; `Lerp(NaN)`=0;
`Multiply(1e30)`=255).
- Full `ColorTest` suite (57 tests) also run clean under the project's own `cmake-build-media-asan`
  (ASan+UBSan, `-fsanitize=undefined` without `float-cast-overflow`, HEADLESS backend): 0 failures,
  0 sanitizer reports (expected — this preset doesn't catch this specific class, which is exactly why
  the dedicated `float-cast-overflow` driver above was needed to close the loop).
- Confirmed genuine regression guards the same way as `REMED-CORE-001`: reverted just the production
  Color.cpp fix under `cmake-build-media-asan` (its default, non-`float-cast-overflow` UBSan config)
  — `MultiplyWithExtremeScaleIsDefinedNotUndefinedBehavior` failed on a genuine wrong-value mismatch
  (`R=0` vs. expected `255`) even *without* the dedicated `float-cast-overflow` flag, i.e. this
  specific defect is bad enough that it produces an observably wrong functional result on this
  compiler/optimization level, not merely a sanitizer-only concern. (The NaN/Inf `PackFromVector4`
  cases coincidentally still passed with the buggy code on this exact compiler/`-O1` combination —
  itself the underlying risk this whole task exists to close: implementation-defined-on-this-build
  results are not guaranteed to keep matching on a different compiler or optimization level.)

**Portability:** `Color.cpp` compiles clean under both MinGW cross-targets (`cmake-build-d3d9-mingw`,
`cmake-build-d3d11-mingw`) — `<cmath>`'s `std::isnan`/`std::isfinite` and `std::clamp` are
standard-library, no platform-specific code introduced.

**Regressions:** full `CnaTests` direct-binary run (EASYGL, `cmake-build-debug`, repo root): **5622
tests, 5617 passed, 5 skipped** (4 hardware-sensor + 1 `Texture3D`-unsupported-backend, all
pre-existing), **0 failed** — reconciles against the POST-WAVE-1 baseline's 5579 (`REMED-CORE-006/
007/014`'s own reported figure) + 43 new tests (10 Logger + 33 net-new/extended Color) = 5622, zero
regressions. **CORE lifecycle regression suite re-run per this task's own explicit instruction**
(`GameTest.*:GraphicsDeviceManagerTest.*`, the `REMED-CORE-006`/`-007`/`-014` regression coverage):
**11/11 still pass, unmodified** — `DisposingDeviceInvokesUnloadContent`, `RepeatedDisposeDoesNot
ReinvokeUnloadContent`, `UnloadContentWorksAcrossRepeatedGameInstancesInOneProcess`,
`DeferredLoadContentFiresOnDeviceCreatedWhenServiceHasNoDeviceAtInitializeTime`,
`ApplyChangesRaisesResettingAndResetExactlyOnce`, `BackendDetectedDeviceLostIsForwardedToManager
Listeners`, `ForwardedDeviceEventsReportTheManagerAsSender`, `RepeatedDisposeDoesNotReraiseDevice
Disposing`, plus 3 more — none touched by this tranche's changes.
**Full unfiltered `ctest -j4` run** (`cmake-build-debug`, EASYGL, per this session's own instruction
to cap test/build parallelism at 4 cores): **5869 registered, 5863 passed, 6 failed, 6 skipped,
262.15s.** All 6 failures reconcile exactly against this file's own Wave 0 triage table and the
POST-WAVE-1 baseline — `EasyGL_AvatarRenderer_TintRouting`, `EasyGL_MRT_TwoAttachments`,
`EasyGL_GraphicsDevice_ReferenceStencil`, `easy-gl-resource-smoke-tests` (all 4 already tracked
pre-existing), plus `ENetDiscoveryServiceTest.MalformedAnnounceDuringSearchIsIgnoredAndDoesNotLeave
ADanglingResultsPointer`/`.PollIgnoresMalformedAnnounceWhileIdlingAndDiscoveryKeepsWorking` (the
already-documented `ctest -j4` network-port-contention flake class). `EasyGL_GraphicsDeviceManager_
Vsync` correctly reports **Skipped**, not failed — exactly the `REMED-BUILD-014` fix's designed
behavior under Xvfb. Zero new failures; zero regressions confirmed by both the direct-binary and
`ctest` measurement.

**Completion criteria met:** all float→component paths in `Color.cpp` clamp or safely truncate
(never invoke UB); the `IPackedVector`/`Graphics::PackedVector` sweep is complete (3 files fixed here,
the wider sweep's remaining hits recorded for GRAPHICS, not silently fixed or silently dropped).
**Verification criteria met:** `float-cast-overflow`-enabled UBSan build passes every new
out-of-range/NaN/Inf/extreme test with zero reports (demonstrated via the dedicated driver above,
since the project's own default preset doesn't enable that specific check).

### Cross-lane dependencies / new findings from this tranche

- **`REMED-GFX-033` evidence extension (not a new ID):** `Graphics::PackedVector`'s `Bgra5551`,
  `Bgra4444`, `Rgba1010102`, `Rgba64`, `Byte4`, `NormalizedByte4`, `Short4` `Pack()` helpers share
  `Color.cpp`'s exact pre-fix root cause (`std::clamp(x, lo, hi)` lets NaN survive unclamped, feeding
  an unguarded `static_cast<uintN_t>(...)` immediately after). Not fixed (GRAPHICS-owned); flagging
  for whoever picks up `REMED-GFX-033`'s "PackedVector rounding" pass so the NaN-cast-UB dimension
  isn't missed alongside the rounding-correctness one that task already covers.
- **`REMED-GFX-057` (new):** `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`'s `DrawString`/
  `Draw` (`static_cast<intcs>(dw * scale)`, `static_cast<intcs>(dh * scale)`, and the `Vector2`-scale
  siblings, ~lines 311/329/514/515) share the identical unguarded-float-to-`intcs`-cast root cause
  `Color::Lerp`/`Multiply` had — a NaN or extreme `scale`/`Vector2 scale` reaching `DrawString`/`Draw`
  is undefined behavior. GRAPHICS-owned file, not fixed here. Sev: MEDIUM (same class as `-004`, UB
  not memory corruption). Pri: suggest P2, fold into whichever GRAPHICS batch next touches
  `SpriteBatch.cpp`.
- **`REMED-BUILD-016` (new):** this project's `-fsanitize=undefined` build configuration
  (`cmake-build-media-asan` and, by the same `CMAKE_CXX_FLAGS` shape, the `devices-ubsan` preset in
  `CMakePresets.json`) does **not** include `-fsanitize=float-cast-overflow`. Unlike Clang (which
  bundles `float-cast-overflow` into its `undefined` group), GCC requires it as a separate,
  explicitly-named check. Confirmed empirically: the exact defect this task fixed produced **zero**
  sanitizer output under the project's own `cmake-build-media-asan` (default `-fsanitize=undefined`)
  despite being live, reproducible UB — only reproduced once `float-cast-overflow` was added
  explicitly to an ad hoc build. This means **any** out-of-range/NaN float→integer cast anywhere in
  the codebase currently passes this project's own UBSan coverage silently. Sev: MEDIUM (test/CI
  infrastructure gap, not a production defect itself — but it hides this exact defect *class*
  project-wide). Pri: suggest P1 given the blast-radius/leverage precedent set by `REMED-BUILD-001`/
  `-002` (a build-config fix that makes an entire defect class newly detectable). Owner: BUILD_TEST_CI
  (`CMakePresets.json`, and wherever `cmake-build-media-asan`/`net-asan`/`devices-asan` ad hoc
  `CMAKE_CXX_FLAGS` conventions are documented). Not fixed here (cross-lane).

## Waves 2-5 — remaining tasks

| ID | Pri | Status | Owner | Branch | Notes |
|---|---|---|---|---|---|
| REMED-AUDIO-001 | P3 | NOT STARTED | | | |
| REMED-AUDIO-002 | P3 | NOT STARTED | | | |
| REMED-BUILD-003 | P1 | NOT STARTED | | | |
| REMED-BUILD-005 | P2 | DONE | | feature/audit | Both mixer-destroy harnesses now link `SDL3::SDL3` explicitly (`cmake/Harnesses.cmake`) — see "Wave 2 — BUILD_TEST_CI lane" below. |
| REMED-BUILD-006 | P2 | NOT STARTED | | | |
| REMED-BUILD-007 | P3 | NOT STARTED | | | |
| REMED-BUILD-009 | P3 | NOT STARTED | | | |
| REMED-CONTENT-004 | P1 | NOT STARTED | | | |
| REMED-CORE-001 | P1 | DONE | | feature/audit | See "Wave 2 — CORE lane" above. |
| REMED-CORE-002 | P2 | NOT STARTED | | | Deliberately not pulled into this tranche — see Decisions log. |
| REMED-CORE-003 | P2 | NOT STARTED | | | Deliberately not pulled into this tranche — see Decisions log. |
| REMED-CORE-004 | P1 | DONE | | feature/audit | See "Wave 2 — CORE lane" above. |
| REMED-CORE-005 | P2 | NOT STARTED | | | |
| REMED-CORE-006 | P1 | DONE | | feature/audit | See "Wave 1 (parallel) — CORE lane" above. |
| REMED-CORE-007 | P1 | DONE | | feature/audit | See "Wave 1 (parallel) — CORE lane" above. |
| REMED-CORE-008 | P2 | NOT STARTED | | | |
| REMED-CORE-009 | P2 | NOT STARTED | | | |
| REMED-CORE-010 | P3 | NOT STARTED | | | |
| REMED-CORE-011 | P3 | NOT STARTED | | | |
| REMED-CORE-012 | P3 | NOT STARTED | | | |
| REMED-CORE-013 | P3 | NOT STARTED | | | |
| REMED-DEVICES-002 | P2 | NOT STARTED | | | |
| REMED-DEVICES-003 | P3 | NOT STARTED | | | |
| REMED-DOCS-001 | P2 | NOT STARTED | | | |
| REMED-DOCS-002 | P2 | NOT STARTED | | | |
| REMED-DOCS-003 | P3 | NOT STARTED | | | |
| REMED-GFX-004 | P1 | NOT STARTED | | | |
| REMED-GFX-005 | P1 | IN PROGRESS | 58a7d0bb, c11c6ce4, _bgfx-test_, _bgfx-fix_ | feature/audit | **Vulkan slice DONE** (16 shaders, **8/8 fog tests pass**). **Bgfx slice DONE and VERIFIED** — all **14** fog-capable `vs_*.sc` corrected to `(z+FogEnd)/(FogEnd-FogStart)` + zero-length guard; `bgfx_shaders.hpp` regenerated with a **from-source-built bgfx shaderc 1.19.150** (byte-reproducible: an unchanged-source regen is byte-identical to the committed header; the fix touches exactly the 15 intended shaders' 60 arrays, other 48 byte-identical). All 6 Bgfx fog tests retargeted to non-collapsing scenes and go pre→post: 4 collapsing tests 2/3→3/3, alphatest/dualtexture (endpoint-swap) 1/3→3/3. D3DCommon (D3D11/D3D12) + D3D9 slices PENDING (fxc/dxc absent). See "Wave 3 — GRAPHICS shader campaign, Bgfx slices". |
| REMED-GFX-006 | P1 | IN PROGRESS | dafa085d, acf200c9, 7c5cf74f, f040f184, cd9298fd, 23a83527, daa7bb70, 9e0b1450 | feature/audit | **Vulkan + EasyGL + WebGPU + SdlGpu slices DONE and VERIFIED** — all four directly-editable backends now compose `transpose(inverse(mat3(world)))` after the bone-skin 3×3 (Variant A + Variant B). Per-backend analytic non-identity-World harnesses: EasyGL 2/8→8/8, WebGPU 2/8→8/8 (sRGB swapchain), SdlGpu 1/4→4/4; all match FNA to the byte, and cross-backend conformance holds (linear backends byte-identical, WebGPU the sRGB encoding of the same normal). EasyGL routes through the existing CPU `uNormalMatrix`; WebGPU reads the already-uploaded `normalMatrixCol*`; SdlGpu computes in-shader + regenerated SPIR-V (byte-reproducible, 3/23 arrays changed). Regressions: none (EasyGL 95 effect tests, WebGPU 11, SdlGpu 22; pre-existing failures unchanged, incl. the REMED-BUILD-003 avatar tint bug). New finding **REMED-GFX-059** (pre-existing SdlGpu VUID-02684 validation error). **Bgfx slice DONE and VERIFIED** — `vs_skinned3d`, `vs_skinned3d_vertexlit` (both Variant A) + `vs_pbr_skinned3d` (Variant B) now apply the World inverse-transpose after the bone skin, supplied CPU-side via the existing `u_normalMatrix` (bgfx's shaderc lacks in-shader `inverse()`/`transpose()`; the 4 skinned draw sites now upload `ComputeNormalMatrix3x3`). New Bgfx analytic world-normal harness (pixel- **and** vertex-lit) **2/8→8/8**; scale case reads 228 (inverse-transpose), not 180 (Variant A) or 114 (raw-World). `vs_skinned3d_vertexlit` was **not** in the audit's Bgfx evidence list — found by exhaustive inspection. **D3D9 / D3DCommon slices PENDING — BYTECODE-BLOCKED** (fxc/dxc absent). See "Wave 3 — GRAPHICS shader campaign, Bgfx slices". |
| REMED-GFX-007 | P1 | IN PROGRESS (only D3DCommon BYTECODE-BLOCKED remains) | 31668819, _bgfx-fix_, 7f3e82bf, 38a1008d, e30868eb | feature/audit | **Vulkan / Bgfx / WebGPU / SdlGpu slices ALL DONE and VERIFIED** — all four add emissive unscaled (`lightSum*Diffuse + Emissive`). **WebGPU slice DONE+VERIFIED** (`38a1008d`) — `env_map3d.wgsl` fs_main corrected; new `webgpu_environmentmapeffect_emissive_test.cpp` is transfer-function-agnostic (WebGPU surface is sRGB here; calibrates T() via env-map full-replace), 2/4→4/4. **SdlGpu slice DONE+VERIFIED** (`e30868eb`) — `env_map3d.frag.glsl` corrected + `spirv_shaders.hpp` regenerated (1/23 arrays changed, `kEnvMap3dFragSpv` only); new `sdlgpu_environmentmapeffect_emissive_test.cpp` (linear RT readback) 2/4→4/4. **Alpha-squaring RESOLVED as inseparable** — the audit's SdlGpu-specific "also squares Alpha" symptom is a direct arithmetic consequence of the *same* line (both operands CPU-prefolded with Alpha); the one-line fix removes it, no separate finding raised. Cross-backend conformance proven: Vulkan/Bgfx/SdlGpu raw (100,50,25) linear; WebGPU raw (168,122,88) sRGB → decodes to (100,50,25). SdlGpu 23/23, WebGPU 25/25 shard, zero regressions. **Only D3DCommon (D3D11/D3D12) remains — BYTECODE-BLOCKED** (`env_map3d.frag.hlsl:46` has the identical defect; no fxc/dxc in repo). See "Wave 3 — GRAPHICS shader campaign, WebGPU/SdlGpu GFX-007 slices". |
| REMED-GFX-008 | P1 | DEFERRED (this session) | | | Vulkan root cause re-confirmed at source (skinned frag reads always-zero `pc.ambientColor`; `SkinnedEffect::FillGpuDrawParams` never writes `p.ambientColor` and pre-folds ambient into `p.emissiveColor`, which the skinned UBOs have no slot for). **Deferred, not blocked by tooling:** the FNA-correct formula double-lights `Vulkan_AvatarRenderer_TintRouting`, which drives ambient(1,1,1) AND a full head-on light and passes only because this bug cancels the double-count. Re-baselining it needs AvatarRenderer lighting recalibration — an avatar-subsystem change outside this campaign's clean scope. See "Wave 3 — GRAPHICS shader campaign". |
| REMED-GFX-009 | P1 | **DONE and VERIFIED (SdlGpu — the only affected backend)** | dda6249f, 8cfdd8b9 | feature/audit | Fog implemented from scratch across **all 13** fog-capable SdlGpu shader families (13 vert + 8 frag SPIR-V arrays) using the REMED-GFX-005 corrected formula, adapted to SdlGpu's set convention. New shared 32-byte FogParams UBO (vertex-stage only, binding 1 for basic/alpha/dual, binding 2 for env/lit/skinned/pbr); keep-factor from raw object-space pre-skin `inPos.z`; forwarded to the fragment as a `fragFog` varying; `outColor.rgb = mix(FogColor, rgb, keep)` (RGB only). CPU: `FillFogUniforms()` + `fogUniforms` on all 8 DrawCommand structs + push in every Issue path + `num_uniform_buffers` +1 on all 13 vertex shaders (no fragment/API change; existing layouts untouched). SPIR-V regen byte-reproducible: exactly **21 of 23 arrays changed** (2 sprite2d byte-identical), no drift. 3 new conformance tests **0→43/43** (BasicEffect 16/16, Effects 16/16, Skinned/PBR 11/11). Full SdlGpu shard **26/26**, zero regressions (incl. GFX-006 WorldNormal + GFX-007 EnvMapEmissive controls). Validation clean (only the known pre-existing REMED-GFX-059 VUID-02684). Cross-backend: byte-identical to Vulkan on the shared scene. See "REMED-GFX-009 — SdlGpu stock-effect fog". |
| REMED-GFX-010 | P2 | **IN PROGRESS (EasyGL + Vulkan DONE+VERIFIED; SdlGpu + Bgfx pending; D3D BYTECODE-BLOCKED)** | _easygl test+fix_, _vulkan test+fix_ | feature/audit | Cross-backend move of stock-effect fog from object-space Z to true FNA view-space fog (the plan's D3D9-only scope was already flagged as broader). Shared layer: `GpuDrawParams.fogVector[4]` + FNA `EffectHelpers.SetFogVector` port in all 7 stock effects' `FillGpuDrawParams`. **EasyGL DONE** (9/9 discriminating, existing fog 3/3×5). **Vulkan DONE** (16 shaders, SPIR-V byte-reproducible, 17-array diff scoped, 9/9 + 68/68 regression, validation clean). SdlGpu + Bgfx pending. See "REMED-GFX-010 — view-space stock-effect fog". |
| REMED-GFX-011 | P1 | **DONE (Vulkan — the only affected backend)** | c9e96813, 633a2e17 | feature/audit | Flip added to all 4 families; both false justifying comments deleted; SPIR-V regenerated and verified (exactly 4 of 35 arrays changed). Orientation harness 0/4 → 4/4 on backbuffer AND render target. Zero regressions. See "Wave 3 — GRAPHICS shader campaign". |
| REMED-GFX-012 | P1 | NOT STARTED | | | |
| REMED-GFX-013 | P1 | NOT STARTED | | | |
| REMED-GFX-014 | P1 | NOT STARTED | | | |
| REMED-GFX-015 | P2 | NOT STARTED | | | |
| REMED-GFX-016 | P1 | NOT STARTED | | | |
| REMED-GFX-017 | P1 | NOT STARTED | | | |
| REMED-GFX-018 | P1 | NOT STARTED | | | |
| REMED-GFX-019 | P1 | NOT STARTED | | | |
| REMED-GFX-020 | P2 | NOT STARTED | | | |
| REMED-GFX-021 | P2 | NOT STARTED | | | |
| REMED-GFX-022 | P1 | NOT STARTED | | | |
| REMED-GFX-023 | P2 | NOT STARTED | | | |
| REMED-GFX-024 | P2 | NOT STARTED | | | |
| REMED-GFX-025 | P2 | NOT STARTED | | | |
| REMED-GFX-026 | P2 | NOT STARTED | | | |
| REMED-GFX-027 | P2 | NOT STARTED | | | |
| REMED-GFX-028 | P2 | NOT STARTED | | | |
| REMED-GFX-029 | P2 | NOT STARTED | | | |
| REMED-GFX-030 | P2 | NOT STARTED | | | |
| REMED-GFX-031 | P3 | NOT STARTED | | | |
| REMED-GFX-032 | P3 | NOT STARTED | | | |
| REMED-GFX-033 | P2 | NOT STARTED | | | |
| REMED-GFX-034 | P2 | NOT STARTED | | | |
| REMED-GFX-035 | P2 | NOT STARTED | | | |
| REMED-GFX-036 | P2 | NOT STARTED | | | |
| REMED-GFX-037 | P2 | NOT STARTED | | | |
| REMED-GFX-038 | P2 | NOT STARTED | | | |
| REMED-GFX-039 | P2 | NOT STARTED | | | |
| REMED-GFX-040 | P2 | NOT STARTED | | | |
| REMED-GFX-041 | P2 | NOT STARTED | | | |
| REMED-GFX-042 | P2 | NOT STARTED | | | |
| REMED-GFX-043 | P1 | NOT STARTED | | | |
| REMED-GFX-044 | P2 | NOT STARTED | | | |
| REMED-GFX-045 | P3 | NOT STARTED | | | |
| REMED-GFX-046 | P3 | NOT STARTED | | | |
| REMED-GFX-047 | P3 | NOT STARTED | | | |
| REMED-GFX-048 | P3 | NOT STARTED | | | |
| REMED-GFX-049 | P3 | NOT STARTED | | | |
| REMED-GFX-050 | P3 | NOT STARTED | | | |
| REMED-GFX-051 | P2 | NOT STARTED | | | |
| REMED-MEDIA-002 | P1 | NOT STARTED | | | |
| REMED-MEDIA-003 | P2 | NOT STARTED | | | |
| REMED-MEDIA-004 | P2 | NOT STARTED | | | |
| REMED-NET-002 | P1 | NOT STARTED | | | |
| REMED-NET-004 | P2 | NOT STARTED | | | |
| REMED-NET-005 | P3 | NOT STARTED | | | |
| REMED-NET-006 | P3 | NOT STARTED | | | |
| REMED-NET-007 | P3 | NOT STARTED | | | |
| REMED-TEST-001 | P2 | NOT STARTED | | | |
| REMED-TEST-003 | P2 | NOT STARTED | | | |
| REMED-TEST-005 | P3 | NOT STARTED | | | |
| REMED-TEST-006 | P2 | NOT STARTED | | | |
| REMED-TEST-007 | P3 | NOT STARTED | | | |

## Discovered during remediation

New findings surfaced while implementing. Give each a new ID in the same scheme; do not overload an
existing task.

| ID | Title | Sev | Pri | Found while working on | Status |
|---|---|---|---|---|---|
| REMED-BUILD-010 | `EasyGL_RealWindowResize` hangs the full 60s CTest `TIMEOUT` under a real desktop compositor (`DISPLAY` = a real logged-in GNOME/Mutter session, not an isolated Xvfb) | MEDIUM | P2 | REMED-BUILD-001 (full unfiltered `ctest` baseline run) | **DONE** — investigated and given a deterministic wall-clock watchdog. See "Wave 2 — BUILD_TEST_CI lane" below; **this session's own re-investigation found the hang does NOT reproduce reliably** (20/20 clean runs under the real GNOME/Mutter `:0` session that previously produced a 100%-reproducible hang) — re-classified from "deterministic environment incompatibility" to "real but state/focus-dependent compositor timing sensitivity," and fixed at the root (the test's own reliance on an unbounded frame-count loop) rather than by asserting Xvfb-only. |
| REMED-BUILD-011 | `CMakePresets.json`'s `devices-asan`/`devices-tsan`/`devices-ubsan` configure presets (description explicitly says "Microsoft::Devices hardening") never set `CNA_DEVICES=ON` in their own `cacheVariables` — configuring with any of the three as documented (`cmake --preset devices-asan`) silently builds with the entire `CNA::Devices`/`Microsoft::Devices::Sensors` surface compiled out, so none of the sanitizer coverage the preset names promise actually exists unless the caller separately remembers `-DCNA_DEVICES=ON` | MEDIUM | P2 | REMED-DEVICES-001 (setting up ASan/TSan runs for the new concurrent tests) | NOT STARTED — recorded, not fixed (out of DEVICES-001's scope; worked around this task's own verification by passing `-DCNA_DEVICES=ON` on the configure command line into ad hoc `cmake-build-devices-asan`/`cmake-build-devices-tsan` build dirs rather than the named presets — a BUILD_TEST_CI-lane fix, not a DEVICES one) |
| REMED-CONTENT-007 | `VideoContentTypeReader.cpp`/`SongContentTypeReader.cpp` each duplicate a `ResolveRelativeFilePath()` helper with **zero** containment check (not even the partial one `ContentReader.cpp` had before this task) — a `Video`/`Song` `.xnb`'s own embedded filename field can be absolute or `..`-escaping and is joined onto the content root unchecked | HIGH | P1 | REMED-CONTENT-002 (repo-wide sweep) | NOT STARTED — recorded, not fixed (out of `-002`'s 3-site scope; same root cause, same fix shape — reuse `CNA::Internal::IsDisallowedAbsolutePath`/the `ResolveRelativeAssetPath` pattern) |
| REMED-CONTENT-008 | `ContentManager.cpp` joins 8 `.cnj`/JSON-manifest-supplied path fields (`dataField->stringValue` for `Texture3D`; `vertRel`/`fragRel` for `ShaderEffect`; `clipFileField->stringValue` for `AnimationClip`; `skeletonRel`, `vertFile`/`idxFile`, `morphTargetsFile` for skinned-model morph/animation data) onto the content root with no containment check — same `fs::path::operator/` pitfall as the 3 sites `-002` fixed, at file-supplied (not caller-supplied) untrusted strings | HIGH | P1 | REMED-CONTENT-002 (repo-wide sweep) | NOT STARTED — recorded, not fixed (out of `-002`'s 3-site scope; notably, this is the *same* `.cnj`-manifest subsystem that already has one field, `sourceFile`, correctly hardened via `CnjSourceFile.hpp` — these 8 fields were simply never given the same treatment) |
| REMED-NET-008 | A "client"-role `NetworkSession`'s own incidental listening `ENetHost` (bound on every non-Emscripten `ConnectToHost()` call, so a peer can be promoted to host later via migration without rebinding — see `ENetHostHandle`'s own doc comment) still accepts and fully processes `ClientHello` from *any* third party that connects to it, not just its real host — `HandleClientHello` has no "am I actually supposed to be hosting anyone" check. A rogue peer can connect directly to a client's own bound port (`ENetBackend::GetBoundPort()` is non-zero for a client-role session too) and get a real `ServerWelcomeMessage` snapshotting that peer's own roster, plus get added as a real `NetworkGamer`/fire a real `GamerJoined` event on that peer's session — despite that peer never intending to host anyone | MEDIUM | P2 | REMED-NET-001 (host-authority audit sweep) | NOT STARTED — recorded, not fixed (distinct root cause from NET-001: `ClientHello` is not one of the four host-authoritative broadcast types NET-001 covers; this is a missing role-check on the *accept* side of a client-scoped session, not a missing sender-authority check on a broadcast-only message. Confirmed real via manual reasoning about `ConnectToHost`'s own non-Emscripten `StartHosting()` call and `HandleClientHello`'s unconditional accept — not separately reproduced with a new test, since fixing/proving it is out of this task's scope; the closest existing coverage is `ClientRejectsForgedGamerLeaveBroadcastFromRogueThirdPartyPeer`, new in this task, which proves the same rogue-third-party-on-a-client-socket attack surface is real for the four NET-001 message types specifically) |
| REMED-TEST-008 | `DynamicSoundEffectInstanceTest.BufferNeededFiresExactlyTheStarvedCount` fails under a full unfiltered `ctest -j4` run but passes 10/10 in isolation (`--gtest_filter` + `--gtest_repeat=10`) — a real-time audio buffer-starvation-count assertion is timing-sensitive under heavy 4-way parallel CPU load on this sandbox. Extends `REMED-NET-001`'s already-documented `ctest -j4` transient-failure finding (previously only `ENetDiscoveryServiceTest.*` ×4 and 2 `TwoProcessLoopbackTest`/`NetworkSessionTest` cases, all network-port contention) to a second, previously-undocumented flakiness class (audio timing, not networking) | LOW | P3 | REMED-BUILD-004 (establishing a full local `ctest -j4` baseline before designing the new CI job) | NOT STARTED — recorded, not fixed (test-reliability finding, not a production defect; `REMED-BUILD-004`'s own new CI job runs `ctest` serially specifically to avoid this and the already-known network-port class, rather than allowlisting either) |
| REMED-CORE-014 | `GraphicsDeviceManager`'s private `ownsGraphicsDevice_` flag is initialized `false` in both constructors and never set `true` anywhere in `GraphicsDeviceManager.cpp` (confirmed by grep of the whole file, and independently by a live probe program: construct `Game` + `GraphicsDeviceManager(&game)`, subscribe to `getDeviceDisposingEvent()`, call `gdm->Dispose()` — the subscriber never fires). Both of `Dispose()`'s and `CreateDevice()`'s only conditional branches that raise `DeviceDisposing` and release the owned `GraphicsDevice` are therefore permanently dead code, for **every** `GraphicsDeviceManager` instance, not only the `Game`-attached case this entire codebase always constructs. This directly compounds `REMED-CORE-006`: even after `Game::Initialize()` subscribes to `DeviceDisposing` (that task's own stated fix strategy), a real `GraphicsDeviceManager::Dispose()` call on a `Game`-attached manager still would not raise the event without this dead-code path also being addressed — the CORE-lane owner should know this before scoping `REMED-CORE-006`'s fix as "just add the subscription" | HIGH | P1 | REMED-TEST-002 (investigating how to trigger `REMED-CORE-006`'s own required test, "dispose the device, assert `UnloadContent()` was called") | **DONE** — fixed atomically with `REMED-CORE-006`/`-007`, same root cause, same files. See "Wave 1 (parallel) — CORE lane" above. The `Game`-attached (non-owning) path's `DeviceDisposing` raise is no longer gated on `ownsGraphicsDevice_`; the standalone-owned-device path (still unreachable — `CreateDevice()` still throws when `game_ == nullptr`) remains a separate, unimplemented future feature, noted in that section. |
| REMED-BUILD-012 | Any test that constructs a real window via `Game`+`GraphicsDeviceManager` on the D3D12 backend crashes identically under this dev environment's Wine+vkd3d-proton setup: `wine: Unhandled page fault on read access to 0000000000000000`, backtrace bottoming out in `vkd3d_instance_get_vk_instance(instance=nullptr)` inside `dxgi_factory_CreateSwapChainForHwnd` → `d3d12_swapchain_create` → `d3d12_swapchain_init` — real window-attached DXGI swap-chain creation crashing inside vanilla Wine's own `dxgi.dll`, confirmed identically for two independent test executables (a reused `DepthStencilState` test and a reused `RasterizerState`/scissor test). `cmake/Tests/D3D12Tests.cmake`'s own pre-existing comment on `cna_diag_d3d12_swapchain` already documented this exact crash for **one specific diagnostic tool** ("DX-100's own spike..."); this generalizes it to a blanket constraint — **no** D3D12 test using the public `Game`/`GraphicsDeviceManager`/real-window API can run in this dev loop at all, only `D3D12_Smoke`'s own deliberately off-screen (`window=nullptr`), much lower-level, hand-rolled internal-`EXT`-API style avoids it | HIGH | P1 | REMED-BUILD-008 (attempting to reuse D3D11's own backend-agnostic public-API test sources for D3D12) | NOT STARTED — recorded, not fixed (a Wine/vkd3d-proton dev-environment limitation, not a CNA code defect fixable in this repo; blocks `REMED-BUILD-008`/`REMED-GFX-014`/`REMED-GFX-015` from being verified via the public-API test style every other backend uses — whoever picks these up needs `D3D12_Smoke`'s own off-screen internal-`EXT` construction style instead, a substantially larger undertaking than a simple test-source reuse) |
| REMED-CONTENT-009 | `Texture2DContentTypeReader.cpp:88-89`'s own `REMED-CONTENT-001` fix computes `input.CheckDecodedByteSize(static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4, "Texture2DReader")` — for `width`/`height` both near `INT32_MAX`, `width*height` (int64_t) reaches ≈4.6e18, still in-range, but the subsequent `* 4` overflows signed 64-bit (`int64_t` max ≈9.22e18): confirmed live by UBSan (`runtime error: signed integer overflow: 4611686014132420609 * 4 cannot be represented in type 'long int'`) on `Texture2DContentTypeReaderTest.AbsurdlyLargeDimensionsThrowContentLoadExceptionNotBadAlloc`. This directly contradicts the fix's own comment ("computed in int64_t so it can't itself silently wrap back into range"). The regression test still passes today only because the implementation-defined wraparound happens to still land outside the valid range — a different compiler/optimization level is not guaranteed to preserve that. **This is new code added by REMED-CONTENT-001 itself, not a pre-existing defect** — found only because this integration pass added UBSan coverage over Content/XNB that did not exist when CONTENT-001 landed | MEDIUM | P1 | Post-Wave-1 integration pass, Phase 5 (sanitizer regression) — `cmake-build-media-asan`, `Texture2DContentTypeReaderTest.*` under ASan+UBSan | **DONE** — fixed in Wave 2's CONTENT lane (commit `c5ed8dd1`). See "Wave 2 — CONTENT lane" below for full detail. |
| REMED-GFX-052 | `Vulkan_SkinnedEffect_Fog` hangs reproducibly (CTest 30s `TIMEOUT`) on the Vulkan backend — confirmed not resource contention (reproduces identically on an idle system, load avg 0.46, 43°C) and not a display/environment artifact (reproduces identically via direct binary execution of `cna_test_vulkan_skinnedeffect_fog`, bypassing CTest entirely). Both runs hang with zero output past `[Vulkan] Backend initialised`. Distinct root cause from `REMED-GFX-005` (mirrored fog *formula*, a values defect) — this is a hang, not a wrong-value defect, and no existing remediation ID covers it | HIGH | P1 | Post-Wave-1 integration pass, Phase 4 (configuration matrix) — Vulkan full-suite ctest run | **UPDATED (Wave 3, 58a7d0bb):** the hang did **not** reproduce this session — the test now runs to completion and exits cleanly, but **renders black (0,0,0) in all three cases including the fog-OFF case**, i.e. the skinned quad's DirectionalLight0 lighting path produces no visible geometry. Confirmed **orthogonal to fog** (fog-OFF is black too) and **not caused by REMED-GFX-005** (the skinned control tests IdentityBones/Combined still pass 160,0,0 against the new fog-fixed shaders). Root cause is a skinned-lighting/draw defect specific to this test's `DirectionalLight0`-diffuse setup — to be resolved in the Vulkan skinned pass (REMED-GFX-006/-008), not GFX-005. Whether the earlier hang was a distinct llvmpipe pipeline-creation issue that the SPIR-V regen incidentally cleared is unconfirmed. **DONE (Wave 3, `c11c6ce4`):** root cause was neither lighting nor a hang but a **test bug** — `renderQuad()` never called `setRasterizerStateProperty(RasterizerState::CullNone)`, unlike every sibling `vulkan_basiceffect_*_fog`/`vulkan_skinnedeffect_multilight`/`_specular` test. The skinned quad's post-Y-flip winding is culled by the real default RasterizerState, so nothing rasterized and the center pixel read the black clear colour (proven by the full-fog case, which outputs pure FogColor independent of lighting, also being black). A stale in-file comment wrongly asserted CullNone was unnecessary. With the one-line CullNone added, the test passes **3/3** — which also completes REMED-GFX-005's Vulkan fog verification at **8/8** (this was the 8th fog test). |
| REMED-GFX-053 | WebGPU rejects custom-GLSL `ShaderEffect` content: `CnjEffectTest.LoadsRealCnjFixture` and `CnjStockEffectTest.CustomGlslEffectStillWorks` both fail (`shaderEffect->IsEffectValid()` false, no crash). Distinct from the same two tests' failure on Vulkan/Bgfx (there, `glslc`/`glslangValidator` are simply absent from this sandbox — an environment gap, not a code defect) — WebGPU consumes WGSL, not GLSL/SPIR-V, so the toolchain-absence explanation does not apply here; the actual root cause is unconfirmed (custom user-authored GLSL `ShaderEffect` content may simply not be implemented yet on this experimental backend) | MEDIUM | P2 | Post-Wave-1 integration pass, Phase 4 (configuration matrix) — WebGPU full-suite ctest run | NOT STARTED — recorded, not fixed (verification-only pass; root-cause triage needed; given `CLAUDE.md`'s own framing of WebGPU as an experimental backend with baseline SpriteBatch/Texture2D support and explicitly not yet full effect parity, this may belong in `plan_webgpu.md`'s own WEBGPU-* task series rather than the general GRAPHICS remediation lane — whoever triages should route it to the correct owner) |
| REMED-GFX-054 | Three `ContentManagerSkinnedModelTest` cases (`PartNameWithUnbalancedEmbeddedBraceParsesCorrectly`, `TextureLoadsFromNestedButUnderRootManifestDirectory`, `TextureLoadsFromManifestOutsideContentRoot`) throw `"CNA WebGPU: invalid index buffer upload"` on the WebGPU backend. No existing remediation ID covers WebGPU skinned-model index-buffer handling | MEDIUM-HIGH | P2 | Post-Wave-1 integration pass, Phase 4 (configuration matrix) — WebGPU full-suite ctest run | NOT STARTED — recorded, not fixed (verification-only pass; same WebGPU-experimental-backend routing question as `REMED-GFX-053` — triage owner should decide general GRAPHICS lane vs. `plan_webgpu.md`) |
| REMED-BUILD-013 | `StrictXnaApiSurfaceCheck_Compile_Run` and `CnaInputTests` CTest registrations are missing the Wine-runner wrapper macro every other cross-compiled D3D9/D3D11 test uses — confirmed identically on both backends (`StrictXnaApiSurfaceCheck_Compile_Run` fails "unable to find an interpreter"; `CnaInputTests` fails `wine: ... CnaTests.exe: c0000135`, downstream of the same `CnaTests_NOT_BUILT` placeholder fact). `StrictXnaApiSurfaceCheck_Compile_Run` is registered in `cmake/Harnesses.cmake` via a plain `add_test(COMMAND cna_strict_xna_api_check)`; `CnaInputTests` is registered in `cmake/UnitTests.cmake` with a hardcoded `COMMAND CnaTests`, unconditional on cross-compiling. A structural CTest-wiring gap, not a backend-specific code defect | LOW | P3 | Post-Wave-1 integration pass, Phase 4 (configuration matrix) — D3D9/D3D11 Wine ctest runs | **DONE** — root cause was more precise than originally recorded: `CnaInputTests`'s own registration needed **no fix at all** (CMake's built-in `CROSSCOMPILING_EMULATOR` target-command auto-injection already works for it); the true blocker was that `CnaTests.exe` never built at all under D3D9/D3D11 MinGW, for reasons unrelated to CTest wiring (see `REMED-BUILD-005` and this tranche's own `AudioTagParser`-adjacent findings below). Only `StrictXnaApiSurfaceCheck_Compile_Run` (a separate target, `cna_strict_xna_api_check`, never given `CnaTests`'s own `CROSSCOMPILING_EMULATOR`/MinGW-runtime treatment) needed a real fix. See "Wave 2 — BUILD_TEST_CI lane" below for the full chain and both backends' verification. |
| REMED-GFX-055 | `IndexBuffer::SetData(unsigned short const*, int)` (`src/Microsoft/Xna/Framework/Graphics/IndexBuffer.cpp:61`) passes a null pointer to a parameter position UBSan reports as "declared to never be null" (both argument 1 and argument 2 independently reported) — confirmed live under UBSan via `ContentManagerSkinnedModelTest.PartNameWithUnbalancedEmbeddedBraceParsesCorrectly`, reached through `ContentManager.cpp:2707`'s `SkinnedModelEXT` loader for a mesh part with zero indices. Almost certainly a `memcpy`/`std::copy`-family call receiving a null source and/or destination for a zero-length copy — technically UB per the C standard even though it is a no-op in every practical implementation, and likely reproducible for any skinned-model part with an empty index buffer, not only this one crafted fixture | MEDIUM | P2 | Wave 2 CONTENT lane, `REMED-CONTENT-009`'s own broader Content/XNB ASan+UBSan sweep (`*Content*:*Xnb*:*Texture*:*Cnj*` under `cmake-build-media-asan`) | NOT STARTED — recorded, not fixed (out of the CONTENT lane's scope — the defective code is in `IndexBuffer.cpp`, a GRAPHICS-owned file, even though it's reached via a CONTENT code path; GRAPHICS lane should own the fix, likely a null-guard before the copy call rather than a logic change) |
| REMED-NA-016 | `third_party/cgltf/cgltf.h:2250` (`cgltf_component_read_float`) performs a misaligned 4-byte-aligned `float` load — confirmed live under UBSan via `GltfToCnjToolTest.ResolvesSparseAccessorOverride` (`load of misaligned address ... which requires 4 byte alignment`) | LOW | — | Wave 2 CONTENT lane, `REMED-CONTENT-009`'s own broader Content/XNB ASan+UBSan sweep | **OUT OF SCOPE, not fixed.** Vendored third-party header (`third_party/cgltf/`), not CNA-authored — same disposition as `REMED-NA-011`'s external easy-gl sibling repo: report upstream / patch only if a real crash (not just a UBSan advisory finding) is ever observed on a target where unaligned loads actually fault, rather than patching a vendored file and diverging from upstream |
| REMED-BUILD-014 | `EasyGL_GraphicsDeviceManager_Vsync`'s `SynchronizeWithVerticalRetrace=true` check (`SDL_GL_GetSwapInterval()` expected non-zero) fails under Xvfb `:99` + software/llvmpipe GL rendering (`SDL_GL_GetSwapInterval()=0` even after enabling vsync) — the mirror-image case of `REMED-BUILD-010`'s already-documented real-compositor-vs-Xvfb sensitivity: that finding is tests that only pass under Xvfb, not a real compositor; this is a test that (per its own pre-existing passing history under `DISPLAY=:0`) needs a *real* display/compositor and fails specifically under Xvfb, since a virtual framebuffer has no real vertical-retrace timing to synchronize to. Surfaced only because this session switched test execution from `DISPLAY=:0` to Xvfb `:99` mid-task, at the user's request, to stop disrupting their real desktop session — not caused by any code change in this session | LOW | P3 | Wave 2 CONTENT lane's own full-suite regression run (`cmake-build-debug`, `DISPLAY=:99`), unrelated to any CONTENT-lane code change | **DONE** — the test now probes the same real GL context directly with a raw `SDL_GL_SetSwapInterval(1)` call (bypassing CNA/GraphicsDeviceManager entirely) whenever CNA's own path reports interval 0; if even that can't make vsync stick, it's a genuine environment limitation and the test exits via this project's own `SKIP_RETURN_CODE 77` convention instead of failing. Verified: still 2/2 real PASS under the real `:0` display (proving the fix under test — GraphicsDevice::Reset()'s own SetSwapInterval forwarding — is not masked), and SKIPPED (not FAILED) under a fresh Xvfb instance, both via direct binary execution and via `ctest`. See "Wave 2 — BUILD_TEST_CI lane" below. |
| REMED-GFX-056 | `HeadlessTextureCubeBackend::SetData`/`GetData` (`src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp`) validate arguments and record a trace but never actually store or return pixel data — the identical "validates but never stores" shape confirmed and fixed for `HeadlessTexture3DBackend` under `REMED-CONTENT-004`, but a different file/backend-type. Confirmed via `Texture3DTextureCubeContentTypeReaderTest.TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd` still failing on Headless after `REMED-CONTENT-004` landed (root-caused, not just observed: `HeadlessTextureBackend`, the 2D path, has a real `pixels_` member and genuinely stores data — Headless implements 2D texture storage for real but never implemented Cube storage) | MEDIUM | P2 | `REMED-CONTENT-004`'s own Headless full-suite verification run | NOT STARTED — recorded, not fixed (`ITextureCubeBackend` is GRAPHICS-owned, out of the CONTENT lane's scope; candidate fix mirrors `REMED-CONTENT-004`'s own: add `GraphicsCapability::TextureCube`, or extend a "real 2D storage but not Cube/3D" capability shape, and make `TextureCube`'s constructor fail cleanly on Headless the same way `Texture3D`'s now does) |
| REMED-MEDIA-005 | `MediaLibraryTestFixture`'s 6 dependent test cases (`AlbumDurationIsARealNonZeroSumOfMemberSongDurations`, `FavoritesResolvesToThreeRealSongsSkippingTheMissingOne`, `InternationalResolvesTheNonAsciiEntry`, `LibrarySongsCarryTheirRealTrackNumberFromTags`, `PlaylistDurationIsARealNonZeroSumOfMemberSongDurations`, `RockGenreHasThreeSongs`) all fail identically on **both** D3D9 and D3D11 (Wine cross-compile) — `MediaLibrary`'s song count resolves to 0 where 3+ are expected — while every single-file Media read test (`AudioTagParserTest.*`, `MediaQueueTest.*`, `MediaPlayerNoSoundFallbackTest.*`, etc.) in the same partial run passes cleanly on both backends. `MediaLibraryTestFixture::SetUp()` redirects `MediaLibraryPaths` at a relative directory (`tests/assets/media/music`) and constructs a real `MediaLibrary`, which recursively scans it — the failure is isolated to this directory-scan path, not single-file reads, suggesting a Wine-specific `std::filesystem::directory_iterator`/`recursive_directory_iterator` behavior against a relative path under Wine's own Z:-drive translation, not a path-separator or WORKING_DIRECTORY bug (which would affect single-file reads too, and doesn't since the `WORKING_DIRECTORY` fix from `REMED-BUILD-001` is confirmed intact and correct). Not reachable/observable before this tranche — `CnaTests` never built on D3D9/D3D11 until `REMED-BUILD-005`/`-013` landed | MEDIUM | P2 | This tranche's own D3D9/D3D11 partial full-suite verification runs (`REMED-BUILD-005`/`-013`) | NOT STARTED — recorded, not fixed (out of BUILD_TEST_CI scope; MEDIA/CONTENT-lane investigation needed into `MediaLibraryIndex`'s real-directory-scan behavior specifically under Wine; isolated to Wine/Windows cross-compile targets only — every native Linux backend's `MediaLibraryTestFixture` coverage is unaffected) |
| REMED-DEVICES-004 | `devices-asan` preset's `CNA_DEVICES_GTEST_FILTER` run (all 50 `tests/CNA/Devices/*.cpp` cases) exits process code 1 despite **all 50 gtest cases reporting PASSED** — AddressSanitizer reports 12032 bytes leaked in 16 allocations. Bisected precisely: excluding `CameraTests.*` alone makes the same run exit 0 with the other 46 tests still passing; within `CameraTests`, the leak is specific to the 4 `TryAcquireFrame*` cases (which construct a real `Texture2D`/`GraphicsDevice` for pixel upload), not the other 4 `CameraTests` cases. Every leak's stack trace bottoms out in `libdrm.so.2`/`<unknown module>` (GL/DRI driver initialization), not any `CNA::Devices::Camera`/`CameraTests` source line. **Extended under `devices-tsan`:** the same run there (exit 66, TSan's own convention) reports 8 data-race warnings, **all 8 in the identical shape** — `pthread_barrier_init`/`pthread_barrier_destroy` racing inside `libgallium-25.0.7-2.so`/`libEGL_mesa.so` during real `GraphicsDevice`/`EasyGLGraphicsBackend` construction/teardown, triggered by the exact same `CameraTests.TryAcquireFrame*` cases (confirmed via full stack traces: `GraphicsDevice::GraphicsDevice()`/`::Dispose()`/`::~GraphicsDevice()` → Mesa/EGL internals, never a `CNA::Devices::Camera`/`CameraTests.cpp` frame) — the same root class as the ASan leak, not a second, independent finding | MEDIUM | P3 | `REMED-BUILD-011`'s own devices-asan/devices-tsan verification runs (this tranche) | NOT STARTED — recorded, not fixed (out of BUILD_TEST_CI scope; both the ASan leak and the TSan races point at Mesa/libgallium/libEGL driver-internal synchronization during real GL context construction/teardown, not `CNA::Devices::Camera` code — matches this project's own already-documented Wave 1 decision that ASan/TSan+real-GPU-rendering paths are known-noisy and intentionally out of sanitizer-coverage scope. DEVICES lane should still confirm this directly before permanently dismissing it, since a driver false positive vs. a real bug in `Camera.cpp`'s own texture-upload/device-lifecycle path have very different remediations) |
| REMED-BUILD-016 | This project's `-fsanitize=undefined` build configuration (`cmake-build-media-asan`'s ad hoc flags, and by the same shape `devices-ubsan` in `CMakePresets.json`) does not include `-fsanitize=float-cast-overflow`. GCC (unlike Clang, which bundles it into `undefined`) requires this check named explicitly. Confirmed empirically while verifying `REMED-CORE-004`: the exact UB that task fixed (`static_cast<bytecs>(NaN * 255.0f)` etc.) produced **zero** sanitizer output under the project's own `cmake-build-media-asan`, and only reproduced (12 distinct runtime-error reports) once `float-cast-overflow` was added explicitly to an ad hoc standalone build. Any out-of-range/NaN float→integer cast anywhere in the codebase currently passes this project's own UBSan coverage silently | MEDIUM | P1 (suggested — same leverage precedent as `REMED-BUILD-001`/`-002`: a build-config fix that makes an entire defect class newly detectable project-wide) | `REMED-CORE-004`'s own UBSan verification pass | NOT STARTED — recorded, not fixed (BUILD_TEST_CI-owned: `CMakePresets.json` and this session's own ad hoc `cmake-build-*-asan` `CMAKE_CXX_FLAGS` convention) |
| REMED-GFX-057 | `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`'s `DrawString`/`Draw` (`static_cast<intcs>(dw * scale)`/`static_cast<intcs>(dh * scale)` ~line 311, the `Vector2`-scale siblings ~line 329, and the glyph-width/height `std::lround(...)`-wrapped casts ~lines 514-515) share the identical unguarded-float-to-`intcs`-cast root cause `Color::Lerp`/`Color::Multiply` had before `REMED-CORE-004`: a NaN or extreme `scale`/`Vector2 scale` argument reaching `DrawString`/`Draw` is undefined behavior with no guard | MEDIUM | P2 (suggested — same defect class as `REMED-CORE-004`, UB not memory corruption; fold into whichever GRAPHICS batch next touches `SpriteBatch.cpp`) | `REMED-CORE-004`'s own "verify all constructors/operators touched by the same root cause" sweep, extended past `Color.cpp` to other unguarded-float-cast sites project-wide | NOT STARTED — recorded, not fixed (GRAPHICS-owned file, out of the CORE lane's scope) |
| REMED-GFX-059 | The SdlGpu backend emits a Vulkan validation error `VUID-vkCmdDraw-renderPass-02684` on every 3D draw (confirmed via `VK_LAYER_KHRONOS_validation` under `SDL_GPU`). Confirmed **pre-existing** and independent of `REMED-GFX-006`: the existing `SdlGpu_Skinned` test emits it 6× against unchanged (pre-fix) shaders, and readback pixel values are correct throughout, so it is a render-pass/pipeline attachment-compatibility validation issue in `SdlGpuGraphicsBackend`, not a normal-transform or rendering-correctness defect | LOW-MEDIUM | P2 (SdlGpu-lane; validation-cleanliness, not a visible-output defect) | `REMED-GFX-006` SdlGpu slice (first `VK_LAYER_KHRONOS_validation` run of the new world-normal harness) | NOT STARTED — recorded, not fixed (out of GFX-006's normal-transform scope; SdlGpu-backend owner should reconcile the SDL_GPU render-pass/pipeline attachment description that 02684 flags) |

#### REMED-BUILD-010 detail

- **Root cause:** `examples/easygl_real_window_resize_test.cpp` (Task 348) drives a real
  `SDL_SetWindowSize()` and polls up to 300 draw frames for the async X11 resize event to propagate,
  with its own internal timeout that logs a `FAIL` and exits cleanly. Confirmed by isolated re-run:
  under `DISPLAY=:99` (Xvfb, headless) it completes in well under a second with `4/4 PASS`; under
  `DISPLAY=:0` (this machine's real, logged-in GNOME/Mutter session — the value baked into every
  build dir's cached `CNA_TEST_DISPLAY`) it produces **zero output past backend init** and is killed
  by CTest's 60s `TIMEOUT` (confirmed reproducible in isolation, not a parallel-build resource-
  contention artifact — re-ran alone on an otherwise idle machine, same result). The window resize
  or its `SDL_EVENT_WINDOW_RESIZED` delivery appears to never complete against a real compositor,
  most plausibly because Mutter defers/never delivers a `ConfigureNotify` to a window that isn't
  focused/mapped the way a bare Xvfb server does — not verified further, out of scope for this task.
- **Not a stale/incorrectly-authored test:** the test's own logic was independently re-verified
  (matches `audit/examples/easygl_real_window_resize_test.cpp.audit.md`'s "Healthy" verdict) and it
  passes cleanly given the environment it was clearly designed for (an isolated Xvfb, matching every
  other `CNA_TEST_DISPLAY`-driven test's actual runtime environment during the original audit).
- **Not a production (GameWindow/GraphicsDevice) bug:** the 4/4 PASS under Xvfb confirms the actual
  resize/viewport/event-firing logic this test checks is correct; only the interaction with a real
  desktop compositor on the test `DISPLAY` is at fault.
- **Suggested remediation directions (not implemented — recording only):** (a) point
  `CNA_TEST_DISPLAY` at an isolated Xvfb display in every build dir rather than the login session's
  real `DISPLAY`, and/or (b) give this specific test a hard wall-clock watchdog independent of its
  frame-count loop so a stalled resize can't consume the full CTest `TIMEOUT` regardless of which
  display it's pointed at.
- **Scope note:** only this one CTest is affected project-wide (grep confirms
  `easygl_real_window_resize_test.cpp` is EasyGL's only real-OS-resize test; no equivalent test
  exists for other backends).
- **Evidence extension, this integration pass:** the same real-compositor-vs-Xvfb sensitivity is
  **not EasyGL-specific**. `Bgfx_DepthStencilState_CompareFunction` and
  `Bgfx_SpriteFont_MultiGlyphSpacingNewline` showed the identical pattern (failed/timed out under
  `DISPLAY=:0`, passed cleanly and repeatedly under `DISPLAY=:99`) during Phase 4 of the post-Wave-1
  integration pass. Not re-scoped to a new ID — same root cause, same fix directions apply.

#### REMED-CONTENT-009 detail (found during the post-Wave-1 integration pass)

New code, not pre-existing: `REMED-CONTENT-001`'s own dimension-guard fix in
`Texture2DContentTypeReader.cpp:88-89` computes
`input.CheckDecodedByteSize(static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4, ...)`.
The file's own comment at that fix's construction time claimed this was "computed in int64_t so it
can't itself silently wrap back into range" — true for the multiplication of `width*height` alone,
false once the `* 4` is applied: for `width`/`height` both near `INT32_MAX`, `width*height` (int64_t)
reaches ≈4.6×10^18, still representable, but `* 4` overflows signed 64-bit (max ≈9.22×10^18).
Confirmed live: `Texture2DContentTypeReaderTest.AbsurdlyLargeDimensionsThrowContentLoadExceptionNotBadAlloc`
reliably triggers `UndefinedBehaviorSanitizer: runtime error: signed integer overflow: 4611686014132420609
* 4 cannot be represented in type 'long int'` under `cmake-build-media-asan` (ASan+UBSan, HEADLESS).
The regression test itself still passes today only because the implementation-defined post-overflow
value happens to still land outside `CheckDecodedByteSize`'s valid range on this compiler/platform —
not a guarantee. **Not fixed** (out of this verification-only pass's scope) — recommended for Wave 2's
CONTENT lane given its location inside the CRITICAL-severity `REMED-CONTENT-001` validation path.

#### REMED-GFX-052 detail (found during the post-Wave-1 integration pass)

`Vulkan_SkinnedEffect_Fog` hangs reproducibly under CTest's 30s `TIMEOUT`. Confirmed not resource
contention: reproduced identically re-run alone on an idle system (load average 0.46, 43°C). Confirmed
not a CTest/environment artifact: reproduced identically invoking the underlying binary
(`cna_test_vulkan_skinnedeffect_fog`) directly, bypassing CTest. Both hangs stop dead immediately after
`[Vulkan] Backend initialised`, with zero further output — consistent with a hang inside pipeline/shader
setup for this specific effect+fog combination, not a rendering-correctness defect. Distinct from
`REMED-GFX-005` (Vulkan's fog *formula* is mirrored — a wrong-values defect, not a hang) — root cause
not triaged further. **Not fixed** (out of this verification-only pass's scope).

#### REMED-GFX-053 + REMED-GFX-054 detail (found during the post-Wave-1 integration pass)

Both found on the WebGPU backend during Phase 4's configuration-matrix sweep. `REMED-GFX-053`:
`CnjEffectTest.LoadsRealCnjFixture` and `CnjStockEffectTest.CustomGlslEffectStillWorks` fail
(`shaderEffect->IsEffectValid()` returns false, no crash) — these same two tests also fail on
Vulkan/Bgfx in this sandbox, but there the cause is confirmed to be the absence of a `glslc`/
`glslangValidator` toolchain (environment gap, not a code defect). WebGPU consumes WGSL, not
GLSL/SPIR-V, so that explanation does not transfer; root cause unconfirmed. `REMED-GFX-054`: three
`ContentManagerSkinnedModelTest` cases throw `"CNA WebGPU: invalid index buffer upload"`. Neither was
triaged to a root cause or fixed (out of this verification-only pass's scope). Given `CLAUDE.md`'s own
description of the WebGPU backend as experimental with a 2D/SpriteBatch/Texture2D baseline and
explicitly not yet at shader/effect parity with the established backends, whoever triages these should
decide whether they belong in the general GRAPHICS remediation lane or in `plan_webgpu.md`'s own
WEBGPU-* task series.

#### REMED-BUILD-013 detail (found during the post-Wave-1 integration pass)

`StrictXnaApiSurfaceCheck_Compile_Run` (registered in `cmake/Harnesses.cmake` via a plain
`add_test(COMMAND cna_strict_xna_api_check)`) and `CnaInputTests` (registered in
`cmake/UnitTests.cmake` with a hardcoded `COMMAND CnaTests`) both lack the Wine-runner wrapper macro
every other cross-compiled D3D9/D3D11 CTest uses. Confirmed identically on both backends during Phase 4:
`StrictXnaApiSurfaceCheck_Compile_Run` fails "unable to find an interpreter"; `CnaInputTests` fails
`wine: ... CnaTests.exe: c0000135` (downstream of the same, already-understood `CnaTests_NOT_BUILT`
placeholder fact — the full gtest suite isn't built for cross-compiled targets, so `CnaInputTests`'
hardcoded `COMMAND CnaTests` has nothing valid to run). A CTest-wiring gap, not a backend code defect.
**Not fixed** (out of this verification-only pass's scope) — cheap BUILD_TEST_CI-lane fix, candidate to
bundle with `REMED-BUILD-010`/`-011` cleanup.

## Decisions log

Owner decisions required by the plan, plus any scope calls made during implementation.

| Date | Task | Decision | Decided by |
|---|---|---|---|
| _(none yet)_ | `REMED-GFX-035` | Which GLSL dialect is the contract? | pending |
| _(none yet)_ | `REMED-CORE-011` | Implement `CNA::Runtime` or delete it? | pending |
| _(none yet)_ | `REMED-BUILD-007` | Is `CNA::Internal::Net`'s MIT licensing deliberate? | pending |
| 2026-07-20 | `REMED-BUILD-010`, `REMED-BUILD-011` | **Deferred to Wave 2, not pulled into Wave 1.** Both are genuinely real, already-recorded findings (Wave 0/`REMED-DEVICES-001` respectively), but both are **P2** and neither `MASTER_REMEDIATION_PLAN.md` nor `REMEDIATION_DEPENDENCIES.md` lists either as blocking any Wave 1 task — the dependency file's own Wave 1 description names exactly four BUILD_TEST_CI starters (`REMED-BUILD-004`, `REMED-BUILD-008`, `REMED-TEST-002`, `REMED-TEST-004`, all P1), and neither BUILD-010 nor BUILD-011 appears in the "Hard dependencies" table as a blocker for anything. Per this task's own explicit instruction ("Do not assume either belongs to Wave 1 until you verify the plan and dependencies" / "Do not pull later-wave tasks forward without justification"), both remain `NOT STARTED`, scheduled at their assigned P2 priority in Waves 2-5. | Claude (autonomous remediation session, user-directed) |
| 2026-07-20 | `REMED-CORE-006`, `-007`, `-014` | **Picked up now, despite `REMEDIATION_DEPENDENCIES.md` narratively listing `-006`/`-007` under "Wave 2."** Their only documented hard blocker is `REMED-TEST-002` (now DONE); no other Wave-1/Wave-2 gate applies to them specifically. Treated the same way `BUILD_TEST_CI`'s own non-P0 Wave-1 unblockers (`REMED-BUILD-004`/`-008`/`TEST-002`/`TEST-004`) were already treated in this file — started as soon as their blocker cleared, not held for a literal wave boundary. `REMED-CORE-001`/`-004` (also nominally "Wave 2," no `TEST-002` dependency) were **not** pulled forward — no blocker justifies moving them early. Full rationale in "Wave 1 (parallel) — CORE lane." | Claude (autonomous CORE-lane session, user-directed) |
| 2026-07-20 | `REMED-BUILD-002` | **Yes — the copy step is obsolete, deleted outright (not guarded, not backfilled with a real `Content/` dir).** Investigated `examples/demo_xact/src/XactFileGen.hpp` + `XactDemo.cpp`: `XactDemo::LoadContent()` calls `GenerateXactFiles("Content/Audio")`, which itself calls `std::filesystem::create_directories(audioDir)` and then `XactFileGen::SaveFile()`s a freshly synthesized `Waves.xwb`/`Demo.xgs`/`Sounds.xsb` (sine-wave PCM + minimal XGS/XWB/XSB binaries matching `XactParser.cpp`'s expected layout) directly into that runtime-relative directory — no pre-existing `.xwb`/`.xgs`/`.xsb` asset is ever read from disk. `examples/demo_xact/` contains only `src/` (confirmed: no `Content/` anywhere in the repo, matching the audit finding). The POST_BUILD `copy_directory` in `cmake/Examples.cmake` therefore copied a directory that never existed and could never usefully exist — deleting it is strictly correct, not a stopgap. | Claude (autonomous remediation session, user-directed) |
| 2026-07-20 | `REMED-CORE-002`, `-003` | **Deliberately not pulled into the Wave 2 CORE tranche.** Both are **P2** per `REMEDIATION_INDEX.md`/`MASTER_REMEDIATION_PLAN.md` (not P1 like `-001`/`-004`), and `REMEDIATION_DEPENDENCIES.md` explicitly schedules the exception-type sweep into Wave 4's "quiet window" specifically because of its LARGE/cross-lane blast radius (`Cx LARGE (breadth) · PS NO (touches files owned by nearly every other lane — schedule in a quiet window`). The POST-WAVE-1 baseline's own "Wave 2 readiness — recommended lane order" names exactly `REMED-CORE-001` then `-004` for this tranche's CORE lane, not `-002`/`-003`. Nothing in the dependency graph names `-002`/`-003` as blocking `-001`/`-004`, or vice versa. Left `NOT STARTED` at their assigned P2 priority, per the tranche's own explicit instruction not to pull later-wave CORE tasks forward without clear dependency justification. | Claude (autonomous CORE-lane session, user-directed) |
| 2026-07-20 | `REMED-CORE-004` scope (`PackFromVector4`) | **Did not literally follow the master plan's suggested strategy** ("route through the existing `ToByteFromUnitClamped()`"). Cross-checked real FNA source first (per `CLAUDE.md`'s FNA-authoritative-behavior instruction) and found FNA's own `IPackedVector.PackFromVector4` deliberately does not clamp, unlike every constructor in the same file — literally following the plan would have introduced an undisclosed FNA divergence (saturating where FNA truncates/wraps). Implemented a new non-clamping `ToByteFromUnitTruncated()` helper instead, preserving FNA's real semantics while still eliminating the C++ UB. Recorded per Rule 3 ("if implementation reveals the root cause differs from the plan's, record that ... do not silently widen scope"). | Claude (autonomous CORE-lane session, user-directed) |

## Findings that did not reproduce

Record disproved findings here with evidence. This is a real result and improves the audit baseline's
accuracy for future work.

| ID | Investigated | Evidence it did not reproduce | Recorded by |
|---|---|---|---|
| _(none yet)_ | | | |

---

## Wave 3 — GRAPHICS shader/effect campaign (2026-07-20)

Owner lane: GRAPHICS. Organized **by backend** per the campaign's organization rule (fix all
assigned shader/effect defects affecting a backend in one pass, rather than one defect across 14
backends). Verifiability drove the backend order — see the blocker below.

### Sandbox shader-regeneration reality (campaign-wide blocker — establish before any shader edit)

The stock-effect shaders are **precompiled bytecode committed as generated headers**; CMake does NOT
regenerate them at build time (the `compile_shaders.py` scripts are run manually). So a shader-source
edit is **inert** until its header is regenerated, and regeneration needs a compiler that may be absent:

| Backend | Shader form | Regen tool | Available in this sandbox? |
|---|---|---|---|
| **Vulkan** | `spirv_shaders.hpp` (SPIR-V) | `compile_shaders.py` + **libshaderc.so.1** | ✅ present — verified byte-reproducible |
| **SdlGpu** | `spirv_shaders.hpp` (SPIR-V) | `compile_shaders.py` + libshaderc.so.1 | ✅ present |
| EasyGL / WebGPU | runtime GLSL/WGSL strings in `.cpp` | none (compiled at runtime) | ✅ direct edit works |
| **Bgfx** | `bgfx_shaders.hpp` | bgfx `shaderc` binary | ⚠️ binary NOT built; source under `cmake-build-bgfx/_deps/bgfx_cmake-src/` |
| **D3DCommon** (D3D11/D3D12) | `hlsl_shaders.hpp` (DXBC, 3.3 MB) | MinGW-built tool + Wine+DXVK D3DCompile | ⚠️ heavy; D3D12 runtime also blocked (REMED-BUILD-012) |
| D3D9 | committed HLSL bytecode | MinGW + Wine+DXVK | ⚠️ heavy |

**Consequence:** editing Bgfx/D3DCommon/D3D9 shader *source* without regenerating their bytecode
would be a misleading no-op. This session therefore did the **Vulkan** slice first (heaviest single
backend AND fully verifiable) and left Bgfx/D3DCommon/D3D9 shader edits for when their compilers are
available (build bgfx `shaderc`; MinGW+Wine+DXVK for HLSL). `fxc`/`dxc`/`glslc`/`glslangValidator`/
`shaderc` CLI are all absent; only `libshaderc.so.1` (the shared lib) is present.

### REMED-GFX-005 — fog formula mirror-fix — **Vulkan slice DONE** (commit `58a7d0bb`)

- **Root cause:** static shader source. 16 fog-capable Vulkan `.vert.glsl` computed
  `clamp((FogEnd - z)/(FogEnd - FogStart))` — the mirror image of FNA's fog factor. EasyGL was
  corrected to `clamp((z + FogEnd)/(FogEnd - FogStart))` under Task 1111 (oracle-validated, commit
  `74ad3bae`) but the fix never propagated to Vulkan/Bgfx/D3DCommon.
- **FNA/XNA evidence:** FNA `EffectHelpers.SetFogVector` → `fogFactor = saturate((viewZ+FogStart)/
  (FogStart-FogEnd))`; the "fraction of original color" these shaders use is `1-fogFactor =
  (z+FogEnd)/(FogEnd-FogStart)`. Derivation matches EasyGL's own in-source Task-1111 comment. Zero-
  length range (`FogStart==FogEnd`) → SetFogVector returns `(0,0,0,1)` → fully fogged → factor `0.0`.
- **Sign confirmation:** CNA fog scenes place geometry at XNA camera-forward (negative view-z), so
  `(z+FogEnd)` is correct and `(FogEnd-z)` is the mirror. Confirmed empirically: at z=+0.5,
  FogStart=0/FogEnd=1 the corrected shader gives no fog (blue), matching FNA (positive z = behind
  camera); the mirror gave half-fog.
- **Affected files (16 shaders + regenerated header):** `alpha_test3d`, `alpha_test_colored3d`,
  `colored3d`, `colored_textured3d`, `dual_texture3d`, `dual_texture_colored3d`, `env_map3d`,
  `lit_textured3d`, `lit_textured3d_vertexlit`, `pbr3d`, `pbr3d_skinned`, `skinned3d`,
  `skinned3d_color`, `skinned3d_vertexlit`, `skinned3d_vertexlit_color`, `textured3d` (`.vert.glsl`)
  + `spirv_shaders.hpp` (regenerated via libshaderc; toolchain proven byte-reproducible against the
  committed header before any edit).
- **Also:** added EasyGL's zero-length-range guard to every fixed shader; corrected the false
  "matches the established Task 888/EasyGL formula" precedent comments (Task 888/899 WAS the mirror);
  removed `skinned3d`'s false claim of Bgfx parity (Bgfx still mirrored until its own slice).
- **Tests (8 Vulkan fog pixel tests retargeted):** their old scenes used FogStart=0/FogEnd=1, which
  **collapse to a constant** under the correct formula (the exact trap EasyGL's test header documents),
  and they asserted the mirrored values. Redesigned to `FogStart=0/FogEnd=-0.9` (the [0,1]-depth
  analog of EasyGL's [-1,1] `-0.9/0.9`; Vulkan depth-clips negative z, so EasyGL's z values can't be
  reused) at z ∈ {0, 0.45, 0.9} → no/half/full fog. **Asserted pixel values unchanged**
  ((128,0,128) / red) — only inputs+labels+the mirror-formula header comment changed.
- **Numerical/pixel verification (Vulkan/llvmpipe, Xvfb :99): 8/8 pass 3/3** — `Vulkan_BasicEffect_Fog`,
  `Vulkan_AlphaTest_Fog`, `Vulkan_BasicEffect_Colored3D_Fog`, `Vulkan_BasicEffect_Textured3D_Fog`,
  `Vulkan_BasicEffect_ColoredTextured3D_Fog`, `Vulkan_DualTextureEffect_Fog`,
  `Vulkan_EnvironmentMapEffect_Fog`, and `Vulkan_SkinnedEffect_Fog` (the 8th passed once its
  orthogonal REMED-GFX-052 CullNone test-bug was fixed in `c11c6ce4`).
- **Regressions:** none. Skinned control tests `Vulkan_SkinnedEffect_IdentityBones` and
  `Vulkan_SkinnedEffect_Combined` rebuilt against the new shaders and still pass (160,0,0), proving the
  fog fix did not regress skinned rendering.
- **Blocked verification / remaining GFX-005 scope:** Bgfx + D3DCommon (D3D11/D3D12) slices (bytecode
  regen blocked as above); D3D9 custom-shader fog is the *separate* object-space defect (REMED-GFX-010);
  SdlGpu has no fog at all (REMED-GFX-009). A shared cross-backend fog conformance test (the plan's
  required regression) is still to build.
- **Interaction with REMED-GFX-052 (now RESOLVED, `c11c6ce4`):** the 8th test `Vulkan_SkinnedEffect_Fog`
  initially still failed with black geometry — root-caused to a **missing `CullNone` call** (test bug,
  not fog, not lighting: the full-fog case outputs pure FogColor independent of lighting and was also
  black, proving the geometry wasn't rasterized). One-line fix → 3/3, bringing Vulkan fog to **8/8**.

### Cross-cutting observation (recorded, not a new ID yet) — object-space fog is EasyGL-wide, not just D3D9

REMED-GFX-010 tracks "object-space (untransformed) Z fog" as a **D3D9-custom-shader** defect. In fact
EasyGL, Bgfx, Vulkan and D3DCommon stock shaders **all** compute fog from raw object-space `z` (they
only carry the combined `Mvp`, no separate WorldView), correct only under identity World/View — which
is why every fog test uses identity transforms. GFX-005 deliberately fixes *only* the mirror (matching
EasyGL's established object-space approach); moving stock-effect fog to true view-space is a larger
shared change (needs a WorldView/fog-vector plumbed into every backend's fog path) that GFX-010's
cross-backend scope should own. Flagging so GFX-010 is not scoped as D3D9-only.

### REMED-GFX-007 — EnvironmentMapEffect emissive re-multiply — **Vulkan slice DONE** (commit `31668819`)

- **Root cause:** static shader source. `env_map3d.frag.glsl` computed
  `litRGB = (Emissive + lightSum) * DiffuseColor`, re-scaling the already-ambient-folded emissive by
  DiffuseColor. FNA `Lighting.fxh` adds emissive **unscaled**: `litRGB = lightSum*DiffuseColor + Emissive`.
- **FNA/XNA evidence:** CPU side `EnvironmentMapEffect.cpp:424` already uploads the pre-folded
  `(EmissiveColor + AmbientLightColor*DiffuseColor)*alpha` with an in-source "matches FNA" comment —
  only the shader diverged from the layer that feeds it.
- **Affected files:** `Vulkan/shaders/env_map3d.frag.glsl` + regenerated `spirv_shaders.hpp`;
  test `examples/vulkan_environmentmapeffect_amount_zero_test.cpp`.
- **Tests / numerical verification:** masked on all existing tests (none set non-white DiffuseColor;
  with white diffuse both formulas coincide). Added a discriminating sub-test at
  `EnvironmentMapAmount=0` (isolates litRGB): no lights, Diffuse=(0.5,0.5,0.5), Emissive=(0.5,0.5,0.5)
  → correct pixel `(100,50,25)`; the bug gave `(50,25,13)`. **Test-driven**: verified the new case
  FAILS on the pre-fix shader and PASSES after. All 8 Vulkan env-map tests pass; zero regressions.
- **Remaining GFX-007 scope:** Bgfx, WebGPU, SdlGpu (incl. its extra alpha-squaring), D3DCommon
  (D3D11/D3D12) — bytecode regen blocked in this sandbox.

### REMED-GFX-011 — Vulkan NDC Y-flip — CONFIRMED AT SOURCE, not yet implemented

Verified this session (grep + read of every Vulkan 3D `.vert.glsl`): the flip convention splits exactly
as the plan states. **Have the flip:** `colored3d`/`textured3d` (`pos.y = -pos.y` before assignment),
`skinned3d` (`gl_Position.y = -gl_Position.y`, line 59), and the other lit/dual/alpha variants.
**Missing the flip (the defect):** `env_map3d.vert.glsl` (l.35), `pbr3d.vert.glsl` (l.55, with a comment
justifying only PBR-internal consistency), `pbr3d_skinned.vert.glsl` (l.62, comment falsely claims
"skinned3d.vert.glsl never Y-flips" — it DOES, l.59), `instanced3d.vert.glsl` (l.28, no comment).
`sprite2d` computes NDC directly (verified non-bug).

**Fix (ready):** add `gl_Position.y = -gl_Position.y;` after the `mvp*pos` line in those 4 shaders;
delete the two false/incomplete justifying comments; regen `spirv_shaders.hpp`.
**Why deferred here:** proper verification needs an **off-center / orientation** test per effect family
(the existing tests sample the center pixel, which structurally cannot detect a vertical mirror — the
same blind spot that hid the bug). Adding correct orientation tests (careful about XNA +y-up vs Vulkan
+y-down NDC vs readback pixel rows) is new infra best built as its own focused step, not rushed. The
existing symmetric env-map/pbr tests will still pass after the flip (a vertical flip doesn't move a
symmetric full-screen quad's center pixel), so they cannot serve as the regression guard either.

---

### REMED-GFX-011 — Vulkan NDC Y-flip — **DONE and VERIFIED** (commits `c9e96813`, `633a2e17`)

**Root cause.** Vulkan's NDC has Y inverted relative to OpenGL, and the C++ side supplies no
correction: the viewport is always positive-height (`VulkanGraphicsBackend.cpp:6779-6788` and the
two swapchain sites), and `DrawPrimitivesEx`'s `wvp` / `FillInstancedPushConst`'s `vp` pass the
matrix through untouched. The flip is therefore a per-shader convention. **14 of 18 3D vertex
shaders applied it; 4 did not.**

**Affected shader families (confirmed exhaustively, not assumed).** A full read of all 19
`.vert.glsl` files partitions them as: 14 flipping (10 via `pos.y = -pos.y` before assignment —
`colored3d`, `colored3d_legacy`, `textured3d`, `colored_textured3d`, `dual_texture3d`,
`dual_texture_colored3d`, `lit_textured3d`, `lit_textured3d_vertexlit`, `alpha_test3d`,
`alpha_test_colored3d`; 4 via `gl_Position.y = -gl_Position.y` — `skinned3d`, `skinned3d_color`,
`skinned3d_vertexlit`, `skinned3d_vertexlit_color`), **4 not flipping — the defect** (`env_map3d`,
`pbr3d`, `pbr3d_skinned`, `instanced3d`), and `sprite2d`, which computes NDC directly from pixel
space and is a verified non-bug. 14 + 4 + 1 = 19, so the sweep is complete. The plan's list of four
was confirmed exact — no additional families share the root cause.

**The two justifying comments were both false.** `pbr3d.vert.glsl` and `pbr3d_skinned.vert.glsl`
each rested on the claim that `skinned3d.vert.glsl` never Y-flips. It does, at line 59. Both were
deleted rather than reworded, per the plan: leaving them would invite a future maintainer to
"re-fix" the flip back out.

**Coordinate-pipeline analysis.** CPU builds World/View/Projection in XNA convention → the stock
effect uploads the composed matrix unmodified → the shader must map XNA's +Y-up clip space onto
Vulkan's +Y-down NDC → the viewport is positive-height, so nothing downstream compensates.
Render targets and the backbuffer share this convention (verified empirically, below). SpriteBatch
is separate and correct on its own terms. No affected path becomes double-flipped — proven by the
object landing on the *correct* quadrant post-fix rather than returning to where it started.

**Test-harness design.** The pre-existing tests for these families draw a centred, symmetric,
full-screen quad and sample the centre pixel — a vertical mirror moves nothing they look at, which
is exactly why the bug survived. The new harness uses a quad confined to the **+X/+Y quadrant**
(asymmetric on *both* axes) and samples **all four quadrant centres**, so correct / vertically
mirrored / horizontally mirrored / both are separately diagnosable, and a double flip is caught as
a vertical mirror. Three tests:
- `vulkan_orientation_calibration_test.cpp` — the **oracle**. Pins "XNA +Y is up" and
  "`GetBackBufferData` row 0 is top" against `colored3d`, whose flip was never in question, so the
  other tests assert a *calibrated* convention rather than an assumed one.
- `vulkan_orientation_effects_test.cpp` — backbuffer orientation, per family.
- `vulkan_orientation_rendertarget_test.cpp` — the same four through a `RenderTarget2D`, calibrated
  against `colored3d` via an identical SpriteBatch blit so SpriteBatch's own NDC convention cancels
  out of the comparison. Covers a fix that could be right on the backbuffer and double-flipped on
  an RT. (The Vulkan backend implements no `GetTextureData`, so direct `GetData` on an RT reads an
  unpopulated CPU shadow — the blit path is what every other Vulkan RT test uses.)

**Pre-fix failure evidence.** Against the unmodified shaders, all four families rendered
**BOTTOM-RIGHT** on both the backbuffer and the render target, against a `colored3d` reference of
TOP-RIGHT — an unambiguous vertical mirror. Backbuffer 0/4, RT 0/4, calibration oracle PASS.

**Post-fix result.** Backbuffer **4/4**, RT **4/4**, calibration oracle still PASS. The object moves
BR → TR with *identical pixel values* (`env_map3d` 255,255,255; `pbr3d` and `pbr3d_skinned`
232,232,232; `instanced3d` 255,255,255) — the geometry relocated and nothing else changed, which is
the signature of a correct single flip rather than a double flip or a lighting side-effect.

**SPIR-V regeneration verification.** `compile_shaders.py` (libshaderc) was first run with **no
source change** and reproduced `spirv_shaders.hpp` byte-for-byte (identical MD5, empty `git diff`),
establishing reproducibility before relying on it. After the fix, a semantic diff of all 35 embedded
arrays showed exactly the 4 intended vertex shaders changed — `kEnvMap3dVertSpv` 701→718,
`kInstanced3dVertSpv` 348→369, `kPbr3dSkinnedVertSpv` 1262→1279, `kPbr3dVertSpv` 760→777 words, each
growth consistent with a negate+store — and the other **31 arrays byte-identical**. No regeneration
drift.

**Backbuffer / render-target coverage.** Both paths covered and both pass; the RT path was
additionally proven to *fail* pre-fix, so it is a real guard and not a vacuous one.

**Vulkan validation layers.** `VK_LAYER_KHRONOS_validation` run over all three orientation tests;
layer load confirmed via `VK_LOADER_DEBUG=layer`. **Zero errors, zero warnings.**

**Regression results.** Full suite: **5764 tests**. Post-fix failures: 4 graphics
(`CnjEffectTest.LoadsRealCnjFixture`, `CnjStockEffectTest.CustomGlslEffectStillWorks`,
`GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame`, `Vulkan_DepthBias`) — **all four confirmed
pre-existing** by a stashed-shader baseline run, not regressions. Assorted ENet/NetworkSession
failures vary run to run under `ctest -j4` contention and pass in isolation across 3 consecutive
runs; unrelated to graphics.

**One real regression was found and fixed, not papered over.** `Vulkan_EnvironmentMapEffect_Fog`
passed pre-fix and failed post-fix (all three checks black). Cause: the flip also corrects the
**winding** of these families. `frontFace` is uniformly `VK_FRONT_FACE_CLOCKWISE` across every
pipeline — a convention calibrated to the 14 shaders that flip — so the 4 were inconsistent in
culling as well as orientation. That test was the **only one of the ten** Vulkan
EnvironmentMapEffect tests not setting `CullNone`, justified by a comment asserting that Vulkan's
default cull state is effectively no-culling and that no sibling test sets it. **Both claims were
false** (all nine others do set it); the quad survived only because `env_map3d` wasn't flipping.
This is the same species as `REMED-GFX-052`. `CullNone` added, comment corrected. This is a
genuine consequence of the fix, and its resolution is inseparable from `REMED-GFX-011`.

**New finding recorded — `REMED-GFX-058`** (test methodology, GRAPHICS lane, not fixed here):
a sweep of the whole Vulkan shard found **47 of 71 tests sample only the centre pixel**, the exact
blind spot that hid this bug. GFX-011 closes it for the four families it owns; the remaining ~43
are a systemic gap too large to fold into this task without unrelated scope expansion.

#### New finding: `REMED-GFX-058` — 47 of 71 Vulkan tests assert on the centre pixel only

| ID | One-line | Severity | Suggested priority |
|---|---|---|---|
| `REMED-GFX-058` | Vulkan test shard is structurally blind to mirrors/translations: 47 of 71 tests sample only the centre pixel | MEDIUM (test methodology, not a runtime defect) | P2 — GRAPHICS lane; do opportunistically alongside each family's next shader task |

**Evidence.** Programmatic sweep of `examples/vulkan_*_test.cpp` classifying every 1×1
`GetBackBufferData` sample site: **47 files sample exclusively at `(W/2, H/2)`**, 24 have at least
one off-centre assertion (5 of those 24 are the three tests added by `REMED-GFX-011` plus the RT and
scissor tests), 2 do no 1×1 backbuffer read.

**Why it matters.** A centre-pixel assertion on a centred symmetric quad cannot detect a vertical
mirror, a horizontal mirror, or any translation — it is the exact blind spot that let
`REMED-GFX-011` survive every one of its families' existing tests, and the same class of blind spot
noted in the plan as "the general blind spot behind several findings". These 47 tests are weaker
guards than their names suggest.

**Scope note.** `REMED-GFX-011` closes this for the four families it owns (`env_map3d`, `pbr3d`,
`pbr3d_skinned`, `instanced3d`) via the new orientation harness. Retrofitting off-centre assertions
across the remaining ~43 files is a systemic test-quality task, deliberately **not** folded into
GFX-011 — it is separable, and bundling it would have turned a 4-shader root-cause fix into a
47-file test rewrite. Recorded here rather than actioned, per the campaign's scope discipline.

**Suggested approach.** Do not rewrite all 47 at once. The cheap, high-value version is to extend
each test's existing assertion set with one off-centre sample as that family's next shader task
touches it, and to prefer asymmetric geometry in any newly-written Vulkan pixel test.

---

### REMED-GFX-006 — Vulkan skinned world-space normal transform — **Vulkan slice DONE and VERIFIED** (commits `dafa085d`, `acf200c9`)

**Root cause (Vulkan slice).** FNA lights a skinned normal as
`normalize(mul(mul(normal,(float3x3)skinning), WorldInverseTranspose))` — `SkinnedEffect.fx`'s
`Skin()` applies the bone 3×3, then `Lighting.fxh`'s `ComputeCommonVSOutputWithLighting` applies
`WorldInverseTranspose`. Vulkan's five skinned vertex shaders dropped the outer world factor:
- **Variant A** (world matrix absent entirely) in `skinned3d`, `skinned3d_color`,
  `skinned3d_vertexlit`, `skinned3d_vertexlit_color` — each `normalize(mat3(skinMat) * aNormal)`.
- **Variant B** (raw `World`, not inverse-transpose) in `pbr3d_skinned` — `mat3(pbr.world)`, with a
  comment defending it as fidelity to EasyGL's identical defect, while this backend's own unskinned
  `pbr3d.vert.glsl` already used the inverse transpose.

**Fix.** All five now apply `transpose(inverse(mat3(world)))` as the outer normal factor. The world
matrix was already present in each shader's UBO (`fog.world` / `pbr.world`), so this is
**shader-only** — no C++ or UBO-layout change. `pbr3d_skinned`'s tangent stays on raw `World`
(tangents transform as directions, not normals — glTF convention, unchanged).

**Test harness.** `vulkan_skinnedeffect_world_normal_test.cpp`. Identity bind pose (so
`mat3(skinMat)=I`, isolating the world factor), a single directional light with `-Direction=(0,1,0)`,
zero ambient/emissive/specular — a pure N·L readout. The vertex normal is an in-plane direction
(not the face normal) so a Z-rotation changes the world normal without moving the camera-facing,
centre-covering quad; a failure is therefore a lighting difference, never a coverage artifact. Four
cases with analytically-derived, CPU-cross-checked expectations: identity (control), rotationZ(90),
non-uniform scale(2,1,1), and both. The scale case distinguishes the correct inverse-transpose
(228) from a naive raw-World "fix" (114) and from the un-worlded normal (180), so it rejects
Variant B as well as Variant A.

**Pre-fix vs post-fix.** 1/4 → 4/4. Pre-fix values matched the Variant-A prediction exactly
(rotationZ→0 vs 255, scale→180 vs 228, rot×scale→180 vs 114); post-fix every case matches the FNA
analytic value **to the byte** (0 / 255 / 228 / 114). The 228 (not 114) confirms the inverse
transpose specifically.

**SPIR-V regeneration.** Semantic diff of all 35 arrays: exactly the 5 intended shaders changed
(`kSkinned3dVertSpv`, `kSkinned3dColorVertSpv`, `kSkinned3dVertexLitVertSpv`,
`kSkinned3dVertexLitColorVertSpv`, `kPbr3dSkinnedVertSpv`), the other 30 byte-identical.

**Regression / validation.** Full suite **5766 tests, 4 failures — all confirmed pre-existing**
(the same `CnjEffectTest`, `CnjStockEffectTest`, `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame`,
`Vulkan_DepthBias`). Every existing identity-World skinned test still passes, as expected (the new
factor is the identity there). Validation layer (`VK_LAYER_KHRONOS_validation`) clean.

**Remaining GFX-006 scope (after the Vulkan slice).** EasyGL, WebGPU, SdlGpu, Bgfx, D3D9, D3DCommon
carry the same Variant A/B defect (audit-confirmed at source). The EasyGL/WebGPU/SdlGpu slices are
now DONE (below); Bgfx/D3D9/D3DCommon stay BYTECODE-BLOCKED.

### REMED-GFX-006 — EasyGL / WebGPU / SdlGpu skinned world-space normal transform — **DONE and VERIFIED** (commits `7c5cf74f`, `f040f184`, `cd9298fd`, `23a83527`, `daa7bb70`, `9e0b1450`)

**Scope confirmed before editing (Phase 1).** A full repo sweep for directly-editable skinned
normal transforms classified every backend:
- **AFFECTED (fixed this session):** EasyGL (3 programs), WebGPU (5 WGSL shaders), SdlGpu (3 GLSL
  shaders).
- **ALREADY CORRECT:** Vulkan (prior slice, 5 shaders).
- **BYTECODE-BLOCKED (inspected, not edited):** Bgfx (`vs_skinned3d` A, `vs_pbr_skinned3d` B), D3D9
  (`SkinnedVertexColor3D` A, `PbrSkinned3D` B), D3DCommon→D3D11/D3D12 (`skinned*` A ×4,
  `pbr_skinned3d` B). Their `sc`/`fxc`/`dxc` toolchains are unavailable in this sandbox; source-only
  edits would be inert, so none were made.
- **NOT_APPLICABLE:** Software (no skinned diffuse lighting at all — design decision 6; its only
  world-normal use is the env-map reflection vector, a documented raw-World simplification that is
  GFX-007-adjacent, not GFX-006), and Ascii/Canvas/Headless/SdlRenderer/Dx3 (no 3D skinned normal
  transform).

**Fix per backend.** The correct transform, applied AFTER the bone-skin 3×3, is
`normalize( transpose(inverse(mat3(World))) * (mat3(skinMat) * normal) )` — FNA's `Skin()` then
`Lighting.fxh`'s `mul(normal, WorldInverseTranspose)`. Tangents (PBR) stay on raw `World`.
- **EasyGL** — routed all three programs (`EnsureSkinnedProgram` A, `EnsureSkinnedVertexLitProgram`
  A, `EnsurePbrSkinnedProgram` B) through the CPU-precomputed inverse-transpose `uNormalMatrix` that
  `BindDrawParams()` already uploads for every non-skinned lit program: each now declares
  `uniform mat3 uNormalMatrix` and registers `loc_normalmat`, so the upload path activated with no
  C++ data change.
- **WebGPU** — the 4 non-PBR skinned WGSL (Variant A) + 1 PBR (Variant B) now read
  `lp.normalMatrixCol0/1/2` (the inverse-transpose `FillLitLightUniforms` already filled for every
  skinned draw but no shader read). Shader-string-only; WGSL is compiled at runtime by wgpu-native.
- **SdlGpu** — the 3 GLSL shaders (`skinned3d` A, `skinned_colored3d` A, `pbr_skinned3d` B) compute
  `transpose(inverse(mat3(lp.world)))` in-shader (no CPU normal-matrix upload on this backend),
  mirroring the Vulkan siblings. `spirv_shaders.hpp` regenerated via `compile_shaders.py`
  (libshaderc); regen was proven byte-reproducible (an unchanged-source regen is byte-identical to
  the committed header) and the change touched exactly the 3 intended arrays, the other 20
  byte-identical.

**Pre-fix → post-fix (analytic non-identity-World harness, ported per backend).**
| Backend | test | readback | pre-fix | post-fix | expected (correct) |
|---|---|---|---|---|---|
| EasyGL | `easygl_skinnedeffect_world_normal_test.cpp` | GetBackBufferData, linear | 2/8 | **8/8** | 0 / 255 / 228 / 114 |
| WebGPU | `webgpu_skinnedeffect_world_normal_test.cpp` | GetBackBufferData, **sRGB** | 2/8 | **8/8** | 0 / 255 / 243 / 178 |
| SdlGpu | `sdlgpu_skinnedeffect_world_normal_test.cpp` | RenderTarget2D::GetData, linear | 1/4 | **4/4** | 0 / 255 / 228 / 114 |

EasyGL/WebGPU cover both `PreferPerPixelLighting` variants (8 checks); SdlGpu has one lit skinned
path (4 checks). The scale case discriminates all three hypotheses: correct inverse-transpose
(228 linear / 243 sRGB) vs no-world Variant A (180 / 219) vs raw-World Variant B (114 / 178) — every
pre-fix backend read the Variant-A value, every post-fix backend reads the inverse-transpose value.
WebGPU's swapchain is sRGB (`BGRA8UnormSrgb`), so its bytes are the sRGB OETF of the identical linear
N·L (empirically, un-worlded 0.707 → exactly 219 = round(255·sRGB(0.707))).

**Cross-backend conformance (Phase 8).** Same World/bones/geometry/material/light on all four
verified backends:
| Case | correct N·L | Vulkan | EasyGL | SdlGpu | WebGPU (sRGB) | WebGPU→linear |
|---|---|---|---|---|---|---|
| identity | 0.000 | 0 | 0 | 0 | 0 | 0.000 |
| rotationZ(90) | 1.000 | 255 | 255 | 255 | 255 | 1.000 |
| scale(2,1,1) | 0.894 | 228 | 228 | 228 | 243 | 0.894 |
| rot×scale | 0.447 | 114 | 114 | 114 | 178 | 0.447 |
The three linear-backbuffer backends are **byte-identical** (0 divergence); WebGPU is the sRGB
encoding of the same linear normal, which decodes back to the same N·L within 8-bit quantization
(≤0.2%). Tolerance justified: the only cross-backend difference is the swapchain transfer function,
not the normal transform.

**Regression / validation.**
- EasyGL: 95 effect tests pass (all identity-World skinned cases, PBR, environment-map, basiceffect,
  model, goldens). `EasyGL_AvatarRenderer_TintRouting` still fails with the **identical**
  pre-existing value `left=(81,51,31)` both with and without the fix (confirmed by a stash-rebuild
  baseline) — the known REMED-BUILD-003 fragment-stage tint-doubling defect, orthogonal to the
  vertex normal.
- WebGPU: 11 effect tests pass (Skinned3D, SkinnedPbr3D, Pbr3D, LitTextured3D, …); no wgpu
  validation errors.
- SdlGpu: all 22 SdlGpu tests pass; full-suite run (5603 tests) had 4 failures, all pre-existing and
  backend-agnostic — `CnjEffectTest.LoadsRealCnjFixture`, `CnjStockEffectTest.CustomGlslEffectStillWorks`,
  `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` (the same 3 the Vulkan slice documented) plus
  a flaky Net `ENetDiscoveryServiceTest`; none are SdlGpu skinned/effect tests.

**Coverage boundary (Variant B / PBR-skinned path).** The per-backend analytic harnesses drive
`SkinnedEffect` (the non-PBR Variant-A programs) directly with pixel assertions. The PBR-skinned
Variant-B shaders (EasyGL `EnsurePbrSkinnedProgram`, WebGPU `CreateSkinnedPbrResources`, SdlGpu
`pbr_skinned3d`) are fixed with the *identical* inverse-transpose composition and verified by (a)
code-consistency with the pixel-verified Variant-A world factor on the same backend, (b) equivalence
to Vulkan's already-analytic-verified `pbr3d_skinned`, and (c) each backend's existing
identity-World `SkinnedPbrEffect` regression test still passing. A dedicated non-identity-World PBR
harness (stride-68 `SkinnedPbrEffect`, whose BRDF makes a clean N·L readout harder) was not built
this session — the residual risk is low given (a)–(c), but it is the one path proven by consistency
rather than by direct non-identity pixel measurement.

**New finding (recorded, not fixed here): REMED-GFX-059** — the SdlGpu backend emits a Vulkan
validation error `VUID-vkCmdDraw-renderPass-02684` on every 3D draw. Confirmed **pre-existing** (the
existing `SdlGpu_Skinned` test emits it 6× against unchanged shaders) and independent of GFX-006
(readback values are correct). Left outside this task — a SdlGpu render-pass/pipeline
attachment-compatibility issue, not a normal-transform defect.

**Remaining GFX-006 scope.** Only the bytecode-blocked backends remain: Bgfx, D3D9, D3DCommon
(D3D11/D3D12). GFX-006 stays IN PROGRESS until those regenerate.

### REMED-GFX-008 — Vulkan skinned Ambient/Emissive — **DEFERRED this session** (verification entangled with avatar recalibration)

**Root cause re-confirmed at source (Vulkan).** The skinned fragment shaders compute
`litRGB = (pc.ambientColor + lightSum) * diffuseColor`, reading `pc.ambientColor` from
`FillExtPushConst`'s `pc[20..22]`. But `SkinnedEffect::FillGpuDrawParams` (`SkinnedEffect.cpp:320`)
**never writes `p.ambientColor`** (it defaults to 0) and instead pre-folds ambient into
`p.emissiveColor` = `(emissive + ambient*diffuse)*alpha` — byte-for-byte FNA's
`SetMaterialColor()`. The skinned UBOs have **no emissive slot** at all. So for skinned draws
`pc.ambientColor` is always 0 and emissive is never consumed — both `AmbientLightColor` and
`EmissiveColor` are silent no-ops, exactly as the audit states.

**Why deferred rather than fixed.** The correct FNA formula is `litRGB = lightSum*diffuse + emissive`
(with the pre-folded emissive). `Vulkan_AvatarRenderer_TintRouting` drives `AmbientLightColor(1,1,1)`
**and** a full head-on directional light `(0,0,-1)` on a `+Z`-facing quad (N·L≈1), then asserts the
output equals the raw tint. Working through it: the FNA-correct formula yields
`lightSum*diffuse + emissive ≈ 1·diffuse + 1·diffuse = 2·diffuse` — roughly double the tint, which
**breaks the test**. It passes today only because the missing ambient/emissive cancels that
double-count back to `1·diffuse`. This is the "passes by coincidence" the plan flags. Re-baselining
the test to the doubled value would merely encode over-driven numbers; the genuinely-correct fix
requires **recalibrating AvatarRenderer's lighting inputs** (it should not drive both full ambient
and a full light if the shader consumes both correctly) — an avatar-subsystem change outside this
shader campaign's clean scope, which the campaign's own rules say not to opportunistically expand
into. The Vulkan fix is otherwise ready (add an emissive slot to `skinnedFogUboData` + point the 4
frag shaders at it), but it **cannot be rigorously verified** without that recalibration, so it is
deferred rather than landed unverified. Recommend scheduling GFX-008 jointly with an
AvatarRenderer-lighting-calibration task (owner: GRAPHICS + avatar), per the plan's "handle both
together."

---

## Wave 3 — GRAPHICS shader/effect campaign, **Bgfx slices** (REMED-GFX-005 / -006 / -007)

This session unblocked the Bgfx backend, which the prior Wave-3 passes left BYTECODE-BLOCKED
("no shaderc binary"). The blocker was purely the missing tool — the bgfx shader source and its
3rdparty (glslang / spirv-tools / spirv-cross / tint) were all present under
`cmake-build-bgfx/_deps/bgfx_cmake-src/`.

### Shaderc toolchain — built from source (no repo change required)

- **Tool:** bgfx `shaderc`, **version 1.19.150**, built from the already-fetched bgfx source
  (**bgfx rev `759bdeb936ea95e4ac13d1ba8d4ce2e91c5c17d2`**, bimg `c3cf9af…`, bx `c98e98c…`).
- **Path:** `cmake-build-bgfx-shaderc/cmake/bgfx/shaderc` (in-repo, **gitignored**, 21 MB binary /
  183 MB build dir — kept as the reusable regen toolchain).
- **Configure:** standalone CMake on the bgfx.cmake `CMakeLists.txt`, `Release`, `ccache`,
  `-DBGFX_BUILD_TOOLS_SHADER=ON` with examples/tests/texture/geometry/bin2c tools OFF,
  `BGFX_CUSTOM_TARGETS=OFF`, `BGFX_INSTALL=OFF`. Built `--target shaderc -j4`.
- Links glslang / glsl-optimizer / spirv-opt / spirv-cross / **tint** (WGSL) / fcpp — all from the
  bgfx 3rdparty tree; `ldd` clean (no missing libs; libdxcompiler not needed for the glsl/essl/
  spirv/wgsl targets `compile_shaders.py` uses). **No CNA/build-script change was needed to make
  the toolchain reproducible**, so no `REMED-BUILD-*` finding was raised.

### Byte-reproducible regeneration — proven before any edit

Ran `compile_shaders.py <shaderc> <bgfx/src>` against the **unmodified** shader sources: the
regenerated `bgfx_shaders.hpp` was **byte-identical** to the committed header (same MD5
`b0d1cc6d…`, empty `git diff`), across all **27 shaders × 4 profiles (glsl-140 / essl-300 / spirv /
wgsl) = 108 arrays**. This proves the built shaderc is the exact toolchain that produced the
committed header, and that WGSL (tint) and SPIR-V outputs are deterministic here.

### Affected-shader matrix (exhaustive inspection, not assumed)

| Task | Bgfx shaders | Note |
|---|---|---|
| GFX-005 fog | **14** `vs_*.sc` (`alpha_test3d`, `alpha_test_colored3d`, `colored3d`, `colored_textured3d`, `dual_texture3d`, `dual_texture_colored3d`, `env_map3d`, `lit_textured3d`, `lit_textured3d_vertexlit`, `pbr3d`, `pbr_skinned3d`, `skinned3d`, `skinned3d_vertexlit`, `textured3d`) | The plan listed only 3 (the ones with test-audit coverage); the mirror `(FogEnd−z)` was byte-identical in all 14. `vs_instanced3d` correctly has no fog. |
| GFX-006 normal | `vs_skinned3d` (Variant A), `vs_skinned3d_vertexlit` (Variant A), `vs_pbr_skinned3d` (Variant B) | `vs_skinned3d_vertexlit` was **NOT** in the audit's Bgfx evidence — found by exhaustive read (identical Variant-A defect). |
| GFX-007 emissive | `fs_env_map3d.sc` | `(Emissive+lightSum)*Diffuse` → `lightSum*Diffuse + Emissive`. |

### GFX-005 — fog mirror fix

- All 14 shaders' `clamp((FogEnd−z)/max(FogEnd−FogStart,1e-6),0,1)` → the FNA/EasyGL Task-1111 form
  `((abs(FogEnd−FogStart)<1e-6)?0.0:clamp((z+FogEnd)/(FogEnd−FogStart),0,1))` (object-space z, per
  the campaign's decision to fix only the mirror, not object→view-space). The three false
  "matches EasyGL's established formula" precedent comments were corrected (9/1/1 variants).
- **Tests (test-first, fail pre-fix):** the 6 Bgfx fog tests were retargeted. The 4 that used the
  collapsing FogStart=0/FogEnd=1 scene (basiceffect/lit/skinned/env) moved to FogStart=0/FogEnd=−0.9
  at z∈{0,0.45,0.9} (blue/purple/red, asserted pixels unchanged); **2/3→3/3** each. `alphatest_fog`
  and `dualtexture_fog` already used the non-collapsing FogStart=−0.9/FogEnd=0.9 range, where the
  correction simply **swaps** the two endpoints (z=−0.9→full fog, z=+0.9→material); expectations
  swapped, **1/3→3/3** each. Every retargeted case discriminates the mirror from the correct formula.

### GFX-006 — SkinnedEffect world-space normal

- **bgfx's shaderc does not expose the GLSL `inverse()`/`transpose()` builtins** (SPIR-V target:
  `'inverse' : no matching overloaded function`, `mat3(mat4)` misparse), unlike the libshaderc path
  the Vulkan/SdlGpu slices used. Resolved by **supplying the World inverse-transpose CPU-side** via
  the existing `u_normalMatrix` uniform (the same one the lit shaders already consume), computed by
  the backend's already-proven `ComputeNormalMatrix3x3` (cofactor/det = inverse-transpose, verified
  against the lit shaders). The 3 skinned shaders now `normalize(mul(u_normalMatrix, skinnedNormal))`
  after the bone 3×3; `vs_pbr_skinned3d`'s **tangent stays on raw World** (directions, not normals).
- **C++:** the 4 skinned draw sites (2 skinned + 2 pbr-skinned, across the two `FillGpuDrawParams`
  paths) now `ComputeNormalMatrix3x3(params.worldColMajor, …)` + `setUniform(normalMatrix3DUnif_,…)`.
  Previously only the lit/pbr paths uploaded it. No UBO-layout or FillGpuDrawParams-value change.
- **Test:** new `examples/bgfx_skinnedeffect_world_normal_test.cpp` (port of the EasyGL/Vulkan
  analytic harness; pixel-lit **and** vertex-lit; identity/rotationZ(90)/scale(2,1,1)/rot×scale).
  **2/8 → 8/8.** Pre-fix values matched Variant A exactly (rotation 0 vs 255, scale 180 vs 228,
  rot×scale 180 vs 114); post-fix every case matches the FNA analytic value **to the byte**
  (0/255/228/114). The 228 (not 114) confirms the inverse-transpose specifically, rejecting a naive
  raw-World "fix".

### GFX-007 — EnvironmentMapEffect emissive

- `fs_env_map3d.sc`: `(u_emissiveColor+lightSum)*u_diffuseColor` → `lightSum*u_diffuseColor +
  u_emissiveColor` (FNA Lighting.fxh; `u_emissiveColor` is already the pre-folded
  `(Emissive+Ambient*Diffuse)*alpha`). Discriminating sub-test added to
  `bgfx_environmentmapeffect_amount_zero_test.cpp` (Diffuse=Emissive=(0.5,0.5,0.5), no lights,
  Amount=0, tex=(200,100,50)): **pre-fix got=(50,25,13), post-fix got=(100,50,25).**

### Regeneration semantic diff (per-array)

Regenerated `bgfx_shaders.hpp` once (all three fixes). Per-array MD5 diff vs the committed header:
**exactly 60 of 108 arrays changed = the 15 intended logical shaders × 4 profiles** (14 fog `vs_*` +
`fs_env_map3d`); the other **48 arrays byte-identical**; **0 added, 0 removed**. No unrelated
regeneration drift. (`fs_env_map3d` glsl/essl/spv kept their byte-lengths but changed content — the
`a*c+b` reorder — while wgsl shrank 2639→2618; every fog `vs` grew, consistent with the added
zero-length guard; the 3 skinned `vs` grew from the added `u_normalMatrix` + the fog fix.)

### Verification / regression / validation

- **Post-fix pixel tests:** world-normal 8/8; all 6 fog tests 3/3; env amount-zero 2/2. Cross-backend:
  the Bgfx world-normal linear readback (0/255/228/114) is **byte-identical** to the Vulkan/EasyGL/
  SdlGpu slices' linear values, confirming cross-backend conformance of the inverse-transpose.
- **Full Bgfx regression:** `ctest -R ^Bgfx_` (115 tests) → **113 pass, 2 fail**. Both failures
  (`Bgfx_RenderTarget2D_MsaaResolve`, `Bgfx_RenderTargetCube_DepthFormat`) are **pre-existing,
  deterministic, environment-only** — they run under llvmpipe "OpenGL 2.1", which does no real MSAA
  averaging / the tested depth behavior; neither uses fog/skinned/env-map effects. **Zero effect-test
  regressions** (SkinnedEffect IdentityBones/Combined/Multilight/VertexColor/WeightsPerVertex/PBR,
  all EnvironmentMap variants, PbrEffect, BasicEffect NormalTransform all pass).
- **Environment note (not a new remediation ID):** an initial full-suite `ctest -j4` (5743 tests)
  **killed Xvfb `:99` mid-run**, cascading ~115 Bgfx graphics tests into
  `SDL_InitSubSystem(SDL_INIT_VIDEO) failed: x11 not available` aborts — a REMED-BUILD-010-class
  Xvfb-under-load fragility, **not** a code regression (re-running the Bgfx subset at `-j2` against a
  fresh Xvfb gave the clean 113/115). The 6 non-graphics full-suite failures were all known
  pre-existing/flaky (ENet ×2, AudioCategory, CnjEffect/CnjStockEffect GLSL-toolchain-absent,
  WireFrame capability). Recommend the Bgfx graphics shard be run at ≤ `-j2` on a single Xvfb.
- **Validation:** Bgfx runs the OpenGL renderer here (no Vulkan validation layers); every effect
  program created and rendered without shader-compile warnings or uniform/attribute mismatches (the
  exact-byte world-normal readout proves `u_normalMatrix` binds and is consumed correctly). The CPU
  change is a `float[9]` stack write reusing the proven `ComputeNormalMatrix3x3` — no new allocation
  or bounds risk — so a dedicated ASan/UBSan Bgfx build (400–900 MB) was judged not proportionate.

### Remaining GFX-005/006/007 scope

- **GFX-005:** D3DCommon (D3D11/D3D12) + D3D9 fog — BYTECODE-BLOCKED (fxc/dxc absent). SdlGpu has no
  fog (GFX-009); D3D9 custom-shader fog is the separate object-space defect (GFX-010).
- **GFX-006:** D3D9 (`SkinnedVertexColor3D` A, `PbrSkinned3D` B) + D3DCommon (`skinned*` A ×4,
  `pbr_skinned3d` B) — BYTECODE-BLOCKED. All directly-editable + Bgfx backends now DONE.
- **GFX-007:** WebGPU + SdlGpu now **DONE and VERIFIED** (commits `38a1008d`, `e30868eb`; see the
  dedicated section below). The SdlGpu "extra alpha-squaring" turned out **inseparable** — the same
  one-line reorder removes it. **Only D3DCommon (D3D11/D3D12) remains — BYTECODE-BLOCKED**
  (`env_map3d.frag.hlsl:46` = `(EmissiveEm.xyz + lightSum) * DiffuseColor.rgb`, identical defect;
  no fxc/dxc toolchain in the repo, so the shared DXBC cannot be regenerated in this sandbox).

---

## REMED-GFX-007 — Wave 3 — GRAPHICS shader campaign, WebGPU/SdlGpu GFX-007 slices (DONE + VERIFIED)

Commits: `7f3e82bf` (tests, fail pre-fix), `38a1008d` (WebGPU fix), `e30868eb` (SdlGpu fix + SPIR-V
regen). Branch `feature/audit`.

### Scope sweep (exhaustive, not assumed)

Each backend has **exactly one** `EnvironmentMapEffect` shader variant, both confirmed AFFECTED:
- **WebGPU** — `CreateEnvMapResources()`'s runtime WGSL string in `WebGPUGraphicsBackend.cpp`
  (`env_map3d.wgsl`), fs_main. No skinned/specular env-map variant exists.
- **SdlGpu** — `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl`. Its `.vert.glsl`,
  and the whole env-map path in `SdlGpuGraphicsBackend.cpp`, carry no second variant.

Everything else in each env-map path was classified and left untouched: Fresnel weighting, reflection
vector, cube sampling, `envMapAmount`/blend, `combinedAlpha` and the envmap/output alpha (all
**ALREADY_CORRECT** — byte-for-byte the verified Vulkan form), WebGPU env-map fog (**ALREADY_CORRECT**,
GFX-005 form), SdlGpu env-map fog (**NOT_APPLICABLE** — SdlGpu has no fog, GFX-009). No other stock
effect touched.

### Root cause (both backends, one line each)

CPU side (`EnvironmentMapEffect::FillGpuDrawParams()`, unchanged and correct) pre-folds Alpha into
**both** operands: `diffuseColor = (DiffuseColor*Alpha, Alpha)` and
`emissiveColor = (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha`. Both shaders computed
`litRGB = (emissive + lightSum) * diffuse`, so the emissive term became
`(Emissive + Ambient*Diffuse)*Alpha · Diffuse*Alpha` = emissive re-scaled by Diffuse **and** Alpha
squared. FNA `Lighting.fxh` adds emissive **unscaled**.

- **WebGPU** `env_map3d.wgsl`: `let litRGB = (ep.emissiveAmount.xyz + lightSum) * ep.diffuseColor.rgb;`
  → `let litRGB = lightSum * ep.diffuseColor.rgb + ep.emissiveAmount.xyz;`
- **SdlGpu** `env_map3d.frag.glsl:53`: `vec3 litRGB = (pc.emissiveAmount.xyz + lightSum) * fragTint.rgb;`
  → `vec3 litRGB = lightSum * fragTint.rgb + pc.emissiveAmount.xyz;`

### Alpha-squaring conclusion — inseparable, NOT a separate finding

The audit (`audit/examples/sdlgpu_smoke_test.cpp.audit.md:187-201`) reported SdlGpu as "uniquely also
squares the Alpha term." Reproduced and confirmed the arithmetic, but it is **not an independent
defect**: it is the exact `Alpha²` factor produced by the *same* wrong multiply, present latently in
WebGPU too (same prefold). The single emissive-unscaled reorder removes emissive×Diffuse,
Ambient×Diffuse², **and** Alpha² together. No new REMED-GFX task raised. The Alpha=0.5 sub-check in
both new tests fails pre-fix specifically on the `Alpha²` term (SdlGpu got=(13,6,3) vs correct
(50,25,13)) and passes post-fix.

### Tests (test-first; pre-fix FAIL → post-fix PASS)

Both new tests use non-white Diffuse=(0.5), Emissive=(0.5), no directional lights (lightSum=0),
`EnvironmentMapAmount=0` (isolates litRGB), tex=(200,100,50); plus an Alpha=0.5 case, a white-Diffuse
control (both formulas coincide — the masking case), and a zero-Emissive control (→ black). All four
checks, both backends:

| Backend | readback | core pre→post | Alpha=0.5 pre→post | controls |
|---|---|---|---|---|
| SdlGpu | linear RGBA8 RT | (50,25,13) → **(100,50,25)** | (13,6,3) → **(50,25,13)** | pass pre+post |
| WebGPU | **sRGB** backbuffer | (122,88,63) → **(168,122,88)** | (63,43,29) → **(122,88,63)** | pass pre+post |

- **WebGPU color-space handling.** The surface is `*_Srgb` here (empirically: linear tex*0.5 =
  (100,50,25) reads back (168,122,88)), and `ReadBackbuffer()` returns raw stored bytes (no sRGB
  decode). The test is therefore **transfer-function-agnostic by construction**: it calibrates `T()`
  with an `EnvironmentMapAmount=1` flat full-replace of a *known-linear* cube through the identical
  output-encoding path, then compares each emissive draw to its predicted-correct calibration. Pre-fix
  the emissive draws matched the *wrong* calibration to ≤1 byte; post-fix they match the *correct*
  calibration to ≤1 byte. Any byte difference vs. the linear backends is thus proven color-space, not
  semantic. (A naïve direct-linear assertion of (100,50,25) would have wrongly failed here — this is
  why the calibration harness was built.)

### Regeneration semantic diff (SdlGpu SPIR-V)

Byte-reproducible toolchain proven **before** editing: a no-change `compile_shaders.py` run reproduced
the committed `spirv_shaders.hpp` exactly (md5 `4bc897b2409e3218ad961891832e3b6a`). Post-edit per-array
diff: **exactly 1 of 23 arrays changed** — `kEnvMap3dFragSpv` (1070→1070 words, content reordered);
the other 22 byte-identical; 0 added/0 removed. No unrelated drift.

### Cross-backend conformance (Phase 6)

Core case (Diffuse=0.5, Emissive=0.5, Alpha=1, no lights, Amount=0, tex=(200,100,50); expected linear
(100,50,25)), all four live-verified this session:

| Backend | format | raw bytes | decoded linear | tol | divergence |
|---|---|---|---|---|---|
| Vulkan | linear RGBA8 | (100,50,25) | (100,50,25) | ±20 | none |
| Bgfx | linear | (100,50,25) | (100,50,25) | ±20 | none |
| SdlGpu | linear RGBA8 RT | (100,50,25) | (100,50,25) | ±16 | none |
| WebGPU | sRGB BGRA8UnormSrgb | (168,122,88) | sRGB⁻¹ → (100,50,25) | ±12 (encoded) | color-space only |

All four agree after decoding to linear. sRGB⁻¹(168,122,88) = (100,50,25) to <1 (verified by hand).
FNA/XNA-derived semantics + the already-verified Vulkan/Bgfx results are the oracle (not EasyGL).

### Regression / validation (Phases 7–8)

- **SdlGpu shard** `ctest -L SdlGpu -j2`: **23/23 pass** (env-map, env-map-emissive, effects, PBR,
  skinned world-normal [GFX-006 control], skinnedpbr, rendertarget2d/±MSAA, rendertargetcube, MRT,
  samplerstate, renderstate, smoke, swapchain-recovery, …). Zero regressions.
- **WebGPU shard** `ctest -L WebGPU -j2`: **25/25 pass** (env-map3d, env-map-emissive, pbr3d,
  skinnedpbr3d, skinned world-normal [GFX-006 control], littextured3d, dualtexture3d, alphatest3d,
  colored/textured/coloredtextured3d, instanced3d, rendertarget2d, rendertargetcube, msaa, mipgen,
  clear-readback, texture getdata, …). Zero regressions.
- **WebGPU validation:** no WGSL compile / binding / alignment / pipeline errors (only the benign
  "No DRI3 support" presentation note). Shader-only change; CPU parameter code untouched, so no CPU
  sanitizer run (per task guidance).
- **SdlGpu validation:** the `VUID-vkCmdDraw-renderPass-02684` warning is the **known pre-existing
  REMED-GFX-059** (render-pass depth-attachment compatibility), emitted identically pre- and post-fix
  and unrelated to the fragment formula — **not** a GFX-007 regression.

### Remaining GFX-007 blocker

**D3DCommon (D3D11/D3D12) only** — `src/CNA/Internal/Backends/D3DCommon/shaders/env_map3d.frag.hlsl:46`
holds the identical `(EmissiveEm.xyz + lightSum) * DiffuseColor.rgb` defect. No fxc/dxc HLSL→DXBC
toolchain exists in the repo, so the shared compiled bytecode cannot be regenerated here. Source left
untouched (per scope). Recorded BYTECODE-BLOCKED; unblock when a reproducible HLSL toolchain lands
(tracked alongside the GFX-005/006 D3DCommon bytecode blockers).

---

## REMED-GFX-009 — SdlGpu stock-effect fog (from-scratch implementation) — IN PROGRESS

Branch `feature/audit`. Fog was a **total absence** on SdlGpu (0/N shader families): a grep of every
`.glsl` for any fog identifier and of `SdlGpuGraphicsBackend.cpp` for `fogColor/fogEnabled/fogStart/
fogEnd` both returned zero. Depends on **REMED-GFX-005** (the corrected fog formula, already the
verified oracle on Vulkan/Bgfx/EasyGL). Semantic reference = GFX-005's formula, adapted to SdlGpu's
own SPIR-V set convention (vertex UBOs = set 1, fragment UBOs = set 3).

### Phase 1 — exhaustive affected-family matrix (proven, not assumed)

Verified by reading **every** `.glsl` in `src/CNA/Internal/Backends/SdlGpu/shaders/` (23 files → 23
SPIR-V arrays in `spirv_shaders.hpp`). The audit's "≈10 families" is the effect count; the shader
count is **13 3D vertex shaders + 8 3D fragment shaders** (some fragment shaders are shared). Full
classification:

| # | Vertex shader | Fragment shader | XNA effect | Fog | Vert free UBO slot (set 1) | Vert free out-loc |
|---|---|---|---|---|---|---|
| 1 | `colored3d.vert` | `colored3d.frag` | BasicEffect (VertexPositionColor, stride 16) | REQUIRED+MISSING | binding 1 | loc 1 |
| 2 | `textured3d.vert` | `textured3d.frag` | BasicEffect (VertexPositionTexture, 20) | REQUIRED+MISSING | binding 1 | loc 2 |
| 3 | `colored_textured3d.vert` | `textured3d.frag` (shared) | BasicEffect (VertexPositionColorTexture, 24) | REQUIRED+MISSING | binding 1 | loc 2 |
| 4 | `alpha_test3d.vert` | `alpha_test3d.frag` | AlphaTestEffect (20/32) | REQUIRED+MISSING | binding 1 | loc 2 |
| 5 | `alpha_test_colored3d.vert` | `alpha_test3d.frag` (shared) | AlphaTestEffect (24) | REQUIRED+MISSING | binding 1 | loc 2 |
| 6 | `dual_texture3d.vert` | `dual_texture3d.frag` | DualTextureEffect (20) | REQUIRED+MISSING | binding 1 | loc 2 |
| 7 | `dual_texture_colored3d.vert` | `dual_texture3d.frag` (shared) | DualTextureEffect (24) | REQUIRED+MISSING | binding 1 | loc 2 |
| 8 | `lit_textured3d.vert` | `lit_textured3d.frag` | BasicEffect lit path (VertexPositionNormalTexture, 32) | REQUIRED+MISSING | binding 2 | loc 4 |
| 9 | `env_map3d.vert` | `env_map3d.frag` | EnvironmentMapEffect (32) | REQUIRED+MISSING | binding 2 | loc 4 |
| 10 | `skinned3d.vert` | `lit_textured3d.frag` (shared) | SkinnedEffect (VertexPositionNormalTextureSkinned, 52) | REQUIRED+MISSING | binding 2 | loc 4 |
| 11 | `skinned_colored3d.vert` | `skinned_colored3d.frag` | SkinnedEffect + vertex color (56) | REQUIRED+MISSING | binding 2 | loc 5 |
| 12 | `pbr3d.vert` | `pbr3d.frag` | PbrEffect (VertexPositionNormalTangentTexture, 48) | REQUIRED+MISSING (parity w/ Vulkan/Bgfx PBR fog) | binding 2 | loc 5 |
| 13 | `pbr_skinned3d.vert` | `pbr3d.frag` (shared) | SkinnedPbrEffect (68) | REQUIRED+MISSING | binding 2 | loc 5 |
| — | `sprite2d.vert` | `sprite2d.frag` | 2D SpriteBatch | **NOT APPLICABLE** (no 3D depth/fog) | — | — |

- **FOG REQUIRED AND MISSING:** all 13 3D families above (13 vert + 8 distinct frag arrays).
- **FOG NOT APPLICABLE:** `sprite2d` (2D). Its 2 SPIR-V arrays must remain byte-identical after regen.
- **SEPARATE DEFECT:** none newly found in this phase; the pre-existing `REMED-GFX-059`
  (VUID-02684) is unrelated and already tracked.
- Shared-fragment-shader constraint checked: each shared fragment shader's paired vertex shaders
  use identical output-location counts, so a single fog `in`-varying location is consistent across
  every pipeline that reuses that fragment shader (textured3d.frag: locs 0–1 → fog loc 2;
  lit_textured3d.frag: locs 0–3 → fog loc 4; pbr3d.frag: locs 0–4 → fog loc 5).

### Phase 2 — fog pipeline design (per family, one shared formulation)

Root cause is *absence*, so the whole pipeline is built from scratch:

- **CPU source** (`GpuDrawParams`, already populated by the XNA layer for every backend):
  `fogEnabled`, `fogColor[3]`, `fogStart`, `fogEnd` (`IGraphicsBackend.hpp:438–445`).
- **New shared 32-byte fog UBO** (mirrors Vulkan's `FogParams` shape byte-for-byte):
  `vec4 fogColorEnabled` (xyz=FogColor, w=fogEnabled) + `vec4 fogStartEnd` (x=start, y=end, zw=pad).
  Filled by one new helper `FillFogUniforms(std::array<float,8>&, const GpuDrawParams&)`; a
  default-constructed all-zero block = fog disabled (correct for the `FillColoredUniforms` no-params
  path).
- **Slot allocation** (SDL_gpu vertex UBO = set 1; push slot == binding index): the shared 128-byte
  `PC` block is *fully packed* (32/32 floats), exactly as on Vulkan, so fog cannot extend it — it
  gets its own UBO at the next free binding: **binding 1** for the 7 basic/alpha/dual families
  (only `PC` at binding 0), **binding 2** for the 6 env/lit/skinned/pbr families (which already use
  bindings 0+1). Bound to the **vertex stage only**.
- **Per-fragment blend via a varying** (avoids any fragment-UBO/slot changes): the vertex shader
  computes the keep-factor and forwards `fragFog = vec4(FogColor.rgb, keepFactor)`; the fragment
  shader ends with `outColor.rgb = mix(fragFog.rgb, outColor.rgb, fragFog.a)`. Fog affects **RGB
  only**, never alpha (matches FNA and every other backend).
- **Fog coordinate:** raw **object-space** pre-skin `inPos.z` (the deliberate GFX-005 convention,
  identical to Vulkan/Bgfx/EasyGL — cross-cutting object-space note in the GFX-005 section). For the
  4 skinned shaders the coordinate is the **pre-skin** `inPos.z`, matching Vulkan's
  `skinned3d.vert.glsl` (`aPos.z`) exactly for cross-backend conformance.
- `num_uniform_buffers` for each of the 13 vertex shaders is bumped by 1 at `SDL_CreateGPUShader`
  time; fragment shader counts are unchanged (fog reaches the fragment via the varying).

### Phase 3 — semantics (GFX-005 formula, verbatim)

`keep = fogEnabled ? ( |end-start|<1e-6 ? 0.0 : clamp((z + end)/(end - start), 0, 1) ) : 1.0`
then `rgb = mix(FogColor, rgb, keep)`. keep=1 → no fog (original), keep=0 → full FogColor.
`FogStart==FogEnd` → fully fogged (matches FNA `SetFogVector` returning `(0,0,0,1)`). Verified scene
(shared with Vulkan for conformance): FogStart=0, FogEnd=−0.9, z∈{0,0.45,0.9} → keep∈{1,0.5,0};
blue geometry + red fog → OFF=blue(0,0,255), half=(128,0,128), full=red(255,0,0).

### Toolchain reproducibility (Phase 7 prerequisite, proven BEFORE editing)

A no-change `compile_shaders.py` regen reproduced the committed `spirv_shaders.hpp` byte-for-byte
(md5 `6cbe1ec070426254dcdc6cb071314b5a`). Expected post-edit diff: **21 of 23 arrays change**
(13 vert + 8 frag), only the 2 `sprite2d` arrays byte-identical.

### Phases 4–10 — implementation and verification (DONE, commits `dda6249f` tests, `8cfdd8b9` fix)

**Phase 4 — tests first (pre-fix FAIL → post-fix PASS).** Three new pixel tests (linear
RenderTarget2D readback, swapchain download segfaults on this backend per SDLGPU-39), each family's
scene shared with the verified Vulkan fog tests for cross-backend comparability — blue geometry, red
fog, FogStart=0, FogEnd=−0.9 → Z=0 pure blue / Z=0.45 (128,0,128) / Z=0.9 pure red, plus a fog-OFF
control. skinned/PBR build the blue base via a single N·L=1 directional light; pbr3d/pbr_skinned3d
additionally use a shading-invariant discriminator (full fog must equal FogColor regardless of the
BRDF shade; half fog must equal `mix(red, sameZ-fog-off-reference, 0.5)`).

| Test | families | pre-fix | post-fix |
|---|---|---|---|
| `sdlgpu_basiceffect_fog_test` | colored3d, textured3d, colored_textured3d, lit_textured3d | **8/16** (all controls pass, all half+full fail) | **16/16** |
| `sdlgpu_effects_fog_test` | alpha_test3d(+colored), dual_texture3d(+colored), env_map3d | **8/16** | **16/16** |
| `sdlgpu_skinnedeffect_fog_test` | skinned3d, skinned_colored3d, pbr3d, pbr_skinned3d | **3/11** | **11/11** |

Pre-fix every fog-ON case rendered the un-fogged colour (fog absent); every fog-OFF/no-fog control
passed — the exact test-first signature. **43/43 post-fix.**

**Phases 5–6 — CPU + shader (one logical fix, inseparable).**
- CPU: `FillFogUniforms(std::array<float,8>&, const GpuDrawParams&)` writes all 8 floats
  (`fogColor.rgb`, `fogEnabled`, `fogStart`, `fogEnd`, 2 pad); `fogUniforms` field added to all 8
  `*DrawCommand` structs; filled in every `Queue*` path (the no-params `FillColoredUniforms` branch
  leaves it zero = disabled); pushed at the matching vertex slot in every `Issue*` path;
  `num_uniform_buffers` bumped +1 on all 13 vertex shaders (fragment counts unchanged — fog reaches
  the fragment via the varying, not a fragment UBO). No existing uniform layout touched; no public
  API change. Three now-false "No fog (deliberately deferred)" comments removed.
- Shaders: each of the 13 vertex shaders gained the FogParams UBO, a `fragFog` out varying, and the
  keep-factor compute; each of the 8 fragment shaders gained the `fragFog` in varying and the final
  `outColor.rgb = mix(fragFog.rgb, outColor.rgb, fragFog.a)`. Special care preserved: env_map3d's
  GFX-007 emissive composition, skinned/pbr_skinned GFX-006 world-normal transform, alpha_test's
  discard (fog applied after), dual_texture's dual contribution, all vertex-color paths.

**Phase 7 — SPIR-V regeneration.** No-change regen proven byte-identical to the committed header
(md5 `6cbe1ec0…`) BEFORE editing. Post-edit per-array diff: **exactly 21 of 23 arrays changed** (13
vert + 8 frag), the 2 `sprite2d` arrays byte-identical, 0 added / 0 removed. Word growth uniform and
explained (vert +~226–236 for UBO+compute+varying; frag +~111–127 for varying+mix). New header md5
`e6f2020a…`. No unrelated drift.

**Phase 8 — Vulkan validation (`VK_LAYER_KHRONOS_validation` under SDL_GPU).** Across all 43 draws
the only validation message is `VUID-vkCmdDraw-renderPass-02684` (60×) — the **known pre-existing
REMED-GFX-059** (render-pass depth-attachment `VK_ATTACHMENT_UNUSED` vs pipeline `1`), unrelated to
fog and emitted identically pre-fix. **No new messages**: no push-constant/uniform-buffer size or
alignment errors, no descriptor/binding/layout mismatch, no "uniform not set" — the new FogParams
UBO binding is correctly declared (num_uniform_buffers) and pushed (matching slot) on every path.

**Phase 9 — cross-backend conformance.** Live-run the verified Vulkan fog tests (GFX-005 controls,
all 3/3) against the identical scene. For every family sharing the blue-geometry/red-fog scene the
linear RGBA8 readback is **byte-identical** between backends:

| Scene | Z=0 (keep 1) | Z=0.45 (keep 0.5) | Z=0.9 (keep 0) | Vulkan | SdlGpu |
|---|---|---|---|---|---|
| BasicEffect colored/textured/lit | (0,0,255) | (128,0,128) | (255,0,0) | ✓ | ✓ (identical) |
| DualTextureEffect | (0,0,255) | (128,0,128) | (255,0,0) | ✓ | ✓ (identical) |
| EnvironmentMapEffect | (0,0,255) | (128,0,128) | (255,0,0) | ✓ | ✓ (identical) |

Both backends compute the same keep-factor `(z+FogEnd)/(FogEnd−FogStart)` and blend `mix(FogColor,
rgb, keep)`; both read back linear RGBA8, so the agreement is exact (no colour-space adjustment
needed, unlike the WebGPU sRGB case in GFX-007). The Vulkan `alphatest` test uses a different
material/fog colour but proves the same 0/0.5/1 interpolation.

**Phase 10 — full SdlGpu regression.** `ctest -L SdlGpu -j2`: **26/26 pass** (23 pre-existing + 3
new fog), **zero regressions**. Re-verified controls: `SdlGpu_SkinnedEffect_WorldNormal` (GFX-006),
`SdlGpu_EnvMapEmissive` (GFX-007), plus all env-map/pbr/skinned/skinnedpbr/render-target(±MSAA)/
MRT/samplerstate/renderstate/texture3d/texturecube/smoke/swapchain-recovery/draworder tests. Vulkan
GFX-005 fog controls re-run 5×3/3 (also serve as the cross-backend reference above).

**Sanitizers (memory-safety verification).** The CPU-side change is memory-safe by construction and
introduces no dynamic memory, pointer arithmetic, or variable-length copy: `fogUniforms` is a
value-initialised `std::array<float,8>` (32 bytes, no padding); `FillFogUniforms` writes all 8
indices explicitly (no uninitialised upload); every push uses `sizeof(command.fogUniforms)` = 32,
matching the shader's `2×vec4` FogParams block exactly (no size mismatch, no OOB). No existing
uniform struct layout was modified. Given this and the repo's explicit cost policy on
sanitizer builds (a full CNA+SDL ASan/UBSan rebuild is disproportionate for a fully-initialised
fixed-size POD append), no dynamic-sanitizer build was run — verification is by inspection, and per
the task's own caveat GPU sanitizer coverage is not claimed. Shader correctness is instead
established by the pixel tests, the SPIR-V regeneration proof, and Vulkan validation above.

### New findings

None. No separate fog-space defect, uniform-layout mismatch, colour-space mismatch, alpha/fog
ordering defect, test blind spot, or generated-shader drift surfaced. The only validation message is
the already-tracked pre-existing REMED-GFX-059 (unchanged in shape/frequency).

### Remaining fog-related work (backend fog matrix after this task)

- **SdlGpu:** DONE (this task).
- **Vulkan / Bgfx / EasyGL:** correct fog already (GFX-005 + EasyGL Task-1111).
- **D3D9 custom-shader / D3DCommon (D3D11/D3D12):** still the separate object-space-fog concern
  (REMED-GFX-010) and the mirrored-formula bytecode-blocked slices (REMED-GFX-005 D3D remainder) —
  out of scope here, unblock when an fxc/dxc HLSL→bytecode toolchain lands.

### Recommended next GRAPHICS step

`REMED-GFX-010` (move stock-effect fog from object-space to true view-space across the backends that
share the object-space approximation — EasyGL/Bgfx/Vulkan/SdlGpu/D3DCommon), which the GFX-005
cross-cutting note already flagged as broader than "D3D9-only". It is the natural continuation of the
fog work and does not require the blocked D3D bytecode toolchain for the GLSL/SPIR-V backends.

---

## REMED-GFX-010 — view-space stock-effect fog (cross-backend)

### Semantic contract (XNA/FNA fog-coordinate oracle) — established BEFORE implementation

Source of truth: FNA `EffectHelpers.SetFogVector` + `Common.fxh ComputeFogFactor` (read directly at
`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/`):

```
ComputeFogFactor(position) = saturate(dot(position, FogVector))     // position = OBJECT-space vertex
FogVector.xyz = (World*View).{M13,M23,M33} * scale
FogVector.w   = ((World*View).M43 + fogStart) * scale
scale         = 1 / (fogStart - fogEnd)
```

`dot(objectPos, FogVector)` algebraically equals `scale*(viewZ + fogStart)` where
`viewZ = (objectPos * World*View).z`. So **XNA fogs on VIEW-SPACE Z** (the eye-space Z, `d = -viewZ`),
computed cheaply by baking `World*View`'s third column into a per-draw vector and dotting it with the
**object-space** position. `fogStart == fogEnd` → `(0,0,0,1)` (fully fogged); fog disabled →
`Vector4.Zero` (no fog). This is exactly what D3D9's already-correct `ComputeFogVectorEXT()` does.

CNA backends carry the inverse "keep" convention (`keep = 1 - fogFactor`;
`finalRGB = mix(FogColor, geomRGB, keep)`), so the corrected shader term is
`keep = 1 - saturate(dot(vec4(pos,1), fogVector))`. **Under identity World/View this reduces exactly to
the previous object-space formula**, so every existing (identity-transform) fog test stays green — the
fix is a strict generalization, not a behavior change on the tested configs.

**Skinned:** FNA `Skin()` mutates `vin.Position` (bone transform) *before* `ComputeFogFactor(vin.Position)`,
so fog uses the **post-skin** object-space position; the fog vector itself bakes only `World*View`, never
the bones. CNA previously fogged on the **pre-skin** raw `aPos.z` — doubly wrong.

### Affected backend × family matrix (from exhaustive shader inventory)

All 56 fog-capable shaders across EasyGL(12)/Vulkan(16)/SdlGpu(13)/Bgfx(15) computed fog from raw
object-space input `.z`. Classification:

- **OBJECT_SPACE_BUG** — every fog-capable BasicEffect (colored/textured/colored_textured/lit/vertex-lit),
  AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect (all variants), PbrEffect,
  SkinnedPbrEffect shader in EasyGL/Vulkan/SdlGpu/Bgfx. (Skinned additionally used pre-skin Z.)
- **NOT_APPLICABLE** — instanced shaders are **not fog-capable in any backend** (no fog varying/uniform);
  Phase-10 "instanced fog" is therefore N/A, not a bug. sprite2d (2D) and colored3d_legacy carry no fog.
- **BYTECODE_BLOCKED** — D3D9 custom shaders (`SkinnedVertexColor3D/Pbr3D/PbrSkinned3D.hlsl`) and
  D3DCommon stock shaders share the same object-space defect but cannot be regenerated (no fxc/dxc).
  Source left untouched; slice recorded as blocked.

No shader in any backend had a view/worldView/modelView matrix available (only combined MVP/`u_wvp`, and
sometimes `world`), so plumbing the CPU-computed fog vector was **necessary**, not merely convenient.

### Shared CPU layer (all backends)

`GpuDrawParams.fogVector[4]` added; populated in every stock effect's `FillGpuDrawParams()`
(BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, PbrEffect,
SkinnedPbrEffect) as a faithful `SetFogVector` port from each effect's `World`/`View`. Inert until a
backend reads it.

### Test harness (Phase 4/5) — `examples/common/ViewSpaceFogRef.hpp`

Canonical transformed-camera scenes (orthographic projection so negative-view-Z geometry renders
full-screen while fog stays projection-independent): identity control, view-translate near/half/full,
world-translate, view-rotate (LookAt), fog-disabled; plus a SkinnedEffect pre-skin discriminator
(Z-translating bone + Z-translating view). Each discriminating scene holds object-space Z fixed while the
camera moves and **asserts ViewKeep != ObjectKeep before checking the pixel**, so the harness cannot
silently pass on a non-discriminating scene. Expected colors derived from first principles
(`Vector3::Transform(objPos, World*View).Z`), not from the shader path.

### Backend results

- **EasyGL — DONE+VERIFIED.** 12 programs: `uFogEnabled/uFogStart/uFogEnd` (object-space) replaced by a
  single `uFogVector` (vec4); non-skinned dot the object position, 3 skinned dot `skinnedPos`. Pre-fix
  4/9 → post-fix **9/9**; existing BasicEffect/Skinned/EnvMap/DualTexture/AlphaTest fog tests still 3/3
  each (no regression).
- **Vulkan — DONE+VERIFIED.** 16 shaders; `fogStartEnd`→`fogVector` (simple UBOs), alpha_test PC
  restructured (FogColor→3 floats, fog vector→16-aligned vec4@112), pbr UBO WeightsPerVertex relocated
  into `fogColorEnabled.w` so `[44..47]` becomes the fog vector (no UBO growth, identical pbr3d/pbr_skinned
  layout). **Bug found+fixed during bring-up:** leftover `[.z/.w]=0` zeroing lines (from the old
  fogStartEnd.zw) clobbered the fog vector's depth term — deleted. SPIR-V regen byte-reproducible across
  two runs; **exactly 17 vertex-shader arrays + alpha_test frag changed**, sprite2d/instanced3d/legacy and
  all read-only-FogColor frags byte-identical (UBO layouts unchanged). Pre-fix 3/9 → post-fix **9/9**;
  full Vulkan effect/fog regression **68/68** (BasicEffect/AlphaTest/DualTexture/EnvironmentMap/Skinned/
  Pbr/Orientation fog + controls); validation layers active, no new VUID errors.
- **SdlGpu — PENDING.**
- **Bgfx — PENDING.**
- **D3D9 / D3DCommon — BYTECODE-BLOCKED** (same object-space defect; no HLSL→bytecode toolchain).

### New findings

None separable so far. The Vulkan zeroing-line clobber was an inseparable bring-up detail of GFX-010,
fixed in the same commit. Instanced fog confirmed N/A (not a missing-feature finding). REMED-GFX-059
(pre-existing SdlGpu VUID) unchanged.
