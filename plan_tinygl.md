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

Target platforms: anything with a C99 compiler; OpenMP acceleration is optional. No platform gate is declared in
`cmake/RendererSelection.cmake`, matching `PORTABLEGL`.

Pinned upstream: commit `36a7987e7bebfda19615ea33341b1cc0ff9c3b13` (2023-11-04), fetched and built
from source by `cmake/ThirdPartyTinyGL.cmake`. TinyGL is zlib-style licensed with one clause plain
zlib does not have — an acknowledgment in the product **and its documentation** is *required* — so
`THIRD_PARTY_NOTICES.md` carries that acknowledgment and must not be dropped.

## Status

**The renderer implementation and post-implementation contract audit are delivered.** The local
Linux baseline has 14 CTest suites, 113 checks and 14/14 passing under
`-DCNA_GRAPHICS_RENDERER=TINYGL`; the public renderer identity count is **47**
(`scripts/check_renderer_identities.py`). TINYGL-19 cross-platform closure is still in progress:
Linux/GCC x86_64 and macOS/AppleClang arm64 pass all 14/14 suites in every run, while Windows/MSVC
x86_64 configures successfully and is still working through portability debt in the pinned
`sharp-runtime`, before CNA or the TinyGL tests are compiled. Each cycle advances through a
*different* error class, not a repeat of one: build step 37 → 55 → 91 → 154 of 596 so far.

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
- 3D: all four built-in fixed-function layouts: `VertexPositionColor` (stride 16),
  `VertexPositionTexture` (stride 20), `VertexPositionColorTexture` (stride 24), and
  `VertexPositionNormalTexture` (stride 32); every point, line and triangle topology;
  non-indexed (`glDrawArrays`) and 16/32-bit indexed
  (`glArrayElement` inside `glBegin`/`glEnd`) draws; and `vertexStart`, `startIndex`, `baseVertex`
  plus the stream's `VertexOffset`. A pixel-level analytical test distinguishes real reciprocal-W
  texture interpolation from an affine substitute.
- `BasicEffect`'s `VertexColorEnabled`, `DiffuseColor`, `Alpha` and `TextureEnabled`; packed vertex
  colour is multiplied by the forwarded diffuse/alpha material exactly once.
- `BasicEffect` per-vertex lighting on `VertexPositionNormalTexture`: all three directional lights,
  ambient, diffuse, emissive, inverse-transpose normal transforms and XNA-correct specular. The
  ambient/diffuse/emissive term uses TinyGL's `glLight*`/`glMaterial*` pipeline. Specular uses an
  exact second TinyGL raster pass into a scratch color plane, source-alpha masking for textured
  draws, and one saturated framebuffer add per pixel; this avoids both upstream's broken local
  viewer calculation and its lack of separate-specular color.
- 2D: a real textured, viewport-local `SpriteBatch` quad path with source/destination rectangles,
  tint, rotation, source-texel origin and both flip flags.
- World/View/Projection through TinyGL's own `GL_PROJECTION`/`GL_MODELVIEW` stacks (`glLoadMatrixf`).
- State: blend factors/equation (`glBlendFunc`/`glBlendEquation`), depth enable/write
  (`glEnable(GL_DEPTH_TEST)`/`glDepthMask`), cull mode (`glCullFace`/`glFrontFace`), fill mode
  (`glPolygonMode`), viewport (`glViewport`).
- The draw-time `VertexDeclaration` fidelity guard
  (`CNA::Internal::Graphics::RequireFaithfulVertexDeclaration`), so a declaration that changes a
  built-in Position/Colour/Normal/UV layout is refused rather than reinterpreted.
- Five shared golden-image scenes run unchanged against `examples/golden/`: framebuffer clear,
  opaque `BasicEffect`, rotated point-sampled `SpriteBatch`, depth-write control and `CullMode`.
  Together they add nine independent pixel/image checks against references produced by another
  renderer. Golden scenes that require a capability TinyGL explicitly refuses are not registered.

## Intentional TinyGL limitations

Each is refused deterministically with `System::NotSupportedException`, never silently no-opped,
and is covered by `TinyGL_Rejection`, `TinyGL_Contract`, or `TinyGL_Lighting`:

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
- **No shaders of any kind**, so no custom `Effect`, no `SkinnedEffect`, no PBR, and no per-pixel
  lighting. `BasicEffect.PreferPerPixelLighting=true` is refused.
- **No MSAA, no anisotropy, no mip levels.** Mip-chain texture creation, non-default mip sampler
  controls and a non-default `MultiSampleMask` are refused.
