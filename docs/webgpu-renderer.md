# WebGPU graphics renderer

## Status

The WebGPU renderer was activated by the project owner on **2026-07-12** and is an **experimental
CNA graphics renderer** -- one of the project's 49+ public renderer identities, native (wgpu-native)
on desktop and, since 2026-08-26, also in the browser through Emscripten's emdawnwebgpu port. Select
it with:

```bash
cmake -S . -B cmake-build-webgpu \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-webgpu -j
```

CNA pins the native implementation to **wgpu-native v29.0.1.1**. By default CMake downloads the
official prebuilt package for a supported host. For offline or reproducible builds, extract the
official package yourself and configure:

```bash
cmake -S . -B cmake-build-webgpu \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_WEBGPU_ROOT=/absolute/path/to/extracted/wgpu-native \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF
```

The extracted root must contain `include/webgpu.h` (or `include/webgpu/webgpu.h`) and a
`libwgpu_native` library below `lib/`. The existing CNA requirements still apply: the SDL3,
SDL3_image and SDL3_mixer submodules, plus the sibling `../sharp-runtime` checkout, must be
present.

For the verified Linux x86_64 layout, an extracted package may be placed at
`vendor/wgpu-native/` with `include/webgpu/webgpu.h` and `lib/libwgpu_native.so`. The following
clean offline sequence builds a self-contained demo directory:

```bash
cmake -S . -B cmake-build-webgpu \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_WEBGPU_ROOT="$PWD/vendor/wgpu-native" \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-webgpu --target cna_demo_2d -j"$(nproc)"
cd cmake-build-webgpu
readelf -d ./cna_demo_2d | grep -E 'NEEDED.*wgpu|RUNPATH'
ldd ./cna_demo_2d | grep libwgpu_native.so
timeout 60s ./cna_demo_2d --smoke 120
```

The WebGPU CMake target links wgpu-native by filename despite the package library lacking an ELF
SONAME, copies the runtime beside `cna_demo_2d`, and gives the executable a `$ORIGIN` runtime path.
This enables the executable to use its sibling `libwgpu_native.so` rather than requiring the
original package location at runtime.

## Web / browser target (Emscripten) — in progress

`WEBGPU` is one renderer identity with two backends, not two renderers. The browser build reuses all
of `WebGPURenderer.cpp` and every WGSL shader; five seams differ, each behind
`#if defined(__EMSCRIPTEN__)` -- the three below, then the two after them:

- **Surface.** `CreateSurface()` uses `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` targeting the
  CSS selector `"#canvas"` (SDL3's Emscripten default canvas), instead of a native window handle.
- **Async completion.** WebGPU is promise-based in the browser. The renderer keeps its single
  synchronous adapter/device/readback path and, under Emscripten, requests
  `WGPUCallbackMode_AllowSpontaneous` and yields with `emscripten_sleep()` (Asyncify, enabled
  project-wide) so the browser event loop can settle the promise — instead of the native
  `wgpuInstanceProcessEvents()` pump.
- **Toolchain.** `cmake/ThirdPartyWebGPU.cmake` has an `if(EMSCRIPTEN)` branch that links
  Emscripten's **emdawnwebgpu** port (`--use-port=emdawnwebgpu`) with no wgpu-native download and no
  runtime library to copy. The port's `webgpu/webgpu.h` is the same unified header wgpu-native v29
  exposes, which is why the shared code compiles unchanged. (The older `-sUSE_WEBGPU=1` built-in was
  removed in Emscripten 4.0.10 and is not used.)

The remaining two seams (the fourth and fifth), which the first browser run required:

- **Present.** `Present()` skips `wgpuSurfacePresent()` under Emscripten — emdawnwebgpu aborts on it
  ("use requestAnimationFrame instead"). The canvas is shown automatically when `Game::RunLoop()`
  yields to `requestAnimationFrame` each frame; the queue submit is the whole frame.
- **Resize.** The browser sizes the surface from the `<canvas>` backing store, not from
  `wgpuSurfaceConfigure()`, so on a canvas resize `wgpuSurfaceGetCurrentTexture()` returns the new
  size while the configured depth/MSAA attachments still hold the old one. After acquiring the frame
  texture the renderer resyncs `physicalWidth_/physicalHeight_` (and the depth/MSAA textures) to the
  texture it actually got. One shared guard, `PlatformRendererSurfaceState`, was relaxed to accept
  the `Web` surface, which is a canvas selector rather than a native window pointer.

Select it under `emcmake` with `-DCNA_GRAPHICS_RENDERER=WEBGPU`. As of 2026-08-26 the **2D path runs
in a real browser**: `cna_demo_2d` renders 120 SpriteBatch frames in headless Chrome (over the real
AMD Vulkan WebGPU path — SwiftShader exposes no WebGPU adapter here) with audio, no WebGPU
validation error, a mid-run canvas resize, and clean teardown. Reproduce with
`scripts/run-webgpu-browser-test.sh`; `CNA_WEBGPU_DEMO=cna_house3d_demo` drives the 3D `BasicEffect`
path (perspective, depth test, texturing) through the same harness, also green in-browser, and
`CNA_WEBGPU_DEMO=cna_webgpu_{pbr3d,envmap3d,skinned3d,dualtexture3d,alphatest3d}_page` confirm every
stock effect shader -- `PbrEffect`, cube-map `EnvironmentMapEffect`, bone-palette `SkinnedEffect`,
`DualTextureEffect` and `AlphaTestEffect` -- compiles and renders in-browser (`plans/plan_webgpu.md`
`WEBGPU-121`/`122`, both ✅). **`WEBGPU-133` (fixed).** Under a **multiple-`ReadBackbuffer`-per-frame** pattern (which the effect
suites use), `ReadBackbuffer()`'s buffer-map wait yields to the browser event loop (`emscripten_sleep`,
Asyncify), and the browser presents and invalidates the canvas's current surface texture during that
yield. The renderer cached the acquired texture across the yield, so a later same-frame flush re-
submitted a now-destroyed texture -- which wgpu-native tolerates and Dawn rejects with
`Destroyed texture ... used in a submit`. Fixed with a *lazy discard*: a readback marks the acquired
texture stale (`acquiredBackbufferStale_`), and `EnsureFrameRendered()` discards and re-acquires a
fresh one only when it is about to render again -- while a same-frame re-READ still reuses the
already-captured `readbackBuffer_`. This handles both browser readback patterns (draw/read per check,
and several reads of one frame). All five effect pages pass in-browser (`PbrEffect` 5/5,
`EnvironmentMapEffect` 4/4, `SkinnedEffect` 9/9, `DualTextureEffect` 4/4, `AlphaTestEffect` 4/4) and
the 2D/3D smokes are unregressed. (The earlier `SkinnedEffect` D2 failure was this same multi-read
artifact, not a skinning bug: per-render probing confirmed the WeightsPerVertex=2 quad shifts right
correctly.) The one remaining web-specific refusal is the `--webgpu-2d-validation` scene's
`MinimizeEXT()` (a native-only `GameWindow` operation, not a renderer limit).

