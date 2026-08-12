# plan_study.md — Ten-Year CNA Study and Debugging Plan

> **What this is.** The project owner's personal plan for gradually learning the *entire*
> CNA source code over ten years (520 weeks). Most of that code was machine-generated in
> three months. The plan also covers fixing, simplifying, and optimizing it at the same pace.
>
> **What this is not.** This is not a plan for developing new features. No new renderer,
> module, or API surface is added here—for that, see `FUTURE.md` and the individual
> `plan_*.md` files. This document says **what to read, what to understand, and what to fix
> in return**, week by week.

## 0. Baseline (measured on 2026-08-12 on `develop` + `11branches`)

| Metric | Value |
|---|---|
| Production code (`modules/**`, excluding `tests/` and `examples/`) | **416,356 lines** in **1,194 files** |
| Of which renderers (`modules/renderers/**`) | **282,459 lines** (68%) |
| The rest (math, core, runtime, graphics, content, audio, input, media, net, devices, gamer-services, storage) | **133,897 lines** |
| Tests | **144,584 lines**, 419 `.cpp` files |
| Examples | **328,898 lines**, 1,233 `.cpp` files |
| Planning documents `plan*.md` | 66,720 lines |
| `docs/*.md` | 31,606 lines |
| Public renderer identities | 46 |
| Renderer implementation families | 42 |

**Plan arithmetic.** 520 weeks minus 40 reserve weeks (4 per year) = **480 working weeks**.
At ~10 hours/week, that is ~4,800 hours. For 416,000 lines of production code, this works
out to ~870 lines per working week—the pace of *slow reading with verification*, not skimming.
That is exactly the point.

> **Note on the assignment.** The assignment mentioned both "ten years" and "ten months."
> This document covers ten years, as requested by the title. Anyone who wants the ten-month
> version can take **year 1 + year 2 Q1–Q2** (weeks 1–78) and postpone the rest. That is a
> coherent unit that makes sense on its own: the build is under control, the math is verified,
> the game loop is understood, and the `Graphics` core has been read.

---

## 1. Ground Rules

### 1.1 Weekly rhythm (~10 hours)

| Block | Time | Content |
|---|---|---|
| **Reading** | 4 h | CNA code alongside the FNA reference (`/rv/data/library/github.com/FNA-XNA/FNA`). Never without the reference open. |
| **Running** | 2 h | Build + run + debugger/print. I do not understand code that I have only read and never run. |
| **Intervention** | 2 h | One fix, one test, one simplification. Even a small one. |
| **Writing** | 2 h | `study/W<NNN>-<topic>.md` + commit. |

### 1.2 Required output every week

1. A notes file, `study/W<NNN>-<topic>.md`—what I read, what I *did not understand*, and what looks suspicious.
2. **At least one commit.** A test, fix, comment, or document. A week without a commit counts as incomplete.
3. A new entry in `known_bugs.md` if I found something. Suspicions are recorded too—with a question mark.

### 1.3 Seven questions for every file read

Record these in the notes, not in the code:

1. What does the corresponding FNA file do, and how does CNA differ?
2. Is that deviation listed among the acceptable ones in `CHECKLIST.md`, or is it a bug?
3. Which test covers it? If none, why did it pass the audit?
4. How much of this code is actually reachable from the public API?
5. What would happen if I deleted all of it? (Test the answer: delete, compile, run the tests.)
6. Which renderer depends on it, and which does not?
7. Would it be shorter if I wrote it myself?

### 1.4 Reserve weeks

The last week of each quarter (**W13, W26, W39, W52** within each year) is a reserve:
catch up, reread my notes from the quarter, and update `AUDIT.md` and `known_bugs.md`.
A reserve week is **not pulled forward**. If I am not behind, I read the notes.

### 1.5 Rules for the entire decade

- **No large AI batches.** From now on, a tool may fix only a few files at a time and only where
  I understand both the assignment and the result. The original three-month "generate a module"
  mode is over.
- **No new renderer** until I understand `IGraphicsRenderer` (year 3).
- **No refactoring** of a module I have not yet read.
- **Do not delete tests** to make the build pass. Either fix the test or mark it as a known issue.

---

## 2. Renderer Study Scope

Renderers account for 68% of the code, and most of them **will not** be studied. This
classification is binding.

### Tier A — study in depth (years 3–5)

| Renderer | Module | Why |
|---|---|---|
| `IGraphicsRenderer` + `common` | `modules/graphics/include/CNA/Internal/Renderers/Common/` | The contract through which everything passes |
| EasyGL (5 profiles) | `modules/renderers/easygl` | Reference implementation, most tests, `OPENGL33`/`OPENGLES1..3`/`WEBGL1..2` |
| `OPENGL1`, `OPENGL2`, `OPENGL4` | `modules/renderers/opengl{1,2,4}` | Separate profiles outside EasyGL |
| `VULKAN` | `modules/renderers/vulkan` (11,824 lines) | Most complete explicit backend |
| `SDL_RENDERER` | `modules/renderers/sdl-renderer` | 2D foundation, easiest to read |
| `SDL_GPU` | `modules/renderers/sdl-gpu` | Future default portable backend |
| `DIRECTX11`, `DIRECTX12` | `modules/renderers/directx1{1,2}` | Windows reality |
| `DIRECTX9` | `modules/renderers/directx9` | XNA's native home, the best reflection of its semantics |
| `METAL` | `modules/renderers/metal` | Apple reality |
| `WEBGPU` | `modules/renderers/webgpu` (10,394 lines) | Future of the web |
| `BGFX` | `modules/renderers/bgfx` | Reference middleware layer |
| `SOFTWARE` | `modules/renderers/software` | CPU rasterization—the best textbook |
| `HTML_DOM` | `modules/renderers/html-dom` | **Explicitly requested** |
| `SVG_DOM` | `modules/renderers/svg-dom` | **Explicitly requested** |
| `CANVAS` | `modules/renderers/canvas` | Small, complements HTML_DOM |
| `FNA3D` | `modules/renderers/fna3d` | The only one that runs actual XNA stock effects—the fidelity benchmark |
| `HEADLESS`, `STUB` | `modules/renderers/{headless,stub}` | Test infrastructure; read these first |

### Tier B — one week on "what it is and why it is here" (year 5 Q4)

`SOKOL`, `DILIGENT`, `MAGNUM`, `DIRECT2D`, `GDI`, `OPENGLES1` (as a separate family),
`PORTABLEGL`, `FREEDIRECT`.

### Tier C — do not study; decide only what to do with it (year 5 Q4, year 9 Q4)

`SKIA`, `BLEND2D`, `GLIDE`, `LLGL`, `WICKED`, `OPENVG`, `DIRECTX1`–`DIRECTX8`, `DIRECTX10`.

Tier C has just one task for the entire decade: decide **maintain / freeze / remove**,
record the decision in `FUTURE.md`, and configure CI accordingly. Do not read it line by line.

---

## 3. Ten-Year Overview

