# MASTER_REMEDIATION_PLAN.md — CNA Post-Audit Remediation

**Status: COMPLETE (planning only). No production code modified.**
**Source baseline:** `audit/` at commit `74ebf356` (branch `feature/audit`), frozen.

This is the authoritative, deduplicated task list derived from the full repository-wide audit: 2297
per-file reports, `AUDIT_FINDINGS_INDEX.md`, `AUDIT_CROSS_CUTTING_FINDINGS.md`,
`AUDIT_GRAPHICS_BACKEND_MATRIX.md`, `AUDIT_FINAL_REPORT.md`, and the Pass 3/4/5/6 synthesis.

## How to read a task

Every task carries the same field set. Fields that do not apply say `N/A` rather than being omitted,
so a missing field always means an error in this document, never "not relevant."

- **Severity** — the audit's own grading (CRITICAL / HIGH / MEDIUM / LOW), preserved as-is.
- **Priority** — this plan's scheduling decision (P0–P3). *Deliberately not the same as severity.*
  A cheap fix with huge blast radius outranks a severe but isolated one when it unblocks later work.
- **Parallel safe** — `YES` (no file overlap with any other task), `NO` (shares files; must serialize),
  `CONDITIONAL` (safe only under the stated constraint).
- **Verification required** — `YES` when the finding rests on static analysis that was never executed.
  Reproduce before fixing. A finding that fails to reproduce is a valid, recordable outcome.

## ID scheme

`REMED-<GROUP>-<NNN>`, stable and never reused. Groups:

| Group | Owner lane | Scope |
|---|---|---|
| `CORE` | CORE | `Microsoft::Xna::Framework` core, math types, `Game`/`GraphicsDeviceManager`/`GameWindow`, `CNA::Logger`, project-wide conventions |
| `GFX` | GRAPHICS | `Microsoft::Xna::Framework::Graphics`, `IGraphicsBackend`, all 14 backends, all shaders |
| `CONTENT` | CONTENT | `Content`/`Storage`/`Xnb` readers, asset path handling |
| `NET` | NET | `Net`, `CNA::Internal::Net`, `GamerServices` |
| `MEDIA` | MEDIA | `Media`, `CNA::Internal::Media` |
| `DEVICES` | DEVICES | `Microsoft::Devices`, `cna-devices` |
| `AUDIO` | AUDIO | `Microsoft::Xna::Framework::Audio` |
| `BUILD` | BUILD_TEST_CI | CMake, CI workflows, build config, repo hygiene |
| `TEST` | BUILD_TEST_CI | Test-suite defects, coverage gaps |
| `DOCS` | BUILD_TEST_CI | Source comments and `docs/*.md` accuracy |

`TEST` and `DOCS` are sub-groups of the single `BUILD_TEST_CI` owner lane, split only for ID
readability. They are never assigned to a different owner.

**No `INPUT` lane exists.** The audit's `Input` namespace sweep (44 files, plus a full member-level
xn65 cross-check) produced exactly two LOW documentation-framing notes and one NOXNA tagging nit —
not enough to justify a lane. Those are folded into `REMED-CORE-012` and `REMED-CORE-013`. The
`AUDIO` lane is retained but is similarly thin (2 LOW tasks); see `REMEDIATION_DEPENDENCIES.md` § Lanes.

---

# P0 — Security, memory corruption, undefined behavior, and build/test blockers

P0 contains two distinct kinds of work, both scheduled first for different reasons:

- **P0-SAFETY** — genuine security/memory-safety defects (the severity case).
- **P0-LEVERAGE** — cheap fixes that make all later verification trustworthy (the blast-radius case).
  These are not severe in themselves. They are first because **every task below them is unverifiable
  until they land.**

---

## REMED-BUILD-001 — `gtest_discover_tests(CnaTests)` has no `WORKING_DIRECTORY`, silently breaking ~220 tests

- **Severity:** HIGH
- **Priority:** P0-LEVERAGE — **the single first task in the entire plan**
- **Owner:** BUILD_TEST_CI
- **Status:** NOT STARTED
- **Root cause:** `cmake/UnitTests.cmake:215` calls `gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)`
  with no `WORKING_DIRECTORY` argument. CMake defaults it to the target's runtime output directory
  (`cmake-build-debug/`), not the repo root where `tests/assets/**` lives.
- **Evidence:** Confirmed in the generated `cmake-build-debug/CnaTests[1]_tests.cmake` — all 5507
  discovered cases have `WORKING_DIRECTORY .../cmake-build-debug` baked in. Direct-binary run from the
  repo root: **5503 passed / 4 skipped / 0 failed**. Same binary under `ctest`: **229 failed**.
  Independently root-caused twice by two separately-dispatched investigations.
- **Audit references:** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Pass 6 … ROOT CAUSE, HIGH severity";
  `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_FINAL_REPORT.md` §5, §8 item 2.
- **Affected files:** `cmake/UnitTests.cmake`
- **Affected backends:** ALL (backend-independent)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** None directly. Indirect and severe: FNA-parity regressions in
  Media/Xnb/Content/ENet/Lzx are currently undetectable by CI.
- **Security impact:** Indirect — `XnbContainerFuzzTest` and `LzxDecoderFuzzTest`, the project's two
  adversarial-input harnesses, are among the affected tests. Security regressions are currently invisible.
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Add `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` to the
  `gtest_discover_tests` call, matching the pattern already correct in
  `cmake/Tests/EasyGLTests.cmake` / `VulkanTests.cmake`. One line.
- **Required tests:** None new. The fix *is* restoring ~220 existing tests.
- **Required regression tests:** Add a CI assertion that an unfiltered `ctest` run discovers and
  passes the full default set, so this cannot silently regress again.
- **Required backend parity checks:** Re-run the full suite on at least EasyGL, Vulkan, SdlGpu,
  Bgfx to establish a trustworthy post-fix baseline.
- **Dependencies:** None. **Nothing depends on it technically; everything depends on it evidentially.**
- **Estimated complexity:** TRIVIAL (1 line) — but expect follow-on work triaging tests that now
  genuinely run for the first time.
- **Parallel safe:** YES
- **Verification required:** NO (already empirically confirmed both ways)
- **Completion criteria:** Unfiltered `ctest` from a clean build directory reports the same
  pass/fail set as the direct-binary run, ±only genuinely-failing tests.
- **Verification criteria:** `ctest --test-dir <build>` and `./<build>/CnaTests` agree on the
  pass/fail status of every test in the default set.

---

## REMED-BUILD-002 — `cna_demo_xact` POST_BUILD copy aborts the top-level build on every backend

- **Severity:** HIGH
- **Priority:** P0-LEVERAGE
- **Owner:** BUILD_TEST_CI
- **Status:** NOT STARTED
- **Root cause:** `cmake/Examples.cmake` registers an unconditional `POST_BUILD` step copying
  `examples/demo_xact/Content`, which does not exist anywhere in the repository
  (`examples/demo_xact/` contains only `src/`).
- **Evidence:** Independently hit by 6 backend builds this audit session (EasyGL, Bgfx, SdlRenderer,
  Software, Ascii, Headless), each initially dismissing it as "pre-existing, unrelated" before the
  Bgfx pass root-caused it precisely. It silently broke the audit's own earlier EasyGL build, masked
  because `cmake --build ... | tail -60` reports the pipe's exit code, not the build's.
- **Audit references:** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Bgfx: build+test complete …";
  `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_FINAL_REPORT.md` §5.
- **Affected files:** `cmake/Examples.cmake`; possibly `examples/demo_xact/`
- **Affected backends:** ALL (target is backend-agnostic)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** N/A
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Determine first whether `XactFileGen.hpp` already generates
  this demo's XACT content at runtime — if so the copy step is simply obsolete and should be deleted.
  Otherwise guard with `if(EXISTS ...)` or add the real `Content` directory. **Do not** reflexively
  add an empty directory; establish which of the three is actually correct.
- **Required tests:** None.
- **Required regression tests:** A CI job that runs an unfiltered top-level `cmake --build` **without**
  piping through `tail`, so a non-zero exit code is actually observed.
- **Required backend parity checks:** Confirm a clean unfiltered top-level build on ≥3 backends.
- **Dependencies:** None.
- **Estimated complexity:** TRIVIAL–SMALL (depends on which of the 3 resolutions is correct)
- **Parallel safe:** YES
- **Verification required:** NO (universally reproduced)
- **Completion criteria:** `cmake --build <dir>` completes with exit code 0 on every backend, with no
  output pipe masking the result.
- **Verification criteria:** `echo $?` is 0 after an unpiped full build on ≥3 backends.

---

## REMED-CONTENT-001 — Malformed `Texture2D` `.xnb` crashes the process (Vulkan stack smashing, WebGPU Rust panic)

