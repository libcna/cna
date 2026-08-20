# AlphaTestEffect Exactness Support Matrix

Phase 43 (`plans/plan_graphics.md` Tasks 371–380) audited and pixel-verified `AlphaTestEffect`
conformance against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document
summarizes the findings and closes the phase.

---

## 1. Property/default audit (Tasks 371–372)

Task 371 audited all 8 `AlphaTestEffect` properties and `OnApply()`'s alpha-comparison logic
line-by-line against FNA's `AlphaTestEffect.cs` — **verify-only, zero bugs found**, unlike Task
361's `BasicEffect` opener. Every default matched exactly (`FogEnabled`/`VertexColorEnabled=false`,
`World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`, `Alpha=1`, `FogStart=0`,
`FogEnd=1`, `AlphaFunction=CompareFunction.Greater`, `ReferenceAlpha=0`, unclamped), the 8-case
`AlphaTest` switch statement matched FNA's literal-for-literal (including the `0.5/255` comparison
tolerance and the `eqNe` shader-index side effect), and `Clone()`'s field list matched FNA's clone
constructor. The audit also confirmed (not a bug, correctly deferred) that `FillGpuDrawParams()`
never forwards any fog fields at all — squarely Task 378's scope, not this task's.

Task 371 found **zero existing test coverage** for `AlphaTestEffect`. Task 372 wrote
`tests/Microsoft/Xna/Framework/Graphics/AlphaTestEffectTests.cpp` from scratch (mirroring Task
362's `BasicEffectTests.cpp` style): 27 tests covering all 8 property defaults, a setter round-trip
per property, `Clone()`, and `GetTypeName()`. No new bugs found — pure test-authoring.

## 2. CompareFunction pixel coverage, all 3 renderers (Tasks 373–375)

Task 373 extended Task 190's EasyGL boundary-only coverage (which only ever tested the exact
`alpha==reference` point) with a genuine 3-point sweep (`64/255` below, `128/255` at, `192/255`
above a fixed `reference=128`) across all 8 `CompareFunction` values — 24 assertions, each
function's below/at/above signature re-derived independently from FNA's `OnApply()` switch and
confirmed unique, giving the sweep real discriminating power beyond a single boundary point.
**24/24 PASS, zero bugs** — EasyGL's comparison logic was already fully correct.

Task 374 ported the identical sweep to Vulkan — the **first-ever** `AlphaTestEffect`
`CompareFunction` pixel coverage Vulkan had. 24/24 PASS, byte-for-byte same expected values.

Task 375 ported the sweep to Bgfx — again the first-ever Bgfx coverage for this. Found one
integration issue (not an `AlphaTestEffect` bug): copying Task 190/373's
`dev.SetDepthTestEnabled(false)` call verbatim crashed, since `GraphicsDevice::
SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` are unconditional-throw stubs on
Bgfx — a known, pre-existing, intentional gap (2 other existing Bgfx tests already document
skipping the same call). Fixed by removing the call (harmless here since every iteration clears
before drawing a single quad at `z=0`). Also needed the standard `RasterizerState::CullNone`
workaround (Task 884). 24/24 PASS after the fix.

## 3. Reference-alpha scaling (Task 376)

Tasks 371/373 had already established the `referenceAlpha/255.0f` scaling was correct, but neither
exercised multiple *reference* values or true out-of-range inputs directly. Task 376 added a
parameterized, GPU-independent unit test (`::testing::Values(-10, 0, 1, 64, 128, 254, 255, 300)`)
calling `FillGpuDrawParams()` directly and checking the `alphaTest` vec4 output for both of FNA's
switch-case shapes (`Greater`-style and `Equal`-style). 16 parameterized assertions, all matched
within `1e-6` tolerance, confirmed the scaling stays unclamped for out-of-range inputs (matching
FNA's own unclamped `int referenceAlpha` field). 17 new tests total, zero bugs found.

## 4. Vertex color × diffuse interaction (Task 377)

FNA's `AlphaTestEffect.fx` multiplies vertex color into `Diffuse` *before* the alpha-test `clip()`
runs — meaning `VertexColorEnabled` affects not just RGB tinting but whether the alpha test passes
at all, since the comparison runs against the fully-combined alpha, not material alpha alone.

**EasyGL: correct**, confirmed by a dedicated pixel test (`easygl_alphatest_vertexcolor_diffuse_
test.cpp`) using two `CompareFunction::Greater` reference thresholds chosen so "combined alpha
used" and "diffuse-alpha-alone" hypotheses diverge — 2/2 PASS, exact match. EasyGL reuses the same
generic per-stride shaders `BasicEffect` uses, which already carry the correct vertex-color logic.

**Vulkan and Bgfx: real bug found, `VertexColorEnabled` has zero effect, true by default — not
fixed here.** Both renderers route `AlphaTestEffect` through one generic alpha-test
pipeline/shader (`alpha_test3d.vert/frag.glsl` on Vulkan; `vs/fs_alpha_test3d.sc` on Bgfx) that
only ever declares `position`+`texcoord` vertex inputs, never a color attribute — and critically,
`AlphaTestEffect`'s own defaults already route essentially all real-world usage through this
pipeline (`AlphaFunction=Greater`, `ReferenceAlpha=0` already produces `alphaTestActive=true`).
Verified empirically with a temporary, uncommitted Vulkan port of the same test: both cases failed
with vertex color completely dropped, matching the predicted `TextureColor×DiffuseColor×Alpha`-only
formula exactly. **Opened Task 887** to unify Vulkan/Bgfx's alpha-test dispatch with their
already-correct per-stride pipelines (mirroring EasyGL's architecture) — a genuinely large,
6-shader-file, 2-renderer change, not a Task-377-sized fix.

## 5. Fog behavior (Task 378)

**Real bug found and fixed on EasyGL; a much larger, pre-existing, project-wide gap discovered and
not fixed (new Task 888).** `AlphaTestEffect::FillGpuDrawParams()` never forwarded any of
`FogEnabled`/`FogColor`/`FogStart`/`FogEnd` — fog was a total no-op regardless of settings, even
though EasyGL's shared per-stride shaders already fully implement fog generically (the same
shaders `BasicEffect` uses, and already correctly wired from `BasicEffect::FillGpuDrawParams()`).
**Fixed** by forwarding the 4 fields, mirroring `BasicEffect`'s pattern (`FogColor` is backed by an
`EffectParameter*` here, read via `getFogColorProperty()`) — no shader changes needed on EasyGL.

