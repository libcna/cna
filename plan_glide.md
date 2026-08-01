# Glide 3.x backend plan

## Goal and non-goal

The goal is authentic **Glide 3.x** submission to a caller-installed emulator such as dgVoodoo2,
not an EasyGL/OpenGL compatibility facade. `glide3x.dll` is loaded dynamically and never shipped by
CNA. The supported application ABI is 32-bit Windows/x86.

Glide is a fixed-function Voodoo-era API. CNA therefore exposes a small, reliable subset and throws
for functionality with no faithful native counterpart instead of silently falling back to a different
renderer.

## Completed

- [x] Native `grSstWinOpen`, back-buffer clear/present, LFB readback, and loader support for both
  undecorated and x86 stdcall-exported Glide DLLs.
- [x] `SpriteBatch` texture upload and quad submission through TMU0 plus `grDrawTriangle`.
- [x] Fixed-function `VertexPositionColor` triangle lists/strips, including indexed draws and CPU
  `world * view * projection` transforms.
- [x] Native 16-bit Z buffer, depth clear/test/write state, source/destination alpha blending, and
  a manual dgVoodoo smoke target.
- [x] Homogeneous CPU frustum clipping before the perspective divide. Triangles crossing a side,
  near, far, or eye plane are split into a clipped fan and then emitted as real `grDrawTriangle`
  calls; invalid non-finite input is rejected rather than sent to Glide's FIFO.
- [x] Textured 3D `VertexPositionTexture` and `VertexPositionColorTexture` through TMU0, with
  Glide `s/w`, `t/w`, and `1/w`. Indexed versions expand the index stream before the same real
  triangle submission.
- [x] The documented fixed-function `BasicEffect` subset: unlit diffuse/vertex-colour, or
  `VertexPositionNormalTexture` directional **per-vertex** diffuse lighting with ambient and
  emissive terms. It deliberately rejects specular and `PreferPerPixelLighting`.
- [x] AlphaTestEffect's discrete alpha-test encoding maps to `grAlphaTestFunction` and
  `grAlphaTestReferenceValue`; cull and depth-compare states map to their native Glide equivalents.
- [x] Logical textures are tiled to the runtime-reported `GR_MAX_TEXTURE_SIZE`, each tile is padded
  to Glide's reported aspect limit, and each gets a complete generated mip chain before
  `grTexDownloadMipMap`.
- [x] Startup queries `grQueryResolutions` for the actually usable double-buffered depth modes and
  `grGet` for `GR_MAX_TEXTURE_SIZE`, `GR_MAX_TEXTURE_ASPECT_RATIO`, and `GR_NUM_TMU`. It selects
  the smallest listed historical Glide mode that contains CNA's virtual framebuffer.

## Fixed-function fidelity notes

- Fog and BasicEffect vertex lighting are evaluated on the CPU and submitted as Glide iterated RGB.
  This exactly preserves CNA's supplied per-vertex fog factor and directional diffuse calculation,
  but it is not Glide's depth-table fog: a Glide fog table cannot represent CNA's arbitrary
  `fogVector` semantic. The final rasterization, texture sampling, depth test, alpha test and blend
  remain real Glide operations.
- Generated mips are a 2×2 box average in ARGB4444 space. That is faithful to the historical
  texture format and hardware mip selection, but differs slightly from an RGBA8 source-space mip
  generator because the source has already been quantized to four bits per channel.
- Tiled 3D images partition clipped geometry at logical tile and address-mode boundaries on the
  CPU, so `SamplerState.Wrap`, `Clamp` and `Mirror` work over the complete logical image while
  Glide TMU0 still performs the final filtered sample. A malformed draw spanning more than 4096
  Wrap/Mirror intervals is rejected to avoid an unbounded CPU submission loop.
- CNA exposes no dither setting in `RasterizerState` or `GpuDrawParams`. The backend therefore
  keeps the emulator's native Glide dither default and does not invent a state mapping. When CNA
  adds a dither control, it can map directly to `grDitherMode` (`Disable`, `2x2`, `4x4`).
- The mode selector recognizes the historical `GR_RESOLUTION_*` list returned by Glide 3.x. A
  vendor-specific extended resolution token is rejected until its ABI and dimensions are documented;
  this avoids treating an emulator extension as historical Glide.

## Explicitly unsupported by this backend

- Programmable shaders, arbitrary `Effect` source, PBR, and GPU skinning: those are outside
  Glide's fixed-function model.
- Render-to-texture, MRT, MSAA, cube/volume textures, stencil, and occlusion queries: no faithful
  baseline Glide 3.x implementation exists. Any future approximation needs separate opt-in design
  approval, because it would no longer be a real Glide backend.
- Native 64-bit applications: historical Glide's window-handle ABI is 32-bit. Use the supplied
  i686 MinGW toolchain and an x86 emulator DLL.

## Next implementation work

- [ ] Preserve linear and mip filtering exactly at logical tile seams. The new 3D address-mode
  partition draws from the correct tile for `Wrap`, `Clamp`, and `Mirror`, but the current
  power-of-two tile padding repeats that tile's edge texel. It needs address-mode-aware neighbour
  gutters, generated consistently for every mip level, to blend with the adjacent logical texel.
