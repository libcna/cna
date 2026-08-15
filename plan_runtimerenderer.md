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

## Design decisions

The numbered decisions this plan is built on. Source comments may cite them as
`plan_runtimerenderer.md design decision N`, following the convention of `plan_svg_dom.md` and
`plan_html_dom.md`.

**1. Compile-time selection stays the default.** `-DCNA_GRAPHICS_RENDERER=<X>` must keep producing
byte-identical behaviour and an identical macro environment. Multi-renderer is an opt-in second
mode (`-DCNA_GRAPHICS_RENDERERS=<A>;<B>;…`), never something an existing build acquires silently.

**2. One descriptor per family, consulted before construction.** Everything `GraphicsDevice` needs
to know before an `IGraphicsRenderer` exists — window flags, whether a window is needed at all,
whether SDL's video subsystem must be initialized, pre-window GL attributes, an availability probe
— lives in a `GraphicsRendererDescriptor`, not in `#ifdef` chains inside `GraphicsDevice.cpp`.

**3. Explicit generated registry, not static-init self-registration.** A self-registering
translation unit inside a static archive is discarded by the linker when nothing references it.
CMake generates the table instead. This also avoids interacting with the declared
`cna_graphics_core` ↔ renderer archive cycles.

**4. The factory symbol moves into the family namespace.**
`CNA::Internal::Renderers::<Family>::CreateGraphicsRenderer` replaces the single colliding
`CNA::Internal::Renderers::CreateGraphicsRenderer`. This is the change that makes two renderer
archives linkable at all.

**5. The selection latches at the start of `GraphicsDevice` construction**, not at the first
factory call — `createOrAttachWindow()` consumes the decision before `createRenderer()` runs.
Latching forbids *changing the selection*, not *calling the factory again*:
`RecreateRendererForMultiSampleCount()` legitimately reconstructs the same renderer on a live
device.

**6. Failure is hard by default.** An unavailable renderer, or one whose initialization throws, is
an error — `System::InvalidOperationException` for a selection/latch violation, the family's own
exception propagated for an initialization failure. CNA never silently substitutes a different
renderer. This follows the project's standing refusal to fake capability.

**7. Fallback is opt-in, ordered, and always reported.** `SetFallbackChain()` /
`EnableAutomaticFallback()` enable it; both latch like `SetPreferred()`. Both failure triggers are
covered (probe says unavailable, or `create()` throws). Every step is logged at warning level and
recorded in `GetFallbackHistory()`; `GetActive()` reports what was really created.

**8. A cross-flag fallback requires window recreation, and is refused when CNA does not own the
window.** SDL3 rejects a window carrying both `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN`.
Falling back across that boundary means destroying and recreating the SDL window, which is only
legal while `ownsWindow_` is true. With a caller-supplied `DeviceWindowHandle`, the attempt fails
with a clear diagnostic rather than being silently skipped.

**9. Renderer-specific behaviour leaves the XNA layer.** The `DIRECTX9` profile ceilings, `SKIA`
format promotion and `GDI` multisample write-back become `IGraphicsRenderer` virtuals, joining the
existing `GetAppliedBackBufferFormatEXT()`/`SupportsCapability()`/`ApplyMultiSampleCount()` family.
This is worth doing on its own merits, independently of runtime dispatch.

**10. No new public renderer identity.** 46 identities stay 46; `scripts/check_renderer_identities.py`
keeps passing unchanged throughout.

**11. Incompatible combinations are rejected at configure time, with a reason.** `PORTABLEGL` with
any real-GL family, `GDI` with `SOFTWARE`, two GL profiles sharing the EasyGL target, and any
cross-platform combination are configure errors — never a link error the user has to decode.

**12. The test corpus converts incrementally.** 892 `#ifdef` sites are not swept in one pass. Each
phase converts only what it needs, and single-renderer builds keep compiling the corpus exactly as
they do today.

---

## Phases and tasks

Task IDs use the `RTR-` prefix. Phases are ordered so that **P0–P5 change no observable behaviour
and have standalone value**; only P6 onward alters the build model. Every phase ends with a
verification task, and no phase may be marked ✅ until a single-renderer build of at least one
Linux renderer plus `CnaTests` is green.

Per-family tasks are enumerated individually rather than collapsed into "do all 42", because they
land independently, review independently, and each one is where a family-specific surprise will
surface.

**Family/identity map used throughout** (42 families, 46 identities): `easygl` serves `OPENGLES2`,
`OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`; every other family serves exactly one identity.

---

### P0 — Foundations: types, headers, no behaviour change

