# FNA3D and MojoShader: defects for the human maintainers

CNA's FNA3D renderer family draws through two pinned forks, [`libcna/FNA3D`](https://github.com/libcna/FNA3D)
and [`libcna/mojoshader`](https://github.com/libcna/mojoshader) (pins in
[`fna3d-renderer.md`](fna3d-renderer.md#dependency-and-source-pin)). Their AI policies leave changes to
those repositories to human maintainers, so CNA does not patch them. This page lists every defect CNA
has found in them, with a reproducer and what CNA does meanwhile. It is the hand-over list for whoever
maintains the forks; `handoff.md` §5.1/§5.3 keeps the short CNA-side entry for each.

Each entry says how far the attribution is established. "Confirmed" means the faulty code path was
read and a CNA test reproduces the symptom on the GPU; "probable" means the symptom is reproduced but
the cause is not yet located.

## Open

### FNA3D-1: OpenGL two-sided stencil follows a stale front face when culling is off

**Status:** confirmed (2026-11-03, TQ-017). **Driver:** OpenGL only; the SDLGPU driver is correct.

`OPENGL_SetDepthStencilState` (`src/FNA3D_Driver_OpenGL.c`, the `separateStencilEnable` branch) gives
XNA's ordinary stencil state to `GL_FRONT` and the counter-clockwise state to `GL_BACK`, but
`glFrontFace` is only issued from `OPENGL_ApplyRasterizerState` while a cull mode is set. With
`CullMode.None` the faces follow whatever front face was set last (GL's initial `GL_CCW` included),
and the ordinary (clockwise) state lands on counter-clockwise triangles.

- **Reproducer:** `Fna3dPipelinePixels.TwoSidedStencilUsesClockwiseOrdinaryState`
  (`modules/renderers/fna3d/tests/CNA/Internal/Renderers/Fna3d/Fna3dPipelinePixelTests.cpp`): draws a
  clockwise and a counter-clockwise triangle with `TwoSidedStencilMode`, Replace on the ordinary state
  and Invert on the CCW state, then probes stencil 5 and 255. On OpenGL the two colours come out
  swapped in every iteration.
- **CNA meanwhile:** the test runs with `FNA3D_FORCE_DRIVER=SDLGPU` and skips on the OpenGL driver,
  naming this entry. No CNA workaround.
- **Suggested repair:** issue the front face the stencil mapping assumes whenever two-sided stencil is
  enabled, independently of culling (or select `GL_FRONT`/`GL_BACK` from the current `frontFace`).

### FNA3D-2: MojoShader leaks an effect object when two small-object records name the same index

**Status:** confirmed (2026-11-03, MTH-SAFE-007 long fuzz campaign, LeakSanitizer; seen with gdb).

`readsmallobjects` (`mojoshader_effects.c`) stores each record's string, sampler name or compiled
shader into `effect->objects[index]` without checking whether that object is already filled. A
repeated index overwrites, and leaks, the first allocation; changing one object index in
`BasicEffect.fxb` (24 to 16) compiles `ShaderFunction16` twice.

- **Reproducer:** seeds 20261103001801 and 20261103032166,
  `python3 tools/fuzz/reproduce.py ../cnaplan/evidence/safe007/long-campaign/findings/compiled-effect/<record>.json --regenerate`
  (exits 1 while the leak reproduces). Measured with the legacy checkout `build-asan` uses
  (MojoShader 6333f74); the pinned fork `03b686f` has the same code path by reading.
- **CNA meanwhile:** none possible from the outside; the weekly `robustness-long-fuzz.yml` run fails
  on the `compiled-effect` target until this is repaired.
- **Suggested repair:** refuse (or free and replace, deliberately) a second record for an object that
  is already filled.

### FNA3D-6: MojoShader leaks a compiled shader when two large-object records name the same index

**Status:** confirmed (2026-11-04, TQ-018 `.xnb` reader fuzz campaign, LeakSanitizer).

The sibling of FNA3D-2 in `readlargeobjects` (`mojoshader_effects.c`): a large-object record stores its
compiled shader into `object->shader.shader` without checking whether the object already holds one,
so a repeated object index overwrites, and leaks, the first compilation (1050 bytes in 14 allocations
in the witness).

- **Reproducer:** seed 20261104107187,
  `python3 tools/fuzz/reproduce.py ../cnaplan/evidence/tq018/evidence/exit-seed-20261104107187.json --regenerate`
  (the `.xnb` beside it is the input). Measured with `build-asan`'s legacy MojoShader checkout; the
  pinned fork has the same code path by reading.
- **CNA meanwhile:** none possible from the outside. Local campaigns suppress it by name
  (`leak:readlargeobjects`, next to FNA3D-2's `leak:readsmallobjects`); the weekly run reports it.
- **Suggested repair:** the one for FNA3D-2 -- refuse (or deliberately free and replace) a second record
  for an object that is already filled, in both readers.

### FNA3D-3: the SDL GPU driver applies no MultiSampleMask

**Status:** confirmed (2026-11-03, MTH-CONF-005). **Driver:** SDLGPU.

The driver writes the mask into `SDL_GPUMultisampleState.enable_mask`/`sample_mask`, which SDL
documents as reserved (`src/FNA3D_Driver_SDL.c` ~1640-1650 at the pin), so the mask has no effect.

- **CNA meanwhile:** reports `RendererFeature::MultiSampleMask=Unsupported` for that driver
  (Supported on the OpenGL driver); `blend_msaa_boundaries` skips the mask cases with that reason.
- **Suggested repair:** depends on SDL exposing sample masks; until then the driver should refuse
  rather than silently ignore a non-full mask.

### FNA3D-4: SDL GPU texture uploads are ordered ahead of the frame's draws

**Status:** confirmed (2026-10-24 and 2026-11-03, MTH-EXIT-003). **Driver:** SDLGPU.

Uploads go on a command buffer submitted ahead of the frame's draws, and only a whole single-level 2D
texture is cycled. A `TextureCube` (by its source also a `Texture3D` or a mipmapped `Texture2D`)
changed between two draws of one frame shows its new contents in the earlier draw too; the same
ordering overwrote later render-target cube patches with earlier clears and mipmapped
`RenderTarget2D` uploads with earlier mip generation.

- **CNA meanwhile:** orders render-target uploads on that driver with a synchronous one-texel native
  readback before uploading (FNA3D exposes no flush-only API); ordinary texture updates keep the
  limitation. Details in `handoff.md` §5.3.
- **Suggested repair:** an upload path that respects draw order (cycle the resource or split the
  command buffer), or a public flush entry point.

### FNA3D-5 (probable): `SamplerState.MaxMipLevel` is not honoured on either driver

**Status:** probable, cause not located (2026-11-03, found while verifying TQ-016/TQ-017).

`SdlGpuTextureTransfer.SamplerMipClampUsesMostDetailedAllowedLevel` uploads a distinct colour to each
mip of a 4x4 texture and draws it with Point filtering and `MaxMipLevel` 0, 1, 2, -1, 99, 0. XNA uses
the value as `D3DSAMP_MAXMIPLEVEL` (`SamplerState.Apply` IL_0342-0355), so level 1 must sample mip 1.
Run with `CNA_GRAPHICS_RENDERER=FNA3D` in `cmake-build-multi`, both drivers sample the wrong level
(level 1 reads mip 0 on SDLGPU; OpenGL differs again).

What is ruled out: CNA's FNA3D SpriteBatch forwards `maxMipLevel` into `FNA3D_SamplerState`
(`Fna3dSpriteBatch.cpp`), and FNA3D's packed sampler key includes it (`FNA3D_PipelineCache.c`,
`GetPackedSamplerState`). Still open: how each driver applies it (`min_lod` on SDLGPU, the texture
base level on OpenGL) and whether the per-level `SetData` uploads land where expected. This entry
moves to "confirmed" or is withdrawn once an FNA3D-family regression attributes it.

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
  a native reproducer for human fork maintainers. Neither fork is patched by CNA.
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