- **Severity:** CRITICAL (the audit's single most severe finding)
- **Priority:** P0-SAFETY — **highest-severity item in the plan**
- **Owner:** CONTENT (single owner; the fix is shared, NOT per-backend)
- **Status:** NOT STARTED
- **Root cause:** XNB-decoded `width` / `height` / `mipLevels` are trusted and passed directly into a
  native GPU API with no CNA-side sanity check against real device limits. The native APIs' own
  validation does not substitute: Vulkan's validation layer is **advisory** (RADV proceeds anyway),
  and wgpu-native validates `baseMipLevel` **lazily at `wgpuQueueSubmit()` time**, past the existing
  `nullptr` check on `wgpuTextureCreateView()`.
- **Evidence:** `XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly` —
  a test whose entire stated purpose is guaranteeing this never happens — crashes the process 100%
  reproducibly, in isolation, on two backends:
  - **Vulkan:** `*** stack smashing detected ***: terminated`, with the validation layer reporting
    `extent.width=16777217`, `mipLevels=25` against a 15-level maximum.
  - **WebGPU:** `thread '<unnamed>' panicked … panic in a function that cannot unwind … aborting`
    across the wgpu-native FFI boundary — **non-catchable**.
  - **Confirmed clean on EasyGL** (full-suite run, 0 failures).
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` § CRITICAL; `AUDIT_CROSS_CUTTING_FINDINGS.md`
  § "HEADLINE, CRITICAL/HIGH"; `AUDIT_FINAL_REPORT.md` §5 headline, §8 item 1.
- **Affected files:** `src/CNA/Internal/Xnb/Texture2DContentTypeReader.cpp`;
  `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`;
  symptom sites `src/CNA/Internal/Backends/Vulkan/VulkanTextureBackend.*`,
  `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` (`GenerateMipsForLayer()`)
- **Affected backends:** Vulkan, WebGPU **confirmed**; EasyGL confirmed clean. D3D9/D3D11/D3D12/Bgfx/
  SdlGpu/SdlRenderer/Software/Ascii/Headless/Canvas/Dx3 **not yet isolated** — true blast radius unknown.
- **Affected platforms:** ALL platforms running an affected backend.
- **XNA/FNA compatibility impact:** FNA/XNA fail cleanly with a catchable content-load exception.
  Crashing the process is a hard divergence.
- **Security impact:** **HIGH — real crash-DoS.** Reachable by any application loading a corrupted,
  truncated, or maliciously-crafted `Texture2D` asset from disk, a mod directory, or network transfer.
  On WebGPU the panic is not catchable, so no application-level mitigation is possible.
- **Memory/resource safety impact:** **Confirmed stack corruption on Vulkan.** Stack smashing is
  detected, not merely suspected — this is memory corruption, not just a crash.
- **Suggested implementation strategy:** Validate decoded `width`/`height`/`mipLevels` immediately
  after XNB decode and **before any backend-specific texture creation**, in shared
  `Texture2DContentTypeReader` / `Texture2D` construction code. Reject with the same allowlisted
  `ContentLoadException` every other malformed-input case in this fuzz test already expects.
  Validate against the device's real reported limits, not a hardcoded constant.
  **Explicitly do not fix this per-backend** — a shared fix closes it on all 14 at once, and a
  per-backend fix would leave the 11 unisolated backends exposed.
- **Required tests:** Direct unit tests for the new validation: zero/negative dimensions, dimensions
  exceeding device limits, `mipLevels` exceeding `log2(max(w,h))+1`, and integer-overflow-inducing
  dimension/format-size products.
- **Required regression tests:** `XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly`
  must pass on **every** backend, not just the two where the crash was isolated.
- **Required backend parity checks:** Re-run that exact fuzz test in isolation on all 14 backends to
  establish the true blast radius — both before the fix (to measure) and after (to prove closure).
- **Dependencies:** `REMED-BUILD-001` (the fuzz test is one of the ~220 broken by the CTest bug —
  without that fix, CI cannot even observe this test's result).
- **Estimated complexity:** MEDIUM — the validation is simple; sourcing real per-device limits into
  shared content-reading code is the actual design work.
- **Parallel safe:** CONDITIONAL — safe if implemented purely in shared content/`Texture2D` code.
  Becomes `NO` the moment anyone touches backend texture code, which conflicts with `REMED-GFX-*`.
- **Verification required:** NO (reproduced live on both backends)
- **Completion criteria:** The fuzz test passes on all 14 backends; malformed dimensions produce a
  catchable `ContentLoadException` rather than any crash, abort, or panic.
- **Verification criteria:** 1500 fuzz rounds complete with zero process terminations on every
  backend, plus explicit negative tests proving the exception type and message.

---

## REMED-CONTENT-002 — `fs::path` concatenation pitfall: three path-containment bypasses, one shared root cause

- **Severity:** HIGH
- **Priority:** P0-SAFETY
- **Owner:** CONTENT (single owner — **do not** split across Storage/Content/Media lanes)
- **Status:** NOT STARTED
- **Root cause:** `std::filesystem::path::operator/` silently **discards the left-hand base** when the
  right-hand operand is absolute, and does not reject `..` components. Three independent subsystems
  build paths from caller- or file-supplied strings this way without a containment check. The audit
  explicitly recommends a project-wide grep for this exact shape — it is one pitfall, not three bugs.
- **Evidence:** Three confirmed instances:
  1. **`StorageDevice::DeleteContainer()`** (`StorageDevice.cpp:194-199`) — `fs::remove_all(fs::path(EnsureStorageRoot()) / titleName)`.
     `DeleteContainer("../../../SomeOtherAppData")` or any absolute path resolves outside the storage
     root and is **recursively deleted**. **Not FNA-faithful:** FNA's own `DeleteContainer` is
     `throw new NotImplementedException();` (`StorageDevice.cs:349-352`) — CNA chose to implement it,
     and implemented it unsafely. The audit calls this "arguably the most severe finding of this session."
  2. **`ContentReader::ReadExternalReference<T>()`** — `ResolveRelativeAssetPath()` (`ContentReader.cpp:25-49`)
     rejects only a resolved path exactly equal to `".."` or beginning with `"../"`. An **absolute**
     external reference (e.g. `/etc/passwd`) in a crafted `.xnb`/`.cnj` passes through unchanged, and
     `ContentManager::BuildAssetPath()` does not re-contain it. This **directly contradicts the method's
     own doc comment**, which promises such paths are "rejected outright."
  3. **`PlaylistParser.cpp`** — no containment check on `.m3u`/`.m3u8` entries; accepts absolute and
     `..`-escaping paths as-is.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer (all three);
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Production correctness bugs outside the graphics-backend layer"
  and § Security/adversarial-input hardening;
  `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`;
  `include/Microsoft/Xna/Framework/Content/ContentReader.hpp.audit.md`;
  `src/CNA/Internal/Media/PlaylistParser.cpp.audit.md`.
- **Affected files:** `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`;
  `src/Microsoft/Xna/Framework/Content/ContentReader.cpp`;
  `include/Microsoft/Xna/Framework/Content/ContentReader.hpp`;
  `src/CNA/Internal/Media/PlaylistParser.cpp`; plus a new shared containment utility.
- **Affected backends:** N/A (backend-independent)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** Mixed, and this distinction matters for the fix:
  - `StorageDevice::DeleteContainer` — **CNA-introduced**, no FNA behavior to preserve. Fix freely.
  - `ContentReader::ReadExternalReference` — CNA's containment check is a **disclosed addition** FNA
    does not have; completing it diverges further from FNA in the safe direction. Acceptable and correct.
  - `PlaylistParser` — absolute paths are standard M3U behavior; tightening is a deliberate
    security-over-compatibility choice that should be **explicitly documented**, not silent.
  - **Do not "fix" `StorageContainer`'s equivalent joins** — those are confirmed FNA-faithful
    (FNA's own `StorageContainer.cs` uses unchecked `Path.Combine` for every equivalent method).
- **Security impact:** **HIGH.** (1) is a path-traversal-enabled **data-loss** vulnerability reachable
  from public XNA API — it deletes, it does not merely read. (2) is arbitrary-file-read via crafted
  asset. (3) lets a hostile playlist make the engine open and decode any readable file.
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Write **one** shared containment helper (reject absolute
  RHS; lexically normalize; verify the result is still under the base) and apply it at all three
  sites. Then grep the whole repo for `fs::path(...) / <caller-supplied string>` lacking a preceding
  `is_absolute()` rejection and audit every hit — the three known instances were each found
  incidentally, so the true count is likely higher.
  `CnjSourceFile.hpp` and `SavedPictureStore.cpp` already contain working containment patterns; use
  them as the reference implementation rather than inventing a fourth convention.
- **Required tests:** For the shared helper: absolute RHS, `..` traversal, `.`-only, empty, symlink
  escape, and a Windows drive-letter/UNC case. For each of the 3 call sites: one absolute-escape and
  one `..`-escape rejection test.
- **Required regression tests:** `ContentReaderExternalReferenceTests.cpp` currently tests **only**
  relative `..` escapes and would not have caught (2) — see `REMED-TEST-004`. Add the absolute-path case.
- **Required backend parity checks:** N/A
- **Dependencies:** `REMED-BUILD-001` (the Content/Storage tests are among the ~220 currently broken).
- **Estimated complexity:** MEDIUM (helper + 3 sites + repo-wide sweep)
- **Parallel safe:** CONDITIONAL — the shared helper must land before, or atomically with, the three
  call-site changes. Do not let three lanes each write their own helper.
- **Verification required:** NO (all three traced to specific lines)
- **Completion criteria:** All three sites reject absolute and `..`-escaping paths; the repo-wide
  sweep is complete and every hit is either fixed or explicitly recorded as intentionally FNA-faithful.
- **Verification criteria:** A test proves `DeleteContainer("../../../x")` deletes nothing and throws;
  a crafted `.xnb` with an absolute external reference is rejected; the sweep's hit list is documented
  in `REMEDIATION_PROGRESS.md`.

---

## REMED-CONTENT-003 — `TextureCubeContentTypeReader` missing byte-count validation → OOB heap read

- **Severity:** HIGH
- **Priority:** P0-SAFETY
- **Owner:** CONTENT
- **Status:** NOT STARTED
- **Root cause:** For an uncompressed `SurfaceFormat::Color` face/level, the file's own declared
  `byteCount` (fully attacker-controlled, independent of `faceSize`) sizes the `bytes` buffer, which
  is then indexed via unchecked `std::vector::operator[]` in the pixel-unpacking loop with no
  `bytes.size() != pixelCount*4` guard. Both sibling readers
  (`Texture2DContentTypeReader.cpp`, `Texture3DContentTypeReader.cpp`) **do** have this check — a
  clear porting omission, not an intentional scope difference.
- **Evidence:** `src/CNA/Internal/Xnb/TextureCubeContentTypeReader.cpp.audit.md`; side-by-side
  comparison of the three sibling readers makes the gap obvious. The compressed DXT path is
  unaffected (`DxtUtil`'s own bounds checks guarantee a fixed decompressed size or a clean throw).
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "HIGH: `TextureCubeContentTypeReader.cpp` is missing…".
- **Affected files:** `src/CNA/Internal/Xnb/TextureCubeContentTypeReader.cpp`
- **Affected backends:** ALL (shared CPU-side reader)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** None — this is a hardening gap, not a behavioral divergence.
- **Security impact:** **HIGH.** A crafted `.xnb` TextureCube asset with an undersized declared byte
  count triggers an out-of-bounds heap read: crash, or **heap-memory disclosure** via pixels uploaded
  to the GPU and potentially read back.
- **Memory/resource safety impact:** Confirmed OOB read.
- **Suggested implementation strategy:** Port the sibling readers' existing check verbatim. Do not
  invent a new validation shape — consistency across the three readers is itself the fix.
- **Required tests:** Crafted-fixture tests: undersized `byteCount`, oversized `byteCount`, zero,
  and a value chosen to overflow `pixelCount*4`.
- **Required regression tests:** Extend `XnbContainerFuzzTest` to cover mutated **TextureCube**
  fixtures, not only `Texture2D` — the existing fuzz coverage is why this was never caught.
- **Required backend parity checks:** N/A (CPU-side).
- **Dependencies:** `REMED-BUILD-001`. Shares a theme (and reviewer) with `REMED-CONTENT-001`;
  no file overlap.
- **Estimated complexity:** SMALL
- **Parallel safe:** YES
- **Verification required:** NO
- **Completion criteria:** Undersized/oversized declared byte counts throw a clean
  `ContentLoadException`; all three sibling readers share an identical validation shape.
- **Verification criteria:** ASan-instrumented run of the new crafted-fixture tests reports no
  heap-buffer-overflow.

---

## REMED-GFX-001 — EasyGL: constructor failure after `RegisterForWindow()` leaves a dangling registry entry (use-after-free)

- **Severity:** HIGH (flagged in the findings index as "the most severe confirmed finding in this audit")
- **Priority:** P0-SAFETY
- **Owner:** GRAPHICS
- **Status:** NOT STARTED
- **Root cause:** `EasyGLGraphicsBackend`'s constructor calls `RegisterForWindow()` **before**
  `SDL_GL_CreateContext`, which can throw. A constructor that throws never runs the destructor, so
  the unregister never happens — leaving a dangling pointer in a **static** window registry, later
  dereferenced unconditionally by `SdlInputBridge.cpp` / `Mouse.cpp` on the next mouse/input event.
- **Evidence:** `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md` F1. All four
  `RegisterForWindow` callers were checked: **only EasyGL** has this ordering. WebGPU is the model
  example (every fallible step wrapped in `try`, `RegisterForWindow` last, full `catch(...)` releasing
  every resource before rethrow); Canvas and SdlGpu also register last.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends; `AUDIT_CROSS_CUTTING_FINDINGS.md`
  § Architecture ("CONFIRMED LIVE BUG"); `AUDIT_GRAPHICS_BACKEND_MATRIX.md` `RegisterForWindow` row;
  `AUDIT_FINAL_REPORT.md` §2 item 10.
- **Affected files:** `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`;
  `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (registry contract);
  dereference sites `src/CNA/Internal/.../SdlInputBridge.cpp`, `Mouse.cpp`
- **Affected backends:** EasyGL only (confirmed). WebGPU/Canvas/SdlGpu confirmed clean; D3D11/D3D12/
  Bgfx/Vulkan/D3D9 do not call `RegisterForWindow` at all. Ascii/Software/Headless/SdlRenderer/Dx3
  not re-checked for this specific call.
- **Affected platforms:** ALL platforms using EasyGL (the **default backend on Linux and Emscripten**).
- **XNA/FNA compatibility impact:** N/A (CNA-internal architecture)
- **Security impact:** Use-after-free is exploitable in principle, though the trigger (a failing
  GL context creation) is not attacker-controlled in normal operation.
- **Memory/resource safety impact:** **Confirmed reachable use-after-free.**
- **Suggested implementation strategy:** Move `RegisterForWindow()` to be the **last** statement of
  the constructor, after every fallible step — matching WebGPU/Canvas/SdlGpu. Additionally consider
  hardening `IGraphicsBackend`'s registry contract itself (RAII registration guard) so the ordering
  cannot be got wrong again by a future backend; and add null-checks at the `SdlInputBridge`/`Mouse`
  dereference sites as defence in depth.
- **Required tests:** A test that forces constructor failure after the current registration point
  and then dispatches a mouse event, asserting no crash. Use the existing backend-injection seam.
- **Required regression tests:** Extend to all `RegisterForWindow` callers so a future backend
  reintroducing the ordering is caught.
- **Required backend parity checks:** Re-check Ascii, Software, Headless, SdlRenderer, Dx3 — the four
  the audit did not re-verify for this specific call.
- **Dependencies:** None. Related in *shape* to `REMED-GFX-027` (state-mutation-before-fallible-call)
  but a different file and fix.
- **Estimated complexity:** SMALL (reorder) / MEDIUM (if the RAII registry hardening is included)
- **Parallel safe:** CONDITIONAL — `NO` against any other task touching
  `EasyGLGraphicsBackend.cpp` (notably `REMED-GFX-006`, `REMED-GFX-016`). EasyGL is a single
  4733-line file; serialize all EasyGL work.
- **Verification required:** NO (traced to specific ordering)
- **Completion criteria:** Registration is the last constructor action on every backend that
  registers; a forced-failure test dispatches input events without crashing.
- **Verification criteria:** ASan build passes the forced-failure test with no use-after-free report.

---

## REMED-GFX-002 — `SpriteFont::MeasureString` / `SpriteBatch::DrawString` dereference `unordered_map::end()`

- **Severity:** HIGH
- **Priority:** P0-SAFETY
- **Owner:** GRAPHICS
- **Status:** NOT STARTED
- **Root cause:** Both functions' "character not found → fall back to `defaultCharacter_`" path
  performs a second `characterIndexMap_.find()` and dereferences `it->second` **without checking that
  the second lookup succeeded.** Nothing anywhere validates that a `SpriteFont`'s `defaultCharacter`
  is itself present in `characters`, and `setDefaultCharacterProperty` performs no validation.
- **Evidence:** `SpriteFont.cpp:101-111`, `SpriteBatch.cpp:457-465`. Found via
  `examples/sprite_font_test.cpp`, which sets a `DefaultCharacter` absent from the map — one call
  short of triggering it.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § `Microsoft::Xna::Framework::Graphics`;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-graphics HIGH findings; `AUDIT_FINAL_REPORT.md` §2 item 11.
- **Affected files:** `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp`;
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
- **Affected backends:** ALL (XNA-facing shared layer — not a backend bug)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** **Direct divergence.** FNA's equivalent
  (`characterIndexMap[DefaultCharacter.Value]`, a C# `Dictionary` indexer) throws a clean, catchable
  `KeyNotFoundException`. CNA has undefined behavior. Fix should throw
  `System::Collections::Generic::KeyNotFoundException` to match.
- **Security impact:** UB reachable through fully public API with attacker-influenceable text input
  (e.g. drawing user-supplied strings) — treat as a real exposure, not theoretical.
- **Memory/resource safety impact:** Dereferencing an end() iterator is undefined behavior.
- **Suggested implementation strategy:** Check the second `find()` result; throw
  `KeyNotFoundException` when it fails, matching FNA. Additionally validate `defaultCharacter` at
  `SpriteFont` construction and in `setDefaultCharacterProperty`, so the invariant is enforced at
  the point it is violated rather than at use.
- **Required tests:** `SpriteFont` constructed with a `defaultCharacter` absent from `characters`,
  then `MeasureString` and `DrawString` on a string with a missing glyph — both must throw
  `KeyNotFoundException`, tested separately for each method.
- **Required regression tests:** Extend `examples/sprite_font_test.cpp` (which already sets up the
  precondition) to take the final step.
- **Required backend parity checks:** None — but confirm via the shared `RecordingSpriteBatchBackend.hpp`
  test double that no backend masks the throw.
- **Dependencies:** Coordinate with `REMED-CORE-002` (exception-type sweep) — this fix introduces a
  `System::*Exception` at a site the sweep will also touch.
- **Estimated complexity:** SMALL
- **Parallel safe:** CONDITIONAL — `NO` against `REMED-GFX-003` (same `SpriteBatch.cpp`) and
  `REMED-CORE-002`. Sequence GFX-002 → GFX-003.
- **Verification required:** NO
- **Completion criteria:** Both methods throw `KeyNotFoundException` instead of invoking UB;
  construction-time validation rejects an inconsistent `defaultCharacter`.
- **Verification criteria:** UBSan/ASan build passes the new tests; behavior matches FNA's documented
  exception type.

---

## REMED-GFX-003 — `SpriteBatch::DrawString` axis-direction tables are undersized for the composable `SpriteEffects` flag enum (OOB stack read)

- **Severity:** HIGH
- **Priority:** P0-SAFETY
- **Owner:** GRAPHICS
- **Status:** NOT STARTED
- **Root cause:** Real XNA's `SpriteEffects` is a composable `[Flags]` enum with a valid 4th combined
  value (`FlipHorizontally|FlipVertically`). CNA's four lookup tables in `DrawString` are sized for 3
  entries; FNA's own `SpriteBatch.cs` declares them with 4 specifically to handle this. `effIdx=3`
  reads past the end of all four `constexpr` arrays.
- **Evidence:** `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp.audit.md`;
  `include/Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp.audit.md`. CNA's `SpriteEffects` lacks
  the `operator|` overload other flag enums in this codebase have (e.g. `GestureType`), but this does
  **not** prevent the value being constructed — this codebase already does it via `static_cast` at
  `examples/sdlgpu_2d_test.cpp:126`.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § `Microsoft::Xna::Framework::Graphics`;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-graphics HIGH findings.
- **Affected files:** `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`;
  `include/Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp`
- **Affected backends:** ALL (XNA-facing shared layer)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** Two real gaps: the missing 4th table entry, and the missing
  `operator|` that makes the combined value awkward to construct legitimately. Both are FNA/XNA parity
  items. Adding `operator|`/`&`/`|=`/`&=` matches the convention `GestureType` already follows.
- **Security impact:** Out-of-bounds stack read with attacker-influenceable index if `SpriteEffects`
  ever originates from data.
- **Memory/resource safety impact:** Confirmed OOB read of a `constexpr` stack array.
- **Suggested implementation strategy:** Resize all four tables to 4 entries with FNA's values, and
  add the flag-enum operators to `SpriteEffects.hpp`. Adding the operators without resizing the tables
  makes the bug **easier** to hit — do both, in one change.
- **Required tests:** `DrawString` with the combined `FlipHorizontally|FlipVertically` value, asserting
  correct doubly-flipped glyph placement (not merely "does not crash"). Test the new operators directly.
- **Required regression tests:** Add the combined value to any existing `SpriteEffects`-parameterized test.
- **Required backend parity checks:** Verify the combined value renders identically on ≥3 backends.
- **Dependencies:** `REMED-GFX-002` (same file — sequence after it).
- **Estimated complexity:** SMALL
- **Parallel safe:** NO (shares `SpriteBatch.cpp` with `REMED-GFX-002`)
- **Verification required:** NO
- **Completion criteria:** Tables have 4 entries matching FNA; `SpriteEffects` has the standard flag
  operators; the combined value renders correctly.
- **Verification criteria:** ASan/UBSan build passes the combined-value test with no OOB report.

---

## REMED-NET-001 — `ENetBackend::HandleReceive()` accepts host-only broadcast messages from any peer

- **Severity:** HIGH
- **Priority:** P0-SAFETY
- **Owner:** NET
- **Status:** NOT STARTED
- **Root cause:** `peer` is passed to `HandleClientHello`/`HandleConnect`/`HandleDisconnect`/
  `HandleAppData`, but **not** to the four broadcast-only handlers
  (`ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast`). Those handlers
  therefore cannot check, and do not check, that the sender is this session's authoritative host.
- **Evidence:** `src/CNA/Internal/Net/ENetBackend.cpp.audit.md`. Any connected peer — using a
  modified or custom ENet client speaking this fully-inferable wire format, **no MITM or spoofing
  needed** — can forge these directly to the host and:
  - kick arbitrary gamers (forged `GamerLeaveBroadcastMessage`)
  - inject fake gamers (forged `GamerJoinBroadcastMessage`)
  - corrupt the host's own wire-id assignment (forged `ServerWelcomeMessage`)
  - force an arbitrary `NetworkEventType::StateChange`
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "HIGH: `ENetBackend.cpp`'s `HandleReceive()`…".
- **Affected files:** `src/CNA/Internal/Net/ENetBackend.cpp`
- **Affected backends:** N/A
- **Affected platforms:** ALL with networking enabled
- **XNA/FNA compatibility impact:** None — FNA has **no** `Net` namespace at all, so there is no
  upstream behavior to match. This is CNA-original code and can be hardened freely.
- **Security impact:** **HIGH — remote, unauthenticated, no MITM required.** This is the plan's most
  straightforwardly adversarial finding. Explicitly orthogonal to the subsystem's extensive prior
  remediation history (`audit_net.md`, a dozen-plus fixes), **none of which considered a
  modified-client threat model.**
- **Memory/resource safety impact:** N/A (logic/authorization, not memory)
- **Suggested implementation strategy:** Thread `peer` into all four broadcast handlers and reject
  any message whose sending peer is not the session's authoritative host. Treat rejection as a
  first-class protocol event (log it; consider disconnecting the peer), not a silent drop.
  While here, address the related lower-severity gap tracked as `REMED-NET-003`
  (`HandleClientHello` has no per-peer resend guard, allowing unbounded fake-gamer injection).
- **Required tests:** One forged-message test per message type, asserting the host's roster/state is
  unchanged and the forgery is rejected. `ENetBackendTests.cpp` (2083 lines, described in the audit as
  reference-quality) is the right home.
- **Required regression tests:** A negative test proving a **legitimate** host broadcast still
  succeeds — the authorization check must not break normal operation.
- **Required backend parity checks:** N/A
- **Dependencies:** `REMED-BUILD-001` (the ENet tests are among the ~220 currently broken).
  Do `REMED-NET-003` in the same change.
- **Estimated complexity:** MEDIUM (threading `peer` through, plus deciding the rejection policy)
- **Parallel safe:** CONDITIONAL — `NO` against `REMED-NET-003` (same file, and they should be one
  change); `YES` otherwise.
- **Verification required:** NO (traced to specific handler signatures)
- **Completion criteria:** All four broadcast handlers verify sender authority; forged messages from
  a non-host peer are rejected and logged.
- **Verification criteria:** Forgery tests pass; the two-process loopback harness confirms legitimate
  host broadcasts still work end to end.

---

## REMED-DEVICES-001 — `FileDialog` / `MessageBox`: mutex released before the returned backend pointer is used (use-after-free)

- **Severity:** HIGH
- **Priority:** P0-SAFETY
- **Owner:** DEVICES
- **Status:** NOT STARTED
- **Root cause:** Both files implement the same "swappable global backend for test injection" pattern:
  ```cpp
  IFileDialogBackend* GetBackend() {
      std::lock_guard<std::mutex> lock(BackendMutex());
      return BackendStorage().get();   // lock released HERE, raw pointer returned
  }
  ```
  Every public entry point then calls `GetBackend()->ShowOpenFile(...)`. The mutex is released the
  instant `GetBackend()` returns — **before** the pointer is dereferenced. A concurrent
  `SetBackendForTesting()` reassigns the owning `unique_ptr`, destroying the old object while the
  first thread is still calling through it.
- **Evidence:** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Recurring memory/resource risk patterns" —
  confirmed in both files. The mutex correctly protects the `unique_ptr`'s own read/write, but **not
  the pointee's lifetime across the subsequent virtual call.**
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer;
  `AUDIT_FINAL_REPORT.md` §2 item 8.
- **Affected files:** `FileDialog.cpp`, `MessageBox.cpp` (both in `cna-devices`)
- **Affected backends:** N/A
- **Affected platforms:** ALL with `CNA_DEVICES` enabled
- **XNA/FNA compatibility impact:** N/A (NOXNA extension)
- **Security impact:** Use-after-free; low real-world reachability given the documented test-only
  intent of `SetBackendForTesting()`, but the synchronization as written does not actually enforce
  that intent.
- **Memory/resource safety impact:** **Confirmed use-after-free window.**
- **Suggested implementation strategy:** Either hold the lock for the duration of the actual backend
  call, or make `BackendStorage()` a `shared_ptr` and return/hold a local copy so the pointee outlives
  the lock scope. **Prefer the `shared_ptr` approach** — holding a mutex across a UI-blocking call
  (a file dialog!) invites deadlock. `SystemTray`/`Camera` avoid this entirely via per-instance
  constructor-injected backends; consider migrating to that design as the real fix.
  Note the sibling `VibrateController` already does the "hold the lock for the whole call" variant
  correctly, and the `Microsoft::Devices::Sensors` subsystem does **not** share this bug.
- **Required tests:** A concurrent test racing `SetBackendForTesting()` against a live call —
  neither `FileDialogTests.cpp` nor `MessageBoxTests.cpp` currently exercises this (see `REMED-TEST-004`).
- **Required regression tests:** Run the new tests under TSan; this project already has a
  `devices-tsan` CMake preset.
- **Required backend parity checks:** N/A
- **Dependencies:** None.
- **Estimated complexity:** SMALL–MEDIUM (larger if migrating to constructor injection)
- **Parallel safe:** YES
- **Verification required:** NO (traced to specific mutex scoping)
- **Completion criteria:** The backend object's lifetime is guaranteed for the duration of every call
  through it, in both files.
- **Verification criteria:** TSan/ASan run of the new concurrent test is clean.

---

## REMED-MEDIA-001 — `AudioTagParser` bounds checks vulnerable to unsigned integer overflow (32-bit targets)

- **Severity:** HIGH (narrow platform scope)
- **Priority:** P0-SAFETY
- **Owner:** MEDIA
- **Status:** NOT STARTED
- **Root cause:** ID3v2.3 and FLAC-picture-block length validation uses `pos + len > bound`-style
  checks. On a 32-bit `size_t` target, `pos + len` can wrap around, making the check pass for a
  length that is actually out of bounds.
- **Evidence:** `src/CNA/Internal/Media/AudioTagParser.cpp.audit.md`. Contrast with the sibling
  `XactParser.cpp` in the same shard, **explicitly hardened against this exact overflow class** per a
  cited external audit (`AUDIO-PARSER-001`) — the two files show different hardening maturity against
  an identical vulnerability class, which is itself the useful signal here.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "HIGH (32-bit `size_t` targets only)".
- **Affected files:** `src/CNA/Internal/Media/AudioTagParser.cpp`
- **Affected backends:** N/A
- **Affected platforms:** **32-bit `size_t` targets only** — e.g. Android `armeabi-v7a`, 32-bit
  Emscripten. Safe on all 64-bit desktop builds (the primary target).
- **XNA/FNA compatibility impact:** N/A (CNA-original)
- **Security impact:** **HIGH on affected platforms.** A crafted `.mp3`/`.flac` in the user's Music
  library triggers an out-of-bounds heap read. Media libraries are scanned automatically, so the
  attack requires only file placement, not user action.
- **Memory/resource safety impact:** OOB heap read on 32-bit builds.
- **Suggested implementation strategy:** Rewrite as `len > bound - pos` (subtraction, after
  establishing `pos <= bound`), the standard overflow-safe form. Port `XactParser.cpp`'s existing
  hardened pattern rather than inventing a new one — and audit **every** length check in the file,
  not only the two the audit named.
- **Required tests:** Crafted ID3v2.3 and FLAC fixtures with lengths chosen to wrap a 32-bit `size_t`.
  These must be written to fail on a 32-bit build even if CI is 64-bit only.
- **Required regression tests:** Add a 32-bit build configuration (or a `size_t`-narrowing test
  harness) to CI — otherwise this class of defect stays permanently untestable in practice.
- **Required backend parity checks:** N/A
- **Dependencies:** `REMED-BUILD-001` (`AudioTagParserTest` is among the ~220 currently broken).
- **Estimated complexity:** SMALL (fix) / MEDIUM (if 32-bit CI coverage is added)
- **Parallel safe:** YES
- **Verification required:** **YES** — the overflow was identified by inspection, not executed.
  Reproduce on a 32-bit build before fixing, and record the result either way.
- **Completion criteria:** Every length check in the file uses the overflow-safe subtraction form.
- **Verification criteria:** ASan on a 32-bit build reports no OOB for the crafted fixtures.

---

## REMED-CONTENT-006 — XNB hardening: `XnbTypeName` unbounded recursion (stack-overflow DoS) + two dead `XnbReadLimits` controls

- **Severity:** MEDIUM (audit per-file grading) — **treated as P0 here; see Priority rationale**
- **Priority:** P0-SAFETY
- **Owner:** CONTENT
- **Status:** NOT STARTED
- **Root cause:** `XnbReadLimits` declares a documented set of security bounds, but **two of them have
  zero consumers anywhere in the codebase**, so the protection they exist to provide is never applied:
  - `maxObjectNestingDepth` — unused. `XnbTypeName::Detail::ParseOne()` recurses once per nested
    generic-argument level **with no depth limit**. A crafted `.xnb` type-reader name costs ~3 bytes
    per nesting level, so a small file can exhaust the C++ call stack and crash the process.
    `ContentReader`'s recursive object-graph deserialization is unbounded for the same reason.
  - `maxStringBytes` — unused. No call site bounds a string read by it; only `BinaryReader`'s much
    coarser whole-file clamp applies (up to `maxFileSize`/64 MB instead of the intended 1 MB).
    Clearest concrete call site: `XnbTypeReaderTable.hpp:67`, `entry.rawName = reader.ReadString();`.
- **Evidence:** `include/CNA/Internal/Xnb/XnbReadLimits.hpp.audit.md` F1/F2;
  `include/CNA/Internal/Xnb/XnbTypeName.hpp.audit.md` F1;
  `include/CNA/Internal/Xnb/XnbTypeReaderTable.hpp.audit.md` F1.
  **Note:** this finding does **not** appear in `AUDIT_FINDINGS_INDEX.md` or
  `AUDIT_CROSS_CUTTING_FINDINGS.md` — it was recovered only by the exhaustive per-file report sweep
  performed while building this plan. It is a genuine gap in the audit's own synthesis layer, not in
  its per-file work.
- **Audit references:** the three per-file reports above. (Absent from all synthesis documents.)
- **Affected files:** `include/CNA/Internal/Xnb/XnbReadLimits.hpp`;
  `include/CNA/Internal/Xnb/XnbTypeName.hpp`; `include/CNA/Internal/Xnb/XnbTypeReaderTable.hpp`;
  `src/Microsoft/Xna/Framework/Content/ContentReader.cpp` (recursive `ReadObject`/`InnerReadObject`)
- **Affected backends:** ALL (shared CPU-side content pipeline)
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** None — `XnbReadLimits` is a CNA-original hardening layer with no
  FNA equivalent. Enforcing its own declared limits cannot diverge from FNA.
- **Security impact:** **HIGH.** Unbounded recursion driven by attacker-controlled input is a
  stack-exhaustion DoS reachable through the same untrusted-asset channel as `REMED-CONTENT-001`
  (disk, mods, network). A declared-but-unenforced security control is worse than no control, because
  it creates a false assurance that reviewers and callers rely on.
- **Memory/resource safety impact:** Stack exhaustion → process crash; unbounded string allocation.
- **Suggested implementation strategy:** Thread an explicit depth counter through
  `XnbTypeName::ParseOne()` and `ContentReader`'s recursion, bounded by `maxObjectNestingDepth`,
  throwing `ContentLoadException` when exceeded. Pass `maxStringBytes` as an explicit cap to the
  string-read call sites (or add a capped `ReadString` overload).
  **Then audit all 7 declared `XnbReadLimits` fields for consumers** — two were found dead by
  inspection of two files; there is no reason to assume the other five are wired up.
- **Required tests:** A crafted deeply-nested type name that must throw rather than crash; an
  oversized string that must be rejected at `maxStringBytes`; and — per the audit's own
  recommendation — **one enforcement test per declared limit**, which is precisely the test that
  would have caught both dead controls.
- **Required regression tests:** Extend `XnbContainerFuzzTest` with deep-nesting mutations. Its
  existing design (hard-failing on `std::bad_alloc`) is well suited; a stack-overflow guard is the
  natural sibling.
- **Required backend parity checks:** N/A (CPU-side)
- **Dependencies:** `REMED-BUILD-001`. Same reviewer as `REMED-CONTENT-001`/`-003` (one coherent
  "harden the XNB reader against untrusted input" review), but no file overlap with either.
- **Estimated complexity:** MEDIUM
- **Parallel safe:** YES
- **Verification required:** **YES** — the stack-overflow is reasoned from the recursion shape, not
  executed. Build the crafted input and confirm the crash before fixing.
- **Completion criteria:** All 7 `XnbReadLimits` fields have a real enforcement site and a test;
  deep nesting and oversized strings throw `ContentLoadException`.
- **Verification criteria:** The crafted deep-nesting fixture throws cleanly instead of crashing,
  confirmed under ASan.

---

# P1 — Test/CI reliability, HIGH correctness, broad cross-backend defects, major FNA divergences

---

## REMED-BUILD-003 — CTest's `WILL_FAIL` mechanism has never been adopted anywhere in the project

- **Severity:** MEDIUM (per instance) / HIGH (as a systemic gap)
- **Priority:** P1
- **Owner:** BUILD_TEST_CI
- **Status:** NOT STARTED
- **Root cause:** Not five independent oversights — `grep -rn "WILL_FAIL" cmake/Tests/*.cmake
  cmake/UnitTests.cmake` returns **zero matches project-wide**. The project has simply never adopted
  the mechanism, for any backend, despite maintaining several dozen honestly-disclosed-in-source
  known limitations. The only related mechanism in use is `SKIP_REGULAR_EXPRESSION` (WebGPU's
  environment-conditional MSAA test — a genuine environment skip, not an expected-failure marker).
- **Evidence:** 6+ confirmed currently-failing, unannotated CTests: `EasyGL_AvatarRenderer_TintRouting`,
  `Bgfx_RenderTargetCube_DepthFormat`, `Bgfx_SkinnedEffect_WeightsPerVertex`,
  `EasyGL_GraphicsDevice_ReferenceStencil`, `EasyGL_MRT_TwoAttachments`, plus 2 Bgfx
  custom-`ShaderEffect` tests failing against a disclosed architectural limitation.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics backends;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "CI-masking risk" and § Pass 6; `AUDIT_FINAL_REPORT.md` §8 item 8.
- **Affected files:** `cmake/Tests/*.cmake`, `cmake/UnitTests.cmake`
- **Affected backends:** EasyGL, Bgfx confirmed; policy applies to all 14.
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** N/A
- **Security impact:** Indirect — a real new regression is currently indistinguishable from these
  known failures in CI output.
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Adopt `WILL_FAIL` (or an equivalent documented convention)
  as project-wide policy, then annotate every known-failing test with a comment naming the tracking
  task. **Do not annotate a test whose underlying bug is being fixed in this same remediation effort**
  — `EasyGL_MRT_TwoAttachments` (`REMED-GFX-016`), `EasyGL_AvatarRenderer_TintRouting`, and
  `Bgfx_*` are all scheduled for real fixes. `WILL_FAIL` is for genuinely accepted limitations, not a
  way to silence work in progress. Sequence this task **after** the fixes it might otherwise mask.
  Preserve the project's existing, genuinely strong discipline of stating precisely which check fails
  and why — that discipline is a real asset; this task adds machine-readability to it, not bureaucracy.
- **Required tests:** N/A (test infrastructure)
- **Required regression tests:** A CI gate asserting zero unannotated failures in a full run.
- **Required backend parity checks:** Apply the policy uniformly across all 14 backends' `.cmake` files.
- **Dependencies:** `REMED-BUILD-001` (until it lands, the failing-test list is not trustworthy).
  Scheduled **after** `REMED-GFX-016`, `REMED-GFX-017`, `REMED-GFX-018`.
- **Estimated complexity:** SMALL per test / MEDIUM as a policy rollout
- **Parallel safe:** NO — touches every `cmake/Tests/*.cmake`, conflicting with any backend task that
  adds a test registration. Do this as a single sweep in a quiet window.
- **Verification required:** NO
- **Completion criteria:** Every knowingly-failing test is either fixed or annotated with a reason
  and a tracking ID; a full run has zero unexplained failures.
- **Verification criteria:** `ctest` output distinguishes expected from unexpected failures.

---

## REMED-BUILD-004 — All 3 CI workflows use label filters that never run the general test set

- **Severity:** HIGH
- **Priority:** P1
- **Owner:** BUILD_TEST_CI
- **Status:** NOT STARTED
- **Root cause:** All three GitHub Actions workflows (`d3d-windows-ci.yml`, `devices-tests.yml`,
  `input-ci.yml`) invoke `ctest --test-dir build -L <label>` with a label filter (`D3D9`/`D3D11`/
  `D3D12`, devices-specific, or `input`). **None ever runs the general/default `CnaTests` set.**
  Combined with `REMED-BUILD-001`, this means ~220 tests covering Media, audio-tag parsing, the XNB
  content pipeline, ENet networking, and Lzx decompression have likely **never once passed in any CI
  run this project has had.**
- **Evidence:** `AUDIT_CROSS_CUTTING_FINDINGS.md` § Pass 6 ROOT CAUSE — "invisible to every existing
  CI workflow, not just unflagged." Note `--test-dir build` only changes where `ctest` looks for
  `CTestTestfile.cmake`; it does not override each test's baked-in `WORKING_DIRECTORY`.
- **Audit references:** as above; `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure.
- **Affected files:** `.github/workflows/d3d-windows-ci.yml`, `devices-tests.yml`, `input-ci.yml`
- **Affected backends:** ALL
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** Indirect — FNA-parity regressions across five subsystems are
  currently undetectable.
- **Security impact:** Both adversarial-input fuzz harnesses live in the unrun set.
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Add a workflow (or a job in an existing one) that runs the
  full unfiltered suite on the default Linux backend. Land this **after** `REMED-BUILD-001`, or the
  new job goes red immediately for the already-known reason. Expect genuine, previously-invisible
  failures on first green-field run — budget triage time; do not assume a clean result.
- **Required tests:** N/A
- **Required regression tests:** The new job is itself the regression gate.
- **Required backend parity checks:** Decide explicitly which backends CI runs the general suite on;
  running it on one backend still leaves the other 13 unguarded.
- **Dependencies:** `REMED-BUILD-001` (hard). Best sequenced after `REMED-BUILD-003` so the first
  full CI run is interpretable.
- **Estimated complexity:** SMALL (workflow) / LARGE (triaging what it exposes)
- **Parallel safe:** YES
- **Verification required:** NO
- **Completion criteria:** CI runs the unfiltered default suite on at least one backend and gates on it.
- **Verification criteria:** A deliberately-broken test causes the new job to fail.

---

## REMED-GFX-005 — Fog formula is mirrored in Bgfx, Vulkan, and all 15 shared D3DCommon fog shaders

- **Severity:** HIGH — the single widest-reaching shader-level defect in the audit
- **Priority:** P1
- **Owner:** GRAPHICS (**one owner for all four backend groups — do not split**)
- **Status:** NOT STARTED
- **Root cause:** One wrong formula, propagated by copy-porting. Correct FNA formula:
  `(z + FogEnd) / (FogEnd - FogStart)`. These backends compute `(FogEnd - z) / (FogEnd - FogStart)` —
  the **mirror image**, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`)
  and fixed in EasyGL as Task 1111 — then never ported onward.
- **Evidence:**
  - **Bgfx:** `vs_alpha_test3d.sc`, `vs_colored3d.sc`, `vs_lit_textured3d.sc` (3 shaders, 3 test audits)
  - **Vulkan:** `textured3d.vert.glsl`, `env_map3d.vert.glsl`, and by extension every 3D fog-capable shader
  - **D3DCommon (D3D11 + D3D12):** **all 15** fog-capable vertex shaders, confirmed by exhaustive
    grep — `colored3d`, `colored_textured3d`, `textured3d`, `dual_texture3d`, `lit_textured3d`,
    `lit_textured3d_vertexlit`, `env_map3d`, `pbr3d`, `skinned3d`, `skinned3d_vertexlit`,
    `skinned_colored3d`, `skinned_colored3d_vertexlit`, `pbr_skinned3d`, `alpha_test3d`,
    `alpha_test_colored3d`. (`sprite2d`/`instanced3d` correctly have no fog term, matching FNA.)
  - **Propagation mechanism, documented in-source:** `skinned3d.vert.hlsl` claims its formula
    "matches EasyGL/Bgfx's established SkinnedEffect fog formula exactly" — **false**; EasyGL's is the
    corrected post-Task-1111 one. A later port copied a prior *wrong* instance while believing it
    agreed with EasyGL's since-fixed version.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Systematic FNA parity gaps" (first entry, ~4 update rounds);
  `AUDIT_GRAPHICS_BACKEND_MATRIX.md` fog row; `AUDIT_FINAL_REPORT.md` §2 item 1.
- **Affected files:** `src/CNA/Internal/Backends/Bgfx/shaders/vs_*.sc` (3);
  `src/CNA/Internal/Backends/Vulkan/shaders/*.vert.glsl` (fog-capable subset);
  `src/CNA/Internal/Backends/D3DCommon/shaders/*.vert.hlsl` (15);
  plus every affected test's expected values.
- **Affected backends:** Bgfx, Vulkan, D3D11, D3D12. **EasyGL correct** (fixed). D3D9 vendored stock
  effects correct (real `ComputeFogVectorEXT()`); D3D9 custom shaders have a *different* defect
  (`REMED-GFX-010`). SdlGpu has no fog at all (`REMED-GFX-009`).
  Canvas/SdlRenderer/Software/Dx3/Ascii/Headless/WebGPU: **unchecked** — the matrix marks these `?`.
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** **Direct, severe.** Fog is inverted relative to real XNA: distant
  geometry is under-fogged and near geometry over-fogged. Any XNA title relying on fog for distance
  culling or atmosphere renders visibly wrong on 4 of 14 backends.
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Fix D3DCommon **once** (one shared source tree, closes D3D11
  and D3D12 together — this is the highest-leverage single edit in the graphics lane), then Bgfx, then
  Vulkan. **Critical:** each affected test's expected values currently assert the *wrong* behavior, so
  shader and test must change in the same commit or the suite goes red for the right reason.
  Before starting, determine the true blast radius on the 7 unchecked backends — a fix that leaves
  half the backends divergent is worse than a documented known gap, because it silently splits
  behavior across the matrix.
  Delete or correct the three false "matches EasyGL's established formula" comments; leaving them
  will re-propagate the bug on the next port.
- **Required tests:** A fog test at a **mid-range** Z (not at the `Z=FogEnd` boundary, where both the
  correct and mirrored formulas saturate identically — this is exactly why D3D11's existing fog checks
  pass despite the bug). Non-identity World/View, to distinguish this from `REMED-GFX-010`'s
  object-space defect.
- **Required regression tests:** A single shared cross-backend fog conformance test with
  analytically-derived expected values, run on all 14 backends. The absence of one is why this
  survived in 4 backends.
- **Required backend parity checks:** All 14 backends produce the same fog factor for the same scene,
  within tolerance.
- **Dependencies:** `REMED-BUILD-001`. Coordinate with `REMED-GFX-006` (same shader files),
  `REMED-GFX-009`, `REMED-GFX-010`. Sequence before `REMED-BUILD-003`.
- **Estimated complexity:** MEDIUM (the edit is mechanical; the test-expectation updates and the
  7-backend blast-radius determination are the real work)
- **Parallel safe:** NO — shares shader files with `REMED-GFX-006`, `REMED-GFX-007`, `REMED-GFX-008`,
  `REMED-GFX-011`. **All shader-level tasks must be serialized within the GRAPHICS lane**; see
  `REMEDIATION_DEPENDENCIES.md` § Shader serialization.
- **Verification required:** NO (confirmed at source level in all 4 groups)
- **Completion criteria:** All fog-capable shaders on all backends use FNA's formula; tests assert
  correct values; false precedent comments removed.
- **Verification criteria:** A cross-backend fog conformance test passes on all 14 backends with
  analytically-derived expectations at a non-saturating Z.

---

## REMED-GFX-006 — `SkinnedEffect` world-space normal transform missing on every backend that implements it

- **Severity:** HIGH — the audit's most exhaustively-confirmed defect (a complete, no-exceptions sweep)
- **Priority:** P1
- **Owner:** GRAPHICS (**one owner; one coordinated fix across all backends**)
- **Status:** NOT STARTED
- **Root cause:** A single conceptual mistake — *skinning code forgets to compose the outer
  world-space normal matrix* — reproduced in every backend, largely by explicit copy-porting.
  Two variants:
  - **Variant A (complete omission):** normal transformed by the bone-skin matrix alone, with no
    world-space contribution at all. `mat3(skinMat) * normal`.
  - **Variant B (raw World, not inverse-transpose):** a world-space transform *is* applied, but using
    raw `World` instead of the inverse-transpose normal matrix — correct only for rotation and
    uniform scale, wrong under non-uniform scale.
- **Evidence:**
  - **Variant A confirmed at shader-source level in all 6 backend groups:** EasyGL
    (`EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram` — no `uNormalMatrix` uniform at all),
    WebGPU (`CreateSkinnedResources()` WGSL), Vulkan (`skinned3d.vert.glsl`,
    `skinned3d_vertexlit.vert.glsl`), SdlGpu (`skinned3d.vert.glsl`, `skinned_colored3d.vert.glsl`),
    D3DCommon (**all 4** non-PBR skinned shaders → D3D11 + D3D12), Bgfx (`vs_skinned3d.sc:27-29`,
    spelled out component-wise).
  - **Variant B confirmed in 6 instances:** EasyGL `EnsurePbrSkinnedProgram`, SdlGpu
    `pbr_skinned3d.vert.glsl`, Bgfx `vs_pbr_skinned3d.sc:39`, D3D9 `PbrSkinned3D.hlsl`,
    D3DCommon `pbr_skinned3d.vert.hlsl` (→ D3D11 + D3D12).
  - **The control group proves the authors knew the correct convention:** D3DCommon's three
    *unskinned* lit vertex shaders (`lit_textured3d`, `pbr3d`, `lit_textured3d_vertexlit`) all
    correctly compute `InverseTranspose3x3((float3x3)World)`. Only the skinned path gets it wrong.
  - **Three self-documented porting comments** name the source: D3DCommon "Ported line-by-line from
    Vulkan's skinned3d.vert.glsl"; WebGPU "ported from EasyGL's GLSL shader line-for-line"; SdlGpu
    "mirrors VulkanGraphicsBackend's own skinned3d.vert.glsl exactly."
  - **Universally invisible to tests** because every existing skinned test uses `World = Identity`.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Systematic FNA parity gaps" (the longest entry, resolved to a
  complete sweep); `AUDIT_GRAPHICS_BACKEND_MATRIX.md` skinned-normal rows; `AUDIT_FINAL_REPORT.md` §2 item 2.
- **Affected files:** EasyGL `EasyGLGraphicsBackend.cpp`; WebGPU `WebGPUGraphicsBackend.cpp`;
  Vulkan `shaders/skinned3d*.vert.glsl`, `pbr3d_skinned.vert.glsl`;
  SdlGpu `shaders/skinned*.vert.glsl`, `pbr_skinned3d.vert.glsl`;
  D3DCommon `shaders/skinned*.vert.hlsl`, `pbr_skinned3d.vert.hlsl`;
  Bgfx `shaders/vs_skinned3d.sc`, `vs_pbr_skinned3d.sc`; D3D9 `shaders/PbrSkinned3D.hlsl`,
  `SkinnedVertexColor3D.hlsl`
