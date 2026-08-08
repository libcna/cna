# Glide 3.x backend

The `GLIDE` backend is a 32-bit Windows-only, dynamically loaded implementation of the historical 3dfx
**Glide 3.x** API. It is deliberately not an SDL renderer facade: CNA opens a real Glide context
through `grSstWinOpen`, batches compatible SpriteBatch quads through
`grDrawVertexArrayContiguous`, uploads texture data
through the Glide TMU API, swaps with `grBufferSwap`, and reads pixels with `grLfbReadRegion`.

It is intended for an emulator runtime such as [dgVoodoo2](https://dgvoodoo2.dege.freeweb.hu/).
`glide3x.dll` is external to CNA: this repository neither contains nor copies it. Place a compatible
DLL beside the executable, or set `CNA_GLIDE3X_DLL` to the DLL's full Windows path before launching.
Use `PresentationMode::NativeBackBuffer` and a swap interval of either 0 or 1. Glide has no
faithful CNA logical-surface scaling path; other presentation modes and intervals above one are
rejected rather than silently ignored. A later virtual-resolution change is accepted only while it
fits the Glide mode selected at startup, so `GraphicsDevice` retains its previous public size if
the backend rejects it.

## Dependency and validation provenance

CNA links no Glide library and carries no third-party Glide source or binary. Its only reproducible
dependency contract is the hand-declared 32-bit Glide 3.x ABI: an independent x86 fake DLL exercises
all 39 required exports. The production DLL is deliberately caller-supplied and unpinned; CNA does
not select a dgVoodoo2 version/commit, build it, patch it, redistribute it, or claim its license as
CNA's. Consult the selected runtime's own license and redistribution terms. CNA's backend source is
covered by the repository's Ms-PL license.

The 2026-08-08 post-audit host had no physical Voodoo hardware and no compatible `glide3x.dll`.
Validation is therefore **build-only** for the production renderer, plus a fake-wrapper ABI contract
under Wine. It is not emulator image validation and not native-hardware validation. Full CNA i686
linkage was externally blocked in the sibling `sharp-runtime` dependency path; the backend itself
passed i686 whole-translation-unit syntax, 78 portable tests, and the same 78 tests under linked
ASan/UBSan runtimes. Five OPENGLES pixel/state tests provided the shared-interface control.

Set `CNA_GLIDE_DIAGNOSTICS=1` to print the loaded runtime path, selected virtual/native mode,
TMU count, texture limits and usable TMU0 bytes at startup. The report contains no native pointer
values and is intended for emulator/hardware bug reports.

## Build

The backend is hard-gated to the native 32-bit Windows ABI. From Linux, use the i686 MinGW-w64
toolchain and keep global build parallelism at two jobs or fewer:

```bash
cmake -S . -B cmake-build-glide -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake \
  -DCNA_GRAPHICS_BACKEND=GLIDE \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-glide --target cna_glide_smoke -j2
```

The loader ABI can be checked independently of CNA, SDL, sharp-runtime and dgVoodoo:

```bash
cmake --build cmake-build-glide --target cna_glide_abi_loader_test -j2
cd cmake-build-glide && wine cna_glide_abi_loader_test.exe
```

That target builds a deliberately tiny x86 fake DLL and verifies undecorated and MinGW stdcall
export lookup, plus the error path for a missing export. It is a loader-contract test, not a
replacement for the full fake-DLL render-call capture or a visual runtime test.

Copy a compatible 32-bit `glide3x.dll` (and its required dgVoodoo companion files) beside
`cna_glide_smoke.exe`, configure dgVoodoo for the machine, then run the executable under Windows
or Wine. Alternatively, set `CNA_GLIDE3X_DLL` before launching. The smoke program verifies a green
clear, a red one-pixel texture drawn with `SpriteBatch`, a native Gouraud-shaded
`VertexPositionColor` triangle, and direct backbuffer readback. It is not a
normal CTest because CNA cannot provide or configure the external emulator.

## Supported fixed-function scope

- `Clear`, `Present`, `SpriteBatch`, full and partial `Texture2D::SetData` uploads on every
  declared mip level, point/bilinear filtering, and clamp/wrap/mirror addressing. The shared
  texture layer supplies Glide a complete updated mip image after a rectangle write; Glide
  retains explicit lower levels and rebuilds its tiled ARGB4444 pyramid after FIFO completion.
  Runtime image capture of this path remains part of the hardware/emulator release gate.
- Fixed-function 3D `VertexPositionColor`, `VertexPositionTexture`,
  `VertexPositionColorTexture`, and vertex-lit `VertexPositionNormalTexture` triangle lists/strips,
  `LineList`, `LineStrip`, and `PointListEXT`, indexed or non-indexed. Triangles use
  `GR_TRIANGLES`; points use `GR_POINTS`; and independent or clipped line-strip runs use
  `GR_LINES`, so clipping cannot join disjoint visible runs. Compatible custom `VertexDeclaration`s may place `Position0` (Vector3),
  `Color0` (Color), `Normal0` (Vector3), and `TextureCoordinate0` (Vector2) at arbitrary offsets
  in the declared stride; all other semantics/formats are rejected rather than misread. CNA
  CPU-transforms and clips triangles, lines and points in homogeneous XNA space; Glide then
  rasterizes the resulting primitives with its native depth, TMU, alpha-test and blending pipeline.
  The point/line implementation has portable clip tests, while its native image capture remains a
  release-validation item. Texture
  parameters are submitted as perspective-correct `s/w`, `t/w`, `1/w`. `Viewport` XY, depth range
  and the intersection of viewport/scissor are applied before native rasterization.
- Unlit BasicEffect material/vertex colour, and the documented per-vertex directional BasicEffect
  subset (ambient, emissive, and three-light Blinn-Phong specular included). Per-pixel lighting
  is rejected. CNA computes vertex lighting and its `fogVector` on the CPU, then hands the
  iterated RGB to Glide; this is required because Glide's depth-table fog cannot represent an
  arbitrary XNA fog vector. The CPU specular arithmetic has deterministic unit coverage; image
  capture on a real Glide runtime remains a release-validation item, not a fallback renderer.
- Color-only and color-plus-depth clears, native depth-test/depth-write/depth-compare, native
  culling, AlphaTestEffect's discrete alpha comparisons, and blend enable/disable. Glide has a
  depth buffer but no stencil buffer: common `GraphicsDevice::Clear(Color)` clears colour and
  depth, while stencil-only portions are masked by the shared clear routing. RGB and alpha write
  masks are independently supported; a mask that separates R/G/B or requests a sample mask is
  rejected.
- Textures are converted from CNA RGBA8 to native Glide ARGB4444, padded to power-of-two tiles,
  and receive complete generated ARGB4444 mip chains. The backend queries the runtime texture and
  aspect limits and tiles larger logical images rather than downsampling them. For 3D draws, the
  CPU partitions clipped geometry at logical tile and `Wrap`/`Clamp`/`Mirror` boundaries, then
  Glide TMU0 performs the final filtered sample from each tile. Tile padding is regenerated from
  the retained RGBA source when the address mode changes, and an internal one-texel neighbour
  gutter preserves level-0 linear filtering at a tile boundary. A single logical ARGB4444 mip
  pyramid supplies every tile LOD, so minification does not derive colours from isolated tile
  padding. SpriteBatch source rectangles are split per tile directly. The remaining fidelity item
  is only a possible sub-texel LOD phase at a tile edge; it requires visual validation on a real
  Glide runtime before any compensation is added.
- The native context uses `grQueryResolutions` and selects the smallest supported historical Glide
  double-buffered depth mode that contains CNA's virtual resolution, rather than assuming a fixed
  640×480/800×600 capability. It also queries texture/TMU limits through `grGet`.
- `GetBackBufferData` reads the Glide backbuffer as RGB565; alpha reads back as fully opaque,
  matching the native color-buffer format.

## Deliberate boundaries

- The 3D path is deliberately fixed-function: arbitrary shader Effects, environment mapping, PBR,
  skinning, instancing and per-pixel lighting remain unsupported and report that explicitly. A
  narrow `DualTextureEffect` subset uses two real TMUs when the runtime reports them: both textures
  must be same-sized, single-tile triangle textures sharing address modes and one texture-coordinate
  channel; their filter and LOD-bias sampler state remains independent. It is not a
  shader-emulation layer.
- Render targets, multisampling, texture cubes/volumes, stencil, occlusion queries, and custom
  SpriteBatch effects are unsupported by this native Glide scope and report that explicitly.
- Only additive blend equations are available in this scope. The standard source/destination blend
  factors supported by Glide are mapped; XNA constant blend-color factors are rejected.
- There is no fallback to SDL, Direct3D, OpenGL, or software rendering. A missing or incompatible
  `glide3x.dll` is a startup error by design.

## Capability and input contract

| Current capability | GLIDE result |
|---|---|
| `ThreeD` | Supported by the documented fixed-function subset; runtime build-covered on this host |
| `DepthStencilBuffer` | False: default backbuffer has depth but no stencil |
| `MultiSampleAntiAliasing`, `MultipleRenderTargets` | Unsupported and rejected |
| `AnisotropicFiltering` | Unsupported; anisotropic filters reject |
| `WireFrame` | Unsupported; only solid fill is accepted |
| `OcclusionQuery`, `CustomEffects`, `Texture3D` | Unsupported and rejected |
| `MultiStreamVertexInput`, `Instancing` | Unsupported and rejected |

One ordinary vertex stream is accepted. Its `VertexBufferBinding::VertexOffset` reaches Glide only
through the current shared layer's folded `vertexStart`/`baseVertex`; `startIndex` is an index-element
offset and `baseVertex` is added once to each decoded index. Residual per-stream offsets, instance
frequency, duplicate semantics, unsupported declaration elements, or a second stream fail before
native submission. `DrawInstancedPrimitives` remains unsupported.

Texture width/height are texels; RGBA row length, source pitch, retained storage, and destination
storage are bytes. Pitched copies validate dimensions, arithmetic, and row extent, including
nontrivial widths. The normal destination is ARGB4444; opt-in alpha classification can select
RGB565 or ARGB1555. The backend keeps its own retained image and never changes `Texture2D`'s shared
CPU/GPU cache-authority contract.

For emulator installation and redistribution conditions, consult dgVoodoo2's own
[documentation](https://dgvoodoo2.dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/). CNA does not package
that runtime.
