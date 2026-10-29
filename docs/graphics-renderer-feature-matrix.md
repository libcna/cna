# Graphics renderer feature matrix — established GPU paths and CPU/raster companions

Master, up-to-date cross-renderer feature matrix for CNA's established renderers, written for Task
451 (Phase 51) and extended 2026-07-15 with a `DIRECTX9` column (`../the DX9 plan`, `D9-130`). The
WebGPU renderer is intentionally tracked separately in [`webgpu-renderer.md`](webgpu-renderer.md);
SDL_GPU ([`sdl-gpu-renderer.md`](sdl-gpu-renderer.md)) and FNA3D
([`fna3d-renderer.md`](fna3d-renderer.md)) have no columns here. A cell is only marked ✅ if a real,
GPU-facing pixel/behavior check actually exercised it; anything implemented but not independently
pixel-tested is marked 🟨, and anything not attempted/not built at all is marked ⬜ (distinct from
this doc's own ❌, which means "tested and found to genuinely not work").

**The `DIRECTX9` column measures something narrower and stricter than the EasyGL column: pixel-for-pixel
indistinguishability from the real XNA 4.0 runtime**, not just "feature works." A `DIRECTX9` ✅ means the
feature is exercised by `tools/xna-oracle/`'s checked-in scene corpus (`D9-120`, `D3D9_XNA_Diff`
CTest) and its render is byte-identical (`--tolerance 0`) to the real XNA 4.0 runtime's own render of
the same scene — a strictly stronger bar than the EasyGL column's ✅. This is
"verified through Wine+DXVK on this dev machine's real GPU," not real Windows hardware (`D9-140`,
open). See `docs/directx9-renderer.md` and `docs/d3d9-divergence-report.md` for the full detail this
column's cells summarize.

The **Headless** renderer (`CNA_GRAPHICS_RENDERER=HEADLESS`, [`headless-renderer.md`](headless-renderer.md)) is deliberately
**not** a column in this matrix: it never renders a single pixel, so none of the below
correctness/parity rows are meaningful for it. It exists for a different purpose entirely — running
game logic headlessly (no window, no GPU, no display server) for fast CI tests — and validates
itself via argument checks, resource-lifecycle tracking, and draw-call/state-change counters instead
of pixel output. See [`headless-renderer.md`](headless-renderer.md) for its own status.

The **Software** renderer (`CNA_GRAPHICS_RENDERER=SOFTWARE`,
[`software-renderer.md`](software-renderer.md)) is a first-class independent CPU renderer for the
classic XNA/Core contract. It is represented by the companion matrix below rather than forced into
the GPU-native columns, because physical swap chains, driver state and context-loss rows are
intentionally non-applicable. The historical v1 boundary survives only in the deleted plan's Git
history; the current renderer has behavioral evidence across every in-scope family. See
[`software-renderer.md`](software-renderer.md) and
[`software-easygl-parity-ledger.md`](software-easygl-parity-ledger.md) for detailed proofs.

## Software CPU-raster XNA/Core companion matrix (`SOFTWARE-128`)

`✅` means the CPU implementation and public behavioral evidence are present. `N/A` is an
intentional implementation-model difference, and `OUTSIDE-SCOPE` is outside this classic
XNA/Core campaign rather than a Software parity gap.

