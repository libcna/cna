# Runtime Graphics Renderer Selection — Implementation Plan

> **Created 2026-08-14** on branch `feature/graphicsruntimedispatch`, at the project owner's
> request, as a pure analysis pass — no source file was modified to produce it. Every count,
> line reference and structural claim below was measured against the tree at that point, not
> estimated.
>
> **Status legend** (this project's own convention, matching `plan_svg_dom.md` /
> `plan_html_dom.md`): ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Goal

Keep the current **compile-time** renderer selection as the default and recommended mode — it is
simpler, produces the smallest binary, and lets the compiler eliminate every unused renderer.

Add a **second, opt-in mode** in which CNA is compiled with *several* renderers linked in, and the
concrete renderer is chosen at **runtime through a new CNA API, before CNA is started**. Once CNA
has begun using the selected renderer, any attempt to change the selection throws.

The owner further specified the failure policy: if the selected renderer is unavailable or its
initialization fails, the **default behaviour is to throw**. A new CNA API must allow opting in to
a **fallback renderer chain** instead.

---

## 1. What exists today

Renderer selection is compile-time and resolved in three layers.

| Layer | Mechanism | Location |
|---|---|---|
| CMake | `CNA_GRAPHICS_RENDERER` (single value) resolves to `RENDERER_DIR`, `RENDERER_TARGET`, a **global** `add_compile_definitions(CNA_RENDERER_<X>)`, and `CNA_RENDERER_DEFINE` | `cmake/RendererSelection.cmake` (870 lines) |
| C++ identity | `CNA::getCurrentGraphicsRendererType()` / `CNA::getCurrentGraphicsRendererName()` — a `constexpr` `#elif` chain over those macros | `modules/core/include/CNA/GraphicsRendererType.hpp` |
| Construction | `CNA::Internal::Renderers::CreateGraphicsRenderer(const GraphicsRendererCreateArgs&)` — **one** free function, declared once and defined once per renderer family | declared `IGraphicsRenderer.hpp:2078`; defined in `modules/renderers/<family>/src/…` |

Current registry sizes, measured:

