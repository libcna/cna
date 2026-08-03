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
- [x] `SpriteBatch` texture upload and quad submission through TMU0 plus `grDrawTriangle`; native
  texture-coordinate unit conversion remains open under GLIDE-AUD-010.
- [x] Fixed-function `VertexPositionColor` triangle lists/strips, including indexed draws and CPU
  `world * view * projection` transforms. Compatible clipped triangle runs are batched through
  native `grDrawVertexArray(GR_TRIANGLES)`; SpriteBatch remains two direct `grDrawTriangle` calls.
- [x] Native 16-bit Z buffer, depth clear/test/write state, initial alpha-blend state plumbing, and
  a manual dgVoodoo smoke target. Exact XNA-to-Glide blend-factor fidelity remains open under
  GLIDE-AUD-011 because Glide restricts factors by argument position and alpha factors more
  strongly than the current shared mapper expresses.
- [x] Homogeneous CPU frustum clipping before the perspective divide. Triangles crossing a side,
  near, or far plane are split into a clipped fan and then emitted as native
  `grDrawVertexArray(GR_TRIANGLES)` batches; invalid non-finite input is rejected rather than sent
  to Glide's FIFO. Triangle clipping shares the same strict positive-W eye-plane margin as
  point/line clipping (GLIDE-AUD-014).
- [x] Textured 3D `VertexPositionTexture` and `VertexPositionColorTexture` through TMU0, with
  Glide `s/w`, `t/w`, and `1/w`. Indexed versions expand the index stream before the same real
  triangle submission. Correct native coordinate scaling remains open under GLIDE-AUD-010, and
  indexed custom-layout preservation under GLIDE-AUD-012.
