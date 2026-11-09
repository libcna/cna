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

**State on 2026-11-09 (FNA3D 76a76cf).** The OpenGL half has a fork repair (TQ-FORK-GLPRESENT-01,
4ff109a: source and destination rectangles become GL endpoints and presentation targets the override
drawable; the fork's `presentation_tests` check default-framebuffer pixels, window-override rectangles
included). No CNA-level native rerun of `Gap08PresentedPixels` exists for it. The retained diagnostic binary cannot provide one: it links
FNA3D 10f650b statically. A rerun needs CNA's forwarding reinstated in a probe build, and the
original forwarding patch no longer applies to `next`; the patch, the fixture and the 2026-11-06 logs
are kept in `cnaplan/evidence/rev002/fna3d-8-gap08a-retained/`. On SDLGPU, replacing
`SDL_GPU_LOADOP_DONT_CARE` with `LOAD` is not a repair: an acquired swapchain texture holds
whichever earlier frame last used that image (the swapchain rotates two or three images), not the
previous presentation. The owner first chose an explicit refusal (TQ-FORK-SDLPRESENT-01, FNA3D
`d001379`), but CNA's ordinary letterboxed Present passes a destination rectangle too and FNA3D
cannot tell the two apart, so the refusal blanked letterboxed frames
(`Fna3d_CrossFamily_SpriteBatch_Presentation_SdlGpuDriver`). By the owner's second decision the
driver now clears the swapchain to opaque black whenever the destination does not cover it exactly
and presents (TQ-FORK-SDLPRESENT-02, FNA3D `76a76cf`): letterbox bars and a region Present's
surroundings are defined black instead of undefined, while the previous pixels still cannot be kept.
The fork's window-free `sdlgpu_command_tests presentation` group pins the load op and clear colour.
CNA keeps refusing non-default Present on FNA3D: XNA keeps the pixels outside the rectangle, which
neither driver now reproduces natively, and the OpenGL half still has no CNA-level native rerun.


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

The owner-authorized repairs through TQ-FORK-SDLPRESENT-02 were published by
2026-11-09. CNA pins FNA3D `76a76cf98544f008584a5c6e0a1f5eb5b468795b` with MojoShader
`6f1e91b65c8dfe493ec1ace818b294b7181fa1b6`. Earlier identities below identify each
repair's delivery and qualification; they are not the current pin.

### TQ-FORK-EFFECT-TRUNC-01: truncated effect object headers reached an assertion

**Status:** fixed in published MojoShader `6f1e91b`, carried by FNA3D `0bfbe7f`.
`readui32` returns zero at end of input, and the small/large object loops read their
headers unchecked (upstream since 2016). A 1884-byte prefix of CNA's conformance effect
fabricated small record 0 of type VOID and reached `assert(0 && "Small object type
unknown!")` -- fatal for CNA's `CompiledEffectTruncationSweepTest` under `SDL_ASSERT=abort`.
Both loops now require complete headers and untyped slots are parser errors; the fork's
`cna_effect_tests` cover every four-byte prefix and every header cut, and three production
mutants fail them.

### TQ-TEXM3X2DEPTH-01: desktop depth APIs selected in OpenGL ES contexts

**Status:** fixed in published FNA3D `86e1fa0dd54e86246a1dc8fdb554f0e1fb856550`,
retained by the current pin. **Driver:** OpenGL, ES3 profile.

The OpenGL loader can expose non-null desktop `glClearDepth`/`glDepthRange` dispatch
pointers in an ES3 context. Clear and viewport previously selected those calls from
`supports_DoublePrecisionDepth` alone. OpenGL ES 3.2 sections 15.2.3 and 12.6.1 specify
`ClearDepthf` and `DepthRangef`; the fix requires a desktop context for the double APIs
(`src/FNA3D_Driver_OpenGL.c:1604,1854` at `86e1fa0`). ES3 uses the float entry points.

The fork's native matrix-depth witness first passed desktop OpenGL and SDLGPU but failed
ES3. The invalid clear calls left the initial depth-clear value at one. The literal
fraction/zero-depth probes independently predicted all 27 API errors, 12 wrong low-LESS
observations and 12 wrong zero-denominator GREATER observations in the retained log.
The 28-check device-free driver regression now exercises desktop/ES and pointer availability,
clear value `.25`, range `.125/.875`, clear bits and redundant-state caching. Separate
production mutations restoring each old dispatch independently fail and restore exactly.
See the fork-texm lane report and receipts for the original native failure and repair;
new CNA GPU witnesses still require the current integration's private runner gate.

### GAP-13: missing legacy SPIR-V emission and public sampler inference

**Status:** repaired in the published fork pair. The old MojoShader `6ae750b59e6d`
lacked TEXREG2AR/GB/RGB, TEXDP3/TEXDP3TEX and TEXM3X2DEPTH. `cc58b0d` and `3588784`
implemented the dependent-read/dot operations; `0dc4f2e` adds TEXM3X2DEPTH's implicit
FragDepth registration, PAD-time numerator capture, independent `_bx2` sources,
zero-denominator result one and consumed-register guards. It also fixes the GLSL
signed-source calculation. BEM and register TEXDEPTH remain covered independently.

MojoShader `93ee117` / FNA3D `866d402` additionally infer legacy sampler dimensions
from typed Effect Texture parameter/object links using CTAB register associations or
explicit pass sampler bindings. Explicit caller maps win. Generic declarations without
a typed association still default to 2D; conflicting per-pass dimensions for a shared
shader and inferred sampler arrays refuse by name. Opcode-specific 2D-only limits remain.

