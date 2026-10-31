# Headless graphics renderer

## Status

The Headless renderer is a **CI/testing-only graphics renderer**, verified 2026-10-31. Select it
with:

```bash
cmake -S . -B cmake-build-headless -G Ninja \
  -DCNA_PLATFORM=HEADLESS \
  -DCNA_AUDIO_PLATFORM=NULL \
  -DCNA_GRAPHICS_RENDERER=HEADLESS \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-headless --parallel 8
ctest --test-dir cmake-build-headless -L Headless --output-on-failure -j8
```

No native graphics dependency or display server is needed. With the Headless platform and Null
audio selections above, the tests create no window and initialize no video subsystem. The normal
repository prerequisites still apply, including initialized third-party sources and the sibling
`../sharp-runtime` checkout.

## What this renderer is for (and isn't)

CNA's GPU renderers (`OPENGLES3`/`SDL_GPU`/`WEBGPU` and the rest) need a real window and a real
GPU context to run at all. That makes them unsuitable for fast, CI-friendly testing of game
*logic* — a CI container with no display server can't run them, and even where a virtual display
(Xvfb) is available, spinning one up per test run is slow compared to what a logic test actually
needs to prove.

The Headless renderer covers the supported `IGraphicsRenderer` surface without touching a GPU or
a window at all: `SDL_InitSubSystem(SDL_INIT_VIDEO)` is never called, no `SDL_Window` is ever
created, and supported draw/resource calls perform argument validation, resource-lifecycle
tracking, counters or trace bookkeeping instead of talking to a GPU. Unsupported capabilities are
reported and rejected explicitly; they are not represented by fabricated resources or pixels.

**What it proves:** "this code path ran, with these arguments, this many times, and nothing leaked
or misused an API." **What it does *not* prove:** anything about pixel correctness. If you need to
know whether something actually *looks* right, use one of the real GPU renderers' pixel-asserted
tests (see `docs/graphics-renderer-feature-matrix.md`) — Headless renders nothing, by design.

**The one rule it lives by:** `Validation` and `Trace` never silently accept something a real
renderer would reject, because their value is being a trustworthy stand-in. `Fast` deliberately
skips those checks while retaining structurally safe bookkeeping. Where a real renderer's answer to
some misuse is ambiguous or undocumented, settle that answer first rather than guessing one here,
and keep Headless-only needs out of the shared `IGraphicsRenderer`/`GpuDrawParams` interface unless
a genuine common need justifies them.

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
  clear, present, state-change, and resource-creation counters. Instanced work uses logical-work
  semantics: `P` primitives across `N` instances adds `N` draw calls and `P * N` primitives.
- `AliveResources()` / `AssertNoLeaks()` — walks the shared resource registry; `AssertNoLeaks()`
  throws `HeadlessValidationException` listing every still-alive resource, or returns normally if
  none remain. Callable at any point during a test, not just at teardown.
- `TraceLog()` — the structured call log accumulated in `Trace` mode (empty otherwise).
- `SetMode(HeadlessMode)` / `GetMode()` — the runtime strictness dial.

## Known limitations (2026-10-31)

- "Disposed state object" validation (`BlendState`/`DepthStencilState`/etc.) is not implemented —
  `IGraphicsRenderer`'s `ApplyBlendState()`/etc. take raw `int`/`bool`/`float` parameters, not object
  references, so there is no state-object identity left for the renderer to check by the time a call
  reaches it. This is a real, permanent interface constraint, not a temporary gap (HEADLESS-24).
- Nor is there a "disposed resource" guard on the renderer's own resource objects (HEADLESS-19):
  each is owned by exactly one `std::unique_ptr` in its XNA wrapper, so a disposed one is destroyed,
  never left callable. What exists instead is registration on construction and removal on
  destruction, which is what leak detection reads.
- Leak detection is the explicit `AssertNoLeaks()`, not a check in the renderer's destructor
  (HEADLESS-34/35): at an ordinary `Game` shutdown a game's `SpriteBatch` and textures are often
  still alive, destroyed in no fixed order relative to the renderer, so an automatic check would
  report false leaks. Call it where a test wants the answer, mid-run included, and compare against a
  baseline when long-lived resources are legitimately alive.
- Plain `TextureCube` has real per-face/per-mip CPU storage and exact format-native round trips for
  every format Headless advertises. `Texture3D` remains unsupported: the device reports no
  `GraphicsCapability::Texture3D`, and public construction rejects instead of accepting and
  discarding volume data.
- Render targets track binding, dimensions and lifecycle, but Headless never rasterizes their
  colour content. Render-target and backbuffer readback therefore reject instead of returning
  invented black pixels.
- Trace covers supported state-mutating, resource, upload, clear, presentation and draw calls,
  including target changes and instancing. Pure queries do not add trace entries.

## Verified coverage

The module-owned tests plus the shared renderer-contract/conformance registrations are listed by
`ctest --test-dir cmake-build-headless -N -L Headless`; do not rely on an old fixed test count.
Focused regressions include:

- cube target dimensions, 2D-to-cube transitions and return to the backbuffer;
- cumulative/per-frame instanced statistics and a distinct structured instanced trace entry;
- target, virtual-resolution, presentation, blend-factor and reference-stencil trace entries;
- format-aware Texture2D stride, mip-index/dimension/null validation and retained mip bytes;
- exact TextureCube face/format round trips and explicit Texture3D refusal;
- every `LastBatch()` field, raw invalid `PrimitiveType` values and foreign-renderer resources;
- debug labels, leak reports, trace formatting/diffing, stock effects and the runtime mode dial.

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

The task record (`HEADLESS-1`..`HEADLESS-64`, 2026-07-13) is in Git history; comments cite its rows
as "HEADLESS-nn".

## Real-world validation

Beyond this project's own synthetic tests, `../mobile-eggbert` (`WindowsPhoneSpeedyBlupi`, a real,
complete third-party game — the same one used to validate the WebGPU renderer) builds clean against
`-DCNA_GRAPHICS_RENDERER=HEADLESS` and runs 20+ seconds with zero crashes or exceptions, with
`DISPLAY`/`WAYLAND_DISPLAY` both explicitly unset — confirming the renderer holds up against real
game code exercising asset loading, `SpriteBatch`, audio, and input polling, not just a purpose-
built smoke test.
