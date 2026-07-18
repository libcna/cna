# Audit: examples/bgfx_basiceffect_vertexcolor_enabled_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_vertexcolor_enabled_test.cpp` (151 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect.VertexColorEnabled=true` (no texture)
  pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_enabled …)` /
  `cna_register_backend_test(NAME Bgfx_BasicEffect_VertexColorEnabled …)`,
  `cmake/Tests/BgfxTests.cmake:287-289`).
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled`, `BasicEffect.DiffuseColor`,
  `IEffectLights`-free no-lighting/no-texture shader path.
- FNA reference: FNA's `EffectHelpers`/`BasicEffect.cs` diffuse-color forwarding semantics
  (`vout.Diffuse = DiffuseColor`, then `vout.Diffuse *= vin.Color` when `VertexColorEnabled`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()`, lines 45-72), `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc`
  (lines 19-23: `vc = enabled ? a_color0 : vec4(1)`, `v_color0 = vc * u_diffuseColor`).

## Purpose

Draws a 2-triangle quad (`VertexPositionColor`, per-vertex color `(200,100,50,200)`) with
`BasicEffect.VertexColorEnabled=true`, `DiffuseColor=(0.8,0.4,0.6)`, no texture, no lighting, and
asserts the rendered centre pixel equals the exact component-wise product `VertexColor*DiffuseColor`
(lines 46-51), while also asserting it does *not* equal either input alone (`DiffuseColor`-only or
`VertexColor`-only) — a real three-way discriminating design, not a single tautological assertion.

## Executive Verdict

**Mostly healthy** — the test's own arithmetic and its 3 assertions were independently re-derived and
confirmed correct against both `BasicEffect::FillGpuDrawParams()` and `vs_colored3d.sc`'s real current
formula. The one issue found (F1) is a **stale, no-longer-accurate header comment**, not a functional
defect: the workaround it uses (`RasterizerState::CullNone`) is still genuinely necessary and correct,
but the comment's stated *reason* for it ("Bgfx is the only one of the 3 backends" with a correct
default cull state) has been overtaken by a later, unrelated fix and is provably false as of the
current codebase.

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = true` (line 98) is a direct public-field assignment, not a
`setXProperty()`-style call — this matches `BasicEffect.hpp:48`'s actual declaration
(`bool VertexColorEnabled = false;`, a raw public field, unlike `DiffuseColor`'s
`getDiffuseColorProperty()`/`setDiffuseColorProperty()` pair used two lines later at line 99). This is
a pre-existing inconsistency in `BasicEffect.hpp` itself (out of this file's scope — that header is not
part of this audit batch), not something introduced by this test; the test simply uses the production
API as it currently exists, correctly.

### Behavioral correctness
Re-derived the expected product by hand: `VertexColor` normalized = `(200/255, 100/255, 50/255) =
(0.7843, 0.3922, 0.1961)`; `DiffuseColor = (0.8, 0.4, 0.6)`; `alpha_` defaults to `1.0f`
(`BasicEffect.hpp:366`), so `u_diffuseColor = DiffuseColor*alpha = (0.8,0.4,0.6)` unchanged
(`BasicEffect.cpp:67-70`). Component product: `0.7843*0.8=0.6275→160`, `0.3922*0.4=0.1569→40`,
`0.1961*0.6=0.1176→30` — matches `kExpected(160,40,30,255)` (line 49) *exactly*, not approximately.
Confirmed the shader performs precisely this multiply: `vs_colored3d.sc:22-23`,
`vc = (u_vertexColorEnabled3D.x>0.5) ? a_color0 : vec4(1)`; `v_color0 = vc * u_diffuseColor` — no
lighting/texture term intervenes since `BasicEffect::FillGpuDrawParams()` only adds those when
`lightingEnabled_`/`textureEnabled_` are set (neither is, here).
`kDiffuseOnly(204,102,153)` (`200*0.8/0.7843` is not how it's derived — it's simply "what a
VertexColor=White read would look like": `1.0*0.8=0.8→204`, `1.0*0.4=0.4→102`, `1.0*0.6=0.6→153`) and
`kVertexOnly(200,100,50)` (`DiffuseColor=(1,1,1)` case) are both correctly *not* equal to `kExpected`
within the `±8` tolerance used by `matches()` (line 77-82) — minimum channel separation is 20 (B channel,
`kExpected.B=30` vs `kVertexOnly.B=50`), comfortably outside the `±8` band, so the two negative
assertions (lines 125-128) cannot pass by accidental tolerance overlap.

### Logic
The 20-iteration retry loop (lines 109-121) re-`Clear()`s and re-`Draw()`s every iteration before
reading, breaking on the first non-black centre pixel — the established, correct pattern for Bgfx's
"first read per rendered frame" `GetBackBufferData` limitation (each iteration is its own genuine
Clear+Draw+Read cycle, not a bare re-read of stale state).

### Robustness
`fx.setDiffuseColorProperty(kDiffuse)` and `fx.VertexColorEnabled=true` are both applied once, before
the loop, then `fx.Apply()` re-invoked every iteration — correct, since `Apply()` (not construction) is
what pushes `GpuDrawParams` to the backend each draw.

### Testing
Three genuinely distinct assertions from one rendered pixel (equals the product, not-equals either
factor alone) is an efficient, real discriminating design — a regression that dropped the
`VertexColorEnabled` gate entirely (always outputting `DiffuseColor` alone) or one that ignored
`DiffuseColor` (outputting raw vertex color) would each be caught by a different one of the three
checks.

## Detailed Findings

### F1 — Header comment's claim that Bgfx is "the only one of the 3 backends" with a correct default cull state is stale; the project-wide fix (Task 896) that supersedes it landed the day after this file was authored

- Severity: LOW
- Confidence: HIGH (git history + current source both directly confirm the timeline and the current
  shared behavior)
- Category: stale-comment / documentation-accuracy (not a functional defect — the workaround itself
  remains correct and necessary)
- Location/symbol: header comment lines 13-17: *"Per Task 364's finding (tracked as Task 884, not
  fixed there or here): Bgfx's default RasterizerState cull state (`BGFX_STATE_CULL_CCW`) is the only
  one of the 3 backends that actually matches FNA's real `CullCounterClockwiseFace` default, so it
  silently culls the standard NDC quad winding used throughout this pixel-test family unless
  `RasterizerState::CullNone` is set explicitly…"*
- Evidence: this file was added by commit `0eb66f1c` ("test(Task 365): verify VertexColorEnabled=true
  multiplies vertex color, all 3 backends"), dated **2026-07-06 17:14:41**. Task 896
  ("fix(Task 896): push GraphicsDevice's real default RasterizerState to all 3 backends", commit
  `b6a00bc6`) landed **2026-07-07 19:39:24** — the very next day — and is confirmed live in the
  current source: `GraphicsDevice.cpp:196-207` unconditionally calls
  `setBlendStateProperty(blendState_); setDepthStencilStateProperty(depthStencilState_);
  setRasterizerStateProperty(rasterizerState_);` in the constructor, for every backend equally (this
  code is backend-agnostic, shared `GraphicsDevice.cpp`, not Bgfx-specific). `rasterizerState_`'s
  default-constructed value is `CullMode::CullCounterClockwiseFace`
  (`RasterizerState.cpp:11`, matching FNA). This means **EasyGL and Vulkan's own GPU cull state now
  also defaults to culling CCW-winding faces**, identically to Bgfx's own hardcoded default — Bgfx is
  no longer "the only one of the 3 backends" doing this correctly; all 3 now behave identically by
  construction.
- Why it matters: the workaround (`RasterizerState::CullNone` at line 115, confirmed correct — this
  file's own quad genuinely has CCW winding: `tl=(-1,1), bl=(-1,-1), br=(1,-1)`,
  `cross=(bl.x-tl.x)*(br.y-tl.y)-(bl.y-tl.y)*(br.x-tl.x) = 0*(-2) - (-2)*2 = 4 > 0` = CCW, per this
  project's own established convention) is *not* wrong and does not need to change. The problem is
  purely that a future maintainer reading this comment would conclude "Bgfx has a unique quirk here
  that EasyGL/Vulkan don't" and could waste time looking for a Bgfx-specific explanation, or
  mistakenly treat EasyGL/Vulkan defaults as still permissive, when in fact (per Task 896) the
  behavior is now uniform project-wide and centrally enforced in shared code. The same
  now-superseded characterization recurs verbatim in at least one other file in this same shard
  (`examples/bgfx_depthstencilstate_compare_function_test.cpp`, authored 2026-07-09 — *after* Task 896
  — whose own prior audit report, `audit/examples/bgfx_depthstencilstate_compare_function_test.cpp.audit.md`,
  independently repeats the identical "Bgfx is the only one of the 3 backends" claim as a confirmed
  fact), suggesting this is a genuinely propagating stale narrative in this shard's comments/tests
  rather than an isolated slip in this one file.
- FNA/XNA comparison: N/A — this is a CNA-internal, cross-backend-state-sync question, not an
  XNA/FNA behavioral question. FNA's own `CullCounterClockwiseFace` default (confirmed already
  matched by `RasterizerState.cpp:11`) is what all 3 backends now correctly implement.
- Related files: `examples/bgfx_depthstencilstate_compare_function_test.cpp` (same shard, similar but
  softer wording — "on Bgfx specifically" rather than an explicit "only one of 3" claim);
  `GraphicsDevice.cpp:196-207`; `RasterizerState.cpp:11`.
- Suggested future action (not implemented by this audit): reword the comment to state the cull-need
  as a property of the quad's own CCW winding under CNA's now-uniform default (shared by all 3
  backends since Task 896), rather than attributing it to a Bgfx-only quirk.

## Cross-File Observations

- Shares the exact `matches()`/`closeTo()` tolerance helper pattern and the 20-iteration
  Clear+Draw+Read retry idiom with the other `BlendState` files in this batch, though this file's
  variant (lines 75-82) additionally supports a negated match (`!matches(...)`, lines 125-128) that
  those files do not need.
- `kExpected`'s derivation is *exact* integer arithmetic (`200*0.8=160.0` precisely, no rounding
  ambiguity), unlike some of this shard's other pixel tests that must reason about GPU
  floating-point/interpolation slop — a deliberately well-chosen set of test constants.

## Missing or Weak Tests

None beyond F1 (a documentation issue, not a coverage gap) — the three-assertion design already
covers the two most likely regressions (dropped vertex-color multiply, dropped diffuse multiply)
for this specific `VertexColorEnabled=true`/no-texture code path.

## Positive Findings

- The negative assertions (`!matches(got, kDiffuseOnly)`, `!matches(got, kVertexOnly)`) are a real,
  non-trivial strengthening over a bare "matches expected" test — they specifically rule out the two
  most likely wrong implementations (vertex-color multiply silently dropped, or diffuse-color multiply
  silently dropped), and this audit independently confirmed both alternates fall well outside the
  `±8` tolerance band, so the negation is not a coincidental pass.
- Test constants are chosen so the correct product is numerically distinct from either input alone in
  every channel (documented explicitly in the file's own header, lines 19-21, and independently
  re-verified here).

## Final Assessment

A correct, well-designed three-way pixel test whose only flaw is an outdated causal claim in its own
header comment about *why* the `CullNone` workaround is needed — the underlying fix, math, and shader
cross-reference are all independently confirmed accurate against the current production code.
