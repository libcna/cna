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
(`scripts/check_renderer_identities.py`). TINYGL-19 cross-platform closure is nearly complete:
Linux/GCC x86_64 and macOS/AppleClang arm64 pass all 14/14 suites in every run, and Windows/MSVC
x86_64 now **configures, compiles and links completely** — all 596 build steps — after eleven fix
cycles that each cleared a *different* error class rather than repeating one (build step
37 → 55 → 91 → 154 → 168 → 257 → 329 → 571 → 596). The one remaining Windows problem is that the
suites do not yet run there: they timed out with no output because the SDL DLLs were not beside
the executables. That fix is pushed and awaiting its run. Not one blocker so far has been a TinyGL
rendering-contract failure.

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
| `TINYGL-19` | Native GCC/Linux x86_64, AppleClang/macOS arm64 and MSVC/Windows x86_64 verification | **IN PROGRESS** — Linux and macOS pass 14/14 in every run; Windows configures, compiles and links all 596 steps, and the suites' first run there is pending |

## Continuation handoff (2026-08-15, written for a fresh context)

The renderer, its audit and its capability work are **complete and unchanged by any of the work
below**. Everything since is TINYGL-19: proving the same 14 suites build and pass on native
Linux/GCC x86_64, macOS/AppleClang arm64 and Windows/MSVC x86_64. Nothing CI has exposed so far
was a TinyGL rendering-contract failure — every single blocker was portability debt in the shared
`sharp-runtime`, in upstream TinyGL's own build, or in CNA's build wiring.

### Where this stands right now

Linux and macOS have passed 14/14 in every run all day. Windows now **configures, compiles and
links completely** — all 596 build steps — after eleven fix cycles. The last remaining problem is
that the tests do not yet *run* there.

| Windows stage | State |
|---|---|
| Configure | passes |
| Compile `sharp-runtime` (~250 steps) | passes |
| Compile upstream TinyGL C sources | passes |
| Compile CNA (~300 steps) | passes |
| Link all 15 test executables | passes |
| **Run the 14 suites** | **all timed out at 30 s with no output; fix pushed, unverified** |

