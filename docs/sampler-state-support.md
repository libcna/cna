# SamplerState / texture-sampling support matrix

> **Current-state reconciliation, 2026-10-31:** this Phase 35 audit now incorporates the later
> mip-upload/completeness fixes (Tasks 924–926), EasyGL anisotropy (Task 918), the corrected
> nine-value `TextureFilter` mapping (REMED-GFX-175), complete sampler-state propagation
> (FX-083/091/092), and the measured `SpriteBatch` publication contract. Historical findings are
> labelled as such; the remaining profile- and route-specific limitations are stated in §6b and §8.

Phase 35 (graphics Tasks 291–300) audited and pixel-verified `SamplerState` and texture
sampling conformance against FNA. This document summarizes the findings.

---

## 1. `SamplerState` API surface (Task 291)

The 7-property surface, all 6 static presets (`AnisotropicClamp`/`Wrap`, `LinearClamp`/`Wrap`,
`PointClamp`/`Wrap` — FNA has no `Default`/"PointMirror"/etc. presets beyond these 6), the
default-constructor values (`Filter=Linear`, `AddressU=AddressV=AddressW=Wrap`,
`MaxAnisotropy=4`, `MaxMipLevel=0`, `MipMapLevelOfDetailBias=0`), and every preset's filter/address
values already matched FNA exactly.

One real, fixed finding: FNA's private preset constructor sets `Name` on every preset (e.g.
`"SamplerState.PointClamp"`); CNA's didn't. Fixed. Tasks 301, 311 and 321 subsequently fixed the
identical gap in `BlendState`, `DepthStencilState` and `RasterizerState`, closing Task 866.

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
`EnvironmentMapEffect`/`SkinnedEffect`), fixed this session:

| Renderer | Root cause | Fix |
|---|---|---|
| EasyGL / shared `GraphicsDevice` | All 18 `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` overloads never called `applySamplerStatesToRenderer()` — unlike `DrawPrimitives`/`DrawIndexedPrimitives`, which do. | Added the missing call to all 18 sites. |

Proven with a pixel test (`modules/renderers/easygl/examples/easygl_sampler_state_effect_test.cpp`,
`EasyGL_SamplerState_DualTextureEffect`) that initially failed and now passes.

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

The renderers already correctly mapped `TextureAddressMode` to their GPU's wrap/clamp/mirror
address modes — no bugs found in the mapping itself; the only real bug in this whole area was
Task 293's binding-never-applied issue above, which these tests now guard against regressing.

## 5. `TextureFilter::Point` vs `Linear` (Task 297)

First genuinely new-ground test in Phase 35 — no prior test exercised `TextureFilter` itself. New
test draws 4 columns in one frame (magnification × {`Point`,`Linear`}, minification ×
{`Point`,`Linear`}) on `DualTextureEffect`, sampling each at a texel boundary: `Point` reads a
pure, unblended colour; `Linear` reads a genuine ~50/50 blend. All 4 checks passed on the first
run (blend values landed at 127–128, exactly as predicted).

CNA maps XNA's single `TextureFilter` value to one GL min+mag filter pair (matching FNA's
flat enum, which has no separate min/mag control), and generates no mipmaps by default, so
magnification and minification exercise *identical* underlying sampler math in this project — the
two scales in this test confirm robustness at scale, not a functionally distinct GPU code path.

## 6. Mipmap filter behavior (Task 298; corrected by REMED-GFX-175)

The original Phase 35 test proved that `LinearMipPoint` selected an authored high mip, but it drew
the wrong conclusion from `Point` and `Linear` selecting level zero. XNA/FNA's flat nine-value
`TextureFilter` table defines a minification, magnification **and mip** component for every value:
`Point` maps to point/point/point and `Linear` to linear/linear/linear. They are full mip-aware
filters, not aliases for “disable mipmapping”.

Task 924 subsequently made EasyGL upload nonzero mip levels and clamp `GL_TEXTURE_MAX_LEVEL` to a
texture's real level count, fixing incomplete single-level textures. REMED-GFX-175 then removed the
remaining false exception on 2026-07-30: all nine EasyGL ordinals now use their FNA min/mag/mip
triples, including mipmap terms for `Point` and `Linear`. The shared `TextureFilterMipContract`
guards every ordinal, full and partial mip chains, and the single-level case across the rasterizing
renderer families. There is no accepted `Point`/`Linear` deviation today.

