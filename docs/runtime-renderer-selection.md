# Runtime graphics renderer selection

**Status: in development.** Nothing described here is usable yet. This document tracks
`plan_runtimerenderer.md` as it is implemented, and states plainly what exists and what does not.
Do not describe CNA as supporting runtime renderer selection until the status table below says so.

---

## What this is

CNA selects its graphics renderer at **compile time** (`-DCNA_GRAPHICS_RENDERER=<X>`). That stays
the default and recommended mode: it produces the smallest binary and lets the compiler eliminate
every renderer that was not selected.

This document describes a **second, opt-in mode**, in which CNA is compiled with several renderers
linked in and the concrete one is chosen at runtime, before CNA is started.

---

## Status

| Capability | Status |
|---|---|
| Descriptor / registry value types | ✅ present (`GraphicsRendererDescriptor`, `GraphicsRendererRegistry`, `GraphicsRendererFallbackRecord`) |
| Pre-window contract extracted from `GraphicsDevice` | ✅ window flags, `SDL_INIT_VIDEO`, the no-window branch and OPENGL1's GLX attributes are all descriptor-driven |
| Per-family descriptors | ✅ all 42 families; guarded by `scripts/check_runtime_renderer_discipline.py` |
| Namespaced factories / generated registry | ⬜ not implemented |
| `GraphicsRendererSelection` API | ⬜ not implemented |
| Fallback chain | ⬜ not implemented |
| Multi-renderer CMake mode | ⬜ not implemented |
| Runtime identity reporting | ⬜ not implemented |

Legend: ✅ implemented and verified · 🟨 exists but unverified · ⬜ not implemented.

---

## What a renderer descriptor answers

Four decisions have to be made *before* an `IGraphicsRenderer` instance exists, so no virtual method
can serve them. Each renderer family answers them through its own
`GraphicsRendererDescriptor` (`modules/renderers/<family>/src/*RendererDescriptor.cpp`):

| Question | Field |
|---|---|
| Does this renderer need a window at all? | `needsWindow` — false for `HEADLESS`, `SOFTWARE`, `STUB`, `PORTABLEGL` |
| Must SDL's video subsystem be initialized? | `needsVideoSubsystem` — false for the same four, which is what lets them run with no display server |
| Which SDL window flags does it need? | `prepareWindowFlags()` |
| Anything to set before `SDL_CreateWindow`? | `applyPreWindowAttributes()` — only `OPENGL1` has real work here |

Four families compute their window flags at **runtime**, because their own native API is itself a
runtime choice. This predates runtime renderer selection; the plan generalizes their existing
mechanism rather than inventing one:

| Renderer | Decides | Via |
|---|---|---|
| `BGFX` | Vulkan vs OpenGL/GLES | `Bgfx::Detail::ResolveRendererType()`, honouring `CNA_BGFX_RENDERER` |
| `LLGL` | OpenGL module needs a GL window; Vulkan module needs no flag | `Llgl::Detail::RendererModuleNeedsOpenGLWindow()` |
| `FNA3D` | SDL_GPU / D3D11 / OpenGL | `FNA3D_PrepareWindowAttributes`, which also primes the GL attributes |
| `DILIGENT` | D3D12 / Vulkan / D3D11 / OpenGL | `ParseDeviceTypeOverride()`, honouring `CNA_DILIGENT_DEVICE` |

`DILIGENT` carries a documented limitation here: SDL3 rejects a window created with both
`SDL_WINDOW_VULKAN` and `SDL_WINDOW_OPENGL`, so an `auto` build whose first preference fails at
runtime cannot fall through across that boundary against an already-created window
(plan_diligent.md DILIGENT-57).

---

## Intended API (not yet available)

```cpp
#include "CNA/GraphicsRendererSelection.hpp"

int main()
{
    // Before any GraphicsDevice is constructed:
    CNA::GraphicsRendererSelection::SetPreferred(CNA::GraphicsRendererType::Vulkan);

    // Optional, off by default: try these in order if Vulkan is unavailable or fails to start.
    CNA::GraphicsRendererSelection::SetFallbackChain({
        CNA::GraphicsRendererType::OpenGLES3,
        CNA::GraphicsRendererType::Software,
    });

    MyGame game;
    game.Run();
}
```

Selection precedence, highest first:

1. an explicit `SetPreferred()` call,
2. the `CNA_GRAPHICS_RENDERER` **environment variable**,
3. the build's compile-time default.

---

## Failure policy

**Default: hard failure.** If the selected renderer is not compiled into the build, reports itself
unavailable, or throws during initialization, CNA throws. There is no silent substitution — a game
that asked for Vulkan and quietly got a CPU rasterizer is a worse outcome than a clear error.

**Fallback is opt-in.** `SetFallbackChain()` or `EnableAutomaticFallback(true)` enables it. When
enabled:

- both failure modes are covered: the renderer's availability probe returning false, and its
  construction throwing;
- every step is logged at warning level and recorded in `GetFallbackHistory()`;
- `GetActive()` reports what was really created, which may differ from `GetSelected()`;
- if the chain is exhausted, CNA throws, carrying the first failure as the primary cause.

### The window-kind limitation

SDL3 refuses to create a window carrying both `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN`. Falling
back across that boundary therefore requires the SDL window to be destroyed and recreated, which is
only legal while CNA owns it. When the game supplied its own window through
`PresentationParameters::DeviceWindowHandle`, such a candidate is skipped and recorded as
`WindowKindConflict` rather than silently attempted.

---

## The latch

Once CNA has begun using the selected renderer, the selection can no longer change: `SetPreferred()`,
`SetFallbackChain()` and `EnableAutomaticFallback()` all throw `System::InvalidOperationException`
after the first `GraphicsDevice` is constructed.

The latch forbids *changing the selection*, not *creating a renderer again*: `GraphicsDevice::Reset()`
and its multisample reconstruction path legitimately rebuild the same renderer on a live device.

---

## Renderer combinations

Not every pair of renderers can be linked into one binary. Incompatible combinations are rejected at
**configure time with a reason**, never left to surface as a link error.

*(The full conflict matrix lands with the multi-renderer CMake mode; see `plan_runtimerenderer.md`
phase P6.)*

---

## See also

- `plan_runtimerenderer.md` — the design decisions and the full task breakdown.
- `modules/core/include/CNA/GraphicsRendererType.hpp` — the 46 public renderer identities.
- `cmake/RendererSelection.cmake` — compile-time selection.
