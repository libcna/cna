# AUDIT_FINDINGS_INDEX.md

**Status: POPULATED AND COMPLETE (rebuilt 2026-07-19; Pass 3 and Pass 6 both fully closed out same
day).** This index consolidates every `MEDIUM`+ finding (and recurring `LOW`s) from the entire
repository-wide audit — all 2297 files across 105 shards, Pass 3's complete API-surface-completeness
sweep (every real `Microsoft.Xna.Framework.*` namespace vs. the real xn65 XML reference, ~2700+
members checked), and Pass 6's complete build/runtime-test sweep of all 14 real graphics backends
(EasyGL, Canvas, D3D9, D3D11, D3D12, Dx3, WebGPU, Vulkan, SdlGpu, Bgfx, SdlRenderer, Software, Ascii,
Headless). It supersedes an earlier partial draft that only covered the first few graphics-backend
batches; the fuller narrative for every entry below, including every incremental "confirmed in N
more backends" update as the investigation progressed, lives in `AUDIT_CROSS_CUTTING_FINDINGS.md`
(~2500 lines) and each finding's own `audit/<path>.audit.md` report. Entries below are deduplicated
to their final, fully-confirmed state — e.g. the fog-formula bug went through
~4 "UPDATE" rounds as more backends were checked; there is one entry for it here, not four.

No `CRITICAL` findings were confirmed anywhere in this audit.

Recommendations recorded here are for future prioritization only — **no implementation work is performed as part
of this audit** (see `CLAUDE.md`/audit prompt "No-development rule").

## By severity

### CRITICAL

- **Malformed/mutated `.xnb` `Texture2D` content crashes the process on BOTH Vulkan and WebGPU**
  (`XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly`) — likely the
  single most severe finding of this entire audit. Confirmed clean on EasyGL. Two independent,
  structurally different root causes converging on the same shape: XNB-decoded
  `width`/`height`/`mipLevels` are trusted and passed directly into a native GPU API with no
  CNA-side sanity check. **Vulkan**: real `*** stack smashing detected ***` — `VulkanTextureBackend`'s
  constructor drives `vkCreateImage` from unvalidated dimensions; the driver accepts
  grossly-out-of-spec values the validation layer only warns about, corrupting the stack downstream.
  **WebGPU**: a fatal, non-catchable Rust panic across the wgpu-native FFI boundary —
  `GenerateMipsForLayer()` checks `wgpuTextureCreateView()`'s synchronous return for `nullptr`, but
  wgpu-native validates the view's `baseMipLevel` *lazily* at `wgpuQueueSubmit()` time, past the
  existing check entirely. Real crash-DoS security exposure via any corrupted, truncated, or
  maliciously-crafted `Texture2D` asset loaded from disk, mods, or network transfer — on either
  backend. Suggested fix: validate decoded dimensions/mip-count against real device limits
  immediately after XNB decode, in shared `Texture2D`/`Texture2DContentTypeReader` code, not
  per-backend. See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6 continued).

### HIGH

**Testing infrastructure / CI (Pass 6):**

- **`gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)` has no `WORKING_DIRECTORY` override,
  breaking ~220 fixture-file-loading tests invisibly to every existing CI workflow.** Confirmed via
  the generated `CTestTestfile.cmake`: all 5507 discovered test cases have
  `WORKING_DIRECTORY=cmake-build-debug` baked in, not the repo root where `tests/assets/**` lives —
  any test loading a real fixture file by repo-root-relative path (Media/Audio-tag-parsing/Xnb-
  content-pipeline/ENet-networking/Lzx-decompression test groups) throws `FileNotFoundException`
  under `ctest`, despite passing cleanly when manually run with the correct working directory. All
  3 existing GitHub Actions workflows use a `-L <label>`-filtered `ctest --test-dir build` invocation
  that never runs the general/default test set this bug affects — meaning these ~220 tests have
  likely never once passed in any CI run this project has had, not because they're known-broken and
  excluded, but as a side effect of CI's narrow labeled-subset structure combined with this
  registration gap. Independently confirmed twice (two separately-dispatched investigations found
  the identical root cause), and reconciled against this project's own existing, narrower-scoped
  awareness that `ctest` is unreliable for the general suite (a `/tmp`-scratch-path race under
  parallelism, `plans/plan_audio.md` P9-BUILD-007) — this is a third, previously-undocumented, more
  fundamental, and more impactful reason, not a duplicate of the known one. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **`cna_demo_xact`'s Content-copy build step fails on every backend, confirmed universal across 6
  independently-built backends this session.** `cmake/Examples.cmake` registers an unconditional
  `POST_BUILD` step copying `examples/demo_xact/Content`, which does not exist anywhere in the
  repository — aborts the whole top-level `cmake --build` invocation regardless of backend. Silently
  also broke this session's own earlier EasyGL build, unnoticed because a `| tail` pipe masked the
  real exit code. See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6, Bgfx section).
