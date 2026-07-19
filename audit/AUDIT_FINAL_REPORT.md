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

## 5. What this audit did NOT do (explicitly out of scope or not completed)

- **No source code was modified, and no bug listed above was fixed.** This is a read-only inspection.
- **Pass 3 (a dedicated, systematic FNA/XNA parity sweep)** was never run as its own standalone pass with a
  fresh checklist; its intended findings were substantially pre-empted by defects found incidentally during the
  per-file backend/shard audits (fog formula, skinned-normal-transform, and the dozens of smaller items in
  `AUDIT_CROSS_CUTTING_FINDINGS.md`). A project owner wanting the *specific* guarantee "every public XNA 4.0
  member was diffed against FNA one at a time" should treat that as still open, distinct from "defects were
  found" (which did happen, extensively).
- **The full ~30-feature backend capability grid** (§4 above) remains unpopulated beyond the cross-cutting
  defect matrix, though its blockers are now cleared.
- **Pass 6 (systematic build/test/sanitizer evidence gathering)** was not run as a dedicated pass. Some
  evidence of this kind was gathered opportunistically and incidentally (e.g. `EasyGL_AvatarRenderer_TintRouting`
  was independently re-confirmed by an actual build+run during this audit, not merely relayed from a report), but
  a systematic sweep — building every backend, running every CTest suite, and cataloging every currently-failing
  test with its `WILL_FAIL` status — was not performed. The 3+ already-found unflagged failures (§2 item 13,
  §3 CI-masking risk) suggest more may exist.
- **No attempt was made to reproduce every finding at runtime.** Findings are graded by `Confidence`
  (`HIGH`/`MEDIUM`/`LOW`) per `AUDIT_CHECKLIST.md`'s scale; several `HIGH`-severity/`HIGH`-confidence findings
  above were confirmed via direct source-code tracing to a specific failing input or sequence, but this is
  static analysis at scale, not a substitute for actually running the affected code paths where that wasn't
  already done.

## 6. Final integrity verification

Re-confirmed at the close of this report:

- `audit/AUDIT_MANIFEST.md`'s rollup shows `PENDING: 0` for all 105 shards (`2297/2297` files `AUDITED`).
- `git diff --name-only <develop-branch-point> feature/audit -- . ':!audit'` returns `0` — no file outside
  `audit/**/*.md` was ever created, modified, or staged on this branch, across every commit.
- Working tree is clean (no uncommitted changes) as of this report.

## 7. Suggested next steps for the project owner

In rough priority order, based on blast radius and confirmed-vs-disclosed status:

1. Fix the two use-after-free bugs (§2 items 8, 10) — these are the only `HIGH`-severity findings with a
   concrete crash/corruption path, not just wrong pixels.
2. Fix `CNA::Logger::ToSDLPriority()` (§2 item 9) — foundational, always-compiled, currently silently
   mis-prioritizing every warning/error/fatal log line project-wide.
3. Decide on a remediation strategy for the fog-formula and skinned-normal-transform defects (§2 items 1-2) —
   given their traceable propagation via explicit cross-backend porting, a single shared-source fix (where a
   shared source exists, e.g. D3DCommon) or a coordinated per-backend fix pass (where it doesn't) is likely more
   efficient than treating each backend as an independent bug.
4. Add `WILL_FAIL`/skip annotations to the 3+ already-confirmed currently-failing CTests (§2 item 13, §3) so CI
   output stops conflating known gaps with new regressions — cheap, immediate risk reduction.
5. Address the `PackedVector` rounding defect (§2 item 12) and, separately, fix the test-input gap in
   `tools/fna-reference/PackedVectorReference.cs` that let it go undetected — otherwise a similar defect class
   could recur invisibly.
6. Narrow `.gitignore`'s `build*` pattern (§3) to the specific directories it was meant to exclude.
7. Schedule the documentation-rot sweep (§3) — several in-source "known bug" comments actively mislead a reader
   about the current state of already-fixed code.
8. If continuing this audit effort: complete the backend capability grid (§4) and run a dedicated Pass 6
   CTest sweep (§5) — both are now unblocked and represent the highest-value remaining synthesis work.
