# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering
to one of four backends: SDL\_Renderer, EasyGL (OpenGL ES 3.2), Vulkan, or Bgfx.

**Current phase**: Phase 28 — PresentationParameters and GraphicsDeviceManager
conformance (Tasks 221–230). Tasks 221–225 complete.

**Key architectural decisions**:
- Backend selected at **compile time** via `CNA_GRAPHICS_BACKEND` CMake option.
- `IGraphicsBackend` is the sole contract between the XNA API layer and any backend.
- `Color` inherits `IPackedVectorT<UInt32>` (virtual base) — vtable pointer precedes
  the packed pixel; never cast `Color*` to `uint8_t*` for GL pixel I/O.
- SharpRuntime (`/rv/data/development/github.com/openeggbert/sharp-runtime`) provides
  .NET primitive type aliases and `System.*` stubs.
- FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src` is the authoritative
  XNA 4.0 API reference; all logic is ported line-by-line from there.

---

## 2. Current status

### EasyGL backend (`cmake-build-debug`) — primary backend
- **Builds**: clean.
- **Tests**: 53/54 EasyGL integration tests pass; 1694/1696 total tests pass.
- Pre-existing failures (not caused by recent work):
  - `EasyGL_MRT_TwoAttachments` (test 1655) — MRT framebuffer attachment bug.
  - `easy-gl-resource-smoke-tests` (test 1694) — upstream easygl assertion failure
    in `test_texture_upload_sets_unpack_alignment_wrap_and_unit0_binding`.
- Recently confirmed working (Tasks 215–225):
  - GPU handle released on `Dispose()` (not on C++ destructor)
  - Move semantics for `VertexBuffer` and `IndexBuffer` (no double-free)
  - `ResourceCreated`/`ResourceDestroyed` events fired from `GraphicsResource`
  - `GraphicsDevice` tracks resources and disposes them before backend teardown
  - Resource leak-check: 80 resources created/disposed with zero handle leaks
  - `PresentationParameters` fully conforms to FNA (all 10 fields, correct 800×480 defaults)
  - `PresentationInterval` → VSync mapping implemented across all four backends
  - `Texture`/`Texture2D`/`RenderTarget2D` C++ name-hiding bug fixed (`using` declarations)
  - `IsFullScreen` stored correctly in device PP regardless of backend fullscreen capability
  - `GraphicsDeviceManager` registers as `IGraphicsDeviceManager` + `IGraphicsDeviceService`; `Game::DoInitialize()` calls `CreateDevice()` automatically

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean.
- **Tests**: 9/9 Vulkan integration tests pass.

### Bgfx backend (`cmake-build-bgfx`)
- **Builds**: clean.
- Smoke tests pass; pixel readback and 3D state incomplete.

### What does not work yet
- **Framework.Net** — 0 % (NetworkSession, PacketReader/Writer entirely absent).
- **Content pipeline (.xnb)** — 0 % (ContentManager uses custom JSON/PNG/OGG only).
- **GamerServices** — ~5 % (stubs only).
- **sRGB SurfaceFormats** — silently map to linear GL/Vulkan internal formats.
- **Bgfx pixel readback** — `SetDepthTestEnabled` / `SetBlendEnabled` still throw.

---

## 3. Recent changes

| Task / Commit | What changed |
|---|---|
| Task 225 | GDM service registration: `registerServices()` now calls `game_->getServicesProperty().AddService<IGraphicsDeviceManager>(this)` and `AddService<IGraphicsDeviceService>(this)`; duplicate-registration guard throws `invalid_argument`; `unregisterServices()` removes both entries (null-guard for GDM without a Game); `Game::DoInitialize()` now finds GDM and calls `CreateDevice()` automatically — matching FNA behavior. Also fixed `StorageDeviceNotConnectedException`+`StorageDevice.cpp` to use `std::exception_ptr` following sharp-runtime API change |
| Task 224 | `IsFullScreen` field consistency: `SDL_SetWindowFullscreen` failure changed from throw to `SDL_ClearError()` soft-skip; `easygl_fullscreen_field_test.cpp` verifies field round-trip through GDM setter + `ApplyChanges()` + `ToggleFullScreen()`; 7/7 PASS |
| Task 223 | `PresentationInterval` → VSync mapping: `swapInterval` added to `GraphicsBackendCreateArgs`; `IGraphicsBackend::SetSwapInterval` virtual method; EasyGL calls `SDL_GL_SetSwapInterval`; SDL_Renderer calls `SDL_SetRenderVSync`; Vulkan picks present mode at swapchain creation; Bgfx uses `resetFlags_` + `bgfx::reset`; runtime change via `GraphicsDevice::SetPresentationParameters`; 10/10 smoke-test PASS |
| Task 221+222 | `PresentationParameters` audit: fixed default dimensions bug (1024×768→800×480); fixed C++ name-hiding bug; added tests; 26/26 unit tests pass |
| Task 220 | `docs/graphics-resource-lifetime.md` created |

**Files modified (Tasks 224–225):**
- `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`
- `include/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp`
- `src/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.cpp`
- `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`
- `examples/easygl_fullscreen_field_test.cpp` (new, Task 224)

---

## 4. Current blocker / main problem

No hard blocker. Two pre-existing test failures exist but are not caused by recent work:

1. **`EasyGL_MRT_TwoAttachments`** (test 1655): Multiple Render Target test fails.
   - Suspected: FBO attachment count or draw buffer setup in EasyGL backend.
   - Affected: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`, `SetRenderTargets`.
   - Not investigated yet.

