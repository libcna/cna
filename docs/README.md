# Documentation index

This is the topical index of the top-level pages in `docs/`; the
[C API index](c-api/README.md) covers its nested contract pages.

The 2026-10-31 to 2026-11-01 review checked the pages against their owning sources, tests and
recorded evidence. Historical measurements, frozen baselines, proposals and qualification limits
remain explicitly identified; a review date does not turn them into new runtime or device results.
For current defects use [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), and for renderer
support use the [feature matrix](graphics-renderer-feature-matrix.md) together with the selected
build/device's [capability profile](renderer-capability-profiles.md). Generated reports describe
their stated metadata/source checks, rather than proving complete behavioral parity.

## Start here

- [KNOWN_ISSUES.md](../KNOWN_ISSUES.md) — Current project state and the authoritative current defect and
  limitation register.
- [maintainer-study-guide.md](maintainer-study-guide.md) — Evidence-driven study and change
  workflow, module dependencies and a rolling study cycle.
- [migration-guide.md](migration-guide.md) — Porting an XNA/FNA game: C++ API conventions, content,
  renderer choices and validation.
- [binding.md](binding.md) — The approved native C API/C ABI boundary and the rules for separately
  authorized language consumers.
- [c-api/README.md](c-api/README.md) — Native C API entry point and index of its ABI, ownership,
  subsystem and release-gate contracts.
- [xna-fna-behavior-decisions.md](xna-fna-behavior-decisions.md) — Measured XNA/FNA disagreements,
  retained decisions and their evidence.
- [downstream-migrations.md](downstream-migrations.md) — What other repositories must change or migrate
  because of a CNA change.
- [task-id-index.md](task-id-index.md) — Historical task prefixes, deleted plans and their surviving
  results.

## Build, architecture and release

- [development-environment.md](development-environment.md) — Debian/Linux dependencies, sibling
  checkouts, submodules, optional feature groups, and system-SDL and source-archive builds.
- [windows-visual-studio.md](windows-visual-studio.md) — Native Windows presets, Visual Studio/VS
  Code, MSVC runtime consistency, deployment and CI evidence.
- [installed-cpp-sdk.md](installed-cpp-sdk.md) — Opt-in installed C++ SDK, exported modules,
  relocation and install verification.
- [physical-modules.md](physical-modules.md) — Module ownership, physical source layout and stable
  public include paths.
- [platform-abstraction.md](platform-abstraction.md) — CNA-owned platform contracts and the default
  SDL3 implementation.
- [filesystem-path-model.md](filesystem-path-model.md) — Native filesystem paths, Unicode conversion
  and cross-platform boundaries.
- [link-size-architecture.md](link-size-architecture.md) — Subsystem link boundaries, lazy
  registration, footprint options and regression gates.
- [build-performance.md](build-performance.md) — Compiler/linker policy, ccache, build presets and
  measurement methodology.
- [releasing.md](releasing.md) — Product version sources, independent ABI versions and the release
  procedure.
- [framework-design.md](framework-design.md) — The opt-in `Microsoft.Xna.Framework.Design`
  type-converter module.

## Runtime API references

- [xna-4-runtime-member-coverage.md](xna-4-runtime-member-coverage.md) — Generated current
  declaration census against Microsoft reference metadata; presence does not establish behavior or
  test coverage.
- [xna-4-enumerator-reference.md](xna-4-enumerator-reference.md) — Typed and boxed
  collection-enumerator contracts, cursor state and reference evidence.
- [xna4-runtime-compatibility.md](xna4-runtime-compatibility.md) — Process-wide Framework-style
  argument-exception diagnostics and the `Game` compatibility switch.

## Renderer selection and implementations

- [renderer-registry.md](renderer-registry.md) — Canonical public renderer identities, selectors,
  factories and dependency/platform gates.
- [graphics-renderer-feature-matrix.md](graphics-renderer-feature-matrix.md) — Cross-renderer
  feature overview; consult the live profile for the selected build and device.
