# Audit: examples/sdlgpu_mrt_test.cpp

## Metadata

- Source file: `examples/sdlgpu_mrt_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — Multiple Render Targets (MRT) proof for the SDL_GPU
  backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_MRT`,
  `cmake/Tests/SdlGpuTests.cmake:51-53`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: direct — `GraphicsDevice.SetRenderTargets(RenderTargetBinding[])`,
  `RenderTargetBinding`, `RenderTargetUsage`, `SpriteBatch.Begin(..., Effect*)` with a custom
  `ShaderEffect` (NOXNA extension).
- FNA reference: `Graphics/RenderTargetBinding.cs`, `Graphics/GraphicsDevice.cs`
  (`SetRenderTargets`).
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`SetRenderTargets` lines 1302-1328, `Clear`/`ClearDepth`/`ClearStencil` MRT propagation lines
  914-971, `RenderToTarget`'s multi-attachment build lines 809-851, `SdlGpuEffectBackend::
  GetOrCreatePipeline`'s per-`colorTargetCount` pipeline lines 4894-4972), `src/Microsoft/Xna/
  Framework/Graphics/GraphicsDevice.cpp` (`SetRenderTargets(vector<RenderTargetBinding>&)`, lines
  1881-1954).

## Purpose

Five-check test proving real SDL_GPU multi-attachment rendering, explicitly split into two
scopes: Checks A-C prove the established "stock effects stay single-target" boundary (rts[1..N-1]
are bound/cleared but never drawn to by a stock shader, since no stock shader family in this
codebase declares more than one fragment output — matches the project's own D3D11/D3D12 MRT
scope), while Checks D/E (added per the file's own "adversarial-review finding #2" comment,
lines 9-16) prove genuine simultaneous multi-attachment writes from a single draw call, using a
runtime-compiled 2-output custom `ShaderEffect` drawn via `SpriteBatch`. The file's header (lines
27-30) documents a real use-after-free finding from authoring this test with `Draw()`-local
`RenderTarget2D` instances instead of members — render targets here are members precisely to
avoid that hazard (contrast with `sdlgpu_rendertarget_lifetime_test.cpp`, which deliberately
re-introduces the local-variable case to prove the underlying fix instead — see Cross-File
Observations).

## Executive Verdict

**Healthy.** All 5 checks map onto real, independently-traced backend code paths; Checks D/E are
a genuine discriminator (verified both by reading `RenderToTarget`'s multi-attachment build and by
this audit's own analytic re-derivation of the expected pixel values), not a "didn't throw"
placebo. No correctness defect found in the file itself.

## Checklist Results

### API / XNA / FNA parity

`RenderTargetBinding(RenderTarget2D*)` (lines 172, 213) and `GraphicsDevice::SetRenderTargets(const
std::vector<RenderTargetBinding>&)` (lines 173, 182, 234, 248, 274-276, 294) match FNA's plural
`SetRenderTargets` overload. `SpriteBatch::Begin(SpriteSortMode, BlendState*, ..., Effect*)` (line
178) correctly threads a custom `ShaderEffect` (a documented NOXNA CNA extension, not part of XNA's
own `Effect` family) through the same `Begin` overload real XNA uses for a custom `Effect`.

### Behavioral correctness

- **Checks A-C** (lines 268-296): `SetRenderTargets({rt0,rt1,rt2})` + `Clear(Magenta)`, a
  `BasicEffect`+`VertexColorEnabled` quad draw, then `ClearColorAndDepth` + unbind — each wrapped in
  a single `try`/`catch` recording only "no exception," consistent with this shard's established
  convention of pairing a no-throw check with a screenshot for scope-boundary proofs where a
  dedicated pixel assertion isn't the discriminator (that role is Checks D/E's, not A-C's).
  Independently confirmed against production: `SdlGpuGraphicsBackend::Clear()` (lines 914-931)
  really does `currentRenderTarget_->QueueClear(...)` **and** loops
  `currentExtraMrtTargets_` to clear every secondary target too — a single `Clear()` call while
  MRT is bound genuinely reaches all 3 targets, matching the test's own claim.
- **Checks D/E** (`RunRealMrtCheck`, lines 170-199): binds two fresh targets
  (`rtMrtA_`/`rtMrtB_`), draws one `SpriteBatch` sprite through a runtime-compiled 2-output GLSL
  fragment shader (`kMrtFragSrc`, lines 124-140: `outColorA = texture*pc.color`,
  `outColorB = vec4(outColorA.g, outColorA.b, outColorA.r, outColorA.a)`), then reads back both
  targets' centre pixel via `RenderTarget2D::GetData()`. Independently re-derived the expected
  values by hand: `pc.color=(0.2,0.4,0.8,1.0)` (line 176) times white texture gives
  `wantA=(51,102,204,255)` (`0.2*255≈51`, `0.4*255≈102`, `0.8*255≈204`) and the channel-swap
  `(g,b,r,a)` gives `wantB=(102,204,51,255)` — both exactly match the file's own literals (lines
  189-190). Traced this through the actual backend: `SdlGpuEffectBackend::GetOrCreatePipeline`
  (lines 4894-4972) builds `colorTargetCount` real `SDL_GPUColorTargetDescription` entries (not
  hardcoded to 1) keyed by `(colorFormat, sampleCount, colorTargetCount)` (line 4906), and
  `RenderToTarget` (lines 809-851) constructs `1+mrtSiblings.size()` real `SDL_GPUColorTargetInfo`
  entries passed to **one** `SDL_BeginGPURenderPass` call (lines 817-821, 836-837) — this is a
  real multi-attachment pass, not `Clear()`-only propagation dressed up as MRT.
- Check E's `!Matches(gotB, gotA, 15)` (line 195) is the correct additional guard against a
  degenerate "both targets got the same value" false-pass — confirmed necessary in principle since
  `wantA`/`wantB` happen to share no channel value pairwise, but a regression that wrote `gotA`'s
  value into both targets would still fail the `Matches(gotB, wantB)` half of the check even
  without this guard, so it is a defense-in-depth addition rather than the sole discriminator.

### Logic

`SetRenderTargets(rts, count)` in the backend (lines 1302-1328) resets `rts[0]`'s own
`isMrtSibling`/`mrtSiblings` via `SetRenderTarget2D(rts[0])` (which itself unconditionally clears
stale MRT bookkeeping per its own comment, lines 1267-1272) before re-populating them for
`i=1..count-1` — correctly guards against a stale sibling list from an earlier MRT bind leaking
into this one, which this test's own repeated per-frame `RenderMrtAndSample`/frame-1-only
`RunRealMrtCheck` pattern (lines 263-331) would otherwise be able to expose across the 30-frame
run.

### C++ correctness

`RunRealMrtCheck` constructs a second, function-local `SpriteBatch sb(dev)` (line 177) distinct
from the member `sb_` used elsewhere in the file — deliberate, since `sb_->Begin()` is called
without an effect elsewhere in the same `Draw()` call (line 314) and `SpriteBatch` does not support
re-`Begin()` with different parameters mid-object-lifetime; using a fresh local object avoids that
constraint entirely. No lifetime issue: `sb` is fully used (Begin/Draw/End) before its own
destructor runs, well before any deferred-release concern applies.

### Memory/resource lifetime

Render targets are members (`rt0_`, `rt1_`, `rt2_`, `rtMrtA_`, `rtMrtB_`), explicitly to sidestep
the local-variable use-after-free the header comment documents finding during authoring (lines
27-30) — correctly avoids the hazard `sdlgpu_rendertarget_lifetime_test.cpp` exists specifically to
regression-test against (see Cross-File Observations).

### Testing

Genuinely two-tier: Checks A-C are scope-boundary/no-throw proofs (appropriate, since nothing in
this codebase's stock-effect shaders can produce a second output to assert on), Checks D/E are
real, independently-verified pixel assertions. This is a materially stronger test than a
single-tier "everything didn't throw" MRT smoke test.

## Cross-File Observations

- `plans/plan_sdlgpu.md`'s SDLGPU-37 row (consulted as secondary context per D-3) confirms this file's
  own narrative is not just self-reported: the same real use-after-free this header describes was
  independently git-log-documented as fixed via a `shared_ptr`-owned GPU-state redesign, and the
  plan row states Checks D/E were verified by **temporarily reverting** the render pass back to 1
  attachment (and the custom pipeline's `colorTargetCount` back to 1), which reproduced the
  predicted failure exactly (target A still correct, target B read back untouched black) before
  the fix was restored to 5/5 — i.e. this test's discriminating power for the real MRT capability
  was empirically proven, not merely asserted by its own comments. This is a materially stronger
  evidentiary basis than most test files in this shard get, and is worth citing precisely because
  a sibling file in this same batch (`sdlgpu_rendertarget2d_msaa_test.cpp`) makes an analogous
  claim about its own Checks D/E that the *same* plan document's own honesty note reveals was
  **not** actually empirically reproducible — see that file's own report.
- `GraphicsDevice::SetRenderTargets(vector<RenderTargetBinding>&)` (lines 1917-1918) mutates
  `currentRenderTargets_`/`renderTargetBound_` **before** the `backend_->SetRenderTargets(...)`
  call that could throw (line 1936) — this is the same "mutate-before-validate" shape
  `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents as a recurring pattern in this codebase
  (EasyGL's window registry, `SpriteBatch::Begin()`, and a prior SdlRenderer MRT-throws test).
  It does **not** manifest as an observable defect for this specific file, however: SdlGpu
  genuinely supports MRT (confirmed above), so `SetRenderTargets` never throws for the counts this
  test uses, and this file exercises no failure path that would expose the stale-state risk.
  Noted for completeness, not filed as a new finding (the pattern is already tracked centrally and
  `GraphicsDevice.cpp` is outside this shard's file list).
- Fog and skinned-normal-transform cross-cutting bugs (`AUDIT_CROSS_CUTTING_FINDINGS.md`) are
  **not applicable**: this file's only shading is `BasicEffect`+`VertexColorEnabled` (routes to
  `colored3d.vert/frag.glsl`, which implement no fog at all — confirmed via
  `FillColoredUniforms`'s own "no fog" doc comment, `SdlGpuGraphicsBackend.cpp` lines 295-296) and
  a hand-written custom `ShaderEffect` with no lighting/normal-transform logic whatsoever (`kMrtVertSrc`/
  `kMrtFragSrc`, lines 99-140). No `SkinnedEffect`/`EnvironmentMapEffect` code path is exercised.

## Missing or Weak Tests

- Checks A-C could not, by the file's own design (no stock shader in this codebase has a second
  output), directly assert that `rt1_`/`rt2_` receive the *shared* `Clear()` color rather than
  independently-tracked garbage — the screenshot-based verification (lines 308-318) is qualitative,
  and there is no `GetData()` pixel assertion on `rt1_`/`rt2_` themselves for the "still magenta
  after the colored3d draw" claim in the header comment (line 36). This is a reasonable scope
  choice (Checks D/E already prove the mechanism that would make a false claim here), but is worth
  noting as an easy, cheap strengthening (`ReadPixel`-style assertions on `rt1_`/`rt2_` centre pixel
  = Magenta after the draw) if this file is revisited.

## Positive Findings

- Checks D/E's expected pixel values were independently re-derived by hand from the shader source
  and the `SpriteBatch::Draw` call's own texture/tint values, and matched the file's literals
  exactly — this is not a self-consistent-but-unverified assertion.
- The two-scope design (stock-effect boundary vs. genuine custom-effect MRT) accurately reflects
  this codebase's actual capability shape (confirmed by reading `SdlGpuEffectBackend::
  GetOrCreatePipeline`'s dynamic `colorTargetCount` handling vs. every stock pipeline's hardcoded
  `num_color_targets = 1`), rather than asserting a capability boundary the test author merely
  assumed.
- Render targets correctly kept as members with an explicit rationale tied to a real, previously
  hit use-after-free — good engineering discipline, and independently corroborated by
  `SdlGpuRenderTarget2DState`'s deferred-release destructor (`SdlGpuGraphicsBackend.cpp` lines
  4244-4253).

## Final Assessment

A genuinely two-tier, well-evidenced MRT test: the scope-boundary checks (A-C) are honestly scoped
to what this codebase's stock shaders can prove, and the real-MRT checks (D/E) are backed by both
independent analytic re-derivation in this audit and the project's own documented git-stash
regression proof. No defect found; only a minor, optional strengthening noted (pixel-assert
rt1_/rt2_ untouched-by-draw claim rather than relying on the screenshot alone).