- **Affected backends:** **All 14 that implement `SkinnedEffect`.** Only D3D9's *vendored* stock
  `SkinnedEffect.fx` is immune — real Microsoft bytecode, structurally impossible to have the bug.
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** **Direct.** Any rotated or non-uniformly-scaled skinned model is
  lit incorrectly on every backend. This is core `SkinnedEffect` behavior, not an edge case.
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Derive the correct transform **once** from FNA, then apply
  it deliberately to each backend — reusing the porting discipline that spread the bug, this time
  with a verified source. Fix D3DCommon once for D3D11+D3D12. Treat Variant B as the same task:
  same root cause, and a partial fix would leave the PBR path inconsistent with its own non-PBR sibling.
  **Related, non-blocking context:** the audit resolved that this is *not* the cause of the
  long-standing "infinite slab" bone-weight defect — `SkinnedModelEXT::ComputeBoneTransformsEXT()`
  does no per-vertex weight blending at all, and that fix already landed in
  `tools/avatar_builder/generate_body.py`'s `fix_automatic_weights()`. Do not conflate them.
- **Required tests:** A skinned lighting test with a **non-identity, rotated World matrix** and,
  separately, a non-uniform scale — the two cases every existing test structurally cannot detect.
- **Required regression tests:** A shared cross-backend skinned-lighting conformance test with
  analytically-derived expectations, run on all 14 backends.
- **Required backend parity checks:** All 14 produce identical lighting for a rotated skinned model.
- **Dependencies:** `REMED-BUILD-001`. Serialized against every other shader task.
  Note `REMED-GFX-008` touches the same skinned shaders — consider doing them together per backend.
- **Estimated complexity:** LARGE (14 backends, 2 variants, new test infrastructure)
- **Parallel safe:** NO — see `REMED-GFX-005`.
- **Verification required:** NO (exhaustively confirmed at source level)
- **Completion criteria:** Every skinned shader composes the inverse-transpose world normal matrix
  with the bone-skin matrix; false/misleading porting comments corrected.
- **Verification criteria:** The rotated-World conformance test passes on all 14 backends.

---

## REMED-GFX-007 — `EnvironmentMapEffect` re-multiplies `EmissiveColor` by `DiffuseColor` in 5 backend groups

- **Severity:** HIGH
- **Priority:** P1
- **Owner:** GRAPHICS (one owner)
- **Status:** NOT STARTED
- **Root cause:** Shaders compute `litRGB = (emissive + lightSum) * diffuse` instead of FNA's
  `Lighting.fxh` convention of adding emissive **unscaled**: `lightSum * diffuse + emissive`.
  CNA's own `EnvironmentMapEffect.cpp` comment explicitly states the unscaled add is required to
  match FNA — so the C++ layer knows the right convention and the shaders diverge from it.
- **Evidence:** Confirmed at shader-source level in **Bgfx** (original source, 5 test audits),
  **WebGPU** (`CreateEnvMapResources()`), **Vulkan** (`env_map3d.frag.glsl`), **SdlGpu**
  (`env_map3d.frag.glsl` — additionally squares alpha), and **D3D11+D3D12** (shared
  `D3DCommon/shaders/env_map3d.frag.hlsl`, also "ported line-by-line from Vulkan").
  Universally masked because **no test varies `DiffuseColor` from white or `EmissiveColor`/
  `AmbientLightColor` from black** — with those defaults the two formulas are numerically identical.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "CONFIRMED IN 5 BACKENDS";
  `AUDIT_GRAPHICS_BACKEND_MATRIX.md` EnvironmentMapEffect row.
- **Affected files:** Bgfx, WebGPU, Vulkan, SdlGpu, D3DCommon env-map fragment shaders
- **Affected backends:** Bgfx, WebGPU, Vulkan, SdlGpu, D3D11, D3D12. D3D9 vendored stock: correct.
  EasyGL: no `EnvironmentMapEffect` implementation confirmed. Software/SdlRenderer/Dx3/Canvas/Ascii/
  Headless likely do not implement the reflection math — **verify rather than assume**.
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** Direct divergence from FNA's documented lighting convention,
  visible whenever a title sets a non-white diffuse or non-black emissive/ambient — i.e. most real use.
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** One-line formula correction per shader. Fix D3DCommon once
  for both D3D backends. Fix SdlGpu's alpha-squaring in the same pass.
- **Required tests:** An env-map test with **non-white `DiffuseColor` and non-black `EmissiveColor`**
  — the exact combination no current test exercises.
- **Required regression tests:** Add that parameterization to the shared cross-backend env-map test.
- **Required backend parity checks:** All implementing backends agree for non-default colors.
- **Dependencies:** `REMED-BUILD-001`. Serialized with other shader tasks.
- **Estimated complexity:** SMALL (fix) / MEDIUM (test coverage)
- **Parallel safe:** NO — see `REMED-GFX-005`.
- **Verification required:** NO
- **Completion criteria:** All affected shaders add emissive unscaled, matching FNA.
- **Verification criteria:** The new non-default-color test passes on every implementing backend.

---

## REMED-GFX-008 — `SkinnedEffect` Ambient/Emissive misconsumed by Vulkan and D3D11/D3D12

- **Severity:** HIGH
- **Priority:** P1
- **Owner:** GRAPHICS
- **Status:** NOT STARTED
- **Root cause:** **Resolved by the audit — the upstream C++ value is correct.**
  `SkinnedEffect::FillGpuDrawParams()` computes
  `emissiveColor[i] = (emissiveColor_.i + ambientLightColor_.i * diffuseColor_.i) * alpha_` —
  byte-for-byte FNA's `EffectHelpers.SetMaterialColor()` lighting-enabled formula — deliberately
  pre-folding ambient into `emissiveColor` and never writing `p.ambientColor`.
  Two backend groups **misconsume** this correct value:
  - **Vulkan:** skinned shaders read the wrong (always-zero) `ambientColor` field, and every skinned
    UBO lacks an `emissiveColor` field entirely — both halves broken.
  - **D3D11/D3D12:** all 4 shared `D3DCommon` skinned fragment shaders lack an `EmissiveColor`
    cbuffer field. `AmbientColor` **is** present and correctly consumed — only the emissive half transfers.
