# CNA plans

This directory contains CNA implementation plans and retained task logs. The status recorded inside
each plan is authoritative; this index deliberately does not duplicate task counts or completion
percentages that would become stale.

Repository-wide contributor rules remain in [`AGENTS.md`](../AGENTS.md), and the per-file porting
requirements remain in [`CHECKLIST.md`](../CHECKLIST.md).

## Coordination and architecture

- [`plan_postaudit.md`](plan_postaudit.md) — post-audit development work.
- [`MODULARIZATION_PLAN.md`](MODULARIZATION_PLAN.md) — library modularization.
- [`plan_platform.md`](plan_platform.md) — platform abstraction.
- [`plan_apple.md`](plan_apple.md) — macOS and iOS platform support.
- [`plan_runtimerenderer.md`](plan_runtimerenderer.md) — runtime graphics-renderer selection.
- `plan_sdl3free.md` — historical SDL3-independent migration plan; its SDL2 route was superseded
  by native backends and retired on 2026-10-04, and those native backends were retired in turn on
  2026-10-06. Deleted on 2026-10-26 (Git history only); the SDL-free configurations that remain
  are described in [`../docs/platform-abstraction.md`](../docs/platform-abstraction.md).
- `plan_modern.md` — the former `CNA::Graphics` modern engine layer; that layer was removed in the
  2026-09 scope reduction and the plan was deleted on 2026-10-11 (Git history only). The surviving
  extension module is described in [`../docs/cnaext-engine-layer.md`](../docs/cnaext-engine-layer.md).
- `plan_modern_retirement.md` — MRT-001..012, the 2026-10-13 removal of the modern GPU layer
  (compute, storage, texture arrays, indirect draw, GPU timers; C ABI 0.47.0, see
  [`../docs/c-api/ABI_VERSIONING.md`](../docs/c-api/ABI_VERSIONING.md)); all done, deleted on
  2026-10-26 (Git history only).
- [`plan_diagnostics.md`](plan_diagnostics.md) — renderer-independent diagnostics, profiling,
  bounded recording, and the Inspector provider boundary.
- [`plan_inspector.md`](plan_inspector.md) — optional authenticated agent, compact protocol,
  separate bridge/browser UI, security, tests, and performance validation.
- [`cna-samples/plan.md`](../../cna-samples/plan.md) — authoritative sample coverage,
  cross-repository gap fixes and native/web verification work.

## XNA subsystems, content, and ABI

- [`plan_audio.md`](plan_audio.md) — audio and XACT compatibility.
- [`plan_devices.md`](plan_devices.md) — XNA devices and sensors.
- [`plan_cna_devices.md`](plan_cna_devices.md) — `CNA::Devices` extensions.
- [`plan_input.md`](plan_input.md) — input compatibility and stabilization.
- [`plan_media.md`](plan_media.md) — media API implementation.
- [`plan_net.md`](plan_net.md) — networking, GamerServices, and Avatar APIs up to 2026-07 (history;
  continued in `plan_gamer_services_server.md`, `plan_gamer_services_avatar_polish.md`,
  `plan_gamer_services_xbox_fidelity.md` and `plan_gamer_services_final_hardening.md`; the current
  state is [`../docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md)).
- [`plan_binding.md`](plan_binding.md) — native C API and stable C ABI.
- [`plan_bindings_upstream.md`](plan_bindings_upstream.md) — defects the ten language
  bindings measured in CNA, and the CNA-side work they are waiting on.
- [`plan_cnj.md`](plan_cnj.md) — `.cnj` content format.
- [`plan_gltf.md`](plan_gltf.md) — glTF import correctness.
- [`plan_xnb.md`](plan_xnb.md) — XNB content pipeline.
- [`plan_xnapipeline.md`](plan_xnapipeline.md) — native XNB output (closed; external verification rows only).
- [`plan_xnapipeline_parity.md`](plan_xnapipeline_parity.md) — TRUE XNA 4.0 Content Pipeline public API + input-format parity, measured against the genuine SDK.
- `plan_missing.md` — XNA-MISSING-001..020, the 160 runtime members that took coverage from 3,467
  to 3,627/3,627 (closed 2026-09-21; deleted on 2026-10-26, Git history only). The live figure is
  [`../docs/xna-4-runtime-member-coverage.md`](../docs/xna-4-runtime-member-coverage.md).

## Graphics architecture and effects

- `plan_graphics.md` — the original graphics API backlog (Tasks 1–1120 in Phases 1–83, written
  for SDL_Renderer, EasyGL, Vulkan and Bgfx). Every row was done, withdrawn with a retired
  renderer, or superseded (compiled effects by `plan_fx.md`, samples by `cna-samples/plan.md`),
  and the plan was deleted on 2026-10-26 (Git history only). Comments elsewhere cite its rows as
  "graphics Task N" or "graphics row N".
- [`plan_glbackends.md`](plan_glbackends.md) — OpenGL backend restructuring.
- [`plan_fna3d.md`](plan_fna3d.md) — FNA3D renderer integration.
- [`plan_fx.md`](plan_fx.md) — compiled XNA effect bytecode.

## Graphics backends and renderers

- [`plan_renderer_retirement.md`](plan_renderer_retirement.md) — the 2026-10-13 retirement of
  `VULKAN`, `DIRECTX11`, `METAL` and `SDL_RENDERER` (RRC-030..RRC-039, remediation from RRC-040),
  and its report. [`../docs/renderer-registry.md`](../docs/renderer-registry.md) is the canonical
  list of the 10 identities that remain; [`../docs/removed-renderers.md`](../docs/removed-renderers.md)
  records all 41 retired ones.
- [`plan_vulkan_parity.md`](plan_vulkan_parity.md) — history only: the Vulkan hardware-parity
  ledger, kept for its XNA-measured shared contracts after `VULKAN` was retired on 2026-10-13.
- [`plan_dx9.md`](plan_dx9.md) — Direct3D 9.
- [`plan_headless.md`](plan_headless.md) — headless rendering.
- [`plan_sdlgpu.md`](plan_sdlgpu.md) — SDL GPU.
- [`plan_software.md`](plan_software.md) — CPU software rasterizer.
- [`plan_stub.md`](plan_stub.md) — no-op stub renderer.
- [`plan_webgpu.md`](plan_webgpu.md) — WebGPU.
- `plan_webgpu_perf.md` — WebGPU's per-draw CPU cost (binding-model redesign); deleted on
  2026-10-11 and available only in Git history.
