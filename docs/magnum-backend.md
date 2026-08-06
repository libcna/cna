# MAGNUM graphics backend

CNA's `MAGNUM` backend renders through [Magnum](https://github.com/mosra/magnum)'s typed OpenGL
wrappers (`Magnum::GL`) on a desktop OpenGL 3.3 core context. SDL3 stays the window and GL-context
owner, exactly as in every other windowed CNA backend; Magnum is handed that already-current context
through `Platform::GLContext` and from then on every resource, state change and draw goes through
`Magnum::GL` rather than through raw GL calls.

Select it with:

```bash
cmake -S . -B cmake-build-magnum -DCNA_GRAPHICS_BACKEND=MAGNUM
cmake --build cmake-build-magnum -j4
```

## Acquiring Magnum

Magnum is a real multi-repository C++ library (`mosra/corrade` + `mosra/magnum`), not a vendorable
single header, so it is acquired the way `BGFX` already is — a pinned upstream revision, fetched on
demand and built inside the selected `cmake-build-<variant>/` directory.
`cmake/ThirdPartyMagnum.cmake` tries three routes in order:

1. **`-DCNA_MAGNUM_ROOT=<prefix>`** — an existing Corrade+Magnum install prefix. Preferred for
   offline and reproducible builds; a mismatch here is a hard error rather than a silent download.
2. **A system-wide install** already on `CMAKE_PREFIX_PATH`.
3. **`FetchContent`** of the pinned revisions in `CNA_CORRADE_GIT_TAG` / `CNA_MAGNUM_GIT_TAG`.

Magnum's last tagged release (`v2020.06`) predates the toolchains CNA builds with, so the pins are
the exact upstream master revisions this backend was developed and verified against, not a moving
branch.

Only the pieces the backend uses are built (`MAGNUM_WITH_GL` plus one platform context library);
Magnum's `Shaders`, `Trade`, `Primitives`, `MeshTools`, `SceneGraph`, `Text` and `DebugTools`
libraries are all switched off.

### The platform context library

`Platform::GLContext` needs an OpenGL function loader, and Magnum supplies it in exactly one of four
platform-specific libraries — there is no generic "load through a caller-supplied `getProcAddress`"
entry point. The choice is therefore made at configure time, per platform:

| Platform     | Magnum component | Selected by                                       |
|--------------|------------------|---------------------------------------------------|
| Windows      | `WglContext`     | automatic                                         |
| macOS        | `CglContext`     | automatic                                         |
| Linux/BSD    | `GlxContext`     | default                                           |
| Linux/BSD    | `EglContext`     | `-DCNA_MAGNUM_USE_EGL=ON`                         |

`CNA_MAGNUM_USE_EGL=ON` is the right choice when SDL is driving a Wayland or EGL-only session.

### System packages (Debian/Ubuntu)

```bash
sudo apt-get install -y libgl1-mesa-dev libglx-dev libx11-dev   # GLX (default)
sudo apt-get install -y libegl1-mesa-dev                        # additionally, for CNA_MAGNUM_USE_EGL=ON
```

## What is implemented

- **Context and presentation** — SDL3 GL 3.3 core context with a stencil buffer, back-buffer MSAA
  requested from the context itself (no manual resolve), swap interval, virtual resolution and the
  full `CnaPresentationMode` set, window/logical coordinate transforms.
- **Clears** — colour, depth, stencil and every combination, with XNA's rule that a clear ignores
  `BlendState`'s colour write masks and `DepthStencilState`'s write enables and then restores them.
- **Textures** — `Texture2D` (full mip-chain storage, sub-level upload, readback), `TextureCube`,
  `Texture3D`, all RGBA8.
- **Render targets** — `RenderTarget2D` and `RenderTargetCube`, with optional depth/stencil,
  optional mip chains regenerated on unbind, MSAA colour storage resolved on unbind, and readback
  normalized to top-row-first. Up to four simultaneous targets (MRT).
- **SpriteBatch** — batched quads with rotation, origin, flip, tint and transform; per-batch sampler
  filter/addressing; a custom `ShaderEffect` binds its own program rather than a recompiled copy.