- [renderer-capability-profiles.md](renderer-capability-profiles.md) — Detailed feature, limit and
  format queries, limitations reports and matching C ABI.
- [runtime-renderer-selection.md](runtime-renderer-selection.md) — Opt-in multi-renderer binaries,
  selection/fallback rules and window-kind constraints.
- [opengl33-renderer.md](opengl33-renderer.md) — Desktop OpenGL 3.3 core contract and its sharing
  with the OPENGLES3 EasyGL family.
- [webgl2-renderer.md](webgl2-renderer.md) — Browser WebGL 2 context, shader dialect, validation and
  qualification boundary.
- [sdl-gpu-renderer.md](sdl-gpu-renderer.md) — SDLGPU build options, SDL driver selection,
  presentation/resource rules, bounded parity evidence and the stock-effect fog-alpha contract
  (CNA-FIX-D01).
- [webgpu-renderer.md](webgpu-renderer.md) — Supported native/browser WebGPU path, pinned
  dependency, parity evidence and important limitations.
- [directx9-renderer.md](directx9-renderer.md) — Direct3D 9 implementation, capability and platform
  contract.
- [d3d9-divergence-report.md](d3d9-divergence-report.md) — D3D9 oracle measurement against real XNA 4.0: method, corpus coverage,
  unmeasured areas and the six project-wide divergence statuses.
- [fna3d-renderer.md](fna3d-renderer.md) — Experimental FNA3D family, pinned dependencies, driver
  selection and compiled-effect route.
- [fna3d-dependency-defects.md](fna3d-dependency-defects.md) — Open FNA3D/MojoShader fork defects
  (FNA3D-8, FNA3D-3), pending qualification and the fixed fork repairs.
- [software-renderer.md](software-renderer.md) — Experimental CPU rasterizer, presentation paths,
  resource limits and effect execution boundaries.
- [headless-renderer.md](headless-renderer.md) — HEADLESS rendering contract, simulated resources
  and test boundaries.
- [stub-renderer.md](stub-renderer.md) — STUB no-op renderer, accepted surface and intentional
  limitations.
- [removed-renderers.md](removed-renderers.md) — Retired identities, reserved C ABI values, removal commits, reasons
  and dependency coordinates.

## Graphics APIs and effects

- [basiceffect-support.md](basiceffect-support.md) — BasicEffect variants, material/lighting, fog,
  inputs, measured saturation and extensions.
- [alphatesteffect-support.md](alphatesteffect-support.md) — AlphaTestEffect variants, comparison
  boundaries, alpha ordering, fog and discard.
- [dualtextureeffect-support.md](dualtextureeffect-support.md) — DualTextureEffect state, shader
  behavior and renderer evidence.
- [environmentmapeffect-support.md](environmentmapeffect-support.md) — EnvironmentMapEffect
  lighting, environment contribution and evidence.
- [skinnedeffect-support.md](skinnedeffect-support.md) — SkinnedEffect weights, palette, lighting,
  fog and renderer coverage.
- [depthstencilstate-support.md](depthstencilstate-support.md) — Depth/stencil state defaults,
  application and renderer limitations.
- [sampler-state-support.md](sampler-state-support.md) — Filter/address/LOD mapping, sampler
  lifetime and SpriteBatch publication.
- [rasterizerstate-support.md](rasterizerstate-support.md) — Culling, wireframe, depth bias, scissor
  and the distinct MSAA controls.
- [graphics-resource-lifetime.md](graphics-resource-lifetime.md) — Ownership, disposal/event
  ordering, device tracking and native resource retirement.
- [rendertarget-support.md](rendertarget-support.md) — RenderTarget2D/RenderTargetCube, binding,
  preservation, readback and MSAA boundaries.
- [texture3d-texturecube-support.md](texture3d-texturecube-support.md) — Volume/cube construction,
  transfer and per-renderer support.
- [surface-format-support.md](surface-format-support.md) — SurfaceFormat storage/transfer behavior,
  focused on EasyGL; use live profiles for other families.