| Feature family | Status | Evidence / boundary |
|---|---:|---|
| Device creation, CPU backbuffer, clear and readback | ✅ | Windowless renderer descriptor and null device window; deterministic backbuffer/target readback. The complete Software CTest label passes 156/156 under authenticated Xvfb. |
| Primitive assembly and vertex/index input | ✅ | Triangle list/strip, line list/strip and existing PointListEXT regression path; indexed/non-indexed user/buffer draws, alternating strip winding, all 12 declaration formats, multiple streams, offsets, base vertex, 16/32-bit indices, dynamic updates and classic indexed instancing. |
| Clipping and raster rules | ✅ | Six-plane homogeneous clipping, D3D/XNA top-left and integer-pixel-center rules, perspective-correct varyings, viewport depth, scissor, culling, wireframe and depth/slope bias have shared or strict CPU pixel contracts. |
| Blend, depth, stencil, MSAA and queries | ✅ | All blend factors/functions with independent RGB/alpha, blend factor, four color masks, all depth/stencil comparisons/ops/masks/two-sided state, sample-correct 4× storage/coverage/resolve and exact CPU OcclusionQuery counts. |
| Texture2D and sampling | ✅ | Complete classic format matrix including packed, DXT, SNORM and float/half storage; exact full/partial/mip/NPOT transfer; independent U/V Wrap/Clamp/Mirror, point/linear min-mag-mip selection, anisotropic sampling and per-slot state. |
| TextureCube | ✅ | Every XNA-permitted plain-cube layout has exact face/mip transfer and format-correct EnvironmentMap sampling; measured-XNA-invalid layouts reject explicitly. |
| Texture3D | ✅ | Exact classic storage/readback/upload for mip chains and partial boxes. No classic stock effect samples volume textures; CNAEXT shader sampling remains deferred. |
| RenderTarget2D / RenderTargetCube | ✅ | Complete classic renderable color-format matrix, depth formats, discard/preserve, typed transfer, sampling after render, mips, face isolation and per-face 4× resolve. |
| Multiple render targets | ✅ | Four ordered 2D/cube-face targets, independent clear/finalization/color masks, slot-zero depth ownership, mips and mixed-set validation pass the same 22-check public contract as EasyGL. |
| Classic stock effects | ✅ | BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect and SpriteEffect behavior cover unlit/lighting/default lights/fog/alpha/texture/vertex color/specular/per-pixel/normal-transform/bone paths with shared Software/EasyGL probes. |
| SpriteBatch and SpriteFont | ✅ | Every draw/layout family, sort modes, transforms, sub-pixel placement, viewport/scissor/state/target interaction, glyph spacing/newline/fallback/flips/rotation/scale and deterministic disposal have shared public proof. |
| Model.Draw orchestration | ✅ | Nine shared rigid/skinned hierarchy, effect, texture, content, index-width and sampled-animation scenes plus 122 shared Model unit checks. |
| Resource lifecycle, validation and presentation reset | ✅ | Shared disposal/ownership/events/range checks and CPU backbuffer/MSAA/depth/stencil resize/reset behavior. Physical fullscreen or swap timing is not fabricated. |
| PbrEffect / SkinnedPbrEffect | ✅ | CPU metallic-roughness GGX direct lighting, three directional lights plus one punctual light, split-sum IBL with explicit roughness LOD, all seven material textures, independent UV selectors/transforms, sRGB transfer, alpha coverage, vertex colour, inverse-transpose skin normals and mirrored tangent handedness. Single/cascade directional, point-cube and spot-map shadow reception is implemented. The IBL and shadow oracles each pass 8/8. |
| Custom ShaderEffect and standalone stylization effects | OUTSIDE-SCOPE | These CNA additions are outside this classic XNA/Core Software campaign; capability depends on the selected renderer. The former modern engine pipeline has been removed. |
| Physical window, GPU context, swap chain and driver state | N/A | Software owns no native window or graphics context; `Present()` completes synchronously while preserving the CPU framebuffer. |

The **Stub** renderer (`CNA_GRAPHICS_RENDERER=STUB`) is, like Headless, **not** a column in this
matrix and for the same reason: it never renders a single pixel. Unlike
Headless, it also keeps no bookkeeping at all (no counters, no validation modes, no trace log) — it
exists purely as the smallest possible complete `IGraphicsRenderer` implementation, a minimal
reference and a dependency-free placeholder, not a pixel-parity or diagnostic tool. See
`docs/stub-renderer.md` for its current capability boundary.

## glTF campaign companion matrix (`GLTF-393`)

The glTF differential campaign has a deliberately narrower executable renderer set than the
project-wide tables below: STUB establishes importer independence, HEADLESS observes the native
draw boundary without rasterising, and OPENGLES3 provides real graphics-API and framebuffer
evidence. This table records the four renderer-sensitive features named by
the glTF campaign; it does not turn a boundary-only result into pixel support.

Status here is specific: ✅ means a real native draw or framebuffer assertion; ◇ means the complete
value/topology reaches an observable non-rasterising boundary; ❌ means the renderer explicitly has
no such route. The general feature tables remain authoritative for renderers outside this campaign,
while `gltf-renderer-pbr-fallbacks.md` carries the separate PBR shader inventory.

