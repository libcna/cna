# SamplerState / texture-sampling support matrix

`SamplerState` and texture-sampling conformance against FNA and, for `SpriteBatch` publication,
measured Microsoft XNA 4.0. Profile- and route-specific limitations are stated in §6b and §8.

---

## 1. `SamplerState` API surface

The 7-property surface, all 6 static presets (`AnisotropicClamp`/`Wrap`, `LinearClamp`/`Wrap`,
`PointClamp`/`Wrap` — FNA has no `Default`/"PointMirror"/etc. presets beyond these 6), the
default-constructor values (`Filter=Linear`, `AddressU=AddressV=AddressW=Wrap`,
`MaxAnisotropy=4`, `MaxMipLevel=0`, `MipMapLevelOfDetailBias=0`), and every preset's filter/address
values match FNA exactly. Every preset carries its FNA/XNA `Name` (e.g.
`"SamplerState.PointClamp"`), as do the presets of `BlendState`, `DepthStencilState` and
`RasterizerState`.

## 2. Default sampler states

FNA's `SamplerStateCollection` defaults every one of its 16 slots to the `SamplerState.LinearWrap`
preset itself (observable through `Name`), not to a default-constructed state with equal values.
`GraphicsDevice.SamplerStates`/`VertexSamplerStates` do the same (`SamplerStateCollectionTests.cpp`).

## 3. Per-slot sampler binding