- **Effects** — `ShaderEffect` GLSL compiled and linked at runtime, with uniform assignment by name
  and texture/cube/volume binding. Compile and link diagnostics are captured from Magnum's own
  output streams and returned through `IEffectBackend::GetCompileError()`.
- **Stock 3D shaders** — generated programs selected from the draw's combined stride plus its stock
  effect flags. The four built-in layouts (strides 16/20/24/32) cover diffuse colour, vertex colour,
  texturing, three directional lights with Blinn-Phong specular, ambient and emissive terms, alpha
  test and view-space fog; `DualTextureEffect` adds its own two-sampler programs for strides 20 and
  24, carrying FNA's `color.rgb *= 2` overbright factor on the base layer, and
  `EnvironmentMapEffect` adds a cube-map program whose reflection lerps over the lit base (flat or
  Fresnel-weighted) with its specular tint added on top.
- **Draws** — non-indexed, indexed and instanced, with per-stream binding offsets and instance
  frequencies honoured individually. Multi-stream vertex input is supported: a declaration split
  across several buffers is re-slotted per element, and a per-instance world matrix may itself be
  split across several bindings.
- **Pipeline state** — blend factors/equations/colour write masks, depth test and function, full
  two-sided stencil, cull mode, real wireframe fill (desktop GL has `glPolygonMode`), polygon
  offset, scissor, viewport and depth range.
- **Occlusion queries** — real `GL_SAMPLES_PASSED` queries.

## Current limitations

- **`SurfaceFormat`** — only `Color` (RGBA8) storage is allocated. Other formats reach the backend
  and are recorded, but every texture, cube, volume and render target is created as RGBA8.
- **`BlendState.MultiSampleMask`** — reaches the backend but only its all-ones default is applied; a
  real coverage mask would need `GL_SAMPLE_MASK` plus `glSampleMaski`.
- **Stock effect coverage** — `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect` and
  `EnvironmentMapEffect` are covered. `SkinnedEffect` and `PbrEffect`/`SkinnedPbrEffect` are not:
  neither their shader variants nor their vertex layouts (strides 52/48/68) exist here yet, so such
  a draw is refused rather than rendered without its terms.
- **Multi-target MSAA** — an ordered multi-target set attaches the targets' resolved colour
  textures, so a multisampled target contributes its single-sample image while it is part of a set.
- **Mesh construction** — a `GL::Mesh` (and therefore a vertex array object) is built per draw
  rather than cached per buffer/layout pair. Correct, but a known performance boundary.
- **Context loss** — `SetContextRecoveryEnabled`/`DebugSimulateContextLoss` keep
  `IGraphicsBackend`'s defaults; desktop GL contexts are not lost the way an ES/WebGL one can be.

`plan_magnum.md` tracks these as explicit tasks rather than leaving them implicit.

## Tests

- `tests/CNA/Internal/Backends/Magnum/MagnumGraphicsBackendTests.cpp` — GTest coverage for
  everything that needs no live GL context: the XNA-ordinal → Magnum-enum mappings, the vertex
  layout resolution both the stock and declaration-driven routes share, and the generated stock
  GLSL. Runs as part of `CnaTests`.
- `examples/magnum_smoke_test.cpp` (`ctest -R Magnum_Smoke`) — the integration smoke test: clear +
  back-buffer readback, a SpriteBatch textured quad, a 3D coloured primitive draw and a render
  target round trip, each with a pixel assertion.
- `examples/magnum_dualtextureeffect_test.cpp` (`ctest -R Magnum_DualTextureEffect`) — the
  overbright factor and the second layer's participation, both measured against a mid-tone texel so
  a saturated scene cannot hide either.
- `examples/magnum_environmentmapeffect_test.cpp` (`ctest -R Magnum_EnvironmentMapEffect`) — the
  reflection at amount 0 and 1 plus the specular tint. The amount-1 case is what separates a lerp
  from an addition, which a saturated cube map cannot.

Both suites were run against Mesa's `llvmpipe` software rasterizer under `Xvfb`, so they need no GPU
— point `CNA_TEST_DISPLAY` at the X server the ctest registration should use.