- **`MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` SEGFAULTs, now confirmed universal
  across 6+ backends** (EasyGL, SdlRenderer, Software, Ascii, Headless, and structurally implicated
  elsewhere) — a real, backend-independent robustness bug in shared CPU-side object-graph-walking
  code, not an EasyGL-specific artifact. See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **EasyGL: `SetRenderTargets` with 2 attachments only draws to the first one** — confirmed
  reproducible in complete isolation (`EasyGL_MRT_TwoAttachments`): left attachment correctly green,
  right attachment stays black instead of the expected blue. A real, previously-undisclosed defect
  in a documented XNA 4.0 feature (multiple render targets), not an untested edge case. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **D3D11: 2 new runtime-confirmed defects** — (a) Blinn-Phong specular fails for non-skinned
  `lit_textured3d` at a `dot(H,N)=1` geometry while the structurally identical `skinned3d` specular
  check at the same geometry passes (a genuine asymmetry, not yet root-caused to a specific shader
  line); (b) `skinned3dvertexlitcolored` produces green instead of the expected black for an
  explicitly black-colored vertex, meaning vertex color is not correctly applied for the
  skinned+vertex-lit+vertex-color effect combination. Both confirmed genuine via real DXVK-backed
  execution (not a WineD3D fallback). See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **D3D12: a real testing-coverage gap** — only ONE CTest exists for this entire backend
  (`D3D12_Smoke`), so this audit's two most significant D3D12 static-review findings (Stencil/
  Scissor/DepthBias inertness; `OcclusionQuery` multi-draw overwrite) have no dedicated test to
  confirm or refute at runtime — a gap distinct from the bugs themselves. `D3D12_Smoke` itself is
  exceptionally rigorous (220/220 checks pass under real vkd3d-proton) but doesn't reach either bug's
  actual trigger condition. See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **SdlGpu: custom `ShaderEffect`/CNJ GLSL content that works on EasyGL is rejected outright** by
  SdlGpu's stricter SPIR-V-based shader pipeline (missing `#version 310 es`+, missing explicit
  `location` qualifiers, non-block uniforms) — a real cross-backend compatibility gap for
  user-authored effect content, not merely a test-fixture issue. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **Bgfx: XNA's real default cull mode (`CullCounterClockwiseFace`) fails to cull anything** —
  confirmed via 2 independent test files, ruling out coincidence. Concrete root-cause hypothesis: the
  same `glFrontFace`-relativity issue already discovered and fixed for stencil (Task 763) may also
  apply to culling, just not yet fixed there. See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **Task 868 (Vulkan `BlendState` hardcoding) is CONFIRMED FIXED, correcting this audit's own
  earlier static-review finding** — all 6 blend-state test executables now pass cleanly, including
  every case the documented per-check failure predictions said should fail. `ApplyBlendState`'s own
  source comment confirms the fix landed since this audit's static review.
  `AUDIT_GRAPHICS_BACKEND_MATRIX.md`'s Stencil+Scissor+DepthBias row and `cmake/Tests/
  VulkanTests.cmake`'s own per-check predictions need updating. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).

**Graphics backends and shared XNA-facing graphics code:**

- **Fog formula backwards (mirrored) in Bgfx, Vulkan, and the entire shared `D3DCommon` shader library
  (D3D11+D3D12) — all 15 of D3DCommon's fog-capable shaders, the single widest-reaching shader-level defect in
  this audit.** Correct FNA formula: `(z+FogEnd)/(FogEnd-FogStart)`; these backends compute
  `(FogEnd-z)/(FogEnd-FogStart)`. Fixed in EasyGL pre-session (Task 1111); never ported to the other three
  backend-groups. Several shaders' own header comments falsely cite "EasyGL's established formula" as their
  precedent — a likely propagation mechanism (a later port copied a prior *wrong* instance while believing it
  matched EasyGL's since-fixed one). See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Systematic FNA parity gaps).
- **`SkinnedEffect`'s world-space normal transform is completely missing across every one of the 14 backends
  that implement `SkinnedEffect`** — EasyGL, WebGPU, Vulkan, SdlGpu, Bgfx, D3D11+D3D12 (shared `D3DCommon`, all
  5 skinned shaders) all confirmed at the shader-source level; propagation explicitly traceable via
  self-documented "ported from EasyGL/Vulkan line-by-line" comments in at least 3 of them. A related, narrower
  "raw World instead of inverse-transpose" variant (rather than complete omission) is separately confirmed in 6
  instances: EasyGL/`EnsurePbrSkinnedProgram`, SdlGpu, Bgfx, D3D9's custom `PbrSkinned3D.hlsl`, and D3D11+D3D12's
  shared `pbr_skinned3d.vert.hlsl`. Any rotated skinned model's lighting is wrong; invisible to every existing
  test because they all use `World=Identity`. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **SdlGpu: fog is completely unimplemented across all 10 stock-effect shader families** — not a wrong formula,
  a total absence (zero fog identifiers anywhere in 23 `.glsl` files or the C++ backend), confirmed deliberate
  via the shader's own "No fog, deliberately deferred" comment. `GraphicsDevice.FogEnable`/`BasicEffect.
  FogEnabled`/`FogColor`/`FogStart`/`FogEnd` have zero visible effect on this backend. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **D3D12: `StencilState` (all fields) and `RasterizerState.ScissorTestEnable`/`DepthBias`/`SlopeScaleDepthBias`
  are completely non-functional** — parameters received as literally-commented-out unused, never forwarded;
  every PSO hardcodes `StencilEnable=FALSE`/leaves `ScissorEnable=FALSE`. A real regression relative to D3D11.
  Honestly disclosed in-code as a scope cut, not hidden — but 2 commonly-used XNA features are fully inert. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **D3D12: `OcclusionQuery` only captures the last draw call when multiple draws occur between `Begin()`/
  `End()`** — every draw-recording method wraps its own `BeginQuery`/`EndQuery` on the same query-heap slot, so
  a 2nd draw's query overwrites the 1st's result. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Vulkan: `SpriteBatch.Begin(transformMatrix)`'s transform is silently dropped** — the only one of 14 checked
  backends (12 real implementations plus Ascii's delegation) that doesn't apply it. No test anywhere exercises a
  non-Identity transform on Vulkan. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Vulkan: the missing-Y-flip mirroring bug, previously known only for `EnvironmentMapEffect`, also affects
  `PbrEffect`, `SkinnedPbrEffect`, and `InstancedEffect`** — one of the four omits the flip while its own
  in-source comment falsely claims a sibling shader "never Y-flips" (that sibling does, and its own comment says
  so). 4 of Vulkan's effect-shader families render vertically mirrored. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Vulkan: `GraphicsDevice.ScissorRectangle` is completely non-functional whenever a `RenderTarget2D`/
  `RenderTargetCube` is bound** — hardcodes full-target scissor for every RT pass; only the backbuffer pass
  correctly checks `scissorEnabled_`. Unlike the paired Viewport-when-RT-bound limitation (explicitly disclosed
  in-source), this gap has no disclosure anywhere near the scissor code. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`EnvironmentMapEffect`'s fragment shader re-multiplies `EmissiveColor` by `DiffuseColor` instead of adding it
  unscaled (FNA's real `Lighting.fxh` convention) — confirmed in 5 backends: Bgfx (original source), WebGPU,
  Vulkan, SdlGpu, and D3D11+D3D12** (shared `D3DCommon`, also explicitly "ported line-by-line from Vulkan"). All
  masked because no test varies `DiffuseColor` away from white or `EmissiveColor`/`AmbientLightColor` away from
  black. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`SkinnedEffect::FillGpuDrawParams()`'s `ambientColor`/`emissiveColor` are misconsumed by 2 backend-groups.**
  Root cause resolved: the C++ layer deliberately pre-folds `AmbientLightColor` into `emissiveColor` for skinned
  draws (confirmed correct via 4 independent backends' own consumption code — EasyGL, Bgfx, SdlGpu, D3D9 stock
  effects). Vulkan's skinned shaders read the wrong (always-zero) `ambientColor` field instead and have no
  `emissiveColor` slot at all; D3D11+D3D12's shared skinned fragment shaders likewise have no `EmissiveColor`
  cbuffer field (narrower — `AmbientColor` IS correctly present there). See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **WebGPU: `SpriteBatch`'s clip-space mapping is always backbuffer-relative, never render-target-relative** —
  drawing into an off-screen target of a different size mis-maps sprite placement. See
  [audit report](examples/webgpu_rendertargetcube_test.cpp.audit.md) and `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Bgfx: `EnsureViewState()` unconditionally clears color+depth+stencil on every `Clear*()` call regardless of
  the requested `ClearOptions`** — a stencil-only clear silently wipes color and depth too. See
  [audit report](examples/bgfx_graphicsdevice_clear_stencil_test.cpp.audit.md).
- **EasyGL: a constructor failure after `RegisterForWindow()` but before construction completes leaves a
  dangling entry in a static window registry**, later dereferenced unconditionally by `SdlInputBridge.cpp`/
  `Mouse.cpp` on the next mouse/input event — a real, reachable use-after-free. **The most severe confirmed
  finding in this entire audit.** See
  [audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F1.
- **D3D9's own custom (non-vendored) `PbrSkinned3D.hlsl` shares the skinned-normal-transform bug** (its
  *vendored* stock effects do not); separately, D3D9's own custom shaders (`SkinnedVertexColor3D.hlsl`,
  `Pbr3D.hlsl`, `PbrSkinned3D.hlsl`) share a *second*, distinct "object-space-only fog" defect — fog computed
  from raw local-space Z ignoring World/View, unlike this same backend's own correct `ComputeFogVectorEXT()`
  used for every vendored stock effect. See [audit report](examples/d3d9_pbr_test.cpp.audit.md) and
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`Bgfx_vertex_format_test.cpp`'s entire subject (`BgfxVertexFormatHelper.hpp`) is never called by production
  code** — real `MakeBgfxLayout()` dispatches on hardcoded byte-size instead; the test's own `UploadAndCheck()`
  never calls `SetData`. See [audit report](examples/bgfx_vertex_format_test.cpp.audit.md).

**Recurring architecture pattern:**

- **State-mutation-before-fallible-call, 3 confirmed instances**: `SpriteBatch::Begin()` sets `begun_=true`
  before backend calls that can throw, permanently wedging the object on failure (found via
  `sdlrenderer_custom_effect_throws_test.cpp`); `GraphicsDevice::SetRenderTargets` mutates tracked MRT-binding
  state before the backend call that actually throws (found via `sdlrenderer_rendertargets_mrt_throws_test.cpp`);
  `IGraphicsBackend`'s window registry has the identical shape (EasyGL F1, above). A genuine, repeated authoring
  pattern, not three coincidences. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

**`Microsoft::Xna::Framework::Graphics` (shared XNA-facing layer, affects every backend uniformly):**

- **`SpriteFont::MeasureString`/`SpriteBatch::DrawString` dereference an `unordered_map::end()` iterator with no
  check, reachable via fully public API** — setting `DefaultCharacter` (no validation) to a character absent
  from the font's own map, then measuring/drawing a genuinely-missing glyph, is undefined behavior. FNA throws
  `KeyNotFoundException` in the equivalent case. See
  [audit report](examples/sprite_font_test.cpp.audit.md) and `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`SpriteBatch::DrawString()`'s axis-direction lookup tables are sized for only 3 entries, but real XNA's
  `SpriteEffects` is a composable `[Flags]` enum with a valid 4th combined value** — an out-of-bounds stack
  read. FNA's own tables have 4 entries. CNA's `SpriteEffects` is missing the `operator|` overload other flag
  enums have, but this doesn't prevent the combined value being constructed via `static_cast` (already used at
  `examples/sdlgpu_2d_test.cpp:126`). See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`EffectParameter`'s Matrix Get/Set/Transpose semantics are inverted relative to FNA across all 8
  Matrix-related methods.** Cross-validated: the sibling `EffectAnnotation::GetValueMatrix()` implements the
  identical FNA formula *correctly*, proving the right convention was known elsewhere in this codebase — a
  transcription slip, not a design choice. Exposure: custom/user-authored `Effect`s using the generic accessors
  directly (stock effects bypass `EffectParameter` entirely). See
  `src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp.audit.md`.
- **`EffectParameter::Elements`/`StructureMembers` are permanently empty** — nothing anywhere populates them;
  array/struct-typed custom parameters silently report zero sub-elements. See
  `include/Microsoft/Xna/Framework/Graphics/EffectParameter.hpp.audit.md`.
- **`BasicEffect` never populates its own `Effect::Parameters` collection at all** — unlike every sibling stock
  effect. Rendering is unaffected, but the standard `effect.Parameters["X"]` generic access silently returns
  nothing for the single most commonly used stock effect in the API. See
  `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp.audit.md`.
- **`VertexBuffer`/`IndexBuffer` have no destination-byte-offset concept anywhere in their public `SetData`
  API — the confirmed root cause of an `IVertexBufferBackend`/`IIndexBufferBackend::SetDataWithOptions()` gap
  independently found in 3 backends (D3D11, EasyGL, D3D9).** Real FNA exposes a genuine `offsetInBytes`
  overload; this port dropped it entirely, making real ring-buffer/streaming `NoOverwrite` usage architecturally
  impossible on *any* backend, not just the 3 where a backend audit happened to trip over the symptom. See
  `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp.audit.md`.
- **`GraphicsDevice.cpp` has ~27 raw `std::runtime_error`/`std::invalid_argument` throws**, inconsistent with
  the same file's own correct `System::*Exception` use at 13 other sites — the largest single-file instance of
  this audit's recurring exception-type pattern, in the framework's single most central class. See
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp.audit.md`.