CNA's enabled SPIR-V envelope now executes all eight legacy instruction witnesses;
OFF builds still require every named CompiledEffects refusal before translation. The
44-case corpus preserves its old assertions and adds full 8x8 plain/signed/mixed/zero
matrix-depth checks. Separate public replays cover FNA3D matrix depth and unsigned/_bx2
2D/cube/volume inference without a private sampler map. Current native renderer/browser
GPU qualification remains separate from the fork numeric and driver evidence.

### TQ-SWEEP12-15: SPIR-V saturated ps_1.x results before their scale modifier

MojoShader `4984c2253e233d97f0cbb624530329bc43be7cb2`, recorded by FNA3D
`e95b48595cabc59246993a19b50fb7c2b08b9f1e`, scales the result before applying saturation.
[Microsoft's ps_1.x modifier contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-1-x)
requires this order. Previously `mov_x2_sat` of .75 produced 1.5 rather than 1, while
`add_d2_sat` of .75 plus .75 produced .5 rather than .75. The final masked merge preserves
unwritten lanes even if their old values lie outside [0,1]. Result shifts remain limited to
the pre-SM2 pixel-shader path; predicated SM3 instructions cannot use them.

The independent oracle adds 456 legal ps_1.1/1.2/1.3/1.4/profile/shift/mask/MOV-or-ADD/saturation
combinations, with unsaturated controls and a final multiply to expose an early clamp through
later arithmetic. The original ordering fails 156 numeric checks; a wrong-x8-factor mutation
also fails and was reverted. All 21 fork tests, the expanded ASan/UBSan oracle, real private
SDL GPU device checks, and FNA3D's 29-test private harness pass. ShaderCross's stand-in device
case remains explicitly skipped. No new CNA pixel-corpus result is claimed for this pin.

### TQ-SWEEP12-14: SPIR-V refused predicated destination writes

MojoShader `2e281ca8bfa84da633412d9d398985c6ce5bce7f`, recorded by FNA3D
`856a30205867f9bb5510caa040532d4bc480b772`, selects the new or original destination value
immediately before the store. The predicate uses its declared selector and optional negation;
the result retains the destination's scalar/vector float, integer or boolean type. Loading
both operands before storing also preserves self-predicated SETP semantics. Inactive lanes
keep their original values even when the instruction saturates or changes only part of a vector.

The independent numeric oracle adds 1,080 profile/predicate/selector/negation/mask/mode variants
for MOV, ADD, saturating MOV, scalar depth, self-predicated SETP and integer MOVA destinations.
All fail with the previous refusal; a mutation bypassing the predicate fails old-value
preservation checks and was reverted. All 21 normal fork tests, the expanded predicate oracle
under ASan/UBSan, and FNA3D's 29 private harness tests pass. The complete pass of all eight CNA
multi policies and both unmodified 44-case SDLGPU/WEBGPU pixel corpora is first reached at
this task-14 pin (`856a302` / `2e281ca`); it is not a result for the later task-15 pin.
The forks and CNA integration are committed locally; publication approval remains pending.

### TQ-SWEEP12-13: Naga rejected vector logical masking in predicated TEXKILL

MojoShader `599ef154ecc3abdca920015a99fae086db250837`, recorded by FNA3D
`cba21423862a279711911673b15daeeec5a5c928`, selects zero for inactive components before
the ordered-negative comparison and `OpAny`. Negated predicates reverse the select operands.
This preserves the per-lane kill condition while avoiding vector `OpLogicalAnd`, which the
pinned wgpu-native/Naga rejects even though the original module passes SPIR-V validation.
The independent numeric oracle covers 640 profile/predicate/selector/negation/negative-lane
variants; the original code fails its portability guard and a dropped-negation mutation fails
the truth table. All 21 fork tests, the predicate oracle under ASan/UBSan, and FNA3D's 29 private
harness tests pass. CNA's original predicated-TEXKILL pixel witness passes on WEBGPU and SDLGPU.
Predicated destination writes remain a separate repair.

### TQ-SWEEP12-12: sampler source selectors were applied to image objects

MojoShader `1ab34bd2a88c22ad6aa89fcb5e520dfe305adb55`, recorded by FNA3D
`d7d8adf4ec56e286a5a4d9c45342a95da2188a1b`, loads the declared sampler as an image and
applies its selector to the sampled vec4 before destination masking in TEXLD/TEXLDD/TEXLDL.
Previously a legal `.bgra` sampler operand failed with an unknown-type swizzle error.
TEXLDD gradients now also have exactly the texture's coordinate dimension, as SPIR-V requires.
The fork's numeric/module oracle covers 144 combinations of pixel/vertex routes, sampler
dimensions, selectors and write masks. The original code and a dropped-selector mutation fail;
all 20 fork tests and the two new ASan/UBSan oracles pass. Both CNA GPU corpora retain their
original pixel expectations; SDLGPU reaches 43/44 and WEBGPU 42/44. Predication and WEBGPU's
predicated TEXKILL remain separate defects. FNA3D's 29-test private harness passes at this pin.

### TQ-SWEEP12-11: legacy SPIR-V specular reflection used a component-wise quotient

MojoShader `b6125e566caaec3627ee56d2ae2f8925c143a7eb`, recorded by FNA3D
`c3d242f42d41fe8d7d873423d91c4b1c80fd3fff`, computes
`2 * dot(N,E) / dot(N,N) * N - E` for TEXM3X3SPEC and TEXM3X3VSPEC.
The old vector quotient returned the eye direction and selected incorrect cube faces.
A fork numeric interpreter evaluates emitted coordinates for non-axis vectors and an
unnormalized normal on SPIR-V/glspirv; both the old code and a wrong-denominator mutation fail.
All 19 fork tests and FNA3D's 29-test harness pass. CNA's original three pixel/LOD assertions
pass privately on SDLGPU and WEBGPU; no expected value or fixture was changed.

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
