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

## 5. Update (same day): Passes 4, 5 completed; Passes 3 and 6 substantially progressed

Since §1-4 above were written, four more passes ran:

- **Pass 4 (backend capability matrix) is now COMPLETE.** `AUDIT_GRAPHICS_BACKEND_MATRIX.md`'s
  previously-skeleton ~30-feature grid is populated (10 XNA-facing-feature rows uniform across all
  backends, 13 backend-facing-feature rows x 14 backends), plus a full EasyGL cross-comparison
  section. A handful of cells are honestly left `?` where the audit corpus genuinely lacks
  first-hand evidence (occlusion-query/MSAA/sRGB depth on several backends) rather than guessed.
- **Pass 5 (cross-cutting findings + findings index) is now COMPLETE.** `AUDIT_FINDINGS_INDEX.md`
  was rebuilt from a stale, graphics-backend-only partial draft into a full severity-ranked index
  (CRITICAL/HIGH/MEDIUM/LOW) plus By-subsystem and By-category views, covering the entire
  2297-file audit, not just the early batches.
- **Pass 3 (systematic FNA/XNA API-surface-completeness sweep) is now PARTIALLY done, with a
  genuinely new methodology**, not just pre-emption by incidental findings: using the real
  Microsoft XNA 4.0 Windows reference XML doc-comments (`/rv/data/library/github.com/borgesdan/
  xn65/references/Windows/*.xml` — more authoritative than FNA for API *surface*, since FNA itself
  sometimes omits real XNA members), every member of `Microsoft.Xna.Framework.Graphics` (781 XML
  entries, 95 types, 635 individually-checked members), `.Net` (23 types, ~120 members),
  `.GamerServices` (37 types, ~200 members), and the XACT subset of `.Audio` (7 types, ~60 members)
  was cross-referenced against CNA's actual implementation. Result: 95/95 and 27/27 (Graphics
  types/enums), 23/23 (Net), 37/37 (GamerServices) types present — the API surface is
  overwhelmingly complete. 5 genuine gaps survived manual verification of ~40 initial candidates
  (most were false positives from expected C#-to-C++ idiom translation): `DisplayMode.
  TitleSafeArea`/`ToString()` entirely missing; `VertexPositionColor` missing `IVertexType`
  (independently re-confirms an existing finding via a different method); `NetworkSession.
  MaxSupportedGamers`/`MaxPreviousGamers` mistagged `NOXNA`; `AudioCategory.ToString()` missing
  from both CNA and FNA. **Still open**: `Microsoft.Xna.Framework` (root namespace), `.Content`,
  `.Input.Touch`, `.Media`, `.Storage`, `.Avatar`, and the rest of `.Audio` beyond the XACT subset —
  a natural, likely similarly high-signal continuation given how clean these four sweeps were.
- **Pass 6 (build/test evidence gathering) is now PARTIALLY done, and found this audit's most
  operationally significant discovery.** Built `CnaTests` for EasyGL (the project's Linux default
  backend) and ran its full 5754-test CTest suite. After ruling out `-j8` parallelism noise (~100
  spurious failures that vanished on individual re-run), a reliable `-j2` baseline found 229 real
  failures with one root cause: `cmake/UnitTests.cmake`'s `gtest_discover_tests(CnaTests
  DISCOVERY_MODE PRE_TEST)` has no `WORKING_DIRECTORY` override, so every discovered test's working
  directory is baked in as the build directory, not the repo root where `tests/assets/**` fixture
  files live. This breaks ~220 tests covering Media/Audio-tag-parsing/Xnb-content-pipeline/ENet-
  networking/Lzx-decompression — confirmed side-by-side (the identical test passes when manually
  run with the correct working directory) — and is **invisible to every existing CI workflow**: all
  3 (`d3d-windows-ci.yml`/`devices-tests.yml`/`input-ci.yml`) use a `-L <label>`-filtered `ctest`
  invocation that never runs the general/default test set this bug affects, meaning these ~220
  tests have likely never once passed in any CI run this project has had. Two more concrete findings
  came out of the same sweep: a genuinely new HIGH defect (`EasyGL_MRT_TwoAttachments` —
  `SetRenderTargets` with 2 attachments only draws to the first one, confirmed reproducible in
  isolation) and a 4th confirmed instance of the CI-masking-risk pattern
  (`EasyGL_GraphicsDevice_ReferenceStencil`, disclosed in-comment as known-failing since Task
  319/872 but with no `WILL_FAIL` property). **Still open**: Bgfx, Vulkan, SdlGpu, D3D9, D3D11,
  D3D12, Dx3, WebGPU, SdlRenderer, Software, Ascii, Canvas were not built/tested in this pass; no
  sanitizer (ASan/UBSan/TSan) build was attempted at all.

Both `AUDIT_CROSS_CUTTING_FINDINGS.md` and `AUDIT_FINDINGS_INDEX.md` have been updated with all of
the above; this section is the only part of this report itself that has been refreshed to match —
§§1-4's per-backend/per-file counts above remain accurate and unchanged.

## 6. What this audit still did not do (updated)

- **No source code was modified, and no bug listed above was fixed.** This is a read-only inspection.
- Pass 3's API-surface sweep covers 4 of ~9 `Microsoft.Xna.Framework.*` namespaces (Graphics, Net,
  GamerServices, and the XACT subset of Audio) — the remainder (root namespace, Content, Input.Touch,
  Media, Storage, Avatar, the rest of Audio) is still open, though the four completed sweeps were
  consistently high-signal-to-effort (5 genuine gaps across ~1015 combined members checked).
- Pass 6's build/test sweep covers only EasyGL — the other 13 backends are unbuilt/untested in this
  audit; no sanitizer instrumentation was used at all.
- **No attempt was made to reproduce every finding at runtime.** Findings are graded by `Confidence`
  (`HIGH`/`MEDIUM`/`LOW`) per `AUDIT_CHECKLIST.md`'s scale; several `HIGH`-severity/`HIGH`-confidence
  findings above were confirmed via direct source-code tracing to a specific failing input or
  sequence, but this is static analysis at scale, not a substitute for actually running every
  affected code path.

## 7. Final integrity verification

Re-confirmed at the close of this report:

- `audit/AUDIT_MANIFEST.md`'s rollup shows `PENDING: 0` for all 105 shards (`2297/2297` files `AUDITED`).
- `git diff --name-only <develop-branch-point> feature/audit -- . ':!audit'` returns `0` — no file outside
  `audit/**/*.md` was ever created, modified, or staged on this branch, across every commit (re-verified
  independently against the real branch point on `develop`, not assumed).
- Working tree is clean (no uncommitted changes) as of this report's original writing; §5's update added
  further commits, all equally audit-only.

## 8. Suggested next steps for the project owner

In rough priority order, based on blast radius and confirmed-vs-disclosed status:

1. **Fix `gtest_discover_tests(CnaTests ...)`'s missing `WORKING_DIRECTORY` (§5, Pass 6)** — a
   one-line CMake fix (`WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"`, matching the pattern already
   correct in `cmake/Tests/EasyGLTests.cmake`'s golden-image tests) that would immediately restore
   ~220 tests to real, CI-visible pass/fail status. Given these tests currently can't fail loudly in
   CI at all, this is arguably higher-priority than any single behavioral bug below — it's the
   difference between "CI would catch a regression here" and "CI structurally cannot."
2. Fix the two use-after-free bugs (§2 items 8, 10) — these are the only other `HIGH`-severity
   findings with a concrete crash/corruption path, not just wrong pixels.
3. Fix `CNA::Logger::ToSDLPriority()` (§2 item 9) — foundational, always-compiled, currently silently
   mis-prioritizing every warning/error/fatal log line project-wide.
4. Decide on a remediation strategy for the fog-formula and skinned-normal-transform defects (§2 items 1-2) —
   given their traceable propagation via explicit cross-backend porting, a single shared-source fix (where a
   shared source exists, e.g. D3DCommon) or a coordinated per-backend fix pass (where it doesn't) is likely more
   efficient than treating each backend as an independent bug.
5. Fix the new EasyGL MRT second-attachment defect (§5, Pass 6) — a documented, presumably-once-working
   XNA feature (Task 145) that's currently confirmed broken.
6. Add `WILL_FAIL`/skip annotations to the 4 already-confirmed currently-failing CTests (§2 item 13, §3,
   §5) so CI output stops conflating known gaps with new regressions — cheap, immediate risk reduction.
7. Address the `PackedVector` rounding defect (§2 item 12) and, separately, fix the test-input gap in
   `tools/fna-reference/PackedVectorReference.cs` that let it go undetected — otherwise a similar defect class
   could recur invisibly.
8. Narrow `.gitignore`'s `build*` pattern (§3) to the specific directories it was meant to exclude.
9. Schedule the documentation-rot sweep (§3) — several in-source "known bug" comments actively mislead a reader
   about the current state of already-fixed code.
10. If continuing this audit effort: extend Pass 3's API-surface sweep to the remaining 5 namespaces, and
    extend Pass 6's build/test sweep to the other 13 backends plus a sanitizer-instrumented build — both are
    now well-precedented, proven-valuable methodologies (this session's Pass 3/6 work found 5 and 4+ new
    findings respectively, including this report's single highest-value discovery) rather than unproven ideas.