**Outside the graphics layer:**

- **`CNA::Logger::ToSDLPriority()` (`src/CNA/Logger.cpp`) mistags every `Fatal`/`Error`/`Warn` log call as
  `SDL_LOG_PRIORITY_INFO`** — the switch's real cases are commented out with a literal `//todo`. Also breaks
  `SetMinimumLevel()`, which routes through the same function. Unlike every other finding in this audit, `Logger`
  is foundational, always-compiled, project-wide infrastructure. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`FileDialog.cpp` and `MessageBox.cpp` (`cna-devices`) share a real use-after-free window**: a swappable
  global backend pointer's mutex is released before the returned pointer is dereferenced; a concurrent
  `SetBackendForTesting()` can free the object out from under an in-flight call. `SystemTray`/`Camera` avoid
  this via per-instance constructor-injected backends instead. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`StorageDevice::DeleteContainer()` performs a real, unchecked recursive filesystem delete
  (`fs::remove_all`) driven directly by a caller-supplied `titleName` string, with zero path-containment
  validation** — and, unlike every other "missing containment check" finding this session, this is NOT a
  faithful FNA reproduction (FNA's own method is an unimplemented `NotImplementedException` stub; CNA chose to
  actually implement it). `DeleteContainer("../../../SomeOtherAppData")` or an absolute path resolves outside
  the storage root and is **deleted**, not just read. A genuine, CNA-introduced path-traversal data-loss
  vulnerability reachable from public API — arguably the most severe finding in this audit session. See
  `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`.
- **`ENetBackend.cpp`'s `HandleReceive()` dispatches host-only broadcast messages
  (`ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast`) with no check that the
  sending peer is this session's authoritative host** — any connected peer (a modified/custom client speaking
  this fully-inferable wire format, no MITM needed) can forge these to the host: kick arbitrary gamers, inject
  fake gamers, or force an arbitrary state change. Orthogonal to the subsystem's extensive prior
  lifecycle/bookkeeping remediation history, none of which covered an adversarial-client threat model. See
  `src/CNA/Internal/Net/ENetBackend.cpp.audit.md`.
- **`AudioTagParser.cpp`'s ID3v2.3 and FLAC-picture-block length validation uses `pos + len > bound`-style
  bounds checks vulnerable to unsigned-integer-overflow wraparound (32-bit `size_t` targets only)** — safe on
  64-bit desktop, but a crafted `.mp3`/`.flac` in the user's Music library could trigger a genuine
  out-of-bounds heap read on a 32-bit build (e.g. Android armeabi-v7a). Contrast with the sibling `XactParser.cpp`,
  explicitly hardened against this exact class per a cited external audit. See
  `src/CNA/Internal/Media/AudioTagParser.cpp.audit.md`.
- **`TextureCubeContentTypeReader.cpp` is missing the byte-count-vs-pixel-count validation both sibling readers
  correctly have** — a crafted `.xnb` TextureCube asset with an undersized declared byte count triggers a
  genuine out-of-bounds heap read (crash or heap-memory disclosure via pixels uploaded to the GPU). A clear
  porting omission, not an intentional scope difference. See
  `src/CNA/Internal/Xnb/TextureCubeContentTypeReader.cpp.audit.md`.
- **`ContentReader::ReadExternalReference<T>()`'s documented "rejected outright" containment guarantee has a
  real absolute-path bypass** — `ResolveRelativeAssetPath()` only rejects a resolved path that is exactly `".."`
  or begins with `"../"`; an absolute-path external reference (e.g. `/etc/passwd`) in a crafted `.xnb`/`.cnj`
  file sails through unchanged, and `ContentManager::BuildAssetPath()` doesn't re-contain it downstream. Not
  FNA-faithful (FNA's own method has no containment check at all) — a disclosed CNA addition that's incomplete.
  The 3rd confirmed instance this session of the same `fs::path` concatenation pitfall (alongside
  `StorageDevice`). See `include/Microsoft/Xna/Framework/Content/ContentReader.hpp.audit.md`.
- **`GameTests.cpp` and `GraphicsDeviceManagerTests.cpp` both have zero real test coverage** (each a 2-line stub
  citing "requires a live SDL window") — leaves the two confirmed production HIGH bugs (`Game::UnloadContent()`
  dead hook; `GraphicsDeviceManager` device-event-forwarding gap, both below) completely untested by CI.
  `GameWindowTests.cpp` demonstrates the correct alternative already used elsewhere in the same directory
  (attempt a real SDL window, `GTEST_SKIP()` when unavailable). See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`Game::UnloadContent()` is a dead virtual lifecycle hook** — declared with an empty default body exactly
  like FNA's, but never invoked anywhere. FNA's real `Initialize()` subscribes `graphicsDeviceService.
  DeviceDisposing += UnloadContent`; CNA's `Initialize()` never performs this subscription (confirmed via
  whole-repo grep: exactly 2 hits, declaration + empty body, no call site). A game overriding `UnloadContent()`
  per the documented XNA lifecycle contract silently never has it called. See
  `include/Microsoft/Xna/Framework/Game.hpp.audit.md`.
- **`GraphicsDeviceManager` never subscribes to its own `GraphicsDevice`'s `DeviceResetting`/`DeviceReset`/
  `Disposing` events**, unlike FNA's real `IGraphicsDeviceManager.CreateDevice()`. `GraphicsDevice.cpp`'s own
  `deviceEventCallback` mechanism can raise a genuine backend-detected device-lost/reset cycle completely
  outside any `GraphicsDeviceManager` call — since `GraphicsDeviceManager` never forwards it, that real event
  silently never reaches `IGraphicsDeviceService` listeners, even though `GraphicsDevice` itself correctly
  raised it. See `include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp.audit.md`.

### MEDIUM

**Graphics backends:**

- **SdlGpu backend: constructor resource leak if any of 10 sequential shader/pipeline-creation calls throws** —
  unlike WebGPU's model-example try/catch+cleanup, no exception-safety wrapper exists around
  `CreateSpriteResources` through `CreatePbrResources`. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`BasicEffect::VertexColorEnabled` is a bare public field with no property wrapper**, violating this
  project's own C# property convention — confirmed 3 times across Bgfx, Vulkan, and generic test audits
  exercising the same production code. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **4 known-failing CTests registered with no `WILL_FAIL`/skip annotation**: `Bgfx_RenderTargetCube_DepthFormat`
  (Task 952, still open), `Bgfx_SkinnedEffect_WeightsPerVertex` (pre-existing since before commit `0cb4a591`),
  `EasyGL_AvatarRenderer_TintRouting` (independently re-confirmed failing by direct build+execution during
  synthesis — the `Vulkan_AvatarRenderer_TintRouting` sibling passes only by coincidence, per the Vulkan
  ambient/emissive HIGH finding above), and **`EasyGL_GraphicsDevice_ReferenceStencil`** (Pass 6, new: disclosed
  in-comment as "a documented known failure" since Task 319/872, confirmed still genuinely failing and still
  has no `WILL_FAIL` property backing that disclosure). See `AUDIT_CROSS_CUTTING_FINDINGS.md` (CI-masking risk,
  Pass 6). **Reframed by a Pass 6 systemic check**: `grep -rn "WILL_FAIL" cmake/Tests/*.cmake
  cmake/UnitTests.cmake` returns zero matches anywhere in the project — these 4 are not independent
  oversights, this project has simply never adopted CTest's expected-failure mechanism at all, for any
  backend.
- **`MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` SEGFAULTs rather than failing cleanly** (Pass 6)
  when the picture-library scan's root result is null/empty — a sibling test in the same fixture fails cleanly
  on the identical condition, so some downstream object-graph-walking code dereferences the null without a
  check. A real defensive-programming gap, independent of whatever causes the scan to come up empty in a given
  environment. See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Pass 6).
