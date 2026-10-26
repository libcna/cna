# FNA3D renderer

`-DCNA_GRAPHICS_RENDERER=FNA3D` selects CNA's FNA3D renderer: XNA's programming model rendered
through **FNA3D**, the C graphics library FNA itself renders through.

## glTF metallic-roughness materials

`PbrEffect` and `SkinnedPbrEffect` use CNA-owned Effect Framework binaries compiled from the same
Shader Model 3 sources as the DirectX 9 renderer. The rigid and skinned paths evaluate the GGX
metallic-roughness BRDF, normal/metallic-roughness/emissive/occlusion/specular maps, texture
transforms, sRGB conversion, vertex colour and alpha, three directional lights, one point/spot
light, fog and skinning. Optional maps receive explicit white or flat-normal fallbacks. A valid
`ImageBasedLightEXT` replaces flat ambient with the same split-sum irradiance, prefiltered-cube
and BRDF-LUT equation used by EasyGL, Software and WebGPU, including explicit roughness mip LOD.

Double-sided materials reverse the complete tangent basis on a back face, including under a
mirroring World transform; every FNA3D driver hands the shader Direct3D 9's clockwise-positive
`VFACE` (OpenGL flips `gl_FrontFacing` to it since the 2026-10-26 audit, D-01; D3D11 takes
clockwise front faces since its review, R8-01). A zero normal or tangent falls back as EasyGL's does.
Material maps independently select UV0 or UV1 before their own affine
texture transform. The CNA-owned compiled effects sample the shared directional/cascaded,
point-cube and spot-map shadow contract across Basic, Skinned, PBR and SkinnedPBR. Custom shader
*source* is still unsupported, which is a separate capability from these committed compiled
effects.

Until 2026-08-18, FNA3D silently shaded PBR draws with Basic/Skinned. GLTF-477 changed that to an
honest refusal. The native path above now supersedes the refusal and is pixel-verified on both the
OpenGL and SDL_GPU/Vulkan drivers: analytic rigid/skinned BRDF 5/5, vertex colour 5/5, padded
declarations 3/3, and a 1,600-draw uniform ring 1,600/1,600. The shared IBL pixel oracle additionally
checks ambient replacement, analytic diffuse/specular split-sum output, intensity, mip selection,
sampler-controlled mip interpolation and the skinned path. The shared public double-sided oracle
adds five front/back/mirrored cases apiece for rigid and skinned PBR. The shared material-map
oracle adds 21 rigid/skinned checks, including independent base-colour UV0 and emissive UV1.

## Identity

HiDef vertex textures support `SurfaceFormat::Single` and `Vector4`. The Single
format uses binary32 storage, including ordinary 2D and cube textures;
uploads/readbacks do not convert it through 8-bit colour. FNA3D exposes no texture
component swizzle, so CNA stores ordinary Single 2D/cube textures as Vector4 with
missing channels set to one, as XNA samples them. This uses four times the native
storage but preserves every red-channel bit and the public Single transfer layout.
Regression coverage checks
vertex-slot sampling with PointClamp/PointWrap, exact 2D mip/rectangle transfers and
all six cube faces across mip levels. These checks pass on Linux with both FNA3D's
OpenGL and SDL_GPU/Vulkan drivers; this is not a new Windows or Apple qualification.

| Field | Value |
|---|---|
| Public identity | `FNA3D` (`GraphicsRendererType::Fna3d`) |
| Compile definition | `CNA_RENDERER_FNA3D` |
| Module | `modules/renderers/fna3d` |
| CMake target | `cna_renderer_fna3d` |
| Namespace | `CNA::Internal::Renderers::Fna3d` |
| Main class | `Fna3dRenderer` |
| Category | Portable RHI — not one native API |

## Upstream

