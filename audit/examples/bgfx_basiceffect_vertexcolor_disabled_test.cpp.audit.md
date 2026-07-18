# Audit: examples/bgfx_basiceffect_vertexcolor_disabled_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_vertexcolor_disabled_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` `VertexColorEnabled=false`, no texture, diffuse-only
  pixel test; the **original** file (Task 364) that first documented the Bgfx cull-state default bug for this
  whole test family
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_disabled …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_VertexColorDisabled …)`, `cmake/Tests/BgfxTests.cmake:281-284`)
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled` (default `false`), `DiffuseColor`, the
  simplest `PSBasic`/`VSBasic` no-texture no-lighting shader path
- FNA reference: `HLSL/BasicEffect.fx` (`VSBasic`/`PSBasic`: `vout.Diffuse = DiffuseColor;` unconditionally,
  `color = pin.Diffuse`), `RasterizerState.cs` (parameterless constructor default `CullMode =
  CullMode.CullCounterClockwiseFace`, confirmed by direct inspection)
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc`, `fs_colored3d.sc`;
  `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp:396`; `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:162,207`

## Purpose

2-check pixel test proving `BasicEffect`'s simplest shader path: with `LightingEnabled=false`,
`TextureEnabled=false`, `VertexColorEnabled=false` (all real FNA defaults per Task 361), the shader outputs
`DiffuseColor*Alpha` and is not influenced by any per-vertex color attribute in the vertex buffer — the test
deliberately uses a bright-red `VertexPositionColor` vertex color that must be ignored. Per the file's own
extensive header comment, this is also the **first** Bgfx pixel-readback test in the codebase's history to
exercise `GraphicsDevice::GetBackBufferData()` for a `BasicEffect` draw, and the file that originally
discovered and documented the cull-state default mismatch across the three graphics backends (Bgfx's
hardcoded `cullFlags_ = BGFX_STATE_CULL_CCW` happening to match FNA's real default, while EasyGL/Vulkan's own
hardcoded defaults did not, at the time this file was authored).

## Executive Verdict

**Healthy** — the 2 checks are simple, correct, and independently re-derived to match exactly; the file's own
detailed technical narrative about the cull-state discovery was corroborated point-by-point against the
current codebase (the `BGFX_STATE_CULL_CCW` default, the `GetBackBufferData()`/`ReadBackbuffer()`
implementation detail, and the FNA `RasterizerState` default all check out). The one issue is that this
narrative — accurate when written — is now stale in one respect: the cross-backend default-push gap it
describes as unaddressed was subsequently closed (see F1), and its own self-assigned tracking number ("new
Task 884") turned out not to match the task that eventually fixed it.

## Checklist Results

### API / XNA / FNA parity
`fx.setDiffuseColorProperty(Vector3(0.2f, 0.6f, 0.9f))` (line 119) is the only property this test sets;
`VertexColorEnabled`/`TextureEnabled` are deliberately left at their real FNA defaults (`false`/`false`),
correctly not set at all (comment lines 117-118) rather than explicitly set to `false` — a good practice that
would also catch a regression in the *default value itself*, not just the gating logic.

### Behavioral correctness
Re-derived: `DiffuseColor=(0.2,0.6,0.9)`, `Alpha=1.0` (default). `R=0.2*255=51`; `G=0.6*255=153`;
`B=0.9*255=229.5→230` (standard round-half-up) → **(51,153,230)**, exact match to `kExpected`. The second
check (`!looksRed(got)`, using a loose `R≥200 && G≤60 && B≤60` heuristic rather than an exact-match negative)
correctly would catch the specific failure mode this test targets (vertex red leaking through) without being
so tight it could spuriously fail on unrelated rendering noise — an appropriately-scoped negative check.

### Logic
Single, non-parameterized scene — appropriately minimal for the simplest shader path in this whole family.

### C++ correctness
No issues found.

### Robustness
`looksRed()`'s deliberately loose thresholds (`R≥200`, not `R==255`) are well-chosen: strict enough to
unambiguously detect the specific failure mode (raw vertex red bleeding through unmultiplied) while loose
enough not to be brittle against unrelated minor rendering variance.

### Testing
2 checks (positive diffuse-only value, negative not-vertex-red) is complete and minimal for this feature.

### Cross-file consistency
Independently verified every specific technical claim in this file's unusually long header comment
(lines 18-38) against the current codebase:
- `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp:396`: `uint64_t cullFlags_ = BGFX_STATE_CULL_CCW;`
  — confirmed, matches the comment's claim exactly.
- `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:162`: `rasterizerState_(RasterizerState::CullCounterClockwise)`
  — confirmed, matches the comment's claim that `GraphicsDevice`'s own default field is FNA-correct.
- `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/States/RasterizerState.cs:125-128`: parameterless
  constructor sets `CullMode = CullMode.CullCounterClockwiseFace` — confirmed, matches the comment's claim
  about FNA's actual real default.
- `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:303-325`: `ReadBackbuffer()` is indeed implemented
  via `bgfx::requestScreenShot(...)` plus a callback — confirmed, matches the comment's claim about the
  implementation mechanism (lines 12-14).
Every specific technical detail in this file's header holds up under direct source inspection — this is a
well-researched, evidence-based comment, not speculation.

## Detailed Findings

### F1 — The file's own "tracked as new Task 884, not fixed in this task" claim used a task number that was never actually assigned to this bug, and the underlying issue it describes was subsequently fixed elsewhere in the codebase

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 30-38 (`"tracked as new Task 884, not fixed in this task"`)
- Evidence: `git log --oneline --all | grep "Task 884"` shows the real Task 884 as
  `75aefb7b fix(Task 884): EffectParameterCollection/EffectPassCollection dangling-pointer hazard` — an
  unrelated fix, never the cull-state issue this file describes at length. The cull-state default-push gap
  was instead fixed as Task 896: `b6a00bc6 fix(Task 896): push GraphicsDevice's real default RasterizerState
  to all 3 backends`, confirmed via `git merge-base --is-ancestor b6a00bc6 HEAD` to be an ancestor of the
  currently checked-out commit. `GraphicsDevice.cpp` line 207 (`setRasterizerStateProperty(rasterizerState_);`,
  called in the constructor right after `createBackend()`/`UpdateViewportFromWindow()`) confirms the fix is
  live: `GraphicsDevice`'s real `CullCounterClockwiseFace` default is now pushed to whichever backend is
  active — EasyGL and Vulkan included, not just Bgfx — at construction time. This file's own last content
  change is commit `736d3b95` (Jul 6 16:48), predating `b6a00bc6` (Jul 7 19:39) by about a day.
- Why it matters: this is the **origin file** for the cull-state narrative that all 7 sibling files in this
  batch repeat (some faithfully continuing the wrong "Task 884" label, others correctly updating to "Task
  896" once that task existed) — so its own self-assigned tracking reference was apparently aspirational
  ("new Task 884") at the time of writing rather than a confirmed number, and it never was corrected once the
  real fix landed under a different number. The functional workaround this file performs
  (`RasterizerState::CullNone`, line 137) remains entirely correct and necessary today — this specific quad's
  winding genuinely needs no-culling under FNA's real default on every backend now — but a reader relying on
  this comment to check "has this architectural gap been addressed yet" would be misled on two counts: the
  wrong task number, and the now-outdated "not fixed" status.
- FNA/XNA comparison: N/A (documentation-accuracy; the underlying `CullCounterClockwiseFace` default claim
  itself was independently confirmed correct against FNA's `RasterizerState.cs`).
- Related files: this is the shared root of the same finding recorded in all 7 sibling files in this batch
  (`bgfx_basiceffect_{multilight_emissive,normaltransform,one_light,preferperpixellighting,specular,
  texture_enabled,texture_vertexcolor_enabled}_test.cpp`).
- Suggested future action (not implemented by this audit): update this comment (and its 7 descendants) to
  reference Task 896 (`b6a00bc6`) as the commit that actually closed the cross-backend default-push gap.

## Cross-File Observations

- This file is the historical origin of the cull-state narrative repeated (with two different task-number
  variants) across all 8 files in this batch — auditing it in detail here let this audit independently verify
  the *factual* claims (Bgfx's hardcoded default, FNA's real default, the `ReadBackbuffer()` mechanism) that
  the other 7 files' shorter comments merely reference without re-explaining.
- Confirms this is genuinely the first Bgfx `BasicEffect` pixel-readback test (per its own claim, corroborated
  by this file's Task-364 tracking number being the earliest in this batch's chronology, and by `git log`
  showing no earlier Bgfx `BasicEffect` pixel test predates it).

## Missing or Weak Tests

None found — the 2-check set is complete and appropriately minimal for this, the simplest shader path in the
whole `BasicEffect` Bgfx test family.

## Positive Findings

- Every specific technical claim in this file's unusually detailed header comment (cull-state defaults across
  all 3 backends, FNA's real default, the `ReadBackbuffer()` implementation mechanism) was independently
  verified accurate by direct source inspection — a well-researched piece of test documentation, let down only
  by having become stale in one specific, narrow respect (F1) after a later fix landed elsewhere.
- The deliberately-loose `looksRed()` negative-check threshold is a well-judged piece of test design, avoiding
  both false negatives (missing the real failure mode) and false positives (spurious failures from unrelated
  rendering noise).
- Correctly leaves `VertexColorEnabled`/`TextureEnabled` unset rather than explicitly `false`, so this test
  would also catch a regression in the properties' own default values, not merely in their gating logic.

## Final Assessment

A simple, correct, and unusually well-documented test — the origin file for a real, accurately-diagnosed
cross-backend architectural bug at the time it was written. Its only issue now is that the fix it describes as
open (tracked under a task number that, as it turned out, was never actually assigned to this bug) was
subsequently closed under a different task number (896), and neither this file nor its 7 descendants in this
batch have been updated to reflect that.
