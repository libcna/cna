# CNA TINYGL Renderer Plan

## Scope

`TINYGL` is CNA's fixed-function CPU OpenGL renderer, implemented on
[C-Chads/tinygl](https://github.com/C-Chads/tinygl) — an archived fork of Fabrice Bellard's
TinyGL. It MUST NOT depend on EasyGL, PortableGL, SDL_Renderer, SDL_GPU, bgfx or any other CNA
renderer, and it never opens a window, touches SDL's video subsystem, or talks to a GPU driver.

Its place among the existing identities is precise, and is what justifies it as a separate public
identity rather than an alias (`docs/renderer-registry.md`'s no-alias rule):

| | Pipeline model | Implementation |
|---|---|---|
| `OPENGL1` | fixed-function GL 1.x | a real GL **driver** |
| `PORTABLEGL` | shader-era GL 3.x | CPU (`rswinkle/PortableGL`) |
| `SOFTWARE` | CNA's own | CNA's own hand-rolled rasterizer |
| **`TINYGL`** | **fixed-function GL 1.x** | **CPU (C-Chads/tinygl)** |

No existing identity occupies the fixed-function + CPU + third-party-library cell.

Target platforms: anything with a C99 compiler and OpenMP. No platform gate is declared in
`cmake/RendererSelection.cmake`, matching `PORTABLEGL`.

Pinned upstream: commit `36a7987e7bebfda19615ea33341b1cc0ff9c3b13` (2023-11-04), fetched and built
from source by `cmake/ThirdPartyTinyGL.cmake`. TinyGL is zlib-style licensed with one clause plain
zlib does not have — an acknowledgment in the product **and its documentation** is *required* — so
`THIRD_PARTY_NOTICES.md` carries that acknowledgment and must not be dropped.

## Status

**Delivered and green after the post-implementation contract audit.** 6 CTest suites, 81 checks,
6/6 passing under
`-DCNA_GRAPHICS_RENDERER=TINYGL`. Public renderer identity count is **47**
(`scripts/check_renderer_identities.py`).

## Implemented

- Independent `CNA_GRAPHICS_RENDERER=TINYGL` selection and `CNA_RENDERER_TINYGL` compile
  definition; `modules/renderers/tinygl/` module registered in the physical source-partition
  validator.
- Real TinyGL context (`ZB_open(ZB_MODE_RGBA)` + `glInit`) with a CPU colour buffer, no window and
  no display server. `Present()` is a no-op, and no native window or native 2D renderer exists.
- `Clear` / `ClearColorAndDepth` / `ClearDepth` through `glClearColor`/`glClear`; TinyGL's
  executable far-depth value 1.0 is accepted and every other requested depth is refused.
- `ReadBackbuffer()` straight off `ZBuffer::pbuf` (see TINYGL-0 fact A).
- `SetVirtualResolution()` through `ZB_resize` — no context teardown. Because upstream rounds a
  `ZBuffer` width down to a multiple of four, CNA pads the private allocation up while preserving
  the exact requested logical width and clipping readback to it.
- Texture upload through `glGenTextures`/`glTexImage2D`, with an untouched CPU RGBA shadow for
  exact `GetData()`. Opaque RGB and alpha-cutout sampling use separate TinyGL texture objects, so
  the discard key is selected only by an alpha-preset blend state.
- **Real 3D**, not merely a rasterized quad: perspective projection (a quad at z=-6 covers 256 px
  against the same quad's 1521 px at z=0), depth-buffer occlusion that is independent of draw
  order, `DepthStencilState::DepthRead`'s test-without-write, modelview rotation that really
  foreshortens (1521 px -> 854 px at 60 degrees about Y), and perspective texture mapping. All
  measured by `TinyGL_3D`.
- 3D: the `VertexPositionColor` (stride 16) and `VertexPositionColorTexture` (stride 24) routes,
  non-indexed (`glDrawArrays`) and indexed (`glArrayElement` inside `glBegin`/`glEnd`), honouring
  `vertexStart`, `startIndex`, `baseVertex` and the stream's `VertexOffset`.
- `BasicEffect`'s `VertexColorEnabled`, `DiffuseColor`, `Alpha` and `TextureEnabled`; packed vertex
  colour is multiplied by the forwarded diffuse/alpha material exactly once.
- 2D: a real textured, viewport-local `SpriteBatch` quad path with source/destination rectangles,
  tint, rotation, source-texel origin and both flip flags.
- World/View/Projection through TinyGL's own `GL_PROJECTION`/`GL_MODELVIEW` stacks (`glLoadMatrixf`).
- State: blend factors/equation (`glBlendFunc`/`glBlendEquation`), depth enable/write
  (`glEnable(GL_DEPTH_TEST)`/`glDepthMask`), cull mode (`glCullFace`/`glFrontFace`), fill mode
  (`glPolygonMode`), viewport (`glViewport`).
- The draw-time `VertexDeclaration` fidelity guard
  (`CNA::Internal::Graphics::RequireFaithfulVertexDeclaration`), so a declaration that puts
  something other than Position+Colour(+UV) in the same stride is refused rather than reinterpreted.

## Intentional TinyGL limitations

Each is refused deterministically with `System::NotSupportedException`, never silently no-opped,
and is covered by `TinyGL_Rejection` or the post-audit `TinyGL_Contract` suite:

- **No stencil.** TinyGL's `ZBuffer` has a depth plane and a colour plane and nothing else.
  Enabling the stencil test and setting a non-zero `ReferenceStencil` are refused.
- **No selectable depth comparison.** There is no `glDepthFunc`; only `CompareFunction::LessEqual`
  is accepted (design decision 2).
- **No general blending.** Only the factor/equation set TinyGL's rasterizer really switches on is
  installed; everything else is refused (design decision 3).
- **No colour mask.** There is no `glColorMask`; a partial `ColorWriteChannels` is refused.
- **No scissor.** There is no `glScissor`; `ScissorTestEnable` is refused.
- **No depth bias.** `glPolygonOffset` stores its arguments and the rasterizer never reads them.
- **No depth range.** There is no `glDepthRange`; a `Viewport` outside 0..1 is refused.
- **No render targets, cube maps or 3D textures.** TinyGL owns exactly one framebuffer per context
  and has no framebuffer-object, cube or volume texture concept.
- **No shaders of any kind**, so no custom `Effect`, no `SkinnedEffect`, no PBR, no per-pixel
  lighting.
- **No MSAA, no anisotropy, no mip levels.** Mip-chain texture creation, non-default mip sampler
  controls and a non-default `MultiSampleMask` are refused.
- **No instancing, no multi-stream vertex input.**
- **Lighting is not wired up.** TinyGL has a real `glLight*` fixed-function pipeline, but CNA does
  not translate XNA's lighting parameters onto it yet, so `lightingEnabled` is refused rather than
  approximated. This is the most obvious future extension (see "Possible future phases").

## Recorded approximations

Three behaviours are accepted rather than refused, because refusing them would refuse XNA's own
defaults and leave a renderer that can only throw. Each is documented, tested, and reported as
`false` by the relevant `SupportsCapability()` query.

1. **Transparency is 1-bit, not alpha blending.** `BlendState::AlphaBlend` and
   `BlendState::NonPremultiplied` are matched on their complete factor+function signature and
   executed as TinyGL's own `TGL_NO_DRAW_COLOR` colour-key cutout: texels below
   `TinyGLTextureRenderer::kAlphaCutoutThreshold` (128) are uploaded into a separate cutout texture
   as the key colour and TinyGL's triangle rasterizer discards them per fragment. The effective
   threshold includes uniform `BasicEffect.Alpha`, constant vertex alpha, or SpriteBatch tint alpha.
   `BlendState::Opaque` selects the ordinary RGB object and never applies this alpha discard. A
   complete draw below the threshold is skipped; varying untextured alpha that crosses the threshold
   and varying textured alpha are refused because TinyGL cannot express them faithfully. Alpha is
   thresholded, never silently ignored.
2. **Sampler state is inert.** `glTexParameteri` is an upstream no-op and the texel fetch masks the
   fixed-point S/T against the texture dimension, so sampling is always nearest with wrap
   addressing whatever `SamplerState` asks for. `TextureFilter::Anisotropic` is still refused,
   because `SupportsCapability(AnisotropicFiltering)` reports false.
3. **Textures are resampled to 256×256.** `glTexImage2D` rescales every upload to
   `TGL_FEATURE_TEXTURE_DIM` with nearest-neighbour. `Texture2D.Width`/`Height` keep reporting the
   requested size, because that is what the XNA contract requires; the resampling is a
   sampling-fidelity loss, not an API-surface one.

`GraphicsDevice.BlendFactor` and `GraphicsDevice.ScissorRectangle` are recorded but inert: no
installable state can consult either, because the factors and the scissor enable that would use
them are refused one step earlier.

## Tasks

| ID | Task | Status |
|---|---|---|
| `TINYGL-0` | Existence-gate spike: prove TinyGL can clear, rasterize, texture and read back with no GPU/window | **DONE** — `tinygl-spike/`, all checks pass |
| `TINYGL-1` | `cmake/ThirdPartyTinyGL.cmake`: pinned FetchContent + OpenMP link | **DONE** |
| `TINYGL-2` | Registry: enum, selector, compile definition, name, identity-check table (46 → 47) | **DONE** |
| `TINYGL-3` | Module skeleton `modules/renderers/tinygl/` + source-partition declaration | **DONE** |
| `TINYGL-4` | Context lifecycle, clear, resize, readback | **DONE** |
| `TINYGL-5` | Texture handle: RGB conversion, colour-key cutout, CPU shadow, `GetData` | **DONE** |
| `TINYGL-6` | Vertex/index handles + the de-interleaving draw path (design decision 1) | **DONE** |
| `TINYGL-7` | Non-indexed and indexed 3D routes, both vertex layouts | **DONE** |
| `TINYGL-8` | SpriteBatch quad path | **DONE** |
| `TINYGL-9` | Blend / depth / rasterizer / viewport state translation and refusals | **DONE** |
| `TINYGL-10` | `SupportsCapability()` truth table | **DONE** |
| `TINYGL-11` | `TinyGL_Smoke` (10 checks) | **DONE** |
| `TINYGL-12` | `TinyGL_TextureSprite` (7 checks) | **DONE** |
| `TINYGL-13` | `TinyGL_State` (9 checks) | **DONE** |
| `TINYGL-14` | `TinyGL_Rejection` (17 checks) | **DONE** |
| `TINYGL-14b` | `TinyGL_3D` (8 checks): earn `SupportsCapability(ThreeD)` with perspective, depth occlusion and modelview proofs | **DONE** |
| `TINYGL-14c` | `TinyGL_Contract` (30 checks): post-implementation audit regressions for framebuffer alignment, effect identity/modulation, offsets, SpriteBatch geometry/viewport/alpha, mip/MSAA refusals, capability hooks and validation ordering | **DONE** |
| `TINYGL-15` | `docs/tinygl-renderer.md` capability boundary | **DONE** |
| `TINYGL-20` | Post-audit contract remediation: explicit effect identity, vertex-alpha cutout, depth-clear validation and transactional overflow-safe resize | **DONE** |
| `TINYGL-16` | Fixed-function lighting via `glLight*` | **OPEN** — needs its own owner instruction |
| `TINYGL-17` | Golden-image reuse against the shared `examples/golden/` corpus | **OPEN** |
| `TINYGL-18` | `VertexPositionNormalTexture` (stride 32) route, prerequisite for TINYGL-16 | **OPEN** |
| `TINYGL-19` | Windows/macOS build verification (only Linux x86_64 has been run) | **OPEN** |

## Design decisions

**1. The draw path de-interleaves into float arrays, and that is not an emulation.**
TinyGL's vertex-array API only *looks* like OpenGL's. Its arrays are `GLfloat*`; `glColorPointer`
ignores its `type` argument entirely and always reads floats; and its stride counts **extra floats
between records**, not bytes (`arrays.c`: `i = idx * (size + stride)`). An interleaved XNA record —
three floats followed by a packed 4-byte colour — is not expressible in that API at all. The
renderer therefore converts the bound buffer into three tightly packed float arrays and hands
TinyGL exactly the shape its API defines. Every transform, clip, cull, raster, texel fetch and
blend after that point is still TinyGL's. This was found by running code, not by reading headers:
the first implementation pointed TinyGL at the raw records with byte strides, compiled cleanly, and
rendered nothing.

TinyGL's own buffer objects (`glGenBuffers`/`glBufferData`) are deliberately not used: the only way
to read one as an attribute array is `glBindBufferAsArray(target, buffer, type, size, stride)`,
which carries no attribute offset and so cannot describe an interleaved layout either.

**2. The accepted depth comparison is `LessEqual`, not `Less`.**
TinyGL has no `glDepthFunc`. Its rasterizer's single comparison is
`ZCMPSIMP(z, zpix) == (z >= zpix)` over a z-buffer in which a **larger** stored value is nearer —
which is `LessEqual` in XNA's own depth convention, and is also exactly XNA's
`DepthStencilState::Default`. The first implementation accepted `Less` on the strength of the
upstream `LIMITATIONS` file's claim that the comparison is "hardcoded as GL_LESS"; that file is
marked unmaintained, and the running code disagrees with it. Reading the rasterizer settled it.

**3. Blending has exactly three outcomes.**
`ApplyBlendState()` either installs a state exactly, maps one of the two XNA alpha presets onto the
colour-key cutout, or refuses. There is no fourth "close enough" branch. In particular
`BlendState::Additive` is **refused**, because its `SourceAlpha` source factor has no case in
TinyGL's factor switch and would silently degrade to `GL_ONE`; `SupportsCapability(AdditiveBlending)`
reports false to match. A custom `(One, One) + Add` state, which *is* expressible, is accepted and
really adds — `TinyGL_State` checks the resulting pixels.

**4. Stencil clears are accepted; stencil behaviour is refused.**
Clearing an absent stencil plane is a legal no-op in real OpenGL, so `ClearStencil()` and friends
clear the planes that exist and do nothing for the one that does not. Refusing them would refuse
`GraphicsDevice.Clear(Color)`, which asks for all three planes. What is refused is every request
that would be a false promise about stencil *behaviour*.

**5. Validation runs before the native call, always.**
TinyGL answers an argument combination it cannot handle by calling `gl_fatal_error()`, which prints
a message and **terminates the process** — there is no error flag and no recoverable path
(`TINYGL-0` fact B′, observable via `tinygl-spike/tinygl_existence_gate --prove-rgba-fatal`). Every
validation in this renderer therefore runs before the corresponding TinyGL call. This is a
correctness requirement, not a courtesy, and `TinyGL_Rejection` is what keeps it in place.

**6. One renderer instance per process.**
`glInit`/`glClose` install and tear down a file-scope global inside the library and there is no
make-current entry point, so a second `TinyGLRenderer` would silently corrupt the first one's
context. The constructor throws instead.

**7. XNA's clockwise faces are TinyGL's front faces.**
`glFrontFace(GL_CW)` is stated explicitly in the constructor rather than inherited, so
`CullMode::CullClockwiseFace` maps to `glCullFace(GL_FRONT)` and `CullCounterClockwiseFace` to
`GL_BACK`. `TinyGL_State` proves the cull really happens rather than trusting the mapping.

## Boundary facts established by TINYGL-0

Recorded here because they are the reasons the contract above looks the way it does. Full detail
and the reproduction command are in `tinygl-spike/README.md`.

- **A.** `glReadPixels` is an upstream stub — it validates its arguments and returns without
  writing. Readback goes through `ZBuffer::pbuf`.
- **B.** `glTexImage2D` accepts `GL_RGB`/`GL_UNSIGNED_BYTE`, level 0, no border, and nothing else.
  TinyGL has no texture alpha.
- **B′.** An unsupported argument terminates the process (design decision 5).
- **C.** Blending implements `GL_ONE`, `GL_ZERO`, `GL_ONE_MINUS_SRC_COLOR` (source slot only) and
  `GL_ONE_MINUS_DST_COLOR` (destination slot only), with `ADD`/`SUBTRACT`/`REVERSE_SUBTRACT`.
  Anything else falls through to the switch's `default:` and behaves as `GL_ONE`.
- **D.** No stencil plane, no scissor, no colour mask, no selectable depth function.

## Precision note

TinyGL interpolates colours in fixed point, so a channel can land one LSB below the requested value
(the spike measured 254 for a requested 255, and 63/127/191 for a requested 64/128/191). The test
suites use a tolerance of 2–3 rather than demanding byte equality. This is a documented property of
the rasterizer, not slack absorbed to make a test pass.

## Build

```bash
cmake -S . -B cmake-build-tinygl -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=TINYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-tinygl -j4
(cd cmake-build-tinygl && ctest -R TinyGL --output-on-failure)
```

TinyGL is fetched at configure time. For an offline build, point CMake at an existing checkout with
`-DFETCHCONTENT_SOURCE_DIR_TINYGL=/path/to/tinygl`.

OpenMP is required: upstream compiles `glopCopyTexImage2D` and `glopDrawPixels` with OpenMP pragmas,
so the archive carries unresolved `GOMP_*` references even though this renderer calls neither.
Upstream's own CMake checks the incorrectly-cased `OPENMP_C_FOUND` name instead of CMake's
`OpenMP_C_FOUND`. `cmake/ThirdPartyTinyGL.cmake` therefore enables C, requires the OpenMP C
component before adding the upstream directory, and explicitly links `OpenMP::OpenMP_C`.

Upstream compiles with `-march=native` when not cross-compiling, so the archive is tuned for the
build host. That is upstream's own choice and CNA builds TinyGL per machine; it is stated rather
than overridden.

## Possible future phases

Each needs its own explicit owner instruction, exactly like every other renderer's plan.

1. `TINYGL-16`/`TINYGL-18` — fixed-function lighting. TinyGL has a complete `glLight*`/`glMaterial*`
   pipeline that CNA does not currently drive. Doing it properly needs the stride-32
   `VertexPositionNormalTexture` route first, and then a careful translation of XNA's
   `BasicEffect` lighting parameters onto TinyGL's material model — `OPENGL1`'s own phase 4 is the
   obvious reference.
2. `TINYGL-17` — golden-image reuse. `OPENGL1` reuses `examples/golden/`'s checked-in PNGs through
   the shared `PixelTestGame::CompareGoldenImage()` helper; the flat, edge-free scenes in that
   corpus are the ones a second fixed-function rasterizer can match. The 256×256 texture resample
   will exclude any scene whose sampled region depends on texel-exact minification.
3. `TINYGL-19` — Windows and macOS verification. Nothing in the renderer is Linux-specific, but
   only Linux x86_64 has actually been built and run.
