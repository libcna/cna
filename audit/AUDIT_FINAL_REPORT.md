# AUDIT_FINAL_REPORT.md — CNA Repository-Wide Deep Audit: Final Report

**Status: COMPLETE.** Every AUDIT-eligible file in the repository has an individual `.audit.md` report, and every
manifest shard shows `PENDING: 0`. This document synthesizes that work into a single deliverable: what was
covered, what was found, and what remains open for the project owner to act on. Per the audit's own governing
rule, **no source code was changed as part of this work** — this is a read-only inspection producing
documentation under `audit/**/*.md` only.

## 1. Scope and methodology

- **2,634 tracked files** in the repository (`git ls-files`, branch `feature/audit`), split into:
  - **2,297 AUDIT-eligible files**, organized into **105 manifest shards** (`AUDIT_MANIFEST.md`) grouped by
    subsystem/backend for tractable, logically-batched review.
  - **337 EXEMPT files** across 8 reason categories (vendored third-party code, binary/generated assets, legal
    text, planning/tracking docs, VCS metadata, vendored test fixtures, vendored-verbatim stock-effect shaders) —
    see `AUDIT_SCOPE.md` for the exact classifier rules. Invariant `2297 + 337 = 2634` holds with zero
    unclassified files.
- **Every one of the 2,297 eligible files has its own `audit/<path>.audit.md` report** following the structure in
  `AUDIT_CHECKLIST.md`: Purpose, XNA/FNA parity (where applicable), Behavioral correctness, Logic, Memory/resource
  lifetime, C++ correctness, Performance, Thread safety, Architecture, Maintainability, Portability, Robustness,
  Testing, and Cross-file consistency, each rated `PASS`/`WARNING`/`FAIL`/`N/A`/`NOT VERIFIED` with concrete
  evidence (symbol names, line regions, test names, FNA source paths) — the anti-boilerplate rule requires every
  non-trivial finding to demonstrate the file was actually read.
- **Reference authority**: the local FNA source tree (`/rv/data/library/github.com/FNA-XNA/FNA`) is the primary
  behavioral/API reference, with the caveat (recorded early and applied throughout) that FNA itself sometimes
  omits real XNA 4.0 members that exist in the original Microsoft/xn65 API surface (e.g. certain `Song`
  properties) — parity claims were checked against XNA intent, not blindly against FNA's own gaps.
- **Coverage by area** (all shards `AUDITED`): 16 graphics-backend shards (~314 files: Ascii, Bgfx, Canvas, D3D9,
  D3D11, D3D12, D3DCommon, Dx3, EasyGL, Headless, SdlGpu, SdlRenderer, Software, Vulkan, WebGPU, Common) · 5 CNA
  internal-core shards (~205 files) · 9 `Microsoft.Xna.Framework` public-API shards (~541 files) ·
  `Microsoft.Devices`/`.Sensors` (54 files) · test suites (~350 files: `Microsoft.Xna`, CNA, Devices, misc) ·
  developer tooling (~124 files across 10 `tools/` shards) · examples (~797 files: demo apps + backend
  integration tests) · documentation (72 files) · build/CI/scripts (~47 files: CMake modules, CI workflows,
  developer shell/Python scripts).
- **Git discipline maintained throughout**: every commit on this branch touches only `audit/**/*.md`, verified
  repeatedly via `git diff --name-only <develop-branch-point> feature/audit -- . ':!audit'` returning `0` at
  every checkpoint (confirmed again at the close of this report, see §6).

## 2. Headline findings (see `AUDIT_FINDINGS_INDEX.md` for the complete, evidence-linked list)

No `CRITICAL` findings were confirmed. The most significant `HIGH`-severity findings, in rough order of
blast radius:

1. **Fog formula is backwards (mirrored) in Bgfx, Vulkan, and the entire shared D3DCommon shader library
   (D3D11+D3D12) — all 15 of D3DCommon's fog-capable shaders, the single widest-reaching confirmed defect in
   this audit.** The correct FNA formula is `(z+FogEnd)/(FogEnd-FogStart)`; these backends compute
   `(FogEnd-z)/(FogEnd-FogStart)`. Already fixed in EasyGL pre-session (Task 1111); never ported to the other
   three backend groups. D3D9's vendored stock effects are correct (real Microsoft bytecode); D3D9's own custom
   shaders have a *different* fog defect (object-space-only, ignores World/View entirely).