- [x] The documented fixed-function `BasicEffect` subset: unlit diffuse/vertex-colour, or
  `VertexPositionNormalTexture` directional **per-vertex** diffuse and Blinn-Phong specular
  lighting with ambient and emissive terms. It deliberately rejects `PreferPerPixelLighting`.
  Normal-matrix correctness for rotated/sheared worlds is reopened under GLIDE-AUD-004.
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
  the smallest listed historical Glide resolution that contains CNA's virtual framebuffer.
  Selecting and opening the refresh rate from the same returned candidate remains open under
  GLIDE-AUD-013.

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
- [x] **GLIDE-AUD-004 — Correct per-vertex lighting for non-uniform world transforms.**
  `TransformGlideLightingNormal()` now dots the normal against each *row* of the inverse World 3×3
  (`inverseWorld[0..2]`, `[3..5]`, `[6..8]`) instead of striding down its columns, which applies the
  required inverse-transpose instead of the plain inverse. Singular/non-finite rejection in
  `InvertGlideLightingWorld3x3()` is unchanged. Added three portable probes whose expected normals
  are derived independently of the production helper: a 90° rotation (orthogonal World, so the
  correct answer is the ordinary point-transform row, not an inverted one), a non-symmetric unit
  lower-triangular shear whose inverse is derived by hand via forward substitution, and a
  perpendicularity-preservation invariant (`n·t == 0` before implies `n'·t' == 0` after, for any
  invertible World) checked against a second, unrelated shear matrix. All three fail against the
  pre-fix code and pass after it. A lit-image probe on real Glide output remains blocked by the
  same external i686 `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007.
- [x] **GLIDE-AUD-005 — Make reset, virtual resolution and presentation policy honest.** Glide explicitly supports only `NativeBackBuffer` and swap intervals 0/1. Resize remains an atomic fit-or-throw operation, which `GraphicsDevice::SetVirtualResolution()` already commits only after backend success.
- [ ] **GLIDE-AUD-006 — Add an automated native-ABI contract test.** The backend deliberately hand-declares Glide 3.x types, numeric constants, layouts and stdcall byte counts. Factor the loader-facing declarations into a small auditable unit and test them against a purpose-built x86 fake `glide3x.dll` that records calls, including undecorated and decorated exports, `grSstWinOpen`, vertex layout, combiner state, texture calls, clear/readback and shutdown ordering. This must run without dgVoodoo and without the external `sharp-runtime` i686 executable dependency. **Implementation staged:** `GlideAbi.hpp` now owns every renderer-facing signature and native layout; the independent x86 fake DLL + Wine contract resolves the complete 37-export surface, checks undecorated and `name@N` stdcall lookup plus missing-export rejection, and calls every resolved signature while the fake recorder proves each entry point was reached. The actual CNA renderer still cannot be instantiated in that target because the sibling i686 `sharp-runtime` build fails on `__int128`; when that dependency is portable, the next stage must use the recorder to assert the real initialization, draw, texture, clear/readback and teardown sequences and their values/order.
- [ ] **GLIDE-AUD-006 follow-up — real renderer fake-DLL sequence.** **needs_external_dependency:** the sibling `../sharp-runtime` must first build for i686 MinGW without `__int128`. Then link CNA against the fake DLL and assert real call order/values for startup, primitive modes, texture updates, clear/readback and shutdown.
- [ ] **GLIDE-AUD-007 — Expand rendering regressions beyond the ten smoke probes.** Add deterministic pixel probes/golden captures for all alpha-test compare functions, source/destination blend factors, depth compare/write combinations, cull modes, colour-mask groups, scissor, indexed strips, point/line clipping and joins, fog, and BasicEffect lighting. Include NPOT and multi-tile textures under Point/Linear × Clamp/Wrap/Mirror, with minification and texture updates. Run the visual subset on both dgVoodoo and, when available, real Voodoo hardware; record emulator/version, tolerance and known intentional ARGB4444/RGB565 differences. The corrective tasks GLIDE-AUD-010 through GLIDE-AUD-015 add mandatory targeted cases to this suite.
- [ ] **GLIDE-AUD-007 execution — native image suite.** **needs_external_dependency:** requires the same runnable i686 CNA executable plus a caller-provided dgVoodoo or Voodoo runtime; no emulator DLL is bundled or configured by CNA.
- [x] **GLIDE-AUD-008 — Harden texture input and TMU allocation failure paths.** Texture creation rejects undersized RGBA8 input, oversized logical dimensions and overflowing byte counts; TMU range allocation/coalescing now uses checked 64-bit intermediates. Failure-injection coverage remains part of the fake-DLL harness in GLIDE-AUD-006.
- [x] **GLIDE-AUD-009 — Batch native submissions without crossing state or ordering boundaries.**
  The textured triangle, point and line paths now traverse primitives in submission order and
  iterate each primitive's owning tile(s) in the inner loop, instead of a tile-major outer loop.
  A shared `boundTile` pointer only re-issues `grTexSource`/`grTexFilterMode`/`grTexClampMode`/
  `grTexMipMapMode`/`grTexLodBiasValue` and flushes the pending batch when the tile actually
  changes, so a single-tile texture (the overwhelming common case) is completely unaffected: one
  bind, one growing batch, one final flush, identical to before. For a genuinely multi-tile
  texture this removes the reordering: the previous tile-major loop could emit `A/tile0, B/tile0,
  A/tile1, B/tile1`, which silently drew `B` before `A` at any pixel where `A` samples tile1 while
  `B` samples tile0. Points still resolve to exactly one owning tile (tiles partition UV space), so
  the inner loop `break`s once found; lines and triangles keep their existing per-tile
  address-segment clipping, just parameterized by the tile passed in rather than closed over from
  an outer loop. Verified by i686 MinGW `-fsyntax-only` recompilation of the whole backend and the
  existing portable Glide unit suite (18/18, unaffected since none of it touches this file). A
  fake-DLL native call-order capture and overlapping-translucent-multi-tile dgVoodoo image remain
  blocked by the same external i686 `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007.
  SpriteBatch keeps its direct two-triangle path, unaffected by this change.
- [ ] **GLIDE-AUD-010 — Convert all texture coordinates to native Glide units.** Glide window-space
  TMU coordinates use `0..256` across the texture's long side and an aspect-scaled range across the
  short side. The current SpriteBatch path submits normalized UVs directly, while the 3D path
  submits tile texel coordinates; neither applies the required native scale. Define one shared
  conversion based on each physical tile's padded dimensions, use it for sprites, triangles,
  points and lines before multiplying by reciprocal W, and keep gutter/tile offsets in the same
  unit system. Add fake-DLL vertex-value assertions plus coloured 2x2, 64x32, 128x128, NPOT and
  multi-tile Point/Linear image probes. A uniform 1x1 texture is not sufficient evidence.
