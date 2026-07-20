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
| P1 | 21 | 3 | 0 | 0 | 18 |
| P2 | 44 | 3 | 0 | 1 | 40 |
| P3 | 28 | 0 | 0 | 0 | 28 |
| **Total** | **105** | **18** | **0** | **1** | **86** |

*P0's total was corrected 11→12 while closing `REMED-GFX-001`: `REMED-GFX-001`, `-002`, and `-003`
are all `Priority: P0-SAFETY` per `MASTER_REMEDIATION_PLAN.md`, so the row's prior total had
undercounted one of the three by one. P0 is now fully closed (all 12 DONE).*

*P1's Done count 1→3 reflects `REMED-CORE-006`/`-007` closing (see "Wave 1 (parallel) — CORE lane"
below). `REMED-CORE-014` (a `Discovered during remediation` finding, not one of the 105 counted
tasks) closed in the same change but is not part of this table.*

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

## Waves 2-5 — remaining tasks

| ID | Pri | Status | Owner | Branch | Notes |
|---|---|---|---|---|---|
| REMED-AUDIO-001 | P3 | NOT STARTED | | | |
| REMED-AUDIO-002 | P3 | NOT STARTED | | | |
| REMED-BUILD-003 | P1 | NOT STARTED | | | |
| REMED-BUILD-005 | P2 | NOT STARTED | | | |
| REMED-BUILD-006 | P2 | NOT STARTED | | | |
| REMED-BUILD-007 | P3 | NOT STARTED | | | |
| REMED-BUILD-009 | P3 | NOT STARTED | | | |
| REMED-CONTENT-004 | P1 | NOT STARTED | | | |
| REMED-CORE-001 | P1 | NOT STARTED | | | |
| REMED-CORE-002 | P2 | NOT STARTED | | | |
| REMED-CORE-003 | P2 | NOT STARTED | | | |
| REMED-CORE-004 | P1 | NOT STARTED | | | |
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
| REMED-GFX-005 | P1 | NOT STARTED | | | |
| REMED-GFX-006 | P1 | NOT STARTED | | | |
| REMED-GFX-007 | P1 | NOT STARTED | | | |
| REMED-GFX-008 | P1 | NOT STARTED | | | |
| REMED-GFX-009 | P1 | NOT STARTED | | | |
| REMED-GFX-010 | P2 | NOT STARTED | | | |
| REMED-GFX-011 | P1 | NOT STARTED | | | |
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
| REMED-BUILD-010 | `EasyGL_RealWindowResize` hangs the full 60s CTest `TIMEOUT` under a real desktop compositor (`DISPLAY` = a real logged-in GNOME/Mutter session, not an isolated Xvfb) | MEDIUM | P2 | REMED-BUILD-001 (full unfiltered `ctest` baseline run) | NOT STARTED — recorded, not fixed (out of scope for Wave 0) |
| REMED-BUILD-011 | `CMakePresets.json`'s `devices-asan`/`devices-tsan`/`devices-ubsan` configure presets (description explicitly says "Microsoft::Devices hardening") never set `CNA_DEVICES=ON` in their own `cacheVariables` — configuring with any of the three as documented (`cmake --preset devices-asan`) silently builds with the entire `CNA::Devices`/`Microsoft::Devices::Sensors` surface compiled out, so none of the sanitizer coverage the preset names promise actually exists unless the caller separately remembers `-DCNA_DEVICES=ON` | MEDIUM | P2 | REMED-DEVICES-001 (setting up ASan/TSan runs for the new concurrent tests) | NOT STARTED — recorded, not fixed (out of DEVICES-001's scope; worked around this task's own verification by passing `-DCNA_DEVICES=ON` on the configure command line into ad hoc `cmake-build-devices-asan`/`cmake-build-devices-tsan` build dirs rather than the named presets — a BUILD_TEST_CI-lane fix, not a DEVICES one) |
| REMED-CONTENT-007 | `VideoContentTypeReader.cpp`/`SongContentTypeReader.cpp` each duplicate a `ResolveRelativeFilePath()` helper with **zero** containment check (not even the partial one `ContentReader.cpp` had before this task) — a `Video`/`Song` `.xnb`'s own embedded filename field can be absolute or `..`-escaping and is joined onto the content root unchecked | HIGH | P1 | REMED-CONTENT-002 (repo-wide sweep) | NOT STARTED — recorded, not fixed (out of `-002`'s 3-site scope; same root cause, same fix shape — reuse `CNA::Internal::IsDisallowedAbsolutePath`/the `ResolveRelativeAssetPath` pattern) |
| REMED-CONTENT-008 | `ContentManager.cpp` joins 8 `.cnj`/JSON-manifest-supplied path fields (`dataField->stringValue` for `Texture3D`; `vertRel`/`fragRel` for `ShaderEffect`; `clipFileField->stringValue` for `AnimationClip`; `skeletonRel`, `vertFile`/`idxFile`, `morphTargetsFile` for skinned-model morph/animation data) onto the content root with no containment check — same `fs::path::operator/` pitfall as the 3 sites `-002` fixed, at file-supplied (not caller-supplied) untrusted strings | HIGH | P1 | REMED-CONTENT-002 (repo-wide sweep) | NOT STARTED — recorded, not fixed (out of `-002`'s 3-site scope; notably, this is the *same* `.cnj`-manifest subsystem that already has one field, `sourceFile`, correctly hardened via `CnjSourceFile.hpp` — these 8 fields were simply never given the same treatment) |
| REMED-NET-008 | A "client"-role `NetworkSession`'s own incidental listening `ENetHost` (bound on every non-Emscripten `ConnectToHost()` call, so a peer can be promoted to host later via migration without rebinding — see `ENetHostHandle`'s own doc comment) still accepts and fully processes `ClientHello` from *any* third party that connects to it, not just its real host — `HandleClientHello` has no "am I actually supposed to be hosting anyone" check. A rogue peer can connect directly to a client's own bound port (`ENetBackend::GetBoundPort()` is non-zero for a client-role session too) and get a real `ServerWelcomeMessage` snapshotting that peer's own roster, plus get added as a real `NetworkGamer`/fire a real `GamerJoined` event on that peer's session — despite that peer never intending to host anyone | MEDIUM | P2 | REMED-NET-001 (host-authority audit sweep) | NOT STARTED — recorded, not fixed (distinct root cause from NET-001: `ClientHello` is not one of the four host-authoritative broadcast types NET-001 covers; this is a missing role-check on the *accept* side of a client-scoped session, not a missing sender-authority check on a broadcast-only message. Confirmed real via manual reasoning about `ConnectToHost`'s own non-Emscripten `StartHosting()` call and `HandleClientHello`'s unconditional accept — not separately reproduced with a new test, since fixing/proving it is out of this task's scope; the closest existing coverage is `ClientRejectsForgedGamerLeaveBroadcastFromRogueThirdPartyPeer`, new in this task, which proves the same rogue-third-party-on-a-client-socket attack surface is real for the four NET-001 message types specifically) |
| REMED-TEST-008 | `DynamicSoundEffectInstanceTest.BufferNeededFiresExactlyTheStarvedCount` fails under a full unfiltered `ctest -j4` run but passes 10/10 in isolation (`--gtest_filter` + `--gtest_repeat=10`) — a real-time audio buffer-starvation-count assertion is timing-sensitive under heavy 4-way parallel CPU load on this sandbox. Extends `REMED-NET-001`'s already-documented `ctest -j4` transient-failure finding (previously only `ENetDiscoveryServiceTest.*` ×4 and 2 `TwoProcessLoopbackTest`/`NetworkSessionTest` cases, all network-port contention) to a second, previously-undocumented flakiness class (audio timing, not networking) | LOW | P3 | REMED-BUILD-004 (establishing a full local `ctest -j4` baseline before designing the new CI job) | NOT STARTED — recorded, not fixed (test-reliability finding, not a production defect; `REMED-BUILD-004`'s own new CI job runs `ctest` serially specifically to avoid this and the already-known network-port class, rather than allowlisting either) |
| REMED-CORE-014 | `GraphicsDeviceManager`'s private `ownsGraphicsDevice_` flag is initialized `false` in both constructors and never set `true` anywhere in `GraphicsDeviceManager.cpp` (confirmed by grep of the whole file, and independently by a live probe program: construct `Game` + `GraphicsDeviceManager(&game)`, subscribe to `getDeviceDisposingEvent()`, call `gdm->Dispose()` — the subscriber never fires). Both of `Dispose()`'s and `CreateDevice()`'s only conditional branches that raise `DeviceDisposing` and release the owned `GraphicsDevice` are therefore permanently dead code, for **every** `GraphicsDeviceManager` instance, not only the `Game`-attached case this entire codebase always constructs. This directly compounds `REMED-CORE-006`: even after `Game::Initialize()` subscribes to `DeviceDisposing` (that task's own stated fix strategy), a real `GraphicsDeviceManager::Dispose()` call on a `Game`-attached manager still would not raise the event without this dead-code path also being addressed — the CORE-lane owner should know this before scoping `REMED-CORE-006`'s fix as "just add the subscription" | HIGH | P1 | REMED-TEST-002 (investigating how to trigger `REMED-CORE-006`'s own required test, "dispose the device, assert `UnloadContent()` was called") | **DONE** — fixed atomically with `REMED-CORE-006`/`-007`, same root cause, same files. See "Wave 1 (parallel) — CORE lane" above. The `Game`-attached (non-owning) path's `DeviceDisposing` raise is no longer gated on `ownsGraphicsDevice_`; the standalone-owned-device path (still unreachable — `CreateDevice()` still throws when `game_ == nullptr`) remains a separate, unimplemented future feature, noted in that section. |
| REMED-BUILD-012 | Any test that constructs a real window via `Game`+`GraphicsDeviceManager` on the D3D12 backend crashes identically under this dev environment's Wine+vkd3d-proton setup: `wine: Unhandled page fault on read access to 0000000000000000`, backtrace bottoming out in `vkd3d_instance_get_vk_instance(instance=nullptr)` inside `dxgi_factory_CreateSwapChainForHwnd` → `d3d12_swapchain_create` → `d3d12_swapchain_init` — real window-attached DXGI swap-chain creation crashing inside vanilla Wine's own `dxgi.dll`, confirmed identically for two independent test executables (a reused `DepthStencilState` test and a reused `RasterizerState`/scissor test). `cmake/Tests/D3D12Tests.cmake`'s own pre-existing comment on `cna_diag_d3d12_swapchain` already documented this exact crash for **one specific diagnostic tool** ("DX-100's own spike..."); this generalizes it to a blanket constraint — **no** D3D12 test using the public `Game`/`GraphicsDeviceManager`/real-window API can run in this dev loop at all, only `D3D12_Smoke`'s own deliberately off-screen (`window=nullptr`), much lower-level, hand-rolled internal-`EXT`-API style avoids it | HIGH | P1 | REMED-BUILD-008 (attempting to reuse D3D11's own backend-agnostic public-API test sources for D3D12) | NOT STARTED — recorded, not fixed (a Wine/vkd3d-proton dev-environment limitation, not a CNA code defect fixable in this repo; blocks `REMED-BUILD-008`/`REMED-GFX-014`/`REMED-GFX-015` from being verified via the public-API test style every other backend uses — whoever picks these up needs `D3D12_Smoke`'s own off-screen internal-`EXT` construction style instead, a substantially larger undertaking than a simple test-source reuse) |

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

## Findings that did not reproduce

Record disproved findings here with evidence. This is a real result and improves the audit baseline's
accuracy for future work.

| ID | Investigated | Evidence it did not reproduce | Recorded by |
|---|---|---|---|
| _(none yet)_ | | | |