| Renderer | 32-bit index draw | `POINTS` | line modes | MRT | glTF PBR sRGB contract | Evidence boundary |
|---|---:|---:|---:|---:|---:|---|
| STUB | ❌ | ❌ | ❌ | ❌ | ◇ | Imports and L1–L6 effect capture run, but `SupportsCapability(ThreeD)` is false and no draw is submitted. |
| HEADLESS | ◇ | ◇ | ◇ | ◇ | ◇ | `HeadlessIndexBufferRenderer`, topology/MRT trace and `GpuDrawParams` are observable; no pixel is produced. |
| OPENGLES3 / EasyGL | ✅ | ✅ | ✅ | ✅ | ✅ | Full glTF selection 520/520; `PointListPrimitiveTest`, its interleaved line control, EasyGL MRT tests and `EasyGL_Pbr_SrgbTransfer`. |

MRT is recorded because a conformance/viewer harness may render diagnostic outputs to several
attachments; core glTF itself has one material colour result and does not require MRT. Likewise,
“sRGB” here means the glTF semantic contract — decode only base-colour/emissive samples, keep
normal/MR/occlusion and factors linear, mix fog in linear space, then optionally encode the final
RGB. It is not a claim that every native texture or swap-chain format is sRGB. The ordinary
`GltfRendererPbrFallbackPolicy.EveryPbrShaderHonorsColorSpaceDeclarations` source audit prevents the
other PBR implementations from silently diverging even when this machine cannot execute
them.

### All-renderer 32-bit index audit (`GLTF-163`)

This is a source-capability matrix, not a claim that all platform APIs were executed on this Linux
host. It covers every renderer directory that implements
`CreateIndexBuffer16`. ✅ means the renderer owns an explicit 32-bit factory and preserves uint32
values to its draw boundary; ◇ means the exact width is observable but that renderer deliberately
does not rasterise.

The shared `IGraphicsRenderer::CreateIndexBuffer32` no longer delegates to
`CreateIndexBuffer16`: it throws `32-bit index buffers are not supported by this renderer`.
Therefore a backend outside the ✅/◇ set cannot return a plausible 16-bit handle and later truncate
a glTF mesh. `GltfRendererIndexWidthPolicy` derives the complete directory inventory and locks the
provider/rejecter partition; adding a renderer without a disposition fails L0. The public
runtime contract is independently exercised by
`IndexBufferEmptyDataTest.SharedThirtyTwoBitFactoryRejectsInsteadOfDelegatingToSixteenBits`.

| Renderer | Status | 32-bit route / refusal boundary |
|---|---:|---|
| DirectX 9 | ✅ | Explicit factory; `D3DFMT_INDEX32`. |
| EasyGL | ✅ | Explicit factory; draw selects `GL_UNSIGNED_INT`. |
| FNA3D | ✅ | Explicit factory; FNA3D 32-bit index element size. |
| Headless | ◇ | Width-locked uint32 shadow and validated indexed-draw trace; no pixels. |
| SDL_GPU | ✅ | Explicit width-locked factory; `SDL_GPU_INDEXELEMENTSIZE_32BIT`. |
| Software | ✅ | Width-locked uint32 CPU buffer and raster path. |
| Stub | ◇ | Explicit width-locked handle accepts uint32 exactly; renderer intentionally draws nothing. |
| WebGPU | ✅ | Explicit factory; `WGPUIndexFormat_Uint32`. |

SDL_GPU and Stub were exact-width implementations that previously relied on the unsafe shared
delegation. They now own explicit factories; Stub also rejects a direct renderer-level upload whose
width disagrees with the factory declaration.

### Explicit unsupported paths (`GLTF-394`)

An unsupported cell is not permission to submit a different primitive or a narrower index. STUB's
campaign contract ends at `GraphicsCapability::ThreeD == false`, and draw-requiring glTF tests name
that capability when they skip; HEADLESS exposes a trace and never calls it a framebuffer result.
The shared uint32 factory throws the width-specific message above for every renderer without an
explicit 32-bit route.

The Direct3D comparison audit found the remaining silent point-topology defaults. D3D9's five draw
implementations select the API's real point-list topology. The shared validation fixture accepts
all five public primitive types while rejecting an invalid ordinal. No D3D9 mapper retains an
unnamed default that can silently turn a future topology into a triangle list.

Status legend: ✅ correct and verified · ⚠️ partial/emulated/environment-limited · ❌ known gap, not
fixed · ⛔ BLOCKED, needs a project-owner architecture decision · 🟨 implemented but not
independently verified (D3D9 column only, see above) · ⬜ not attempted this session (D3D9 column
only).

## 2D SpriteBatch / SpriteFont

