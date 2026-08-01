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
  `world * view * projection` transforms. Compatible clipped triangle runs are batched through
  native `grDrawVertexArray(GR_TRIANGLES)`; SpriteBatch remains two direct `grDrawTriangle` calls.
- [x] Native 16-bit Z buffer, depth clear/test/write state, source/destination alpha blending, and
  a manual dgVoodoo smoke target.
- [x] Homogeneous CPU frustum clipping before the perspective divide. Triangles crossing a side,
  near, far, or eye plane are split into a clipped fan and then emitted as native
  `grDrawVertexArray(GR_TRIANGLES)` batches; invalid non-finite input is rejected rather than sent
  to Glide's FIFO.
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
- [x] Tile bodies have address-mode-aware padding and a one-texel neighbour gutter at internal
  logical boundaries. This preserves point sampling and level-0 bilinear samples through tile
  seams for `Wrap`, `Clamp`, and `Mirror` while the final sample remains on TMU0.
- [x] A single address-mode-aware ARGB4444 mip pyramid is generated for the complete logical
  image. Each native Glide tile copies every LOD from that shared pyramid instead of downsampling
  its isolated padded tile, removing the cross-tile source mismatch during minification.
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

- [x] **GLIDE-AUD-001 — Make the standard clear path work without inventing stencil.** The shared backend contract now distinguishes default depth and stencil planes. Glide reports depth=true/stencil=false, so `GraphicsDevice::Clear(Color)` preserves its colour+Z clear while masking only its impossible stencil portion; the smoke target executes this standard path.
- [x] **GLIDE-AUD-002 — Implement the XNA viewport contract and combine it correctly with scissor.** Glide now stores and validates XY/depth viewport state, maps 3D XY/Z into it, and submits the viewport/scissor/framebuffer intersection through `grClipWindow`. The smoke target contains an offset-viewport probe; the comprehensive state matrix remains under GLIDE-AUD-007.
- [x] **GLIDE-AUD-003 — Fence texture mutation/reuse and make resource lifetime safe.** `UpdatePixels()` and address-mode rebuilds now finish the FIFO before replacing TMU contents; destruction finishes before returning ranges. Textures keep only a weak native-state reference, so backend-first teardown is safe instead of dereferencing a destroyed backend.
- [x] **GLIDE-AUD-004 — Correct per-vertex lighting for non-uniform world transforms.** BasicEffect lighting now uses the transpose of the inverse World 3×3 and rejects singular/non-finite normal transforms with a clear error.
- [x] **GLIDE-AUD-005 — Make reset, virtual resolution and presentation policy honest.** Glide explicitly supports only `NativeBackBuffer` and swap intervals 0/1. Resize remains an atomic fit-or-throw operation, which `GraphicsDevice::SetVirtualResolution()` already commits only after backend success.
- [ ] **GLIDE-AUD-006 — Add an automated native-ABI contract test.** The backend deliberately hand-declares Glide 3.x types, numeric constants, layouts and stdcall byte counts. Factor the loader-facing declarations into a small auditable unit and test them against a purpose-built x86 fake `glide3x.dll` that records calls, including undecorated and decorated exports, `grSstWinOpen`, vertex layout, combiner state, texture calls, clear/readback and shutdown ordering. This must run without dgVoodoo and without the external `sharp-runtime` i686 executable dependency.
- [ ] **GLIDE-AUD-007 — Expand rendering regressions beyond the nine smoke probes.** Add deterministic pixel probes/golden captures for all alpha-test compare functions, source/destination blend factors, depth compare/write combinations, cull modes, colour-mask groups, scissor, indexed strips, fog, and BasicEffect lighting. Include NPOT and multi-tile textures under Point/Linear × Clamp/Wrap/Mirror, with minification and texture updates. Run the visual subset on both dgVoodoo and, when available, real Voodoo hardware; record emulator/version, tolerance and known intentional ARGB4444/RGB565 differences.
- [x] **GLIDE-AUD-008 — Harden texture input and TMU allocation failure paths.** Texture creation rejects undersized RGBA8 input, oversized logical dimensions and overflowing byte counts; TMU range allocation/coalescing now uses checked 64-bit intermediates. Failure-injection coverage remains part of the fake-DLL harness in GLIDE-AUD-006.
- [x] **GLIDE-AUD-009 — Batch native submissions without crossing state boundaries.** Compatible
  3D fan triangles now accumulate in bounded (1024-triangle) submission-order batches and use
  `grDrawVertexArray(GR_TRIANGLES)`. The batch flushes before a texture-tile/state change and
  preserves the existing per-triangle vertex order; SpriteBatch deliberately retains its direct
  two-triangle path. The still-pending fake-DLL/dgVoodoo checks are tracked in GLIDE-AUD-006/007.
- [ ] Validate and, if observable on real Voodoo/dgVoodoo output, compensate the remaining
  sub-texel LOD phase at a logical tile seam. All levels now use one shared logical mip pyramid,
  but a physical tile's one-texel gutter means its coordinate origin cannot be exactly aligned for
  every power-of-two LOD simultaneously. The new path's backend source compiles with the i686
  MinGW compiler; executing the current CNA smoke target is separately blocked by the external
  `../sharp-runtime` dependency's unsupported i686 `__int128` use. Resume this validation after
  that dependency is made i686-compatible, or with an equivalent prebuilt x86 CNA smoke binary.
