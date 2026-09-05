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

## Running the full WebGPU test suite

The WebGPU CTests create a real GPU-backed surface (wgpu-native needs a real adapter), so the DISPLAY
they use is fixed at configure time via `CNA_TEST_DISPLAY` (each test's CTest `ENVIRONMENT` sets
`SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}`). The reproducible configuration is:

```bash
cmake -S . -B cmake-build-webgpu \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_TEST_DISPLAY=:0 \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-webgpu -j"$(nproc)"
ctest --test-dir cmake-build-webgpu -L WebGPU --output-on-failure -j1
```

`:0` is a real desktop with a GPU. In a headless sandbox use a **GPU-backed virtual display** instead
— an `Xvfb` started with the GLX extension on a host whose render node (`/dev/dri/renderD*`) exposes a
real Vulkan adapter (e.g. `Xvfb :131 -screen 0 1280x1024x24 +extension GLX`), configured with
`-DCNA_TEST_DISPLAY=:131`. A plain software `Xvfb` (no render node / no adapter) has **no** WebGPU
adapter and the readback tests fail for that reason alone — that is an environment limitation, not a
renderer defect. (`WebGPU_ChecksumVerification` and `WebGPU_PresentModeMapping` are pure CMake/unit
tests and run with no display or GPU at all.)

Known gap: outside these two configurations, a no-display / no-adapter host makes the GPU-backed tests
FAIL rather than SKIP. A shared preflight that returns the CTest skip code (77) when no usable
display/adapter is present is not yet wired for the whole suite (only the native smoke test skips
cleanly today) — tracked as WebGPU test-infra follow-up.

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
verified. (At the time of this 2026-07-12 record, 3D effects, render targets and MRT were still open;
all have since shipped — see the "Important limitations" section below for the current boundary.)

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
the depth portion. Stencil ops (`WEBGPU-83`) are now implemented across **every 3D family**. Each
`GetOrCreatePipeline*3D()` bakes the XNA `DepthStencilState` stencil parameters into
`WGPUStencilFaceState` (via `ToWGPUStencilOperation` / `FillWGPUStencilState`), captures the state
per draw in its `*DrawCommand` (`CaptureStencilStateEXT`), folds the read/write masks into that
family's pipeline cache key (WebGPU keeps the masks as *pipeline* state, unlike Vulkan's dynamic
masks; a disabled stencil folds a constant, so non-stencil draws keep their existing keys), and
applies the reference dynamically per draw (`wgpuRenderPassEncoderSetStencilReference`). This makes
a stamp-then-gate stencil sequence (e.g. `Always`/`Replace` then `Equal`/`Keep`) work within one
render-target bind cycle. Three tests prove it on the real GPU: the shared
`rendertarget_depthstencil_usage` acceptance test (colored3d route, its `stencilInRT`/
`stencilPreserves` flags flipped true, discriminating check C2); the WebGPU-local
`WebGPU_StencilFamily` test (Textured3D route — stamps then gates and asserts the gate is *rejected*
outside the stamped region); and `WebGPU_StencilTwoSided` (`DepthStencilState.TwoSidedStencilMode` —
a back-facing triangle picks up the `CounterClockwise*` ops, differential vs the two-sided=false
control, matching the EasyGL cross-renderer parity contract). The two-sided front/back mapping
(front = the primary/CW ops → `stencilFront`, back = the CCW ops → `stencilBack`) is therefore
pixel-verified, not merely implemented.

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
time) renders into it unchanged, with zero new pipeline-cache dimensions; and its own depth
texture created in the exact format the requested `DepthFormat` maps to (`WEBGPU-39`,
`MapDepthFormatEXT()`: `None`→no depth texture, `Depth16`→`Depth16Unorm`, `Depth24`→`Depth24Plus`,
`Depth24Stencil8`→`Depth24PlusStencil8`, the same per-value mapping `VulkanRenderer::PickDepthFormat()`/
EasyGL/Bgfx do). The pass's depth attachment, the pipeline's `depthStencil` state (null for `None`,
where no depth test happens) and the stencil load/store ops (named only on a stencil-carrying format)
are all threaded from that real format via `replayDepthFormat_`/`replayDepthHasStencil_`, and the
format is part of the 3D and sprite pipeline keys so a pass with a different depth format gets its own
pipeline. Observably verified by `WebGPU_DepthFormat`.

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

Mip-chain regeneration was deferred here until `WEBGPU-164` (2026-09-05) — `CreateRenderTarget2D()`
threw a clear error for `mipMap=true` rather than under-delivering a chain the XNA layer had already
promised. It is implemented now: see *Mip-mapped RenderTarget2D* below. MSAA (`WEBGPU-58`) is implemented
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
no `TextureCube` support whatsoever; this cube-sampling slice was deliberately minimal at the time
(no `GetData()`, no `RenderTargetCube`, mip regeneration untested), all of which have since been added
(`GetData` in `WEBGPU-113`, `RenderTargetCube` render targets + mip regeneration in `WEBGPU-114`, plain
mip generation in `WEBGPU-52`). `WebGPU_EnvMap3D`
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
shared `size`×`size` depth texture — created in the exact format the requested `DepthFormat` maps to
(`WEBGPU-39`, `MapDepthFormatEXT()`; `None` allocates no depth texture at all) — reused across all 6
faces, safe because only ONE face is ever bound/rendered-into at a time, mirroring
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

Per-face MSAA and `mipMap=true` mip regeneration are both implemented (`WEBGPU-114`, closed
2026-08-27). MSAA: when the renderer's global `sampleCount_` > 1 each face renders into its own
multisampled colour attachment and resolves into that face's single-sample layer — SIX separate
single-layer MSAA textures, because WebGPU forbids a multisampled ARRAY texture (unlike Vulkan's
6-layer image) — with the shared depth attachment allocated at the same sample count;
`GetMultiSampleCount()` reports the applied count, mirroring `RenderTarget2D` (the per-instance
`multiSampleCount` argument is not read — the cube multisamples only when the backbuffer was created
multisampled). mipMap: the colour texture carries a full mip chain and each face is regenerated from
its resolved level 0 after that face's render pass (`GenerateMipsForLayer`, the `WEBGPU-52`
downsample cascade, parameterized by colour format so a `surfaceFormat_`/BGRA cube target works);
MSAA and mipMap compose — the resolve writes level 0, then the cascade downsamples it — and `GetData`
accepts levels 0..`LevelCount`-1. `WebGPU_RenderTargetCube` (18/18) verifies: 6-face direct
face-to-face switching, a real `BasicEffect` 3D draw into a face, the `EnvironmentMapEffect` sampling
round trip above, the critical "an intervening cube-face-targeted `Clear()` must not leak into the
backbuffer's own render pass" architecture check (mirrors the `RenderTarget2D` section's own Check E),
a mipMap=true chain with a genuine level-1 downsample (Check E), 4x per-face MSAA resolve with no
cross-face leak and blended edge pixels (Check F), and MSAA+mipMap combined.

