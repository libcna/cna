# Stav dokumentace v `docs/`

Inventura: 2026-11-01. Rozsah: všech 176 aktuálně verzovaných souborů `docs/**/*.md`.
Souhrn: 77 zpracovaných, 14 podpůrně aktualizovaných čekajících na vlastní audit a 85 dosud
samostatně nezpracovaných.

Tento soubor sleduje současný dokumentační úklid. Stav neříká, že čekající dokument je nutně
chybný; říká pouze, že v průchodu z 2026-10-31 až 2026-11-01 ještě neproběhla jeho samostatná
věcná kontrola proti aktuálnímu zdrojovému kódu, testům a `handoff.md`.

## Význam stavů

- ✅ **ZPRACOVÁNO** — dokument prošel samostatnou kontrolou/reconciliací a případnou úpravou.
- 🔵 **PROBÍHÁ** — právě probíhá samostatný audit; nevybírat znovu jako další úlohu.
- 🟡 **ČEKÁ — podpůrně aktualizováno** — při jiné úloze byl změněn, ale ještě neprošel vlastním
  úplným auditem. Samotný počet commitů proto neznamená dokončení.
- ⬜ **ČEKÁ** — v tomto průchodu zatím nebyl samostatně zpracován.

Při dalším úklidu se před zahájením práce nejprve změní stav vybraného souboru zde. Po dokončení,
ověření a commitu se označí jako ✅. Tím se má zabránit nechtěnému opakování práce.

## Byly některé dokumenty zpracovány opakovaně?

Ano. Dvě stránky byly během těchto dvou dnů skutečně předmětem dvou samostatných reconciliací:

| Dokument | 2026-10-31 | 2026-11-01 | Závěr |
|---|---|---|---|
| `docs/basiceffect-support.md` | oprava faktických rozporů Tasks 885/886 | úplný přepis z Phase 42 deníku na aktuální kontrakt | částečně duplicitní, druhý průchod byl hlubší a nyní je konečný |
| `docs/alphatesteffect-support.md` | oprava fog/null-texture historie | úplný přepis z Phase 43 deníku na aktuální kontrakt | částečně duplicitní, druhý průchod byl hlubší a nyní je konečný |

Další vícenásobné změny nebyly stejnou prací dvakrát:

- `docs/README.md` bylo změněno 28× jako průběžný index; ještě neproběhl jeho vlastní úplný audit.
- `docs/graphics-renderer-feature-matrix.md` bylo změněno 7×: jednou vlastním auditem a dále při
  navazujících opravách jednotlivých rendererů a schopností.
- `docs/task-id-index.md` bylo změněno 6× jako směrovací registr, ne šesti samostatnými audity.
- `docs/webgpu-renderer.md` bylo před vlastním auditem změněno 5× kvůli různým subsystémům;
  samostatný šestý průchod 2026-11-01 jej přepsal na aktuální kontrakt a je nyní dokončený.
- `docs/migration-guide.md` bylo změněno 4×; první tři změny byly návazné opravy a poslední commit
  byl úplný audit.
- `docs/rasterizerstate-support.md` bylo změněno 4× kvůli odstranění starého culling auditu,
  opravám graphics-state, vlastní reconciliaci a následné OPENGL33 korekci.
- `docs/depthstencilstate-support.md` a `docs/sampler-state-support.md` byly změněny po dvou
  různých návazných krocích; jejich stav je dokončený.

Odstraněné historické soubory (`graphics-ext-scope-reduction.md`,
`xna_culling_compatibility_audit.md`, `xna_depth_occlusion_compatibility_audit.md`,
`graphicsdevice-fna-audit.md`, `graphicsresource-fna-audit.md`, `fx-bytecode-support-plan.md`,
`webgpu-design-notes.md` a `c-api/GENERATION_AUDIT.md`) už nejsou součástí aktuálního inventáře.

## `docs/`

- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/README.md`](docs/README.md)
- ✅ **ZPRACOVÁNO** — [`docs/alphatesteffect-support.md`](docs/alphatesteffect-support.md)
- ⬜ **ČEKÁ** — [`docs/android-graphics-limitations.md`](docs/android-graphics-limitations.md)
- ⬜ **ČEKÁ** — [`docs/apple-platforms.md`](docs/apple-platforms.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/ascii-post-process-effect.md`](docs/ascii-post-process-effect.md)
- ⬜ **ČEKÁ** — [`docs/audio-alsa.md`](docs/audio-alsa.md)
- ⬜ **ČEKÁ** — [`docs/avatar-art-direction.md`](docs/avatar-art-direction.md)
- ⬜ **ČEKÁ** — [`docs/avatar-demos.md`](docs/avatar-demos.md)
- ⬜ **ČEKÁ** — [`docs/avatars.md`](docs/avatars.md)
- ✅ **ZPRACOVÁNO** — [`docs/basiceffect-support.md`](docs/basiceffect-support.md)
- ⬜ **ČEKÁ** — [`docs/binding.md`](docs/binding.md)
- ⬜ **ČEKÁ** — [`docs/browser-network-readiness.md`](docs/browser-network-readiness.md)
- ⬜ **ČEKÁ** — [`docs/build-performance.md`](docs/build-performance.md)
- ⬜ **ČEKÁ** — [`docs/cna-devices-camera-design.md`](docs/cna-devices-camera-design.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/cnaext-engine-layer.md`](docs/cnaext-engine-layer.md)
- ⬜ **ČEKÁ** — [`docs/cnb-compression-measurements.md`](docs/cnb-compression-measurements.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/cnb-format.md`](docs/cnb-format.md)
- ⬜ **ČEKÁ** — [`docs/cnb-mmap-measurements.md`](docs/cnb-mmap-measurements.md)
- ⬜ **ČEKÁ** — [`docs/cnj-format.md`](docs/cnj-format.md)
- ⬜ **ČEKÁ** — [`docs/consoles.md`](docs/consoles.md)
- ⬜ **ČEKÁ** — [`docs/content-pipeline-benchmark.md`](docs/content-pipeline-benchmark.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/content-pipeline.md`](docs/content-pipeline.md)
- ✅ **ZPRACOVÁNO** — [`docs/cross-renderer-parity-fixtures.md`](docs/cross-renderer-parity-fixtures.md)
- ✅ **ZPRACOVÁNO** — [`docs/d3d9-divergence-report.md`](docs/d3d9-divergence-report.md)
- ✅ **ZPRACOVÁNO** — [`docs/demo-input-checklist.md`](docs/demo-input-checklist.md)
- ✅ **ZPRACOVÁNO** — [`docs/depthstencilstate-support.md`](docs/depthstencilstate-support.md)
- ⬜ **ČEKÁ** — [`docs/development-environment.md`](docs/development-environment.md)
- ✅ **ZPRACOVÁNO** — [`docs/devices-android.md`](docs/devices-android.md)
- ⬜ **ČEKÁ** — [`docs/devices-api-coverage.md`](docs/devices-api-coverage.md)
- ✅ **ZPRACOVÁNO** — [`docs/devices-build.md`](docs/devices-build.md)
- ⬜ **ČEKÁ** — [`docs/devices-event-contract.md`](docs/devices-event-contract.md)
- ⬜ **ČEKÁ** — [`docs/devices-ext.md`](docs/devices-ext.md)
- ✅ **ZPRACOVÁNO** — [`docs/devices-hardware-checklist.md`](docs/devices-hardware-checklist.md)
- ✅ **ZPRACOVÁNO** — [`docs/devices-native-backend-design.md`](docs/devices-native-backend-design.md)
- ✅ **ZPRACOVÁNO** — [`docs/devices-thread-safety.md`](docs/devices-thread-safety.md)
- ⬜ **ČEKÁ** — [`docs/devices_sensor_hardware_qa_template.md`](docs/devices_sensor_hardware_qa_template.md)
- ⬜ **ČEKÁ** — [`docs/diagnostics-benchmark.md`](docs/diagnostics-benchmark.md)
- ⬜ **ČEKÁ** — [`docs/diagnostics.md`](docs/diagnostics.md)
- ✅ **ZPRACOVÁNO** — [`docs/directx9-renderer.md`](docs/directx9-renderer.md)
- ✅ **ZPRACOVÁNO** — [`docs/dualtextureeffect-support.md`](docs/dualtextureeffect-support.md)
- ⬜ **ČEKÁ** — [`docs/emscripten-mainloop-game-lifetime.md`](docs/emscripten-mainloop-game-lifetime.md)
- ✅ **ZPRACOVÁNO** — [`docs/environmentmapeffect-support.md`](docs/environmentmapeffect-support.md)
- ⬜ **ČEKÁ** — [`docs/filesystem-path-model.md`](docs/filesystem-path-model.md)
- ⬜ **ČEKÁ** — [`docs/fna-reference-harness.md`](docs/fna-reference-harness.md)
- ✅ **ZPRACOVÁNO** — [`docs/fna3d-parity-report.md`](docs/fna3d-parity-report.md)
- ✅ **ZPRACOVÁNO** — [`docs/fna3d-renderer.md`](docs/fna3d-renderer.md)
- ⬜ **ČEKÁ** — [`docs/framework-design.md`](docs/framework-design.md)
- ⬜ **ČEKÁ** — [`docs/fx-bytecode-fuzzing.md`](docs/fx-bytecode-fuzzing.md)
- ⬜ **ČEKÁ** — [`docs/fx-compiled-effects.md`](docs/fx-compiled-effects.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/gamer-services-known-limitations.md`](docs/gamer-services-known-limitations.md)
- ⬜ **ČEKÁ** — [`docs/gamer-services-manual-qualification.md`](docs/gamer-services-manual-qualification.md)
- ⬜ **ČEKÁ** — [`docs/gamer-services-server.md`](docs/gamer-services-server.md)
- ✅ **ZPRACOVÁNO** — [`docs/gdm-coverage.md`](docs/gdm-coverage.md)
- ✅ **ZPRACOVÁNO** — [`docs/gltf-api-change-review.md`](docs/gltf-api-change-review.md)
- ⬜ **ČEKÁ** — [`docs/gltf-campaign-retrospective.md`](docs/gltf-campaign-retrospective.md)
- ✅ **ZPRACOVÁNO** — [`docs/gltf-center-collapse-verdict.md`](docs/gltf-center-collapse-verdict.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/gltf-conformance.md`](docs/gltf-conformance.md)
- ⬜ **ČEKÁ** — [`docs/gltf-conventions.md`](docs/gltf-conventions.md)
- ⬜ **ČEKÁ** — [`docs/gltf-limitations.md`](docs/gltf-limitations.md)
- ⬜ **ČEKÁ** — [`docs/gltf-performance.md`](docs/gltf-performance.md)
- ⬜ **ČEKÁ** — [`docs/gltf-renderer-pbr-fallbacks.md`](docs/gltf-renderer-pbr-fallbacks.md)
- ⬜ **ČEKÁ** — [`docs/gltf-renderer-stride-conformance.md`](docs/gltf-renderer-stride-conformance.md)
- ✅ **ZPRACOVÁNO** — [`docs/graphics-renderer-feature-matrix.md`](docs/graphics-renderer-feature-matrix.md)
- ✅ **ZPRACOVÁNO** — [`docs/graphics-resource-lifetime.md`](docs/graphics-resource-lifetime.md)
- ✅ **ZPRACOVÁNO** — [`docs/headless-renderer.md`](docs/headless-renderer.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-backend.md`](docs/input-backend.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-build-and-test.md`](docs/input-build-and-test.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-fna-fidelity.md`](docs/input-fna-fidelity.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-manual-verification-results.md`](docs/input-manual-verification-results.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-member-parity-matrix.md`](docs/input-member-parity-matrix.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-pre-merge-checklist.md`](docs/input-pre-merge-checklist.md)
- ⬜ **ČEKÁ** — [`docs/input-public-api-frozen.md`](docs/input-public-api-frozen.md)
- ✅ **ZPRACOVÁNO** — [`docs/input-test-coverage.md`](docs/input-test-coverage.md)
- ⬜ **ČEKÁ** — [`docs/inspector-benchmark.md`](docs/inspector-benchmark.md)
- ⬜ **ČEKÁ** — [`docs/inspector.md`](docs/inspector.md)
- ⬜ **ČEKÁ** — [`docs/installed-cpp-sdk.md`](docs/installed-cpp-sdk.md)
- ⬜ **ČEKÁ** — [`docs/keyboard-device-emulation.md`](docs/keyboard-device-emulation.md)
- ⬜ **ČEKÁ** — [`docs/link-size-architecture.md`](docs/link-size-architecture.md)
- ⬜ **ČEKÁ** — [`docs/location-future-plan.md`](docs/location-future-plan.md)
- ⬜ **ČEKÁ** — [`docs/location-proposal.md`](docs/location-proposal.md)
- ✅ **ZPRACOVÁNO** — [`docs/maintainer-study-guide.md`](docs/maintainer-study-guide.md)
- ✅ **ZPRACOVÁNO** — [`docs/migration-guide.md`](docs/migration-guide.md)
- ⬜ **ČEKÁ** — [`docs/model-content-pipeline-support.md`](docs/model-content-pipeline-support.md)
- ✅ **ZPRACOVÁNO** — [`docs/occlusionquery-support.md`](docs/occlusionquery-support.md)
- ✅ **ZPRACOVÁNO** — [`docs/opengl33-renderer.md`](docs/opengl33-renderer.md)
- ⬜ **ČEKÁ** — [`docs/physical-modules.md`](docs/physical-modules.md)
- ⬜ **ČEKÁ** — [`docs/platform-abstraction.md`](docs/platform-abstraction.md)
- ⬜ **ČEKÁ** — [`docs/platform-entrypoint-audit.md`](docs/platform-entrypoint-audit.md)
- ⬜ **ČEKÁ** — [`docs/platform-input-notes.md`](docs/platform-input-notes.md)
- ⬜ **ČEKÁ** — [`docs/platform-input-seam-mapping.md`](docs/platform-input-seam-mapping.md)
- ⬜ **ČEKÁ** — [`docs/platform-performance.md`](docs/platform-performance.md)
- ⬜ **ČEKÁ** — [`docs/platform-renderer-boundary.md`](docs/platform-renderer-boundary.md)
- ⬜ **ČEKÁ** — [`docs/platform-renderer-sdl-audit.md`](docs/platform-renderer-sdl-audit.md)
- ⬜ **ČEKÁ** — [`docs/platform-sdl-lifecycle-audit.md`](docs/platform-sdl-lifecycle-audit.md)
- ⬜ **ČEKÁ** — [`docs/platform-terminal-analysis.md`](docs/platform-terminal-analysis.md)
- ✅ **ZPRACOVÁNO** — [`docs/rasterizerstate-support.md`](docs/rasterizerstate-support.md)
- ⬜ **ČEKÁ** — [`docs/releasing.md`](docs/releasing.md)
- ✅ **ZPRACOVÁNO** — [`docs/removed-renderers.md`](docs/removed-renderers.md)
- ✅ **ZPRACOVÁNO** — [`docs/renderer-capability-profiles.md`](docs/renderer-capability-profiles.md)
- ✅ **ZPRACOVÁNO** — [`docs/renderer-registry.md`](docs/renderer-registry.md)
- ✅ **ZPRACOVÁNO** — [`docs/rendertarget-support.md`](docs/rendertarget-support.md)
- ✅ **ZPRACOVÁNO** — [`docs/runtime-renderer-selection.md`](docs/runtime-renderer-selection.md)
- ✅ **ZPRACOVÁNO** — [`docs/sampler-state-support.md`](docs/sampler-state-support.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/sdl-gpu-renderer.md`](docs/sdl-gpu-renderer.md)
- ✅ **ZPRACOVÁNO** — [`docs/sdlgpu-shader-effect-layout.md`](docs/sdlgpu-shader-effect-layout.md)
- ✅ **ZPRACOVÁNO** — [`docs/shader-effect-vs-fx-bytecode.md`](docs/shader-effect-vs-fx-bytecode.md)
- ⬜ **ČEKÁ** — [`docs/skinned-model-ext.md`](docs/skinned-model-ext.md)
- ✅ **ZPRACOVÁNO** — [`docs/skinnedeffect-support.md`](docs/skinnedeffect-support.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/software-easygl-parity-ledger.md`](docs/software-easygl-parity-ledger.md)
- ✅ **ZPRACOVÁNO** — [`docs/software-renderer.md`](docs/software-renderer.md)
- ✅ **ZPRACOVÁNO** — [`docs/spritefont-support.md`](docs/spritefont-support.md)
- ✅ **ZPRACOVÁNO** — [`docs/stub-renderer.md`](docs/stub-renderer.md)
- ✅ **ZPRACOVÁNO** — [`docs/surface-format-support.md`](docs/surface-format-support.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/task-id-index.md`](docs/task-id-index.md)
- ⬜ **ČEKÁ** — [`docs/texture-stream-formats.md`](docs/texture-stream-formats.md)
- ✅ **ZPRACOVÁNO** — [`docs/texture3d-texturecube-support.md`](docs/texture3d-texturecube-support.md)
- ✅ **ZPRACOVÁNO** — [`docs/vertex-format-support.md`](docs/vertex-format-support.md)
- ⬜ **ČEKÁ** — [`docs/video-backend.md`](docs/video-backend.md)
- ✅ **ZPRACOVÁNO** — [`docs/viewport-displaymode-adapter-support.md`](docs/viewport-displaymode-adapter-support.md)
- ✅ **ZPRACOVÁNO** — [`docs/web-emscripten-graphics-limitations.md`](docs/web-emscripten-graphics-limitations.md)
- ✅ **ZPRACOVÁNO** — [`docs/webgl2-renderer.md`](docs/webgl2-renderer.md)
- ✅ **ZPRACOVÁNO** — [`docs/webgpu-renderer.md`](docs/webgpu-renderer.md)
- ⬜ **ČEKÁ** — [`docs/windows-visual-studio.md`](docs/windows-visual-studio.md)
- ⬜ **ČEKÁ** — [`docs/xma-encoder-backend.md`](docs/xma-encoder-backend.md)
- ⬜ **ČEKÁ** — [`docs/xna-4-enumerator-reference.md`](docs/xna-4-enumerator-reference.md)
- ⬜ **ČEKÁ** — [`docs/xna-4-runtime-member-coverage-baseline.md`](docs/xna-4-runtime-member-coverage-baseline.md)
- ⬜ **ČEKÁ** — [`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
- ⬜ **ČEKÁ** — [`docs/xna-content-pipeline-compat-api.md`](docs/xna-content-pipeline-compat-api.md)
- ⬜ **ČEKÁ** — [`docs/xna-content-pipeline-components.md`](docs/xna-content-pipeline-components.md)
- ⬜ **ČEKÁ** — [`docs/xna-content-pipeline-final-audit.md`](docs/xna-content-pipeline-final-audit.md)
- ⬜ **ČEKÁ** — [`docs/xna-content-pipeline-media.md`](docs/xna-content-pipeline-media.md)
- ⬜ **ČEKÁ** — [`docs/xna-content-pipeline-migration.md`](docs/xna-content-pipeline-migration.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/xna-content-pipeline-parity-report.md`](docs/xna-content-pipeline-parity-report.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/xna-fna-behavior-decisions.md`](docs/xna-fna-behavior-decisions.md)
- ⬜ **ČEKÁ** — [`docs/xna-intermediate-xml-format.md`](docs/xna-intermediate-xml-format.md)
- ⬜ **ČEKÁ** — [`docs/xna4-runtime-compatibility.md`](docs/xna4-runtime-compatibility.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/xnb-content-pipeline-support.md`](docs/xnb-content-pipeline-support.md)
- 🟡 **ČEKÁ — podpůrně aktualizováno** — [`docs/xnb-interoperability.md`](docs/xnb-interoperability.md)

