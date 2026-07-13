# Null Graphics Backend Implementation Plan

> **Status: proposed, not yet started.** No `NullGraphicsBackend` code exists yet — this file is the
> task breakdown for a new, fifth-plus `CNA_GRAPHICS_BACKEND` option: `NULL`. All tasks below are
> ⬜ until implementation begins.
>
> **Why a Null backend:** every existing backend (`SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`)
> needs a real window and a real GPU context to run at all, which makes them unsuitable for fast,
> headless, CI-friendly testing of game *logic* (as opposed to pixel output). A Null backend
> implements the full `IGraphicsBackend` surface (see `include/CNA/Internal/Backends/Common/
> IGraphicsBackend.hpp`) without touching a GPU or a window: it accepts every call, validates
> arguments, tracks resource lifecycles, and counts what happened — cheap to build, cheap to run,
> and useful in its own right (argument/lifecycle bugs that a real backend might silently tolerate
> or mask become hard failures here).
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Design decisions (recorded before implementation, not left implicit)

1. **One CMake backend, three runtime modes.** `CNA_GRAPHICS_BACKEND=NULL` selects a single build
   (one `NullGraphicsBackend` implementation), matching the existing `SDL_RENDERER`/`EASYGL`/
   `BGFX`/`VULKAN`/`WEBGPU` pattern exactly (see `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` cache
   variable and `CNA_BACKEND_<NAME>` option flags). `NullFast` / `NullValidation` / `NullTrace` are
   a **runtime** choice, not three separate compiled variants — selected via an environment
   variable (`CNA_NULL_MODE=Fast|Validation|Trace`, default `Validation`) and overridable
   programmatically before `Game::Run()`. Building three separate binaries for what is fundamentally
   a verbosity/strictness dial would triple build time for no real benefit; a single binary that
   branches on a small runtime flag is both cheaper to build and easier for a test harness to
   toggle per-test.
2. **No window is created at all**, not even a hidden one — `NullGraphicsBackend` does not touch
   SDL's video subsystem. `GraphicsDeviceManager`/`Game::Run()` must tolerate a backend that never
   produces a real `SDL_Window*` (see `NULL-4`); this is the mechanism that makes the backend
   genuinely usable in CI containers with no display server at all, not just a headless-but-present
   one.
