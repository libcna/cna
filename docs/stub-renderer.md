# Stub (no-op) graphics renderer

## Status

The Stub renderer is a **deliberately minimal no-op graphics renderer**, verified 2026-07-19:
`Stub_Smoke` CTest passes 7/7 checks, and the full `CnaTests` corpus passes 5413/5423 (4
hardware-sensor skips; 6 known failures — 5 inherent to `Texture3D`/`TextureCube`/custom-`Effect`
needing a real renderer this one deliberately doesn't provide, 1 a pre-existing `EASYGL`-only test
unrelated to this renderer; see `plans/plan_stub.md` for the full breakdown). Select it with:

```bash
cmake -S . -B cmake-build-stub \
  -DCNA_GRAPHICS_RENDERER=STUB \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-stub -j
```

No extra dependencies are needed — like `HEADLESS`/`SOFTWARE`, this renderer never touches SDL's
video subsystem, OpenGL, Vulkan, or any GPU library. It only needs the same SDL3/SDL3_image/
SDL3_mixer and `../sharp-runtime` checkout every other renderer already requires.

## What this renderer is for (and isn't)

Two renderers already exist that avoid the GPU entirely: `HEADLESS` (real argument validation, a
structured trace log, resource-lifecycle/leak tracking, and draw-call/state-change counters across
three runtime strictness modes) and `SOFTWARE` (a genuine CPU rasterizer that produces real,
correct pixels). Both are substantial in their own right.

Stub is neither. It is the smallest possible complete `IGraphicsRenderer` implementation: every
method either does nothing or returns a fixed/trivial value, with **no bookkeeping of any kind** —
no counters, no validation, no trace log, no resource registry. It exists for:

- **A minimal reference implementation.** Reading `StubRenderer.hpp`/`.cpp` end-to-end shows
  exactly which `IGraphicsRenderer` members must be implemented (pure virtual) vs. already have a
  usable default, without wading through Headless's validation modes or Software's rasterizer math.
- **The fastest "does the game loop even run" smoke check** — nothing to maintain, nothing to get
  subtly wrong.
- **A genuinely dependency-free placeholder renderer** for build configurations that need *some*
  renderer to link without pulling in SDL video, a GPU library, or Headless/Software's own overhead.

If you need call counters, resource-leak detection, or argument validation while still avoiding a
GPU, that's `HEADLESS` (specifically `HeadlessMode::Fast` for "accepts everything, minimal
bookkeeping"), not this renderer — see `docs/headless-renderer.md`.

## Naming

Named `Stub`, not `Null` — `NULL` is a `<cstddef>`/`<cstdlib>` macro (`0`/`nullptr` depending on
context), so a bare `CNA_GRAPHICS_RENDERER=NULL`/`NullGraphicsRenderer` name risks silent macro
substitution (e.g. in a `#if CNA_GRAPHICS_RENDERER == NULL` comparison) on top of just reading
oddly. See `plans/plan_stub.md`'s own "Naming" section for the full rationale.

## Writing a Stub test

Like `HEADLESS`/`SOFTWARE`, a Stub test is a normal `Game` subclass — the only difference is the
renderer selected at CMake configure time. See `modules/renderers/stub/examples/stub_smoke_test.cpp` for a full working
example (registered as the `Stub_Smoke` CTest). The pattern:

```cpp
class MyStubTest : public Game
{
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black);

        // ... build a VertexBuffer, apply a BasicEffect, draw -- all accepted, none of it
        // actually renders anything, and none of it throws.
        BasicEffect fx(dev);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);

        Exit();
    }
};
```

## Known limitations

- **5 `CnaTests` failures are the direct consequence of the `Texture3D`/`TextureCube`/custom-
  `Effect` gap below**: `Texture3DTextureCubeContentTypeReaderTest.TextureCubeReaderLoads...`/
  `...Texture3DReaderParsesHandConstructedBytes...`, `CnjEffectTest.LoadsRealCnjFixture`,
  `CnjStockEffectTest.CustomGlslEffectStillWorks`, `CnjTexture3DTest.LoadsRealCnjFixture` — all
  expect a real GPU-backed `Texture3D`/`TextureCube`/custom `Effect`, which this renderer does not
  provide (same accepted-gap shape as `SDL_Renderer`'s own Task 725, see
  `docs/graphics-renderer-feature-matrix.md`).
- **Renders nothing.** `Clear()`/`Present()`/every `Draw*` call are no-ops; there is no framebuffer
  of any kind, CPU or GPU. This is the entire point, not a gap to close later.
- **`ReadBackbuffer()` throws** (the shared `IGraphicsRenderer` default) — there is no real pixel
  data to honestly return.
- **`CreateEffectRenderer()`/`CreateOcclusionQuery()`/`CreateTexture3D()`/`CreateTextureCube()`/
  `CreateRenderTarget2D()`/`CreateRenderTargetCube()` all return `nullptr`** (the shared
  `IGraphicsRenderer` defaults) — a custom `ShaderEffect`, an occlusion query, or a 3D/cube texture
  or render target simply isn't available under this renderer.
- **No counters, no validation, no leak detection.** If you need any of that while still avoiding a
  GPU, use `HEADLESS` instead (see above).
- **Not a column in `docs/graphics-renderer-feature-matrix.md`** — with zero real rendering, none of
  that matrix's pixel/behavior rows are meaningful for this renderer, the same reasoning that
  already excludes `HEADLESS`.

See `plans/plan_stub.md` for the full design rationale.