- **No instancing, no multi-stream vertex input.**
- **Specular plus non-opaque blending.** TinyGL has no separate-specular color. CNA's exact second
  pass is equivalent to XNA only when the first pass is opaque, so a contributing specular term
  combined with any other installed blend mode is refused.

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
| `TINYGL-1` | `cmake/ThirdPartyTinyGL.cmake`: pinned FetchContent + optional OpenMP acceleration | **DONE** |
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
| `TINYGL-21` | Make OpenMP optional: accelerated when found, complete single-threaded archive with no runtime dependency otherwise | **DONE** |
| `TINYGL-22` | `TinyGL_DrawRoutes` (6 checks): every topology, 32-bit indices and analytical perspective-correct texture mapping proof | **DONE** |
| `TINYGL-16` | `TinyGL_Lighting` (13 checks): fixed-function ambient/diffuse/emissive, three directional lights, inverse-transpose normals and an exact separate specular pass | **DONE** |
| `TINYGL-17` | Golden-image reuse against the shared `examples/golden/` corpus (5 suites, 9 checks) | **DONE** |
| `TINYGL-18` | Fixed-function layouts without packed color: `VertexPositionTexture` (stride 20) and `VertexPositionNormalTexture` (stride 32), including normal-array binding for TINYGL-16 | **DONE** |
| `TINYGL-19` | Native GCC/Linux x86_64, AppleClang/macOS arm64 and MSVC/Windows x86_64 verification | **IN PROGRESS** — Linux and macOS pass 14/14 in every run; Windows is at build step 91/596, still inside `sharp-runtime` |

## Continuation handoff (2026-08-15)

The renderer audit and feature work are complete. The remaining work is to finish the Windows leg
of TINYGL-19, then record the final three-platform evidence. Cross-platform CI has so far exposed
portability debt in the shared `sharp-runtime`, not a TinyGL rendering-contract failure.

### Repositories and pushed heads

- CNA: `/rv/data/development/github.com/openeggbert/cnanext`, branch `next`, pushed head
  `b7a9cb532` plus the commit carrying this plan update.
- SharpRuntime: `/rv/data/development/github.com/openeggbert/sharp-runtime`, branch `develop`, pushed
  head `062b9286a0ffc26a777f1a86d4dd727fde4c4abb`. The user explicitly authorized direct pushes to
  `sharp-runtime/develop` for this work.
- `.github/workflows/tinygl-cross-platform-ci.yml` pins that complete SharpRuntime SHA and TinyGL
  SHA `36a7987e7bebfda19615ea33341b1cc0ff9c3b13`; do not replace either pin with a moving branch.
- Both worktrees were clean at this handoff before this plan update.

### Completed TinyGL audit and capability work

- `d5fdb2354` — TINYGL-20: explicit effect identity, vertex-alpha cutout, depth-clear validation and
  transactional overflow-safe resize.
- `433b7b667` — TINYGL-21: optional OpenMP with a complete single-threaded fallback.
- `c21eb51e2` — TINYGL-22: all topology/draw routes, 32-bit indices and analytical
  perspective-correct texture mapping coverage.
- `f8497971d`, `75d7538a7`, `6c163c437` — fixed-function layouts, complete supported BasicEffect
  lighting and five shared golden-image suites.
- Local TinyGL result: 14/14 suites, 113 checks, no skips. Golden-image subset: 5/5 suites and nine
  checks. Renderer identity validator: 47 identities.

### Completed cross-platform infrastructure and SharpRuntime fixes

- CNA commits `2addbf912`, `37fccdec6`, `a51771dfd`, `d14aa68bd` and `a21d48f38` add the native
  GCC/AppleClang/MSVC matrix, propagate Ninja into vendored builds, locate the MSVC SDL package,
  watch SDL bootstrap changes and provision Windows zlib.
- CNA commits `951b42f3c`, `d4ae8398b`, `fec882983` and `2b8b56880` advance the workflow's immutable
  SharpRuntime pin after verified portability fixes.
- SharpRuntime commits `3c5d74cb`, `7cf5d1e5`, `ca82a055`, `52c0e103` and `74a8fe5a` fix macOS
  process-environment access, safe MSVC environment reads, portable floating-point bit casts,
  unreachable conversion returns and Windows environment API macro collisions.
- Every listed SharpRuntime change was built locally with at most two jobs and followed by
  `scripts/run_component_tests.sh build`: 16,341/16,341 checks passed across 37 executables before
  it was pushed to `develop`.

### Windows portability cycles completed on 2026-08-15

Each row is one CI cycle: a SharpRuntime fix verified locally at 16,341→16,344 checks across 37
executables, pushed to `develop`, then pinned by its full SHA in the workflow.

