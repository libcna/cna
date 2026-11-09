# FNA3D and MojoShader: dependency defects and fork repairs

CNA's FNA3D renderer family draws through two pinned forks, [`libcna/FNA3D`](https://github.com/libcna/FNA3D)
and [`libcna/mojoshader`](https://github.com/libcna/mojoshader). The pin is FNA3D
`76a76cf98544f008584a5c6e0a1f5eb5b468795b`, which records MojoShader
`6f1e91b65c8dfe493ec1ace818b294b7181fa1b6` (`cmake/ThirdPartyFNA3D.cmake`;
[`fna3d-renderer.md`](fna3d-renderer.md#dependency-and-source-pin)). The owner authorizes fork
changes; CNA configure never patches either checkout. This page is the defect and repair list for
whoever maintains the forks; `KNOWN_ISSUES.md` keeps the short CNA-side entry for each open one.

"Confirmed" means the faulty code path was read and a CNA test reproduces the symptom on the GPU;
"probable" means the symptom is reproduced but the cause is not yet located.

## Open

### FNA3D-8: region and override presentation produces wrong native client pixels

**Status:** confirmed (GAP-08a). **Drivers:** OpenGL and SDLGPU.

On a private Xwayland display (AMD Radeon 780M, Mesa 25.0.7, FNA3D `10f650b`), CNA's forwarding
implementation passed the fixture's ordinary-Present magenta control on both drivers, then failed
all nine `Gap08PresentedPixels` cases — native-client XGetImage observations, not backbuffer
comparisons. Source-only, oversized-source, asymmetric-source, destination-retention, scaling,
clipping and native-size-backbuffer cases failed on both drivers, as did the foreign-client region
assertions of the two override cases. (Their own-window comparisons had an overlapping-X11-window
exposure artefact, also seen with SOFTWARE, and are not evidence of a driver write.)

Attribution at `10f650b`:

- OpenGL `OPENGL_SwapBuffers` reads `srcW/srcH` and `dstW/dstH` as extents, then passes them as
  second corners to `glBlitFramebuffer` (`src/FNA3D_Driver_OpenGL.c:1373–1390,1466–1471`), so
  `(4,4,4,4)` becomes an empty span; its direct-window branch (`:1494–1498`) ignores rectangles,
  and the override is swapped without an apparent context rebind.
- SDL GPU loads the destination swapchain texture with `SDL_GPU_LOADOP_DONT_CARE`
  (`src/FNA3D_Driver_SDL.c:1207`), which does not preserve untouched pixels.

**Fork state at the pin.** OpenGL: repaired by TQ-FORK-GLPRESENT-01 (`4ff109a`) — rectangles
become GL endpoints and presentation targets the override drawable; the fork's
`presentation_tests` check default-framebuffer pixels including window-override rectangles. SDL GPU:
`LOAD` is not a repair, because an acquired swapchain texture holds whichever earlier frame last
used that image, not the previous presentation. An explicit refusal (`d001379`) blanked CNA's
ordinary letterboxed frames, which also pass a destination rectangle
(`Fna3d_CrossFamily_SpriteBatch_Presentation_SdlGpuDriver`). By owner decision the driver clears the
swapchain to opaque black whenever the destination does not cover it exactly
(TQ-FORK-SDLPRESENT-02, `76a76cf`; pinned by the fork's `sdlgpu_command_tests presentation`
group): letterbox bars and a region Present's surroundings are defined black, but previous pixels
are still not kept.

**CNA meanwhile:** a non-default `GraphicsDevice::Present` on FNA3D throws a named
`NotSupportedException` citing FNA3D-8, before adopting any override target; ordinary Present is
supported. `Gap08PresentBindingTest.Fna3dRefusesEveryNonDefaultPresentBeforeNativeAdoption` pins
independent source/destination/window refusal, no adoption/output, and ordinary controls. XNA keeps
the pixels outside the rectangle, which neither driver reproduces natively.

**Reproducer:** `Gap08PresentedPixels.*` through `tools/platform/run_gpu_tests_private.sh --exec`
with `CNA_GRAPHICS_RENDERER=FNA3D`, `SDL_VIDEODRIVER=x11`, and `FNA3D_FORCE_DRIVER=OpenGL` then
`=SDLGPU`. The checked-in native fixture exercises SOFTWARE only; an FNA3D rerun needs CNA's
forwarding reinstated in a probe build. Any new FNA3D presentation claim needs that native rerun.

### FNA3D-3: the SDL GPU driver applies no MultiSampleMask

**Status:** confirmed (MTH-CONF-005). **Driver:** SDLGPU.

The driver writes the mask into `SDL_GPUMultisampleState.enable_mask`/`sample_mask`, which SDL
documents as reserved (`src/FNA3D_Driver_SDL.c` ~1640-1650 at the pin), so the mask has no effect.

- **CNA meanwhile:** reports `RendererFeature::MultiSampleMask=Unsupported` for that driver
  (Supported on the OpenGL driver); `blend_msaa_boundaries` skips the mask cases with that reason.
- **Suggested repair:** depends on SDL exposing sample masks; until then the driver should refuse
  rather than silently ignore a non-full mask.

### Other FNA3D behaviour recorded in `KNOWN_ISSUES.md` (External and driver limitations)

`SkinnedEffect.VertexColorEnabled` is ignored outright and saturates before the fold; the back buffer
keeps a 3, 5 or 7 sample request unrounded while render targets round it down;
`GraphicsDeviceManager`'s default `Depth24` request is not applied in `graphics_adapter_query`.

## Pending qualification

- **FNA3D-8, OpenGL half:** no CNA-level native rerun of `Gap08PresentedPixels` against the repair.
- **FNA3D-2 and FNA3D-6:** owner LeakSanitizer runs and replays of the original fuzz seeds
  (`20261103001801`, `20261103032166`; `20261104107187`) with
  `python3 tools/fuzz/reproduce.py <record>.json --regenerate`, against the repaired pair and with
  no `readsmallobjects`/`readlargeobjects` suppression. A `detect_leaks=0` run or a historical
  suppressed campaign is not LSan qualification. The possible pass-constant preshader overwrite is
  outside these repairs.
- **TQ-TEXM3X2DEPTH-01, GAP-13, TQ-SWEEP12-15:** fork numeric and driver evidence only; CNA native
  renderer/browser GPU witnesses at the current pin are separate.
- **FNA3D-7 (historical crash):** an abnormal termination reported during TQ-085/TQ-087 is
  unattributed. `Fna3dCompiledEffectTest.MissingShaderParameterSymbolIsRejectedNormally` (BasicEffect.fxb
  byte 25018 XOR 0x10 turns a CTAB symbol's `o` into DEL) passes with the expected
  `Shader parameter not found in effect.` refusal on OpenGL and SDLGPU, aggregate and focused
  executables (TQ-FNA-QUAL-01), and debugger runs show normal native cleanup. No guard, fork change
  or skip was added; a recurrence needs its exact executable, loaded-library/source identities and
  a symbolized failing stack.

## Driver behaviour CNA must design around (not defects)

- **The OpenGL driver creates and uploads every texture through texture unit 0** (`BindTexture` in
  `FNA3D_Driver_OpenGL.c`, called from texture creation and `SetTextureData`). Any texture CNA creates
  lazily between verifying a sampler and drawing silently replaces unit 0's binding. CNA therefore
  creates every lazy resource before binding samplers: the null environment cube (MTH-REV-001) and
  the null second texture of `DualTextureEffect` (TQ-017), each with a cold-device regression in
  `Fna3dPipelinePixelTests.cpp` / `StockEffectNullTextureTests.cpp`.

## Fixed on the forks

All repairs below are in the pinned pair. Commits name where each repair landed.

| ID | Fork commit | Behaviour now | Remaining boundary / evidence |
|---|---|---|---|
| FNA3D-1 | FNA3D `912e26d` | OpenGL selects ordinary/CCW stencil faces at each draw from the current front face and render-target orientation, independently of culling. XNA Refresh `DepthStencilState.Apply` IL_0154-019b / IL_01b4-0242 installs the two states independently; D3D9's ordinary face is clockwise. | `Fna3dPipelinePixels.TwoSidedStencilUsesClockwiseOrdinaryState` (Replace=5, Invert=255, false/true/false winding) passes on OpenGL and SDLGPU. |
| FNA3D-2, FNA3D-6 | MojoShader `04616e1` | Small and large object readers reject a second payload for a defined slot (shared occupancy table) before allocation/compilation; repeated references stay valid. | Fork `cna_duplicate_object_tests`; LSan and seed replay pending (above). |
| FNA3D-4 | FNA3D `912e26d` | SDL_GPU submits prior upload/render work before a new texture update, including cube/volume/mipmapped and deferred clear-only work. No CPU wait or implicit Present; one extra submission per update, no throughput claim. CNA's one-texel upload barrier was removed. | `HalfFloatCubeTargetTypedTransfersPreserveRenderedNeighbours`, `MipmappedTargetUploadsFollowQueuedResolve` and the Texture2D/Cube `texture_setdata_after_draw` legs pass on both drivers. |
| FNA3D-5 (attributed part) | FNA3D `912e26d` | Mip bounds are unsigned and stored: XNA Refresh `SamplerState.Apply` IL_0342-0355 passes `cachedMaxMipLevel` as uint32; SDL's `min_lod` is never negative and GL's base level is bounded to stored levels (requests 0,1,2,-1,99,0 on three levels → 0,1,2,2,2,0). | `SdlGpuTextureTransfer.SamplerMipClampUsesMostDetailedAllowedLevel` and `Fna3dPipelinePixels.SamplerMipClampUsesMostDetailedAllowedLevel` pass on OpenGL and SDLGPU (point-filtered upload/state/SpriteBatch). Imported GL textures, minification/bias and D3D11 are outside scope. |
| TQ-FORK-EFFECT-TRUNC-01 | MojoShader `6f1e91b` | Small/large object loops require complete headers (`readui32` returned zero at end of input) and untyped slots are parser errors; a truncated effect no longer reaches `assert(0 && "Small object type unknown!")`. | Fork `cna_effect_tests` cover every four-byte prefix and header cut; CNA `CompiledEffectTruncationSweepTest` under `SDL_ASSERT=abort`. |
| TQ-TEXM3X2DEPTH-01 | FNA3D `86e1fa0` | OpenGL clear and viewport use the double-precision depth APIs only in a desktop context; ES3 uses `ClearDepthf`/`DepthRangef` (OpenGL ES 3.2 §15.2.3, §12.6.1). | 28-check device-free driver regression (desktop/ES, pointer availability, clear value, range, redundant-state caching). |
| GAP-13 | MojoShader `cc58b0d`, `3588784`, `0dc4f2e`, `93ee117`; FNA3D `866d402` | SPIR-V emits TEXREG2AR/GB/RGB, TEXDP3/TEXDP3TEX and TEXM3X2DEPTH (implicit FragDepth, PAD-time numerator, independent `_bx2` sources, zero-denominator result one); the GLSL signed-source calculation is fixed. Legacy sampler dimensions are inferred from typed Effect texture links (CTAB register associations or pass sampler bindings); explicit caller maps win, untyped declarations default to 2D, conflicting per-pass dimensions and inferred sampler arrays refuse by name. | CNA's SPIR-V envelope executes all eight legacy instruction witnesses; OFF builds keep the named CompiledEffects refusals. |
| TQ-SWEEP12-15 | MojoShader `4984c22` | ps_1.x results are scaled before saturation, per Microsoft's ps_1.x modifier contract (`mov_x2_sat` of .75 → 1; `add_d2_sat` of .75+.75 → .75); masked merges keep unwritten lanes. Shifts are pre-SM2 pixel-shader only. | Fork oracle of 456 profile/shift/mask/saturation combinations. |
| TQ-SWEEP12-14 | MojoShader `2e281ca` | SPIR-V predicated destination writes select new or original values before the store, with declared selector and negation, preserving type and self-predicated SETP. | Fork oracle of 1,080 variants. |
| TQ-SWEEP12-13 | MojoShader `599ef15` | Predicated TEXKILL selects zero for inactive components before the ordered-negative comparison and `OpAny`, avoiding the vector `OpLogicalAnd` that the pinned Naga rejects. | CNA's predicated-TEXKILL pixel witness passes on WEBGPU and SDLGPU. |
| TQ-SWEEP12-12 | MojoShader `1ab34bd` | TEXLD/TEXLDD/TEXLDL apply the sampler selector to the sampled vec4, not the image object; TEXLDD gradients have the texture's coordinate dimension. | Fork oracle of 144 route/dimension/selector/mask combinations. |
| TQ-SWEEP12-11 | MojoShader `b6125e5` | TEXM3X3SPEC/TEXM3X3VSPEC compute `2 * dot(N,E) / dot(N,N) * N - E` rather than a component-wise quotient. | CNA's three pixel/LOD assertions pass on SDLGPU and WEBGPU. |
| TQ-SWEEP12-10 | MojoShader `100025e` | Sampler reflection carries `matrix_only` for a ps_1_4 BEM bump matrix that samples no texture; SDLGPU binds its neutral texture there, while an unbound sampled slot still refuses. | CNA's BEM pixel witness and missing-sampled-texture control pass. |

The earlier fork changes (SDLGPU device-array and vertex-binding cache leaks, the MojoShader SPIR-V
attribute fixup, dumpspirv and trace-lock leaks) are listed in `KNOWN_ISSUES.md` (External and
driver limitations) under the LeakSanitizer entry.