2. **`easy-gl-resource-smoke-tests`** (test 1694): Upstream easygl library assertion.
   - Assertion: `g_state.last_active_texture == GL_TEXTURE0` in `SmokeResourceTests.cpp:336`.
   - Affected: `vendor/easy-gl` — not in CNA code directly.
   - Not investigated yet.

**Note**: The `easygl_present_interval_test.cpp` still has a manual `gdm_->ApplyChanges()`
call in its `Initialize()` override (now a harmless no-op since `CreateDevice()` is called
first by `DoInitialize()`). It can be cleaned up but is not a blocker.

---

## 5. Known bugs and limitations

| Status | Item |
|---|---|
| **confirmed pre-existing** | `EasyGL_MRT_TwoAttachments` fails — MRT FBO setup bug in EasyGL backend |
| **confirmed pre-existing** | `easy-gl-resource-smoke-tests` fails — upstream easygl `GL_TEXTURE0` assertion |
| **missing NOXNA tags** | 7 non-XNA members on `GraphicsDevice` are missing `NOXNA` (documented in `docs/graphicsdevice-fna-audit.md`) |
| **missing XNA methods** | `Present(Rectangle?,Rectangle?,IntPtr)`, `Clear(ClearOptions,Vector4,float,int)`, `GetRenderTargetsNoAllocEXT` (Task 201 audit) |
| **missing callback** | `GraphicsDeviceResetting()` callback on `GraphicsResource` not yet called (Gap 2 from Task 211 audit) |
| **known limit** | EasyGL `FillMode::WireFrame` — no `glPolygonMode` on GLES3 |
| **known limit** | Bgfx `SetDepthTestEnabled` / `SetBlendEnabled` / `SetDepthWriteEnabled` still throw |
| **known limit** | Bgfx `GetBackBufferData` not integration-tested |
| **known limit** | Vulkan `SetSwapInterval` does not recreate swapchain at runtime (applied only at creation) |
| **known limit** | SDL_Renderer `PresentInterval::Two` maps to VSync=1 (SDL3 has no swap interval 2) |
| **incomplete** | sRGB SurfaceFormats silently map to linear GL/Vulkan internal formats |
| **0 %** | Framework.Net (NetworkSession, PacketReader/Writer) |
| **0 %** | XNA binary `.xnb` content pipeline |
| **~5 %** | GamerServices (Guide.Show no-op only) |