A finding surfaced while writing this test — that `QueueSprite()` computed every sprite's clip-space
geometry from the BACKBUFFER's logical dimensions unconditionally, never from the currently-bound
render target — **has since been fixed** (REMED-GFX-019: `QueueSprite` now derives clip space from the
bound target's own dimensions; the letterboxed-backbuffer edge was closed by `WEBGPU-141`(A)). A
`SpriteBatch.Draw()` into an off-screen `RenderTarget2D`/cube face now maps 1:1 into that target's own
pixels, verified by `WebGPU_SpriteBatch_RenderTarget`. (The historical note that
`webgpu_rendertargetcube_test.cpp` Check C paints with a 3D draw to avoid this remains true of that
test, but is no longer a workaround for a live bug.)

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

## Shader sources and whole-set validation (WEBGPU-28)

Every WGSL shader source lives in one place -- `include/CNA/Internal/Renderers/WebGPU/webgpu_shaders.hpp`
-- as `inline constexpr char k*[]` constants (`kSprite`, `kColored`, `kPbr`, `kSkinnedPbr`, `kMipBlit`,
…). `WebGPURenderer.cpp` references them by a `const char*` alias where each literal used to be inline,
so the compiled shader bytes are unchanged (the whole pixel suite still passes). A `kDirectShaders`
registry lists the 18 directly-compiled sources; `kPbr`/`kSkinnedPbr` are marked templates expanded at
runtime by `ExpandPbrVertexColourWgslEXT` into a bare and a vertex-colour variant.

`WebGPURenderer::ValidateAllShadersEXT()` compiles the WHOLE set (22 modules: the 18 direct sources plus
the 4 Pbr/SkinnedPbr variants) through the device inside a `WGPUErrorFilter_Validation` error scope and
returns the number that failed. Because most stock shaders are already compiled at device init
(`ConfigureSurface`), the value this adds is catching an error in a shader a given scene never draws --
the lazy mipBlit shader, or any unused effect -- as a single deterministic failure rather than at that
effect's first pipeline creation. It runs at device init when `CNA_WEBGPU_VALIDATE_SHADERS` is set (off
by default -- compiling ~24 modules is not free), and is exercised unconditionally by the
`WebGPU_ShaderValidation` CTest.

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

## Semantic vertex layouts for the stock 3D families (2026-09-04, `WEBGPU-155`–`159`)

Until `WEBGPU-155` this renderer chose a stock shader family, and that family's whole
`WGPUVertexAttribute` array, **from the vertex buffer's byte stride**. A stride is not a layout:

* the same semantic content is legally described at several strides (padding, an unused element, a
  different vertex struct) — a `Position+Colour` record padded to 32 bytes was read as
  `VertexPositionNormalTexture`, so its declared colour was dropped and twelve padding bytes were lit
  as a normal;
* the same stride legally describes different semantic content — 24 bytes is
  `VertexPositionColorTexture` *and* the `Position+Normal` vertex Microsoft's own Primitives3D sample
  uses, which must be lit;
* strides the table did not list were refused outright — including 36, which is exactly what the
  stock XNA `ModelProcessor` emits for a mesh with a colour channel, and 40, the canonical
  `PositionNormalDualTexture`.

The dispatch now asks the **declaration**. `WebGPURenderer::SelectStockVertexShapeEXT()` picks the
family from the semantics the declaration names plus the effect state, and
`ResolveStockVertexLayoutForDrawEXT()` binds each of that family's inputs by `(usage, usageIndex)` at
the declared element's own offset and format. The stride survives as exactly one thing: the
pipeline's `arrayStride`. The resolver itself is renderer-neutral
(`modules/graphics/include/CNA/Internal/Graphics/StockVertexSemantics.hpp`), so a second renderer
adopting it gets the same bindings from the same declaration rather than a second interpretation.

Element **order** is unrestricted, deliberately: XNB model data routinely orders
`TextureCoordinate` before `Normal`, and a renderer that keys on list position reads such a mesh
from the wrong bytes. What is still checked, before anything native exists, is the **format** of each
semantic the selected program consumes (`RequireDeclarationMatchesStockProgram`).

A semantic a program declares but the declaration does not supply — `DualTextureEffect`'s
`TEXCOORD1` on a single-UV mesh, a colour input on a mesh with no colour channel — reads a shared
16-byte **neutral record** holding `(0, 0, 0, 1)`, bound at vertex-buffer slot 1 with per-instance
step so every vertex of the single-instance draw reads it. That value is not a convenience: D3D9,
which XNA is defined against, fills a vertex register's missing components with `(0, 0, 0, 1)`, and
OpenGL's disabled generic vertex attribute — what the reference renderer leaves such an input at —
has the same default.

Shader changes that came with it: the two lit families gained a colour input at `@location(3)`
(XNA's `VSBasicVertexLightingVc` family — the vertex colour multiplies the diffuse result *before*
the specular term is added), and both dual-texture families gained `TEXCOORD1` so the overlay is
sampled with its own UV set.

**What was deliberately NOT converted**, and still selects its attribute array from the stride with
`REMED-GFX-DECL-GUARD`'s refusal keeping it safe: the **skinned** and **PBR** routes. `WEBGPU-177`
owns their conversion. A declaration those routes cannot represent is refused by name, exactly as
before. (The **instanced** route was on this list until `WEBGPU-172` converted it — see below.)

Verified by the shared EasyGL↔WebGPU parity fixtures (`WEBGPU-207`, see
[`cross-renderer-parity-fixtures.md`](cross-renderer-parity-fixtures.md)) plus the renderer-neutral
`VertexDeclarationLayoutTests`, where WebGPU moved from the refusing arm to the translating one.

## Multiple vertex streams (2026-09-05, `WEBGPU-172`)