| Field | Value |
|---|---|
| Repository | CNA's fork https://github.com/libcna/FNA3D (upstream https://github.com/FNA-XNA/FNA3D) |
| Pinned revision | `10f650b9d0f6bb0e6f3f9378833fbd807dfe8cde` on the fork's `cna` branch: release **26.08** (`32401479a3ab`) plus CNA's commits, merged with release **26.10** (`62a41708e975`), then the 2026-10-26 dependency audit's fixes and those of its review |
| License | zlib |
| Vendored submodule | MojoShader from CNA's fork https://github.com/libcna/mojoshader (upstream https://github.com/icculus/mojoshader), `03b686fe1a33` on its `cna` branch: `6333f74` plus CNA's commits, merged with upstream `main` `ad5dff8` (the revision 26.10 pins), then the 2026-10-26 audit's and review's fixes; built from FNA3D's own CMake |
| Dependencies | **SDL 3.2.0 or newer, and nothing else** — the very SDL3 CNA already vendors |
| Integration | `cmake/ThirdPartyFNA3D.cmake`, FetchContent at the pin (`CNA_FNA3D_GIT_REPOSITORY`, `CNA_FNA3D_GIT_TAG`), built as a static archive; nothing is patched at configure time |
| Offline builds | `-DFETCHCONTENT_SOURCE_DIR_FNA3D=/path/to/FNA3D`: a clone of the fork at the pin with its MojoShader submodule initialized (`git submodule update --init`). Configure warns when that checkout is at another commit or has local changes, and builds it as it is |

### The two forks

Both forks exist only to carry CNA's fixes as ordinary commits on a `cna` branch; their default
branches (`master`, `main`) mirror upstream. Upstream accepts no AI-written code, so the fixes are
not offered there. Both are linked statically into CNA only: they add entry points
(`FNA3D_SetXnaPixelCenterEXT`, `FNA3D_SetLegacyBumpMapEnvEXT`, `MOJOSHADER_glProgram*Info`, the
`MOJOSHADER_sdl*` hooks), render-state values and fields of public structures
(`MOJOSHADER_attribute`, `MOJOSHADER_effectState`), so neither is ABI-compatible with an upstream
shared library.

- **libcna/FNA3D** `cna`: the XNA/Direct3D 9 pixel-centre entry point (OpenGL, D3D11 and SDL_GPU
  drivers), OpenGL multisampled-target readback and cube mipmap resolve (FNA3D-54), per-call GL and
  MojoShader context activation for several live devices (FNA3D-51), the SDL_GPU multisampled cube
  face (FNA3D-56) — and the MojoShader submodule switched to the fork. Since the 2026-10-26
  dependency audit also: bounded, format-exact GetData on all three drivers, VFACE's XNA sign,
  `FNA3D_SetLegacyBumpMapEnvEXT`, Depth vertex usage, disposal at device destruction, tracing of
  the extension entry points, the installed `mojoshader.h` and package, a working
  `MOJOSHADER_STATIC_SPIRVCROSS`, and its own regression tests (`tests/cna`). Its review the same
  day added: readbacks that restore the context's pack state, OpenGL ES 3 reads in the
  implementation's own layout where that is the format's (`ColorSrgbEXT` is refused there: ES 3
  decodes sRGB to linear as it reads), SDL_GPU downloads copying only their rectangle and a
  checked `ReadBackbuffer`, pipeline-cache keys packed unsigned (blend states differing only in a
  blend function or the multisample mask no longer share one), freed SDL_GPU vertex-binding
  cache, dumpspirv effect table and trace lock, and tests for the backbuffer, Depth usage, the
  installed package, SDL 2 replay and trace round trips.
- **libcna/mojoshader** `cna`: 133 commits — malformed-bytecode hardening of the Effect and
  preshader parsers, Direct3D 9 shader-model validation, GLSL/GLSL ES, SPIR-V and Metal profile
  fixes (TEXCRD, vertex colour clamp, pixel centres, centroid, predication, precision), the SDL_GPU
  adapter hooks (static SDL_shadercross, SPIR-V transform, program identity, colour-target count)
  and the D3D11 bound-shader lifetime. Each commit names the CNA patch it was and the CNA commit that
  introduced it (`CNA-Patch:` trailer). One further commit adds the owner's `AGENTS.md` and
  `CLAUDE.md`. After the 26.10 merge come the 2026-10-26 audit's fixes, each naming its finding
  (`CNA-Audit-Finding:` trailer), and the fork's regression tests (`tests/cna`).

