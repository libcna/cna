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


### Other FNA3D behaviour recorded in `handoff.md` §5.3

`SkinnedEffect.VertexColorEnabled` is ignored outright and saturates before the fold; the back buffer
keeps a 3, 5 or 7 sample request unrounded while render targets round it down;
`GraphicsDeviceManager`'s default `Depth24` request is not applied in `graphics_adapter_query`.

## Requalified historical reports

### FNA3D-7: current malformed-symbol rejection passes; historical crash remains unattributed

The integrator reported abnormal termination in 2026-11-05's TQ-085/TQ-087 work.
TQ-FNA-QUAL-01 (2026-11-07) rebuilt both aggregate and focused renderer executables
at CNA `b7ab4afa4` with the published FNA3D `5c0b9b8c13f5cca36a336d481cd5f696d0223275`
and its MojoShader `6ae750b59e6d6e08eadf14d241e38d051fae45a2` submodule.
`Fna3dCompiledEffectTest.MissingShaderParameterSymbolIsRejectedNormally` passes in
all four explicit OpenGL/SDLGPU and aggregate/focused combinations. The retained
BasicEffect.fxb byte 25018 XOR 0x10 still changes the CTAB symbol's `o` into DEL;
the expected `Shader parameter not found in effect.` refusal is unchanged.

Two additional successful debugger runs on SDLGPU capture the actual cleanup stack
and loaded library paths. At `SDLGPU_AddDisposeEffect`, the rejected allocated effect
has error_count 1, a valid shader context, all nine shader callbacks and both resolved
allocators. The stack runs through CNA's `DisposeFailedNativeEffect` and native FNA3D
disposal; cleanup completes and both processes exit normally. The unmodified regression
and native SDL_GPU/Vulkan driver identity are retained alongside the requested
environment.

These are current Linux/AMD Radeon 780M observations, not an attribution or a repair
of the old crash. No current failure was reproduced, so no speculative runtime guard,
fork change or skip was introduced. A future recurrence needs its exact executable,
loaded-library/source identities and a symbolized failing stack. Evidence and SHA-256
identities: `../../cnaplan/evidence/rev002/fna-current-qualification-b7ab/fna-receipt.json`.

## Driver behaviour CNA must design around (not defects)

- **The OpenGL driver creates and uploads every texture through texture unit 0** (`BindTexture` in
  `FNA3D_Driver_OpenGL.c`, called from texture creation and `SetTextureData`). Any texture CNA creates
  lazily between verifying a sampler and drawing silently replaces unit 0's binding. CNA therefore
  creates every lazy resource before binding samplers: the null environment cube (`be814e4af`,
  MTH-REV-001) and the null second texture of `DualTextureEffect` (`e06d46978`, TQ-017), each with a
  cold-device regression in `Fna3dPipelinePixelTests.cpp` / `StockEffectNullTextureTests.cpp`.

## Fixed on the forks

### TQ-SWEEP12-10: BEM matrix metadata was treated as a sampled image

MojoShader `100025ed2f1b423ae2ed215a8eb27bdbcb3ce85b`, recorded by FNA3D
`f2b34dccd9a5988816efef62c9a5de8df6ec6189`, appends `matrix_only` to sampler reflection.
The ps_1_4 BEM instruction uses a destination-numbered bump matrix without sampling a texture.
Its reflected sampler remains present for the uniform layout, but now explicitly distinguishes
this use from a real image lookup. `texbem` and uniform offsets retain their previous values.
Any real texture instruction on the same stage keeps `matrix_only` false, in either order.
SDLGPU binds its neutral texture for a matrix-only slot; an unbound sampled slot still refuses.

The fork regression covers six destination stages, BEM modifiers, BEM plus TEXLD before/after,
and TEXBEM controls on SPIR-V/glspirv. Normal and ASan/UBSan harnesses pass; an overwrite mutation
fails the mixed-stage control. The opt-in private SDL GPU device leg and FNA3D's 29-test harness
pass. CNA's BEM pixel witness, SPIR-V policy and missing-sampled-texture control pass privately.

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
is removed. TQ-FNA-QUAL-01 subsequently qualifies the unchanged CNA stencil witness
on both OpenGL and SDLGPU in aggregate and focused executables (four passes) at
`b7ab4afa4`. This is current CNA integration evidence in addition to the fork run.

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
and `MipmappedTargetUploadsFollowQueuedResolve` require SetData after the queued clear/resolve
with no intervening GetData/Present. TQ-FNA-QUAL-01 now passes both unchanged witnesses
on both drivers in aggregate and focused binaries, plus both original Texture2D/Cube
`texture_setdata_after_draw` conformance legs on each driver. The evidence records
the current published pair, actual driver identities and executable/library hashes.

### FNA3D-5 (attributed part): unsigned and stored mip bounds

**Status:** fixed in libcna/FNA3D `912e26d` (2026-11-06), delivered by
`5c0b9b8c13f5cca36a336d481cd5f696d0223275`. XNA Refresh `SamplerState.Apply` IL_0342-0355 passes
`cachedMaxMipLevel` as uint32. SDL's min_lod no longer becomes negative and GL's base level is
bounded to stored levels: requests 0,1,2,-1,99,0 on a three-level chain normalize to 0,1,2,2,2,0.
Fork state-boundary checks pass and conversion mutants fail; the owner qualified native
`mip_clamp` pixels on OpenGL and SDL_GPU.

TQ-FNA-QUAL-01 (2026-11-07) also qualifies the previously open positive-level CNA
SpriteBatch report at `b7ab4afa4`. Both the original
`SdlGpuTextureTransfer.SamplerMipClampUsesMostDetailedAllowedLevel` and FNA3D-owned
`Fna3dPipelinePixels.SamplerMipClampUsesMostDetailedAllowedLevel` pass on both OpenGL
and SDLGPU in aggregate and focused binaries: eight test executions, no skips.
The unchanged literal oracle checks every pixel of a 4x4 target after requests
0,1,2,-1,99,0: red,green,blue,blue,blue,red. Driver output confirms native
OpenGL/radeonsi and SDL_GPU/Vulkan/RADV on AMD Radeon 780M. This qualifies those
point-filtered upload/state/SpriteBatch paths; imported GL textures, minification/bias
and D3D11 remain outside scope. Evidence:
`../../cnaplan/evidence/rev002/fna-current-qualification-b7ab/fna-receipt.json`.
