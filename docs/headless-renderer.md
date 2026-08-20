# Headless graphics renderer

## Status

The Headless renderer is a **CI/testing-only graphics renderer**, verified 2026-07-13. Select it
with:

```bash
cmake -S . -B cmake-build-headless \
  -DCNA_GRAPHICS_RENDERER=HEADLESS \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-headless -j
```

No extra dependencies are needed — the Headless renderer never links against SDL's graphics
functions, OpenGL, Vulkan, or any GPU library. It only needs the same SDL3/SDL3_image/SDL3_mixer
and `../sharp-runtime` checkout every other renderer already requires.

## What this renderer is for (and isn't)

Every other CNA renderer (`SDL_RENDERER`/`OPENGLES3`/`BGFX`/`VULKAN`/`WEBGPU`) needs a real window and
a real GPU context to run at all. That makes them unsuitable for fast, CI-friendly testing of game
*logic* — a CI container with no display server can't run them, and even where a virtual display
(Xvfb) is available, spinning one up per test run is slow compared to what a logic test actually
needs to prove.

The Headless renderer implements CNA's entire `IGraphicsRenderer` interface without touching a GPU or
a window at all: `SDL_InitSubSystem(SDL_INIT_VIDEO)` is never called, no `SDL_Window` is ever
created, and every draw/resource call does real bookkeeping (argument validation, resource
lifecycle tracking, draw-call/state-change counters) instead of talking to a GPU.

**What it proves:** "this code path ran, with these arguments, this many times, and nothing leaked
or misused an API." **What it does *not* prove:** anything about pixel correctness. If you need to
know whether something actually *looks* right, use one of the real GPU renderers' pixel-asserted
tests (see `docs/graphics-renderer-feature-matrix.md`) — Headless renders nothing, by design.

## Runtime modes

Unlike the other renderers, Headless has a single build with a **runtime** strictness dial, since
building three separate binaries for a verbosity/strictness setting would triple build time for no
benefit. Select it via the `CNA_HEADLESS_MODE` environment variable, or override it programmatically
before `Game::Run()`:

| Mode | Behaviour |
|---|---|
| `Fast` | Minimum bookkeeping (draw-call/resource counters only), skips all argument/bounds validation. For test runs that just need the game loop to execute quickly. |
| `Validation` (default) | Full argument validation on top of `Fast`'s counters — throws `HeadlessValidationException` for the same kind of misuse a real renderer is expected to reject (out-of-range draw calls, oversized `SetData()` calls, `TextureEnabled=true` with no texture bound, etc). |
| `Trace` | Everything `Validation` does, plus a structured call log (method name, argument summary, frame index) accumulated in memory for later inspection. |

```bash
CNA_HEADLESS_MODE=Fast ./my_test
```

```cpp
auto& renderer = static_cast<CNA::Internal::Renderers::Headless::HeadlessRenderer&>(
    graphicsDevice.GetRenderer());
renderer.SetMode(CNA::Internal::Renderers::Headless::HeadlessMode::Fast);
```

## Writing a headless test

A headless test is a normal `Game` subclass, run exactly like any other CNA example — the only
difference is the renderer selected at CMake configure time. See
`modules/renderers/headless/examples/headless_smoke_test.cpp` and `modules/renderers/headless/examples/headless_resource_renderers_test.cpp` for full
working examples. The pattern:

```cpp
class MyHeadlessTest : public Game
{
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<HeadlessRenderer&>(dev.GetRenderer());

        // ... exercise real game code: VertexBuffer, SpriteBatch, Effects, RenderTargets ...

        const HeadlessStatistics& stats = renderer.GetStatistics();
        // assert on stats.drawCallCount, stats.clearCount, stats.vertexBuffersCreated, ...

        renderer.AssertNoLeaks();  // throws HeadlessValidationException if anything is still alive
                                  // that this test itself created and should have disposed
        Exit();
    }
};
```

Key APIs on `HeadlessRenderer` (all `CNAEXT`, not part of the XNA surface):

- `GetStatistics()` / `GetLastFrameStatistics()` — cumulative and per-frame draw-call, primitive,
  clear, present, state-change, and resource-creation counters.
- `AliveResources()` / `AssertNoLeaks()` — walks the shared resource registry; `AssertNoLeaks()`
  throws `HeadlessValidationException` listing every still-alive resource, or returns normally if
  none remain. Callable at any point during a test, not just at teardown.
- `TraceLog()` — the structured call log accumulated in `Trace` mode (empty otherwise).
- `SetMode(HeadlessMode)` / `GetMode()` — the runtime strictness dial.

## Known limitations (2026-07-13)

- "Disposed state object" validation (`BlendState`/`DepthStencilState`/etc.) is not implemented —
  `IGraphicsRenderer`'s `ApplyBlendState()`/etc. take raw `int`/`bool`/`float` parameters, not object
  references, so there is no state-object identity left for the renderer to check by the time a call
  reaches it. This is a real, permanent interface constraint, not a temporary gap.