Before the upstream merges (`f4bef618d2c2` and `e6d80c19b3f6`), every source file of the two forks
is identical to what CNA's former configure-time patch series (137 files under `cmake/patches/`)
produced from FNA3D `3240147` and MojoShader `6333f74`; MojoShader's branch differs from that
result only by those two instruction files. The merges are separate commits on top. The MojoShader merge keeps CNA's `printFloat` (a superset of upstream's 32-bit
`long` fix) and takes upstream's HLSL `BLENDWEIGHT` pixel input.

**Following upstream.** In a clone of each fork, add upstream as a remote, merge its default
branch (or a release tag) into `cna` — a merge, never a rebase, so pinned commits stay reachable —
resolve conflicts in favour of the behaviour CNA's commits establish, and push. Push MojoShader
first, then commit its new gitlink in FNA3D (`git -C MojoShader checkout <sha>; git add MojoShader`)
and push FNA3D, then move `CNA_FNA3D_GIT_TAG` here. A pin is always a full commit SHA on the
fork, never a branch name.

**Testing an update** before the pin moves: configure a tree with
`-DFETCHCONTENT_SOURCE_DIR_FNA3D=<the candidate checkout>` and run the FNA3D, SDL_GPU, WebGPU and
GL compiled-effect suites (`Fna3dCompiledEffect*`, `SdlGpuCompiledEffect*`, `WebGPUCompiledEffect*`,
`CompiledEffect*`, `MojoShader*`, `RenderTargetSemantics.*`) per renderer, with `FNA3D_FORCE_DRIVER`
set to `OpenGL` and to `SDLGPU`, plus `cna_mojoshader_effect_probe` and `CnaFna3dSamplerLifetimeTests`
(see [Runtime renderer selection](runtime-renderer-selection.md) for running one multi-renderer
tree per renderer), and each fork's own `tests/cna` (`docs/development-environment.md`).

FNA3D's own API design is XNA 4.0: `FNA3D_DrawIndexedPrimitives`, `FNA3D_SetBlendState`,
`FNA3D_VerifySampler`, `FNA3D_SetRenderTargets`, `FNA3D_ResolveTarget` and the rest map to
`IGraphicsRenderer` almost one to one, and **every enumeration it exposes is numerically the XNA
enumeration CNA already ports** — pinned by `static_assert` in `Fna3dEnumMapping.hpp` so a future
divergence on either side is a compile error rather than a silently wrong blend mode.

## Actual graphics route

```
CNA game code
  -> Microsoft::Xna::Framework::Graphics (GraphicsDevice / SpriteBatch / BasicEffect / ...)
  -> CNA::Internal::Renderers::Fna3d::Fna3dRenderer   (IGraphicsRenderer)
  -> FNA3D                                            (chooses its driver at runtime)
  -> SDL_GPU  |  Direct3D 11  |  OpenGL
```

FNA3D tries its drivers in the order **SDL_GPU, Direct3D 11, OpenGL** and reports the SDL window
flags the chosen driver needs. Pin a driver with the `FNA3D_FORCE_DRIVER` SDL hint
(`OpenGL`, `D3D11`, `SDLGPU`, or the aliases `Vulkan`/`D3D12`/`Metal`, which FNA3D maps onto
SDL_GPU backends). CNA's registered tests default `CNA_FNA3D_TEST_DRIVER` to `OpenGL` so a
machine with a partially functional Vulkan stack cannot silently change the validation target.
Set that CMake cache variable to `Vulkan` to run the same registered suite through SDL_GPU's
Vulkan driver; the resulting CTest environment records the requested driver explicitly.

```bash
cmake -S . -B cmake-build-qual-fna3d \
  -DCNA_GRAPHICS_RENDERER=FNA3D \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_FNA3D_TEST_DRIVER=Vulkan
cmake --build cmake-build-qual-fna3d --parallel 12
tools/platform/run_gpu_tests_private.sh cmake-build-qual-fna3d \
  -R '^(Fna3d_|Fna3dCompiledEffect|Fna3dEffect)' -j1 --output-on-failure
```

Driver selection happens *before* the window exists, because
`FNA3D_PrepareWindowAttributes()` also primes the GL attributes the window's visual is chosen
from. `GraphicsDevice` therefore calls `Fna3d::Detail::PrepareWindowNeedsOpenGl()` while assembling
the platform-neutral `WindowDescription`. The FNA3D implementation interprets its SDL flags at
the renderer edge and exposes only the resulting OpenGL requirement to graphics.

