# AlphaTestEffect Exactness Support Matrix

Phase 43 (graphics Tasks 371–380) audited and pixel-verified `AlphaTestEffect`
conformance against FNA. This document summarizes the findings and closes the phase.

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

## 2. CompareFunction pixel coverage (Tasks 373–375)

Task 373 extended Task 190's EasyGL boundary-only coverage (which only ever tested the exact
`alpha==reference` point) with a genuine 3-point sweep (`64/255` below, `128/255` at, `192/255`
above a fixed `reference=128`) across all 8 `CompareFunction` values — 24 assertions, each
function's below/at/above signature re-derived independently from FNA's `OnApply()` switch and
confirmed unique, giving the sweep real discriminating power beyond a single boundary point.
**24/24 PASS, zero bugs** — EasyGL's comparison logic was already fully correct.

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

## 5. Fog behavior (Task 378)

**Real bug found and fixed on EasyGL.** `AlphaTestEffect::FillGpuDrawParams()` never forwarded any of
`FogEnabled`/`FogColor`/`FogStart`/`FogEnd` — fog was a total no-op regardless of settings, even
though EasyGL's shared per-stride shaders already fully implement fog generically (the same
shaders `BasicEffect` uses, and already correctly wired from `BasicEffect::FillGpuDrawParams()`).
**Fixed** by forwarding the 4 fields, mirroring `BasicEffect`'s pattern (`FogColor` is backed by an
`EffectParameter*` here, read via `getFogColorProperty()`) — no shader changes needed on EasyGL.

A 3-point Z-sweep pixel test (`easygl_alphatest_fog_test.cpp`) proved genuine interpolation, not an
on/off switch — 3/3 PASS, near-exact match. Verified genuine discriminating power via `git stash`:
2/3 assertions correctly failed pre-fix.

## 6. Null/disabled texture behavior (Task 379)

FNA's `AlphaTestEffect` has no `TextureEnabled` flag; every shader variant unconditionally samples
`Texture`. Task 379 treated the null result as undefined and invented opaque white. SOFTWARE-303
instead measured Microsoft XNA 4.0: the sampler returns opaque black. **EasyGL and Software now
match that result.**

## Support matrix

| Feature | EasyGL |
|---|---|
| Property defaults (8/8) | ✅ Task 371/372 |
| `AlphaTest` switch, all 8 `CompareFunction` values | ✅ Task 373 |
| `ReferenceAlpha` 0–255 scaling, boundary + out-of-range | ✅ Task 376 |
| `VertexColorEnabled` × `DiffuseColor` × alpha-test | ✅ Task 377 |
| Fog (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`) | ✅ fixed Task 378 |
| Null texture samples opaque black | ✅ SOFTWARE-303 |

Legend: ✅ verified working.

This closes Phase 43 (graphics Tasks 371–380) in full.
