# CNA — Cross-cutting / Deferred Task Plan

> A home for tasks that are **real and wanted** but do not belong to a single feature plan
> (`plan_input.md`, `plan_graphics.md`, …) — typically because they cross a layer boundary that
> the originating branch is scoped not to touch. Tasks here use an **`a-NNNN`** id scheme,
> independent of the numeric task bands used by the feature plans.
>
> A task lands here (instead of being dropped) when it is deferred by an explicit decision, so the
> reasoning and the full implementation scope are preserved for whoever picks it up later.

## Legend

| Symbol | Meaning |
|--------|---------|
| ⛔ | Deferred (not started; blocked on a decision or on another track) |
| 🔄 | In progress |
| ✅ | Done |

---

## a-0001 — `Mouse::SetPosition` inverse logical→window coordinate transform

**Status:** ✅ Done — implemented in Phase I10 task 846 (the deferral was lifted by the user, who
authorized the graphics-layer change in Phase I10's rule 6).

**Resolution.** Added the symmetric inverse virtual `IGraphicsBackend::TransformLogicalToWindow`
(default no-op passthrough) and implemented it in EasyGL (`window = logical * physH/virtualHeight_`,
the algebraic inverse of `TransformWindowToLogical`). `Mouse::SetPosition` now converts the caller's
logical coordinates to window space via a new `logical_to_window` helper (SDL_Renderer path via
`SDL_RenderCoordinatesToWindow`; other backends via `TransformLogicalToWindow`; pass-through when no
scaling transform exists) before `SDL_WarpMouseInWindow`, so the OS cursor lands at the correct
physical pixel on a scaled/letterboxed window. Vulkan/bgfx use the no-op passthrough (they don't do
logical-presentation scaling). Verified by `MouseInputTests` (`SetPositionConvertsLogicalToWindowFor
LetterboxedRenderer`, passing under both ambient Wayland and `SDL_VIDEODRIVER=x11`) and the stale
`Mouse.cpp` deviation comment was replaced (task 848). **2026-07-15 update**: `DX3` (`plan_freedirect.md`
DX3-68) now also implements a real `TransformLogicalToWindow` (a letterbox scale+offset computed
from the actual physical `SDL_Window` size) and gets `Mouse::SetPosition` correctness through this
exact same generic `logical_to_window` helper with zero DX3-specific code in `Mouse.cpp`. The
historical deferral rationale is retained
below for context.

---

**(Historical — original deferral rationale, superseded by the resolution above.)**

**Origin:** Phase I9 tasks **800** and **801** in `plan_input.md`, from the external (ChatGPT Plus)
review of the input work. Deferred here by explicit user decision (2026-07-04) rather than
implemented on the `feature/input` branch.

**Problem.** `Mouse::SetPosition(x, y)` (XNA takes coordinates in the game's logical/render space)
warps the OS cursor with `SDL_WarpMouseInWindow`, which expects **window-space** pixels. CNA has a
forward transform (`IGraphicsBackend::TransformWindowToLogical`, used by
`SdlInputBridge::to_logical_position` so `Mouse::GetState()` already reports logical coords) but
**no inverse**. So `SetPosition` currently passes the logical coordinates straight through as
window coordinates: correct when the window size equals the logical/render resolution, but off by
the letterbox/scale factor on a scaled or letterboxed window. Documented in-source in `Mouse.cpp`.

**Why deferred (not done on `feature/input`).** Fixing it requires adding a new virtual
`TransformLogicalToWindow` (the inverse of `TransformWindowToLogical`) to `IGraphicsBackend` and
implementing it in every backend — a **graphics-layer** change. `NEXT.md` §9 ("No graphics changes
on this branch") and `plan_input.md`'s own "Known limitations / deferred" table both scope this to
the graphics track. There is no way to implement task 800 without touching the graphics backend
interface, so it cannot be done cleanly inside the input branch.

**Implementation scope (when picked up).**
- Add `virtual bool TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY)`
  to `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (inverse of the existing
  `TransformWindowToLogical`; return `false` / identity when no scaling is active).
- Implement it per backend:
  - **SDL_Renderer:** `SDL_RenderCoordinatesToWindow` (the inverse of the
    `SDL_RenderCoordinatesFromWindow` already used in `to_logical_position`).
  - **EasyGL:** invert `EasyGLGraphicsBackend::TransformWindowToLogical`'s viewport/letterbox math.
  - **Vulkan / bgfx:** implement or fall back to identity pass-through, consistent with how each
    handles the forward transform.
- Wire it into `Mouse::SetPosition` (`src/Microsoft/Xna/Framework/Input/Mouse.cpp`): map `(x, y)`
  logical → window via the backend for the resolved window before `SDL_WarpMouseInWindow`. Remove
  the "known limitation" comment once real.
- **Task 801:** add backend/integration tests for `SetPosition` with logical size ≠ physical window
  size (e.g. a 2× letterboxed presentation), asserting the OS warp lands at the correct window
  pixel. If not verifiable headless, add it to the `examples/demo_input` manual checklist (Phase I9
  task 836) instead.

**References.**
- In-source deviation note: `src/Microsoft/Xna/Framework/Input/Mouse.cpp` (`SetPosition`).
- FNA's own approach: fixed `INTERNAL_WindowWidth/Height ÷ INTERNAL_BackBufferWidth/Height` ratio
  (`Mouse.cs:107-116`); CNA removed those fields in task 747 in favor of the general
  `IGraphicsBackend` transform, which is why an inverse on that interface is the right fix.
- `plan_input.md` Phase I9 tasks 800/801; `plan_graphics.md` (graphics track).