---

## 6. Architecture notes

### Module map

```
Microsoft::Xna::Framework::*            ← XNA public API (include/ + src/)
  └─ GraphicsDevice                     ← delegates to IGraphicsBackend*
       └─ IGraphicsBackend              ← include/CNA/Internal/Backends/Common/
            ├─ EasyGLGraphicsBackend    ← src/CNA/Internal/Backends/EasyGL/
            ├─ VulkanGraphicsBackend    ← src/CNA/Internal/Backends/Vulkan/
            ├─ BgfxGraphicsBackend      ← src/CNA/Internal/Backends/Bgfx/
            └─ SDLGraphicsBackend       ← src/CNA/Internal/Backends/SDL/

SharpRuntime    ← /rv/data/development/github.com/openeggbert/sharp-runtime
metagl          ← raw GL function loader + typed enum wrappers
easygl          ← GL resource wrappers (Device, Texture, Framebuffer, Sampler, …)
```

### GraphicsResource lifetime rules

- `backend_` is a `unique_ptr` held by the derived class (VB, IB, Texture2D, etc.).
- GPU handle is freed when `Dispose()` is called (via `backend_.reset()` in `Dispose(bool)` override), not when the C++ object is destroyed.
- `Dispose(bool)` override chain: derived class resets backend → calls base `Dispose(bool)`.
- `GraphicsDevice::resources_` tracks all live resources; disposal uses copy-and-clear pattern to avoid re-entrancy.
- See `docs/graphics-resource-lifetime.md` for the full specification.

### PresentationParameters / GDM wiring

- `GDM(Game*)` ctor: registers as `IGraphicsDeviceManager` + `IGraphicsDeviceService`, calls `ApplyChanges()` with default prefs.
- `Game::DoInitialize()` → `Services_.GetService<IGraphicsDeviceManager>()` → `CreateDevice()` — this applies any prefs set between GDM ctor and `Run()`.
- `GraphicsDevice::SetPresentationParameters(pp)` stores the PP AND calls `backend_->SetSwapInterval(toSwapInterval(...))`.
- `PresentInterval::Default/One → swapInterval=1`, `Two → 2`, `Immediate → 0`.
- `IsFullScreen` is stored in the PP before `SDL_SetWindowFullscreen` is called. Backend failure to switch fullscreen is non-fatal (SDL_ClearError); the stored value is always correct.
- `unregisterServices()` is called from `Dispose(bool)` — null-guards `game_` pointer.

### RenderTarget lifecycle (EasyGL)

`SetRenderTarget(rt)` → backend binds the RT's FBO; sets `currentRtHeight_` to RT height.
`SetRenderTarget(nullptr)` → backend binds FBO 0 (backbuffer); `currentRtHeight_` = 0.
`GetBackBufferData` reads from whatever FBO is currently bound.

### Critical invariants

- **`Color` has a vtable pointer** — use `uint8_t[]` + `Color(r,g,b,a)` for pixel I/O.
- **Vulkan build: `-j1`** — race condition in SPIR-V header generation.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.
- **FNA is authoritative** — do not deviate from FNA logic without a `//` comment.
- **`Texture`/`Texture2D`/`RenderTarget2D`** must each have `using BaseClass::Dispose;` to prevent C++ name-hiding.
- **sharp-runtime inner-exception ctors use `std::exception_ptr`** — not `const std::exception&`.

---

## 7. Useful commands

```bash
# EasyGL — configure + build + all integration tests
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug -R EasyGL --output-on-failure

# EasyGL — unit tests only (no display needed)
ctest --test-dir cmake-build-debug --exclude-regex "EasyGL|easy-gl" --output-on-failure

# All tests
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug --output-on-failure

# Vulkan — build (use -j1 to avoid SPIR-V race)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan -j1
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-vulkan -R Vulkan --output-on-failure

# Bgfx — build + smoke tests
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-bgfx
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-bgfx -R Bgfx --output-on-failure

# Run specific integration tests
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_present_interval
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_fullscreen_field

# Run PresentationParameters unit tests
./cmake-build-debug/CnaTests --gtest_filter="PresentationParameters*"

# Check git history
git log --oneline -20
```