- **Two SDL_Renderer tests have stale expected-throw assertions superseded by a real, intentional FNA-parity
  fix** (commit `90f5db2c` made `Clear(ClearOptions,...)` degrade silently instead of throwing on backends with
  no real depth/stencil buffer; production code is correct, tests weren't updated). See
  [audit report](examples/sdlrenderer_clearoptions_audit_test.cpp.audit.md).
- **Headless: `HeadlessStatistics::primitiveCount` undercounts instanced draws by a factor of `instanceCount`.**
  See [audit report](src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp.audit.md) F1.
- **Software: `DepthBufferWriteEnable` has no effect (depth always written), and `DepthBufferFunction` is
  ignored (hardcoded to LessEqual)** — two findings, same method (`ApplyDepthStencilState`). See
  [audit report](src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp.audit.md) F1/F2.
- **Dx3: a failed resize destroys the working primary/backbuffer surfaces before confirming the replacement
  succeeds**, leaving the backend permanently unusable on any subsequent draw call. See
  [audit report](src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp.audit.md) F1.
- **EasyGL: `SkinnedPbrEffect`'s shader uses the raw `uWorld` matrix instead of the inverse-transpose normal
  matrix** — correct only for rotation/uniform-scale, wrong for non-uniform scale. See
  [audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F3.
- **Recurring mip-regeneration shape across 2 backends, 2 resource types: cube mip regen always touches all 6
  faces even when only one changed** — SdlGpu's `TextureCube::SetData()`, D3D11's `RenderTargetCube`. Positive
  counter-example: D3D12's `RenderTargetCube` correctly regenerates only the active face.
- **Architecture-level, likely universal: `IGraphicsBackend::ApplySamplerState()` has no `AddressW` parameter;
  `ApplyBlendState()` carries no per-RT color-write mask; `ApplyRasterizerState()` carries no
  `MultiSampleAntiAlias` flag** — all confirmed real, correct XNA-facing properties silently unenforceable at
  the shared backend-interface level, not a per-backend defect. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **D3D12: every vertex/index-buffer `SetData` performs a full synchronous GPU stall regardless of
  `SetDataOptions`** — `options` is a literally-unused parameter; every other backend at least attempts a
  no-stall path. Performance, not correctness. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

**`Microsoft::Xna::Framework` core / Graphics (XNA-facing, affects every backend uniformly):**

- **`Texture2D::GetTypeName()` returns bare `"Texture2D"`** instead of the fully-qualified name every sibling
  correctly returns. **`RenderTargetCube` lacks `RenderTarget2D`'s own Task 717 `Dispose(bool)` fix** — a real
  use-after-free risk in the identical pointer pattern. **`DynamicVertexBuffer`/`DynamicIndexBuffer` don't
  override `GetTypeName()`** either — same shape, 3rd instance.
- **`RenderTargetBinding`'s two-argument constructors have zero validation** (FNA throws for null texture/
  invalid `CubeMapFace`), and it carries an undisclosed, non-`NOXNA`-tagged `arraySlice` extension.
- **`TextureCollection` is missing FNA's real render-target/sampler-conflict check** (binding a texture that's
  simultaneously an active render target silently succeeds instead of throwing).
- **`VertexBufferBinding.VertexOffset` is modeled as a vertex-count offset, not FNA's real byte offset.**
- **`ModelMeshPartCollection`/`ModelEffectCollection::operator[](int)` perform unchecked `std::vector::
  operator[]` indexing** where FNA's `ReadOnlyCollection<T>` always bounds-checks — same shape as the confirmed
  `NetworkSessionProperties` bug below. `Model.cpp` has 5 raw `std::out_of_range`/`std::runtime_error` throw
  sites where FNA documents `System.*` exception types.
- **`PackedVector/Byte4.hpp`, `Short2.hpp`, `Short4.hpp` all truncate instead of round in `Pack()`** — a
  systematic off-by-up-to-1 error for any non-integer input. **Root cause of why this went undetected**: the
  project's own FNA-comparison harness (`tools/fna-reference/PackedVectorReference.cs`)'s `DumpByte4()`/
  `DumpShort2()`/`DumpShort4()` use only integer test inputs, unlike all 14 sibling `Dump*()` functions, which
  deliberately include a fractional value — for an exact-integer input, round and truncate agree, so the
  harness was structurally incapable of catching this. See
  `audit/tools/fna-reference/PackedVectorReference.cs.audit.md`.
- **`VertexPositionColor.hpp` does not implement `IVertexType`** — unlike real FNA and every sibling type.
- **`GraphicsDevice::Dispose()` disposes owned resources *before* raising `Disposing`**, inverted from FNA's
  real order — a `Disposing` handler can never observe a still-valid resource.
- **`DisplayMode` is missing `TitleSafeArea`, `GetHashCode()`, and `ToString()`** — all real, documented FNA
  members. **`DeviceLostException`/`DeviceNotResetException`/`NoSuitableGraphicsDeviceException` all derive from
  `std::runtime_error` instead of `System::Exception`**, missing the `(message, innerException)` constructor.
  **4 `EffectXxxCollection::operator[](int)` implementations throw raw `std::out_of_range`** instead of
  `System::ArgumentOutOfRangeException` (bounds-checking itself is correct in all 4).
- **Exception-type convention violated in `SkinnedEffect.cpp` (4 sites), `EnvironmentMapEffect.cpp`,
  `PbrEffect.cpp`, `SkinnedPbrEffect.cpp` (4 sites), `SamplerStateCollection.cpp`, and pervasively across the
  `Texture*` family** (`Texture2D.cpp` alone has ~15+ raw-`std::`-exception sites) — the same recurring
  project-wide pattern, largest concentration found in this shard.
- **`Color::PackFromVector4()` uses an unclamped `static_cast<bytecs>(float)` conversion** — real UB for
  out-of-range/NaN input, unlike every sibling float-to-component path in the same file, which correctly clamps
  first. See `src/Microsoft/Xna/Framework/Color.cpp.audit.md`.
- **A signed-integer-overflow-UB fix applied to `Vector2::GetHashCode()` was never propagated to 4 structurally
  identical siblings**: `Vector3`, `Vector4`, `Quaternion`, and `Matrix::GetHashCode()` (16 terms — the
  highest-risk instance). `Point`/`Rectangle` (XOR-combining) are unaffected. See
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`Matrix::Invert()` computes every intermediate cofactor determinant in single-precision `float`, unlike
  FNA's deliberate double-precision implementation** — the port's own comment asserts "no observable difference
  in practice" without demonstrating it. See `src/Microsoft/Xna/Framework/Matrix.cpp.audit.md`.
- **`GameWindow::EndScreenDeviceChange` never centers/repositions the window onto the named display**, and the
  orientation model substitutes an unconditional window-aspect-ratio heuristic for FNA's real mobile-gated
  SDL-display-orientation mechanism — both confirmed via direct comparison against FNA's actual platform
  implementation, neither disclosed in `GameWindow.hpp`'s doc comments. Concretely reachable: `GraphicsDeviceManager`
  genuinely calls `EndScreenDeviceChange` with the real adapter name during normal operation.
- **`Game::PollEvents()` omits four real FNA SDL3 event reactions**: `WINDOW_MOVED` (cross-display move ->
  `GraphicsDevice.Reset()`), `WINDOW_EXPOSED` (redraw during blocking resize-drag), `ENTER/LEAVE_FULLSCREEN`
  (sync `IsFullScreen` back from OS-level toggles), `MOUSE_ENTER/LEAVE` (screensaver toggle — CNA disables it
  unconditionally from an unrelated call site instead). See `src/Microsoft/Xna/Framework/Game.cpp.audit.md`.
- **3 independent instances in `xna-framework-core` of a raw `std::` exception thrown where a project-provided
  `System::*Exception` is available and already used elsewhere**: `GameComponentCollection::SetItem()` (claims
  `NotSupportedException` "isn't available yet" — it is, used by 16 other files), `GraphicsDeviceManager`'s
  constructor/`registerServices()` (`std::invalid_argument` vs. `ArgumentNullException`/`ArgumentException`),
  `Game::AssertNotDisposed()` (`std::runtime_error` vs. `ObjectDisposedException`, used by 28 other files).

**Content / Storage / Net / GamerServices / Devices / Media:**

- **`NetworkSessionProperties::Insert(int)`/`RemoveAt(int)` perform unchecked iterator arithmetic**, unlike
  every other index-taking member in the same file — UB for an out-of-range index, reachable from public API.
  See `src/Microsoft/Xna/Framework/Net/NetworkSessionProperties.cpp.audit.md`.
- **`GamerPresence.cpp`'s `presenceModeStrings_` table is sorted alphabetically, not indexed to
  `GamerPresenceMode`'s declared ordinals** — 59 of 60 modes resolve to the wrong display string. Currently
  dormant (no public getter exposes it). **`GamerServicesComponent` doesn't override `GetTypeName()`**
  (confirmed isolated — sibling `DrawableGameComponent` gets it right). **`GuideAlreadyVisibleException` is
  fully implemented/tested but dead code** — real guards throw a generic `InvalidOperationException` instead.
  **`PropertyDictionary`'s entire 9-method read-accessor surface throws raw `std::out_of_range`/
  `std::bad_any_cast`** instead of `System::Collections::Generic::KeyNotFoundException`/
  `InvalidCastException` — the single largest-blast-radius instance of the exception-type pattern in this shard.
