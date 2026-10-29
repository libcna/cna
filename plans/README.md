# CNA plans

This directory contains CNA implementation plans and retained task logs. The status recorded inside
each plan is authoritative; this index deliberately does not duplicate task counts or completion
percentages that would become stale.

Repository-wide contributor rules remain in [`AGENTS.md`](../AGENTS.md), and the per-file porting
requirements remain in [`CHECKLIST.md`](../CHECKLIST.md).

## Coordination and architecture

- `plan_postaudit.md` — post-audit graphics remediation (`GFX-220`..`GFX-246`, `BUILD-017/018`),
  reconciled on 2026-10-14 and deleted on 2026-10-28 (Git history only). Its lasting capability
  admission rule is in
  [`../docs/renderer-capability-profiles.md`](../docs/renderer-capability-profiles.md).
- `MODULARIZATION_PLAN.md` — the three-phase target, physical-source and final module/package
  modularization campaign, closed and promoted on 2026-08-10; deleted on 2026-10-28 (Git history
  only). The current architecture and its permanent validators are in
  [`../docs/physical-modules.md`](../docs/physical-modules.md).
- [`plan_platform.md`](plan_platform.md) — platform abstraction.
- `plan_apple.md` (`APPLE-1`..`APPLE-20`, macOS/iOS platform subsystem) and `plan_apple_m4.md`
  (`AM4-*`, the Mac mini M4 qualification campaign of 2026-10-07/08) — deleted on 2026-10-28
  (Git history only). The current support boundary, design and known gaps are in
  [`../docs/apple-platforms.md`](../docs/apple-platforms.md); comments cite their rows as
  "Apple APPLE-N" and "Apple M4 AM4-NNN".
- `plan_windows_portability.md` (`WINPORT-*`, the Unicode filesystem path model on native
  Windows) and `plan_windows_portability_closeout.md` (`WINCLOSE-*`, its closing pass and the
  post-closeout `DIRECTX11` rounds) — all done on the since-deleted Windows 10 VM; deleted on
  2026-10-28 (Git history only), as `plan_win32_native_validation.md` (`WINNATIVE-*`) was on
  2026-10-11. The path contract is
  [`../docs/filesystem-path-model.md`](../docs/filesystem-path-model.md); comments cite rows as
  "Windows portability WINPORT-NNNN", "Windows closeout WINCLOSE-NNNN" and "Win32 native
  validation WINNATIVE-NNNN".
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
- `plan_diagnostics.md` (`DIAG-0001`, 2026-09-17) and `plan_diagnostics_inspector_audit.md`
  (`AUD-DIAG-INSP-0001`..`0005`, 2026-09-17/18) — the diagnostics and profiler module and the
  production-readiness audit of it and the Inspector; deleted on 2026-10-28 (Git history only).
  The module is [`../docs/diagnostics.md`](../docs/diagnostics.md), the Inspector
  [`../docs/inspector.md`](../docs/inspector.md); the one public-API defect the audit left open
  is in [`../handoff.md`](../handoff.md) §5.1.
- [`plan_inspector.md`](plan_inspector.md) — optional authenticated agent, compact protocol,
  separate bridge/browser UI, security, tests, and performance validation.
- [`cna-samples/plan.md`](../../cna-samples/plan.md) — authoritative sample coverage,
  cross-repository gap fixes and native/web verification work.
