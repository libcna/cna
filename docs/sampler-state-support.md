# SamplerState / Texture Sampling Support Matrix

> **Status update, 2026-07-11:** two gaps this document treats as open below are now fixed. **Task
> 867** (`Texture2D::SetData(level>0)` silent no-op on Vulkan/Bgfx, §6) was split into Tasks
> 924 (EasyGL)/925 (Vulkan)/926 (Bgfx) and all three are closed (2026-07-09) — mip-level `SetData`
> is now real GPU upload on all 3 hardware renderers. **Task 918** (EasyGL `TextureFilter::Anisotropic`
> falling back to trilinear, §7) is also fixed (2026-07-09) — EasyGL now issues a real
> `GL_EXT_texture_filter_anisotropic` call. The ❌ markers for these in §6/§7 and the summary table
> below are historical. See `docs/xna-4-api-coverage.md`'s per-class table or `NEXT.md` §5 for
> current status.

Phase 35 (`plans/plan_graphics.md` Tasks 291–300) audited and pixel-verified `SamplerState` and texture
sampling conformance against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This
document summarizes the findings.

---

## 1. `SamplerState` API surface (Task 291)

The 7-property surface, all 6 static presets (`AnisotropicClamp`/`Wrap`, `LinearClamp`/`Wrap`,
`PointClamp`/`Wrap` — FNA has no `Default`/"PointMirror"/etc. presets beyond these 6), the
default-constructor values (`Filter=Linear`, `AddressU=AddressV=AddressW=Wrap`,
`MaxAnisotropy=4`, `MaxMipLevel=0`, `MipMapLevelOfDetailBias=0`), and every preset's filter/address
values already matched FNA exactly.

One real, fixed finding: FNA's private preset constructor sets `Name` on every preset (e.g.
`"SamplerState.PointClamp"`); CNA's didn't. Fixed. The identical gap exists in
`BlendState`/`DepthStencilState`/`RasterizerState` — tracked separately as Task 866, not yet fixed.

## 2. Default sampler states (Task 292)

FNA's `SamplerStateCollection` defaults every one of its 16 slots to `SamplerState.LinearWrap`.
CNA's `SamplerStateCollection` previously default-constructed each slot instead of copying the real
`LinearWrap` preset — functionally identical filter/address values (both are `Linear`+`Wrap`×3),
which is exactly why this went undetected until `SamplerState.Name` existed to distinguish them
(Task 291). Fixed; `GraphicsDevice.SamplerStates`/`VertexSamplerStates` now correctly default every
slot, confirmed via a new `SamplerStateCollectionTests.cpp`.

## 3. Per-slot sampler binding — the central Phase 35 finding (Task 293)

**`GraphicsDevice.SamplerStates`/`VertexSamplerStates` was silently ignored by essentially all 3D
stock-effect texture draws** (`BasicEffect`/`DualTextureEffect`/`AlphaTestEffect`/
`EnvironmentMapEffect`/`SkinnedEffect`), for three different, renderer-specific reasons, all fixed
this session:

| Renderer | Root cause | Fix |
|---|---|---|
| EasyGL / shared `GraphicsDevice` | All 18 `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` overloads never called `applySamplerStatesToRenderer()` — unlike `DrawPrimitives`/`DrawIndexedPrimitives`, which do. | Added the missing call to all 18 sites. |
| Vulkan | `GetOrCreateDualTexDescSet` hardcoded `defaultSampler_` (Linear+ClampToEdge) into both descriptor image slots and cached the descriptor set keyed only by image views — the correctly-computed `slotSamplers_[0]`/`[1]` were never actually bound. | Threaded both samplers through as parameters; widened the descriptor cache key to include them (mirrors the already-correct single-texture `GetOrCreateTexSamplerDescSet(view, sampler)` pattern). |
| Bgfx | The dual-texture draw branch bound texture slot 1 using `samplerFlags_[0]` instead of `samplerFlags_[1]`, so the second texture always inherited slot 0's sampler state. | Fixed the index. |

Proven with a pixel test (`modules/renderers/easygl/examples/easygl_sampler_state_effect_test.cpp`, registered on both
EasyGL and Vulkan as `*_SamplerState_DualTextureEffect`) that initially failed on all three
renderers and now passes; Bgfx confirmed via full-suite no-regression only (no pixel-readback API
in this project).

**Practical impact**: before this fix, any XNA-ported game drawing textured 3D geometry through
the `DrawUserPrimitives` family (the normal way to use stock effects) had its assigned
`TextureAddressMode`/`TextureFilter` completely ignored — always sampling with whatever GPU state
happened to be left over from a prior draw, or a renderer's hardcoded default.

## 4. `TextureAddressMode` (Tasks 294–296)

