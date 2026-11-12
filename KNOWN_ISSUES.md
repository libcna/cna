# CNA known issues and current state

This file lists what is broken or limited in CNA now. Subsystem behaviour is in each subsystem's
page under `docs/` (index: `docs/README.md`), deliberate divergences from FNA and XNA are in
`docs/xna-fna-behavior-decisions.md`, and finished work is Git history. State of 2026-11-11.

## Current state

- **What CNA is.** A C++23 reimplementation of the XNA 4.0 programming model
  (`Microsoft::Xna::Framework`) on a CNA-owned platform abstraction (SDL3 by default) and a curated
  set of graphics renderers. FNA's source is the day-to-day behavioural reference; where FNA and
  measured XNA 4.0 Refresh disagree, XNA wins. The project now goes into long-term maintenance:
  no new renderers, asset formats or subsystems.
- **Renderers.** 10 public identities over 8 implementation families: `OPENGLES3`, `OPENGL33`,
  `WEBGL2` (the three share EasyGL), `WEBGPU`, `SDLGPU`, `FNA3D`, `DIRECTX9`, `SOFTWARE`, `HEADLESS`,
  `STUB`. The list is `cmake/RendererIdentities.cmake`; `docs/renderer-registry.md` has the gates and
  `docs/removed-renderers.md` the 41 retired identities, which are refused by name.
- **Platforms.** `CNA_PLATFORM` is `SDL3` (Windows, X11, Wayland, macOS, iOS, Android and the browser
  through SDL3), `HEADLESS` or POSIX-only `TERMINAL`; audio is `SDL3`, `NULL` or `ALSA`. Linux x86_64
  is the primary verified host. Windows is verified through MinGW-w64 builds under Wine and a hosted
  native-MSVC smoke workflow, the browser in headless Chrome, Android on an x86_64 emulator, and
  macOS/iOS through CI only; see Limitations.
- **Native C API.** Experimental ABI 0.60.0 (`modules/c-api/include/CNA/C/abi.h`); changes are in
  `docs/c-api/ABI_VERSIONING.md`.
