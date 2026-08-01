# Glide 3.x backend

The `GLIDE` backend is a 32-bit Windows-only, dynamically loaded implementation of the historical 3dfx
**Glide 3.x** API. It is deliberately not an SDL renderer facade: CNA opens a real Glide context
through `grSstWinOpen`, sends SpriteBatch quads as two `grDrawTriangle` calls, uploads texture data
through the Glide TMU API, swaps with `grBufferSwap`, and reads pixels with `grLfbReadRegion`.

It is intended for an emulator runtime such as [dgVoodoo2](https://dgvoodoo2.dege.freeweb.hu/).
`glide3x.dll` is external to CNA: this repository neither contains nor copies it. Place a compatible
DLL beside the executable, or set `CNA_GLIDE3X_DLL` to the DLL's full Windows path before launching.

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

## Initial supported scope

- `Clear`, `Present`, `SpriteBatch`, `Texture2D` upload/update, point/bilinear filtering, and
  clamp/wrap/mirror addressing.
- Fixed-function 3D `VertexPositionColor` triangle lists and strips, both indexed (16- or 32-bit
  CNA indices expanded to native `grDrawTriangle` calls) and non-indexed. CNA performs the
  `world * view * projection` transform on the CPU; Glide handles the actual Gouraud triangle
  rasterization and its native 16-bit Z buffer. Vertices must have positive clip W and post-project
  Z in `[0, 1]`; crossing triangles are not yet split at a frustum plane.
- Color-only and color-plus-depth clears, depth-test/depth-write toggles, and blend enable/disable.
  Glide has a depth buffer but no stencil buffer, so stencil requests remain unsupported.
- Textures are converted from CNA RGBA8 to native Glide ARGB4444. Oversized images are resampled
  to fit the 256-pixel axis, then padded to Glide's power-of-two geometry; uploaded data remains
  within Glide's 8:1 aspect-ratio envelope while the SpriteBatch destination rectangle is preserved.
- The native context selects Glide's 640×480 or 800×600 double-buffered mode at startup. CNA
  virtual resolution may be from 1×1 through 800×600 and cannot outgrow the selected native mode.
- `GetBackBufferData` reads the Glide backbuffer as RGB565; alpha reads back as fully opaque,
  matching the native color-buffer format.

## Deliberate boundaries

- The 3D path is deliberately fixed-function and color-only: textured vertices, normals/lighting,
  fog, alpha test, custom and stock shader effects, instancing, lines/points, and clipping of a
  triangle that crosses a frustum plane are unsupported and report that explicitly. It is not a
  shader-emulation layer.
- Render targets, multisampling, texture cubes/volumes, stencil, occlusion queries, and custom
  SpriteBatch effects are unsupported by this native Glide scope and report that explicitly.
- Only additive blend equations are available in this scope. The standard source/destination blend
  factors supported by Glide are mapped; XNA constant blend-color factors are rejected.
- There is no fallback to SDL, Direct3D, OpenGL, or software rendering. A missing or incompatible
  `glide3x.dll` is a startup error by design.

For emulator installation and redistribution conditions, consult dgVoodoo2's own
[documentation](https://dgvoodoo2.dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/). CNA does not package
that runtime.