All three values verified on a 3D stock effect (`DualTextureEffect`), using a 2-texel red/green
pattern texture with UV sampled past the `[0,1]` range:

| Value | Status | Notes |
|---|---|---|
| `Clamp` | ✅ Verified (Task 294) | New test; Task 269's existing address-mode test only covers `SpriteBatch` (a different code path from Task 293's fix). |
| `Wrap` | ✅ Already covered (Task 295) | Task 293's own fix-proof test already *is* a `Wrap` pixel test on a 3D stock effect — no duplicate written. |
| `Mirror` | ✅ Verified (Task 296) | FNA has no `PointMirror` static preset, so this test builds a custom `SamplerState`. Sample point chosen deliberately (`u=1.6`, not the `u=1.25` used elsewhere) so `Mirror`'s answer can't coincidentally match `Clamp`'s. |

All three renderers already correctly mapped `TextureAddressMode` to their GPU's wrap/clamp/mirror
address modes — no bugs found in the mapping itself; the only real bug in this whole area was
Task 293's binding-never-applied issue above, which these tests now guard against regressing.

## 5. `TextureFilter::Point` vs `Linear` (Task 297)

First genuinely new-ground test in Phase 35 — no prior test exercised `TextureFilter` itself. New
test draws 4 columns in one frame (magnification × {`Point`,`Linear`}, minification ×
{`Point`,`Linear`}) on `DualTextureEffect`, sampling each at a texel boundary: `Point` reads a
pure, unblended colour; `Linear` reads a genuine ~50/50 blend. All 4 checks passed on both renderers
on the first run (blend values landed at 127–128, exactly as predicted).

CNA maps XNA's single `TextureFilter` value to one GL/Vulkan min+mag filter pair (matching FNA's
flat enum, which has no separate min/mag control), and generates no mipmaps by default, so
magnification and minification exercise *identical* underlying sampler math in this project — the
two scales in this test confirm robustness at scale, not a functionally distinct GPU code path.

## 6. Mipmap filter behavior (Task 298) — second severe finding

**EasyGL: verified correct for explicit `Mip*` filters.** A test with a real 8-level mipmapped
`Texture2D` (level colours: 0–2 Red, 3–7 Green) drawn at an 8×8px on-screen size (forcing GPU
automatic LOD ≈4) confirmed `TextureFilter::LinearMipPoint` correctly selects a high mip level
(Green). `TextureFilter::Point`/`Linear` always sample level 0 (Red) regardless of minification —
a **real, confirmed, deliberate deviation from FNA semantics** (XNA's `Point`/`Linear` are supposed
to be mip-aware too): CNA doesn't set `GL_TEXTURE_MAX_LEVEL` to match each texture's real level
count, so using a mip-aware GL filter unconditionally for the common non-mipmapped (single-level)
texture case would render it GL-incomplete (typically solid black). Documented as an accepted
tradeoff, tracked as part of Task 867's scope.

**Vulkan: found and tracked a severe, separate bug (Task 867).** `Texture2D::SetData(level>0,...)`
is a **total silent no-op** on both Vulkan and Bgfx — `ITextureRenderer::UpdatePixelsLevel` has an
empty default body that neither renderer overrides. This is the same severity class as the
already-tracked Task 865 (`Texture3D`/`TextureCube::GetData` no-op), but for `Texture2D` itself,
previously undocumented. Vulkan additionally hardcodes `VkImageCreateInfo::mipLevels=1`,
`VkImageViewCreateInfo::levelCount=1`, and never sets sampler `minLod`/`maxLod` (defaulting to 0,
clamping automatic LOD selection to level 0 regardless of filter) — all fixes needed together for
Vulkan `Texture2D` mips to work at all. The confounded Vulkan test variant was un-registered rather
than left misleadingly failing for the wrong reason.

## 6b. `MaxMipLevel` and `MipMapLevelOfDetailBias` (plans/plan_fx.md FX-083, 2026-08-17)

`IGraphicsRenderer::ApplySamplerMipState(slot, maxMipLevel, lodBias)` carries XNA's two LOD states
across the renderer-neutral boundary. It has a default no-op body, so a renderer that has not
adopted it accepts the state, publishes it on `GraphicsDevice.SamplerStates[slot]`, and does not
change what the GPU samples. That was true of **every** renderer except FNA3D until FX-083.