| Feature | EasyGL | D3D9 |
|---|---|---|
| All `Draw` overloads, sort modes, rotation/flip/scale/crop | ✅ | ✅ real Microsoft `SpriteEffect.fx`, oracle-verified 0-divergence for rotation (`sprite_rotated_quad`), flip (`sprite_flipped_quad`), multi-texture batching (`sprite_multitexture_quad`), and 3 of 5 `SpriteSortMode` values (`Deferred`/`BackToFront`/`FrontToBack`, `D9-93`); `.Immediate`/`.Texture` confirmed not oracle-viable, not a coverage gap — see `docs/directx9-renderer.md` |
| Custom `Effect` via `Begin(effect)` | ✅ | ✅ **row corrected 2026-07-16 — was stale.** Real color-inversion custom-GLSL shader via `D3D9EffectRenderer` (runtime `D3DCompile()`), through the real public `SpriteBatch.Begin(effect)` API (`D9-110`/`D9-111`/`D9-112`, Phase D9-11 closed 2026-07-15) — this cell previously said "not started," written before that phase landed |
| SpriteFont — glyph placement/spacing/newline/fallback/flip | ✅ pixel-verified (Tasks 424-429) | ⬜ not attempted this renderer |
| `TextureAddressMode::Wrap`/`Mirror` via SpriteBatch | ✅ | ✅ real `D3DSAMP_ADDRESSU`/`V`, oracle-verified: distinct 4-band tiling (Wrap) vs. symmetric mirroring pattern (Mirror), proven genuinely different from each other, not just "some non-Clamp behavior" (`D9-92`, `sprite_wrap_quad`/`sprite_mirror_quad`) |

## RenderTarget / MSAA / mip / depth

| Feature | EasyGL | D3D9 |
|---|---|---|
| `RenderTarget2D`/`RenderTargetCube`/MRT construction | ✅ | ✅ **row corrected 2026-07-16 — the crash below is fixed.** `MaxRenderTargets` `GraphicsProfile` ceiling (Reach=1/HiDef=4) real and mutation-verified (`D9-103`); full construction/bind/clear/readback pixel-verified (`DirectX9_Smoke` Checks S/T/U). The reproducible, uncaught DXVK crash this cell used to describe (sampling a `RenderTarget2D` as an ordinary effect texture) was root-caused and fixed 2026-07-16 — a real CNA-side type-confusion bug (`static_cast` between the unrelated sibling classes `D3D9TextureRenderer`/`D3D9RenderTargetRenderer`), not a DXVK/environment limitation (see `NEXT.md` §4). No `D9-A5` oracle scene exercises this path yet, though — that remains open, separately from the crash |
| MSAA (both RT types) | ✅ | ⬜ not attempted — the render-target-as-texture crash that used to block this is fixed (2026-07-16, see the row above), but no oracle scene exercises `RenderTarget2D`/`RenderTargetCube` MSAA yet |
| Mip chains (both RT types) | ✅ | ⬜ not attempted — same as MSAA above: no longer crash-blocked (2026-07-16), just not yet oracle-covered |
| Per-instance `DepthStencilFormat` fidelity | ✅ | ⬜ not attempted — same as MSAA/mip above: no longer crash-blocked (2026-07-16), just not yet oracle-covered |

### Shared MRT attachment limit (Task 775)

`GraphicsDevice::SetRenderTargets` throws `std::invalid_argument` above **4** targets in shared C++
code (`MAX_RENDERTARGET_BINDINGS`, Task 881) — mirroring FNA's own real
`internal const int MAX_RENDERTARGET_BINDINGS = 4` cap — before any renderer ever sees the call, so
this is the practical, enforced limit on every renderer regardless of what the underlying device
itself could support.

## Texture2D / Texture3D / TextureCube

| Feature | EasyGL | D3D9 |
|---|---|---|
| Texture2D `SetData`/`GetData`/`FromStream`/`SaveAsPng`/NPOT | ✅ | ✅ real `D3DPOOL_MANAGED` upload, exercised (and oracle-verified byte-exact) by every textured scene in the corpus (`texturepixel=` used 64×); `FromStream`/`SaveAsPng`/NPOT-on-`Reach` not separately tested |
| Texture2D mip-level `SetData` (level > 0) | ✅ | ⬜ not attempted — no oracle scene exercises mip level > 0 |
| Texture3D/TextureCube `SetData`/`GetData`, incl. mip | ✅ | 🟨 split: `TextureCube` `SetData` real, oracle-verified byte-exact via `envmap_quad`/`envmap_fresnel_quad`; `Texture3D` has only its `GraphicsProfile` size-ceiling mutation-verified (`D9-103`) — no oracle scene or GPU round-trip test exists for `Texture3D` at all |
| Texture3D/TextureCube sampled in shaders | ❌ don't inherit `Texture` (Task 863, architectural) | 🟨 `TextureCube` sampled and oracle-verified byte-exact via `EnvironmentMapEffect` (`envmap_quad`/`envmap_fresnel_quad`); `Texture3D` has no consuming shader variant tested |
| Non-`Color` `SurfaceFormat` for real GPU texture data | ⛔ **BLOCKED** (Task 732) | same shared-code limitation |

