# Documentation status in `docs/`

Inventory: 2026-11-01. Scope: all 177 currently tracked `docs/**/*.md` files.
Summary: 168 reviewed, 7 updated incidentally and awaiting their own audit, and 2 not yet
independently reviewed.

This file tracks the current documentation cleanup. A pending status does not imply that a
document is incorrect; it means that it has not yet had an independent factual review against
current source code, tests and `handoff.md` during the 2026-10-31 to 2026-11-01 pass.
Keep this inventory and future updates in English.

## Status definitions

- ✅ **REVIEWED** — independently checked/reconciled and revised where needed.
- 🔵 **IN PROGRESS** — an independent audit is underway; do not select it again as the next task.
- 🟡 **PENDING — updated incidentally** — changed during another task, but still awaiting its own
  complete audit. The number of commits alone does not establish completion.
- ⬜ **PENDING** — not yet independently reviewed during this pass.

Before starting the next cleanup task, update the selected file's status here. Mark it ✅ when
the work is complete, verified and committed. This prevents accidental duplicate work.

## Were any documents reviewed more than once?

Yes. Two pages underwent two separate reconciliations over these two days:

| Document | 2026-10-31 | 2026-11-01 | Conclusion |
|---|---|---|---|
| `docs/basiceffect-support.md` | factual corrections in Tasks 885/886 | full rewrite from a Phase 42 log into a current contract | partially duplicated; the second pass was deeper and is now complete |
| `docs/alphatesteffect-support.md` | correction of fog/null-texture history | full rewrite from a Phase 43 log into a current contract | partially duplicated; the second pass was deeper and is now complete |

Other repeated edits did not repeat the same work:

- `docs/README.md` was changed 28 times as a running index; its own complete audit is still pending.
- `docs/graphics-renderer-feature-matrix.md` was changed seven times: once in its own audit and
  subsequently during related renderer/capability corrections.
- `docs/task-id-index.md` was changed six times as a routing registry, not in six separate audits.
- `docs/webgpu-renderer.md` was changed five times for different subsystems before its own audit;
  the sixth pass on 2026-11-01 rewrote it as a current contract and completed the review.
- `docs/migration-guide.md` was changed four times; the first three were related updates and the
  last commit was its complete audit.
- `docs/rasterizerstate-support.md` was changed four times for retirement of the old culling audit,
  graphics-state corrections, its own reconciliation and a subsequent OPENGL33 correction.
- `docs/depthstencilstate-support.md` and `docs/sampler-state-support.md` were each changed in two
  distinct related steps; both reviews are complete.

Removed historical files (`graphics-ext-scope-reduction.md`,
`xna_culling_compatibility_audit.md`, `xna_depth_occlusion_compatibility_audit.md`,
`graphicsdevice-fna-audit.md`, `graphicsresource-fna-audit.md`, `fx-bytecode-support-plan.md`,
`webgpu-design-notes.md` and `c-api/GENERATION_AUDIT.md`) are no longer part of the current inventory.

## `docs/`