- **Evidence:** 4 independent backends corroborate the pre-folding convention (EasyGL, Bgfx, SdlGpu,
  D3D9 — D3D9's `D3DSkinnedVertexColorDraw.cpp:150` documents it explicitly). SdlGpu is a positive
  counter-example: it reuses `lit_textured3d.frag.glsl` unchanged, structurally preventing the bug.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Resolved: the `SkinnedEffect` `ambientColor`/`emissiveColor`
  open hypothesis" (a multi-round investigation, fully closed).
- **Affected files:** Vulkan `shaders/skinned3d*.{vert,frag}.glsl`, `VulkanGraphicsBackend.cpp`
  (`FillExtPushConst`); D3DCommon `shaders/skinned*.frag.hlsl`, `D3DConstantBuffers.hpp`
  (`D3DSkinnedExtraConstants`)
- **Affected backends:** Vulkan (both fields), D3D11 + D3D12 (emissive only).
  EasyGL, Bgfx, SdlGpu, D3D9 confirmed correct.
- **Affected platforms:** ALL
- **XNA/FNA compatibility impact:** `AmbientLightColor` and `EmissiveColor` are silently no-ops for
  skinned models on the affected backends.
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** **Do not change `FillGpuDrawParams()`** — it is correct and
  4 backends depend on the current convention. Add the missing `emissiveColor` slot to Vulkan's
  skinned UBOs and D3DCommon's `D3DSkinnedExtraConstants`, and point Vulkan's shaders at
  `emissiveColor` instead of the always-zero `ambientColor`.
  **Note the interaction with `EasyGL_AvatarRenderer_TintRouting`:** the Vulkan sibling of that test
  currently *passes by coincidence*, because this bug cancels out a separate miscalibration. Fixing
  this will make `Vulkan_AvatarRenderer_TintRouting` start failing — that is correct and expected.
  Handle both together.
- **Required tests:** A skinned test with non-zero `AmbientLightColor` **and** non-zero
  `EmissiveColor`. Note `vulkan_skinnedeffect_vertexcolor_test.cpp` currently routes *around* this
  defect by setting `AmbientLightColor = 0` — that workaround must be removed.
- **Required regression tests:** Re-baseline both `AvatarRenderer_TintRouting` variants once fixed.
- **Required backend parity checks:** All 14 agree on skinned ambient/emissive.
- **Dependencies:** `REMED-BUILD-001`. Same shader files as `REMED-GFX-006` — do together per backend.
- **Estimated complexity:** MEDIUM
- **Parallel safe:** NO — see `REMED-GFX-005`.
- **Verification required:** NO (root cause fully resolved by the audit)
- **Completion criteria:** Vulkan and D3D11/D3D12 skinned shaders consume `emissiveColor` correctly;
  both TintRouting tests re-baselined and passing.
- **Verification criteria:** The non-zero-ambient/emissive skinned test passes on all 14 backends.

---

## REMED-GFX-009 — SdlGpu: fog is entirely unimplemented across all 10 stock-effect shader families

- **Severity:** HIGH
- **Priority:** P1
- **Owner:** GRAPHICS
- **Status:** NOT STARTED
- **Root cause:** Not a wrong formula — a **total absence**, confirmed deliberate.
  `grep` across all 23 `.glsl` files in `src/CNA/Internal/Backends/SdlGpu/shaders/` for any fog
  identifier returns **zero matches**; `SdlGpuGraphicsBackend.cpp` likewise never references fog.
  `colored3d.vert.glsl`'s own comment: "No fog (deliberately deferred, same as this codebase's
  WebGPU backend's own initial 3D vertical slice)."
- **Evidence:** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "HIGH, SdlGpu-specific"; exhaustive grep both
  shader-side and C++-side, confirming the gap runs top to bottom.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends;
  `AUDIT_GRAPHICS_BACKEND_MATRIX.md` fog row ("MISSING ENTIRELY (0/10 shader families)").
- **Affected files:** all `src/CNA/Internal/Backends/SdlGpu/shaders/*.vert.glsl`;
  `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
- **Affected backends:** SdlGpu only
- **Affected platforms:** ALL running SdlGpu
- **XNA/FNA compatibility impact:** **Arguably more severe than the mirrored-formula bug.**
  `GraphicsDevice.FogEnable`, `BasicEffect.FogEnabled`, `FogColor`, `FogStart`, `FogEnd` have **zero
  visible effect** — a scene renders identically whether fog is on or off. The mirrored-formula
  backends at least do *something*.
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Implement fog across all 10 families using the **corrected**
  formula from `REMED-GFX-005` — do not port from Bgfx/Vulkan/D3DCommon, which are the wrong ones.
  Sequence after `REMED-GFX-005` so the correct formula exists to copy. Requires both C++-side
  parameter plumbing and shader work.
- **Required tests:** Fog tests for SdlGpu across representative effect families; none currently exist
  in the 22-file `examples-tests-sdlgpu` shard.
- **Required regression tests:** Include SdlGpu in the cross-backend fog conformance test from `REMED-GFX-005`.
- **Required backend parity checks:** SdlGpu matches the other 13 for the same fog scene.
- **Dependencies:** **`REMED-GFX-005` (hard — needs the correct formula first).** `REMED-BUILD-001`.
- **Estimated complexity:** LARGE (10 shader families + C++ plumbing, from scratch)
- **Parallel safe:** NO — see `REMED-GFX-005`.
- **Verification required:** NO (exhaustively confirmed absent)
- **Completion criteria:** All 10 families implement fog with FNA's formula; the XNA fog properties
  have visible, correct effect.
- **Verification criteria:** SdlGpu passes the cross-backend fog conformance test.

---

## REMED-GFX-011 — Vulkan: NDC Y-flip missing in 4 effect families (renders vertically mirrored)

- **Severity:** HIGH
- **Priority:** P1
- **Owner:** GRAPHICS
- **Status:** NOT STARTED
- **Root cause:** Vulkan's NDC has inverted Y versus OpenGL, so every Vulkan 3D vertex shader applies
  a per-shader Y-flip by convention (the C++ side supplies no flip — confirmed at
  `DrawPrimitivesEx`'s `wvp` and `FillInstancedPushConst`'s `vp`). **14 shaders flip correctly;
  4 do not:** `env_map3d`, `pbr3d`, `pbr3d_skinned`, `instanced3d`.
- **Evidence:** Full source read plus exact grep sweep of every Vulkan `.vert.glsl`.
  Two of the four carry justifying comments, **one of which is demonstrably false**:
  - `pbr3d.vert.glsl` — reasoning checks only *internal* consistency between the two PBR shaders,
    ignoring that every other 3D effect sharing the identical `mvp` input does flip.
  - `pbr3d_skinned.vert.glsl` — claims "`skinned3d.vert.glsl` never Y-flips." **It does**, verified at
    line 59: `gl_Position.y = -gl_Position.y;`. A confidently wrong claim about a sibling file, which
    makes this instance more dangerous than a silent omission.
  - `instanced3d` — no comment or rationale at all.
  - `sprite2d.vert.glsl` also lacks the flip but is a **verified non-bug** (computes NDC directly from
    pixel space with its own correct Y-down mapping).
  Masked because affected tests use identity View, centered cameras, and **center-pixel-only sampling**.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "MAJOR UPDATE — the missing-Y-flip bug is NOT limited to
  EnvironmentMapEffect"; `AUDIT_FINAL_REPORT.md` §2 item 7.
- **Affected files:** `src/CNA/Internal/Backends/Vulkan/shaders/{env_map3d,pbr3d,pbr3d_skinned,instanced3d}.vert.glsl`
- **Affected backends:** Vulkan only. D3D9/D3D11/D3D12 correctly and deliberately never flip
  (D3D clip space already matches XNA) — a documented, correct family-wide convention.
- **Affected platforms:** ALL running Vulkan
- **XNA/FNA compatibility impact:** `PbrEffect`, `SkinnedPbrEffect`, `InstancedEffect`, and
  `EnvironmentMapEffect` render **vertically mirrored relative to every other effect type in the same
  frame** — so a scene mixing effect types is visibly inconsistent with itself.
- **Security impact:** N/A
- **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** Add the flip to all four shaders. **Delete the two false/
  incomplete justifying comments** — leaving them would lead a future maintainer to "re-fix" the flip
  back out. Replace with a single accurate statement of the family-wide convention.
- **Required tests:** Tests sampling an **asymmetric, off-center pixel** for each of the four effect
  families on Vulkan. Center-pixel sampling structurally cannot detect a vertical mirror — this is
  the general blind spot behind several findings.
- **Required regression tests:** Audit the whole Vulkan test shard for center-pixel-only assertions
  and add off-center checks.
- **Required backend parity checks:** The four families render identically oriented on Vulkan and a
  non-Vulkan reference backend.
- **Dependencies:** `REMED-BUILD-001`. Serialized with other shader tasks; overlaps `REMED-GFX-006`
  (`pbr3d_skinned`) and `REMED-GFX-007` (`env_map3d`) — do all three together for Vulkan.
- **Estimated complexity:** SMALL (fix) / MEDIUM (off-center test infrastructure)
- **Parallel safe:** NO — see `REMED-GFX-005`.
- **Verification required:** NO
- **Completion criteria:** All 18 Vulkan 3D vertex shaders share one correct, accurately-documented
  flip convention.
- **Verification criteria:** Off-center-pixel tests pass for all four families.

---

## Compact task format (P1 remainder, P2, P3)

The tasks above carry full narrative because they are the plan's highest-risk or highest-leverage
items. Every task below carries the **same complete field set** in a condensed layout. Field meanings
are unchanged. `Sev` = audit severity, `Pri` = scheduling priority, `Cx` = estimated complexity
(TRIVIAL/SMALL/MEDIUM/LARGE), `PS` = parallel safe, `Verify` = verification required before fixing.

---

### REMED-CORE-001 — `CNA::Logger::ToSDLPriority()` mistags every Fatal/Error/Warn as `SDL_LOG_PRIORITY_INFO`

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / CORE / NOT STARTED |
| Root cause | The switch's `FATAL`/`ERROR`/`WARN`/`INFO` cases are all commented out behind a literal `//todo`, so all four fall through to `default: return SDL_LOG_PRIORITY_INFO`. Only `DEBUG`/`TRACE`/`EXPERIMENT` have live cases. Abandoned work; the correct implementation is visible, commented out, directly above the bug. |
| Evidence | `src/CNA/Logger.cpp`; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "HIGH: `CNA::Logger::ToSDLPriority()`" |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer; `AUDIT_FINAL_REPORT.md` §2 item 9, §8 item 4 |
| Affected files | `src/CNA/Logger.cpp` |
| Backends / Platforms | ALL / ALL — **foundational, always-compiled, not gated behind any option** |
| XNA/FNA impact | N/A (CNA infrastructure) |
| Security impact | Indirect but real: security-relevant `Error`/`Fatal` log lines are emitted at INFO priority and may be filtered out of production logs entirely. |
| Memory/resource impact | N/A |
| Strategy | Uncomment and verify the four cases. Also fixes `SetMinimumLevel()`, which routes through the same function — setting the minimum to `WARN` currently sets SDL's native threshold to `INFO`. **Widest blast radius of any single bug in the audit**; the fix is a few lines. |
| Required tests | One assertion per `LogLevel` → `SDL_LOG_PRIORITY_*` mapping; plus a `SetMinimumLevel(WARN)` test asserting the SDL threshold. |
| Regression tests | A table-driven test over every enum value, so a future addition cannot silently fall through to `default`. |
| Backend parity | N/A |
| Dependencies | None |
| Cx / PS / Verify | SMALL / YES / NO |
| Completion criteria | Every `LogLevel` maps to its correct SDL priority; `SetMinimumLevel` sets the intended threshold. |
| Verification criteria | Table-driven test passes for all enum values, including any added later. |

---

### REMED-CORE-006 — `Game::UnloadContent()` is a dead virtual lifecycle hook, never invoked

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / CORE / NOT STARTED |
| Root cause | FNA's `Initialize()` (`Game.cs:649-662`) subscribes `graphicsDeviceService.DeviceDisposing += (o,e) => UnloadContent();`. CNA's `Initialize()` (`Game.cpp:513-529`) never performs this subscription. Whole-repo grep for `UnloadContent`: exactly 2 hits — the declaration and the empty default body. **No call site.** |
| Evidence | `include/Microsoft/Xna/Framework/Game.hpp.audit.md`; `src/Microsoft/Xna/Framework/Game.cpp.audit.md` |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "`Game::UnloadContent()` is a dead virtual lifecycle hook" |
| Affected files | `src/Microsoft/Xna/Framework/Game.cpp`; `include/Microsoft/Xna/Framework/Game.hpp` |
| Backends / Platforms | ALL / ALL |
| XNA/FNA impact | **Direct.** A game overriding `UnloadContent()` per the documented XNA lifecycle contract silently never has it called, under any circumstance. GPU-dependent resources are never released at the documented point. |
| Security impact | N/A |
| Memory/resource impact | Resource-release hook never fires — a real leak path for any game following the documented contract. |
| Strategy | Subscribe to `DeviceDisposing` in `Initialize()`, matching FNA. **Compounds with `REMED-CORE-007`:** fixing that alone will not help here, because `Game::Initialize()` is not listening for `DeviceDisposing` at all. Both subscriptions are needed; do them together. |
| Required tests | Override `UnloadContent()` in a test `Game`, dispose the device, assert it was called. Requires `REMED-TEST-002` first — `GameTests.cpp` currently has **zero** real coverage. |
| Regression tests | Full `Game` lifecycle ordering test: `Initialize` → `LoadContent` → … → `UnloadContent`. |
| Backend parity | N/A |
| Dependencies | `REMED-TEST-002` (no test harness exists today); pairs with `REMED-CORE-007` |
| Cx / PS / Verify | MEDIUM / CONDITIONAL — `NO` against `REMED-CORE-007`/`-009` (same `Game.cpp`) / NO |
| Completion criteria | `UnloadContent()` is invoked on device disposal, matching FNA's lifecycle. |
| Verification criteria | Lifecycle-ordering test passes. |

---

### REMED-CORE-007 — `GraphicsDeviceManager` never subscribes to `GraphicsDevice`'s own device events

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / CORE / NOT STARTED |
| Root cause | FNA's `IGraphicsDeviceManager.CreateDevice()` wires `graphicsDevice.DeviceResetting += OnDeviceResetting; graphicsDevice.DeviceReset += OnDeviceReset;` (`GraphicsDeviceManager.cs:556-557`). CNA instead raises its **own separate copies** manually, only around its own `applyToExistingBackend()` call. |
| Evidence | A real bypassing path is confirmed: `GraphicsDevice.cpp`'s `createBackend()` installs a `deviceEventCallback` (lines 1459-1478) that raises `GraphicsDevice`'s own `DeviceResetting`/`DeviceReset` for a genuine backend-detected device-lost recovery — entirely outside any `GraphicsDeviceManager` call. The audit separately **resolved this in `GraphicsDevice`'s favor**: `GraphicsDevice` raises correctly; this is purely a subscriber-side gap. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Resolved: the `GraphicsDeviceManager`/`GraphicsDevice` device-events open question"; `AUDIT_GRAPHICS_BACKEND_MATRIX.md` event-forwarding row |
| Affected files | `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`; `include/.../GraphicsDeviceManager.hpp` |
| Backends / Platforms | ALL / ALL (the callback is currently implemented by 1 of 10 backends — D3D9-class alt-tab/display-change) |
| XNA/FNA impact | **Direct.** A real device-lost→reset cycle silently never reaches `IGraphicsDeviceService` listeners — the conventional place resource-reload code subscribes — even though `GraphicsDevice` correctly raised it. |
| Security impact | N/A |
| Memory/resource impact | GPU resources are not reloaded after a real device reset. |
| Strategy | Subscribe in `CreateDevice()`, matching FNA, and forward to `IGraphicsDeviceService` listeners. Guard against double-raising for resets that `GraphicsDeviceManager` itself initiates. |
| Required tests | Simulate a backend-detected device-lost via the `deviceEventCallback` seam; assert an `IGraphicsDeviceService` listener receives it. |
| Regression tests | Assert `GraphicsDeviceManager`-initiated resets raise exactly once, not twice. |
| Backend parity | Verify on the backend that actually implements the callback. |
| Dependencies | `REMED-TEST-002`; pairs with `REMED-CORE-006` |
| Cx / PS / Verify | MEDIUM / CONDITIONAL — coordinate with `REMED-CORE-006` / NO |
| Completion criteria | All `GraphicsDevice` lifecycle events reach `IGraphicsDeviceService` listeners regardless of trigger. |
| Verification criteria | Backend-triggered reset test passes; no double-raise. |

---

### REMED-CORE-004 — `Color::PackFromVector4()` unclamped `static_cast<bytecs>(float)` (undefined behavior)

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | MEDIUM / **P1** (real UB) / CORE / NOT STARTED |
| Root cause | The `IPackedVector::PackFromVector4` override skips the `ToByteFromUnitClamped()` clamp that **every other** float-to-component path in the same file correctly applies. Out-of-range or NaN input is genuine C++ UB, not merely an XNA-style "unspecified" result. |
| Evidence | `src/Microsoft/Xna/Framework/Color.cpp.audit.md` |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` MEDIUM § core; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "MEDIUM: `Color::PackFromVector4()`" |
| Affected files | `src/Microsoft/Xna/Framework/Color.cpp` |
| Backends / Platforms | ALL / ALL |
| XNA/FNA impact | XNA clamps; CNA invokes UB. Reachable from unclamped procedural/HDR color math or a stray NaN. |
| Security impact | UB with data-derived input. |
| Memory/resource impact | N/A |
| Strategy | Route through the existing `ToByteFromUnitClamped()` helper, matching the file's other three paths. Then check every other `IPackedVector` implementation for the same pattern — the audit explicitly flags this as worth sweeping. |
| Required tests | Components at -1.0, 2.0, NaN, +Inf, -Inf. |
| Regression tests | Add out-of-range/NaN inputs to the shared packed-vector test matrix. |
| Backend parity | N/A |
| Dependencies | Sweep overlaps `REMED-GFX-033` (PackedVector rounding) — same directory, coordinate. |
| Cx / PS / Verify | SMALL / YES / NO |
| Completion criteria | All float→component paths clamp; the `IPackedVector` sweep is complete. |
| Verification criteria | UBSan build passes the out-of-range/NaN tests. |

---

### REMED-MEDIA-002 — `MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` SEGFAULTs on an empty scan

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH (crash) / **P1** / MEDIA / NOT STARTED |
| Root cause | When the picture-library scan returns null/empty, some downstream object-graph-walking code dereferences the result without a null check. The **sibling test in the same fixture fails cleanly** on the identical condition — proving this is a genuine defensive-programming gap, not merely a consequence of the empty scan. |
| Evidence | Confirmed **universal across 6+ backends** (EasyGL, SdlRenderer, Software, Ascii, Headless, and structurally implicated elsewhere) — backend-independent, in shared CPU-side code. CTest classifies it `Exception: SegFault`. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH + MEDIUM § Testing infrastructure; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Derived finding, MEDIUM severity" and § Pass 6 continued |
| Affected files | `MediaLibraryTestFixture`-adjacent object-graph-walking helper(s) in `src/Microsoft/Xna/Framework/Media/` |
| Backends / Platforms | ALL (shared CPU-side) / ALL |
| XNA/FNA impact | N/A — FNA's `MediaLibrary` is a complete `NotImplementedException` stub, so there is no reference behavior. CNA's implementation is from scratch. |
| Security impact | A malformed or incomplete **real** media library on a user's machine crashes the process — reachable without any adversary. |
| Memory/resource impact | Null-pointer dereference → SIGSEGV. |
| Strategy | Add defensive null checks along the object-graph walk. **Independent of `REMED-BUILD-001`** — although the empty scan is currently *triggered* by the working-directory bug, a genuinely empty library must not crash either. Fix both; do not treat BUILD-001 as closing this. |
| Required tests | Walk the object graph against a deliberately empty and a deliberately malformed library; assert clean failure, not a crash. |
| Regression tests | Keep the empty-library case in the suite permanently, so it stays covered after BUILD-001 makes the fixture load correctly. |
| Backend parity | N/A |
| Dependencies | None (deliberately not gated on `REMED-BUILD-001`) |
| Cx / PS / Verify | SMALL / YES / NO (reproduced on 6 backends) |
| Completion criteria | An empty or malformed library produces a clean failure on every backend. |
| Verification criteria | ASan run over the empty-library test is clean; no SIGSEGV on any backend. |

---

### REMED-GFX-016 — EasyGL: `SetRenderTargets` with 2 attachments only draws to the first

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | Not determined. The second render-target attachment never receives its draw output. |
| Evidence | `EasyGL_MRT_TwoAttachments` (Task 145) **fails reproducibly in complete isolation** (re-run alone, not a parallelism artifact): `left=(0,255,0)` correct green, `right=(0,0,0)` black where blue was expected. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "NEW, HIGH severity: EasyGL `SetRenderTargets`…" |
| Affected files | `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (MRT path) |
| Backends / Platforms | EasyGL (**the default backend on Linux and Emscripten**) / ALL |
| XNA/FNA impact | **Direct.** Multiple render targets are a real, documented XNA 4.0 feature, fully non-functional beyond the first attachment. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | The test's own Task 145 comment implies it once passed — **run `git log`/`git bisect` on the MRT code first.** If it is a regression, the introducing commit will likely show the fix directly and save the whole investigation. The audit could not determine regression-vs-day-one gap (static, point-in-time). |
| Required tests | The existing test is adequate; extend to 3+ attachments and to differing attachment formats. |
| Regression tests | Add MRT to the cross-backend conformance suite — MRT is currently only tested on EasyGL. |
| Backend parity | Check MRT on every backend claiming support; the matrix has no MRT row at all. |
| Dependencies | `REMED-BUILD-001`. Must precede `REMED-BUILD-003` (do not annotate `WILL_FAIL` on a bug being fixed). Serialize against other EasyGL work (`REMED-GFX-001`, `-006`). |
| Cx / PS / Verify | MEDIUM (unknown until bisected) / NO — EasyGL single-file serialization / NO |
| Completion criteria | All bound render targets receive their draw output. |
| Verification criteria | `EasyGL_MRT_TwoAttachments` passes; extended multi-attachment tests pass. |

---

### REMED-GFX-012 — Vulkan: `SpriteBatch.Begin(transformMatrix)` silently dropped

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | `VulkanSpriteBatchBackend` never overrides `SetTransformMatrix()` (exhaustive grep across the whole Vulkan backend: zero matches), so it falls through to `IGraphicsBackend`'s shared **no-op default**. |
| Evidence | `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Vulkan-specific: `VulkanSpriteBatchBackend` never overrides…". Every other backend applies it, via one of two valid mechanisms: a stateful override consumed at flush (EasyGL, Bgfx, D3D9, D3D11, Canvas, Dx3, Software, Headless) or the transform threaded as a `Draw()`/`QueueSprite()` parameter (WebGPU, SdlGpu, SdlRenderer). Ascii inherits SdlRenderer's fix by delegation. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends; `AUDIT_GRAPHICS_BACKEND_MATRIX.md` `SetTransformMatrix` row; `AUDIT_FINAL_REPORT.md` §2 item 6 |
| Affected files | `src/CNA/Internal/Backends/Vulkan/VulkanSpriteBatchBackend.*` |
| Backends / Platforms | Vulkan only (the sole affected backend of 14) / ALL |
| XNA/FNA impact | **Direct.** `SpriteBatch.Begin(transformMatrix:)` is the standard XNA idiom for camera-relative 2D rendering (scrolling worlds). On Vulkan, sprites render as if the transform were always Identity. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Implement `SetTransformMatrix()`. `SdlRenderer` fixed this exact gap under Task 675 and documents it — use as the reference. **Consider making `IGraphicsBackend::SetTransformMatrix()` pure virtual** rather than a silent no-op default; the silent default is what allowed this to go unnoticed, and it is an instance of the audit's broader "silent-default-degradation" architecture finding (see `REMED-GFX-026`). |
| Required tests | A non-identity `transformMatrix` on Vulkan — **no test anywhere in the codebase currently exercises this**, which is exactly why it survived. |
| Regression tests | Add non-identity transform to the shared cross-backend SpriteBatch test, run on all 14. |
| Backend parity | Confirm D3D12, the one backend the audit did not directly check (likely correct, mirroring D3D11). |
| Dependencies | `REMED-BUILD-001`. Related to `REMED-GFX-026` (shared silent-default architecture). |
| Cx / PS / Verify | SMALL / YES (Vulkan SpriteBatch file is not touched by other tasks) / NO |
| Completion criteria | Vulkan applies the transform; D3D12 confirmed. |
| Verification criteria | Non-identity-transform test passes on all 14 backends. |

---

### REMED-GFX-013 — Vulkan: `ScissorRectangle` completely non-functional when a render target is bound

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | `VulkanGraphicsBackend::RecordCommandBuffer()`'s RT-pass loop hardcodes `VkRect2D rtSc{{0,0},{rtW,rtH}}` before every `vkCmdSetScissor` for an RT pass. The correctly-captured `scissorEnabled_`/`scissorX_`/`Y_`/`W_`/`H_` are never read in that loop. The **backbuffer** pass correctly checks `scissorEnabled_`. |
| Evidence | `AUDIT_CROSS_CUTTING_FINDINGS.md` § "NEW, Vulkan-specific … `GraphicsDevice.ScissorRectangle`". Unlike the paired Viewport-when-RT-bound limitation (explicitly disclosed in `SetViewport()`'s own header comment), **this gap has no disclosure anywhere near the scissor code** — a silent gap, not a documented scope cut. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends; `AUDIT_GRAPHICS_BACKEND_MATRIX.md` stencil/scissor row |
| Affected files | `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` |
| Backends / Platforms | Vulkan / ALL |
| XNA/FNA impact | Rendering to an off-screen `RenderTarget2D` while relying on scissor clipping — a common pattern for UI clip regions and split-screen-to-texture — produces silently unclipped output, with no error or warning. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Read the captured scissor state in the RT-pass loop as the backbuffer pass already does. If the Viewport limitation genuinely blocks a full fix, **disclose it in-source** as the Viewport path does — the absence of disclosure is itself part of this finding. |
| Required tests | `ScissorRectangle` combined with a bound `RenderTarget2D`. **No test anywhere in the codebase exercises this combination on any backend** — so this is likely under-tested project-wide, not merely unfixed on Vulkan. |
| Regression tests | Add the combination to the cross-backend suite for all 14. |
| Backend parity | Check every backend for the same RT-vs-backbuffer scissor asymmetry. |
| Dependencies | `REMED-BUILD-001` |
| Cx / PS / Verify | MEDIUM / CONDITIONAL — `NO` against other `VulkanGraphicsBackend.cpp` tasks / NO |
| Completion criteria | Scissor applies correctly to RT passes, or the limitation is explicitly disclosed in-source. |
| Verification criteria | The scissor+RT test passes on all backends claiming scissor support. |

---

### REMED-GFX-014 — D3D12: `StencilState` and `ScissorTestEnable`/`DepthBias`/`SlopeScaleDepthBias` are fully inert

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | `D3D12GraphicsBackend::ApplyDepthStencilState()` receives all 11 stencil parameters as **literally commented-out unused parameters** (`bool /*stencilEnable*/, int /*stencilFunc*/, …`) and never stores or forwards them; `ApplyRasterizerState()` does the same for scissor/depth-bias. `D3D12PipelineStateCache.cpp` confirms why it could not work anyway: every PSO hardcodes `ds.StencilEnable = FALSE` (line 99) and leaves `RasterizerState.ScissorEnable` at its zero-initialised `FALSE`. `RSSetScissorRects()` **is** called, but with `ScissorEnable=FALSE` in the PSO it has no effect — necessary but not sufficient in D3D12's model. |
| Evidence | `AUDIT_CROSS_CUTTING_FINDINGS.md` § "HIGH, D3D12-specific". Honestly disclosed in-code as a deliberate DX-107/DX-118 scope cut — not hidden, but two commonly-used XNA features are fully non-functional. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends; `AUDIT_GRAPHICS_BACKEND_MATRIX.md`; `AUDIT_FINAL_REPORT.md` §2 item 4 |
| Affected files | `src/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.cpp`, `D3D12PipelineStateCache.cpp` |
| Backends / Platforms | D3D12 / Windows |
| XNA/FNA impact | **A real regression relative to D3D11**, which implements full dynamic stencil (including two-sided) and scissor correctly. Stencil techniques (mirrors, decals, shadow volumes, outlines) and `ScissorRectangle` UI clipping silently do nothing. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Thread the parameters into `RenderStateSnapshot`-equivalent PSO key state and set the PSO flags. **D3D11's own `D3D11DepthStencilStateCache`/`D3D11RasterizerStateCache` are the working reference** — and SdlGpu shows the same pattern done correctly on a third API. Expect PSO-cache-key churn: these become part of the pipeline identity. |
| Required tests | Dedicated D3D12 stencil and scissor tests. **None exist** — see `REMED-BUILD-008`; this backend has exactly one CTest total. |
| Regression tests | Add D3D12 to the cross-backend stencil/scissor suite. |
| Backend parity | D3D12 matches D3D11 for identical stencil/scissor scenes. |
| Dependencies | **`REMED-BUILD-008`** (without a second D3D12 test, this fix is unverifiable). `REMED-BUILD-001`. |
| Cx / PS / Verify | LARGE (PSO key redesign) / YES (D3D12-specific files) / NO |
| Completion criteria | Stencil and scissor are functional and match D3D11. |
| Verification criteria | New D3D12 stencil/scissor tests pass under real vkd3d-proton. |

---

### REMED-GFX-017 — Bgfx: XNA's default cull mode (`CullCounterClockwiseFace`) culls nothing

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | **Hypothesis, not proven.** `ApplyRasterizerState()`'s own comment claims `BGFX_STATE_CULL_CW`/`CCW` map to raw winding direction, unaffected by `glFrontFace`. But a Task 763 comment in the same file documents that bgfx's default `glFrontFace` is `GL_CW` — opposite EasyGL's effective convention — and that this **does** affect stencil face-relativity. If cull mode is similarly `glFrontFace`-relative rather than absolute, this is an unfixed sibling of the already-fixed stencil issue. |
| Evidence | `Bgfx_RasterizerState_CullMode_Camera` and `Bgfx_RasterizerState_CullMode_IndexedBasicEffect` both fail identically — **two independent test files, ruling out coincidence**. `None` and `CullClockwiseFace` both work correctly in the same two tests. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Bgfx: build+test complete" |
| Affected files | `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyRasterizerState`) |
| Backends / Platforms | Bgfx / ALL |
| XNA/FNA impact | **XNA's real *default* cull mode is non-functional** — so every Bgfx scene that does not explicitly set a cull mode renders back faces it should not. High practical impact precisely because it is the default. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Test the `glFrontFace` hypothesis directly before changing anything. Task 763's already-landed stencil fix is the template. Correct the `ApplyRasterizerState()` comment either way — it currently asserts something that may well be false. |
| Required tests | Both existing tests are adequate; add a `glFrontFace`-explicit case to pin the semantics. |
| Regression tests | Add all three `CullMode` values to the cross-backend suite. |
| Backend parity | Verify default cull behavior matches on all 14 — a default-value defect could plausibly exist elsewhere unexamined. |
| Dependencies | `REMED-BUILD-001`. Precedes `REMED-BUILD-003`. |
| Cx / PS / Verify | MEDIUM / YES / **YES** — hypothesis stated but not proven; confirm the `glFrontFace` mechanism first |
| Completion criteria | All three `CullMode` values behave correctly; the misleading comment is corrected. |
| Verification criteria | Both failing tests pass; cross-backend cull parity confirmed. |

---

### REMED-GFX-018 — Bgfx: `EnsureViewState()` clears color+depth+stencil regardless of requested `ClearOptions`

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | `EnsureViewState()` issues an unconditional full clear on every `Clear*()` call, ignoring the requested `ClearOptions` mask. |
| Evidence | `examples/bgfx_graphicsdevice_clear_stencil_test.cpp.audit.md` |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends |
| Affected files | `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` |
| Backends / Platforms | Bgfx / ALL |
| XNA/FNA impact | **Direct data loss.** A stencil-only clear silently wipes color and depth too — the caller's rendered content disappears. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Map `ClearOptions` to bgfx's clear flags. Mind bgfx's view-state model: clear flags are view properties, so this may need per-view state tracking rather than a per-call flag. |
| Required tests | Each `ClearOptions` combination (Target / DepthBuffer / Stencil and their unions), asserting untouched buffers retain prior content. |
| Regression tests | Add the full `ClearOptions` matrix to the cross-backend suite — a stencil-only clear wiping color would be a serious defect on any backend. |
| Backend parity | All 14 honor `ClearOptions` identically. |
| Dependencies | `REMED-BUILD-001`. Precedes `REMED-BUILD-003`. |
| Cx / PS / Verify | MEDIUM / YES / NO |
| Completion criteria | Only the requested buffers are cleared. |
| Verification criteria | The full `ClearOptions` matrix passes on Bgfx and every other backend. |

---

### REMED-GFX-019 — WebGPU: `SpriteBatch` clip-space mapping is always backbuffer-relative

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | `WebGPUGraphicsBackend::QueueSprite()` derives its clip-space viewport exclusively from the backbuffer's physical/virtual size via `ComputeLogicalViewport()`, never from the currently-bound render target. |
| Evidence | `examples/webgpu_rendertargetcube_test.cpp.audit.md` — the test file's own Check-C comment already self-discloses the defect (empirically observed), independently re-verified against production source. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics backends |
| Affected files | `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` |
| Backends / Platforms | WebGPU (**experimental** per `CLAUDE.md`) / ALL |
| XNA/FNA impact | `SpriteBatch.Draw()` into an off-screen target of a different size mis-maps its destination rectangle. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Derive the viewport from the bound render target when one is bound, else the backbuffer. **Check the same call path on every backend** — this is exactly the shape that recurs cross-backend, and only WebGPU has been checked. |
| Required tests | `SpriteBatch` into a render target whose size differs from the backbuffer, asserting exact placement. |
| Regression tests | Add differing-size RT sprite placement to the cross-backend suite. |
| Backend parity | Check all 14 for the same backbuffer-relative assumption. |
| Dependencies | `REMED-BUILD-001` |
| Cx / PS / Verify | MEDIUM / YES / NO |
| Completion criteria | Sprite placement is correct for any bound render target size. |
| Verification criteria | The differing-size RT test passes on WebGPU and every other backend. |

---

### REMED-GFX-022 — `EffectParameter` Matrix Get/Set/Transpose semantics inverted across all 8 methods

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | A transcription slip, not a design choice. FNA's `GetValueMatrix()`/`SetValue(Matrix)` apply a column-major transpose (HLSL's default non-`row_major` layout); CNA's plain variants do the untransposed read/write — exactly what FNA's `*Transpose` variants do, and vice versa. All 8 Matrix methods are affected. |
| Evidence | **Cross-validated with high confidence:** the sibling `EffectAnnotation::GetValueMatrix()` implements the identical FNA formula **correctly**, proving the right convention was known and applied elsewhere in this same codebase. |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` HIGH § `Microsoft::Xna::Framework::Graphics`; `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-graphics HIGH findings; `AUDIT_GRAPHICS_BACKEND_MATRIX.md` |
| Affected files | `src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp`; `tests/.../EffectParameterTests.cpp` |
| Backends / Platforms | ALL (XNA-facing shared layer) / ALL |
| XNA/FNA impact | **Direct.** Exposure is custom/user-authored `Effect`s using the generic Matrix accessors — a real, supported XNA pattern. The 7 stock effects are unaffected (their `FillGpuDrawParams()` bypasses `EffectParameter` entirely), which is why rendering looks fine. |
| Security impact | N/A |
| Memory/resource impact | N/A |
| Strategy | Swap the transposed/untransposed implementations to match FNA, using `EffectAnnotation::GetValueMatrix()` as the in-repo reference. **`EffectParameterTests.cpp`'s `SetValueTransposeRawLayoutDiffersFromSetValue` asserts the exact inverse as correct** — production and test must change in the same commit. See `REMED-TEST-001`. |
| Required tests | Round-trip each of the 8 methods against an asymmetric matrix (a symmetric one cannot detect a transpose). |
| Regression tests | Cross-check against `EffectAnnotationTests.cpp`'s `GetValueMatrixRoundTrip`, which already gets the convention right. |
| Backend parity | Verify a custom effect using generic accessors renders identically on ≥3 backends. |
| Dependencies | **`REMED-TEST-001` (must be same commit).** |
| Cx / PS / Verify | MEDIUM / CONDITIONAL — `NO` against `REMED-TEST-001` (must be atomic) / NO |
| Completion criteria | All 8 methods match FNA's convention; the test asserts the correct convention. |
| Verification criteria | Asymmetric-matrix round-trip tests pass and agree with `EffectAnnotation`'s behavior. |

---

### REMED-GFX-004 — `RenderTargetCube` lacks `RenderTarget2D`'s `Dispose(bool)` fix (use-after-free risk)

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | MEDIUM / **P1** (memory safety) / GRAPHICS / NOT STARTED |
| Root cause | `RenderTarget2D` received a Task 717 fix adding a "still bound to device" guard and a dangling-pointer clear. `RenderTargetCube` has **no `Dispose(bool)` override at all**, so it has neither — despite the structurally identical pointer pattern. |
| Evidence | `include/Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp.audit.md` |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics; `AUDIT_GRAPHICS_BACKEND_MATRIX.md` XNA-facing row (marked MISSING, universal) |
| Affected files | `include/Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp`; corresponding `.cpp` |
| Backends / Platforms | ALL (XNA-facing state, not backend state) / ALL |
| XNA/FNA impact | Disposing a bound `RenderTargetCube` leaves a dangling device pointer. |
| Security impact | Use-after-free. |
| Memory/resource impact | **Confirmed UAF risk in the identical pattern Task 717 already fixed once.** |
| Strategy | Port `RenderTarget2D`'s `Dispose(bool)` verbatim. Then check every other `GraphicsResource` subclass for the same omission — Task 717 fixed one instance; this is the second found by inspection, so assume more. |
| Required tests | Dispose a bound `RenderTargetCube`, then use the device; assert clean behavior. Mirror `RenderTarget2D`'s existing Task 717 regression test. |
| Regression tests | Extend the Task 717 test to every `GraphicsResource` subclass. |
| Backend parity | N/A (XNA-facing) |
| Dependencies | None |
| Cx / PS / Verify | SMALL / YES / NO |
| Completion criteria | `RenderTargetCube` has the guard and clear; the subclass sweep is complete. |
| Verification criteria | ASan run of the dispose-while-bound test is clean. |

---

### REMED-GFX-043 — `GraphicsDevice::DrawUserPrimitives` explicit `VertexDeclaration` overload never reaches the backend

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | HIGH / **P1** / GRAPHICS / NOT STARTED |
| Root cause | The explicit-declaration overloads of `DrawUserPrimitives`/`DrawUserIndexedPrimitives` accept a `const VertexDeclaration&` but never propagate it to the backend. EasyGL's `ApplyLayout` then **guesses** the layout from a hardcoded stride table, so any custom layout whose stride does not match the guess renders silently wrong. |
| Evidence | `examples/easygl_draw_user_primitives_custom_test.cpp.audit.md` F1. **Recovered by the per-file sweep for this plan — absent from all audit synthesis documents.** |
| Audit refs | the per-file report above (not in `AUDIT_FINDINGS_INDEX.md`) |
| Affected files | `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`; EasyGL `ApplyLayout` |
| Backends / Platforms | Root cause is XNA-facing (**all backends**); symptom confirmed on EasyGL / ALL |
| XNA/FNA impact | **Direct FNA gap.** A documented XNA overload accepting a custom vertex declaration silently ignores it. Related in kind to `REMED-GFX-025` (`SetData` destination offset), another dropped-overload-semantics finding. |
| Security impact | N/A |
| Memory/resource impact | Mismatched stride interpretation could read past the intended vertex data. |
| Strategy | Call `vb->SetVertexDeclaration(vertexDeclaration.GetVertexElements())` before `SetData` in both explicit-declaration overloads. Verify each backend then honors the propagated declaration rather than its own stride guess. |
| Required tests | A custom `VertexDeclaration` whose stride does **not** match any hardcoded table entry, asserting correct rendering. |
| Regression tests | Add custom-declaration `DrawUserPrimitives` to the cross-backend suite. |
| Backend parity | Check every backend for the same stride-guessing fallback. |
| Dependencies | `REMED-BUILD-001` |
| Cx / PS / Verify | MEDIUM / CONDITIONAL — `NO` against other `GraphicsDevice.cpp` tasks / **YES** (confirmed on EasyGL only; verify the XNA-layer root cause and each backend's behavior) |
| Completion criteria | The declaration reaches the backend; custom layouts render correctly on every backend. |
| Verification criteria | The non-matching-stride test passes on all backends supporting user primitives. |

---

### REMED-NET-002 — `NetworkSessionProperties::Insert(int)`/`RemoveAt(int)` unchecked iterator arithmetic

| Field | Value |
|---|---|
| Sev / Pri / Owner / Status | MEDIUM / **P1** (UB via public API) / NET / NOT STARTED |
| Root cause | Both perform unchecked `properties_.begin() + index` arithmetic, unlike **every other index-taking member in the same file** (`operator[]` uses `.at()`; `CopyTo` performs explicit range checks). |
| Evidence | `src/Microsoft/Xna/Framework/Net/NetworkSessionProperties.cpp.audit.md` |
| Audit refs | `AUDIT_FINDINGS_INDEX.md` MEDIUM § Content/Storage/Net |
| Affected files | `src/Microsoft/Xna/Framework/Net/NetworkSessionProperties.cpp` |
| Backends / Platforms | N/A / ALL |
| XNA/FNA impact | FNA has **no** `Net` namespace, so there is no reference. XNA documents `ArgumentOutOfRangeException` for these. |
| Security impact | UB for an out-of-range index, reachable from the public `NetworkSession::Create`/`Find` surface. |
| Memory/resource impact | Invalid iterator construction is undefined behavior. |
| Strategy | Add bounds checks throwing `System::ArgumentOutOfRangeException`, matching the file's own siblings. **Positive counter-examples confirmed:** `GamerCollection<T>` and `AchievementCollection` both bounds-check every index-taking member correctly — this is an isolated lapse, not a systemic collection pattern. `ModelMeshPartCollection`/`ModelEffectCollection` share the shape (`REMED-GFX-040`). |
| Required tests | Negative and past-the-end indices for both methods, separately. |
| Regression tests | Add out-of-range cases to the shared collection test matrix. |
| Backend parity | N/A |
| Dependencies | Overlaps `REMED-CORE-002` (exception types) — use `System::*` from the start. |
| Cx / PS / Verify | SMALL / YES / NO |
| Completion criteria | Both methods bounds-check and throw the correct exception type. |
| Verification criteria | UBSan build passes the out-of-range tests. |

---

## REMED-TEST-001 — Three test files assert confirmed defects as correct behavior (blocking coordination task)

- **Severity:** HIGH · **Priority:** P1 · **Owner:** BUILD_TEST_CI · **Status:** NOT STARTED
- **Root cause:** When a production defect was introduced, a matching test was written against the
  wrong behavior. The test now actively defends the bug.
- **Evidence / affected files:**
  1. `tests/.../EffectParameterTests.cpp` — `SetValueTransposeRawLayoutDiffersFromSetValue` asserts the
     exact inverse of FNA's real Matrix storage convention. Blocks `REMED-GFX-022`.
     (The sibling `EffectAnnotationTests.cpp` gets the same convention right.)
  2. `tests/.../GraphicsExceptionTests.cpp` — 6 tests assert `DeviceLostException`/
     `DeviceNotResetException`/`NoSuitableGraphicsDeviceException` inherit/catch as `std::runtime_error`.
     Blocks `REMED-CORE-003`.
  3. `tests/.../GamerServicesDataTests.cpp` — asserts `PropertyDictionary`'s raw-`std::` exception types.
     Blocks `REMED-CORE-002`.
- **Audit references:** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Testing/documentation;
  `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Test-coverage gaps for already-confirmed production defects"
- **Affected backends / platforms:** ALL / ALL
- **XNA/FNA compatibility impact:** These tests currently encode divergence from FNA as the project's
  asserted-correct behavior — the most durable form of a parity bug.
- **Security impact:** N/A · **Memory/resource safety impact:** N/A
- **Suggested implementation strategy:** **This task is not implemented standalone.** It is a
  coordination marker: each test change ships **in the same commit** as its paired production fix.
  Landing either half alone turns CI red for a correct change, which invites a revert of the fix
  rather than the test.
- **Required tests:** The corrected assertions themselves.
- **Required regression tests:** After correction, each test is the regression guard for its fix.
- **Required backend parity checks:** N/A
- **Dependencies:** Bidirectional with `REMED-GFX-022`, `REMED-CORE-002`, `REMED-CORE-003`.
- **Estimated complexity:** SMALL (per file) · **Parallel safe:** NO (atomic with its production fix)
- **Verification required:** NO
- **Completion criteria:** All three assert FNA-correct behavior, each landed atomically with its fix.
- **Verification criteria:** No commit exists in which production and test disagree.

---

# P2 — MEDIUM correctness, missing backend features, lifecycle, API surface gaps

Each entry carries the full field set in compact form.

### REMED-CORE-002 — Project-wide raw `std::` exceptions where a `System::*Exception` exists

**Sev** MEDIUM (individually) / HIGH (aggregate — the audit's single most numerous pattern) · **Pri** P2 ·
**Owner** CORE (single owner; **do not** split across lanes) · **Status** NOT STARTED
**Root cause** No enforced convention, so each site chose independently. Confirmed in ≥10 distinct areas.
**Evidence / affected files** `GraphicsDevice.cpp` (~27 sites — the widest single file, in the framework's
most central class, alongside 13 *correct* `System::` uses in the same file); `Texture2D.cpp` (~15+) and
the wider `Texture*` family; `PropertyDictionary` (9 methods, the largest single-file instance in
gamerservices); `SkinnedEffect.cpp` (4), `SkinnedPbrEffect.cpp` (4), `EnvironmentMapEffect.cpp` (1),
`PbrEffect.cpp` (1), `SamplerStateCollection.cpp` (2); 4 `Effect*Collection::operator[](int)`;
`Model.cpp` (5); `GameComponentCollection::SetItem()` (claims `NotSupportedException` "isn't available
yet" — it is, used by 16 other files); `GraphicsDeviceManager` ctor/`registerServices()`;
`Game::AssertNotDisposed()` (`std::runtime_error` where `ObjectDisposedException` is used by 28 other files).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` § By category "API-design"; `AUDIT_CROSS_CUTTING_FINDINGS.md` (multiple)
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** FNA/XNA document specific `System.*` types; callers writing FNA-correct catch blocks
miss these. **Security** N/A. **Memory/resource** N/A.
**Strategy** Sweep by area, smallest first, to build confidence before touching `GraphicsDevice.cpp`.
Note this is a **catch-site-visible change** — any code catching `std::runtime_error` stops catching.
Grep the repo (including examples/tools) for such catch sites before starting. `GraphicsDevice.cpp`'s
own 13 correct sites are the in-file style reference.
**Required tests** Per area: assert the new exception type and that the message is preserved.
**Regression tests** `GamerServicesDataTests.cpp` currently asserts the raw types — must change atomically
(`REMED-TEST-001`). **Backend parity** N/A.
**Dependencies** `REMED-TEST-001` (atomic for the gamerservices slice); overlaps `REMED-GFX-002`,
`REMED-NET-002`, `REMED-GFX-040`, which introduce `System::` types at sites this sweep also touches.
**Cx** LARGE (breadth) · **PS** NO (touches files owned by nearly every other lane — schedule in a
quiet window, or slice per-area with explicit hand-offs) · **Verify** NO
**Completion** Every raw `std::` throw shadowing an available `System::*Exception` is converted or
explicitly recorded as intentional. **Verification** Repo-wide grep returns only recorded exceptions.

### REMED-CORE-003 — Graphics/Content exception types derive from `std::runtime_error`, not `System::Exception`

**Sev** MEDIUM · **Pri** P2 · **Owner** CORE · **Status** NOT STARTED
**Root cause** `DeviceLostException`, `DeviceNotResetException`, `NoSuitableGraphicsDeviceException`, and
`ContentLoadException` all derive from `std::runtime_error` instead of `System::Exception`, and all lack
the `(message, innerException)` constructor FNA's real types have. `ContentLoadException` also lacks
FNA's parameterless constructor.
**Evidence** `include/.../ContentLoadException.hpp.audit.md`; the three graphics exception audits.
The base-class choice has a **concrete** consequence: the message+inner constructor flattens the inner
exception into `.what()` text and **cannot preserve the actual inner exception object**, unlike
`System::Exception::getInnerExceptionProperty()` — a capability this codebase already implements
correctly in `StorageDeviceNotConnectedException`.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics; `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-content
**Affected files** the 4 exception headers/sources; `tests/.../GraphicsExceptionTests.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Direct — inner-exception chaining is lost. **Security** N/A.
**Memory/resource** N/A.
**Strategy** Re-base on `System::Exception`; add the missing constructors. **Distinct from
`REMED-CORE-002`** (which changes *throw sites*); this changes *type hierarchies*, so it has different
blast radius at catch sites. `GraphicsExceptionTests.cpp`'s 6 tests assert the current hierarchy —
atomic change required.
**Required tests** Inner-exception preservation round-trip; catch-as-`System::Exception`.
**Regression tests** Rewrite the 6 `GraphicsExceptionTests.cpp` assertions.
**Backend parity** N/A. **Dependencies** `REMED-TEST-001` (atomic).
**Cx** MEDIUM · **PS** NO (atomic with its test) · **Verify** NO
**Completion** All 4 derive from `System::Exception` with FNA's full constructor set.
**Verification** Inner-exception preservation test passes.

### REMED-CORE-005 — `GetHashCode()` signed-overflow UB fix never propagated to 4 siblings

**Sev** MEDIUM · **Pri** P2 · **Owner** CORE · **Status** NOT STARTED
**Root cause** `Vector2::GetHashCode()` sums `FloatHash()` values via an explicit unsigned-wraparound
cast, citing fix INPUT-BUILD-006 for exactly this signed-overflow-UB class. Four structurally identical
siblings never received it: `Vector3` (3 terms), `Vector4` (4), `Quaternion` (4), and **`Matrix` (16
terms — the highest-risk instance)**.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "MEDIUM, CONFIRMED WIDESPREAD". `Point`/`Rectangle`
XOR-combine and are unaffected; `Plane` XOR-combines at its own level but **transitively inherits
`Vector3`'s bug** via `Normal.GetHashCode()`.
**Affected files** `Vector3.cpp`, `Vector4.cpp`, `Quaternion.cpp`, `Matrix.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** None behavioral (hash values are unspecified); this is pure UB elimination.
**Security** UB. **Memory/resource** N/A.
**Strategy** Apply `Vector2`'s exact fix (sum via `static_cast<unsigned>`, cast back) to all four.
Mechanical and low-risk. Fixing `Vector3` also fixes `Plane` transitively.
**Required tests** Hash values chosen to overflow, under UBSan.
**Regression tests** Add overflow-inducing inputs to the shared math-type hash tests.
**Backend parity** N/A. **Dependencies** None.
**Cx** SMALL · **PS** YES · **Verify** NO
**Completion** All 5 types use the overflow-safe form. **Verification** UBSan reports no
signed-overflow for any math type's `GetHashCode()`.

### REMED-CORE-008 — `GameWindow::EndScreenDeviceChange` never repositions; orientation heuristic diverges from FNA

**Sev** MEDIUM · **Pri** P2 · **Owner** CORE · **Status** NOT STARTED
**Root cause** Two deviations, neither disclosed in `GameWindow.hpp`: (a) `EndScreenDeviceChange` never
centers/repositions the window onto the named display, where FNA's `ApplyWindowChanges`
(`SDL3_FNAPlatform.cs:471-575`) does, with an explicit "Window always gets centered on changes, per XNA
behavior" comment; (b) the orientation model substitutes an **unconditional window-aspect-ratio
heuristic** for FNA's real mobile-gated SDL-display-orientation mechanism — FNA deliberately makes
`SetSupportedOrientations` a desktop no-op, where CNA performs real cascading state mutation.
**Evidence** `include/.../GameWindow.hpp.audit.md`; `src/.../GameWindow.cpp.audit.md`. **Concretely
reachable:** `GraphicsDeviceManager` genuinely calls `EndScreenDeviceChange` with the real adapter name
during normal operation. **Internal inconsistency:** `GraphicsDeviceManager.cpp`'s own
`platformSupportsOrientations()` correctly gates to iOS/Android, matching FNA — the project already
implements the right gate in one sibling file and omits it in another.
**Affected files** `src/Microsoft/Xna/Framework/GameWindow.cpp`; `include/.../GameWindow.hpp`
**Backends / Platforms** ALL / desktop (the orientation heuristic misfires on desktop specifically)
**XNA/FNA impact** Direct: multi-display window placement does not work; resizing a desktop window
narrow/tall flips `CurrentOrientation` to `Portrait` with no XNA counterpart.
**Security** N/A. **Memory/resource** N/A.
**Strategy** Implement centering per FNA; gate orientation via the existing, already-correct
`platformSupportsOrientations()` rather than writing a second gate.
**Required tests** `EndScreenDeviceChange` with a second display (skip when unavailable, per
`GameWindowTests.cpp`'s established pattern); assert desktop resize does not change orientation.
**Regression tests** Orientation gating on a desktop platform. **Backend parity** N/A.
**Dependencies** None. **Cx** MEDIUM · **PS** CONDITIONAL (`NO` against `REMED-CORE-009`) · **Verify** NO
**Completion** Window centering matches FNA; orientation is mobile-gated. **Verification** Multi-display
test passes or skips cleanly; desktop resize leaves orientation unchanged.

### REMED-CORE-009 — `Game::PollEvents()` omits four real FNA SDL3 event reactions

**Sev** MEDIUM · **Pri** P2 · **Owner** CORE · **Status** NOT STARTED
**Root cause** Four handlers absent (confirmed by grep): `WINDOW_MOVED` (FNA detects a cross-display move
and calls `GraphicsDevice.Reset()` with the new adapter — real multi-monitor support CNA lacks);
`WINDOW_EXPOSED` (FNA calls `RedrawWindow()` to keep rendering during a blocking resize-drag; CNA has no
such call anywhere); `ENTER/LEAVE_FULLSCREEN` (FNA syncs `IsFullScreen` back when the OS toggles
fullscreen outside the app's request); `MOUSE_ENTER/LEAVE` (FNA toggles the screensaver; CNA disables it
unconditionally from an unrelated call site in `Guide.cpp`). No `DISPLAY_ORIENTATION` handling either.
**Evidence** `src/Microsoft/Xna/Framework/Game.cpp.audit.md`
**Affected files** `src/Microsoft/Xna/Framework/Game.cpp`
**Backends / Platforms** ALL / desktop especially (multi-monitor, window-manager fullscreen)
**XNA/FNA impact** Four concrete, observable behavioral gaps. **Security** N/A. **Memory/resource** N/A.
**Strategy** Add the four reactions, matching FNA's platform layer. `WINDOW_MOVED` interacts with
`REMED-CORE-007` (it triggers a device reset that must reach `IGraphicsDeviceService` listeners) — do
after that lands.
**Required tests** Event-injection tests via the SDL seam, skipping when no display is available.
**Regression tests** Fullscreen-sync round-trip. **Backend parity** N/A.
**Dependencies** `REMED-CORE-007` (for `WINDOW_MOVED`); `REMED-TEST-002`.
**Cx** MEDIUM · **PS** CONDITIONAL (`NO` against `REMED-CORE-006`/`-008` — same `Game.cpp`) · **Verify** NO
**Completion** All four reactions implemented per FNA. **Verification** Event-injection tests pass.

### REMED-GFX-010 — D3D9 CNA-custom shaders compute fog from raw object-space Z

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** A **second, distinct** fog defect, separate from `REMED-GFX-005`'s mirrored formula.
`SkinnedVertexColor3D.hlsl`, `Pbr3D.hlsl`, `PbrSkinned3D.hlsl` compute fog from raw local-space vertex Z,
never transforming by World/View. The formula's *arithmetic shape* is correct — only the untransformed Z
feeding it is wrong.
**Evidence** `examples/d3d9_pbr_test.cpp.audit.md`, `d3d9_skinnedvertexcolor_test.cpp.audit.md`. Mechanism
confirmed by direct read of `D3D9EffectDraw.cpp`: `ComputeFogVectorEXT()` (line 149) is a **faithful,
correct port of FNA's `EffectHelpers.SetFogVector`**, building a per-vertex dot-product fog vector from
the combined `World*View` matrix — used for every *vendored* stock effect. So two structurally different
fog algorithms coexist in one backend: vendored effects get real FNA fidelity, CNA-original effects
diverge from that same backend's own established-correct convention. Matches a standing memory note
about the identical mistake class in EasyGL.
**Affected files** `src/CNA/Internal/Backends/D3D9/shaders/{SkinnedVertexColor3D,Pbr3D,PbrSkinned3D}.hlsl`
**Backends / Platforms** D3D9 (custom shaders only) / Windows. EasyGL's non-stock shaders **should be
checked for the same defect** — the audit explicitly recommends it.
**XNA/FNA impact** Fog looks visibly inconsistent between stock-effect and PBR/skinned meshes in the
same scene, especially under camera rotation (object-space Z is fixed to the mesh's own orientation).
**Security** N/A. **Memory/resource** N/A.
**Strategy** Route these three through the existing, already-correct `ComputeFogVectorEXT()` rather than
writing a fourth fog path. Treat "object-space-only fog in a CNA-original shader" as its own pattern to
grep for across all backends.
**Required tests** Fog with a **rotated camera** and non-identity World/View — the only configuration
that distinguishes object-space from view-space fog.
**Regression tests** Add rotated-camera fog to the cross-backend suite; it also closes the D3D11/D3D12
identity-matrix blind spot (`REMED-TEST-006`).
**Backend parity** D3D9 custom and stock effects agree; all backends agree.
**Dependencies** `REMED-GFX-005` (shared conformance test). **Cx** MEDIUM · **PS** NO (shader serialization) · **Verify** NO
**Completion** All D3D9 shaders use one correct view-space fog path. **Verification** Rotated-camera fog
test passes; stock and custom effects agree.

### REMED-GFX-015 — D3D12 `OcclusionQuery` captures only the last draw between `Begin()`/`End()`

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** All 4 draw-recording methods (`DrawColoredPrimitives`, `DrawIndexedColoredPrimitives`,
`DrawPrimitivesExImpl`, `DrawInstancedPrimitivesEx`) each wrap their own submission in its own
`BeginQuery`/`EndQuery` on the **same query-heap slot (index 0)**. A slot holds one result, so a second
draw overwrites the first.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "MEDIUM-HIGH, D3D12-specific". `D3D12OcclusionQueryBackend.cpp`'s
own `Begin()` comment self-discloses this is "correct for exactly one draw call" and refers the reader to
the header — **but the header documents nothing of the sort** (grep: zero matches), a
documentation-cross-reference failure on top of the defect. The same comment records a genuinely useful
empirical constraint: `BeginQuery`/`EndQuery` **must** be in the same command-list submission as the
draws they bracket (a vkd3d-proton requirement, discovered by reproducing a real bug).
**Affected files** `src/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.cpp`, `D3D12OcclusionQueryBackend.*`
**Backends / Platforms** D3D12 / Windows. D3D9 is **structurally immune** (native `Issue(BEGIN/END)`,
GPU-level accumulation). Other backends' multi-draw semantics are unchecked (`?` in the matrix).
**XNA/FNA impact** XNA semantics require the cumulative visible-sample count across all draws between one
`Begin()`/`End()` pair. **Security** N/A. **Memory/resource** N/A.
**Strategy** Issue one `BeginQuery` at `Begin()` and one `EndQuery` at `End()`, spanning all draws — while
respecting the same-submission constraint the existing comment documents. Fix the header documentation too.
**Required tests** Multiple draws between one `Begin()`/`End()`, asserting the cumulative count.
**Regression tests** Add multi-draw occlusion to the cross-backend suite; only D3D9 and D3D12 have been checked.
**Backend parity** Determine multi-draw semantics on all 14 — the matrix marks most `?`.
**Dependencies** `REMED-BUILD-008` (D3D12 has one CTest; this needs a second). **Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** Cumulative counts across multi-draw. **Verification** Multi-draw test passes under vkd3d-proton.

### REMED-GFX-020 — D3D11: Blinn-Phong specular asymmetry and black-vertex-color failure

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** DONE (2026-07-21) — two UNRELATED root causes; neither was a shader-math bug.
**Resolution** The two failures had **no shared root cause** (the "asymmetry" framing was a red herring):
- **Specular (DX-125) = test-oracle defect, no production bug.** The check set `lightingEnabled=true`
  with the default `preferPerPixelLighting=false`, which makes `DrawPrimitivesEx` dispatch the
  **vertex-lit** `LitTextured3dVertexLit` (Gouraud) variant. Its per-vertex specular interpolated
  across the large screen triangle to ~228 at the sample pixel (barycentric 0.900→229), a *correct*
  Gouraud result, but the check asserted the per-pixel peak 255. DX-151 (skinned) passed only
  incidentally: it left `lightingEnabled` at the default `false`, steering selection to the per-pixel
  `Skinned3d` base shader. **Verified on real DXVK (Radeon 780M): the same DX-125 scene with
  `preferPerPixelLighting=true` yields exactly 255** — the per-pixel `lit_textured3d` shader is correct.
  Fixed by opting DX-125/DX-151 into per-pixel lighting + a reversed-light discriminator. `796885b3`.
- **Black vertex color (PBR quad D) = real production bug in the D3D11 draw path, unrelated to vertex
  color.** `D3D11GraphicsBackend::DrawPrimitivesExImpl` hardcoded `context->Draw(vertexCount, 0)`
  (StartVertexLocation) and `DrawIndexed(indexCount, 0, 0)` (StartIndex/BaseVertex), ignoring
  `GpuDrawParams::vertexStart/startIndex/baseVertex`. Quad D issues `DrawPrimitives(..., startVertex=6,
  2)`; with the offset dropped it silently redrew vertices 0..5 (quad C's region), leaving quad D's
  screen region at the green clear color — misread as a broken `Skinned3dVertexLitColored` shader.
  Diagnostic proof: the per-pixel `Skinned3dColored` variant *also* produced green on quad-D geometry
  at `startVertex=6`, while a fresh vb at `startVertex=0` drew fine. Fixed by threading the offsets
  into `Draw`/`DrawIndexed`; added a lighting-independent startVertex regression. `ed1d906b`.
**Verification** `D3D11_Pbr_VertexColor` 6/6 PASS; `D3D11_Smoke` **154/154**; full **D3D11 ctest shard
11/11** on real DXVK. **New finding split out:** the 8 direct-backend fog checks that also fail in
`D3D11_Smoke` are a *separate* regression from the GFX-005/010 fog campaign, tracked+fixed as
**REMED-GFX-055** (`0453dc1c`) — not part of this task's root causes.
**Original root cause (pre-investigation, retained for history)** Not determined for either. Two runtime-confirmed defects: (a) specular fails for
non-skinned `lit_textured3d` at a geometry where `dot(H,N)=1` exactly (should be a full-white highlight),
while the **structurally identical `skinned3d` check at the same geometry passes** — a genuine asymmetry;
(b) `skinned3dvertexlitcolored` produces green `(0,255,0,255)` instead of the expected black for an
explicitly black-colored vertex.
**Evidence** `D3D11_Smoke` 152/153 checks; `D3D11_Pbr_VertexColor` fails. Confirmed genuine via real
DXVK-backed execution (verified by DXVK log lines, not a WineD3D fallback).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_CROSS_CUTTING_FINDINGS.md` § D3D11
**Affected files** `src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d.frag.hlsl`,
`skinned_colored3d_vertexlit.*.hlsl` (exact files TBD by investigation)
**Backends / Platforms** D3D11 confirmed; **D3D12 shares `D3DCommon` and must be checked** / Windows
**XNA/FNA impact** Incorrect specular and vertex-color application for specific effect combinations.
**Security** N/A. **Memory/resource** N/A.
**Strategy** The specular asymmetry is the more tractable lead: two structurally identical checks
diverging at the same geometry points at a specific shader-line difference between the skinned and
non-skinned paths. Diff them first. Since both live in shared `D3DCommon`, verify whether D3D12 shows the
same failures — it has no test that would reveal them (`REMED-BUILD-008`).
**Required tests** The existing checks are adequate; add intermediate `dot(H,N)` values to distinguish a
boundary bug from a general one.
**Regression tests** Add specular-at-`dot(H,N)=1` and black-vertex-color to the cross-backend suite.
**Backend parity** Check D3D12 and every backend for the same asymmetry.
**Dependencies** `REMED-BUILD-001`; overlaps `REMED-GFX-005`/`-006`/`-008` in `D3DCommon` — serialize.
**Cx** MEDIUM · **PS** NO (shader serialization) · **Verify** **YES** — root cause unknown; investigate before fixing
**Completion** Both checks pass; root cause documented. **Verification** `D3D11_Smoke` 153/153 and
`D3D11_Pbr_VertexColor` pass under real DXVK.

### REMED-GFX-060 — D3D9 draw paths drop `DrawPrimitives`/`DrawIndexedPrimitives` vertex offsets

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** DONE (2026-07-21) — post-audit follow-up, split
out of `REMED-GFX-020`'s Phase-12 cross-backend draw-offset sweep.
**Root cause** Every effect-aware D3D9 draw site hardcoded the vertex/index offsets into the underlying
`IDirect3DDevice9` call — `DrawPrimitive(topology, 0 /*StartVertex*/, primCount)` and
`DrawIndexedPrimitive(topology, 0 /*BaseVertexIndex*/, 0 /*MinIndex*/, vertexCount /*NumVertices*/,
0 /*StartIndex*/, primCount)` — dropping the XNA `DrawPrimitives(vertexStart)` /
`DrawIndexedPrimitives(baseVertex, startIndex)` offsets that `GraphicsDevice` threads through
`GpuDrawParams::vertexStart/startIndex/baseVertex`. Identical defect class to `REMED-GFX-020`
(D3D11/D3D12), left unfixed there because D3D9 spreads it across several draw sites in a separate shader
model.
**Affected files (15 sites)** `src/CNA/Internal/Backends/D3D9/D3D9EffectDraw.cpp` (10: BasicEffect/
AlphaTestEffect/DualTextureEffect/EnvironmentMapEffect/SkinnedEffect, indexed + non-indexed),
`D3D9PbrDraw.cpp` (2), `D3D9SkinnedVertexColorDraw.cpp` (2), `D3D9InstancedDraw.cpp` (1). The
colored-primitive path (`D3D9GraphicsBackend.cpp` `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`)
and `D3D9SpriteBatch.cpp` carry no `GpuDrawParams` offsets and are correctly untouched.
**D3D9 parameter mapping** `DrawPrimitive(StartVertex ← vertexStart)`; `DrawIndexedPrimitive(BaseVertexIndex
← baseVertex, MinIndex = 0, NumVertices = vertexCount − baseVertex, StartIndex ← startIndex, PrimitiveCount
unchanged)`. `NumVertices` uses the in-buffer remainder (not full `vertexCount`) so a non-zero
`BaseVertexIndex` cannot declare an out-of-buffer range; `baseVertex = 0` reproduces the old value exactly.
**Backends / Platforms** D3D9 confirmed + fixed + runtime-verified / Windows (Wine + DXVK9).
**XNA/FNA impact** `DrawPrimitives`/`DrawIndexedPrimitives` with any non-zero offset silently drew from
vertex/index 0 on D3D9. **Security** N/A. **Memory/resource** N/A (integer draw-arg change only).
**Verification** New `D3D9_DrawOffset` test (7 checks, all 3 offset kinds × BasicEffect/Pbr/SkinnedVertexColor/
Instanced) **0/7 → 7/7** on real DXVK 2.6.0; full **D3D9 ctest shard 20/20**, zero regressions; D3D11
`d3d11_pbr_vertexcolor` (GFX-020 startVertex regression) still 6/6 (change is D3D9-local); EasyGL
public-API `vertexStart` control green; Vulkan honors by construction. D3D12 unchanged (fixed under
GFX-020; runtime still blocked by REMED-BUILD-012).
**Cx** MEDIUM · **PS** NO · **Verify** done. **Commits** `aa23eed2` (test), `d2491a17` (fix).
**Spawned** `REMED-GFX-061` (D3D-family scalar fog-field / stale `IGraphicsBackend.hpp` comment cleanup —
Phase-11 decision: separate documentation/deprecation task, not an offset-tranche change). Full detail in
`REMEDIATION_PROGRESS.md` § REMED-GFX-060.

### REMED-GFX-021 — Dx3 `SpriteBatch` 180° rotation defect, possibly shared with `SoftwareGraphicsBackend`

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Not determined. **New lead from Pass 6:** Dx3's rotation formula is a **byte-for-byte
verbatim port of `SoftwareGraphicsBackend.cpp`'s identical code** — so this may be a shared inherited
defect rather than Dx3-specific.
**Evidence** `Dx3_SpriteBatch` Check G (180° rotation about center) fails — **empirically confirmed in
Pass 6 by running the binary**, closing a long-standing multi-session investigation. Both forks
independently re-derived XNA's origin-scaled corner-rotation formula from scratch and confirmed the
**test's math is sound**, so a failure here is a real backend defect.
**Scope note — Check D is NOT part of this task.** Check D (zero-alpha blend) is a confirmed
**test-authoring bug**: the fixture uses a non-premultiplied `Color(255,0,0,0)` under
`BlendState::AlphaBlend`, a premultiplied-convention preset. Hand-derived real result: `(255,6,7)`, not
the asserted "destination untouched". Tracked separately as `REMED-TEST-003`.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` § Resolved standing investigations; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Dx3
**Affected files** `src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp`;
`src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp`
**Backends / Platforms** Dx3 confirmed; Software **suspected, unverified** / ALL
**XNA/FNA impact** Rotated sprites render incorrectly. **Security** N/A. **Memory/resource** N/A.
**Strategy** **First run Software's equivalent rotation test.** If it fails the same way, fix the shared
formula once and both backends close together. Do not fix Dx3 in isolation before checking.
**Required tests** Software needs a 180°-rotation `SpriteBatch` test if none exists.
**Regression tests** Add rotation angles (90/180/270/arbitrary) to the cross-backend suite.
**Backend parity** All 14 agree on rotated sprite placement.
**Dependencies** `REMED-BUILD-001`. **Cx** MEDIUM · **PS** YES · **Verify** **YES** — confirm whether Software shares it
**Completion** Rotation is correct on both; shared-vs-specific resolved. **Verification** `Dx3_SpriteBatch`
Check G passes and Software's equivalent passes.

### REMED-GFX-023 — `EffectParameter::Elements`/`StructureMembers` are permanently empty

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Nothing anywhere populates `elements_`/`members_` after construction (confirmed repo-wide grep).
**Evidence** `include/.../EffectParameter.hpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics
**Affected files** `EffectParameter.cpp`/`.hpp`; the effect-reflection population path
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Array- and struct-typed custom effect parameters silently report zero sub-elements
regardless of the shader's actual declaration. **Security** N/A. **Memory/resource** N/A.
**Strategy** Populate from shader reflection at effect-load time. Scope depends on how much reflection
data each backend surfaces — likely needs an `IGraphicsBackend` extension, making this **architecturally
larger than it looks**. Scope it before committing.
**Required tests** A custom effect with an array parameter and a struct parameter; assert correct counts.
**Regression tests** Cross-backend custom-effect reflection test. **Backend parity** All backends supporting custom effects.
**Dependencies** Related to `REMED-GFX-024` (both are `Effect::Parameters` population). **Cx** LARGE · **PS** CONDITIONAL · **Verify** NO
**Completion** Sub-elements reported correctly. **Verification** Array/struct reflection test passes.

### REMED-GFX-024 — `BasicEffect` never populates its own `Effect::Parameters` collection

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Unlike **all 6** sibling stock effects, `BasicEffect` has no `EffectParameter*` members, no
`CacheEffectParameters()`, and `OnApply()` is a literal no-op.
**Evidence** `src/.../BasicEffect.cpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics
**Affected files** `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** `effect.Parameters["DiffuseColor"]` — standard, documented XNA access that works on all
6 siblings — silently returns nothing for **the single most commonly used stock effect in the API**.
Rendering is unaffected (the draw path uses `FillGpuDrawParams()` directly).
**Security** N/A. **Memory/resource** N/A.
**Strategy** Port the `CacheEffectParameters()` pattern from a sibling (e.g. `AlphaTestEffect`). Well-trodden
in-repo; lower risk than `REMED-GFX-023`.
**Required tests** Generic `Parameters[...]` access for every `BasicEffect` property, mirroring an existing
sibling's test. **Regression tests** Assert all 7 stock effects populate `Parameters`, so a new effect
cannot ship without it. **Backend parity** N/A (XNA-facing).
**Dependencies** Related to `REMED-GFX-023`. **Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** `BasicEffect` populates `Parameters` like its siblings. **Verification** Generic-access test passes for all 7.

### REMED-GFX-025 — `VertexBuffer`/`IndexBuffer` have no destination-byte-offset `SetData` overload

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** **Confirmed root cause** of the `IVertexBufferBackend`/`IIndexBufferBackend::SetDataWithOptions()`
no-offset gap independently found in 3 backends. The gap does **not** originate at the backend interface —
it originates in the XNA-facing API: FNA's `VertexBuffer.cs`/`IndexBuffer.cs` both expose a genuine
`offsetInBytes` destination parameter as their most-general `SetData` overload, and this port dropped it entirely.
**Evidence** `include/.../VertexBuffer.hpp.audit.md`, `IndexBuffer.hpp.audit.md`. Symptom independently
confirmed in D3D11 (`D3D11Buffers.cpp`), EasyGL (`uploadWithOptions()`), D3D9
(`D3D9VertexBufferBackend::Upload()` — all hardcode offset 0).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Graphics; `AUDIT_GRAPHICS_BACKEND_MATRIX.md` (universal)
**Affected files** `VertexBuffer.hpp`/`.cpp`, `IndexBuffer.hpp`/`.cpp`, `IGraphicsBackend.hpp` buffer
interfaces, and every backend's buffer implementation
**Backends / Platforms** **ALL** — architecturally present on every backend, not just the 3 where a
backend audit tripped over the symptom / ALL
**XNA/FNA impact** Real ring-buffer/streaming dynamic vertex data across multiple draws per frame — the
scenario `SetDataOptions::NoOverwrite` exists for — is **architecturally impossible to express**, not merely
suboptimal. **Security** N/A.
**Memory/resource impact** D3D11's `D3D11_MAP_WRITE_NO_OVERWRITE` is a hard driver promise that the mapped
range is not being read by pending GPU work. With every call writing the same `[0, byteCount)` region,
a `NoOverwrite` write can race a still-in-flight GPU read. **Plausible, not reproduced.** Same contract
applies to D3D9's `D3DLOCK_NOOVERWRITE`.
**Strategy** Add FNA's `offsetInBytes` overload to the XNA-facing API, thread it through the backend
interface, then implement per backend. **Fix the XNA layer first** — fixing backends first would just add
an unreachable parameter.
**Required tests** Streaming writes at non-zero offsets with `NoOverwrite`, asserting earlier data is preserved.
**Regression tests** Cross-backend streaming test. **Backend parity** All 14 honor the offset.
**Dependencies** Related to `REMED-GFX-026` (both are `IGraphicsBackend` signature gaps) — consider one
coordinated interface revision. **Cx** LARGE · **PS** NO (touches every backend's buffer code) · **Verify** NO
**Completion** The offset overload exists and is honored everywhere. **Verification** Non-zero-offset
streaming test passes on all 14.

### REMED-GFX-026 — `IGraphicsBackend` state-apply signatures silently drop 3 real XNA properties

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `ApplySamplerState()` carries no `AddressW`; `ApplyBlendState()` carries no per-RT
`ColorWriteChannels` mask; `ApplyRasterizerState()` carries no `MultiSampleAntiAlias`. **Resolved by the
audit:** all three are fully real, correct, settable properties at the XNA class level — the gap is 100%
confined to the shared backend interface, so **every** backend is equally unable to honor them.
**Evidence** `IGraphicsBackend.hpp:656-658`; `D3D11SamplerCache.cpp`'s own comment honestly discloses
"IGraphicsBackend::ApplySamplerState has no addressW parameter (a pre-existing interface limitation, not
introduced here)" and works around it by reusing `addressV`. `D3D11StateObjectCache.cpp` similarly
hardcodes `D3D11_COLOR_WRITE_ENABLE_ALL` and `MultisampleEnable = FALSE`.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Architecture;
`AUDIT_GRAPHICS_BACKEND_MATRIX.md` XNA-facing row
**Affected files** `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` + every backend's `Apply*State`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** `SamplerState.AddressW` (used for `Texture3D`/volume wrapping) and
`BlendState.ColorWriteChannels` are documented, settable XNA properties that silently do nothing on every
backend. **Security** N/A. **Memory/resource** N/A.
**Strategy** Extend the three signatures and implement per backend. **Consider addressing the broader
"silent-default-degradation" finding at the same time** — `IGraphicsBackend`'s optional methods default to
silent no-ops with `SupportsCapability()` defaulting to `true` for everything, which is the architectural
reason gaps like this and `REMED-GFX-012` go unnoticed. SdlRenderer and Dx3 demonstrate the good
counter-pattern (every unsupported method explicitly throws).
**Required tests** `AddressW` differing from `AddressV` on a `Texture3D`; per-RT color-write masking.
**No test anywhere currently exercises `AddressW`** — a grep across `tests/` and `examples/` found nothing.
**Regression tests** Add all three to the cross-backend state suite. **Backend parity** All 14 honor them or
explicitly report non-support.
**Dependencies** Coordinate with `REMED-GFX-025` as one interface revision. **Cx** LARGE · **PS** NO · **Verify** NO
**Completion** All three properties are honored or explicitly rejected. **Verification** New state tests pass on all 14.

### REMED-GFX-027 — State mutated before the fallible call that can reject it (2 XNA-layer instances)

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** A genuine repeated authoring pattern — mutate optimistically, discover invalidity only via
a later exception. Two XNA-layer instances: `SpriteBatch::Begin()` sets `begun_ = true` **before** backend
calls that can throw, permanently wedging the object on failure; `GraphicsDevice::SetRenderTargets`
mutates `currentRenderTargets_`/`renderTargetBound_` **before** the backend call that actually throws for
MRT-unsupported backends.
**Evidence** Found via `sdlrenderer_custom_effect_throws_test.cpp` and
`sdlrenderer_rendertargets_mrt_throws_test.cpp` audits. A third instance of the identical shape is
`REMED-GFX-001` (EasyGL window registry). A **positive counter-example** exists: D3D11's MRT-finalization
bug (DX-143) was the same risk pattern, found already correctly handled.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` § Recurring architecture pattern; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Architecture
**Affected files** `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`, `GraphicsDevice.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** After a caught exception the object is left in an inconsistent state, so recovery is
impossible even though the caller handled the error correctly. **Security** N/A.
**Memory/resource impact** No leak, but permanently wedged objects.
**Strategy** Move state mutation **after** the fallible call, or use a scope-guard rollback. Then grep for
the pattern project-wide — three instances found incidentally means more exist.
**Required tests** Force the backend call to throw; assert the object is still usable afterward.
**Regression tests** Add exception-safety cases to the state-mutating API tests. **Backend parity** N/A.
**Dependencies** `NO` against `REMED-GFX-002`/`-003` (`SpriteBatch.cpp`) and `REMED-GFX-043` (`GraphicsDevice.cpp`).
**Cx** MEDIUM · **PS** NO · **Verify** NO
**Completion** Both mutate only after success; the project-wide sweep is recorded.
**Verification** Throw-then-reuse tests pass.

### REMED-GFX-028 — SdlGpu constructor leaks device+window+pipelines if any of 10 fallible calls throws

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** The constructor creates the SDL GPU device and claims the window (with correct explicit
cleanup for `SDL_ClaimWindowForGPUDevice` failure specifically), then makes 10 sequential
`Create*Resources()` calls **entirely unwrapped by any try/catch**. A constructor that throws never runs
the destructor, so everything created so far leaks.
**Evidence** `SdlGpuGraphicsBackend.cpp` ~487-543. Plausible failure mode, per the constructor's own
comment that non-Linux shader-format support is incomplete. The destructor performs a complete, correct
teardown of exactly these resources — verified by direct comparison — it just never runs.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Architecture
**Affected files** `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
**Backends / Platforms** SdlGpu / ALL, most likely on non-Linux
**XNA/FNA impact** N/A · **Security** N/A
**Memory/resource impact** GPU device, claimed window, and any successfully-created pipelines/shaders all leak.
**Strategy** Wrap the sequence in try/catch + cleanup + rethrow. **WebGPU's constructor is this audit's
designated model example** of exactly this pattern — copy its structure rather than inventing one.
**Required tests** Inject a failure into each of the 10 calls; assert no leak. **Regression tests** Run under
ASan/LeakSanitizer. **Backend parity** Audit every backend's constructor for the same unwrapped-sequence shape
(the matrix marks most `?`).
**Dependencies** None. **Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** Constructor is exception-safe; no resource survives a failed construction.
**Verification** LeakSanitizer clean under injected failures.

### REMED-GFX-029 — Dx3: failed resize destroys working surfaces before confirming the replacement

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `SetVirtualResolution` destroys the primary/backbuffer surfaces before confirming the
replacement succeeded, leaving the backend permanently unusable on any subsequent draw.
**Evidence** `src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp.audit.md` F1
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics
**Affected files** `Dx3GraphicsBackend.cpp` · **Backends/Platforms** Dx3 / ALL
**XNA/FNA impact** A recoverable failure becomes unrecoverable. **Security** N/A
**Memory/resource impact** Working resources destroyed with no replacement — a permanently broken object.
**Strategy** Create the replacement first, swap on success, destroy the old only after. Same
"commit-before-confirm" family as `REMED-GFX-027`, in a resource-lifetime rather than state-flag form.
**Required tests** Force resize failure; assert the backend still draws with the previous resolution.
**Regression tests** Add resize-failure recovery to the backend lifecycle suite.
**Backend parity** Check every backend's resize path for destroy-before-replace.
**Dependencies** None. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** A failed resize leaves the backend fully usable. **Verification** Resize-failure test draws successfully afterward.

### REMED-GFX-030 — Software: `DepthBufferWriteEnable` inert and `DepthBufferFunction` hardcoded to LessEqual

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Two findings in one method (`ApplyDepthStencilState`): depth is always written regardless of
`DepthBufferWriteEnable`/`SetDepthWriteEnabled`, and `DepthBufferFunction` is ignored — LessEqual is hardcoded,
so the other 6 `CompareFunction` values do nothing.
**Evidence** `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp.audit.md` F1/F2
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics; `AUDIT_GRAPHICS_BACKEND_MATRIX.md`
**Affected files** `SoftwareGraphicsBackend.cpp` · **Backends/Platforms** Software / ALL
**XNA/FNA impact** Two documented, settable XNA `DepthStencilState` properties are non-functional. Common
techniques (depth-write-off transparency passes, `Greater`/`Always` depth tests) silently do nothing.
**Security** N/A · **Memory/resource** N/A
**Strategy** Thread both into the rasterizer's depth test/write. The backend is a genuine from-scratch
rasterizer with correct perspective-correct interpolation and Sutherland-Hodgman clipping, so the
surrounding code is sound — this is a plumbing gap, not a design problem.
**Required tests** All 7 `CompareFunction` values; depth-write-disabled preserving prior depth.
**Regression tests** Add the full `CompareFunction` matrix to the cross-backend suite. Note
`easygl_depthstencilstate_compare_function_test.cpp` tests only **5 of 8** values (`REMED-TEST-005`).
**Backend parity** All 14 honor both properties.
**Dependencies** `REMED-BUILD-001`. **Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** Both properties functional. **Verification** Full `CompareFunction` matrix passes on Software.

### REMED-GFX-033 — `Byte4`/`Short2`/`Short4` `Pack()` truncate instead of round (+ the harness gap that hid it)

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `static_cast<uint32_t>(clamp(x,0,255))` instead of FNA's `(uint)Math.Round(...)` — a
systematic off-by-up-to-1 for **any** non-integer input, not just boundary ties. Sibling
`NormalizedByte2/4`/`NormalizedShort2/4` correctly use `std::lroundf`.
**Root cause of why it was never caught** (a second, equally important half):
`tools/fna-reference/PackedVectorReference.cs`'s `DumpByte4()`/`DumpShort2()`/`DumpShort4()` use
**exclusively integer test inputs** — unlike all 14 sibling `Dump*()` functions, which deliberately include
a fractional value because their formulas are rounding-sensitive. For exact-integer input,
`Round(x) == Truncate(x)`, so the project's own FNA-vs-CNA comparison harness (Task 479) was
**structurally incapable** of catching this. The tool correctly calls real FNA types, so its reference data
is trustworthy — only the input choice was wrong.
**Evidence** `audit/tools/fna-reference/PackedVectorReference.cs.audit.md`; the three header audits
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core; `AUDIT_FINAL_REPORT.md` §2 item 12
**Affected files** `include/.../PackedVector/{Byte4,Short2,Short4}.hpp`;
`tools/fna-reference/PackedVectorReference.cs`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Direct arithmetic divergence for non-integer input. **Security** N/A · **Memory/resource** N/A
**Strategy** Use `std::lroundf` as the siblings do, **and** add fractional inputs to the three `Dump*()`
functions. Fixing only the packing leaves the harness blind to the next instance — fix both.
**Required tests** Fractional inputs (0.25, 0.5, 0.75) for all three types; regenerate FNA reference data.
**Regression tests** Audit **all 17** `Dump*()` functions for rounding-sensitive coverage.
**Backend parity** N/A. **Dependencies** Overlaps `REMED-CORE-004`/`REMED-GFX-034` (same directory).
**Cx** SMALL (fix) / MEDIUM (harness + regeneration) · **PS** YES · **Verify** NO
**Completion** All three round; the harness uses rounding-sensitive inputs.
**Verification** Regenerated FNA reference data matches CNA byte-for-byte for fractional inputs.

### REMED-GFX-034 — All 16 `PackedVector` types lack `Equals()`/`GetHashCode()`/`ToString()`

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Never implemented. Confirmed a genuine CNA gap, **not** FNA-inherited and **not** a
project-wide convention: FNA's `Byte4.cs` implements all three non-trivially, and CNA's own
`GamePadState`/`MouseState`/`KeyboardState`/`Vector2`/`Color`/`Rectangle` all implement them correctly —
making this an isolated gap in one type family.
**Evidence** Pass 3 systematic xn65 XML sweep; direct read of 4 representative headers (0 matches for any of the three).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Pass 6 continued; `AUDIT_CROSS_CUTTING_FINDINGS.md` § `.Graphics.PackedVector`
**Affected files** all 16 `include/.../Graphics/PackedVector/*.hpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** `operator==`/`!=` cover direct comparison so nothing is silently *wrong* — but a
`Byte4`/`Short2` in a hash-based container, or debug-printed, **simply will not compile.** A hard
compile-time gap, not a subtle behavioral one.
**Security** N/A · **Memory/resource** N/A
**Strategy** Implement all three per FNA (`GetHashCode()` → `packedValue`'s hash; `ToString()` → hex).
Mechanical and highly parallelizable across the 16 types.
**Required tests** Per type: equality consistency, hash consistency for equal values, `ToString()` format.
**Regression tests** A compile-time test placing each type in an `unordered_map`.
**Backend parity** N/A. **Dependencies** Overlaps `REMED-GFX-033`/`REMED-GFX-049` — do all PackedVector work together.
**Cx** MEDIUM (16 types, mechanical) · **PS** YES · **Verify** NO
**Completion** All 16 implement all three. **Verification** Hash-container compile test passes for every type.

### REMED-GFX-035 — SdlGpu rejects EasyGL-compatible custom GLSL effect content

**Sev** HIGH · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** SdlGpu's SPIR-V-based pipeline enforces a stricter GLSL dialect than EasyGL's permissive
raw-OpenGL path: it requires `#version 310 es`+, explicit `location` qualifiers, and block uniforms.
**Evidence** `CnjEffectTest.LoadsRealCnjFixture` and `CnjStockEffectTest.CustomGlslEffectStillWorks` both
fail on SdlGpu against a **real, already-committed** test-fixture shader that works on EasyGL.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_CROSS_CUTTING_FINDINGS.md` § SdlGpu
**Affected files** SdlGpu shader-compilation path; `tests/assets/**` CNJ/GLSL fixtures; `docs/`
**Backends / Platforms** SdlGpu / ALL
**XNA/FNA impact** A real cross-backend compatibility gap for **user-authored** effect content — an effect
that works on the default backend fails on another, with no documented dialect requirement.
**Security** N/A · **Memory/resource** N/A
**Strategy** **This is a judgment call for the project owner, not a mechanical fix.** Three viable
resolutions: (a) SdlGpu auto-upgrades older GLSL; (b) document a stricter dialect requirement for effect
authors and update the fixtures; (c) fix only the fixtures (leaves real user content broken).
The audit deliberately declined to choose. **Decide before implementing.**
**Required tests** Once decided: either the existing fixtures pass on SdlGpu, or a documented
dialect-conformance test exists. **Regression tests** Run custom-effect tests on every backend supporting them.
**Backend parity** Establish which dialect is the contract, and enforce it uniformly.
**Dependencies** `REMED-BUILD-001`; owner decision required. **Cx** MEDIUM–LARGE (depends on choice) · **PS** YES · **Verify** NO
**Completion** A single documented dialect contract holds across backends.
**Verification** Custom-effect tests pass on every supporting backend.

### REMED-GFX-036 — `WireFrame` capability: test-authoring bug on 3 backends, possible shared default flag on 2

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Two different underlying causes behind one failing test:
(a) **Test-authoring bug** — `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` hardcodes an
EasyGL/GLES3-specific limitation as a universal truth. Vulkan, SdlGpu, and Bgfx **genuinely support**
wireframe fill mode, so the assertion is simply wrong for them.
(b) **Possible shared-default bug** — Headless does zero real GPU work yet reports `true`, and Software is
a CPU rasterizer that could plausibly implement wireframe trivially. Headless claiming support while
rendering nothing suggests `WireFrame` may default to `true` in a shared `IGraphicsBackend` implementation
that only some backends override.
**Evidence** Fails identically on 5 backends (Vulkan, SdlGpu, Bgfx, Software, Headless).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Pass 6 continued; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Pass 6 final
**Affected files** the capability test; `IGraphicsBackend::SupportsCapability` default; per-backend overrides
**Backends / Platforms** 5 confirmed / ALL
**XNA/FNA impact** A capability query returning a wrong answer misleads callers into using an unsupported
feature (or skipping a supported one). **Security** N/A · **Memory/resource** N/A
**Strategy** **Check `SupportsCapability`'s default implementation directly** before assuming this is
purely a test bug — the audit explicitly warns against that assumption. Then correct the test to be
per-backend, and fix any genuinely wrong capability reports. Connects to the broader
silent-default-degradation finding (`REMED-GFX-026`).
**Required tests** Per-backend expected wireframe support, plus an actual wireframe render where supported.
**Regression tests** Audit every capability flag for the same universal-assertion shape.
**Backend parity** Every backend reports its real capabilities. **Dependencies** `REMED-BUILD-001`; related to `REMED-GFX-026`.
**Cx** MEDIUM · **PS** YES · **Verify** **YES** — determine per backend whether it is the test or the flag
**Completion** Capability reports are accurate on all 14; the test is per-backend.
**Verification** The test passes on all 14 with correct per-backend expectations.

### REMED-GFX-037 — `SDL_Renderer_FullscreenToggle` terminates the process with an uncaught exception

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** An uncaught `std::runtime_error` ("`ReadBackbuffer`: physical/logical size mismatch
(letterbox or stretch scaling active) — exact-pixel readback unsupported") during a fullscreen toggle
aborts the whole test process instead of failing as a clean assertion. CTest reports "Subprocess aborted."
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "NEW, real robustness gap"
**Affected files** `SdlRendererGraphicsBackend` `ReadBackbuffer`; `examples/sdlrenderer_fullscreen_toggle_test.cpp`
**Backends / Platforms** SdlRenderer / ALL
**XNA/FNA impact** An unhandled exception escaping to `std::terminate` is a robustness gap regardless of API.
**Security** N/A · **Memory/resource** N/A
**Strategy** Decide the intent first: if the test should not read back during a scaling mismatch, fix the
test; if verifying behavior *during* that state is the point, production code must surface it via the
project's exception-safe check pattern. Either way an exception must not escape to `terminate`.
Related shape: `examples/*` demos have no top-level handler, so an unguarded `Content.Load<Model>()` also
reaches `std::terminate` — worth addressing as a class.
**Required tests** Fullscreen toggle with letterbox scaling active, asserting a clean failure or success.
**Regression tests** Ensure no test aborts the process rather than failing.
**Backend parity** Check every backend's readback for the same unguarded throw.
**Dependencies** `REMED-BUILD-001`. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** No uncaught exception escapes. **Verification** The test reports a clean pass/fail, never "Subprocess aborted."

### REMED-GFX-038 — `GraphicsDevice::Dispose()` disposes resources before raising `Disposing`

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Inverted relative to FNA's real order (event first, then teardown).
**Evidence** `src/.../GraphicsDevice.cpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core;
`AUDIT_GRAPHICS_BACKEND_MATRIX.md` (universal)
**Affected files** `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** A `Disposing` handler can **never** observe a still-valid resource — the entire purpose
of the event. Any FNA-correct cleanup handler is broken. **Security** N/A
**Memory/resource impact** Handlers may touch already-disposed resources.
**Strategy** Raise `Disposing` first, then dispose owned resources. Watch for CNA code that depends on the
current (wrong) order. **Required tests** A `Disposing` handler asserting resources are still valid.
**Regression tests** Full disposal-ordering test. **Backend parity** N/A.
**Dependencies** `NO` against `REMED-CORE-002` (same file). **Cx** SMALL · **PS** CONDITIONAL · **Verify** NO
**Completion** Event precedes teardown, matching FNA. **Verification** The handler test observes valid resources.

### REMED-GFX-039 — XNA-facing validation gaps: `RenderTargetBinding`, `TextureCollection`, `VertexDeclaration`

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Three missing validations FNA performs: (a) `RenderTargetBinding`'s two-argument
constructors have **zero** validation (FNA throws `ArgumentNullException` for a null texture and
`ArgumentOutOfRangeException` for an invalid `CubeMapFace`) and it carries an undisclosed,
non-`NOXNA`-tagged `arraySlice` extension; (b) `TextureCollection` lacks FNA's render-target/sampler
conflict check — binding a texture that is simultaneously an active render target silently succeeds
instead of throwing `InvalidOperationException`; (c) `VertexDeclaration`'s auto-stride constructor does not
validate an empty element list.
**Evidence** the three per-file header audits · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics
**Affected files** `RenderTargetBinding.hpp`, `TextureCollection.hpp`, `VertexDeclaration.hpp` (+ `.cpp`)
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** (b) is the most consequential — reading a bound render target is undefined at the GPU
level on most APIs, and XNA throws specifically to prevent it. **Security** N/A
**Memory/resource impact** (b) can produce GPU-level undefined behavior.
**Strategy** Add each validation per FNA. Tag or remove `arraySlice` per the `NOXNA` convention.
**Required tests** Null texture, invalid `CubeMapFace`, bind-active-render-target, empty element list.
**Regression tests** Add to the shared graphics-validation suite. **Backend parity** Confirm the RT-conflict
throw fires before any backend call. **Dependencies** Overlaps `REMED-CORE-002`.
**Cx** SMALL · **PS** YES · **Verify** NO
**Completion** All three validate per FNA; `arraySlice` is correctly tagged.
**Verification** Each negative case throws the FNA-documented exception type.

### REMED-GFX-040 — `ModelMeshPartCollection`/`ModelEffectCollection::operator[](int)` unchecked indexing

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Both use unchecked `std::vector::operator[]` where FNA's `ReadOnlyCollection<T>` always
bounds-checks. **Contrast within the same family:** `ModelBoneCollection`/`ModelMeshCollection` correctly
use `.at()` — an isolated lapse, not a family convention. Same shape as `REMED-NET-002`.
Additionally `Model.cpp` has 5 raw `std::out_of_range`/`std::runtime_error` sites where FNA documents `System.*` types.
**Evidence** the collection audits; `src/.../Model.cpp.audit.md`
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics
**Affected files** `ModelMeshPartCollection.hpp`, `ModelEffectCollection.hpp`, `Model.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Out-of-range indexing is UB where XNA throws `ArgumentOutOfRangeException`.
**Security** UB via public API. **Memory/resource** Out-of-bounds vector access.
**Strategy** Switch to `.at()` (or explicit checks) throwing `System::ArgumentOutOfRangeException`; fix
`Model.cpp`'s 5 throw sites in the same change (overlaps `REMED-CORE-002`).
**Required tests** Negative and past-the-end indices for both collections.
**Regression tests** Add out-of-range cases to the shared collection matrix.
**Backend parity** N/A. **Dependencies** Coordinate with `REMED-CORE-002`. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** Both bounds-check; `Model.cpp` uses `System::` types.
**Verification** UBSan passes the out-of-range tests.

### REMED-GFX-041 — `VertexBufferBinding.VertexOffset` modeled as a vertex count, not FNA's byte offset

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `VertexBufferBinding.cs` explicitly documents **bytes**; CNA models a vertex-count offset — a
genuine unit-semantics divergence.
**Evidence** `include/.../VertexBufferBinding.hpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics
**Affected files** `VertexBufferBinding.hpp`/`.cpp`; `GraphicsDevice`'s consumption of the field
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Code ported from XNA/FNA passing a byte offset gets a wrong result silently — worse than
a compile error, because the types match.
**Security** A large vertex-count value interpreted as an offset could read out of bounds.
**Memory/resource** Possible OOB read.
**Strategy** **Verify how `GraphicsDevice` actually consumes the field before changing the semantics** — the
audit explicitly flags this cross-check as unresolved. If consumption is internally consistent, changing the
unit is a breaking change requiring a coordinated update; if inconsistent, that is a second bug.
**Required tests** Non-zero offsets in both interpretations against known vertex data.
**Regression tests** Cross-backend multi-binding test with non-zero offsets.
**Backend parity** All 14 interpret the offset identically. **Dependencies** Related to `REMED-GFX-025` (also offset semantics).
**Cx** MEDIUM · **PS** YES · **Verify** **YES** — resolve the consumption cross-check first
**Completion** Offset is byte-based per FNA and consumed consistently.
**Verification** Non-zero-offset test produces FNA-matching results on all 14.

### REMED-GFX-042 — `VertexPositionColor` does not implement `IVertexType`

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Declared with no base class and only a `getVertexDeclarationStatic()` helper, while its 3
siblings (`VertexPositionColorTexture`, `VertexPositionNormalTexture`, `VertexPositionTexture`) all
correctly declare `: public IVertexType` with `getVertexDeclarationProperty() const override`.
**Evidence** Confirmed **twice by independent methods**: the `xna-graphics` per-file audit, and Pass 3's
xn65 XML surface sweep — a genuine second confirmation, not a duplicate report.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Pass 3
**Affected files** `include/.../Graphics/VertexPositionColor.hpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Generic code constrained on `IVertexType` — a real XNA pattern — cannot accept the single
most basic vertex type. **Security** N/A · **Memory/resource** N/A
**Strategy** Add the base and the override, mirroring the 3 siblings.
**Required tests** Use `VertexPositionColor` through an `IVertexType`-constrained path.
**Regression tests** A compile-time test asserting all 4 vertex types satisfy `IVertexType`.
**Backend parity** N/A. **Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Implements `IVertexType` like its siblings. **Verification** The constrained-path test compiles and passes.

### REMED-GFX-044 — `DisplayMode` missing `TitleSafeArea`, `GetHashCode()`, `ToString()`

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Never ported. All three are real XNA 4.0 members, **present in FNA with trivial
implementations** (`TitleSafeArea` = `new Rectangle(0,0,Width,Height)`; `ToString()` a simple formatter,
`DisplayMode.cs:52-58,105-113`).
**Evidence** Pass 3 xn65 XML sweep — one of only 2 genuine MEDIUM surface gaps found across ~2700 members.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Pass 3
**Affected files** `include/.../Graphics/DisplayMode.hpp` (+ `.cpp`)
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Title-safe-area-aware UI layout code has **no CNA equivalent to call** — no workaround exists.
**Security** N/A · **Memory/resource** N/A
**Strategy** Port all three from FNA verbatim; trivially portable.
**Required tests** `TitleSafeArea` for several modes; `ToString()` format; `GetHashCode()` consistency.
**Regression tests** Add to the `DisplayMode` test file. **Backend parity** N/A.
**Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** All three present and FNA-matching. **Verification** Format and value tests pass.

### REMED-DEVICES-002 — `Dispose(bool)` re-declared `public` in all 4 `Microsoft::Devices::Sensors` classes

**Sev** MEDIUM · **Pri** P2 · **Owner** DEVICES · **Status** NOT STARTED
**Root cause** `Accelerometer`, `Compass`, `Gyroscope`, and `Motion` each re-declare `Dispose(bool disposing)`
as `public`, even though the base `SensorBase<T>` correctly declares it `protected`.
**Evidence** Originally found in `Accelerometer`/`Compass`; the `Gyroscope`/`Motion` fork missed it despite a
thorough thread-safety review, so it was **independently re-verified by grepping all four headers' access
specifiers against the base class** and confirmed in all four.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Content/Storage/Net/Devices;
`include/Microsoft/Devices/Sensors/Accelerometer.hpp.audit.md` + 3 siblings
**Affected files** the 4 sensor headers · **Backends/Platforms** N/A / ALL with sensors enabled
**XNA/FNA impact** N/A (FNA never implemented WP7 sensor APIs). Breaks the standard .NET `Dispose(bool)` idiom.
**Security** N/A
**Memory/resource impact** **Real, externally-reachable resource leak plus a permanently-broken object.** An
external caller can invoke `accel.Dispose(false)`; each class's `!disposing` branch sets `disposed_ = true`
**without** running any cleanup — no `Stop()`, no instance-count decrement, no SDL-subsystem release, no
`control_->owner` nulling. Every subsequent call then throws `ObjectDisposedException`.
**Strategy** Change all four to `protected`, matching the base. **A source-compatibility break** for any code
calling it externally — grep first, though such a call is precisely the bug.
**Required tests** A compile-time assertion that `Dispose(bool)` is not publicly accessible on all four.
**Regression tests** Assert `Dispose()` (the public one) runs full cleanup.
**Backend parity** N/A. **Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** All four declare it `protected`. **Verification** The access-level compile test passes.

### REMED-MEDIA-003 — `VideoDecoder::ConvertFrameToRGBA()` indexes frames with stale cached dimensions

**Sev** MEDIUM · **Pri** P2 · **Owner** MEDIA · **Status** NOT STARTED
**Root cause** Uses cached `width_`/`height_` rather than the decoded frame's own dimensions.
**Evidence** `src/CNA/Internal/Media/VideoDecoder.cpp.audit.md`. Notable: this file has the **densest
prior-review-fix documentation in the whole audit** (18+ cited `plans/plan_media.md` findings) yet does not
address this case.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Content/Storage/Net/Media
**Affected files** `src/CNA/Internal/Media/VideoDecoder.cpp`
**Backends / Platforms** N/A / ALL with FFmpeg
**XNA/FNA impact** N/A · **Security** A crafted video that changes resolution mid-stream triggers an OOB read.
**Memory/resource impact** Potential OOB read. Low likelihood for the authored-cutscene use case, but unguarded.
**Strategy** Index using the frame's own `width`/`height`; handle mid-stream resolution change explicitly
(reallocate or fail cleanly).
**Required tests** A test video that changes resolution mid-stream.
**Regression tests** Add to the video-decoder suite. **Backend parity** N/A.
**Dependencies** `REMED-BUILD-001` (`VideoDecoderTest` is among the ~220). **Cx** SMALL · **PS** YES · **Verify** **YES** — construct the resolution-changing fixture and confirm
**Completion** Frame indexing uses per-frame dimensions. **Verification** ASan clean on the resolution-change fixture.

### REMED-MEDIA-004 — `MediaLibrary::SavePicture(name, Stream*)` assumes one `Read()` fills the buffer

**Sev** MEDIUM · **Pri** P2 · **Owner** MEDIA · **Status** NOT STARTED
**Root cause** The `Read()` return value is discarded entirely, violating this project's own
`System::IO::Stream::Read()` contract, which explicitly documents that a single call may return fewer bytes
than requested.
**Evidence** `src/.../Media/MediaLibrary.cpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Media
**Affected files** `src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp`
**Backends / Platforms** N/A / ALL
**XNA/FNA impact** N/A (FNA's `MediaLibrary` is a stub). A **confirmed defect against CNA's own interface
contract**, not a parity gap. **Security** N/A
**Memory/resource impact** For any `Stream` returning a legitimate partial read, the trailing buffer stays
zeroed and is **silently saved as if complete** — silent data corruption.
**Strategy** Loop until the buffer is full or EOF, checking each return value.
**Required tests** A `Stream` subclass that deliberately returns partial reads.
**Regression tests** Grep for other single-`Read()`-call sites — this contract is easy to violate repeatedly.
**Backend parity** N/A. **Dependencies** None. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** Reads loop to completion. **Verification** The partial-read stream test saves a byte-exact image.

### REMED-NET-004 — `GamerPresence`'s display-string table is alphabetically sorted, not enum-indexed

**Sev** MEDIUM · **Pri** P2 · **Owner** NET · **Status** NOT STARTED
**Root cause** `presenceModeStrings_` is sorted alphabetically while `setPresenceModeProperty()` indexes it
by raw enum ordinal, so **59 of 60 modes resolve to the wrong string** (verified programmatically, not by
inspection: `None` → "Arcade Mode", `CornflowerBlue` → "Won the Game").
**Evidence** `src/.../GamerServices/GamerPresence.cpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM
**Affected files** `src/Microsoft/Xna/Framework/GamerServices/GamerPresence.cpp`
**Backends / Platforms** N/A / ALL
**XNA/FNA impact** FNA has no `GamerServices`. **Currently dormant** — no public getter exposes the resolved
string, and its only consumer `SetPresenceModeStringEXT` is a permanent no-op — but it becomes a live,
silent, wrong-everywhere bug the instant either half is implemented.
**Security** N/A · **Memory/resource** N/A
**Strategy** Reindex to enum ordinals, or use an explicit ordinal→string map that cannot drift.
**A map keyed by enum value is strongly preferred** — the current design fails silently on any future enum
reorder, which is exactly how this arose.
**Required tests** Assert every one of the 60 modes maps to its correct string.
**Regression tests** A static assertion that the table size matches the enum's value count.
**Backend parity** N/A. **Dependencies** None. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** All 60 modes resolve correctly and cannot drift. **Verification** The 60-case test passes.

### REMED-BUILD-005 — 2 mixer-destroy harnesses omit an explicit `SDL3::SDL3` link

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** `cna_audio_mixer_destroy_active_static_voice_harness` and `..._dynamic_voice_harness` rely on
fragile transitive propagation from `CNA` instead of linking `SDL3::SDL3` explicitly.
**Evidence** Independently rediscovered by **3 of 3 non-native-GCC toolchains** (Canvas/Emscripten, D3D11
MinGW, D3D9 MinGW), all hitting the identical `fatal error: 'SDL3/SDL.h' file not found`.
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Recurring build-config finding … 3 independent toolchains"
**Affected files** `cmake/Harnesses.cmake`
**Backends / Platforms** N/A / **cross-compilation targets only** — harmless on every native desktop build
this project's CI already exercises
**XNA/FNA impact** N/A · **Security** N/A · **Memory/resource** N/A
**Strategy** Add `SDL3::SDL3` to both `target_link_libraries()` calls, matching other `AudioMixer.hpp`
consumers. Then audit all targets for the same implicit-transitive assumption.
**Required tests** N/A **Regression tests** A cross-compile CI job would catch this class; currently none exists
for Emscripten. **Backend parity** N/A. **Dependencies** None.
**Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Both harnesses build under all 3 toolchains. **Verification** A successful Emscripten and MinGW build.

### REMED-BUILD-006 — Root `.gitignore`'s bare `build*` silently untracks files repo-wide

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Line 1 is a bare `build*`. Gitignore patterns without a leading `/` match at **any** depth
against the basename, so it matches any file or directory anywhere whose name starts with "build."
**Evidence** Discovered **by the audit process itself**: `audit/manifest/build-cmake.md` and
`build-cmake-tests.md` were silently invisible to plain `git add`/`git status` and required `git add -f`.
Confirmed via `git status --porcelain --ignored=matching audit/`, which lists exactly those files with `!!`.
The project's real build directories are separately listed as `cmake-build-<backend>/*` and are unaffected.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Testing/documentation; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Repo-hygiene
**Affected files** `.gitignore`
**Backends / Platforms** N/A / ALL
**XNA/FNA impact** N/A · **Security** A file intended to be tracked can be silently omitted from a commit —
including, in principle, a security fix in a file named `build*`.
**Memory/resource** N/A
**Strategy** Narrow to `/build/` or `/build*/`, matching the already-precise `cmake-build-*/*` entries just
below it. Then check whether any currently-untracked file is being masked.
**Required tests** N/A **Regression tests** `git status --porcelain --ignored=matching` should show no
unintentionally-ignored source file. **Backend parity** N/A. **Dependencies** None.
**Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** The pattern is anchored; no intended file is ignored. **Verification** The `--ignored=matching` check is clean.

### REMED-BUILD-008 — D3D12 has exactly one CTest, leaving its two most significant findings unverifiable

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** `cmake/Tests/D3D12Tests.cmake` registers exactly one test (`D3D12_Smoke`) for the entire backend.
**Evidence** `D3D12_Smoke` itself is **exceptionally rigorous** (220/220 checks under real vkd3d-proton:
exact byte-level readback, NPOT alignment, all 4 `DepthFormat` values, stencil-plane clear+readback, full
mip-chain regeneration on both RT types, MSAA resolve, windowless device construction, pixel-exact
`DrawString`, `Model::Draw()`'s bone pipeline, PNG round-trip, runtime-compiled HLSL) — **but its
stencil/depth checks only exercise the direct clear-value path, not `DepthStencilState`/`RasterizerState`'s
settable properties**, so they never reach the bugs in `REMED-GFX-014`/`REMED-GFX-015`.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Testing infrastructure; `AUDIT_CROSS_CUTTING_FINDINGS.md` § D3D12
**Affected files** `cmake/Tests/D3D12Tests.cmake`; new D3D12 test executables
**Backends / Platforms** D3D12 / Windows (via MinGW + Wine + vkd3d-proton, this project's established pattern)
**XNA/FNA impact** Indirect — two HIGH findings cannot be confirmed or refuted at runtime.
**Security** N/A · **Memory/resource** N/A
**Strategy** Add dedicated stencil, scissor, and multi-draw-occlusion tests. `D3D12_Smoke` is the quality
bar to match. **This is a prerequisite for `REMED-GFX-014` and `REMED-GFX-015`, not a follow-up** — without
it those fixes are unverifiable.
**Required tests** As above. **Regression tests** Bring D3D12's test count nearer its siblings' (D3D11: 3, D3D9: 14).
**Backend parity** N/A. **Dependencies** Blocks `REMED-GFX-014`, `REMED-GFX-015`.
**Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** D3D12 has tests reaching both findings' trigger conditions.
**Verification** New tests run under real vkd3d-proton and demonstrate the defects pre-fix.

### REMED-TEST-002 — `GameTests.cpp`/`GraphicsDeviceManagerTests.cpp` have zero coverage; `GameCrashTest.cpp` is dead

**Sev** HIGH · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Each of the first two is a 2-line file containing only a comment explaining that
`Game`/`GraphicsDeviceManager` require a live SDL window. `GameCrashTest.cpp`'s 24 lines are entirely
commented out behind a stale `#ifdef XNA5` gate referencing an API shape the current `Game` class does not expose.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § tests-xna-framework-core. This leaves **both** confirmed
production HIGH bugs — `REMED-CORE-006` and `REMED-CORE-007` — completely untested, so neither would be
caught by CI today. `GameWindowTests.cpp` (147 lines) demonstrates the correct alternative **already used in
the same directory**: attempt a real SDL window, `GTEST_SKIP()` when unavailable.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` HIGH § Outside the graphics layer
**Affected files** `tests/.../GameTests.cpp`, `GraphicsDeviceManagerTests.cpp`, `GameCrashTest.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Indirect — the `Game` lifecycle, the most FNA-parity-sensitive area in the project, is untested.
**Security** N/A · **Memory/resource** N/A
**Strategy** Rewrite both following `GameWindowTests.cpp`'s skip-when-unavailable pattern. Either revive
`GameCrashTest.cpp` against the current API or delete it — a permanently commented-out file is worse than
none, since it implies coverage that does not exist.
**Required tests** `Game` lifecycle ordering, `GraphicsDeviceManager` device creation and events — the
harness `REMED-CORE-006`/`-007` need.
**Regression tests** Once written, these become the regression guards for both.
**Backend parity** Run on ≥2 backends. **Dependencies** **Blocks `REMED-CORE-006` and `REMED-CORE-007`.**
**Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** Both files have real coverage; the dead file is resolved.
**Verification** The new tests fail against the current buggy `UnloadContent`/event-forwarding behavior — proving they test the right thing.

### REMED-TEST-003 — SdlRenderer and Dx3 tests with stale or incorrect expectations

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Three test-authoring defects where **production code is correct** and the test is wrong:
(a) `sdlrenderer_clearoptions_audit_test.cpp` and (b) `sdlrenderer_rendertarget_depth_decision_test.cpp`
assert expected-throw behavior for `ClearOptions`/`DepthBuffer` that a **real, intentional FNA-parity fix**
(commit `90f5db2c`) deliberately changed to silently-degrade; the tests were never updated.
(c) `dx3_spritebatch_test.cpp` Check D constructs a **non-premultiplied** `Color(255,0,0,0)` under
`BlendState::AlphaBlend`, a premultiplied-convention preset, then asserts the destination is untouched.
Hand-derived real result: `(255,6,7)`. A faithful implementation legitimately fails this check.
**Evidence** the three test audits; Check D empirically confirmed failing in Pass 6.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics backends and § Resolved standing investigations
**Affected files** the three test files · **Backends/Platforms** SdlRenderer, Dx3 / ALL
**XNA/FNA impact** These tests currently assert **pre-FNA-parity behavior as correct** — reverting them would
undo a deliberate correctness fix. **Security** N/A · **Memory/resource** N/A
**Strategy** Update (a)/(b) to assert the post-`90f5db2c` degrade behavior. For (c) construct a genuinely
premultiplied `Color(0,0,0,0)`, or explicitly document straight-alpha as an intentional CNA deviation if
that is ever the real intent. **Ambiguity flagged by the audit, unresolved:** (b) may instead be a genuinely
never-implemented per-target depth feature — reconcile against `docs/sdl-renderer-2d-completeness.md`'s own
depth-support row before assuming it is merely stale.
**Required tests** The corrected assertions. **Regression tests** Grep for other tests asserting pre-`90f5db2c`
behavior. **Backend parity** N/A.
**Dependencies** `REMED-GFX-021` (Dx3 Check G is the *production* half of the same test file).
**Cx** SMALL · **PS** YES · **Verify** **YES** for (b) — resolve the stale-vs-unimplemented ambiguity first
**Completion** All three assert current correct behavior; (b)'s ambiguity is resolved and recorded.
**Verification** `SDL_Renderer_ClearOptions_Audit` and `Dx3_SpriteBatch` Check D pass.

### REMED-TEST-004 — Missing tests for three already-confirmed defects

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Each defect was found by direct code reading, not by the test suite — because the specific
triggering case is absent.
**Evidence / gaps:** (a) `ContentReaderExternalReferenceTests.cpp` constructs **only** relative `..`-style
escapes, never an absolute-path one, so it could not have caught `REMED-CONTENT-002`'s bypass — while the
sibling `CnjSourceFileSafetyTests.cpp` already has a working containment pattern to model.
(b) `FileDialogTests.cpp`/`MessageBoxTests.cpp` never race `SetBackendForTesting()` against a live call, as
`REMED-DEVICES-001` requires. (c) `PictureLibraryIndexTests.cpp` has no symlink-cycle or
permission-denied-subdirectory test, unlike the equivalent music-scanner tests in the same shard.
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Test-coverage gaps for already-confirmed production defects"
**Affected files** the four test files · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** N/A · **Security** All three untested defects are security-relevant. · **Memory/resource** (b) is a UAF.
**Strategy** Write each test **before** its production fix, so it demonstrably fails first — turning each
into a genuine regression guard rather than a post-hoc confirmation.
**Required tests** As above. **Regression tests** These are the regression tests.
**Backend parity** N/A. **Dependencies** Pairs with `REMED-CONTENT-002`, `REMED-DEVICES-001`.
**Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** All three exist and fail pre-fix. **Verification** Each fails before its fix and passes after.

### REMED-TEST-006 — D3D11/D3D12 fog tests use identity matrices and cannot detect the fog bugs

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** `d3d11_smoke_test.cpp`/`d3d12_smoke_test.cpp` set `World = View = Projection = Identity`, making
object-space and view-space vertex Z numerically identical. D3D12's test is an explicit reuse of D3D11's
fixture, so it **inherits the identical blind spot** rather than independently re-deriving it.
**Evidence** Empirically re-confirmed in Pass 6: D3D11's fog-at-boundary checks pass **as expected**, since
both the correct and mirrored formulas saturate identically at the exact `Z=FogEnd` boundary sampled. This
does not contradict the mirrored-formula finding — it confirms **the test suite cannot tell either way.**
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Test blind spot: identity-matrix fog tests…"
**Affected files** `examples/d3d11_smoke_test.cpp`, `d3d12_smoke_test.cpp`
**Backends / Platforms** D3D11, D3D12 / Windows
**XNA/FNA impact** Indirect — two confirmed fog defects are invisible to their own backends' tests.
**Security** N/A · **Memory/resource** N/A
**Strategy** Use non-identity World/View and sample at a **mid-range, non-saturating** Z. This single change
makes the tests able to detect **both** `REMED-GFX-005` (mirrored formula) and `REMED-GFX-010`
(object-space-only) — two distinct bugs, one test improvement.
**Required tests** As above. **Regression tests** Audit every fog test project-wide for identity matrices and
boundary-only sampling. **Backend parity** Fold into the cross-backend fog conformance test.
**Dependencies** Pairs with `REMED-GFX-005`, `REMED-GFX-010`.
**Cx** SMALL · **PS** YES · **Verify** NO
**Completion** Fog tests use non-identity transforms and non-saturating sample points.
**Verification** The improved tests fail against the current mirrored formula — proving they now detect it.

### REMED-DOCS-001 — Systemic in-source documentation rot (20+ confirmed instances)

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Behavior is fixed without a corresponding sweep of comments describing the old behavior.
Confirmed across **four independent mechanical batch passes** (EasyGL, SdlRenderer, Bgfx, Vulkan) — a
systemic process gap, not incidental to any subsystem.
**Evidence (selected):** stale "known bug" comments in the 218-file EasyGL shard (Vulkan blend state
"almost entirely fake" (Task 868, now fixed), `SetReferenceStencil` "universally missing" (6 backends
implement it), anisotropic filtering "open" (Tasks 918/924 landed), `GetData()` "unimplemented",
pre-fix env-map formula); 9+ more in Bgfx/Vulkan; `D3DConstantBuffers.hpp` claims `D3DLightingConstants`/
`D3DBoneConstants` are "NOT YET WIRED" (both actively used in D3D11 **and** D3D12 today);
`D3D12RootSignatureCache.hpp` claims static samplers (upgraded to dynamic descriptor tables by DX-119);
`D3D12Textures.hpp` claims cube/3D textures "deliberately NOT implemented" (both exist, 260/290 lines);
`RenderPipelineSettings.hpp` references `GraphicsDevice::GetRenderPipelineSettings()`, which **does not
exist anywhere** (grep: zero matches).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` § By category "Documentation-rot"; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Recurring testing gaps
**Affected files** 20+ across every backend shard
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** None directly — but stale comments **actively mislead**, including future audits. Three
false "matches EasyGL's established formula" comments are a **confirmed propagation mechanism** for
`REMED-GFX-005`: a later port copied a wrong instance while trusting the comment.
**Security** N/A · **Memory/resource** N/A
**Strategy** A dedicated sweep for "Task NNN"/"known bug"/"currently broken"-style comments, cross-checked
against `git log` and current source — as the audit recommends, independent of any one file's own review.
**Do this after the P0/P1 fixes land**, or it will need redoing. Preserve the project's genuinely strong
disclosure discipline; the goal is accuracy, not fewer comments.
**Required tests** N/A **Regression tests** Consider a CI lint flagging "Task NNN" comments referencing closed tasks.
**Backend parity** Sweep all 14. **Dependencies** After P0/P1.
**Cx** MEDIUM (breadth) · **PS** CONDITIONAL — comment-only, but touches files every lane owns; do in a quiet window · **Verify** NO
**Completion** Every stale comment is corrected or removed; false precedent claims are gone.
**Verification** A re-sweep finds no comment contradicted by current source.

### REMED-DOCS-002 — Seven `docs/*.md` staleness findings, each contradicted by confirmed current state

**Sev** MEDIUM · **Pri** P2 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Same as `REMED-DOCS-001`, at the document level.
**Evidence:** `docs/coverage.md` claims ".xnb binary support entirely absent" (contradicted by 10+ audited,
clean readers); `docs/d3d9-backend.md` vs `docs/cnatests-mingw-setenv-proposal.md` **directly contradict each
other under the same task ID `D9-123`** on whether `CnaTests` builds under D3D9 (the setenv doc's detailed
implementation record is far more credible); `docs/cna_audio_deep_audit_2026-07-17.md` (a genuinely rigorous
audit that triggered 80+ `AUD-XX` commits) has no banner noting its flagship P0 finding is now fixed;
`docs/dx3-backend.md` claims SpriteBatch "fully verified" (contradicted by `REMED-GFX-021`/`REMED-TEST-003`);
`docs/easygl_bugs.md` describes the fog shader as clip-space when the source is explicitly object-space;
`docs/gdm-coverage.md` shows all events "supported", never mentioning `REMED-CORE-007`;
`docs/graphics-resource-lifetime.md` and `docs/graphicsresource-fna-audit.md` contradict each other on whether
a resource-tracking list exists (resolved in favor of the latter being stale).
Also: `docs/webgpu-backend.md`'s `WebGPU_Msaa` "intentionally left failing" note is **stale** (confirmed
passing; fixed 2026-07-18, WEBGPU-58), and `CLAUDE.md`'s WebGPU summary now **understates** the backend
(real PbrEffect/SkinnedEffect/EnvironmentMapEffect/instancing/RenderTarget all working, 23/23 tests) —
understating, not overclaiming, but worth syncing.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Testing/documentation; `AUDIT_CROSS_CUTTING_FINDINGS.md` § docs shard
**Affected files** the 7 docs + `docs/webgpu-backend.md` + `CLAUDE.md`
**Backends / Platforms** N/A / ALL
**XNA/FNA impact** None directly; misleads implementers. **Security** N/A · **Memory/resource** N/A
**Strategy** Correct each against confirmed current state. Add status banners with dates rather than silently
editing, so future readers can judge age. **Note the pattern the audit identified:** these docs were honest
and careful when written — the failure is not revisiting them, so the durable fix is a review trigger, not
better authorship. Standouts needing no change: `docs/d3d12-backend.md`, `basiceffect-support.md`,
`input-fna-fidelity.md`, `input-public-api-frozen.md` (compile-time enforced, cannot drift),
`graphics-backend-feature-matrix.md`.
**Required tests** N/A **Regression tests** Consider a doc-review checklist item on behavior-changing PRs.
**Backend parity** N/A. **Dependencies** After P0/P1 (several docs describe things being fixed).
**Cx** SMALL · **PS** YES · **Verify** NO
**Completion** All 7 (+2) match confirmed current state. **Verification** Cross-check each claim against source.

### REMED-NET-003 — `HandleClientHello` has no per-peer resend guard (unbounded fake-gamer injection)

**Sev** MEDIUM · **Pri** P2 · **Owner** NET · **Status** NOT STARTED
**Root cause** No guard against a single peer sending `ClientHello` repeatedly, each time injecting a gamer.
**Evidence** `src/CNA/Internal/Net/ENetBackend.cpp.audit.md` (recorded alongside `REMED-NET-001`)
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § ENetBackend ("Related, lower-severity")
**Affected files** `src/CNA/Internal/Net/ENetBackend.cpp` · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** N/A (no FNA `Net`). **Security** Resource-exhaustion / roster-pollution DoS from one
connected peer. **Memory/resource** Unbounded roster growth.
**Strategy** Track per-peer hello state; ignore or reject duplicates. **Implement together with
`REMED-NET-001`** — same file, same threat model, same review.
**Required tests** Repeated `ClientHello` from one peer; assert exactly one gamer is added.
**Regression tests** Assert a legitimate reconnect still works. **Backend parity** N/A.
**Dependencies** Atomic with `REMED-NET-001`. **Cx** SMALL · **PS** NO (same file/change as NET-001) · **Verify** NO
**Completion** One peer cannot inject more than one gamer. **Verification** The repeated-hello test passes.

### REMED-GFX-051 — SdlGpu `DepthBias`/`SlopeScaleDepthBias` stored but never applied

**Sev** MEDIUM · **Pri** P2 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** Honestly disclosed: SDL_GPU has no per-draw dynamic depth-bias equivalent to Vulkan's
`vkCmdSetDepthBias`, so the values are captured but never used.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § D3D12 UPDATE. A **much narrower** gap than D3D12's complete
stencil+scissor non-functionality — SdlGpu correctly tracks and applies all stencil fields and `scissorTestEnable`.
**Affected files** SdlGpu `ApplyRasterizerState`/pipeline creation · **Backends/Platforms** SdlGpu / ALL
**XNA/FNA impact** `DepthBias`/`SlopeScaleDepthBias` (used for shadow-map acne and decal z-fighting) do nothing.
**Security** N/A · **Memory/resource** N/A
**Strategy** Bake depth bias into pipeline state if SDL_GPU supports it there, since dynamic setting is
unavailable. If genuinely unsupported, report non-support via `SupportsCapability()` rather than silently
accepting the values — that is the honest failure mode, and connects to `REMED-GFX-026`.
**Required tests** Depth bias resolving z-fighting between coplanar polygons.
**Regression tests** Add depth-bias to the cross-backend state suite. **Backend parity** All 14 apply or explicitly reject.
**Dependencies** Related to `REMED-GFX-026`. **Cx** MEDIUM · **PS** YES · **Verify** **YES** — confirm SDL_GPU's real capability first
**Completion** Depth bias applies, or non-support is explicitly reported. **Verification** The z-fighting test passes or skips explicitly.

---

# P3 — LOW severity, performance, maintainability, non-urgent architecture

### REMED-GFX-031 — Headless `primitiveCount` undercounts instanced draws by `instanceCount`

**Sev** MEDIUM · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `HeadlessStatistics::primitiveCount` counts primitives once per draw, not once per instance.
**Evidence** `src/.../Headless/HeadlessGraphicsBackend.cpp.audit.md` F1 · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics
**Affected files** `HeadlessGraphicsBackend.cpp` · **Backends/Platforms** Headless / ALL
**XNA/FNA impact** Statistics are wrong for instanced rendering. Headless exists precisely for
statistics-based testing, so a wrong statistic undermines its purpose. **Security** N/A · **Memory/resource** N/A
**Strategy** Multiply by `instanceCount`. Note the related standing memory that `DrawPrimitives`' count
parameter is *primitives*, not vertices — verify which unit is intended here before multiplying.
**Required tests** Instanced draw asserting `primitiveCount == primitives * instances`.
**Regression tests** Add instanced draws to the Headless statistics tests.
**Backend parity** Check every backend exposing statistics. **Dependencies** None.
**Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Instanced draws counted correctly. **Verification** The instanced statistics test passes.

### REMED-GFX-032 — Cube mip regeneration touches all 6 faces when only one changed

**Sev** MEDIUM · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** SdlGpu's `TextureCube::SetData()` and D3D11's `RenderTargetCubeBackend::UnbindAsRenderTarget()`
both regenerate the whole cube's mip chain whenever `mipMap_` is true, after only one face changed.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Recurring shape across 2 backends, 2 resource types".
**Positive counter-example:** `D3D12RenderTargetCubeBackend::GenerateMipsEXT()` correctly regenerates only
the drawn-to face (`face = activeFace_`, correct per-face subresource indexing) — despite D3D12 sharing
almost every other finding with D3D11. A useful reminder that sibling backends genuinely diverge.
**Affected files** SdlGpu `TextureCube::SetData()`; D3D11 `RenderTargetCubeBackend`
**Backends / Platforms** SdlGpu, D3D11 / ALL
**XNA/FNA impact** Not a crash in the single-face case existing tests exercise, but **a real correctness risk
for genuine multi-face cube-map generation**: the other 5 faces' mips are regenerated from whatever they
currently hold, possibly uninitialized data. Plus 5/6 of the work is wasted even when correct.
**Security** N/A · **Memory/resource** Uninitialized data may be read into mip chains.
**Strategy** Regenerate per-face, using D3D12's implementation as the in-repo reference. Check every
backend's cube mip trigger for the same whole-resource shape.
**Required tests** Render to one face, verify the other 5 are unchanged; then a full 6-face generation workflow.
**Regression tests** Add multi-face cube generation to the cross-backend suite.
**Backend parity** All backends regenerate per-face. **Dependencies** None.
**Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** Only the changed face's mips regenerate. **Verification** The single-face test shows the other 5 untouched.

### REMED-GFX-045 — `SamplerStateCollection` has no per-slot dirty tracking

**Sev** LOW · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** No equivalent of FNA's `modifiedSamplers`, so `GraphicsDevice::applySamplerStatesToBackend()`
unconditionally re-applies **all 16 slots on every call**.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-graphics LOW findings — traced through to the consumption site.
**Affected files** `SamplerStateCollection.hpp`/`.cpp`; `GraphicsDevice.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Pure performance divergence, **no correctness impact**. **Security** N/A · **Memory/resource** N/A
**Strategy** Add per-slot dirty flags mirroring FNA. Measure first — 16 slots per draw may be negligible on
modern hardware, and this is a P3 for that reason.
**Required tests** Assert only modified slots are re-applied (via the recording test double).
**Regression tests** Ensure a modified slot is never skipped — a dirty-tracking bug is a correctness bug.
**Backend parity** N/A. **Dependencies** `NO` against `REMED-CORE-002` (`GraphicsDevice.cpp`).
**Cx** SMALL · **PS** CONDITIONAL · **Verify** NO
**Completion** Only modified slots re-apply. **Verification** The recording double confirms it, with no missed updates.

### REMED-GFX-046 — `GraphicsResource` cannot reassign `graphicsDevice_` after construction

**Sev** LOW · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** No equivalent of FNA's `internal set`-backed property, used by `VertexDeclaration` to move
between devices.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-graphics LOW findings
**Affected files** `GraphicsResource.hpp`/`.cpp` · **Backends/Platforms** ALL / ALL
**XNA/FNA impact** A resource cannot migrate between devices as FNA allows. Low practical impact — multi-device
use is rare. **Security** N/A · **Memory/resource** Reassignment is a lifetime hazard; any fix must consider
`REMED-GFX-004`'s dangling-pointer class.
**Strategy** Add an internal setter with appropriate visibility. Weigh against the risk: an unrestricted
setter makes dangling-device bugs **easier**, so restrict it to the `VertexDeclaration` case FNA actually uses.
**Required tests** Move a `VertexDeclaration` between devices. **Regression tests** Assert reassignment cannot leave a dangling pointer.
**Backend parity** N/A. **Dependencies** Consider alongside `REMED-GFX-004`. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** Reassignment is possible and safe. **Verification** The migration test passes under ASan.

### REMED-GFX-047 — Two correct-but-dead vertex-format helper headers

**Sev** LOW · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `BgfxVertexFormatHelper.hpp` and `VulkanVertexFormatHelper.hpp` are correct, well-mapped
`VertexElementFormat` → native-format helpers with **zero production call sites** (exhaustive grep). The real
per-pipeline layouts are hardcoded per-stride/per-shader instead.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "NEW, now a confirmed 2-backend pattern". Vulkan's own test
is **well-designed** (directly unit-tests the mapping functions against an expected-value table, so the logic
is genuinely verified); Bgfx's equivalent test never calls `SetData` at all, so all four "stride 16/20/24/32"
cases silently exercise the same hardcoded stride-16 layout — **the test's entire subject is dead code.**
**Affected files** the 2 helper headers; `bgfx_vertex_format_test.cpp`; `MakeBgfxLayout()`
**Backends / Platforms** Bgfx, Vulkan / ALL
**XNA/FNA impact** None — the hardcoded paths work. The risk is maintenance: a future change to the helper
has no effect, and Bgfx's test would not notice.
**Security** N/A · **Memory/resource** N/A
**Strategy** Decide per backend: wire the helper into the real dispatch (better — removes the hardcoded
stride assumption `REMED-GFX-043` also depends on), or delete it and its test. **Do not leave it as-is**;
correct-but-unreachable code with a passing test is actively misleading.
**Required tests** If wired in: the existing Vulkan test already covers the mapping. Bgfx's test needs a real `SetData` call.
**Regression tests** Check every backend for a similar dead helper.
**Backend parity** N/A. **Dependencies** Related to `REMED-GFX-043` (hardcoded stride guessing).
**Cx** MEDIUM (wire in) / TRIVIAL (delete) · **PS** YES · **Verify** NO
**Completion** Each helper is used or removed; Bgfx's test exercises the real path.
**Verification** No correct-but-unreachable format helper remains.

### REMED-GFX-048 — `BasicEffect::VertexColorEnabled` is a bare public field

**Sev** MEDIUM · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** No `getXProperty()`/`setXProperty()` wrapper, unlike every other property on the class —
a direct violation of this project's own C# property convention in `CLAUDE.md`.
**Evidence** Independently confirmed **3 times** in different test batches exercising the same production
code (`bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp`, `vulkan_basiceffect_vertexcolor_enabled_test.cpp`,
`examples/basic_effect_test.cpp`) — a production-code issue, not a backend one.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Graphics; `AUDIT_CROSS_CUTTING_FINDINGS.md` § API design
**Affected files** `BasicEffect.hpp`/`.cpp`; every call site using it as a bare field
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Convention violation rather than a behavioral gap — but it is CNA's **own documented,
binding convention**, and inconsistency here undermines the pattern everywhere.
**Security** N/A · **Memory/resource** N/A
**Strategy** Add the property pair and update call sites. **A source-breaking change** for existing code —
per `CLAUDE.md`'s "no backward-compatibility hacks" rule, fix the call sites rather than adding an alias.
Audit `BasicEffect` for other bare fields at the same time.
**Required tests** Getter/setter round-trip. **Regression tests** A convention check across all stock effects.
**Backend parity** N/A. **Dependencies** `REMED-GFX-024` also touches `BasicEffect` — combine.
**Cx** SMALL · **PS** CONDITIONAL (`NO` against `REMED-GFX-024`) · **Verify** NO
**Completion** Accessed via properties; no bare public fields remain on the class.
**Verification** The round-trip test passes and all call sites compile.

### REMED-GFX-049 — Rounding-tie divergence (round-half-up vs FNA's banker's rounding) in 7 PackedVector types

**Sev** LOW · **Pri** P3 · **Owner** GRAPHICS · **Status** NOT STARTED
**Root cause** `Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Rg32`, `Rgba1010102`, `Rgba64` round half-up where
FNA's `Math.Round` uses banker's rounding (half-to-even).
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-graphics LOW findings
**Affected files** the 7 headers · **Backends/Platforms** ALL / ALL
**XNA/FNA impact** Off-by-one **only for exact .5 ties** — much narrower than `REMED-GFX-033`'s
truncate-vs-round, which is wrong for *every* non-integer input. Kept LOW for that reason.
**Security** N/A · **Memory/resource** N/A
**Strategy** Implement banker's rounding to match FNA. Do **with** `REMED-GFX-033` and `REMED-GFX-034` as one
PackedVector pass — same files, same reviewer, one regeneration of reference data.
**Required tests** Exact-.5 tie inputs for all 7. **Regression tests** Add tie values to the harness's
`Dump*()` inputs — the same gap class as `REMED-GFX-033`'s root cause.
**Backend parity** N/A. **Dependencies** Bundle with `REMED-GFX-033`/`-034`. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** All 7 use banker's rounding. **Verification** Regenerated FNA reference data matches for tie inputs.

### REMED-GFX-050 — Missing `GetTypeName()` overrides (4 confirmed instances of one shape)

**Sev** MEDIUM · **Pri** P3 · **Owner** GRAPHICS (owns 3 of 4; NET's instance folded here to keep one owner) · **Status** NOT STARTED
**Root cause** Concrete `System::Object`-derived classes must override `GetTypeName()` with their
fully-qualified .NET name (a `CLAUDE.md` requirement). Four do not: `Texture2D` returns bare `"Texture2D"`
instead of the qualified name **every sibling correctly returns**; `DynamicVertexBuffer` and
`DynamicIndexBuffer` do not override at all; `GamerServicesComponent` silently reports
`"Microsoft.Xna.Framework.GameComponent"`.
**Evidence** the four per-file audits. **Confirmed not project-wide:** sibling `DrawableGameComponent`,
`Texture3D`, `TextureCube`, `RenderTarget2D`, `RenderTargetCube` all get it right — isolated misses, not a
systemic convention failure.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core/Graphics and § GamerServices
**Affected files** `Texture2D.cpp`, `DynamicVertexBuffer.hpp`, `DynamicIndexBuffer.hpp`, `GamerServicesComponent.hpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Type-name-based reflection/diagnostics report the wrong type. `Texture2D` is the single
most commonly used XNA type, so it is the most visible instance. **Security** N/A · **Memory/resource** N/A
**Strategy** Add the four overrides. Then sweep **every** concrete `System::Object` subclass — four found
incidentally implies more. Consider a compile-time or test-time check that every concrete subclass overrides it.
**Required tests** Assert the qualified name for each of the four. Note `Texture2DTests.cpp` (1085 lines, the
shard's most rigorous file) has **zero** `GetTypeName()` test, while `Texture3DTests.cpp`/`TextureCubeTests.cpp`
both test it — the coverage asymmetry is why this survived.
**Regression tests** A test enumerating every concrete subclass and asserting a qualified name.
**Backend parity** N/A. **Dependencies** None. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** All four override correctly; the sweep is complete.
**Verification** The enumerate-all-subclasses test passes.

### REMED-CORE-010 — `Matrix::Invert()` uses single precision where FNA deliberately uses double

**Sev** MEDIUM · **Pri** P3 · **Owner** CORE · **Status** NOT STARTED
**Root cause** FNA's `Invert` (`Matrix.cs:1836`) promotes every operand to `double` before rounding back to
`float`, for all ~20 intermediate cofactor terms — a deliberate, consistently-applied precision choice for a
classically numerically-sensitive operation. CNA uses plain `float` throughout.
**Evidence** `src/.../Matrix.cpp.audit.md`. The port's own comment acknowledges the difference but asserts
"no observable difference in practice" **without demonstrating it** via any cited test or numerical
comparison — unlike this project's own standard elsewhere (e.g. `SoundEffectContentTypeReader.cpp`'s
fixture-verified claims).
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § core; `AUDIT_CROSS_CUTTING_FINDINGS.md`
**Affected files** `src/Microsoft/Xna/Framework/Matrix.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** **Unknown — that is precisely the finding.** Given the project's explicit FNA-fidelity
policy, this is a testable, currently-unverified claim. **Security** N/A · **Memory/resource** N/A
**Strategy** **Test the claim before changing anything.** Construct a poorly-conditioned or
widely-varying-magnitude matrix, invert both ways, compare against a high-precision reference. If the claim
holds, keep the implementation and **replace the bare assertion with the evidence**. If it fails, adopt FNA's
double-precision approach. Either outcome is a valid, recordable result.
**Required tests** Ill-conditioned matrix inversion against a high-precision reference.
**Regression tests** Add the ill-conditioned case to `MatrixTests.cpp` permanently.
**Backend parity** N/A. **Dependencies** None. **Cx** SMALL (test) / MEDIUM (if the claim fails) · **PS** YES
**Verify** **YES — this task *is* a verification task.**
**Completion** The claim is empirically settled and the source comment cites the evidence.
**Verification** The numerical comparison test exists and its result is recorded either way.

### REMED-CORE-011 — `CNA::Runtime` is a fully documented public class with zero implementation

**Sev** MEDIUM · **Pri** P3 · **Owner** CORE · **Status** NOT STARTED
**Root cause** `include/CNA/Misc.hpp` declares `CNA::Runtime` with 5 Doxygen-documented methods
(`Initialize`/`Shutdown`/`IsGraphicsEnabled`/`IsAudioEnabled`/`IsInputEnabled`) and **no `.cpp` definition
anywhere** — no `Misc.cpp` exists at all. Zero consumers repo-wide.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "`CNA::Runtime` … ZERO implementation". Distinct from (and
more severe than) the `cna-graphics` NOXNA shard's "implemented but unconsumed" scaffolds — this one is not
even implemented.
**Affected files** `include/CNA/Misc.hpp` · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** N/A (CNA extension). Any code instantiating it and calling any method **fails to link** —
a fully documented public API that cannot be used.
**Security** N/A · **Memory/resource** N/A
**Strategy** Implement it, or delete it. A documented public class that does not link is worse than an absent
one, because the documentation implies it works. **Owner decision required** on which. Note `Misc.hpp` also
has **no SPDX header at all** — a third license-header variant (see `REMED-BUILD-007`).
**Required tests** If implemented: coverage per method. **Regression tests** A link test instantiating every
public CNA class would catch this class of defect generally.
**Backend parity** N/A. **Dependencies** Owner decision. **Cx** SMALL (delete) / MEDIUM (implement) · **PS** YES · **Verify** NO
**Completion** Either implemented and tested, or removed. **Verification** No public class fails to link.

### REMED-CORE-012 — `NOXNA` mistagging: 3 real XNA members tagged as extensions

**Sev** LOW · **Pri** P3 · **Owner** CORE · **Status** NOT STARTED
**Root cause** `CLAUDE.md` states `NOXNA` wraps only functionality **not** part of real XNA 4.0. Three
genuine XNA members are tagged anyway: `NetworkSession::MaxSupportedGamers` (=31) and `MaxPreviousGamers`
(=100) — both confirmed real via the xn65 XML — and `KeyboardState::ToString()`, while
`MouseState`/`GamePadState`'s own `ToString()` are correctly untagged.
**Evidence** Pass 3 xn65 XML sweep · **Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § Pass 3 continued
**Affected files** `NetworkSession.hpp`, `KeyboardState.hpp` · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** No behavioral impact — the members exist and work. But `NOXNA` is the project's signal for
"this is not real XNA," so mistagging **corrupts the signal** future readers and audits rely on.
**Security** N/A · **Memory/resource** N/A
**Strategy** Remove the three tags. Consider a broader `NOXNA` audit against the xn65 reference — mistagging
in the other direction (a real extension left untagged) would be worse and was not systematically swept.
Related: `docs/graphicsdevice-fna-audit.md` flags `Indices()`/`Indices(const IndexBuffer*)` living inside a
NOXNA-helpers section without being NOXNA-tagged themselves.
**Required tests** N/A **Regression tests** Consider a lint cross-checking `NOXNA` against the xn65 reference.
**Backend parity** N/A. **Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** All three untagged; the broader sweep is done or explicitly deferred.
**Verification** No real XNA 4.0 member carries `NOXNA`.

### REMED-CORE-013 — FNA-faithful-but-surprising behavior lacks the explanatory comments this codebase uses elsewhere

**Sev** LOW · **Pri** P3 · **Owner** CORE · **Status** NOT STARTED
**Root cause** Several spots look like defects in isolation but are **confirmed-faithful FNA reproductions**;
in each case the port omits the explanatory comment this codebase otherwise provides well.
**Evidence (all verified against real FNA source):** `BoundingSphere::Contains(BoundingFrustum)` can never
return `Disjoint` (FNA's own `// TODO : calcul dmin`, never implemented upstream);
`BoundingFrustum::Intersects(Ray)`'s general case throws `NotImplementedException` (identical in FNA, though
CNA's message is clearer); `Curve::ComputeTangent()`'s asymmetric near-zero epsilons (FNA has the identical
asymmetry, stemming from the well-known `float.Epsilon`-is-not-a-tolerance C# pitfall, which CNA's
`std::numeric_limits<float>::denorm_min()` correctly-but-confusingly mirrors); `StorageContainer`'s unchecked
`Path.Combine`-style joins (FNA-faithful — explicitly **not** to be "fixed", unlike `REMED-CONTENT-002`'s
`DeleteContainer`, which only superficially resembles it).
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Recurring pattern in `xna-framework-core`";
`AUDIT_FINDINGS_INDEX.md` § "Confirmed FNA-faithful, NOT a regression"
**Affected files** `BoundingSphere.cpp`, `BoundingFrustum.cpp`, `Curve.cpp`, `StorageContainer.cpp`
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** **None — these are correct.** The risk is the opposite of a bug: a future maintainer
mistakes faithful-but-surprising behavior for an accidental regression and "fixes" it, silently diverging
from FNA. `StorageContainer` is the highest-risk instance precisely because it sits next to a genuine
path-traversal fix.
**Security** N/A · **Memory/resource** N/A
**Strategy** **Documentation only — change no behavior.** Add a short comment at each site citing the exact
FNA source location, following the pattern `LzxDecoder.cpp` and `VideoDecoder.cpp` already use well.
**Required tests** None. **Regression tests** None. **Backend parity** N/A.
**Dependencies** Best done **with** `REMED-CONTENT-002`, so the `StorageContainer`-vs-`StorageDevice`
distinction is documented exactly when someone is looking at both.
**Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Each site cites its FNA source. **Verification** Review confirms no behavior changed.

### REMED-NET-005 — `GuideAlreadyVisibleException` is implemented and tested but dead

**Sev** MEDIUM · **Pri** P3 · **Owner** NET · **Status** NOT STARTED
**Root cause** The real "a Guide UI is already pending" guards in `Guide::BeginShowMessageBox`/
`BeginShowKeyboardInput` throw a generic `System::InvalidOperationException` instead of this dedicated,
purpose-built type (grep: never constructed outside its own declaration and test).
**Evidence** `include/.../GamerServices/Guide.hpp.audit.md` · **Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § GamerServices
**Affected files** `Guide.cpp`, `GuideAlreadyVisibleException.hpp` · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** No FNA reference. Callers cannot distinguish this specific condition from any other
invalid-operation. **Security** N/A · **Memory/resource** N/A
**Strategy** Throw the dedicated type from both guards. Overlaps `REMED-CORE-002` — same review.
**Required tests** Assert both guards throw the specific type. **Regression tests** The existing test file
already covers the type; add the throw-site assertions. **Backend parity** N/A.
**Dependencies** Coordinate with `REMED-CORE-002`. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Both guards throw the dedicated type. **Verification** The throw-site tests pass.

### REMED-NET-006 — `SignedInGamerCollection::operator[](PlayerIndex)` checks only the upper bound

**Sev** LOW · **Pri** P3 · **Owner** NET · **Status** NOT STARTED
**Root cause** Checks `id >= collection_.size()` but not the lower bound before indexing.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § xna-gamerservices. **LOW rather than MEDIUM** because it is
reachable only via a deliberately-misused explicit negative `static_cast` — the enum has no negative named values.
**Affected files** `SignedInGamerCollection.hpp`/`.cpp` · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** No FNA reference. **Security** UB, but only via a deliberate misuse.
**Memory/resource** Negative-index access.
**Strategy** Add the lower-bound check. Trivial, and removes a UB path regardless of reachability.
**Required tests** A negative `PlayerIndex` cast. **Regression tests** Add to the collection matrix.
**Backend parity** N/A. **Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Both bounds checked. **Verification** The negative-cast test returns null or throws, no UB.

### REMED-NET-007 — `NetworkSession*` `Dispose()`d but never `delete`d across 10 files

**Sev** LOW · **Pri** P3 · **Owner** NET · **Status** NOT STARTED
**Root cause** `NetworkSession`'s own class doc explicitly states the caller must `delete` separately, since
`Dispose()` deliberately does not `delete this`. Ten call sites do not.
**Evidence** 8 example demos (`demo_qos_probe`, `demo_session_lifecycle_events`, `demo_gamer_roster_hud`,
`demo_session_browser`, `demo_simulated_network_conditions`, `demo_net_client_server_arena`,
`demo_gamerservices_dispatcher_watchdog`, `demo_net_avatar_sync`) **plus 2 real regression harnesses**
(`tools/net/gamerservices_dispatcher_harness.cpp`, `tools/net/net_two_process_harness.cpp` — the latter with
~a dozen call sites), the latter spawned by real GTest suites. **Positive counter-example:**
`demo_gamer_profile_privileges` correctly `Dispose()`s **and** `delete`s its owned `GamerProfile*` — the
correct pattern is understood; this is a repeated omission around one class.
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Recurring pattern: `NetworkSession*` `Dispose()`d but never `delete`d"
**Affected files** the 10 files · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** N/A · **Security** N/A
**Memory/resource impact** **Practically harmless in every instance found** (each process exits immediately,
so the OS reclaims), but a real leak if any file is used as a copy-paste template for a long-running app.
**Strategy** Add `delete` at each site — or, better, reconsider the ownership contract itself. A class whose
documented contract is repeatedly missed across 10 files, including its own test infrastructure, has an API
design problem, not 10 independent authoring mistakes. Consider returning `std::unique_ptr<NetworkSession>`.
**Required tests** N/A **Regression tests** Run the demos/harnesses under LeakSanitizer.
**Backend parity** N/A. **Dependencies** None. **Cx** SMALL (add deletes) / MEDIUM (ownership redesign) · **PS** YES · **Verify** NO
**Completion** No leak at any site. **Verification** LeakSanitizer clean across all 10.

### REMED-DEVICES-003 — Duplicate `Clipboard` and `Power`/`PowerState` across `CNA::Input` and `CNA::Devices`

**Sev** MEDIUM · **Pri** P3 · **Owner** DEVICES · **Status** NOT STARTED
**Root cause** Two entirely independent implementations wrapping the identical SDL3 calls, with different
naming conventions: `CNA::Input::Clipboard` (`GetTextEXT()`/`SetTextEXT()`/`HasTextEXT()`, the `EXT`-suffix
NOXNA convention) vs `CNA::Devices::Clipboard` (`getTextProperty()`/`setTextProperty()`, the C# property
convention); likewise `Input::PowerStateEXT`/`Power` vs `Devices::PowerState`/`PowerInfo`.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § "Duplicate NOXNA-extension API surfaces". Both are
**individually correct** — both correctly `SDL_free()` the clipboard result, and both use explicit exhaustive
switches (not raw casts) for the `SDL_PowerState` ordinal mismatch. Minor behavioral difference:
`Devices::setTextProperty` returns SDL's success `bool`; `Input::SetTextEXT` is `void` and discards it.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Input/Devices
**Affected files** `CNA::Input` and `CNA::Devices` Clipboard/Power sources
**Backends / Platforms** N/A / ALL (`CNA::Devices` is gated behind `CNA_DEVICES`, default OFF)
**XNA/FNA impact** N/A (both NOXNA). A project enabling `CNA_DEVICES` gets **two clipboard APIs with
different conventions** and a behavioral difference. **Security** N/A · **Memory/resource** N/A
**Strategy** Consolidate onto one implementation behind two thin naming facades, or deprecate one.
Preserve the return-value difference deliberately (discarding SDL's success flag is arguably its own small
bug). **Confirmed not to extend further:** `cna-input`'s 31 files have no `Camera`/`FileDialog`/`MessageBox`/
`SystemTray` equivalents, so those `cna-devices` features are genuinely not duplicated.
**Required tests** Existing tests for both; assert equivalent behavior after consolidation.
**Regression tests** Check for other duplicate NOXNA surfaces. **Backend parity** N/A.
**Dependencies** None. **Cx** MEDIUM · **PS** YES · **Verify** NO
**Completion** One implementation backs both surfaces. **Verification** Both APIs' tests pass against the shared implementation.

### REMED-AUDIO-001 — `SoundBank::GetCue()` returns a raw owning `Cue*`

**Sev** LOW · **Pri** P3 · **Owner** AUDIO · **Status** NOT STARTED
**Root cause** Returns a raw owning pointer instead of `std::unique_ptr<Cue>`, inconsistent with this
codebase's own ownership-transfer convention for the identical pattern
(`StorageDevice::EndOpenContainer()`/`EndShowSelector()`).
**Evidence** `include/.../Audio/SoundBank.hpp.audit.md` — **the only new finding in the entire 31-file
`xna-audio` shard**, described as the most thoroughly self-audited subsystem encountered.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` LOW
**Affected files** `SoundBank.hpp`/`.cpp` and call sites · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** XNA's `GetCue()` returns a reference-typed `Cue`; a raw pointer is a reasonable C++
translation. **Functionally fine** — the header explicitly documents the ownership contract.
**Security** N/A · **Memory/resource** Leak risk if a caller ignores the documented contract (cf. `REMED-NET-007`,
the same contract-based-ownership class of problem, there confirmed missed 10 times).
**Strategy** Return `std::unique_ptr<Cue>`, matching the sibling convention. A source-breaking change; update
call sites per `CLAUDE.md`'s no-compat-hacks rule.
**Required tests** Ownership-transfer round-trip. **Regression tests** LeakSanitizer over the audio suite.
**Backend parity** N/A. **Dependencies** None. **Cx** SMALL · **PS** YES · **Verify** NO
**Completion** Ownership is expressed in the type. **Verification** Call sites compile; LeakSanitizer clean.

### REMED-AUDIO-002 — `AudioCategory::ToString()` missing from both CNA and FNA

**Sev** LOW · **Pri** P3 · **Owner** AUDIO · **Status** NOT STARTED
**Root cause** A real XNA 4.0 member (confirmed in the reference XML) absent from CNA — **and from FNA too**.
**Evidence** Pass 3 XACT-subset sweep. A **distinct sub-pattern**: "real XNA member, absent from FNA too"
needs new code written from the XNA spec directly, unlike "present in FNA, never ported" (e.g.
`REMED-GFX-044`'s `DisplayMode`), which is a simple port. Different root cause, different effort.
**Audit refs** `AUDIT_CROSS_CUTTING_FINDINGS.md` § Pass 3 continued
**Affected files** `include/.../Audio/AudioCategory.hpp` (+ `.cpp`) · **Backends/Platforms** N/A / ALL
**XNA/FNA impact** A minor completeness gap; CNA correctly mirrors FNA's own incompleteness rather than
diverging. **Security** N/A · **Memory/resource** N/A
**Strategy** Implement from the XNA spec (there is no FNA implementation to port). Follow the `ToString()`
format conventions used by CNA's other value types.
**Required tests** Format assertion. **Regression tests** Add to the audio test suite. **Backend parity** N/A.
**Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** Present and format-tested. **Verification** The format test passes.

### REMED-BUILD-007 — Three inconsistent SPDX/license-header conventions

**Sev** MEDIUM · **Pri** P3 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Three coexisting conventions: (a) the entire `CNA::Internal::Net` subsystem (12 files) uses
`MIT` + an explicit copyright line, diverging from every other CNA-original NOXNA file's plain `MS-PL`;
(b) all three `xna-storage` file pairs have an **intra-pair** mismatch (`.hpp` MS-PL, `.cpp` MIT + copyright)
— a stronger form than (a), since it is inconsistent *within* each pair; (c) `include/CNA/Misc.hpp` has **no
SPDX header at all**.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § Licensing/header-convention inconsistencies
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Testing/documentation
**Affected files** 12 `CNA::Internal::Net` files; 6 `xna-storage` files; `include/CNA/Misc.hpp`
**Backends / Platforms** N/A / ALL
**XNA/FNA impact** N/A
**Security impact** N/A — but licensing ambiguity is a genuine **legal/compliance** risk, which is why this
is MEDIUM rather than LOW despite being cosmetic in code terms.
**Memory/resource impact** N/A
**Strategy** **Owner decision required — this is a licensing question, not an engineering one.** The Net
subsystem's MIT may well be deliberate (ENet itself is MIT). The `xna-storage` intra-pair mismatch is almost
certainly accidental and is the clearest candidate for correction. Do **not** mass-rewrite headers without an
explicit decision. Then add a CI lint for missing/inconsistent SPDX headers.
**Required tests** N/A **Regression tests** An SPDX-presence-and-consistency lint.
**Backend parity** N/A. **Dependencies** Owner decision; overlaps `REMED-CORE-011` (`Misc.hpp`).
**Cx** SMALL (once decided) · **PS** YES · **Verify** NO
**Completion** Every file has an SPDX header consistent with a documented policy.
**Verification** The lint passes repo-wide.

### REMED-BUILD-009 — `cna_xnb_audio_metadata_dump` fails to link under MinGW cross-compile

**Sev** LOW · **Pri** P3 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** Undefined reference to `Video::Video`'s constructor/vtable — `Video.cpp`'s translation unit is
not linked into this tool target under `CNA_BUILD_EXAMPLES=OFF`.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § D3D12 ("Incidental, unrelated build finding"). Did not block
`D3D12_Smoke`. **Affected files** the tool's CMake target · **Backends/Platforms** N/A / MinGW cross-compile
**XNA/FNA impact** N/A · **Security** N/A · **Memory/resource** N/A
**Strategy** Add the missing source/library to the target. Same family as `REMED-BUILD-005` (implicit
transitive linking assumptions that hold only on native GCC) — audit both together.
**Required tests** N/A **Regression tests** A cross-compile CI job. **Backend parity** N/A.
**Dependencies** Related to `REMED-BUILD-005`. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** The tool links under MinGW. **Verification** A successful MinGW build with `CNA_BUILD_EXAMPLES=OFF`.

### REMED-TEST-005 — Weak tests: metadata-only assertions, dead subjects, unguarded checks

**Sev** LOW · **Pri** P3 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** A recurring shape: tests that assert metadata or capacity rather than actual data content or
actual code-path execution — so they pass whether or not the feature works.
**Evidence** `easygl_vertexbuffer_setdata_test.cpp` (capacity getters only, never checks uploaded bytes);
`easygl_dynamic_buffer_stress_test.cpp` (the index-buffer half never performs an indexed draw, so the pixel
verification its header promises does not exist); `easygl_msaa_test.cpp` (scene cannot distinguish
MSAA-resolved from never-engaged; also claims "4×" while configuring 8);
`bgfx_vertex_format_test.cpp` (`UploadAndCheck()` never calls `SetData`, so all 4 stride cases exercise the
same layout — and its subject is dead code, see `REMED-GFX-047`);
`bgfx_render_target_usage_test.cpp` (never reads back a pixel to verify Discard vs Preserve);
`bgfx_blendstate_separate_functions_test.cpp` (never reads the alpha channel, so `AlphaBlendFunction`
independence is inferred, not observed);
`easygl_depthstencilstate_compare_function_test.cpp` (only **5 of 8** `CompareFunction` values);
`headless_resource_backends_test.cpp` (Checks A/B are unconditional `check(true, ...)` with no `try`/`catch`,
so a real regression **crashes the test process** instead of reporting a clean `FAIL`).
Broader recurring patterns from the per-file sweep: **center-pixel-only readback** (the structural reason the
fog, skinned-normal, env-map, and Y-flip defects all survive a green CI); alpha never compared in color-equality
helpers; undocumented 20-iteration blank-frame retry loops that can yield a **false PASS**; golden values
captured from the implementation rather than analytically derived.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` LOW; `AUDIT_CROSS_CUTTING_FINDINGS.md` § Recurring testing gaps
**Affected files** the files above, plus the wider `examples-tests-*` shards
**Backends / Platforms** ALL / ALL
**XNA/FNA impact** Indirect but significant — **these weaknesses are why several P1 defects survived.**
**Security** N/A · **Memory/resource** N/A
**Strategy** Strengthen each to assert real observable output. **Prioritize the center-pixel-only and
analytically-derived-expectation patterns** — those are the ones that let real bugs through, and several
P1 tasks already require fixing them for their own verification. Treat the rest as ongoing hygiene.
**Required tests** Strengthened versions of the above.
**Regression tests** A review guideline: assert observable output, not metadata.
**Backend parity** Apply across all shards. **Dependencies** Overlaps the test work in `REMED-GFX-005`/`-006`/`-007`/`-011`.
**Cx** LARGE (breadth) · **PS** YES · **Verify** NO
**Completion** Named tests assert real content; the systemic patterns are addressed in the highest-value shards.
**Verification** Each strengthened test fails against a deliberately broken implementation.

### REMED-TEST-007 — `DecimalDateTimeContentTypeReaderTests.cpp`'s MSVC-only exclusion has no stated rationale

**Sev** LOW · **Pri** P3 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** A `#if !defined(_MSC_VER)` test/registration exclusion with no explanation, unlike every other
platform-conditional test in the same shard, which documents its reason.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § tests-cna-internal
**Affected files** `tests/.../DecimalDateTimeContentTypeReaderTests.cpp` · **Backends/Platforms** N/A / MSVC
**XNA/FNA impact** Unknown — **that is the finding.** The `DecimalReader` may be genuinely broken on MSVC,
or the exclusion may be obsolete. Nobody can tell from the source.
**Security** N/A · **Memory/resource** N/A
**Strategy** Determine why it was excluded (check `git log`/`git blame`), then either document the reason or
remove the exclusion if obsolete. If `DecimalReader` really is broken on MSVC, that is a **new production
finding** this task would surface.
**Required tests** If the exclusion is removed: the existing tests must pass on MSVC.
**Regression tests** N/A **Backend parity** N/A. **Dependencies** None.
**Cx** TRIVIAL (document) / UNKNOWN (if it exposes a real MSVC defect) · **PS** YES
**Verify** **YES** — establish whether the exclusion is still warranted
**Completion** The exclusion is documented or removed. **Verification** `git log` rationale recorded, or MSVC tests pass.

### REMED-DOCS-003 — `tools/avatar_builder/generate_animations.py`'s docstring describes 5 of 31 clips

**Sev** LOW · **Pri** P3 · **Owner** BUILD_TEST_CI · **Status** NOT STARTED
**Root cause** The top-of-file docstring was never updated as clips were added. Actual content:
`_GENERIC_BUILDERS` has 11 entries (`Stand0`-`Stand7`, `Wave`, `Clap`, `Celebrate`) plus 10
`_FEMALE_BUILDERS` and 10 `_MALE_BUILDERS`.
**Evidence** `AUDIT_CROSS_CUTTING_FINDINGS.md` § tools-avatar-builder. **This resolves a real open ambiguity**:
`validate_gltf.py`'s 8-name `Stand0`-`Stand7` requirement is **correct** and matches current behavior — the
project's README is the stale artifact, not the validation gate.
**Affected files** `tools/avatar_builder/generate_animations.py`; possibly the README
**Backends / Platforms** N/A / ALL
**XNA/FNA impact** N/A · **Security** N/A · **Memory/resource** N/A
**Strategy** Update the docstring to describe all 31 clips, or state the count and point at the builder tables.
**Required tests** N/A **Regression tests** Consider a test asserting the documented count matches the table lengths.
**Backend parity** N/A. **Dependencies** None. **Cx** TRIVIAL · **PS** YES · **Verify** NO
**Completion** The docstring matches reality. **Verification** Manual review against the builder tables.

---

# Accepted / no-action items

Recorded so the traceability check can account for them. Each was considered and **deliberately not turned
into a remediation task**, with the reason stated. None is a silent drop.

| ID | Finding | Disposition |
|---|---|---|
| `REMED-NA-001` | `.Content`'s 5 absent `ContentSerializer*Attribute` types | **NO ACTION.** C# custom attributes consumed by the build-time reflection-driven serializer. C++ has no attribute-based reflection, and CNA uses hand-written `ContentTypeReader<T>` registration by design. A deliberate architectural consequence, not an oversight. |
| `REMED-NA-002` | `.Content.Pipeline` (120 types) and `.Design` (66 members) entirely absent | **NO ACTION.** Positively scope-confirmed, not skipped: build-time content-pipeline tooling and WinForms `TypeConverter` subclasses respectively. CNA is a runtime. Zero matching files, correctly. |
| `REMED-NA-003` | `GraphicsDeviceInformation` missing `Equals`/`GetHashCode` | **NO ACTION.** Real XNA members, but **FNA never implements them either** — an FNA-inherited gap with no evident behavioral dependency on value-equality for this class. Reconsider only if a concrete need appears. |
| `REMED-NA-004` | `BoundingSphere::Contains(BoundingFrustum)` cannot return `Disjoint`; `BoundingFrustum::Intersects(Ray)` general case throws; `Curve::ComputeTangent()` asymmetric epsilons | **NO BEHAVIOR CHANGE.** All three are verified-faithful FNA reproductions (FNA's own source has the identical shapes, including its unimplemented `// TODO : calcul dmin`). Per the project's FNA-fidelity policy these must **not** be independently "fixed." Documentation-only follow-up is `REMED-CORE-013`. |
| `REMED-NA-005` | `StorageContainer`'s unchecked `Path.Combine`-style joins | **NO ACTION.** Confirmed FNA-faithful (FNA's `StorageContainer.cs` uses unchecked `Path.Combine` for every equivalent method). Explicitly **excluded** from `REMED-CONTENT-002`'s scope despite superficial similarity — the distinction from `DeleteContainer` (CNA-original, unsafe) is the whole point. |
| `REMED-NA-006` | `VertexBuffer`/`IndexBuffer` plain `SetData` has no bounds validation in any build config | **NO ACTION.** Matches FNA's dominant Release-mode behavior exactly (FNA's equivalent check is `[Conditional("DEBUG")]`). Parity, not regression. |
| `REMED-NA-007` | `EffectMaterial::Clone()` preserves type identity, unlike FNA (which would slice to a plain `Effect`) | **NO ACTION.** A deliberate improvement over FNA. Recorded for visibility, not treated as a defect. |
| `REMED-NA-008` | `BlendFunction.hpp`'s `Max`/`Min` Doxygen comments differ from FNA's | **NO ACTION.** FNA's own doc comments for those two values are internally swapped/contradictory; this port correctly declined to copy an upstream documentation bug. |
| `REMED-NA-009` | `GamePadState`/`MouseState` `GetHashCode()` comments imply a preserved FNA formula | **NO ACTION.** FNA's real implementation is `base.GetHashCode()` (an opaque CLR default with no portable equivalent), so CNA's custom formulas are necessary inventions that correctly satisfy the contract. Only the comment framing is imprecise — folded into `REMED-DOCS-001` if touched. |
| `REMED-NA-010` | `org/libsdl/app/*.java` (11 files) under `examples/demo_devices/android/` | **NO ACTION.** Unmodified upstream SDL3 Android glue (confirmed: zero project-specific matches). Correctly scoped as audit-eligible per the classifier but carries no findings. |
| `REMED-NA-011` | `easy-gl` sibling repo's `SmokeResourceTests.cpp` assertion failure | **OUT OF SCOPE.** External sibling repository, reference-only per decision D-6. Noted because it caused a CTest "Subprocess aborted"; **not a CNA finding.** Report upstream. |
| `REMED-NA-012` | 4 hardware-gated sensor tests reported "Skipped" | **NO ACTION.** Expected and correct — the project gracefully skips hardware-dependent accelerometer/gyroscope tests when no real device is present. |
| `REMED-NA-013` | SdlRenderer's 56 and Ascii's 52 general-suite failures | **NO ACTION.** Confirmed expected methodology noise: `CnaTests`' shared Model/glTF/CNJ content tests assume 3D capability and are not written backend-conditionally. Both backends correctly throw for every 3D-only entry point (clean `ThrowNo3D` results). Consider backend-conditional test registration as future hygiene, not a defect. |
| `REMED-NA-014` | D3D12 Proton-based swapchain-fix path unverified | **ENVIRONMENT LIMITATION.** No Steam/Proton installation in the audit sandbox. The crash itself **was** reproduced live via system-Wine, confirming it; the fix script is independently confirmed correct in its own audit report. Only the specific runtime verification is unavailable. Re-verify where Proton exists. |
| `REMED-NA-015` | `glslc`/`glslangValidator` missing, blocking 2 CNJ/custom-GLSL tests on Vulkan | **ENVIRONMENT LIMITATION.** Needs `apt-get install glslang-tools`. Not a CNA defect. Distinct from `REMED-GFX-035`, which is a real dialect incompatibility. |

---

# Addendum

### REMED-CONTENT-004 — Texture3D content-reader round-trip returns all-zero/garbage data

*(Placed here rather than in the P1 body purely because it was identified during the final
traceability pass; its priority is P1 and it should be scheduled with the other P1 content work.)*

**Sev** MEDIUM · **Pri** P1 · **Owner** CONTENT · **Status** NOT STARTED
**Root cause** Not determined. Strongly indicated to be **shared CPU-side code** — either the XNB/CNJ
Texture3D content reader, or `Texture3D`'s own generic `GetData`/`SetData` path — not a per-backend
GPU-texture defect.
**Evidence** Reproduces **identically on Software and Headless**:
`Texture3DTextureCubeContentTypeReaderTest.Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder`
and `CnjTexture3DTest.LoadsRealCnjFixture` fail on both; Headless additionally fails
`TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd`. SdlRenderer's failure list includes the same
content-type-reader test, though conflated there with its broader expected 2D-only failure set.
**Reproducing identically on a CPU rasterizer AND a no-op/no-GPU backend is the key signal** — neither
executes real GPU texture code, so the defect must be upstream of both.
**Audit refs** `AUDIT_FINDINGS_INDEX.md` MEDIUM § Pass 6 continued; `AUDIT_CROSS_CUTTING_FINDINGS.md`
§ Pass 6 final ("NEW, likely shared CPU-side defect")
**Affected files** `src/CNA/Internal/Xnb/Texture3DContentTypeReader.cpp`;
`src/Microsoft/Xna/Framework/Graphics/Texture3D.cpp` (generic `GetData`/`SetData`)
**Backends / Platforms** Software, Headless confirmed; SdlRenderer likely. **Not yet checked on
EasyGL, Vulkan, or any other backend** — no earlier Pass 6 run isolated this exact test. / ALL
**XNA/FNA impact** `Texture3D` content loaded from `.xnb`/`.cnj` yields zero or garbage pixel data —
the type is effectively non-functional for real content on the affected backends.
**Security** A reader returning garbage rather than failing cleanly may indicate an unvalidated size or
stride calculation — check for the same class of issue as `REMED-CONTENT-003` while investigating.
**Memory/resource impact** Unknown until root-caused; garbage data suggests a buffer/stride mismatch.
**Strategy** **Isolate the layer first.** Run the reader test with a hand-constructed in-memory
`Texture3D` bypassing the content reader: if that also fails, the defect is in `Texture3D`'s generic
path; if it passes, it is in the reader. Then run the exact test on EasyGL and Vulkan to establish the
true blast radius before fixing — the audit explicitly notes this was never determined.
Note `Texture3DContentTypeReader.cpp` is one of the two sibling readers that **correctly** has the
byte-count validation `REMED-CONTENT-003` found missing in the TextureCube reader, so the file is not
uniformly weak — this is a specific defect, not general neglect.
**Required tests** The two failing tests already exist and are adequate once the working directory is
fixed. Add a layer-isolating unit test for `Texture3D::SetData`/`GetData` independent of any reader.
**Regression tests** Add Texture3D round-trip to the cross-backend content suite.
**Backend parity** Run the exact failing tests on all 14 backends to establish real scope.
**Dependencies** `REMED-BUILD-001` (both tests are among the ~220 broken by the CTest bug — **they may
even be failing partly for that reason**, which must be ruled out first).
**Cx** MEDIUM (unknown until isolated) · **PS** YES · **Verify** **YES** — confirm the failures persist
after `REMED-BUILD-001` lands, and isolate reader-vs-`Texture3D` before fixing
**Completion criteria** Texture3D content round-trips correctly on every backend; the layer is identified.
**Verification criteria** Both named tests pass on Software, Headless, and every other backend.
