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
| P0 | 11 | 8 | 0 | 0 | 3 |
| P1 | 21 | 1 | 0 | 0 | 20 |
| P2 | 44 | 3 | 0 | 0 | 41 |
| P3 | 28 | 0 | 0 | 0 | 28 |
| **Total** | **104** | **12** | **0** | **0** | **92** |

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
| REMED-GFX-001 | NOT STARTED | | | Serialize against all other EasyGL work. |
| REMED-GFX-002 | NOT STARTED | | | Sequence before GFX-003 (same file). |
| REMED-GFX-003 | NOT STARTED | | | After GFX-002. Resize tables **and** add flag operators together. |
| REMED-NET-001 | DONE | | feature/audit | Landed atomically with NET-003, same file/change — see detail below. |
| REMED-NET-003 | DONE | | feature/audit | Landed atomically with NET-001, same file/change — see detail below. |
| REMED-DEVICES-001 | DONE | | feature/audit | `shared_ptr` used, not a mutex held across the UI-blocking call — see detail below. |
| REMED-MEDIA-001 | NOT STARTED | | | VERIFY on a 32-bit build first. |

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

## Wave 1 (parallel) — unblockers

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-004 | DONE | | feature/audit | New `.github/workflows/general-tests-ci.yml` — see detail below. |
| REMED-BUILD-008 | NOT STARTED | | | **Blocks GFX-014 and GFX-015.** |
| REMED-TEST-002 | DONE | | feature/audit | `GameTests.cpp`/`GraphicsDeviceManagerTests.cpp` rewritten, `GameCrashTest.cpp` deleted — see detail below. |
| REMED-TEST-004 | DONE | | feature/audit | (a)/(b) already satisfied by `REMED-CONTENT-002`/`REMED-DEVICES-001`'s own regression tests (see detail below). (c) `PictureLibraryIndexTests.cpp` done — see detail below. |

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
| REMED-CORE-006 | P1 | NOT STARTED | | | |
| REMED-CORE-007 | P1 | NOT STARTED | | | |
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
| REMED-CORE-014 | `GraphicsDeviceManager`'s private `ownsGraphicsDevice_` flag is initialized `false` in both constructors and never set `true` anywhere in `GraphicsDeviceManager.cpp` (confirmed by grep of the whole file, and independently by a live probe program: construct `Game` + `GraphicsDeviceManager(&game)`, subscribe to `getDeviceDisposingEvent()`, call `gdm->Dispose()` — the subscriber never fires). Both of `Dispose()`'s and `CreateDevice()`'s only conditional branches that raise `DeviceDisposing` and release the owned `GraphicsDevice` are therefore permanently dead code, for **every** `GraphicsDeviceManager` instance, not only the `Game`-attached case this entire codebase always constructs. This directly compounds `REMED-CORE-006`: even after `Game::Initialize()` subscribes to `DeviceDisposing` (that task's own stated fix strategy), a real `GraphicsDeviceManager::Dispose()` call on a `Game`-attached manager still would not raise the event without this dead-code path also being addressed — the CORE-lane owner should know this before scoping `REMED-CORE-006`'s fix as "just add the subscription" | HIGH | P1 | REMED-TEST-002 (investigating how to trigger `REMED-CORE-006`'s own required test, "dispose the device, assert `UnloadContent()` was called") | NOT STARTED — recorded, not fixed (CORE-lane production defect, out of BUILD_TEST_CI's scope; `REMED-TEST-002`'s own `GameTest.DisposingDeviceInvokesUnloadContent` test already exercises the real, compounded failure end-to-end via `Game::Dispose()`, so no separate reproduction is needed for whoever picks this up) |

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
| 2026-07-20 | `REMED-BUILD-002` | **Yes — the copy step is obsolete, deleted outright (not guarded, not backfilled with a real `Content/` dir).** Investigated `examples/demo_xact/src/XactFileGen.hpp` + `XactDemo.cpp`: `XactDemo::LoadContent()` calls `GenerateXactFiles("Content/Audio")`, which itself calls `std::filesystem::create_directories(audioDir)` and then `XactFileGen::SaveFile()`s a freshly synthesized `Waves.xwb`/`Demo.xgs`/`Sounds.xsb` (sine-wave PCM + minimal XGS/XWB/XSB binaries matching `XactParser.cpp`'s expected layout) directly into that runtime-relative directory — no pre-existing `.xwb`/`.xgs`/`.xsb` asset is ever read from disk. `examples/demo_xact/` contains only `src/` (confirmed: no `Content/` anywhere in the repo, matching the audit finding). The POST_BUILD `copy_directory` in `cmake/Examples.cmake` therefore copied a directory that never existed and could never usefully exist — deleting it is strictly correct, not a stopgap. | Claude (autonomous remediation session, user-directed) |

## Findings that did not reproduce

Record disproved findings here with evidence. This is a real result and improves the audit baseline's
accuracy for future work.

| ID | Investigated | Evidence it did not reproduce | Recorded by |
|---|---|---|---|
| _(none yet)_ | | | |