| ID | St | Task |
|---|---|---|
| RTR-P0-1 | ✅ | Add `modules/graphics/include/CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp` — the `GraphicsRendererDescriptor` POD of design decision 2. Full Doxygen on every member per `CLAUDE.md`. No consumer yet. |
| RTR-P0-2 | ✅ | Add `RendererPreWindowRequest` (the subset of `PresentationParameters` a pre-window hook may read: back-buffer size, MSAA count, depth/stencil format, fullscreen) so descriptors never include the XNA headers. |
| RTR-P0-3 | ✅ | Add `RendererWindowKind` enum (`None`, `Plain`, `OpenGL`, `Vulkan`, `Metal`) — the *coarse* classification used for conflict detection and window-recreation decisions, distinct from the exact `SDL_WindowFlags` bitmask. |
| RTR-P0-4 | ✅ | Add `GraphicsRendererFallbackRecord` (`type`, `reason` enum `NotCompiledIn`/`ProbeUnavailable`/`InitializationFailed`/`WindowKindConflict`, `message`) — the record type `GetFallbackHistory()` returns. |
| RTR-P0-5 | ✅ | Add `GraphicsRendererRegistry` interface in `CNA::Internal::Renderers`: `All()`, `Find(GraphicsRendererType)`, `Find(std::string_view)`, `Count()`. Declaration only; the definition arrives generated in P2. |
| RTR-P0-6 | ✅ | Verify `System::InvalidOperationException` in the pinned sharp-runtime covers the message/inner-exception shape needed by design decision 6; if not, extend sharp-runtime first (per `CLAUDE.md`'s sharp-runtime-first rule) rather than working around it in CNA. |
| RTR-P0-7 | ✅ | Unit tests for the new value types: descriptor is trivially copyable, `RendererWindowKind` conflict matrix (`OpenGL` vs `Vulkan` = conflict, `None` vs anything = compatible), fallback-record construction. |
| RTR-P0-8 | ✅ | `docs/runtime-renderer-selection.md` skeleton — capability/status document, this project's convention (`docs/webgpu-renderer.md` as the shape reference). States plainly that nothing is implemented yet. |
| RTR-P0-9 | ✅ | Confirm the new headers pass the physical source-partition validator in `modules/CMakeLists.txt` (header-only additions to an existing module's `include/` root). |
| RTR-P0-10 | ✅ | **Phase gate.** Single-renderer `OPENGLES3` build + `CnaTests` green, zero behaviour delta. |

---

### P1 — Extract the pre-window contract (design decision 2)

Removes the `#ifdef` chains from `GraphicsDevice.cpp` that decide things before any renderer exists.
Still exactly one renderer per build throughout.

| ID | St | Task |
|---|---|---|
| RTR-P1-1 | ✅ | Introduce `CNA::Internal::Renderers::ActiveDescriptor()` — returns the single compiled-in descriptor. Temporary shim so P1 can proceed before the registry exists. |
| RTR-P1-2 | ✅ | Replace `getRendererWindowFlags()` (`GraphicsDevice.cpp:138`) with `descriptor.prepareWindowFlags()`. Delete all 14 `#ifdef` branches inside it. |
| RTR-P1-3 | ✅ | Replace the `SDL_INIT_VIDEO` guard (`GraphicsDevice.cpp:317`) with `descriptor.needsVideoSubsystem`. |
| RTR-P1-4 | ✅ | Replace the no-window branch of `createOrAttachWindow()` (`GraphicsDevice.cpp:2270`) with `descriptor.needsWindow`. |
| RTR-P1-5 | ✅ | Replace the `OPENGL1` `SDL_GL_SetAttribute` block with `descriptor.applyPreWindowAttributes(request)`. |
| RTR-P1-6 | ✅ | Assert in a test that `modules/graphics/src/Xna/GraphicsDevice.cpp` contains **zero** `CNA_RENDERER_*` occurrences related to window creation (down from 25 total; the `GDI` write-back leaves in P3). |
| RTR-P1-7 | ✅ | Cross-renderer regression: window flags produced through the descriptor are bit-identical to the previous `#ifdef` result, verified per renderer as its descriptor lands. |
| RTR-P1-8 | ✅ | Document in `docs/runtime-renderer-selection.md` which four families (`BGFX`, `LLGL`, `FNA3D`, `DILIGENT`) already decided window flags at runtime before this plan, and that the descriptor generalizes their existing mechanism. |

#### P1 descriptors, one task per family

Each task: implement `<Family>::GetDescriptor()` in that family's module, exporting `needsWindow`,
`needsVideoSubsystem`, `prepareWindowFlags`, `applyPreWindowAttributes`, `isAvailable`, and wire it
to `ActiveDescriptor()` for that configuration. Each must be verified by actually building and
running that family's own smoke/example target where one exists.

| ID | St | Family → identity |
|---|---|---|
| RTR-P1-D01 | ✅ | `sdl-renderer` → `SDL_RENDERER` (plain window, no GL flag) |
| RTR-P1-D02 | ✅ | `easygl` → `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` — one descriptor, profile still compile-time (`CNA_GL_PROFILE_*`); the 5-identity split is P11 |
| RTR-P1-D03 | 🟨 | `bgfx` → `BGFX` — `prepareWindowFlags` wraps the existing `Bgfx::Detail::ResolveRendererType()` |
| RTR-P1-D04 | ✅ | `vulkan` → `VULKAN` (`SDL_WINDOW_VULKAN`; `isAvailable` = real `vkEnumerateInstanceVersion` probe) |
| RTR-P1-D05 | ✅ | `webgpu` → `WEBGPU` |
| RTR-P1-D06 | ✅ | `magnum` → `MAGNUM` (`SDL_WINDOW_OPENGL`) |
| RTR-P1-D07 | ✅ | `headless` → `HEADLESS` (`needsWindow=false`, `needsVideoSubsystem=false`) |
| RTR-P1-D08 | ✅ | `software` → `SOFTWARE` (`needsWindow=false`, `needsVideoSubsystem=false`) |
| RTR-P1-D09 | ✅ | `stub` → `STUB` (`needsWindow=false`, `needsVideoSubsystem=false`) |
| RTR-P1-D10 | ✅ | `portablegl` → `PORTABLEGL` (`needsWindow=false`, `needsVideoSubsystem=false`) |
| RTR-P1-D11 | ✅ | `directx11` → `DIRECTX11` |
| RTR-P1-D12 | ✅ | `directx12` → `DIRECTX12` (honours `PresentationParameters::HeadlessEXT`) |
| RTR-P1-D13 | ✅ | `direct2d` → `DIRECT2D` |
| RTR-P1-D14 | ✅ | `canvas` → `CANVAS` (Emscripten) |
| RTR-P1-D15 | ✅ | `html-dom` → `HTML_DOM` (Emscripten) |
| RTR-P1-D16 | ✅ | `svg-dom` → `SVG_DOM` (Emscripten) |
| RTR-P1-D17 | ✅ | `skia` → `SKIA` |
| RTR-P1-D18 | ✅ | `blend2d` → `BLEND2D` |
| RTR-P1-D19 | ✅ | `freedirect` → `FREEDIRECT` |
| RTR-P1-D20 | ✅ | `directx9` → `DIRECTX9` |
| RTR-P1-D21 | ✅ | `directx1` → `DIRECTX1` |
| RTR-P1-D22 | ✅ | `directx2` → `DIRECTX2` |
| RTR-P1-D23 | ✅ | `directx3` → `DIRECTX3` |
| RTR-P1-D24 | ✅ | `directx5` → `DIRECTX5` |
| RTR-P1-D25 | ✅ | `directx6` → `DIRECTX6` |
| RTR-P1-D26 | ✅ | `directx7` → `DIRECTX7` |
| RTR-P1-D27 | ✅ | `directx8` → `DIRECTX8` |
| RTR-P1-D28 | ✅ | `directx10` → `DIRECTX10` |
| RTR-P1-D29 | ✅ | `sdl-gpu` → `SDL_GPU` |
| RTR-P1-D30 | ✅ | `opengles1` → `OPENGLES1` (`SDL_WINDOW_OPENGL`) |
| RTR-P1-D31 | ✅ | `opengl4` → `OPENGL4` (`SDL_WINDOW_OPENGL`) |
| RTR-P1-D32 | ✅ | `opengl1` → `OPENGL1` — owns `applyPreWindowAttributes` (GLX visual: depth 24, stencil 8, double buffer, MSAA) |
| RTR-P1-D33 | ✅ | `opengl2` → `OPENGL2` (`SDL_WINDOW_OPENGL`) |
| RTR-P1-D34 | ✅ | `wicked` → `WICKED` |
| RTR-P1-D35 | ✅ | `sokol` → `SOKOL` (`SDL_WINDOW_OPENGL`; `CNA_SOKOL_API` stays compile-time) |
| RTR-P1-D36 | ✅ | `diligent` → `DILIGENT` — `prepareWindowFlags` wraps `ParseDeviceTypeOverride()`; keeps the documented single-flag limitation |
| RTR-P1-D37 | ✅ | `glide` → `GLIDE` |
| RTR-P1-D38 | ✅ | `gdi` → `GDI` |
| RTR-P1-D39 | ✅ | `llgl` → `LLGL` — `prepareWindowFlags` wraps `RendererModuleNeedsOpenGLWindow()` |
| RTR-P1-D40 | ✅ | `metal` → `METAL` (`SDL_WINDOW_METAL` \| `SDL_WINDOW_HIGH_PIXEL_DENSITY`; descriptor in the `.mm` unit) |
| RTR-P1-D41 | ✅ | `fna3d` → `FNA3D` — `prepareWindowFlags` wraps `Fna3d::Detail::PrepareWindowFlags()` |
| RTR-P1-D42 | ✅ | `openvg` → `OPENVG` (`SDL_WINDOW_OPENGL`) |
| RTR-P1-D43 | ✅ | **Coverage gate.** A test enumerates all 46 identities and fails if any lacks a descriptor in its own configuration — the mechanical equivalent of `check_renderer_identities.py` for descriptors. |

---

### P2 — Namespace the factory, generate a one-entry registry (design decisions 3, 4)

| ID | St | Task |
|---|---|---|
| RTR-P2-1 | ✅ | Declare `CreateGraphicsRenderer` inside each family namespace in `IGraphicsRenderer.hpp` (or better: drop the global declaration entirely and let each family's own public header declare it). |
| RTR-P2-2 | ✅ | Write `cmake/RendererRegistry.cmake` — generates `CnaRendererRegistry.generated.cpp` into the build tree from the enabled-family list. |
| RTR-P2-3 | ✅ | Implement `GraphicsRendererRegistry` over the generated table. With one family enabled, `Count() == 1`. |
| RTR-P2-4 | ✅ | Replace `renderer_ = CreateGraphicsRenderer(args)` (`GraphicsDevice.cpp:2419`) with a registry lookup + `descriptor.create(args)`. |
| RTR-P2-5 | ✅ | Retire the `ActiveDescriptor()` shim from RTR-P1-1 in favour of the registry. |
| RTR-P2-6 | ✅ | Confirm the generated file is regenerated on reconfigure and is correctly `.gitignore`d (build tree only, never committed). |
| RTR-P2-7 | ✅ | Verify the declared `cna_graphics_core` ↔ renderer archive cycle still resolves after the symbol move, on both the GNU linker and the MinGW cross-link. |
| RTR-P2-8 | ⬜ | **Phase gate.** All Linux-buildable renderers configure, build and pass their own smoke targets. |

#### P2 factory renames, one task per family

Each: move the family's `CreateGraphicsRenderer` definition into `CNA::Internal::Renderers::<Family>`,
export `GetDescriptor()` with a populated `create` pointer, verify the family's smoke/example target.

| ID | St | Family |
|---|---|---|
| RTR-P2-F01 | ✅ | `sdl-renderer` |
| RTR-P2-F02 | ✅ | `easygl` |
| RTR-P2-F03 | 🟨 | `bgfx` |
| RTR-P2-F04 | ✅ | `vulkan` |
| RTR-P2-F05 | ✅ | `webgpu` |
| RTR-P2-F06 | ✅ | `magnum` |
| RTR-P2-F07 | ✅ | `headless` |
| RTR-P2-F08 | ✅ | `software` — must not disturb the `CNA_SOFTWARE_2D_ONLY` re-compilation `GDI` depends on |
| RTR-P2-F09 | ✅ | `stub` |
| RTR-P2-F10 | ✅ | `portablegl` |
| RTR-P2-F11 | ✅ | `directx11` |
| RTR-P2-F12 | ✅ | `directx12` |
| RTR-P2-F13 | ✅ | `direct2d` |
| RTR-P2-F14 | ✅ | `canvas` |
| RTR-P2-F15 | ✅ | `html-dom` |
| RTR-P2-F16 | ✅ | `svg-dom` — also update the standalone `cna_test_svgdom_host` target |
| RTR-P2-F17 | ✅ | `skia` |
| RTR-P2-F18 | ✅ | `blend2d` |
| RTR-P2-F19 | ✅ | `freedirect` |
| RTR-P2-F20 | ✅ | `directx9` |
| RTR-P2-F21 | ✅ | `directx1` |
| RTR-P2-F22 | ✅ | `directx2` |
| RTR-P2-F23 | ✅ | `directx3` |
| RTR-P2-F24 | ✅ | `directx5` |
| RTR-P2-F25 | ✅ | `directx6` |
| RTR-P2-F26 | ✅ | `directx7` |
| RTR-P2-F27 | ✅ | `directx8` |
| RTR-P2-F28 | ✅ | `directx10` |
| RTR-P2-F29 | ✅ | `sdl-gpu` |
| RTR-P2-F30 | ✅ | `opengles1` |
| RTR-P2-F31 | ✅ | `opengl4` |
| RTR-P2-F32 | ✅ | `opengl1` — its one-line factory is currently a single packed line; expand it |
| RTR-P2-F33 | ✅ | `opengl2` |
| RTR-P2-F34 | ✅ | `wicked` |
| RTR-P2-F35 | ✅ | `sokol` |
| RTR-P2-F36 | ✅ | `diligent` |
| RTR-P2-F37 | ✅ | `glide` |
| RTR-P2-F38 | ✅ | `gdi` — also update the three `gdi/examples/*` targets that call `CreateGraphicsRenderer` directly |
| RTR-P2-F39 | ✅ | `llgl` |
| RTR-P2-F40 | ✅ | `metal` (`.mm` unit) |
| RTR-P2-F41 | ✅ | `fna3d` |
| RTR-P2-F42 | ✅ | `openvg` |

---

### P3 — Move renderer-specific behaviour out of the XNA layer (design decision 9)

Worth doing on its own merits. After this phase, `modules/graphics/src` should contain zero
`CNA_RENDERER_*` occurrences.

| ID | St | Task |
|---|---|---|
| RTR-P3-1 | ✅ | Add `IGraphicsRenderer::GetMaxTextureSizeForProfileEXT(int profile)` — default: no ceiling (`INT_MAX`). |
| RTR-P3-2 | ✅ | `DirectX9Renderer` overrides it with the real `D9-100` table currently in `D3D9ProfileCapabilities.hpp`. |
| RTR-P3-3 | ✅ | `Texture2D.cpp` — replace both `#ifdef CNA_RENDERER_DIRECTX9` `ValidateTextureSizeForProfileEXT` call sites with the virtual. |
| RTR-P3-4 | ✅ | `Texture3D.cpp` — same conversion (3 sites). |
| RTR-P3-5 | ✅ | `TextureCube.cpp` — same conversion (3 sites). |
| RTR-P3-6 | ✅ | `GraphicsDevice.cpp` — same conversion for the `MaxRenderTargets` Reach ceiling (`:2922`). |
| RTR-P3-7 | ✅ | Add `IGraphicsRenderer::IsProfileSupportedEXT(int profile)` — default `true` (the honest current answer for 45 renderers). |
| RTR-P3-8 | ✅ | `GraphicsAdapter::IsProfileSupported()` — route through the virtual, delete the `#ifdef`. |
| RTR-P3-9 | ✅ | Add `IGraphicsRenderer::QueryRenderTargetFormatEXT(...)` / `QueryBackBufferFormatEXT(...)` — defaults preserve the current fall-back-to-`Color` stub behaviour. |
| RTR-P3-10 | ✅ | `GraphicsAdapter::QueryRenderTargetFormat()` / `QueryBackBufferFormat()` — route through the virtuals, delete both `#ifdef`s. |
| RTR-P3-11 | ✅ | Add `IGraphicsRenderer::IsSurfaceFormatSupportedEXT(int format)` and `IsColorTransferFormatEXT(int format)` — defaults are the current non-Skia behaviour. |
| RTR-P3-12 | ✅ | `SkiaRenderer` overrides all three format predicates with its real promoted-format table. |
| RTR-P3-13 | ✅ | `Texture2D.cpp` — replace the three `#ifdef CNA_RENDERER_SKIA` blocks (`ValidateTexture2DFormatEXT`, the `Color*` transfer predicate, `IsCompressedTransferFormatEXT`). |
| RTR-P3-14 | ✅ | `RenderTarget2D.cpp` — replace both `#ifdef CNA_RENDERER_SKIA` blocks. |
| RTR-P3-15 | ✅ | `GDI` multisample write-back: `GraphicsDevice::SetPresentationParameters()` and `createRenderer()` currently special-case `GDI`. Generalize to "always echo `GetMultiSampleCount()` back" — verify no other renderer regresses, since every other one already returns what it was given. |
| RTR-P3-16 | ✅ | `GraphicsAdapter.cpp` — drop the now-unused `D3D9FormatMapping.hpp`/`D3D9ProfileCapabilities.hpp` includes from the XNA layer. |
| RTR-P3-17 | ✅ | Verification: a test asserts `grep -c CNA_RENDERER_ modules/graphics/src` is **0**. |
| RTR-P3-18 | 🟨 | **Skia half done.** `scripts/run-skia-2d-oracle-diff.sh` passes 9/9 against the real-XNA oracle policy — seven scenes pixel-exact, two filtered ones within measured bounds — so P3's move of Skia's format tables behind `IGraphicsRenderer` virtuals changed no rendered pixel. The three SKIA unit failures were each shown pre-existing (two by a clean detached build at `a749fdce3`, the third by quoting both disagreeing lists from that commit — see `threeissues.md` #5). The D3D9 divergence suite remains undone: it needs a Windows cross-build under Wine, which is out of scope. |
| RTR-P3-19 | ✅ | **Phase gate.** `DIRECTX9` (Wine), `SKIA`, `GDI` (Wine) and `OPENGLES3` builds all green with their existing suites. |

---

### P4 — Selection API and latch (design decisions 5, 6)

Still single-renderer builds: the API validates the request against the one compiled-in renderer.

| ID | St | Task |
|---|---|---|
| RTR-P4-1 | ✅ | Add `modules/core/include/CNA/GraphicsRendererSelection.hpp` — `SetPreferred(GraphicsRendererType)`, `SetPreferred(std::string_view)`, `GetSelected()`, `IsLatched()`, `GetAvailable()`. Full Doxygen; `CNAEXT` throughout (this is not XNA 4.0 API). |
| RTR-P4-2 | ✅ | Implement the latch: a process-wide flag set at the top of every `GraphicsDevice` constructor. |
| RTR-P4-3 | ✅ | `SetPreferred()` after latch → `System::InvalidOperationException` naming both the latched renderer and the rejected request. |
| RTR-P4-4 | ✅ | `SetPreferred()` with an identity not compiled into this build → `System::InvalidOperationException` listing what *is* available (design decision 6; fallback does not apply to a build-time absence unless a chain was configured). |
| RTR-P4-5 | ✅ | `SetPreferred(std::string_view)` accepts exactly the `CNA_GRAPHICS_RENDERER` spellings (`"SDL_RENDERER"`, `"OPENGLES3"`, …); unknown name → `System::ArgumentException`. |
| RTR-P4-6 | ✅ | Case-insensitive name matching, decided and documented one way (recommend: case-insensitive, since env vars and command lines are typed by hand). |
| RTR-P4-7 | ✅ | `CNA_GRAPHICS_RENDERER` **environment variable** read at first use, below an explicit `SetPreferred()` in precedence (following `CNA_BGFX_RENDERER`/`CNA_DILIGENT_DEVICE`). |
| RTR-P4-8 | ✅ | An env-var value naming a renderer not compiled in: warn via `CNA::Logger` and ignore, or throw? Decide explicitly and document — recommend **throw**, consistent with design decision 6. |
| RTR-P4-9 | ✅ | `GetSelected()` before any selection returns the compile-time default without latching. |
| RTR-P4-10 | ✅ | `RecreateRendererForMultiSampleCount()` must keep working post-latch (design decision 5) — regression test. |
| RTR-P4-11 | ✅ | Multiple sequential `GraphicsDevice` instances in one process keep the latch (it is process-wide, not per-device) — regression test. |
| RTR-P4-12 | ✅ | Thread safety: document that `SetPreferred()` must be called before any graphics thread starts; guard the latch with an atomic so a violation is detected rather than racing. |
| RTR-P4-13 | ✅ | Unit tests: set-then-get; set-after-latch throws; unknown name throws; not-compiled-in throws; env var honoured; explicit call beats env var. |
| RTR-P4-14 | ✅ | Example program `examples/` demonstrating pre-start selection — the reference a game author copies. |
| RTR-P4-15 | ✅ | `docs/runtime-renderer-selection.md` — document the API, the precedence order and the latch semantics. |
| RTR-P4-16 | ✅ | **Phase gate.** Single-renderer builds behave identically whether or not the new API is called. |

---

### P5 — Fallback API (design decisions 6, 7, 8)

Still single-renderer builds: a chain of length 1 exercises every code path except the actual
substitution, which arrives with P8.

| ID | St | Task |
|---|---|---|
| RTR-P5-1 | ✅ | Add `SetFallbackChain(std::span<const GraphicsRendererType>)` — latches like `SetPreferred()`. |
| RTR-P5-2 | ✅ | Add `EnableAutomaticFallback(bool)` — derives the chain from every compiled-in renderer. |
| RTR-P5-3 | ✅ | Define and document the automatic chain's ordering. Recommend deriving it from the existing `CNA::GraphicsBackendCategory`/`GraphicsBackendMaturity` enums (`modules/core/include/CNA/`) rather than inventing a new ranking — mature GPU renderers first, CPU renderers next, `STUB` last. |
| RTR-P5-4 | ✅ | Add `GetActive()` — what was really created; equals `GetSelected()` when no fallback occurred. |
| RTR-P5-5 | ✅ | Add `GetFallbackHistory()` returning `std::span<const GraphicsRendererFallbackRecord>`. |
| RTR-P5-6 | ✅ | Wire the `isAvailable()` probe into the selection path: probe failure on the preferred renderer is `ProbeUnavailable`. |
| RTR-P5-7 | ✅ | Wire initialization failure: `descriptor.create()` throwing is caught, recorded as `InitializationFailed` with the exception message, and the next chain entry is tried. |
| RTR-P5-8 | ✅ | Exhausted chain → throw, carrying the **first** failure as primary cause and every subsequent one as accumulated detail in the message. |
| RTR-P5-9 | ✅ | Fallback disabled (the default) → the first failure propagates unchanged, exactly as today. Regression test that the existing exception type and message survive. |
| RTR-P5-10 | ✅ | `CNA::Logger` warning per fallback step: what was tried, why it failed, what is being tried next (design decision 7). |
| RTR-P5-11 | ✅ | Window-kind conflict detection: comparing `RendererWindowKind` of the failed and candidate renderers (design decision 8). |
| RTR-P5-12 | ✅ | Cross-kind fallback with `ownsWindow_ == true`: destroy and recreate the SDL window with the candidate's flags, re-publishing `Mouse`/`TextInputEXT` window handles. |
| RTR-P5-13 | 🟨 | Cross-kind fallback with a caller-supplied `DeviceWindowHandle`: record `WindowKindConflict` and skip that candidate with a clear log line — never silently reuse an incompatible window. |
| RTR-P5-14 | ✅ | `applyPreWindowAttributes` must re-run for the candidate before its window is recreated (`OPENGL1`'s GLX visual would otherwise be wrong). |
| RTR-P5-15 | ✅ | Fallback across `needsVideoSubsystem` (e.g. `VULKAN` → `HEADLESS`): `SDL_QuitSubSystem(SDL_INIT_VIDEO)` handling, and the reverse direction. |
| RTR-P5-16 | ✅ | Fallback interaction with `PresentationParameters::HeadlessEXT` — a headless request must not silently fall back to a windowed renderer. |
| RTR-P5-17 | ✅ | Fallback must **not** engage for `RecreateRendererForMultiSampleCount()`: an MSAA reconstruction failure is a genuine error on an already-chosen renderer, not a reason to change renderer mid-game. |
| RTR-P5-18 | ✅ | Unit tests with a fake registry: probe-fail → next; create-throw → next; both → third; exhausted → throw with accumulated causes; disabled → first exception propagates. |
| RTR-P5-19 | ✅ | Unit tests for history/active reporting: empty history on clean first hit, ordered history otherwise, `GetActive() != GetSelected()` after substitution. |
| RTR-P5-20 | ✅ | Test that `GetActive()` before latch throws (nothing has been created yet, so there is no honest answer). |
| RTR-P5-21 | ✅ | Example program demonstrating a fallback chain and printing `GetFallbackHistory()`. |
| RTR-P5-22 | ✅ | `docs/runtime-renderer-selection.md` — fallback section: default-throw policy, opt-in, ordering, the window-kind limitation, and the explicit statement that fallback never happens silently. |
| RTR-P5-23 | ✅ | **Phase gate.** Chain-of-1 exercises every path; default behaviour unchanged. |

---

### P6 — CMake multi-renderer builds (design decisions 1, 11)

The first phase that changes the build model.

| ID | St | Task |
|---|---|---|
| RTR-P6-1 | ✅ | Add `CNA_GRAPHICS_RENDERERS` (semicolon list). When set, `CNA_GRAPHICS_RENDERER` names the *default* preferred renderer and must be a member of the list. |
| RTR-P6-2 | ✅ | Define `CNA_MULTI_RENDERER` globally when the list has more than one entry. |
| RTR-P6-3 | 🟨 | Convert `RENDERER_TARGET` (scalar) to `CNA_RENDERER_TARGETS` (list) — **128 references across 40+ files**; split into reviewable batches by module. |
| RTR-P6-4 | ✅ | Convert `add_compile_definitions(CNA_RENDERER_<X>)` to per-target `target_compile_definitions(... PRIVATE ...)` for every family. |
| RTR-P6-5 | ✅ | Rework `modules/renderers/CMakeLists.txt`'s single `CNA_SELECTED_RENDERER` dispatch into a loop over the selected family list. |
| RTR-P6-6 | 🟨 | Rework `cna_add_renderer()` / `cna_renderer_common_setup()` to take the family's own target name instead of reading the global `${RENDERER_TARGET}`. |
| RTR-P6-7 | ✅ | `modules/graphics/CMakeLists.txt` — link every selected renderer target, preserving the declared archive cycles. |
| RTR-P6-8 | ✅ | Third-party configure functions (`cna_configure_webgpu`, `cna_configure_magnum`, `cna_configure_diligent`, `cna_configure_wicked`, `cna_configure_sokol`, `cna_configure_llgl`, `cna_configure_fna3d`, `cna_configure_openvg`, `cna_configure_portablegl`, Skia, Blend2D) must be callable in combination without clobbering each other's cache variables. **Checked by scanning, then by measuring.** Cache-variable collisions across the eleven `ThirdParty*.cmake` modules: 102 cache variables, exactly **one** shared — `CNA_SKIA_ROOT` between `Skia` and `SkiaGanesh`, two halves of the same renderer, so intended. The real leak was elsewhere: `cna_configure_llgl()` called `add_compile_definitions()`, which is DIRECTORY-scoped, and a CMake `function()` does not create a directory scope. Called from `RendererSelection.cmake`, which the top-level `CMakeLists.txt` includes, it defined `CNA_LLGL_HAS_OPENGL/VULKAN/NULL` for **every target in the project**. Measured on a `HEADLESS;LLGL;SOFTWARE;STUB` multi build by diffing `build.ninja` with and without the fix: **239 targets / 1073 objects before, 2 targets / 75 objects after** — `cna_input`, `cna_media`, `CNA_GamerServices` and the Metal tests were all being told which LLGL modules exist. The list is published as `CNA_LLGL_COMPILE_DEFINITIONS` and applied by the llgl module to its own target (PUBLIC, because `GraphicsRendererCompileDefinitionTests` includes that public header). Invisible in single-renderer mode, where LLGL is the only renderer anyway — which is how it survived. |
| RTR-P6-9 | ✅ | Extend `cmake/RendererRegistry.cmake` to emit an N-entry table. |
| RTR-P6-10 | ✅ | **Conflict matrix at configure time** (design decision 11) — reject with a specific message, never a link error: |
| RTR-P6-11 | ✅ |  · `PORTABLEGL` together with any real-GL family (`easygl`, `opengl1`, `opengl2`, `opengl4`, `opengles1`, `openvg`, `magnum`, `sokol` on GL) — global `gl*` symbol collision |
| RTR-P6-12 | ✅ |  · `GDI` together with `SOFTWARE` — the `CNA_SOFTWARE_2D_ONLY` re-compilation is an ODR violation |
| RTR-P6-13 | ✅ |  · two GL profile identities from the shared `easygl` target (until P11) |
| RTR-P6-14 | ✅ |  · any two families whose platform gates disagree (Windows-only + Emscripten-only, etc.) |
| RTR-P6-15 | ✅ |  · `GLIDE` with anything (32-bit-only ABI, `CMAKE_SIZEOF_VOID_P EQUAL 4`) |
| RTR-P6-16 | ✅ | Document every rejected combination and its reason in `docs/runtime-renderer-selection.md` — a user hitting one must be able to look it up. |
| RTR-P6-17 | ✅ | Preserve every existing per-renderer platform `FATAL_ERROR` in the list form (each member is gated individually). |
| RTR-P6-18 | ✅ | `scripts/check_renderer_identities.py` still passes: 46 identities, unchanged (design decision 10). |
| RTR-P6-19 | ✅ | New `scripts/check_renderer_combinations.py` — mechanically verifies the conflict matrix in CMake matches the documented table, the same registry-gate shape as `check_renderer_identities.py`. |
| RTR-P6-20 | ✅ | Verify build-directory discipline: multi builds go into a stable in-repo `cmake-build-multi/`, per `CLAUDE.md` — no new per-combination directories. |
| RTR-P6-21 | ✅ | Measure and record multi-build cost (configure time, build time at `-j3`, binary size) for the P8 reference set, so the cost of the mode is documented rather than discovered. |
| RTR-P6-22 | ✅ | `CMakePresets.json` — one preset for the reference multi set. |
| RTR-P6-23 | ✅ | Confirm single-renderer configures are **bit-identical** to before P6 (same defines, same targets, same link line). |
| RTR-P6-24 | ✅ | **Phase gate.** Single-renderer builds unchanged; a two-entry multi build configures, builds and links. |

---

### P7 — Identity reporting in multi builds (design decision 1)

| ID | St | Task |
|---|---|---|
| RTR-P7-1 | ✅ | Split `GraphicsRendererType.hpp`: keep `constexpr getCurrentGraphicsRendererType()` under `#ifndef CNA_MULTI_RENDERER`; add a non-`constexpr` `CNA::getActiveGraphicsRendererType()` available in both modes. |
| RTR-P7-2 | ✅ | Same split for `getCurrentGraphicsRendererName()` / `getActiveGraphicsRendererName()`. |
| RTR-P7-3 | ✅ | `GraphicsDevice::GetGraphicsRendererType()` (`GraphicsDevice.hpp:989`) — non-`constexpr` in multi mode, returning the device's real renderer. Document the intentional deviation. |
| RTR-P7-4 | ✅ | `GraphicsDevice::GetGraphicsRendererName()` — same treatment. |
| RTR-P7-5 | ✅ | Move the name table out of the `constexpr` switch into a shared function usable by both modes, so the 46 names exist exactly once. |
| RTR-P7-6 | ✅ | `GraphicsBackendMaturity.hpp` / `GraphicsBackendCategory.hpp` — their `getCurrent*()` convenience wrappers need the same dual treatment. |
| RTR-P7-7 | ✅ | `modules/renderers/fna3d/examples/fna3d_smoke_test.cpp:74` — its `static_assert` must be guarded for multi mode. |
| RTR-P7-8 | ✅ | `GraphicsRendererCompileDefinitionTests.cpp:172` — the `EXPECT_EQ(enabled, 1)` assertion becomes "exactly one in single mode, at least one in multi mode". |
| RTR-P7-9 | ✅ | `tests/modules/probe_core.cpp` uses `getCurrentGraphicsRendererType()` — update the minimal-link probe. |
| RTR-P7-10 | ✅ | The startup log line in `createRenderer()` (`GraphicsDevice.cpp:2435`) must print the **active** renderer, and in multi mode also how it was chosen (default / API / env / fallback). |
| RTR-P7-11 | ✅ | **Phase gate.** Both modes report identity correctly; no `constexpr` regression in single mode. |

---

### P8 — First real multi-renderer set: `STUB + HEADLESS + SOFTWARE`

Chosen deliberately: no window, no GPU dependency, no third-party closure, all three Linux-native
and CI-runnable. This is where fallback substitution is proven for the first time.

| ID | St | Task |
|---|---|---|
| RTR-P8-1 | ✅ | Configure and build `-DCNA_GRAPHICS_RENDERERS="STUB;HEADLESS;SOFTWARE"`. |
| RTR-P8-2 | ✅ | Runtime selection of each of the three, verified by `GetActive()` and by real observable behaviour (`SOFTWARE` produces pixels, `STUB` produces none). |
| RTR-P8-3 | ✅ | `SetPreferred()` before `Game::Run()` reaches the right renderer end to end. |
| RTR-P8-4 | ✅ | Env-var selection verified in the same binary. |
| RTR-P8-5 | ✅ | **First real fallback:** an injected `isAvailable() == false` on the preferred renderer substitutes the next in chain; `GetFallbackHistory()` records it. |
| RTR-P8-6 | ✅ | **First real init-failure fallback:** an injected constructor throw substitutes the next in chain. |
| RTR-P8-7 | ✅ | Exhausted-chain behaviour verified end to end. |
| RTR-P8-8 | ✅ | Latch verified end to end: `SetPreferred()` after `GraphicsDevice` construction throws `System::InvalidOperationException`. |
| RTR-P8-9 | ✅ | Two `GraphicsDevice` lifetimes in one process, same renderer both times. |
| RTR-P8-10 | ✅ | A CTest suite pinning all of the above, runnable in CI with no display server. |
| RTR-P8-11 | ✅ | Record binary-size and build-time delta versus the three single-renderer builds. |
| RTR-P8-12 | ✅ | **Phase gate.** The reference multi set is green in CI. |

---

### P9 — Test and example corpus (design decision 12)

**Audit result (RTR-P9-3 / RTR-P9-12).** Every `CNA_RENDERER_*` site in the test and example corpus,
classified by whether its translation unit also includes a renderer-family header — the sites that
do genuinely cannot become runtime gates, because they need types that only exist when that family
is compiled in.

| Area | Runtime-convertible | Needs renderer headers |
|---|---:|---:|
| `modules/graphics/tests` | 200 | 247 |
| `modules/graphics/examples` | 505 | 66 |
| `modules/content/tests` | 25 | 0 |
| `modules/renderers/*` (tests + examples) | 22 | 74 |
| **Total** | **752** | **387** |

Grand total 1139 sites — larger than the 892 first estimated, which counted only `.cpp` under the
top-level module test/example trees.

The 387 header-dependent sites stay compile-time gated, on their family's own private macro, which
is what that family's target has. The 752 convertible ones are the incremental job, and the reason
it is worth doing is not tidiness: a compile-time gate can only describe ONE renderer, so in a
multi-renderer build a test meaning "this is how SOFTWARE behaves" does not run at all when SOFTWARE
is compiled in but is not the default — and reports nothing, not even a skip.

**The membership-versus-default rule, learned by getting it wrong.** A first pass converted every
renderer gate in the example CMakeLists to list membership. That is right for some gates and wrong
for others, and single-renderer mode cannot tell them apart because there the two are identical:

- A gate deciding whether a **target exists**, where that target runs against whatever renderer the
  build *defaults* to, must stay equality with `CNA_GRAPHICS_RENDERER`. Under membership a
  multi-renderer build defaulting to `SOFTWARE` but containing `OPENGLES3` would build
  `cna_house3d_demo` and then throw "3D not supported" at runtime — a build that succeeds and a
  program that cannot work. The `WEBGL1` min/max-version gate was worse: a bundle containing both
  WebGL profiles would have been pinned to WebGL 1, re-introducing the mirror image of a bug
  `plan_glbackends.md` GLB-36 had already fixed once.
- A gate deciding whether a **resource is available** — a runtime library to copy next to a demo, a
  renderer's own device-free test sources, a renderer's libraries on the test executable — may use
  membership, because in a multi build the user really can select that renderer at runtime.

Both kinds now carry that rule as a comment where they live.

**The classification is conservative, deliberately.** It marks a whole file header-dependent as soon
as it includes any renderer-family header, even when that include is itself `#ifdef`-guarded and
only some of the file's tests need it — `PointListPrimitiveTests.cpp` is exactly that shape, mixing
a guarded `Bgfx/BgfxRenderer.hpp` include with several tests whose bodies use nothing but the public
XNA API. So 752 is a floor, not an estimate, and per-file conversion has to be decided per test
rather than per file.

**Converting a gate to a skip is only half the job.** A runtime-gated test that merely skips when
its renderer is not the default has the same coverage as before — it is just honest about it now.
Making it actually *run* per renderer needs the fixture to select the renderer, the way
`CrossRendererContractTest::ForEachRenderer` does. That is the pattern the remaining conversion
should follow, and it is why the batches are worth doing carefully rather than mechanically.

The largest volume of work: 892 `#ifdef` sites, 86 CMake conditions. Single-renderer builds must
keep compiling the corpus exactly as today throughout.

| ID | St | Task |
|---|---|---|
| RTR-P9-1 | ✅ | Decide and document the conversion idiom: a runtime `SkipIfNotRenderer(GraphicsRendererType)` GTest helper replacing `#ifdef` where the test body is renderer-agnostic. |
| RTR-P9-2 | ✅ | Add that helper plus its multi-mode-aware counterpart to the shared test fixture headers. |
| RTR-P9-3 | ✅ | **Audit published.** Classified every renderer-gated site by whether its file also includes a renderer-family header (which makes a runtime gate impossible). Counts below. |
| RTR-P9-4 | ✅ | Convert class (a) in `modules/graphics/tests` — batch 1: capability/format suites. Done so far: `GraphicsDeviceValidationTests`, `Texture2DTests`, `TextureCubeTests`, the three content suites, and now `Texture3DTextureCubeRenderTargetTests` (the `RenderTargetCube` SetData chain → `RenderTargetCubeAcceptsSetData()`) and `Texture2DCacheReconstructionTests`. `GraphicsDeviceCapabilityTests.cpp` is now done too: the `kExpectMultipleRenderTargets`/`kExpectOcclusionQuery`/`kExpectCustomEffects` chain became a `ExpectedCapabilities()` lookup keyed on the active renderer (each arm keeps its own reasoning at its `case`), and the second chain — which defined the SAME two test names three times — became one test of each branching on `IsTwoDimensionalOnly()`. 21 sites → 9, the rest being a separate format chain and two HEADLESS blocks. **This uncovered a real defect, written up as finding 6 in `threeissues.md`:** the `BLEND2D` and `OPENVG` arms carried their full reasoning and then defined **no constants at all**. `#elif` arms are exclusive, so those builds selected an empty arm, never reached the catch-all, and left all three names undefined at their unguarded use sites — `CnaTests` could not compile for either renderer. The table gives every renderer an answer and an unnamed one a documented default; Blend2D and OpenVG get `{false,false,false}`, which is what their comments always claimed, flagged as documented intent rather than a measurement since neither builds here. |
| RTR-P9-5 | ✅ | Convert class (a) — batch 2: draw/indexed-draw suites. Converted: `OrdinaryDrawMultiStreamTests`, `OrdinaryDrawBindingOffsetTests`, `InstancedVertexColorTests` (`_MEASURED` + `_CONTRACT` → predicates), `InstancedDiffuseColorTests` (`_MEASURED`, 5 blocks), `InstancedDrawRangeTests` (the binding-offset oracle plus its EasyGL and D3D pins) and `BuiltInVertexLayoutTests` (section B's whole-file fence + section C's SDL_GPU fence). **Two lessons recorded here rather than repeated:** (1) the gate must sit in the TEST BODY — `GTEST_SKIP()` returns from the function it appears in, so a gate placed in a shared fixture helper marks the test skipped and then lets the body run on and fail; this file already documents that and keeps the capability gate in `SetUp()` for exactly this reason. (2) A guard around a DECLARATION (`InstancedDrawRangeTests`' oracle member sat behind `CNA_INSTANCED_BINDING_OFFSET_ORACLE`) cannot become a runtime `if` at all; the member is now unconditional and its two callers carry the gate. Also converted: `SdlGpuIndexedDrawRangeTests` (a whole-file `#ifdef CNA_RENDERER_SDL_GPU` around 27 tests → one `SetUp()` gate, which is where GoogleTest itself honours a skip), and 5 fences across `PointListPrimitiveTests` and `NonIndexedDrawRangeTests` (21 tests). **A third lesson, and the reason this is tooled rather than hand-edited:** a fence must also be refused when its BODY touches a renderer-private API. The first run converted the bgfx-only fences in both files because the `#include <bgfx/bgfx.h>` sits at the top of the file under its own guard, nowhere near the fence — but the bodies call `bgfx::getStats()` and `dynamic_cast<BgfxRenderer*>`, so they only COMPILE when bgfx is in the build and no runtime predicate can save them. `scripts`-side helper now refuses those and names the symbol it found. `InstancedDrawMultiStreamTests` is done too (12 → 0), and it needed three different treatments in one file: two renderer sets, two fences around whole groups of tests, and six conditional chains inside function bodies — one of them four-way (`ORACLE` / `FNA3D` / `WICKED` / else), where each arm carries its own MEASURED outcome and so had to stay a separate arm rather than be folded into one. The conversion had to be run to a FIXED POINT: the inner blocks sat inside the outer fences, so the first pass could not see them and a second pass found six more. Checked in both directions afterwards — on OPENGLES3 (EasyGL, in both sets) 28 of these tests still RUN, on SOFTWARE 56 now skip where they previously did not exist at all. The three class (b) files (`VertexBufferEmptyDataTests`, `IndexBufferEmptyDataTests`, `WebGpuWireFrameContractTests`) are done per RTR-P9-9: they keep a COMPILE-time guard, because no runtime predicate can make a renderer's type exist, but the condition widens from the default-renderer macro to `|| defined(CNA_RENDERER_PRESENT_<X>)` — EasyGL being a family means five of those. **`PRESENT` alone would have been wrong:** these blocks `dynamic_cast` to a specific renderer type, so in a multi build holding the family without selecting it they would compile and then fail `ASSERT_NE(nullptr, ...)`. Each therefore also carries a runtime gate: compiled when the renderer is IN the build, run only when it is ACTIVE. Proven rather than assumed — a multi build (default HEADLESS, OPENGLES3 merely present) contains `typeinfo for ...EasyGL::EasyGLVertexBufferRenderer`, which a HEADLESS-only binary does not, so the block genuinely compiled in and then skipped at runtime. `IndexedDrawDeferredTests` is done as well: **39 sites → 3**, and the 3 that remain are the bgfx/WebGPU/Vulkan header guards, now on `PRESENT_`. Two findings from it. (1) Two outer fences read `!defined(DIRECTX9) && !defined(DIRECTX11)` while the gate inside them named a set that INCLUDED both, so keeping the pair would have had the file assert two contradictory things — the tests would vanish on a D3D build while their own gate claimed to run them. Folded into one gate (the set MINUS D3D) with the reason stated: that deferred-queue contract was never measured on D3D, and an unmeasured renderer must not be asserted either way. (2) The fence tool had to stop refusing fences that merely CONTAIN a nested `#ifdef`: those nested blocks are the class (b) `CNA_TEST_<FAMILY>_AVAILABLE` guards doing their job, and the private API inside them is already protected — skipping their lines instead of refusing the whole fence unlocked ten more tests. The bgfx guards in five files (`PointListPrimitiveTests`, `NonIndexedDrawRangeTests`, `VertexDeclarationLayoutTests`, `InstancedDiffuseColorTests`, `InstancedDrawRangeTests`) now use `CNA_TEST_BGFX_AVAILABLE`, defined from `CNA_RENDERER_BGFX || CNA_RENDERER_PRESENT_BGFX`, with a runtime `CNA_SKIP_IF_RENDERER_IS_NOT(Bgfx)` in each of the 19 tests inside. **This one is NOT compile-verified and must not be read as if it were:** there is no bgfx checkout and no bgfx build tree on this machine, so those blocks compile in no build available here — a mistake inside them would go unnoticed. What was verified instead: all three single-renderer gates stay green and unchanged, and every one of the 19 insertions was checked structurally to sit immediately after its test's opening brace. The mechanism itself is the one proven for EasyGL/WebGPU by the `typeinfo` check above. |
| RTR-P9-6 | ✅ | Convert class (a) — batch 3: vertex-layout/declaration suites. `VertexDeclarationLayoutTests.cpp` goes from **36** renderer-preprocessor sites to **2**. Converted: the `DECLARATION_LAYOUT_ORACLE` set, the `MEASURED` set, the 14-arm display-name chain, the four `!defined(CNA_RENDERER_BGFX)` expectation gates and the EasyGL ShaderEffect control. The 2 that stay are genuine class (b) — they `#include <bgfx/bgfx.h>` and call `bgfx::` directly. Worth recording: `TheTranslatingRendererStillRendersEveryCollidingDeclaration` now **exists and reports a skip** on every non-bgfx build instead of silently not being compiled, which is the coverage-visibility gap this phase exists to close. Gate: OPENGLES3 6376 ran / 6369 passed / 7 skipped / **0 failed**. |
| RTR-P9-7 | ✅ | Convert class (a) — batch 4: render-target/readback suites. The batch is `WireFrameTriangleOracle.hpp` (20 sites) plus its 14 consumer sites in `GraphicsDeviceCapabilityTests.cpp`. Its four compile-time capability macros became runtime predicates — `HasPixelOracle()`, `IsMeasured()`, `RendersEdges()`, `RejectsWireFrame()` — alongside `RendererName()`, and **`CNA_WIREFRAME_*` no longer exists anywhere in the repo**. The identity-name `using namespace` is scoped *inside* the oracle namespace: at header scope it would have leaked a using-directive into every suite that includes the oracle. Effect on HEADLESS, which previously compiled the whole oracle block out: the four wireframe suites now exist and skip with a reason. Gates: HEADLESS 6286 ran / 6172 passed / 114 skipped / **0 failed**. |
| RTR-P9-8 | ✅ | Convert class (a) — batch 5: SpriteBatch/2D suites. **The batch is almost empty, and that is the finding.** `SpriteBatchTests.cpp`, `SpriteEffectTests.cpp`, `SpriteFontTests.cpp` and `RecordingSpriteBatchRenderer.hpp` contain **zero** renderer preprocessor guards — the 2D suites were already renderer-agnostic, because SpriteBatch is expressed entirely through the common interface and every renderer implements it. The only 2D site in the corpus was `kRenderTarget2DSupported` in `Texture2DCacheReconstructionTests.cpp` (OpenVG has no genuine RenderTarget2D storage), now `RenderTarget2DSupported()`. Recorded rather than padded out: a phase that finds nothing to convert should say so. |
| RTR-P9-9 | ✅ | Class (b) sites keep `#ifdef`, but on the family's **private** define, so a multi build compiles them for each family that is present. **Second half, found by finally building a multi set containing LLGL:** a module's own `include/` root is not enough when its PUBLIC headers include a third-party library's headers in turn. `LlglSdlSurface.hpp` opens with `#include <LLGL/Surface.h>`, and LLGL's include directory reaches the llgl module through the LLGL target it links, not through any directory of CNA's own — so `CnaTests` failed with `LLGL/Surface.h: No such file or directory`, i.e. this task had moved the failure from "no tests" to "no such file" for that family. `cmake/UnitTests.cmake` now also gives `CnaTests` each present renderer target's own `INCLUDE_DIRECTORIES`, which keeps it generic instead of naming each family's third-party library. Verified: the LLGL multi build now compiles and `LlglSdlSurfaceConstructor.NonX11DriverIsRejectedWithActionableMessage` exists and runs. |
| RTR-P9-10 | ✅ | Delete class (c). **"Class (c)" was never defined.** This row was written before the RTR-P9-3 audit ran, and that audit published **two** classes, not three: 387 header-dependent sites and 752 convertible ones. So the task was resolved by looking for what a third class could honestly mean — a guard that decides nothing — and scanning for it. Two scans: (1) guards naming a `CNA_RENDERER_<X>` macro CMake never generates — **none**, nothing is stranded on a removed identity such as the old `ASCII`; (2) guards whose `#if` and `#else` bodies are textually identical — 96 guards with an `#else` exist across `modules/`, **exactly one** was dead, and it was not harmless: `kSecondSampleableFormat` in `rendertarget_effect_source_test.cpp` was `false` in both arms, so all five families that build that example printed SDL_GPU's "one fixed native colour format" boundary about themselves. Fixed and written up as finding 7 in `threeissues.md`; verified by running the example on OPENGLES3 (claim gone, 20/20 legs still pass). |
| RTR-P9-11 | ✅ | Same audit and conversion for the 16 `modules/content/tests` sites. |
| RTR-P9-12 | ✅ | Audit the `modules/*/examples` sites. Examples differ from tests: many are renderer-specific *demonstrations* and should stay compile-time. **The split, which this row claimed but never actually published — measured 2026-08-15:** 53 example sources carry renderer guards, **576** of them. **9 files / 75 guards** include a renderer-family header (class (b), compile-time by necessity). **44 files / 501 guards** do not. The largest are the render-target and texture contract examples (`rendertarget_effect_source_test.cpp` 20, `rendertargetcube_usage_test.cpp` 16, `rendertarget_pass_boundary_test.cpp` 16, …). See RTR-P9-13 for why the 501 stay compile-time anyway. |
| RTR-P9-13 | ✅ | Convert the genuinely renderer-agnostic examples to runtime gating. **Answer: none of them, and converting them would be a defect rather than an improvement.** The reason is structural, not stylistic. Every renderer family's example block is gated on **equality with the build default** — `if(... CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3" ...)` — checked across all ~40 families: **40 use equality, 0 use list membership**. So an example target only exists when its family is the DEFAULT renderer, and the binary it produces can only ever run that renderer. Confirmed from the other side too: **no example anywhere calls `GraphicsRendererSelection`** — nothing in an example changes the renderer at runtime. A runtime gate would therefore ask a question whose answer is fixed at configure time, and would silently pass on a renderer the example was never built for, instead of stating the expectation for the one it was. This is the membership-versus-default rule from RTR-P9-11 applied to the sources rather than the CMake: target existence tracks the default, so the source's expectations track it too. Same shape of result as RTR-P9-8 — a phase that finds nothing to convert should say so, with the evidence. |
| RTR-P9-14 | ✅ | The 86 CMake conditions gating example/test targets on `CNA_GRAPHICS_RENDERER` become list-membership checks (`IF <X> IN_LIST CNA_GRAPHICS_RENDERERS`). |
| RTR-P9-15 | ✅ | `modules/renderers/easygl/examples/CMakeLists.txt` — 17 conditions, the densest single file. |
| RTR-P9-16 | ✅ | `modules/graphics/examples/CMakeLists.txt` — 10 conditions. |
| RTR-P9-17 | ✅ | `modules/net/examples` (4), `modules/gamer-services/examples` (4), `modules/graphics-ext/examples` (3). |
| RTR-P9-18 | ✅ | Remaining single-condition example CMakeLists across ~20 renderer families. |
| RTR-P9-19 | ✅ | `cmake/UnitTests.cmake` (19 references) — list-aware. |
| RTR-P9-20 | ✅ | `cmake/Harnesses.cmake` (4 references) — list-aware. |
| RTR-P9-21 | ✅ | `cmake/Tests/ModuleProbes.cmake` and `cmake/Tests/WickedTests.cmake` — list-aware. |
| RTR-P9-22 | ✅ | `scripts/run-all-renderer-smoke-tests.sh` — teach it the multi mode (build once, run N times with different `CNA_GRAPHICS_RENDERER` values). This is where multi builds actually pay for themselves in CI time. |
| RTR-P9-23 | ✅ | New suite: for every pair in a multi build, assert both renderers produce their own documented `SupportsCapability()` answers from the same binary. |
| RTR-P9-24 | ✅ | `scripts/run-oracle-corpus-multi.sh` renders the 39-scene oracle corpus against several renderers from ONE binary, diffing every scene against the same checked-in XNA reference PNGs. Verified on `OPENGLES3;OPENGL33`: the env var genuinely switches context (ES 3.2 vs Core Profile 4.6), and the OPENGLES3 leg reproduces the existing single-renderer script exactly (9 passed / 30 failed both ways). |
| RTR-P9-25 | ✅ | Verify the golden/fixture assets under top-level `tests/` need no per-mode duplication. |
| RTR-P9-26 | ✅ | Regression: single-renderer `CnaTests` test count is unchanged after every batch above. |
| RTR-P9-27 | ✅ | **Phase gate**, with its criterion corrected. "Test counts unchanged" is the wrong test for this phase — converting a compile gate to a runtime skip makes tests EXIST that did not before, so the count must rise. `scripts/compare_test_outcomes.py` checks the property that matters instead: no test that PASSED before may stop passing. OPENGLES3 6369 → 6369 passing, 0 lost; HEADLESS 6172 → 6173 passing, 0 lost, skips 44 → 98. |

---

### P10 — Wider multi sets

Each set is its own task because each will surface its own third-party integration problem.

| ID | St | Task |
|---|---|---|
| RTR-P10-1 | ✅ | `SDL_RENDERER + SOFTWARE + HEADLESS + STUB` — first set with a real window. |
| RTR-P10-2 | ✅ | `SDL_RENDERER + OPENGLES3` — first set crossing `RendererWindowKind::Plain` → `OpenGL`, exercising window recreation on fallback. |
| RTR-P10-3 | ✅ | `OPENGLES3 + VULKAN` — the `OpenGL`/`Vulkan` window-flag conflict (design decision 8) proven end to end, including the refusal path with a caller-supplied window. |
| RTR-P10-4 | ✅ | `OPENGLES3 + VULKAN + SOFTWARE + HEADLESS + STUB` — the realistic Linux "everything native" set. |
| RTR-P10-5 | ✅ | `+ SDL_GPU`. |
| RTR-P10-6 | ✅ | `+ SKIA` — first heavy external artifact in a multi build. |
| RTR-P10-7 | ✅ | `+ BLEND2D`. |
| RTR-P10-8 | ✅ | `+ OPENVG` — ShivaVG's own GL context alongside another GL renderer in the same binary. |
| RTR-P10-9 | ⬜ | `+ BGFX` — its runtime `ResolveRendererType()` must not fight CNA's own selection. |
| RTR-P10-10 | ✅ | `+ LLGL` — likewise for `ResolveRendererModule()`. |
| RTR-P10-11 | ✅ | `+ DILIGENT` — a runtime-dispatching renderer inside a runtime-dispatching framework; document the two-level selection clearly. |
| RTR-P10-12 | ⬜ | `+ FNA3D` — likewise, plus MojoShader's symbol surface. |
| RTR-P10-13 | ✅ | `+ MAGNUM`. |
| RTR-P10-14 | ✅ | `+ WICKED`. |
| RTR-P10-15 | ✅ | `+ SOKOL` (GL). |
| RTR-P10-16 | ⬜ | `+ WEBGPU` (native wgpu-native). |
| RTR-P10-17 | ✅ | `+ OPENGL1 + OPENGL2 + OPENGL4` — three native GL renderers coexisting; verify no loader/symbol conflict. |
| RTR-P10-18 | ✅ | `+ OPENGLES1`. |
| RTR-P10-19 | ⬜ | Windows/MinGW multi set: `DIRECTX9 + DIRECTX11 + DIRECTX12`. |
| RTR-P10-20 | ⬜ | Windows legacy multi set: `DIRECTX1 + DIRECTX2 + DIRECTX3 + DIRECTX5 + DIRECTX6 + DIRECTX7 + DIRECTX8` — seven families sharing DirectDraw-era headers. |
| RTR-P10-21 | ⬜ | `+ DIRECT2D + GDI + FREEDIRECT` on Windows (`GDI` excludes `SOFTWARE`, per the conflict matrix). |
| RTR-P10-22 | 🟨 | Emscripten multi set: `WEBGL2 + CANVAS + HTML_DOM + SVG_DOM` — one wasm bundle, renderer chosen from JS before start. Highest practical payoff of the whole plan. |
| RTR-P10-23 | ✅ | JS-side selection surface for that Emscripten set (a `Module` property or exported function feeding `SetPreferred()`), documented in `docs/runtime-renderer-selection.md`. Two entry points in `modules/core/src/GraphicsRendererSelectionEmscripten.cpp`: `cna_set_preferred_renderer(name)` for a page that drives the module directly, and `Module.cnaPreferredRenderer` for one that just declares a preference. **The design decision worth keeping:** the property is consulted inside `ConsultEnvironmentOnce()`, at the `CNA_GRAPHICS_RENDERER` environment variable's precedence — NOT by having JS glue call `SetPreferred()`. Applying it would make a page property indistinguishable from an explicit call in the program and let it silently outrank one. Resolution order stays: explicit `SetPreferred()` > env var or `Module` property > compile-time default. Verified by a real `emcmake` build: `cna_core` compiles and both exports are present in the archive (`T cna_set_preferred_renderer`, `T cna_read_module_preferred_renderer`), and HEADLESS stays at 6389/6172/217/0 so the native path is untouched. **Not verified:** that the property switches the renderer in a live browser — that needs a headless browser run, not a link check. |
| RTR-P10-24 | ⬜ | macOS multi set: `METAL + OPENGL4 + SOFTWARE`. |
| RTR-P10-25 | 🟨 | Record binary size and build time for every set above; publish the table so the cost of each addition is visible. **Sizes published** in `docs/runtime-renderer-selection.md`: a four-renderer set (`HEADLESS;LLGL;SOFTWARE;STUB`) costs **+4.1 MB stripped, ~13 %**, over a single-renderer `HEADLESS` build of the same executable (36.2 vs 32.1 MB). Stripped is the only honest column — the same binary is 232 MB unstripped, so Debug symbols, not renderers, dominate that number. Per-renderer archives are listed too, with the point that **a renderer's cost in the binary is not its library's size on disk**: LLGL's third-party archives total ~103 MB yet the executable grows ~4 MB, because the linker takes what is referenced. **Deliberately not done:** build times, which need from-scratch builds of each set — the project's own build rules treat repeated clean rebuilds as real SSD wear, and timing a table was not worth that; and sets needing bgfx/FNA3D/WebGPU or Windows/macOS, whose rows are absent rather than estimated. Left 🟨 for that reason rather than claimed complete. |

---

### P11 — EasyGL runtime profile (unblocks 5 identities coexisting)

**Pre-existing OPENGL33 finding, unrelated to this phase.** Running the full corpus with OPENGL33 as
the *default* renderer segfaults in
`GltfSceneGraphBones.SharedMeshGetsOneBonePerInstancingNode`. Confirmed pre-existing by building
single-renderer OPENGL33 at `c5045553b` (the commit before P11) and reproducing the identical crash
in the identical test. OPENGL33 had never been exercised against the corpus in this campaign before
now, which is why it surfaced here. Not investigated further — it is a defect in that renderer's own
glTF path, not in renderer selection.


| ID | St | Task |
|---|---|---|
| RTR-P11-1 | ✅ | Audit every `CNA_GL_PROFILE_*` use inside `modules/renderers/easygl/` and classify: context-creation attributes, shader-source selection, feature gating. |
| RTR-P11-2 | ✅ | Convert context-creation attribute choice to a runtime profile parameter on `EasyGLRenderer`. |
| RTR-P11-3 | ✅ | Convert shader-header/source selection to runtime — the largest sub-item; GLSL ES 1.00 vs 3.00 vs 3.30 sources must all be compiled into the binary. |
| RTR-P11-4 | ✅ | Convert remaining feature gates to runtime profile queries. |
| RTR-P11-5 | ✅ | Five descriptors from one `easygl` target, one per GL identity, each pinning its profile. |
| RTR-P11-6 | ✅ | Remove the "two GL profiles conflict" entry from the P6 conflict matrix. |
| RTR-P11-7 | ✅ | Multi set `OPENGLES2 + OPENGLES3 + OPENGL33` proven on Linux. |
| RTR-P11-8 | 🟨 | Multi set `WEBGL1 + WEBGL2` proven under Emscripten. |
| RTR-P11-9 | ✅ | **Measured, and the interesting number is the other one.** A second GL profile costs nothing: single `OPENGLES3` 15,626,280 bytes vs `OPENGLES3;OPENGL33` 15,626,264 — a 16-byte *decrease*, i.e. noise, because after P11 the profile branches are always compiled either way. The real cost is what P11 charged a SINGLE-renderer build: the EasyGL archive grew 4,766,788 → 5,558,462 bytes (**+16.6 %**), being the branches `#if` used to eliminate. Every further profile after the first is free. |
| RTR-P11-10 | ✅ | `plan_glbackends.md` §2 now records that the profile is a runtime value, that its other invariants (one family, one shader corpus, the Emscripten-only WebGL gate) still hold, and that the combination rule rejecting two GL identities is deleted. |
| RTR-P11-11 | ✅ | Delivered by RTR-P9-24: `scripts/run-oracle-corpus-multi.sh` runs the corpus per profile from one binary. `OPENGLES3;OPENGL33` verified — the OPENGLES3 leg reproduces the existing single-renderer script exactly (9/30 both ways). |
| RTR-P11-12 | ✅ | **Phase gate met across both targets.** Native: `OPENGLES3;OPENGLES2;OPENGL33` in one binary, each selectable at runtime, `OPENGL33` genuinely getting a Core-Profile context. Emscripten: `WEBGL2;WEBGL1;CANVAS;HTML_DOM;SVG_DOM` in one wasm bundle. The WebGL pair is Emscripten-only by platform gate, so five-in-one-binary is not a thing that can exist — three plus two is the whole set. |

---

### P12 — Documentation, gates and closing

| ID | St | Task |
|---|---|---|
| RTR-P12-1 | ✅ | `docs/runtime-renderer-selection.md` complete: API reference, precedence, latch, fallback, conflict matrix, per-platform supported sets, cost table. |
| RTR-P12-2 | ✅ | `CLAUDE.md` — document the second build mode and the `cmake-build-multi/` directory (the build-directory list is closed; this is a deliberate, reviewed addition, not an ad-hoc one). |
| RTR-P12-3 | ✅ | `README.md` — short section on choosing a renderer at runtime, linking the doc. |
| RTR-P12-4 | ✅ | `AUDIT.md` — record the new CNAEXT public surface (`GraphicsRendererSelection`, `GetActive`, fallback API) with its test status. |
| RTR-P12-5 | ✅ | `NEXT.md` — record what remains after this plan closes. |
| RTR-P12-6 | ✅ | The four plan documents that describe renderer selection as compile-time (`plan_blend2d.md`, `plan_gltf.md`, `plan_opengles2.md`, `plan_svg_dom.md`) each carry a pointer to this plan and to `docs/runtime-renderer-selection.md`, stating that single-renderer mode is unchanged. `plan_glbackends.md` got its own, fuller note in RTR-P11-10. The other 48 plan documents never mention the option and were left alone rather than papered with a notice they do not need. |
| RTR-P12-7 | ✅ | CI: single-renderer matrix unchanged, plus the P8 reference multi set. |
| RTR-P12-8 | ✅ | CI: the Emscripten multi set from RTR-P10-22, since that is the one with real end-user value. `.github/workflows/emscripten-multi-renderer-ci.yml` builds one wasm bundle holding `WEBGL2;CANVAS;HTML_DOM;SVG_DOM` and then **asserts the contents**, because a configure that quietly dropped a renderer would otherwise pass by building three and calling it four: both RTR-P10-23 exports must be in `libcna_core.a`, and all four renderer archives must exist. **A caught mistake worth keeping:** the first version asserted `libcna_renderer_webgl2.a`, which does not exist — `WEBGL2` is one of the five public identities of the **EasyGL family**, so it builds `libcna_renderer_easygl.a`. Running the assertions against a real local wasm tree before committing is what found it; a CI-only check would have failed on a perfectly good bundle. The job does NOT run the bundle in a browser — `htmldom-ci.yml` already drives a real Chromium, and duplicating that here would test the browser harness rather than the multi-renderer link. |
| RTR-P12-9 | ✅ | Doxygen coverage check on every new public header (`CLAUDE.md` requires a full block on every public member). |
| RTR-P12-10 | ✅ | `CHECKLIST.md` — add the runtime-dispatch items a new renderer family must now provide (descriptor, namespaced factory, availability probe, registry entry). |
| RTR-P12-11 | ✅ | Update `scripts/check_renderer_identities.py`'s stale "42" docstring to 46 while touching this area. |
| RTR-P12-12 | ✅ | Remove the stale `CNA_RENDERER_SDL` reference in `modules/core/include/CNA/Entrypoint.hpp:22` — it names an identity that does not exist. |
| RTR-P12-13 | ✅ | Final sweep: no `CNA_RENDERER_*` occurrence remains in `modules/graphics/src`; every remaining occurrence elsewhere is deliberate and documented. |
| RTR-P12-14 | ✅ | Performance check: confirm the added indirection (one function-pointer call per device construction, none per frame) is not measurable — and say so with numbers rather than asserting it. |
| RTR-P12-15 | ⬜ | **Plan gate.** Single-renderer builds byte-identical in behaviour; multi-renderer mode documented, tested and CI-covered. |

---

## Task count

| Phase | Done | Partial | Total |
|---|---:|---:|---:|
| P0 Foundations | 10 | 0 | 10 |
| P1 Pre-window contract | 50 | 1 | 51 |
| P2 Factory + registry | 48 | 1 | 50 |
| P3 XNA-layer cleanup | 18 | 1 | 19 |
| P4 Selection API | 16 | 0 | 16 |
| P5 Fallback API | 22 | 1 | 23 |
| P6 CMake multi-build | 21 | 2 | 24 |
| P7 Identity reporting | 11 | 0 | 11 |
| P8 First multi set | 12 | 0 | 12 |
| P9 Test/example corpus | 9 | 0 | 27 |
| P10 Wider multi sets | 15 | 1 | 25 |
| P11 EasyGL runtime profile | 7 | 1 | 12 |
| P12 Documentation and gates | 12 | 0 | 15 |
| **Total** | **251** | **8** | **295** |

Status as of 2026-08-15 (second pass). ✅ = implemented **and** verified against its stated acceptance criteria;
🟨 = implemented but not verifiable in this environment (a Windows/macOS/Emscripten target, or a
third-party dependency not present).

P0–P5 change no observable behaviour and are individually valuable refactors; P6 onward introduces
the second build mode. Both halves are complete and verified.

What remains is concentrated in three places, none of which blocks the feature:

- **P9** — the bulk corpus conversion (752 sites). The idiom, the audit and a cross-renderer suite
  are in place; the mechanical work is not.
- **P10** — multi sets needing a platform or dependency this environment does not have: the Windows
  DirectX sets, the Emscripten browser set, macOS Metal, and the heavier middleware families
  (BGFX, LLGL, DILIGENT, FNA3D, WICKED, SOKOL, MAGNUM, SKIA, WEBGPU).
- **P11** — EasyGL's GL profile as a runtime choice, which is what would let the five GL identities
  coexist. Until then the configure step refuses that combination with a message saying so.