2. **`SkinnedEffect`'s world-space normal transform is completely missing across every one of the 14 backends
   that implement `SkinnedEffect`** (EasyGL, WebGPU, Vulkan, SdlGpu, D3D11, D3D12, Bgfx — with the bug's
   propagation explicitly traceable through self-documented "ported from EasyGL/Vulkan line-by-line" comments in
   at least 3 of them). Any rotated skinned model's lighting is wrong; invisible to existing tests because they
   all use `World=Identity`.
3. **SdlGpu backend: fog is completely unimplemented** across all 10 stock-effect shader families (not a wrong
   formula — a total absence, confirmed by exhaustive grep).
4. **D3D12: `StencilState` and `RasterizerState.ScissorTestEnable`/`DepthBias`/`SlopeScaleDepthBias` are
   completely non-functional** — parameters are received and literally left as commented-out unused code; every
   PSO hardcodes stencil/scissor off. Honestly disclosed as a scope cut in-code, but two commonly-used XNA
   features are fully inert on this backend.
5. **D3D12: `OcclusionQuery` only captures the last draw when multiple draws occur between `Begin()`/`End()`**
   (each draw's query overwrites the previous one on the same heap slot).
6. **Vulkan: `SpriteBatch.Begin(transformMatrix)` is silently dropped** — the only one of 14 checked backends
   (12 real implementations plus Ascii's delegation) that doesn't apply it. No test anywhere exercises a
   non-identity transform on Vulkan.
7. **Vulkan: the missing-Y-flip mirroring bug**, previously known only for `EnvironmentMapEffect`, also affects
   `PbrEffect`, `SkinnedPbrEffect`, and `InstancedEffect` — one of the four omits a Y-flip while its own
   in-source comment falsely claims a sibling shader "never Y-flips" (that sibling does, and says so).
8. **`FileDialog.cpp`/`MessageBox.cpp` (cna-devices) share a real use-after-free window**: a swappable-global
   backend pointer is dereferenced after its protecting mutex is released.
9. **`CNA::Logger::ToSDLPriority()` mistags every `Fatal`/`Error`/`Warn` log call as `SDL_LOG_PRIORITY_INFO`**
   — the switch's real cases are commented out with a literal `//todo`. Unlike every other finding above, this is
   foundational, always-compiled, project-wide infrastructure, not backend-specific.
10. **EasyGL: a constructor failure after `RegisterForWindow()` but before construction completes leaves a
    dangling entry in a static window registry**, later dereferenced unconditionally by `SdlInputBridge.cpp`/
    `Mouse.cpp` on the next mouse/input event — a real, reachable use-after-free. Flagged in the findings index
    as **"the most severe confirmed finding in this audit."**
11. **`SpriteFont::MeasureString`/`SpriteBatch::DrawString` dereference an `unordered_map::end()` iterator with
    no check**, reachable via fully public API (an unvalidated `DefaultCharacter` plus a genuinely-missing
    glyph) — undefined behavior where FNA throws `KeyNotFoundException`.
12. **`PackedVector/Byte4.hpp`/`Short2.hpp`/`Short4.hpp` truncate instead of round** in `Pack()` — a systematic
    off-by-up-to-1 error for any non-integer input. Root-caused during this audit: the project's own
    FNA-vs-CNA comparison harness (`tools/fna-reference/PackedVectorReference.cs`) uses only integer test
    inputs for exactly these three types, making the harness structurally incapable of catching this class of
    defect (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).
13. **A currently-failing CTest, `EasyGL_AvatarRenderer_TintRouting`, is registered with no `WILL_FAIL`
    annotation** — independently re-confirmed by direct build+execution during this audit (not merely relayed).
    Its Vulkan sibling passes only by coincidence, because a separate, independently-confirmed Vulkan
    `SkinnedEffect` bug cancels out the same miscalibration.

Numerous `MEDIUM` and `LOW` findings are cataloged in `AUDIT_FINDINGS_INDEX.md` and each file's own report,
including: Headless's instanced-draw primitive-count undercount; Software's non-functional depth-write-enable
and hardcoded depth-compare function; a Dx3 resize path that destroys working surfaces before confirming the
replacement succeeds; `BasicEffect::VertexColorEnabled` as a bare public field (violating this project's own C#
property convention); at least 2 more known-failing CTests with no `WILL_FAIL` annotation (`Bgfx_RenderTargetCube_DepthFormat`,
`Bgfx_SkinnedEffect_WeightsPerVertex`); and two SDL_Renderer tests whose expected-throw assertions are stale
relative to a real, intentional production behavior change.

## 3. Cross-cutting themes (full detail in `AUDIT_CROSS_CUTTING_FINDINGS.md`, ~1,800 lines)

- **Bug propagation through explicit cross-backend porting.** Several of the most widely-shared defects above
  were not independently reinvented per backend — they were copy-ported, with the porting comments themselves
  naming the source (`"ported line-by-line from EasyGL/Vulkan"`). This is a double-edged pattern: it explains
  *why* a defect appears in 5+ backends at once, and it means a single upstream fix, propagated the same
  explicit way it was copied, would very plausibly resolve most instances at once.
- **A strong, recurring, and worth-preserving positive discipline: known limitations are disclosed, not hidden.**
  Across `cmake/Tests/*.cmake` and multiple backend source files, this project consistently registers a test
  that's known to fail, with an in-source comment stating *exactly* which check fails and why (WebGPU's MSAA
  test left "intentionally registered and failing," Vulkan's blend-state tests each predicting per-check
  pass/fail against the known Task 868 hardcoding, Bgfx's environment-limitation findings empirically proven via
  an independent renderer switch rather than merely asserted). This is a genuinely uncommon level of honesty in
  a large test suite and should be maintained as new gaps are found.
- **Documentation-rot**: several in-source "known bug" comments were found to be stale — describing a defect
  that was later fixed without the comment being revisited (Vulkan blend state, `SetReferenceStencil`
  availability, anisotropic filtering, the `easygl_env_map_test.cpp` fog formula, `GetData()` availability). Not
  a single mistake but a recurring pattern worth a dedicated documentation sweep.
- **Raw `std::` exceptions instead of this project's own `System::` exception types** recur across dozens of
  files in nearly every shard audited (production code and tests alike) — already tracked as a single
  cross-cutting item rather than re-litigated per occurrence; see `AUDIT_CROSS_CUTTING_FINDINGS.md` for the
  running tally.
- **A `.gitignore` hygiene gap discovered *by* this very audit process**: the root `.gitignore`'s bare `build*`
  pattern (intended for an ad-hoc `build/` directory) silently matches any file anywhere in the repo whose
  basename starts with "build" — including this audit's own `build-cmake.md`/`build-cmake-tests.md` manifest
  shards and a `build.gradle.audit.md` report. Not fixed (outside this audit's `audit/**/*.md`-only scope) but
  flagged for the project owner; `git add -f` was required to track the affected manifest files.
- **CI-masking risk**: at least 3 confirmed currently-failing CTests carry no `WILL_FAIL`/skip annotation,
  meaning a CI run that doesn't already know to expect these specific failures would report them as regressions
  indistinguishable from a fresh, real one — or, if CI treats any failure as acceptable noise, real new
  regressions could hide alongside these known ones.

## 4. Backend maturity summary (full detail in `AUDIT_GRAPHICS_BACKEND_MATRIX.md`)

14 real backends (Ascii, Bgfx, Canvas, D3D9, D3D11, D3D12, Dx3, EasyGL, Headless, SdlGpu, SdlRenderer, Software,
Vulkan, WebGPU) plus 2 shared-infrastructure directories (D3DCommon, Common). File-count alone reveals a real
structural split: EasyGL/Dx3/Headless/SdlRenderer/Software/WebGPU are each a single monolithic adapter file
(EasyGL's is 4,733 lines; WebGPU's own single file, at 8,805 lines, is nearly the largest in the entire audit —
surpassed only by the split-across-many-files Vulkan backend's main file at 8,954 lines), while
D3D9/D3D11/D3D12/Bgfx/SdlGpu/Vulkan are split across dozens of files. This is a maintainability data point, not
itself evidence of feature completeness.

- **D3D9** stands out for stated design ambition ("pixel-for-pixel indistinguishability from the original XNA
  4.0 runtime, not mere feature parity") backed by concrete evidence: 61/66 byte-identical vendored-shader
  matches against Microsoft's real shipped bytecode, real Reach/HiDef profile enforcement, and loud
  over-request errors rather than silent degradation. It is also the most complete backend for the
  Stencil+Scissor+DepthBias feature triad (direct native render states, no emulation needed).
- **WebGPU** remains explicitly experimental per `CLAUDE.md` — this audit's findings are consistent with that
  status (e.g. the intentionally-left-failing MSAA test) and do not contradict the project's own stated
  capability boundary in `docs/webgpu-backend.md`.
- **Vulkan** carries the widest single-backend defect count found in this audit (missing-Y-flip across 4 effect
  families, the `SpriteBatch` transform-matrix no-op, hardcoded full-target scissor when a render target is
  bound, and the Task-868 blend-state hardcoding affecting at least 9 registered tests) despite being one of the
  more actively-tested backends — a sign the defects are real and specific, not evidence of low overall test
  investment.
- **The full ~30-feature capability grid** (presentation modes, disposal semantics, sRGB/gamma, CI coverage per
  backend) that `AUDIT_GRAPHICS_BACKEND_MATRIX.md` marks as still-skeleton can now be completed without further
  file audits — the blocking shards (`xna-graphics`, `tests-xna-graphics`, and the rest of `tests-*`) that
  matrix's own header names as prerequisites are now all `AUDITED`. Populating that grid is the highest-value
  remaining synthesis task and does not require re-reading source; it requires re-reading already-written
  `.audit.md` reports for the relevant disposal/effect-parameter evidence.

## 5. FINAL update: Passes 3, 4, 5, and 6 are now ALL COMPLETE

Since §1-4 above were first written, every remaining pass has been finished in full. This section
supersedes the earlier "substantially progressed" interim update.

### Pass 4 (backend capability matrix) — COMPLETE

`AUDIT_GRAPHICS_BACKEND_MATRIX.md`'s ~30-feature grid is fully populated (10 XNA-facing-feature rows
uniform across all backends, 13 backend-facing-feature rows x 14 backends), plus a full EasyGL
cross-comparison section and — added after Pass 6 finished — a per-backend "Pass 6
runtime-verification summary" table. A handful of grid cells remain honestly marked `?` where even
the runtime sweep didn't specifically exercise that exact feature; these are now few and genuinely
narrow, not broad unknowns.

### Pass 5 (cross-cutting findings + findings index) — COMPLETE

`AUDIT_FINDINGS_INDEX.md` was rebuilt from a stale, graphics-backend-only partial draft into a full
severity-ranked index (CRITICAL/HIGH/MEDIUM/LOW) plus By-subsystem and By-category views, covering
the entire 2297-file audit plus every Pass 3/6 finding below.

### Pass 3 (systematic FNA/XNA API-surface-completeness sweep) — COMPLETE

Using the real Microsoft-shipped Windows XNA 4.0 reference XML doc-comments
(`/rv/data/library/github.com/borgesdan/xn65/references/Windows/*.xml` — more authoritative than FNA
for API *surface*, since FNA itself sometimes omits real XNA members), **every real
`Microsoft.Xna.Framework.*` namespace with runtime-relevant surface** was swept: Graphics (781
entries, 95 types, 635 members), Net, GamerServices, the full Audio namespace (XACT + plain, 19
types), the root `Microsoft.Xna.Framework` namespace (Vector2/3/4, Matrix, Color, Rectangle, Game,
GameComponent, GameTime, Bounding*, Curve family, etc. — 42 types, ~900 members), Storage,
Input.Touch, Video, GamerServices.Avatar* (resolving a real scope question along the way — the real
XNA Avatar API is namespaced under `GamerServices`, not separately, confirming CNA's own placement
was already correct), Graphics.PackedVector, Content (the runtime API), and Input
(GamePad/Keyboard/Mouse). `.Content.Pipeline` and `.Design` were both explicitly confirmed correctly
out of scope (build-time content-pipeline tooling and WinForms property-grid `TypeConverter`s
respectively — CNA is a runtime, zero matching files for either) rather than left unswept.

**Total: ~2700+ individually-checked real XNA 4.0 members, 7 genuine gaps found**:
- 2 MEDIUM: `DisplayMode.TitleSafeArea`/`ToString()` entirely missing from CNA (present in FNA); all
  16 concrete `PackedVector` types (`Byte4`, `Short2`, etc.) entirely lack `Equals()`/`GetHashCode()`/
  `ToString()` (confirmed present and non-trivial in FNA, and present on every comparable CNA value
  type, making this an isolated gap in one type family).
- 1 re-confirmation via an independent method: `VertexPositionColor` missing `IVertexType`.
- 4 LOW: `GraphicsDeviceInformation` missing `Equals`/`GetHashCode` (FNA-inherited, not CNA-specific);
  `AudioCategory.ToString()` missing from both CNA and FNA; `NetworkSession.MaxSupportedGamers`/
  `MaxPreviousGamers` mistagged `NOXNA`; `KeyboardState::ToString()` mistagged `NOXNA`.

CNA's declared XNA-facing API *surface* is confirmed overwhelmingly complete — this audit's other
passes found real behavioral bugs, but almost nothing is actually *missing*.

### Pass 6 (build/test/sanitizer evidence gathering) — COMPLETE, and the source of this audit's single most severe finding

**Every one of the 14 real graphics backends** (EasyGL, Canvas, D3D9, D3D11, D3D12, Dx3, WebGPU,
Vulkan, SdlGpu, Bgfx, SdlRenderer, Software, Ascii, Headless) was built AND runtime-tested this
session — not merely statically reviewed. Windows-only backends (D3D9, D3D11, D3D12) were verified
via genuine MinGW cross-compilation + Wine+DXVK/vkd3d-proton (this project's own established CI
pattern, executed for the first time in this audit, confirmed via real DXVK/vkd3d-proton log lines,
not a silent fallback) — this is the strongest feasible verification available in this Linux
sandbox, short of a real Windows machine. Dx3 turned out to need neither (its `free-direct`
dependency is a from-scratch SDL3-based reimplementation, not real DirectDraw). Canvas/Emscripten
turned out to be genuinely buildable (an `emsdk` install exists in this sandbox, correcting an
earlier "unavailable" assumption). The one specific environmental limitation actually encountered —
D3D12's Proton-based swapchain-crash fix path, since no Steam/Proton installation exists in this
sandbox — is explicitly marked as **unavailable in this environment**, not silently skipped: the
crash itself was reproduced live via the standard system-Wine path, confirming it, and the
documented fix script is independently confirmed correct in its own audit report; only the specific
runtime verification of that fix path is unavailable here.

**HEADLINE FINDING, CRITICAL/HIGH, likely the single most severe finding of this entire audit**: a
real, security-relevant memory-safety crash reachable via any corrupted or maliciously-crafted
`Texture2D` `.xnb` asset, confirmed on **two independent backends** — Vulkan (`*** stack smashing
detected ***`, from unvalidated XNB-decoded texture dimensions driving `vkCreateImage`) and WebGPU (a
non-catchable Rust panic across the wgpu-native FFI boundary, from a lazily-timed mip-level
validation gap). Confirmed clean on EasyGL. Both share the identical underlying shape: XNB-decoded
`width`/`height`/`mipLevels` are trusted and passed directly into a native GPU API with no CNA-side
sanity check, and the native API's own validation (advisory on Vulkan, lazily-timed on WebGPU) does
not substitute for one. `XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly`
— a test whose entire stated purpose is guaranteeing this never happens — genuinely crashes the
process on both backends, 100% reproducibly. Real crash-DoS exposure for any game loading content
from disk, mods, or network transfer.

**Other Pass 6 findings**:
- A project-wide `WORKING_DIRECTORY` CTest registration gap (`gtest_discover_tests(CnaTests ...)`
  has no override) that breaks ~220 fixture-loading tests, invisible to every existing CI workflow
  (all 3 use a `-L`-filtered invocation that never runs the general test set this affects) —
  independently discovered twice, converging on the same root cause.
- A universal `cna_demo_xact` build defect (an unconditional `POST_BUILD` copy step for a `Content/`
  directory that doesn't exist anywhere in the repo), precisely root-caused after 6 different
  backend builds all independently hit and initially dismissed it as "pre-existing, unrelated."
  Silently also broke this session's own earlier EasyGL build, masked by a `| tail` pipe.
  the `MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` SEGFAULT is now confirmed universal
  across 6+ backends, solidifying it as a genuine, backend-independent robustness bug.
- This project has never adopted CTest's `WILL_FAIL` mechanism anywhere, for any backend — now 6+
  concrete already-failing, unflagged CTest instances catalogued across the full sweep.
- 2 stale findings corrected: Task 868 (Vulkan `BlendState` hardcoding) is now CONFIRMED FIXED; the
  `WebGPU_Msaa` "intentionally left failing" characterization is stale (fixed 2026-07-18).
- Several new per-backend defects: D3D11's Blinn-Phong specular asymmetry and a black-vertex-color
  bug; D3D12's real testing-coverage gap (only 1 CTest exists for 2 of its most significant static
  findings); Bgfx's default-cull-mode failure (with a concrete `glFrontFace`-relativity hypothesis);
  SdlGpu's stricter SPIR-V GLSL dialect rejecting real user-authored EasyGL-compatible content; a
  shared Texture3D content-reader round-trip defect (Software, Headless, likely SdlRenderer); a
  shared `WireFrame`-capability test-authoring pattern across 5 backends; and
  `SDL_Renderer_FullscreenToggle`'s uncaught-exception crash.
- The `Dx3_SpriteBatch` 2/10-failing-checks investigation (a standing, multi-session project memory
  item) is now **empirically closed**: both static predictions (Check D a test-authoring bug, Check
  G a real backend defect) confirmed by directly running the binary, with a new lead that Check G's
  rotation bug may be shared with `SoftwareGraphicsBackend.cpp`'s identical, verbatim-ported formula.

Full narrative for every finding above lives in `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Pass 3"/"Pass 6"
sections (~2500 lines total); the severity-ranked summary lives in `AUDIT_FINDINGS_INDEX.md`.

## 6. What this audit did NOT do (final)

- **No source code was modified, and no bug listed above was fixed.** This is a read-only
  inspection, start to finish.
- **No attempt was made to reproduce every single finding at runtime** beyond what Pass 6's build/
  test sweep covered — findings are graded by `Confidence` (`HIGH`/`MEDIUM`/`LOW`) per
  `AUDIT_CHECKLIST.md`'s scale; many `HIGH`-confidence findings were confirmed via direct
  source-code tracing to a specific failing input/sequence rather than an executed reproduction,
  which is still static analysis at scale, not a substitute for running every affected code path.
  Pass 6 closed much of this gap for the graphics-backend layer specifically (all 14 backends now
  runtime-tested), but the wider codebase (Net, GamerServices, Devices, tools) was not re-verified
  at runtime beyond what Pass 2's own per-file audits and the existing test suite already covered.
- No sanitizer-instrumented (ASan/UBSan/TSan) build was run as part of Pass 6 — this project's own
  pre-existing `devices-asan`/`devices-tsan`/`devices-ubsan` CMake presets (confirmed real and
  already historically used, per their own descriptions in `CMakePresets.json`) were not re-executed
  in this session; a genuine opportunity for a future pass.
- A few `AUDIT_GRAPHICS_BACKEND_MATRIX.md` grid cells remain marked `?` (occlusion-query multi-draw
  semantics on several backends, MSAA resolve-correctness depth beyond D3D11, sRGB coverage beyond
  Vulkan) — narrow, specific gaps, not broad unknowns, left unguessed rather than filled
  speculatively.

## 7. Final integrity verification

Re-confirmed at the close of this report:

- `audit/AUDIT_MANIFEST.md`'s rollup shows `PENDING: 0` for all 105 shards (`2297/2297` files
  `AUDITED`).
- `git diff --name-only <develop-branch-point> feature/audit -- . ':!audit'` returns `0` — no file
  outside `audit/**/*.md` was ever created, modified, or staged on this branch, across every commit
  from the very first to the very last, re-verified independently against the real merge-base with
  `develop` (not `master`, which shares no common history with this branch — confirmed via
  `git merge-base`), not assumed.
- Every one of Pass 3's and Pass 6's dispatched investigations wrote only to dedicated scratch files
  outside the repository, or (where forks self-committed despite instructions not to) committed only
  `audit/**/*.md` content — verified via `git log`/`git status` after every batch, with two duplicate/
  redundant sections found and cleaned up during consolidation (never a scope violation, only
  narrative redundancy).
- Working tree is clean (no uncommitted changes) as of this final report update.

## 8. Suggested next steps for the project owner

In rough priority order, based on blast radius and confirmed-vs-disclosed status:

1. **Fix the Texture2D-fuzz crash on Vulkan and WebGPU (§5, Pass 6)** — the single highest-priority
   item in this entire report. A real, security-relevant crash-DoS reachable via any malformed/
   malicious `Texture2D` asset, on two backends. Validate decoded `width`/`height`/`mipLevels` against
   real device limits immediately after XNB decode, ideally in shared content-reading code rather
   than duplicated per-backend.
2. **Fix `gtest_discover_tests(CnaTests ...)`'s missing `WORKING_DIRECTORY`** — a one-line CMake fix
   (`WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"`) that would immediately restore ~220 tests to real,
   CI-visible pass/fail status across every backend, not just the ones exercised by this pass.
3. Fix the two use-after-free bugs (§2 items 8, 10) — concrete crash/corruption paths, not just
   wrong pixels.
4. Fix `CNA::Logger::ToSDLPriority()` (§2 item 9) — foundational, always-compiled, silently
   mis-prioritizing every warning/error/fatal log line project-wide.
5. Decide on a remediation strategy for the fog-formula and skinned-normal-transform defects (§2
   items 1-2), given their traceable cross-backend porting — a single shared-source fix where one
   exists (e.g. D3DCommon) is likely more efficient than treating each backend independently.
6. Fix the `cna_demo_xact` build defect (§5) — a one-line CMake guard, but currently breaks the
   unfiltered top-level build for every single backend.
7. Fix the EasyGL MRT second-attachment defect and the D3D11/Bgfx/SdlGpu/SdlRenderer defects found
   during Pass 6 (§5) — real, newly-confirmed rendering/robustness bugs.
8. Adopt `WILL_FAIL`/skip annotations project-wide for the 6+ already-confirmed currently-failing
   CTests (§5) so CI output stops conflating known gaps with new regressions.
9. Address the `PackedVector` rounding defect and its systemic missing `Equals`/`GetHashCode`/
   `ToString` (§2 item 12, §5 Pass 3), and separately fix the test-input gap in
   `tools/fna-reference/PackedVectorReference.cs` that let the rounding bug go undetected.
10. Narrow `.gitignore`'s `build*` pattern and schedule the documentation-rot sweep (§3) — both
    cheap, low-risk hygiene fixes already fully scoped by this audit.
11. If continuing this audit effort: a sanitizer-instrumented (ASan/UBSan/TSan) run using this
    project's own existing presets, and a full runtime re-verification pass for the non-graphics
    subsystems (Net, GamerServices, Devices), are the two highest-value remaining extensions —
    everything else this report identified as "still open" in earlier drafts is now closed.
