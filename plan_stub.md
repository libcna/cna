# Stub (no-op) Graphics Backend — Implementation Plan

> **Status: Phase ST1 complete, verified 2026-07-19.** `CNA_GRAPHICS_BACKEND=STUB` configures,
> builds, and `CnaTests` (the pre-existing GTest corpus) links and runs against it. This backend
> does the absolute minimum to satisfy `IGraphicsBackend`: every method either does nothing or
> returns a trivial value. No SDL window, no SDL video subsystem, no GPU library of any kind is
> touched (confirmed: `SDL_WasInit(SDL_INIT_VIDEO) == 0` throughout `Stub_Smoke`).
> `Stub_Smoke` CTest: 7/7 checks pass. Full `CnaTests` run (from repo root, `DISPLAY`/
> `WAYLAND_DISPLAY` unset, excluding the pre-existing-and-unrelated `MediaLibraryTestFixture` suite
> — see below): **5413/5423 pass, 4 skipped (hardware-sensor, every backend), 6 known failures**:
> 5 are the direct, expected consequence of `CreateTexture3D`/`CreateTextureCube`/
> `CreateEffectBackend` keeping the shared `IGraphicsBackend` nullptr defaults (same accepted-gap
> shape as `SDL_Renderer`'s own Task 725), and 1 (`GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame`)
> is a pre-existing test hardcoded to assume `EASYGL` specifically, unrelated to this backend (would
> already fail identically under `HEADLESS`/`VULKAN`/`BGFX`/etc.).
>
> **Separately found, NOT fixed, NOT caused by this backend**: `MediaLibraryTestFixture`'s
> `ObjectGraphIsInternallyConsistent` and `VacationAlbumHasOnePictureAndFamilyHasOne` segfault in
> this sandbox — `MediaLibrary`'s "real local device" media source finds 0 songs/albums/genres/
> pictures here (no real media library present in this container) and
> `getRootPictureAlbumProperty()` returns null, which a couple of the Media test suite's own
> integration tests then dereference unconditionally. Confirmed via source inspection to have zero
> dependency on `IGraphicsBackend`/`GraphicsDevice`/any `CNA_BACKEND_*` macro — a pre-existing
> Media-subsystem/environment gap that would crash identically under every graphics backend, flagged
> here for whoever owns Media next, out of this task's own scope.

---

## Why this backend, in the owner's own words

> "přidat nový backend STUB, ktery nebude nic delat neco jako null backend, akorat se to nebude
> jmenovat null backend jelikoz NULL, muze mit konflikt v C++"
> (add a new backend called STUB that does nothing — like a "null" backend, except it isn't called
> that, since `NULL` can conflict in C++.)

Two backends already exist that avoid touching the GPU (`HEADLESS`, `SOFTWARE`), but both carry
real weight of their own: `HEADLESS` has three runtime strictness modes, a structured trace log, a
resource registry with leak detection, and per-call statistics counters (~1,500 combined
lines); `SOFTWARE` is a genuine CPU rasterizer (~1,700 lines). Neither is "does nothing" — both are
deliberately substantial. `STUB` fills the gap those two leave open: the smallest possible backend
that still satisfies `IGraphicsBackend` completely, with no bookkeeping, no validation, and no
counters at all. Its value is:

- **A minimal reference implementation.** Anyone adding a tenth graphics backend can read
  `StubGraphicsBackend.hpp`/`.cpp` end-to-end in a few minutes to see exactly which
  `IGraphicsBackend` members are pure-virtual (must be implemented) vs. already defaulted, without
  wading through Headless's validation modes or Software's rasterizer math.
- **The fastest possible "does the game loop even run" smoke check** — no counters to maintain, no
  validation rules to enforce, nothing to get subtly wrong.
- **A genuinely dependency-free placeholder** for build configurations that need *some* backend to
  link (e.g. compiling the non-Graphics parts of CNA, or a CI job that only cares whether the code
  compiles) without pulling in SDL video, a GPU library, or Headless/Software's own bookkeeping.

## Naming (avoiding `NULL`)

`NULL` is a `<cstddef>`/`<cstdlib>` macro (`0`/`nullptr`); using it as a bare backend name would
raise both the practical bug (`#if CNA_GRAPHICS_BACKEND == NULL` gets silently macro-expanded to
`0`) and a readability problem the moment `CNA_BACKEND_NULL`/`NullGraphicsBackend` compile fine but
read strangely next to real code. `Stub` (`StubGraphicsBackend`, `CNA_GRAPHICS_BACKEND=STUB`) was
chosen instead: unambiguous, no collision with any C++ keyword/macro/standard-library name, and
already the exact word the project owner used when requesting this backend. (`stub.md`, the
owner's own brainstorming notes, separately suggested `NoOp` as a common alternative convention —
recorded here for context, not used, since the owner had already settled on `Stub` by name.)

---

## Design decisions

1. **No SDL, no window, no GPU context — ever.** Mirrors `HEADLESS`/`SOFTWARE`'s own precedent
   (`GraphicsDevice::createOrAttachWindow()`'s `#if defined(CNA_BACKEND_HEADLESS) ||
   defined(CNA_BACKEND_SOFTWARE)` guard) by adding `CNA_BACKEND_STUB` to that same guard — `window_`
   stays `nullptr`, `GetWindowInternal()`/`GetRendererInternal()` both return `nullptr`.
2. **No bookkeeping of any kind.** Unlike `HEADLESS`, this backend keeps no draw-call/resource
   counters, no resource registry, no leak detection, no runtime mode dial, no trace log. Every
   method is either an empty body or returns a fixed/trivial value. If a future need arises for
   "does nothing, but also counts calls," that belongs in `HEADLESS::HeadlessMode::Fast`, which
   already exists for exactly that purpose — not as scope creep here.
3. **Concrete resource handles are the simplest possible structs.** `StubVertexBufferBackend`/
   `StubIndexBufferBackend` store only the count they were given (so `GetVertexCount()`/
   `GetIndexCount()` are honest); `StubTextureBackend` stores only width/height. No pixel data, no
   backing store, no CPU shadow copy — `SetData`/`GetData` calls are accepted and ignored.
4. **`GetViewportSize()` returns the requested virtual resolution (or a fixed 1024×768 default)**,
   matching `HEADLESS`'s own precedent, rather than always `0,0` — several call sites divide by the
   viewport size, and a real backend would never report a zero-sized viewport either.
5. **`ReadBackbuffer()` keeps the shared `IGraphicsBackend` default (throws).** This backend never
   renders a pixel, so there is nothing honest to return; throwing matches `HEADLESS`'s own
   behavior for anything it can't answer for real rather than fabricating a plausible-looking value.
6. **No CMake library work needed beyond the standard per-backend wiring.** Like `HEADLESS`/
   `SOFTWARE`, `cmake/BackendLibraries.cmake` needs no dedicated `elseif` branch at all — the
   backend target already gets `cna_backend_graphics_common`/`SHARP_RUNTIME` from the shared
   default, and links no additional library (not even `SDL3::SDL3`).

---

## Tasks

| ID | Task | Status |
|----|------|--------|
| STUB-1 | Write this design/plan doc | ✅ |
| STUB-2 | Implement `StubGraphicsBackend` (`.hpp`/`.cpp`) under `include/CNA/Internal/Backends/Stub/` and `src/CNA/Internal/Backends/Stub/` | ✅ |
| STUB-3 | Wire `STUB` into `cmake/BackendSelection.cmake` (option, `CNA_GRAPHICS_BACKEND` STRINGS list, `elseif` branch); add `CNA_BACKEND_STUB` to `GraphicsDevice.cpp`'s window-skip guard | ✅ |
| STUB-4 | Add `examples/stub_smoke_test.cpp` + `cmake/Tests/StubTests.cmake`, registered from root `CMakeLists.txt` | ✅ |
| STUB-5 | Add `docs/stub-backend.md`; add a short Stub paragraph to `docs/graphics-backend-feature-matrix.md`; add `STUB` to `README.md`'s backend list | ✅ |
| STUB-6 | Configure+build with `-DCNA_GRAPHICS_BACKEND=STUB -DCNA_BUILD_TESTS=ON`, run `Stub_Smoke`, spot-check `CnaTests` | ✅ |

## Known limitations

- **Renders nothing.** `Clear()`/`Present()`/every `Draw*` call are no-ops; there is no framebuffer
  of any kind, CPU or GPU. This is the entire point (see "Why this backend" above) — it is not a
  gap to close later.
- **`CreateEffectBackend()`/`CreateOcclusionQuery()`/`CreateTexture3D()`/`CreateTextureCube()`/
  `CreateRenderTarget2D()`/`CreateRenderTargetCube()` all keep the shared `IGraphicsBackend`
  defaults (return `nullptr`)** — a custom `ShaderEffect`, an occlusion query, or a 3D/cube texture
  or render target simply isn't available under this backend, the same shape as `HEADLESS`'s own
  accepted gaps for the features it hasn't implemented.
- **Not a column in `docs/graphics-backend-feature-matrix.md`** for the same reason `HEADLESS` is
  excluded: with zero real rendering, none of that matrix's pixel/behavior rows are meaningful.