| Renderer | `MaxMipLevel` | `MipMapLevelOfDetailBias` |
|---|---|---|
| FNA3D | `GL_TEXTURE_BASE_LEVEL` / SDL_GPU `min_lod`, whichever driver is active | desktop GL only; FNA3D's own GL driver skips it under ES |
| SDL_GPU | **implemented** — `SDL_GPUSamplerCreateInfo::min_lod`, and part of the sampler cache key | **implemented** — `mip_lod_bias` |
| EasyGL, ES 3 profiles (`OPENGLES3`, `WEBGL2`) | **implemented** — sampler-object `GL_TEXTURE_MIN_LOD` | **not representable**: OpenGL ES has no `GL_TEXTURE_LOD_BIAS` at all |
| EasyGL, `OPENGL33` | **implemented** — `GL_TEXTURE_MIN_LOD` | **implemented** — `GL_TEXTURE_LOD_BIAS` |
| EasyGL, ES 2 profiles (`OPENGLES2`, `WEBGL1`) | **not representable**: no sampler objects and no `GL_TEXTURE_MIN_LOD` | not representable |
| EasyGL `AddressW` | **implemented on the ES 3 and desktop profiles** since FX-092 — sampler-object `GL_TEXTURE_WRAP_R`; not representable on the ES 2 profiles, which have neither sampler objects nor volume textures | — |
| every other renderer | default no-op | default no-op |

Why `GL_TEXTURE_MIN_LOD` rather than FNA3D's `GL_TEXTURE_BASE_LEVEL`: base level is texture-object
state, and CNA's contract is per **slot**. Two slots sampling one texture with different
`MaxMipLevel` values would fight over a single base level, whereas `min_lod` on a sampler object is
exactly "never resolve a level more detailed than this" and is per slot. It is also the mapping
FNA3D's own SDL_GPU driver makes (`samplerCreateInfo.min_lod = samplerState->maxMipLevel`).

**Known remaining gap.** On SDL_GPU the two states reach the GPU through the **compiled-effect**
draw route. The stock 3D draw families capture only filter/addressing/anisotropy into their own
deferred command structs, so a game assigning `GraphicsDevice.SamplerStates[0].MaxMipLevel` and
then drawing with `BasicEffect` still gets `min_lod = 0`. Closing that means adding the two fields
to each family's command struct; it is a stock-draw sampler task, not a compiled-effect one.

### 6b.1 Sampler identity and state lifetime (plans/plan_fx.md FX-091, FX-092, 2026-08-18)

Two follow-up findings, both about the same thing: what a renderer's sampler is a function OF.

**SDL_GPU's sampler cache key was lossy.** It packed the description into a `uint64` with the
32-bit LOD bias shifted to bit 40, so an IEEE-754 float's sign and seven of its eight exponent bits
fell off the end. `0.0`, `+/-0.5`, `+/-2.0` and `+/-8.0` produced one key; `+/-1.0`, `+/-4.0` and
`0.25` produced another; each family was served the first native sampler ever built for it.
`MaxMipLevel` was masked to eight bits on top of that. It is now a struct with member-wise equality
over filter, all three address modes, anisotropy, `MaxMipLevel` and the bias's exact bit pattern.
The lesson generalises: a cache key assembled by bit-shifting is a silent-corruption hazard, because
losing a field costs nothing at the point where it happens and everything at the point where it is
read.

**A mutated sampler object keeps what nobody rewrote.** EasyGL keeps one long-lived GL sampler per
slot, and FNA3D keeps one `FNA3D_SamplerState` per slot; both were written by
`ApplySamplerState`-shaped calls that describe only filter and addressing. So `MaxMipLevel` and the
LOD bias, once written by an Effect's own `sampler_state` block, survived into every later draw on
that slot -- including a stock `SpriteBatch` flush, which in XNA assigns its whole `SamplerState` to
slot zero and therefore resets them. Both renderers now establish the complete sampler state on
every application rather than the subset the call names. Any renderer that keeps mutable per-slot
sampler state has this hazard; a renderer that builds a fresh descriptor per draw (Vulkan, WebGPU)
cannot.

EasyGL also adopted `ApplySamplerAddressW` in the same pass, so its `GL_TEXTURE_WRAP_R` row above is
no longer "not adopted".

## 7. Anisotropic filtering (Task 299, EasyGL row updated 2026-07-11 per Task 918)

| Renderer | Status |
|---|---|
| Vulkan | **Correct.** Queries `VkPhysicalDeviceFeatures.samplerAnisotropy` support and the real device cap (`VkPhysicalDeviceProperties.limits.maxSamplerAnisotropy`), clamps the requested `MaxAnisotropy` to it before creating the sampler. |
| EasyGL | **Fixed, Task 918 (2026-07-09).** At the time this section was written, `TextureFilter::Anisotropic` silently fell back to plain trilinear filtering — the underlying `easy-gl` library had zero anisotropy-related API and `MaxAnisotropy` had no effect at any value. Now real: gated on `GL_EXT_texture_filter_anisotropic` being available, reads the live driver cap, and clamps the requested `MaxAnisotropy` to it before applying, matching Vulkan's own pattern above. |
| Bgfx | **Partial.** Enables `BGFX_SAMPLER_MIN`/`MAG_ANISOTROPIC` flags (some anisotropic filtering does occur) but the `maxAnisotropy` parameter is unused — the requested level is never communicated, only on/off. |