## `docs/c-api/`

- ✅ **ZPRACOVÁNO** — [`docs/c-api/ABI_VERSIONING.md`](docs/c-api/ABI_VERSIONING.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/AUDIO.md`](docs/c-api/AUDIO.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/CALLBACKS_AND_THREADING.md`](docs/c-api/CALLBACKS_AND_THREADING.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/CNB.md`](docs/c-api/CNB.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/COMPATIBILITY.md`](docs/c-api/COMPATIBILITY.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/CONSUMING.md`](docs/c-api/CONSUMING.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/CONTENT.md`](docs/c-api/CONTENT.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/CORE.md`](docs/c-api/CORE.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/COVERAGE.md`](docs/c-api/COVERAGE.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/DEVICES.md`](docs/c-api/DEVICES.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/EFFECTS.md`](docs/c-api/EFFECTS.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/ERRORS.md`](docs/c-api/ERRORS.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/FUZZING.md`](docs/c-api/FUZZING.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/GAMER_SERVICES.md`](docs/c-api/GAMER_SERVICES.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/GAME_COMPONENTS.md`](docs/c-api/GAME_COMPONENTS.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/GENERATED_ROUTES.md`](docs/c-api/GENERATED_ROUTES.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/GRAPHICS_DEVICE.md`](docs/c-api/GRAPHICS_DEVICE.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/GRAPHICS_EXT.md`](docs/c-api/GRAPHICS_EXT.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/GRAPHICS_RESOURCES.md`](docs/c-api/GRAPHICS_RESOURCES.md)
- ⬜ **ČEKÁ** — [`docs/c-api/HANDLES.md`](docs/c-api/HANDLES.md)
- ⬜ **ČEKÁ** — [`docs/c-api/INDEX_BUFFERS.md`](docs/c-api/INDEX_BUFFERS.md)
- ⬜ **ČEKÁ** — [`docs/c-api/INPUT_SNAPSHOTS.md`](docs/c-api/INPUT_SNAPSHOTS.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/LIMITATIONS.md`](docs/c-api/LIMITATIONS.md)
- ⬜ **ČEKÁ** — [`docs/c-api/MATH_AND_3D_VALUES.md`](docs/c-api/MATH_AND_3D_VALUES.md)
- ⬜ **ČEKÁ** — [`docs/c-api/MEDIA.md`](docs/c-api/MEDIA.md)
- ⬜ **ČEKÁ** — [`docs/c-api/MODELS.md`](docs/c-api/MODELS.md)
- ⬜ **ČEKÁ** — [`docs/c-api/NET.md`](docs/c-api/NET.md)
- ⬜ **ČEKÁ** — [`docs/c-api/OWNERSHIP.md`](docs/c-api/OWNERSHIP.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/README.md`](docs/c-api/README.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/RELEASE_GATE.md`](docs/c-api/RELEASE_GATE.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/RENDERERS_AND_CAPABILITIES.md`](docs/c-api/RENDERERS_AND_CAPABILITIES.md)
- ⬜ **ČEKÁ** — [`docs/c-api/SHARP_RUNTIME_BOUNDARY.md`](docs/c-api/SHARP_RUNTIME_BOUNDARY.md)
- ⬜ **ČEKÁ** — [`docs/c-api/STORAGE.md`](docs/c-api/STORAGE.md)
- ⬜ **ČEKÁ** — [`docs/c-api/STRINGS_AND_BUFFERS.md`](docs/c-api/STRINGS_AND_BUFFERS.md)
- ⬜ **ČEKÁ** — [`docs/c-api/TEXTURES.md`](docs/c-api/TEXTURES.md)
- ⬜ **ČEKÁ** — [`docs/c-api/TEXTURE_VOLUMES.md`](docs/c-api/TEXTURE_VOLUMES.md)
- ⬜ **ČEKÁ** — [`docs/c-api/VERTEX_BUFFERS.md`](docs/c-api/VERTEX_BUFFERS.md)
- ✅ **ZPRACOVÁNO** — [`docs/c-api/WASM_ARTIFACT.md`](docs/c-api/WASM_ARTIFACT.md)
