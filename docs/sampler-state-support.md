# SamplerState / Texture Sampling Support Matrix

> **Status update, 2026-07-11:** two gaps this document treats as open below are now fixed. **Task
> 867** (`Texture2D::SetData(level>0)` silent no-op on Vulkan/Bgfx, §6) was split into Tasks
> 924 (EasyGL)/925 (Vulkan)/926 (Bgfx) and all three are closed (2026-07-09) — mip-level `SetData`
> is now real GPU upload on all 3 hardware backends. **Task 918** (EasyGL `TextureFilter::Anisotropic`
> falling back to trilinear, §7) is also fixed (2026-07-09) — EasyGL now issues a real
> `GL_EXT_texture_filter_anisotropic` call. The ❌ markers for these in §6/§7 and the summary table
> below are historical. See `docs/xna-4-api-coverage.md`'s per-class table or `NEXT.md` §5 for
> current status.

Phase 35 (`plan_graphics.md` Tasks 291–300) audited and pixel-verified `SamplerState` and texture
sampling conformance against FNA across all three graphics backends (EasyGL, Vulkan, Bgfx). This
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
`EnvironmentMapEffect`/`SkinnedEffect`), for three different, backend-specific reasons, all fixed
this session:

| Backend | Root cause | Fix |
|---|---|---|
| EasyGL / shared `GraphicsDevice` | All 18 `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` overloads never called `applySamplerStatesToBackend()` — unlike `DrawPrimitives`/`DrawIndexedPrimitives`, which do. | Added the missing call to all 18 sites. |
| Vulkan | `GetOrCreateDualTexDescSet` hardcoded `defaultSampler_` (Linear+ClampToEdge) into both descriptor image slots and cached the descriptor set keyed only by image views — the correctly-computed `slotSamplers_[0]`/`[1]` were never actually bound. | Threaded both samplers through as parameters; widened the descriptor cache key to include them (mirrors the already-correct single-texture `GetOrCreateTexSamplerDescSet(view, sampler)` pattern). |
| Bgfx | The dual-texture draw branch bound texture slot 1 using `samplerFlags_[0]` instead of `samplerFlags_[1]`, so the second texture always inherited slot 0's sampler state. | Fixed the index. |

Proven with a pixel test (`examples/easygl_sampler_state_effect_test.cpp`, registered on both
EasyGL and Vulkan as `*_SamplerState_DualTextureEffect`) that initially failed on all three
backends and now passes; Bgfx confirmed via full-suite no-regression only (no pixel-readback API
in this project).

**Practical impact**: before this fix, any XNA-ported game drawing textured 3D geometry through
the `DrawUserPrimitives` family (the normal way to use stock effects) had its assigned
`TextureAddressMode`/`TextureFilter` completely ignored — always sampling with whatever GPU state
happened to be left over from a prior draw, or a backend's hardcoded default.

## 4. `TextureAddressMode` (Tasks 294–296)

All three values verified on a 3D stock effect (`DualTextureEffect`), using a 2-texel red/green
pattern texture with UV sampled past the `[0,1]` range:

| Value | Status | Notes |
|---|---|---|
| `Clamp` | ✅ Verified (Task 294) | New test; Task 269's existing address-mode test only covers `SpriteBatch` (a different code path from Task 293's fix). |
| `Wrap` | ✅ Already covered (Task 295) | Task 293's own fix-proof test already *is* a `Wrap` pixel test on a 3D stock effect — no duplicate written. |
| `Mirror` | ✅ Verified (Task 296) | FNA has no `PointMirror` static preset, so this test builds a custom `SamplerState`. Sample point chosen deliberately (`u=1.6`, not the `u=1.25` used elsewhere) so `Mirror`'s answer can't coincidentally match `Clamp`'s. |

All three backends already correctly mapped `TextureAddressMode` to their GPU's wrap/clamp/mirror
address modes — no bugs found in the mapping itself; the only real bug in this whole area was
Task 293's binding-never-applied issue above, which these tests now guard against regressing.