- **Dependencies.** FNA3D fork `libcna/FNA3D` at 76a76cf (branch `cna-audit-fixes`), which records
  MojoShader fork 6f1e91b; their CNA patches and open defects are in
  `docs/fna3d-dependency-defects.md`. SDL 86296ac8 with five CNA patches (`cmake/patches/`).
  sharp-runtime (CNA's `System.*`) is the sibling repository.
- **Tests.** The last full run, sweep 18 on 2026-11-11 (837b687c3), built seven configurations (multi,
  sdlgpu, webgpu, fna3d, software, headless and opengl33; about 102,900 CTest cases). Its failures were a
  reference file's line endings (fixed in 4ef67873b) and `GamerServices_DirectoryServiceContract`, whose
  server fixture build had been deleted (rebuilt, it passes). The thirteen consumer repositories were
  rechecked on 4ef67873b the same day; none needed a code change. GPU and window tests must run through
  `tools/platform/run_gpu_tests_private.sh`, never on the live desktop.

## Defects

- **Genuine-XNA native teardown fault (TQ-SWEEP14-02), unattributed.** The harness ownership gap is
  repaired (2026-11-08); the one-off Wine fault of sweep 14 did not reproduce with the old or the
  new harness. If it recurs, capture `+loaddll,+seh` and a WineDbg stack; never suppress the nonzero result.
- **FNA3D-3 and FNA3D-8 are open in the FNA3D fork** (`docs/fna3d-dependency-defects.md`). FNA3D-3:
  the SDL GPU driver applies no MultiSampleMask (see External and driver limitations). FNA3D-8: CNA
  forwards a source rectangle and a whole-client destination on FNA3D's OpenGL and SDL GPU drivers
  (1b7320e41, evidence `../cnaplan/evidence/rev002/ki-fna3d-present/`); a partial destination, an
  override window and any rectangle on the D3D11 driver are refused by name, because neither driver
  preserves the pixels outside a destination (OpenGL leaves the back buffer's content, SDL GPU clears
  to black). SDL GPU's whole-client override measures correct but is still refused: the device decides
  before adoption without a renderer query.

- **`DIRECTX9` compiled effects: three legacy cases fail under Wine + DXVK 3.1.1, attributed to DXVK**
  (`CNA_DIRECTX9_COMPILED_EFFECTS=ON`; the option stays default OFF; evidence
  `../cnaplan/evidence/rev002/ki-dx9-legacy/`, `docs/fx-compiled-effects.md` §16).
  `LegacyBumpEnvironment` (TEXBEM/BEM off-diagonals: DXVK stores the bump matrix by columns and its
  dxbc-spirv translator computes MAT00·r + MAT01·g where D3D9 specifies MAT00·r + MAT10·g),
  `LegacyDepthOutput` and `CompiledEffectLegacyMatrixDepthTest.PlainSignedMixedAndZero...` (TEXM3X2DEPTH
  with `_bx2` on only one of PAD/DEPTH: the translator treats PAD as a no-op and applies the final
  instruction's source modifier to every row). Wine's wined3d passes all three with the same binary;
  DXVK 2.6.0 fails the depth cases too. No CNA change; the assertions stay strict. No real Windows
  hardware run exists.
- **X11 foreign-window import does not correlate the refusing BadWindow by request serial**
  (`AdoptNativeWindowHandle`, CNA-DEMOS-ISSUE-158). The refusal matches BadWindow, the
  GetWindowAttributes request, the pending id and SDL's own display, and refuses when more displays
  report than it records (e28fea5bf). A same-id GetWindowAttributes from another thread on SDL's
  private connection during the import is not told apart, so it can refuse a valid import (only if
  the window's existence changed during the import), never accept an invalid one. A serial check
  cannot close it (KI-X11-SERIAL, 2026-11-11): SDL publishes its X11 `Display*` per window only, so it
  is unknown before a first import, and the range [next request before, last processed after] also
  contains a concurrent request on the same connection. The concurrent cases are argued from source,
  not covered by a test. An independent review of e28fea5bf (2026-11-11) adds three gaps, none
  reproduced against a server and none fixed (X11 takes only fixes that real use finds): an error
  callback from another Display that matched the pending id and paused after reserving its slot can
  resume during the next import and overwrite that import's SDL entry, so an invalid id is accepted
  (needs a capture mutex covering reservation, store and the reset); `XGetWindowAttributes` sends
  GetWindowAttributes and then GetGeometry, and a window destroyed between the two fails the second
  with BadDrawable (request 14), which the filter does not match, while SDL reads the unfilled
  geometry; and the error handler CNA installs replaces the host's for good (the previous one is
  neither kept nor restored).

### Divergences from XNA, not fixed (state of 2026-11-11)

- XACT (HND-XACT-06, evidence `../cnaplan/evidence/rev002/hnd-xact-06-loop-extensions/`): with only an
  effect NEW_ON_LOOP (families 4/6), XACT holds one wave and varies its pitch per pass; CNA plays n+1
  complete waves of the first candidate with a new effect draw each (count and candidate match).
  Unmeasured: volume/filter NEW_ON_LOOP, ADD operators, type 6 with both NEW_ON_LOOP kinds, per-pass
  pitch values, Ordered From Random / Random No Immediate Repeats, more than two candidates, raw 255,
  streaming, cross-bank candidates.
- Graphics: a stock parameter's `SetValue(Texture*)` and `SetValue(Texture2D*)` write separate storage,
  so `Parameters["Texture"].SetValue((Texture*)t)` is not seen by `GetValueTexture2D` or the stock draw;
  a SpriteEffect passed to `SpriteBatch.Begin` does not run its own matrix (FNA ignores
  `transformMatrix` there); `SpriteBatch.Draw`'s disposed-texture refusal names the texture's Name
  (unchecked against XNA). Out-of-range ShaderIndex on a stock clone (measured on XNA 4.0 under Wine,
  `tests/reference/xna40/stock-clone-shader-index/`): CNA matches XNA's InvalidOperationException
  "An unexpected error has occurred."; native D3DX also accepts -1, int.MinValue and int.MinValue+1
  and draws from outside its tables, which CNA refuses. In that original XNA 4.0 build (the prefix has
  no Refresh), EffectMaterial clones of BasicEffect, AlphaTestEffect, EnvironmentMapEffect and
  SkinnedEffect throw NullReferenceException in `Effect..ctor(Effect)`; Refresh is unmeasured, which
  bears on the HND-TQ-25 D9 premise.
- Graphics, from an independent review of the Apply latch (d07de8c49, c5afd4874, 0d97c3033) on
  2026-11-11, source-traced and not yet reproduced by a test: (1) the five stock classes latch their
  properties at Apply, not the final parameter values, so a raw `Parameters["DiffuseColor"]`,
  `["WorldViewProj"]`, `["FogVector"]` or `["ShaderIndex"]` write that the next OnApply does not
  overwrite (its dirty bit is clear) never reaches the draw, where XNA's pass commits it; Basic,
  EnvironmentMap and Skinned also rebuild light values and lighting transforms from fields (the
  renderers take World/View/Projection and material fields, not WVP, so a fix crosses every renderer
  interface; base-Effect and EffectMaterial clones already read raw storage, so a source and its clone
  can render differently); (2) SpriteEffect (CNAEXT,
  FNA's) reflects no `Texture` parameter, so a clone cannot carry or bind FNA's texture at Apply and
  draws device slot 0 (SpriteBatch rebinds slot 0 and hides this).
- Graphics, compiled-effect passes that omit a shader stage (review rv-stage, 2026-11-12, source
  analysis): XNA keeps the device's shader and constants for a stage a pass does not assign.
  `RunCompiledEffectInheritedVertexStageContract` is registered for Software only. EasyGL, FNA3D,
  SDLGPU, WebGPU and DIRECTX9 are predicted to fail its other-effect and last-assigner cases:
  MojoShader's BeginPass starts from the effect's previous raw shaders, so CommitChanges writes the
  applying effect's parameters into the inherited stage's registers. Software resets those raw
  sources before BeginPass; the other four backends can do the same before their BeginPass, FNA3D
  needs it in the fork. Software itself refuses (InvalidOperationException) a pass without a vertex
  shader after a stock, ShaderEffect or plain-sprite draw, where XNA would inherit the stock shader;
  its stage record splits when `RecreateRendererForMultiSampleCount` replaces the renderer, and is
  lost with the effect that owned it.
- C API: content tags of meshes/parts have no C route; HND-TQ-22 C route (ObjectValue handles, ABI 0.61)
  not done; `CNA::Net::SystemLinkDiscovery` (GALAXY-NET-01) has no C route (planned, CBIND-127).
- Content: Texture3D/TextureCube level counts <= 0 are accepted by XNA (same loop as Texture2D), CNA
  refuses; a level count past the full chain or a wrong level byte count is ContentLoadException in CNA
  where XNA fails inside SetData (type unconfirmed); the TextureCube reader still decodes before it
  constructs, so the profile's cube refusals come after the levels; Reach NPOT DXT textures are
  uploaded as Color by CNA although XNA's Texture2D constructor refuses them (IL_00d0-0122); XNA's
  Texture2D/Texture3D readers ignore an existing instance, CNA reloads into it. LZX (KI-CONTENT-LZXGRAN,
  measured on XnaNative 4.0.30901): a valid frame after a short one is unmeasured, and the loss of
  buffered output when a chunk fetch fails has no test. sharp-runtime `BinaryReader(nullptr)` names
  the parameter "stream" (.NET 4: "input").
- OPENGLES3 compiled effects: LOD bias is unrepresentable (ES 3 has no sampler bias), so 2 of the 44
  corpus cases skip and the family is not qualified; `cmake-build-multi` keeps it OFF.

## Limitations

- **XACT native qualification, 2026-11-08 (HND-XACT-01..04).** Genuine Refresh-selected XACT 3.6 x86/x64
  captures (288 cells under Wine with null audio, each after the retained resident control; evidence
  `../cnaplan/evidence/rev002/hnd-xact-0{1,2,3,4}-*/`) now settle preparation (weighted/resident/streaming prepare one dependency
  before Play; interactive defers; a missing interactive bank plays silently and is not retried),
  ordered NEW_ON_LOOP successor prefetch before EOF without late retry, cross-bank selection (first
  registered duplicate wins; clones own their banks; a sole foreign category victim is replaced) and
  sequential notification/resource lifetimes; CNA fixes landed for the differences. A finite loop byte n plays
  n+1 passes for every play-wave family (HND-XACT-05, 56 native cells, event types 1/3; HND-XACT-06,
  136 cells: families 4/6, loop-region waves, random/shuffle, raw 5/100/254), split into separate waves
  only by the track NEW_ON_LOOP bit. Still open: exact hidden timing/RNG,
  ordering among several category victims and other replacement modes, live interactive changes and
  rollback, concurrent callback quiescence and forced in-use destruction; native Windows, managed
  execution and gain/PCM remain unqualified.
- **The static C API archive with the C++ SDK is unsupported and refused at configure time**
  (decided 2026-11-03 by the owner, MTH-PLAT-004). `libcna_c_api_static.a` remains the encapsulated
  pure-C form; mixed C/C++ programs use the shared adapter and SDK, sharing one runtime.
  Installed CMake targets enforce this rule; pkg-config/plain linker consumers must follow it.
- The retained XNA/Wine wma/mp3_mono_44100_128k.mp3 record returns AudioContent labelled Wma
  with readable metadata, then fails Data access. CNA's AudioContent::ReadThroughMediaDecoder
  instead enforces a Windows Media codec and rejects that MP3 during Import. The current
  XnaWmaImporter.RefusalsMatchXna test covers only missing/empty/truncated WMA, so it does not
  qualify the cross-format call stage. docs/xna-content-pipeline-media.md records the discrepancy;
  obtain native-Windows measurements with working WMA positive controls before treating the
  prefix-limited result as the complete Microsoft contract. No new regression/runtime change
  was added in this documentation task.
- CApiRouteTestCoverage is an evidence-metadata gate, not runtime qualification
  (CNA-FIX-L05 / MTH-PRE-005). It separates declaration presence, configured CTest registration and
  validated retained execution. Source scans exclude comments,
  literals and literal false branches; unknown conditions, macro expansion and build inclusion
  remain unevaluated. Optional --execution-record joins ctest_evidence.py artifacts; the explicit
  --require-execution gate refuses missing/disabled/skipped witnesses and wrong evidence categories.
  Passing empty bodies still establish no assertion coverage: runtime qualification stays
  not-established and assertion counts unknown. See the route-presence evidence section in
  docs/c-api/README.md for scope and capture identity limits.
- CNB read limits are not a total process-memory budget. Owned input, expanded chunks, decoded
  objects and GPU resources can coexist, and concurrent asset loads remain application-owned.
  Shared animation ranges additionally obey materialized track/key element totals; see
  `docs/cnb-format.md` §12.1 for the bounds and their scope.
- Original CNBF-105/108 payload hashes and raw timing samples are absent, so their historical
  tables cannot be replayed exactly. Corrected probes validate covered I/O/codec paths for new
  stable inputs; they do not establish full CNB-load performance. Production CRC tests compare
  selected dispatch against the table, not every forced hardware/fallback architecture; see
  `docs/cnb-format.md` §8.1 and §15.3.
- The build benchmark/report tools record effective cache values and source/dependency identities;
  those hashes are not source/package archives or a capture of every transitive configuration
  input. Matching before/after snapshots cannot exclude transient source changes. RSS remains a
  sampled process-tree sum; the CI report's cache counts are cumulative and its executed-edge
  counts parse console descriptions. See `docs/build-performance.md` for measurement scope.
- SkinnedModelEXT's indexed-skeleton and device checks cannot prove semantic joint identity:
  joint names are absent, and callers still need pieces from a common source rig. Raw clips have
  file/count/range preflight, but input and decoded arrays coexist and do not constitute a total
  process/GPU budget. Vertex joints/weights are not exhaustively validated. Legacy skeleton/vertex
  scalars remain native copies of little-endian converter output, without big-endian execution
  qualification; see `docs/skinned-model-ext.md` for the current validation scope.
- No real XMA encoder output or console playback is qualified: the external adapter's XMA2
  checks, measured loop, fingerprint, processor version and Xbox WAV routing (L16, 2026-11-02) are
  verified with stub and fake encoders only (`docs/xma-encoder-backend.md`).
- Native Camera has no public reopen/device-selector route after Lost. SDL3 removal and denial
  events and a revoked permission now end a session (CNA-FIX-L18), verified with pushed SDL events
  only: no physical unplug or revocation has been run (`docs/cna-devices-camera-design.md`).
- `RasterizerState.MultiSampleAntiAlias` is honored by `SOFTWARE`, desktop `OPENGL33` (also by
  runtime selection) and, since CNA-FIX-L20, `FNA3D`'s OpenGL driver; `DIRECTX9` writes render
  state 161 as XNA does, but its pixel effect is unmeasured because DXVK reports the state
  unhandled. `OPENGLES3`/`WEBGL2`, `WEBGPU`, `SDLGPU` and FNA3D's SDL_GPU-based drivers have no
  per-draw multisample switch, so the property has no effect there. This is separate from
  multisampled target storage, which those families still allocate and resolve
  (`docs/rasterizerstate-support.md`).
- `SDLGPU` applies `BlendState.MultiSampleMask` on a multisampled target only when it keeps every
  sample or none: the pinned SDL reserves `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so
  a mask of none drops the draw and any partial mask throws `System::NotSupportedException`
  (DC-R2, owner decision, `c7f6c68e5`). `RendererFeature::MultiSampleMask` reports each renderer's
  answer; `ColorWriteChannels` is honoured.
- `SurfaceFormat::Alpha8` render targets are stored natively by SOFTWARE only (L22, 2026-11-02:
  one-byte declared texels, sampled as (0, 0, 0, A)). The GPU renderers answer
  `SupportsSurfaceFormatAsRenderTargetEXT(Alpha8)` false and fall back to `Color` as XNA's
  preferred-format rule allows (`GraphicsCapabilityFloatRenderTargetTests.cpp`): GL has no
  renderable alpha-only format, SDL maps `A8_UNORM` to a swizzled `R8` on Vulkan that a colour
  attachment does not honour, and the shaders would have to route alpha to the stored channel.
  How the Xbox 360 allocated an Alpha8 target was never captured.
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`docs/browser-network-readiness.md`, `docs/gamer-services-known-limitations.md` §26).
- `DIRECTX9` is validated under Wine (+DXVK) only. `SDLGPU`'s Direct3D 12 backend is validated
  under Wine (+vkd3d-proton); the automatic hosted native-MSVC smoke workflow first
  passed on 2026-11-02 (`4fef632cf`, eight SDLGPU programs). Hosted smoke is not
  real-hardware qualification, and current local fixes still need a post-publication CI run. The C API's pure-C suites
  run under Wine from the MinGW-w64 build; `MediaLibrarySmoke` reports a skip on Windows, whose
  known folders cannot be pointed at its fixture. The MinGW-w64 test binaries also skip on
  Windows what it cannot provide: a Python interpreter where none can be started, `/dev/full`,
  aging a directory through `utime`, symbolic links, a renderer without 3D, and the ordering guard
  for tests that need an uninitialized `GamerServicesDispatcher`.
- The installed C++ SDK (`CNA_INSTALL_CXX`, `docs/installed-cpp-sdk.md`) is validated relocated on
  native Linux (shared, all-static with the C API off, video OFF) and, for WEBGPU, as a moved
  Emscripten SDK whose application ran three frames in headless Chrome 155 (MTH-PLAT-004,
  2026-11-03); Firefox, Safari and WEBGL2 installed consumers are not run. Its pure-C
  `CNA::CApiStatic` (+ `CNA::CApiStaticAsyncify`) is validated the same way, for WEBGPU only:
  Node and one Chrome page (`docs/c-api/CONSUMING.md`, Emscripten). MinGW-w64 only
  configures and exports the package: no relocated Windows SDK has been run.
- googletest's own stream capture and death tests create temporary files through the ANSI API, so
  any gtest binary aborts under a Windows `TEMP` holding a character outside the ANSI code page
  (measured under Wine, ACP 1252, 2026-11-02). The content and content-pipeline suites are
  otherwise Unicode-clean: under MinGW+Wine they give identical results with an ASCII and a
  non-ASCII (code-page-representable) `TEMP`. No MSVC run has repeated that measurement.
- **`SDLGPU` is the default renderer on Windows, macOS and iOS but is not qualified there** since
  the 2026-10-13 retirement of `DIRECTX11` and `METAL` (`docs/removed-renderers.md`). Evidence that
  exists: the full tree passed with `SDLGPU` (Metal backend) on a Mac mini M4 in the AM4 campaign,
  before the retirement; Direct3D 12 runs under Wine. The automatic hosted native-MSVC Direct3D 12
  smoke gate (added 2026-10-30) first passed on 2026-11-02 (`4fef632cf`): all eight SDLGPU programs on
  the hosted runner's Direct3D 12, which multisamples `D32_FLOAT` but neither stencil format. Not done:
  native Windows hardware qualification, a re-run on Apple hardware, and any iOS build (`SDLGPU`
  has never been built for iOS; CI tries it
  as a non-gating leg). Known gaps against the retired native backends, each to be implemented or
  capability-gated in that campaign: the legacy `ShaderEffect` GLSL-text route (`CustomEffects`)
  is compiled out on Windows, Apple and the browser because no runtime GLSL compiler is present;
  with `CNA_CNAEXT=ON`, explicitly typed SPIR-V remains available on a live SDLGPU device and uses
  ShaderCross for D3D12/Metal. Occlusion queries are unavailable (SDL_gpu 3.5 has none).
  `RendererFeature::BaseInstanceDrawing` is implemented by
  `SDLGPU` since RRC-043 (its stock, instanced-matrix and compiled-effect routes expand per-instance
  streams on the host from the first instance); no other survivor claims it.
- iOS has simulator smoke evidence only, with `SOFTWARE`.
- `Microsoft::Devices` sensors and vibration have never run on a phone: the Android axis, compass
  and attitude mappings are derived and host-tested only. The device checks still open are listed
  at the top of `docs/devices-hardware-checklist.md`.
- `WEBGPU` is supported (promoted from experimental on 2026-10-17) and verified on Linux x86_64
  and in headless Chrome only; its Windows, macOS and linux-aarch64 wgpu-native packages are
  unverified (`WEBGPU-1`, `docs/webgpu-renderer.md`).
- `SOFTWARE` is a bounded CPU renderer, and `HEADLESS` and `STUB` make no pixel claim
  (`docs/renderer-registry.md`, "Categories and maturity").
- Per-subsystem capability boundaries, by design rather than defect, are in the documents indexed by
  `docs/README.md`.
- **Pixel-centre bias.** Every renderer moves 3D triangles 63/128 of a pixel, not 1/2, to emulate
  Direct3D 9's integer pixel centres (2026-10-26 audit B2-07). The 1/128 keeps a fill edge that
  falls exactly on a host sample on XNA's side; it is the value DXVK itself applies, and the XNA 4.0
  runs that settled pixel centres on this machine go through DXVK. Interpolated attributes are
  therefore evaluated 1/128 pixel off D3D9's centre. Native Direct3D 9 was not available to
  measure whether it differs.
- **Pixel centres of lines and points.** The XNA correction above moves triangles only, in every
  FNA3D driver; lines and points are drawn at the host's half-integer centres. The MSAA measurement
  that extended the correction to multisampled targets (B2-04) drew triangles, so nothing measured
  says what XNA does for lines and points there.
- **MojoShader SPIR-V envelope** (FNA3D's SDLGPU driver, CNA's SDLGPU and WEBGPU compiled effects):
  published MojoShader `c6397f8dd949d21e72bf9ad2550648a96e6d7d3f`, recorded by FNA3D
  `6a35962dbe48ed202259fa60f927688a7c5e4ea8` (CNA pin since TQ-FORK-PIN-03), implements BEM, TEXDEPTH, TEXREG2AR/GB/RGB,
  TEXDP3/TEXDP3TEX and TEXM3X2DEPTH; none of these eight retains a missing-emitter refusal.
  TEXM3X2DEPTH captures PAD's numerator, handles independent plain/_bx2 sources, writes depth
  one for zero denominator and guards the consumed destination. The same pin infers legacy
  sampler dimensions from typed Effect Texture parameter/object bindings, using CTAB register
  associations or explicit pass sampler bindings; caller maps retain priority. Generic metadata
  without a typed association remains default 2D, shared-shader conflicting pass dimensions and
  inferred arrays refuse by name, and opcode-specific 2D-only limits remain. CNA's unchanged
  44-case corpus retains the old assertions and adds full 8x8 matrix-depth brackets; standalone
  public depth and six 2D/cube/volume unsigned/_bx2 inference draws also reach FNA3D. OFF builds
  retain all eight named capability refusals. In cmake-build-multi all ten `Gap13_` tests and the new witnesses pass privately on SDLGPU, WEBGPU
  and FNA3D (OpenGL, ES3, SDLGPU drivers); a zero-denominator depth mutant and a 2D-forcing sampler
  mutant turn them red (TQ-FORK-PIN-02, 2026-11-08; native browser runs not repeated).
- **MojoShader FOG and other vertex semantics: repaired 2026-11-08 (TQ-FORK-L38, c6397f8).** GLSL ES and
  Metal now transport FOG in canonical x like the other profiles (all 16 lane pairs tested), a masked
  vs_3_0 PSIZE keeps its lanes and exports x, ARB1 binds result.fogcoord/pointsize and refuses what it
  cannot express by name, and HLSL names its refused FOG/SAMPLE/TESSFACTOR vertex inputs. Vulkan
  pipeline creation and GL linking of FOG pairs, and Direct3D 9, were still not run.
- **FNA3D OpenGL ES 3 readback** reads RGBA8, `ColorSrgbEXT` (stored as `GL_SRGB8_ALPHA8` since the
  review; the unsized format FNA3D used before was refused by ES 3 at upload, so such a texture
  never had storage), the float formats, and any other format only where the implementation's own
  read layout is the format's; it refuses the rest by name. On Mesa 25.0.7 (radeonsi) Rgba1010102,
  Rgba64, ByteEXT and UShortEXT read, Rg32 and `ColorBgraEXT` are refused for their layout.
  Desktop GL reads every uncompressed format.
- **FNA3D pixel-store state**: readbacks and uploads save and restore every pack/unpack value they
  set, so another user of the context keeps its own; an upload therefore makes six
  `glGetIntegerv` calls, each a synchronization point under Mesa's glthread.
- **FNA3D tracing with several devices: now refused explicitly (2026-11-08, TQ-FORK-L41, 1f59268).**
  A second traced device and a foreign-window present are refused before anything is recorded;
  one-device traces are unchanged. Covered device-free; the native overlap leg needs a
  TRACING_SUPPORT=ON build.
- **MojoShader's D3D11 glue: repaired 2026-11-08 (TQ-FORK-L42, 32352d0).** Split packed pixel semantics
  map to existing HLSL members and integer input-layout semantics match by exact name and index
  (DEPTH1 no longer matches DEPTH10). Generated HLSL compiles with shaderc's HLSL front end; no native
  D3D11 execution has been run.
- **SDL_shadercross initialization is the application's** (MojoShader's SDL_GPU glue): the glue
  calls neither `SDL_ShaderCross_Init()` nor `_Quit()`, which act on the whole process without
  reference counting, so DXBC is offered only after the application initialized ShaderCross
  (`mojoshader.h`); FNA3D's SDLGPU driver forwards DXBC to SDL when it is offered. CNA's SDLGPU
  renderer initializes ShaderCross; a standalone FNA3D build with `MOJOSHADER_STATIC_SPIRVCROSS`
  must. FNA3D's test of that selector links a stand-in static package (archive shape, C++ runtime),
  not the real one: the only static SDL_shadercross package on this machine was built without DXC
  and its package config requires DirectXShaderCompiler anyway.
- **Audio output device changes are measured nowhere.** The SDL3 backend opens
  `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, so a change of the system default is whatever SDL3 does
  with a default-device stream. The ALSA backend reopens a PCM that disappears (CNA-FIX-L45,
  `docs/audio-alsa.md`), tested with injected `ENODEV` failures only; no physical unplug or
  default-device switch has been run on either backend (former AUD-04-010/011).
- **Sound lags on the web (Emscripten, SDL3 audio).** CNA mixes only when SDL asks
  (`Sdl3AudioDevice::FeedStream`) and queues nothing of its own, but SDL3's Emscripten driver plays
  through a `ScriptProcessorNode` of twice the default buffer (2048 frames at 48 kHz, 42.7 ms;
  `third_party/SDL/src/audio/emscripten/SDL_emscriptenaudio.c:186`) whose callback runs on the
  browser's main thread. The constant delay is one to two such buffers plus the browser's output
  latency, estimated at 70-130 ms against 20-40 ms natively. In an Asyncify build (`Game::RunLoop`
  awaits `requestAnimationFrame`) the callback runs only between frames, so a frame longer than one
  buffer delays or drops sound as well. Reported by Mobile Eggbert (its `TODO.md`: "Web version: Sound
  is a little bit delayed") and by a Galaxy Eggbert web player (2026-11-11; there the game's own
  repeated death sounds and terrain-rebuild stalls make it worse, sent to that game). Analysed from
  the code only, not measured in a browser. Remedies, none taken: `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`
  (halves the constant part, more dropouts on long frames); a CNA web audio device on an
  AudioWorklet (`-sAUDIO_WORKLET`, needs cross-origin isolation for `SharedArrayBuffer`); running the
  game off the main thread (pthreads, same hosting requirement). An independent review the same day
  agreed and added two smaller effects: while the AudioContext is still suspended (before the first
  user gesture) SDL's timer mixes and discards each block (`SDL_emscriptenaudio.c:276-293`), so an
  effect played then is never heard; and the first block after opening a 44.1 kHz stream on a 48 kHz
  context comes up six or seven frames short (SDL's resampler padding; CNA's `FeedStream` supplies
  exactly the approximate amount SDL asks for), a sub-millisecond gap. Mobile Eggbert's 20 Hz
  update adds 0-50 ms before `Play` on top. The owner decided on 2026-11-11 to leave the web audio
  path unchanged for now.
- **Three audio reports from the 2026-07-17 audit stay unconfirmed, not open:** high-pitched sound
  effects, missing audio files, and distorted or sped-up gameplay audio. They were never
  reproduced against the game they came from, which is not available. The audit narrowed the
  pitch report to a wrong sample rate reaching CNA before the mixer -- the class CBIND-146 fixed
  (`51d9c84cc`, 2026-10-02, an XNA-built sound's content sample rate).
- One content-pipeline residue against genuine XNA output, not visible in the 7,726-reference
  sample sweep (`tools/xna-sample-sweep/README.md`, *What is left*): FBX bone transforms keep an
  ulp-level residue that grows with depth, inside the float tolerance (`XNASWEEP-228`..`232`),
  which is the FBX SDK's own arithmetic. (The generated `.x` normal zero-sign residue of
  `XNASWEEP-231` is fixed: CNA-FIX-L49 generates in the file's own space, measured 2026-11-09.)
  (`MeshHelper.OptimizeForCache` is exact on all 1,210 probes since `XNASWEEP-149`'s round six.)
- **`.x` duplication without `MeshNormals`** (KI-XVERT-WINDOW, 296cb350d, evidence
  `../cnaplan/evidence/rev002/ki-xvert-02/`): the VertexDuplicationIndices rule with normals is
  measured on genuine D3DX (211 probes, 86 of them blind); without `MeshNormals` a non-identity map
  also changes the generated normals (probe NM2 answers a zero normal), which CNA does not model (it
  treats the map as inert there). `target.x`, on which XNASWEEP-198 was measured, is not on disk;
  target-like probes behave as the rule predicts, which is an inference, not a re-measurement.
- **A reliable SystemLink packet in flight at `Dispose` can be lost** (UB-012, reported by Galaxy
  Eggbert). As in XNA (SendDataNow only fills the session's packet buffer; `NetworkSession.Update`
  hands it to the native layer, DispatchUpdateKernelCall IL_0042/IL_0062), data given to `SendData`
  and followed by `Dispose` without an `Update` is never sent. Data an `Update` already put on the
  wire but not yet acknowledged is dropped too, because ENet's `enet_peer_disconnect` resets the peer's
  queues; XNA's native behaviour for that case is unmeasured.
- **Local/SystemLink same-machine host departure is implemented under owner policy (2026-11-08, HND-NET-01).** When the host gamer leaves and another local gamer remains on the authority machine, the machine stays host. At the next session Update the first remaining local in join order becomes Host, independently of AllowHostMigration. GamerLeft is raised before HostChanged, whose OldHost is the actual departed host and NewHost the successor. SystemLink broadcasts both changes; a later welcome identifies the successor so a later joiner's EndJoin can complete. With no remaining local, the existing end/machine-migration behavior is preserved. Managed XNA IL specifies result processing, not this election policy. Device-free regressions cover policy and event ordering; the peer, later-join and leak-enabled runs remain integration gates until their recorded commands pass. PlayerMatch/Ranked service host authority is unchanged.
- **Local/SystemLink sign-out removes membership under owner policy (2026-11-08, HND-NET-01).** The session observes SignedOut and removes the matching local identity at its next Update, raising GamerLeft locally and publishing that individual departure to peers. Other locals keep the session; the last local ends it with ClientSignedOut through the existing CNA end path. A pending AddLocalGamer whose identity was disposed before completion is refused by Update with ObjectDisposedException("gamer") and never joins; other results/notifications of that update still complete. The exception follows the closest existing disposed-entry refusal, extended to completion as CNA policy. Tests cover backend/dispatcher sign-out, host/nonhost/last-local cases, pending adds, duplicate notifications, reused slots, throwing subscribers and disposal/destruction. SystemLink peer execution and leak qualification remain integrator gates. Online/service account and session authority is unchanged. Signed-out identities retained in PreviousGamers keep their original SignedInGamer pointer.

## External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **FNA3D's SDL GPU driver applies no MultiSampleMask** (2026-11-03, MTH-CONF-005). It writes the mask
  into `SDL_GPUMultisampleState.enable_mask`/`sample_mask`, which SDL documents as reserved
  (`FNA3D_Driver_SDL.c` ~1640-1650 at the pinned fork). CNA reports
  `RendererFeature::MultiSampleMask=Unsupported` for that driver (Supported on FNA3D's OpenGL
  driver), and `blend_msaa_boundaries` skips the mask cases with that reason. SDLGPU itself is
  Restricted: only all/none masks.
- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - FNA3D (resolved 2026-11-08, CNA-FIX-E01a/b/c and fork TQ-FORK-E01): `SkinnedEffect.VertexColorEnabled`
    is a CNAEXT extension (Refresh has no such property), so its checks pin CNA's extension contract;
    back-buffer and render-target sample requests are normalized as QueryMultiSampleCount does and the
    native count is reported; the default `Depth24` propagates (now pinned by a test). FNA3D-4 upload ordering (ordinary cube/volume/mipmapped textures
    and render-target updates after queued clears/resolves) is fixed by fork `912e26d` at the
    current pin. The owner qualified native fork pre/fixed/mutant witnesses. CNA's interim
    synchronous one-texel render-target upload readback is removed; the existing RT cube/mip
    tests and `texture_setdata_after_draw` conformance have since passed on both drivers
    against the published repaired pair (TQ-FNA-QUAL-01). There is no CPU stall or implicit Present in the fork repair;
    extra submissions per texture update are deliberate. TQ-FNA-QUAL-01 now passes those
    two RT witnesses in aggregate/focused binaries and both ordinary texture conformance legs
    on each driver. FNA3D-5's negative/native-bound conversion and positive-level CNA
    SpriteBatch witnesses pass on both drivers; imported GL textures, bias/minification and
    D3D11 remain unqualified by that point-filtered 4x4 test.

- **LeakSanitizer reports leaks in the pinned SDL** (measured 2026-10-22/23 during the museum
  repairs): about 50 KB in about 910 allocations from initialization to destruction with no CNA code
  linked (lavapipe and RADV), so an SDLGPU sanitizer run does not end at exit 0 under
  LeakSanitizer; CNA suppresses none of it. The MojoShader SPIR-V attribute-fixup leak measured then
  (8,792 bytes in 513 allocations per device), FNA3D's SDLGPU device-array and vertex-binding
  cache leaks, and the dumpspirv and trace-lock leaks are fixed on the forks (2026-10-26; the owner
  authorized fork changes that day). MojoShader's `tests/cna` runs leak-free under
  AddressSanitizer; FNA3D's does on OpenGL and OpenGL ES 3, and on SDLGPU every remaining report
  is SDL's own (X11/xkb and Vulkan ICD initialization reached through device creation).
- **`DIRECTX9` under Wine with DXVK 2.6.0: `DirectX9_CompiledEffects` hangs or aborts in DXVK's
  device teardown; DXVK 3.1.1 does not** (measured 2026-10-17, `cmake-build-mingw`, Release,
  `CNA_DIRECTX9_COMPILED_EFFECTS=ON`). Its 31 gtests were run 30 times on each version in one Wine
  prefix that differed only in the DXVK DLLs: 2.6.0 (Debian's package, `/usr/lib/dxvk`) failed 9 of
  30 -- hangs inside the first test, `CapabilityAndPublicRuntimeContract`, and `std::system_error`
  "Thread not joinable", a string of DXVK's own thread wrapper -- while 3.1.1 passed 30 of 30, and
  with it the whole `DIRECTX9` ctest suite passes 60/60 twice. On 2026-10-15 teardown markers had
  shown every renderer object released and the final `IDirect3DDevice9::Release` never returning.
  CNA was not changed for it. `scripts/run-wine-dxvk9.sh` still defaults to `~/.wine-cna-d3d11`,
  which links the system 2.6.0; `CNA_D3D9_WINEPREFIX=~/deps/wineprefix-cna-dxvk-3.1.1` selects a
  copy with 3.1.1 (machine-local, outside Git); the script's DXVK gate accepts the `v` official
  builds print before the version. `CnaGraphicsTests.exe` is run as eight
  `GTEST_TOTAL_SHARDS` shards because one 2,956-test process under 2.6.0 dies or hangs at a varying
  point.
