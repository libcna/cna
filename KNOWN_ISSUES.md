# CNA known issues and current state

This file lists what is broken or limited in CNA now. Subsystem behaviour is in each subsystem's
page under `docs/` (index: `docs/README.md`), deliberate divergences from FNA and XNA are in
`docs/xna-fna-behavior-decisions.md`, and finished work is Git history. State of 2026-11-09.

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
- **Tests.** The last full run, sweep 17 on 2026-11-09, built seven configurations (multi, sdlgpu,
  webgpu, fna3d, software, headless and opengl33; about 102,000 CTest cases). Its only failures
  were two regressions from that day, both fixed and rerun green. GPU and window tests must run
  through `tools/platform/run_gpu_tests_private.sh`, never on the live desktop.

## Defects

- **`.x` VertexDuplicationIndices window disagrees with genuine XNA on its own fixture (observed
  2026-11-09, not fixed).** For `x_vertex_duplication.x` (map `0,1,2,3,3,3`; faces `(0,1,2) (0,4,5)
  (1,3,4) (1,2,3)`) CNA's XNASWEEP-198 rule drops both faces naming two vertices of group 3 and answers
  4 positions / 6 indices, as the fixture's generator comment expects; the genuine importer answers 6
  positions / 9 indices (one face dropped), both in today's bits run and in the committed reference
  `tests/reference/xna40/model/model-import-oracle.json`. No test checks this fixture. The rule was
  measured on `target.x` and is documented as not understood outside that window; re-measure with
  per-face probes before changing it.
  First probes (CNA-FIX-XVERT-01, evidence `../cnaplan/evidence/rev002/hnd-xvert-01/`, design in its
  DESIGN.md): with map `2,2,2,3` both faces `(0,2,3)` and `(1,2,3)` stay (CNA drops both), which only the "two lowest file
  indices of a group" candidate (D_pair) predicts -- still to be checked against target.x, which is not
  available locally, and the fixture's full vertex remapping (source position order `0,1,2,3,5,4`) is not
  explained by any face rule. No production change to the duplication window yet. The mesh topology is
  D3DX's own (XImporter calls `D3DXLoadMeshHierarchyFromXW`), so only black-box probes can settle it.
- **Genuine-XNA native teardown fault (TQ-SWEEP14-02), unattributed.** The harness ownership gap is
  repaired (2026-11-08); the one-off Wine fault of sweep 14 did not reproduce with the old or the
  new harness. If it recurs, capture `+loaddll,+seh` and a WineDbg stack; never suppress the nonzero result.
- **FNA3D-3 and FNA3D-8 are open in the FNA3D fork** (`docs/fna3d-dependency-defects.md`). FNA3D-3:
  the SDL GPU driver applies no MultiSampleMask (see External and driver limitations). FNA3D-8:
  presentation with a source or destination rectangle. The OpenGL driver converts the rectangles
  (fork 4ff109a), and the SDL GPU driver clears the swapchain outside a partial destination
  (fork 76a76cf). CNA still refuses a non-default Present on FNA3D.

### Divergences from XNA found on 2026-11-09, not fixed

- XACT: effect families 4/6 and loop-region waves unmeasured (banks authored in
  `../cnaplan/evidence/rev002/hnd-xact/banks/`, `run-native.sh run effect`); random/shuffle without NEW_ON_LOOP and raw 5-254 follow the rule by
  extension only; `check-xact-prefetch-oracle.py` fails "Probe source changed" since HND-XACT-04.
- Graphics: effect ObjectDisposedException names use the resource Name, not XNA's type name; stock
  EffectParameter::SetValue(Texture) never refuses a disposed texture; an unwritten stock matrix
  parameter reads identity (XNA: zero), so a stock clone made before the source's first Apply draws;
  SpriteEffect clones go through the custom path; stock classes read fields at draw time rather than
  latching at Apply; DIRECTX9 stock-clone path compiled only.
- C API: `cna_effect_clone` does not extend parameter-level/shader-unit C retention; content tags of
  meshes/parts have no C route; HND-TQ-22 C route (ObjectValue handles, ABI 0.61) not done.
- Content: Refresh closes an uncompressed XNB's stream twice (CNA once); Refresh skips totalLength for
  non-seekable streams; null OpenStream is ArgumentNullException in XNA (CNA ContentLoadException);
  texture dimension <= 0 is ArgumentOutOfRangeException in XNA; LZX failures surface per frame;
  sharp-runtime PeekChar on a non-seekable stream throws (.NET returns -1).
- Out-of-range ShaderIndex on a stock clone: CNA refuses at Apply; genuine XNA unmeasured.
- Tooling: worktrees with a symlinked `third_party/SDL` fail SDL staging (fix proposed on the local
  branch `work/hnd-gfx`, 0f5474490, not integrated).
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
  n+1 passes (HND-XACT-05, 56 native cells, event types 1/3); effect families 4/6, loop-region waves,
  random/shuffle variations and raw 5-254 are unmeasured. Still open: exact hidden timing/RNG,
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
- XnaPipelineFinalAuditIsGreen remains an evidence-metadata gate, not runtime qualification
  (CNA-FIX-L05 / MTH-PRE-005). Both it and CApiRouteTestCoverage now separate declaration presence,
  configured CTest registration and validated retained execution. Source scans exclude comments,
  literals and literal false branches; unknown conditions, macro expansion and build inclusion
  remain unevaluated. Optional --execution-record joins ctest_evidence.py artifacts; the explicit
  --require-execution gate refuses missing/disabled/skipped witnesses and wrong evidence categories.
  Passing empty bodies still establish no assertion coverage: runtime qualification stays
  not-established and assertion counts unknown. The 23 new CPU-only regression cases and five
  selected metadata CTests pass in the software tree; no broader runtime/interop/sanitizer or GPU
  qualification is claimed. See docs/xna-content-pipeline-final-audit.md and the route-presence
  evidence section in docs/c-api/README.md for scope and capture identity limits.
- CNB read limits are not a total process-memory budget. Owned input, expanded chunks, decoded
  objects and GPU resources can coexist, and concurrent asset loads remain application-owned.
  Shared animation ranges additionally obey materialized track/key element totals; see
  `docs/cnb-format.md` §12.1 for the bounds and their scope.
- Original CNBF-105/108 payload hashes and raw timing samples are absent, so their historical
  tables cannot be replayed exactly. Corrected probes validate covered I/O/codec paths for new
  stable inputs; they do not establish full CNB-load performance. Production CRC tests compare
  selected dispatch against the table, not every forced hardware/fallback architecture; see
  `docs/cnb-compression-measurements.md` and `docs/cnb-mmap-measurements.md`.
- The historical August clean-build matrix cannot be reconstructed from current defaults alone.
  Current benchmark/report tools record effective cache values and source/dependency identities;
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