- [texture-stream-formats.md](texture-stream-formats.md) — Texture2D encoded stream decoding and the
  explicit DDS extension route.
- [vertex-format-support.md](vertex-format-support.md) — Vertex declarations, element formats,
  usages and renderer mappings.
- [viewport-displaymode-adapter-support.md](viewport-displaymode-adapter-support.md) —
  Viewport/display values, adapter lifetime and device/presentation selection.
- [occlusionquery-support.md](occlusionquery-support.md) — Query lifecycle, overlapping intervals
  and exact versus visibility results.
- [spritefont-support.md](spritefont-support.md) — SpriteFont/DrawString behavior, UTF-8/BMP
  adaptation and rendering limits.
- [gdm-coverage.md](gdm-coverage.md) — GraphicsDeviceManager lifecycle, reset/events and deliberate
  ownership/resize adaptations.
- [model-content-pipeline-support.md](model-content-pipeline-support.md) — Model runtime API and the
  distinct XNB, CNB, CNJ and glTF loading routes.
- [skinned-model-ext.md](skinned-model-ext.md) — The separate CNAEXT SkinnedModelEXT animation and
  GPU-skinning system.
- [shader-effect-vs-fx-bytecode.md](shader-effect-vs-fx-bytecode.md) — Renderer-dialect ShaderEffect
  acceptance/execution versus compiled XNA bytecode.
- [fx-compiled-effects.md](fx-compiled-effects.md) — Compiled XNA effects: build options,
  translation, reflection, state and renderer execution.
- [sdlgpu-shader-effect-layout.md](sdlgpu-shader-effect-layout.md) — SDLGPU source/bytecode resource
  bindings and ShaderEffect layout contract.
- [fx-bytecode-fuzzing.md](fx-bytecode-fuzzing.md) — Compiled-effect fuzzing gates, the scheduled long campaign,
  reproduction recipes and the FX-051 campaign's limits.
- [cnaext-engine-layer.md](cnaext-engine-layer.md) — Standalone graphics extensions and the
  retirement of the former modern engine layer.
- [ascii-post-process-effect.md](ascii-post-process-effect.md) — Renderer-neutral ASCII/glyph
  post-processing and migration from the retired ASCII renderer.

## Graphics conformance and glTF

- [cross-renderer-parity-fixtures.md](cross-renderer-parity-fixtures.md) — Shared
  EasyGL/WebGPU/SDLGPU fixtures, the separate six-family corpus, oracles and tolerances.
- [fna-reference-harness.md](fna-reference-harness.md) — FNA value/effect comparison tools,
  dependencies and evidence boundaries.
- [gltf-conformance.md](gltf-conformance.md) — Pinned specification/validator, corpus
  classifications and layered conformance evidence.
- [gltf-limitations.md](gltf-limitations.md) — Current glTF feature, rendering and interoperability
  boundaries.
- [gltf-conventions.md](gltf-conventions.md) — Coordinate, unit, material and asset conventions;
  conversions CNA deliberately does not perform.
- [gltf-renderer-stride-conformance.md](gltf-renderer-stride-conformance.md) — Seven vertex layouts
  and bounded STUB/HEADLESS/OpenGL ES 3 evidence.
- [gltf-renderer-pbr-fallbacks.md](gltf-renderer-pbr-fallbacks.md) — PBR binding ABIs, semantic
  neutral textures and renderer-specific evidence.
- [gltf-performance.md](gltf-performance.md) — Import performance/lifetime records and their
  measurement scope.
- [gltf-api-change-review.md](gltf-api-change-review.md) — Historical API-change decisions,
  including retired extension/renderer evidence.

## Content formats and build-time pipeline

- [content-pipeline.md](content-pipeline.md) — Native importer/processor/writer engine, CNB/XNB
  routes, dependencies, manifests and custom extensions.
- [cnj-format.md](cnj-format.md) — Editable JSON content envelopes, sidecars, per-type payloads and
  custom loaders.