3. **Mode semantics:**
   - `NullFast` — accepts everything, does the minimum bookkeeping needed for draw-call/resource
     counters, skips argument/bounds validation. For test runs that just need the game loop to
     execute quickly and are not specifically testing backend-argument correctness.
   - `NullValidation` (default) — full argument validation (bounds, disposed-object guards, state
     consistency) on top of `NullFast`'s counters; throws the same exception types real backends
     are expected to throw for equivalent misuse (mirrors the `ObjectDisposedException` guard
     pattern already established elsewhere in the codebase, e.g. Task 240's disposed-buffer checks).
   - `NullTrace` — everything `NullValidation` does, plus a structured call log (method name, args
     summary, frame index) and creation-site tracking for every resource, dumped at end of run.
     For diagnosing exactly *what* a failing test actually did, not just that it failed.
4. **Resource IDs, not GPU handles.** Every `Null*Backend` resource gets a monotonically increasing
   `NOXNA` debug ID instead of a real GPU handle. Leak reports and trace logs refer to resources by
   this ID plus (in `NullTrace` mode) their creation call site.

---

## Active execution order — do this one phase at a time

1. Phase N1 (CMake integration + skeleton) unblocks everything else — no other phase can be
   meaningfully worked on before `NullGraphicsBackend` exists and compiles as a selectable backend.
2. Phase N2 (resource backends) and Phase N3 (argument validation) are naturally sequenced together
   per resource type — validating `SetData()` bounds only makes sense once `NullVertexBufferBackend`
   itself exists and tracks capacity.
3. Phase N4 (counters/leak detection) depends on Phase N2's resource registry existing.
4. Phase N5 (`NullTrace` logging) is the highest-effort, lowest-urgency phase — build it last, once
   `NullValidation` is solid and something is actually worth tracing.
5. Phase N6 (headless `Game::Run()` integration) can start as soon as Phase N1's skeleton exists and
   should be verified continuously, not left to the end — it is the actual point of this backend.
6. Phase N7 (tests) — per this project's own convention (see `CLAUDE.md`), every public method
   needs test coverage added in the same task that implements it, not bolted on afterward.

For every task: build the affected target(s), run the relevant tests, and do not mark a task ✅
without both.

---

## Phase N1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-1 | Add `"NULL"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_NULL` option flag, following the exact existing pattern for `SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU` | ⬜ | |
| NULL-2 | Add a `cna_backend_graphics_null` static library target (mirrors `cna_backend_graphics_webgpu`'s own `elseif(CNA_GRAPHICS_BACKEND STREQUAL "...")` block) | ⬜ | |
| NULL-3 | Create `include/CNA/Internal/Backends/Null/NullGraphicsBackend.hpp` + `src/CNA/Internal/Backends/Null/NullGraphicsBackend.cpp`: a class implementing every `IGraphicsBackend` pure virtual with a real (not throwing-stub) no-op/tracking implementation | ⬜ | See `IGraphicsBackend.hpp` for the full 131-method surface across `IGraphicsBackend`, `IVertexBufferBackend`, `IIndexBufferBackend`, `IOcclusionQueryBackend`, `ITextureBackend`, `ITextureCubeBackend`, `ITexture3DBackend`, `IRenderTargetBackend`, `IRenderTargetCubeBackend`, `IEffectBackend`, `ISpriteBatchBackend` |
| NULL-4 | `CreateGraphicsBackend()` factory dispatch for `NULL`; the constructor must succeed with **no SDL window at all** — `GraphicsDeviceManager`/`Game::Run()` must be audited and, if needed, adjusted so a null `SDL_Window*` doesn't crash device/input/event-pump code paths that assume one exists | ⬜ | This is the task most likely to surface an existing implicit assumption elsewhere in the engine — treat any such finding as in-scope to fix, not a reason to fall back to a hidden window |
| NULL-5 | `CNA_NULL_MODE` environment-variable parsing (`Fast`/`Validation`/`Trace`, case-insensitive, default `Validation` on unrecognized/unset) plus a `NOXNA` programmatic override API (e.g. `NullGraphicsBackend::SetMode(NullMode)`) callable before `Game::Run()` | ⬜ | |
| NULL-6 | `docs/graphics-backend-feature-matrix.md` (or equivalent): add a `NULL` column documenting it as "headless/CI, no pixel output" rather than leaving it looking like a feature gap in the matrix | ⬜ | |

---

## Phase N2 — Resource lifecycle backends

Each resource backend tracks its own live/disposed state and registers with the shared resource
registry from `NULL-18`.

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-10 | `NullVertexBufferBackend` — tracks capacity, stride, current vertex count; `SetData`/`SetDataWithOptions` copy into an internal `std::vector` (not discarded) so buffer *contents* remain inspectable by tests, not just counted | ⬜ | |
| NULL-11 | `NullIndexBufferBackend` — same, both 16-bit and 32-bit | ⬜ | |
| NULL-12 | `NullTextureBackend` — tracks width/height/mip levels/format; `UpdatePixels`/`UpdatePixelsLevel` validate and store bytes (needed so `GetBackBufferData`-style round-trip tests can still assert on uploaded content even with no real GPU) | ⬜ | |
| NULL-13 | `NullTextureCubeBackend` | ⬜ | |
| NULL-14 | `NullTexture3DBackend` | ⬜ | |
| NULL-15 | `NullRenderTargetBackend` / `NullRenderTargetCubeBackend` — track size/format only; `SetRenderTarget`/`SetRenderTargets` update backend-internal "current target" state for validation (`NULL-23`) without needing an actual attachment | ⬜ | |
| NULL-16 | `NullEffectBackend` — accepts any GLSL/HLSL/WGSL source string without compiling it; tracks which uniforms were set (name → last value) so a test can assert "did the game set this uniform" without a real shader compiler | ⬜ | |
| NULL-17 | `NullSpriteBatchBackend` — records every `Draw()` call's arguments (texture id, rects, color, rotation, effects, depth) into a per-`Begin()`/`End()` batch log instead of building GPU vertex data | ⬜ | |
| NULL-18 | Shared resource registry: a single table (keyed by the `NOXNA` debug ID from the design decisions above) recording resource type, creation frame/time, and disposed/alive state for every `Null*Backend` instance created | ⬜ | Backbone for `NULL-33`/`NULL-34` |
| NULL-19 | Disposed-object guards: every method on every `Null*Backend` checks its own alive/disposed flag first and throws `std::runtime_error`/`ObjectDisposedException`-equivalent if called after disposal — mirrors the existing Task 240 pattern used by the real backends | ⬜ | Gated by `NullValidation`/`NullTrace`; skipped in `NullFast` per the mode semantics above |

---

## Phase N3 — Argument and API-contract validation

All tasks in this phase are active only in `NullValidation`/`NullTrace` mode (see design decision
3) — `NullFast` intentionally skips them.

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-20 | Validate `SetData()`/`SetDataWithOptions()` bounds: `offset + count` must not exceed the buffer's declared capacity; throw on violation instead of silently truncating or writing out of the tracked range | ⬜ | |
| NULL-21 | Validate `DrawPrimitives`/`DrawIndexedPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` argument consistency: a vertex (and, for indexed calls, index) buffer must actually be bound, `primitiveCount` must be consistent with the bound buffer's size and the given `PrimitiveType`, and the `PrimitiveType` value itself must be a recognized enumerator | ⬜ | |
| NULL-22 | Validate texture/effect state consistency before a draw call: e.g. `TextureEnabled=true` with no texture actually bound, or a bound texture whose backend type doesn't match what the vertex format/effect expects | ⬜ | |
| NULL-23 | Validate viewport/scissor rectangles and `SetRenderTarget(s)` calls against the currently-bound target's tracked size (from `NULL-15`) — catch off-target rectangles instead of silently accepting them | ⬜ | |
| NULL-24 | Validate state-object transitions: using a disposed `BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`, or passing `nullptr` where the real XNA API contract requires a non-null state object | ⬜ | |
| NULL-25 | A single `NullValidationException`-style exception type (or reuse of whatever exception convention `Task 240`'s guards already established) carrying the specific rule that was violated, not a generic message — needed so failing tests point directly at the actual mistake | ⬜ | |

---

## Phase N4 — Counting and leak detection

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-30 | `NullStatistics` struct: cumulative `DrawCallCount`, `PrimitiveCount`, `ClearCount`, `PresentCount`, and per-state-object-type `StateChangeCount` (blend/depth/rasterizer/sampler/viewport/scissor) | ⬜ | |
| NULL-31 | `NOXNA` public accessor, e.g. `NullGraphicsBackend::GetStatistics() const`, so a test can read counters mid-run without any backend-internal access | ⬜ | |
| NULL-32 | Per-frame snapshot in addition to the cumulative counters (reset at each `Present()`) — needed so a test can assert "this single `Draw()` call issued exactly N draw calls" without subtracting cumulative totals by hand | ⬜ | |
| NULL-33 | Resource creation/destruction counters, broken down by resource type (vertex buffer, index buffer, `Texture2D`, `TextureCube`, `Texture3D`, `RenderTarget2D`, `RenderTargetCube`, effect, `SpriteBatch`) plus a live "currently alive" count per type, backed by the `NULL-18` registry | ⬜ | |
| NULL-34 | Leak detection: at `NullGraphicsBackend` destruction (i.e. `GraphicsDevice`/`Game` teardown), walk the `NULL-18` registry and report every resource still marked alive — in `NullValidation` mode this should be a hard failure (throw or `assert`), not just a log line, so a leaking test fixture cannot pass silently | ⬜ | |
| NULL-35 | Explicit `NOXNA` `NullGraphicsBackend::AssertNoLeaks()` callable mid-test (not just at teardown), for tests that want to check "did this specific scene/level clean up after itself" without waiting for full `Game` shutdown | ⬜ | |

---

## Phase N5 — `NullTrace` mode: structured logging

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-40 | Structured call log: every `IGraphicsBackend`-surface method call recorded (method name, a short argument summary, frame index, monotonic call index) into an in-memory buffer | ⬜ | Only active in `NullTrace` — must have effectively zero overhead in `NullFast`/`NullValidation`, since this is the mode nobody should pay for by default |
| NULL-41 | Creation-site tracking: every `Null*Backend` resource created in `NullTrace` mode also records `std::source_location` (or an explicit debug-label parameter, whichever is more useful in this codebase's call sites) so a leak report can point at the exact `new Texture2D(...)`/`VertexBuffer(...)` call responsible | ⬜ | |
| NULL-42 | Trace log export: dump the accumulated log to stdout or a file at end of run, in a format that's easy to diff between two CI runs | ⬜ | |
| NULL-43 | (Aspirational, low priority) Trace-log comparison tooling: diff two runs' logs to catch behavioral drift in game logic between commits, independent of any pixel output | ⬜ | Do not block the rest of this plan on this task — it's a nice-to-have built on top of `NULL-40`/`42`, not a prerequisite for anything else here |

---

## Phase N6 — Headless `Game::Run()` integration

This is the actual point of the backend — the other phases exist to make this trustworthy.

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-50 | Verify `GraphicsDeviceManager`/`Game::Run()` completes a full init → update/draw loop → shutdown cycle against the `NULL` backend with **zero** SDL video-subsystem calls (confirm via `SDL_WasInit(SDL_INIT_VIDEO)` or equivalent in a test) | ⬜ | |
| NULL-51 | Verify `SpriteBatch`, every stock `Effect` (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`), and `Model.Draw()` all route through the `NULL` backend without requiring a display, a GPU, or throwing | ⬜ | |
| NULL-52 | `Mouse`/`Keyboard`/`GamePad` input backends: confirm (or, if needed, adjust) that input polling degrades gracefully with no real window to receive OS input events, rather than crashing on a null window handle | ⬜ | Cross-cutting with input code, not strictly "graphics", but a real blocker for headless game-logic tests if not handled |
| NULL-53 | CTest registration: a genuine headless smoke test — construct a `Game` subclass exercising `LoadContent`/`Update`/`Draw` against the `NULL` backend, run N frames, assert on `NullStatistics`, tear down, assert no leaks | ⬜ | This test should be cheap enough to run on *every* commit, unlike the GPU-backed backends' tests which need real hardware/a display |
| NULL-54 | `docs/`: document how/why to use the `NULL` backend for CI game-logic tests, explicitly distinguishing it from the pixel-asserted tests the other backends use (`NULL` proves "the game ran without crashing and did the right number of draws/state changes", not "the pixels are correct") | ⬜ | |

---

## Phase N7 — Tests

Per this project's existing convention: every public method added by a task in Phases N1–N6 must
get test coverage in the *same* task, not a separate later one. This phase exists to name the
cross-cutting test suites that don't belong to one single implementation task.

| # | Task | Status | Notes |
|---|---|---|---|
| NULL-60 | Bounds/argument-validation tests per `Null*Backend` class: confirm each `NULL-20`–`24` rule actually throws under `NullValidation` and does *not* throw under `NullFast` (proving the mode dial genuinely does something, not just cosmetic) | ⬜ | |
| NULL-61 | Draw-call/state-change counting tests: a known, fixed sequence of `Draw*`/`SetBlendState`/etc. calls produces the exact expected `NullStatistics` values, both cumulative and per-frame | ⬜ | |
| NULL-62 | Leak-detection tests: deliberately leak a resource (never `Dispose()`/never let it go out of scope) and confirm `AssertNoLeaks()`/teardown reports it; then dispose it and confirm the same run reports clean | ⬜ | |
| NULL-63 | Mode-switching tests: the exact same invalid-argument call throws under `NullValidation`/`NullTrace` and does not throw under `NullFast` | ⬜ | |
| NULL-64 | End-to-end headless test: a small synthetic game (a few sprites, one 3D model, one custom effect) runs N frames entirely under `NULL`, asserting both on `NullStatistics` and on captured `SpriteBatch`/`Effect` call data from `NULL-16`/`17`, with zero real rendering anywhere in the run | ⬜ | This is the test that actually proves the backend is useful, not just that it compiles |

---

## Boundaries (stop and ask, don't improvise)

- This backend must **never** silently succeed at something a real backend would reject — if a real
  backend's behavior for a given misuse is ambiguous or undocumented, that's a "stop and ask"
  moment, not a guess, since `NULL`'s whole value proposition is being a trustworthy stand-in.
- Do not let `NULL`-specific code leak into the shared `IGraphicsBackend`/`GpuDrawParams` interface
  layer beyond what a genuine common-interface need justifies — same backend-locality rule the
  other backends (see `CLAUDE.md`, `plan_webgpu.md`'s own boundaries) already follow.
- If `Game::Run()`/`GraphicsDeviceManager`/input code turns out to have deeper assumptions about a
  real window existing than `NULL-4`/`NULL-52` anticipated, treat that as a legitimate finding to
  fix (it's a real bug for headless use in general, not `NULL`-specific scope creep) — but if fixing
  it would require a genuinely large refactor, stop and flag it rather than pushing through
  unilaterally.
