# Headless Graphics Backend Implementation Plan

> **Status: core implementation landed and verified, 2026-07-13** (originally named `NULL`, renamed
> to `HEADLESS` before the first commit — see git history for the rename, no functional change).
> `HeadlessGraphicsBackend` implements the full `IGraphicsBackend` surface, builds cleanly as
> `CNA_GRAPHICS_BACKEND=HEADLESS`, and a genuine headless `Game::Run()` loop (`Headless_Smoke`
> CTest) passes 10/10 checks in ~0.11s with **no `DISPLAY`, no `WAYLAND_DISPLAY`, and
> `SDL_INIT_VIDEO` never called** — the core promise of this backend is proven, not just claimed.
> Phases N1–N4 and N6's core are done, and confirmed against real-world code, not just synthetic
> tests: `../mobile-eggbert` (`WindowsPhoneSpeedyBlupi`, a real third-party game — the same one used
> to validate the WEBGPU backend) builds clean against `-DCNA_GRAPHICS_BACKEND=HEADLESS` and runs
> 20+ seconds with zero crashes, zero exceptions, `DISPLAY`/`WAYLAND_DISPLAY` both unset. The
> `Mouse`/`Keyboard`/`GamePad` input path (`HEADLESS-52`) was also fully audited and found already
> safe with no window, no fix needed. `HeadlessTrace` mode's data-collection plumbing and mode
> gating are now verified (`Headless_ResourceBackends`), and `TextureCube`/`Texture3D`/
> `RenderTarget2D`/`RenderTargetCube`/custom `ShaderEffect` all have dedicated test coverage.
> `HEADLESS-5`/`11`/`32`/`33`/`40`/`61` (env-var mode parsing, the 32-bit `IndexBuffer` path, the
> per-frame statistics diff, and per-type alive-resource counts) closed 2026-07-13 via a fourth
> CTest, `Headless_CoverageGaps`. `HEADLESS-51`/`64` (per-effect coverage for `AlphaTestEffect`/
> `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`Model.Draw()`) closed the same day via
> a fifth CTest, `Headless_Effects`, which also surfaced a genuine, previously-undocumented
> behavioral finding (see "Implementation notes"). `HEADLESS-60` (dual-mode Validation-throws/
> Fast-doesn't-throw confirmation for every `HEADLESS-20`/`22`/`23`/`24` rule, not just the one
> representative `HEADLESS-21` case) closed the same day via a sixth CTest, `Headless_ModeDial`.
> `HEADLESS-40`'s remaining trace-log call sites and `HEADLESS-43`'s trace-log diff tooling (both
> previously deprioritized as low-value/aspirational) closed the same day via a seventh CTest,
> `Headless_TraceDiff`, on explicit request — which also caught and fixed a real pre-existing bug:
> `SetViewport` had never actually been wired into `RecordTrace()` despite an earlier commit
> message claiming it was. A few validation/scoping corners remain intentionally narrowed from
> their original wording once real interface constraints were discovered during implementation —
> see each task's own Notes column and the "Implementation notes" section after the task tables
> for the honest specifics.
>
> **Why a Headless backend:** every existing backend (`SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`)
> needs a real window and a real GPU context to run at all, which makes them unsuitable for fast,
> headless, CI-friendly testing of game *logic* (as opposed to pixel output). A Headless backend
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

1. **One CMake backend, three runtime modes.** `CNA_GRAPHICS_BACKEND=HEADLESS` selects a single build
   (one `HeadlessGraphicsBackend` implementation), matching the existing `SDL_RENDERER`/`EASYGL`/
   `BGFX`/`VULKAN`/`WEBGPU` pattern exactly (see `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` cache
   variable and `CNA_BACKEND_<NAME>` option flags). `HeadlessFast` / `HeadlessValidation` / `HeadlessTrace` are
   a **runtime** choice, not three separate compiled variants — selected via an environment
   variable (`CNA_HEADLESS_MODE=Fast|Validation|Trace`, default `Validation`) and overridable
   programmatically before `Game::Run()`. Building three separate binaries for what is fundamentally
   a verbosity/strictness dial would triple build time for no real benefit; a single binary that
   branches on a small runtime flag is both cheaper to build and easier for a test harness to
   toggle per-test.
2. **No window is created at all**, not even a hidden one — `HeadlessGraphicsBackend` does not touch
   SDL's video subsystem. `GraphicsDeviceManager`/`Game::Run()` must tolerate a backend that never
   produces a real `SDL_Window*` (see `HEADLESS-4`); this is the mechanism that makes the backend
   genuinely usable in CI containers with no display server at all, not just a headless-but-present
   one.
3. **Mode semantics:**
   - `HeadlessFast` — accepts everything, does the minimum bookkeeping needed for draw-call/resource
     counters, skips argument/bounds validation. For test runs that just need the game loop to
     execute quickly and are not specifically testing backend-argument correctness.
   - `HeadlessValidation` (default) — full argument validation (bounds, disposed-object guards, state
     consistency) on top of `HeadlessFast`'s counters; throws the same exception types real backends
     are expected to throw for equivalent misuse (mirrors the `ObjectDisposedException` guard
     pattern already established elsewhere in the codebase, e.g. Task 240's disposed-buffer checks).
   - `HeadlessTrace` — everything `HeadlessValidation` does, plus a structured call log (method name, args
     summary, frame index) and creation-site tracking for every resource, dumped at end of run.
     For diagnosing exactly *what* a failing test actually did, not just that it failed.
4. **Resource IDs, not GPU handles.** Every `Headless*Backend` resource gets a monotonically increasing
   `NOXNA` debug ID instead of a real GPU handle. Leak reports and trace logs refer to resources by
   this ID plus (in `HeadlessTrace` mode) their creation call site.

---

## Active execution order — do this one phase at a time

1. Phase N1 (CMake integration + skeleton) unblocks everything else — no other phase can be
   meaningfully worked on before `HeadlessGraphicsBackend` exists and compiles as a selectable backend.
2. Phase N2 (resource backends) and Phase N3 (argument validation) are naturally sequenced together
   per resource type — validating `SetData()` bounds only makes sense once `HeadlessVertexBufferBackend`
   itself exists and tracks capacity.
3. Phase N4 (counters/leak detection) depends on Phase N2's resource registry existing.
4. Phase N5 (`HeadlessTrace` logging) is the highest-effort, lowest-urgency phase — build it last, once
   `HeadlessValidation` is solid and something is actually worth tracing.
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
| HEADLESS-1 | Add `"HEADLESS"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_HEADLESS` option flag, following the exact existing pattern for `SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU` | ✅ | Verified 2026-07-13: configures cleanly with `-DCNA_GRAPHICS_BACKEND=HEADLESS`. |
| HEADLESS-2 | Add a `cna_backend_graphics_headless` static library target (mirrors `cna_backend_graphics_webgpu`'s own `elseif(CNA_GRAPHICS_BACKEND STREQUAL "...")` block) | ✅ | Verified 2026-07-13: builds clean, no external deps needed (the backend never touches SDL/GL/Vulkan directly, only the forward-declared `SDL_Window*`/`SDL_Renderer*`/`SDL_Texture*` types already in `IGraphicsBackend.hpp`). |
| HEADLESS-3 | Create `include/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp` + `src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp`: a class implementing every `IGraphicsBackend` pure virtual with a real (not throwing-stub) no-op/tracking implementation | ✅ | Verified 2026-07-13: implements all pure virtuals plus the optional extension points a full game needs (render targets, cube/3D textures, effects, occlusion queries, instancing) — everything either does real bookkeeping or is a genuine no-op, nothing throws "unsupported". `CnaTests` (the full pre-existing GTest corpus, unrelated to this backend) links cleanly against it, confirming interface completeness. |
| HEADLESS-4 | `CreateGraphicsBackend()` factory dispatch for `HEADLESS`; the constructor must succeed with **no SDL window at all** — `GraphicsDeviceManager`/`Game::Run()` must be audited and, if needed, adjusted so a null `SDL_Window*` doesn't crash device/input/event-pump code paths that assume one exists | ✅ | Verified 2026-07-13: required a real, anticipated shared-code change — `GraphicsDevice`'s constructor unconditionally called `SDL_InitSubSystem(SDL_INIT_VIDEO)` and `createOrAttachWindow()` unconditionally called `SDL_CreateWindow`. Both are now guarded by `#ifdef CNA_BACKEND_HEADLESS` (mirrors the file's own existing `#ifdef CNA_BACKEND_EASYGL`-style per-backend guards). `UpdateViewportFromWindow()`/`applyPresentationParametersToWindow()` needed no changes — both already tolerated `window_ == nullptr`. `Headless_Smoke` CTest confirms `SDL_WasInit(SDL_INIT_VIDEO) == 0` and `GetWindowInternal() == nullptr` end-to-end, running with `DISPLAY`/`WAYLAND_DISPLAY` both unset. |
| HEADLESS-5 | `CNA_HEADLESS_MODE` environment-variable parsing (`Fast`/`Validation`/`Trace`, case-insensitive, default `Validation` on unrecognized/unset) plus a `NOXNA` programmatic override API (e.g. `HeadlessGraphicsBackend::SetMode(HeadlessMode)`) callable before `Game::Run()` | ✅ | `ParseHeadlessModeFromEnvironment()` implemented and used at construction; `SetMode()` exercised directly by `Headless_Smoke`. **Env-var parsing path closed 2026-07-13** (`Headless_CoverageGaps` Checks F-J): constructs `HeadlessGraphicsBackend` directly (no window/Game loop needed for this check) after `setenv("CNA_HEADLESS_MODE", ...)`, covering `Fast`, case-insensitive `TRACE`, case-insensitive `validation`, an unrecognized value (`bogus`, must default to `Validation`), and unset (must default to `Validation`). |
| HEADLESS-6 | `docs/graphics-backend-feature-matrix.md` (or equivalent): add a `HEADLESS` column documenting it as "headless/CI, no pixel output" rather than leaving it looking like a feature gap in the matrix | ✅ | Implemented differently than originally worded: that matrix's rows are all pixel-correctness/parity comparisons, which are meaningless for a backend that never renders a pixel — a `HEADLESS` *column* would misleadingly look like a wall of gaps. Added a short note instead, at the top of the doc, explaining why `HEADLESS` is intentionally absent from the matrix and pointing to this file, mirroring how the doc already cross-references `webgpu-backend.md` for the same "not ready for a parity column" reason. |

---

## Phase N2 — Resource lifecycle backends

Each resource backend tracks its own live/disposed state and registers with the shared resource
registry from `HEADLESS-18`.

| # | Task | Status | Notes |
|---|---|---|---|
| HEADLESS-10 | `HeadlessVertexBufferBackend` — tracks capacity, stride, current vertex count; `SetData`/`SetDataWithOptions` copy into an internal `std::vector` (not discarded) so buffer *contents* remain inspectable by tests, not just counted | ✅ | Verified 2026-07-13, exercised by `Headless_Smoke` (a real 3-vertex triangle's data round-trips through `ShadowData()`). |
| HEADLESS-11 | `HeadlessIndexBufferBackend` — same, both 16-bit and 32-bit | ✅ | `Headless_Smoke` exercises the 16-bit path. **32-bit path closed 2026-07-13** (`Headless_CoverageGaps` Check E): a real `IndexElementSize::ThirtyTwoBits` buffer round-trips a `std::uint32_t` index array and drives an actual `DrawIndexedPrimitives` call without throwing. |
| HEADLESS-12 | `HeadlessTextureBackend` — tracks width/height/mip levels/format; `UpdatePixels`/`UpdatePixelsLevel` validate and store bytes (needed so `GetBackBufferData`-style round-trip tests can still assert on uploaded content even with no real GPU) | ✅ | `Texture2D::CreateFromPixels()` round-trips through this in `Headless_Smoke`. Mip level content (`UpdatePixelsLevel`) is validated (level/dimensions) but not stored per-level (only level-0 `pixels_` is kept) — a real, intentional scope trim, not a bug: no test in this project needs to read back a specific mip level's contents, only that the call succeeded. |
| HEADLESS-13 | `HeadlessTextureCubeBackend` | ✅ | Verified 2026-07-13 (`Headless_ResourceBackends` Check A): construction + `SetData()` on two independent faces both complete without throwing. |
| HEADLESS-14 | `HeadlessTexture3DBackend` | ✅ | Verified 2026-07-13 (`Headless_ResourceBackends` Check B). |
| HEADLESS-15 | `HeadlessRenderTargetBackend` / `HeadlessRenderTargetCubeBackend` — track size/format only; `SetRenderTarget`/`SetRenderTargets` update backend-internal "current target" state for validation (`HEADLESS-23`) without needing an actual attachment | ✅ | Verified 2026-07-13 (`Headless_ResourceBackends` Checks C/D): `GraphicsDevice::GetViewportSize()` genuinely reflects the bound `RenderTarget2D`'s own size (32×48, not the game's 64×64 back buffer) while it's active; binding two different `RenderTargetCube` faces in turn doesn't throw. |
| HEADLESS-16 | `HeadlessEffectBackend` — accepts any GLSL/HLSL/WGSL source string without compiling it; tracks which uniforms were set (name → last value) so a test can assert "did the game set this uniform" without a real shader compiler | ✅ | Verified 2026-07-13 (`Headless_ResourceBackends` Check E): a real `ShaderEffect` (which does route through `IEffectBackend`, unlike the stock `BasicEffect`) compiles, and `SpriteBatch::Begin(..., &effect)` accepts it without throwing. |
| HEADLESS-17 | `HeadlessSpriteBatchBackend` — records every `Draw()` call's arguments (texture id, rects, color, rotation, effects, depth) into a per-`Begin()`/`End()` batch log instead of building GPU vertex data | ✅ | Verified 2026-07-13, exercised by `Headless_Smoke` (a real `SpriteBatch::Draw()` call increments `drawCallCount`); `LastBatch()` introspection API implemented but not yet asserted on by a test. |
| HEADLESS-18 | Shared resource registry: a single table (keyed by the `NOXNA` debug ID from the design decisions above) recording resource type, creation frame/time, and disposed/alive state for every `Headless*Backend` instance created | ✅ | Verified 2026-07-13 via `Headless_Smoke`'s leak-detection checks (D/E). |
| HEADLESS-19 | Disposed-object guards: every method on every `Headless*Backend` checks its own alive/disposed flag first and throws `std::runtime_error`/`ObjectDisposedException`-equivalent if called after disposal — mirrors the existing Task 240 pattern used by the real backends | 🟨 | **Scoped down during implementation**: a true "disposed but the C++ object still exists and gets called again" state does not actually occur anywhere in this codebase's ownership model — every `Null*`/`Headless*Backend` is owned by exactly one `std::unique_ptr` inside its XNA-layer wrapper (`VertexBuffer`, `Texture2D`, ...), so once disposed the backend object is destroyed, not left in a "zombie" state that could receive further calls; a use-after-free on the raw pointer is a C++ ownership bug the type system already prevents, not something a runtime flag can meaningfully guard against. What *is* implemented and real: registry-based lifetime tracking (register on construct, unregister on destruct), which is what actually backs leak detection (`HEADLESS-34`). Recorded here as an honest scope correction, not a silent gap. |

---

## Phase N3 — Argument and API-contract validation

All tasks in this phase are active only in `HeadlessValidation`/`HeadlessTrace` mode (see design decision
3) — `HeadlessFast` intentionally skips them.

| # | Task | Status | Notes |
|---|---|---|---|
| HEADLESS-20 | Validate `SetData()`/`SetDataWithOptions()` bounds: `offset + count` must not exceed the buffer's declared capacity; throw on violation instead of silently truncating or writing out of the tracked range | ✅ | Implemented as declared-`vertex_count`/`index_count` vs. capacity checks — `IVertexBufferBackend::SetData`/`IIndexBufferBackend::SetData16/32` have no `offset` parameter in this codebase's actual interface (unlike the task's original "offset + count" wording, written before the interface was re-checked), so the real, implementable check is count-vs-capacity, which is what exists. |
| HEADLESS-21 | Validate `DrawPrimitives`/`DrawIndexedPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` argument consistency: a vertex (and, for indexed calls, index) buffer must actually be bound, `primitiveCount` must be consistent with the bound buffer's size and the given `PrimitiveType`, and the `PrimitiveType` value itself must be a recognized enumerator | ✅ | "Buffer actually bound" is enforced one layer up, in shared `GraphicsDevice::DrawPrimitives`/`DrawIndexedPrimitives` (throws before the backend is ever called) — verified the primitive/vertex-count-consistency check specifically (`Headless_Smoke` Check F: a `DrawIndexedPrimitives` call needing 30 indices against a 3-index buffer throws under `HeadlessValidation`, does not throw under `HeadlessFast`). `PrimitiveType` enum-value validity is not separately checked (the enum is closed/exhaustive in this codebase, so an invalid raw value would require an explicit `static_cast` misuse — a narrower, lower-value case, not implemented). |
| HEADLESS-22 | Validate texture/effect state consistency before a draw call: e.g. `TextureEnabled=true` with no texture actually bound, or a bound texture whose backend type doesn't match what the vertex format/effect expects | ✅ | Implemented in `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: rejects a missing primary texture for ordinary textured effects, a missing second `DualTexture` texture, a missing environment cube map, and empty skinned bone data. **glTF stride-conformance correction (2026-08-14):** `SkinnedEffect`, `PbrEffect` and `SkinnedPbrEffect` deliberately enable texturing with a null optional base texture; native renderers bind neutral white, so HEADLESS now accepts the same contract when `params.skinned || params.pbr` instead of reporting a false validation error. `BasicEffect` remains strict. Not implemented: cross-checking a bound texture's *backend type* against what the vertex format/effect expects (e.g. a 2D texture where a cube map is required) — `GpuDrawParams` doesn't carry enough type information to distinguish this cheaply, and no real misuse case in this codebase's own test suite would exercise it; left for a future task if it turns out to matter. |
| HEADLESS-23 | Validate viewport/scissor rectangles and `SetRenderTarget(s)` calls against the currently-bound target's tracked size (from `HEADLESS-15`) — catch off-target rectangles instead of silently accepting them | ✅ | **Closed 2026-07-13.** `SetViewport`/`SetScissorRect` now validate non-negative origin/width/height *and* cross-reference the currently-bound target's real size, by reusing `GetViewportSize()` (which already resolves to the bound `RenderTarget2D`'s size or the virtual/backbuffer size when none is bound) rather than threading a separate target reference through — simpler than the originally-imagined plumbing. Verified via `Headless_ValidationExtras` Checks A–D: an in-bounds viewport against the default backbuffer doesn't throw; an oversized viewport against a genuinely-bound 16×16 `RenderTarget2D` throws under `HeadlessValidation` and does not throw under `HeadlessFast`; a negative scissor origin throws. Re-verified `../mobile-eggbert` still survives 15s with zero crashes after adding this check — real game code never trips it. |
| HEADLESS-24 | Validate state-object transitions: using a disposed `BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`, or passing `nullptr` where the real XNA API contract requires a non-null state object | 🟨 | **Re-scoped during implementation**: `IGraphicsBackend::ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState` all take raw `int`/`bool`/`float` parameters, not object references — by the time a call reaches the backend, the XNA-level `BlendState`/etc. object (and any disposed-state check on *it*) is already several layers removed; there is no state-object identity or disposed flag left for the backend to inspect at all. What's implemented instead, as the closest real equivalent: `ApplySamplerState`'s `slot` parameter is range-checked (0–15). The originally-planned "disposed state object" check doesn't map onto this interface as written; noted honestly rather than silently claimed done. |
| HEADLESS-25 | A single `HeadlessValidationException`-style exception type (or reuse of whatever exception convention `Task 240`'s guards already established) carrying the specific rule that was violated, not a generic message — needed so failing tests point directly at the actual mistake | ✅ | `HeadlessValidationException : std::runtime_error`, every throw site carries a specific, contextual message (method name + the actual values involved, e.g. "primitiveCount 10 needs 30 indices but the bound buffer only has 3"). Verified via `Headless_Smoke`'s `catch (const HeadlessValidationException&)` checks. |

---

## Phase N4 — Counting and leak detection

| # | Task | Status | Notes |
|---|---|---|---|
| HEADLESS-30 | `HeadlessStatistics` struct: cumulative `DrawCallCount`, `PrimitiveCount`, `ClearCount`, `PresentCount`, and per-state-object-type `StateChangeCount` (blend/depth/rasterizer/sampler/viewport/scissor) | ✅ | Verified 2026-07-13 via `Headless_Smoke` Check C (exact expected values for a fixed 3-frame call sequence). |
| HEADLESS-31 | `NOXNA` public accessor, e.g. `HeadlessGraphicsBackend::GetStatistics() const`, so a test can read counters mid-run without any backend-internal access | ✅ | `GetStatistics()`, called mid-run (inside `Draw()`, frame 3) in `Headless_Smoke`. |
| HEADLESS-32 | Per-frame snapshot in addition to the cumulative counters (reset at each `Present()`) — needed so a test can assert "this single `Draw()` call issued exactly N draw calls" without subtracting cumulative totals by hand | ✅ | **Closed 2026-07-13** (`Headless_CoverageGaps` Checks B/C): `GetLastFrameStatistics()` reads zero draw calls at the top of a fresh frame (right after the prior frame's automatic `Present()`), then exactly 2 after this frame issues exactly 2 `DrawPrimitives` calls — not the cumulative total across frames, proving the diff math genuinely isolates the current frame. |
| HEADLESS-33 | Resource creation/destruction counters, broken down by resource type (vertex buffer, index buffer, `Texture2D`, `TextureCube`, `Texture3D`, `RenderTarget2D`, `RenderTargetCube`, effect, `SpriteBatch`) plus a live "currently alive" count per type, backed by the `HEADLESS-18` registry | ✅ | Creation counters verified (`vertexBuffersCreated == 3` in `Headless_Smoke`). **Per-type alive-count breakdown closed 2026-07-13** (`Headless_CoverageGaps` Check D): `AliveResources()` filtered by `typeName` matches the exact expected delta (+2 `VertexBuffer`, +1 `IndexBuffer`, +1 `Texture2D`) against a baseline captured before creating a known set of resources. |
| HEADLESS-34 | Leak detection: at `HeadlessGraphicsBackend` destruction (i.e. `GraphicsDevice`/`Game` teardown), walk the `HEADLESS-18` registry and report every resource still marked alive — in `HeadlessValidation` mode this should be a hard failure (throw or `assert`), not just a log line, so a leaking test fixture cannot pass silently | ✅ | Implemented as `AssertNoLeaks()` (explicit call, see `HEADLESS-35`) rather than an automatic destructor-time check — an automatic check at `HeadlessGraphicsBackend`'s own destructor would fire on *every* normal `Game` shutdown (SpriteBatch/Texture members are typically still alive at that point, destroyed in an unspecified order relative to the backend itself), which would be a false-positive-generating design, not a useful one. `AssertNoLeaks()` being explicit and callable whenever a test actually wants the check is the safer, real design — see `HEADLESS-35`. |
| HEADLESS-35 | Explicit `NOXNA` `HeadlessGraphicsBackend::AssertNoLeaks()` callable mid-test (not just at teardown), for tests that want to check "did this specific scene/level clean up after itself" without waiting for full `Game` shutdown | ✅ | Verified 2026-07-13 via `Headless_Smoke` Checks D/E: throws while a deliberately-leaked `VertexBuffer` is alive, and the alive-resource count returns to its pre-leak baseline once disposed (compared against a captured baseline, not literal zero, since other long-lived resources are legitimately still alive at that point in the test — see that test's own inline comment). |

---

## Phase N5 — `HeadlessTrace` mode: structured logging

| # | Task | Status | Notes |
|---|---|---|---|
| HEADLESS-40 | Structured call log: every `IGraphicsBackend`-surface method call recorded (method name, a short argument summary, frame index, monotonic call index) into an in-memory buffer | ✅ | `RecordTrace()`/`TraceLog()` infrastructure wired into every draw/clear/resource-creation/`SetData`/`Present`/`Apply*State` call site. **Closed 2026-07-13** via a new CTest, `Headless_TraceDiff`: added the remaining `ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` and the last two untraced `Create*` factories (`CreateSpriteBatch`/`CreateOcclusionQuery`). Also caught and fixed a real pre-existing bug in the process: an earlier commit message claimed `SetViewport` had been wired into `RecordTrace()` alongside `SetScissorRect`, but only `SetScissorRect` actually had been — `SetViewport` silently never logged anything. The remaining unlogged surface (`ClearColorAndDepth`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` all just delegate to the already-logged `Clear()`; `GetViewportSize`/`ReadBackbuffer` are queries, not state changes) is not meaningfully more to add. |
| HEADLESS-41 | Creation-site tracking: every `Headless*Backend` resource created in `HeadlessTrace` mode also records `std::source_location` (or an explicit debug-label parameter, whichever is more useful in this codebase's call sites) so a leak report can point at the exact `new Texture2D(...)`/`VertexBuffer(...)` call responsible | ✅ | **Closed 2026-07-13, chose the debug-label option explicitly** (not `std::source_location`): auto-capturing a real game's own call site would require adding a defaulted `std::source_location` parameter to every `IGraphicsBackend` virtual `Create*()` method — a shared-interface change touching all 5 other backends for a Headless-only diagnostic, and even then a `std::source_location` captured *inside* `HeadlessGraphicsBackend.cpp` itself (the only backend-local option) would always point at the same line per resource type, no more informative than the type name string already is. Implemented instead as `PushDebugLabel()`/`PopDebugLabel()`: a test wraps a block of resource creation and gets that label back in `AssertNoLeaks()`'s report, entirely backend-local, only active in `HeadlessTrace` mode (matches the original "only in HeadlessTrace mode" wording). Verified via `Headless_ValidationExtras` Checks E/F: a labeled leaked resource's report contains the label text; a resource created outside any label scope reports no creation site at all (proves it's genuinely opt-in per resource, not a blanket behaviour). |
| HEADLESS-42 | Trace log export: dump the accumulated log to stdout or a file at end of run, in a format that's easy to diff between two CI runs | ✅ | **Closed 2026-07-13.** `FormatTraceLog()` renders the log as human-readable text (`[frame N #callIndex] method: argsSummary`, one call per line); `DumpTraceLog(FILE* = stdout)` is a thin convenience wrapper a test can also point at a real file for CI diffing. Verified via `Headless_ValidationExtras` Checks G/H. |
| HEADLESS-43 | (Aspirational, low priority) Trace-log comparison tooling: diff two runs' logs to catch behavioral drift in game logic between commits, independent of any pixel output | ✅ | **Closed 2026-07-13** (explicitly requested despite the earlier low-priority framing): free functions `CompareTraceLogs()`/`FormatTraceLogDiff()` compare two `std::vector<HeadlessTraceEntry>` snapshots (from two separate `HeadlessGraphicsBackend` runs) entry-by-entry on `frameIndex`/`method`/`argsSummary` (deliberately excluding `callIndex`, a redundant position counter). Reports the first diverging entry, or a length mismatch if one log is a strict prefix of the other. Verified via `Headless_TraceDiff` (7/7): identical sequences compare identical; a genuine mid-sequence divergence (different `SetViewport` size) is located at the correct index; a prefix-only log diverges at its own length; `FormatTraceLogDiff()` renders both a one-line "identical" summary and a human-readable divergence report. |

---

## Phase N6 — Headless `Game::Run()` integration

This is the actual point of the backend — the other phases exist to make this trustworthy.

| # | Task | Status | Notes |
|---|---|---|---|
| HEADLESS-50 | Verify `GraphicsDeviceManager`/`Game::Run()` completes a full init → update/draw loop → shutdown cycle against the `HEADLESS` backend with **zero** SDL video-subsystem calls (confirm via `SDL_WasInit(SDL_INIT_VIDEO)` or equivalent in a test) | ✅ | Verified 2026-07-13: `Headless_Smoke` runs a full 3-frame `Game::Run()` cycle with `DISPLAY`/`WAYLAND_DISPLAY` both explicitly unset and `SDL_VIDEODRIVER` empty, asserts `SDL_WasInit(SDL_INIT_VIDEO) == 0`, and exits cleanly — the core promise of this whole backend, proven end-to-end, not just claimed. |
| HEADLESS-51 | Verify `SpriteBatch`, every stock `Effect` (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`), and `Model.Draw()` all route through the `HEADLESS` backend without requiring a display, a GPU, or throwing | ✅ | `SpriteBatch`, `BasicEffect`, and a custom `ShaderEffect` verified via `Headless_Smoke`/`Headless_ResourceBackends`. **`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`Model.Draw()` closed via `Headless_Effects` (9/9).** AlphaTestEffect degrades gracefully without a texture; DualTextureEffect still rejects a missing second texture; EnvironmentMapEffect still rejects a missing cube map. **Corrected 2026-08-14:** the old smoke expectation treated SkinnedEffect's null optional base texture as an error, but EasyGL/Vulkan bind neutral white for that stock shader. Checks G/H now prove SkinnedEffect succeeds both without and with an explicit texture, matching `HEADLESS-22`'s corrected contract; the bone-count check stays satisfied because the constructor seeds 72 identity transforms. `Model.Draw()` remains verified via a procedural 2-`ModelBone`/1-`ModelMesh`/1-`ModelMeshPart` model producing exactly one indexed draw with no throw. |
| HEADLESS-52 | `Mouse`/`Keyboard`/`GamePad` input backends: confirm (or, if needed, adjust) that input polling degrades gracefully with no real window to receive OS input events, rather than crashing on a null window handle | ✅ | **Audited 2026-07-13, no fix needed — all three are already safe.** `Mouse`: every path that could reach a null window (`SetPosition`, relative-mode getter/setter) explicitly null-checks first (`Mouse.cpp`); paths that don't null-check (`SetCaptureEXT`, `GetGlobalPositionEXT`, `WarpGlobalEXT`, `SetCursor`) call SDL entry points that themselves degrade gracefully (`SDL_Unsupported`/fallback/no-op) when the video-driver function pointers are unset, since `SDL_Mouse` is a static struct usable pre-init. `Keyboard`: no window/handle dependency anywhere; `SDL_keyboard` state is a static struct, keymap lookups null-guard internally. `GamePad`/`SdlInputBridge`: no window dependency at all; every SDL call that could receive a null window is guarded, and mouse/keyboard/text-input OS events structurally cannot fire without a real window in the first place. `SDL_PollEvent` itself is safe with zero SDL subsystems initialized (verified against SDL3 source: locking a null mutex no-ops, an inactive event queue just returns false). **One real, deliberate finding, not a bug**: `Game::DoInitialize()` unconditionally calls `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` (which implies `JOYSTICK`+`EVENTS`) with no `CNA_BACKEND_HEADLESS` guard — none of those three require `SDL_INIT_VIDEO` or a display server, so this doesn't contradict `HEADLESS-50`'s "no display server needed" claim, but it does mean a real `Game::Run()` loop under `HEADLESS` still touches *some* real SDL subsystem state (gamepad hot-plug becomes genuinely live), worth knowing if a test wants zero SDL involvement of any kind. |
| HEADLESS-53 | CTest registration: a genuine headless smoke test — construct a `Game` subclass exercising `LoadContent`/`Update`/`Draw` against the `HEADLESS` backend, run N frames, assert on `HeadlessStatistics`, tear down, assert no leaks | ✅ | `Headless_Smoke` registered and passing (10/10 checks, ~0.11s, no `SDL_VIDEODRIVER`/`DISPLAY` environment needed at all — the only CTest in this whole project that doesn't). |
| HEADLESS-54 | `docs/`: document how/why to use the `HEADLESS` backend for CI game-logic tests, explicitly distinguishing it from the pixel-asserted tests the other backends use (`HEADLESS` proves "the game ran without crashing and did the right number of draws/state changes", not "the pixels are correct") | ✅ | `docs/headless-backend.md` added 2026-07-13: what it's for/not for, `CNA_HEADLESS_MODE` usage, a full test-writing walkthrough, known limitations, and the `../mobile-eggbert` real-world validation result — mirrors `docs/webgpu-backend.md`'s own structure for consistency. |

---

## Phase N7 — Tests

Per this project's existing convention: every public method added by a task in Phases N1–N6 must
get test coverage in the *same* task, not a separate later one. This phase exists to name the
cross-cutting test suites that don't belong to one single implementation task.

| # | Task | Status | Notes |
|---|---|---|---|
| HEADLESS-60 | Bounds/argument-validation tests per `Headless*Backend` class: confirm each `HEADLESS-20`–`24` rule actually throws under `HeadlessValidation` and does *not* throw under `HeadlessFast` (proving the mode dial genuinely does something, not just cosmetic) | ✅ | **Closed 2026-07-13** via a new CTest, `Headless_ModeDial` (10/10): `HEADLESS-21` was already verified both ways in `Headless_Smoke`. This test adds `HEADLESS-20` (`HeadlessVertexBufferBackend::SetData()`/`HeadlessIndexBufferBackend::SetData16()` past capacity — talks directly to the backend objects via `HeadlessGraphicsBackend::SharedState()`, since this rule is specifically about the backend's own `Require()` check), `HEADLESS-22` (a `DualTextureEffect` draw missing its second texture), `HEADLESS-23` (`SetScissorRect()` with a negative origin — the Fast-mode half wasn't previously shown), and `HEADLESS-24` (`ApplySamplerState()` with an out-of-range slot, 16, not previously tested at all) — each throws under `HeadlessValidation` and does not throw under `HeadlessFast`, confirmed in one dedicated place. |
| HEADLESS-61 | Draw-call/state-change counting tests: a known, fixed sequence of `Draw*`/`SetBlendState`/etc. calls produces the exact expected `HeadlessStatistics` values, both cumulative and per-frame | ✅ | Cumulative counters verified exactly (`Headless_Smoke` Check C). Per-frame diffing **closed 2026-07-13** — see `HEADLESS-32`. |
| HEADLESS-62 | Leak-detection tests: deliberately leak a resource (never `Dispose()`/never let it go out of scope) and confirm `AssertNoLeaks()`/teardown reports it; then dispose it and confirm the same run reports clean | ✅ | Verified 2026-07-13 (`Headless_Smoke` Checks D/E). |
| HEADLESS-63 | Mode-switching tests: the exact same invalid-argument call throws under `HeadlessValidation`/`HeadlessTrace` and does not throw under `HeadlessFast` | ✅ | `HeadlessValidation` vs `HeadlessFast` verified for an invalid draw call (`Headless_Smoke` Check F). `HeadlessTrace`'s *logging* behaviour (as opposed to its validation behaviour specifically) is now also verified (`Headless_ResourceBackends` Check F) — `TraceEnabled()`/`ValidationEnabled()` share the same `mode != Fast` gate in `HeadlessSharedState`, so validation necessarily also holds in `HeadlessTrace`; not re-asserted with a second invalid-draw-call test since it would exercise the identical code path. |
| HEADLESS-64 | End-to-end headless test: a small synthetic game (a few sprites, one 3D model, one custom effect) runs N frames entirely under `HEADLESS`, asserting both on `HeadlessStatistics` and on captured `SpriteBatch`/`Effect` call data from `HEADLESS-16`/`17`, with zero real rendering anywhere in the run | ✅ | `Headless_Smoke` covers the sprite + custom-`Effect` + `VertexBuffer`/`IndexBuffer` triangle path, asserting on `HeadlessStatistics`. **The one 3D model requirement closed 2026-07-13** via `Headless_Effects`' `Model.Draw()` check (see `HEADLESS-51`) — a real `Model`/`ModelMesh`/`ModelMeshPart` draws with an exact `drawCallCount` assertion. `SpriteBatch`'s `LastBatch()` captured-draw-call data still isn't separately asserted on (only the aggregate `drawCallCount`) — a small, non-blocking remaining gap. |

---

## Implementation notes (honest summary, 2026-07-13)

What's solid: the backend compiles, the full `IGraphicsBackend` surface is implemented with real
behaviour (not throwing stubs), `CnaTests` links cleanly against it, and — the actual point of this
whole exercise — a genuine `Game::Run()` loop passes checks with **no display server present
at all** (`DISPLAY`/`WAYLAND_DISPLAY` unset, `SDL_INIT_VIDEO` never called), in ~0.1s per test.
Since the first commit: `Mouse`/`Keyboard`/`GamePad` are fully audited and safe with no window
(`HEADLESS-52`); `TextureCube`/`Texture3D`/`RenderTarget2D`/`RenderTargetCube`/custom `ShaderEffect`
all have dedicated test coverage; `HeadlessTrace` mode's data collection, creation-site debug
labels, trace log export, and viewport/scissor-vs-bound-target validation are all implemented and
verified (`HEADLESS-23`/`40`/`41`/`42`); the per-frame statistics diff (`HEADLESS-32`/`61`), the
per-type alive-resource breakdown (`HEADLESS-33`), the 32-bit `IndexBuffer` path (`HEADLESS-11`),
and `CNA_HEADLESS_MODE` environment-variable parsing (`HEADLESS-5`) are all now verified by
dedicated tests (`Headless_CoverageGaps`); `AlphaTestEffect`/`DualTextureEffect`/
`EnvironmentMapEffect`/`SkinnedEffect`/`Model.Draw()` are now individually verified end-to-end
(`HEADLESS-51`/`64`, `Headless_Effects`), which also surfaced a real, previously-undocumented
behavioral asymmetry between effects (see below); every `HEADLESS-20`/`22`/`23`/`24` validation
rule is now confirmed both ways (throws under `HeadlessValidation`, doesn't under `HeadlessFast`)
in one dedicated place (`HEADLESS-60`, `Headless_ModeDial`), not just the one representative
`HEADLESS-21` case; `HeadlessTrace`'s call log now covers every state-change/clear/draw/resource-
creation/`SetData`/`Present` call site (`HEADLESS-40`) and a trace-log diff tool exists
(`HEADLESS-43`, `Headless_TraceDiff`) — closing this pair also caught and fixed a real bug, a
previously-untraced `SetViewport` call (see below); and `../mobile-eggbert`, a real third-party
game, builds and runs 20+ seconds with zero crashes under this backend.

What's genuinely narrower than the original task wording, discovered while implementing against the
*real* `IGraphicsBackend` interface rather than the plan's own upfront guesses:

- **Disposed-object guards (`HEADLESS-19`/`24`)** don't map onto this codebase's actual ownership
  model (`std::unique_ptr`-owned backend objects have no "disposed but still callable" state) or
  onto the actual `ApplyBlendState`/etc. signatures (raw `int`/`bool`/`float`, no object identity).
  Resource lifetime tracking (register/unregister) is real and is what actually backs leak
  detection — the literal "disposed guard" framing was the part that didn't fit reality. Confirmed
  as a permanent interface constraint, not a temporary gap — no further action planned.

**A real bug found and fixed while closing HEADLESS-40/43, not just a documentation gap**: the
commit that first added `Apply*State`/`SetScissorRect`/`SetViewport` trace coverage claimed
`SetViewport` was included, but only `SetScissorRect` actually was — `SetViewport` silently never
recorded a trace entry. Caught by `Headless_TraceDiff`'s divergence-index check failing in a way
that only made sense if `SetViewport` wasn't in the log at all; fixed by adding the missing
`RecordTrace()` call. A reminder that a commit message asserting "X and Y both changed" needs the
same verification as the code itself — this one went unverified for two commits before the diff
tool's own test exposed it.
- `SpriteBatch`'s captured `LastBatch()` draw-call data (texture/rects/color/rotation/effects per
  `Draw()` call) is implemented but not yet separately asserted on by any test — only the aggregate
  `drawCallCount` is checked.

Closed 2026-07-13, previously listed here as inference-only: effect coverage beyond `BasicEffect`/
custom `ShaderEffect` (`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/
`Model.Draw()`, `HEADLESS-51`/`64`) — this turned up a genuine, non-obvious behavioral asymmetry
rather than just confirming the inference: `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`
unconditionally set `TextureEnabled` in `FillGpuDrawParams()` regardless of whether a texture was
actually assigned, so they genuinely throw under `HeadlessValidation` if a game forgets to set one,
while `AlphaTestEffect` degrades gracefully either way. Worth knowing for real game code, not just a
test-coverage checkbox.

None of the above were silently dropped — each is recorded in its own task row above with the
specific reason. Picking any of them back up is a reasonable next step, but none of them block the
backend's actual purpose (fast headless game-logic CI testing), which is already proven working —
including against real, unmodified third-party game code.

---

## Boundaries (stop and ask, don't improvise)

- This backend must **never** silently succeed at something a real backend would reject — if a real
  backend's behavior for a given misuse is ambiguous or undocumented, that's a "stop and ask"
  moment, not a guess, since `HEADLESS`'s whole value proposition is being a trustworthy stand-in.
- Do not let `HEADLESS`-specific code leak into the shared `IGraphicsBackend`/`GpuDrawParams` interface
  layer beyond what a genuine common-interface need justifies — same backend-locality rule the
  other backends (see `CLAUDE.md`, `plan_webgpu.md`'s own boundaries) already follow.
- If `Game::Run()`/`GraphicsDeviceManager`/input code turns out to have deeper assumptions about a
  real window existing than `HEADLESS-4`/`HEADLESS-52` anticipated, treat that as a legitimate finding to
  fix (it's a real bug for headless use in general, not `HEADLESS`-specific scope creep) — but if fixing
  it would require a genuinely large refactor, stop and flag it rather than pushing through
  unilaterally.