## 5. `TextureFilter::Point` vs `Linear` (Task 297)

First genuinely new-ground test in Phase 35 — no prior test exercised `TextureFilter` itself. New
test draws 4 columns in one frame (magnification × {`Point`,`Linear`}, minification ×
{`Point`,`Linear`}) on `DualTextureEffect`, sampling each at a texel boundary: `Point` reads a
pure, unblended colour; `Linear` reads a genuine ~50/50 blend. All 4 checks passed on both backends
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
is a **total silent no-op** on both Vulkan and Bgfx — `ITextureBackend::UpdatePixelsLevel` has an
empty default body that neither backend overrides. This is the same severity class as the
already-tracked Task 865 (`Texture3D`/`TextureCube::GetData` no-op), but for `Texture2D` itself,
previously undocumented. Vulkan additionally hardcodes `VkImageCreateInfo::mipLevels=1`,
`VkImageViewCreateInfo::levelCount=1`, and never sets sampler `minLod`/`maxLod` (defaulting to 0,
clamping automatic LOD selection to level 0 regardless of filter) — all fixes needed together for
Vulkan `Texture2D` mips to work at all. The confounded Vulkan test variant was un-registered rather
than left misleadingly failing for the wrong reason.

## 7. Anisotropic filtering (Task 299)

| Backend | Status |
|---|---|
| Vulkan | **Correct.** Queries `VkPhysicalDeviceFeatures.samplerAnisotropy` support and the real device cap (`VkPhysicalDeviceProperties.limits.maxSamplerAnisotropy`), clamps the requested `MaxAnisotropy` to it before creating the sampler. |
| EasyGL | **No support at all.** `TextureFilter::Anisotropic` silently falls back to plain trilinear filtering — the underlying `easy-gl` library has zero anisotropy-related API. `MaxAnisotropy` has no effect, at any value. |
| Bgfx | **Partial.** Enables `BGFX_SAMPLER_MIN`/`MAG_ANISOTROPIC` flags (some anisotropic filtering does occur) but the `maxAnisotropy` parameter is unused — the requested level is never communicated, only on/off. |

A true visual anisotropic-quality pixel test (comparing detail preservation under oblique/
aspect-skewed minification) was judged too driver-dependent/fragile to assert precisely across
backends. Instead, the task's own "caps and fallback" framing was tested literally: a new test
assigns `MaxAnisotropy=9999` (far beyond any real GPU's cap) and confirms this does not crash or
throw on either backend — the load-bearing assertion.

**Additional finding while building this test**: assigning `TextureFilter::Anisotropic` to an
ordinary single-level `Texture2D` (e.g. `Texture2D::CreateFromPixels`, the common case for real
game textures) renders **solid black on EasyGL** — the exact same mipmap-incompleteness symptom as
§6, since `Anisotropic` also maps to a `_MIPMAP_`-suffixed GL filter. Vulkan does not share this
symptom (renders correctly even on a single-level texture). Confirms Task 867's root cause is
broader than just Vulkan's `Texture2D` mip pipeline — EasyGL needs a `GL_TEXTURE_MAX_LEVEL` fix
too. Task 867's tracked scope covers both.

---

## Summary: what actually works today, per backend

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
  backends, Vulkan's `mipLevels`/`levelCount`/`minLod`/`maxLod` are no longer hardcoded, and
  EasyGL's `GL_TEXTURE_MAX_LEVEL` mipmap-incompleteness black-screen bug is fixed.
- ~~EasyGL's anisotropic filtering gap~~ — **fixed, Task 918** (2026-07-09): real
  `GL_EXT_texture_filter_anisotropic` support added, gated on the extension genuinely being
  available and clamped to the live driver's `MaxTextureMaxAnisotropy` cap.
- Bgfx's anisotropic `maxAnisotropy` level still being ignored (only on/off, §7) remains open —
  not part of Task 918's EasyGL-scoped fix.
