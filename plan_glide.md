# Glide 3.x backend plan

## Goal and non-goal

The goal is authentic **Glide 3.x** submission to a caller-installed emulator such as dgVoodoo2,
not an EasyGL/OpenGL compatibility facade. `glide3x.dll` is loaded dynamically and never shipped by
CNA. The supported application ABI is 32-bit Windows/x86.

Glide is a fixed-function Voodoo-era API. CNA therefore exposes a small, reliable subset and throws
for functionality with no faithful native counterpart instead of silently falling back to a different
renderer.

## Post-audit adaptation record (2026-08-08)

- Provenance: `feature/glide` at `2f9b47e1281590e6735b5f76ef1e13dd781d8981`, forked from
  `a7a49e3dc135cd3394b04dbc761123584b4e1d45`, contains 32 unique commits. The immutable annotated
  archive is `archive/preintegration/glide-20260804`; adaptation is on `adapt/glide` from
  `integration/post-audit-phase1` at `0a51f8647eb4ddf2fdcd2102756ea79bb49625b7`.
- Runtime identity: CNA's public backend is exactly `GLIDE`; it dynamically resolves the native
  32-bit 3dfx Glide 3.x ABI from `CNA_GLIDE3X_DLL` or Windows DLL lookup. There is no OpenGL,
  EasyGL, SDL_Renderer, or software fallback. CNA has no link-time Glide dependency and vendors no
  compatibility runtime. dgVoodoo2 is only an example caller-supplied binary runtime: this lane
  deliberately pins no dgVoodoo version/commit, does not build it, and does not redistribute or
  relicense it. CNA's own code remains Ms-PL.
- Host classification: the production backend is **build-only / runtime unavailable** on this host.
  No physical Voodoo hardware and no compatible `glide3x.dll` were present. The independent x86
  fake-DLL test exercised all 39 required ABI exports under Wine, but that test double is only a
  wrapper-contract oracle and is not native-hardware or emulator rendering validation. Full i686
  CNA linkage stops in the sibling `sharp-runtime`: its i686 dependency path first lacks ZLIB and
  its accepted `__int128` use is unsupported by `i686-w64-mingw32-g++`.
- Current draw contract: one ordinary stream is supported. Its public `VertexOffset` is consumed
  through the already-folded `vertexStart`/`baseVertex`; `startIndex` selects index elements and
  each decoded index receives `baseVertex`. Residual per-stream offsets, duplicate semantics,
  multi-stream input, all per-instance streams/frequencies, and instancing are rejected
  deterministically. Indexed expansion preserves the resolved declaration and clears only the
  addressing already consumed before entering the common non-indexed submitter.
- Shared adaptations are limited to additive default hooks for sampler mip state and independent
  depth/stencil-plane queries, `GraphicsDevice` forwarding/masking for those hooks, and
  `ClearOptions` complement support. Current integration remains authoritative for every other
  shared interface; in particular, its stream-array `GpuDrawParams` shape is retained and
  `Texture2D` is byte-identical to the pre-Glide integration tree.
- Capability truth: `ThreeD` is true. `DepthStencilBuffer` is false because the backbuffer has
  depth but no stencil. MSAA, MRT, anisotropy, wireframe, occlusion query, custom effects,
  Texture3D, multi-stream input, and instancing are false. PBR is explicitly rejected through draw
  validation; it is not a member of the current common capability enumeration.
- Texture units are explicit: dimensions are texels; retained images, row lengths, and pitch are
  bytes. Pitched RGBA8 rows are bounds/overflow checked and tested at width 3 with padding. CNA
  retains RGBA8, builds address-aware logical mips, and converts to ARGB4444; opt-in adaptive
  conversion can choose RGB565 or ARGB1555 after full-chain alpha classification. No shared
  `Texture2D` cache-authority path or unconditional backend reconstruction hook was added.
