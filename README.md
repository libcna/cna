# CNA

## 1. 🚀 Overview

CNA is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics renderer layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (`Microsoft::Xna::Framework`) while using modern C++ internals.

**CNA demonstrates engine-level C++ architecture, graphics abstraction design, and renderer-oriented systems engineering.**

### Quick Start

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build --target CNA CnaTests
ctest --test-dir build --output-on-failure
```

> **Looking for a specific doc?** `docs/` has 93 Markdown documents — see [`docs/README.md`](docs/README.md)
> for an index of what's current vs. historical.

### Project Status

- **`Microsoft::Xna::Framework::Graphics` milestone:** qualified **~90% XNA/FNA compatibility, test-execution-verified** (not estimated) — every one of the ~26 major Graphics classes is present, implemented, and tested. **As of 2026-07-11, all 5 confirmed bugs behind the original 2026-07-09 milestone declaration are fixed** (Vulkan `BlendState`, EasyGL anisotropic filtering, `IndexElementSize`'s numeric values, `Model`'s root-bone override, `SpriteBatch::Draw`'s optional source rectangle), and Vulkan `OcclusionQuery` (previously architecturally blocked) is fixed too. `docs/graphics-compatibility-report.md` is a dated snapshot from that declaration, kept for its methodology, not current status — see `NEXT.md` §5 for the actively-maintained bug list. What's left to 100% is a smaller set of individually-tracked issues plus a handful of project-owner architecture decisions (e.g. SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, `Texture3D`/`TextureCube` sampler-bind architecture) — none silent or undocumented.
- **Overall XNA 4.0 API surface:** 227 of 245 public FNA types are present in CNA (**92.7%**, computed 2026-07-11 by diffing FNA's public type list against CNA's headers) — 100% for `Graphics`/`Audio`/`Input`(+`Touch`)/`Storage`; the real gap is `.Content` (4/12 — no `.xnb` reader, by design) and `.Media` (25/25 present, but 14 are shells). See `docs/xna-4-api-coverage.md`. **Note this is a different metric from the Graphics bullet above** — this one counts whether a type/class exists at all across every XNA namespace (a raw presence count), while the Graphics "~90%" figure is a narrower, bug-weighted quality gate scoped to just the ~26 major Graphics classes (it also counts behavioral correctness, not just presence — Graphics itself is 91/91 = 100% present). The two numbers measuring different things is expected, not a typo or a contradiction.
- **Compiled XNA effects:** `Effect(GraphicsDevice&, byte[])` and the canonical XNB `EffectReader`
  execute XNA/FNA Direct3D 9 Effect Framework bytecode on the `FNA3D` renderer, including public
  reflection, parameter mutation, techniques/passes, pass states, cloning, 3D draws, and
  `SpriteBatch`. Other renderers currently report `GraphicsCapability::CompiledEffects == false`
  and reject the constructor explicitly; MGFX and runtime `.fx` source compilation remain separate
  formats/projects. See [`docs/shader-effect-vs-fx-bytecode.md`](docs/shader-effect-vs-fx-bytecode.md).
- **`SDL_RENDERER` renderer:** Implemented path focused on practical 2D rendering workflows; 2D-only by design (3D calls throw).
- **`OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` renderers:** the most mature GL-family public renderers overall — one shared internal implementation (`EasyGL`, on top of `easy-gl`) driven by a GL profile choice, not five separate implementations. `OPENGLES3` (desktop/mobile GLES 3.0) and `WEBGL2` (Emscripten, GLES 3.0 → WebGL 2.0) have full 2D+3D pixel-verified coverage — this is what was previously the single `EASYGL` public renderer, split into its real public identities. `OPENGL33` (desktop GL 3.3 core) and `WEBGL1` (Emscripten, GLES 2.0 → WebGL 1.0) are newer and still landing — see `plan_glbackends.md` for current per-profile status. `OPENGLES2` (native GLES 2.0, GLSL ES 1.00, Phase-2 expansion) carries a deliberately narrower ES 2.0 capability boundary — see [`docs/opengles2-renderer.md`](docs/opengles2-renderer.md).
- **`VULKAN` renderer:** Real, working 3D rendering (all 5 stock effects, render targets, depth/stencil state, `BlendState`, `OcclusionQuery`) — second-most mature renderer; the one remaining named gap is an isolated `RasterizerState.DepthBias` sub-case. See `docs/xna-4-api-coverage.md`'s per-renderer table for current detail.
- **`BGFX` renderer:** Broad 2D+3D functionality, largely pixel-verified parity with EasyGL/Vulkan as of this project's Phase 72 — but not unqualified full parity: known real limitations remain, including a `Depth24Stencil8`-attached `RenderTargetCube` face producing no colour output (Task 952, deferred, root cause not yet found), `DrawIndexedPrimitivesEx` silently discarding `startIndex`/`baseVertex` on a sub-range indexed draw (Task 954), and occlusion-query pixel-count correctness that can't be verified under this project's own sandbox's software GL driver. See `NEXT.md` §5 for the current, complete list.
- **`SOKOL` renderer:** Experimental renderer on `sokol_gfx`, a single-header GPU abstraction (OpenGL 4.1 core today; GLES3/D3D11/Metal/WebGPU are wired but unimplemented). The current baseline covers context/pass lifecycle, the full `Clear` family, back-buffer read-back, `Texture2D`, `TextureCube`, vertex/index buffers, a pixel-verified `SpriteBatch` with real `BlendState`/`SamplerState` support, vertex-coloured/textured/lit/dual-textured/skinned/environment-mapped 3D geometry (`BasicEffect` with real depth testing, face culling, ambient + up to 3 per-pixel directional lights, specular, emissive, alpha test and fog; `DualTextureEffect`; `SkinnedEffect` with a 72-bone palette; `EnvironmentMapEffect` with real cube-map reflection and Fresnel), `Viewport.MinDepth`/`MaxDepth`, instanced draws, custom `ShaderEffect`s (raw-GL bypass of the `sg_pipeline` path, both `SpriteBatch` and 3D draws), MRT (2-4 `RenderTarget2D` targets via a real multi-attachment `sg_pass`), and `RenderTarget2D`/`RenderTargetCube` (real colour + depth-stencil attachments, MSAA, correct sampling orientation, immediate first-use, direct `GetData` readback, mip-mapped rendering). PBR shading, `RenderTargetCube` MSAA (a permanent sokol_gfx boundary), and `Texture3D` sampling are not implemented and fail loudly. See [`docs/sokol-renderer.md`](docs/sokol-renderer.md) and `plan_sokol.md`.
- **`WEBGPU` renderer:** Experimental renderer using native `wgpu-native`. The current baseline covers device/surface setup, clear/present, RGBA8 `Texture2D`, vertex/index uploads and WGSL SpriteBatch rendering. It is not yet a 3D-parity replacement for EasyGL/Vulkan/Bgfx; see [`docs/webgpu-renderer.md`](docs/webgpu-renderer.md) and `plan_webgpu.md`.
- **`SKIA` renderer:** Experimental CPU-raster 2D path using a pinned external Skia build. Clear/presentation/readback, `Texture2D`, complete shared `SpriteBatch`/`SpriteFont` 2D routes, raster targets, bounded 2D SkSL, CPU cube/volume transfer storage, and six-face `RenderTargetCube` emulation are verified. It deliberately does not advertise 3D, depth/stencil, MSAA, MRT, occlusion queries, or cube/volume sampling; direct and emulated alternatives are recorded in [`docs/skia-renderer.md`](docs/skia-renderer.md), the [feature matrix](docs/graphics-renderer-feature-matrix.md), and `plan_skia.md`.
- **`OPENGL4` renderer:** Real desktop OpenGL 4.x core-profile renderer (4.1 minimum requested, `SDL_GL_CONTEXT_PROFILE_CORE`), deliberately independent of the EasyGL-implemented `OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` family — those profiles target ES 3.0/WebGL or a GL 3.3 core profile, not 4.x core. Uses its own small hand-rolled GL loader (`GL4Loader`), zero new third-party dependency beyond the platform's own GL library. All five stock effects plus `PbrEffect`/`SkinnedPbrEffect` (GLSL 410 core, stride-dispatched), real FBO render targets (2D + cube + MRT), backbuffer and render-target MSAA, real `GL_SAMPLES_PASSED` occlusion queries (exact pixel counts, unlike the EasyGL family's ES any-samples boolean), real wireframe via `glPolygonMode` (pixel-oracle-verified), `Texture3D`/`TextureCube` with real readback, 16/32-bit index buffers, `baseVertex`, custom GLSL `ShaderEffect` (3D + SpriteBatch), and hardware instancing through the unified vertex-stream transport. Multi-stream vertex input is reported unsupported and refused deterministically. 25 dedicated pixel-readback CTest suites, all verified against a real 4.5-core context. See [`docs/opengl4-renderer.md`](docs/opengl4-renderer.md) and `plan_opengl4.md`.
- **`OPENGL1` renderer:** Historical-class legacy desktop OpenGL 1.x **fixed-function** renderer — immediate-mode vertex emission, `GL_MODELVIEW`/`GL_PROJECTION` matrices, `glLight*` lighting (3 directional lights + specular + emissive), `glTexEnv*` combiners (`DualTextureEffect`, `EnvironmentMapEffect` reflection subset), real `GL_FOG` driven by an exact inversion of the FNA fog vector, `glAlphaFunc` alpha-test approximation. Runtime-discovers 1.2–1.5-era features via `SDL_GL_GetProcAddress` (FBO render targets 2D+cube with readback and mip regeneration, backbuffer + RT MSAA, `ARB_occlusion_query` with exact `GL_SAMPLES_PASSED` counts, extended blend, anisotropy, cube maps) — no GL loader library, no shaders anywhere, zero new third-party dependency. No custom effects, no MRT, no `Texture3D`, no instancing/multi-stream — all reported truthfully and refused deterministically. 38 dedicated CTest suites. See [`docs/opengl1-renderer.md`](docs/opengl1-renderer.md) and `plan_opengl1.md`.
- **`OPENGL2` renderer:** Native desktop OpenGL 2.1 **compatibility-profile** renderer, GLSL 1.10 throughout (runtime-compiled inline programs, attribute names bound via `glBindAttribLocation`), deliberately independent of the EasyGL-implemented GL family and of the other GL renderers. All five stock effects plus `PbrEffect`/`SkinnedPbrEffect`, FNA fog-vector fog, real FBO render targets (2D incl. MSAA + cube), real MRT (up to 8 targets with real depth/MSAA resolve), real `GL_SAMPLES_PASSED` occlusion queries, `Texture3D`/`TextureCube` with readback, 16/32-bit indices, software `baseVertex` (pointer re-base -- no `glDrawElementsBaseVertex` on 2.1), full custom `VertexDeclaration` support (name-driven binding reads exactly the declared bytes), custom GLSL 1.10 `ShaderEffect` (3D + SpriteBatch), real Letterbox/Overscan/Stretch presentation modes, context-loss recovery, and hardware instancing when the driver grants `GL_ARB_draw_instanced`/`GL_ARB_instanced_arrays` -- the driver-dependence is why this lane added `GraphicsCapability::Instancing`. Multi-stream vertex input reported unsupported and refused deterministically. 48 dedicated CTest suites. See [`docs/opengl2-renderer.md`](docs/opengl2-renderer.md) and `plan_opengl2.md`.
- **`OPENGLES1` renderer:** Genuine **OpenGL ES 1.1 fixed-function** renderer ("Common"/CM profile), deliberately independent of the EasyGL-implemented GL family — EasyGL targets shader-based ES 3.0/GL 3.3/WebGL pipelines and cannot create an ES 1.1 context at all, so the two share no code. No shaders anywhere (zero `#version` directives, zero shader entry points); fixed-function matrices, `glLight*` lighting, `glTexEnv*` multitexture combiners, `GL_FOG`, alpha test, FBO render targets via `GL_OES_framebuffer_object`, and `WireFrame` emulated by re-expanding triangles to `GL_LINES`. Multiple render targets, occlusion queries, custom effects, `Texture3D`, multi-stream vertex input and instancing are all reported `false` and refused deterministically. Requires a **real system OpenGL ES 1.1 library** (`libGLESv1_CM` plus `GLES/gl.h`/`GLES/glext.h`; Debian `libgles1`, `libgles-dev`), gated at configure time with a `FATAL_ERROR` — nothing vendored or downloaded. Note that Debian builds Mesa with `-Dgles1=disabled`, so its stock driver cannot create an ES 1.1 context on **any** device; the renderer is validated against a locally built ES1-capable Mesa driven by `scripts/opengles1-test-env.sh` (verified runtime identity: `OpenGL ES-CM 1.1 Mesa 25.0.7`, softpipe). 7 dedicated pixel-readback CTest suites. See [`docs/opengles1-renderer.md`](docs/opengles1-renderer.md) and `plan_opengles1.md`.
- **`MAGNUM` renderer:** Desktop OpenGL 3.3 core through Magnum's typed GL wrappers. It provides native polygon-mode wireframe while the EasyGL profiles provide their own measured line-expansion emulation. Its accepted capability and validation boundary is recorded in [`docs/magnum-renderer.md`](docs/magnum-renderer.md), `plan_magnum.md`, and `integration/lanes/magnum.md`.
- **`LLGL` renderer:** Experimental renderer on [LLGL](https://github.com/LukasBanana/LLGL). CNA's accepted support route is LLGL's OpenGL module on Linux/X11/x86_64; it is the only automatic choice. Explicit Vulkan selection is rejected after native validation exposed descriptor, image-layout, and teardown violations, and Null is diagnostics-only. The current accepted boundary is recorded in [`docs/llgl-renderer.md`](docs/llgl-renderer.md), `plan_llgl.md`, and `integration/lanes/llgl.md`.
- **`DIRECTX9` renderer:** Windows-only native Direct3D 9 renderer targeting real **XNA 4.0 pixel authenticity**, not just feature parity — it runs Microsoft's own vendored Stock Effects HLSL bytecode, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (14 CTest binaries). A checked-in 31-scene oracle corpus diffs CNA's render against the **real XNA 4.0 runtime's own render** of the same scene at `--tolerance 0`: **0/31 scenes currently diverge.** `GraphicsProfile.Reach`/`.HiDef` enforcement is real (the only CNA renderer where it is). Render targets sampled as textures, non-`Color` `SurfaceFormat`, and real-Windows hardware verification are still open. See [`docs/directx9-renderer.md`](docs/directx9-renderer.md), [`docs/d3d9-divergence-report.md`](docs/d3d9-divergence-report.md), and `plan_dx9.md`.
- **`DIRECTX11` renderer:** Windows-only native Direct3D 11 renderer, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (6 CTest binaries, 96+ checks) — all 10 stock HLSL shader variants (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`), textures/render targets (MRT/MSAA/occlusion queries), state objects, SpriteBatch, and a runtime-`D3DCompile()` custom `ShaderEffect` path are real and pixel-verified. Real-Windows hardware verification (device-lost recovery, WARP fallback, driver-specific parity) is still open. See [`docs/directx11-renderer.md`](docs/directx11-renderer.md) and `plan_dx.md`.
- **`DIRECT2D` renderer:** Windows-only hardware-accelerated **2D-only** renderer using `ID2D1DeviceContext`; D3D11/DXGI only host the Direct2D device and flip-model swap chain. Authored `Texture2D` mips, non-mipmapped `RenderTarget2D`, SpriteBatch transforms/tint/flip/addressing, native anisotropic filtering, exact supported Direct2D Porter–Duff modes, logical presentation/readback, resize and 2D resource recovery are implemented. Additive blending, mipmapped render targets, and every 3D path are rejected rather than approximated. Wine covers the compatibility matrix; built-in effects, selected image composites, physical display/DPI output, and debug-layer evidence remain native-Windows gates. See [`docs/direct2d-renderer.md`](docs/direct2d-renderer.md) and `plan_direct2d.md`.
- **`DIRECTX12` renderer:** Windows-only native Direct3D 12 renderer, cross-compiled via MinGW-w64 and verified through Wine+vkd3d-proton on a real GPU, **off-screen only** (`DirectX12_Smoke` CTest, 80/80 checks) — device/queue/heaps/command-lists/fences/barriers/PSOs/root-signatures are real, all 10 stock HLSL shader variants (same DXBC as `DIRECTX11`) and a real `SpriteBatch` are pixel-verified off-screen, and device-removed recovery is real and functionally proven. Swap-chain presentation is a known, real, unresolved gap on this dev loop (genuine Wine/vkd3d-proton `dxgi.dll` architecture mismatch, not a CNA bug); runtime-settable blend/depth-stencil/rasterizer state, per-slot `SamplerState`, render targets, `Texture3D`, occlusion queries, and real-Windows hardware verification are all still open. See [`docs/directx12-renderer.md`](docs/directx12-renderer.md) and `plan_dx.md`.
- **`CANVAS` renderer:** Emscripten-only HTML Canvas 2D renderer (`SpriteBatch`/`Texture2D`/`SpriteFont`/`RenderTarget2D` only, 2D-only by design like `SDL_RENDERER`) — `SpriteBatch` (incl. rotation/origin/flip/tint/transform), textures/render targets, `BlendState`/`SamplerState` mapping, and `SpriteFont` are all implemented and structurally reviewed, verified via a real `emcmake`/`emcc` 6.0.2 configure+build (`CnaTests` links and a renderer-agnostic GTest suite genuinely passes under `node`). **Not yet pixel-verified in a real browser** — this dev loop has no DOM/`CanvasRenderingContext2D` at all (`node` has none; `SDL_Init(SDL_INIT_VIDEO)` itself throws under Emscripten/`node`). See [`docs/canvas-renderer.md`](docs/canvas-renderer.md) (incl. a manual browser verification checklist) and `plan_canvas.md`.
- **`HTML_DOM` renderer:** Emscripten-only DOM/CSS renderer with the same 2D-only scope, but no `<canvas>` in the sprite path at all — each sprite is a pooled `<div>` placed by a CSS `transform`, textured with `background-image`, faded with `opacity`, and composited by the browser itself. A frame in which nothing moves costs nothing; a frame in which sprites only move costs one `transform` write each, and a whole `SpriteBatch` crosses the wasm/JS boundary exactly once. `RenderTarget2D` is backed by a real off-screen canvas; the DOM backbuffer cannot be read back (no browser API rasterizes a live DOM subtree) and says so. Verified by a real `emcmake` build **and a real headless-Chromium run** (`scripts/run-htmldom-browser-test.sh`), which asserts against the produced DOM. See [`docs/html-dom-renderer.md`](docs/html-dom-renderer.md) and `plan_html_dom.md`.
- **`FREEDIRECT` renderer** (formerly `DIRECTX3`)**:** Cross-platform (genuinely — builds and runs via ordinary `/usr/bin/c++`, no MinGW/Wine needed) DirectDraw-shaped 2D renderer fronting `../free-direct`, a sibling project's own DirectDraw reimplementation. 2D-only by design, same spirit as `SDL_RENDERER`. All 8 plan phases complete: real device/window bring-up with a CPU-owned "shadow backbuffer" (working around a real `Lock()`-on-primary gap in `free-direct` itself), texture/render-target renderers, a CPU `SpriteBatch` compositor (`BltFast` fast path + a from-scratch edge-function rasterizer for everything else), all 4 real `BlendState` presets with genuinely distinct formulas (not one collapsed baseline), bilinear filtering, and real `Wrap`/`Mirror` texture addressing — the latter two are a real capability win over `SDL_RENDERER`, which has `Wrap`/`Mirror` ⛔ BLOCKED. See [`docs/freedirect-renderer.md`](docs/freedirect-renderer.md) and `plan_freedirect.md`.
- **`DIRECTX1` renderer:** Windows-only, MinGW-cross-compiled 2D renderer talking to a **real** Windows `ddraw.h` — genuine COM `IDirectDraw`/`IDirectDrawSurface` **v1 interfaces only** (never `IDirectDraw2+`), run under Wine, with **no `../free-direct` reimplementation involved at all** (the opposite delivery route from `FREEDIRECT`). DirectX 1 shipped no Direct3D at all, so every 3D call throws by construction, not by policy. Ports `FREEDIRECT`'s already-verified CPU `SpriteBatch` compositor and blend-mode math verbatim (`IDirectDrawSurface::Blt` has never supported rotation in any DirectX version), while sourcing the surface layer from a real device — a real Win32 `HWND`, a real `IDirectDraw` object, and a per-frame recomputed letterbox `Present()` that (unlike `FREEDIRECT`) has no stale-scale bug after a resize. All 8 plan phases complete, 10/10 CTests passing through a real Wine `ddraw.dll` run. See [`docs/directx1-renderer.md`](docs/directx1-renderer.md), `plan_dx1.md`, and `plan_dxold.md` (the roadmap for the wider DIRECTX1/2/3/5/6/7/8/10 renderer family).
- **`DIRECTX2` renderer:** Same real Windows-only 2D layer as `DIRECTX1` (DirectDraw v1, ported verbatim), plus a **real, working 3D pipeline** — the first legacy-DirectX renderer in this family with actual 3D rendering, not a permanent throw. Built on `IDirect3D2`/`IDirect3DDevice2`'s `DrawPrimitive`/`DrawIndexedPrimitive` immediate-mode API, not the literal DirectX-2-SDK execute-buffer surface (`IDirect3D`/`IDirect3DDevice::Execute`), which an exhaustive 14-variant existence-gate spike found non-functional in this environment's Wine despite every API call succeeding — an owner-confirmed scope decision to deliver genuine 3D over exact-SDK-version purity. `VertexBuffer`/`IndexBuffer` (16- and 32-bit), real CPU transform + near-plane clipping submitted as `D3DTLVERTEX`, genuine order-independent depth-test occlusion, real one-texture sampling, full per-draw rasterizer/depth/blend/sampler state, and (Phase O9) real CPU-computed ambient + directional-light Lambertian/Blinn-Phong lighting for the two normal-bearing vertex layouts (specular composited by real `D3DRENDERSTATE_SPECULARENABLE` hardware) are all pixel-verified; `WireFrame` fill mode is spike-confirmed genuinely distinct. Fog/multitexture/environment-mapping/skinning are still accepted but not evaluated, matching the `Software` renderer's own identical scope boundary. All 9 plan phases complete, 19/19 CTests passing through a real Wine `ddraw.dll`+`d3d.dll` run. See [`docs/directx2-renderer.md`](docs/directx2-renderer.md), `plan_dx2.md`, and `dx2-spike/README.md` (the execute-buffer investigation).
- **`DIRECTX3` renderer:** CNA's real DirectX 3 renderer (originally landed under the temporary `DX30` name; renamed to `DIRECTX3` on 2026-08-04 when the `free-direct`-backed renderer became `FREEDIRECT`, then to `DIRECTX3` in the 2026-08 naming normalization — its historical `DX30-*` task IDs are unchanged). Architecturally `DIRECTX2` plus one upgrade: the DirectDraw object is `IDirectDraw2` (not v1), QueryInterface'd immediately after `DirectDrawCreate` and used for every subsequent call — spike-confirmed a fully-functional drop-in for everything `DIRECTX1`/`DIRECTX2` already do, including the entire `IDirect3D2`/`IDirect3DDevice2` 3D chain. Everything else (2D compositor, 3D pipeline, CPU lighting, `WireFrame`) is a verbatim, mechanically-ported copy of `DIRECTX2`'s own post-Phase-O9 code. 19/19 CTests pass, all green on the first run. See [`docs/directx3-renderer.md`](docs/directx3-renderer.md), `plan_dx3.md`, and `dx3-spike/README.md`.
- **`DIRECTX5` renderer:** CNA's real DirectX 5 renderer — the first release where execute buffers disappear entirely (`IDirect3DDevice3` only ever exposes `DrawPrimitive`/`DrawIndexedPrimitive`). A further port of `DIRECTX3`'s own 2D+3D layers: *every* surface (not just the top object) upgrades to v4 (`IDirectDraw4`/`IDirectDrawSurface4`/`DDSURFACEDESC2`/`DDSCAPS2`), and the 3D layer upgrades to `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`, submitting the same `D3DTLVERTEX` struct via the new `D3DFVF_TLVERTEX` FVF bitmask instead of the old `D3DVERTEXTYPE` enum. Also uses a real `IDirect3DViewport3::Clear2` call for depth clearing, replacing `DIRECTX2`/`DIRECTX3`'s manual Z-buffer `Lock()` workaround. 19/19 CTests pass, all green on the first run. See [`docs/directx5-renderer.md`](docs/directx5-renderer.md), `plan_dx5.md`, and `dx5-spike/README.md`.
- **`DIRECTX6` renderer:** CNA's real DirectX 6 renderer — introduces **no new COM interface at all** (confirmed via header inspection: no `IDirect3D4`/`IDirect3DDevice4` exists), reusing `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`/`IDirectDraw4` verbatim from `DIRECTX5`. Its real deliverable is genuine **stencil buffer operations**, resolving a boundary `DIRECTX2`/`DIRECTX3`/`DIRECTX5` all explicitly documented as unavailable: a combined depth+stencil Z-buffer surface (`DDPF_ZBUFFER|DDPF_STENCILBUFFER`, 32-bit total, D24S8-equivalent) plus real `D3DRENDERSTATE_STENCIL*` write/test wiring in `ApplyDepthStencilState`, proven end-to-end through `GraphicsDevice.DepthStencilState` (stamp-then-test, order-independent). Multitexturing is deliberately deferred (`D3DFVF_TLVERTEX` carries only one UV pair; genuine `DualTextureEffect` support would need a second vertex layout) and DXTn is out of scope (no consumer in CNA's content pipeline) — both documented, not silently dropped. Everything else is an unmodified port of `DIRECTX5`'s own 2D+3D layers. 20/20 CTests pass (19 ported + the new `DirectX6_Stencil`), all green on the first run. See [`docs/directx6-renderer.md`](docs/directx6-renderer.md), `plan_dx6.md`, and `dx6-spike/README.md`.
- **`DIRECTX7` renderer:** CNA's real DirectX 7 renderer — a genuine architectural change vs `DIRECTX6`: new `IDirectDraw7`/`IDirect3D7`/`IDirect3DDevice7` interfaces (created via the new `DirectDrawCreateEx` entry point), the **entire viewport object removed** (no `IDirect3DViewport` at all any more — `IDirect3DDevice7::SetViewport`/`Clear` are direct device methods), a shorter `CreateDevice` signature, and texture binding simplified to a direct `SetTexture(stage, surface)` call (no more texture-handle indirection). Stencil is unchanged from `DIRECTX6`, ported verbatim and spike-confirmed to survive all three architectural changes. Hardware T&L is genuinely available in this environment's Wine but deliberately not adopted (this renderer family submits CPU-pre-transformed-and-lit vertices by design); cube environment maps are deferred for the same class of reason as `DIRECTX6`'s multitexture deferral. A real, empirically-found API restriction: the legacy `D3DRENDERSTATE_TEXTUREMAPBLEND` render state is rejected outright by DIRECTX7 ("Render state 0x15 is invalid in d3d7"), fixed with `SetTextureStageState`/`D3DTOP_MODULATE` instead. 20/20 CTests pass (19 ported + the renamed `DirectX7_Stencil`). See [`docs/directx7-renderer.md`](docs/directx7-renderer.md), `plan_dx7.md`, and `dx7-spike/README.md`.
- **`DIRECTX8` renderer:** CNA's real DirectX 8 renderer — architecturally very different from `DIRECTX1`..`DIRECTX7`: DirectDraw and Direct3D merge in DIRECTX8, so this renderer has **no DirectDraw at all**, a single `IDirect3D8::CreateDevice` call creating both the device and its own real swap chain (the same device-bring-up shape as `DirectX9Renderer`). Delivered via **DXVK 2.6.0's D8VK** (`Direct3DCreate8`, not Wine's built-in `ddraw.dll`), the same "Route B" pattern D3D9/D3D11/D3D12 already use. Scope is fixed-function 3D only (an owner-confirmed decision — real XNA effects need `ps_2_0`+ regardless of Shader Model 1.x support, so a real SM1.x pipeline would not make `CreateEffectRenderer` usable for actual content). `D3DTLVERTEX` no longer exists in the real headers (D3D8 introduced the generic FVF model) — a hand-defined `DirectX8TLVertex` reproduces the same byte layout. No scaled-blit primitive exists at all (`CopyRects` is same-size-only, no `StretchRect`) — solved with an internal logical-resolution render target and a letterbox-scaled full-screen-quad `Present()`. `SpriteBatch` is a genuine redesign (real GPU-textured quads through the fixed-function pipeline, not a DirectDraw `Blt` compositor) and blending is real GPU hardware blending with no preset-detection fallback (unlike `DIRECTX2`-`DIRECTX7`'s CPU emulation) — though D3D8 has no configurable blend equation, so factor-only-matching a preset is genuinely indistinguishable from that preset on this hardware. `AnisotropicFiltering` reports `true` (unlike every prior renderer in this family) since DIRECTX8 runs on a real GPU via DXVK. Two environment-specific Wine/DXVK/AMD-RADV driver bugs were found and worked around (a dedicated Wine prefix with `dxgi` deliberately not DXVK-overridden, and forcing the software Vulkan device to avoid a real RADV bug on the second consecutive `Present()` call) — both fully documented, not code defects. 20/20 CTests pass. See [`docs/directx8-renderer.md`](docs/directx8-renderer.md), `plan_dx8.md`, and `dx8-spike/README.md`.
- **`DIRECTX10` renderer:** CNA's real Direct3D 10 renderer — architecturally very different from `DIRECTX1`..`DIRECTX8`: D3D10 (2006) removed the fixed-function pipeline **entirely**, so every 2D and 3D draw is a real, compiled HLSL `vs_4_0`/`ps_4_0` shader pair (`D3DCompile`, following this project's own `DIRECTX9`/`DIRECTX11` precedent), not `DIRECTX1`..`DIRECTX8`'s CPU-transform-and-submit model. Delivered via **Wine's own builtin `d3d10.dll`/`d3d10_1.dll`** (thin wrappers; DXVK 2.6.0 ships no `d3d10.dll` at all) forwarding to **DXVK's real `d3d10core.dll`** + DXVK's `dxgi.dll`. Real state OBJECTS (`ID3D10BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`, not per-call render states) and real MRT support (`MultipleRenderTargets` reports `true`, a genuine difference from every `DIRECTX1`..`DIRECTX8` renderer). Scope is deliberately bounded for this v1 (an owner-confirmed decision, mirroring `DIRECTX1`'s own "baseline first, richness later" precedent): `DrawColoredPrimitives` is real (vertex-color only, matching `BasicEffect(VertexColorEnabled=true)`), while lighting/texturing via `DrawPrimitivesEx`, custom effects, and occlusion query are left at `IGraphicsRenderer`'s own safe defaults. `SpriteBatch` is real GPU-quad rendering through the same real shader pipeline. Two environment bugs were found and fixed (a stale/broken `d3d10.dll` symlink inherited from an older DXVK version, and a real DXVK `dxgi` `Present()`-path bug under Xvfb worked around by testing against the real desktop `DISPLAY=:0`), plus three real renderer bugs found via CTest (a `D3D10_BLEND_DESC` API-shape difference from D3D11's own per-target blend array, a 180°-rotation winding-order/culling bug, and a `SpriteBatch::Begin()` ordering bug that discarded a caller's own transform matrix). 10/10 CTests pass. See [`docs/directx10-renderer.md`](docs/directx10-renderer.md), `plan_d3d10.md`, and `dx10-spike/README.md`.
- **`GLIDE` renderer:** 32-bit Windows native Glide 3.x call path for a separately supplied `glide3x.dll` runtime, with no rendering fallback. `SpriteBatch` textures are native ARGB4444 TMU uploads and compatible quads are submitted through `grDrawVertexArrayContiguous`. The deliberately constrained real 3D path includes clipped color/textured triangle lists/strips, TMU0 mipmapping, native Z/cull/alpha-test state and the fixed-function per-vertex BasicEffect subset; it is not shader emulation or full EasyGL parity. See [`docs/glide-renderer.md`](docs/glide-renderer.md) and [`plan_glide.md`](plan_glide.md).
- **`METAL` renderer (macOS only, experimental):** Direct native `MTLDevice`/`CAMetalLayer` rendering with runtime-compiled MSL shaders. Its supported and evidence-backed boundary is documented in [`docs/metal-renderer.md`](docs/metal-renderer.md) and `plan_metal.md`; iOS and tvOS remain unvalidated and are not claimed.
- **Verification methodology:** differential testing against a real, running `FNA.dll` reference implementation (`tools/fna-reference/`), disputed behavior settled against genuine XNA 4.0 on a Windows 7 VM, and a compile-time `CNAEXT` purity check (a dedicated CMake build option that turns every non-XNA-tagged declaration into a `[[deprecated]]` warning under `-Werror`) — see `CHECKLIST.md`'s "CNAEXT markers" section and `CMakeLists.txt`.
- **Automatic CI is partial** (see `.github/workflows/`): Linux workflows run the `Input` and `Devices`/`Sensors` gtest suites, not the full ~4,370-test unit suite or the complete GPU pixel matrix. A dedicated macOS 14 workflow builds the native Metal renderer and runs only its supported contract tests; a separate manual-dispatch Windows MSVC workflow covers D3D11/D3D12/Direct2D renderer CTests, with the Direct2D leg recording native debug-layer, WARP, and runtime/adapter artifacts. There is no automatic Windows or Android gate, and the macOS gate does not establish support for the Metal paths documented as unsupported.

## 2. 🎯 Goals

- Recreate the XNA developer experience in native C++.
- Provide a native C++ path for teams that like the XNA/MonoGame model but need non-managed runtime/toolchain control.
- Mirror core XNA namespaces and API patterns while implementing them incrementally.
- Decouple gameplay-facing API from rendering renderer implementation details.
- Enable one high-level API surface across different rendering technologies.
- Keep SDL/OpenGL/Vulkan-level concerns behind framework abstractions.

## 3. ✨ Features

### XNA API Compatibility (Incremental)

- Public API uses XNA-style namespaces, especially under `Microsoft::Xna::Framework`.
- Core game loop and framework primitives are available (`Game`, `GameTime`, graphics types, input/audio surfaces).
- Compatibility is partial and evolving; implementation status is tracked progressively in source.

### Input

- `Keyboard`, `Mouse` (incl. `MouseCursor`), `GamePad` (up to 4 players), `TouchPanel`/`TouchCollection`,
  and the `GestureDetector` gesture recognizer (Tap/DoubleTap/Hold/Drag/Flick/Pinch) — all under
  `Microsoft::Xna::Framework::Input`, matching FNA/XNA 4.0 behavior member-for-member.
  See `plan_input.md` for the full FNA-parity audit record.
- CNAEXT extensions beyond stock XNA: `TextInputEXT` (IME composition), rumble/trigger-rumble/light-bar/
  gyro/accelerometer on `GamePad`, raw `CNA::Input::Joysticks` (distinct from `GamePad`'s mapped view),
  device-level `CNA::Input::Sensors`/`Power`, and `CNA::Input::Haptics` for standalone haptic devices.
- Single SDL event funnel (`SdlInputBridge::ProcessEvent`), renderer-agnostic — Input behavior is
  identical across all 4 graphics renderers (EasyGL/Vulkan/bgfx/SDL_RENDERER), verified by the
  `CnaTests` input suite.

### Rendering

- `GraphicsDevice` abstraction with renderer delegation.
- `SpriteBatch` API with `Begin(...)` / `Draw(...)` / `End()` workflow.
- `Texture2D` abstraction with renderer-owned texture resources.

### Cross-Platform Direction

- SDL3-based platform foundation for windowing/input/audio integration.
- Renderer abstraction supports targeting multiple rendering paths from one API layer.
- **Windows support** via the `SDL_RENDERER` renderer (MSVC, clang-cl, or MinGW-w64) — cross-compiled
  with MinGW-w64 and verified running under Wine.
- Linux support via `OPENGLES3`/`OPENGL33` (OpenGL) or `SDL_RENDERER`.
- **Web (Emscripten) and Android (NDK) targets are implemented and verified**, not just
  architecturally planned — see section 7 (Networking, Services & Avatar) below for real
  cross-platform `Net` verification on both.

### Performance / C++ Advantages

- Native C++23 codebase and explicit control over memory/lifetime.
- Interface-driven renderer boundaries to keep hot rendering paths renderer-specific.
- Lightweight gameplay-facing API over renderer-specific implementations.

## 4. 🏗 Architecture

CNA is organized into clear layers with strict responsibility boundaries:

```text
+-----------------------------------------------------------+
|                 Game / Application Code                  |
|        (uses Microsoft::Xna::Framework API)             |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|            API Layer (XNA-style public surface)           |
| include/Microsoft/Xna/Framework/...                       |
| - Game, GraphicsDevice, SpriteBatch, Texture2D, ...       |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|         CNA Internal Layer (abstractions/factories)       |
| include/CNA/Internal/Renderers + src/CNA/Internal/Renderers |
| - IGraphicsRenderer, ISpriteBatchRenderer, ITextureRenderer  |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|               Renderer Implementations                     |
| src/CNA/Internal/Renderers/{SdlRenderer,EasyGL,Vulkan,Skia} |
+-----------------------------------------------------------+
```

### Interface vs Implementation Separation

- **Public API** lives under `include/Microsoft/...` and stays framework-facing.
- **Renderer contracts** live under `CNA::Internal::Renderers` interfaces.
- **Renderer implementations** live under `src/CNA/Internal/Renderers/...`.
- `GraphicsDevice` constructs renderers via factory (`CreateGraphicsRenderer(...)`) based on build-time renderer selection.

## 5. 🎮 Rendering System

`SpriteBatch` is the primary 2D rendering abstraction.

- You create it against a `GraphicsDevice`.
- Call `Begin(...)` to start a draw pass.
- Issue `Draw(...)` calls for textures/sprites.
- Call `End()` to close the batch.

The API surface is renderer-agnostic, while rendering behavior is executed by renderer-specific `ISpriteBatchRenderer` implementations.

This keeps game code stable while allowing renderer-specific optimizations in SDL renderer,
EasyGL, Vulkan, Skia, and the other selected paths.

## 6. 🔌 Renderer System

CNA exposes **46 public renderer identities** through `CNA_GRAPHICS_RENDERER` (choose one per build
configuration). The canonical registration, implementation-sharing, capability, and platform-gate
inventory is [`docs/renderer-registry.md`](docs/renderer-registry.md).

The former `ASCII` renderer identity was removed 2026-08 in favor of a renderer-neutral post-process
effect, `CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`), usable with any renderer's
`RenderTarget2D` output — see [`docs/ascii-post-process-effect.md`](docs/ascii-post-process-effect.md).

- `SDL_RENDERER`
- `SDL_GPU`
- `OPENGLES2` (native OpenGL ES 2.0, GLSL ES 1.00; internal implementation: EasyGL -- see [`docs/opengles2-renderer.md`](docs/opengles2-renderer.md))
- `OPENGLES3` (internal implementation: EasyGL)
- `OPENGL33` (internal implementation: EasyGL)
- `WEBGL1` (Emscripten only; internal implementation: EasyGL)
- `WEBGL2` (Emscripten only; internal implementation: EasyGL)
- `BGFX`
- `VULKAN`
- `WEBGPU`
- `MAGNUM`
- `SKIA`
- `BLEND2D` (CPU 2D vector rasterizer via Blend2D, presented through a streaming SDL_Renderer texture -- see [`docs/blend2d-renderer.md`](docs/blend2d-renderer.md))
- `HEADLESS`
- `SOFTWARE`
- `STUB`
- `OPENGLES1` (genuine OpenGL ES 1.1 fixed-function, "Common"/CM profile -- independent of the `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` family (internally EasyGL, a shader-based programmable pipeline), which cannot create an ES 1.1 context; needs a real system `libGLESv1_CM`)
- `OPENGL4` (real desktop OpenGL 4.x core profile -- deliberately independent of the `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` family (internally EasyGL), which cannot create a desktop 4.x core-profile context)
- `OPENGL1` (legacy desktop OpenGL 1.x fixed-function -- Historical class, independent of the EasyGL-implemented GL family and of `OPENGL4`; desktop Linux/Windows only)
- `OPENGL2` (native desktop OpenGL 2.1 compatibility profile, GLSL 1.10 -- independent of the EasyGL-implemented GL family and of the other GL renderers)
- `DIRECTX9` (Windows-only; native Direct3D 9 running Microsoft's own vendored Stock Effects HLSL bytecode)
- `DIRECTX11` (Windows-only)
- `DIRECT2D` (Windows-only, 2D-only)
- `DIRECTX12` (Windows-only)
- `CANVAS` (Emscripten only)
- `HTML_DOM` (Emscripten only)
- `SVG_DOM` (Emscripten only, 2D-only; SpriteBatch output as real `<svg>`/`<image>` DOM elements -- see [`docs/svg-dom-renderer.md`](docs/svg-dom-renderer.md))
- `FREEDIRECT` (formerly `DIRECTX3`)
- `DIRECTX1` (Windows-only)
- `DIRECTX2` (Windows-only)
- `DIRECTX3` (Windows-only; CNA's real DirectX 3 renderer -- see `plan_dx3.md`)
- `DIRECTX5` (Windows-only; CNA's real DirectX 5 renderer -- DirectDraw v4 + Direct3D v3 FVF `DrawPrimitive`)
- `DIRECTX6` (Windows-only; CNA's real DirectX 6 renderer -- same interfaces as DIRECTX5, plus real stencil buffer operations)
- `DIRECTX7` (Windows-only; CNA's real DirectX 7 renderer -- new IDirectDraw7/IDirect3D7/IDirect3DDevice7, viewport object removed, direct texture binding)
- `DIRECTX8` (Windows-only; CNA's real DirectX 8 renderer -- no DirectDraw at all, real IDirect3D8/IDirect3DDevice8 via DXVK's D8VK, fixed-function 3D only)
- `DIRECTX10` (Windows-only; CNA's real Direct3D 10 renderer -- no fixed-function pipeline at all, real HLSL vs_4_0/ps_4_0 shaders via Wine's own d3d10.dll + DXVK's d3d10core.dll, real MRT)
- `WICKED` (Wicked Engine's `wi::graphics` RHI; Linux/Windows, needs a Wicked Engine checkout)
- `SOKOL` (sokol_gfx single-header GPU abstraction; dispatches onto desktop OpenGL 4.1 core here)
- `DILIGENT` (experimental)
- `GLIDE` (32-bit Windows-only; requires external `glide3x.dll` at runtime)
- `GDI` (Windows-only, 2D-only)
- `LLGL` (experimental; accepted support is OpenGL on Linux/X11/x86_64)
- `METAL` (macOS only, experimental — see [`docs/metal-renderer.md`](docs/metal-renderer.md))
- `FNA3D` (FNA's own XNA-shaped graphics library; picks SDL_GPU/Direct3D 11/OpenGL at runtime, and executes XNA's actual stock effects — see [`docs/fna3d-renderer.md`](docs/fna3d-renderer.md))
- `OPENVG` (OpenVG 1.1 2D vector graphics via ShivaVG on a desktop OpenGL context; desktop Linux/Windows/macOS -- see [`docs/openvg-renderer.md`](docs/openvg-renderer.md))
- `PORTABLEGL` (CPU software OpenGL 3.x-ish pipeline via `rswinkle/PortableGL`; no GPU/window required -- see [`docs/portablegl-renderer.md`](docs/portablegl-renderer.md))

### Tradeoffs

- **SDL_Renderer renderer**
    - Simpler integration and broad SDL portability.
    - Good for straightforward 2D workflows.

- **EasyGL renderer (OpenGL-based path through `easy-gl`)**
    - Custom shader-driven rendering path.
    - Better control over rendering behavior and extensibility than fixed SDL renderer usage.

- **Direct2D renderer (Windows-only 2D path)**
    - Native accelerated `SpriteBatch`, `Texture2D` and `RenderTarget2D` through Direct2D 1.1.
    - Explicitly excludes 3D/depth/MRT/custom shaders; use `DIRECTX11` for those features.

- **BGFX renderer**
    - Dedicated renderer option with the same public rendering API coverage as other renderers.
    - Integrates through CNA renderer abstraction and can be selected via `CNA_GRAPHICS_RENDERER=BGFX`.
    - Uses native `bgfx` API (window/platform init, texture creation, sprite draws, frame submission), not `SDL_Renderer` rendering.
    - `bgfx` is integrated in CMake for this renderer via `FetchContent` (`bgfx.cmake`).

- **Diligent Engine renderer (experimental)**
    - The only renderer that does not target one native API: DiligentCore is itself an abstraction over Direct3D 11/12, Vulkan, OpenGL and Metal, so the native API is chosen **at runtime** (`D3D12` → `Vulkan` → `D3D11` → `OpenGL`, overridable with the `CNA_DILIGENT_DEVICE` environment variable).
    - Shaders are authored once in HLSL and cross-compiled by Diligent for whichever device was selected.
    - Implements a 2D/3D baseline only — render targets, cube/volume textures, MSAA and most stock effects are not implemented yet and refuse loudly instead of approximating. See `docs/diligent-renderer.md` and `plan_diligent.md`.

- **Vulkan renderer**
    - Present as an architecture target/scaffold.
    - Current implementation is incomplete and contains TODO/stub areas.

- **Skia renderer**
    - Select with `CNA_GRAPHICS_RENDERER=SKIA`; requires the explicitly pinned external raster
      artifact rather than downloading a dependency during ordinary CMake configuration.
    - Provides the verified CPU-raster 2D boundary above. It is not a fallback alias for EasyGL,
      and SDL is used only to present the completed CPU image.
    - See [`docs/skia-renderer.md`](docs/skia-renderer.md) for the exact capability policy and
      [`docs/skia-developer-build.md`](docs/skia-developer-build.md) for the pinned artifact,
      fresh-checkout build, Xvfb tests, fallback policy, and diagnostics.

- **GDI renderer (Windows-only, 2D-only)**
    - CPU-rasterizes CNA's 2D SpriteBatch/textures and presents the resulting RGBA8 backbuffer to
      the SDL window's native `HWND` using classic Win32 GDI.
    - Intended for compatibility, tools, UI, and modest-resolution 2D games; it is not a hardware-
      accelerated replacement for the D3D/SDL_GPU renderers and explicitly rejects 3D operations.
      See [`docs/gdi-renderer.md`](docs/gdi-renderer.md) for its exact supported surface.

## 7. 🌐 Networking, Services & Avatar

Beyond graphics, CNA ports the XNA 4.0 `GamerServices` and `Net` namespaces (and, within
`GamerServices`, the Avatar subsystem), with real cross-platform networking behind them.

### GamerServices

- Complete XNA-shaped port of the Xbox LIVE-era gamer services API surface: `Gamer`,
  `SignedInGamer`, `GamerProfile`, `FriendGamer`/`FriendCollection`, leaderboards
  (`LeaderboardReader`/`LeaderboardWriter`/`LeaderboardEntry`), `Guide`, achievements, and more.
- **Not** binary-compatible with real Xbox Live — reimplements the public API shape with
  local/synthetic semantics, matching how FNA itself already handles this namespace.

### Net (`Microsoft::Xna::Framework::Net`)

- Complete `NetworkSession` API surface (5 enums + 18 classes).
- **Real networking** for `NetworkSessionType::SystemLink`, backed by
  [ENet](http://enet.bespin.org/) (reliable UDP, vendored directly under `third_party/enet`) —
  hosting, joining, LAN discovery, `AppData` relay, disconnect handling, and `StartGame`/`EndGame`
  state broadcast all run over a genuine transport, not a stub. Every other `NetworkSessionType`
  remains a synthetic (non-networked) stub, matching upstream XNA/FNA behavior.
- **Verified real networking across four platforms:**
  - **Linux** — native ENet/UDP, including a genuine two-OS-process loopback test.
  - **Windows** — native ENet/UDP via WinSock2; cross-compiled with MinGW-w64 and verified running
    under Wine.
  - **Web (Emscripten)** — real ENet traffic carried over actual WebSocket connections. A browser
    tab can only ever be a network *client* (browsers cannot open listening sockets at all); real
    hosting requires a Node.js-run process.
  - **Android (NDK)** — native ENet/UDP via bionic libc's genuine POSIX sockets, verified on a real
    x86_64 emulator — no platform-specific transport workarounds needed at all, unlike Web.

### Avatar

- `AvatarAnimation`, `AvatarDescription`, `AvatarRenderer`, and their supporting enums/types (all
  within `Microsoft::Xna::Framework::GamerServices`) are ported from a decompiled real Microsoft
  XNA 4.0 reference assembly — FNA itself never implemented Avatar, since real avatar rendering
  required Xbox Live's cloud avatar-editor service. The API shape is complete, with the real
  (occasionally surprising, always-inert) stubbed behavior of the original assembly preserved
  faithfully rather than "improved."

## 8. 🧰 Technology Stack

- **Language:** C++23
- **Core platform/runtime library:** SDL3 (vendored via Git submodule at `third_party/SDL`)
- **Media integration:** `SDL3_image`, `SDL3_mixer` (vendored via Git submodules)
- **Graphics dependency:** `easy-gl` (for the `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` renderers),
  resolved from the canonical `../easy-gl` sibling; EasyGL in turn resolves `../meta-gl`
- **Networking:** [ENet](http://enet.bespin.org/) (vendored directly at `third_party/enet`) —
  reliable-UDP transport backing `Microsoft::Xna::Framework::Net`'s `SystemLink` sessions
- **Utility/runtime layer:** `sharp-runtime`
- **Build system:** CMake
- **Tests:** GoogleTest (`CnaTests` target)

## 9. ⚡ Getting Started

### Prerequisites (Linux)

- CMake 3.20+
- C++23-capable compiler (GCC 12+ or Clang 15+)
- Dependency directories available to CMake:
    - `../sharp-runtime`
    - `../easy-gl` and `../meta-gl` (needed for the `OPENGLES2`/`OPENGLES3`/`OPENGL33`/
      `WEBGL1`/`WEBGL2` renderers)
- SDL3, SDL3_image, and SDL3_mixer are built from vendored submodules by default — no system SDL packages required.

### Prerequisites (Windows)

- CMake 3.20+
- One of:
    - **MSVC 2022** (Visual Studio 2022, v17.8+, with C++20/23 support)
    - **clang-cl** (LLVM for Windows, targeting MSVC ABI)
    - **MinGW-w64** (either natively on Windows or cross-compiled from Linux)
- Dependency directories:
    - `../sharp-runtime` (no external dependencies — builds cleanly on Windows)
- SDL3, SDL3_image, and SDL3_mixer are built from vendored submodules by default — no pre-built SDL binaries or `CMAKE_PREFIX_PATH` configuration required.

### Initialise Submodules

Before the first build, initialise the vendored SDL submodules:

```bash
git submodule update --init --recursive
```

This populates `third_party/SDL`, `third_party/SDL_image`, and `third_party/SDL_mixer`.
After that, no system SDL packages are required.

> **Building from a source zip/tarball instead of a Git clone?** GitHub's "Download ZIP"
> and release archives do **not** include submodule contents, so `third_party/SDL` will be
> empty and CMake aborts with a clear error (`Missing vendored 'SDL' … Run: git submodule
> update --init --recursive`, from `cmake/ThirdPartySDL.cmake`). Either clone with Git and run
> the command above, or set `-DCNA_USE_SYSTEM_SDL=ON` to use system-installed SDL3 packages.

### Build (Linux — OPENGLES3 renderer, default)

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build --target CNA CnaTests
```

### Build (Linux — SDL_RENDERER renderer)

```bash
git submodule update --init --recursive
cmake -S . -B build-sdlrenderer -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build-sdlrenderer --target CNA CnaTests
```

### Build (Windows — SDL_RENDERER renderer, vendored SDL)

On Windows the `SDL_RENDERER` renderer is selected automatically when no renderer is
explicitly specified. SDL is built from the vendored submodule — no pre-built SDL
binaries or `CMAKE_PREFIX_PATH` needed.

```bash
git submodule update --init --recursive
cmake -S . -B build-win -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build-win --target CNA CnaTests
```

### Build (Linux → Windows cross-compilation with MinGW-w64)

```bash
# Install cross toolchain
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build-windows --target CNA CnaTests
```

### Optional: use system-installed SDL

If you prefer to link against system-installed SDL3 packages instead of the
vendored submodules, pass `-DCNA_USE_SYSTEM_SDL=ON`:

```bash
cmake -S . -B build -DCNA_USE_SYSTEM_SDL=ON -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build --target CNA CnaTests
```

This calls `find_package(SDL3 REQUIRED)`, `find_package(SDL3_image REQUIRED)`,
and `find_package(SDL3_mixer REQUIRED)` and requires those packages to be present
on the system (e.g. installed via your package manager).

### Other renderers

```bash
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=BGFX
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=VULKAN
```

### Build (Windows cross-compilation — Glide 3.x renderer)

`GLIDE` is a real historical Glide 3.x call path, not an SDL rendering fallback. It loads a
separately supplied `glide3x.dll` at runtime (typically an emulator such as dgVoodoo2). It supports
2D `SpriteBatch` draws and a constrained fixed-function color/textured 3D path, both submitted as
real Glide triangles. See
[`docs/glide-renderer.md`](docs/glide-renderer.md) for runtime setup and limitations.

```bash
cmake -S . -B cmake-build-glide -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake \
      -DCNA_GRAPHICS_RENDERER=GLIDE \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-glide --target cna_glide_smoke -j2
```

Copy the external emulator's compatible `glide3x.dll` beside `cna_glide_smoke.exe` (or set
`CNA_GLIDE3X_DLL`) before running it under Windows or Wine. CNA does not redistribute that DLL.

### Build (Windows cross-compilation — D3D9 renderer)

A native Direct3D 9 renderer, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`/`D3D12`), targeting real XNA 4.0 pixel authenticity rather than just feature parity —
see [`docs/directx9-renderer.md`](docs/directx9-renderer.md) for what that means and why. Developed and
verified on this repo's own Debian dev machine via the same MinGW-w64 cross toolchain the other
Windows renderers use, tested locally through Wine + DXVK (`scripts/run-wine-dxvk9.sh`). See
[`docs/directx9-renderer.md`](docs/directx9-renderer.md) and [`plan_dx9.md`](plan_dx9.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/D3D12/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d9 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=D3D9 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d9 --target CNA
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop, in a prefix separate from D3D11's own
(`docs/directx9-renderer.md` has full setup steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d9 -L D3D9 --output-on-failure
```

### Build (Windows cross-compilation — D3D11 renderer)

A native Direct3D 11 renderer, Windows-only (hard-`FATAL_ERROR`-gated at configure time on any other
`CMAKE_SYSTEM_NAME`). Developed and verified on this repo's own Debian dev machine via the same
MinGW-w64 cross toolchain `SDL_RENDERER` uses, tested locally through Wine + DXVK
(`scripts/run-wine-dxvk.sh`) before any real-Windows verification pass. See
[`docs/directx11-renderer.md`](docs/directx11-renderer.md) and [`plan_dx.md`](plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d11 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=D3D11 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d11 --target CNA
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop (`docs/directx11-renderer.md` has full setup
steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d11 -R D3D11 --output-on-failure
```

### Build (Windows cross-compilation — D3D12 renderer)

A native Direct3D 12 renderer, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`). Also developed and verified on this repo's own Debian dev machine via the same MinGW-w64
cross toolchain, but tested locally through Wine + **vkd3d-proton** (`scripts/run-wine-vkd3d.sh`),
not DXVK — D3D12 needs a different Windows-D3D-to-Vulkan translation layer than D3D11, with its own
dedicated Wine prefix. Every check currently runs **off-screen only** — swap-chain presentation is
a known, real, unresolved gap on this dev loop (see `docs/directx12-renderer.md`). See
[`docs/directx12-renderer.md`](docs/directx12-renderer.md) and [`plan_dx.md`](plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d12 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=D3D12 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d12 --target CNA
```

Running the resulting `.exe`s needs a Wine + vkd3d-proton dev-loop, in a prefix separate from
D3D11's own (`docs/directx12-renderer.md` has full setup steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d12 -R D3D12 --output-on-failure
```

### Run Demo / Verification

This repository intentionally prioritizes framework/runtime development over shipping a bundled game demo executable.

Use these commands for quick environment and rendering-path verification:

```bash
ctest --test-dir build --output-on-failure
cmake --build build --target hello-triangle-sdl
```

### Tested Compilers

| Platform | Compiler | Renderer | Status |
|----------|----------|---------|--------|
| Linux x86_64 | GCC 12+ | OPENGLES3, SDL_RENDERER | ✅ |
| Linux x86_64 | Clang 15+ | OPENGLES3, SDL_RENDERER | ✅ |
| Windows x86_64 | MSVC 2022 | SDL_RENDERER | planned |
| Windows x86_64 (native) | MinGW-w64 | SDL_RENDERER | planned |
| Linux → Windows (cross) | MinGW-w64 | SDL_RENDERER | ✅ verified building + full test suite under Wine |
| Linux → Windows (cross) | MinGW-w64 | D3D9 | ✅ verified building + `D3D9` CTest suite (14 tests) under Wine+DXVK on a real GPU — 0/31 oracle scenes diverge from real XNA 4.0 at `--tolerance 0`; real Windows hardware verification still open, see `docs/directx9-renderer.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D11 | ✅ verified building + `D3D11` CTest suite (6 tests, 96+ checks) under Wine+DXVK on a real GPU — real Windows hardware verification still open, see `docs/directx11-renderer.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D12 | ✅ verified building + `D3D12` CTest suite (1 test, 80/80 checks, off-screen only) under Wine+vkd3d-proton on a real GPU — swap-chain presentation and real Windows hardware verification both still open, see `docs/directx12-renderer.md` |
| Web (Emscripten) | emcc/Clang (emsdk) | WEBGL2 | ✅ verified building + running under Node.js (as `EASYGL`, prior to the `plan_glbackends.md` rename — not yet re-verified under its new `WEBGL2` name/build flags) |
| Android (NDK) | Clang (NDK 29/30) | OPENGLES3 | ✅ verified building + running on a real x86_64 emulator (as `EASYGL`, prior to the `plan_glbackends.md` rename) |

## 10. 📖 Usage Example

Minimal XNA-style game skeleton in CNA:

```cpp
#include <memory>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MyGame final : public Game {
public:
    MyGame()
        : graphics_(this)
    {
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        logo_ = std::make_unique<Texture2D>("assets/logo.png", getGraphicsDeviceProperty());
    }

    void Update(GameTime& gameTime) override
    {
        (void)gameTime;
        // Update game state here.
    }

    void Draw(const GameTime& gameTime) override
    {
        (void)gameTime;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(CornflowerBlue);

        spriteBatch_->Begin();
        spriteBatch_->Draw(*logo_, 100.0f, 80.0f);
        spriteBatch_->End();

        device.Present();
    }

private:
    GraphicsDeviceManager graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> logo_;
};

int main()
{
    MyGame game;
    game.Run();
    return 0;
}
```

## 11. 🧠 Design & Engineering Highlights

- **API mirroring strategy:** Public classes follow XNA naming and namespace conventions to reduce conceptual migration cost from XNA/MonoGame-style code.
- **Abstraction design:** Gameplay-facing rendering APIs (`GraphicsDevice`, `SpriteBatch`, `Texture2D`) delegate to renderer interfaces instead of exposing low-level renderer objects.
- **Separation of concerns:** Public framework API, internal contracts, and renderer implementations are physically separated in directory structure and ownership.
- **Renderer-oriented architecture:** Renderer can be swapped at build-time with a single CMake option while keeping high-level game code stable.
- **Performance-minded C++ implementation:** Native code path enables tighter control over memory, lifetime, and rendering behavior than managed runtime abstractions.

## 12. 🛣 Roadmap

- Continue expanding XNA API coverage and behavior parity (incremental, class-by-class).
- Extend compiled XNA Effect Framework bytecode beyond the completed `FNA3D` implementation. Each
  additional renderer remains gated off until it passes the shared reflection, state, lifecycle,
  3D, and SpriteBatch conformance contract; fixed-function/2D-only renderers stay explicitly
  unsupported.
- Consider a real `.xnb` content-pipeline reader (currently a deliberate design choice, not a bug — CNA loads raw assets + JSON descriptors instead).
- Close the remaining named architecture-decision gaps (SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, SDL_Renderer `Texture3D`/`TextureCube` construction, EasyGL non-`Color` `SurfaceFormat` GPU forwarding, `Texture3D`/`TextureCube` sampler-bind architecture) — see `NEXT.md` §5 and `docs/graphics-renderer-feature-matrix.md`.
- Strengthen cross-platform execution targets and validation coverage.

## 13. 📜 License

CNA is licensed under the Microsoft Public License (Ms-PL). See the [LICENSE](LICENSE) file for details.

Portions of CNA are derived from or based on FNA, which is also licensed under the Microsoft Public License (Ms-PL).