---

## 8. Next smallest tasks

All tasks follow the same pattern: implement/test → build EasyGL → run relevant test →
update GRAPHICS_TASKS.md + NEXT.md → commit + push.

### Task 227 — Backbuffer resize through PP
**Goal**: Verify that changing `BackBufferWidth`/`BackBufferHeight` via
`GDM::setPreferredBackBufferWidth/Height` + `ApplyChanges()` updates the viewport and
backend logical size correctly. Test both the GDM path and direct
`GraphicsDevice::SetPresentationParameters`.
**Files**: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`,
`examples/easygl_backbuffer_resize_test.cpp` (new)
**Verify**: New EasyGL integration test passes.

### Task 228 — Depth/stencil format changes after device creation
**Goal**: Verify that changing `DepthStencilFormat` via `PresentationParameters` and
applying it does not crash and stores the new format in the device PP.
**Files**: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`,
`tests/Microsoft/Xna/Framework/Graphics/PresentationParametersTests.cpp`
**Verify**: Unit test or EasyGL integration test passes.

### Task 229 — MSAA count changes
**Goal**: Verify that changing `MultiSampleCount` via `PresentationParameters` is
either accepted (EasyGL backend supports it) or explicitly rejected with a clear error.
**Files**: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`,
`examples/easygl_msaa_change_test.cpp` (new)
**Verify**: New EasyGL integration test passes.

### Task 231 — VertexBuffer/IndexBuffer/DynamicBuffer API audit
**Goal**: Compare `VertexBuffer`, `DynamicVertexBuffer`, `IndexBuffer`,
`DynamicIndexBuffer` against FNA — document or implement missing constructors
and `SetData` overloads.
**Files**: `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp`,
`src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp`, FNA reference.
**Verify**: Build + existing EasyGL tests still pass.

### Task 232 — VertexBuffer::SetData stride and offset tests
**Goal**: Add unit or EasyGL tests for `SetData` with non-zero `startIndex`,
`elementCount < total`, and explicit `vertexStride`.
**Files**: `tests/Microsoft/Xna/Framework/Graphics/VertexBufferTests.cpp` (new or extend),
`examples/easygl_vertexbuffer_setdata_test.cpp` (new)
**Verify**: New tests pass.

---

## 9. Do not do yet

- **Do not implement Framework.Net** — out of scope for current phase.
- **Do not add .xnb content pipeline** — custom descriptor format is the current contract.
- **Do not refactor IGraphicsBackend** — changing the interface breaks all 4 backends at once.
- **Do not change the `Color` memory layout** — packed ABGR order is relied on by all backends.
- **Do not convert integration tests to unit tests without a mock device** — there is no
  fake `GraphicsDevice`; integration tests using `Game` + EasyGL are the established pattern.
- **Do not investigate the `easy-gl-resource-smoke-tests` failure** until the upstream
  easygl library is updated; it is not a CNA issue.
- **Do not fix the MRT bug** until Phase 28 (PresentationParameters) is complete and a
  proper reproduction test is written.
- **Do not implement Bgfx 3D state** (`SetDepthTestEnabled`, `SetBlendEnabled`) — deferred.
- **Do not remove the manual `gdm_->ApplyChanges()` from `easygl_present_interval_test.cpp`**
  yet — it is harmless as a no-op and removing it is a cosmetic-only change.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: Tasks 1–225 complete. Next unstarted: Task 227
(Verify BackBufferWidth/Height changes via GDM ApplyChanges() update the
viewport and backend logical size; add EasyGL integration test).

After finishing: build cmake-build-debug, run the affected tests, update
GRAPHICS_TASKS.md (mark task ✅) and NEXT.md, then commit and push to develop.
```