- Supported-path findings resolved during adaptation:
  - `REMED-GFX-226` (MEDIUM): TMU1 reused slot 0 filter/address/LOD state. Slots 0 and 1 now retain
    independent filter/LOD state; the one shared s/t channel requires equal address modes and
    rejects a mismatch.
  - `REMED-GFX-227` (MEDIUM): a texture or device could release/close native state while a final
    CNA-owned SpriteBatch remained unsubmitted. Destruction now flushes and fences in order before
    TMU reuse or context shutdown.
  - `REMED-GFX-228` (MEDIUM): preparing texture 1 for TMU1 could evict the already-validated TMU0
    texture. The first texture is restored as the final TMU0 requester and revalidated before any
    native submission.
- Focused validation: 78/78 portable Glide tests across 12 suites, 13/13 shared identity/clear
  contracts, the 39-export x86 fake-DLL ABI contract under Wine, and i686 whole-backend syntax all
  pass. The same 78/78 pass with linked ASan and UBSan runtimes and leak detection enabled, with no
  CNA-originating report. Five serial OPENGLES pixel/state controls pass under Xvfb (textured quad,
  linear filtering, depth-write behavior, culling, and viewport/scissor reset). Sokol, Diligent,
  and Skia need no separate build because no backend-local or texture-cache code changed and the
  additive shared default path is exercised by OPENGLES.

## Completed

- [x] Native `grSstWinOpen`, back-buffer clear/present, LFB readback, and loader support for both
  undecorated and x86 stdcall-exported Glide DLLs.
- [x] `SpriteBatch` texture upload through TMU0 and compatible quad batching through
  `grDrawVertexArrayContiguous`, with
  native `0..256`-per-repeat texture-coordinate unit conversion (GLIDE-AUD-010).