- **46 public renderer identities** (`GraphicsRendererType` enum entries = 46; `scripts/check_renderer_identities.py` `IDENTITIES` = 46 — its module docstring still says "42" and is stale).
- **42 implementation families** under `modules/renderers/` (EasyGL alone serves 5 GL identities: `OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`).
- All 42 families define `CreateGraphicsRenderer` exactly once (Metal's lives in `MetalRenderer.mm`).
- ~148 700 lines of renderer implementation source in total; the largest single families are `vulkan` (11 824), `webgpu` (10 394), `sdl-gpu` (7 347), `easygl` (7 272).

### The good news

`IGraphicsRenderer` (`modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp:1322`)
is already a wide virtual interface. **Everything that happens after construction is already
runtime-polymorphic** — drawing, capability queries, format negotiation, state application.
Runtime renderer selection therefore does *not* require rewriting the rendering path.

Every family also already lives in its own namespace (`…Renderers::Vulkan`,
`…Renderers::DirectX9`, `…Renderers::OpenGL1`, …), verified across all 42 — so there are **no
class-name collisions** between families.

---

## 2. The single hard blocker

`CreateGraphicsRenderer` has an **identical signature in every family**. Linking two renderer
archives into one binary is a duplicate-symbol error. This one symbol — not the rendering
architecture — is what makes multi-renderer builds impossible today.

---

## 3. Where the compile-time choice actually leaks

Measured occurrences of `CNA_RENDERER_*` outside the renderer modules themselves:

| Area | Occurrences | Notes |
|---|---|---|
| `modules/graphics/src` (production) | **48** in 6 files | the real work |
| `modules/graphics/tests` | 335 | per-renderer test gating |
| `modules/graphics/examples` | 557 | per-renderer example gating |
| `modules/content/tests` | 16 | XNB/CNJ capability gating |
| `modules/core` | 47 | 46 of them *are* the `GraphicsRendererType.hpp` switch itself; 1 stale `CNA_RENDERER_SDL` in `Entrypoint.hpp:22` (not a real identity) |
| CMake (`modules/**/CMakeLists.txt`) | 86 conditions | plus **128** references to the scalar `RENDERER_TARGET` across 40+ files |

Production code is far cleaner than the raw totals suggest:

| File | Count | Nature |
|---|---|---|
| `modules/graphics/src/Xna/GraphicsDevice.cpp` | 25 | ★ the core of the problem |
| `modules/graphics/src/Xna/Texture2D.cpp` | 9 | `SKIA` format promotion, `DIRECTX9` profile ceilings |
| `modules/graphics/src/Xna/TextureCube.cpp` | 4 | `DIRECTX9` profile ceilings |
| `modules/graphics/src/Xna/Texture3D.cpp` | 4 | `DIRECTX9` profile ceilings |
| `modules/graphics/src/Xna/GraphicsAdapter.cpp` | 4 | `DIRECTX9` real `D3DCAPS9` queries |
| `modules/graphics/src/Xna/RenderTarget2D.cpp` | 2 | `SKIA` format promotion |

These fall into three categories of very different difficulty.

### (a) Decisions that must be made *before* the renderer object exists — architecturally hardest

`getRendererWindowFlags()` (`GraphicsDevice.cpp:138`, consumed at `:2309`) must know the SDL window
flags (`SDL_WINDOW_OPENGL` / `SDL_WINDOW_VULKAN` / `SDL_WINDOW_METAL`) **before `SDL_CreateWindow`**,
i.e. before any `IGraphicsRenderer` instance exists. The same applies to:

- `OPENGL1`'s `SDL_GL_SetAttribute` block (depth/stencil/MSAA fixed at window-creation time on GLX).
- Skipping `SDL_INIT_VIDEO` entirely for `HEADLESS`/`SOFTWARE`/`STUB`/`PORTABLEGL` (`GraphicsDevice.cpp:317`).
- The "never create a window at all" branch in `createOrAttachWindow()` (`GraphicsDevice.cpp:2270`).

A virtual method on the instance cannot serve this. It needs a **static, per-renderer descriptor**
consulted before construction.

**The precedent already exists.** `BGFX`, `LLGL`, `FNA3D` and `DILIGENT` already make this decision
*at runtime* today — `Bgfx::Detail::ResolveRendererType()`, `Llgl::Detail::RendererModuleNeedsOpenGLWindow()`,
`Fna3d::Detail::PrepareWindowFlags()`, `Diligent::ParseDeviceTypeOverride()`. They are simply
reached through `#ifdef` instead of through a function pointer. Runtime dispatch generalizes a
pattern this codebase already runs in production, rather than inventing one.

### (b) Renderer-specific behaviour embedded in XNA types

`DIRECTX9` `GraphicsProfile` texture-size ceilings and `D3DCAPS9`-backed
`GraphicsAdapter::IsProfileSupported()`/`QueryRenderTargetFormat()`; `SKIA` surface-format
promotion in `Texture2D`/`RenderTarget2D`; `GDI`'s multisample write-back in
`SetPresentationParameters()`/`createRenderer()`.

These belong behind virtuals on `IGraphicsRenderer`. The pattern is already established there —
`GetAppliedBackBufferFormatEXT()`, `GetAppliedDepthStencilFormatEXT()`, `SupportsCapability()`,
`ApplyMultiSampleCount()` all exist precisely to keep renderer-specific truth out of the XNA layer.

### (c) Identity reporting — the one visible API break

`GraphicsDevice::GetGraphicsRendererType()` (`GraphicsDevice.hpp:989`) is `inline constexpr`, as is
`CNA::getCurrentGraphicsRendererType()`. In a multi-renderer build it cannot stay `constexpr`.
There is at least one real `static_assert` over it in the tree
(`modules/renderers/fna3d/examples/fna3d_smoke_test.cpp:74`), plus `GraphicsRendererCompileDefinitionTests.cpp:172`
asserting that exactly one `CNA_RENDERER_*` macro is defined.

---

## 4. Proposed architecture

### Layer 1 — Per-family descriptor and factory

Rename the colliding symbol into the family namespace
(`CNA::Internal::Renderers::<Family>::CreateGraphicsRenderer`) and add a static descriptor that
answers everything `GraphicsDevice` needs *before* construction:

```cpp
namespace CNA::Internal::Renderers
{
    /** Pre-construction contract of one renderer family. */
    struct GraphicsRendererDescriptor
    {
        GraphicsRendererType type;
        std::string_view     name;                 // "VULKAN", matches CNA_GRAPHICS_RENDERER

        bool                 needsWindow;          // HEADLESS/SOFTWARE/STUB/PORTABLEGL: false
        bool                 needsVideoSubsystem;  // controls the SDL_INIT_VIDEO call

        // Runtime-decided window flags (bgfx/llgl/fna3d/diligent already do this today);
        // constant-flag renderers get a trivial implementation.
        std::uint32_t      (*prepareWindowFlags)();

        // Attributes that must be set before SDL_CreateWindow (OPENGL1's GLX visual).
        void               (*applyPreWindowAttributes)(const RendererPreWindowRequest&);

        // Cheap, side-effect-free availability probe (loader present, device enumerable).
        bool               (*isAvailable)();

        std::unique_ptr<IGraphicsRenderer> (*create)(const GraphicsRendererCreateArgs&);
    };
}
```

### Layer 2 — CMake-generated registry, not static-init self-registration

**Deliberate decision: do not use static-initializer self-registration.** In a static library the
linker discards object files no one references, so a self-registering renderer TU silently
vanishes unless the whole archive is force-linked (`--whole-archive`/`OBJECT` libraries). That is
a well-known footgun and it interacts badly with this project's existing declared static-library
cycles (`cna_graphics_core` ↔ renderer archives, see `modules/renderers/CMakeLists.txt`).

Instead, CMake generates one translation unit — `CnaRendererRegistry.generated.cpp` — containing an
explicit table of the enabled families. Deterministic, no static-init ordering, no linker flags.
In single-renderer mode the table has exactly one entry and behaviour is bit-for-bit what it is
today.

### Layer 3 — Selection API and the latch

```cpp
namespace CNA
{
    /** CNAEXT. Chooses which compiled-in graphics renderer CNA will use. */
    class GraphicsRendererSelection
    {
    public:
        static void SetPreferred(GraphicsRendererType type);       // throws once latched
        static void SetPreferred(std::string_view name);           // "VULKAN"

        static GraphicsRendererType GetSelected();                 // does not latch
        static bool IsLatched();

        static std::span<const GraphicsRendererDescriptor> GetAvailable();  // compiled-in set
    };
}
```

Details that matter:

- **The latch point is the start of `GraphicsDevice` construction**, not the first
  `CreateGraphicsRenderer` call — `createOrAttachWindow()` consumes the decision earlier than
  `createRenderer()` does.
- `GraphicsDevice::RecreateRendererForMultiSampleCount()` (`GraphicsDevice.cpp:2250`) calls
  `createRenderer()` again on a live device. The semantics must therefore be **"the selection can
  no longer change"**, not "the factory may run only once".
- Exception type: `System::InvalidOperationException` (exists in sharp-runtime) — the XNA-correct
  choice, more specific than `CNA::CNAException`.
- Precedence: explicit API call > `CNA_GRAPHICS_RENDERER` environment variable (following the
  existing `CNA_BGFX_RENDERER` / `CNA_DILIGENT_DEVICE` precedent) > compile-time default.

### Layer 4 — Failure policy and opt-in fallback (owner instruction)

**Default: hard failure.** If the selected renderer is not compiled in, reports itself unavailable,
or throws during initialization, CNA throws. No silent substitution — a game that asked for Vulkan
and got a CPU rasterizer without being told is a worse outcome than a clear error, and it matches
this project's standing refusal to fake capability.

**Opt-in: an explicit fallback chain.**

```cpp
namespace CNA
{
    class GraphicsRendererSelection
    {
    public:
        /** Enables fallback and defines the ordered chain tried after the preferred renderer. */
        static void SetFallbackChain(std::span<const GraphicsRendererType> chain);

        /** Convenience: try every compiled-in renderer, in descending capability order. */
        static void EnableAutomaticFallback(bool enabled);

        /** What actually got created, once latched — may differ from GetSelected(). */
        static GraphicsRendererType GetActive();

        /** Renderers that were tried and rejected, with the reason. Empty on a clean first hit. */
        static std::span<const GraphicsRendererFallbackRecord> GetFallbackHistory();
    };
}
```

Semantics:

- Fallback is **off** unless `SetFallbackChain()` or `EnableAutomaticFallback(true)` was called;
  both are subject to the same latch as `SetPreferred()`.
- Two distinct triggers, both covered: `isAvailable()` returning false (probe), and the family's
  `create()` throwing (real initialization failure).
- **Window-flag crossings bound what fallback can do.** SDL3 refuses a window carrying both
  `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN` (already documented for `DILIGENT` in
  `GraphicsDevice.cpp`). Falling back from a Vulkan-flagged renderer to a GL one therefore requires
  **destroying and recreating the SDL window**, which is only legal while `GraphicsDevice` owns it
  (`ownsWindow_`). With a caller-supplied `DeviceWindowHandle`, a cross-flag fallback must be
  refused with a clear error rather than silently attempted.
- Every fallback step must be reported through `CNA::Logger` at warning level and recorded in
  `GetFallbackHistory()`. Silent degradation is explicitly out of scope.
- If the whole chain is exhausted, throw — carrying the *first* failure as the primary cause and
  the rest as accumulated detail.

### Layer 5 — CMake

- `RENDERER_TARGET` (scalar, **128 references**) becomes a list, `CNA_RENDERER_TARGETS`.
- `add_compile_definitions(CNA_RENDERER_<X>)` is currently **global** — it must become
  `target_compile_definitions(<target> PRIVATE …)`, or the graphics core would see every renderer's
  macro at once in a multi build.
- New option `CNA_GRAPHICS_RENDERERS` (list) alongside the existing singular
  `CNA_GRAPHICS_RENDERER`, plus a global `CNA_MULTI_RENDERER` define for the multi mode.
- `scripts/check_renderer_identities.py` keeps its role unchanged: 46 identities stay 46. Runtime
  dispatch adds no public identity.

---

## 5. Which renderers can actually coexist

This is a genuine, and welcome, constraint on scope.

- **Platform gates already exclude most combinations.** The whole DirectX family is Windows-only;
  `CANVAS`/`HTML_DOM`/`SVG_DOM`/`WEBGL1`/`WEBGL2` are Emscripten-only; `METAL` is macOS-only.
  `RendererSelection.cmake` enforces this with hard `FATAL_ERROR`s today, and those gates carry
  over unchanged.
- **`PORTABLEGL` defines real global `gl*` C symbols** (`modules/renderers/portablegl/src/PortableGLImpl.cpp:9`,
  `#define PORTABLEGL_IMPLEMENTATION`). It collides at link time with any real OpenGL renderer and
  must stay mutually exclusive with them.
- **The 5 GL identities share one EasyGL target**, distinguished by the `CNA_GL_PROFILE_*` compile
  define. `OPENGLES2` and `OPENGL33` cannot coexist until EasyGL's profile becomes a runtime
  parameter — a separate sub-effort, not a prerequisite.
- **`GDI` re-compiles the `SOFTWARE` translation units** with `CNA_SOFTWARE_2D_ONLY`
  (`modules/renderers/software/CMakeLists.txt`). `GDI` + `SOFTWARE` in one build is an ODR
  violation and must be rejected at configure time.
- **Build cost is real.** Each added family brings its own third-party closure (bgfx, Wicked,
  Diligent, LLGL, Skia, Magnum, wgpu-native). A multi build is opt-in for good reason.

A realistic first Linux multi-set: `STUB + HEADLESS + SOFTWARE` (no window, no GPU dependency, no
third-party closure — ideal for tests), then `SDL_RENDERER + OPENGLES3 + VULKAN`, then the heavier
middleware families.

---

## 6. Test and example impact — the largest volume of work

892 `#ifdef CNA_RENDERER_*` sites across tests and examples, plus 86 CMake conditions. One test
explicitly asserts that exactly one renderer macro is defined
(`GraphicsRendererCompileDefinitionTests.cpp:172`).

**Recommendation: multi-renderer is a separate configuration, never the default.** Single-renderer
builds must not see a single changed macro — that is also the condition under which the 40+
existing renderer plan documents stay valid. The test corpus is converted to runtime gating
incrementally, per phase, not in one sweep.

---

## 7. Deliberate non-goals

- **No renderer switching on a live `GraphicsDevice`.** The window flags, and in several families
  the whole device/swap-chain, are fixed at window-creation time. This is what the latch enforces.
- **No dynamic loading of renderers from shared objects.** The set is fixed at link time; only the
  *choice* within that set is runtime.
- **No new public renderer identity.** 46 stays 46.
- **No change to the default build.** `-DCNA_GRAPHICS_RENDERER=<X>` keeps producing exactly the
  binary it produces today.

---

## Phases and tasks

Task IDs use the `RTR-` prefix. Tasks are listed in dependency order within each phase; phases are
ordered so that **phases 1–4 have standalone value and change no behaviour**, and only phases 5+
alter the build model.

*(Task tables follow in the next revision of this document.)*