A 3-point Z-sweep pixel test (`easygl_alphatest_fog_test.cpp`) proved genuine interpolation, not an
on/off switch — 3/3 PASS, near-exact match. Verified genuine discriminating power via `git stash`:
2/3 assertions correctly failed pre-fix.

**Much larger finding at the time, since fixed:** grepping every `.glsl` (Vulkan) and `.sc` (Bgfx)
shader file in both renderers for "fog" found **zero matches anywhere** — fog was a total,
project-wide no-op on Vulkan and Bgfx for **every** 3D effect, including `BasicEffect`, despite
`BasicEffect::FillGpuDrawParams()` already forwarding the fields correctly on the C++ side.
**Opened Task 888** to track it; **fixed by Task 899 (closed 2026-07-07)** — fog uniforms/varyings
and the blend formula were added across every 3D shader on both renderers, including
`AlphaTestEffect`'s. See `docs/graphics-renderer-feature-matrix.md`'s "Fog, all applicable
effects/pipelines" row for current status.

## 6. Null/disabled texture behavior (Task 379)

**Real bug found and fixed on Bgfx — general, not `AlphaTestEffect`-specific.** FNA's
`AlphaTestEffect` has no `TextureEnabled` flag; every shader variant unconditionally samples
`Texture`, and FNA itself leaves a null-texture draw undefined. CNA's own established, cross-effect
convention is to fall back to a 1×1 opaque white texture. **EasyGL and Vulkan: already correct.**
**Bgfx: real bug** — all 7 texture-binding call sites in `DrawPrimitivesEx` only bound a texture
when one was present, with no fallback at all; a null-texture draw left whatever the *previous*
draw had bound (confirmed empirically: black, not the previous texture nor the correct fallback).

**Fixed** by adding a `defaultWhiteTexture3D_` and an `else` branch to all 7 call sites uniformly —
the bug affected every texture-sampling dispatch branch (`dualTexture`, `skinned`, `envMap`,
`alphaTest`, `lighting`, `textureEnabled`, `textureEnabled+vertexColorEnabled`), not just the one
`AlphaTestEffect` happens to exercise. One pixel test per renderer, 3/3 PASS on all 3 renderers,
verified discriminating via `git stash`. **Noted, not fixed**: Bgfx's second texture slot
(`texColor3DSampler2_`, used only by `DualTextureEffect`) has the identical gap, deliberately left
for whoever next touches `DualTextureEffect` in Phase 44 — `DualTextureEffect` always requires both
textures by design, unlike `AlphaTestEffect`, so the impact is much narrower.

## Support matrix

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| Property defaults (8/8) | ✅ Task 371/372 | ✅ (shared C++) | ✅ (shared C++) |
| `AlphaTest` switch, all 8 `CompareFunction` values | ✅ Task 373 | ✅ Task 374 | ✅ fixed Task 375 (depth-state stub) |
| `ReferenceAlpha` 0–255 scaling, boundary + out-of-range | ✅ Task 376 | ✅ (shared C++) | ✅ (shared C++) |
| `VertexColorEnabled` × `DiffuseColor` × alpha-test | ✅ Task 377 | ❌ Task 887 | ❌ Task 887 |
| Fog (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`) | ✅ fixed Task 378 | ✅ fixed Task 899 (2026-07-07) | ✅ fixed Task 899 (2026-07-07) |
| Null-texture fallback to opaque white | ✅ Task 379 | ✅ Task 379 | ✅ fixed Task 379 |

Legend: ✅ verified working · ❌ confirmed not implemented.

## Open, tracked follow-up work

Phase 43 opened 2 new tracked tasks:

- **Task 887** — unify Vulkan/Bgfx's alpha-test pipeline dispatch with their already-correct
  per-stride textured/colored-textured pipelines (mirroring EasyGL's architecture), so
  `VertexColorEnabled` actually affects the alpha-test comparison on those 2 renderers. A 6-shader-
  file, 2-renderer change.
- ~~**Task 888**~~ — **fixed by Task 899** (closed 2026-07-07): real fog is now implemented on
  Vulkan and Bgfx, project-wide, for every 3D effect including `AlphaTestEffect`.

This closes Phase 43 (`plans/plan_graphics.md` Tasks 371–380) in full.