**Cross-backend pixel parity (`WEBGPU-123`).** `cna_diag_webgpu` builds the shared, renderer-agnostic
`cross_renderer_diagnostic_scene` (one unlit vertex-colour triangle -> 64x64 RGBA8) for WEBGPU,
native and Emscripten, exactly as `cna_diag_easygl`/`cna_diag_software` do.
`scripts/run-webgpu-parity-test.sh` runs the Emscripten `cna_diag_webgpu` in headless Chrome,
extracts its dump out of MEMFS, and diffs it against another backend's dump with `cna_diag_compare`.
Against the native SOFTWARE CPU rasterizer's dump of the same scene the max per-channel difference is
1 and the mean 0.139 across the whole frame (both have the identical 1682 non-black pixels) -- well
inside the tool's default tolerance of 40. A native `OPENGL33` (EasyGL) dump, produced headless under
Xvfb + Mesa `llvmpipe`, was compared too: WebGPU matches the CPU reference essentially exactly (0
pixels over tolerance), whereas EasyGL differs from BOTH WebGPU and SOFTWARE at the same 57
triangle-edge coverage pixels -- a GL fill-rule / pixel-centre convention difference at the triangle
boundary, not a WebGPU defect (WebGPU is on the reference-matching side). Finally a native VULKAN dump
(`cna_diag_vulkan` on the real AMD Radeon 780M / RADV) was compared: it is **byte-identical to the
browser WebGPU dump (max diff 0)** -- unsurprising, since Chrome's WebGPU is Dawn on the same AMD
Vulkan. So strict per-pixel edge parity is not universal across rasterizers, but the two Vulkan-backed
paths (native Vulkan and browser WebGPU) are exact, and both agree with the CPU reference to within 1.

## Automated native smoke test

With `CNA_BUILD_TESTS=ON`, the WebGPU configuration registers `WebGPU_Native2D_Smoke` with CTest:

```bash
ctest --test-dir cmake-build-webgpu -R '^WebGPU_Native2D_Smoke$' --output-on-failure
```

The test runs `cna_demo_2d --smoke 120` when the host exposes Wayland or X11. It passed in 2.30
seconds on the verified desktop. When neither `WAYLAND_DISPLAY` nor `DISPLAY` is available, the
wrapper reports a clear skipped result rather than treating the lack of a desktop GPU/display as a
renderer failure.

CNA enables compiler caching automatically when `ccache` is installed. The setting is applied
before the sibling `sharp-runtime` project is added, so both CNA and `sharp-runtime` objects are
reused across compatible build directories. Disable it with `-DCNA_USE_CCACHE=OFF`; existing custom
compiler launchers are never overwritten. Even without `ccache`, reusing the same CMake build
directory keeps the normal incremental object cache and avoids rebuilding unchanged
`sharp-runtime` sources.

## Verified native smoke gate

`WEBGPU-125` was verified on Linux desktop on 2026-07-12 with the explicit offline package:

```bash
cmake -S . -B /tmp/cna-webgpu-125 \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_WEBGPU_ROOT="$PWD/vendor/wgpu-native" \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cna-webgpu-125 --target cna_demo_2d -j1
cd /tmp/cna-webgpu-125
timeout 60s ./cna_demo_2d --smoke 120
```

The executable cleared and presented 120 frames, then exited with status 0 in 2.10 seconds. No
uncaptured WebGPU error, device-loss report or dynamic-loader failure was emitted. This verifies
the native initialization, frame submission, present and normal teardown path only; it is not a
pixel-correctness result for SpriteBatch or Texture2D.

## SpriteBatch validation scene

`cna_demo_2d` provides the deterministic coverage scene used to verify `WEBGPU-126`:

```bash
cd /tmp/cna-webgpu-125
./cna_demo_2d --webgpu-2d-validation --smoke 120
```

It uploads `player.png` and displays source rectangles, tint/alpha blending, rotation, horizontal
and vertical flips, linear and point filtering, and clamp/wrap/mirror address modes. The last
three use UVs outside the texture's `[0, 1]` range so their visual results differ. During the run,
the native window resizes from 800×600 to 960×540 and back, exercising logical presentation and
surface reconfiguration. The command completes without WebGPU errors; a manual desktop screenshot
review on 2026-07-12 confirmed the expected crop, tint/alpha, rotation, flips and sampler results.

## Lifecycle recovery