The final failing evidence is run
[31892934349](https://github.com/openeggbert/cna/actions/runs/31892934349): every suite
`***Timeout 30.00 sec`, not one gtest banner printed. That is not a test failure — the process
never reached `main()`. SDL3 is built as DLLs on Windows and installed to
`.sdl-prebuilt-Windows-AMD64/install/bin`, the test executables link `SDL3::SDL3`, and Windows
resolves DLLs from the executable's own directory with no RPATH, so the loader killed each process
before entry and the runner sat on it until ctest timed it out. CNA commit `a9017a01b` copies
those DLLs next to the executables at configure time; **its CI run is the thing to check first.**

### Repositories, heads and pins

- CNA: `/rv/data/development/github.com/openeggbert/cnanext`, branch `next`, pushed head
  `a9017a01b` plus whatever commit carries this plan update.
- SharpRuntime: `/rv/data/development/github.com/openeggbert/sharp-runtime`, branch `develop`. The
  user explicitly authorised direct pushes to `sharp-runtime/develop` for this work. The last
  commit made for TINYGL-19 is `f23ded28c7b94a745cf82f8804b5b7104ca5780e`, **and that is what the
  workflow pins** — deliberately, not because the pin is stale. A concurrent session has since
  advanced `develop` to `7a46a538` with 316 files of unrelated XML/IO work (its test floor is
  17,057 across 38 executables, not the 16,344 this task verified against). Windows is now past
  sharp-runtime entirely, so advancing the pin would re-open ~250 build steps of brand-new,
  MSVC-unverified code while this task is being closed. Do not advance it to pick up unrelated
  work; if a further SharpRuntime fix does prove necessary, that content comes along with it and
  has to be re-verified on all three platforms.
- `.github/workflows/tinygl-cross-platform-ci.yml` pins that complete SharpRuntime SHA and TinyGL
  SHA `36a7987e7bebfda19615ea33341b1cc0ff9c3b13`. Never replace either with a moving branch.
- Local build directory `cmake-build-tinygl/` is configured and warm; local sharp-runtime builds
  use its `build/` directory with at most two jobs.

### What TINYGL-19 has fixed, in order

Each row is one CI cycle. SharpRuntime rows were verified locally (full build, then
`scripts/run_component_tests.sh build`, 16,341→16,344 checks across 37 executables) before being
pushed to `develop` and pinned by full SHA.

| Where | What Windows rejected | Fix |
|---|---|---|
| SR `3ad2dd90` | `TimeSpan.cpp` C4996, deprecated `std::sscanf` (step 37/596) | `sscanf_s` on MSVC only |
| SR `4df333e6` | `Calendar.hpp` C4244 (step 55/596) + three more found locally | see the census note below |
| SR `707a5b9b` | `IdnMapping.cpp` C2015, UTF-8 `char32_t` literals read in the host code page | compile with `/utf-8` |
| SR `498a1304` | `FileSystemInfo.cpp` C2039/C3861, no `file_clock::to_sys`/`from_sys` (step 91/596) | *superseded, see below* |
| SR `391563f1` | the fix above broke the previously green macOS job | preprocessor split on `_MSVC_STL_VERSION` |
| SR `062b9286` | `Socket.cpp`/`TcpClient.cpp` C2589/C4003, `windows.h` `min`/`max` macros (step 154/596) | `NOMINMAX` + two Winsock narrowings CI had not reached |
| SR `f23ded28` | `HttpDateParser.hpp` C4477, `sscanf_s` size passed as `size_t` (step 168/596) | narrow it to `unsigned`, what the UCRT reads back |
| CNA `d042781b7` | upstream TinyGL `clip.c` C7660, `#pragma omp simd` is OpenMP 4.0 (step 257/596) | disable the OpenMP *package* for the fetched subdirectory on MSVC |
| CNA `a1019b671` | `Bc7Util.cpp` C2039/C3861 on `std::to_string` (step 329/596) | the three missing standard includes |
| CNA `3f486d9df` | `LNK2019` on `Media::Video::Video` (step 571/596) | gate the video reader on `CNA_FFMPEG_AVAILABLE` |
| CNA `82ee49a1b` | *(gate hole)* the fix above started no CI run at all | watch every module the matrix links, not six |
| CNA `a9017a01b` | all 14 suites time out with no output | copy the SDL DLLs next to the executables |

Matching CNA pin commits for the SharpRuntime rows: `1faefcd4b`, `3d5303410`, `9ad2194d9`,
`5e4457e27`, `4dd97692c`, `b7a9cb532`.

### Four lessons that will save the next context real time

**1. Three local detectors replace most CI round-trips. Run them before every push.** None needs a
build directory; all are `-fsyntax-only` with `-I` for each `modules/*/include` and
`-isystem vendor`. **Validate a detector against a known failure before trusting its silence** —
each of these was, and that is what makes a clean result meaningful.

- *Host census*, for `/W4` diagnostics GCC never emits:
  `clang++ -std=c++23 -fsyntax-only -Wshorten-64-to-32 -Wfloat-conversion -Wshadow -Wshadow-all`.
- *Windows-branch census*, over `_WIN32` code no local compiler had ever built:
  `clang++ --target=x86_64-w64-mingw32 -std=c++23 -fsyntax-only -Wshorten-64-to-32
  -Wfloat-conversion -Wshadow -Wunused-parameter -Wunused-variable`. MinGW-w64 GCC 14 and its
  headers are installed on this machine and clang targets them directly. This one reproduced
  exactly the four `TcpClient.cpp` lines MSVC named, then found four more in `NetworkStream.cpp`
  and `UdpClient.cpp` that CI had not yet reached.
- *Include-closure check*, for the class both of the above are blind to: walk each translation
  unit's full project include closure and report standard headers whose contents are used but
  which appear nowhere in it. libstdc++ and libc++ both hand out `<string>` transitively where
  Microsoft's STL does not. It named the file MSVC had just failed on and found two more.

What no local detector can see: MSVC-STL-only strictness (the `file_clock` case), `windows.h`
macro collisions (MinGW does not define `min`/`max` the same way), C4127, and anything about
*running* the tests.

**2. Only preprocessor text is portable across these three standard libraries.** The `498a1304`
cycle is the cautionary one: a `requires`-based detection of `file_clock::to_sys` looked portable
and broke the green macOS job. Microsoft's STL *diagnoses* that requires-expression rather than
evaluating it to false, because `file_clock` is not a dependent type there; and Apple's libc++
does not declare `std::chrono::clock_cast` at all, so naming it even in a discarded
`if constexpr` branch fails at definition time.

**3. Do not trust a comment that says "must match".** Two separate bugs were exactly that. CNA's
`ThirdPartyTinyGL.cmake` claimed upstream's `OPENMP_C_FOUND` check was an ineffective
mis-spelling of `OpenMP_C_FOUND`; it is not — CMake's `FindOpenMP` calls
`find_package_handle_standard_args(OpenMP_C ...)` per language and FPHSA defines the upper-cased
`<NAME>_FOUND`, so upstream really did switch OpenMP back on after CNA declined to link it. And
`ContentManager.cpp` carried three copies of a platform list standing in for "FFmpeg is
unavailable", with a comment saying it must match CMakeLists.txt — it had drifted, because CMake
also turns FFmpeg off for every `WIN32` build. Both were settled by measurement (a two-file CMake
probe for the first) rather than by reading.

**4. A gate that does not run is worse than no gate.** The `paths:` filter named six modules while
the fifteen test targets link fourteen, so the `modules/content` link fix pushed and started
nothing, leaving a green tick that belonged to an older tree. Fixed in `82ee49a1b`; if a module is
added to the link closure, add it to both `paths:` lists.

### Known false positives, so they are not re-investigated

- **`CNA::Internal::JsonValue`** (`modules/content/include/CNA/Internal/Json.hpp`): the
  Windows-branch census rejects it in 56 places because `objectValue` is a
  `std::vector<std::pair<std::string, JsonValue>>` whose `~vector` is instantiated while
  `JsonValue` is incomplete. `arrayValue` is fine — `std::vector` is one of the three containers
  the standard allows over an incomplete type, `std::pair` is not — so it is genuinely
  non-conforming. But **MSVC accepts it**: Windows compiled `ContentManager.cpp` and reached the
  link stage. Leave it alone unless a compiler actually rejects it; if one does, the small fix is
  user-declared special members defined out of line (no API change), and the conforming one
  replaces the pair with a `JsonMember` struct (changes `.first`/`.second` to `.name`/`.value` at
  11 sites).
- **Census noise from compiling every `.cpp` in a module** rather than what CMake builds:
  `termios.h`, `poll.h`, `SIGWINCH` and `struct sigaction` come from `src/Terminal/`, gated
  `if(NOT WIN32)`; `SDL.h` and `SDL2/SDL_audio.h` come from SDL2 backends this configuration never
  builds; `libavformat/avformat.h` appears only because the census reuses Linux flags carrying
  `CNA_FFMPEG_AVAILABLE`, which the Windows job does not set (it installs zlib only).
- **34 files use `std::move`/`std::pair` without `<utility>`.** Real by include-what-you-use
  standards, but every container header supplies it on MSVC too. Not touched; the list is one
  include-closure run away if MSVC ever complains.
- **Runner warnings**: the Node.js 20 action-deprecation annotation and the macOS Homebrew
  untrusted-tap message are non-blocking, and both green platform jobs prove they are unrelated.
- `workflow_dispatch` returns HTTP 403 — it needs repository-admin permission. Push a change under
  the watched paths to start the matrix.

### Work remaining

1. **Check the run for CNA `a9017a01b`.** If the 14 suites now pass on Windows, TINYGL-19 is done
   except for documentation; go to step 4.
2. If they still fail, distinguish the two shapes before fixing anything: *no output plus a
   uniform 30 s timeout* means the process never started (a DLL the loader cannot find — check
   which, the SDL trio is handled but zlib comes from vcpkg), whereas *gtest output followed by a
   comparison failure* is a genuine renderer or fixture difference and belongs in this plan as a
   capability note, not a build fix.
3. Repeat the cycle for any further failure: run the three detectors first, fix at the root, never
   weaken a warning, never replace a pin with a branch, and verify locally on Linux before
   pushing. If a SharpRuntime change is needed, build it with at most two jobs, run
   `scripts/run_component_tests.sh build` to a clean full pass, push to `develop` and pin the new
   full 40-character SHA.
4. When all three jobs are green: set this Status and the TINYGL-19 row to **DONE**, and update
   `docs/tinygl-renderer.md` — it still says "Verified on Linux x86_64 only", which must become
   native Linux x86_64, macOS arm64 and Windows x86_64, with the final green run linked. Note
   there that MSVC builds TinyGL single-threaded (already documented in the build section).
5. The local evidence for the final report is **already collected** against SharpRuntime
   `f23ded28` and does not need repeating unless CNA sources change again: 14/14 TinyGL suites
   pass; the no-OpenMP pass (`cmake -S . -B cmake-build-tinygl
   -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON`, rebuild, test, then reconfigure with `=OFF` to
   restore) passes 14/14 with zero `GOMP_*`/`omp_*` references in `libtinygl-static.a`;
   `python3 scripts/check_renderer_identities.py` reports 47 identities.

Keep the current capability boundary: "maximum" here means every XNA-facing operation the
fixed-function TinyGL rasterizer can implement faithfully, plus deterministic rejection of
unsupported stencil, shader, render-target, general-alpha and related paths. Do not claim parity
with shader-capable GPU backends, and do not turn a documented refusal into a silent
approximation.

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

`TINYGL-19` is the only active phase. Linux x86_64 and macOS arm64 are verified; Windows x86_64
builds and links completely and needs its first passing test run, then the final documentation --
follow the continuation handoff above, which is written to be picked up cold. Any new renderer feature after TINYGL-19 needs its own explicit owner instruction,
exactly like every other renderer's plan.