## 6b. `MaxMipLevel` and `MipMapLevelOfDetailBias` (FX-083, 2026-08-17)

`IGraphicsRenderer::ApplySamplerMipState(slot, maxMipLevel, lodBias)` carries XNA's two LOD states
across the renderer-neutral boundary. Its default no-op body originally hid missing renderer
adoption; every current rasterizing renderer now overrides it. `ApplySamplerAddressW` has likewise
reached every rasterizing renderer. `HEADLESS` and `STUB` deliberately have no sampling result on
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

**EasyGL compiled vertex sampling is the narrow ES exception since FX-146 (2026-10-04).** A
`vs_3_0 TEXLDL` already supplies an explicit level in the coordinate's W component, so the GLSL
ES 3 translation adds the selected one of four XNA vertex-sampler biases to that value. MojoShader's
OpenGL adapter uploads the four values through a program uniform immediately before the draw.
Desktop GLSL does not declare that uniform and continues to use `GL_TEXTURE_LOD_BIAS`, preventing a
double application. This does not claim a general ES sampler-state solution: implicit pixel/stock
sampling still has no equivalent state in EasyGL. The existing shared vertex-sampler transition
test covers zero, +1, `MaxMipLevel=1`, reset, null and disposal and passes in the complete 686/686
OPENGLES3 compiled-effect selection.

### WebGPU's LOD bias is a SHADER emulation, and its boundary is named

`WGPUSamplerDescriptor` carries `addressModeU/V/W`, `magFilter`, `minFilter`, `mipmapFilter`,
`lodMinClamp`, `lodMaxClamp`, `compare` and `maxAnisotropy` — and nothing else. That absence was
once taken as proof the state had to stay unsupported; `WEBGPU-205` rejected that reasoning as the
same argument already disproved for `FillMode::WireFrame`, because an absent *state field* is not an
absent *capability*. WGSL's `textureSampleBias(t, s, coords, bias)` applies exactly XNA's semantic —
a bias added to the computed level of detail — in the fragment stage, which is where every one of
that renderer's sampling calls already sits. The value travels in the per-draw uniform block
(widened 160→176 bytes for it, as `WEBGPU-149` widened it for fog).

Two things a caller needs to know, and both are reported through
`GetAdditionalLimitationsTextEXT()` rather than left to be discovered from pixels:

* **WGSL clamps a sample bias to roughly [-16, +16)**, so a larger magnitude saturates rather than
  extrapolating. CNA clamps to that range on the way in instead of leaving the edges
  implementation-defined.
* **Two routes do not apply it**: a custom CNAEXT `ShaderEffect`, which supplies its own WGSL and
  therefore its own sampling calls; and the
  metallic-roughness `PbrEffect`/`SkinnedPbrEffect` families, which are the glTF route rather than
  an XNA stock effect.

**Compiled XNA Effects apply it too, since `WEBGPU-208` (2026-09-06).** They were a fourth route on
that list for one day. A compiled pass's sampling call comes from MojoShader, not from a shader CNA
writes, so the emulation moves one level down: `MojoShaderEffect::InjectSamplerLodBias` rewrites the
normalized SPIR-V so each implicit sample carries a `Bias` image operand fed from **its own D3D9
sampler register's** slot in a small renderer-owned uniform block. Because the rewrite happens
before the native and browser routes diverge, both targets execute the same semantic -- natively
naga lowers the SPIR-V operand, and in a browser `SpirvToWgsl` renders it as `textureSampleBias`
for Tint. The shared compiled-effect sampler pixel contract runs on both routes with
`supportsLodBias` true.