A true visual anisotropic-quality pixel test (comparing detail preservation under oblique/
aspect-skewed minification) was judged too driver-dependent/fragile to assert precisely across
renderers. Instead, the task's own "caps and fallback" framing was tested literally: a new test
assigns `MaxAnisotropy=9999` (far beyond any real GPU's cap) and confirms this does not crash or
throw on either renderer — the load-bearing assertion.

**Additional finding while building this test**: at the time, assigning `TextureFilter::Anisotropic`
to an ordinary single-level `Texture2D` (e.g. `Texture2D::CreateFromPixels`, the common case for
real game textures) rendered **solid black on EasyGL** — the exact same mipmap-incompleteness
symptom as §6, since `Anisotropic` also maps to a `_MIPMAP_`-suffixed GL filter. Vulkan did not
share this symptom (rendered correctly even on a single-level texture). This confirmed Task 867's
root cause was broader than just Vulkan's `Texture2D` mip pipeline — EasyGL needed a
`GL_TEXTURE_MAX_LEVEL` fix too. **Fixed, Task 924 (2026-07-09)** — `GL_TEXTURE_MAX_LEVEL` is now
clamped to each texture's real level count, so a single-level texture no longer reads as
GL-incomplete under `Anisotropic`/`Mip*` filters.

---

## Summary: what actually works today, per renderer

**Updated 2026-07-11** — Vulkan/Bgfx mip `SetData` and EasyGL anisotropic filtering rows reflect
Tasks 924/925/926 and 918 (see the status banner at the top of this document).

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| `SamplerState` API/presets/`Name` | ✅ | ✅ | ✅ |
| Default `SamplerStates`/`VertexSamplerStates` (`LinearWrap`) | ✅ | ✅ | ✅ |
| Per-slot sampler actually applied on 3D stock-effect draws | ✅ (fixed) | ✅ (fixed) | ✅ (fixed) |
| `TextureAddressMode::Clamp`/`Wrap`/`Mirror` | ✅ | ✅ | 🔍 not pixel-verified (no readback API) |
| `TextureFilter::Point` vs `Linear` | ✅ | ✅ | 🔍 not pixel-verified |
| Mip-level `SetData(level>0)` uploads real GPU data | ✅ (fixed, Task 924) | ✅ (fixed, Task 925) | ✅ (fixed, Task 926) |
| Mip-aware filters (`LinearMipPoint` etc.) select the correct mip | ✅ (mipmapped textures only) | ✅ (Task 925 fixed the underlying mip upload) | ✅ (Task 926) |
| `Point`/`Linear`/`Mip*` filters on a **non-mipmapped** texture | ✅ (fixed, Task 924 — `GL_TEXTURE_MAX_LEVEL` now clamped to the real level count, so a single-level texture no longer reads as GL-incomplete under a mip-aware filter) | ✅ no incompleteness issue | 🔍 unconfirmed |
| `TextureFilter::Anisotropic` — level respected | ✅ (fixed, Task 918 — real `GL_EXT_texture_filter_anisotropic`, clamped to driver cap) | ✅ correct, capped to device limit | ⚠️ enabled but level ignored |
| Extreme/over-cap `MaxAnisotropy` doesn't crash | ✅ | ✅ | 🔍 not tested (no crash observed elsewhere) |

Legend: ✅ verified working · ❌ confirmed broken/absent · ⚠️ partial/inconsistent ·
🔍 not empirically verified this phase (Bgfx has no GPU pixel-readback API in this project, so its
sampler-related coverage is smoke-test/no-regression only by design).

## Open, tracked follow-up work

- **Task 866** — `BlendState`/`DepthStencilState`/`RasterizerState` static presets don't set
  `Name` (same gap `SamplerState` had before Task 291).
- ~~**Task 867**~~ — **fixed**, split into Tasks 924 (EasyGL)/925 (Vulkan)/926 (Bgfx), all closed
  2026-07-09: `Texture2D::SetData(level>0,...)` now does a real GPU upload on all 3 hardware
  renderers, Vulkan's `mipLevels`/`levelCount`/`minLod`/`maxLod` are no longer hardcoded, and
  EasyGL's `GL_TEXTURE_MAX_LEVEL` mipmap-incompleteness black-screen bug is fixed.
- ~~EasyGL's anisotropic filtering gap~~ — **fixed, Task 918** (2026-07-09): real
  `GL_EXT_texture_filter_anisotropic` support added, gated on the extension genuinely being
  available and clamped to the live driver's `MaxTextureMaxAnisotropy` cap.
- Bgfx's anisotropic `maxAnisotropy` level still being ignored (only on/off, §7) remains open —
  not part of Task 918's EasyGL-scoped fix.