| Year | Weeks | Theme | Completion criterion |
|---|---|---|---|
| 1 | 1–52 | Build, tools, `math`, `core`, `runtime`, sharp-runtime | I can build, measure, and debug the project; I own the math |
| 2 | 53–104 | `Microsoft::Xna::Framework::Graphics`—public API | I know every one of the ~108 `graphics` headers and who calls it |
| 3 | 105–156 | Renderer contract, GL family, SOFTWARE, pixel tests | I can write a renderer from scratch |
| 4 | 157–208 | Vulkan, SDL_GPU, D3D11/12, Metal, WebGPU, bgfx | I understand explicit APIs and know where CNA lies about capabilities |
| 5 | 209–260 | HTML_DOM, SVG_DOM, Canvas, D3D9, FNA3D + portfolio decisions | The renderer portfolio has been deliberately reduced |
| 6 | 261–312 | `content`, XNB, glTF/CNJ, textures, fonts | I can load any asset and know why it fails |
| 7 | 313–364 | `audio`, `media`, `input`, `devices`, `net`, `gamer-services`, `storage` | Non-gameplay subsystems are understood and trimmed |
| 8 | 365–416 | Tests, sanitizers, FNA fidelity, eliminating `known_bugs.md` | A green build means a correct build |
| 9 | 417–468 | Performance, memory, threads, architectural cleanup | CNA is measurably faster and smaller |
| 10 | 469–520 | XNA samples, documentation, platforms, 1.0 | 1.0 released and a plan for the next decade |

---
## Year 1 (W1–W52) — Build, Tools, Math, Game Loop

> Goal for the year: stop being a guest in the project. By the end of the year, I must be
> able to build the project in every configuration, measure and debug it, and know `math`,
> `core`, and `runtime` well enough to spot nonsense without opening the FNA reference.

### Q1 (W1–W13) — Build and tools