**`SpriteBatch` carries the complete state since SDLGPU-64 (2026-09-09).** The common
`ISpriteBatchRenderer::SetSamplerState` hook now carries filter, U/V/W addressing, anisotropy,
`MaxMipLevel` and `MipMapLevelOfDetailBias`. `SpriteBatch::Begin` also assigns the resolved state to
`GraphicsDevice.SamplerStates[0]`, matching FNA's observable `PrepRenderState` behavior. EasyGL,
SDL GPU, WebGPU and, since CNA-DEMOS-ISSUE-006, FNA3D consume the complete hook; FNA3D's sprite
route had replaced the anisotropy, `MaxMipLevel`, bias and W address with the defaults.
`RendererFeature::SamplerLevelOfDetailBias` reports whether a renderer applies the bias at all, and
`CrossRendererContractTest` checks that answer against the mip level SpriteBatch actually samples. The shared `parity_sprite_sampler_state` oracle now
requires bias +1 and -2 to select deliberately different mip colours, checks every retained device
property, passes on all three renderers, and remains byte-identical between EasyGL and WebGPU.

Note this makes WebGPU's coverage *broader* than the reference renderer's, which implements the bias
only on desktop core (`GL_TEXTURE_LOD_BIAS` does not exist in OpenGL ES at all). The shared parity
fixture `parity_sampler_lod_bias` therefore compares against the `OPENGL33` profile.

Why `GL_TEXTURE_MIN_LOD` rather than FNA3D's `GL_TEXTURE_BASE_LEVEL`: base level is texture-object
state, and CNA's contract is per **slot**. Two slots sampling one texture with different
`MaxMipLevel` values would fight over a single base level, whereas `min_lod` on a sampler object is
exactly "never resolve a level more detailed than this" and is per slot. It is also the mapping
FNA3D's own SDLGPU driver makes (`samplerCreateInfo.min_lod = samplerState->maxMipLevel`).

**SDL GPU stock routes are complete since SDLGPU-121 (2026-09-11).** SDLGPU-64 established that
every stock deferred command captures all seven sampler properties per slot. `MaxMipLevel`, filter,
addressing and anisotropy remain native sampler state. The bias is deliberately different: SDL's
vendored `SDL_gpu.h` states that `SDL_GPUSamplerCreateInfo::mip_lod_bias` is a no-op on Metal and
must be applied in the shader. Every stock 2D/cube sample now uses SPIR-V's Bias operand supplied by
an eight-slot per-draw fragment uniform, while its native sampler bias is zero to prevent a double
application on Vulkan/D3D12. The same exact mip-colour fixture passes on real Vulkan and cross-
compiled D3D12, and the SpriteBatch variant verifies its independent state path. AddressW reaches
`SDL_GPUSamplerCreateInfo::address_mode_w`; its volume-pixel proof remains coupled to the separate
Texture3D/compiled-effect volume work tracked by `SDLGPU-71`/`SDLGPU-79`.

**Compiled XNA Effects are complete since SDLGPU-122 (2026-09-11).** The pinned MojoShader adapter
now exposes one bounded callback after SPIR-V linking and before native shader creation. CNA uses it
to apply the shared `InjectSamplerLodBias` rewrite directly to MojoShader's combined samplers,
placing one vec4 per D3D9 sampler register in SDLGPU fragment-uniform set 3/binding 1. Replay
snapshots and pushes those values at fragment UBO slot 1 and deliberately zeros the corresponding
native sampler bias, preventing double application on Vulkan/D3D12. On Apple, the statically linked
ShaderCross configuration now consumes the same transformed SPIR-V and translates it to Metal's
native shader format rather than bypassing the transform through MojoShader's direct MSL profile.
The shared authored-mip pixel contract passes on Vulkan and headless D3D12 while a test hook rejects
every nonzero native bias; a 27-pass structural corpus additionally proves each combined and split
sample reads its own register. A real Metal execution remains platform-validation work, not an
unimplemented sampler semantic.

### 6b.1 Sampler identity and state lifetime (FX-091, FX-092, 2026-08-18)

Two follow-up findings, both about the same thing: what a renderer's sampler is a function OF.

**SDLGPU's sampler cache key was lossy.** It packed the description into a `uint64` with the
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
sampler state has this hazard; a renderer that builds a fresh descriptor per draw (WebGPU) cannot.

EasyGL also adopted `ApplySamplerAddressW` in the same pass, so its `GL_TEXTURE_WRAP_R` row above is
no longer "not adopted".

The shared `SamplerLodAddressWContract`'s nine differential pixel checks prove level-zero and
level-one mip selection, positive LOD bias and reset, independent V/W Clamp and Wrap behavior, and
state reset; it passes 9/9 on EasyGL `OPENGL33`.