Every 3D draw entry point applies `GraphicsDevice.SamplerStates`/`VertexSamplerStates` to the
renderer: `DrawPrimitives`, `DrawIndexedPrimitives` and all 18
`DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` overloads call
`applySamplerStatesToRenderer()`, so stock-effect texture draws (`BasicEffect`/`DualTextureEffect`/
`AlphaTestEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) honor the assigned
`TextureAddressMode`/`TextureFilter` rather than leftover GPU state. Pixel test:
`modules/renderers/easygl/examples/easygl_sampler_state_effect_test.cpp`
(`EasyGL_SamplerState_DualTextureEffect`). `SpriteBatch` frames are covered by §8.

## 4. `TextureAddressMode`

All three values verified on a 3D stock effect (`DualTextureEffect`), using a 2-texel red/green
pattern texture with UV sampled past the `[0,1]` range:

| Value | Status | Notes |
|---|---|---|
| `Clamp` | ✅ | `EasyGL_TextureAddressMode_Clamp_DualTextureEffect`; the separate `SpriteBatch` address-mode test covers the 2D path. |
| `Wrap` | ✅ | `EasyGL_SamplerState_DualTextureEffect` (§3) is a `Wrap` pixel test on a 3D stock effect. |
| `Mirror` | ✅ | `EasyGL_TextureAddressMode_Mirror_DualTextureEffect`. FNA has no `PointMirror` static preset, so the test builds a custom `SamplerState`; it samples at `u=1.6` (not `u=1.25`) so `Mirror`'s answer cannot coincidentally match `Clamp`'s. |

## 5. `TextureFilter::Point` vs `Linear`

The test draws 4 columns in one frame (magnification × {`Point`,`Linear`}, minification ×
{`Point`,`Linear`}) on `DualTextureEffect`, sampling each at a texel boundary: `Point` reads a
pure, unblended colour; `Linear` reads a ~50/50 blend (127–128).

CNA maps XNA's single `TextureFilter` value to one GL min+mag filter pair (matching FNA's flat
enum, which has no separate min/mag control), and the test texture has no mipmaps, so the two
scales exercise the same sampler math rather than distinct GPU code paths.

## 6. Mipmap filter behavior

XNA/FNA's flat nine-value `TextureFilter` table defines a minification, magnification **and mip**
component for every value: `Point` maps to point/point/point and `Linear` to linear/linear/linear.
They are full mip-aware filters, not aliases for “disable mipmapping”; all nine EasyGL ordinals use
their FNA min/mag/mip triples (REMED-GFX-175).

EasyGL uploads nonzero mip levels (`SetData(level>0)`) and clamps `GL_TEXTURE_MAX_LEVEL` to each
texture's real level count, so a single-level texture is GL-complete under `Anisotropic`/`Mip*`
filters. The shared `TextureFilterMipContract` guards every ordinal, full and partial mip chains,
and the single-level case across the rasterizing renderer families.

## 6b. `MaxMipLevel` and `MipMapLevelOfDetailBias` (FX-083)

`IGraphicsRenderer::ApplySamplerMipState(slot, maxMipLevel, lodBias)` carries XNA's two LOD states
across the renderer-neutral boundary; every rasterizing renderer overrides its default no-op body,
and likewise `ApplySamplerAddressW`. `HEADLESS` and `STUB` deliberately have no sampling result on
which these states could be observed.

| Renderer | `MaxMipLevel` | `MipMapLevelOfDetailBias` | `AddressW` |
|---|---|---|---|
| FNA3D | `GL_TEXTURE_BASE_LEVEL` / SDLGPU `min_lod`, whichever driver is active | desktop GL only; FNA3D's own GL driver skips it under ES | implemented |
| SDLGPU | **implemented** — `SDL_GPUSamplerCreateInfo::min_lod`, and part of the sampler cache key | **implemented on every ordinary stock/SpriteBatch and compiled-XNA-effect route** — SDLGPU-121/122 use SPIR-V's Bias operand because SDL documents native `mip_lod_bias` as a Metal no-op; Vulkan and D3D12 run the same shader emulation so the proof cannot pass through their working descriptor field | `SDL_GPUSamplerCreateInfo::address_mode_w` |
| EasyGL, ES 3 profiles (`OPENGLES3`, `WEBGL2`) | **implemented** — sampler-object `GL_TEXTURE_MIN_LOD` | **not representable for implicit sampling**: OpenGL ES has no `GL_TEXTURE_LOD_BIAS`; compiled vertex `TEXLDL` is nevertheless implemented by shader emulation (`FX-146`) | sampler-object `GL_TEXTURE_WRAP_R` |
| EasyGL, `OPENGL33` | **implemented** — `GL_TEXTURE_MIN_LOD` | **implemented** — `GL_TEXTURE_LOD_BIAS` | sampler-object `GL_TEXTURE_WRAP_R` |
| WebGPU | **implemented** (`WEBGPU-161`) — `WGPUSamplerDescriptor::lodMinClamp`, and part of the sampler cache key | **implemented** — by WGSL `textureSampleBias` on every stock 3D route (`WEBGPU-205`) and, by rewriting the compiled shader's SPIR-V, on compiled XNA Effects too (`WEBGPU-208`); `WGPUSamplerDescriptor` has no `lodBias` field, so both are shader emulations | `WGPUSamplerDescriptor::addressModeW` (`WEBGPU-160`) |
| Software | **implemented** in the CPU sampler state | **implemented** in the CPU sampler state | implemented in the CPU sampler state |
| DirectX9 | native `D3DSAMP_MAXMIPLEVEL` | native `D3DSAMP_MIPMAPLODBIAS` | native `D3DSAMP_ADDRESSW` |
| `HEADLESS`, `STUB` | no raster sampling | no raster sampling | no raster sampling |

**EasyGL compiled vertex sampling is the narrow ES exception (FX-146).** A `vs_3_0 TEXLDL`
already supplies an explicit level in the coordinate's W component, so the GLSL
ES 3 translation adds the selected one of four XNA vertex-sampler biases to that value. MojoShader's
OpenGL adapter uploads the four values through a program uniform immediately before the draw.
Desktop GLSL does not declare that uniform and continues to use `GL_TEXTURE_LOD_BIAS`, preventing a
double application. This does not claim a general ES sampler-state solution: implicit pixel/stock
sampling still has no equivalent state in EasyGL. The shared vertex-sampler transition test covers
zero, +1, `MaxMipLevel=1`, reset, null and disposal on OPENGLES3.

### WebGPU's LOD bias is a SHADER emulation, and its boundary is named

`WGPUSamplerDescriptor` carries `addressModeU/V/W`, `magFilter`, `minFilter`, `mipmapFilter`,
`lodMinClamp`, `lodMaxClamp`, `compare` and `maxAnisotropy` — and nothing else. An absent *state
field* is not an absent *capability* (`WEBGPU-205`): WGSL's `textureSampleBias(t, s, coords, bias)`
applies exactly XNA's semantic — a bias added to the computed level of detail — in the fragment
stage, where all of that renderer's sampling calls sit. The value travels in the per-draw uniform
block.

Two things a caller needs to know, and both are reported through
`GetAdditionalLimitationsTextEXT()` rather than left to be discovered from pixels:

* **WGSL clamps a sample bias to roughly [-16, +16)**, so a larger magnitude saturates rather than
  extrapolating. CNA clamps to that range on the way in instead of leaving the edges
  implementation-defined.
* **Two routes do not apply it**: a custom CNAEXT `ShaderEffect`, which supplies its own WGSL and
  therefore its own sampling calls; and the metallic-roughness `PbrEffect`/`SkinnedPbrEffect`
  families, which are the glTF route rather than
  an XNA stock effect.

**Compiled XNA Effects apply it too (`WEBGPU-208`).** A compiled pass's sampling call comes from
MojoShader, not from a shader CNA writes, so the emulation moves one level down:
`MojoShaderEffect::InjectSamplerLodBias` rewrites the normalized SPIR-V so each implicit sample
carries a `Bias` image operand fed from **its own D3D9 sampler register's** slot in a small
renderer-owned uniform block. Because the rewrite happens before the native and browser routes
diverge, both targets execute the same semantic -- natively naga lowers the SPIR-V operand, and in a
browser `SpirvToWgsl` renders it as `textureSampleBias` for Tint. The shared compiled-effect sampler
pixel contract runs on both routes with `supportsLodBias` true.

**`SpriteBatch` carries the complete state (SDLGPU-64).** The common
`ISpriteBatchRenderer::SetSamplerState` hook carries filter, U/V/W addressing, anisotropy,
`MaxMipLevel` and `MipMapLevelOfDetailBias`; the batch's state also reaches
`GraphicsDevice.SamplerStates[0]` as §9 describes. EasyGL, SDL GPU, WebGPU and FNA3D
(CNA-DEMOS-ISSUE-006) consume the complete hook. `RendererFeature::SamplerLevelOfDetailBias`
reports whether a renderer applies the bias at all, and `CrossRendererContractTest` checks that
answer against the mip level SpriteBatch actually samples. The shared `parity_sprite_sampler_state`
oracle requires bias +1 and -2 to select deliberately different mip colours, checks every retained
device property, and is byte-identical between EasyGL and WebGPU.

Note this makes WebGPU's coverage *broader* than the reference renderer's, which implements the bias
only on desktop core (`GL_TEXTURE_LOD_BIAS` does not exist in OpenGL ES at all). The shared parity
fixture `parity_sampler_lod_bias` therefore compares against the `OPENGL33` profile.

Why `GL_TEXTURE_MIN_LOD` rather than FNA3D's `GL_TEXTURE_BASE_LEVEL`: base level is texture-object
state, and CNA's contract is per **slot**. Two slots sampling one texture with different
`MaxMipLevel` values would fight over a single base level, whereas `min_lod` on a sampler object is
exactly "never resolve a level more detailed than this" and is per slot. It is also the mapping
FNA3D's own SDLGPU driver makes (`samplerCreateInfo.min_lod = samplerState->maxMipLevel`).

**SDL GPU stock routes (SDLGPU-121).** Every stock deferred command captures all seven sampler
properties per slot. `MaxMipLevel`, filter, addressing and anisotropy remain native sampler state.
The bias is deliberately different: SDL's vendored `SDL_gpu.h` states that
`SDL_GPUSamplerCreateInfo::mip_lod_bias` is a no-op on Metal and must be applied in the shader.
Every stock 2D/cube sample uses SPIR-V's Bias operand supplied by an eight-slot per-draw fragment
uniform, while its native sampler bias is zero to prevent a double application on Vulkan/D3D12. The
exact mip-colour fixture runs on Vulkan and cross-compiled D3D12, and the SpriteBatch variant
verifies its independent state path. AddressW reaches `SDL_GPUSamplerCreateInfo::address_mode_w`;
its volume-pixel proof remains coupled to the separate Texture3D/compiled-effect volume work tracked
by `SDLGPU-71`/`SDLGPU-79`.

**SDL GPU compiled XNA Effects (SDLGPU-122).** The pinned MojoShader adapter exposes one bounded
callback after SPIR-V linking and before native shader creation. CNA uses it to apply the shared
`InjectSamplerLodBias` rewrite directly to MojoShader's combined samplers, placing one vec4 per D3D9
sampler register in SDLGPU fragment-uniform set 3/binding 1. Replay snapshots and pushes those
values at fragment UBO slot 1 and deliberately zeros the corresponding native sampler bias,
preventing double application on Vulkan/D3D12. On Apple, the statically linked ShaderCross
configuration consumes the same transformed SPIR-V and translates it to Metal's native shader format
rather than bypassing the transform through MojoShader's direct MSL profile. The shared authored-mip
pixel contract runs on Vulkan and headless D3D12 while a test hook rejects every nonzero native
bias; a structural corpus proves each combined and split sample reads its own register. Real Metal
execution is platform-validation work, not an unimplemented sampler semantic.

### 6b.1 Sampler identity and state lifetime (FX-091, FX-092)

**A sampler cache key compares every field exactly.** SDLGPU's sampler cache key is a struct with
member-wise equality over filter, all three address modes, anisotropy, `MaxMipLevel` and the LOD
bias's exact bit pattern; a bit-packed key that drops bits would hand different biases the same
native sampler.

**Every application writes the complete sampler state.** EasyGL keeps one long-lived GL sampler per
slot, and FNA3D one `FNA3D_SamplerState` per slot. Both establish filter, addressing, anisotropy,
`MaxMipLevel` and LOD bias on every application, so state written by an Effect's own
`sampler_state` block does not survive into a later draw that names only filter and addressing —
for example a `SpriteBatch` flush, which in XNA assigns its whole `SamplerState` to slot zero. A
renderer that builds a fresh descriptor per draw (WebGPU) cannot have this hazard.

The shared `SamplerLodAddressWContract`'s nine differential pixel checks prove level-zero and
level-one mip selection, positive LOD bias and reset, independent V/W Clamp and Wrap behavior, and
state reset on EasyGL `OPENGL33`.

## 7. Anisotropic filtering

EasyGL implements `TextureFilter::Anisotropic` when `GL_EXT_texture_filter_anisotropic` is
available: it reads the live driver cap and clamps the requested `MaxAnisotropy` to it before
applying.

A visual anisotropic-quality pixel comparison is too driver-dependent to assert precisely across
renderers. The test instead assigns `MaxAnisotropy=9999` (beyond any real GPU's cap) and requires
no crash or throw. `Anisotropic` also maps to a `_MIPMAP_`-suffixed GL filter, so the
`GL_TEXTURE_MAX_LEVEL` clamp of §6 is what keeps an ordinary single-level `Texture2D` (e.g.
`Texture2D::CreateFromPixels`) from sampling black.

---

## 8. Sampler slots 1–15 in a `SpriteBatch` frame

`applySamplerStatesToRenderer()` (§3) runs from the device's draw entry points only, and a sprite
batch does not go through them. A stock sprite draw uses one texture whose sampler travels down the
batch's own path (`ISpriteBatchRenderer::SetSamplerState`), but a **custom effect** can bind a
second texture to the same batch, and XNA governs the texture at sampler register *u* with
`SamplerStates[u]`.

`SpriteBatch` therefore publishes the device's sampler states in the shared layer, where FNA puts
it, from the two places FNA's `PrepRenderState` runs — `Begin()` for `SpriteSortMode::Immediate`
(which never reaches a batch flush) and the flush otherwise. One narrower difference remains: XNA
re-applies device state at every draw call, and CNA's sprite path does not go through the device's
draw entry points, so a `SamplerStates[u]` assigned *between* `Begin()` and a `Draw()` reaches a
`Deferred` batch (published at its flush) but not an `Immediate` one.

**It publishes from slot 1 upward, deliberately.** Slot 0 already has a writer — the batch's own
sampler, down the `ISpriteBatchRenderer` path — and two renderer-side writers with no ordering
between them would race across the renderer families.

## 9. What a `SpriteBatch` leaves behind in `SamplerStates[0]`

XNA's `PrepRenderState` assigns the batch's `SamplerState` to `GraphicsDevice.SamplerStates[0]`
(`SpriteBatch.cs:1426`), and CNA does too; **when** it happens is not what FNA's source reads like.

Measured on the shipped XNA 4.0 runtime (`spikes/xna-spritebatch-sampler0-spike/`), with
`SamplerStates[0]` set to `PointClamp` and a `Deferred` batch begun with `PointWrap`:

```
before Begin: SamplerStates[0] = PointClamp
after  Begin: SamplerStates[0] = PointClamp     <- not published yet
after  End  : SamplerStates[0] = PointWrap      <- published at the flush
3D draw after the batch, sampled at u = 1.25 -> red, i.e. the batch's Wrap
```

So the assignment happens where `PrepRenderState` runs, which for a `Deferred` batch is the
**flush** and not `Begin()` — the same two points §8's publication uses. CNA assigns there, and
only the *device collection* is written: the push to the renderer stays in `GraphicsDevice`'s draw
entry points, so the batch's state reaches the sampler on the **next
draw**. That is XNA's ordering, and it keeps slot 0 to one renderer-side writer.

**For a game:** a 3D draw issued after a `SpriteBatch` samples with the batch's `SamplerState`,
not with whatever the game last assigned. This is XNA behaviour and it is CNA-wide, not
renderer-specific; a game that needs another state assigns `SamplerStates[0]` explicitly after
`End()`, as on XNA.

Asserted by `modules/graphics/examples/spritebatch_sampler0_publication_test.cpp`, registered on
EasyGL and SDLGPU.

## 10. The W axis reaches EasyGL

2D sampling never consults W, but a `sampler3D` does. The W axis travels in
`ISpriteBatchRenderer::SetSamplerState`, which carries all seven `SamplerState` properties; its
default body fans out to `SetSamplerAddressW` for a renderer (DirectX 9) that overrides only the
per-property hooks.

On EasyGL, `EasyGLRenderer::ApplySamplerState` derives `WrapR` from `addressU`, which is
correct for every XNA 4.0 preset — `PointClamp`, `LinearWrap` and the rest set all three axes alike
— and wrong for a `SamplerState` that set W on its own. The sprite flush applies the batch's own W
after it, at both flush sites (the stock one and the compiled-effect one), and only when the batch
supplied one: `-1` keeps the `addressU` derivation for any caller that flushes sprites without going
through `Begin()`.

**Why one override is enough here.** On this renderer `ShaderEffect::SetTexture(0, volume)` binds
`GL_TEXTURE_3D` on GL unit 0 while the sprite's own texture occupies `GL_TEXTURE_2D` on the same
unit, and a single GL sampler object at that unit governs both — so the batch's slot-0 sampler is
exactly what addresses the volume.

Asserted by `modules/renderers/easygl/examples/easygl_texture3d_addressw_test.cpp`
(`EasyGL_Texture3DAddressW`).

## Summary: EasyGL contract

The detailed cross-renderer LOD and W-addressing boundary is in §6b; the broad renderer comparison
is in `graphics-renderer-feature-matrix.md`.

| Feature | EasyGL status |
|---|---|
| `SamplerState` API, presets, preset `Name` and collection defaults | ✅ |
| Per-slot sampler application on 3D stock-effect draws | ✅ |
| `TextureAddressMode::Clamp`/`Wrap`/`Mirror`, including independent W | ✅ |
| All nine `TextureFilter` ordinals, including mip-aware `Point`/`Linear` | ✅ |
| Mip-level `SetData(level>0)` and full/partial/single-level completeness | ✅ |
| `TextureFilter::Anisotropic` and over-cap `MaxAnisotropy` clamping | ✅ |
| `MaxMipLevel` | ✅ |
| `MipMapLevelOfDetailBias` | ✅ `OPENGL33`; ⚠️ ES implicit sampling has no equivalent state, while compiled vertex `TEXLDL` is emulated (§6b) |
| `SpriteBatch` publication of slots 1–15 and measured slot-0 state | ✅ |
| Sampler mutation/reset and cache identity | ✅ |
| Mid-batch `SamplerStates[u]` mutation in `SpriteSortMode::Immediate` | ⚠️ not republished before each draw (§8) |

Legend: ✅ verified working · ⚠️ explicit limitation described in the cited section.