| W | Topic | Material | Output |
|---|---|---|---|
| W1 | Repository topology | `README.md`, `CLAUDE.md`, `CHECKLIST.md`, `docs/README.md` | `study/` created; one-page repository map |
| W2 | Root build | `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | Target diagram; custom preset |
| W3 | Module separation | `modules/CMakeLists.txt`, `scripts/check_module_link_closure.py` | Source separation validator understood; deliberately break it |
| W4 | Renderer selection | `cmake/RendererSelection.cmake`, `CNA/GraphicsRendererType.hpp`, `scripts/check_renderer_identities.py` | Table of 46 identities manually verified |
| W5 | Build speed | `ccache`, `-j4`, `cmake-build-debug/` | Build-time baseline measured (clean and incremental) |
| W6 | Test infrastructure | `ctest`, GoogleTest registration, `tests/` | Full suite run; list of skipped tests and reasons |
| W7 | First run | `modules/*/examples/`, `examples/xvfb_screenshot_demo.cpp` | My own screenshot from a running example |
| W8 | Debugging tools | gdb/lldb, ASan, valgrind, perf | `study/tooling.md` + scripts in `scripts/` |
| W9 | Dependencies | `third_party/`, `vendor/`, `cmake/ThirdParty*.cmake` | What is vendored vs. downloaded; verified offline build |
| W10 | sharp-runtime as a dependency | `SharpRuntime::*` targets, `SharpRuntimeHelper.hpp` | Diagram of what CNA actually uses from the runtime |
| W11 | Physical layout | `docs/physical-modules.md`, `MODULARIZATION_PLAN.md` §11 | File placement rules understood |
| W12 | What runs in CI | `.github/`, `scripts/run-all-renderer-smoke-tests.sh` | List of what CI **does not check** |
| W13 | **RESERVE** | my notes | Update `AUDIT.md`/`known_bugs.md` |

### Q2 (W14–W26) — `modules/math` (20,760 lines)

| W | Topic | Material | Output |
|---|---|---|---|
| W14 | Fundamentals | `MathHelper.hpp`, `Point.hpp` | Test for every boundary value |
| W15 | `Vector2` | `Vector2.{hpp,cpp}` vs. FNA | Differences from FNA recorded |
| W16 | `Vector3` | `Vector3.{hpp,cpp}` | Out-ref overloads verified |
| W17 | `Vector4` | `Vector4.{hpp,cpp}` | — |
| W18 | `Matrix` I | construction, `Create*` | Manually verify 5 matrices against XNA |
| W19 | `Matrix` II | `Decompose`, `Invert`, transforms, operators | Numerical stability test |
| W20 | `Quaternion` | `Quaternion.{hpp,cpp}` | Test quat↔matrix↔Euler conversions |
| W21 | `Plane`, `Ray` | + `PlaneIntersectionType` | Intersection tests |
| W22 | `BoundingBox`, `BoundingSphere` | + `ContainmentType` | All `Contains`/`Intersects` overloads |
| W23 | `BoundingFrustum` | `BoundingFrustum.{hpp,cpp}` | Hardest type in the module—custom visual test |
| W24 | Curves | `Curve`, `CurveKey`, `CurveKeyCollection`, 3 enums | Test every `CurveLoopType` |
| W25 | `Color` and packed types | `Color.hpp` (141 constants), `IPackedVector` | Verify AABBGGRR layout |
| W26 | **RESERVE** | `math` test coverage | Coverage report |

### Q3 (W27–W39) — `modules/core` + `modules/runtime` + `storage`

| W | Topic | Material | Output |
|---|---|---|---|
| W27 | CNA core | `CNAHelper.hpp` (CNAEXT), `CNAException`, `Logger`, `LogLevel`, `LogCategory` | Logging rules recorded |
| W28 | Program startup | `CNA/Entrypoint.hpp`, `Platform.hpp`, `DesktopOS.hpp`, `main.cpp` | Startup sequence on 3 platforms |
| W29 | `Game` I | `Game.{hpp,cpp}`: construction, `Run`, `Initialize`, `LoadContent` | — |
| W30 | `Game` II | game loop, fixed/variable time step, `IsRunningSlowly` | Measure actual time-step stability |
| W31 | Time and window | `GameTime`, `GameWindow` | Test behavior during a freeze |
| W32 | Components | `GameComponent`, `DrawableGameComponent`, collections, `IUpdateable`/`IDrawable` | Verify `Update`/`Draw` order |
| W33 | Services | `GameServiceContainer`, `IGraphicsDeviceManager`, `GraphicsDeviceInformation` | — |
| W34 | `GraphicsDeviceManager` I | device selection, `PreparingDeviceSettingsEventArgs` | — |
| W35 | `GraphicsDeviceManager` II | `ApplyChanges`, fullscreen, reset | Display mode switching test |
| W36 | Miscellaneous | `TitleContainer`, `TitleLocation`, `LaunchParameters`, `CNA/Misc.hpp` | — |
| W37 | `storage` (1,033 lines) | `StorageDevice`, `StorageContainer`, exception | Entire module read in one week |
| W38 | Shutdown | `ExitingEventArgs`, `FrameworkDispatcher`, `docs/emscripten-mainloop-game-lifetime.md` | Lifecycle on web vs. desktop |
| W39 | **RESERVE** | — | — |

### Q4 (W40–W52) — sharp-runtime and audit methodology

| W | Topic | Material | Output |
|---|---|---|---|
| W40 | Runtime overview | sharp-runtime repository, its components | `System::*` map |
| W41 | `System::Object` | + `GetTypeName()` across CNA | Check that all concrete classes have an override |
| W42 | `IDisposable` | `Dispose(bool)` pattern, `isDisposed_` | Find classes without a guard (see `Texture2D::SetData`!) |
| W43 | Events | `System::EventHandler<T>`, `Raise`/`Invoke` | Unsubscription test |
| W44 | Type aliases | `bytecs`…`charcs`, `Single`, `String` | List places where the API uses raw C++ types |
| W45 | Exceptions | `System::Exception` and descendants | Map to XNA behavior |
| W46 | I/O | `System::IO::Stream` and files | — |
| W47 | Collections | `System::Collections::*` | What is unnecessary |
| W48 | Time | `TimeSpan`, `DateTime`, `DateTimeOffset` (see `TODO.md`—partial) | List what is missing |
| W49 | Checklist | `CHECKLIST.md` line by line | My own shortened version for daily use |
| W50 | Audit methodology | `AUDIT.md`—verify 20 random ✅ rows | Report: how many ✅ claims held up |
| W51 | Own audit | `math`+`core`+`runtime` vs. FNA, `scripts/compare-fna-reference.py` | List of findings |
| W52 | **RESERVE** | year-end review | `study/rok1-zaver.md` |

---

## Year 2 (W53–W104) — `Microsoft::Xna::Framework::Graphics` (Public API)

> Goal for the year: know all ~108 headers in the `graphics` module (146,078 lines including
> tests/examples) and, for each one, know who calls it, which renderer implements it, and which
> test protects it.

### Q1 (W53–W65) — `GraphicsDevice` and its surroundings

| W | Topic | Material | Output |
|---|---|---|---|
| W53 | Stated claims | `docs/xna-4-api-coverage.md`, `docs/graphics-compatibility-report.md` | List of claims to verify during the year |
| W54 | Public surface | `GraphicsDevice.hpp` | Diagram: method → renderer call |
| W55 | `GraphicsDevice` I | `.cpp` (3,108 lines): construction, `Reset`, `PresentationParameters` | — |
| W56 | `GraphicsDevice` II | `Clear`, `Present`, viewport, scissor | — |
| W57 | `GraphicsDevice` III | `Draw*` family | Table of all overloads |
| W58 | `GraphicsDevice` IV | `SetRenderTarget(s)`, backbuffer | — |
| W59 | Resource lifetime | `GraphicsResource`, `docs/graphics-resource-lifetime.md` | Disposal-order test |
| W60 | Adapters | `GraphicsAdapter`, `DisplayMode`, `DisplayModeCollection` | — |
| W61 | Profiles and errors | `GraphicsProfile`, `GraphicsDeviceStatus`, `DeviceLost*`/`DeviceNotReset*`/`NoSuitable*` | — |
| W62 | Viewport | `Viewport`, `docs/viewport-displaymode-adapter-support.md` | — |
| W63 | Capabilities | `CNA/GraphicsCapability.hpp` | Who reports what—the first suspicion of a lie |
| W64 | Rejection | `Unsupported3DGraphicsCallBehavior`, `NotYetImplemented`, `NoOp3DResources` | Rejection policy recorded |
| W65 | **RESERVE** | — | — |

### Q2 (W66–W78) — Resources

| W | Topic | Material | Output |
|---|---|---|---|
| W66 | Texture fundamentals | `Texture`, `SurfaceFormat`, `docs/surface-format-support.md` | Format matrix |
| W67 | `Texture2D` | `Texture2D.{hpp,cpp}` | — |
| W68 | `Texture3D`, `TextureCube` | `docs/texture3d-texturecube-support.md` | Open architectural question about sampler binding |
| W69 | Render targets | `RenderTarget2D`, `RenderTargetCube`, `IRenderTarget`, `docs/rendertarget-support.md` | — |
| W70 | Depth and MSAA | `DepthFormat`, `RenderTargetUsage`, multisampling | — |
| W71 | Compression and loading | `DxtUtil`, `Bc7Util`, `ImageLoader`, `ImageData` | — |
| W72 | Vertex buffers | `VertexBuffer`, `DynamicVertexBuffer`, `BufferUsage` | — |
| W73 | Index buffers | `IndexBuffer`, `DynamicIndexBuffer`, `IndexElementSize` | Verify enum numeric values (previous bug) |
| W74 | Vertex declarations | `VertexDeclaration`, `VertexElement*`, `IVertexType`, `docs/vertex-format-support.md` | — |
| W75 | Built-in types | `VertexPosition*`, `BuiltInVertexStreams`, `VertexDeclarationFidelity` | — |
| W76 | `SetData`/`GetData` | semantics, `SetDataOptions`, offsets | **Known gap**: `SetData` with a destination offset is missing—proposal |
| W77 | Queries | `OcclusionQuery`, `docs/occlusionquery-support.md` | — |
| W78 | **RESERVE** | — | — |

### Q3 (W79–W91) — States and 2D

| W | Topic | Material | Output |
|---|---|---|---|
| W79 | Blending | `BlendState`, `Blend`, `BlendFunction`, `ColorWriteChannels` | Test every preset state |
| W80 | Depth/stencil | `DepthStencilState`, `CompareFunction`, `StencilOperation`, `docs/depthstencilstate-support.md` | — |
| W81 | Rasterization | `RasterizerState`, `CullMode`, `FillMode`, `docs/rasterizerstate-support.md` | — |
| W82 | Sampling | `SamplerState`, `TextureFilter`, `TextureAddressMode`, `docs/sampler-state-support.md` | — |
| W83 | `SpriteBatch` I | `SpriteBatch.{hpp,cpp}` (690 lines): `Begin`/`End`, `SpriteSortMode` | — |
| W84 | `SpriteBatch` II | `Draw` overloads, source rectangle, origin, rotation | Verify the optional source-rectangle fix |
| W85 | `SpriteBatch` III | `DrawString`, batching, buffer management | — |
| W86 | Known bug | `known_bugs.md`: multiple `Begin`/`End` calls in one frame | Reproduce on 3 renderers + test |
| W87 | Fonts | `SpriteFont`, `docs/spritefont-support.md`, `Utf8Decode` | — |
| W88 | Primitives | `SpriteEffects`, `PrimitiveType`, `DrawUserPrimitives`, `DrawUserIndexedPrimitives` | — |
| W89 | First renderer | 2D path in `modules/renderers/sdl-renderer` | Preview of year 3 |
| W90 | Custom extension | `modules/graphics-ext`: `AsciiPostProcessEffect`, `docs/ascii-post-process-effect.md` | Pattern for writing CNAEXT |
| W91 | **RESERVE** | — | — |

### Q4 (W92–W104) — Effects and models

| W | Topic | Material | Output |
|---|---|---|---|
| W92 | Effect as an object | `Effect`, `EffectPass`, `EffectTechnique` + collections | — |
| W93 | Parameters | `EffectParameter`, `EffectParameterClass/Type`, `EffectAnnotation` | — |
| W94 | Shaders vs. bytecode | `docs/shader-effect-vs-fx-bytecode.md`, `docs/fx-bytecode-support-plan.md` | Decision: ever pursue FX bytecode? |
| W95 | `BasicEffect` | + `docs/basiceffect-support.md` | Visual test |
| W96 | `AlphaTestEffect`, `DualTextureEffect` | + corresponding docs | — |
| W97 | `EnvironmentMapEffect` | `docs/environmentmapeffect-support.md` | — |
| W98 | `SkinnedEffect` | + bone matrix packing | Verify the `SetMatrix4x3Array` type bug across renderers |
| W99 | Effect interfaces | `IEffectMatrices`, `IEffectLights`, `IEffectFog`, `DirectionalLight`, `EffectMaterial` | — |
| W100 | Model | `Model`, `ModelMesh`, `ModelMeshPart` + collections | — |
| W101 | Skeleton | `ModelBone`, `ModelBoneCollection`, root bone | Verify the previous root-bone override fix |
| W102 | Animation and morphs | `AnimationPlayer`, `MorphTargetEXT` | Mark what is outside XNA |
| W103 | Own audit | `graphics` module against FNA | List of untested public APIs |
| W104 | **RESERVE** | year-end review | `study/rok2-zaver.md` |

---
## Year 3 (W105–W156) — Renderer Contract, GL Family, SOFTWARE, Pixel Tests

> Goal for the year: by the end, I can write a CNA renderer from scratch and know why
> EasyGL is the reference implementation.

### Q1 (W105–W117) — Contract and the simplest implementations

| W | Topic | Material | Output |
|---|---|---|---|
| W105 | Registry | `docs/renderer-registry.md`, `docs/graphics-renderer-feature-matrix.md` | Verify that 46 rows match the code |
| W106 | `IGraphicsRenderer` I | 2,079 lines—lifecycle, device | — |
| W107 | `IGraphicsRenderer` II | resources: textures, buffers | — |
| W108 | `IGraphicsRenderer` III | states and shaders | — |
| W109 | `IGraphicsRenderer` IV | drawing, render targets, readback | Complete list of contract methods |
| W110 | `STUB` | all of `modules/renderers/stub` (415 lines) | Read in full |
| W111 | `HEADLESS` | `modules/renderers/headless`, `docs/headless-renderer.md` | — |
| W112 | Renderer registration | CMake + macros + selector | Create a custom practice renderer |
| W113 | Practice renderer | continuation | My renderer passes smoke tests |
| W114 | `SDL_RENDERER` I | `modules/renderers/sdl-renderer` | — |
| W115 | `SDL_RENDERER` II | `docs/sdl-renderer-2d-completeness.md` | Verify the open `TextureAddressMode::Wrap/Mirror` question |
| W116 | `SOFTWARE` I | rasterizer (3,246 lines) | — |
| W117 | **RESERVE** | — | — |

### Q2 (W118–W130) — SOFTWARE and EasyGL

| W | Topic | Material | Output |
|---|---|---|---|
| W118 | `SOFTWARE` II | interpolation, depth buffer | — |
| W119 | `SOFTWARE` III | texturing, blending | — |
| W120 | `SOFTWARE` IV | effects and limits, `docs/software-renderer.md` | Best GPU textbook—write "how a GPU works" |
| W121 | EasyGL: architecture | why one implementation supports 5+ profiles | — |
| W122 | EasyGL: context | initialization, capability detection | — |
| W123 | EasyGL: textures | — | — |
| W124 | EasyGL: buffers | vertex attributes, baseVertex emulation | — |
| W125 | EasyGL: shaders | generate GLSL by dialect | Most important week of the quarter |
| W126 | EasyGL: states | map XNA states to GL | — |
| W127 | EasyGL: FBO | render targets, MRT, mipmaps | — |
| W128 | EasyGL: SpriteBatch | 2D batching path | — |
| W129 | EasyGL: effects | basic/alphatest/dual/envmap/skinned | — |
| W130 | **RESERVE** | — | — |

### Q3 (W131–W143) — GL profiles

| W | Topic | Material | Output |
|---|---|---|---|
| W131 | `OPENGL33` | default profile | — |
| W132 | `OPENGLES3` | — | — |
| W133 | `OPENGLES2` | `plan_opengles2.md`, `docs/opengles2-renderer.md` | Verify declared rejections |
| W134 | `OPENGLES1` | `modules/renderers/opengles1`—fixed pipeline | `docs/opengles1-parity-report.md` verified |
| W135 | `WEBGL1`/`WEBGL2` | + Emscripten | Run in a browser |
| W136 | `OPENGL1` | immediate mode (5,048 lines) | — |
| W137 | `OPENGL2` | 12,262 lines | — |
| W138 | `OPENGL4` | 10,781 lines | — |
| W139 | EasyGL bugs | `docs/easygl_bugs.md` | Confirm or close every item |
| W140 | Profile matrix | what each profile rejects | One table instead of eight documents |
| W141 | Exercise | add a missing capability to one profile | Commit with test |
| W142 | Golden corpus | `examples/golden/`, `scripts/run-oracle-corpus-diff-easygl.sh` | — |
| W143 | **RESERVE** | — | — |

### Q4 (W144–W156) — Pixel-level confidence

| W | Topic | Material | Output |
|---|---|---|---|
| W144 | Pixel test harness | test code for image comparison | — |
| W145 | FNA reference | `docs/fna-reference-harness.md`, `scripts/compare-fna-reference.py` | My own run against FNA |
| W146 | XNA oracle | 39-scene corpus (`*_XNA_Oracle`) | Read all 39 scenes |
| W147 | Renderer differences | cross-renderer diff tests | List of mismatches |
| W148 | Virtual display | Xvfb, `scripts/*virtual-display*` | — |
| W149 | Visual regression | `scripts/avatar_visual_regression_check.py` | — |
| W150 | Custom golden test | uncovered property | Commit |
| W151 | Test duration | measurement | Acceleration plan |
| W152 | Tier A audit | which capability claims are false | List of lies in capability reporting |
| W153 | Fix #1 | from W152 | Commit |
| W154 | Fix #2 | from W152 | Commit |
| W155 | Guide | `docs/how-to-write-a-renderer.md` | New document, written by me |
| W156 | **RESERVE** | year-end review | `study/rok3-zaver.md` |

---

## Year 4 (W157–W208) — Explicit and Modern Backends

> Goal for the year: understand Vulkan/D3D12-class APIs well enough to recognize where
> the CNA abstraction needlessly throttles performance and where it instead hides incorrect
> behavior.

### Q1 (W157–W169) — Vulkan I

| W | Topic | Material | Output |
|---|---|---|---|
| W157 | Orientation | `VulkanRenderer.cpp` (11,824 lines)—file map | Divide into 12 reading blocks |
| W158 | Foundation | instance, device, queues, swapchain | — |
| W159 | Memory | allocation, heaps, staging | — |
| W160 | Images | textures, layouts, transitions | — |
| W161 | Buffers | uploads, dynamic buffers | — |
| W162 | Descriptors | sets, layouts, pools | — |
| W163 | Render passes | framebuffers, attachments | — |
| W164 | Pipeline | objects, cache, keys | — |
| W165 | Shaders | SPIR-V, generation, reflection | — |
| W166 | Commands | command buffers, synchronization, fences/semaphores | — |
| W167 | SpriteBatch | 2D path in Vulkan | — |
| W168 | Render targets | MRT, mipmaps, cube faces | — |
| W169 | **RESERVE** | — | — |

### Q2 (W170–W182) — Vulkan II and SDL_GPU

| W | Topic | Material | Output |
|---|---|---|---|
| W170 | Readback | `GetData`, occlusion query (previous architectural blocker) | — |
| W171 | Effects | stock effects in Vulkan | — |
| W172 | Resilience | resize, device lost, error paths | — |
| W173 | Validation | validation layers, debugging tools (RenderDoc) | Analyze a captured frame |
| W174 | Performance | Vulkan vs. EasyGL on the same scene | Measured numbers |
| W175 | Fixes | two from my own audit | Commits |
| W176 | `SDL_GPU` I | API model, differences from Vulkan | — |
| W177 | `SDL_GPU` II | resources, copy pass | — |
| W178 | `SDL_GPU` III | shaders and their formats | — |
| W179 | `SDL_GPU` IV | states and pipeline | — |
| W180 | `SDL_GPU` V | render targets (7,347 lines total) | — |
| W181 | Comparison | what to unify between Vulkan and SDL_GPU | Shared-layer proposal |
| W182 | **RESERVE** | — | — |

### Q3 (W183–W195) — Direct3D and Metal

| W | Topic | Material | Output |
|---|---|---|---|
| W183 | D3DCommon I | `modules/renderers/common/d3d`: formats, states | — |
| W184 | D3DCommon II | constant buffers, shader cache, HLSL sources | — |
| W185 | `DIRECTX11` I | device, swapchain, context | — |
| W186 | `DIRECTX11` II | resources and states | — |
| W187 | `DIRECTX11` III | drawing and effects | — |
| W188 | `DIRECTX12` I | queues, allocators, fences | — |
| W189 | `DIRECTX12` II | root signature, descriptor heaps | — |
| W190 | `DIRECTX12` III | PSO, barriers, drawing | — |
| W191 | Testing from Linux | `scripts/run-wine-*.sh`, `run-proton-vkd3d.sh`, DXVK | My own D3D11 and D3D12 run |
| W192 | `METAL` I | `docs/metal-renderer.md` | — |
| W193 | `METAL` II | `docs/metal-shader-effect-contract.md` | — |
| W194 | Truth about Metal | what has not been verified without macOS | Honest entry in the docs |
| W195 | **RESERVE** | — | — |

### Q4 (W196–W208) — WebGPU and bgfx

| W | Topic | Material | Output |
|---|---|---|---|
| W196 | WebGPU status | `plan_webgpu.md`, `docs/webgpu-renderer.md` | List of what is actually complete |
| W197 | Foundation | device, surface, present (wgpu-native v29.0.1.1) | — |
| W198 | Resources | buffers and textures | — |
| W199 | Bind groups | layouts and their cache | — |
| W200 | WGSL | shaders and SpriteBatch | — |
| W201 | Pipeline | render pipeline and states | — |
| W202 | Reach parity | what is missing | My own task list in `plan_webgpu.md` |
| W203 | `BGFX` I | view/encoder model | — |
| W204 | `BGFX` II | resources and uniforms | — |
| W205 | `BGFX` III | `shaderc`, generated `bgfx_shaders.hpp` | Rebuild the shaders myself |
| W206 | Lessons | middleware vs. native API | Notes for CNA architecture |
| W207 | Summary | 6 explicit backends vs. the CNA abstraction | List of bad abstractions |
| W208 | **RESERVE** | year-end review | `study/rok4-zaver.md` |

---

## Year 5 (W209–W260) — Document Renderers, D3D9, FNA3D, Portfolio Reduction

> Goal for the year: finish studying the explicitly requested `HTML_DOM` and `SVG_DOM`,
> understand the highest-fidelity backends (`DIRECTX9`, `FNA3D`), and **deliberately reduce**
> the number of maintained renderers.

### Q1 (W209–W221) — Web and HTML_DOM

| W | Topic | Material | Output |
|---|---|---|---|
| W209 | Web build | Emscripten pipeline, `docs/web-emscripten-graphics-limitations.md` | My own web build |
| W210 | `CANVAS` | all of `modules/renderers/canvas` (1,679 lines) | — |
| W211 | `HTML_DOM` I | how `Draw` becomes a DOM node | Most interesting architecture in the project |
| W212 | `HTML_DOM` II | textures and images | — |
| W213 | `HTML_DOM` III | transforms and layering (z-order) | — |
| W214 | `HTML_DOM` IV | text and fonts | — |
| W215 | `HTML_DOM` V | performance and limits | Measure node count vs. FPS |
| W216 | Test harness | `scripts/htmldom-browser-test.mjs`, `run-htmldom-test-suite.sh` | — |
| W217 | Host integration | `scripts/htmldom-host-integration-test.mjs` | — |
| W218 | Custom demo | small game running entirely in the DOM | Commit the example |
| W219 | Fixes | two | Commits |
| W220 | Boundaries | what `HTML_DOM` should support and what it never should | Record in `docs/html-dom-renderer.md` |
| W221 | **RESERVE** | — | — |

### Q2 (W222–W234) — SVG_DOM

| W | Topic | Material | Output |
|---|---|---|---|
| W222 | `SVG_DOM` I | architecture, `docs/svg-dom-renderer.md` | — |
| W223 | `SVG_DOM` II | geometry and paths | — |
| W224 | `SVG_DOM` III | fills, gradients, images | — |
| W225 | `SVG_DOM` IV | text | — |
| W226 | `SVG_DOM` V | clipping, masks, transparency | — |
| W227 | Tests | `scripts/run-svgdom-browser-test.sh` | — |
| W228 | Sharing | what `SVG_DOM` shares with `HTML_DOM` | Shared-layer proposal |
| W229 | Export | static SVG as output (vector screenshot) | Prototype |
| W230 | Fixes | two | Commits |
| W231 | `OPENVG` | Tier B overview—does it make sense alongside `SVG_DOM`? | Recommendation |
| W232 | Web as a whole | `WEBGL1/2` + `WEBGPU` + `CANVAS` + `HTML_DOM` + `SVG_DOM` | One decision table |
| W233 | Decision | web portfolio | Record in `FUTURE.md` |
| W234 | **RESERVE** | — | — |

### Q3 (W235–W247) — DIRECTX9 and FNA3D

| W | Topic | Material | Output |
|---|---|---|---|
| W235 | Why D3D9 | `docs/d3d9-divergence-report.md`—XNA's native home | — |
| W236 | `DIRECTX9` I | device, present, device loss | — |
| W237 | `DIRECTX9` II | resources and pools | — |
| W238 | `DIRECTX9` III | states, fixed vs. programmable | — |
| W239 | `DIRECTX9` IV | shaders and effects (20,481 lines total) | — |
| W240 | Divergence | verify the report point by point | Update the document |
| W241 | `FNA3D` I | what it is, pinning (release 26.08) | — |
| W242 | `FNA3D` II | map enums with `static_assert`, device API | — |
| W243 | `FNA3D` III | MojoShader and **actual** XNA stock effects | Closest contact with the original |
| W244 | `FNA3D` IV | native multi-stream vertex input | — |
| W245 | FNA3D as an oracle | run the other renderers against it | `scripts/run-oracle-corpus-diff-fna3d.sh` |
| W246 | Parity | verify `docs/fna3d-parity-report.md` | — |
| W247 | **RESERVE** | — | — |

### Q4 (W248–W260) — Portfolio decisions

| W | Topic | Material | Output |
|---|---|---|---|
| W248 | Tier B blitz 1 | `SOKOL`, `DILIGENT` | Half a page each |
| W249 | Tier B blitz 2 | `MAGNUM`, `DIRECT2D` | — |
| W250 | Tier B blitz 3 | `GDI`, `FREEDIRECT`, `PORTABLEGL` | — |
| W251 | Costs I | `SKIA` (32,324 lines), `LLGL` (20,840 lines) | Maintenance cost in hours |
| W252 | Costs II | `DIRECTX1`–`DIRECTX8`, `DIRECTX10` (~35,000 lines) | — |
| W253 | Costs III | `GLIDE`, `BLEND2D`, `WICKED`, `OPENVG` | — |
| W254 | Measurement | CI time, build time, test count per renderer | Cost table |
| W255 | **DECISION** | maintain / freeze / remove | Binding record in `FUTURE.md` |
| W256 | Implementation 1 | CI and build matrix | Commit |
| W257 | Implementation 2 | documentation and registries | Commit |
| W258 | Implementation 3 | remove or freeze code | Commit |
| W259 | Verification | identity count, tests, and documents agree | `scripts/check_renderer_identities.py` |
| W260 | **RESERVE** | year-end review + **halfway point** | `study/rok5-zaver.md` |

---
## Year 6 (W261–W312) — Content: `content`, XNB, glTF/CNJ, Textures, Fonts

> Goal for the year: be able to load any supported asset, know exactly why it fails, and
> have a glTF importer I trust (today, `cna-gltf-viewer` displays many assets incorrectly).

### Q1 (W261–W273) — ContentManager and XNB fundamentals

| W | Topic | Material | Output |
|---|---|---|---|
| W261 | `ContentManager` I | paths, cache, lifetime | — |
| W262 | `ContentManager` II | `Load<T>`, `ResourceContentManager` | — |
| W263 | Reader layer | `ContentReader`, `ContentTypeReader`, `ContentTypeReaderManager` | — |
| W264 | CNA extensions | `LooseFileContentTypeReader`, `ContentManifestEntry` | Mark as CNAEXT surface |
| W265 | Readable failure | `KnownUnsupportedContentTypeReader` | Error-message policy |
| W266 | XNB header | `XnbHeader`, version, flags | Manual byte analysis |
| W267 | Decompression | `XnbDecompression`, `LzxDecoder` | Hardest code in the module |
| W268 | Type table | `XnbTypeName`, `XnbTypeReaderTable` | — |
| W269 | Readers I | primitive + collection | — |
| W270 | Readers II | math + `Curve` | — |
| W271 | Readers III | `Texture2D`/`Texture3D`/`TextureCube` | — |
| W272 | Readers IV | `SpriteFont` | — |
| W273 | **RESERVE** | — | — |

### Q2 (W274–W286) — Complete XNB and start CNJ/glTF

| W | Topic | Material | Output |
|---|---|---|---|
| W274 | Readers V | `Model` | — |
| W275 | Readers VI | stock effects | — |
| W276 | Readers VII | `SoundEffect`, `Song`, `Video` | — |
| W277 | Security | `XnbReadLimits`, `XnbArithmetic` | Test with corrupted files |
| W278 | Fuzzing | XNB inputs | First run; findings in `known_bugs.md` |
| W279 | Claim verification | `docs/xnb-content-pipeline-support.md`, `xnb.md` | — |
| W280 | Why CNJ | `cnj.md`—custom format | Decide whether to maintain it |
| W281 | CNJ | `CnjEnvelope`, `CnjSourceFile` | — |
| W282 | JSON | `CNA/Internal/Json.hpp`—custom parser | Limits and risks |
| W283 | glTF I | `GltfImportCore`: structure, `cgltf` | — |
| W284 | glTF II | buffers, accessors, primitives | — |
| W285 | glTF III | materials and textures | — |
| W286 | **RESERVE** | — | — |

### Q3 (W287–W299) — Complete glTF

| W | Topic | Material | Output |
|---|---|---|---|
| W287 | glTF IV | scene, node hierarchy → `ModelBone` | — |
| W288 | glTF V | skins and coordinate spaces | Historically the most error-prone area |
| W289 | glTF VI | animation | — |
| W290 | glTF VII | morph targets | — |
| W291 | Draco | optional compression, `find_package(draco)` | Verify behavior without the library |
| W292 | Open items | `gltfissues.md`, `docs/gltf-conformance.md` | Confirm/close every one |
| W293 | Reality | `cna-gltf-viewer` on the corpus | List of incorrectly displayed assets |
| W294 | glTF fix #1 | from W293 | Commit + fixture |
| W295 | glTF fix #2 | — | Commit + fixture |
| W296 | glTF fix #3 | — | Commit + fixture |
| W297 | Corpus | `GltfFixtureCorpus`, `GltfOracleEXT`, `GltfBufferOracleEXT` | Extend |
| W298 | Boundaries | how far to take glTF (it is outside the XNA 4.0 API) | Decision in `FUTURE.md` |
| W299 | **RESERVE** | — | — |

### Q4 (W300–W312) — Textures, fonts, loading performance

| W | Topic | Material | Output |
|---|---|---|---|
| W300 | DXT | `DxtUtil`—decoder line by line | My own reference implementation for testing |
| W301 | BC7 | `Bc7Util` | — |
| W302 | Images | `ImageLoader`, `stb_image`—formats and limits | — |
| W303 | Conversion | `ImageData` and format conversions | — |
| W304 | Streams | `docs/texture-stream-formats.md` | Verified |
| W305 | Text layout | `SpriteFont`: kerning, glyph maps, line spacing | — |
| W306 | Models | `docs/model-content-pipeline-support.md` | — |
| W307 | Threads | what in `content` is thread-safe | Honest record |
| W308 | Measurement | typical asset load time | Baseline |
| W309 | Optimization #1 | allocations during loading | Commit + measurement |
| W310 | Optimization #2 | I/O and buffering | Commit + measurement |
| W311 | Own audit | `content` module (32,952 lines) | List of findings |
| W312 | **RESERVE** | year-end review | `study/rok6-zaver.md` |

---

## Year 7 (W313–W364) — Audio, Media, Input, Devices, Network, Services

> Goal for the year: understand the non-gameplay subsystems and **trim them down to what
> actually works**. Many of them are currently more ambitious than is maintainable.

### Q1 (W313–W325) — Audio (31,622 lines)

| W | Topic | Material | Output |
|---|---|---|---|
| W313 | Architecture | SDL3_mixer vs. FAudio backends | Decision table |
| W314 | Foundation | `SoundEffect`, `SoundEffectInstance` | — |
| W315 | Streaming | `DynamicSoundEffectInstance` | — |
| W316 | 3D audio | `AudioEmitter`, `AudioListener`, attenuation and panning | Verify math against XNA |
| W317 | XACT I | `AudioEngine` | — |
| W318 | XACT II | `WaveBank` | — |
| W319 | XACT III | `SoundBank`, `Cue` | — |
| W320 | XACT IV | `AudioCategory`, instance limits, fades | — |
| W321 | Recording | `Microphone` | — |
| W322 | Verification | `docs/cna_audio_deep_audit_2026-07-17.md` | What from the audit still applies |
| W323 | Latency | measurement | Baseline |
| W324 | Fixes | two | Commits |
| W325 | **RESERVE** | — | — |

### Q2 (W326–W338) — Media and input

| W | Topic | Material | Output |
|---|---|---|---|
| W326 | Music | `Song`, `MediaPlayer`, `MediaQueue` | — |
| W327 | Video | `VideoPlayer` + FFmpeg decoding | — |
| W328 | Colors | custom YUV→RGBA conversion (without libswscale) | Verify correctness against a reference |
| W329 | Library | `MediaLibrary` and metadata | Decide what makes sense |
| W330 | Keyboard | `Keyboard`, `Keys` mapping | — |
| W331 | Mouse | `Mouse`, `MouseState` | — |
| W332 | Gamepad I | state, dead zones | — |
| W333 | Gamepad II | vibration, capabilities, SDL mapping | — |
| W334 | Touch | `TouchPanel`, gestures | — |
| W335 | Fidelity | `docs/input-fna-fidelity.md`, `input-member-parity-matrix.md` | Verified |
| W336 | Outside XNA | `input_noxna.md` | Mark extensions |
| W337 | Latency and fixes | measure input latency | Commits |
| W338 | **RESERVE** | — | — |

### Q3 (W339–W351) — Devices and services

| W | Topic | Material | Output |
|---|---|---|---|
| W339 | Architecture | `docs/devices-native-backend-design.md` | — |
| W340 | Sensors I | `Accelerometer`, `SensorBase`, `ISensorReading` | — |
| W341 | Sensors II | `TODO.md` roadmap—what exists vs. what is planned | Realistic list |
| W342 | Camera | `docs/cna-devices-camera-design.md` | — |
| W343 | Events | event contract, thread safety | — |
| W344 | Android | `docs/devices-android.md` | Verify on a real device |
| W345 | Benchmark | `docs/devices-benchmark-baseline.jsonl` | Remeasure |
| W346 | `devices-ext` | what goes beyond XNA | — |
| W347 | Boundaries | how far to go with hardware | Decision |
| W348 | GamerServices I | profile, avatar (18,395 lines) | — |
| W349 | GamerServices II | achievements, presence | — |
| W350 | Stubs | what only pretends to work | Honest record in the docs |
| W351 | **RESERVE** | — | — |

### Q4 (W352–W364) — Network and trimming

| W | Topic | Material | Output |
|---|---|---|---|
| W352 | Model | `NetworkSession` (16,680 lines) | — |
| W353 | Participants | host/client/`NetworkGamer` | — |
| W354 | Data | packets and serialization | — |
| W355 | Transport | transport implementation | — |
| W356 | Reality | two-process test | What actually works |
| W357 | Boundaries | networking scope | Decision |
| W358 | Revisit `storage` | after a year of platform experience | Additions |
| W359 | Trimming plan | what to remove completely | List |
| W360 | Trimming #1 | — | Commit |
| W361 | Trimming #2 | — | Commit |
| W362 | Audit A | `audio`+`media`+`input` | Report |
| W363 | Audit B | `devices`+`net`+`gamer-services` | Report |
| W364 | **RESERVE** | year-end review | `study/rok7-zaver.md` |

---

## Year 8 (W365–W416) — Quality: Tests, Sanitizers, Fidelity, Bug Elimination

> Goal for the year: make "green build" mean "correct build." Today it does not—audits
> have repeatedly found tasks marked complete merely because code existed.

### Q1 (W365–W377) — Tests

| W | Topic | Material | Output |
|---|---|---|---|
| W365 | Test map | 419 files, 144,584 lines | What covers what |
| W366 | Coverage | `docs/coverage.md`, gcov/llvm-cov | Actual numbers |
| W367 | Gaps | untested public API | List |
| W368 | Rules | `CHECKLIST.md` test requirements vs. reality | Difference |
| W369 | Additions #1 | `math`/`core`/`runtime` | Commits |
| W370 | Additions #2 | `graphics` states and resources | Commits |
| W371 | Additions #3 | `content` | Commits |
| W372 | Additions #4 | `audio`/`input` | Commits |
| W373 | Golden corpus | extend with uncovered scenes | Commits |
| W374 | Speed | parallelization, suite splitting | Measured |
| W375 | Unstable tests | identify flaky tests | Fixed or marked |
| W376 | Simplification | test infrastructure | Commit |
| W377 | **RESERVE** | — | — |

### Q2 (W378–W390) — Sanitizers and resilience

| W | Topic | Material | Output |
|---|---|---|---|
| W378 | ASan | pass through all modules | List of findings |
| W379 | UBSan | pass | List of findings |
| W380 | TSan | game loop + audio threads | List of findings |
| W381 | LSan | leaks; classify external ones | Report |
| W382 | valgrind | where the renderer supports it | — |
| W383 | Fuzzing | XNB | Findings |
| W384 | Fuzzing | glTF/CNJ | Findings |
| W385 | Fuzzing | images and fonts | Findings |
| W386 | Policy | behavior with corrupted data | Unified rule |
| W387 | Policy | behavior on device loss | Unified rule |
| W388 | Policy | OOM and large assets | Unified rule |
| W389 | Fixes | from the entire quarter | Commits |
| W390 | **RESERVE** | — | — |

### Q3 (W391–W403) — Fidelity to FNA

| W | Topic | Material | Output |
|---|---|---|---|
| W391 | Methodology | systematic CNA vs. FNA diff | Script |
| W392 | Diff | `Framework` root | Report |
| W393 | Diff | `Graphics` I | Report |
| W394 | Diff | `Graphics` II | Report |
| W395 | Diff | `Content` | Report |
| W396 | Diff | `Audio` | Report |
| W397 | Diff | `Input` + `Media` | Report |
| W398 | Classification | bug / intent / C++ necessity | Table |
| W399 | `CHECKLIST.md` | update deviation table | Commit |
| W400 | `AUDIT.md` | bring it in line with reality | Commit |
| W401 | Fidelity fix #1 | — | Commit |
| W402 | Fidelity fix #2 | — | Commit |
| W403 | **RESERVE** | — | — |

### Q4 (W404–W416) — Eliminate `known_bugs.md`

| W | Topic | Material | Output |
|---|---|---|---|
| W404 | Inventory | all of `known_bugs.md` | Close dead items |
| W405 | Reproduction | every open bug | Reproduction test |
| W406 | Fix 1 | one bug | Commit + test |
| W407 | Fix 2 | — | Commit + test |
| W408 | Fix 3 | — | Commit + test |
| W409 | Fix 4 | — | Commit + test |
| W410 | Fix 5 | — | Commit + test |
| W411 | Fix 6 | — | Commit + test |
| W412 | Fix 7 | — | Commit + test |
| W413 | Fix 8 | — | Commit + test |
| W414 | Release gate | define the "green" state | Document |
| W415 | Automation | release gate in CI | Commit |
| W416 | **RESERVE** | year-end review | `study/rok8-zaver.md` |

---
## Year 9 (W417–W468) — Performance, Memory, Threads, Architectural Cleanup

> Goal for the year: make CNA measurably faster and smaller. No optimization without
> before-and-after numbers.

### Q1 (W417–W429) — Measurement

| W | Topic | Material | Output |
|---|---|---|---|
| W417 | What to measure | frame time, load time, RAM, binary size, build time | Metric definitions |
| W418 | Harness | benchmark infrastructure | New tool |
| W419 | Baseline | all Tier A renderers | Measured table |
| W420 | Profilers | perf, callgrind, custom instrumentation | Scripts |
| W421 | 2D profile | typical 2D scene | Report |
| W422 | 3D profile | typical 3D scene | Report |
| W423 | Content profile | asset loading | Report |
| W424 | Memory | verify and remeasure `RAM.md` | Update |
| W425 | Allocations | what allocates in the hot loop | List |
| W426 | Size | binary size and build time as metrics | Baseline |
| W427 | Analysis | top 10 opportunities | Ranked list |
| W428 | Plan | optimizations for Q2–Q3 | Plan |
| W429 | **RESERVE** | — | — |

### Q2 (W430–W442) — CPU and build

| W | Topic | Material | Output |
|---|---|---|---|
| W430 | SpriteBatch | allocations and copies | Commit + numbers |
| W431 | Batching | sorting and merging batches | Commit + numbers |
| W432 | Streaming | vertex/index buffer | Commit + numbers |
| W433 | Math | only where the impact is measured | Commit + numbers |
| W434 | Loading | `content` hot path | Commit + numbers |
| W435 | Strings | logging and formatting | Commit + numbers |
| W436 | Data | containers and memory layout | Commit + numbers |
| W437 | Build | PCH, unity build, forward declarations | Measured |
| W438 | Linking | what can be eliminated | Measured |
| W439 | Verification | remeasure everything from W419 | Comparison table |
| W440 | Protection | performance regressions in CI | Commit |
| W441 | Record | results in `docs/` | Document |
| W442 | **RESERVE** | — | — |

### Q3 (W443–W455) — GPU and threads

| W | Topic | Material | Output |
|---|---|---|---|
| W443 | States | reduce state changes | Commit + numbers |
| W444 | Draw calls | merge | Commit + numbers |
| W445 | Uploads | staging and mapping | Commit + numbers |
| W446 | Cache | shader and pipeline cache across backends | Commit |
| W447 | Barriers | Vulkan/D3D12 render-target transitions | Commit |
| W448 | Promises | what CNA claims about thread safety | Truthful record |
| W449 | Threads | background content loading | Prototype |
| W450 | Threads | audio vs. game loop | Verification |
| W451 | Threads | renderer command recording | Where possible |
| W452 | Measurement | after GPU optimizations | Table |
| W453 | Boundaries | how far to go with multithreading | Decision |
| W454 | Documentation | performance characteristics | Document |
| W455 | **RESERVE** | — | — |

### Q4 (W456–W468) — Architecture

| W | Topic | Material | Output |
|---|---|---|---|
| W456 | Module boundaries | `scripts/check_module_link_closure.py` | Report |
| W457 | Cycles | dependencies to break | Commit |
| W458 | CNAEXT inventory | `CNAEXT.md` vs. code | Complete list of extensions |
| W459 | CNAEXT fate | what to promote, what to remove | Decision + commits |
| W460 | Accidentally public | what should not be in the public API | Commit |
| W461 | Hiding | what belongs in implementation details | Commit |
| W462 | sharp-runtime | what to return, what to finish | Commit in sharp-runtime |
| W463 | Refactor #1 | from W427/W456 | Commit |
| W464 | Refactor #2 | — | Commit |
| W465 | Refactor #3 | — | Commit |
| W466 | Dead code | find and remove | Commit |
| W467 | Record | "CNA after nine years" | Architecture document |
| W468 | **RESERVE** | year-end review | `study/rok9-zaver.md` |

---

## Year 10 (W469–W520) — Samples, Documentation, Platforms, 1.0

> Goal for the year: release **CNA 1.0** and have a plan for the next decade.

### Q1 (W469–W481) — XNA sample campaign (FUTURE.md Phase 3)

| W | Topic | Material | Output |
|---|---|---|---|
| W469 | Corpus | list of original XNA samples; what to port | Plan for 10 samples |
| W470 | Sample 1 | 2D fundamentals | Runs on all Tier A renderers |
| W471 | Sample 2 | sprites and fonts | — |
| W472 | Sample 3 | input and game loop | — |
| W473 | Sample 4 | 3D fundamentals + `BasicEffect` | — |
| W474 | Sample 5 | models and animation | — |
| W475 | Sample 6 | render targets and post-processing | — |
| W476 | Sample 7 | sound and music | — |
| W477 | Sample 8 | content and loading | — |
| W478 | Sample 9 | more complex scene (skinning, shadows) | — |
| W479 | Sample 10 | small complete game | — |
| W480 | Summary | what the samples revealed | New items in `known_bugs.md` |
| W481 | **RESERVE** | — | — |

### Q2 (W482–W494) — Documentation

| W | Topic | Material | Output |
|---|---|---|---|
| W482 | Inventory | 150+ documents in `docs/` | What is dead |
| W483 | Archiving | historical documents to `docs/archive/` | Commit |
| W484 | Doxygen | completeness of public API comments | Commit |
| W485 | README | rewrite to match reality | Commit |
| W486 | Migration | XNA/MonoGame → CNA | New document |
| W487 | Tutorial 1 | first game | — |
| W488 | Tutorial 2 | 3D scene | — |
| W489 | Tutorial 3 | custom effect | — |
| W490 | Renderer guide | expand the document from W155 | — |
| W491 | Plans | consolidate `plan*.md` (66,720 lines) | Maintainable state |
| W492 | `AUDIT.md` | final form | Commit |
| W493 | Presentation | project website/page | — |
| W494 | **RESERVE** | — | — |

### Q3 (W495–W507) — Platforms and release candidates

| W | Topic | Material | Output |
|---|---|---|---|
| W495 | Linux | release build + package | Artifact |
| W496 | Windows | release build | Artifact |
| W497 | macOS | release build (and finally test `METAL` for real) | Artifact |
| W498 | Android | release build | Artifact |
| W499 | Web | Emscripten release | Artifact |
| W500 | CI matrix | all platforms | Commit |
| W501 | Versioning | semantic versioning and ABI policy | Document |
| W502 | Licenses | verify `LICENSE`, `NOTICE.md`, `THIRD_PARTY_NOTICES.md` | Commit |
| W503 | **RC1** | release candidate 1 | Tag |
| W504 | RC1 bugs | — | Commits |
| W505 | **RC2** | release candidate 2 | Tag |
| W506 | RC2 bugs | — | Commits |
| W507 | **RESERVE** | — | — |

### Q4 (W508–W520) — 1.0 and the next decade

| W | Topic | Material | Output |
|---|---|---|---|
| W508 | **CNA 1.0** | release | Tag + announcement |
| W509 | Post-release | bugs | Commits |
| W510 | Post-release | bugs | Commits |
| W511 | Feedback | triage issues | — |
| W512 | Maintenance | what to do every week indefinitely | Document |
| W513 | Retrospective I | cost and benefit of the machine-generated start | Essay |
| W514 | Retrospective II | what I would do differently | Essay |
| W515 | Extraction | what from CNA could become a separate library | Proposal |
| W516 | sharp-runtime | as a standalone product? | Decision |
| W517 | Next decade | plan proposal | Draft |
| W518 | Next decade | completion | `plan_study_2.md` |
| W519 | Archiving | long-term preservation of the project and notes | — |
| W520 | **RESERVE** | decade-end review | `study/dekada-zaver.md` |

---

## 4. How to Change the Plan

The plan is binding in **sequence** and flexible in **detail**. Rules for changes:

1. **Do not rearrange years.** Their order follows dependencies: renderers only after
   `Graphics`, optimization only after tests, and tests only after understanding. A skipped
   year returns as debt.
2. **Weeks within a quarter may be swapped** when reality requires it (unavailable hardware,
   broken build).
3. **A finding displaces the plan.** When reading reveals a serious bug, fixing it takes
   priority over the next chapter—but record that the week moved.
4. **Review the plan once a year**, in the final reserve week of the year. Rewrite the
   remaining years based on what is actually known. Ten years cannot be planned precisely;
   the direction can be planned.
5. If something (typically Tier C renderers or `net`/`gamer-services`) turns out to need
   removal, **remove the corresponding weeks instead of replacing them**. A smaller project
   is a better outcome than a fully read project.

## 5. Success Criteria

After ten years, all of these statements must be true—each is verifiable, not subjective:

- I can open any file in `modules/` and explain within a minute what it does and who calls it.
- `known_bugs.md` contains only bugs I know about and have consciously left unfixed.
- `AUDIT.md` and `docs/` describe reality, not intent.
- Every public method has a test that would fail if the method broke.
- The number of maintained renderers is a number I can defend.
- A `1.0` release exists, and someone other than me has written a game with it.