## 7. Anisotropic filtering (Task 299, EasyGL row updated 2026-07-11 per Task 918)

| Renderer | Status |
|---|---|
| EasyGL | **Fixed, Task 918 (2026-07-09).** At the time this section was written, `TextureFilter::Anisotropic` silently fell back to plain trilinear filtering — the underlying `easy-gl` library had zero anisotropy-related API and `MaxAnisotropy` had no effect at any value. Now real: gated on `GL_EXT_texture_filter_anisotropic` being available, reads the live driver cap, and clamps the requested `MaxAnisotropy` to it before applying. |

A true visual anisotropic-quality pixel test (comparing detail preservation under oblique/
aspect-skewed minification) was judged too driver-dependent/fragile to assert precisely across
renderers. Instead, the task's own "caps and fallback" framing was tested literally: a new test
assigns `MaxAnisotropy=9999` (far beyond any real GPU's cap) and confirms this does not crash or
throw — the load-bearing assertion.

**Additional finding while building this test**: at the time, assigning `TextureFilter::Anisotropic`
to an ordinary single-level `Texture2D` (e.g. `Texture2D::CreateFromPixels`, the common case for
real game textures) rendered **solid black on EasyGL** — the exact same mipmap-incompleteness
symptom as §6, since `Anisotropic` also maps to a `_MIPMAP_`-suffixed GL filter, so EasyGL needed a
`GL_TEXTURE_MAX_LEVEL` fix (Task 867's scope). **Fixed, Task 924 (2026-07-09)** — `GL_TEXTURE_MAX_LEVEL` is now
clamped to each texture's real level count, so a single-level texture no longer reads as
GL-incomplete under `Anisotropic`/`Mip*` filters.

---

## 8. The half of Task 293 that a `SpriteBatch` frame never reached (2026-09-07)

Task 293 above fixed `GraphicsDevice.SamplerStates[u]` for the **3D** draw entry points, by adding
the missing `applySamplerStatesToRenderer()` calls to the eighteen `DrawUserPrimitives` overloads
that lacked them. That is where it stopped, and the gap it left is exact: **that method runs from
the draw entry points only**, so in a frame whose only drawing is a `SpriteBatch`, nothing ever
pushed slots 1–15 to any renderer at all. The renderer kept whatever those slots held from an
earlier 3D draw, or the state it was constructed with.

Invisible for years, because a stock sprite draw uses exactly one texture and the batch carries its
sampler down its own path (`ISpriteBatchRenderer::SetSamplerState`). It becomes decisive the moment a **custom effect**
binds a second texture to the same batch: XNA governs the texture at sampler register *u* with
`SamplerStates[u]`, and CNA gave every unit slot 0's state.

**Fixed in the shared layer**, where FNA puts it: `SpriteBatch` now publishes the device's sampler
states from the two places FNA's `PrepRenderState` runs — `Begin()` for `SpriteSortMode::Immediate`
(which never reaches a batch flush) and the flush otherwise. One narrower difference remains and is
worth stating rather than implying parity: XNA re-applies device state at every draw call, and
CNA's sprite path does not go through the device's draw entry points, so a `SamplerStates[u]`
assigned *between* `Begin()` and a `Draw()` reaches a `Deferred` batch (published at its flush) but
not an `Immediate` one.

**It publishes from slot 1 upward, deliberately.** Slot 0 already has a writer — the batch's own
sampler, down the `ISpriteBatchRenderer` path — and giving one slot two writers with no ordering
between them across the renderer families is the exact shape of the bug Task 293 fixed. That is
still true, and it is still slot 1 upward that gets *pushed to
the renderer* from the sprite path.

## 9. What a `SpriteBatch` leaves behind in `SamplerStates[0]` (2026-09-07)

§8 left one divergence open on purpose: `GraphicsDevice.SamplerStates[0]` did not hold the batch's
`SamplerState` afterwards, where XNA's `PrepRenderState` assigns it (`SpriteBatch.cs:1426`). It does
now, and **when** it happens is not what FNA's source reads like.

Measured on the shipped XNA 4.0 runtime (`spikes/xna-spritebatch-sampler0-spike/`), with
`SamplerStates[0]` set to `PointClamp` and a `Deferred` batch begun with `PointWrap`:

```
before Begin: SamplerStates[0] = PointClamp
after  Begin: SamplerStates[0] = PointClamp     <- not published yet
after  End  : SamplerStates[0] = PointWrap      <- published at the flush
3D draw after the batch, sampled at u = 1.25 -> red, i.e. the batch's Wrap
```

So the assignment happens where `PrepRenderState` runs, which for a `Deferred` batch is the
**flush** and not `Begin()` — the same two points §8's publication already uses. CNA now assigns
there, and only the *device collection* is written: the push to the renderer stays where it was,
in `GraphicsDevice`'s draw entry points, so the batch's state reaches the sampler on the **next
draw**. That is XNA's ordering, and it keeps slot 0 to one renderer-side writer.

**What this changes for a game:** a 3D draw issued after a `SpriteBatch` now samples with the
batch's `SamplerState`, not with whatever the game last assigned. This is XNA behaviour and it is
CNA-wide, not renderer-specific. A game that relied on the old behaviour should assign
`SamplerStates[0]` explicitly after `End()` — which is what it would have had to do on XNA too.

Asserted by `modules/graphics/examples/spritebatch_sampler0_publication_test.cpp`, registered on
EasyGL and SDLGPU.

## 10. The W axis reaches EasyGL (2026-09-07)

`SpriteBatch` was forwarding a batch sampler's filter and its U and V axes and dropping W —
invisible to 2D sampling, which never consults W, and decisive for a `sampler3D`. The W axis now
travels in `ISpriteBatchRenderer::SetSamplerState`, which carries all seven `SamplerState`
properties; its default body fans out to `SetSamplerAddressW` for a renderer (DirectX 9) that
overrides only the per-property hooks. (The `SetSamplerAddressModeWEXT` hook this section
originally described was removed on 2026-10-14, RRC-041: its only implementor was the retired
Vulkan renderer.)

EasyGL takes it. `EasyGLRenderer::ApplySamplerState` derives `WrapR` from `addressU`, which is
correct for every XNA 4.0 preset — `PointClamp`, `LinearWrap` and the rest set all three axes alike
— and wrong for a `SamplerState` that set W on its own. The sprite flush applies the batch's own W
after it, at both flush sites (the stock one and the compiled-effect one), and only when the batch
supplied one: `-1` keeps the old derivation for any caller that flushes sprites without going
through `Begin()`.

**Why one override is enough here.** On this renderer `ShaderEffect::SetTexture(0, volume)` binds
`GL_TEXTURE_3D` on GL unit 0 while the sprite's own texture occupies `GL_TEXTURE_2D` on the same
unit, and a single GL sampler object at that unit governs both — so the batch's slot-0 sampler is
exactly what addresses the volume.

Asserted by `modules/renderers/easygl/examples/easygl_texture3d_addressw_test.cpp`
(`EasyGL_Texture3DAddressW`).

## Summary: EasyGL contract today

**Reconciled 2026-10-31.** The detailed cross-renderer LOD and W-addressing boundary is in §6b;
the broad current renderer comparison remains in `graphics-renderer-feature-matrix.md`.

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

## Closed Phase 35 follow-up work

- **Task 866:** fixed — the other static state presets now set `Name`, closing the same gap found
  for `SamplerState` in Task 291.
- **Task 867 / Task 924:** fixed on 2026-07-09 — nonzero mip uploads are real and EasyGL clamps
  `GL_TEXTURE_MAX_LEVEL` to the texture's actual chain.
- **Task 918:** fixed on 2026-07-09 — EasyGL uses the live anisotropy extension/capability and
  clamps `MaxAnisotropy` to the driver limit.
- **REMED-GFX-175:** fixed on 2026-07-30 — all nine EasyGL filter ordinals carry their correct mip
  component; the former claim that `Point`/`Linear` deliberately selected only level zero was false.

No item in this historical list remains open. The two current EasyGL/XNA differences are the ES
implicit-LOD-bias boundary (§6b) and `SpriteSortMode::Immediate` not republishing a sampler changed
between `Begin()` and `Draw()` (§8).
