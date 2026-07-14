# Established graphics backend feature matrix — SDL_Renderer, EasyGL, Vulkan, Bgfx, D3D11, D3D12

Master, up-to-date cross-backend feature matrix for CNA's established backends, written for
Task 451 (Phase 51), extended 2026-07-14 with a `D3D11` column (`../plan_dx.md`, Phase DX11
`DX-96`), then a `D3D12` column (Phase DX12 `DX-115`) the same day, once each backend's feature set
was broad enough for a meaningful row-by-row comparison. The experimental WebGPU backend is
intentionally tracked separately in [`webgpu-backend.md`](webgpu-backend.md) and
`../plan_webgpu.md` until its feature surface is broad enough for meaningful parity columns.

**A `D3D11`/`D3D12` cell means "verified through Wine+DXVK/vkd3d-proton on this dev machine's real
GPU," not "verified on real Windows hardware"** — see `docs/d3d11-backend.md`/`docs/d3d12-backend.md`'s
own "Known limitations" for the real-Windows gate (`plan_dx.md` `DX-90`/`DX-91` for D3D11, `DX-114`
for D3D12) that still applies on top of every ✅ below. **`D3D12`'s own checks are all off-screen** —
its swap chain does not work on this dev loop at all (see `docs/d3d12-backend.md`), so a `D3D12` ✅
means "real off-screen GPU pixel/behavior proof," never "verified through a real presented frame." A
cell is only marked ✅ if a real, GPU-facing pixel/behavior check actually exercised it; anything
implemented but not independently pixel-tested is marked 🟨, and anything not attempted/not built at
all is marked ⬜ (distinct from this doc's own ❌, which means "tested and found to genuinely not
work").

The **Headless** backend (`CNA_GRAPHICS_BACKEND=HEADLESS`, tracked in `../plan_headless.md`) is deliberately
**not** a column in this matrix: it never renders a single pixel, so none of the below
correctness/parity rows are meaningful for it. It exists for a different purpose entirely — running
game logic headlessly (no window, no GPU, no display server) for fast CI tests — and validates
itself via argument checks, resource-lifecycle tracking, and draw-call/state-change counters instead
of pixel output. See `plan_headless.md` for its own status.

The **Software** backend (`CNA_GRAPHICS_BACKEND=SOFTWARE`, tracked in `../plan_software.md`) is
also **not yet** a column here, but for a different reason than Headless: unlike Headless, it
*does* render real pixels (a genuine CPU rasterizer), so it could plausibly become a real
pixel-parity comparison column once its feature set is broad enough — v1 only covers `TriangleList`,
a `BasicEffect` subset (no lighting/fog), nearest-neighbor texturing, and a simplified
`Opaque`/`AlphaBlend` distinction, too narrow for a meaningful row-by-row comparison against the
established backends yet. Worth revisiting as `plan_software.md`'s scope grows. See
`docs/software-backend.md` for its current capability boundary.

**Supersedes `docs/coverage.md`**, which is dated 2026-06-21
and predates almost this entire session's work
(dozens of real bugs fixed across all 4 backends, an entire SDL_Renderer 2D-only audit phase,
Model/OcclusionQuery correctness phases) and never covered SDL_Renderer at all. `docs/coverage.md`
is kept for its still-accurate non-Graphics namespace estimates (Audio/Media/Content/Net/
GamerServices); this doc is Graphics-only and current.

Status legend: ✅ correct and verified · ⚠️ partial/emulated/environment-limited · ❌ known gap, not
fixed · ⛔ BLOCKED, needs a project-owner architecture decision · 🟨 implemented but not
independently verified (D3D11 column only, see above) · ⬜ not attempted this session (D3D11 column
only).

## 2D SpriteBatch / SpriteFont

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D11 | D3D12 |
|---|---|---|---|---|---|---|
| All `Draw` overloads, sort modes, rotation/flip/scale/crop | ✅ | not separately re-audited (Task 861) | not separately re-audited | ✅ (2 real bugs fixed: rotation pivot, `transformMatrix`) | 🟨 destination-rect placement + `SpriteEffects::FlipHorizontally` pixel-verified (`plan_dx.md` `DX-70`); rotation/scale/crop/sort-mode not separately pixel-tested | 🟨 same 2 aspects pixel-verified off-screen (`DX-112`, Checks R2/R3); rotation/scale/crop/sort-mode not separately tested |
| Custom `Effect` via `Begin(effect)` | ✅ | ✅ | ✅ | ❌ throws by design (no shader stage, 2D-only backend) | ✅ real color-inversion custom-HLSL pixel test (`DX-71`) | ⬜ not built — `D3D12SpriteBatchBackend` only draws through the stock `sprite2d` pipeline, no `DX-71` equivalent |
| SpriteFont — glyph placement/spacing/newline/fallback/flip | ✅ pixel-verified (Tasks 424-429) | not separately re-audited (Task 861) | not separately re-audited | ✅ (1 real cross-backend bug found and fixed, Task 694) | ⬜ not attempted — builds on already-tested `Texture2D`/`SpriteBatch` but no D3D11-specific test yet | ⬜ not attempted, same reason |
| `TextureAddressMode::Wrap`/`Mirror` via SpriteBatch | ✅ | ✅ | ✅ | ⛔ **BLOCKED** (Tasks 686/687) | ✅ real `D3D11SamplerCache`-backed Wrap/Mirror with discriminating probe pixels (`DX-72`) | ⬜ not built — samplers are hardcoded static `WRAP`/linear in `D3D12RootSignatureCache`, not driven by XNA `SamplerState` at all yet; no D3D12 equivalent of `D3D11SamplerCache` exists |

## Stock Effects

| Feature | EasyGL | Vulkan | Bgfx | D3D11 | D3D12 |
|---|---|---|---|---|---|
| BasicEffect core (MVP, lighting, texture, vertex color) | ✅ | ✅ | ✅ | ✅ `colored3d`/`textured3d`/`colored_textured3d` all real, pixel-verified (`DX-61`/`DX-62`) | ✅ all 3 real, pixel-verified off-screen, same DXBC as D3D11 (`DX-111`, Checks M/N) |
| BasicEffect `DirectionalLight1`/`2` + `EmissiveColor` | ✅ | ✅ | ✅ | 🟨 fields present in `D3DLightingConstants`/HLSL, single-light lit-vs-unlit difference pixel-verified (`DX-63`); no dedicated multi-light/emissive discriminating test | 🟨 same single-light lit-vs-unlit proof (Check O2); no dedicated multi-light/emissive test |
| BasicEffect real specular highlights (`SpecularColor`/`Power`) | ✅ | ✅ | ✅ | 🟨 implemented in HLSL; the lit pixel test deliberately zeroes specular for CPU-comparison determinism, so specular itself is unverified | 🟨 same gap — same HLSL/DXBC, same determinism-driven zeroing |
| AlphaTestEffect core + fog | ✅ | ✅ | ✅ | ✅ real `clip()` discard + pass-case exact color incl. alpha byte, both pixel-verified (`DX-64`); fog wired (`DX-69`) | 🟨 real `clip()` discard + pass-case pixel-verified off-screen (Checks P1/P2); fog constant buffer not confirmed wired for this specific variant, only `colored3d`'s bundle has a dedicated fog test (Check V, `DX-113`) |
| AlphaTestEffect `VertexColorEnabled` | ✅ | ❌ (Task 887) | ❌ (Task 887) | ⬜ not separately tested | ⬜ not separately tested |
| DualTextureEffect core + fog | ✅ | ✅ | ✅ | ✅ two real SRVs/samplers pixel-verified (`DX-65`); fog wired (`DX-69`) | 🟨 two-texture combine pixel-verified off-screen (`DX-111`, Check Q1/Q2, incl. a real descriptor-table binding bug found and fixed along the way); fog not dedicated-tested for this variant |
| DualTextureEffect `VertexColorEnabled` | ❌ (Task 889) | ❌ | ❌ | ⬜ not separately tested | ⬜ not separately tested |
| EnvironmentMapEffect core/Fresnel/reflection | ✅ | ✅ | ✅ | ✅ real `TextureCube` SRV, reflection geometrically constrained into a known cube face, pixel-verified (`DX-66`) | ✅ same geometrically-constrained-reflection methodology, real `D3D12TextureCubeBackend` (new for this backend), pixel-verified off-screen (`DX-111`, Check U1) |
| EnvironmentMapEffect `DirectionalLight1`/`2` | ✅ fixed (Task 890, 2026-07-11) | ✅ fixed (Task 890) | ✅ fixed (Task 890) | 🟨 shares `D3DLightingConstants` wiring with BasicEffect; no dedicated test | 🟨 same — no dedicated test |
| EnvironmentMapEffect base-lerp alpha scaling | ✅ fixed (Task 891) | ✅ fixed (Task 891) | ✅ fixed (Task 891) | ⬜ not separately tested | ⬜ not separately tested |
| SkinnedEffect core (72-bone GPU skinning) | ✅ | ✅ | ✅ | ✅ real `D3DBoneConstants` populated from `GpuDrawParams::boneTransforms`, traced against `SkinnedEffect::SetBoneTransforms()` to rule out a transpose bug (`DX-67`) | ✅ real `D3DBoneConstants` populated the same way (direct, unmodified port of D3D11's own field population), single-identity-bone pixel-verified off-screen (`DX-111`, Check S1) |
| SkinnedEffect `DirectionalLight1`/`2` | ✅ fixed (Task 893, 2026-07-11) | ✅ fixed (Task 893) | ✅ fixed (Task 893) | 🟨 no dedicated test | 🟨 no dedicated test |
| SkinnedEffect `SpecularColor`/`SpecularPower` | ✅ fixed (Task 894, 2026-07-11) | ✅ fixed (Task 894) | ✅ fixed (Task 894) | 🟨 same specular-determinism gap as BasicEffect | 🟨 same gap |
| SkinnedEffect `WeightsPerVertex` GPU enforcement | ✅ fixed (Task 895, 2026-07-11) | ✅ fixed (Task 895) | ✅ fixed (Task 895) | 🟨 implemented, not independently pixel-tested per weight count | 🟨 same — implemented, not independently tested per weight count |
| Fog, all applicable effects/pipelines | ✅ | ✅ | ✅ | 🟨 wired for all 8 fog-capable variants (`DX-69`); dedicated fog-on/off discriminating pixel test only exists for `colored3d` (Check AC, `DX-81`) | 🟨 narrower than D3D11 — dedicated fog-on/off pixel test only exists for the `colored3d` bundle (Check V, `DX-113`); fog-constant-buffer wiring not confirmed/tested for the other 7 fog-capable variants |
| ShaderEffect (custom shader source) | ✅ (GLSL) | ✅ (SPIR-V) | ❌ `CreateEffectBackend` returns `nullptr` | ✅ (HLSL, runtime `D3DCompile()` via `D3D11EffectBackend`, `DX-58`, incl. a broken-shader failure-path check) | ⬜ not built — no D3D12 equivalent of `D3D11EffectBackend`/runtime `D3DCompile()` path exists |

Note: several per-effect `docs/*-support.md` files (e.g. `basiceffect-support.md`) predate Tasks
885-900's fog/lighting/specular fixes on Vulkan/Bgfx and still show some of these rows as gaps —
this matrix reflects the current, post-fix state; those individual docs are due for a refresh but
not rewritten here (out of this task's own scope).

## RenderTarget / MSAA / mip / depth

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D11 | D3D12 |
|---|---|---|---|---|---|---|
| `RenderTarget2D`/`RenderTargetCube`/MRT construction | ✅ | ✅ | ✅ | ✅ (MRT count > 1 throws by design, Task 709) | 🟨 `RenderTarget2D` bind+clear+readback+unbind-restores-backbuffer pixel-verified; real 2-target MRT bind+independent-clear verified via one `OMSetRenderTargets` call (`plan_dx.md` `DX-43`/`DX-46`); `RenderTargetCube` construction real but not independently pixel-tested; per-target MSAA-resolve/mip-regen-on-unbind only wired for the single-target path | ⬜ **no public `D3D12RenderTargetBackend` exists at all** — off-screen draws use a minimal, test-only `BindOffscreenColorTargetEXT()` helper, not a real `IRenderTargetBackend` implementation; the real XNA `RenderTarget2D`/`RenderTargetCube`/MRT API does not work against this backend yet (`DX-109`'s own honest triage) |
| MSAA (both RT types) | ✅ | ✅ | ✅ (`Bgfx_RenderTarget2D_MsaaResolve` fails only under this session's Xvfb/no-DRI3 sandbox, not a code bug) | N/A (2D-only, no AA needed) | 🟨 `RenderTarget2D` 4x MSAA clear+resolve pixel-verified, device-queried via `CheckMultisampleQualityLevels` (`DX-45`); `RenderTargetCube` MSAA not separately tested | ⬜ not built — no render-target backend to attach MSAA to yet |
| Mip chains (both RT types) | ✅ | ✅ | ✅ | N/A | ⬜ not attempted — texture upload/readback tested at mip level 0 only | ⬜ not attempted, same reason |
| Per-instance `DepthStencilFormat` fidelity | ✅ | ✅ (Task 911) | ✅ | ⚠️ emulated (echoes the requested format back, no real backing storage) | ⬜ not separately tested | ⬜ not separately tested — no render-target backend exists to carry a `DepthStencilFormat` yet |

### Bgfx MRT attachment limits (Task 775)

`GraphicsDevice::SetRenderTargets` throws `std::invalid_argument` above **4** targets in shared C++
code (`MAX_RENDERTARGET_BINDINGS`, Task 881) — mirroring FNA's own real
`internal const int MAX_RENDERTARGET_BINDINGS = 4` cap — before any backend ever sees the call, so
this is the practical, enforced limit on Bgfx (and every other backend) regardless of what the
underlying device itself could support. `BgfxGraphicsBackend::SetRenderTargets`'s own MRT
framebuffer-construction path (`BgfxGraphicsBackend.cpp`) separately caps at a local
`kMaxAttachments = 8`, matching bgfx's own `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS` compile-time
default — unreachable in practice today since the shared 4-target gate rejects anything larger
first, kept only as defense-in-depth (same reasoning as Task 881's own EasyGL/Bgfx ad-hoc-cap
notes). The real device capability, `bgfx::getCaps()->limits.maxFBAttachments`, is logged at
startup (Task 456) and is typically 8 on desktop GL/Vulkan hardware — always ≥ the FNA-mandated 4,
so it has never been the binding constraint in this project.

## Texture2D / Texture3D / TextureCube

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D11 | D3D12 |
|---|---|---|---|---|---|---|
| Texture2D `SetData`/`GetData`/`FromStream`/`SaveAsPng`/NPOT | ✅ | ✅ | ✅ | ✅ (4 real bugs found and fixed) | 🟨 `SetData`/`GetData` byte-exact GPU round-trip verified (`DX-40`); `FromStream`/`SaveAsPng`/NPOT not separately tested | 🟨 `SetData`/`GetData`/`UpdatePixels` byte-exact round-trip verified via explicit upload-heap staging (`DX-109`, Check K); `FromStream`/`SaveAsPng`/NPOT not separately tested |
| Texture2D mip-level `SetData` (level > 0) | ✅ | ❌ silent no-op (Task 867) | ❌ silent no-op (Task 867) | ❌ throws by design (Task 681) | 🟨 `UpdatePixelsLevel` implemented (`DX-40`), not independently pixel-tested | ⬜ not separately tested — texture upload/readback tested at mip level 0 only |
| Texture3D/TextureCube `SetData`/`GetData`, incl. mip | ✅ | ✅ | ✅ (needed a new `GetData` readback path, Task 914) | ⛔ **BLOCKED** — construction succeeds silently with a null backend, 94-test blast radius (Task 725) | ✅ byte-exact GPU round-trip verified for both types at mip 0 (`DX-41`/`DX-42`) | 🟨/⬜ split: `TextureCube` `SetData` real (`DX-111`, needed by `env_map3d`), but `GetData()` is a no-op (interface default) — narrower than D3D11's real readback; **`Texture3D` has no D3D12 backend at all**, explicitly triaged out (`DX-109`) |
| Texture3D/TextureCube sampled in shaders | ❌ don't inherit `Texture` (Task 863, architectural) | ❌ | ❌ | N/A | 🟨 `TextureCube` sampled and pixel-verified via `env_map3d` (`DX-66`); `Texture3D` has no consuming shader variant, unverified in a shader | 🟨 `TextureCube` sampled and pixel-verified via `env_map3d` (`DX-111`, Check U1); `Texture3D` has no D3D12 backend or consuming shader variant at all |
| Non-`Color` `SurfaceFormat` for real GPU texture data | ⛔ **BLOCKED** (Task 732) | same shared-code limitation | same | same | same shared-code limitation | same shared-code limitation |

## GraphicsDevice state objects

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D11 | D3D12 |
|---|---|---|---|---|---|---|
| `BlendState` (all presets + custom factors/equations) | ✅ | ✅ **FIXED (Task 868, 2026-07-09)** — real per-`Blend`/`BlendFunction` mapping across all 9 3D pipeline-creation sites; was "almost entirely fake" (hardcoded one blend equation regardless of request, confirmed 5× via pixel tests) before this fix | ✅ | ✅ (2 real bugs fixed) | ✅ real cached `ID3D11BlendState`, `Opaque`/`AlphaBlend` pixel-behavior-verified (`DX-50`/`DX-82`) | ⬜ **not applicable yet** — D3D12 bakes blend state directly into each PSO description; no runtime-settable blend-state object or `BlendState`→PSO-desc-key mapping exists (`DX-113`'s own audit confirmed this genuinely waits on a future task, not a coverage gap) |
| `DepthStencilState` (compare func + full stencil ops) | ✅ | ✅ (Task 870 — real per-pipeline compare-op + stencil) | not separately re-confirmed this pass | ✅ never throws (deliberate no-op, matches FNA's backend-agnostic-until-drawn model) | ✅ real cached `ID3D11DepthStencilState`, stencil-enable gating pixel-verified (`DX-51`/`DX-82`) | ⬜ not applicable yet — same PSO-baked-state gap as `BlendState`; every PSO hardcodes `depthEnable=false` |
| `RasterizerState` | ✅ | not separately re-confirmed this pass | not separately re-confirmed this pass | ✅ never throws | ✅ real cached `ID3D11RasterizerState`, `CullMode` winding-order pixel-verified (`DX-52`/`DX-82`); depth-bias unit convention (float→rounded `INT`) documented, not itself pixel-tested | ⬜ not applicable yet — same PSO-baked-state gap; every PSO hardcodes `cullMode=None` |
| Per-slot `SamplerState` (16 slots) | ✅ | ✅ | ✅ | ✅ (1 real bug fixed) | 🟨 real cache with identity/distinctness proof + Wrap/Mirror pixel-verified via SpriteBatch (`DX-44`/`DX-72`); not tested across all 16 slots simultaneously | ⬜ not built — samplers are hardcoded static `D3D12_FILTER_MIN_MAG_MIP_LINEAR`+`WRAP` descriptors baked into the root signature (`D3D12RootSignatureCache::MakeDefaultStaticSampler`), not driven by XNA `SamplerState` at all |
| `GraphicsDevice.ReferenceStencil` | ❌ **no backend connection** (Task 872, open) | ✅ **FIXED** — connected via `vkCmdSetStencilReference`, an undocumented side effect of Task 870 (corrected 2026-07-09) | ❌ **no backend connection** (Task 872, open) | N/A | ✅ real `OMSetDepthStencilState` re-bind on `SetReferenceStencil()`, verified (`DX-52`) | ⬜ not applicable — no depth-stencil-state object exists to carry a reference value |
| `Clear` honors `ClearOptions::Stencil` | ❌ **ignored, all 3** (Task 871, open) | ❌ | ❌ | ⚠️ emulated | 🟨 real `ClearDepthStencilView` calls implemented for all 5 combo variants (`DX-25`); only plain `Clear(r,g,b,a)` has a dedicated round-trip pixel test | ⬜ not attempted — no DSV is bound in any current off-screen test (`depthEnable=false` throughout), so depth/stencil `Clear` combos have no target to exercise against yet |

### Vulkan optional device-feature gating (Task 454)

Investigated whether Vulkan's `VkPhysicalDeviceFeatures`-gated optional capabilities are requested
safely (a device that doesn't support a requested optional feature makes `vkCreateDevice` fail
outright, unlike GL/bgfx's more forgiving capability model). Confirmed the device-creation code
(`VulkanGraphicsBackend`'s constructor) only ever requests the 2 optional features CNA actually
uses — `fillModeNonSolid` (`FillMode::WireFrame`) and `samplerAnisotropy` (anisotropic texture
filtering) — and both are correctly gated behind a real `vkGetPhysicalDeviceFeatures` query first
(`if (supported.fillModeNonSolid) { feat.fillModeNonSolid = VK_TRUE; fillModeNonSolidSupported_ =
true; }`, same shape for `samplerAnisotropy`). Neither is ever unconditionally requested. Downstream
usage sites correctly gate on the resulting `fillModeNonSolidSupported_`/`anisotropySupported_`
flags (e.g. `fillModeWireframe_ = (fillMode == 1) && fillModeNonSolidSupported_` — a device without
`fillModeNonSolid` silently falls back to solid fill rather than requesting an invalid pipeline
state), and `maxSamplerAnisotropy_` is read from real `VkPhysicalDeviceLimits` and used to clamp any
requested anisotropy level. MSAA sample-count selection (`PickSampleCount`) also respects the
device's real `framebufferColorSampleCounts` limit, picking the best available count ≤ the
requested one rather than assuming an arbitrary count is always supported. **No gap found** — this
was already correctly implemented, just not previously documented anywhere; recorded here per Task
454's own "throw or document fallback behavior" framing (this backend's own answer is "gracefully
falls back," which is the idiomatic Vulkan pattern for optional features, not a bug needing a fix).

## OcclusionQuery (Phase 50, closed this session — see `docs/occlusionquery-support.md` for full detail)

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D11 | D3D12 |
|---|---|---|---|---|---|---|
| Wired to real GPU work (`Begin`/`End`) | ✅ | ✅ **FIXED (Task 447, 2026-07-10)** — real per-draw-call query correlation via `Pending3DDraw::occlusionQuery` tagging + `vkCmdBeginQuery`/`vkCmdEndQuery` recording in `RecordCommandBuffer()` | ✅ (Task 448) | N/A — throws at construction (Task 727) | ✅ real `ID3D11Query(D3D11_QUERY_OCCLUSION)`, `Begin`/`End`/`GetData` wired (`DX-47`) | ⬜ **not built at all** — Phase DX12's task list has no `ID3D12Query`-based occlusion-query task; `CreateOcclusionQuery()` falls through to `IGraphicsBackend`'s own silent `nullptr` default |
| Pixel/query correctness (visible vs. occluded) | ✅ verified both directions (Tasks 445/446) | ✅ verified both directions, plus a multi-draw-span check (Task 854) — this sandbox's software Vulkan driver (Mesa Lavapipe) reports fully accurate, discriminating pixel counts (4096 visible / 0 occluded on a 64×64 quad) | ⚠️ can't verify in this sandbox's software GL2.1 driver; dedicated-view architecture gap open (Task 917) | N/A | 🟨 a real completing query verified; not confirmed both-directions (visible vs. occluded) discriminating like EasyGL/Vulkan | N/A — no query support exists |

## Model (Phase 49, closed this session — see `docs/model-content-pipeline-support.md` for full detail)

| Feature | Status | D3D11 | D3D12 |
|---|---|---|---|
| Runtime API (`Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone`) | ✅ fully audited/FNA-faithful, several real bugs found and fixed (Tasks 431-439) | ⬜ not separately tested against this backend | ⬜ not separately tested against this backend |
| Content-pipeline loading (`ModelTypeReader`) | ⚠️ real gaps — no bone hierarchy, no `ParentBone` wiring, no `BoundingSphere`/`Tag`, custom `.model.json` format is not `.xnb`-compatible (Task 440); zero test coverage of the loader itself | ⬜ not separately tested against this backend | ⬜ not separately tested against this backend |
| `Model` constructor root-bone-index flexibility | ✅ fixed (Task 916, 2026-07-09) | ⬜ not separately tested against this backend | ⬜ not separately tested against this backend |

Note: `cna_reference_dump`/`cna_demo_2d` (both `Model`-adjacent example binaries) fail to *link*
under `D3D11` (`undefined reference to Effect::Apply()`) — found during `plan_dx.md` `DX-81`'s
coverage audit and confirmed via `git stash` to pre-date the D3D11 backend work entirely (fails
identically on the base commit); a real, pre-existing, unrelated gap, not caused or fixed by this
backend. See `docs/d3d11-backend.md`. The same link failure applies equally under `D3D12` (same
root cause, same example binaries, same shared code) — not independently re-confirmed but expected
identical, see `docs/d3d12-backend.md`.

## Every currently-BLOCKED task (⛔)

| Task | Backend | One-line reason |
|---|---|---|
| 686 | SDL_Renderer | `TextureAddressMode::Wrap` via `SpriteBatch` — no native support in the `Draw()` path used; 3 options (throw / rewrite to `SDL_RenderGeometry` / hybrid), none picked |
| 687 | SDL_Renderer | Same underlying constraint as 686, for `Mirror` — resolving 686 resolves this too |
| 725 | SDL_Renderer | `Texture3D`/`TextureCube` construction succeeds silently with a null backend; 94 existing tests rely on that silent-success behavior, so fixing needs a blast-radius-aware architecture decision |
| 732 | EasyGL | Real `SurfaceFormat` GPU forwarding conflicts with an already-shipped, already-tested `Texture::ValidateFormat` contract (Task 176) plus the public `SetData`/`GetData` API being `Color*`-only |

## Known pre-existing test-failure baseline, per backend

Confirmed most recently by this session's own regression runs (always run sequentially per
backend, never concurrently — concurrent runs have previously produced transient GPU/driver-
contention false failures):

- **EasyGL**: 3 — `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`,
  `easy-gl-resource-smoke-tests`. Reconfirmed as recently as Task 449's own regression (4510/4513).
- **Bgfx**: **current baseline per `NEXT.md` (verified 2026-07-11): `CnaTests` 4375/4377 (2
  hardware skips), `ctest` 103/105 — 2 remaining failures**: `Bgfx_RenderTarget2D_MsaaResolve`
  (this sandbox's Xvfb has no DRI3 support — an environment limitation, not a code bug) and
  `Bgfx_RenderTargetCube_DepthFormat` (Task 952, **DEFERRED** — a `Depth24Stencil8`-attached
  `RenderTargetCube` face produces no colour output; investigated 3 times, root cause not yet
  found). Task 951 (closed 2026-07-11) fixed 5 of the 6 pre-existing `RenderTarget2D`/
  `RenderTargetCube` `glReadPixels`/Xvfb crashes that used to be counted here (`DepthBuffer`,
  `MipChain` ×2, plus others) via a dedicated highest-id "flush" view — see `NEXT.md` §3/§5 for the
  full root-cause writeup. `Bgfx_ModelJsonReader_Quad` (Task 927/948) is also fixed and passes 2/2.
- **Vulkan**: **current baseline per `NEXT.md` (verified 2026-07-11): `CnaTests` 4371/4373 (2
  hardware skips), `ctest` 126/127 — 1 remaining failure, `Vulkan_DepthBias`.** Both the 5×
  `BlendState` failures (Task 868, fixed 2026-07-09, commit `459a0e37`) and the 3
  `ContentManagerSkinnedModelTest.*` segfaults (Task 953, fixed 2026-07-11) that used to make up
  this baseline are gone — no exclusions needed anymore. **Historical correction (2026-07-09,
  Task 861):** this row previously said "12" pre-existing failures and additionally claimed
  "several `DepthStencilState`-adjacent" ones — both wrong; `DepthStencilState`'s own
  compare-op/stencil-op tests all pass (Task 870 fixed this).
- **SDL_Renderer**: 13 known pre-existing, all throwing `"SDL_Renderer does not support 3D"` —
  matches this backend's accepted 2D-only architectural scope exactly (`EffectApplyTest`,
  `GraphicsDeviceValidationTest.SetRenderTargets_*`, `SkinnedModelEXTPartTest.*`,
  `ContentManagerSkinnedModelTest.*`). Confirmed via Task 915's own systematic full-suite run.

## Remaining genuine Vulkan limitations (Task 861, 2026-07-09)

Phase 73 (Tasks 664-665, 825-861) was written as a checklist of individual Vulkan pixel-test tasks,
but Tasks 825-860 were never checked off — later, higher-numbered work (Tasks 484/495/499/500,
plus the fog/lighting/effect fixes at 885-900) independently established most of the same ground,
superseding the original per-row checklist without formally closing each row. This section is
Task 861's real deliverable: the actual current state, confirmed by spot-checking a representative
sample of Tasks 825-860 against real test coverage rather than re-verifying all 36 rows from
scratch (that would be Task 738-scale work, out of this task's own scope).

**Genuinely already covered by real, current Vulkan tests** (confirmed via
`ctest --test-dir cmake-build-vulkan -N -R "^Vulkan_"`, 93 real tests): `TextureAddressMode`
(Clamp/Mirror), `TextureFilter` (Point vs. Linear), anisotropic filtering, all 7 `BlendState`
presets, all 6 `DepthStencilState` aspects, `CullMode`, `Viewport`, render-target lifecycle
(sample-after-unbind, MSAA, mip chains, depth-format fidelity, MRT-adjacent), and all 5 stock
effects including fog and several per-effect sub-features (specular, Fresnel, eye position, bone
blending) — this maps directly onto Tasks 825-849's own topics. **Confirmed genuine bugs found by
this later work**: `BlendState` (Task 868, **fixed 2026-07-09**), one isolated
`RasterizerState.DepthBias` sub-case (still open) — these are the real content behind Tasks 831-833
and 839's own topics, not clean passes.

**A real, previously-undocumented gap found by this spot-check**: unlike EasyGL and SDL_Renderer,
Vulkan has **no dedicated pixel test** for `SpriteBatch`'s sort-mode ordering, rotation/scale/
source-rectangle-cropping/`SpriteEffects` flip (Task 851/850's own topics), `SpriteFont` glyph
placement (Task 852), or `Model` multi-mesh hierarchy transform propagation (Task 853) —
confirmed via `grep`/`ctest -N` finding zero `Vulkan_SpriteFont*`/`Vulkan_Model*`/
`Vulkan_SpriteSortMode*` test names, despite `Vulkan_SpriteBatch_MultiBeginEnd` and
`Vulkan_Demo2D_SmokeTest` confirming basic `SpriteBatch` drawing works. Corrected the feature
matrix's own "2D SpriteBatch/SpriteFont" table above, which previously (incorrectly) rated Vulkan
✅ for this without a backing test, to "not separately re-audited" — matching the honest phrasing
already used for the adjacent SpriteFont row. This is a **test-coverage gap, not a confirmed
behavioral bug** — the underlying `SpriteBatch`/`SpriteFont`/`Model` code is backend-agnostic C++
already pixel-verified on EasyGL/SDL_Renderer, so a regression specifically on Vulkan is unlikely,
but it is genuinely unverified there. Not opened as a new numbered task here (that's Task 738-scale
triage work); flagging it in this matrix is this task's own real scope.

**`OcclusionQuery` visible-vs-occluded pixel test on Vulkan (Task 854's own topic)**: **FIXED,
2026-07-10** — was Task 447's ⛔ BLOCKED status (Vulkan's deferred-draw architecture couldn't
correlate a query's Begin/End span with a draw at all); now resolved via real per-draw-call query
tagging and `vkCmdBeginQuery`/`vkCmdEndQuery` recording, see the `OcclusionQuery` table above.

**Bottom line**: Vulkan's real, current, confirmed-open limitation is exactly 1 — the isolated
`RasterizerState.DepthBias` sub-case. `BlendState` (Task 868) is fixed as of 2026-07-09, and
`ReferenceStencil` is fixed on Vulkan specifically (an undocumented side effect of Task 870); the
`ReferenceStencil` gap (Task 872) remains open only on EasyGL and Bgfx. The 2D
SpriteBatch/SpriteFont/Model-hierarchy test-coverage gap above is real but distinct in kind
(untested, not un-implemented or known-broken). `OcclusionQuery` (Task 447/854) is no longer on this
list — fixed in full.

## Remaining genuine Bgfx limitations (Task 824, 2026-07-10)

Phase 72 (Bgfx full 2D+3D pixel-verified parity, Tasks 740-824) is now closed in full: of the
original 38 confirmed real gaps found in a first-ever complete row-by-row triage, 37 are ✅ closed
this session and exactly 1 remains open, explicitly flagged (not silently skipped). Three genuine,
confirmed limitations survive this closure — none of them a code bug in this project, each already
root-caused rather than merely observed:

- **Constant `DepthBias` (Task 767): FIXED, 2026-07-10.** Project-owner decision received: bgfx's
  high-level state API has zero depth-bias/polygon-offset mechanism anywhere (confirmed via
  `bgfx/defines.h` — no `BIAS`/`OFFSET` flag exists — and the vendored `renderer_gl.cpp` — no
  `glPolygonOffset` call in the whole file), so constant `DepthBias` is now emulated via a per-draw
  vertex-shader Z-offset (`BgfxGraphicsBackend::SetDepthBiasUniform`, a new `u_depthBias` uniform
  added to every 3D vertex shader, scaled by `kDepthBiasScale` to roughly match the visual magnitude
  a real GL/Vulkan polygon-offset implementation would produce). New `Bgfx_RasterizerState_DepthBias`
  test confirms both the zero-bias baseline (stays RED) and a large negative bias (pulls a coplanar
  redraw in front, turns GREEN); verified via `git stash` revert-and-rebuild.
  **`SlopeScaleDepthBias` remains an intentionally undone gap** (project-owner decision, not
  attempted): a true per-fragment screen-space-slope computation would force every 3D shader off the
  early-Z path, even at `DepthBias=0`, unless duplicate shader variants were added — not worth the
  cost for this one property. EasyGL, by contrast, needed no shader emulation at all: it already had
  real `glPolygonOffset` support in the vendored `easy-gl` library, just never wired up — fixed with
  a native call, covering both constant and slope-scale bias in one shot (see Task 767's own
  `plan_graphics.md` entry). Only Vulkan implements real hardware depth bias (`vkCmdSetDepthBias`,
  dynamic state, including real slope-scale).
- ~~**`RenderTarget2D`/`RenderTargetCube` `glReadPixels` crashes under this sandbox's software GL
  driver**~~ (`Bgfx_RenderTarget2D_DepthBuffer`/`MsaaResolve`/`MipChain`,
  `Bgfx_RenderTargetCube_MipChain`/`MsaaResolve`/`DepthFormat` — 6 tests, all pre-existing at the
  time this section was written) — **5 of these 6 fixed by Task 951 (closed 2026-07-11)**, root
  cause: bgfx processes views in ascending id order each frame, so any render-target view was
  always last-processed and still GL-bound when `glReadPixels()` fired; fixed via a dedicated,
  always-last-processed "flush" view touched right before every screenshot request. Only
  `Bgfx_RenderTargetCube_DepthFormat` remains open (Task 952, **DEFERRED** — a genuinely different,
  still-unsolved root cause: a `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour
  output at all, not a crash). `Bgfx_RenderTarget2D_MsaaResolve` also remains, but as a real
  environment ceiling (no DRI3 in this sandbox), not part of Task 951's crash class. See `NEXT.md`
  §5 for the current, authoritative 2-failure baseline.
- **`OcclusionQuery.PixelCount()` doesn't discriminate visible from occluded geometry in this
  sandbox** (Tasks 814/815): a dedicated scratch probe confirmed the exact same numeric value is
  returned regardless of scene content, extending Task 448's own already-documented finding
  (`IsComplete()`/`PixelCount()` can't distinguish a wired-up query from a never-submitted one) to
  the actual pixel-count magnitude too — the underlying rendering/depth-occlusion behavior each
  scenario depends on IS reliably pixel-verified instead (`Bgfx_OcclusionQuery_PixelCount`'s own 2
  real, sabotage-verified checks). Same software-renderer ceiling as above, not a CNA defect.

**FIXED, 2026-07-10 (Task 927/948): `BgfxGraphicsBackend` never overrode `DrawIndexedPrimitivesEx`**
— was a **real CNA gap**, not an environment limitation, first flagged as an "adjacent, out-of-scope
discovery" by Task 766 and concretely reproduced by `Bgfx_ModelJsonReader_Quad` (previously a
documented 1/2 known failure). Any indexed, `Effect`-bound draw with a vertex format lacking a
`Color` attribute (`VertexPositionNormalTexture`/`VertexPositionTexture` — i.e. any
`Content.Load<Model>()`-loaded mesh) silently fell back to the base `IGraphicsBackend`'s default
`DrawIndexedPrimitivesEx`, which discarded `GpuDrawParams` entirely and rendered via the `colored3D`
pipeline instead — reading an unbound `a_color0` attribute (GL default `(0,0,0,1)`), so the mesh
rendered solid black regardless of its real `DiffuseColor`/texture/lighting. Fixed by adding a real
`BgfxGraphicsBackend::DrawIndexedPrimitivesEx` override mirroring `DrawPrimitivesEx`'s own full
`GpuDrawParams` dispatch; `Bgfx_ModelJsonReader_Quad` now passes 2/2.

**Bottom line**: Bgfx's only remaining code-level gap is Task 767 (depth bias), already flagged for
a project-owner decision; the other 2 items are sandbox/environment ceilings, already root-caused,
that would need a real GPU-backed test environment to resolve or re-confirm — not further Bgfx
backend code work.

## New tracked follow-up tasks opened this session

- ~~**Task 916**~~ — **fixed, 2026-07-09** (same day it was opened): `Model`'s constructor used to
  auto-default `Root` to `bones[0]` with no way to specify a different root bone index; an optional
  `rootBoneIndex` parameter now covers it (low-risk, purely-additive fix).
- **Task 917** — Bgfx occlusion queries share a view/depth buffer with other same-frame geometry
  instead of using bgfx's own dedicated-measurement-view pattern; needed for true scene-depth
  query correctness (deferred, not blocked, can't be verified in this sandbox anyway).

## See also

- `docs/coverage.md` — non-Graphics namespace estimates (Audio/Media/Content/Net/GamerServices),
  still broadly accurate; its own Graphics section is superseded by this document.
- `docs/sdl-renderer-2d-completeness.md` — SDL_Renderer's own full Phase 70 audit in verbose detail.
- `docs/model-content-pipeline-support.md`, `docs/occlusionquery-support.md` — full detail for
  those 2 systems, summarized above.
- Per-effect docs (`docs/basiceffect-support.md` etc.) — largely predate Tasks 885-900's
  fog/lighting/specular fixes on Vulkan/Bgfx; this matrix reflects the current state, those
  individual docs have not been refreshed (out of this task's own scope).
