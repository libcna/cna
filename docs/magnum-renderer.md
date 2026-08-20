# MAGNUM graphics renderer

CNA's `MAGNUM` renderer renders through [Magnum](https://github.com/mosra/magnum)'s typed OpenGL
wrappers (`Magnum::GL`) on a desktop OpenGL 3.3 core context. CNA's active platform owns the window
and creates the context through `IPlatformGlContext`; Magnum is handed that already-current context
through `Platform::GLContext`. From then on every resource, state change and draw goes through
`Magnum::GL` rather than through raw GL or a window-toolkit API.

Select it with:

```bash
cmake -S . -B cmake-build-magnum -DCNA_GRAPHICS_RENDERER=MAGNUM
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
the exact upstream master revisions this renderer was developed and verified against, not a moving
branch.

Only the pieces the renderer uses are built (`MAGNUM_WITH_GL` plus one platform context library);
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

- **Context and presentation** — platform-created GL 3.3 core context with a stencil buffer,
  back-buffer MSAA requested from the context itself (no manual resolve), swap interval, virtual
  resolution and the full `CnaPresentationMode` set, including high-DPI window/logical coordinate
  transforms based on the platform surface snapshot.
- **Clears** — colour, depth, stencil and every combination, with XNA's rule that a clear ignores
  `BlendState`'s colour write masks and `DepthStencilState`'s write enables and then restores them.
- **Textures** — `Texture2D` (full mip-chain storage, sub-level upload, readback), `TextureCube`,
  `Texture3D`, all RGBA8.
- **Render targets** — `RenderTarget2D` and `RenderTargetCube`, with optional depth/stencil,
  optional mip chains regenerated on unbind, MSAA colour storage resolved on unbind, and readback
  normalized to top-row-first. Up to four simultaneous targets (MRT), each keeping its own
  multisample storage while it is part of a set rather than contributing an already-resolved image.
- **SpriteBatch** — batched quads with rotation, origin, flip, tint and transform; per-batch sampler
  filter/addressing; a custom `ShaderEffect` binds its own program rather than a recompiled copy.
- **Effects** — `ShaderEffect` GLSL compiled and linked at runtime, with uniform assignment by name
  and texture/cube/volume binding. Each stage is compiled at the GLSL version its own source
  declares, so an effect reaching for a later feature than 3.30 gets it. Compile and link
  diagnostics are captured from Magnum's own output streams and returned through
  `IEffectRenderer::GetCompileError()`.
- **Stock 3D shaders** — generated programs selected from the draw's combined stride plus its stock
  effect flags. The four built-in layouts (strides 16/20/24/32) cover diffuse colour, vertex colour,
  texturing, three directional lights with Blinn-Phong specular, ambient and emissive terms, alpha
  test and view-space fog; `DualTextureEffect` adds its own two-sampler programs for strides 20 and
  24, carrying FNA's `color.rgb *= 2` overbright factor on the base layer, and
  `EnvironmentMapEffect` adds a cube-map program whose reflection lerps over the lit base (flat or
  Fresnel-weighted) with its specular tint added on top, `SkinnedEffect` adds a 72-bone palette
  program serving strides 52 and 56, and `PbrEffect`/`SkinnedPbrEffect` add a glTF
  metallic-roughness BRDF (GGX distribution, Smith-Schlick geometry, Schlick Fresnel) with normal,
  metallic-roughness, emissive and occlusion maps over strides 48 and 68. `BasicEffect` and
  `SkinnedEffect` each generate both of the families `PreferPerPixelLighting` selects between,
  sharing one lighting function so the flag changes only which stage evaluates it -- XNA's own
  `false` default lands in the per-vertex one.
- **Draws** — non-indexed, indexed and instanced, with per-stream binding offsets and instance
  frequencies honoured individually. Multi-stream vertex input is supported: a declaration split
  across several buffers is re-slotted per element, and a per-instance world matrix may itself be
  split across several bindings.
- **Pipeline state** — blend factors/equations/colour write masks, depth test and function, full
  two-sided stencil, cull mode, real wireframe fill (desktop GL has `glPolygonMode`), polygon
  offset, scissor, viewport and depth range.
- **Occlusion queries** — real `GL_SAMPLES_PASSED` queries.
- **Vertex array caching** — a `GL::Mesh` is built once per binding configuration and reused, with
  primitive, element count, instance count, base vertex and index offset applied per draw. The cache
  lives on the vertex buffer, so a destroyed buffer takes its own arrays with it, and its key holds
  a monotonic buffer identity rather than an address that a later buffer could reuse.

## Current limitations

- **`SurfaceFormat`** — only `Color` (RGBA8) storage is allocated. Other formats reach the renderer
  and are recorded, but every texture, cube, volume and render target is created as RGBA8.
- **`BlendState.MultiSampleMask`** — reaches the renderer but only its all-ones default is applied.
  Magnum's `GL::Renderer` wraps no sample-mask state (`GL_SAMPLE_MASK`/`glSampleMaski` have no
  binding), so honouring a non-default coverage mask would mean going around the wrapper layer this
  renderer exists to use.
- **Context loss** — `SetContextRecoveryEnabled`/`DebugSimulateContextLoss` keep
  `IGraphicsRenderer`'s defaults; desktop GL contexts are not lost the way an ES/WebGL one can be.

`plans/plan_magnum.md` tracks these as explicit tasks rather than leaving them implicit.

## Post-audit contract

- **Declaration fidelity (REMED-GFX-DECL-GUARD).** The stock route resolves its attribute layout
  from the combined byte stride alone, so `RequireFaithfulDeclarationEXT()` runs at draw time,
  before anything native is touched: a custom declaration packing different semantics into one of
  the known stock widths is refused with `System::NotSupportedException` instead of being rendered
  from the wrong bytes, and the device stays usable. The check is asymmetric (only what the caller
  declared is verified), compares split multi-stream declarations in the combined record (each
  stream's elements lifted by its `combinedByteBase`), and deliberately does not guard the
  custom-effect route, which binds each element from the declaration itself.
- **No silent drops.** A stock draw whose layout selects no generated program — an unlisted
  stride, or a flag combination with no variant — throws rather than silently rendering nothing.
- **Capability reporting.** `SupportsCapability` is an exhaustive switch over every
  `GraphicsCapability` member with no default arm, so a future member is a compiler diagnostic
  rather than a confident wrong answer. `WireFrame=true` is proven by the shared wireframe pixel
  oracle, which MAGNUM joins as a measured renderer.
- **Corpus composition.** `tests/CNA/Internal/Renderers/Magnum/` is excluded from every other
  renderer's `CnaTests` glob; under MAGNUM the corpus keeps it.

## Tests

- `tests/CNA/Internal/Renderers/Magnum/MagnumRendererTests.cpp` — GTest coverage for
  everything that needs no live GL context: the XNA-ordinal → Magnum-enum mappings, the vertex
  layout resolution both the stock and declaration-driven routes share, and the generated stock
  GLSL. Runs as part of `CnaTests`.
- `modules/renderers/magnum/examples/magnum_smoke_test.cpp` (`ctest -R Magnum_Smoke`) — the integration smoke test: clear +
  back-buffer readback, a SpriteBatch textured quad, a 3D coloured primitive draw and a render
  target round trip, each with a pixel assertion.
- `modules/renderers/magnum/examples/magnum_dualtextureeffect_test.cpp` (`ctest -R Magnum_DualTextureEffect`) — the
  overbright factor and the second layer's participation, both measured against a mid-tone texel so
  a saturated scene cannot hide either.
- `modules/renderers/magnum/examples/magnum_environmentmapeffect_test.cpp` (`ctest -R Magnum_EnvironmentMapEffect`) — the
  reflection at amount 0 and 1 plus the specular tint. The amount-1 case is what separates a lerp
  from an addition, which a saturated cube map cannot.
- `modules/renderers/magnum/examples/magnum_skinnedeffect_test.cpp` (`ctest -R Magnum_SkinnedEffect`) — an identity bone, a
  translation bone and a two-bone blend, each measured by where the geometry lands rather than by
  its colour.
- `modules/renderers/magnum/examples/magnum_pbreffect_test.cpp` (`ctest -R Magnum_PbrEffect`) — the metallic-roughness BRDF
  on a rig where N, V, L and H coincide, so every expected byte is derived from the formula rather
  than captured from a run.
- `modules/renderers/magnum/examples/magnum_meshcache_test.cpp` (`ctest -R Magnum_MeshCache`) — four draws over one binding,
  each selecting a different range by index offset or base vertex, so a cached vertex array that
  kept either term is caught.
- `modules/renderers/magnum/examples/magnum_pervertexlighting_test.cpp` (`ctest -R Magnum_PerVertexLighting`) — a specular
  highlight aimed at a quad's centre, which is the only place the two lighting families disagree:
  per pixel the centre is lit, per vertex it interpolates four already-dark corners. The corners
  themselves must still agree, which is what keeps the shared formula honest.
- `modules/renderers/magnum/examples/magnum_mrt_msaa_test.cpp` (`ctest -R Magnum_MrtMsaa`) — a `#version 400 core`
  `ShaderEffect` writing `gl_SampleMask[0] = 1` into two slots at once. One sample per pixel is the
  only thing that separates a genuinely multisampled attachment from a resolved image, and the same
  draw into a single-sampled set is the control that makes the reading a measurement.

Both suites were run against Mesa's `llvmpipe` software rasterizer under `Xvfb`, so they need no GPU
— point `CNA_TEST_DISPLAY` at the X server the ctest registration should use.
