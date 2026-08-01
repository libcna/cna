# Glide 3.x backend

The `GLIDE` backend is a 32-bit Windows-only, dynamically loaded implementation of the historical 3dfx
**Glide 3.x** API. It is deliberately not an SDL renderer facade: CNA opens a real Glide context
through `grSstWinOpen`, sends SpriteBatch quads as two `grDrawTriangle` calls, uploads texture data
through the Glide TMU API, swaps with `grBufferSwap`, and reads pixels with `grLfbReadRegion`.

It is intended for an emulator runtime such as [dgVoodoo2](https://dgvoodoo2.dege.freeweb.hu/).
`glide3x.dll` is external to CNA: this repository neither contains nor copies it. Place a compatible
DLL beside the executable, or set `CNA_GLIDE3X_DLL` to the DLL's full Windows path before launching.
Use `PresentationMode::NativeBackBuffer` and a swap interval of either 0 or 1. Glide has no
faithful CNA logical-surface scaling path; other presentation modes and intervals above one are
rejected rather than silently ignored. A later virtual-resolution change is accepted only while it
fits the Glide mode selected at startup, so `GraphicsDevice` retains its previous public size if
the backend rejects it.

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

Copy a compatible 32-bit `glide3x.dll` (and its required dgVoodoo companion files) beside
`cna_glide_smoke.exe`, configure dgVoodoo for the machine, then run the executable under Windows
or Wine. Alternatively, set `CNA_GLIDE3X_DLL` before launching. The smoke program verifies a green
clear, a red one-pixel texture drawn with `SpriteBatch`, a native Gouraud-shaded
`VertexPositionColor` triangle, and direct backbuffer readback. It is not a
normal CTest because CNA cannot provide or configure the external emulator.

## Supported fixed-function scope

- `Clear`, `Present`, `SpriteBatch`, `Texture2D` upload/update, point/bilinear filtering, and
  clamp/wrap/mirror addressing.
- Fixed-function 3D `VertexPositionColor`, `VertexPositionTexture`,
  `VertexPositionColorTexture`, and vertex-lit `VertexPositionNormalTexture` triangle lists/strips,
  indexed or non-indexed. CNA CPU-transforms and clips the triangle in homogeneous XNA space;
  Glide then rasterizes every resulting fan triangle with its native depth, TMU, alpha-test and
  blending pipeline. Texture parameters are submitted as perspective-correct `s/w`, `t/w`, `1/w`.
  `Viewport` XY, depth range and the intersection of viewport/scissor are applied before native
  rasterization.
- Unlit BasicEffect material/vertex colour, and the documented per-vertex directional-diffuse
  BasicEffect subset (ambient/emissive included). Per-pixel lighting and specular are rejected.
  CNA computes vertex lighting and its `fogVector` on the CPU, then hands the iterated RGB to
  Glide; this is required because Glide's depth-table fog cannot represent an arbitrary XNA fog
  vector. It is a documented approximation, not a hidden fallback renderer.
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

- The 3D path is deliberately fixed-function: arbitrary shader Effects, multi-texture effects,
  environment mapping, PBR, skinning, instancing, lines/points, per-pixel lighting and specular
  remain unsupported and report that explicitly. It is not a shader-emulation layer.
- Render targets, multisampling, texture cubes/volumes, stencil, occlusion queries, and custom
  SpriteBatch effects are unsupported by this native Glide scope and report that explicitly.
- Only additive blend equations are available in this scope. The standard source/destination blend
  factors supported by Glide are mapped; XNA constant blend-color factors are rejected.
- There is no fallback to SDL, Direct3D, OpenGL, or software rendering. A missing or incompatible
  `glide3x.dll` is a startup error by design.

For emulator installation and redistribution conditions, consult dgVoodoo2's own
[documentation](https://dgvoodoo2.dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/). CNA does not package
that runtime.