`WEBGPU-127` replaced the original unbounded adapter/device callback waits with
`wgpuInstanceProcessEvents` polling and a 10-second timeout. This avoids the pinned v29 package's
unimplemented `wgpuInstanceWaitAny` path, which aborts rather than waiting. A minimized or
zero-size window now unconfigures the surface and releases the depth attachment; restoring it
causes the normal surface configuration path to rebuild them. The 180-frame validation command
above exercised resize, minimize, restore and normal teardown with exit status 0 and no WebGPU
error.

## Independent application integration and native 2D baseline

`WEBGPU-130` integrated `../mobile-eggbert` (`WindowsPhoneSpeedyBlupi`, an independently-developed
XNA-style game, not a CNA example) as a second, real-world consumer of this renderer, verified on
Linux desktop 2026-07-12:

```bash
cd ../mobile-eggbert
cmake -B cmake-build-debug \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_WEBGPU_ROOT=/absolute/path/to/cna_graphics/vendor/wgpu-native \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF
cmake --build cmake-build-debug --target WindowsPhoneSpeedyBlupi -j
cd cmake-build-debug
DISPLAY=:0 ./WindowsPhoneSpeedyBlupi
```

The build links `cna_renderer_webgpu` and copies `libwgpu_native.so` beside the executable
with `$ORIGIN` first in `RUNPATH`, the same deployment shape `WEBGPU-128` verified for `cna_demo_2d`.
On a real desktop session it reached its main menu automatically (~5 seconds after launch) with
pixel-correct `SpriteBatch` rendering (title text, an animated character, a player-select panel, a
Setup icon and a Play button), and a simulated click on the Play button correctly triggered its
mission-start sequence — an animated cutscene with a filling progress bar and a cross-fade
transition, rendered correctly frame-by-frame with no WebGPU validation errors. This exercises real
multi-sprite, multi-frame animation and alpha-blended fade compositing beyond the synthetic
validation scene above, independently confirming the SpriteBatch pipeline, Texture2D upload and
resize/present paths against a second, unrelated codebase.

`WEBGPU-131` closes the native 2D baseline on this evidence: `WEBGPU-124`–`WEBGPU-130` are all
verified. 3D effects, render targets and MRT remain open (Phase 57 onward in `plans/plan_webgpu.md`).

## GPU readback and a real translucency fix (2026-07-12)

`WebGPURenderer::ReadBackbuffer()`/`GraphicsDevice.GetBackBufferData()` are implemented
(`WEBGPU-91`/`55`): a row-aligned staging buffer, `wgpuCommandEncoderCopyTextureToBuffer` inside
the frame's own command encoder, `wgpuBufferMapAsync` polling. Because a naive implementation could
only ever observe the *previous* frame's content on a swapchain-backed target, `Present()` was
refactored into a shared `EnsureFrameRendered()` so `GetBackBufferData()` can force an on-demand
render of whatever's queued so far in the current `Draw()` call — matching Vulkan/Bgfx's own
on-demand-submit readback semantics. New `WebGPU_Clear_Readback` CTest.

Writing that test's alpha case then found and fixed a real, previously-unknown bug (`WEBGPU-132`):
the `SpriteBatch` pipeline's blend factors didn't match its own shader's non-premultiplied output,
so any tint/texture alpha strictly between 0 and 255 was silently ignored for colour — a
translucent sprite rendered fully opaque. Invisible to `WEBGPU-126`'s manual screenshot review;
caught only by a pixel assertion. Fixed by matching Vulkan's own blend-factor pairing.

## First 3D draw path (2026-07-12)

`DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` (a `VertexPositionColor`, stride-16,
unlit/untextured 3D draw — the path `GraphicsDevice.DrawUserPrimitives()`/
`DrawUserIndexedPrimitives()` already route to directly) are implemented: a 128-float uniform
buffer matching `VulkanRenderer::FillExtPushConst()`'s layout byte-for-byte, a `colored3d.wgsl`
shader, and a depth-aware pipeline cache. New `WebGPU_Colored3D` CTest proves genuine depth-buffer
comparison (not "last draw wins"): a near and a far full-screen quad resolve to the near one
regardless of draw order. Since `IGraphicsRenderer::DrawPrimitivesEx()`'s own default implementation
falls back to `DrawColoredPrimitives()`, simple (unlit, untextured) `BasicEffect`/`Model.Draw()`
calls now render via that fallback instead of throwing, though real lighting/texture/fog dispatch
(`WEBGPU-66`) is still open. Writing this test's own depth-order check also found and fixed a
separate, real gap: `WebGPURenderer::ApplyDepthStencilState()` was entirely unimplemented
(silently inherited the interface's no-op default), so `GraphicsDevice.DepthStencilState` — the
real XNA API surface almost every game/effect uses — had zero effect on this renderer; only the
older `SetDepthTestEnabled()`/`SetDepthWriteEnabled()` convenience methods worked. Now implements
the depth portion (stencil ops remain open, `WEBGPU-83`).

## PbrEffect (unskinned metallic-roughness BRDF)

`pbr3d.wgsl` / `GetOrCreatePipelinePbr3D()` implement `PbrEffect`'s real glTF 2.0
metallic-roughness BRDF (GGX distribution + Smith-Schlick-GGX visibility + Schlick Fresnel),
ported from `EasyGLRenderer::EnsurePbrProgram()`'s GLSL shader, for stride-48
(`VertexPositionNormalTangentTexture`) draws. Base color, normal, metallic-roughness, emissive
and occlusion maps are all supported, each falling back to a 1x1 default texture (flat normal /
white, matching the EasyGL renderer's own "map absent" convention) when `PbrEffect` leaves that
map unbound. Group 0 reuses the existing `Uniforms`/`LitLightParams` UBO shapes byte-for-byte
(populated by the already-existing `FillExtUniforms()`/`FillLitLightUniforms()` helpers) plus one
new small `PbrFactors` UBO for `MetallicFactor`/`RoughnessFactor`; group 1 is a new 5-texture +
1-sampler bind group. New `WebGPU_Pbr3D` CTest hand-derives the exact GGX/Fresnel formula for a
fixed light/view/normal geometry and independently confirms the observed pixel values are within
a few 8-bit units of the analytic prediction (after accounting for the sRGB swapchain's gamma
encoding), plus qualitative ambient/facing/back-facing/normal-map/metallic-vs-dielectric checks.

**Scope**: unskinned only, at the time this shader was added. Fog and alpha-test are deliberately
not wired into `pbr3d.wgsl`: every other WebGPU 3D shader already defers fog identically, and
`PbrEffect::FillGpuDrawParams()` never sets `GpuDrawParams::alphaTest` away from its always-pass
default, so that branch would be permanently dead code. `SkinnedPbrEffect` (stride 68) is now
implemented separately — see "SkinnedEffect and SkinnedPbrEffect (bone-palette skinning)" below.

## SkinnedEffect and SkinnedPbrEffect (bone-palette skinning)

`skinned3d.wgsl`'s four shader-module variants (per-pixel-lit/per-vertex-lit ×
without/with vertex colour) plus `skinned_pbr3d.wgsl` close this renderer's former "no skinning
shader at all" gap, ported line-for-line from
`EasyGLRenderer::EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()`/
`EnsurePbrSkinnedProgram()`. Bone-palette skinning (up to 72 bones, a new `SkinningParams` UBO —
a `WeightsPerVertex` header plus `array<mat4x4f, 72>`) matches Task 895's convention: only the
first `WeightsPerVertex` (1, 2, or 4) weight/index pairs are summed. Because a WebGPU pipeline must
supply every vertex-shader-referenced attribute location from its own vertex buffer layout (unlike
`EasyGLRenderer::ApplyLayout`'s own "leave attribute 5 unbound" precedent for stride 52),
the stride-52 (`VertexPositionNormalTextureSkinned`) and stride-56 (with a trailing per-vertex
`Color`, CNB-67) cases each get their own shader module pair, mirroring this renderer's existing
`texturedShader_`/`coloredTexturedShader_` precedent for the analogous stride-20/24 split; the
vertex-colour gate multiplies the *final* combined diffuse+specular output, applied after the
specular add (matching the EasyGL reference's own fix for that exact ordering trap).
`skinned_pbr3d.wgsl` (stride 68, `VertexPositionNormalTangentTextureSkinned`) reuses the same
bone-palette vertex transform (extended to also skin Tangent) feeding `pbr3d.wgsl`'s own BRDF
fragment stage unchanged, and reuses `pbrBindGroupLayout1_` (the 5-texture group) unchanged for its
own group 1. No vertex colour on the PBR+skinning combo (matches the EasyGL reference, which has
none either) and no fog/alpha-test on any of the new shaders (same deliberate deferral as every
other WebGPU 3D shader — `SkinnedEffect`/`SkinnedPbrEffect` never set `GpuDrawParams::alphaTest`
away from its always-pass default).

New `WebGPU_Skinned3D` (9 checks) and `WebGPU_SkinnedPbr3D` (5 checks) CTests both pass 100%: a
hand-derived NDC-shift check (two bones, one identity and one a translation, summed under a
uniform-scale-then-perspective-divide argument worked out algebraically, not measured) proves both
the bone-palette translation genuinely reaches the vertex shader *and* that `WeightsPerVertex`
correctly gates which bones contribute; ambient/facing/back-facing checks prove real lighting
reaches both shader families; a `VertexColorEnabled` check (pure black per-vertex colour, mirroring
`modules/renderers/easygl/examples/easygl_skinnedeffect_vertexcolor_test.cpp`'s own convention) proves the stride-56 colour
path; and a `PreferPerPixelLighting` check (Gouraud-averaged vs fresh-per-fragment specular at a
triangle seam) proves the vertex-lit/pixel-lit dispatch selects two genuinely different shaders.

## RenderTarget2D (single-target, 2026-07-18)

`WebGPURenderTargetRenderer` (`WEBGPU-53`/`54`) is this renderer's first real off-screen render
target: its own colour texture, always created in the swapchain's own chosen format
(`surfaceFormat_`) rather than `Texture2D`'s `RGBA8Unorm` so every existing
`GetOrCreatePipeline*3D()` (each hardcodes `target.format = surfaceFormat_` at pipeline-creation
time) renders into it unchanged, with zero new pipeline-cache dimensions; and its own combined
depth+stencil texture, always `Depth24PlusStencil8` regardless of the requested `DepthFormat`
(mirroring `VulkanRenderer`'s own "always allocates a combined depth+stencil buffer using
its device-wide format regardless of the exact value requested" simplification), because every
pipeline here unconditionally declares a depth-stencil state.

The trickier part was this renderer's existing single-deferred-render-pass-per-frame architecture:
every queued `Clear()`/3D-draw/`SpriteBatch` command normally collapses into one render pass
against the swapchain, lazily, the first time something actually needs the frame to be real
(`EnsureFrameRendered()`). `SetRenderTarget2D()` now eagerly flushes whatever was queued for the
OUTGOING target the instant the target actually changes (closing out that render pass immediately
via the new `RenderPendingDrawsToRenderTarget()`, and starting fresh accumulation for the new
target) rather than tagging every one of this renderer's ~10 `Queue*Draw()` families with a target
and replaying them grouped-by-target at final-flush time, closer to `VulkanRenderer`'s own
deferred-recording model — the smaller change for a renderer with no pre-existing per-draw
deferred/replay infrastructure to extend.

A `RenderTarget2D` sampled back as an ordinary texture (`SpriteBatch`/`BasicEffect.Texture`) needed
one more fix: every `Queue*Draw()` previously resolved its bound texture with an *unchecked*
`static_cast<const WebGPUTextureRenderer*>`, safe only because `WebGPUTextureRenderer` used to be the
only `ITextureRenderer`-implementing class this renderer had. `WebGPURenderTargetRenderer` is a
second, unrelated one, so this became a real hazard the first time a `GpuDrawParams::textureN`
actually pointed at one — fixed by introducing `IWebGPUSamplable` (mirroring
`VulkanRenderer`'s own `IVulkanSamplable`) and a `dynamic_cast`-based `ResolveSamplable()`
helper everywhere a texture pointer is stored, so an incompatible type now safely resolves to
"unbound" instead of undefined behaviour.

Mip-chain regeneration is deliberately deferred, not silently under-delivered:
`CreateRenderTarget2D()` throws a clear error for `mipMap=true`. MSAA (`WEBGPU-58`) is implemented
and verified end-to-end as of 2026-07-18: the renderer-global `sampleCount_`,
`ApplyMultiSampleCount()`'s empirically-probed clamped-return-value contract, and
`WebGPURenderTargetRenderer`'s unconditional mirroring of that global sample count all work, and a
genuine multisample-resolved render now works through the real `GraphicsDevice`/`BasicEffect` draw
path for both the backbuffer and a `RenderTarget2D` (`WebGPU_Msaa`, 6/6). The initial investigation
found the MSAA infrastructure itself was already correct; the reported failures (Checks B/D-2/E)
turned out to be a test-authoring defect in `modules/renderers/webgpu/examples/webgpu_msaa_test.cpp` — its diagonal triangle
relied on `BasicEffect`'s default `RasterizerState` (`CullCounterClockwiseFace`) without the
`RasterizerState::CullNone` override every other WebGPU 3D test in this suite sets, and the
triangle's winding is a genuine XNA back face under this renderer's (independently correct)
`ToWGPUCullMode()` mapping, so it was being legitimately backface-culled regardless of MSAA. See
`WEBGPU-58`'s `plans/plan_webgpu.md` row for the full investigation. `WebGPU_RenderTarget2D` (8 checks) verifies a Clear-only round
trip and a real `BasicEffect` draw round trip via `GetData()`, a depth+stencil-tested target (a
farther red quad loses to a nearer green one, with a genuine `ClearOptions::Stencil` clear — this
also closed `WEBGPU-8`/`9`'s previously-unexercised stencil-attachment gap), sampling all 3 targets
back through `SpriteBatch`, and — the architecture's critical proof — that a render-target-targeted
`Clear()` sandwiched between two backbuffer `Clear()`/readback calls does not leak into the
backbuffer's own render pass.

## EnvironmentMapEffect and real instancing (2026-07-18)

`env_map3d.wgsl` / `GetOrCreatePipelineEnvMap3D()` (`WEBGPU-25`/`36`/`74`) close this renderer's
former "no cube-map shader at all" gap, ported from `VulkanRenderer`'s
`env_map3d.{vert,frag}.glsl` (cross-checked against `EasyGLRenderer::
EnsureEnvMapped3DProgram()`'s identical GLSL formula before porting). Stride 32
(`VertexPositionNormalTexture`, the same layout as `lit_textured3d.wgsl`). Group 0 binding 0 is a
new `Transform` UBO (mvp+world — WebGPU has no push constants); binding 1 is `EnvMapParams` (eye
position, diffuse, emissive+amount, all 3 directional lights, envMapSpecular+Fresnel, fog, and a
CPU-precomputed 3×3 normal matrix, since WGSL has no `inverse()`). Group 1 is a new 3-binding shape
(sampler + `texture_2d` + `texture_cube`), mirroring `dualTextureBindGroupLayout_`'s own 3-binding
shape with the second `texture_2d` swapped for a `texture_cube`. Getting a cube map to sample at all
required a new, minimal `WebGPUTextureCubeRenderer` (`WEBGPU-56`/`113`) — this renderer previously had
no `TextureCube` support whatsoever; it is deliberately NOT full parity (no `GetData()`, no
`RenderTargetCube`, mip regeneration untested beyond pre-allocating empty levels). `WebGPU_EnvMap3D`
(4/4): hand-derived geometry (`View`=`World`=Identity, quad at z=0.5, `Normal`=(0,0,-1)) makes the
reflection vector land exactly on `CubeMapFace::NegativeZ` — proven by painting each of the 6 faces
a distinct solid colour and asserting the correct one appears (not just "some colour"); a
differential Fresnel check (identical scene, only `FresnelFactor` differs) proves the Fresnel term
genuinely gates the blend rather than being present-but-inert; `EnvironmentMapAmount=0`
independently proves the amount also gates it; the indexed-draw dispatch path is exercised too.

`instanced3d.wgsl` / `GetOrCreatePipelineInstanced3D()` / `DrawInstancedPrimitivesEx()`
(`WEBGPU-27`/`38`/`68`) is the other half: a genuine second `WGPUVertexStepMode_Instance` vertex
buffer binding carrying a per-instance mat4 world transform, ported from `VulkanRenderer`'s
`instanced3d.{vert,frag}.glsl`. Unlike the bind-group-shaped families above, a second vertex stream
needs **no new bind group layout at all** in WebGPU (vertex buffers are set via
`wgpuRenderPassEncoderSetVertexBuffer()`, entirely separate from bind groups), so this reuses
`coloredBindGroupLayout_`/`coloredPipelineLayout_` unchanged; `[0..15]` of that UBO is
View×Projection rather than a full MVP, since world comes from the per-instance stream (matching
`FillInstancedPushConst()`'s own deliberate choice to ignore the caller's own `World` matrix
entirely). Unlike Vulkan's own hardcoded 64-byte instance-binding stride, this renderer's pipeline
cache key genuinely includes both the per-vertex and per-instance buffer strides, so the GPU-side
binding always matches whatever the caller's buffers actually declare. `params.instanceVb ==
nullptr` falls back to a real `DrawIndexedPrimitivesEx()` draw rather than throwing, matching
`VulkanRenderer`'s identical fallback. `WebGPU_Instanced3D` (5/5, tested directly at the
`IGraphicsRenderer` level, matching `modules/renderers/directx9/examples/directx9_instanced_test.cpp`'s own established
test-authoring convention for this API): 3 instances in ONE draw call each paint their own small
quad at their own independently-predicted screen location with the shared `DiffuseColor` — proving
the per-instance buffer is genuinely read per-instance, not e.g. always instance 0 or a hardcoded
2-instance special case — plus an untouched-background check and the null-`instanceVb` fallback.

## RenderTargetCube (2026-07-18)

`WebGPURenderTargetCubeRenderer` (`WEBGPU-114`) is this renderer's first real render-into-a-cube-face
support — before this, `CreateRenderTargetCube()` was `IGraphicsRenderer`'s own nullptr-returning
default. It owns ONE shared 6-array-layer colour `WGPUTexture` (the same layout
`WebGPUTextureCubeRenderer`, WEBGPU-56/113, already established for a plain `TextureCube`) plus ONE
shared `size`×`size` `Depth24PlusStencil8` depth+stencil texture reused across all 6 faces — safe
because only ONE face is ever bound/rendered-into at a time, mirroring
`VulkanRenderTargetCubeRenderer`'s identical shared-depth-image choice. Each face gets its own
`WGPUTextureViewDimension_2D` view (the render-pass colour attachment); one further
`WGPUTextureViewDimension_Cube` view spans all 6 layers for sampling back.

Making a cube face act as its own distinct "target identity" for this renderer's pre-existing
eager-flush-on-target-switch design (see the `RenderTarget2D` section above) needed generalising
that design's previously `RenderTarget2D`-only switch logic: `WebGPURenderer::
FlushCurrentRenderTarget()` is a new shared entry point (factored out of the old inline
`SetRenderTarget2D`-only if/else) that flushes whichever of {backbuffer, a bound `RenderTarget2D`,
a bound `RenderTargetCube` face} is currently active, via a new `currentRenderTargetCubeFace_`/
`currentRenderTargetCubeFaceIndex_` pair mirroring the pre-existing `currentRenderTarget_` field.
This means switching directly between two DIFFERENT faces of the SAME (or a different)
`RenderTargetCube` — not just face↔backbuffer — still correctly flushes the outgoing face's own
render pass first, verified directly (`WebGPU_RenderTargetCube` Check A: all 6 faces bound
face-to-face with no intervening backbuffer switch, each reading back its own distinct `Clear()`
colour).

Sampling a `RenderTargetCube` back as `EnvironmentMapEffect.EnvironmentMap` needed one more small
piece: a new `IWebGPUCubeSamplable` interface (the cube-map sibling of the pre-existing
`IWebGPUSamplable`), implemented by BOTH `WebGPUTextureCubeRenderer` and
`WebGPURenderTargetCubeRenderer`, resolved via a safe `dynamic_cast` (`ResolveCubeSamplable()`).
Before this, `GpuDrawParams::envMap` was resolved via a `dynamic_cast` to the concrete
`WebGPUTextureCubeRenderer` type ONLY, so a `RenderTargetCube` (a different concrete class) bound as
an env map would have silently failed that cast and rendered the 1x1 white-cube fallback instead of
its own real content — this is CNA's primary real-world `RenderTargetCube` use case (dynamic
reflection/environment maps), so this wiring matters as much as the render-into support itself.

Deliberately, honestly NOT implemented (documented scope cuts, not silently under-delivered): mip
regeneration (`mipMap=true` throws, matching `CreateRenderTarget2D`'s own precedent) and MSAA
(`multiSampleCount` is ignored; `GetMultiSampleCount()` always reports 0). `WebGPU_RenderTargetCube`
(12/12) verifies: 6-face direct face-to-face switching, a real `BasicEffect` 3D draw into a face,
the `EnvironmentMapEffect` sampling round trip above, the critical "an intervening cube-face-
targeted `Clear()` must not leak into the backbuffer's own render pass" architecture check (mirrors
the `RenderTarget2D` section's own Check E), `mipMap=true` throwing, and `MultiSampleCount`
honesty.

A genuinely new, previously-untested finding surfaced while writing this test (documented, not
fixed — pre-existing and renderer-wide, not specific to `RenderTargetCube`): `QueueSprite()` computes
every sprite's clip-space geometry from the BACKBUFFER's own logical dimensions unconditionally,
never from whatever render target is currently bound. A `SpriteBatch.Draw()` issued while a
smaller/different-sized off-screen target is bound therefore does not necessarily cover the whole
bound target the way a caller would expect — confirmed empirically (a 32×32 cube face bound under a
64×64 backbuffer only had one quadrant painted by a destination-rect-(0,0,32,32) `SpriteBatch.Draw`
call). `webgpu_rendertargetcube_test.cpp`'s own Check C works around this by painting each face with
a real 3D (`BasicEffect`) draw instead of `SpriteBatch`, which is not subject to this backbuffer-
relative ortho mapping.

## Real mip generation for Texture2D/TextureCube (2026-07-18)

`WEBGPU-52`: previously, `mipMap=true` on a plain `Texture2D`/`TextureCube` only pre-allocated
empty GPU storage for levels 1+ — no content was ever generated from level 0. Investigating the
row's own originally-suggested technique (`wgpuCommandEncoderCopyTextureToTexture`) confirmed it is
a raw same-size copy in this project's pinned wgpu-native v29.0.1.1 — there is no
`vkCmdBlitImage`-equivalent filtered-downsample primitive in wgpu-native at all. The real technique
other WebGPU-based engines use, and the one implemented here
(`WebGPURenderer::GenerateMipsForLayer()`, wrapped by `GenerateMips2D()`/
`GenerateMipsCubeFace()`): one render pass per mip level, each drawing a full-screen triangle (the
standard 3-vertex, no-vertex-buffer `@builtin(vertex_index)` trick — no `SpriteVertex`-style vertex
buffer needed) that samples the PREVIOUS level through a real `WGPUFilterMode_Linear` `WGPUSampler`
into the NEXT level's own single-mip-level render-attachment view. This is a genuine filtered
downsample, not a nearest-neighbor copy — proven directly by `webgpu_mipgen_test.cpp`'s hard-edged
red/blue stripe check, whose boundary destination pixel reads back an exact `(128,0,128)` 50/50
blend, not a hard unblended edge.

**Important, deliberate, honestly-documented divergence from FNA and every sibling CNA renderer.**
`VulkanRenderer`/`EasyGLRenderer` only ever regenerate mip content for a RENDER
TARGET being unbound (`vkCmdBlitImage`/`glGenerateMipmap`-on-unbind, matching real FNA3D
`OPENGL_ResolveTarget` semantics) — neither renderer, nor real FNA/XNA itself, auto-generates mip
content for a PLAIN `Texture2D`/`TextureCube` at all (mip levels beyond 0 are always
user/content-pipeline-supplied via explicit per-level `SetData()`, matching how a real XNA
content-pipeline-processed `.xnb` already carries every level pre-baked at content-build time —
there is no runtime "generate the rest from level 0" API anywhere in real XNA/FNA for a plain
texture). This WebGPU renderer's mip generation DOES generate real content automatically instead,
triggered whenever content is written to level 0: `WebGPUTextureRenderer::UpdatePixels()` (covers
both the constructor's initial pixel upload AND any later `Texture2D::SetData(level=0,...)` call,
since that XNA-layer method always re-uploads the whole level 0 via a CPU-side shadow buffer, even
for a partial-rectangle update) and `WebGPUTextureCubeRenderer::SetData(face, level=0, ...)`
(per-face, on every call). This makes WebGPU's plain-texture mip behaviour strictly BETTER (never
garbage/undefined content at level>0) but genuinely DIFFERENT from FNA and every other CNA renderer
for the identical public `Texture2D`/`TextureCube(mipMap=true)` constructor call. A later explicit
`SetData(level>0,...)` call is not retroactively protected — a subsequent level-0 write will
regenerate over it, exactly the same interaction a real mip chain has with itself.

`RenderTarget2D`/`RenderTargetCube` mip regeneration (the actual FNA-equivalent feature, matching
Vulkan/EasyGL's own render-target-only precedent) remains a separate, already-tracked, still-open
scope cut (see the `RenderTarget2D`/`RenderTargetCube` sections above) — this section's own work
does not touch either render-target class.

`webgpu_mipgen_test.cpp` (`WebGPU_MipGen` CTest, 9/9): a hard-edged red/blue vertical stripe
(deliberately NOT power-of-2-aligned with the 2:1 downsample's texel pairing, so a straddling
destination pixel actually exists) proves the genuine linear blend above for both `Texture2D` level
1 and one `TextureCube` face's level 1; a chain-downsampled level 2 (sourced from level 1, not
level 0, proving the per-level loop chains correctly) has real, plausible, non-garbage content; and
`mipMap=false` construction with non-empty pixel data does not crash.

## Implemented baseline

The initial renderer is deliberately useful rather than an empty scaffold. It currently provides:

- renderer selection and `CNA_RENDERER_WEBGPU` build wiring;
- native SDL3 surface creation for Win32, macOS/Metal, Linux/X11 and Linux/Wayland, with an
  Android native-window path compiled when the required SDL property is available;
- instance, adapter, device and queue initialization with device-lost and uncaptured-error
  reporting;
- surface capability selection, resize reconfiguration and FIFO/Immediate/Mailbox present-mode
  selection from CNA's swap interval;
- back-buffer acquisition, command encoding, color/depth/stencil clears, submission and present;
- RGBA8 `Texture2D` creation, level uploads and mip-chain allocation;
- 16-bit and 32-bit index-buffer uploads plus generic vertex-buffer uploads;
- a WGSL SpriteBatch pipeline with tint, source rectangle, destination rectangle, rotation,
  origin, layer depth, transforms, flips, linear/nearest filtering and wrap/clamp/mirror sampler
  caching;
- CNA logical-presentation modes and window/logical coordinate conversion.

## Important limitations

The desktop feature set now covers 3D (every stock effect), real instancing,
`RenderTarget2D`/`RenderTargetCube`, MSAA, `Texture3D`, mip generation, the full render state
(blend, rasterizer/cull, viewport, scissor, depth-stencil), `Texture2D`/`TextureCube`/backbuffer
readback (`WEBGPU-51`), and -- since 2026-08-26 -- the browser path (`WEBGPU-119`–`122`). Those are
described in their own sections above and are no longer "limitations". What is **genuinely still
open** in `plans/plan_webgpu.md`:

- **Multiple simultaneous render targets (MRT)** -- infrastructure not built (`WEBGPU-85`/`86`/`87`).
  The capability now truthfully reports false and a `count > 1` bind throws a
  `System::NotSupportedException` (`WEBGPU-134`), rather than claiming MRT and then refusing it.
- **Real GPU-native compressed texture formats** (`WEBGPU-111`) -- a cross-renderer/XNA-layer gap,
  not WebGPU-specific: no CNA renderer does real block-compressed GPU upload today (`Texture2D`
  CPU-decompresses DXT to RGBA8 first, and the common `ImageData` struct has no compressed-format
  field). The dev machine's adapter does support `WGPUFeatureName_TextureCompressionBC`, so this is a
  design task, not a hardware dead end.
- **Per-`RenderTarget2D` `multiSampleCount`** -- a target's own constructor sample count is ignored;
  it mirrors the renderer's global sample count instead. Backbuffer and render-target MSAA otherwise
  work end to end (`WEBGPU-58`, `WebGPU_Msaa` 6/6).
- **Custom WGSL effects** (`ShaderEffect`, `WEBGPU-76`) and custom SpriteBatch effects -- the renderer
  accepts the stock effects but does not compile user-provided WGSL source.
- `TextureCube`/`RenderTargetCube` mip regeneration.

**Occlusion queries are supported** (`WEBGPU-84`): `SupportsCapability(OcclusionQuery)` reports true,
`CreateOcclusionQuery()` returns a real query backed by a `WGPUQuerySet`, and the sample count is
exact -- a fully occluded draw reads back 0 and a visible one a full target of samples
(`WebGPU_OcclusionQuery`). A query whose draws span more than one render-pass segment records only its
first segment.

`FillMode::WireFrame` is deliberately **not** on the open list: it is not "unimplemented" but
**reported unsupported and refused** (`WEBGPU-115`) -- the same shape as the MRT (`WEBGPU-134`)
capability answer above.

`GetBackBufferData()` and a first real 3D draw path (`DrawColoredPrimitives`/
`DrawIndexedColoredPrimitives`, with genuine depth testing) are implemented — see below. Interface
methods still not overridden by this renderer retain the common renderer's existing unsupported/
default behavior (mostly silent no-ops, by `IGraphicsRenderer`'s own design for state setters).

### `FillMode::WireFrame` is reported as unsupported and refused (`WEBGPU-115`)

wgpu-native has **no polygon-mode API at all**: `WGPUPrimitiveState` carries topology, strip index
format, front face and cull mode, and nothing that selects how a polygon's interior is filled. There
is therefore no native state a wireframe request could reach.

**What the renderer does now.**

| Step | Behaviour |
|---|---|
| `GraphicsDevice::SupportsCapability(GraphicsCapability::WireFrame)` | **`false`** — asserted by `WebGPURenderer`, not inherited from `IGraphicsRenderer`'s permissive default |
| Selecting a `RasterizerState` whose `FillMode` is `WireFrame` | **Succeeds.** Setting state is a state operation; a state setter cannot know whether a draw will follow, or which route it would take |
| The first **polygon** draw that would consume it | Throws `System::NotSupportedException` before any command is queued, any pipeline key is computed, any `WGPURenderPipeline` is created, any render pass is encoded and anything is submitted |
| The refused draw's target | **Unchanged.** Nothing is written, nothing is created, nothing is retained |
| The next `FillMode::Solid` draw | Renders exactly, on the same device, with no recreation and no extra frame |
| A `LineList`, `LineStrip` or `PointListEXT` draw under `WireFrame` | **Accepted.** A fill mode selects how a *polygon's interior* is rasterized; a line or point has no interior, so `Solid` and `WireFrame` are the same request and this renderer substitutes nothing. Measured byte-identical under both modes |

The refusal covers every public 3D draw route -- ordinary non-indexed and indexed (16- and 32-bit,
with nonzero `vertexStart` / `startIndex` / `baseVertex`), both `DrawUserPrimitives` /
`DrawUserIndexedPrimitives` families, every stock-effect family, and the instanced route.

**Why this is a refusal rather than a documented deviation.** Until `WEBGPU-115` the renderer
reported `WireFrame` as **supported**, accepted the request without a throw, warning or log, folded
the `wireframe` bit into `Make3DPipelineKey` so a distinct `WGPURenderPipeline` was built and
natively submitted, and returned a frame **byte-identical to the `Solid` one**. A prose note in this
document is not reachable through the public API and was directly contradicted by the capability
query, so callers had no way to find out. A renderer must not report a capability as supported while
silently substituting a different rendering mode.

**Implementing real wireframe** would mean index-expanding triangles into line topology, since
wgpu-native offers no polygon mode. That is a genuine implementation task, tracked separately; it is
not what `WEBGPU-115` did.

## Architecture notes

WebGPU and Vulkan are conceptually related explicit APIs, so the Vulkan renderer informed resource
lifetime, surface recovery, command encoding and render-pass structure. The implementation is not a
line-by-line Vulkan translation:

- WebGPU uses WGSL shader modules rather than CNA's Vulkan SPIR-V modules.
- Resource bindings use bind-group layouts and bind groups rather than Vulkan descriptor sets.
- WebGPU has no push constants, so future 3D effect data will use uniform buffers.
- Pipeline state is largely immutable and must eventually be represented in a pipeline cache.
- Native surface creation is performed directly from SDL3 window properties; CNA does not require
  the separate `sdl3webgpu` compatibility library.

The deliberate, collected departures from the Vulkan renderer — push constants → UBO, wireframe
refusal, async → synchronous callback pumping, `Color` → `Unorm8x4` vertex format, the
`SetStringMarkerEXT` no-op, and windowing handled by the shared platform — are documented in
`docs/webgpu-vs-vulkan-deviations.md`.

See `plans/plan_webgpu.md` for task-level status and the remaining parity work.