## GraphicsDevice state objects

| Feature | EasyGL | D3D9 |
|---|---|---|
| `BlendState` (all presets + custom factors/equations) | ✅ | ✅ real `D3DRS_SRCBLEND`/`DESTBLEND` per-preset dispatch, `Opaque`/`AlphaBlend` pixel-behavior-verified (`DirectX9_BlendState_Opaque`/`_AlphaBlend`) and separately mutation-verified (src/dst swap caught, `D9-122`) |
| `DepthStencilState` (compare func + full stencil ops) | ✅ | ✅ real `D3DRS_STENCILENABLE`/compare-func dispatch, pixel-verified (`DirectX9_DepthStencilState_StencilEnable`) and mutation-verified (forced-false caught, `D9-122`) |
| `RasterizerState` | ✅ | ✅ real `D3DRS_CULLMODE` winding-order dispatch, pixel-verified (`DirectX9_RasterizerState_CullMode`) and mutation-verified (forced-none caught, `D9-122`); depth-bias not separately tested — see this doc's own "Remaining genuine D3D9 limitations" section for `D9-62`'s own outstanding depth-bias oracle-proof attempt (investigated 2026-07-16, not closed: no observable effect at any tested magnitude, suspected environment/driver limitation) |
| Per-slot `SamplerState` (16 slots) | ✅ | 🟨 real `D3DSAMP_ADDRESSU`/`V`/`MAGFILTER`/`MINFILTER` wiring, Wrap/Mirror pixel-behavior oracle-verified via SpriteBatch (`D9-92`); not tested across all 16 slots simultaneously |
| `GraphicsDevice.ReferenceStencil` | ✅ shared stamp/test fixture passes on EasyGL (`GraphicsDevice_ReferenceStencil`, remeasured 2026-09-09) | ⬜ not attempted — no D3D9 test exercises `SetReferenceStencil()` |
| `Clear` honors `ClearOptions::Stencil` | ✅ `GraphicsDevice_ClearStencil` and `ClearOverloads` pass on EasyGL (remeasured 2026-09-09) | ⬜ not attempted — `DirectX9_Smoke` covers plain `Clear(r,g,b,a)` only, not the `ClearOptions` combo variants |

## OcclusionQuery (Phase 50, closed this session — see `docs/occlusionquery-support.md` for full detail)

| Feature | EasyGL | D3D9 |
|---|---|---|
| Wired to real GPU work (`Begin`/`End`) | ✅ | ⬜ **not built at all** — the DX9 plan's task list has no `IDirect3DQuery9`-based occlusion-query task; `CreateOcclusionQuery()` falls through to `IGraphicsRenderer`'s own silent `nullptr` default |
| Pixel/query correctness (visible vs. occluded) | ✅ verified both directions (Tasks 445/446) | N/A — no query support exists |

## Model (Phase 49, closed this session — see `docs/model-content-pipeline-support.md` for full detail)

| Feature | Status | D3D9 |
|---|---|---|
| Runtime API (`Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone`) | ✅ fully audited/FNA-faithful, several real bugs found and fixed (Tasks 431-439) | ⬜ not separately tested against this renderer |
| Content-pipeline loading (`ModelTypeReader`) | 🟨 the custom `.model.json` format is not XNB-compatible, but hierarchy/parent bones, textures/effects, skeleton animation and 32-bit indices are implemented and covered | ⬜ not separately tested against this renderer |
| `Model` constructor root-bone-index flexibility | ✅ fixed (Task 916, 2026-07-09) | ⬜ not separately tested against this renderer |

## Every currently-BLOCKED task (⛔)

| Task | Renderer | One-line reason |
|---|---|---|
| 732 | EasyGL | Real `SurfaceFormat` GPU forwarding conflicts with an already-shipped, already-tested `Texture::ValidateFormat` contract (Task 176) plus the public `SetData`/`GetData` API being `Color*`-only |