- [x] Fixed-function `VertexPositionColor` triangle lists/strips, including indexed draws and CPU
  `world * view * projection` transforms. Compatible clipped triangle runs are batched through
  native `grDrawVertexArray(GR_TRIANGLES)`.
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
  Glide `s/w`, `t/w`, and `1/w` using native `0..256`-per-repeat coordinate scaling
  (GLIDE-AUD-010). Indexed versions expand the index stream, preserving the source buffer's
  resolved vertex layout (GLIDE-AUD-012), before the same real triangle submission.
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
- [ ] **GLIDE-AUD-006 — Add an automated native-ABI contract test.** The backend deliberately hand-declares Glide 3.x types, numeric constants, layouts and stdcall byte counts. Factor the loader-facing declarations into a small auditable unit and test them against a purpose-built x86 fake `glide3x.dll` that records calls, including undecorated and decorated exports, `grSstWinOpen`, vertex layout, combiner state, texture calls, clear/readback and shutdown ordering. This must run without dgVoodoo and without the external `sharp-runtime` i686 executable dependency. **Implementation staged:** `GlideAbi.hpp` now owns every renderer-facing signature and native layout; the independent x86 fake DLL + Wine contract resolves the complete 39-export surface (37 plus `grGetString`/`grGetProcAddress`, added for GLIDE-FUT-011), checks undecorated and `name@N` stdcall lookup plus missing-export rejection, and calls every resolved signature while the fake recorder proves each entry point was reached. The actual CNA renderer still cannot be instantiated in that target because the sibling i686 `sharp-runtime` build fails on `__int128`; when that dependency is portable, the next stage must use the recorder to assert the real initialization, draw, texture, clear/readback and teardown sequences and their values/order.
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
- [x] **GLIDE-AUD-010 — Convert all texture coordinates to native Glide units.** Confirmed against
  the Glide 3.0 Reference Manual's `grVertexLayout`/`GR_PARAM_STn` description: in the
  `GR_WINDOW_COORDS` mode CNA uses (`grCoordinateSpace(GR_WINDOW_COORDS)`), "s and t coordinates
  for TMU n [are] stored as s/q, t/q in the range [0..256] for one repeat of the texture. The
  range of the smaller dimension is limited by the aspect ratio." (The competing `[0..1]`
  normalized range documented under `grCoordinateSpace` applies only to the separate
  `GR_CLIP_COORDS` mode, which this backend does not use.) Both the SpriteBatch quad path and the
  3D `makeGlideVertex` helper (shared by triangles, points and lines) were instead submitting raw
  tile-local texel offsets — including the address-mode gutter — directly as `sow`/`tow`, with no
  conversion to Glide's native units at all. Added a shared, portable
  `include/CNA/Internal/Backends/Glide/GlideTextureCoordinate.hpp`
  (`GlideNativeTextureCoordinateScale(paddedWidth, paddedHeight)`), returning
  `256 / max(paddedWidth, paddedHeight)`. Because both axes share this one scale, a tile's long
  axis always spans exactly `0..256` for a full wrap while its short axis — having proportionally
  fewer texels — automatically spans a proportionally smaller native range, matching the
  documented aspect-ratio limiting without any explicit branching on which axis is longer. Both
  draw paths now multiply their existing tile-local texel offset (gutter included, so it stays in
  the same unit system as everything else) by this scale before the perspective-divide step that
  already existed. This is a real behavioural change for any tile whose padded long dimension
  isn't exactly 256 (i.e. almost all real textures): previously, e.g. a 128-padded tile only ever
  addressed native coordinates up to 128 out of the required 0..256 range. Five portable probes
  cover a square tile, a 2:1 wide tile, a 1:4 tall tile, size-independence (a 2×2 tile and a
  128×128 tile both reach exactly 256 at their full width), and the invalid-dimension rejection.
  Verified with i686 MinGW `-fsyntax-only` recompilation of the whole backend and the portable
  Glide unit suite (44/44). Fake-DLL vertex-value assertions and the coloured 2×2/64×32/128×128/
  NPOT/multi-tile dgVoodoo image probes remain blocked by the same external i686 `sharp-runtime`
  `__int128` dependency as GLIDE-AUD-006/007.
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
- [x] **GLIDE-AUD-013 — Open the exact resolution/refresh candidate returned by Glide.** Startup
  previously selected `displayMode` by pixel area alone and then always opened it with a
  hardcoded `kRefresh60Hz`, even though `grQueryResolutions` is queried with `GR_QUERY_ANY`
  resolution/refresh and can legitimately return the same resolution token multiple times at
  different refresh rates — or never at 60 Hz at all for the chosen dimensions. The selection and
  the native-open call are now driven by a new portable
  `include/CNA/Internal/Backends/Glide/GlideDisplayModeSelection.hpp`
  (`KnownGlideDisplayMode`/`SelectGlideDisplayMode`), which keeps each candidate's resolution and
  refresh paired together: smaller-area candidates always win outright, and among candidates tied
  on area, 60 Hz is preferred deterministically if any tied candidate offers it, otherwise the
  first tied candidate encountered is kept — so the resulting `(resolution, refresh)` handed to
  `grSstWinOpen` is always literally one of the real candidates, never a resolution from one and a
  refresh from another. Eight portable probes cover the smallest-sufficient-resolution case, a
  target resolution with no 60 Hz candidate at all, 60 Hz preference among multiple offered
  refresh rates, input-order-determinism when no candidate is 60 Hz, an explicit
  never-combines-mismatched-candidates check against an awkward multi-resolution/multi-refresh
  matrix, and the two failure cases (nothing large enough, empty list). Verified with i686 MinGW
  `-fsyntax-only` recompilation of the whole backend and the portable Glide unit suite (39/39). A
  fake-DLL mode-matrix capture (no-60-Hz entries, multiple refresh rates, a failing
  `grSstWinOpen`) and the resulting real `grSstWinOpen` argument assertion remain blocked by the
  same external i686 `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007.
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
- [x] **GLIDE-AUD-015 — Make state application exception-atomic.** Audited all four named
  categories:
  - **Blend** — already fixed as part of GLIDE-AUD-011: `ApplyBlendState()` resolves all four
    factors into locals (and validates the depth/auxiliary-buffer conflict) before writing any of
    `impl_->colorSrcBlend`/`colorDstBlend`/`alphaSrcBlend`/`alphaDstBlend` or calling
    `grAlphaBlendFunction`.
  - **Depth** — already fixed as part of GLIDE-AUD-011: `ApplyDepthStencilState()` resolves
    `ToGlideDepthCompare(depthFunc)` and validates the same auxiliary-buffer conflict before
    writing `impl_->depthTestEnabled`/`depthWriteEnabled`/`depthCompare`; `SetDepthTestEnabled()`
    and `SetBlendEnabled()` validate the same conflict symmetrically before their own mutation.
  - **Cull** — audited, no change needed. `ApplyRasterizerState()` already validates `fillMode`
    and `depthBias`/`slopeScaleDepthBias` before touching any state, and its `cullMode` switch's
    `default` throws before reaching a `grCullMode()` call; only a recognized `cullMode` value ever
    mutates native state, so an unsupported value already leaves everything untouched.
  - **Alpha-test** — `DecodeAlphaTest()` is a total function over its float inputs (every input
    maps to a defined native compare function/reference; there is no "unsupported combination" to
    reject), but `DrawPrimitiveRange()` was pushing its result to native Glide
    (`grAlphaTestReferenceValue`/`grAlphaTestFunction`) immediately after decoding it — before
    several later checks that **can** throw and abort the draw entirely (vertex-buffer/texture
    downcasts, `vertexStart` bounds, textured-without-a-texture, lighting-without-a-normal-stream,
    and the lighting normal-matrix inversion for a singular `World`). A draw that failed any of
    those still permanently changed Glide's native alpha-test state even though it submitted no
    geometry. Moved the two native calls to immediately before the actual primitive-type dispatch,
    after every throw-capable check in the function has already passed.
  Verified with i686 MinGW `-fsyntax-only` recompilation of the whole backend and the portable
  Glide unit suite (31/31, unaffected — this is all renderer-internal control flow). A fake-DLL
  failure-injection capture (apply valid state, attempt an invalid state, redraw, and prove the
  prior state's native calls repeat unchanged) remains blocked by the same external i686
  `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007.
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
  applies it before alpha-aware fog, and correctly applies the inverse-transpose normal transform
  (GLIDE-AUD-004 fixed the row/column transpose bug the 2026-08-03 audit found). Nine portable unit
  probes now cover a front/back light, three-light sum, diagonal non-uniform scale, rotation,
  non-symmetric shear, a perpendicularity-preservation invariant,
  vertex-colour/emissive/specular order and fog. The recorder cannot yet drive the full CNA
  backend, so a fake-DLL renderer-sequence assertion and dgVoodoo/real-Voodoo image capture remain
  required before this capability is release-validated.