- **`Dispose(bool disposing)` is re-declared `public` (not `protected`) in all four `Microsoft::Devices::Sensors`
  classes** (`Accelerometer`, `Compass`, `Gyroscope`, `Motion`) even though the base `SensorBase<T>` correctly
  declares it `protected` — any external caller can invoke `accel.Dispose(false)` directly, marking the object
  disposed without running any real cleanup (no `Stop()`, no SDL-subsystem release). A real, externally-reachable
  resource leak plus a permanently-broken object. See
  `include/Microsoft/Devices/Sensors/Accelerometer.hpp.audit.md` (and 3 sibling reports).
- **`PlaylistParser.cpp` performs no path-containment check on `.m3u`/`.m3u8` playlist entries** — accepts
  absolute/`..`-escaping paths as-is, inconsistent with this same shard's own established containment defenses
  elsewhere (`CnjSourceFile.hpp`, `SavedPictureStore.cpp`). See `src/CNA/Internal/Media/PlaylistParser.cpp.audit.md`.
- **`VideoDecoder.cpp`'s `ConvertFrameToRGBA()` indexes a decoded frame using stale cached `width_`/`height_`
  rather than the frame's own dimensions** — a potential OOB read if a video stream changes resolution
  mid-decode. Notable: this file otherwise has the densest prior-review-fix documentation in the whole audit
  (18+ cited findings) yet doesn't address this case. See `src/CNA/Internal/Media/VideoDecoder.cpp.audit.md`.
- **`MediaLibrary::SavePicture(name, Stream*)` assumes a single `Read()` call fills the whole buffer**,
  violating this project's own `Stream::Read()` interface contract (a legitimate partial read leaves the
  trailing buffer portion zeroed and silently saved). See `src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp.audit.md`.
- **Duplicate NOXNA-extension API surfaces across `CNA::Input` and `CNA::Devices`**: `Clipboard` and
  `Power`/`PowerState` each have two entirely independent implementations wrapping the identical SDL3 calls,
  with different naming conventions and minor behavioral differences (return-value discarding). Both
  individually correct, but a fix to one has no reason to reach the other. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

**Testing / documentation:**

- **`GameTests.cpp`/`GraphicsDeviceManagerTests.cpp` zero coverage, `GameCrashTest.cpp` dead file** (all 24
  lines commented out behind a stale `#ifdef XNA5` gate referencing an API shape the current `Game` class
  doesn't expose).
- **`PictureLibraryIndexTests.cpp` has no symlink-cycle or permission-denied-subdirectory test**, unlike the
  equivalent music-scanner tests in the same shard.
- **`EffectParameterTests.cpp` and `GraphicsExceptionTests.cpp` actively bake in 2 of the HIGH findings above as
  asserted-correct**: `SetValueTransposeRawLayoutDiffersFromSetValue` asserts the exact inverse of FNA's real
  Matrix convention; `GraphicsExceptionTests.cpp` has 6 tests asserting the 3 graphics exceptions inherit as
  `std::runtime_error`. Fixing either production bug now requires updating the corresponding tests in the same
  change. A 3rd instance of this shape: `GamerServicesDataTests.cpp` bakes in `PropertyDictionary`'s raw-
  exception assertions. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`ContentReaderExternalReferenceTests.cpp` has no test for the confirmed absolute-path-escape gap above** —
  every existing test only constructs a relative `..`-style escape.
- **7 `docs/*.md` staleness findings**, each a doc's claim directly contradicted by this session's own confirmed
  current state: `docs/coverage.md` claims ".xnb support entirely absent" (contradicted by 10+ audited readers);
  `docs/d3d9-backend.md` vs. `docs/cnatests-mingw-setenv-proposal.md` contradict each other on the same task ID;
  `docs/cna_audio_deep_audit_2026-07-17.md` has no status banner noting its flagship finding is now fixed;
  `docs/dx3-backend.md` claims SpriteBatch "fully verified" (contradicted by the Dx3_SpriteBatch finding below);
  `docs/easygl_bugs.md`'s fog-bug row mischaracterizes the current (object-space) shader as clip-space;
  `docs/gdm-coverage.md` never mentions the `GraphicsDeviceManager` event-forwarding gap above;
  `docs/graphics-resource-lifetime.md` and `docs/graphicsresource-fna-audit.md` directly contradict each other
  on whether a resource-tracking list exists. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Repo-hygiene: root `.gitignore`'s bare `build*` pattern (line 1) silently matches any file/directory
  anywhere in the repo whose basename starts with "build"** — including this very audit's own
  `build-cmake.md`/`build-cmake-tests.md` manifest files, which were silently invisible to plain `git add`/
  `git status` until forced with `git add -f`. A real, currently-live repo-hygiene hazard project-wide, not
  just inside `audit/`.
- **Two independent SPDX/license-header inconsistencies**: the entire `CNA::Internal::Net` subsystem (12 files)
  uses MIT + an explicit copyright line, diverging from every other CNA-original NOXNA file's plain MS-PL; all
  three `xna-storage` file pairs have an *intra-pair* mismatch (`.hpp` MS-PL, `.cpp` MIT+copyright).

**Pass 6 continued — new MEDIUM findings (build/test verification, all 14 backends):**

- **Vulkan: `SkinnedEffect`+Fog renders completely black, including the fog-disabled control case**
  — not the already-documented mirrored-fog-formula bug (which produces a wrong-but-non-black
  color); an apparent silent pipeline-creation or descriptor-binding failure in the skinned3d+fog
  pipeline specifically. Not root-caused further.
- **Texture3D content-reader round-trip returns all-zero/garbage data instead of real pixel
  values**, reproduced identically on Software AND Headless (possibly SdlRenderer too) — likely
  shared CPU-side code (the XNB/CNJ Texture3D reader, or `Texture3D`'s own generic
  `GetData`/`SetData` path), not a per-backend GPU-texture defect. Not yet checked on
  EasyGL/Vulkan/other backends.
- **`GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` fails identically on 5 backends now**
  (Vulkan, SdlGpu, Bgfx, Software, Headless) — for Vulkan/SdlGpu/Bgfx this is a confirmed
  test-authoring bug (the test wrongly hardcodes an EasyGL/GLES3-specific limitation as universal;
  these 3 backends genuinely support wireframe). For Headless specifically (which does zero real
  GPU work yet reports `true`), the direction is less clear — may indicate a shared
  `IGraphicsBackend` default capability flag rather than 5 independent per-backend mistakes.
- **`SDL_Renderer_FullscreenToggle` crashes with an uncaught `std::runtime_error`**
  ("`ReadBackbuffer`: physical/logical size mismatch") during a fullscreen toggle, terminating the
  whole test process instead of failing as a clean, catchable assertion.
- **`docs/webgpu-backend.md`'s `WebGPU_Msaa` "intentionally left failing" characterization is
  stale** — directly confirmed passing 100%; the underlying test-authoring bug (missing
  `RasterizerState::CullNone`) was already fixed 2026-07-18 (`WEBGPU-58`). Separately,
  `CLAUDE.md`'s own WebGPU capability summary now *understates* the backend relative to
  `docs/webgpu-backend.md` and the confirmed-working 23/23 test suite (real PbrEffect/
  SkinnedEffect/EnvironmentMapEffect/instancing/RenderTarget all working, not just the documented
  "Texture2D/SpriteBatch baseline") — understating, not overclaiming, but still worth syncing.
- **All 16 concrete `PackedVector` types entirely lack `Equals()`/`GetHashCode()`/`ToString()`**
  (Pass 3) — confirmed present and non-trivial in FNA, and confirmed present on sibling value types
  elsewhere in CNA (`GamePadState`/`MouseState`/`KeyboardState`/`Vector2`/`Color`), making this an
  isolated CNA gap in one type family, not a project-wide convention or an FNA-inherited omission.
  `operator==`/`!=` cover direct comparisons, so nothing is silently wrong — but code using a
  `Byte4`/`Short2`/etc. in a hash-based container, or debug-printing one, simply won't compile.

**Resolved standing investigations:**

- **`Dx3_SpriteBatch`'s 2/10-failing-checks (persistent project memory) — EMPIRICALLY CONFIRMED in
  Pass 6, not just statically predicted.** Both hypotheses now directly verified by running the
  actual binary: Check D (zero-alpha blend) fails, confirming the test-authoring-bug hypothesis (the
  fixture uses a non-premultiplied `Color(255,0,0,0)` under `BlendState::AlphaBlend`, a
  premultiplied-convention preset — hand-derived real result `(255,6,7)`, not the asserted
  "destination untouched"). Check G (180° rotation) fails, confirming the real-backend-defect
  hypothesis (test math independently re-derived and confirmed sound). New lead: the rotation
  formula is a byte-for-byte verbatim port of `SoftwareGraphicsBackend.cpp`'s identical code —
  possibly a shared, inherited defect rather than Dx3-specific. See `AUDIT_CROSS_CUTTING_FINDINGS.md`
  (Pass 6).
- **D3D11/D3D12's fog tests use `World=View=Projection=Identity`, so they cannot distinguish correct
  view-space fog from the already-confirmed EasyGL-class "object-space-only" bug** — a test blind spot, not a
  confirmation either way for these two backends. Empirically re-confirmed in Pass 6: D3D11's
  fog-at-boundary checks pass, as expected, since both the correct and mirrored formulas saturate
  identically at the exact `Z=FogEnd` boundary sampled — this does not contradict the mirrored-formula
  finding.

### LOW

- **Recurring test-authoring patterns from the `examples-tests-easygl` mechanical batch (218 files)**: stale
  "known bug" comments contradicted by later fixes (6+ files — Vulkan blend state, `SetReferenceStencil`
  availability, anisotropic filtering, `GetData()` availability, the pre-fix env-map formula), now confirmed as
  a systemic documentation-rot pattern recurring in the Bgfx/Vulkan/SdlRenderer batches too (9+ more instances,
  full list in `AUDIT_CROSS_CUTTING_FINDINGS.md`); tests asserting only metadata/capacity, not actual data
  content (`easygl_vertexbuffer_setdata_test.cpp`, `easygl_dynamic_buffer_stress_test.cpp`,
  `easygl_msaa_test.cpp`); weak enum coverage (`easygl_depthstencilstate_compare_function_test.cpp`, only 5/8
  `CompareFunction` values). Full per-file detail lives under `audit/examples/easygl_*.audit.md`.
- **A correct, well-mapped generic `VertexElementFormat` -> native-format helper header that is entirely dead
  code in production, confirmed in 2 backends**: `BgfxVertexFormatHelper.hpp`, `VulkanVertexFormatHelper.hpp` —
  real per-pipeline layouts are hardcoded per-stride/per-shader instead. Vulkan's own test for this at least
  directly unit-tests the mapping functions (genuinely verified); Bgfx's equivalent test silently exercises the
  same hardcoded layout regardless of declaration.
- **`SoundBank::GetCue()` returns a raw owning `Cue*` instead of `std::unique_ptr<Cue>`**, inconsistent with
  this codebase's own established ownership-transfer convention (functionally fine, header documents the
  contract).