- [x] **GLIDE-AUD-011 — Make blend-factor mapping argument-aware and reject impossible states
  atomically.** The Glide 3.0 Reference documents `grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf,
  alpha_df)` as accepting a *different* 9-constant set for the two `*_sf` ("source factor") slots
  and 8-constant set for the two `*_df` ("destination factor") slots; `GR_BLEND_SRC_COLOR` and
  `GR_BLEND_DST_COLOR` (and their inverses) even share the same native numeric code, so which XNA
  `Blend` value a slot can represent depends entirely on which of the four slots it goes into. The
  previous single `ToGlideBlend(int)` mapper ignored this and mapped `Blend::SourceColor` and
  `Blend::DestinationColor` to the same code regardless of position, so e.g. a custom
  `ColorDestinationBlend = Blend::DestinationColor` silently became `GR_BLEND_SRC_COLOR` in the
  `rgb_df` slot instead of throwing — a real, silent semantic error, not a crash. The mapper is
  now `ToGlideBlendFactor(Blend, GlideBlendSlot)` in the new portable
  `include/CNA/Internal/Backends/Glide/GlideBlendFactor.hpp`, with `GlideBlendSlot` distinguishing
  `RgbSource`/`RgbDestination`/`AlphaSource`/`AlphaDestination` and each of the four call sites in
  `ApplyBlendState()` passing its own slot. The Glide 3.0 Reference also documents
  `GR_BLEND_DST_ALPHA`/`GR_BLEND_ONE_MINUS_DST_ALPHA`/`GR_BLEND_ALPHA_SATURATE` as producing
  undefined results whenever depth buffering is enabled (they contend for the same auxiliary
  buffer Glide uses for Z); since CNA's Glide backend always allocates exactly one auxiliary
  buffer for Z, `ApplyBlendState()`, `ApplyDepthStencilState()`, `SetDepthTestEnabled()` and
  `SetBlendEnabled()` now all reject that combination symmetrically (whichever call would create
  it), before mutating any cached or native state — this also incidentally fixed a pre-existing
  non-atomic-mutation bug in `ApplyBlendState()`/`ApplyDepthStencilState()` where earlier fields
  were assigned to `impl_` before a later `ToGlideBlend`/`ToGlideDepthCompare` call could still
  throw. None of XNA's four built-in `BlendState` presets (`Opaque`, `AlphaBlend`, `Additive`,
  `NonPremultiplied`) use `DestinationColor`/`InverseDestinationColor`/`SourceAlphaSaturation` or
  the destination-alpha factors, so this is not a regression for the already-validated SpriteBatch
  default. Nine portable probes cover every legal/illegal factor per slot (including the
  same-native-code-different-slot fact directly) and the auxiliary-buffer-conflict predicate.
  Verified with i686 MinGW `-fsyntax-only` recompilation of the whole backend and the portable
  Glide unit suite (31/31). Fake-DLL argument capture and translucent multi-preset dgVoodoo image
  probes remain blocked by the same external i686 `sharp-runtime` `__int128` dependency as
  GLIDE-AUD-006/007. **Note for future hardware validation:** the Glide 3.0 Reference also states
  original Voodoo Graphics (not Voodoo2/Banshee/Voodoo3+, which is what dgVoodoo2 emulates) accepts
  only `GR_BLEND_ZERO`/`GR_BLEND_ONE` for the two alpha-channel slots; this is not enforced because
  it is chip-generation-specific and outside CNA's stated target runtime, but real first-generation
  Voodoo Graphics hardware testing under GLIDE-AUD-007 should re-check it.