## Shaders: the one thing FNA3D constrains

`FNA3D_CreateEffect`, which takes a **compiled Direct3D 9 Effect Framework binary** and runs it
through MojoShader, is the *only* shader entry point in `FNA3D.h`. There is no call anywhere in
the library that compiles a GLSL or HLSL source string, and the drivers refuse to draw without a
bound MojoShader program.

This renderer draws the XNA stock effects and CNA's punctual/PBR extensions, vendored under
`modules/renderers/fna3d/effects/` (provenance and license in that directory's README) and
embedded into a generated header at build time, and caller-supplied XNA/FNA Effect Framework
bytecode through `Effect(GraphicsDevice&, byte[])`. It is the only CNA renderer that executes **XNA's
actual shader programs** rather than a reimplementation of them: `BasicEffect`, `AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect` and `SkinnedEffect` are the real compiled artefacts,
and the variant is chosen with the same integer `ShaderIndex` arithmetic XNA's own `OnApply()`
computes (`Fna3dStockEffects.hpp`, unit-tested exhaustively).

Consequently **`GraphicsCapability::CompiledEffects` is true**, while
**`GraphicsCapability::CustomEffects` is false** and
`CreateEffectRenderer` returns null. The renderer also advertises the shared `ShadowSampling`
feature when its runtime driver exposes the ten sampler slots required by the compiled shadow
receivers. A caller's `ShaderEffect` GLSL/HLSL source cannot be compiled
by anything FNA3D offers, and claiming otherwise would be a promise broken at the first custom
effect. The two capabilities describe different formats.

## Device creation: `FNA3D_PrepareWindowAttributes` is mandatory

`FNA3D_PrepareWindowAttributes()` is not only a query for the window flags FNA3D's chosen driver
needs -- it is also **where FNA3D selects that driver**, and `FNA3D_CreateDevice` refuses to run
before it has, with `"Call FNA3D_PrepareWindowAttributes first!"`.

This is worth stating because CNA already lost the call once. `plans/plan_runtimerenderer.md` RTR-P1-D41
replaced the renderer descriptor's `prepareWindowFlags` hook with static data
(`windowKind = OpenGL`, `glFramebuffer = 24/8/double`), which is the right shape for the window's
visual -- CNA states those requirements through `WindowDescription` and the platform applies them.
What went with the hook, unnoticed, was the only production call to
`FNA3D_PrepareWindowAttributes()`, and from then until `plans/plan_fx.md` FX-090 **no FNA3D device could
be created at all**: every test failed at `GraphicsDevice device;`.

`Fna3dRenderer`'s constructor now calls it before `FNA3D_CreateDevice`. The GL attributes it also
primes are redundant at that point rather than load-bearing, because the window's visual is already
fixed from the descriptor's own request.

## Capabilities

| Capability | Value | Notes |
|---|---|---|
| `ThreeD` | true | Full `Draw*Primitives` routes |
| `DepthStencilBuffer` / `StencilBuffer` | true | Backbuffer and per-target depth/stencil renderbuffers |
| `MultiSampleAntiAlias` | portable 0/2/4x contract, then device-dependent | Adapter queries, back buffers and render targets share XNA's round-down rule capped at 4x; the live driver may lower it through `FNA3D_GetMaxMultiSampleCount` |
| `MultipleRenderTargets` | true | `FNA3D_SetRenderTargets` takes the whole ordered set |
| `AnisotropicFiltering` | true | `FNA3D_SamplerState::maxAnisotropy` |
| `WireFrame` | true | `FNA3D_FILLMODE_WIREFRAME` |
| `OcclusionQuery` | device-dependent | Probed from a real query on OpenGL/D3D11; false for FNA3D's SDL_GPU driver, whose public API has no query route |
| `Texture3D` | true | `FNA3D_CreateTexture3D` |
| **`Instancing`** | device-dependent | `FNA3D_SupportsHardwareInstancing`; the draw additionally requires a compiled effect whose vertex shader consumes the instance stream. Stock effects are still rejected. |
| `AdditiveBlending` | true | |
| **`MultiStreamVertexInput`** | **true** | Native: `FNA3D_ApplyVertexBufferBindings` takes an array of per-stream declarations, each with its own stride, vertex offset and instance frequency |
| **`CustomEffects`** | **false** | See above — no source-string shader compilation exists in FNA3D |
| **`CompiledEffects`** | **true** | Full Effect Framework construction/reflection, mutation, clone, pass state/samplers, primitive draws, instancing, and SpriteBatch through FNA3D/MojoShader |

`GetMaxVertexStreams()` reports XNA's own 16-binding ceiling.

## Implemented

- Device creation on CNA's SDL3 window; transactional setup rolls back effects and the device on
  failure. Resource wrappers share a liveness/identity token, so they can be destroyed safely
  after their native device and reject post-device use deterministically.
- Clear (colour / depth / stencil and every combination), present, backbuffer readback.
- Presentation modes Letterbox / Overscan / Stretch / NativeBackBuffer / FixedHeightDynamicWidth,
  expressed as the destination rectangle `FNA3D_SwapBuffers` already takes, plus the matching
  window↔logical coordinate transforms.
- `Texture2D`, `Texture3D`, `TextureCube`: creation, mip levels, sub-rectangle upload and readback.
  The public `Texture2D` format gate includes native signed-normalized `NormalizedByte2/4` storage;
  byte round-trip, GPU sampling and authored mip levels are covered on both FNA3D OpenGL and
  SDL_GPU drivers (`FNA3D-52` / `MSR-019`). `NormalizedByte2` is stored as `NormalizedByte4` with B
  and A at +1, so every driver samples Direct3D 9's (R, G, 1, 1) -- no FNA3D driver swizzles, and
  its SDL_GPU driver samples RG8 SNORM as (R, G, 0, 1) (`AM4-193`).
- `RenderTarget2D` and `RenderTargetCube`: MSAA colour renderbuffers with resolve-on-unbind, mip
  generation, per-target depth/stencil renderbuffers, `PreserveContents`, MRT sets, readback.
- Vertex and index buffers (16- and 32-bit) with `SetDataOptions` forwarded verbatim, growing the
  GPU allocation on demand and rejecting byte counts beyond FNA3D's signed 32-bit ABI limit.
- The caller's own `VertexDeclaration` bound verbatim per stream; a stride-keyed table stands in
  only for the internal routes that bind no public buffer (SpriteBatch, `DrawUser*`).
- Blend / depth-stencil / rasterizer / sampler state, colour write masks, multisample mask, blend
  factor, standalone reference stencil, scissor and viewport.
- XNA/D3D9 integer pixel centres for ordinary triangle draws across FNA3D's OpenGL, SDL_GPU and
  D3D11 drivers, multisampled destinations included: XNA 4.0 keeps integer centres there too
  (`spikes/xna-msaa-pixel-center-spike/`). SpriteBatch explicitly keeps its own projection
  convention; point and line draws remain unshifted (`MSR-034`).
- 2D `SpriteBatch` through the stock `SpriteEffect` or every pass of a compiled custom effect:
  tint, source rectangles, rotation about a scaled origin, both flips, layer depth, sampler filter
  and address modes, Immediate flushing; large batches are split at 16,384 quads before their
  16-bit indices can wrap, and deferred texture renderers are retained through backend `End` so
  queued submissions cannot dangle.
- 3D through the stock effects, rigid/skinned metallic-roughness PBR extensions, and
  caller-supplied compiled effects. Hardware instancing is available only to a compiled vertex
  shader declaring the instance inputs.
- XNA/FNA Effect Framework reflection (parameters, arrays/members, annotations, techniques and
  passes), typed value/texture mutation, native cloning, exact pass application, and legacy
  blend/depth/stencil/rasterizer/sampler state translation.
- Hardware occlusion queries.
- `SetStringMarkerEXT` → `FNA3D_SetStringMarker`.
- Driver limits queried once at device creation — `FNA3D_SupportsDXT1` / `SupportsS3TC` /
  `SupportsBC7` / `SupportsSRGBRenderTargets` and `FNA3D_GetMaxTextureSlots` — and enforced: a
  texture or render target in a format the driver has no storage for is refused by name, and so is
  a bind past the driver's real fragment sampler-slot count.
- Format-correct transfer sizing for every `SurfaceFormat`, block-compressed families included:
  `ceil(w/4) * ceil(h/4) * blockBytes`, with the row pitch counted in block rows and compressed
  sub-regions constrained to 4×4 block boundaries (except a legal NPOT mip edge tail).
- `SetDataOptions::NoOverwrite` gated on `FNA3D_SupportsNoOverwrite` and downgraded to `None`
  where the driver has no such fast path.
- `FNA3D_SetTextureName` on textures and render targets, so a RenderDoc/apitrace capture of a CNA
  frame shows named resources.
- `FNA3D_LinkedVersion()` checked against `FNA3D_COMPILED_VERSION` at device creation, so a
  mismatched shared FNA3D is reported rather than silently trusted.

## Not supported, and why

| Area | Behaviour |
|---|---|
| Custom `ShaderEffect` (GLSL/HLSL source) | `CreateEffectRenderer` returns null; `CustomEffects` is false. FNA3D compiles no shader source. |
| Render-target array slices | `SetRenderTargets` throws for a non-zero `arraySlice`; CNA has no array render target, and FNA3D's binding has no slice field. |
| Unknown vertex stride with no `VertexDeclaration` | Throws, naming the stride. FNA3D binds real per-stream declarations and this renderer will not guess a layout. |
| Out-of-contract state ordinals | Throw, naming the state and the ordinal, instead of casting into an undefined FNA3D enumerator. |
| Instanced stock-effect drawing | `DrawInstancedPrimitivesEx` throws because stock shaders declare no instance input. A compatible compiled effect uses native instancing when the driver reports it. |
| Compiled FX on non-FNA3D renderers | Outside this renderer. SDL_GPU, EasyGL, WebGPU, SOFTWARE and DIRECTX9 report true behind their own opt-in build options ([`fx-compiled-effects.md`](fx-compiled-effects.md) §10); other backends report `CompiledEffects == false`. |
| Context-loss simulation | Not implemented; FNA3D exposes no device-loss surface, and the shared `DebugSimulateContextLoss` default is a no-op. |
| Block-compressed readback on OpenGL / D3D11 | `GetData` returns false — "this renderer read nothing" — rather than reporting an untouched buffer as a successful read. Both drivers refuse compressed `GetTextureData2D` upstream; SDL_GPU forwards it. Which one applies is measured once per device by a 4×4 DXT1 probe, not guessed from the driver name. |
| A compressed `Texture3D` / `TextureCube` | Refused by name. Volume and cube transfers are RGBA8 in CNA's own renderer contract (the DDS/XNB readers decompress on the CPU before upload), so a compressed request there would be mis-sized rather than served. |

### What FNA3D offers that CNA cannot currently reach

Not gaps in this renderer — routes the shared `IGraphicsRenderer` contract has no shape for. Each
would need a shared-contract change, so each is reported rather than faked:

| FNA3D entry point | Why it is unreachable |
|---|---|
| `FNA3D_SetTextureDataYUV` | `IGraphicsRenderer` has no YUV texture route; `VideoDecoder` converts YUV→RGBA in the media module before any renderer sees a frame. |
| `FNA3D_GetVertexBufferData` / `FNA3D_GetIndexBufferData` | The buffer renderer interfaces expose no readback; XNA's `GetData` on those buffers is served from the shared layer's own CPU shadow. |

A public block-compressed `Texture2D` remains behind FNA3D's format classifier. The renderer
contract itself can carry compressed `ImageData`, but the complete public XNA constructor,
transfer, readback and content-loader route has not been promoted as one verified promise.
`FNA3D-52` deliberately opened only the independently proven signed-normalized formats rather
than using the sample failures as a reason to advertise every lower-level FNA3D format.

### Backbuffer readback quirk (upstream; fixed in the fork, still worked around here)

Upstream's `FNA3D_ReadBackbuffer` **sub-rectangle** origin is not the same on every driver: the
OpenGL driver forwards `(x, y)` straight to `glReadPixels`, whose origin is the bottom-left, and
only then flips the returned rows — so a full-frame read comes back correctly top-row-first while a
sub-rectangle read comes back from the mirrored row range. The D3D11 driver, routing through
`GetTextureData2D`, uses the top-left origin. This was measured, not inferred:
`fna3d-spike/fna3d_sprite_spike.c` renders a quad at rows 12–35, finds it there in a full-frame
read, and gets the cleared colour from the equivalent sub-rectangle read. The fork converts the
rectangle to GL's rows (2026-10-26 audit B2-10; `tests/cna/backbuffer_tests.c`).

CNA's contract is unambiguous ("x, y are top-left in game coordinates"), so `Fna3dRenderer`
reads the whole backbuffer — which every driver returns top-row-first, fixed or not — and crops
in CNA.
Readback is already a full CPU/GPU sync point FNA3D documents as screenshot-only.

### Texture upload ordering (upstream, reported)

FNA3D's SDL_GPU driver records uploads on a command buffer of its own and submits it ahead of the
frame's draws. `SetTextureData2D` cycles the texture -- earlier draws keep the old resource -- only
for a whole write of a single-level, single-layer texture; `SetTextureDataCube` and
`SetTextureData3D` never cycle. So a `TextureCube` changed between two draws of one frame shows its
new contents in the earlier draw too, where XNA's keeps the old (`texture_setdata_after_draw` leg
B, measured 2026-10-24 on the SDL_GPU driver over Vulkan); by the same source a `Texture3D` or a
mipmapped `Texture2D` does the same. CNA reports it rather than working around it (handoff.md §5.3).