## Known pre-existing test-failure baseline, per renderer

Confirmed most recently by this session's own regression runs (always run sequentially per
renderer, never concurrently — concurrent runs have previously produced transient GPU/driver-
contention false failures):

- **EasyGL**: 3 — `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`,
  `easy-gl-resource-smoke-tests`. Reconfirmed as recently as Task 449's own regression (4510/4513).

## New tracked follow-up tasks opened this session

- ~~**Task 916**~~ — **fixed, 2026-07-09** (same day it was opened): `Model`'s constructor used to
  auto-default `Root` to `bones[0]` with no way to specify a different root bone index; an optional
  `rootBoneIndex` parameter now covers it (low-risk, purely-additive fix).

## Remaining genuine D3D9 limitations (`D9-130`, 2026-07-15)

Unlike the EasyGL column, `DIRECTX9`'s ✅ cells above are not "implemented and plausible" — they are
each backed by a byte-identical (`--tolerance 0`) match against the real XNA 4.0 runtime for a
checked-in oracle scene (`D9-120`, `D3D9_XNA_Diff`). The corpus has grown to 36/36 scenes (0
diverge) as of Task 1101 (2026-07-16), up from 31 at `D9-130`'s original 2026-07-15 writing.
**Three of this section's original six gaps are now fixed, corrected below rather than silently
removed** — the genuine, currently-open gaps are:

- **`Texture3D` has no GPU round-trip or oracle coverage at all** — only its `GraphicsProfile`
  size-ceiling is tested; no scene in the corpus samples a `Texture3D` in a shader.
- **`OcclusionQuery` is not built** for this renderer.
- **Not verified on real Windows hardware** (`D9-140`) — every result here is Wine+DXVK-on-this-
  machine.

Fixed since this section was originally written (2026-07-15):

- ~~Render targets cannot be sampled as textures at all — a reproducible, uncaught DXVK crash~~ —
  **fixed 2026-07-16**: a real type-confusion bug (`static_cast` between the unrelated sibling
  classes `D3D9TextureRenderer`/`D3D9RenderTargetRenderer`, both implementing `ITextureRenderer`),
  not a DXVK/environment limitation. See `NEXT.md` §4 and `src/Graphics/Renderers/D3D9/
  D3D9EffectDraw.cpp`'s `ResolveD3D9TextureEXT`/`ResolveD3D9TextureCubeEXT`.
- ~~`PreferPerPixelLighting` and real specular highlights are structurally unreachable~~ — **fixed
  2026-07-16** (graphics Phase 80, Task 1101): `GpuDrawParams::preferPerPixelLighting`/
  `specularEnabled` are now real fields (Task 1100), read by this renderer's dispatch instead of
  hardcoded `false`. 4 of the 5 `PixelLighting` bytecode variants `D9-73` had flagged as diverging
  from Microsoft's own shipped bytes are now oracle-proven pixel-perfect; the 5th (untextured
  `VSBasicPixelLighting`) remains permanently unreachable, blocked by the same missing Position-only
  vertex layout as the untextured vertex-lit bucket — unrelated to this fix. This was also the
  project-wide "Divergence 1" every other CNA renderer shared; graphics Phase 80 has since
  fixed EasyGL and WebGPU (`BasicEffect`-only) too. Software subsequently gained
  distinct per-vertex/per-pixel classic lighting in `SOFTWARE-113` and `SOFTWARE-115`; the old
  no-lighting statement is retained here only as the historical state when this D3D9 note landed.
- ~~`CnaTests` does not build under D3D9~~ — **fixed** (`D9-123`, 2026-07-15, same session `D9-130`
  was written in but not yet reflected here): all POSIX `::setenv()`/`::unsetenv()` call sites
  replaced with `System::Environment::SetEnvironmentVariable`; `CnaTests` compiles and the
  `gtest_discover_tests`/`CROSSCOMPILING_EMULATOR` wiring works under D3D9.

See `docs/directx9-renderer.md` for the full writeup and `docs/d3d9-divergence-report.md` for the raw
measurement this section summarizes.

## See also

- `docs/model-content-pipeline-support.md`, `docs/occlusionquery-support.md` — full detail for
  those 2 systems, summarized above.
- `docs/directx9-renderer.md`, `docs/d3d9-divergence-report.md` — the `DIRECTX9` column's full detail: what
  it proves, the oracle harness, and the honest DXVK-authenticity caveat every result inherits.