- `SpriteBatch`'s captured `LastBatch()` draw-call data (texture/rects/color/rotation/effects per
  `Draw()` call) is implemented but not yet separately asserted on by any test — only the aggregate
  `drawCallCount` is checked.

Resolved since the first commit: viewport/scissor validation now cross-references the actual
bound-target size (not just non-negative dimensions); creation-site tracking is implemented via an
explicit `PushDebugLabel()`/`PopDebugLabel()` API (see below) rather than left as an unused field;
(2026-07-13) `CNA_HEADLESS_MODE` environment-variable parsing, the 32-bit `IndexBuffer` path,
`GetLastFrameStatistics()`'s per-frame diff math, and `AliveResources()`'s per-type breakdown are
all now verified by a fourth CTest, `Headless_CoverageGaps`; and `AlphaTestEffect`/
`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`Model.Draw()` are individually verified
end-to-end by a fifth CTest, `Headless_Effects` — which also surfaced a real behavioral finding:
`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` unconditionally set `TextureEnabled` in
their `FillGpuDrawParams()` regardless of whether a texture was actually assigned, so they genuinely
throw under `HeadlessValidation` if a game forgets to set one, while `AlphaTestEffect` degrades
gracefully either way (its texture is truly optional). Worth knowing when writing a real game
against these effects, not just a Headless test-coverage note; (2026-07-13) every
`HEADLESS-20`/`22`/`23`/`24` validation rule is now confirmed both ways — throws under
`HeadlessValidation`, does not throw under `HeadlessFast` — in one dedicated sixth CTest,
`Headless_ModeDial`, not just the one representative draw-call-bounds case shown elsewhere; and
(2026-07-13) `HeadlessTrace`'s call log now covers every draw/clear/resource-creation/`SetData`/
`Present`/state-change call site (`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/
`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`/`CreateSpriteBatch`/
`CreateOcclusionQuery` were the last untraced ones), verified by a seventh CTest,
`Headless_TraceDiff`, which also introduces `CompareTraceLogs()`/`FormatTraceLogDiff()` (see
below) and, while closing it, caught and fixed a real bug: `SetViewport` had never actually been
wired into `RecordTrace()` despite an earlier commit message claiming it was.

## Debug labels and trace log export

```cpp
renderer.SetMode(HeadlessMode::Trace);
renderer.PushDebugLabel("Level1/EnemySpawner");
auto vb = std::make_unique<VertexBuffer>(device, decl, count, BufferUsage::None);
renderer.PopDebugLabel();

// ... if `vb` leaks, AssertNoLeaks() reports "Level1/EnemySpawner" alongside it ...

renderer.DumpTraceLog();          // writes FormatTraceLog() to stdout
std::string text = renderer.FormatTraceLog();  // or capture it yourself for a CI artifact
```

`PushDebugLabel()`/`PopDebugLabel()` only affect resources created in `HeadlessTrace` mode — in
`Fast`/`Validation` they're accepted but have no effect, matching `HeadlessTrace`'s own
"diagnostic-only, pay nothing for it elsewhere" design.

## Trace-log diffing

Two `HeadlessTrace`-mode runs of the same (ideally deterministic) game can be compared directly to
catch behavioral drift between commits, independent of any pixel output:

```cpp
// captured from run A (e.g. against `main`) and run B (e.g. against your branch)
const std::vector<HeadlessTraceEntry>& logA = rendererA.TraceLog();
const std::vector<HeadlessTraceEntry>& logB = rendererB.TraceLog();

const HeadlessTraceLogDiff diff = CompareTraceLogs(logA, logB);
if (!diff.identical)
    std::cerr << FormatTraceLogDiff(logA, logB);  // "Trace logs diverge at entry #N: ..."
```

`CompareTraceLogs()` compares entries by `frameIndex`/`method`/`argsSummary` (not `callIndex`,
which is just a position counter). `FormatTraceLogDiff()` renders either a one-line "identical"
summary or the first diverging entry from each log side-by-side, plus an entry-count note if the
logs are different lengths.

See `plans/plan_headless.md` for the full task-by-task status and design rationale.

## Real-world validation

Beyond this project's own synthetic tests, `../mobile-eggbert` (`WindowsPhoneSpeedyBlupi`, a real,
complete third-party game — the same one used to validate the WebGPU renderer) builds clean against
`-DCNA_GRAPHICS_RENDERER=HEADLESS` and runs 20+ seconds with zero crashes or exceptions, with
`DISPLAY`/`WAYLAND_DISPLAY` both explicitly unset — confirming the renderer holds up against real
game code exercising asset loading, `SpriteBatch`, audio, and input polling, not just a purpose-
built smoke test.