- [cnb-format.md](cnb-format.md) — Compiled CNB container, versioned schemas, references and runtime
  loading contract.
- [xnb-content-pipeline-support.md](xnb-content-pipeline-support.md) — Runtime XNB readers, lazy
  registration, object graphs, compression and limitations.
- [xnb-interoperability.md](xnb-interoperability.md) — Native XNB writer, targets/compression,
  custom writers and the strength of each interoperability claim.
- [xna-content-pipeline-compat-api.md](xna-content-pipeline-compat-api.md) — XNA-style C++ Content
  Pipeline façade over the native engine.
- [xna-content-pipeline-migration.md](xna-content-pipeline-migration.md) — Existing .contentproj
  builds, directory/custom-compiler routes, XML and intentional differences.
- [xna-content-pipeline-components.md](xna-content-pipeline-components.md) — Generated
  importer/processor/property reference with XNA defaults and C++ mappings.
- [xna-content-pipeline-parity-report.md](xna-content-pipeline-parity-report.md) — Generated
  classifications from the maintained member/input map; executable parity evidence is separate.
- [xna-intermediate-xml-format.md](xna-intermediate-xml-format.md) — Measured IntermediateSerializer
  format, CNA implementation boundaries and retained corpus.
- [xna-content-pipeline-media.md](xna-content-pipeline-media.md) — MP3/WMA/WMV processing routes,
  dependencies and finite source-parity evidence.
- [xma-encoder-backend.md](xma-encoder-backend.md) — Optional XMA encoder boundary and the measured
  fixture; no universal encoder/device qualification.
- [content-pipeline-benchmark.md](content-pipeline-benchmark.md) — Dated build-time pipeline
  measurements and reproduction methodology.

## Platform integration and qualification

- [android-graphics-limitations.md](android-graphics-limitations.md) — Android graphics
  configuration, lifecycle and qualification boundaries.
- [apple-platforms.md](apple-platforms.md) — macOS/iOS builds, bundles, lifecycle/storage and
  explicit CI/simulator/device evidence limits.
- [web-emscripten-graphics-limitations.md](web-emscripten-graphics-limitations.md) — WEBGL2/WEBGPU
  browser linking, canvas, persistence, threading and context-loss boundaries.
- [emscripten-mainloop-game-lifetime.md](emscripten-mainloop-game-lifetime.md) — Browser Game::Run
  main-loop ownership and object lifetime.
- [platform-entrypoint-audit.md](platform-entrypoint-audit.md) — Application entry points and
  platform-specific launch ownership.
- [platform-renderer-boundary.md](platform-renderer-boundary.md) — Platform services exposed to
  renderers and the implementation boundary.
- [platform-renderer-sdl-audit.md](platform-renderer-sdl-audit.md) — SDL dependencies in renderer
  code and their maintained dispositions.
- [platform-sdl-lifecycle-audit.md](platform-sdl-lifecycle-audit.md) — SDL subsystem acquisition,
  ownership and teardown.
- [platform-terminal-analysis.md](platform-terminal-analysis.md) — Implemented terminal
  platform/presentation behavior and limitations.
- [consoles.md](consoles.md) — Console portability considerations; no CNA console port is
  implemented or qualified.

## Input

- [input-backend.md](input-backend.md) — SDL/platform input architecture, frame snapshots and FNA
  polling differences.
- [input-build-and-test.md](input-build-and-test.md) — Build and private-display test workflow.
- [input-fna-fidelity.md](input-fna-fidelity.md) — Behavioral comparisons, adaptations and evidence
  limits.
- [input-member-parity-matrix.md](input-member-parity-matrix.md) — Member-level source mappings and
  evidence.
- [input-test-coverage.md](input-test-coverage.md) — Source-to-test coverage map and its scope.
- [input-public-api-frozen.md](input-public-api-frozen.md) — Public input API boundary and signature
  guards.
- [input-pre-merge-checklist.md](input-pre-merge-checklist.md) — Maintained input regression
  checklist.
