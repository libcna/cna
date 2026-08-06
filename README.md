# CNA

## 1. 🚀 Overview

CNA is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics backend layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (`Microsoft::Xna::Framework`) while using modern C++ internals.

**CNA demonstrates engine-level C++ architecture, graphics abstraction design, and backend-oriented systems engineering.**

### Quick Start

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=EASYGL
cmake --build build --target CNA CnaTests
ctest --test-dir build --output-on-failure
```

> **Looking for a specific doc?** `docs/` has 57 files — see [`docs/README.md`](docs/README.md)
> for an index of what's current vs. historical.

### Project Status

- **`Microsoft::Xna::Framework::Graphics` milestone:** qualified **~90% XNA/FNA compatibility, test-execution-verified** (not estimated) — every one of the ~26 major Graphics classes is present, implemented, and tested. **As of 2026-07-11, all 5 confirmed bugs behind the original 2026-07-09 milestone declaration are fixed** (Vulkan `BlendState`, EasyGL anisotropic filtering, `IndexElementSize`'s numeric values, `Model`'s root-bone override, `SpriteBatch::Draw`'s optional source rectangle), and Vulkan `OcclusionQuery` (previously architecturally blocked) is fixed too. `docs/graphics-compatibility-report.md` is a dated snapshot from that declaration, kept for its methodology, not current status — see `NEXT.md` §5 for the actively-maintained bug list. What's left to 100% is a smaller set of individually-tracked issues plus a handful of project-owner architecture decisions (e.g. SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, `Texture3D`/`TextureCube` sampler-bind architecture) — none silent or undocumented.
- **Overall XNA 4.0 API surface:** 227 of 245 public FNA types are present in CNA (**92.7%**, computed 2026-07-11 by diffing FNA's public type list against CNA's headers) — 100% for `Graphics`/`Audio`/`Input`(+`Touch`)/`Storage`; the real gap is `.Content` (4/12 — no `.xnb` reader, by design) and `.Media` (25/25 present, but 14 are shells). See `docs/xna-4-api-coverage.md`. **Note this is a different metric from the Graphics bullet above** — this one counts whether a type/class exists at all across every XNA namespace (a raw presence count), while the Graphics "~90%" figure is a narrower, bug-weighted quality gate scoped to just the ~26 major Graphics classes (it also counts behavioral correctness, not just presence — Graphics itself is 91/91 = 100% present). The two numbers measuring different things is expected, not a typo or a contradiction.
- **The two gaps that matter most to a real port:** no `.xnb` content pipeline (CNA loads raw assets + JSON descriptors instead) and no compiled `.fx` shader bytecode support (`Effect(GraphicsDevice&, byte[])`) — the latter is the single biggest real gap, blocking 23 of the 86 official XNA samples in `../cna-samples`. See `docs/migration-guide.md`.
- **`SDL_RENDERER` backend:** Implemented path focused on practical 2D rendering workflows; 2D-only by design (3D calls throw).
- **`EASYGL` backend:** Most mature backend overall — implemented OpenGL-based path through `easy-gl`, full 2D+3D pixel-verified coverage.
- **`VULKAN` backend:** Real, working 3D rendering (all 5 stock effects, render targets, depth/stencil state, `BlendState`, `OcclusionQuery`) — second-most mature backend; the one remaining named gap is an isolated `RasterizerState.DepthBias` sub-case. See `docs/xna-4-api-coverage.md`'s per-backend table for current detail.
- **`BGFX` backend:** Broad 2D+3D functionality, largely pixel-verified parity with EasyGL/Vulkan as of this project's Phase 72 — but not unqualified full parity: known real limitations remain, including a `Depth24Stencil8`-attached `RenderTargetCube` face producing no colour output (Task 952, deferred, root cause not yet found), `DrawIndexedPrimitivesEx` silently discarding `startIndex`/`baseVertex` on a sub-range indexed draw (Task 954), and occlusion-query pixel-count correctness that can't be verified under this project's own sandbox's software GL driver. See `NEXT.md` §5 for the current, complete list.
- **`WEBGPU` backend:** Experimental fifth backend using native `wgpu-native`. The current baseline covers device/surface setup, clear/present, RGBA8 `Texture2D`, vertex/index uploads and WGSL SpriteBatch rendering. It is not yet a 3D-parity replacement for EasyGL/Vulkan/Bgfx; see [`docs/webgpu-backend.md`](docs/webgpu-backend.md) and `plan_webgpu.md`.
- **`OPENGL4` backend:** Real desktop OpenGL 4.x core-profile backend (4.1 minimum requested, `SDL_GL_CONTEXT_PROFILE_CORE`), deliberately independent of `EASYGL`/easy-gl — EasyGL requests an ES 3.0/WebGL2 context and cannot serve a desktop core profile at all. Uses its own small hand-rolled GL loader (`GL4Loader`), zero new third-party dependency beyond the platform's own GL library. All five stock effects plus `PbrEffect`/`SkinnedPbrEffect` (GLSL 410 core, stride-dispatched), real FBO render targets (2D + cube + MRT), backbuffer and render-target MSAA, real `GL_SAMPLES_PASSED` occlusion queries (exact pixel counts, unlike EasyGL's ES any-samples boolean), real wireframe via `glPolygonMode` (pixel-oracle-verified), `Texture3D`/`TextureCube` with real readback, 16/32-bit index buffers, `baseVertex`, custom GLSL `ShaderEffect` (3D + SpriteBatch), and hardware instancing through the unified vertex-stream transport. Multi-stream vertex input is reported unsupported and refused deterministically. 25 dedicated pixel-readback CTest suites, all verified against a real 4.5-core context. See [`docs/opengl4-backend.md`](docs/opengl4-backend.md) and `plan_opengl4.md`.
- **`OPENGL1` backend:** Historical-class legacy desktop OpenGL 1.x **fixed-function** backend — immediate-mode vertex emission, `GL_MODELVIEW`/`GL_PROJECTION` matrices, `glLight*` lighting (3 directional lights + specular + emissive), `glTexEnv*` combiners (`DualTextureEffect`, `EnvironmentMapEffect` reflection subset), real `GL_FOG` driven by an exact inversion of the FNA fog vector, `glAlphaFunc` alpha-test approximation. Runtime-discovers 1.2–1.5-era features via `SDL_GL_GetProcAddress` (FBO render targets 2D+cube with readback and mip regeneration, backbuffer + RT MSAA, `ARB_occlusion_query` with exact `GL_SAMPLES_PASSED` counts, extended blend, anisotropy, cube maps) — no GL loader library, no shaders anywhere, zero new third-party dependency. No custom effects, no MRT, no `Texture3D`, no instancing/multi-stream — all reported truthfully and refused deterministically. 38 dedicated CTest suites. See [`docs/opengl1-backend.md`](docs/opengl1-backend.md) and `plan_opengl1.md`.
- **`OPENGL2` backend:** Native desktop OpenGL 2.1 **compatibility-profile** backend, GLSL 1.10 throughout (runtime-compiled inline programs, attribute names bound via `glBindAttribLocation`), deliberately independent of `EASYGL` and of the other GL backends. All five stock effects plus `PbrEffect`/`SkinnedPbrEffect`, FNA fog-vector fog, real FBO render targets (2D incl. MSAA + cube), real MRT (up to 8 targets with real depth/MSAA resolve), real `GL_SAMPLES_PASSED` occlusion queries, `Texture3D`/`TextureCube` with readback, 16/32-bit indices, software `baseVertex` (pointer re-base -- no `glDrawElementsBaseVertex` on 2.1), full custom `VertexDeclaration` support (name-driven binding reads exactly the declared bytes), custom GLSL 1.10 `ShaderEffect` (3D + SpriteBatch), real Letterbox/Overscan/Stretch presentation modes, context-loss recovery, and hardware instancing when the driver grants `GL_ARB_draw_instanced`/`GL_ARB_instanced_arrays` -- the driver-dependence is why this lane added `GraphicsCapability::Instancing`. Multi-stream vertex input reported unsupported and refused deterministically. 48 dedicated CTest suites. See [`docs/opengl2-backend.md`](docs/opengl2-backend.md) and `plan_opengl2.md`.
- **`OPENGLES1` backend:** Genuine **OpenGL ES 1.1 fixed-function** backend ("Common"/CM profile), deliberately independent of `EASYGL` — EasyGL targets a shader-based ES 3.0/WebGL2 pipeline and cannot create an ES 1.1 context at all, so the two share no code. No shaders anywhere (zero `#version` directives, zero shader entry points); fixed-function matrices, `glLight*` lighting, `glTexEnv*` multitexture combiners, `GL_FOG`, alpha test, FBO render targets via `GL_OES_framebuffer_object`, and `WireFrame` emulated by re-expanding triangles to `GL_LINES`. Multiple render targets, occlusion queries, custom effects, `Texture3D`, multi-stream vertex input and instancing are all reported `false` and refused deterministically. Requires a **real system OpenGL ES 1.1 library** (`libGLESv1_CM` plus `GLES/gl.h`/`GLES/glext.h`; Debian `libgles1`, `libgles-dev`), gated at configure time with a `FATAL_ERROR` — nothing vendored or downloaded. Note that Debian builds Mesa with `-Dgles1=disabled`, so its stock driver cannot create an ES 1.1 context on **any** device; the backend is validated against a locally built ES1-capable Mesa driven by `scripts/opengles1-test-env.sh` (verified runtime identity: `OpenGL ES-CM 1.1 Mesa 25.0.7`, softpipe). 7 dedicated pixel-readback CTest suites. See [`docs/opengles1-backend.md`](docs/opengles1-backend.md) and `plan_opengles1.md`.
- **`MAGNUM` backend:** Desktop OpenGL 3.3 core backend expressed through [Magnum](https://github.com/mosra/magnum)'s typed `Magnum::GL` wrappers, on the same SDL3 window every other windowed backend uses. Covers clear/present, `Texture2D`/`TextureCube`/`Texture3D`, `RenderTarget2D`/`RenderTargetCube` (incl. MSAA resolve, mip regeneration and up to 4 simultaneous targets), `SpriteBatch`, runtime-compiled `ShaderEffect` GLSL, the full render-state set (incl. real wireframe fill, which the GLES-profile `EASYGL` backend cannot do), occlusion queries, and indexed/instanced/multi-stream draws with per-binding offsets and instance frequencies. Verified end-to-end against Mesa `llvmpipe` under `Xvfb` (pixel-asserting `Magnum_Smoke` CTest plus a GTest suite), so it needs no GPU to check. The stock-effect coverage is `BasicEffect`/`AlphaTestEffect`-shaped: the `DualTexture`/`EnvironmentMap`/`Skinned`/`Pbr` shader variants EasyGL and Vulkan carry are not generated here yet, and only `SurfaceFormat::Color` storage is allocated. See [`docs/magnum-backend.md`](docs/magnum-backend.md) and `plan_magnum.md`.
- **`D3D9` backend:** Windows-only native Direct3D 9 backend targeting real **XNA 4.0 pixel authenticity**, not just feature parity — it runs Microsoft's own vendored Stock Effects HLSL bytecode, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (14 CTest binaries). A checked-in 31-scene oracle corpus diffs CNA's render against the **real XNA 4.0 runtime's own render** of the same scene at `--tolerance 0`: **0/31 scenes currently diverge.** `GraphicsProfile.Reach`/`.HiDef` enforcement is real (the only CNA backend where it is). Render targets sampled as textures, non-`Color` `SurfaceFormat`, and real-Windows hardware verification are still open. See [`docs/d3d9-backend.md`](docs/d3d9-backend.md), [`docs/d3d9-divergence-report.md`](docs/d3d9-divergence-report.md), and `plan_dx9.md`.
- **`D3D11` backend:** Windows-only native Direct3D 11 backend, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (6 CTest binaries, 96+ checks) — all 10 stock HLSL shader variants (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`), textures/render targets (MRT/MSAA/occlusion queries), state objects, SpriteBatch, and a runtime-`D3DCompile()` custom `ShaderEffect` path are real and pixel-verified. Real-Windows hardware verification (device-lost recovery, WARP fallback, driver-specific parity) is still open. See [`docs/d3d11-backend.md`](docs/d3d11-backend.md) and `plan_dx.md`.
- **`D3D12` backend:** Windows-only native Direct3D 12 backend, cross-compiled via MinGW-w64 and verified through Wine+vkd3d-proton on a real GPU, **off-screen only** (`D3D12_Smoke` CTest, 80/80 checks) — device/queue/heaps/command-lists/fences/barriers/PSOs/root-signatures are real, all 10 stock HLSL shader variants (same DXBC as `D3D11`) and a real `SpriteBatch` are pixel-verified off-screen, and device-removed recovery is real and functionally proven. Swap-chain presentation is a known, real, unresolved gap on this dev loop (genuine Wine/vkd3d-proton `dxgi.dll` architecture mismatch, not a CNA bug); runtime-settable blend/depth-stencil/rasterizer state, per-slot `SamplerState`, render targets, `Texture3D`, occlusion queries, and real-Windows hardware verification are all still open. See [`docs/d3d12-backend.md`](docs/d3d12-backend.md) and `plan_dx.md`.
- **`CANVAS` backend:** Emscripten-only HTML Canvas 2D backend (`SpriteBatch`/`Texture2D`/`SpriteFont`/`RenderTarget2D` only, 2D-only by design like `SDL_RENDERER`) — `SpriteBatch` (incl. rotation/origin/flip/tint/transform), textures/render targets, `BlendState`/`SamplerState` mapping, and `SpriteFont` are all implemented and structurally reviewed, verified via a real `emcmake`/`emcc` 6.0.2 configure+build (`CnaTests` links and a backend-agnostic GTest suite genuinely passes under `node`). **Not yet pixel-verified in a real browser** — this dev loop has no DOM/`CanvasRenderingContext2D` at all (`node` has none; `SDL_Init(SDL_INIT_VIDEO)` itself throws under Emscripten/`node`). See [`docs/canvas-backend.md`](docs/canvas-backend.md) (incl. a manual browser verification checklist) and `plan_canvas.md`.
- **`ASCII` backend:** SDL-windowed retro glyph-grid backend — not a real terminal/TTY backend, a thin decorator around `SDL_RENDERER`'s own implementation. The game draws normally into a private offscreen target; `Present()` quantizes that frame into a glyph/color grid (a hand-authored, license-free 10-glyph density ramp, no vendored font asset) and draws it onto the real window. Two runtime modes (`CNA_ASCII_MODE=BLACKWHITE|COLOR`, no rebuild needed). 2D-only by design, same as `SDL_RENDERER`; fully pixel-verified (6 CTest binaries, real window + real readback, no `needs_human` gate unlike the GPU backends above). See [`docs/ascii-backend.md`](docs/ascii-backend.md) and `plan_ascii.md`.
- **`FREEDIRECT` backend** (formerly `DX3`)**:** Cross-platform (genuinely — builds and runs via ordinary `/usr/bin/c++`, no MinGW/Wine needed) DirectDraw-shaped 2D backend fronting `../free-direct`, a sibling project's own DirectDraw reimplementation. 2D-only by design, same spirit as `SDL_RENDERER`. All 8 plan phases complete: real device/window bring-up with a CPU-owned "shadow backbuffer" (working around a real `Lock()`-on-primary gap in `free-direct` itself), texture/render-target backends, a CPU `SpriteBatch` compositor (`BltFast` fast path + a from-scratch edge-function rasterizer for everything else), all 4 real `BlendState` presets with genuinely distinct formulas (not one collapsed baseline), bilinear filtering, and real `Wrap`/`Mirror` texture addressing — the latter two are a real capability win over `SDL_RENDERER`, which has `Wrap`/`Mirror` ⛔ BLOCKED. See [`docs/freedirect-backend.md`](docs/freedirect-backend.md) and `plan_freedirect.md`.
- **`DX1` backend:** Windows-only, MinGW-cross-compiled 2D backend talking to a **real** Windows `ddraw.h` — genuine COM `IDirectDraw`/`IDirectDrawSurface` **v1 interfaces only** (never `IDirectDraw2+`), run under Wine, with **no `../free-direct` reimplementation involved at all** (the opposite delivery route from `FREEDIRECT`). DirectX 1 shipped no Direct3D at all, so every 3D call throws by construction, not by policy. Ports `FREEDIRECT`'s already-verified CPU `SpriteBatch` compositor and blend-mode math verbatim (`IDirectDrawSurface::Blt` has never supported rotation in any DirectX version), while sourcing the surface layer from a real device — a real Win32 `HWND`, a real `IDirectDraw` object, and a per-frame recomputed letterbox `Present()` that (unlike `FREEDIRECT`) has no stale-scale bug after a resize. All 8 plan phases complete, 10/10 CTests passing through a real Wine `ddraw.dll` run. See [`docs/dx1-backend.md`](docs/dx1-backend.md), `plan_dx1.md`, and `plan_dxold.md` (the roadmap for the wider DX1/2/3/5/6/7/8/10 backend family).
- **`DX2` backend:** Same real Windows-only 2D layer as `DX1` (DirectDraw v1, ported verbatim), plus a **real, working 3D pipeline** — the first legacy-DirectX backend in this family with actual 3D rendering, not a permanent throw. Built on `IDirect3D2`/`IDirect3DDevice2`'s `DrawPrimitive`/`DrawIndexedPrimitive` immediate-mode API, not the literal DirectX-2-SDK execute-buffer surface (`IDirect3D`/`IDirect3DDevice::Execute`), which an exhaustive 14-variant existence-gate spike found non-functional in this environment's Wine despite every API call succeeding — an owner-confirmed scope decision to deliver genuine 3D over exact-SDK-version purity. `VertexBuffer`/`IndexBuffer` (16- and 32-bit), real CPU transform + near-plane clipping submitted as `D3DTLVERTEX`, genuine order-independent depth-test occlusion, real one-texture sampling, full per-draw rasterizer/depth/blend/sampler state, and (Phase O9) real CPU-computed ambient + directional-light Lambertian/Blinn-Phong lighting for the two normal-bearing vertex layouts (specular composited by real `D3DRENDERSTATE_SPECULARENABLE` hardware) are all pixel-verified; `WireFrame` fill mode is spike-confirmed genuinely distinct. Fog/multitexture/environment-mapping/skinning are still accepted but not evaluated, matching the `Software` backend's own identical scope boundary. All 9 plan phases complete, 19/19 CTests passing through a real Wine `ddraw.dll`+`d3d.dll` run. See [`docs/dx2-backend.md`](docs/dx2-backend.md), `plan_dx2.md`, and `dx2-spike/README.md` (the execute-buffer investigation).
- **`DX3` backend:** CNA's real DirectX 3 backend (originally landed under the temporary `DX30` name; renamed to `DX3` on 2026-08-04 when the `free-direct`-backed backend became `FREEDIRECT` — its historical `DX30-*` task IDs are unchanged). Architecturally `DX2` plus one upgrade: the DirectDraw object is `IDirectDraw2` (not v1), QueryInterface'd immediately after `DirectDrawCreate` and used for every subsequent call — spike-confirmed a fully-functional drop-in for everything `DX1`/`DX2` already do, including the entire `IDirect3D2`/`IDirect3DDevice2` 3D chain. Everything else (2D compositor, 3D pipeline, CPU lighting, `WireFrame`) is a verbatim, mechanically-ported copy of `DX2`'s own post-Phase-O9 code. 19/19 CTests pass, all green on the first run. See [`docs/dx3-backend.md`](docs/dx3-backend.md), `plan_dx3.md`, and `dx3-spike/README.md`.
- **`DX5` backend:** CNA's real DirectX 5 backend — the first release where execute buffers disappear entirely (`IDirect3DDevice3` only ever exposes `DrawPrimitive`/`DrawIndexedPrimitive`). A further port of `DX3`'s own 2D+3D layers: *every* surface (not just the top object) upgrades to v4 (`IDirectDraw4`/`IDirectDrawSurface4`/`DDSURFACEDESC2`/`DDSCAPS2`), and the 3D layer upgrades to `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`, submitting the same `D3DTLVERTEX` struct via the new `D3DFVF_TLVERTEX` FVF bitmask instead of the old `D3DVERTEXTYPE` enum. Also uses a real `IDirect3DViewport3::Clear2` call for depth clearing, replacing `DX2`/`DX3`'s manual Z-buffer `Lock()` workaround. 19/19 CTests pass, all green on the first run. See [`docs/dx5-backend.md`](docs/dx5-backend.md), `plan_dx5.md`, and `dx5-spike/README.md`.
- **`DX6` backend:** CNA's real DirectX 6 backend — introduces **no new COM interface at all** (confirmed via header inspection: no `IDirect3D4`/`IDirect3DDevice4` exists), reusing `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`/`IDirectDraw4` verbatim from `DX5`. Its real deliverable is genuine **stencil buffer operations**, resolving a boundary `DX2`/`DX3`/`DX5` all explicitly documented as unavailable: a combined depth+stencil Z-buffer surface (`DDPF_ZBUFFER|DDPF_STENCILBUFFER`, 32-bit total, D24S8-equivalent) plus real `D3DRENDERSTATE_STENCIL*` write/test wiring in `ApplyDepthStencilState`, proven end-to-end through `GraphicsDevice.DepthStencilState` (stamp-then-test, order-independent). Multitexturing is deliberately deferred (`D3DFVF_TLVERTEX` carries only one UV pair; genuine `DualTextureEffect` support would need a second vertex layout) and DXTn is out of scope (no consumer in CNA's content pipeline) — both documented, not silently dropped. Everything else is an unmodified port of `DX5`'s own 2D+3D layers. 20/20 CTests pass (19 ported + the new `Dx6_Stencil`), all green on the first run. See [`docs/dx6-backend.md`](docs/dx6-backend.md), `plan_dx6.md`, and `dx6-spike/README.md`.
- **`DX7` backend:** CNA's real DirectX 7 backend — a genuine architectural change vs `DX6`: new `IDirectDraw7`/`IDirect3D7`/`IDirect3DDevice7` interfaces (created via the new `DirectDrawCreateEx` entry point), the **entire viewport object removed** (no `IDirect3DViewport` at all any more — `IDirect3DDevice7::SetViewport`/`Clear` are direct device methods), a shorter `CreateDevice` signature, and texture binding simplified to a direct `SetTexture(stage, surface)` call (no more texture-handle indirection). Stencil is unchanged from `DX6`, ported verbatim and spike-confirmed to survive all three architectural changes. Hardware T&L is genuinely available in this environment's Wine but deliberately not adopted (this backend family submits CPU-pre-transformed-and-lit vertices by design); cube environment maps are deferred for the same class of reason as `DX6`'s multitexture deferral. A real, empirically-found API restriction: the legacy `D3DRENDERSTATE_TEXTUREMAPBLEND` render state is rejected outright by DX7 ("Render state 0x15 is invalid in d3d7"), fixed with `SetTextureStageState`/`D3DTOP_MODULATE` instead. 20/20 CTests pass (19 ported + the renamed `Dx7_Stencil`). See [`docs/dx7-backend.md`](docs/dx7-backend.md), `plan_dx7.md`, and `dx7-spike/README.md`.
- **`DX8` backend:** CNA's real DirectX 8 backend — architecturally very different from `DX1`..`DX7`: DirectDraw and Direct3D merge in DX8, so this backend has **no DirectDraw at all**, a single `IDirect3D8::CreateDevice` call creating both the device and its own real swap chain (the same device-bring-up shape as `D3D9GraphicsBackend`). Delivered via **DXVK 2.6.0's D8VK** (`Direct3DCreate8`, not Wine's built-in `ddraw.dll`), the same "Route B" pattern D3D9/D3D11/D3D12 already use. Scope is fixed-function 3D only (an owner-confirmed decision — real XNA effects need `ps_2_0`+ regardless of Shader Model 1.x support, so a real SM1.x pipeline would not make `CreateEffectBackend` usable for actual content). `D3DTLVERTEX` no longer exists in the real headers (D3D8 introduced the generic FVF model) — a hand-defined `Dx8TLVertex` reproduces the same byte layout. No scaled-blit primitive exists at all (`CopyRects` is same-size-only, no `StretchRect`) — solved with an internal logical-resolution render target and a letterbox-scaled full-screen-quad `Present()`. `SpriteBatch` is a genuine redesign (real GPU-textured quads through the fixed-function pipeline, not a DirectDraw `Blt` compositor) and blending is real GPU hardware blending with no preset-detection fallback (unlike `DX2`-`DX7`'s CPU emulation) — though D3D8 has no configurable blend equation, so factor-only-matching a preset is genuinely indistinguishable from that preset on this hardware. `AnisotropicFiltering` reports `true` (unlike every prior backend in this family) since DX8 runs on a real GPU via DXVK. Two environment-specific Wine/DXVK/AMD-RADV driver bugs were found and worked around (a dedicated Wine prefix with `dxgi` deliberately not DXVK-overridden, and forcing the software Vulkan device to avoid a real RADV bug on the second consecutive `Present()` call) — both fully documented, not code defects. 20/20 CTests pass. See [`docs/dx8-backend.md`](docs/dx8-backend.md), `plan_dx8.md`, and `dx8-spike/README.md`.
- **`D3D10` backend:** CNA's real Direct3D 10 backend — architecturally very different from `DX1`..`DX8`: D3D10 (2006) removed the fixed-function pipeline **entirely**, so every 2D and 3D draw is a real, compiled HLSL `vs_4_0`/`ps_4_0` shader pair (`D3DCompile`, following this project's own `D3D9`/`D3D11` precedent), not `DX1`..`DX8`'s CPU-transform-and-submit model. Delivered via **Wine's own builtin `d3d10.dll`/`d3d10_1.dll`** (thin wrappers; DXVK 2.6.0 ships no `d3d10.dll` at all) forwarding to **DXVK's real `d3d10core.dll`** + DXVK's `dxgi.dll`. Real state OBJECTS (`ID3D10BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`, not per-call render states) and real MRT support (`MultipleRenderTargets` reports `true`, a genuine difference from every `DX1`..`DX8` backend). Scope is deliberately bounded for this v1 (an owner-confirmed decision, mirroring `DX1`'s own "baseline first, richness later" precedent): `DrawColoredPrimitives` is real (vertex-color only, matching `BasicEffect(VertexColorEnabled=true)`), while lighting/texturing via `DrawPrimitivesEx`, custom effects, and occlusion query are left at `IGraphicsBackend`'s own safe defaults. `SpriteBatch` is real GPU-quad rendering through the same real shader pipeline. Two environment bugs were found and fixed (a stale/broken `d3d10.dll` symlink inherited from an older DXVK version, and a real DXVK `dxgi` `Present()`-path bug under Xvfb worked around by testing against the real desktop `DISPLAY=:0`), plus three real backend bugs found via CTest (a `D3D10_BLEND_DESC` API-shape difference from D3D11's own per-target blend array, a 180°-rotation winding-order/culling bug, and a `SpriteBatch::Begin()` ordering bug that discarded a caller's own transform matrix). 10/10 CTests pass. See [`docs/d3d10-backend.md`](docs/d3d10-backend.md), `plan_d3d10.md`, and `dx10-spike/README.md`.
- **Verification methodology:** differential testing against a real, running `FNA.dll` reference implementation (`tools/fna-reference/`), disputed behavior settled against genuine XNA 4.0 on a Windows 7 VM, and a compile-time `NOXNA` purity check (a dedicated CMake build option that turns every non-XNA-tagged declaration into a `[[deprecated]]` warning under `-Werror`) — see `CHECKLIST.md`'s "NOXNA markers" section and `CMakeLists.txt`.
- **CI is Linux-only and partial** (see `.github/workflows/`): it currently runs only the `Input` and `Devices`/`Sensors` gtest suites — not the full ~4,370-test unit suite and not the ~490-test GPU pixel-test suite across the 4 established graphics backends; the experimental WebGPU backend does not yet have pixel-test coverage. Everything Graphics-related in this README is verified by running the suites locally and by hand, not by an automated Graphics CI gate; there is currently no Windows, macOS, or Android CI at all (Windows/Android are verified manually, per §7/§9 below).

## 2. 🎯 Goals

- Recreate the XNA developer experience in native C++.
- Provide a native C++ path for teams that like the XNA/MonoGame model but need non-managed runtime/toolchain control.
- Mirror core XNA namespaces and API patterns while implementing them incrementally.
- Decouple gameplay-facing API from rendering backend implementation details.
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
- NOXNA extensions beyond stock XNA: `TextInputEXT` (IME composition), rumble/trigger-rumble/light-bar/
  gyro/accelerometer on `GamePad`, raw `CNA::Input::Joysticks` (distinct from `GamePad`'s mapped view),
  device-level `CNA::Input::Sensors`/`Power`, and `CNA::Input::Haptics` for standalone haptic devices.
- Single SDL event funnel (`SdlInputBridge::ProcessEvent`), backend-agnostic — Input behavior is
  identical across all 4 graphics backends (EasyGL/Vulkan/bgfx/SDL_RENDERER), verified by the
  `CnaTests` input suite.

### Rendering

- `GraphicsDevice` abstraction with backend delegation.
- `SpriteBatch` API with `Begin(...)` / `Draw(...)` / `End()` workflow.
- `Texture2D` abstraction with backend-owned texture resources.

### Cross-Platform Direction

- SDL3-based platform foundation for windowing/input/audio integration.
- Backend abstraction supports targeting multiple rendering paths from one API layer.
- **Windows support** via the `SDL_RENDERER` backend (MSVC, clang-cl, or MinGW-w64) — cross-compiled
  with MinGW-w64 and verified running under Wine.
- Linux support via `EASYGL` (OpenGL) or `SDL_RENDERER`.
- **Web (Emscripten) and Android (NDK) targets are implemented and verified**, not just
  architecturally planned — see section 7 (Networking, Services & Avatar) below for real
  cross-platform `Net` verification on both.

### Performance / C++ Advantages

- Native C++23 codebase and explicit control over memory/lifetime.
- Interface-driven backend boundaries to keep hot rendering paths backend-specific.
- Lightweight gameplay-facing API over backend-specific implementations.

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
| include/CNA/Internal/Backends + src/CNA/Internal/Backends |
| - IGraphicsBackend, ISpriteBatchBackend, ITextureBackend  |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|               Backend Implementations                     |
| src/CNA/Internal/Backends/{SdlRenderer,EasyGL,Vulkan}    |
+-----------------------------------------------------------+
```

### Interface vs Implementation Separation

- **Public API** lives under `include/Microsoft/...` and stays framework-facing.
- **Backend contracts** live under `CNA::Internal::Backends` interfaces.
- **Backend implementations** live under `src/CNA/Internal/Backends/...`.
- `GraphicsDevice` constructs backends via factory (`CreateGraphicsBackend(...)`) based on build-time backend selection.

## 5. 🎮 Rendering System

`SpriteBatch` is the primary 2D rendering abstraction.

- You create it against a `GraphicsDevice`.
- Call `Begin(...)` to start a draw pass.
- Issue `Draw(...)` calls for textures/sprites.
- Call `End()` to close the batch.

The API surface is backend-agnostic, while rendering behavior is executed by backend-specific `ISpriteBatchBackend` implementations.

This keeps game code stable while allowing backend-specific optimizations in SDL renderer, EasyGL, or future Vulkan paths.

## 6. 🔌 Backend System

CNA supports backend selection at build-time via `CNA_GRAPHICS_BACKEND` (choose one backend per build configuration):

- `SDL_RENDERER`
- `SDL_GPU`
- `EASYGL`
- `BGFX`
- `VULKAN`
- `WEBGPU`
- `HEADLESS`
- `SOFTWARE`
- `STUB`
- `OPENGLES1` (genuine OpenGL ES 1.1 fixed-function, "Common"/CM profile -- independent of `EASYGL`, which targets ES 3.0/WebGL2 and cannot create an ES 1.1 context; needs a real system `libGLESv1_CM`)
- `OPENGL4` (real desktop OpenGL 4.x core profile -- deliberately independent of `EASYGL`, which targets OpenGL ES 3.0/WebGL2 and cannot create a desktop core-profile context)
- `OPENGL1` (legacy desktop OpenGL 1.x fixed-function -- Historical class, independent of `EASYGL` and of `OPENGL4`; desktop Linux/Windows only)
- `OPENGL2` (native desktop OpenGL 2.1 compatibility profile, GLSL 1.10 -- independent of `EASYGL` and of the other GL backends)
- `D3D9` (Windows-only; native Direct3D 9 running Microsoft's own vendored Stock Effects HLSL bytecode)
- `D3D11` (Windows-only)
- `D3D12` (Windows-only)
- `CANVAS` (Emscripten only)
- `ASCII`
- `FREEDIRECT` (formerly `DX3`)
- `DX1` (Windows-only)
- `DX2` (Windows-only)
- `DX3` (Windows-only; CNA's real DirectX 3 backend -- see `plan_dx3.md`)
- `DX5` (Windows-only; CNA's real DirectX 5 backend -- DirectDraw v4 + Direct3D v3 FVF `DrawPrimitive`)
- `DX6` (Windows-only; CNA's real DirectX 6 backend -- same interfaces as DX5, plus real stencil buffer operations)
- `DX7` (Windows-only; CNA's real DirectX 7 backend -- new IDirectDraw7/IDirect3D7/IDirect3DDevice7, viewport object removed, direct texture binding)
- `DX8` (Windows-only; CNA's real DirectX 8 backend -- no DirectDraw at all, real IDirect3D8/IDirect3DDevice8 via DXVK's D8VK, fixed-function 3D only)
- `D3D10` (Windows-only; CNA's real Direct3D 10 backend -- no fixed-function pipeline at all, real HLSL vs_4_0/ps_4_0 shaders via Wine's own d3d10.dll + DXVK's d3d10core.dll, real MRT)
- `WICKED` (Wicked Engine's `wi::graphics` RHI; Linux/Windows, needs a Wicked Engine checkout)

### Tradeoffs

- **SDL_Renderer backend**
    - Simpler integration and broad SDL portability.
    - Good for straightforward 2D workflows.

- **EasyGL backend (OpenGL-based path through `easy-gl`)**
    - Custom shader-driven rendering path.
    - Better control over rendering behavior and extensibility than fixed SDL renderer usage.

- **BGFX backend**
    - Dedicated backend option with the same public rendering API coverage as other backends.
    - Integrates through CNA backend abstraction and can be selected via `CNA_GRAPHICS_BACKEND=BGFX`.
    - Uses native `bgfx` API (window/platform init, texture creation, sprite draws, frame submission), not `SDL_Renderer` rendering.
    - `bgfx` is integrated in CMake for this backend via `FetchContent` (`bgfx.cmake`).

- **Vulkan backend**
    - Present as an architecture target/scaffold.
    - Current implementation is incomplete and contains TODO/stub areas.

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
- **Graphics dependency:** `easy-gl` (for `EASYGL` backend)
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
    - `../easy-gl` (only needed for `EASYGL` backend)
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

### Build (Linux — EASYGL backend, default)

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=EASYGL
cmake --build build --target CNA CnaTests
```

### Build (Linux — SDL_RENDERER backend)

```bash
git submodule update --init --recursive
cmake -S . -B build-sdlrenderer -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build-sdlrenderer --target CNA CnaTests
```

### Build (Windows — SDL_RENDERER backend, vendored SDL)

On Windows the `SDL_RENDERER` backend is selected automatically when no backend is
explicitly specified. SDL is built from the vendored submodule — no pre-built SDL
binaries or `CMAKE_PREFIX_PATH` needed.

```bash
git submodule update --init --recursive
cmake -S . -B build-win -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build-win --target CNA CnaTests
```

### Build (Linux → Windows cross-compilation with MinGW-w64)

```bash
# Install cross toolchain
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build-windows --target CNA CnaTests
```

### Optional: use system-installed SDL

If you prefer to link against system-installed SDL3 packages instead of the
vendored submodules, pass `-DCNA_USE_SYSTEM_SDL=ON`:

```bash
cmake -S . -B build -DCNA_USE_SYSTEM_SDL=ON -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build --target CNA CnaTests
```

This calls `find_package(SDL3 REQUIRED)`, `find_package(SDL3_image REQUIRED)`,
and `find_package(SDL3_mixer REQUIRED)` and requires those packages to be present
on the system (e.g. installed via your package manager).

### Other backends

```bash
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=BGFX
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=VULKAN
```

### Build (Windows cross-compilation — D3D9 backend)

A native Direct3D 9 backend, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`/`D3D12`), targeting real XNA 4.0 pixel authenticity rather than just feature parity —
see [`docs/d3d9-backend.md`](docs/d3d9-backend.md) for what that means and why. Developed and
verified on this repo's own Debian dev machine via the same MinGW-w64 cross toolchain the other
Windows backends use, tested locally through Wine + DXVK (`scripts/run-wine-dxvk9.sh`). See
[`docs/d3d9-backend.md`](docs/d3d9-backend.md) and [`plan_dx9.md`](plan_dx9.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/D3D12/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d9 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D9 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d9 --target CNA
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop, in a prefix separate from D3D11's own
(`docs/d3d9-backend.md` has full setup steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d9 -L D3D9 --output-on-failure
```

### Build (Windows cross-compilation — D3D11 backend)

A native Direct3D 11 backend, Windows-only (hard-`FATAL_ERROR`-gated at configure time on any other
`CMAKE_SYSTEM_NAME`). Developed and verified on this repo's own Debian dev machine via the same
MinGW-w64 cross toolchain `SDL_RENDERER` uses, tested locally through Wine + DXVK
(`scripts/run-wine-dxvk.sh`) before any real-Windows verification pass. See
[`docs/d3d11-backend.md`](docs/d3d11-backend.md) and [`plan_dx.md`](plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d11 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D11 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d11 --target CNA
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop (`docs/d3d11-backend.md` has full setup
steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d11 -R D3D11 --output-on-failure
```

### Build (Windows cross-compilation — D3D12 backend)

A native Direct3D 12 backend, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`). Also developed and verified on this repo's own Debian dev machine via the same MinGW-w64
cross toolchain, but tested locally through Wine + **vkd3d-proton** (`scripts/run-wine-vkd3d.sh`),
not DXVK — D3D12 needs a different Windows-D3D-to-Vulkan translation layer than D3D11, with its own
dedicated Wine prefix. Every check currently runs **off-screen only** — swap-chain presentation is
a known, real, unresolved gap on this dev loop (see `docs/d3d12-backend.md`). See
[`docs/d3d12-backend.md`](docs/d3d12-backend.md) and [`plan_dx.md`](plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d12 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D12 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d12 --target CNA
```

Running the resulting `.exe`s needs a Wine + vkd3d-proton dev-loop, in a prefix separate from
D3D11's own (`docs/d3d12-backend.md` has full setup steps); CTest wires this in automatically:

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

| Platform | Compiler | Backend | Status |
|----------|----------|---------|--------|
| Linux x86_64 | GCC 12+ | EASYGL, SDL_RENDERER | ✅ |
| Linux x86_64 | Clang 15+ | EASYGL, SDL_RENDERER | ✅ |
| Windows x86_64 | MSVC 2022 | SDL_RENDERER | planned |
| Windows x86_64 (native) | MinGW-w64 | SDL_RENDERER | planned |
| Linux → Windows (cross) | MinGW-w64 | SDL_RENDERER | ✅ verified building + full test suite under Wine |
| Linux → Windows (cross) | MinGW-w64 | D3D9 | ✅ verified building + `D3D9` CTest suite (14 tests) under Wine+DXVK on a real GPU — 0/31 oracle scenes diverge from real XNA 4.0 at `--tolerance 0`; real Windows hardware verification still open, see `docs/d3d9-backend.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D11 | ✅ verified building + `D3D11` CTest suite (6 tests, 96+ checks) under Wine+DXVK on a real GPU — real Windows hardware verification still open, see `docs/d3d11-backend.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D12 | ✅ verified building + `D3D12` CTest suite (1 test, 80/80 checks, off-screen only) under Wine+vkd3d-proton on a real GPU — swap-chain presentation and real Windows hardware verification both still open, see `docs/d3d12-backend.md` |
| Web (Emscripten) | emcc/Clang (emsdk) | EASYGL | ✅ verified building + running under Node.js |
| Android (NDK) | Clang (NDK 29/30) | EASYGL | ✅ verified building + running on a real x86_64 emulator |

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
- **Abstraction design:** Gameplay-facing rendering APIs (`GraphicsDevice`, `SpriteBatch`, `Texture2D`) delegate to backend interfaces instead of exposing low-level renderer objects.
- **Separation of concerns:** Public framework API, internal contracts, and backend implementations are physically separated in directory structure and ownership.
- **Backend-oriented architecture:** Backend can be swapped at build-time with a single CMake option while keeping high-level game code stable.
- **Performance-minded C++ implementation:** Native code path enables tighter control over memory, lifetime, and rendering behavior than managed runtime abstractions.

## 12. 🛣 Roadmap

- Continue expanding XNA API coverage and behavior parity (incremental, class-by-class).
- Implement compiled `.fx` shader bytecode support (`Effect(GraphicsDevice&, byte[])`) — the single biggest real gap; it blocks 23 of the 86 official XNA samples in `../cna-samples`.
- Consider a real `.xnb` content-pipeline reader (currently a deliberate design choice, not a bug — CNA loads raw assets + JSON descriptors instead).
- Close the remaining named architecture-decision gaps (SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, SDL_Renderer `Texture3D`/`TextureCube` construction, EasyGL non-`Color` `SurfaceFormat` GPU forwarding, `Texture3D`/`TextureCube` sampler-bind architecture) — see `NEXT.md` §5 and `docs/graphics-backend-feature-matrix.md`.
- Strengthen cross-platform execution targets and validation coverage.

## 13. 📜 License

CNA is licensed under the Microsoft Public License (Ms-PL). See the [LICENSE](LICENSE) file for details.

Portions of CNA are derived from or based on FNA, which is also licensed under the Microsoft Public License (Ms-PL).
