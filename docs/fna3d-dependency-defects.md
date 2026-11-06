# FNA3D and MojoShader: dependency defects and fork repairs

CNA's FNA3D renderer family draws through two pinned forks, [`libcna/FNA3D`](https://github.com/libcna/FNA3D)
and [`libcna/mojoshader`](https://github.com/libcna/mojoshader) (pins in
[`fna3d-renderer.md`](fna3d-renderer.md#dependency-and-source-pin)). The owner authorized AI fork changes
on 2026-11-06 for the commissioned repair lanes; CNA configure never patches either checkout.
This page records defects, fixed repairs, reproducers and remaining qualification. It is the hand-over list for whoever
maintains the forks; `handoff.md` §5.1/§5.3 keeps the short CNA-side entry for each.

Each entry says how far the attribution is established. "Confirmed" means the faulty code path was
read and a CNA test reproduces the symptom on the GPU; "probable" means the symptom is reproduced but
the cause is not yet located.

## Open

### FNA3D-8: region and override presentation produces wrong native client pixels

**Status:** confirmed (2026-11-06, GAP-08a). **Drivers:** OpenGL and SDLGPU, pinned FNA3D
`10f650b9d0f6bb0e6f3f9378833fbd807dfe8cde`. The integrating owner ran the retained forwarding
implementation on a private Xwayland display (AMD Radeon 780M, Mesa 25.0.7). On both drivers the
fixture's ordinary-Present magenta control **passed**, then all nine `Gap08PresentedPixels` cases
failed. These were native-client XGetImage observations, not backbuffer-only comparisons or
failures at texture/buffer upload. Evidence: campaign GAP-08a
`integrator-evidence/wg08a-fna3d-gl.log` and `wg08a-fna3d-sdlgpu.log`.

- `SourceOnlyPresentsTheSelectedQuadrantAcrossTheClient` never produced the required white client
  from source `(4,4,4,4)`. `OversizedSourcePresentsItsIntersectionWithTheLiveBackbuffer`,
  `SourceOnlyDoesNotLetterboxAnAsymmetricSource`, `DestinationRetainsEveryPixelOutsideTheRequestedRegion`,
  `ScalingPlacesFourQuadrantsInTheDestination`, `DestinationClippingRetainsTheOriginalScale` and
  `NativeSizeBackbufferStillHonorsASourceRectangle` also failed on both drivers.
- `NativeOverrideUsesTheForeignClientsBoundsAndKeepsOwnWindowUntouched` failed its foreign-client
  green-region assertion on both drivers. `NullRectanglesOverrideTheWholeForeignClient` also
  failed overall on both drivers. The original own-window comparisons in these two cases had an
  overlapping-X11-window exposure artefact (also reproduced with SOFTWARE); those comparisons
  alone are not evidence of a driver write. The independently failing source/destination and
  foreign-region assertions establish the region defect. The SOFTWARE fixture now separates
  windows and re-establishes/captures its literal magenta control before comparing untouched pixels.
- Source path supporting attribution: OpenGL `OPENGL_SwapBuffers` reads `srcW/srcH` and `dstW/dstH`
  as extents, then passes them as second corners to `glBlitFramebuffer`
  (`src/FNA3D_Driver_OpenGL.c:1373–1390,1466–1471`); `(4,4,4,4)` becomes an empty span. Its direct-window
  branch (`:1494–1498`) ignores rectangles. It activates the original window at `:1361` and swaps
  the override at `:1489` without an apparent context rebind (activation at `:1239`).
- SDL GPU uses `SDL_GPU_LOADOP_DONT_CARE` for the destination swapchain texture
  (`src/FNA3D_Driver_SDL.c:1207`), which does not preserve untouched destination pixels. The native
  failures confirm the unsupported promise; they do not uniquely attribute every symptom to this
  load operation. Exact native XNA clipping/error parity remains a separate unexecuted Refresh probe.

**CNA meanwhile:** non-default `GraphicsDevice::Present` on FNA3D throws a named
`NotSupportedException` citing this dependency defect; ordinary Present remains supported. CNA
never adopts an override target for this refused request. The unusable swap-forwarding override
was removed, and `Gap08PresentBindingTest.Fna3dRefusesEveryNonDefaultPresentBeforeNativeAdoption`
pins independent source/destination/window refusal, no adoption/output, and ordinary controls.

**Human reproducer:** the GAP-08a evidence directory retains
`binaries/forwarding-diagnostic/CnaGraphicsTests` and
`diagnostics/Fna3dPresentedPixels-report-only.cpp`, with the forwarding implementation and strict
native assertions. Run through `tools/platform/run_gpu_tests_private.sh --exec` with
`--gtest_filter='Gap08PresentedPixels.*'`, `CNA_GRAPHICS_RENDERER=FNA3D`, `SDL_VIDEODRIVER=x11`, and
`FNA3D_FORCE_DRIVER=OpenGL` then `=SDLGPU`. Updated retained fixtures separate the clients; no native
rerun of that fixture revision is claimed yet. The current checked-in native fixture exercises
SOFTWARE only. An FNA3D-8 fork repair must precede any new presentation support claim;
the other repairs below do not cover this defect.


### GAP-13: six legacy opcodes have no MojoShader SPIR-V emitter

**Status:** source-confirmed missing implementations at MojoShader
`6ae750b59e6d6e08eadf14d241e38d051fae45a2`, recorded by FNA3D
`5c0b9b8c13f5cca36a336d481cd5f696d0223275` (2026-11-06).
`TEXREG2AR`, `TEXREG2GB`, `TEXREG2RGB`, `TEXDP3TEX`, `TEXM3X2DEPTH` and `TEXDP3` still report
`<OPCODE> unimplemented in spirv profile` on SPIR-V/glspirv routes. This affects CNA SDLGPU,
WebGPU (native SPIR-V and SPIR-V-to-WGSL) and FNA3D's SDL_GPU driver, not the GLSL/native D3D9
instruction envelope.

BEM and TEXDEPTH are implemented at this pin. Fork tests validate/link modules and evaluate literal
bump/depth arithmetic, including six destination stages, aliasing/modifiers, zero divisor and
FragDepth metadata. The owner qualified the fork witnesses on real GPUs. CNA acceptance now
requires BEM and register TEXDEPTH pixels; native renderer and browser WGSL runs remain separate
integration gates. `Gap13CompiledEffectSpirvTest.EveryOpcodeMatchesRepairedSpirvEnvelopeInEnabledAndDisabledBuilds`
retains all eight capability refusals in OFF builds, executes both repaired pixel contracts in
ON builds and checks the other six exact named refusals. The current shared corpus is 44 cases.

### FNA3D-3: the SDL GPU driver applies no MultiSampleMask

**Status:** confirmed (2026-11-03, MTH-CONF-005). **Driver:** SDLGPU.

The driver writes the mask into `SDL_GPUMultisampleState.enable_mask`/`sample_mask`, which SDL
documents as reserved (`src/FNA3D_Driver_SDL.c` ~1640-1650 at the pin), so the mask has no effect.

- **CNA meanwhile:** reports `RendererFeature::MultiSampleMask=Unsupported` for that driver
  (Supported on the OpenGL driver); `blend_msaa_boundaries` skips the mask cases with that reason.
- **Suggested repair:** depends on SDL exposing sample masks; until then the driver should refuse
  rather than silently ignore a non-full mask.


### FNA3D-5 (positive-level part): reported SpriteBatch mip selection remains open

**Status:** positive-level CNA behavior remains unqualified; the attributed unsigned/native-bound
conversion repair is recorded under Fixed below (2026-11-06).

`SdlGpuTextureTransfer.SamplerMipClampUsesMostDetailedAllowedLevel` uploads a distinct colour to each
mip of a 4x4 texture and draws it with Point filtering and `MaxMipLevel` 0, 1, 2, -1, 99, 0. XNA
`SamplerState.Apply` IL_0342-0355 forwards the request as uint32 to `D3DSAMP_MAXMIPLEVEL`, so a
positive request 1 must sample mip 1. The old FNA3D pair sampled the wrong level through this CNA
SpriteBatch witness (SDLGPU read mip 0 for request 1; OpenGL differed again).

The owner qualified the fork's direct mip witness, including negative/past-last bounds. That does
not by itself qualify CNA SpriteBatch, per-level transfer or adapter/state behavior. Rerun the
new FNA3D-owned `Fna3dPipelinePixels.SamplerMipClampUsesMostDetailedAllowedLevel` and the original
`SdlGpuTextureTransfer` test on both drivers. Keep the positive-level report open until these actual
CNA pixels pass; imported GL textures, minification/bias and D3D11 remain outside this repair.

### FNA3D-7 (unattributed): malformed shader-symbol Effect crashes with the SDL GPU driver

**Status:** reported by the integrator (2026-11-05, TQ-085/TQ-087); the null callback and owning
repository are **not established**. **Driver context:** explicit `FNA3D_FORCE_DRIVER=SDLGPU`.
This is an investigation entry, not an attribution to either fork.

- **Reproducer:** `Fna3dCompiledEffectTest.MissingShaderParameterSymbolIsRejectedNormally` in
  `modules/renderers/fna3d/tests/CNA/Internal/Renderers/Fna3d/Fna3dCompiledEffectTests.cpp`.
  XOR BasicEffect.fxb byte 25018 with 0x10; the shader CTAB symbol `WorldInverseTranspose` then
  contains DEL (0x7f) in place of an `o` (0x6f). Expected: an ordinary rejection containing
  `Shader parameter not found in effect.` The reported result is abnormal termination without a test result.
  The OpenGL CTest context passes; that does not qualify the SDL GPU path.
- **Read-only trace at FNA3D `10f650b9d0f6`, MojoShader `03b686fe1a33`:** `SDLGPU_CreateEffect`
  (`src/FNA3D_Driver_SDL.c:3991-4037`) supplies all nine shader callbacks. MojoShader
  `MOJOSHADER_compileEffect` (`mojoshader_effects.c:1533-1554`) resolves default allocators;
  `readsmallobjects` returns after `findparameter` records the missing-symbol error
  (`:384-395,1063-1069`). CNA validates the returned errors and disposes the allocated effect.
  `SDLGPU_AddDisposeEffect` (`FNA3D_Driver_SDL.c:4062-4079`) reaches
  `MOJOSHADER_deleteEffect` (`mojoshader_effects.c:1726-1823`), whose shader deletion callback
  is explicitly `MOJOSHADER_sdlDeleteShader`. None of those callbacks is null by initialization.
  A symbolized backtrace is required before naming a null callback or proposing a fix.
- **CNA meanwhile:** retain the failing rejection assertion and explicit-driver reproduction;
  do not infer driver identity from the environment when designing a future guard. A temporary
  skip would suppress a malformed-input contract and is not justified by this source evidence.
  If a fork callback is confirmed, prefer a CNA preflight guard for that malformed structure;
  use a narrowly reasoned driver-specific skip only when no safe CNA guard is possible, and keep
  a native reproducer for human fork maintainers. This investigation is not repaired by the current pin.
- **Next evidence:** on a private display, capture a complete symbolized backtrace with the same
  explicit SDL GPU driver in aggregate and focused executables, including loaded-library/source
  identities. Inspect `effectData->ctx` after compile returns and before disposal to distinguish a
  compiler failure from cleanup or driver dispatch. Do not relabel this entry "confirmed" until
  that trace identifies the faulty path.

### Other FNA3D behaviour recorded in `handoff.md` §5.3

`SkinnedEffect.VertexColorEnabled` is ignored outright and saturates before the fold; the back buffer
keeps a 3, 5 or 7 sample request unrounded while render targets round it down;
`GraphicsDeviceManager`'s default `Depth24` request is not applied in `graphics_adapter_query`.

## Driver behaviour CNA must design around (not defects)

- **The OpenGL driver creates and uploads every texture through texture unit 0** (`BindTexture` in
  `FNA3D_Driver_OpenGL.c`, called from texture creation and `SetTextureData`). Any texture CNA creates
  lazily between verifying a sampler and drawing silently replaces unit 0's binding. CNA therefore
  creates every lazy resource before binding samplers: the null environment cube (`be814e4af`,
  MTH-REV-001) and the null second texture of `DualTextureEffect` (`e06d46978`, TQ-017), each with a
  cold-device regression in `Fna3dPipelinePixelTests.cpp` / `StockEffectNullTextureTests.cpp`.

## Fixed on the forks

The fork changes the owner authorized on 2026-10-26 (SDLGPU device-array and vertex-binding cache
leaks, the MojoShader SPIR-V attribute-fixup, dumpspirv and trace-lock leaks) are listed in
`handoff.md` §5.3 under the LeakSanitizer entry.


### FNA3D-1: OpenGL two-sided stencil follows a stale front face when culling is off

**Status:** fixed in libcna/FNA3D `912e26d` (2026-11-06), delivered by
`5c0b9b8c13f5cca36a336d481cd5f696d0223275`. OpenGL selects ordinary/CCW stencil faces at each draw
from the current front face and render-target orientation, independently of culling. XNA Refresh
`DepthStencilState.Apply` IL_0154-019b / IL_01b4-0242 installs the two states independently;
D3D9's ordinary face is clockwise.

Originally confirmed 2026-11-03 (TQ-017): stale `glFrontFace` with CullMode.None swapped the
ordinary/CCW state. The owner qualified fork `vface` stencil pixels on OpenGL and SDL_GPU with
pre-fix and mutant failures. CNA's `Fna3dPipelinePixels.TwoSidedStencilUsesClockwiseOrdinaryState`
retains Replace=5, Invert=255 and false/true/false winding assertions; its obsolete OpenGL skip
is removed. CNA post-pin execution is still an integration gate, not a result of the fork run.

### FNA3D-2: MojoShader leaks an effect object when two small-object records name the same index

**Status:** fixed in libcna/mojoshader `04616e1` (2026-11-06), included in MojoShader
`6ae750b59e6d6e08eadf14d241e38d051fae45a2` and FNA3D
`5c0b9b8c13f5cca36a336d481cd5f696d0223275`. Both small and large object readers reject a second
payload for a defined slot before allocation/compilation. A parser-local occupancy table is shared
across readers; repeated references remain valid. Fork `cna_duplicate_object_tests` covers small
strings/mappings/shaders, large shaders, cross-table definitions, empty records, distinct controls
and allocation failures. Plain and ASan/UBSan ownership checks pass and deliberate mutations fail.
The sandbox prevented LeakSanitizer thread attachment; **owner LSan and original CNA fuzz-seed
replay remain pending**. A `detect_leaks=0` run is not leak-sanitizer qualification.

Historical witnesses (2026-11-03, MTH-SAFE-007): seeds 20261103001801 and 20261103032166 in
`cnaplan/evidence/safe007/long-campaign/findings/compiled-effect/`; changing BasicEffect object
index 24 to 16 compiled ShaderFunction16 twice and leaked the first payload. The original LSan
measurements used legacy MojoShader 6333f74; the old CNA fork pin 03b686f had the same reader by
inspection. Replay each original record with
`python3 tools/fuzz/reproduce.py <record>.json --regenerate`, verifying the actual executable uses
the repaired pair, with no readsmallobjects/readlargeobjects suppression. Historical local
suppression/weekly failures are not a clean replay of this pin. The separate possible
pass-constant preshader overwrite remains outside this repair.

### FNA3D-6: MojoShader leaks a compiled shader when two large-object records name the same index

**Status:** fixed in libcna/mojoshader `04616e1` (2026-11-06), delivered through MojoShader
`6ae750b59e6d6e08eadf14d241e38d051fae45a2` and FNA3D
`5c0b9b8c13f5cca36a336d481cd5f696d0223275`. The same shared occupancy guard described for
FNA3D-2 rejects duplicate large shader records and small-to-large slot redefinition before compiling
a replacement. The large/cross-table ownership tests pass and a disabled-large-guard mutant fails.
Owner LSan and original CNA fuzz-seed replay remain pending under the same qualification boundary.

Historical witness (2026-11-04, TQ-018): seed 20261104107187 in `cnaplan/evidence/tq018/evidence/`
leaked a large compiled shader (1050 bytes/14 allocations). The `.xnb` is beside its JSON; replay
`python3 tools/fuzz/reproduce.py ../cnaplan/evidence/tq018/evidence/exit-seed-20261104107187.json --regenerate`
with the repaired dependency identity and active unsuppressed LSan. Original measurement used the
legacy checkout; the old pinned fork had the same large reader by inspection. Local campaigns'
historical `leak:readlargeobjects` suppression is not evidence of a repaired-pin replay.

### FNA3D-4: SDL GPU texture uploads are ordered ahead of the frame's draws

**Status:** fixed in libcna/FNA3D `912e26d` (2026-11-06), delivered by
`5c0b9b8c13f5cca36a336d481cd5f696d0223275`. SDL_GPU submits prior upload/render work before a new
texture update, including noncycling cube/volume/mipmapped updates and deferred clear-only work.
No CPU wait or implicit Present is added; the repair deliberately adds submissions per update.
No throughput optimization is claimed.

Originally confirmed 2026-10-24 / 2026-11-03 (MTH-EXIT-003): new uploads ran ahead of earlier draws,
clears and mip generation. Fork call-boundary `sdlgpu_command` tests and native `texture_order`
pixels were qualified by the owner, including pre-fix and mutant failures. CNA's synchronous
one-texel render-target upload barrier is removed; format expansion, readback, upload mirrors and
resolve-on-unbind remain intact. Existing CNA `HalfFloatCubeTargetTypedTransfersPreserveRenderedNeighbours`
and `MipmappedTargetUploadsFollowQueuedResolve` are the post-pin gates: SetData must follow the
queued clear/resolve with no intervening GetData/Present. Ordinary-texture conformance also needs
its CNA replay, not just the native fork witness.

### FNA3D-5 (attributed part): unsigned and stored mip bounds

**Status:** fixed in libcna/FNA3D `912e26d` (2026-11-06), delivered by
`5c0b9b8c13f5cca36a336d481cd5f696d0223275`. XNA Refresh `SamplerState.Apply` IL_0342-0355 passes
`cachedMaxMipLevel` as uint32. SDL's min_lod no longer becomes negative and GL's base level is
bounded to stored levels: requests 0,1,2,-1,99,0 on a three-level chain normalize to 0,1,2,2,2,0.
Fork state-boundary checks pass and conversion mutants fail; the owner qualified native
`mip_clamp` pixels on OpenGL and SDL_GPU. The positive-level CNA SpriteBatch report above remains
open, along with imported GL textures, minification/bias and D3D11 qualification.