- [demo-input-checklist.md](demo-input-checklist.md) — Manual demo-input scenarios and
  result-recording instructions.
- [platform-input-notes.md](platform-input-notes.md) — Platform-specific input behavior and
  remaining boundaries.
- [platform-input-seam-mapping.md](platform-input-seam-mapping.md) — Input-service ownership after
  the platform migration.
- [keyboard-device-emulation.md](keyboard-device-emulation.md) — Keyboard-driven GamePad, sensor and
  orientation emulation.

## Devices and sensors

- [devices-api-coverage.md](devices-api-coverage.md) — Current native device/sensor surface and the
  Windows Phone reference boundary.
- [devices-build.md](devices-build.md) — Feature configuration, builds and private-display tests.
- [devices-android.md](devices-android.md) — Android device/sensor integration and qualification
  limits.
- [devices-native-backend-design.md](devices-native-backend-design.md) — Implemented Android sensor
  path, platform interfaces and remaining iOS scope.
- [devices-event-contract.md](devices-event-contract.md) — Callback delivery, subscription and
  lifecycle contract.
- [devices-thread-safety.md](devices-thread-safety.md) — Thread ownership, synchronization and
  permitted API use.
- [devices-hardware-checklist.md](devices-hardware-checklist.md) — Manual hardware qualification
  procedure.
- [devices_sensor_hardware_qa_template.md](devices_sensor_hardware_qa_template.md) — Template for
  recording device, build and hardware evidence.
- [devices-ext.md](devices-ext.md) — CNA-specific device extensions beyond XNA.
- [cna-devices-camera-design.md](cna-devices-camera-design.md) — Implemented CNA::Devices::Camera
  extension, platform capture and ownership.
- [location-future-plan.md](location-future-plan.md) — Unimplemented location scope guard and
  compatibility boundary.
- [location-proposal.md](location-proposal.md) — Location proposal and the owner's 2026-10-25
  decisions; not an implemented service.

## Audio, media, networking and avatars

- [audio-alsa.md](audio-alsa.md) — Linux ALSA/default audio routing and CNA's mixer boundary.
- [xact-rpc.md](xact-rpc.md) — Per-voice runtime curves, intrinsic clocks, filters and release.
- [xact-reverb.md](xact-reverb.md) — Reverb sends, DSP preset ownership and offline evidence.
- [video-backend.md](video-backend.md) — Optional FFmpeg video decoding, build dependencies and
  playback limitations.
- [browser-network-readiness.md](browser-network-readiness.md) — Browser networking refusals,
  retained modules and qualification prerequisites.
- [gamer-services-known-limitations.md](gamer-services-known-limitations.md) —
  GamerServices/Net/Guide/Avatar subsystem boundaries and evidence; KNOWN_ISSUES.md owns the consolidated
  defect list.
- [gamer-services-server.md](gamer-services-server.md) — Server deployment, protocol/configuration
  and operational boundaries.
- [gamer-services-manual-qualification.md](gamer-services-manual-qualification.md) — Manual
  client/server qualification procedure and required evidence.
- [avatars.md](avatars.md) — Original CNA avatars, description encoding, rendering, animation and
  assets.
- [avatar-art-direction.md](avatar-art-direction.md) — Avatar asset/art requirements and the
  distinction from API compatibility.
- [avatar-demos.md](avatar-demos.md) — Avatar demo inventory and reproduction routes.

## Diagnostics and inspection

- [diagnostics.md](diagnostics.md) — OFF/STATS/FULL profiler architecture, metrics, event/trace
  formats, provider boundaries and the instrumentation overhead benchmark.
- [inspector.md](inspector.md) — Optional authenticated out-of-process Inspector, protocol/UI and
  capabilities.

Use the owning page's reproduction route and evidence scope. Run GPU/window tests through
`tools/platform/run_gpu_tests_private.sh`; repository build and contribution rules are in
[AGENTS.md](../AGENTS.md) and [CLAUDE.md](../CLAUDE.md).
