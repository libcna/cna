# DualTextureEffect Exactness Support Matrix

Phase 44 (`plans/plan_graphics.md` Tasks 381–390) audited and pixel-verified `DualTextureEffect`
conformance against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document
summarizes the findings and closes the phase.

---

## 1. Property/default audit (Tasks 381–382)

Task 381 audited all 9 `DualTextureEffect` properties and `OnApply()`'s material-color/shader-index
logic line-by-line against FNA's `DualTextureEffect.cs` — **verify-only, zero bugs found**,
mirroring Task 371's `AlphaTestEffect` opener. Every default matched exactly (`FogEnabled`/
`VertexColorEnabled=false`, `World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`,
`Alpha=1`, `FogStart=0`, `FogEnd=1`, `FogColor=Zero`, `Texture`/`Texture2=null`), the
`EffectDirtyFlags` usage, property-setter dirty-flag side effects, `OnApply()`'s
`SetWorldViewProjAndFog`/`SetFogVector` logic, and `Clone()`'s field-copy list all matched FNA
literal-for-literal. The audit found `FillGpuDrawParams()` never forwards any fog fields at all —
the identical bug shape Task 378 found and fixed for `AlphaTestEffect` — deliberately deferred to
Task 388.

Task 381 found **zero existing default-value unit test coverage**. Task 382 wrote
`tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp` from scratch (mirroring Task
372's `AlphaTestEffectTests.cpp` precedent): 26 tests covering all 9 property defaults, setter
round-trips (including `Texture`/`Texture2`), `Clone()`, and `GetTypeName()`. No bugs found — pure
test-authoring.

## 2. Basic two-texture blend formula (Tasks 383–384)

**Task 383 found and fixed a real bug on all 3 renderers.** FNA's `DualTextureEffect.fx`
`PSDualTexture` computes `color = SAMPLE(Texture); color.rgb *= 2; color *= overlay *
pin.Diffuse;` — a `*2` doubling factor on the first texture's RGB channels (alpha untouched).
CNA's EasyGL, Vulkan, and Bgfx dual-texture fragment shaders all computed
`texture0 * texture1 * diffuseColor` directly, silently missing this factor — invisible to every
prior test (Tasks 133/135/191/293/294/296/297) since they all used pure 0/1-saturated texture
values, where a missing `*2` clamps right back to the same result. **Fixed** by adding
`base.rgb *= 2.0` to all 3 renderers' dual-texture fragment shaders. Fixing it also required
updating a pre-existing test (`TextureFilter::Point vs Linear`, Task 297, shared EasyGL/Vulkan
source) whose blend-detection thresholds only worked *because of* the missing factor —
compensated with `DiffuseColor=0.5`. First-ever Bgfx `DualTextureEffect` pixel test written along
the way.

Task 384 closed Bgfx's last remaining basic-multiply coverage gap (magenta×yellow=red, matching
Tasks 133/135 on EasyGL/Vulkan) — Bgfx-only, no EasyGL/Vulkan production code touched. Found and
fixed an unrelated test-authoring mistake while writing it: a copied `SetDepthTestEnabled` call
crashes on Bgfx (Task 375's known throw-stub).

## 3. Alpha premultiplication (Task 385)

**Verify-only, zero bugs** — Task 381's audit had already confirmed
`FillGpuDrawParams()`'s `Vector4(diffuseColor*alpha, alpha)` formula matches FNA exactly. Task 385
added the missing verification layers: 3 new GPU-independent unit tests directly checking
`FillGpuDrawParams()`'s output, plus real `BlendState.AlphaBlend` pixel tests on EasyGL/Bgfx
proving the premultiplied RGB blends correctly against a real background. **At the time, Vulkan's
`BlendState` support was known-fake** (Task 868 — every Vulkan pipeline hardcoded
`colorBlendFactor=SRC_ALPHA/ONE_MINUS_SRC_ALPHA` regardless of the requested `BlendState`), which
would have made the same full RGB-premultiplication test spuriously fail on Vulkan for a reason
unrelated to `DualTextureEffect` — worked around with a narrower alpha-channel-only pixel test
there instead. **Task 868 is now fixed (2026-07-09)** — Vulkan applies the real requested blend
state like every other renderer — but the Vulkan test itself is still the narrower alpha-channel-only
variant written at the time; it has not been revisited to add the fuller RGB check now that the
workaround's reason no longer applies.

## 4. Texture null-fallback behavior (Tasks 386–387)

`DualTextureEffect` has no `TextureEnabled` flag (like `AlphaTestEffect`) — CNA's established
convention (Task 379) is to fall back to a 1×1 opaque white texture when a slot is left null.

Task 386 verified the **first** texture slot (`Texture`) — **zero bugs, already correct on all 3
renderers** (Bgfx's case was already covered by Task 379's general 7-call-site fix).

Task 387 verified the **second** slot (`Texture2`) and **found and fixed a real bug on Bgfx** —
exactly the gap Task 379 explicitly predicted and left unfixed: `texColor3DSampler2_`'s binding
had no else-branch fallback at all. EasyGL and Vulkan were already correct. **Fixed** with the same
else-branch pattern as slot 0 — a small, single-call-site fix. This closed out
`DualTextureEffect`'s texture-null coverage entirely across all 3 renderers.

## 5. Fog behavior (Task 388)

**Real bug found and fixed on EasyGL.** Task 381's audit had already found
`FillGpuDrawParams()` never forwards any fog fields — the same bug shape Task 378 fixed for
`AlphaTestEffect`. **Larger than Task 378's fix**: unlike `AlphaTestEffect` (which reuses a shared
per-stride shader that already had fog infra), `DualTextureEffect` uses its own dedicated
`EnsureDualTextured3DProgram()` shader on EasyGL, which had **no fog uniforms at all**. Fixed on
both sides: added `uFogEnabled`/`uFogColor`/`uFogStart`/`uFogEnd` uniforms plus the standard
`vFogFactor`/`mix()` blend to the shader (mirroring `EnsureTextured3DProgram()`'s pattern exactly),
then forwarded the 4 fog fields in `FillGpuDrawParams()` (mirroring `AlphaTestEffect`'s Task 378
C++ pattern). **At the time, not fixed here** (tracked as Task 888): Vulkan and Bgfx had zero fog
GPU implementation for any 3D effect. **Since fixed by Task 899** (closed 2026-07-07) — real fog is
now implemented across every 3D shader on both renderers, including `DualTextureEffect`'s.

## 6. Cross-renderer consistency (Task 389)

Closed the pixel-verification work with a capstone test combining everything Tasks 383–388
verified individually — the doubling factor, two-texture multiply, and `DiffuseColor` — to prove
the fixes compose correctly together, not just in isolation. Used, for the first time in any
`DualTextureEffect` pixel test, a **real 2×2 multi-texel texture** (every prior task used a 1×1
solid color) sampled at all 4 texel centers via 4 separate draws.

**Result: all 3 renderers produced byte-identical pixel output**, matching the FNA-derived expected
formula (`Texture × 2 × Texture2 × DiffuseColor`) at all 4 sample points. No new bugs found in the
combined assertions — but writing this test surfaced a real, previously-unaudited gap: **found and
opened Task 889** (`VertexColorEnabled` is a total no-op on all 3 renderers — every renderer's
dedicated dual-texture shader/pipeline declares only position+texcoord inputs, no color attribute
at all). Phase 44 never had a dedicated audit task for this property, unlike `AlphaTestEffect`'s
Task 377.

## Support matrix

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| Property defaults (9/9) | ✅ Task 381/382 | ✅ (shared C++) | ✅ (shared C++) |
| `color.rgb *= 2` doubling factor | ✅ fixed Task 383 | ✅ fixed Task 383 | ✅ fixed Task 383 |
| Basic two-texture multiply (magenta×yellow) | ✅ Task 133/384 | ✅ Task 135/384 | ✅ fixed Task 384 |
| `Alpha` premultiplication | ✅ Task 385 | ✅ Task 385 (alpha-channel-only) | ✅ Task 385 |
| First texture (`Texture`) null-fallback | ✅ Task 386 | ✅ Task 386 | ✅ Task 379/386 |
| Second texture (`Texture2`) null-fallback | ✅ Task 387 | ✅ Task 387 | ✅ fixed Task 387 |
| Fog (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`) | ✅ fixed Task 388 | ✅ fixed Task 899 (2026-07-07) | ✅ fixed Task 899 (2026-07-07) |
| Independent `TextureCoordinate0` / `TextureCoordinate1` | ✅ fixed SAMPLE-073 | not audited here | not audited here |
| `VertexColorEnabled` | ❌ Task 889 | ❌ Task 889 | ❌ Task 889 |
| Cross-renderer pixel consistency | ✅ Task 389 | ✅ Task 389 | ✅ Task 389 |

Legend: ✅ verified working · ❌ confirmed not implemented.

## Open, tracked follow-up work

Phase 44 opened 1 new tracked task and confirmed 2 already-open ones apply here too:

- **SAMPLE-073 follow-up (2026-08-31)** — the earlier matrix never exercised FNA's distinct
  `TEXCOORD0`/`TEXCOORD1` inputs. EasyGL incorrectly forwarded one UV to both samplers. Its stock
  declaration mapper and shaders now consume usage indices 0 and 1 independently; the real-GL
  `EasyGL_DualTextureEffect_IndependentUV` regression uses the original SoccerPitch 40-byte
  position/normal/two-UV declaration and fails black if the second sampler reuses the first UV.

- **Task 887** (opened by Task 377, `AlphaTestEffect`) — Vulkan/Bgfx alpha-test vertex-color
  unification. Architecturally related to Task 889 but a distinct effect/pipeline. Still open.
- ~~**Task 888**~~ (opened by Task 378, `AlphaTestEffect`) — **fixed by Task 899** (closed
  2026-07-07): real fog now implemented on Vulkan and Bgfx, project-wide, for every 3D effect
  including `DualTextureEffect`.
- **Task 889** (opened by Task 389) — `DualTextureEffect.VertexColorEnabled` is a total no-op on
  all 3 renderers. Needs a new vertex-color-carrying stride variant and `vin.Color` multiplication
  into the forwarded diffuse on every renderer's dual-texture shader (mirroring FNA's
  `VSDualTextureVc`/`VSDualTextureVcNoFog` shader variants) — a genuinely new multi-shader-file,
  multi-renderer feature addition, not fixed here.

This closes Phase 44 (`plans/plan_graphics.md` Tasks 381–390) in full.