`GraphicsCapability::MultiStreamVertexInput` is **true**. XNA's `SetVertexBuffers` takes an array
because a vertex's elements may live in several buffers, each with its own `VertexDeclaration`,
stride, `VertexOffset` and `InstanceFrequency`; until this task the capability fell through to the
shared `false` default and `GraphicsDevice::ValidateVertexStreamCapability()` refused such a draw
before submission — truthful, but a refusal of an ordinary XNA shape.

The implementation is WebGPU's native model rather than an emulation: one `WGPUVertexBufferLayout`
per **resolved** stream, each with that stream's own `arrayStride` and step mode. The resolver
(`ResolveStockVertexLayoutAcrossStreamsEXT`) searches every bound declaration for each of the chosen
program's inputs, so an input is bound to whichever buffer declares its semantic, and a stream that
supplies nothing the program consumes never reaches the pipeline at all. Native slots are therefore
assigned **densely over the streams a draw actually reads**, not by public binding slot: XNA lets a
draw bind slots 0 and 15 while using two streams, and taking the public slot as the native index
would need sixteen native buffers to describe a two-buffer draw. The neutral record moves to the
slot after the real streams (slot 1 for a single-stream draw, exactly where it was).

Program **selection** widened with it: "is this a lit vertex?" is now "does *some* bound stream
declare a `Normal`?". Asking only slot 0 picks an unlit program for a mesh whose normals are simply
in another buffer — and for the canonical split (position-only at stride 12, colour-only at stride 4)
neither stream alone is a layout any renderer recognises.

The **instanced** route was converted in the same task, with one deliberate asymmetry. Its
per-vertex inputs (`POSITION0`, and `COLOR0` when a stream declares one) are resolved by semantic
like every other family. Its per-instance world-matrix columns are resolved **positionally** — the
k-th element across the concatenated per-instance declarations feeds column k. That is the reference
renderer's own rule (`EasyGLRenderer::PlaceInstanceStreams` assigns consecutive locations from
`kStockInstanceBaseLocation` in declaration order, whatever semantic each element names), and it is
the only rule both existing corpora satisfy: the shared oracle spells the columns
`TEXCOORD1`–`TEXCOORD4` while this renderer's own instanced examples spell them
`POSITION1`–`POSITION4`. A world matrix has no XNA-defined semantic to be faithful to, so matching
the reference is the answer rather than picking one spelling and breaking the other. The columns may
still be split across buffers — the shared oracle splits them 48 + 16 bytes. A classic instanced
draw resolves to exactly two streams and produces the same binding pair the family always built by
hand, geometry at slot 0 and the matrix at slot 1.

A buffer made through the low-level `IGraphicsRenderer::CreateVertexBuffer(count)` entry point
carries no declaration at all, which this renderer's own instanced examples rely on.
`SynthesizeMissingStreamDeclarationsEXT` gives such a stream the canonical layout for its stride —
position-only for a stride the table does not list, which is what this route has always assumed —
and a per-instance one the four `Vector4` columns at 0/16/32/48, exactly the attribute array the
family used to hardcode.

`InstanceFrequency` greater than one has no native counterpart — in wgpu-native v29.0.1.1
`WGPUVertexBufferLayout` carries only `nextInChain`/`stepMode`/`arrayStride`/`attributes`,
`WGPUVertexStepMode` offers only `Vertex` and `Instance`, and no `WGPUNativeFeature` adds a step
rate. It is honoured by **materializing** the records the draw will read: instance *i* reads source
record `VertexOffset + i / frequency`, so each source record is repeated `frequency` times at queue
time and a divisor-of-one binding reads exactly what a divisor-of-`frequency` one would. The
frequency therefore never reaches the pipeline or its cache key.

`GetMaxVertexStreams()` is the device's own `maxVertexBuffers` limit less the one slot reserved for
the neutral record, clamped to the resolver's stream table — 7 on the development machine, and never
a constant.

The pipeline cache key carries each stream's **input rate**, never its frequency: a native
vertex-buffer layout has a step mode and nothing finer, so letting the frequency into the key would
compile a second identical pipeline for the same geometry at frequency 2.
`WebGPU_InstancedOffsetFrequency_Cardinality` counts pipeline variants and is what measures it.

