# Headless graphics backend

## Status

The Headless backend is a **CI/testing-only graphics backend**, verified 2026-07-13. Select it
with:

```bash
cmake -S . -B cmake-build-headless \
  -DCNA_GRAPHICS_BACKEND=HEADLESS \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-headless -j
```

No extra dependencies are needed — the Headless backend never links against SDL's graphics
functions, OpenGL, Vulkan, or any GPU library. It only needs the same SDL3/SDL3_image/SDL3_mixer
and `../sharp-runtime` checkout every other backend already requires.

## What this backend is for (and isn't)

Every other CNA backend (`SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`) needs a real window and
a real GPU context to run at all. That makes them unsuitable for fast, CI-friendly testing of game
*logic* — a CI container with no display server can't run them, and even where a virtual display
(Xvfb) is available, spinning one up per test run is slow compared to what a logic test actually
needs to prove.

The Headless backend implements CNA's entire `IGraphicsBackend` interface without touching a GPU or
a window at all: `SDL_InitSubSystem(SDL_INIT_VIDEO)` is never called, no `SDL_Window` is ever
created, and every draw/resource call does real bookkeeping (argument validation, resource
lifecycle tracking, draw-call/state-change counters) instead of talking to a GPU.

**What it proves:** "this code path ran, with these arguments, this many times, and nothing leaked
or misused an API." **What it does *not* prove:** anything about pixel correctness. If you need to
know whether something actually *looks* right, use one of the real GPU backends' pixel-asserted
tests (see `docs/graphics-backend-feature-matrix.md`) — Headless renders nothing, by design.

## Runtime modes

Unlike the other backends, Headless has a single build with a **runtime** strictness dial, since
building three separate binaries for a verbosity/strictness setting would triple build time for no
benefit. Select it via the `CNA_HEADLESS_MODE` environment variable, or override it programmatically
before `Game::Run()`:

| Mode | Behaviour |
|---|---|
| `Fast` | Minimum bookkeeping (draw-call/resource counters only), skips all argument/bounds validation. For test runs that just need the game loop to execute quickly. |
| `Validation` (default) | Full argument validation on top of `Fast`'s counters — throws `HeadlessValidationException` for the same kind of misuse a real backend is expected to reject (out-of-range draw calls, oversized `SetData()` calls, `TextureEnabled=true` with no texture bound, etc). |
| `Trace` | Everything `Validation` does, plus a structured call log (method name, argument summary, frame index) accumulated in memory for later inspection. |

```bash
CNA_HEADLESS_MODE=Fast ./my_test
```

```cpp
auto& backend = static_cast<CNA::Internal::Backends::Headless::HeadlessGraphicsBackend&>(
    graphicsDevice.GetBackend());
backend.SetMode(CNA::Internal::Backends::Headless::HeadlessMode::Fast);
```

## Writing a headless test

A headless test is a normal `Game` subclass, run exactly like any other CNA example — the only
difference is the backend selected at CMake configure time. See
`examples/headless_smoke_test.cpp` and `examples/headless_resource_backends_test.cpp` for full
working examples. The pattern:

```cpp
class MyHeadlessTest : public Game
{
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<HeadlessGraphicsBackend&>(dev.GetBackend());

        // ... exercise real game code: VertexBuffer, SpriteBatch, Effects, RenderTargets ...

        const HeadlessStatistics& stats = backend.GetStatistics();
        // assert on stats.drawCallCount, stats.clearCount, stats.vertexBuffersCreated, ...

        backend.AssertNoLeaks();  // throws HeadlessValidationException if anything is still alive
                                  // that this test itself created and should have disposed
        Exit();
    }
};
```

Key APIs on `HeadlessGraphicsBackend` (all `NOXNA`, not part of the XNA surface):

- `GetStatistics()` / `GetLastFrameStatistics()` — cumulative and per-frame draw-call, primitive,
  clear, present, state-change, and resource-creation counters.
- `AliveResources()` / `AssertNoLeaks()` — walks the shared resource registry; `AssertNoLeaks()`
  throws `HeadlessValidationException` listing every still-alive resource, or returns normally if
  none remain. Callable at any point during a test, not just at teardown.
- `TraceLog()` — the structured call log accumulated in `Trace` mode (empty otherwise).
- `SetMode(HeadlessMode)` / `GetMode()` — the runtime strictness dial.

## Known limitations (2026-07-13)

- `HeadlessTrace` mode's call log now covers draws, clears, resource creation, `SetData`,
  `Present`, and the four `Apply*State` methods plus `SetScissorRect`/`SetViewport`, but still not
  literally every `IGraphicsBackend` method (`ClearDepth`/`ClearStencil`/`SetDepthTestEnabled`/etc.
  and the `Create*` factories don't log) — the highest-value call sites for diagnosing a failing
  test are covered, not literally everything.
- "Disposed state object" validation (`BlendState`/`DepthStencilState`/etc.) is not implemented —
  `IGraphicsBackend`'s `ApplyBlendState()`/etc. take raw `int`/`bool`/`float` parameters, not object
  references, so there is no state-object identity left for the backend to check by the time a call
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
against these effects, not just a Headless test-coverage note; and (2026-07-13) every
`HEADLESS-20`/`22`/`23`/`24` validation rule is now confirmed both ways — throws under
`HeadlessValidation`, does not throw under `HeadlessFast` — in one dedicated sixth CTest,
`Headless_ModeDial`, not just the one representative draw-call-bounds case shown elsewhere.

## Debug labels and trace log export

```cpp
backend.SetMode(HeadlessMode::Trace);
backend.PushDebugLabel("Level1/EnemySpawner");
auto vb = std::make_unique<VertexBuffer>(device, decl, count, BufferUsage::None);
backend.PopDebugLabel();

// ... if `vb` leaks, AssertNoLeaks() reports "Level1/EnemySpawner" alongside it ...

backend.DumpTraceLog();          // writes FormatTraceLog() to stdout
std::string text = backend.FormatTraceLog();  // or capture it yourself for a CI artifact
```

`PushDebugLabel()`/`PopDebugLabel()` only affect resources created in `HeadlessTrace` mode — in
`Fast`/`Validation` they're accepted but have no effect, matching `HeadlessTrace`'s own
"diagnostic-only, pay nothing for it elsewhere" design.

See `plan_headless.md` for the full task-by-task status and design rationale.

## Real-world validation

Beyond this project's own synthetic tests, `../mobile-eggbert` (`WindowsPhoneSpeedyBlupi`, a real,
complete third-party game — the same one used to validate the WebGPU backend) builds clean against
`-DCNA_GRAPHICS_BACKEND=HEADLESS` and runs 20+ seconds with zero crashes or exceptions, with
`DISPLAY`/`WAYLAND_DISPLAY` both explicitly unset — confirming the backend holds up against real
game code exercising asset loading, `SpriteBatch`, audio, and input polling, not just a purpose-
built smoke test.