| SharpRuntime | What MSVC rejected | Fix |
|---|---|---|
| `3ad2dd90` | `TimeSpan.cpp` C4996, deprecated `std::sscanf` (step 37/596) | `sscanf_s` on MSVC only |
| `4df333e6` | `Calendar.hpp` C4244 (step 55/596), plus three more found locally | see below |
| `707a5b9b` | `IdnMapping.cpp` C2015, UTF-8 `char32_t` literals read in the host code page | MSVC compiles with `/utf-8` |
| `498a1304` | `FileSystemInfo.cpp` C2039/C3861, `file_clock::to_sys`/`from_sys` absent (step 91/596) | *superseded — see below* |
| `391563f1` | the above fix broke the green macOS job | preprocessor split on `_MSVC_STL_VERSION` |
| `062b9286` | `Socket.cpp`/`TcpClient.cpp` C2589/C4003, `windows.h` `min`/`max` macros (step 154/596) | `NOMINMAX`, plus two Winsock narrowings CI had not yet reached |

Matching CNA pin commits: `1faefcd4b`, `3d5303410`, `9ad2194d9`, `5e4457e27`, `4dd97692c`,
`b7a9cb532`. Linux/GCC x86_64 and macOS/AppleClang arm64 passed 14/14 in every one of these runs
except the `498a1304` cycle, which is the cautionary one: a `requires`-based detection of
`file_clock::to_sys` looked portable and broke the previously green macOS job. Microsoft's STL
*diagnoses* that requires-expression rather than evaluating it to false, because `file_clock` is
not a dependent type there, and Apple's libc++ does not declare `std::chrono::clock_cast` at all,
so naming it even in a discarded `if constexpr` branch fails at definition time. Only text the
preprocessor removes is safe across these three libraries.

**Two local censuses replace most CI round-trips. Use them before every push.** Neither needs a
build directory; both are `-fsyntax-only` over `modules/*/src/**.cpp` with
`-I` for each `modules/*/include` and `-isystem vendor`:

1. *Host census*, for the `/W4` diagnostics GCC does not emit:
   `clang++ -std=c++23 -fsyntax-only -Wshorten-64-to-32 -Wfloat-conversion -Wshadow -Wshadow-all`.
2. *Windows-branch census*, the more valuable one, because until 2026-08-15 no local compiler had
   ever built this repository's `_WIN32` code at all:
   `clang++ --target=x86_64-w64-mingw32 -std=c++23 -fsyntax-only -Wshorten-64-to-32
   -Wfloat-conversion -Wshadow -Wunused-parameter -Wunused-variable`. MinGW-w64 GCC 14 and its
   headers are installed on this machine, and clang targets them directly.