- `plan_cna_rts.md` — RTS-001/RTS-002, the 2026-10-27 `cna-rts` consumer validation and
  HiDef/direct-screenshot repair; deleted on 2026-10-28 (Git history only). Consumer-owned
  evidence and the remaining optional raw-upload cleanup are in
  [`cna-rts/missing.md`](https://github.com/libcnalab/cna-rts/blob/develop/missing.md). CNA retains
  its render-target effect-source
  [regression fixture](../modules/graphics/examples/rendertarget_effect_source_test.cpp).
- `plan_cna_benchmark.md` — BENCH-001, the 2026-10-27 `cna-benchmark` integration and streaming
  upload repair; deleted on 2026-10-28 (Git history only). The
  [`cna-benchmark`](https://github.com/libcnalab/cna-benchmark) repository owns the evidence in
  `docs/cna-integration.md`; CNA retains the texture-cache regression in commit `421a38e4f`.

## XNA subsystems, content, and ABI

- `plan_audio.md` — the 2026-07-17 "audio perfection" audit backlog (`AUD-00`..`AUD-18`, 438
  tasks; earlier audio rows such as `XA-*` and `P9`..`P12-*` came from the version it replaced),
  worked on until 2026-07-18 and deleted on 2026-10-28 (Git history only). Audio deviations are in
  [`../CHECKLIST.md`](../CHECKLIST.md); open audio defects and limitations are in
  [`../handoff.md`](../handoff.md) §5.
- `plan_devices.md` — XNA devices and sensors (`DEV-*`, `VIB-*`, `SENSORBASE-*`, `ACCEL-*`,
  `COMPASS-*`, `MOTION-*`, `ANDROID-BRIDGE-*` …, 2026-07-05/18; earlier IDs such as
  `DEVICES-0031` come from its previous versions). Deleted on 2026-10-28 (Git history only);
  comments cite it as "the devices plan". The checks still waiting for a physical device are at
  the top of [`../docs/devices-hardware-checklist.md`](../docs/devices-hardware-checklist.md); the
  2026-07-17 re-audit's engineering backlog was dropped with it (the deleting commit names the
  rows).
- `plan_cna_devices.md` — `CNA::Devices` extensions (`DEVICES-CNA-000`..`012`, 2026-07-07), all
  closed; deleted on 2026-10-28 (Git history only). The module, its rules and every capability are
  in [`../docs/devices-ext.md`](../docs/devices-ext.md).
- [`plan_input.md`](plan_input.md) — input compatibility and stabilization.
- `plan_keyboard_device_emulation.md` — INPUT-EMU-001/002, the off-by-default keyboard-driven
  accelerometer, window orientation and player-one GamePad (2026-09-28, 2026-10-03); deleted on
  2026-10-28 (Git history only). The API and its boundaries are in
  [`../docs/keyboard-device-emulation.md`](../docs/keyboard-device-emulation.md).
- [`plan_media.md`](plan_media.md) — media API implementation.
- [`plan_net.md`](plan_net.md) — networking, GamerServices, and Avatar APIs up to 2026-07 (history;
  the current state is
  [`../docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md)).
- `plan_gamer_services_server.md` (`GS-001`..`GS-011`, the CNA service, protocol, relay,
  matchmaking and standard Avatar API, 2026-09-28/29), `plan_gamer_services_avatar_polish.md`
  (`GSP-*`), `plan_gamer_services_xbox_fidelity.md` (`GSX-*`) and
  `plan_gamer_services_final_hardening.md` (`GSH-*`, 2026-09-30) — the passes that continued
  `plan_net.md`, all merged into `next`; deleted on 2026-10-28 (Git history only). Every remaining
  limitation is in
  [`../docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md),
  deployment in [`../docs/gamer-services-server.md`](../docs/gamer-services-server.md) and the
  avatar system in [`../docs/avatars.md`](../docs/avatars.md); comments cite the task IDs bare.
- `gamer_services_server_initial_inventory.md`, `gamer_services_acceptance_sources.md` and
  `gamer_services_server_final_register.md` — the GS-001 candidate baseline (`b8eb9c2b9`), sample
  acceptance-source queue (`c611878d5`) and GS-011 refusal register (`f5df1c824`), deleted on
  2026-10-28 (Git history only). Current runtime boundaries are in
  [`../docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md),
  deployment is in [`../docs/gamer-services-server.md`](../docs/gamer-services-server.md), and
  consumer sample status is in [`cna-samples/plan.md`](../../cna-samples/plan.md).
- `plan_avatar_coordinate_space.md` (`GS-009g`, the XNA -Z avatar space, 2026-10-03) and
  `plan_avatar_acceptance.md` (`AVA-01`..`AVA-10`, `AVH-01`..`AVH-08`, Avatar v3 acceptance at
  `ac20344ef`, 2026-10-25) — deleted on 2026-10-28 (Git history only). The coordinate contract and
  the acceptance command are in [`../docs/avatars.md`](../docs/avatars.md), the accepted results in
  [`../docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md) §33.
  Comments cite the rows as "Avatar acceptance AVA-NN".
- `plan_binding.md` — native C API/C ABI campaign (`CBIND-000`–`CBIND-153`) and its historical
  phase ledger; deleted on 2026-10-28 (Git history only). Current measured coverage and planned
  owners are generated in [`../docs/c-api/COVERAGE.md`](../docs/c-api/COVERAGE.md), permanent
  contracts are indexed by [`../docs/c-api/README.md`](../docs/c-api/README.md), and release
  readiness is in [`../docs/c-api/RELEASE_GATE.md`](../docs/c-api/RELEASE_GATE.md).
- `plan_capi_smoke_stability.md` (CSS-1..6), `plan_capi_coverage_audit.md` and
  `plan_capi_coverage_tooling_cleanup.md` (CTC-1..10) — the 2026-09-18 C API series: the
  process-exit lifetime fix and smoke repairs, the backlog audit, and the coverage tooling that
  made the gates truthful. All done; deleted on 2026-10-28 (Git history only). The fixes carry
  their reasons in the code, the tooling in `tools/c-api/generate_coverage_inventory.py` and
  `test_coverage_scope.py`, and the live backlog is
  [`../docs/c-api/COVERAGE.md`](../docs/c-api/COVERAGE.md).
- `plan_cabi.md` — the cross-binding C ABI correctness milestone (`CABI-1`..`CABI-49`, ABI
  0.8-0.9, 2026-08/09); deleted on 2026-10-28 (Git history only). Its contracts are in
  [`../docs/c-api/`](../docs/c-api/README.md) (wasm artifact, ContentLost, owned devices, video
  frames); the `Present` overload's C route is planned in
  [`../docs/c-api/COVERAGE.md`](../docs/c-api/COVERAGE.md) (`CBIND-127`). Comments cite its rows
  as "C ABI CABI-N".
- `plan_bindings_upstream.md` — measured XNA/FNA decisions and binding findings from the 2026-09
  campaign; deleted on 2026-10-28 (Git history only). Permanent decisions are in
  [`../docs/xna-fna-behavior-decisions.md`](../docs/xna-fna-behavior-decisions.md), current binding
  architecture is in [`../docs/binding.md`](../docs/binding.md), and current defects are only in
  [`../handoff.md`](../handoff.md) §5.1.
- `plan_cnj.md` — the `.cnj` content format and its glTF import (`CNB-1`..`CNB-110`, 2026-07);
  deleted on 2026-10-28 (Git history only). The format is
  [`../docs/cnj-format.md`](../docs/cnj-format.md), what glTF import still loses is
  [`../docs/gltf-limitations.md`](../docs/gltf-limitations.md); comments cite the rows as
  "CNJ CNB-n" (the prefix predates the binary CNB format and its `CNBF-*` tasks).
- `plan_content_pipeline.md` — the CNA Content Pipeline, `cna-content` (`CP-001`..`CP-063`,
  2026-08-29/30), merged into `next`; deleted on 2026-10-28 (Git history only). The pipeline,
  its limitations, stability summary, open questions and rejected alternatives are in
  [`../docs/content-pipeline.md`](../docs/content-pipeline.md); comments cite the rows as
  "Content Pipeline CP-nnn".
- `plan_cnb.md` — the engineering record of CNB, the compiled binary content format
  (`CNBF-001`..`CNBF-123`, `CNBF-H001`..`H020`, 2026-08-26/27); deleted on 2026-10-28 (Git
  history only). The format, its numbered reader invariants (§12.2), what is deliberately not
  implemented (`Effect` `CNBF-104`, `.cnapak` `CNBF-107`, §15) and its design decisions D1–D11
  (Appendix A) are in [`../docs/cnb-format.md`](../docs/cnb-format.md); comments cite the
  `CNBF-*` IDs bare.
- [`plan_gltf.md`](plan_gltf.md) — glTF import correctness.
- [`plan_xnb.md`](plan_xnb.md) — XNB content pipeline.
- [`plan_xnapipeline.md`](plan_xnapipeline.md) — native XNB output (closed; external verification rows only).
- [`plan_xnapipeline_parity.md`](plan_xnapipeline_parity.md) — TRUE XNA 4.0 Content Pipeline public API + input-format parity, measured against the genuine SDK.
- `plan_missing.md` — XNA-MISSING-001..020, the 160 runtime members that took coverage from 3,467
  to 3,627/3,627 (closed 2026-09-21; deleted on 2026-10-26, Git history only). The live figure is
  [`../docs/xna-4-runtime-member-coverage.md`](../docs/xna-4-runtime-member-coverage.md).
- `plan_xna_runtime_surface.md` — XNA-ENUM-001 (331/331 runtime types, the nested enumerators) and
  XNA-MEMBER-001/002 (the 3,463/3,627 member baseline and its first four fixes), all complete on
  2026-09-20; deleted on 2026-10-26 (Git history only). See
  [`../docs/xna-4-enumerator-reference.md`](../docs/xna-4-enumerator-reference.md) and
  [`../docs/xna-4-runtime-member-coverage-baseline.md`](../docs/xna-4-runtime-member-coverage-baseline.md).

## Graphics architecture and effects

- `plan_graphics.md` — the original graphics API backlog (Tasks 1–1120 in Phases 1–83, written
  for SDL_Renderer, EasyGL, Vulkan and Bgfx). Every row was done, withdrawn with a retired
  renderer, or superseded (compiled effects by the FX plan, samples by `cna-samples/plan.md`),
  and the plan was deleted on 2026-10-26 (Git history only). Comments elsewhere cite its rows as
  "graphics Task N" or "graphics row N".
- `plan_ascii.md` — the former SDL-windowed ASCII renderer campaign (`ASCII-1`..`ASCII-82`,
  completed 2026-07-15); its reusable quantizer, font atlas and rendering oracles became
  `AsciiPostProcessEffect`, and the plan was deleted on 2026-10-28 (Git history only). The optional
  perf-only `ASCII-42` was not carried forward as active work. Current behavior and migration
  commits are in [`../docs/ascii-post-process-effect.md`](../docs/ascii-post-process-effect.md).
- `plan_glbackends.md` — EasyGL as an internal family behind public GL identities (`GLB-1`..`GLB-43`,
  2026-07/09). Every row done, decided against (`GLB-14`, no rename) or moot after the 2026-10-06
  retirement of `WEBGL1`/`OPENGLES2` (`GLB-37`); deleted on 2026-10-29 (Git history only). Its two
  open performance rows, `GLB-42`/`GLB-43` (EasyGL's per-draw VAO remap and uniform/texture
  re-sends), are in `../handoff.md` §5.2; `GLB-25`'s missing `WEBGL2` test run is in
  [`../docs/webgl2-renderer.md`](../docs/webgl2-renderer.md). Comments cite rows as "GLB-nn".
- `plan_fna3d.md` — the FNA3D renderer (`FNA3D-0`..`FNA3D-63`, 2026-08/10): every task closed
  except `FNA3D-34`'s native Direct3D 11 and macOS/iOS runs, which need hardware this host does
  not have; deleted on 2026-10-29 (Git history only). The renderer, its divergences from XNA,
  its unreachable FNA3D entry points and its validation status, the open driver matrix
  included, are [`../docs/fna3d-renderer.md`](../docs/fna3d-renderer.md); the XNA oracle
  measurement is [`../docs/fna3d-parity-report.md`](../docs/fna3d-parity-report.md). Comments
  cite rows as "FNA3D-nn".
- `plan_fx.md` — compiled XNA effect bytecode (`FX-001`..`FX-147`, 2026-08/10): every task done,
  withdrawn with a retired renderer (`FX-066` Metal, `FX-068` bgfx), or, for the optional MGFX,
  offline-compiler and runtime-`.fx` evaluations `FX-095`..`FX-097`, made moot by standing
  decisions; deleted on 2026-10-29 (Git history only). The porter guide, the support matrix, the
  shared-contract and promotion rules and the standing decisions are
  [`../docs/fx-compiled-effects.md`](../docs/fx-compiled-effects.md); its two XNA-over-FNA
  rulings are in [`../docs/xna-fna-behavior-decisions.md`](../docs/xna-fna-behavior-decisions.md).
  Comments cite rows as "FX-nnn".

## Graphics backends and renderers

- `plan_renderer_retirement.md` — the 2026-10-13 retirement of `VULKAN`, `DIRECTX11`, `METAL` and
  `SDL_RENDERER` (`RRC-030`..`RRC-054`); deleted on 2026-10-28 (Git history only). The canonical
  current list is [`../docs/renderer-registry.md`](../docs/renderer-registry.md); the campaign
  record and all 41 retired identities are in
  [`../docs/removed-renderers.md`](../docs/removed-renderers.md).
- `plan_vulkan_parity.md` — VKPAR-0001..0029, the Vulkan hardware-parity ledger (Radeon 780M,
  closed 2026-09-21; `VULKAN` retired 2026-10-13). Deleted on 2026-10-26 (Git history only); its
  shared contracts live in `TwoSidedStencilTests.cpp` and `StockEffectNullTextureTests.cpp`
  (`modules/graphics/tests/`), [`../tools/xna-oracle/README.md`](../tools/xna-oracle/README.md) and
  the test display policy (`../cmake/TestDisplayPolicy.cmake`). Comments cite its rows as
  "Vulkan parity VKPAR-NNNN".
- `plan_graphics_shared_cleanup.md` — GSC-0001..0010 (2026-09-17/30): XNA's missing-texture,
  two-sided stencil and back-buffer depth rules, PBR basis guards, and test-harness fixes found by
  the DirectX 11/12 validation; its follow-ups were fixed later. Deleted on 2026-10-28 (Git history
  only); each rule is stated in the header of the test that pins it. Comments cite rows as
  "graphics shared cleanup GSC-NNNN".
- `plan_gpu_test_isolation.md` — GTI-0001..0010 (2026-09-21/22): tests no longer reach the live
  desktop, the private GPU runner, and the guard for tests that die before their checks. Deleted on
  2026-10-28 (Git history only). The policy is explained in `../cmake/TestDisplayPolicy.cmake`, the
  runner in `../tools/platform/run_gpu_tests_private.sh`, the guard in
  `../tools/platform/profile_dead_tests.py`; comments cite rows as "GPU test isolation GTI-NNNN".
- `plan_dx9.md` — the Direct3D 9 renderer (`D9-0`..`D9-140`, 2026-07/09): every phase closed
  except `D9-140`, real Windows hardware (`needs_human`); deleted on 2026-10-28 (Git history
  only). The renderer, its 17 design decisions and limitations are
  [`../docs/directx9-renderer.md`](../docs/directx9-renderer.md), the XNA oracle measurement
  [`../docs/d3d9-divergence-report.md`](../docs/d3d9-divergence-report.md); comments cite rows as
  "DX9 D9-nn".
- `plan_fullscreen_regressions.md` (`FULLSCREEN-001`) and `plan_directx9_fullscreen_motion.md`
  (`FULLSCREEN-002`) — fullscreen presentation and DirectX9 loss/reset recovery found in Mobile
  Eggbert, 2026-09-28 (museum follow-up 2026-10-22); their Vulkan and Canvas parts went with those
  renderers. Deleted on 2026-10-28 (Git history only); the DirectX9 contract is pinned by
  `directx9_frame_recovery_test.cpp`, `directx9_resize_state_test.cpp` and
  `spritebatch_presentation_test.cpp`.
- [`plan_headless.md`](plan_headless.md) — headless rendering.
- `plan_pre_sdlgpu_closeout.md` — PSG-0001..0013 (2026-09-23): the WebGPU merge, the bounded
  GoogleTest runner (`../tools/tests/run_gtest_bounded.sh`) and the measured hand-off to the SDL_GPU
  workstream; deleted on 2026-10-28 (Git history only). Comments cite rows as
  "pre-SDL_GPU PSG-NNNN".
- [`plan_sdlgpu.md`](plan_sdlgpu.md) — SDL GPU.
- [`plan_software.md`](plan_software.md) — CPU software rasterizer.
- `plan_stub.md` — STUB-1..6, the no-op Stub renderer (done 2026-07-19); deleted on 2026-10-28
  (Git history only). Its design and limitations are in
  [`../docs/stub-renderer.md`](../docs/stub-renderer.md).
- `plan_webgpu.md` — the WebGPU renderer (`WEBGPU-1`..`WEBGPU-209`, 2026-07/10): 201 of 202 rows
  done; the open one, `WEBGPU-1` (the Windows, macOS and linux-aarch64 wgpu-native packages),
  needs hardware this host does not have. Deleted on 2026-10-29 (Git history only), with
  `tools/count_webgpu_plan_status.sh`, which counted its rows. The status, the precise parity
  statement and every limitation are [`../docs/webgpu-renderer.md`](../docs/webgpu-renderer.md);
  comments cite rows as "WEBGPU-nnn".
- `plan_webgpu_perf.md` — WebGPU's per-draw CPU cost (binding-model redesign); deleted on
  2026-10-11 and available only in Git history.
