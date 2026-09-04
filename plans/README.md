# CNA plans

This directory contains CNA implementation plans and retained task logs. The status recorded inside
each plan is authoritative; this index deliberately does not duplicate task counts or completion
percentages that would become stale.

Repository-wide contributor rules remain in [`AGENTS.md`](../AGENTS.md), and the per-file porting
requirements remain in [`CHECKLIST.md`](../CHECKLIST.md).

## Coordination and architecture

- [`plan.md`](plan.md) — cross-cutting and deferred work.
- [`plan_postaudit.md`](plan_postaudit.md) — post-audit development work.
- [`MODULARIZATION_PLAN.md`](MODULARIZATION_PLAN.md) — library modularization.
- [`plan_platform.md`](plan_platform.md) — platform abstraction.
- [`plan_apple.md`](plan_apple.md) — macOS and iOS platform support.
- [`plan_runtimerenderer.md`](plan_runtimerenderer.md) — runtime graphics-renderer selection.
- [`plan_sdl3free.md`](plan_sdl3free.md) — SDL3-independent build paths.
- [`plan_modern.md`](plan_modern.md) — the `CNA::Graphics` modern engine layer.
- [`cna-samples/plan.md`](../../cna-samples/plan.md) — authoritative sample coverage,
  cross-repository gap fixes and native/web verification work.

## XNA subsystems, content, and ABI

- [`plan_audio.md`](plan_audio.md) — audio and XACT compatibility.
- [`plan_devices.md`](plan_devices.md) — XNA devices and sensors.
- [`plan_cna_devices.md`](plan_cna_devices.md) — `CNA::Devices` extensions.
- [`plan_input.md`](plan_input.md) — input compatibility and stabilization.
- [`plan_media.md`](plan_media.md) — media API implementation.
- [`plan_net.md`](plan_net.md) — networking, GamerServices, and Avatar APIs.
- [`plan_binding.md`](plan_binding.md) — native C API and stable C ABI.
- [`plan_bindings_upstream.md`](plan_bindings_upstream.md) — defects the ten language
  bindings measured in CNA, and the CNA-side work they are waiting on.
- [`plan_cnj.md`](plan_cnj.md) — `.cnj` content format.
- [`plan_gltf.md`](plan_gltf.md) — glTF import correctness.
- [`plan_xnb.md`](plan_xnb.md) — XNB content pipeline.

## Shader language and conformance

- [`plan_csl.md`](plan_csl.md) — CSL, the CSIR intermediate representation, the CNA Shader ABI,
  the five shader backends, the CPU reference interpreter and the shader conformance framework.
  Design proposal: [`../misc/csl.md`](../misc/csl.md).

## Graphics architecture and effects

- [`plan_graphics.md`](plan_graphics.md) — graphics API and shared implementation work.
- [`plan_glbackends.md`](plan_glbackends.md) — OpenGL backend restructuring.
- [`plan_fna3d.md`](plan_fna3d.md) — FNA3D renderer integration.
- [`plan_fx.md`](plan_fx.md) — compiled XNA effect bytecode.

## Graphics backends and renderers

- [`plan_ascii.md`](plan_ascii.md) — ASCII renderer.
- [`plan_blend2d.md`](plan_blend2d.md) — Blend2D.
- [`plan_canvas.md`](plan_canvas.md) — HTML Canvas 2D.
- [`plan_d3d10.md`](plan_d3d10.md) — Direct3D 10.
- [`plan_diligent.md`](plan_diligent.md) — Diligent Engine.
- [`plan_direct2d.md`](plan_direct2d.md) — Direct2D.
- [`plan_dx.md`](plan_dx.md) — Direct3D 11 and Direct3D 12.
- [`plan_dxold.md`](plan_dxold.md) — legacy DirectX backend roadmap.
- [`plan_dx1.md`](plan_dx1.md), [`plan_dx2.md`](plan_dx2.md),
  [`plan_dx3.md`](plan_dx3.md), [`plan_dx5.md`](plan_dx5.md),
  [`plan_dx6.md`](plan_dx6.md), [`plan_dx7.md`](plan_dx7.md),
  [`plan_dx8.md`](plan_dx8.md), and [`plan_dx9.md`](plan_dx9.md) — version-specific DirectX plans.
- [`plan_freedirect.md`](plan_freedirect.md) — FreeDirect.
- [`plan_gdi.md`](plan_gdi.md) — Win32 GDI.
- [`plan_glide.md`](plan_glide.md) — Glide 3.x.
- [`plan_headless.md`](plan_headless.md) — headless rendering.
- [`plan_html_dom.md`](plan_html_dom.md) — HTML DOM rendering.
- [`plan_igl.md`](plan_igl.md) — IGL.
- [`plan_llgl.md`](plan_llgl.md) — LLGL.
- [`plan_magnum.md`](plan_magnum.md) — Magnum.
- [`plan_metal.md`](plan_metal.md) — native Metal.
- [`plan_nanovg.md`](plan_nanovg.md) — NanoVG.
- [`plan_opengl1.md`](plan_opengl1.md), [`plan_opengl2.md`](plan_opengl2.md), and
  [`plan_opengl4.md`](plan_opengl4.md) — desktop OpenGL profiles.
- [`plan_opengles1.md`](plan_opengles1.md) and [`plan_opengles2.md`](plan_opengles2.md) — OpenGL ES profiles.
- [`plan_pixijs.md`](plan_pixijs.md) — PixiJS.
- [`plan_sdlgpu.md`](plan_sdlgpu.md) — SDL GPU.
- [`plan_skia.md`](plan_skia.md) — Skia.
- [`plan_software.md`](plan_software.md) — CPU software rasterizer.
- [`plan_sokol.md`](plan_sokol.md) — sokol_gfx.
- [`plan_stub.md`](plan_stub.md) — no-op stub renderer.
- [`plan_svg_dom.md`](plan_svg_dom.md) — SVG DOM rendering.
- [`plan_tinygl.md`](plan_tinygl.md) — TinyGL.
- [`plan_threejs.md`](plan_threejs.md) — Three.js feasibility analysis (candidate identity; not authorized).
- [`plan_vulkan.md`](plan_vulkan.md) — Vulkan renderer parity, correctness, validation and
  EasyGL-equivalence plan.
- [`plan_webgpu.md`](plan_webgpu.md) — WebGPU.
- [`plan_wicked.md`](plan_wicked.md) — Wicked Engine.

## Archived plans

- [`plan_audio20260717.md`](plan_audio20260717.md) — archived audio-plan history; use
  [`plan_audio.md`](plan_audio.md) for current work.