Census 2 reproduced exactly the four `TcpClient.cpp` lines MSVC named — validate a detector that
way before trusting its silence — and then found four more in `NetworkStream.cpp` and
`UdpClient.cpp` that CI had not reached. Both censuses are clean as of `062b9286`: zero errors
(other than `zlib.h`, which only CI's vcpkg provides) and zero warnings. What they cannot see:
MSVC-STL-only strictness like the `file_clock` case, `windows.h` macro collisions (MinGW's headers
do not define `min`/`max` the same way), and C4127.

**Watch item, not yet a failure.** The Windows-target census rejects `CNA::Internal::JsonValue`
(`modules/content/include/CNA/Internal/Json.hpp:37`): its `std::vector<std::pair<std::string,
JsonValue>>` member instantiates `~vector` while `JsonValue` is still incomplete. GCC and
clang/libc++ accept it, clang/libstdc++ does not, and MSVC is unknown — Windows has not compiled
any CNA translation unit yet. If MSVC rejects it, the fix is a user-declared destructor defined
out of line in a `.cpp`, so the implicit one is not instantiated at class-definition time. Do not
change it speculatively: the header is included widely and a rebuild is expensive.

The `4df333e6` cycle bundled four fixes because they were found *without* CI: compiling all 217
SharpRuntime translation units with `clang++ -fsyntax-only -Wshorten-64-to-32 -Wfloat-conversion
-Wshadow -Wshadow-all` approximates the `/W4` diagnostics GCC never emits. Reuse that technique
before pushing — it turns four ~7-minute CI cycles into one. The whole repository yielded exactly
one further narrowing (`String.cpp` `setw`) and one shadow (`XmlNode.cpp` C4456) beyond the
`Calendar.hpp` C4244, and the census would have caught that C4244 too. `Calendar::AddMilliseconds`
was a real defect, not compiler noise: a `double` amount above int range (2^31 ms ≈ 24.9 days) was
narrowed and applied as a wrapped offset. It now follows .NET's `Calendar.Add` and has tests.

Two scope facts worth keeping. The CI job compiles nearly every SharpRuntime module — `xml` and
`net-http-headers` included, confirmed from a successful Linux job's own log — so a fix there is
on the Windows path even though CNA does not name those components. And `-Wshadow-field` hits
(a derived member hiding a base member) are *not* an MSVC `/W4` class; only `-Wshadow` proper maps
to C4456.

The Node.js 20 action-deprecation annotation and the macOS Homebrew untrusted-tap message are
non-blocking runner warnings; both successful platform jobs prove they are unrelated to TINYGL-19.
Manual `workflow_dispatch` attempts returned HTTP 403 because that operation needs repository-admin
permission. A pushed change to the watched workflow/source paths starts the matrix normally.

### Work remaining

1. Watch the run triggered by the pin of SharpRuntime `062b9286`. If MSVC exposes the next
   portability error, repeat the cycle: focused fix, no weakened warnings, no `develop` pin.
   Run both censuses above first -- they are what turned three separate cycles into one.
2. In SharpRuntime, build with at most two jobs, run the relevant focused tests, then run the full
   build and `scripts/run_component_tests.sh build`. Require zero warnings and all 16,344+ checks.
   Commit only the relevant files and push the completed fix to `develop`.
3. Pin the new full 40-character SharpRuntime commit SHA in
   `.github/workflows/tinygl-cross-platform-ci.yml`, validate the YAML, commit it as TINYGL-19 and
   push CNA `next`. Watch the automatically triggered matrix.
4. Windows has not compiled a single CNA translation unit yet — the whole matrix so far has been
   SharpRuntime. Expect a second wave once step ~250/596 is passed. CNA targets do not set
   `/W4 /WX`, so only hard errors matter there; a libc++ syntax-only pass over the CI-built CNA
   modules (`core`, `math`, `platform`, `graphics`, `runtime`, `renderers/tinygl`) already found
   none, the only two hits being `Sdl2Window.cpp`, which this configuration does not build.
5. When all three jobs build and pass 14/14, update this Status and the TINYGL-19 task to **DONE**,
   and update `docs/tinygl-renderer.md` so it records native Linux x86_64, macOS arm64 and Windows
   x86_64 verification instead of a Linux-only limitation. Link the final green Actions run.
6. Step 6's local evidence is already collected against SharpRuntime `498a1304` and does not need
   repeating unless CNA sources change: 14/14 TinyGL suites pass; the no-OpenMP pass
   (`cmake -S . -B cmake-build-tinygl -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON`, rebuild, test,
   then reconfigure with `=OFF` to restore) passes 14/14 with zero `GOMP_*`/`omp_*` references in
   `libtinygl-static.a`; `python3 scripts/check_renderer_identities.py` reports 47 identities.

Keep the current capability boundary: "maximum" here means every XNA-facing operation that the
fixed-function TinyGL rasterizer can implement faithfully, plus deterministic rejection of
unsupported stencil, shader, render-target, general-alpha and related paths. Do not claim parity
with shader-capable GPU backends and do not turn documented refusals into silent approximations.

## Design decisions

**1. The draw path de-interleaves into float arrays, and that is not an emulation.**
TinyGL's vertex-array API only *looks* like OpenGL's. Its arrays are `GLfloat*`; `glColorPointer`
ignores its `type` argument entirely and always reads floats; and its stride counts **extra floats
between records**, not bytes (`arrays.c`: `i = idx * (size + stride)`). An interleaved XNA record —
three floats followed by a packed 4-byte colour — is not expressible in that API at all. The
renderer therefore converts the bound buffer into tightly packed float arrays and hands
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

OpenMP is optional. Every upstream pragma is guarded by `_OPENMP`, so without OpenMP the same source
builds a complete single-threaded archive with no `GOMP_*`/`omp_*` references. Upstream's own CMake
checks the incorrectly-cased `OPENMP_C_FOUND` name instead of CMake's `OpenMP_C_FOUND`, so CNA finds
the C component quietly and explicitly links `OpenMP::OpenMP_C` only when the imported target exists.

Upstream compiles with `-march=native` when not cross-compiling, so the archive is tuned for the
build host. That is upstream's own choice and CNA builds TinyGL per machine; it is stated rather
than overridden.

## Remaining phase

`TINYGL-19` is the only active phase. Linux x86_64 and macOS arm64 are verified; finish the
Windows x86_64 portability loop and final documentation exactly as recorded in the continuation
handoff above. Any new renderer feature after TINYGL-19 needs its own explicit owner instruction,
exactly like every other renderer's plan.