- [x] **GLIDE-FUT-004 — Use a second TMU when the selected runtime actually has one.**
  **Scope confirmed with the project owner (2026-08-04):** implemented for single-tile texture0
  and texture1 only, both required to share identical width/height; full unequal-tile-grid /
  independent-second-UV-channel support is an explicit, loudly-rejected non-goal of this pass, not
  a silent limitation (see below). Startup now queries `grTexMinAddress(1)`/`grTexMaxAddress(1)`
  whenever `GR_NUM_TMU >= 2` was already detected, registers a second per-TMU free-range allocator
  (`freeTextureRangesByTmu[1]`/`residentTexturesByTmu[1]`, reusing the exact GLIDE-FUT-013
  allocator/eviction machinery `AllocateTexture`/`ReleaseTexture`/`TryFitTexture` now take a `tmu`
  parameter for) and registers `GR_PARAM_ST1` at the same vertex offset as `GR_PARAM_ST0` via
  `grVertexLayout`, and only then sets `secondTmuAvailable = true`.
  `GlideTextureBackend` gained `EnsureTmu1Resident()`/`BuildSingleTmu1Tile()`: a second, independent
  single-tile upload path (no gutter, no multi-tile split -- `BuildSingleTmu1Tile` throws if the
  logical image exceeds one native tile) that reuses the already-built TMU0 logical mip pyramid and
  the same address-mode-aware conversion and adaptive-format selection as GLIDE-FUT-007, tracked by
  its own `tmu1Range_`/`tmu1NativeInfo_`/`tmu1UploadedAddressU_`/`tmu1UploadedAddressV_` state and
  released in the destructor and on rebuild.
  `DrawPrimitiveRange` validates, before touching any native state: TMU0 texture present,
  `TriangleList`/`TriangleStrip` only, `secondTmuAvailable`, `params.texture1` resolves to a
  `GlideTextureBackend`, and `texture0.GetWidth()/GetHeight() == texture1.GetWidth()/GetHeight()`
  -- throwing a specific `std::runtime_error` for each violation rather than misrendering. The
  dimension-equality requirement is load-bearing, not cosmetic: this backend's vertex-declaration
  parser (GLIDE-AUD-012/FUT-002) only accepts a single `TextureCoordinate0` semantic, so `GR_PARAM_ST1`
  necessarily reads the *same* native `(sow, tow)` bytes as `GR_PARAM_ST0`. Native `s = u * width *
  (256/paddedWidth)`; since `paddedWidth = NextPowerOfTwo(width)`, two textures of identical width
  necessarily produce an identical `s` (and likewise `t`) for the same `u`/`v` -- sharing one UV
  channel between TMUs is then mathematically exact, not approximate, which is what makes the
  narrower scope correct rather than merely convenient. `ValidateFixedFunctionDrawParams` also now
  rejects `lightingEnabled && dualTexture` together (`BasicEffect` lighting and `DualTextureEffect`
  are mutually exclusive stock effects in FNA/XNA).
  The fixed-function combiner chain (`Impl::ConfigureDualTextureCombiner()`) reproduces FNA's
  `DualTextureEffect.fx` exactly: `color = tex0; color.rgb *= 2; color *= tex1 * diffuse` (alpha is
  never doubled). Glide's combiner has no native "x2" stage, so the x2 is folded into the
  CPU-computed iterated RGB in `DrawPrimitiveRange`'s `readVertex` (after lighting/fog, before the
  final `ClampUnit`), which is exactly equivalent by associativity/commutativity of scalar
  multiplication (`2*tex0*tex1*diffuse == tex0*tex1*(2*diffuse)`), not an approximation. TMU1 is
  configured as the upstream stage (`grTexCombine(1, LOCAL, ONE, LOCAL, ONE, ...)`, passing its own
  sampled texel through unchanged as `Cother`/`Aother`); TMU0 multiplies its own sample by that
  upstream output (`grTexCombine(0, SCALE_OTHER, LOCAL, SCALE_OTHER, LOCAL, ...)`, chaining
  `tex0 * tex1`); the existing final iterated stage (shared with `ConfigureSpriteCombiner`) then
  multiplies by the (already 2x-prescaled) iterated colour. `DrawPrimitiveRange`'s textured-triangle
  branch selects this combiner instead of `ConfigureSpriteCombiner()` when `params.dualTexture`, and
  binds TMU1 once inside `bindTriangleTile` (texture0 is enforced single-tile whenever dual-textured,
  so this runs at most once per draw call). `DrawIndexedPrimitiveRange` needs no separate wiring --
  it always delegates to `DrawPrimitiveRange` with `params` forwarded unchanged.
  Verified with i686 MinGW `-fsyntax-only` recompilation of the whole backend and the full portable
  Glide unit suite (65/65, unchanged -- this ticket's logic is combiner-native-call sequencing and a
  single associative scalar multiply, not new branchy CPU math, so no new portable header/test file
  was warranted, matching the precedent that `ConfigureSpriteCombiner`/`ConfigureColoredCombiner`
  themselves are native-call sequences and were never separately unit-tested either). An end-to-end
  two-texture fake-DLL capture or dgVoodoo/real-Voodoo image comparison remains blocked by the same
  external i686 `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007. Unequal-tile-grid /
  independent-UV-channel `DualTextureEffect` support (the ticket's original "unequal tile grids"
  clause) is left for a future ticket if ever needed; it would require GLIDE-FUT-002's vertex
  decoder to accept a genuine second `TextureCoordinate1` semantic first.
  **Correctness fix found during self-review:** `UpdatePixels()`/`UpdatePixelsLevel()` already
  refreshed TMU0's `tiles_` from the rebuilt logical pyramid, but a texture currently resident on
  TMU1 keeps its own, separately-uploaded `tmu1NativeTexels_`/`tmu1Range_` -- left untouched, a
  `Texture2D::SetData()` call on a texture bound as `DualTextureEffect`'s second texture would
  silently keep TMU1 sampling the pre-update pixels. Added `RefreshTmu1IfBuilt()`, called from both
  update paths after their existing TMU0 refresh, which releases and rebuilds the TMU1 upload in
  place (no-op when the texture was never used on TMU1).
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
- [x] **GLIDE-FUT-007 — Select the best native texture encoding per logical texture.**
  **Audit (2026-08-01) confirmed and acted on:** the Glide 3.0 guide confirms RGB565 (`0xa`),
  ARGB1555 (`0xb`) and ARGB4444 (`0xc`, cross-checked against the already-shipping
  `kTexFormatArgb4444` constant) are all 16-bit texture formats, so the required TMU byte range is
  identical regardless of which is chosen -- no reallocation is ever needed, only re-packing the
  existing tile range and updating `GrTexInfo.format`. The classifier the audit called for now
  exists in a portable `GlideTextureFormat.hpp`
  (`ClassifyGlideArgb4444AlphaCoverage`/`CombineGlideTextureAlphaClass`), and inspects every
  explicit and generated logical mip level (address-padded, exactly as the audit required) rather
  than only the base level, closing the exact silent-one-bit-mask hazard the audit flagged.
  `BuildLogicalMipChain()` recomputes the combined classification every time it runs (construction,
  address-mode change, eviction rebuild, or a pixel update), so "later updates changing the alpha
  class" are handled by the *same* mechanism that already re-derives the logical pyramid, not a
  separate rule. `GlideArgb4444ToRgb565`/`GlideArgb4444ToArgb1555` re-pack each already-quantized
  ARGB4444 texel using standard high-bit replication (not a naive left-shift, which would bias the
  widened channel toward black) rather than re-deriving from the RGBA8 source, deliberately
  matching this backend's existing "the whole mip pyramid is generated in ARGB4444 space" design
  (documented in this file's Fixed-function fidelity notes) instead of redesigning it.
  **Deliberately not enabled by default:** the prior audit explicitly required a fake-DLL memory/
  value capture and sampling/blending images before enabling any non-ARGB4444 format, and that
  validation is still blocked by the same external i686 `sharp-runtime` dependency as
  GLIDE-AUD-006/007. The classifier and re-packing are wired into `ConvertTileToGlideTexels()` but
  gated behind a new opt-in `CNA_GLIDE_ADAPTIVE_TEXTURE_FORMAT` environment variable (mirroring the
  existing `CNA_GLIDE_DIAGNOSTICS` pattern), defaulting to off, so every already-validated
  ARGB4444 texture is completely unaffected unless a caller explicitly opts in. Ten portable
  probes cover classification (opaque/binary/fractional/empty), combination across levels, and
  both converters' extremes and exact bit positions. Verified with i686 MinGW `-fsyntax-only`
  recompilation of the whole backend and the portable Glide unit suite (65/65). Palette/NCC
  compression remains untouched, as required.
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
- [x] **GLIDE-FUT-011 — Negotiate optional runtime extensions safely.** The Glide 3.0 Reference
  Manual documents exactly one DLL-name-independent extension mechanism: `grGetString(GR_EXTENSION)`
  returns a space-separated list of supported extension names (a single space means none), and
  `grGetProcAddress(name)` resolves an extension's function pointers, only for functions listed
  against an extension actually present in that list. `grGetString`/`grGetProcAddress` are now
  resolved as two additional required core exports in `GlideAbi.hpp` (39 total). Added a portable
  `GlideExtensionCapabilities.hpp` (`ParseGlideExtensionList`, `GlideExtensionListContains`) plus
  `Impl::QueryRuntimeCapabilities()`, called once at startup alongside the existing
  `QueryHardwareLimits()`, which queries `GR_HARDWARE`/`GR_RENDERER`/`GR_VENDOR`/`GR_VERSION`
  (throwing if any of these four basic-identity strings is unexpectedly null) and
  `GR_EXTENSION` (tolerated as "no extensions" if null, since extension support is inherently
  optional). `CNA_GLIDE_DIAGNOSTICS=1` now reports hardware/renderer/vendor/version and the
  parsed extension list, explicitly labelled "none used yet -- detection only": this task is
  infrastructure and reporting only, not a policy to adopt any specific extension yet, matching
  "otherwise keep the portable tiled/core path" until a later task designs, tests and visually
  validates one specific extension's use (e.g. GLIDE-FUT-012's NPOT path, gated on a real
  advertised extension name rather than invented). Six portable probes cover the space-separated
  parser (normal list, the single-space "none" case, empty/null input, repeated/surrounding
  spaces, a single name, and exact-match containment). **Verified further than the usual portable/
  i686-`-fsyntax-only` pair this session:** built the fake DLL and `GlideAbiLoaderTests.cpp`
  client with i686 MinGW and actually ran the resulting `GlideAbiLoaderContract` test under Wine
  outside a build directory (throwaway location, cleaned up afterward) — exit code 0, confirming
  the two new exports resolve and are called correctly and the full 39-bit recorder mask is
  observed, without needing dgVoodoo or the blocked `sharp-runtime` i686 dependency (this specific
  contract test has never needed either). The GLIDE-AUD-006 entry above is updated from a
  37-export to a 39-export count; `docs/glide-backend.md` does not itself state a specific count.
- [ ] **GLIDE-FUT-012 — Exploit genuine NPOT texture support when advertised.** After
  GLIDE-FUT-011, add an optional direct-NPOT storage path for runtimes reporting it, while keeping
  the existing tiled power-of-two path as the canonical fallback. Verify mip selection, wrap,
  clamp, mirror and texture-coordinate precision before preferring the extension path.
- [x] **GLIDE-FUT-013 — Improve texture-memory residency under pressure.** `AllocateTexture()`
  previously threw "TMU0 texture memory is exhausted" outright on the first failed first-fit
  search. It now retries: on exhaustion, it flushes any pending SpriteBatch quad (see below),
  picks the least-recently-used *other*, currently-resident texture via a new portable
  `GlideTextureEviction.hpp` (`SelectGlideEvictionVictim`), evicts it (`grFinish()`, release every
  native tile range, `tiles_.clear()` -- the CPU-side `rgba_`/`logicalMipLevels_`/
  `explicitMipLevels_` source is untouched), and retries the fit. This repeats until an allocation
  succeeds or no evictable candidate remains, at which point it throws the original exhaustion
  error rather than ever returning a partial allocation. `GlideTextureBackend` now also implements
  a small forward-declared `IGlideResidentTexture` interface (`IsResident`/`LastUsedCounter`/
  `EvictAndReleaseNativeMemory`) and self-registers with `Impl::residentTextures` after
  successfully constructing, self-unregisters in its destructor. `EnsureAddressMode()` -- the
  single hook already called at the top of every real draw use -- now also stamps a deterministic
  logical-clock `lastUsedCounter_` (not wall-clock time, so LRU ordering stays reproducible) and,
  when it finds `tiles_` empty (evicted since last use), atomically reconstructs every tile from
  the still-valid logical pyramid via `BuildTiles()` inside the same try/catch-and-roll-back
  pattern the constructor already used, so a mid-rebuild failure (TMU0 still exhausted even after
  evicting everything else) leaves the texture cleanly fully-evicted again, never partially
  resident.
  **Found and fixed a real hazard from composing this with GLIDE-FUT-015's deferred SpriteBatch
  submission, done earlier this session:** a queued-but-unsubmitted sprite's vertex data is
  already computed against whatever a texture's tiles contained when it was queued; mutating that
  content in place (an address-mode change, `UpdatePixels`/`UpdatePixelsLevel`) or evicting/
  rebuilding it, while such a quad is still unflushed, would make it render with texture data it
  was never actually queued against. `EnsureAddressMode()`, `UpdatePixels()`,
  `UpdatePixelsLevel()`, and `AllocateTexture()`'s eviction branch (the last one for the case
  where a *different*, brand-new texture's construction evicts an unrelated existing one) all now
  call `Impl::FlushSpriteBatch()` before touching native tile state. This closes a gap that was
  already latent in the FUT-015 change (its own flush-coverage sweep only checked
  `GlideGraphicsBackend::` methods, not `GlideTextureBackend::` ones).
  Eviction is reported through the existing `CNA_GLIDE_DIAGNOSTICS=1` channel (bytes freed per
  eviction). Five portable probes cover LRU selection, requester self-exclusion, skipping
  non-resident candidates, the no-eligible-candidate case, and deterministic tie-breaking (first
  encountered wins on equal counters, verified with reversed input order). Verified with i686
  MinGW `-fsyntax-only` recompilation of the whole backend and the portable Glide unit suite
  (55/55). An end-to-end memory-pressure/eviction fake-DLL or dgVoodoo capture remains blocked by
  the same external i686 `sharp-runtime` `__int128` dependency as GLIDE-AUD-006/007.
- [x] **GLIDE-FUT-014 — Replace pointer-array batches with the contiguous vertex-array path.**
  Load and ABI-test `grDrawVertexArrayContiguous`, then use it for compatible triangle batches if
  its byte-stride and FIFO behaviour match the existing pointer-array implementation. Compatible
  triangle batches now pass their contiguous `GlideVertex` storage and `sizeof(GlideVertex)`
  directly; the 1024-triangle state/tile flush boundaries are unchanged. The x86 fake-DLL build
  resolves the 16-byte stdcall export; runtime FIFO/image validation remains in GLIDE-AUD-007.
- [x] **GLIDE-FUT-015 — Batch SpriteBatch without changing its ordering contract.**
  `DrawSprite()` previously issued `grTexSource`/`grTexFilterMode`/`grTexClampMode`/
  `grTexMipMapMode`/`grTexLodBiasValue` plus two immediate `grDrawTriangle` calls per sprite per
  tile, even for consecutive sprites sharing the same texture/tile/sampler. It now accumulates
  compatible quads into `Impl::pendingSpriteTriangles` and submits them with one
  `grDrawVertexArrayContiguous` call (bounded at `kMaxPendingSpriteVertices = 3072`, matching the
  existing 3D triangle batch cap), rebinding TMU0 only when the tile's native address or the
  filter/address-mode parameters actually change (tracked by native TMU address, not a C++
  pointer, to avoid any lifetime assumption about `Tile` object addresses).
  **Ordering-contract audit (mandatory, since this changes previously-immediate submission into
  deferred submission):** every `GlideGraphicsBackend` method that either mutates cached/native
  rendering state read at flush time (blend, depth, cull, scissor, viewport, virtual resolution,
  the shared `samplerLodBias`) or issues its own native draw/clear/present/readback command now
  calls `impl_->FlushSpriteBatch()` first, so a queued-but-unsubmitted sprite can never be
  rendered under a state it wasn't actually drawn with, and can never be reordered relative to
  another native submission. Protected entry points: `Clear`, `Present`, `ReadBackbuffer`,
  `SetVirtualResolution`, `SetViewport`, `SetRenderTarget2D`, `SetRenderTargets`,
  `SetScissorRect`, `ApplyBlendState`, `SetBlendEnabled`, `ApplyDepthStencilState`,
  `SetDepthTestEnabled`, `SetDepthWriteEnabled`, `ApplyRasterizerState`, `ApplySamplerMipState`,
  `ClearColorAndDepth`, `ClearDepth`, and `DrawPrimitiveRange` (the single funnel point for all
  six 3D draw entry points). Verified this list is exhaustive with a scripted sweep of every
  `GlideGraphicsBackend::` method body for native `gr*` calls or writes to the relevant `impl_`
  fields; the only method the sweep flagged besides the ones above (`SetPresentationMode`) was
  confirmed to only ever reassign `presentationMode` to the value it is already statically
  guaranteed to hold, with no other code reading it — a true no-op requiring no flush.
  `ApplySamplerState` (TMU sampler filter/address for the *3D* path) and `SetSwapInterval` were
  confirmed independent of the sprite queue and intentionally left unprotected. Verified with
  i686 MinGW `-fsyntax-only` recompilation of the whole backend and the unaffected portable Glide
  unit suite (44/44). Native call-count reduction and translucent-overlap image comparisons
  remain blocked by the same external i686 `sharp-runtime` `__int128` dependency as
  GLIDE-AUD-006/007.
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