- [x] **GLIDE-AUD-012 — Preserve custom vertex declarations in indexed draws.**
  `DrawIndexedPrimitiveRange()` expanded resolved indices into a temporary `GlideVertexBufferBackend`
  and called plain `SetData()` on it, which — because that fresh buffer had no layout of its own yet —
  fell into the same stride-only guessing path used when no `VertexDeclaration` was ever set
  (`KnownGlideVertexLayout`). A custom declaration whose *stride* happens to match one of the four
  built-in packed streams but arranges its fields differently (e.g. texture coordinate before
  colour) would therefore have its indexed copy silently decoded with the wrong offsets, even
  though the non-indexed path (`DrawPrimitiveRange`'s `readVertex`, which reads `vb->Layout()`
  directly) decoded the *same* source buffer correctly. Added
  `GlideVertexBufferBackend::SetDataWithLayout(data, vertexCount, layout)`, which installs an
  already-resolved `GlideVertexLayout` directly instead of parsing or guessing one, and changed
  the indexed expansion to call it with the source buffer's own `vb->Layout()` — carrying forward
  whichever layout (parsed declaration or stride guess) the source buffer actually resolved,
  rather than re-deriving a possibly-different one. `GlideVertexLayout` was already the resolved
  representation shared by both paths, so this makes indexed and non-indexed decoding structurally
  identical by construction instead of by coincidence. Verified with i686 MinGW `-fsyntax-only`
  recompilation of the whole backend and the portable Glide unit suite (31/31, unaffected since
  the change lives entirely in the non-portable renderer class). `GlideVertexLayoutTests.cpp`
  already covers arbitrary legal offsets/padding for `ParseGlideVertexDeclaration`; a fake-DLL
  check that indexed and non-indexed draws of the same aliasing-stride declaration produce
  matching native vertices, plus dgVoodoo images, remain blocked by the same external i686
  `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007. This unblocks the GLIDE-FUT-002
  release-blocker note.
- [ ] **GLIDE-AUD-013 — Open the exact resolution/refresh candidate returned by Glide.** Preserve
  the refresh token while collecting `grQueryResolutions` candidates and pass that token to
  `grSstWinOpen`, or query only the refresh rate that the backend is prepared to open. Candidate
  selection must be deterministic when the same dimensions occur at multiple rates. Extend the
  fake DLL with mode matrices containing no 60 Hz entry, multiple refresh rates and a failing open;
  assert that startup never combines a resolution from one candidate with another refresh value.
- [x] **GLIDE-AUD-014 — Clip triangles against a strict positive-W eye plane.** Triangle/polygon
  clipping now shares the same `clipW - kGlideMinimumPositiveClipW >= 0` half-space as point/line
  clipping. The generic Sutherland-Hodgman clipper and the frustum-plane composition were moved
  from `GlideGraphicsBackend.cpp`'s anonymous namespace into `GlidePrimitiveClip.hpp` as
  `ClipGlidePolygonToHalfSpace`/`ClipGlidePolygonToFrustum` (mirroring the existing segment
  clipper pair in the same header), so the fix is portably unit-testable like the rest of the
  fixed-function CPU math instead of only living in the non-portable renderer. With `clipX ==
  clipY == clipZ == 0`, a vertex sits exactly on the boundary of all six nominal planes (every
  distance evaluates to 0, "inside"), so only the new plane can reject it; that is exactly the
  `W == 0` case that previously survived to the perspective divide and threw in
  `makeGlideVertex()`. Four portable probes cover exact-zero W, below-epsilon positive W, a
  clearly-behind-the-eye crossing, and a fully-behind triangle (entirely rejected, empty output);
  the first two fail against the pre-fix clipper and pass after it. Interpolated colour/UV
  attributes continue to flow through the existing `InterpolateGlideClipVertex()` used by every
  plane, including the new one. Verified with i686 MinGW `-fsyntax-only` recompilation of the
  whole backend and the portable Glide unit suite (22/22). A fake-DLL check that invalid vertices
  never reach Glide remains blocked by the same external i686 `sharp-runtime` `__int128`
  dependency as GLIDE-AUD-006/007.
- [ ] **GLIDE-AUD-015 — Make state application exception-atomic.** Convert and validate every
  blend/depth/cull/alpha-test value into local native values before mutating cached state or issuing
  any Glide call. If a requested combination is unsupported, both the backend cache and native
  state must remain unchanged. Add failure-injection sequences which apply a valid state, attempt
  an invalid state, then draw and prove that the prior complete state is still active.
- [ ] Validate and, if observable on real Voodoo/dgVoodoo output, compensate the remaining
  sub-texel LOD phase at a logical tile seam. All levels now use one shared logical mip pyramid,
  but a physical tile's one-texel gutter means its coordinate origin cannot be exactly aligned for
  every power-of-two LOD simultaneously. The new path's backend source compiles with the i686
  MinGW compiler; executing the current CNA smoke target is separately blocked by the external
  `../sharp-runtime` dependency's unsupported i686 `__int128` use. Resume this validation after
  that dependency is made i686-compatible, or with an equivalent prebuilt x86 CNA smoke binary.

## Future authentic Glide capability roadmap

These are candidate additions that can still use a real Glide 3.x fixed-function pipeline (plus
bounded CPU-side vertex processing). Each must be enabled only where the runtime advertises the
needed capability, must retain a clear failure on older hardware, and needs an x86 fake-DLL test
plus a dgVoodoo/real-hardware visual test before it can be advertised as supported.

- [ ] **GLIDE-FUT-001 — Add the remaining native primitive topologies.** Implement
  `PointListEXT`, `LineList`, and `LineStrip` through Glide's point/line/vertex-array primitive
  modes, with the same finite-input and homogeneous CPU clipping rules as triangles. Investigate
  `FillMode::WireFrame` only as an explicitly tested triangle-edge conversion; retain rejection if
  Glide line rasterization cannot meet CNA's culling, clipping, and duplicate-edge semantics.
  **Implementation staged:** `GR_POINTS` and `GR_LINES` are now emitted through the contiguous
  vertex-array ABI. The standalone point/segment clipper rejects non-finite input, clips every
  XNA frustum plane before division, retains a positive-W margin and interpolates colour/UV data;
  texture-address/tile splits are emitted as independent lines so a clipped `LineStrip` cannot
  reconnect disjoint runs. Four portable clip probes and the i686 source compile pass. Fake-DLL
  draw-mode/value capture and dgVoodoo/real-Voodoo point-size, line-rasterization, blend-at-joint,
  cull and tiled-texture image tests are still required before release validation.
- [ ] **GLIDE-FUT-002 — Decode compatible custom vertex declarations rather than accepting only
  known packed structs.** Build a validated declaration/offset reader for position, colour,
  normal, and texture-coordinate 0 at arbitrary legal offsets and strides. Feed it into the
  existing fixed-function paths and reject unsupported semantics deterministically, instead of
  relying on accidental binary layout compatibility. **Implementation staged:** the parser and
  portable unit coverage now exist, and both the non-indexed 3D decoder and (since GLIDE-AUD-012)
  the indexed expansion use the same resolved layout from the source buffer. The former release
  blocker in GLIDE-AUD-012 is closed; remaining completion requires the fake-DLL draw capture and
  dgVoodoo/real-hardware visual checks from GLIDE-AUD-006/007.
- [ ] **GLIDE-FUT-003 — Complete the feasible `BasicEffect` vertex-lighting subset.** Compute
  the existing FNA-compatible Blinn/Phong specular term on the CPU per vertex (eye position,
  material specular colour/power and all enabled directional lights), add it to the iterated
  Glide RGB result, and add golden tests for non-uniform worlds and multiple lights. Keep
  `PreferPerPixelLighting` rejected: Glide has no per-pixel programmable lighting.
  **Implementation staged:** the CPU path now evaluates FNA's three-light Blinn-Phong term,
  applies it before alpha-aware fog, and attempts an inverse-transpose normal transform. The
  2026-08-03 audit found that the final multiplication instead applies the inverse for the
  backend's row-vector convention; GLIDE-AUD-004 reopens that correction. Six portable unit probes
  cover a front/back light, three-light sum, diagonal non-uniform scale,
  vertex-colour/emissive/specular order and fog, but rotation and shear coverage is still required.
  The recorder cannot yet drive the full CNA backend, so a fake-DLL renderer-sequence assertion
  and dgVoodoo/real-Voodoo image capture remain required before this capability is release-validated.
- [ ] **GLIDE-FUT-004 — Use a second TMU when the selected runtime actually has one.** Add
  per-TMU texture memory allocators/residency, two-texture tiled draw partitioning and the exact
  fixed-function combiner for `DualTextureEffect` / texture slot 1. Gate it on `GR_NUM_TMU >= 2`,
  keep single-TMU Voodoo paths explicit, and test state changes, alpha and unequal tile grids.
- [ ] **GLIDE-FUT-005 — Map sampler mip controls that Glide can represent.** Extend the common
  sampler-state hand-off so `MipMapLevelOfDetailBias` and `MaxMipLevel` reach the backend; map
  the former to native LOD bias and implement/clamp the latter without selecting unavailable
  levels. Define the quantization and out-of-range policy with cross-backend tests, rather than
  silently ignoring either property. **Implementation staged:** the common hand-off now forwards
  both values, Glide maps finite native-range `[-8, 7.75]` LOD bias through `grTexLodBiasValue`
  for sprites and 3D tiles, and explicitly rejects non-zero `MaxMipLevel` until it can be
  represented without changing the selected mip chain. **Audit (2026-08-01):** the Glide 3.0
  programming guide confirms that `GrTexInfo.smallLodLog2`/`largeLodLog2` describe the entire
  downloaded/source LOD range, not a separate sampler clamp. CNA's logical texture may have
  differently sized physical edge tiles; changing `smallLodLog2` by the same XNA level on each
  tile therefore clamps distinct native LOD ranges and can select different logical images at a
  seam. Keep the rejection until a fake-renderer capture and dgVoodoo/real-hardware test establish
  a tile-invariant mapping (or a reallocation/source-range design) for `MaxMipLevel`.
- [ ] **GLIDE-FUT-006 — Support explicit and partial `Texture2D::SetData` updates.** Retain
  CPU copies for every supplied mip level, update rectangles, regenerate only the affected
  derived levels/gutters, fence before re-download, and preserve explicit mip data instead of
  treating every update as a level-0 full-image upload. This also removes the current shared
  `Texture2D` partial-update limitation for Glide-backed textures. **Implementation staged:**
  shared `Texture2D` already reconstructs a complete current mip after a rectangle write, and a
  full level-zero `SetData` now updates an existing backend in place rather than discarding its
  lower subresources. Glide now retains each supplied lower-level RGBA8 image, expands it
  address-mode-correctly into the
  shared power-of-two logical pyramid, regenerates downstream derived levels and re-downloads
  the existing tiles after `grFinish`. Level-zero updates preserve explicitly supplied lower
  mips, and construction rejects a mip count beyond the logical dimensions. Portable tests cover
  RGBA8→ARGB4444 conversion, full-chain counts, and Wrap/Clamp/Mirror expansion/mapping. Full
  fake-renderer sequencing and dgVoodoo/real-Voodoo minification/update captures remain needed,
  as does a performance pass to regenerate only the affected derived levels rather than the
  presently correct full shared pyramid.
- [ ] **GLIDE-FUT-007 — Select the best native texture encoding per logical texture.** Keep a
  deterministic RGBA8 source copy, then use RGB565 for provably opaque data, ARGB1555 for binary
  alpha and ARGB4444 for fractional alpha, with a documented reallocation rule if later updates
  change the alpha class. Compare sampling/blending captures against the existing ARGB4444
  baseline; do not enable palette/NCC compression merely as an undocumented lossy shortcut.
  **Audit (2026-08-01):** the Glide 3.0 guide confirms that RGB565, ARGB1555 and ARGB4444 are all
  16-bit texture formats, but only RGB565 is safe from a proven-opaque source without inspecting
  the full chain. The current 2x2 generated-mip path averages alpha, so a binary-alpha base level
  can produce fractional-alpha derived levels; selecting ARGB1555 from level zero alone would
  silently turn those values into a one-bit mask. A future classifier must inspect every explicit
  and generated logical mip after address padding (or adopt a separately validated alpha-coverage
  rule), then fence and atomically re-download the new format descriptor/source data. Matching
  element width suggests the TMU byte range may be reusable, but that still needs a fake-DLL
  memory/value capture and sampling/blending images before any format is enabled.
- [ ] **GLIDE-FUT-008 — Calibrate constant depth bias.** Establish, on real Voodoo and dgVoodoo,
  the conversion between CNA's normalized `RasterizerState::DepthBias` and Glide's integer depth
  bias for both Z and W depth modes, including sign, clamp and viewport-depth interaction. Support
  only the validated constant part first; continue rejecting `SlopeScaleDepthBias` unless a
  correct CPU-derived mapping can be demonstrated.
- [ ] **GLIDE-FUT-009 — Offer explicit historical raster-quality controls.** When CNA gains an
  opt-in backend/presentation setting, map dither choice to `grDitherMode` and, where supported,
  ordered Glide anti-aliasing. Ordered AA needs documented depth-sorted non-strip submissions and
  must never be presented as XNA MSAA; include deterministic captures for every enabled mode.
- [ ] **GLIDE-FUT-010 — Add opt-in gamma handling with a reversible public contract.** Design a
  CNA-level gamma/presentation control first, then use the Glide helper/runtime gamma facility
  only if it is exported by the chosen 3.x runtime. Restore the previous ramp on shutdown and
  treat per-window emulator gamma as separate from process/global hardware gamma.
- [ ] **GLIDE-FUT-011 — Negotiate optional runtime extensions safely.** Centralize core versus
  optional export loading and capability reporting (non-power-of-two textures, extended blending,
  texture detail controls and emulator-specific functions). Every extension needs an ABI test and
  an explicit policy: use it only when it preserves CNA semantics, otherwise keep the portable
  tiled/core path. Never identify an extension solely from a DLL name.
- [ ] **GLIDE-FUT-012 — Exploit genuine NPOT texture support when advertised.** After
  GLIDE-FUT-011, add an optional direct-NPOT storage path for runtimes reporting it, while keeping
  the existing tiled power-of-two path as the canonical fallback. Verify mip selection, wrap,
  clamp, mirror and texture-coordinate precision before preferring the extension path.
- [ ] **GLIDE-FUT-013 — Improve texture-memory residency under pressure.** Track logical source
  data and native allocation costs, evict least-recently-used unbound textures only after FIFO
  completion, and lazily re-upload them on reuse. Make eviction deterministic and observable in
  diagnostics; if recovery cannot allocate an atomically complete tiled mip chain, fail the draw
  rather than render a partially resident texture.
- [x] **GLIDE-FUT-014 — Replace pointer-array batches with the contiguous vertex-array path.**
  Load and ABI-test `grDrawVertexArrayContiguous`, then use it for compatible triangle batches if
  its byte-stride and FIFO behaviour match the existing pointer-array implementation. Compatible
  triangle batches now pass their contiguous `GlideVertex` storage and `sizeof(GlideVertex)`
  directly; the 1024-triangle state/tile flush boundaries are unchanged. The x86 fake-DLL build
  resolves the 16-byte stdcall export; runtime FIFO/image validation remains in GLIDE-AUD-007.
- [ ] **GLIDE-FUT-015 — Batch SpriteBatch without changing its ordering contract.** Profile the
  native immediate sprite path, then add a bounded queue for adjacent sprites with identical
  texture/sampler/blend/scissor state. Flush at every observable ordering/state boundary and
  compare translucent overlapping sprites, not just opaque throughput scenes.
- [ ] **GLIDE-FUT-016 — Make presentation and mode changes more complete, only through controlled
  recreation.** Investigate native fullscreen/windowed mode selection, higher swap intervals and
  post-start virtual-resolution changes via an atomic `grSstWinClose`/`grSstWinOpen` rebuild that
  restores all live resources and state. Do not claim a resize/fullscreen request succeeded until
  the new mode is selected and a frame/readback probe has passed.
- [ ] **GLIDE-FUT-017 — Add adapter and runtime diagnostics for deployability.** Report selected
  Glide DLL/version, board/TMU count, queried resolution list, texture limits, extension decisions,
  memory use/evictions and rejected CNA features through an opt-in diagnostic channel. Keep the
  data free of raw pointers and use it in the smoke-test failure report. **Implementation staged:**
  `CNA_GLIDE_DIAGNOSTICS=1` now reports the runtime path, selected virtual/native mode, TMU count,
  texture limits and TMU0 capacity. Version/board enumeration, extension/eviction decisions and
  smoke-test integration remain pending.
- [ ] **GLIDE-FUT-018 — Build a hardware compatibility matrix and release gate.** Automate the
  fake-DLL ABI suite, then record results separately for Voodoo Graphics, Voodoo2 and dgVoodoo
  versions on supported 32-bit Windows setups. The matrix must state which FUT capabilities were
  tested on which runtime, image tolerances, driver quirks and the fallback/rejection behaviour.

## Deliberate boundary for future proposals

The following could only be made to "work" by adding a software/modern-renderer emulation layer,
not by extending Glide faithfully: render targets and MRT, stencil, MSAA resolves, cube/volume
textures, occlusion queries, arbitrary effects/shaders, per-pixel lighting, GPU skinning and PBR.
They are not Glide roadmap tasks. Revisit any one only through a separately approved, opt-in
hybrid-backend design that clearly labels the result as non-authentic and keeps the native Glide
path available.