## Validation

| Kind | Status |
|---|---|
| Native runtime (Linux, isolated display, FNA3D OpenGL driver) | **Performed** — all nineteen native `Fna3d_*` CTest binaries pass, including the sequential-device/post-device lifetime regression, punctual-light paths, split-sum IBL, double-sided PBR, independent UV0/UV1 material maps and the shared nine-check shadow oracle. The complete `Fna3d` label is 50/50 without failures (48 pass plus two intentional shader-backend skips). |
| XNA 4.0 oracle corpus (39 scenes vs real XNA reference images) | **Performed** — `Fna3d_XNA_Oracle`; every scene renders, 10/39 exact at the EasyGL baseline. This is the only coverage DualTextureEffect, EnvironmentMapEffect and SkinnedEffect have. See [`fna3d-parity-report.md`](fna3d-parity-report.md) |
| Unit tests (`CnaTests`, `Fna3d*` plus the generic disposed-`Texture2D` regression) | **Performed** — format-region and shared transfer-contract coverage pass |
| Compiled-effect unit/pixel tests (`Fna3dCompiledEffectTest`) | **Performed** — 34 focused tests cover fixture hashes, parse/apply, reflection, padded matrix arrays, clone/disposal, malformed cleanup, general draws, SpriteBatch pixels, and a legally reproducible synthetic multi-technique/pass/default/annotation/render-state conformance binary |
| Sanitizers (ASan + UBSan), renderer suite | **Performed** — the enlarged 13-test suite passes, including post-device lifetime and the >16-bit SpriteBatch path. Leak detection is disabled for the external graphics stack; UBSan still reports the pre-existing MojoShader decimal-parser signed overflow, but found no CNA-originating defect (FNA3D-47). |
| Sanitizers for the new arbitrary compiled-effect path | **Partial** — all 31 targeted FX/XNB/capability tests pass in the ASan/UBSan build with no ASan finding. LeakSanitizer is unavailable under the managed ptrace environment; pinned upstream MojoShader still reports known UBSan findings in float formatting and zero-length clone copies, so the full `plans/plan_fx.md` production gate remains open. |
| Existence-gate spikes | **Performed** — `fna3d-spike/` |
| SDL_GPU driver | **Exercised on AMD Radeon 780M / RADV**: the 13 registered FNA3D renderer executables, compiled-effect contracts and XNA oracle complete as a 66-test selection with 65 passes plus one intentional Reach-profile skip. The Linux sample corpus and focused pixel-centre/SpriteBatch contracts also run through this default driver. |
| Direct3D 11 driver | **Cross-build only**: the patched driver and seven-renderer sample executable compile with MinGW-w64. No native Windows GPU execution has occurred, so runtime qualification remains an external gate. |
| Driver matrix (`plans/plan_fna3d.md` FNA3D-34) | **Open only on native D3D11 execution.** OpenGL and SDL_GPU each complete the same 66-test renderer/compiled-effect/oracle selection (65 pass plus one intentional profile skip), and each visually restores `PrimitivesSample`'s one-pixel geometry. Driver-dependent readback/query behaviour still must not be conflated. |
| macOS / iOS | Not exercised. External gate. |