- 🟡 **PENDING — updated incidentally** — [`docs/README.md`](docs/README.md)
- ✅ **REVIEWED** — [`docs/alphatesteffect-support.md`](docs/alphatesteffect-support.md)
- ✅ **REVIEWED** — [`docs/android-graphics-limitations.md`](docs/android-graphics-limitations.md)
- ✅ **REVIEWED** — [`docs/apple-platforms.md`](docs/apple-platforms.md)
- ✅ **REVIEWED** — [`docs/ascii-post-process-effect.md`](docs/ascii-post-process-effect.md)
- ✅ **REVIEWED** — [`docs/audio-alsa.md`](docs/audio-alsa.md)
- ✅ **REVIEWED** — [`docs/avatar-art-direction.md`](docs/avatar-art-direction.md)
- ✅ **REVIEWED** — [`docs/avatar-demos.md`](docs/avatar-demos.md)
- ✅ **REVIEWED** — [`docs/avatars.md`](docs/avatars.md)
- ✅ **REVIEWED** — [`docs/basiceffect-support.md`](docs/basiceffect-support.md)
- ✅ **REVIEWED** — [`docs/binding.md`](docs/binding.md)
- ✅ **REVIEWED** — [`docs/browser-network-readiness.md`](docs/browser-network-readiness.md)
- ✅ **REVIEWED** — [`docs/build-performance.md`](docs/build-performance.md)
- ✅ **REVIEWED** — [`docs/cna-devices-camera-design.md`](docs/cna-devices-camera-design.md)
- ✅ **REVIEWED** — [`docs/cnaext-engine-layer.md`](docs/cnaext-engine-layer.md)
- ✅ **REVIEWED** — [`docs/cnb-compression-measurements.md`](docs/cnb-compression-measurements.md)
- ✅ **REVIEWED** — [`docs/cnb-format.md`](docs/cnb-format.md)
- ✅ **REVIEWED** — [`docs/cnb-mmap-measurements.md`](docs/cnb-mmap-measurements.md)
- ✅ **REVIEWED** — [`docs/cnj-format.md`](docs/cnj-format.md)
- ✅ **REVIEWED** — [`docs/consoles.md`](docs/consoles.md)
- ✅ **REVIEWED** — [`docs/content-pipeline-benchmark.md`](docs/content-pipeline-benchmark.md)
- ✅ **REVIEWED** — [`docs/content-pipeline.md`](docs/content-pipeline.md)
- ✅ **REVIEWED** — [`docs/cross-renderer-parity-fixtures.md`](docs/cross-renderer-parity-fixtures.md)
- ✅ **REVIEWED** — [`docs/d3d9-divergence-report.md`](docs/d3d9-divergence-report.md)
- ✅ **REVIEWED** — [`docs/demo-input-checklist.md`](docs/demo-input-checklist.md)
- ✅ **REVIEWED** — [`docs/depthstencilstate-support.md`](docs/depthstencilstate-support.md)
- ✅ **REVIEWED** — [`docs/development-environment.md`](docs/development-environment.md)
- ✅ **REVIEWED** — [`docs/devices-android.md`](docs/devices-android.md)
- ✅ **REVIEWED** — [`docs/devices-api-coverage.md`](docs/devices-api-coverage.md)
- ✅ **REVIEWED** — [`docs/devices-build.md`](docs/devices-build.md)
- ✅ **REVIEWED** — [`docs/devices-event-contract.md`](docs/devices-event-contract.md)
- ✅ **REVIEWED** — [`docs/devices-ext.md`](docs/devices-ext.md)
- ✅ **REVIEWED** — [`docs/devices-hardware-checklist.md`](docs/devices-hardware-checklist.md)
- ✅ **REVIEWED** — [`docs/devices-native-backend-design.md`](docs/devices-native-backend-design.md)
- ✅ **REVIEWED** — [`docs/devices-thread-safety.md`](docs/devices-thread-safety.md)
- ✅ **REVIEWED** — [`docs/devices_sensor_hardware_qa_template.md`](docs/devices_sensor_hardware_qa_template.md)
- ✅ **REVIEWED** — [`docs/diagnostics-benchmark.md`](docs/diagnostics-benchmark.md)
- ✅ **REVIEWED** — [`docs/diagnostics.md`](docs/diagnostics.md)
- ✅ **REVIEWED** — [`docs/directx9-renderer.md`](docs/directx9-renderer.md)
- ✅ **REVIEWED** — [`docs/dualtextureeffect-support.md`](docs/dualtextureeffect-support.md)
- ✅ **REVIEWED** — [`docs/emscripten-mainloop-game-lifetime.md`](docs/emscripten-mainloop-game-lifetime.md)
- ✅ **REVIEWED** — [`docs/environmentmapeffect-support.md`](docs/environmentmapeffect-support.md)
- ✅ **REVIEWED** — [`docs/filesystem-path-model.md`](docs/filesystem-path-model.md)
- ✅ **REVIEWED** — [`docs/fna-reference-harness.md`](docs/fna-reference-harness.md)
- ✅ **REVIEWED** — [`docs/fna3d-parity-report.md`](docs/fna3d-parity-report.md)
- ✅ **REVIEWED** — [`docs/fna3d-renderer.md`](docs/fna3d-renderer.md)
- ✅ **REVIEWED** — [`docs/framework-design.md`](docs/framework-design.md)
- ✅ **REVIEWED** — [`docs/fx-bytecode-fuzzing.md`](docs/fx-bytecode-fuzzing.md)
- ✅ **REVIEWED** — [`docs/fx-compiled-effects.md`](docs/fx-compiled-effects.md)
- ✅ **REVIEWED** — [`docs/gamer-services-known-limitations.md`](docs/gamer-services-known-limitations.md)
- ✅ **REVIEWED** — [`docs/gamer-services-manual-qualification.md`](docs/gamer-services-manual-qualification.md)
- ✅ **REVIEWED** — [`docs/gamer-services-server.md`](docs/gamer-services-server.md)
- ✅ **REVIEWED** — [`docs/gdm-coverage.md`](docs/gdm-coverage.md)
- ✅ **REVIEWED** — [`docs/gltf-api-change-review.md`](docs/gltf-api-change-review.md)
- ✅ **REVIEWED** — [`docs/gltf-campaign-retrospective.md`](docs/gltf-campaign-retrospective.md)
- ✅ **REVIEWED** — [`docs/gltf-center-collapse-verdict.md`](docs/gltf-center-collapse-verdict.md)
- ✅ **REVIEWED** — [`docs/gltf-conformance.md`](docs/gltf-conformance.md)
- ✅ **REVIEWED** — [`docs/gltf-conventions.md`](docs/gltf-conventions.md)
- ✅ **REVIEWED** — [`docs/gltf-limitations.md`](docs/gltf-limitations.md)
- ✅ **REVIEWED** — [`docs/gltf-performance.md`](docs/gltf-performance.md)
- ✅ **REVIEWED** — [`docs/gltf-renderer-pbr-fallbacks.md`](docs/gltf-renderer-pbr-fallbacks.md)
- ✅ **REVIEWED** — [`docs/gltf-renderer-stride-conformance.md`](docs/gltf-renderer-stride-conformance.md)
- ✅ **REVIEWED** — [`docs/graphics-renderer-feature-matrix.md`](docs/graphics-renderer-feature-matrix.md)
- ✅ **REVIEWED** — [`docs/graphics-resource-lifetime.md`](docs/graphics-resource-lifetime.md)
- ✅ **REVIEWED** — [`docs/headless-renderer.md`](docs/headless-renderer.md)
- ✅ **REVIEWED** — [`docs/ieffect-renderer-audit.md`](docs/ieffect-renderer-audit.md)
- ✅ **REVIEWED** — [`docs/input-backend.md`](docs/input-backend.md)
- ✅ **REVIEWED** — [`docs/input-build-and-test.md`](docs/input-build-and-test.md)
- ✅ **REVIEWED** — [`docs/input-fna-fidelity.md`](docs/input-fna-fidelity.md)
- ✅ **REVIEWED** — [`docs/input-manual-verification-results.md`](docs/input-manual-verification-results.md)
- ✅ **REVIEWED** — [`docs/input-member-parity-matrix.md`](docs/input-member-parity-matrix.md)
- ✅ **REVIEWED** — [`docs/input-pre-merge-checklist.md`](docs/input-pre-merge-checklist.md)
- ✅ **REVIEWED** — [`docs/input-public-api-frozen.md`](docs/input-public-api-frozen.md)
- ✅ **REVIEWED** — [`docs/input-test-coverage.md`](docs/input-test-coverage.md)
- ✅ **REVIEWED** — [`docs/inspector-benchmark.md`](docs/inspector-benchmark.md)
- ✅ **REVIEWED** — [`docs/inspector.md`](docs/inspector.md)
- ✅ **REVIEWED** — [`docs/installed-cpp-sdk.md`](docs/installed-cpp-sdk.md)
- ✅ **REVIEWED** — [`docs/keyboard-device-emulation.md`](docs/keyboard-device-emulation.md)
- ✅ **REVIEWED** — [`docs/link-size-architecture.md`](docs/link-size-architecture.md)
- ✅ **REVIEWED** — [`docs/location-future-plan.md`](docs/location-future-plan.md)
- ✅ **REVIEWED** — [`docs/location-proposal.md`](docs/location-proposal.md)
- ✅ **REVIEWED** — [`docs/maintainer-study-guide.md`](docs/maintainer-study-guide.md)
- ✅ **REVIEWED** — [`docs/migration-guide.md`](docs/migration-guide.md)
- ✅ **REVIEWED** — [`docs/model-content-pipeline-support.md`](docs/model-content-pipeline-support.md)
- ✅ **REVIEWED** — [`docs/occlusionquery-support.md`](docs/occlusionquery-support.md)
- ✅ **REVIEWED** — [`docs/opengl33-renderer.md`](docs/opengl33-renderer.md)
- ✅ **REVIEWED** — [`docs/physical-modules.md`](docs/physical-modules.md)
- ✅ **REVIEWED** — [`docs/platform-abstraction.md`](docs/platform-abstraction.md)
- ✅ **REVIEWED** — [`docs/platform-entrypoint-audit.md`](docs/platform-entrypoint-audit.md)
- ✅ **REVIEWED** — [`docs/platform-input-notes.md`](docs/platform-input-notes.md)
- ✅ **REVIEWED** — [`docs/platform-input-seam-mapping.md`](docs/platform-input-seam-mapping.md)
- ✅ **REVIEWED** — [`docs/platform-performance.md`](docs/platform-performance.md)
- ✅ **REVIEWED** — [`docs/platform-renderer-boundary.md`](docs/platform-renderer-boundary.md)
- ✅ **REVIEWED** — [`docs/platform-renderer-sdl-audit.md`](docs/platform-renderer-sdl-audit.md)
- ✅ **REVIEWED** — [`docs/platform-sdl-lifecycle-audit.md`](docs/platform-sdl-lifecycle-audit.md)
- ✅ **REVIEWED** — [`docs/platform-terminal-analysis.md`](docs/platform-terminal-analysis.md)
- ✅ **REVIEWED** — [`docs/rasterizerstate-support.md`](docs/rasterizerstate-support.md)
- ✅ **REVIEWED** — [`docs/releasing.md`](docs/releasing.md)
- ✅ **REVIEWED** — [`docs/removed-renderers.md`](docs/removed-renderers.md)
- ✅ **REVIEWED** — [`docs/renderer-capability-profiles.md`](docs/renderer-capability-profiles.md)
- ✅ **REVIEWED** — [`docs/renderer-registry.md`](docs/renderer-registry.md)
- ✅ **REVIEWED** — [`docs/rendertarget-support.md`](docs/rendertarget-support.md)
- ✅ **REVIEWED** — [`docs/runtime-renderer-selection.md`](docs/runtime-renderer-selection.md)
- ✅ **REVIEWED** — [`docs/sampler-state-support.md`](docs/sampler-state-support.md)
- 🟡 **PENDING — updated incidentally** — [`docs/sdl-gpu-renderer.md`](docs/sdl-gpu-renderer.md)
- ✅ **REVIEWED** — [`docs/sdlgpu-shader-effect-layout.md`](docs/sdlgpu-shader-effect-layout.md)
- ✅ **REVIEWED** — [`docs/shader-effect-vs-fx-bytecode.md`](docs/shader-effect-vs-fx-bytecode.md)
- ✅ **REVIEWED** — [`docs/skinned-model-ext.md`](docs/skinned-model-ext.md)
- ✅ **REVIEWED** — [`docs/skinnedeffect-support.md`](docs/skinnedeffect-support.md)
- 🟡 **PENDING — updated incidentally** — [`docs/software-easygl-parity-ledger.md`](docs/software-easygl-parity-ledger.md)
- ✅ **REVIEWED** — [`docs/software-renderer.md`](docs/software-renderer.md)
- ✅ **REVIEWED** — [`docs/spritefont-support.md`](docs/spritefont-support.md)
- ✅ **REVIEWED** — [`docs/stub-renderer.md`](docs/stub-renderer.md)
- ✅ **REVIEWED** — [`docs/surface-format-support.md`](docs/surface-format-support.md)
- 🟡 **PENDING — updated incidentally** — [`docs/task-id-index.md`](docs/task-id-index.md)
- ✅ **REVIEWED** — [`docs/texture-stream-formats.md`](docs/texture-stream-formats.md)
- ✅ **REVIEWED** — [`docs/texture3d-texturecube-support.md`](docs/texture3d-texturecube-support.md)
- ✅ **REVIEWED** — [`docs/vertex-format-support.md`](docs/vertex-format-support.md)
- ✅ **REVIEWED** — [`docs/video-backend.md`](docs/video-backend.md)
- ✅ **REVIEWED** — [`docs/viewport-displaymode-adapter-support.md`](docs/viewport-displaymode-adapter-support.md)
- ✅ **REVIEWED** — [`docs/web-emscripten-graphics-limitations.md`](docs/web-emscripten-graphics-limitations.md)
- ✅ **REVIEWED** — [`docs/webgl2-renderer.md`](docs/webgl2-renderer.md)
- ✅ **REVIEWED** — [`docs/webgpu-renderer.md`](docs/webgpu-renderer.md)
- ✅ **REVIEWED** — [`docs/windows-visual-studio.md`](docs/windows-visual-studio.md)
- ✅ **REVIEWED** — [`docs/xma-encoder-backend.md`](docs/xma-encoder-backend.md)
- ⬜ **PENDING** — [`docs/xna-4-enumerator-reference.md`](docs/xna-4-enumerator-reference.md)
- ✅ **REVIEWED** — [`docs/xna-4-runtime-member-coverage-baseline.md`](docs/xna-4-runtime-member-coverage-baseline.md)
- ⬜ **PENDING** — [`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
- ✅ **REVIEWED** — [`docs/xna-content-pipeline-compat-api.md`](docs/xna-content-pipeline-compat-api.md)
- ✅ **REVIEWED** — [`docs/xna-content-pipeline-components.md`](docs/xna-content-pipeline-components.md)
- ✅ **REVIEWED** — [`docs/xna-content-pipeline-final-audit.md`](docs/xna-content-pipeline-final-audit.md)
- ✅ **REVIEWED** — [`docs/xna-content-pipeline-media.md`](docs/xna-content-pipeline-media.md)
- ✅ **REVIEWED** — [`docs/xna-content-pipeline-migration.md`](docs/xna-content-pipeline-migration.md)
- ✅ **REVIEWED** — [`docs/xna-content-pipeline-parity-report.md`](docs/xna-content-pipeline-parity-report.md)
- 🟡 **PENDING — updated incidentally** — [`docs/xna-fna-behavior-decisions.md`](docs/xna-fna-behavior-decisions.md)
- ✅ **REVIEWED** — [`docs/xna-intermediate-xml-format.md`](docs/xna-intermediate-xml-format.md)
- ✅ **REVIEWED** — [`docs/xna4-runtime-compatibility.md`](docs/xna4-runtime-compatibility.md)
- 🟡 **PENDING — updated incidentally** — [`docs/xnb-content-pipeline-support.md`](docs/xnb-content-pipeline-support.md)
- 🟡 **PENDING — updated incidentally** — [`docs/xnb-interoperability.md`](docs/xnb-interoperability.md)

## `docs/c-api/`

- ✅ **REVIEWED** — [`docs/c-api/ABI_VERSIONING.md`](docs/c-api/ABI_VERSIONING.md)
- ✅ **REVIEWED** — [`docs/c-api/AUDIO.md`](docs/c-api/AUDIO.md)
- ✅ **REVIEWED** — [`docs/c-api/CALLBACKS_AND_THREADING.md`](docs/c-api/CALLBACKS_AND_THREADING.md)
- ✅ **REVIEWED** — [`docs/c-api/CNB.md`](docs/c-api/CNB.md)
- ✅ **REVIEWED** — [`docs/c-api/COMPATIBILITY.md`](docs/c-api/COMPATIBILITY.md)
- ✅ **REVIEWED** — [`docs/c-api/CONSUMING.md`](docs/c-api/CONSUMING.md)
- ✅ **REVIEWED** — [`docs/c-api/CONTENT.md`](docs/c-api/CONTENT.md)
- ✅ **REVIEWED** — [`docs/c-api/CORE.md`](docs/c-api/CORE.md)
- ✅ **REVIEWED** — [`docs/c-api/COVERAGE.md`](docs/c-api/COVERAGE.md)
- ✅ **REVIEWED** — [`docs/c-api/DEVICES.md`](docs/c-api/DEVICES.md)
- ✅ **REVIEWED** — [`docs/c-api/EFFECTS.md`](docs/c-api/EFFECTS.md)
- ✅ **REVIEWED** — [`docs/c-api/ERRORS.md`](docs/c-api/ERRORS.md)
- ✅ **REVIEWED** — [`docs/c-api/FUZZING.md`](docs/c-api/FUZZING.md)
- ✅ **REVIEWED** — [`docs/c-api/GAMER_SERVICES.md`](docs/c-api/GAMER_SERVICES.md)
- ✅ **REVIEWED** — [`docs/c-api/GAME_COMPONENTS.md`](docs/c-api/GAME_COMPONENTS.md)
- ✅ **REVIEWED** — [`docs/c-api/GENERATED_ROUTES.md`](docs/c-api/GENERATED_ROUTES.md)
- ✅ **REVIEWED** — [`docs/c-api/GRAPHICS_DEVICE.md`](docs/c-api/GRAPHICS_DEVICE.md)
- ✅ **REVIEWED** — [`docs/c-api/GRAPHICS_EXT.md`](docs/c-api/GRAPHICS_EXT.md)
- ✅ **REVIEWED** — [`docs/c-api/GRAPHICS_RESOURCES.md`](docs/c-api/GRAPHICS_RESOURCES.md)
- ✅ **REVIEWED** — [`docs/c-api/HANDLES.md`](docs/c-api/HANDLES.md)
- ✅ **REVIEWED** — [`docs/c-api/INDEX_BUFFERS.md`](docs/c-api/INDEX_BUFFERS.md)
- ✅ **REVIEWED** — [`docs/c-api/INPUT_SNAPSHOTS.md`](docs/c-api/INPUT_SNAPSHOTS.md)
- ✅ **REVIEWED** — [`docs/c-api/LIMITATIONS.md`](docs/c-api/LIMITATIONS.md)
- ✅ **REVIEWED** — [`docs/c-api/MATH_AND_3D_VALUES.md`](docs/c-api/MATH_AND_3D_VALUES.md)
- ✅ **REVIEWED** — [`docs/c-api/MEDIA.md`](docs/c-api/MEDIA.md)
- ✅ **REVIEWED** — [`docs/c-api/MODELS.md`](docs/c-api/MODELS.md)
- ✅ **REVIEWED** — [`docs/c-api/NET.md`](docs/c-api/NET.md)
- ✅ **REVIEWED** — [`docs/c-api/OWNERSHIP.md`](docs/c-api/OWNERSHIP.md)
- ✅ **REVIEWED** — [`docs/c-api/README.md`](docs/c-api/README.md)
- ✅ **REVIEWED** — [`docs/c-api/RELEASE_GATE.md`](docs/c-api/RELEASE_GATE.md)
- ✅ **REVIEWED** — [`docs/c-api/RENDERERS_AND_CAPABILITIES.md`](docs/c-api/RENDERERS_AND_CAPABILITIES.md)
- ✅ **REVIEWED** — [`docs/c-api/SHARP_RUNTIME_BOUNDARY.md`](docs/c-api/SHARP_RUNTIME_BOUNDARY.md)
- ✅ **REVIEWED** — [`docs/c-api/STORAGE.md`](docs/c-api/STORAGE.md)
- ✅ **REVIEWED** — [`docs/c-api/STRINGS_AND_BUFFERS.md`](docs/c-api/STRINGS_AND_BUFFERS.md)
- ✅ **REVIEWED** — [`docs/c-api/TEXTURES.md`](docs/c-api/TEXTURES.md)
- ✅ **REVIEWED** — [`docs/c-api/TEXTURE_VOLUMES.md`](docs/c-api/TEXTURE_VOLUMES.md)
- ✅ **REVIEWED** — [`docs/c-api/VERTEX_BUFFERS.md`](docs/c-api/VERTEX_BUFFERS.md)
- ✅ **REVIEWED** — [`docs/c-api/WASM_ARTIFACT.md`](docs/c-api/WASM_ARTIFACT.md)