**Still refused, by name:** a draw that splits its vertex across bindings on the **skinned**/**PBR**
families or under a custom WGSL `ShaderEffect`. Those routes still derive their layout from one byte
stride, so they would read a split vertex from the first stream alone; `RequireSingleStreamRouteEXT`
says so rather than rendering a subset of the bound streams. `WEBGPU-177` converts the first pair.

Verified by the two shared multi-stream oracles (`OrdinaryDrawMultiStreamTests`,
`InstancedDrawMultiStreamTests`, 48 cases) and by the `multi_stream_split` parity fixture, whose
EasyGL and WebGPU frames are byte-identical.

## Mip-mapped RenderTarget2D (2026-09-05, `WEBGPU-164`)

`RenderTarget2D(..., mipMap: true)` used to throw. It now allocates the chain `CalculateMipLevels`
declares and regenerates levels 1.. from level 0 **when the target is unbound** — FNA3D's
`ResolveTarget` timing, and the same rule `WebGPURenderTargetCubeRenderer` (`WEBGPU-114`) already
followed per face. The cascade is the existing `GenerateMipsForLayer` render-pass downsample, so no
new machinery was added; MSAA composes the way it does for the cube target, because the resolve
writes level 0 and the cascade then reads it.

Two details are worth naming. The colour texture now needs **two views**: `colorView_` spans the
whole chain and is what a shader samples, while a new `colorLevel0View_` names exactly one level and
is what the render pass attaches (and what an MSAA pass resolves into) — a colour attachment may name
only one mip level. And `GetData(level)` reads that level's own dimensions rather than the target's,
so a 2×2 level returns four texels instead of a level-0-sized copy.

Implementing this exposed two latent bugs, both unreachable before a mipped target could be part of
an MRT set. The shared mip-blit pipeline is cached **by format alone** but was built with
`InitStockColorTargetsEXT`'s attachment count — the count of whatever MRT set happened to be bound —
so a first creation while a two-target set was bound baked a two-target pipeline and reused it for
every later single-attachment blit. It is now always exactly one target, which is what a blit pass
has by construction. And `~WebGPURenderTargetRenderer` cleared `currentRenderTarget_` while leaving
`mrtExtraTargets_` holding the surviving slots of a set whose slot 0 no longer existed, dropping the
queued draws and leaving the next flush to build an N-attachment pass around a null slot 0; the
destructor now flushes the set first, while every attachment is still alive, and then drops the
binding whole. Mip regeneration also runs for **every** slot of an MRT set rather than slot 0 alone.

Verified by `WebGPU_RenderTarget2D` Check G (a 32×32 target painted in two halves; level 4 keeps
both), by the `render_target_mip` parity fixture — a 64×64 target painted in four quadrants, with
`GetData` asserted at both ends of the chain and the target sampled back both unrestricted and pinned
to its coarsest level, **byte-identical to EasyGL, max diff 0** — and by the three shared
render-target suites (`rendertarget_msaa_mip_readback`, `rendertarget_msaa_depth_contract`,
`rendertarget_first_use`) whose WebGPU "declares mipped targets unimplemented" declarations were
deleted rather than left describing a boundary that no longer exists.

## The pipeline key and the bound target (2026-09-05, `WEBGPU-197`, partial)

A WebGPU render pipeline bakes its colour target's **format** and **sample count**. All twelve of
this renderer's 3D families read the swap-chain `surfaceFormat_` and one renderer-global
`sampleCount_` instead of the pass they were about to be used in, which is why a `RenderTarget2D`
could not honour its own `multiSampleCount` and why no non-`Color` target could ever be drawn into.

`ReplayDrawsInOrder` now records the pass's own colour format and sample count — alongside the depth
format it already recorded — and `Build3DPipelineEXT` reads those, with both folded into every 3D
pipeline cache key. `PassDestination` already carried the values per target, and the SpriteBatch path
had keyed on them since it was written; this is the 3D side catching up.

Both halves are finished and proven — the sample count by `WEBGPU-165` below, the colour format by
`WEBGPU-198`, which makes a non-`Color` target creatable. The measurement that matters for this
section is the 3D-draw leg of `RenderTargetFormatAgreement`: a pipeline built against the swap
chain's format and then used in a float target's pass is a **hard native error** on this pin
(`"Render pipeline targets are incompatible with render pass"` — demonstrated the same day by the
mip-blit bug `WEBGPU-164` found), so eight such draws landing is the key change working rather than
an absence of evidence.

The `WEBGPU-58` finding had to be settled first, because per-pass sample-count variants built from
shared shader modules are exactly the reuse that task measured as silently wrong on this pin — no
validation error, correct-looking draw, wrong pixels — which is why `ClearAllPipelineCaches()` tears
down every module and layout when the global sample count changes. It was **re-measured and does not
reproduce**: `WebGPU_MsaaModuleReuseProbe` uses one module set for a 1-sample pipeline and then a
4-sample one and compares that frame against a 4-sample frame from a fresh set — **max channel
difference 0**, stable across runs, with non-vacuity asserted separately so two empty frames cannot
pass. The probe asserts the equality rather than printing it, so the hazard returning on a new pin
goes red rather than corrupting frames quietly.

One methodological note from that probe, because its first version gave a confident wrong answer:
mapping the readback buffer is not enough here. The map callback can fire while the copy that fills it
is still queued, and the readback then returns an all-zero frame — precisely the "pipeline fine, draw
fine, no validation error, only the pixels wrong" signature `WEBGPU-58` recorded. Waiting on
`wgpuQueueOnSubmittedWorkDone` first is what made the answer trustworthy. That is a plausible
explanation for the original finding, not a demonstrated one.

## Render-target colour formats (2026-09-05, `WEBGPU-198`)

`ClassifyRenderTargetFormatEXT` is overridden here and answered by a **device probe**, not a table: a
1×1 `WGPUTextureUsage_RenderAttachment` texture of the real native format is created inside a
`WGPUErrorFilter_Validation` scope — the technique `Supports4xMsaa()` already used — and the answer
is cached per format. On this adapter all six float formats come back renderable, so WebGPU reports 8
of 27 `SurfaceFormat` values renderable against EasyGL's 9.

The map is the reference renderer's own eight-format set: `Single`→`R32Float`,
`Vector2`→`RG32Float`, `Vector4`→`RGBA32Float`, `HalfSingle`→`R16Float`, `HalfVector2`→`RG16Float`,
`HalfVector4`/`HdrBlendable`→`RGBA16Float`. `Color` is deliberately absent from it — it maps to the
live `surfaceFormat_`, which keeps every existing `Color` target byte-identical to its earlier form.
`Rgba64` maps to `RGBA16Unorm`, which is **not** core WebGPU — natively it needs wgpu-native's
`WGPUNativeFeature_TextureFormat16bitNorm` (which lives in `wgpu.h`, absent from the browser's
emdawnwebgpu, so the whole path is native-only), and a browser would need `TextureFormatsTier2`,
which none exposes yet. It is in the map unconditionally anyway: whether it *works* is the probe's
question, not the table's.

The measured answer here (`WEBGPU-201`) is worth stating exactly, because a feature check alone gets
it wrong in both directions. **This adapter has `TextureFormat16bitNorm`** and the device requests
it — so "the adapter lacks it" is false. And `RGBA16Unorm` still is not a render target here: with
the feature enabled, creating one with `TextureBinding` usage succeeds while the same texture with
`RenderAttachment` usage fails. So "the adapter has it, therefore it works" is false too. `Rgba64` is
refused as a render target for a **usage** boundary, not a missing feature, and
`GetAdditionalLimitationsTextEXT()` says which of the two applies.

`CreateRenderTarget2DEXT` is overridden so the target is **allocated in the format it was asked
for** — `IGraphicsRenderer`'s default forwards to the format-less overload and drops the argument,
which is exactly what would let a target be classified in one format and allocated in another — and
an `Unsupported` format is refused there by name.

**Every render-target transfer is sized from the format** (`WEBGPU-202`), and the pipeline key covers
**every** MRT slot's format, not slot 0's alone — two sets differing only in slot 1 would otherwise
share a pipeline, which the native layer rejects outright (`"Incompatible color attachments at
indices [1]"`). Clear values needed no work: `WGPURenderPassColorAttachment.clearValue` is four
doubles, so a 2.0 clear reaches an RGBA16Float target unclamped. Resolve needed none either — a
multisampled float target resolves here. `UpdatePixelsLevel()`'s fixed four-byte texel is left alone
deliberately: it belongs to a plain `Texture2D`, and `Texture::ValidateFormat` still admits `Color`
alone for those on every renderer, so widening it would be unreachable code.

**The readback is typed by the target's format.** It assumed four UNORM8 bytes per texel, which held
only while a render target could only be `Color`; the width now comes from the format (2/4/8/16) and
the BGRA swizzle applies to BGRA8 alone, since reordering a float texel's bytes would corrupt it.
That was not optional: nine `HdrRenderTargetRoundTripTest` cases had been skipping on
`SupportsSurfaceFormatAsRenderTargetEXT` and started running the moment float targets became
creatable. Five of them failed against an earlier cut of this work that refused the float readback —
a game could query the format, create the target, render HDR into it, and then not read it back,
which is the shape `WEBGPU-163` exists to prevent. All nine pass now, including values above 1.0
surviving, a multisampled float target resolving without clamping, and a float target generating a
mip chain (this section and `WEBGPU-164` composing).

The **cube** path carried the same defect and got the same fix: `CreateRenderTargetCubeEXT` was not
overridden, so a `RenderTargetCube(HdrBlendable)` reported the float format it was asked for while
holding 8-bit texels — MOD-107's silent substitution, in the one path image-based lighting depends
on, and passing its own test only because that test does not read values back.

**32-bit float targets and their two optional features** (`WEBGPU-200`). `R32Float`, `RG32Float` and
`RGBA32Float` are renderable in core WebGPU, but *sampling* one with a filtering sampler needs
`WGPUFeatureName_Float32Filterable` and *blending* into one needs `Float32Blendable`. Both are
requested at device creation when the adapter offers them — this one has both. Where they are
absent, neither refuses the format: the targets stay renderable, drawable, clearable and readable,
and only the feature-dependent operation is refused, **by name**. A non-opaque `BlendState` on such a
target throws naming `Float32Blendable`; a filtering sample throws naming `Float32Filterable` and
says that `TextureFilter::Point` samples it without the feature.
`GetAdditionalLimitationsTextEXT()` names whichever is missing. That text is **composed** from every
applicable clause rather than returning the first one — it returns a single string, and a device can
be subject to several of these boundaries at once; an early-return chain silently hid the float32
clauses the moment the `Rgba64` one was added in front of them.

Both refusals sit at the **public draw entry**, not in the pipeline builder. That distinction was
found the hard way: a builder only runs on a cache miss, so a refusal written there fired for the
first such draw and then silently stopped firing for every later one that hit the pipeline cache.
`WebGPU_Float32Features` blends into the same target twice, which is what caught it. That test also
exists because every adapter here *has* both features, so these branches would otherwise never
execute — `DebugForceFloat32FeaturesAbsentEXT` makes the renderer report them absent without removing
the real device features, so what runs is CNA's own decision-making.

The row's alternative — binding an unfilterable-float view with a non-filtering sampler instead of
refusing — is deliberately not implemented. It needs a second bind-group layout declaring
`UnfilterableFloat`/`NonFiltering` and cannot be verified on any adapter available here, so a named
refusal a caller can act on was chosen over an unverifiable bind path.

One divergence is recorded rather than encoded: EasyGL samples an `R16Float` target back as
(255,255,255) and `RG16Float` as (255,0,255), where WebGPU gives (255,0,0) for both — GL broadcasting
a one-channel texture against WGSL's `texture_2d<f32>` returning `(r, 0, 0, 1)`. The shared test
asserts the red channel only, which every format in that family carries, so it bakes in neither
renderer's swizzle. Which one XNA means is `WEBGPU-199`/`200`'s to settle.

## Per-target MultiSampleCount (2026-09-05, `WEBGPU-165`)

`RenderTarget2D` and `RenderTargetCube` honour their constructor's `multiSampleCount`. Both used to
discard it and mirror the renderer-global `sampleCount_` unconditionally — necessary while every
pipeline baked one global count, since a target that opted out would have been pipeline-incompatible
the moment anything drew 3D into it, but observable through the public property with nothing rendered:
an explicit request for no multisampling reported 4 on a device where the global probe chose 4.

Each target now clamps its own request through the adapter probe, allocates its MSAA colour attachment
— and its depth attachment, which must agree — at that count, and reports it. The clamp itself was
wrong and is fixed: `PickSampleCount` answered 4 for *any* request of 2 or more, which is an increase
rather than a clamp, so a caller asking for 2 got 4. It now rounds **down** to a probed count, which is
what the shared `applied <= requested` contract requires.

`webgpu_msaa_test` Check D measures it in one frame with the backbuffer at 4×: a target requesting 0
reports 0 **and renders a binary diagonal**, while a sibling requesting 4 reports 4 **and blends its
diagonal**. Two live targets, two sample counts, both the property and the edge pixels — which also
exercises the re-measured module-reuse path for real.

## Device loss: what the pin actually does (2026-09-06, `WEBGPU-180`)

Measured against **wgpu-native v29.0.1.1**, not read from the header, by
`spikes/webgpu-devicelost-spike/` — which carries the full transcript and the reasoning. The four
facts a caller needs:

**A device replace is renderer-internal.** `WGPUInstance`, `WGPUAdapter` and `WGPUSurface` all
survive it: a second device requested from the same adapter can re-configure the *same* surface and
acquire from it. Recovery never has to reach back into `CNA::Platform` or rebuild the window.

**A lost device must be gated BEFORE the acquire, never after.** `wgpuSurfaceGetCurrentTexture` on a
surface whose device is lost does not return a failure status — it panics inside wgpu-native
(`Parent device is lost`) and, because the panic cannot unwind across the C ABI, **aborts the
process**. There is nothing to read and nothing to catch. `CanBeginDrawEXT()` returning false is what
stands between a lost device and that abort; it is a safety mechanism, not a convenience for `Game`.

**On native, the device-lost callback never fires.** For an application-initiated `wgpuDeviceDestroy`
this pin delivers no `WGPUDeviceLostCallback` at all — not under `AllowProcessEvents` with the
instance pumped, not with `wgpuDevicePoll` on the device, not on releasing the last reference, and
not under `AllowSpontaneous`. The renderer therefore raises `RendererDeviceEvent` itself at the point
it destroys the device. This is a statement about v29.0.1.1, not about WebGPU; a later pin may change
it, and the spike is the check.

**On the web target it does fire, and that asymmetry is deliberate.** `emdawnwebgpu` bridges the real
`GPUDevice.lost` promise to the C callback, and `wgpuDeviceDestroy` is literally `device.destroy()`,
which the WebGPU specification resolves with reason `"destroyed"`. The browser's reason vocabulary is
narrower than the header's — only `Unknown` and `Destroyed` are reachable from a browser reason
string. This half is derived from the port's own JavaScript, not confirmed in a browser; that
confirmation belongs with `WEBGPU-196`.


## Important limitations

The desktop feature set now covers 3D (every stock effect, with FNA fog parity), real instancing,
`RenderTarget2D`/`RenderTargetCube`, MSAA (backbuffer + `RenderTarget2D`), `Texture3D`, mip generation,
the full render state (blend, rasterizer/cull, viewport, scissor, depth-stencil incl. full stencil ops),
`Texture2D`/`TextureCube`/backbuffer readback (`WEBGPU-51`), MRT, occlusion queries, custom WGSL effects,
GPU-native compressed textures, and -- since 2026-08-26 -- the browser path (`WEBGPU-119`–`122`). Those
are described in their own sections above and are no longer "limitations". What is **genuinely still
open** in `plans/plan_webgpu.md`:

(Per-target `multiSampleCount` on `RenderTarget2D` and `RenderTargetCube` was on this list until
`WEBGPU-165`; see *Per-target MultiSampleCount* above.)

**Multiple render targets are supported** (`WEBGPU-85`/`86`/`87`): `SupportsCapability(MultipleRenderTargets)`
reports true, and `SetRenderTargets` binds 2..4 `RenderTarget2D` targets that share width/height/sample
count (a mismatch, a cube face, a null target, or a count > 4 is refused with a
`System::NotSupportedException`). A custom `ShaderEffect` whose WGSL fragment writes `@location(0..N-1)`
fans out to every attachment; a built-in (stock/SpriteBatch) draw writes attachment 0 only (`writeMask`
0 on the rest) -- the same "the stock pipeline writes attachment 0" behaviour every other renderer has.
Depth/stencil is single and shared by the pass. See the MRT section below for the full boundary.

**GPU-native block-compressed textures are supported** (`WEBGPU-144`): when the adapter advertises
`WGPUFeatureName_TextureCompressionBC` (requested at device creation), DXT1/3/5 and BC7 (and their
sRGB variants) upload their raw 4x4 blocks to a `WGPUTextureFormat_BC{1,2,3,7}*` texture and the GPU
decodes them at sample time -- no CPU decompression. This is the first CNA renderer to do so;
`Texture2D`'s existing compressed block-transfer contract (`IsCompressedTransferFormatEXT`) routes the
bytes, and the renderer keeps the per-mip blocks as the authoritative `GetData` store.
`WebGPU_CompressedTexture` proves a DXT1 and a DXT5 texture sample correctly and round-trip their exact
block bytes. Reachable via the direct `Texture2D(device, w, h, mipMap, SurfaceFormat::Dxt*)` +
`SetData(blockBytes, count)` API **and now via the content loaders too** (Phase 2, XNB-24):
`Texture2D::FromStream` (DDS) and the `.xnb` `Texture2DReader` keep DXT content compressed and upload
the raw blocks instead of CPU-decoding to Color. Both loaders gate on a new renderer-opt-in capability
`LoadsCompressedContentNativelyEXT()` (default false; WebGPU-only, so Skia and every other renderer
keep their existing decode-to-Color loaders) AND the per-format `IsCompressedTransferFormatEXT`, so a
loaded DXT texture keeps its `Dxt*` format exactly when the device can transfer it and decodes to Color
otherwise. `WebGPU_CompressedContent` proves both loaders take the native path (format preserved, full
mip chain, renders correctly). Baking this Phase-2 path exposed and fixed a compressed-mip upload bug:
a sub-4x4 tail mip (2x2/1x1) must be written with its **block-aligned** copy extent (`ceil(dim/4)*4`),
which wgpu-native validates against, not its logical size -- Phase 1's single 4x4 level never hit it.

**Block-compressed `TextureCube` works end to end** (`WEBGPU-206`). `CreateTextureCube` passes the
requested format through — it used to discard it, which is why every cube was RGBA8 whatever it was
asked for — and the cube is classified by the *same* `ClassifyWebGPUTextureFormat` the 2D path uses:
a cube here is a six-layer 2D texture plus a cube view, and the BC feature does not restrict block
storage to non-array 2D. Blocks upload per face and per mip through `SetCompressedDataEXT`, whole
level at a time; `GetData` returns them **decoded**, through the framework's own `DxtUtil`/`Bc7Util`,
because that is what the shared contract asks for and decoding stored blocks is not fabrication. A
DDS/XNB cube keeps its blocks, since the cube content reader has always gated on
`IsCompressedCubeTransferFormatEXT`, which this renderer now overrides to match its 2D answer.
`WEBGPU-163`'s refusal survives exactly where it should: on a device without the BC feature.

(What follows is the state before that, kept because it explains why the refusal existed.)
**Block compression was `Texture2D`-only; a `TextureCube` refused it at construction** (`WEBGPU-163`).
`CreateTextureCube` discards the requested `surfaceFormat` and `WebGPUTextureCubeRenderer` stores RGBA8,
so the support above does not extend to a cube. Until `WEBGPU-163`, one classifier answered for both
resource kinds, and a `TextureCube(device, size, mipMap, SurfaceFormat::Dxt1)` therefore **constructed**
on a BC-capable device and then threw from every `SetData`. `IGraphicsRenderer` now asks
`ClassifyTextureCubeFormatEXT()` for a cube -- defaulting to the 2D verdict, so no other renderer moved --
and this renderer overrides it to refuse every block-compressed format by name, saying in the message
that the same format remains available as a `Texture2D` so a caller can fall back deliberately. This is
an interim honest refusal, **not** a platform limitation: a BC cube is a six-layer 2D texture plus a
`WGPUTextureViewDimension_Cube` view and nothing in `WGPUFeatureName_TextureCompressionBC` forbids it.
`WEBGPU-206` is the parity implementation.

**Occlusion queries are supported** (`WEBGPU-84`): `SupportsCapability(OcclusionQuery)` reports true,
`CreateOcclusionQuery()` returns a real query backed by a `WGPUQuerySet`, and the sample count is
exact -- a fully occluded draw reads back 0 and a visible one a full target of samples
(`WebGPU_OcclusionQuery`). A query whose draws span more than one render-pass segment records only its
first segment.

**Stock-effect fog is at full parity** (`WEBGPU-145`–`148`, plus the pre-existing
`EnvironmentMapEffect` fog): every FNA stock 3D effect that exposes `FogEnabled`/`FogStart`/`FogEnd`/
`FogColor` -- `BasicEffect` (colored/textured/vertex-colour-textured/lit per-pixel + per-vertex),
`AlphaTestEffect`, `DualTextureEffect`, `SkinnedEffect` -- now applies FNA's `ApplyFog`. The design
carries the CPU-prepared FNA view-space fog vector (`EffectHelpers.SetFogVector`, already on
`GpuDrawParams.fogVector`) plus `fogColor` in the shared primary uniform block (widened 128→160 bytes);
each WGSL family computes `fogFactor = 1 - saturate(dot(vec4(pos,1), fogVector))` in the vertex stage
(the SKINNED position for `SkinnedEffect`, matching FNA) and `rgb = mix(fogColor, rgb, fogFactor)` in
the fragment stage. Tests: `WebGPU_BasicEffect_Fog`/`AlphaTestEffect_Fog`/`DualTextureEffect_Fog`/
`SkinnedEffect_Fog` (each uses `FogStart==FogEnd`→`FogColor` as the drop-the-fog discriminator).

`FillMode::WireFrame` is not on the open list either: as of `WEBGPU-153` it is **implemented and
reported true**, by triangle-edge expansion rather than by a polygon mode -- see its own section
below.

`GetBackBufferData()` and a first real 3D draw path (`DrawColoredPrimitives`/
`DrawIndexedColoredPrimitives`, with genuine depth testing) are implemented — see below. Interface
methods still not overridden by this renderer retain the common renderer's existing unsupported/
default behavior (mostly silent no-ops, by `IGraphicsRenderer`'s own design for state setters).

### `FillMode::WireFrame`, by triangle-edge expansion (`WEBGPU-153`)

A wireframe does not need a polygon mode. The reference renderer (EasyGL) has never used one:
`EasyGLRenderer::DrawWireframe` re-expands a triangle index sequence into a 32-bit `GL_LINES` index
buffer and draws that. `WEBGPU-153` gives WebGPU the same mechanism.

**How.** At **queue** time, a draw whose captured `FillMode` is `WireFrame` and whose primitive is
`TriangleList` or `TriangleStrip` has its command rewritten in place: each triangle's three edges
become three two-index lines in a 32-bit index buffer, and the topology becomes
`WGPUPrimitiveTopology_LineList`. Nothing downstream has a wireframe branch at all -- what the
replay receives is an ordinary 32-bit indexed line-list command, which every `Issue*Draw` family
already knows how to draw, and the topology is already part of every pipeline cache key.

| Step | Behaviour |
|---|---|
| `GraphicsDevice::SupportsCapability(GraphicsCapability::WireFrame)` | **`true`**, backed by pixels: the shared `WireFrameTriangleOracle` measures interior `0/1089` with all three edges present, against Solid's `1089/1089` |
| A wireframe polygon draw | **Accepted.** Exactly one queued command, one native draw, one extra pipeline (the line-list variant is a sibling of the solid one, not a new key per draw), no extra pass, retry or frame |
| `RasterizerState.DepthBias` on that draw | **Dropped for it.** WebGPU forbids a depth bias on a line topology, and a depth bias is defined as an offset along a *polygon's* depth slope -- a line has none |
| An interior edge shared by two triangles | Drawn **twice**. Shared vertices stay shared, matching the reference renderer exactly |
| A `LineList`, `LineStrip` or `PointListEXT` draw under `WireFrame` | **Untouched.** A fill mode selects how a *polygon's interior* is rasterized; a line or point has no interior, so `Solid` and `WireFrame` are the same request. Measured byte-identical under both modes |

**Coverage.** Every public 3D draw route performs the expansion -- ordinary non-indexed and indexed
(16- and 32-bit, with nonzero `vertexStart` / `startIndex` / `baseVertex`), both
`DrawUserPrimitives` / `DrawUserIndexedPrimitives` families, every stock-effect family, the custom
WGSL `ShaderEffect` route and the instanced route. `WebGpuWireFrameContract.EveryPublicDrawRouteWireframesAndAcceptsSolid`
asserts it per route, because "some routes wireframe and the rest quietly fill" is exactly the defect
worth catching and a whole-suite total would hide it.

**Why not `polygonMode`.** The pinned wgpu-native *does* expose one:
`WGPUPrimitiveStateExtras::polygonMode` with `WGPUPolygonMode_Line`, behind
`WGPUNativeFeature_PolygonModeLine` (listed for DX12/Vulkan/Metal). It is deliberately unused,
because it is **native-only** and browser WebGPU has no polygon mode at all; index expansion is the
one route that works on both targets, so this renderer has one implementation rather than two.

**The history, because this renderer has given three different answers.** Before `WEBGPU-115` it
reported `WireFrame` **supported**, accepted the request, built and natively submitted a distinct
pipeline for it, and returned a frame byte-identical to the `Solid` one -- an affirmative false claim
through the public capability query. `WEBGPU-115` replaced that with a deterministic refusal, on the
stated grounds that "wgpu-native exposes no polygon mode, so a wireframe request cannot reach any
native pipeline state". `WEBGPU-153` disproved the premise rather than the refusal: a wireframe never
needed a polygon mode, and the sentence was wrong about the API as well. All three states are
recorded in `WebGpuWireFrameContractTests.cpp`, which measures the current one.

### Custom WGSL `ShaderEffect` (`WEBGPU-76`)

A `ShaderEffect` constructed with WGSL vertex and fragment source is genuinely compiled and run by
this renderer on the 3D draw routes (`DrawUserPrimitives` / `DrawUserIndexedPrimitives` /
`DrawPrimitives` / `DrawIndexedPrimitives`). Unlike Vulkan (which takes SPIR-V) or SOFTWARE/HEADLESS
(which accept a source and ignore it), WebGPU's pixels are its shader's, so it reports:

- `GraphicsDevice.GetShaderDialectEXT()` → `ShaderDialectEXT::Wgsl` — ask this before supplying source;
- `GraphicsDevice.ExecutesShaderEffectSourceEXT()` → `true`.

**Authoring contract.** A custom effect's WGSL must follow this renderer's fixed conventions:

| Element | Convention |
|---|---|
| Entry points | vertex `vs_main`, fragment `fs_main` (two independent modules — redeclare any shared struct in each) |
| Vertex inputs | `@location(i)` for the *i*-th element of the bound `VertexDeclaration` (`location = declaration index`, the same convention EasyGL uses) |
| Uniform block | `@group(0) @binding(0) var<uniform> …` — visible to both stages. Declare its size and each member's byte offset to the framework with `ShaderEffect.DeclareUniformBlockEXT(sizeBytes, names, offsets, count)`; WGSL has no loose uniforms, so this name→offset map is how `SetUniform*(name, …)` reaches the shader |
| Matrices | the renderer sets `World`, `View`, `Projection` (those exact names, column-major, untransposed) into the block every draw — declare them and read `Projection * View * World * vec4f(pos, 1.0)` |
| Texture (optional) | if the fragment samples, declare `@group(0) @binding(1) var …: sampler;` and `@group(0) @binding(2) var …: texture_2d<f32>;`. Unit 0 comes from `ShaderEffect.SetTexture(0, tex)`; an unbound unit falls back to a neutral-white texture |

The compiled program, its bind-group/pipeline layouts and its per-pass pipelines are owned by the
effect; a draw captures the uniform block **by value** at the call, so two draws of the same effect
with different `SetUniform*` values render correctly. Compilation failures are reported through
`ShaderEffect.IsEffectValid()` / `GetCompileErrorEXT()`, never as a device error.

The same `WebGPUEffectRenderer` also drives **SpriteBatch** custom effects (`WEBGPU-142`):
`SpriteBatch.Begin(..., effect)` runs the effect's WGSL per sprite. The sprite vertex layout is fixed
(`position`/`uv`/`color` at `@location(0/1/2)`) and needs no matrices — sprite vertices are already NDC
— so a sprite effect's vertex shader passes position through; the fragment samples the sprite's texture
at the reserved `@binding(1)`/`@binding(2)` and reads its uniform block at `@binding(0)`. A compiled
Effect-Framework effect (no WGSL) is refused. Proven by `WebGPU_ShaderEffect3D` (3D) and
`WebGPU_SpriteBatch_ShaderEffect` (2D).

**Still open:** the `SpriteEffect`-style `MatrixTransform` uniform is not auto-injected for a
SpriteBatch custom effect (an app that needs `Begin(..., transformMatrix)` to reach a custom shader
would bake it into a uniform itself); the `SpriteBatch` transform matrix is applied to the sprite
geometry as usual.

### Multiple render targets (`WEBGPU-85`/`86`/`87`)

`SetRenderTargets` binds 2..4 `RenderTarget2D` targets into one render pass. `PassDestination` carries
up to four colour attachments (slot 0 in its single fields, slots 1..N-1 in its `mrt*` arrays), and
the one shared `ReplayOrderedSegments` builds an N-entry `WGPURenderPassColorAttachment` array from it
— there is no separate MRT replay path. Every pipeline that replays into the pass is built for that
attachment count and keyed by it, so a 1-target and a 2-target pipeline are distinct cache entries.

| Aspect | Behaviour |
|---|---|
| Binding | 2..4 `RenderTarget2D` sharing width/height/sample count. A mismatch (named target), a cube face, a null target, or count > 4 is refused with a `System::NotSupportedException`. |
| Custom `ShaderEffect` draw | Writes every attachment: its WGSL fragment declares `@location(0..N-1)`. This is the real MRT path — `WebGPU_MRT` binds 2 then 4 targets and proves slot N holds slot N's own content (a distinct swizzle of `uBase`), not slot 0's. |
| Stock / SpriteBatch draw | Writes attachment 0 only (its single `@location(0)` output); slots 1..N-1 have `writeMask` 0 — the "the stock/2D pipeline writes attachment 0" behaviour every other renderer has (`ExpandStock`/`InitStockColorTargetsEXT`). This is what makes the shared cross-renderer MRT tests pass. |
| Clear | `Clear()` clears every bound attachment (XNA has no per-attachment clear granularity). |
| Depth/stencil | Single, shared by the pass (slot 0's). |
| Unbind | `SetRenderTarget(nullptr)` / `SetRenderTargets({})` / any single-target or cube-face bind flushes the set into all its targets and drops back to one attachment. |

Every render target here uses the backbuffer's `surfaceFormat_`, so all attachments in an MRT set
share a format. Per-slot `ColorWriteChannels`/`1`/`2`/`3` **are** honoured for a custom-effect MRT
draw (`WEBGPU-143`): slot *i*'s `@location(i)` output is masked by that attachment's own
`BlendState.ColorWriteChannels`, proven by `WebGPU_MRT`'s Check C. XNA has one blend *equation* for
all targets, so there is no per-slot independent blend to model; the set shares slot 0's blend
factors. MSAA + MRT resolves each attachment independently.

**Wireframe** was implemented exactly that way by `WEBGPU-153` -- index expansion into a line
topology -- and is no longer a deviation; see its section above.

## Architecture notes

WebGPU and Vulkan are conceptually related explicit APIs, so the Vulkan renderer informed resource
lifetime, surface recovery, command encoding and render-pass structure. The implementation is not a
line-by-line Vulkan translation:

- WebGPU uses WGSL shader modules rather than CNA's Vulkan SPIR-V modules.
- Resource bindings use bind-group layouts and bind groups rather than Vulkan descriptor sets.
- WebGPU has no push constants, so 3D effect data uses uniform buffers.
- Per-draw vertex/uniform buffers are pooled, not churned (`WEBGPU-12`): `AcquireTransientBuffer`
  serves them from a bounded pool keyed by `(usage, power-of-two size class)` and `RecycleTransient-
  Buffer` returns them after submit instead of releasing, so a warmed scene stops allocating.
  Recycling is fence-free -- the single device queue orders every `wgpuQueueWriteBuffer` write after
  the prior cycle's reads. The SpriteBatch vertex buffer is a 3-slot ring (`WEBGPU-59`).
- Pipeline state is largely immutable and cached per family. All 12 3D pipeline families
  (`GetOrCreatePipeline*3D`) share ONE `WGPURenderPipelineDescriptor` assembly,
  `Build3DPipelineEXT(Pipeline3DDescEXT)` (`WEBGPU-29`): each family passes only its vertex layout,
  shader module(s), pipeline layout and label, and keeps its own cache key/map. The SpriteBatch and
  MipBlit pipelines keep their own builders (not 3D families).
- Native surface creation is performed directly from SDL3 window properties; CNA does not require
  the separate `sdl3webgpu` compatibility library.

The deliberate, collected departures from the Vulkan renderer — push constants → UBO, wireframe
refusal, async → synchronous callback pumping, `Color` → `Unorm8x4` vertex format, the
`SetStringMarkerEXT` no-op, and windowing handled by the shared platform — are documented in
`docs/webgpu-vs-vulkan-deviations.md`.

See `plans/plan_webgpu.md` for task-level status and the remaining parity work.