- **`GamePadState`/`MouseState`'s `GetHashCode()` doc comments read as preserving an original FNA formula, but
  FNA's real implementation is `base.GetHashCode()`** (an opaque CLR default) — the custom formulas are
  necessary CNA inventions, not preserved ports, though they correctly satisfy the contract.
- **`SignedInGamerCollection::operator[](PlayerIndex)` checks only the upper bound**, not the lower — reachable
  only via a deliberately-misused explicit negative cast.
- **`SamplerStateCollection` has no per-slot dirty-tracking** (unlike FNA's real `modifiedSamplers`) —
  `GraphicsDevice::applySamplerStatesToBackend()` unconditionally re-applies all 16 slots every call. Pure
  performance divergence, no correctness impact.
- **`GraphicsResource` has no way to reassign `graphicsDevice_` after construction**, unlike FNA's real
  `internal set`-backed property. **`VertexDeclaration`'s auto-stride constructor doesn't validate an empty
  element list**, unlike FNA's real constructor.
- **A shared, narrow rounding-tie divergence** (round-half-up vs. FNA's banker's-rounding) affects `Alpha8`,
  `Bgr565`, `Bgra4444`, `Bgra5551`, `Rg32`, `Rgba1010102`, `Rgba64`.
- **`DecimalDateTimeContentTypeReaderTests.cpp`'s MSVC-only test/registration exclusion has no stated
  rationale**, unlike every other platform-conditional test in the same shard.
- **`tools/avatar_builder/generate_animations.py`'s top-of-file docstring is stale**, describing only 5 of the
  file's actual 31 built animation clips — resolves `validate_gltf.py`'s own flagged ambiguity in favor of the
  README being the stale artifact, not the validation gate.
- **`headless_resource_backends_test.cpp`'s Checks A/B are unconditional `check(true, ...)` with no
  `try`/`catch`**, unlike every other check in the same file — a real regression would crash the test process
  instead of reporting a clean `FAIL`.
- **`docs/devices-api-coverage.md`'s cross-cutting-members table doesn't flag the `Dispose(bool)` MEDIUM finding
  above; `docs/graphicsdevice-fna-audit.md` (dated 2026-06-26) independently re-verified still accurate today
  except one easy-to-miss inconsistency; `docs/README.md` predates several newer docs.**
- Positive notes preserved for context (not defects): `BoundingSphere::Contains(BoundingFrustum)`'s inability to
  return `Disjoint`, `BoundingFrustum::Intersects(Ray)`'s general-case `NotImplementedException`, and
  `Curve::ComputeTangent()`'s asymmetric near-zero epsilons are all confirmed **FNA-faithful** reproductions,
  not CNA regressions — flagged only because the port omits the explanatory comment FNA-fidelity findings
  elsewhere in this codebase usually carry.

## By subsystem

Same findings as "By severity" above, regrouped by which backend/subsystem owns the fix. A finding shared by
multiple backend-groups (e.g. the skinned-normal-transform sweep) is listed once per group it affects.

- **SdlGpu**: fog completely unimplemented (HIGH); constructor resource-leak on any of 10 sequential
  pipeline-creation calls throwing (MEDIUM); SkinnedEffect world-space-normal-transform omission (HIGH, part of
  the 6-backend-group sweep); EnvironmentMapEffect emissive-remultiply (HIGH, part of the 4-backend sweep).
- **D3D12**: `StencilState`/`ScissorTestEnable`/`DepthBias`/`SlopeScaleDepthBias` completely non-functional
  (HIGH); `OcclusionQuery` multi-draw-per-Begin/End only captures the last draw (HIGH); fog formula backwards,
  shared `D3DCommon` source, all 15 fog shaders (HIGH); SkinnedEffect world-space-normal-transform omission
  (HIGH); SkinnedEffect `EmissiveColor` dropped for skinned draws, shared `D3DCommon` (HIGH).
- **D3D11**: fog formula backwards, shared `D3DCommon` source (HIGH, same instance as D3D12); SkinnedEffect
  world-space-normal-transform omission (HIGH, shared source); SkinnedEffect `EmissiveColor` dropped for skinned
  draws (HIGH, shared source); EnvironmentMapEffect emissive-remultiply (HIGH, shared source). Positive: does
  NOT share Vulkan's `EnvironmentMapEffect` Y-flip bug; correct MRT-finalization fix (DX-143).
- **Vulkan**: `SpriteBatch.Begin(transformMatrix)` silently dropped, only affected backend (HIGH); missing-Y-flip
  across 4 effect families — EnvironmentMapEffect, PbrEffect, SkinnedPbrEffect, InstancedEffect (HIGH);
  `SkinnedEffect::FillGpuDrawParams()` never sets `ambientColor`, shaders never consume `emissiveColor` (HIGH);
  fog formula backwards, original source of the bug (HIGH); SkinnedEffect world-space-normal-transform omission,
  original source (HIGH); EnvironmentMapEffect emissive-remultiply (HIGH); scissor silently non-functional
  whenever a render target is bound, undisclosed (HIGH, found this pass); `BasicEffect::VertexColorEnabled` bare
  public field (MEDIUM); dead-code `VulkanVertexFormatHelper.hpp` (LOW-ish architecture note).
- **Bgfx**: `EnsureViewState()`'s `Clear*()` unconditionally clears color+depth+stencil regardless of requested
  `ClearOptions` (HIGH-ish, per its own test report); SkinnedEffect world-space-normal-transform omission, one of
  the 6 confirmed backend-groups (HIGH); EnvironmentMapEffect emissive-remultiply, original source (HIGH); fog
  formula backwards (HIGH); 2 known-failing CTests with no `WILL_FAIL` (`Bgfx_RenderTargetCube_DepthFormat`,
  `Bgfx_SkinnedEffect_WeightsPerVertex`, MEDIUM); `BasicEffect::VertexColorEnabled` bare public field (MEDIUM,
  same cross-backend finding as Vulkan); dead-code `BgfxVertexFormatHelper.hpp`/its own test never exercising the
  real code path (LOW-ish). Positive: the most complete Stencil+Scissor+DepthBias implementation of any backend.
- **EasyGL**: constructor failure after `RegisterForWindow()` leaves a dangling window-registry entry — **the
  most severe confirmed finding in this audit** (HIGH); SkinnedEffect world-space-normal-transform omission,
  originating instance for the WebGPU-porting chain (HIGH); `SkinnedPbrEffect` raw-World-instead-of-inverse-
  transpose variant (MEDIUM); `DrawUserPrimitives(void*, VertexDeclaration&)` never propagates the declaration to
  the backend (finding recorded against a test, XNA-facing root cause); MSAA test cannot actually verify MSAA
  (documentation/test-authoring); dynamic-buffer-stress test's index-buffer half never exercises what it claims
  to; several stale "known bug" doc comments contradicted by later fixes (recurring LOW pattern, 218-file shard).
- **WebGPU**: SkinnedEffect world-space-normal-transform omission, explicitly a line-for-line EasyGL port (HIGH);
  EnvironmentMapEffect emissive-remultiply (HIGH); `SpriteBatch`'s clip-space mapping always backbuffer-relative,
  never render-target-relative (HIGH). Positive: model-example constructor exception safety (the standard this
  audit judged every other `RegisterForWindow`-calling backend against).
- **D3D9**: custom (non-vendored) `PbrSkinned3D.hlsl` shares the skinned-normal-transform bug, 4th confirmed
  instance (HIGH); "object-space-only fog" in 3 custom shaders — a different bug from the Task-1111 mirrored
  formula (HIGH-ish, new pattern). Positive: vendored stock effects are real, byte-verified Microsoft bytecode
  and immune to every shader-level bug above by construction; the most architecturally complete
  Stencil+Scissor+DepthBias implementation of any backend (native render states, no emulation needed).
- **SdlRenderer**: 2 tests with stale expected-throw assertions superseded by a real, intentional production fix
  (commit `90f5db2c`, MEDIUM); `SpriteFont`'s flip-lookup tables sized 3 not FNA's 4, OOB-read risk on an
  out-of-range `SpriteEffects` cast (MEDIUM). Positive: exceptionally well-documented bug-fix history (8+ "Task
  NNN finding" comments), correct 2D/3D boundary enforcement.
- **Headless**: `HeadlessStatistics::primitiveCount` undercounts instanced draws by `instanceCount` (MEDIUM).
- **Software**: `DepthBufferWriteEnable`/`SetDepthWriteEnabled` have no effect (MEDIUM); `DepthBufferFunction`
  hardcoded to LessEqual, ignoring the other 6 `CompareFunction` values (MEDIUM). Positive: correct
  perspective-correct interpolation and Sutherland-Hodgman near-plane clipping, a genuine from-scratch rasterizer.
- **Dx3**: `SetVirtualResolution` resize failure destroys working surfaces before confirming the replacement,
  leaving the backend permanently unusable (MEDIUM). Positive: correct blend-math (including the two
  ostensibly-suspicious `srcAlpha²` terms independently re-derived and confirmed correct), correct constructor
  exception safety.
- **Ascii / Canvas**: no MEDIUM+ findings recorded (Ascii: LOW blend-state-leak-after-Present risk; Canvas: LOW
  uninitialized-buffer risk in an Emscripten-only file's non-Emscripten fallback path). Both confirmed clean of
  the `RegisterForWindow` constructor-ordering bug.
- **`cna-devices`** (not a graphics backend): `FileDialog.cpp`/`MessageBox.cpp` share a use-after-free window via
  a swappable-global-backend pattern (HIGH).
- **CNA core infrastructure** (not a graphics backend): `CNA::Logger::ToSDLPriority()` mistags every
  `Fatal`/`Error`/`Warn` log call as `SDL_LOG_PRIORITY_INFO` — foundational, always-compiled, project-wide (HIGH).
- **`Microsoft::Xna::Framework::Graphics`** (XNA-facing shared layer, affects every backend uniformly):
  `SpriteFont::MeasureString`/`SpriteBatch::DrawString` unchecked `unordered_map::end()` dereference (HIGH);
  `SpriteBatch::DrawString`'s undersized `SpriteEffects` table, OOB read (HIGH); `EffectParameter` Matrix
  inversion, `Elements`/`StructureMembers` always empty (HIGH); `BasicEffect` never populates `Parameters`
  (HIGH); `VertexBuffer`/`IndexBuffer` missing destination-offset, root cause of the 3-backend `NoOverwrite` gap
  (HIGH); `GraphicsDevice.cpp`'s ~27 raw exceptions (HIGH); `SpriteBatch::Begin()` sets `begun_=true` before
  fallible backend calls (MEDIUM, state-mutation-before-fallible-call pattern);
  `GraphicsDevice::SetRenderTargets` mutates tracked state before the backend call can throw (MEDIUM, same
  pattern); `Texture2D::GetTypeName()`/`RenderTargetCube.Dispose(bool)`/`RenderTargetBinding`/
  `TextureCollection`/`VertexBufferBinding.VertexOffset`/`ModelMeshPartCollection`/`Model.cpp` exceptions/
  `PackedVector` rounding/`VertexPositionColor.IVertexType`/`GraphicsDevice::Dispose()` ordering/`DisplayMode`
  missing members/graphics-exception base classes/4 `EffectXxxCollection` exceptions (all MEDIUM — see "By
  severity" for full detail); dead-code mip-regen-whole-cube (MEDIUM, SdlGpu+D3D11); `ApplySamplerState`/
  `ApplyBlendState`/`ApplyRasterizerState` missing fields, universal (MEDIUM).
- **`Microsoft::Xna::Framework` core (`Game`/`GraphicsDeviceManager`/`GameWindow`/math types)**: `Color::
  PackFromVector4()` unclamped cast, real UB (MEDIUM); `Vector3`/`Vector4`/`Quaternion`/`Matrix::GetHashCode()`
  signed-overflow UB not propagated from the `Vector2` fix (MEDIUM); `Matrix::Invert()` single- vs.
  double-precision, unverified claim (MEDIUM); `GameWindow::EndScreenDeviceChange`/orientation-heuristic gaps
  (MEDIUM); `Game::PollEvents()` missing 4 FNA SDL3 event reactions (MEDIUM); 3 raw-`std::`-exception instances
  (`GameComponentCollection`/`GraphicsDeviceManager`/`Game::AssertNotDisposed`, MEDIUM);
  `GameTests.cpp`/`GraphicsDeviceManagerTests.cpp` zero coverage (MEDIUM, leaves the `Game::UnloadContent()` and
  `GraphicsDeviceManager` event-forwarding HIGH bugs untested); `GameCrashTest.cpp` dead file (MEDIUM).
- **`Microsoft::Xna::Framework.Content`/`.Storage`**: `StorageDevice::DeleteContainer()` unchecked recursive
  delete, path traversal — **arguably the most severe finding of this audit session** (HIGH);
  `ContentReader::ReadExternalReference<T>()` absolute-path bypass, 3rd confirmed instance of the same
  `fs::path` pitfall (HIGH); `TextureCubeContentTypeReader.cpp` missing byte-count validation, OOB heap read
  (HIGH); `ContentReaderExternalReferenceTests.cpp` untested against its own confirmed gap (MEDIUM); `xna-storage`
  intra-pair SPDX mismatch (MEDIUM).
- **`Microsoft::Xna::Framework.Net`/`CNA::Internal::Net`**: `ENetBackend.cpp`'s `HandleReceive()` has no
  sender-authority check on 4 host-only broadcast message types, adversarial-client forgery (HIGH);
  `NetworkSessionProperties::Insert`/`RemoveAt` unchecked iterator arithmetic (MEDIUM); `NetworkSession*`
  `Dispose()`d but never `delete`d across 10 example/tool files, systemic but low-severity (LOW-ish, positive
  counter-example exists); `CNA::Internal::Net`'s whole-subsystem MIT-vs-MS-PL SPDX inconsistency (MEDIUM).
  Positive: the previously-known critical `NetworkSession::Dispose()` UAF is confirmed genuinely fixed.
- **`Microsoft::Xna::Framework.GamerServices`**: `GamerPresence.cpp`'s misindexed display-string table
  (MEDIUM, dormant); `GamerServicesComponent` missing `GetTypeName()` (MEDIUM); `GuideAlreadyVisibleException`
  dead code (MEDIUM); `PropertyDictionary`'s 9-method raw-exception surface, largest single-file instance of
  the exception-type pattern (MEDIUM). Positive: `GamerCollection<T>`/`AchievementCollection` do NOT share the
  `NetworkSessionProperties` bug; `GamerServicesDispatcher::UpdateAsync()`'s permanent-no-op-once-initialized
  behavior independently confirmed, validating the `xna-net` shard's own polling-loop fix.
- **`Microsoft::Devices`**: `Dispose(bool disposing)` incorrectly `public` (not `protected`) across all 4
  `Sensors` classes, real resource leak + permanently-broken object (MEDIUM). Positive: the most thoroughly
  self-audited subsystem in the project; does NOT share the `FileDialog`/`MessageBox` UAF bug; Android
  coordinate-math independently re-derived and correct.
- **`CNA::Internal::Media`**: `AudioTagParser.cpp`'s 32-bit-`size_t` integer-overflow-vulnerable bounds checks
  (HIGH, narrow platform scope); `PlaylistParser.cpp` no path-containment check (MEDIUM); `VideoDecoder.cpp`
  stale cached frame dimensions (MEDIUM); `MediaLibrary::SavePicture()` partial-`Read()` assumption (MEDIUM).
- **`CNA::Input`/`CNA::Devices` (NOXNA extensions)**: duplicated `Clipboard`/`Power`/`PowerState`
  implementations across the two namespaces (MEDIUM).
- **Tools / docs / build**: `tools/fna-reference/PackedVectorReference.cs`'s integer-only test inputs, root
  cause of the `PackedVector` rounding bug going undetected (documented alongside the MEDIUM finding above);
  `generate_animations.py`'s stale docstring (LOW); 7 `docs/*.md` staleness findings cross-checked against this
  session's own confirmed state (MEDIUM); root `.gitignore`'s `build*` pattern silently untracking files
  project-wide (MEDIUM); `headless_resource_backends_test.cpp`'s unconditional-check robustness gap (LOW).

## By category

- **FNA-parity / shader-math correctness** (the largest category by finding count): fog formula backwards
  (Bgfx/Vulkan/D3D11/D3D12), fog completely absent (SdlGpu), object-space-only fog (D3D9 custom shaders),
  SkinnedEffect world-space-normal-transform omission (6 backend-groups, exhaustive sweep), SkinnedEffect
  raw-World-not-inverse-transpose variant (6 confirmed instances), SkinnedEffect Ambient/Emissive dropped
  (Vulkan, D3D11/D3D12), EnvironmentMapEffect emissive-remultiply (Bgfx/WebGPU/Vulkan/SdlGpu), Vulkan missing-
  Y-flip (4 effect families), Software's hardcoded depth-write/depth-function.
- **Memory safety / resource lifecycle**: EasyGL `RegisterForWindow`-before-fallible-step dangling pointer
  (the audit's single most severe finding), SdlGpu's unwrapped-constructor resource leak, `cna-devices`
  `FileDialog`/`MessageBox` use-after-free, Dx3's destroy-before-replace resize failure.
- **State-mutation-before-fallible-call** (a recurring shape, 3 independent instances): `SpriteBatch::Begin()`,
  `GraphicsDevice::SetRenderTargets`, and (a variant) D3D11's now-fixed MRT-finalization bug (DX-143) — the
  first two remain open, the third was found already fixed, a genuine positive counter-example of the same
  risk pattern being correctly handled.
- **Backend-specific logic bugs (non-shader)**: Vulkan `SpriteBatch.SetTransformMatrix()` no-op, Vulkan scissor
  silently inert when a render target is bound, D3D12 Stencil/Scissor/DepthBias non-functional, D3D12
  `OcclusionQuery` multi-draw overwrite, Headless `primitiveCount` instancing undercount, WebGPU
  backbuffer-relative-only `SpriteBatch` clip-space mapping.
- **Foundational/infrastructure**: `CNA::Logger::ToSDLPriority()` mistagging every non-`DEBUG`/`TRACE`/
  `EXPERIMENT` log level — the one finding in this list that isn't graphics-backend- or Devices-specific.
- **Architecture / dead code**: `BgfxVertexFormatHelper.hpp`/`VulkanVertexFormatHelper.hpp` (correct logic, zero
  production call sites, and in Bgfx's case, its own test never exercises the real path either);
  `BasicEffect::VertexColorEnabled` as a bare public field (violates this project's own C# property convention).
- **Testing / CI hygiene**: 3+ currently-failing CTests with no `WILL_FAIL`/skip annotation
  (`EasyGL_AvatarRenderer_TintRouting`, `Bgfx_RenderTargetCube_DepthFormat`, `Bgfx_SkinnedEffect_WeightsPerVertex`)
  — a real CI-masking risk, not just noise; 2 SdlRenderer tests with stale expected-throw assertions after an
  intentional production behavior change; a recurring documentation-rot pattern (stale "known bug" comments in
  the EasyGL 218-file shard describing defects already fixed); tests that assert only metadata/capacity instead
  of actual data content; weak enum-value coverage (`CompareFunction` tested for only 5 of 8 values).
- **Robustness / undefined behavior reachable via public API**: `SpriteFont::MeasureString`/`SpriteBatch::
  DrawString`'s unchecked `unordered_map::end()` dereference; `SpriteFont`'s undersized flip-lookup table
  (3 entries, not FNA's 4) risking an out-of-bounds `constexpr`-array read for an out-of-range enum cast;
  `Color::PackFromVector4()`'s unclamped cast; `TextureCubeContentTypeReader.cpp`'s missing byte-count
  validation; `AudioTagParser.cpp`'s 32-bit integer-overflow-vulnerable bounds checks;
  `headless_resource_backends_test.cpp`'s unconditional `check(true)` with no `try`/`catch`.
- **Security / adversarial-input hardening** (a category this audit did not originally expect to need):
  `ENetBackend.cpp`'s missing sender-authority check on host-only broadcast messages (network-forgeable, no
  MITM needed); `StorageDevice::DeleteContainer()`'s unchecked recursive delete driven by caller-supplied path
  (data-loss path traversal, arguably the audit's most severe single finding); `ContentReader::
  ReadExternalReference<T>()`'s absolute-path containment bypass; `TextureCubeContentTypeReader.cpp`'s OOB heap
  read from a crafted asset; `PlaylistParser.cpp`'s unrestricted playlist-entry path. Three of these five
  (`StorageDevice`, `ContentReader`, and a sibling `CnjSourceFile` containment pattern) share the identical
  C++-specific `fs::path(base) / <caller-supplied string>` pitfall (silently discards `base` when the RHS is
  absolute) — worth a dedicated project-wide grep for this exact shape.
  `DeleteContainer` is also the audit's clearest example of a CNA-introduced regression, not an FNA-faithful
  gap: FNA's own equivalent method is an unimplemented stub, so there was no upstream behavior to inherit.
- **API-design: raw `std::` exceptions instead of this project's own `System::*Exception` convention** — the
  single most numerous recurring pattern in this audit, confirmed independently in at least 10 distinct areas:
  `GraphicsDevice.cpp` (~27 sites, the widest single-file instance), the `Texture*` family (`Texture2D.cpp`
  alone ~15+ sites), `SkinnedEffect`/`EnvironmentMapEffect`/`PbrEffect`/`SkinnedPbrEffect`/
  `SamplerStateCollection`, 4 `EffectXxxCollection::operator[]` implementations, `PropertyDictionary` (9
  methods, `xna-gamerservices`), `ContentLoadException`'s base-class choice, `DeviceLostException`/
  `DeviceNotResetException`/`NoSuitableGraphicsDeviceException`'s base-class choice, `Model.cpp` (5 sites), and
  3 independent instances in `xna-framework-core` (`GameComponentCollection`/`GraphicsDeviceManager`/
  `Game::AssertNotDisposed`). Two test files (`EffectParameterTests.cpp`'s Matrix-convention assertion aside,
  `GraphicsExceptionTests.cpp` and `GamerServicesDataTests.cpp`) actively bake wrong-exception-type expectations
  in as the asserted-correct behavior, meaning a fix requires a coordinated test update, not just production code.
- **Documentation-rot** (a doc/comment claiming a status the current code contradicts, not a code bug per se —
  confirmed as a systemic, repeated pattern across at least 20 distinct instances this session, not a single
  mistake): 6+ stale "known bug" comments in the `examples-tests-easygl` batch, 9+ more in the Bgfx/Vulkan/
  SdlRenderer batches, `D3DConstantBuffers.hpp`/`D3D12RootSignatureCache.hpp`/`D3D12Textures.hpp`/
  `RenderPipelineSettings.hpp`'s stale class-level doc comments, and 7 `docs/*.md` staleness findings found by
  cross-checking against this session's own confirmed current state. Recommend a periodic sweep specifically
  for "Task NNN"/"known bug"/"currently broken"-style comments cross-checked against `git log`, independent of
  any single file's own audit.
- **Confirmed FNA-faithful, NOT a regression** (recorded for context — flagged only where the port omits the
  explanatory comment this codebase otherwise provides for this exact situation elsewhere): `BoundingSphere::
  Contains(BoundingFrustum)`'s inability to return `Disjoint`; `BoundingFrustum::Intersects(Ray)`'s general-case
  `NotImplementedException`; `Curve::ComputeTangent()`'s asymmetric near-zero epsilons; `StorageContainer`'s
  unchecked `Path.Combine`-style joins. Contrast with `StorageDevice::DeleteContainer()` above, which looks
  superficially similar but is genuinely NOT FNA-faithful (FNA's own method is an unimplemented stub).