## Tests

| CTest name | What it proves |
|---|---|
| `Fna3d_Smoke` | Identity, device creation, clear + readback at three points, repeated frames, texture upload/readback through the renderer (not the CPU shadow), buffer counts, and either a real occlusion query or the truthful unsupported-resource refusal selected by the running driver |
| `Fna3d_2D` | SpriteBatch placement, non-bleed on all four sides, tint, source rectangles, flips, rotation, point-sampling texel edges, and the first sprite beyond one 16-bit-index batch |
| `Fna3d_3D` | BasicEffect vertex-colour/diffuse/textured variants, indexed route, depth test both orders, AlphaTestEffect discard and keep, wireframe |
| `Fna3d_RenderTarget` | Target clear/readback, geometry placement inside a target, unbind restores the backbuffer, sampling a rendered target, depth/stencil reporting, MSAA resolve, `PreserveContents` |
| `Fna3d_State` | AlphaBlend, Additive, colour write masks, BlendFactor, scissor on/off, a non-default depth comparison, stencil write-then-test, viewport sub-rectangle |
| `Fna3d_Capabilities` | Every capability answer matched against a real factory or a real refusal, and both rejection paths |
| `Fna3d_Pbr_DoubleSided` | Five front/back/mirrored-World cases each through public rigid and identity-bone skinned PBR effects; the same shared oracle runs on EasyGL |
| `Fna3d_Pbr_MaterialMaps` | Twenty-one rigid/skinned material-map checks, including channel semantics, normal/occlusion controls and independent UV0/UV1 selection; the same shared oracle runs on EasyGL |
| `Fna3d_ShadowSampling` | Nine public-contract checks for capability reporting, rigid/skinned PBR directional reception, PCF, cascades, point-cube and spot maps, ambient preservation, and classic skinned vertex colour; the same shared oracle runs on Software |
| `Fna3d_RenderTarget_Advanced` | All six cube faces hold distinct colours and geometry renders into a bound face; two MRT slots are both written and keep their own storage; a mipmapped target's level count, base level and level-1 storage |
| `Fna3d_Buffers` | POSITION and COLOR split across two real vertex streams (both binding orders), all three `SetDataOptions`, source-window selection, consecutive uploads, both index widths |
| `Fna3d_Sampler` | Point vs Linear magnification, Clamp/Wrap/Mirror past u=1, independent U/V addressing, Anisotropic, and the LOD controls |
| `Fna3d_Lifetime` | Invalid levels and regions, undersized destinations, disposed resources, draw-range validation, documented refusals, and resource-before-device teardown |
| `Fna3d_Device_Lifetime` | Resources, render targets, buffers, queries and SpriteBatch survive destruction of their device wrapper safely; post-device use and binds from a sequential replacement device are rejected; environment sampler 1 is exercised through live→null and foreign-device paths; buffer-overflow requests never reach FNA3D |
| `Fna3d_XNA_Oracle` | All 39 XNA oracle scenes render, and each is diffed against the real XNA 4.0 reference image |
| `Fna3d_Compressed` | Hand-built DXT5 blocks uploaded through `IGraphicsRenderer::CreateTexture` decode as the right colour in the right quadrant, including a 6×6 level whose tail blocks are padded; `GetData` matches the driver's real compressed-readback answer; a compressed cube and an out-of-range sampler slot are refused by name |

`CnaTests --gtest_filter=Fna3d*` covers the device-free logic: the `ShaderIndex` arithmetic against
an independently transcribed reference, the presentation layout and its inverse transform, the
enum bridge's range rejection, the stride table, and the per-format transfer byte arithmetic
(including compressed block alignment, partial-tail cases, and a row-pitch × block-rows =
region-count consistency sweep). `Texture2DTest.TransfersAfterDisposeThrowObjectDisposedException`
pins the shared upload/readback contract repaired alongside the renderer audit;
`SpriteBatchSortModeTest.DeferredRetainsTextureRendererThroughEnd` pins queued texture lifetime.
