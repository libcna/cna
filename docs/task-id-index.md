# Task ID index

Comments, commit messages and documents across CNA cite task IDs such as `XNASWEEP-231`,
`GSC-0007` or `RRC-009`. Most came from an implementation plan under `plans/`. Every plan was
finished or retired and deleted, the last on 2026-10-29, and the directory went with them; the
plans themselves are in Git history (`git log --diff-filter=D -- 'plans/*'` lists them,
`git show <commit>^:plans/<file>` prints one). Later bounded maintenance audits may assign IDs
directly from their source note. This table says where each prefix originated, what the work was,
and where whatever outlived it now lives.

Current defects and limitations are [`../handoff.md`](../handoff.md) §5, not here. Where a row
says *Git history only*, nothing outside the plan survived: the work was retired with what it
served, or its result is only the code.

| Prefix | Plan, deleted | What it was | Where it lives now |
|---|---|---|---|
| `a-NNNN` | `plan.md`, 2026-10-26 | Cross-cutting tasks that crossed a layer boundary; all three done. | [`xna4-runtime-compatibility.md`](xna4-runtime-compatibility.md), `cna-samples/plan.md` |
| `AM4-*` | `plan_apple_m4.md`, 2026-10-28 | Mac mini M4 qualification, 2026-10-07/08. Cited "Apple M4 AM4-NNN". | [`apple-platforms.md`](apple-platforms.md) |
| `APPLE-1`..`20` | `plan_apple.md`, 2026-10-28 | macOS/iOS platform subsystem. Cited "Apple APPLE-N". | [`apple-platforms.md`](apple-platforms.md) |
| `ASCII-1`..`82` | `plan_ascii.md`, 2026-10-28 | The SDL-windowed ASCII renderer, completed 2026-07-15; its quantizer, atlas and oracles became a post-process effect. | [`ascii-post-process-effect.md`](ascii-post-process-effect.md) |
| `AUD-00`..`AUD-18`; older `XA-*`, `P9`..`P12-*` | `plan_audio.md`, 2026-10-28; `plan_audio20260717.md`, 2026-10-11 | The FNA audio port and its 2026-07-17 "audio perfection" audit backlog. | Deviations in [`../CHECKLIST.md`](../CHECKLIST.md); open items in handoff §5 |
| `AUD-DIAG-INSP-0001`..`0005` | `plan_diagnostics_inspector_audit.md`, 2026-10-28 | Production-readiness audit of the diagnostics module and the Inspector. | [`diagnostics.md`](diagnostics.md), [`inspector.md`](inspector.md); its open API defect in handoff §5.1 |
| `AVA-01`..`10`, `AVH-01`..`08` | `plan_avatar_acceptance.md`, 2026-10-28 | Avatar v3 acceptance and its hardening. Cited "Avatar acceptance AVA-NN". | [`avatars.md`](avatars.md), [`gamer-services-known-limitations.md`](gamer-services-known-limitations.md) §33 |
| `BENCH-001` | `plan_cna_benchmark.md`, 2026-10-28 | The external `cna-benchmark` integration and a streaming-upload repair. | `cna-benchmark` `docs/cna-integration.md`; CNA commit `421a38e4f` |
| `BINDFIX-*`, `*-UPSTREAM-*` | `plan_bindings_upstream.md`, 2026-10-28 | Findings of the 2026-09 binding campaign and the XNA/FNA decisions they measured. | [`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md), [`binding.md`](binding.md) |
| `CABI-1`..`49` | `plan_cabi.md`, 2026-10-28 | Cross-binding C ABI correctness, ABI 0.8-0.9. Cited "C ABI CABI-N". | [`c-api/`](c-api/README.md) |
| `CBIND-000`..`153` | `plan_binding.md`, 2026-10-28 | The native C API / C ABI campaign. | [`c-api/COVERAGE.md`](c-api/COVERAGE.md), [`c-api/RELEASE_GATE.md`](c-api/RELEASE_GATE.md) |
| `CNB-1`..`110` | `plan_cnj.md`, 2026-10-28 | The `.cnj` format and its glTF import; the prefix predates the binary CNB format. Cited "CNJ CNB-n". | [`cnj-format.md`](cnj-format.md), [`gltf-limitations.md`](gltf-limitations.md) |
| `CNBF-001`..`123`, `CNBF-H001`..`H020` | `plan_cnb.md`, 2026-10-28 | CNB, the compiled binary content format. | [`cnb-format.md`](cnb-format.md) |
| `COMP-*` | `plan_compilation.md`, 2026-10-25 | Build (compilation) performance. | [`build-performance.md`](build-performance.md), `tools/build/check_build_performance_policy.py` |
| `CP-001`..`063` | `plan_content_pipeline.md`, 2026-10-28 | The CNA Content Pipeline, `cna-content`. Cited "Content Pipeline CP-nnn". | [`content-pipeline.md`](content-pipeline.md) |
| `CSL-*` | `plan_csl.md`, 2026-10-17 | A proposed CNA shader language; never started and abandoned. | Git history only |
| `CSS-1`..`6`, `CTC-1`..`10` | `plan_capi_smoke_stability.md`, `plan_capi_coverage_audit.md`, `plan_capi_coverage_tooling_cleanup.md`, 2026-10-28 | The process-exit lifetime fix and smoke repairs, the backlog audit and the coverage tooling of the C API. | [`c-api/COVERAGE.md`](c-api/COVERAGE.md), `tools/c-api/` |
| `D9-0`..`140` | `plan_dx9.md`, 2026-10-28 | The Direct3D 9 renderer. Cited "DX9 D9-nn". | [`directx9-renderer.md`](directx9-renderer.md), [`d3d9-divergence-report.md`](d3d9-divergence-report.md) |
| `DEV-*`, `VIB-*`, `SENSORBASE-*`, `ACCEL-*`, `COMPASS-*`, `MOTION-*`, `ANDROID-BRIDGE-*`, `DEVICES-*` | `plan_devices.md`, 2026-10-28 | XNA devices and sensors. Cited "the devices plan". | [`devices-hardware-checklist.md`](devices-hardware-checklist.md) |
| `DEVICES-CNA-000`..`012` | `plan_cna_devices.md`, 2026-10-28 | The `CNA::Devices` extensions. | [`devices-ext.md`](devices-ext.md) |
| `DIAG-0001` | `plan_diagnostics.md`, 2026-10-28 | The diagnostics and profiler module. | [`diagnostics.md`](diagnostics.md) |
| `EASYGL-PARITY-1`..`10` | `plan_sdlgpu.md`, 2026-10-29 | EasyGL defects SDLGPU's parity oracle found. | `-1` in handoff §5.1; the others are fixed |
| `FNA3D-0`..`63` | `plan_fna3d.md`, 2026-10-29 | The FNA3D renderer. | [`fna3d-renderer.md`](fna3d-renderer.md), [`fna3d-parity-report.md`](fna3d-parity-report.md) |
| `FULLSCREEN-001`, `-002` | `plan_fullscreen_regressions.md`, `plan_directx9_fullscreen_motion.md`, 2026-10-28 | Fullscreen presentation and DirectX 9 loss/reset recovery found in Mobile Eggbert. | `directx9_frame_recovery_test.cpp`, `directx9_resize_state_test.cpp`, `spritebatch_presentation_test.cpp` |
| `FX-001`..`147` | `plan_fx.md`, 2026-10-29 | Compiled XNA effect bytecode. | [`fx-compiled-effects.md`](fx-compiled-effects.md), [`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md) |
| `GFX-220`..`246`, `BUILD-017`/`018` | `plan_postaudit.md`, 2026-10-28 | Post-audit graphics remediation. | [`renderer-capability-profiles.md`](renderer-capability-profiles.md) |
| `GLB-1`..`43` | `plan_glbackends.md`, 2026-10-29 | EasyGL as one internal family behind the public GL identities. | `GLB-42`/`43` in handoff §5.2; [`webgl2-renderer.md`](webgl2-renderer.md) |
| `GLTF-001`..`480` | `plan_gltf.md`, 2026-10-29 | glTF 2.0 correctness. | [`gltf-conformance.md`](gltf-conformance.md) §5.6 and §8, [`gltf-limitations.md`](gltf-limitations.md) |
| graphics Task N | `plan_graphics.md`, 2026-10-26 | The original graphics API backlog, Tasks 1-1120 in Phases 1-83. Cited "graphics Task N". | Git history only |
| `GS-001`..`011`, `GSP-*`, `GSX-*`, `GSH-*`, `GS-009g` | `plan_gamer_services_server.md`, `plan_gamer_services_avatar_polish.md`, `plan_gamer_services_xbox_fidelity.md`, `plan_gamer_services_final_hardening.md`, `plan_avatar_coordinate_space.md`, `gamer_services_server_initial_inventory.md`, `gamer_services_acceptance_sources.md` and `gamer_services_server_final_register.md`, 2026-10-28 | The GamerServices service, protocol, relay, matchmaking and Avatar API, and the XNA -Z avatar space. | [`gamer-services-known-limitations.md`](gamer-services-known-limitations.md), [`gamer-services-server.md`](gamer-services-server.md), [`avatars.md`](avatars.md) |
| `GS-009h` | `plan_guide_render_states.md`, 2026-10-29 | The Guide overlay leaking SpriteBatch state into the title's next frame. | `GuideUi::draw`'s doc comment, `GuideRenderStateTests.cpp` |
| `GSC-0001`..`0010` | `plan_graphics_shared_cleanup.md`, 2026-10-28 | Shared graphics rules (missing texture, two-sided stencil, back-buffer depth, PBR basis guards). Cited "graphics shared cleanup GSC-NNNN". | The header of each test that pins one |
| `GTI-0001`..`0010` | `plan_gpu_test_isolation.md`, 2026-10-28 | Keeping tests off the live desktop. Cited "GPU test isolation GTI-NNNN". | `../cmake/TestDisplayPolicy.cmake`, `../tools/platform/run_gpu_tests_private.sh`, `../tools/platform/profile_dead_tests.py` |
| `HEADLESS-1`..`64` | `plan_headless.md`, 2026-10-29 | The `HEADLESS` renderer. | [`headless-renderer.md`](headless-renderer.md) |
| `INPUT-EMU-001`/`002` | `plan_keyboard_device_emulation.md`, 2026-10-28 | Keyboard-driven accelerometer, orientation and GamePad emulation. | [`keyboard-device-emulation.md`](keyboard-device-emulation.md) |
| `INSP-0001` | `plan_inspector.md`, 2026-10-29 | The optional CNA Inspector. | [`inspector.md`](inspector.md), [`inspector-benchmark.md`](inspector-benchmark.md) |
| `MEDIA-1`..`234` | `plan_media.md`, 2026-10-29 | The Media port. Decisions are cited "media decision Dn". | [`video-backend.md`](video-backend.md), the Media rows of [`../CHECKLIST.md`](../CHECKLIST.md) |
| `MOD-*` | `plan_modern.md`, 2026-10-11 | The former `CNA::Graphics` modern engine layer, removed in the 2026-09 scope reduction. | [`cnaext-engine-layer.md`](cnaext-engine-layer.md) |
| — | `MODULARIZATION_PLAN.md`, 2026-10-28 | The target, physical-source and module/package modularization, closed 2026-08-10. | [`physical-modules.md`](physical-modules.md) |
| `MRT-001`..`012` | `plan_modern_retirement.md`, 2026-10-26 | Removing the modern GPU layer (compute, storage, texture arrays, indirect draw, GPU timers). | [`c-api/ABI_VERSIONING.md`](c-api/ABI_VERSIONING.md) (0.47.0) |
| `MSR-001`..`040` | `plan_samples_multirenderer.md`, 2026-10-29 | Qualifying the native samples on every renderer. | `cna-samples/renderers.md` |
| Net Task N.N | `plan_net.md`, 2026-10-29 | The networking, GamerServices and Avatar re-audit. | [`gamer-services-known-limitations.md`](gamer-services-known-limitations.md) |
| `NPI-*`, `NPV-*`, `WAYLAND-*`, `WIN32-*`, `X11-*` | `plan_native_platforms_integration.md`, `plan_native_platform_validation.md`, `plan_wayland.md`, `plan_win32.md`, `plan_x11.md`, 2026-10-11 | CNA's own Win32, X11 and Wayland platforms and their validation; retired 2026-10-06 for SDL3's drivers. | [`platform-abstraction.md`](platform-abstraction.md) |
| `P0-001`..`P13-006` | `plan_input.md`, 2026-10-29 | The input audit and repair. | [`input-manual-verification-results.md`](input-manual-verification-results.md) and the other `input-*.md` |
| `PLAT-1`..`141` | `plan_platform.md`, 2026-10-29 | The platform abstraction. | [`platform-abstraction.md`](platform-abstraction.md) |
| `PSG-0001`..`0013` | `plan_pre_sdlgpu_closeout.md`, 2026-10-28 | The WebGPU merge and the bounded GoogleTest runner. Cited "pre-SDLGPU PSG-NNNN". | `../tools/tests/run_gtest_bounded.sh` |
| `RRC-001`..`029` | `plan_renderer_cleanup.md`, 2026-10-11 | The renderer curation: the 2026-09-17 retirement of 25 identities and the rounds after it. | [`removed-renderers.md`](removed-renderers.md) |
| `RRC-030`..`054` | `plan_renderer_retirement.md`, 2026-10-28 | The 2026-10-13 retirement of `VULKAN`, `DIRECTX11`, `METAL` and `SDL_RENDERER`. | [`removed-renderers.md`](removed-renderers.md), [`renderer-registry.md`](renderer-registry.md) |
| `RTR-P0`..`P14` | `plan_runtimerenderer.md`, 2026-10-29 | Runtime renderer selection. | [`runtime-renderer-selection.md`](runtime-renderer-selection.md) |
| `RTS-001`/`002` | `plan_cna_rts.md`, 2026-10-28 | The `cna-rts` consumer validation. | `cna-rts` `missing.md`; `rendertarget_effect_source_test.cpp` |
| `SAMPLE-*` | `plan_samples.md`, 2026-08-22 | The sample backlog. | `cna-samples/plan.md` |
| — | `plan_sdl3free.md`, 2026-10-26 | An SDL3-independent migration, superseded twice over. | [`platform-abstraction.md`](platform-abstraction.md) |
| `SDLGPU-1`..`135` | `plan_sdlgpu.md` with `sdlgpu_renderer_contract_audit.csv` and `sdlgpu_easygl_example_classification.csv`, 2026-10-29 | The SDLGPU renderer and its EasyGL parity. | [`sdl-gpu-renderer.md`](sdl-gpu-renderer.md) |
| `SMG-*`, `VMG-*`, `WMG-*`, `GL4-*` (modern) | `plan_sdlgpu_modern_graphics.md`, `plan_vulkan_modern_graphics.md`, `plan_webgpu_modern_graphics.md`, 2026-10-11; `plan_opengl4_modern_graphics.md`, 2026-09-28 | Putting the modern CNAEXT graphics layer on SDLGPU, Vulkan, WebGPU and OpenGL 4; the layer was retired on 2026-10-13. | Git history only |
| `SOFTWARE-1`..`485` | `plan_software.md`, 2026-10-29 | The Software CPU rasterizer and its EasyGL parity. | [`software-renderer.md`](software-renderer.md), [`software-easygl-parity-ledger.md`](software-easygl-parity-ledger.md) |
| `STREET-*`, `STREETS-*`, `STREETW-*`, `STREETGL4-*`, `STREETPERF-*` | `plan_street.md`, `plan_street_sdlgpu.md`, `plan_street_webgpu.md`, `plan_street_perf.md`, 2026-10-11; `plan_street_opengl4.md`, 2026-09-28 | Bringing the `cna-street` consumer up on EasyGL, Vulkan, SDLGPU, WebGPU and OpenGL 4, and its frame rate. | Git history only; the consumer is `cna-street` |
| `STUB-1`..`6` | `plan_stub.md`, 2026-10-28 | The no-op Stub renderer. | [`stub-renderer.md`](stub-renderer.md) |
| `TCR-1`..`8` | `plan_terminal_capi_repair.md`, 2026-10-29 | `SOFTWARE` presenting through the terminal, and the C API building again. | [`platform-terminal-analysis.md`](platform-terminal-analysis.md) |
| `THREEJS-*` | `plan_threejs.md`, 2026-10-11 | A Three.js renderer feasibility study; never a renderer identity. | Git history only |
| `VKPAR-0001`..`0029` | `plan_vulkan_parity.md`, 2026-10-26 | The Vulkan hardware-parity ledger. Cited "Vulkan parity VKPAR-NNNN". | `TwoSidedStencilTests.cpp`, `StockEffectNullTextureTests.cpp`, `../tools/xna-oracle/README.md` |
| `VSIDE-001`..`006` | `_misc/cnavs.md` audit, 2026-10-30; no plan | Native Windows Visual Studio/VS Code presets, MSVC/SDL ABI consistency, runtime deployment, automatic CI, IDE target folders and documentation. | [`windows-visual-studio.md`](windows-visual-studio.md), [`installed-cpp-sdk.md`](installed-cpp-sdk.md), `../CMakePresets.json`, `../.github/workflows/windows-msvc-ci.yml` |
| `WEBGPU-1`..`209` | `plan_webgpu.md`, 2026-10-29 | The WebGPU renderer. | [`webgpu-renderer.md`](webgpu-renderer.md) |
| `WEBGPUPERF-*`, `WGF-*` | `plan_webgpu_perf.md`, `plan_webgpu_failing_eight.md`, 2026-10-11 | WebGPU's per-draw CPU cost, and eight failing WebGPU ctests (fixed). | Git history only; [`webgpu-renderer.md`](webgpu-renderer.md) for the renderer |
| `WINPORT-*`, `WINCLOSE-*`, `WINNATIVE-*` | `plan_windows_portability.md`, `plan_windows_portability_closeout.md`, 2026-10-28; `plan_win32_native_validation.md`, 2026-10-11 | The Unicode filesystem path model on native Windows and its closing passes, run on a since-deleted Windows 10 VM. | [`filesystem-path-model.md`](filesystem-path-model.md) |
| `XACTBUILD-001` | `plan_xact_build_invocation.md`, 2026-10-29 | `BuildXact` invoking XactBld3 with the SDK's switches and Wine paths. | The `BuildXact.Execute()` row of [`xna-content-pipeline-parity-report.md`](xna-content-pipeline-parity-report.md) |
| `XNA-ENUM-001`, `XNA-MEMBER-001`/`002` | `plan_xna_runtime_surface.md`, 2026-10-26 | The runtime type and member census and its first fixes. | [`xna-4-enumerator-reference.md`](xna-4-enumerator-reference.md), [`xna-4-runtime-member-coverage-baseline.md`](xna-4-runtime-member-coverage-baseline.md) |
| `XNA-MISSING-001`..`020` | `plan_missing.md`, 2026-10-26 | The 160 runtime members that completed the XNA surface. | [`xna-4-runtime-member-coverage.md`](xna-4-runtime-member-coverage.md) |
| `XNAP-*` | `plan_xnapipeline.md`, 2026-10-29 | CNA's native XNB writer. | [`xnb-interoperability.md`](xnb-interoperability.md) |
| `XNAPP-*` | `plan_xnapipeline_parity.md`, 2026-10-29 | XNA 4.0 Content Pipeline API and input-format parity. | The generated `xna-content-pipeline-*.md`; its source rules in [`xnb-interoperability.md`](xnb-interoperability.md) §9.1 |
| `XNASWEEP-*` | `plan_xna_sample_xnb_sweep.md`, 2026-10-29 | Rebuilding every genuine sample `.xnb` through CNA and comparing. | [`../tools/xna-sample-sweep/README.md`](../tools/xna-sample-sweep/README.md); its residues in handoff §5.2 |
| `XNB-*` | `plan_xnb.md`, 2026-10-29 | The runtime `.xnb` reader. `XNB-61b` closed the compressed-manifest inventory on 2026-10-31; Phase I, `XNB-61`..`64`, was never started. | [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md) |
| Retired renderers: `BLEND2D-*`, `CANVAS-*`, `D2D-*`, `D3D10-*`/`DX10-*`, `DILIGENT-*`, `DX-*`, `DX1-*`..`DX8-*`, `DX30-*`, `DX12-*`, `GDI-*`, `GL4-*`, `GLIDE-*`, `HTMLDOM-*`, `IGL-*`, `LLGL-*`, `MAGNUM-*`, `METAL-*`, `NVG-*`, `OPENGLES1-*`, `OPENGLES2-*`, `PIXIJS-*`, `RLGL-*`, `SKIA-*`, `SOKOL-*`, `SVGDOM-*`, `TINYGL-*`, `VULKAN-*`, `WICKED-*`, `WIN11-*` | One plan per retired renderer: `plan_blend2d`, `plan_canvas`, `plan_direct2d`, `plan_d3d10`, `plan_diligent`, `plan_dx` (Direct3D 11/12), `plan_dx1`, `plan_dx2`, `plan_dx3`, `plan_dx5`, `plan_dx6`, `plan_dx7`, `plan_dx8`, `plan_dxold`, `plan_freedirect`, `plan_directx12_parity`, `plan_gdi`, `plan_opengl1`, `plan_opengl2`, `plan_opengl4`, `plan_glide`, `plan_html_dom`, `plan_igl`, `plan_llgl`, `plan_magnum`, `plan_metal`, `plan_nanovg`, `plan_opengles1`, `plan_opengles2`, `plan_pixijs`, `plan_rlgl`, `plan_skia`, `plan_sokol`, `plan_svg_dom`, `plan_tinygl`, `plan_vulkan` (with `baselines/easygl-ctests-2026-09-06.txt`), `plan_wicked`, `plan_windows11_directx`; deleted 2026-08-30..10-14 | The renderers CNA built and later retired, and the physical Windows 11 bring-up of the retired DirectX renderers. | [`removed-renderers.md`](removed-renderers.md) |
