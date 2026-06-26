# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering
to one of four backends: SDL\_Renderer, EasyGL (OpenGL ES 3.2), Vulkan, or Bgfx.

**Current phase**: Phase 28 — PresentationParameters and GraphicsDeviceManager
conformance (Tasks 221–230). Task 224 complete.

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
- Recently confirmed working (Tasks 215–223):
  - GPU handle released on `Dispose()` (not on C++ destructor)
  - Move semantics for `VertexBuffer` and `IndexBuffer` (no double-free)
  - `ResourceCreated`/`ResourceDestroyed` events fired from `GraphicsResource`
  - `GraphicsDevice` tracks resources and disposes them before backend teardown
  - Resource leak-check: 80 resources created/disposed with zero handle leaks
  - `PresentationParameters` fully conforms to FNA (all 10 fields, correct 800×480 defaults)
  - `PresentationInterval` → VSync mapping implemented across all four backends
  - `Texture`/`Texture2D`/`RenderTarget2D` C++ name-hiding bug fixed (`using` declarations)

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
- **GDM not registered as IGraphicsDeviceManager service** — `Game::DoInitialize()`
  cannot find GDM through `Services_.GetService<IGraphicsDeviceManager>()`. Its
  `CreateDevice()` is only reachable if called directly. `ApplyChanges()` must be
  called manually in `Initialize()` overrides when GDM prefs change after construction.

---

## 3. Recent changes

| Task / Commit | What changed |
|---|---|
| Task 224 | `IsFullScreen` field consistency: `SDL_SetWindowFullscreen` failure changed from throw to soft-clear (SDL_ClearError) — backends that cannot switch fullscreen no longer crash; 7/7 PASS (`easygl_fullscreen_field_test.cpp`) verifies field round-trip through GDM setter + `ApplyChanges()` + `ToggleFullScreen()` |
| Task 223 | `PresentationInterval` → VSync mapping: `swapInterval` added to `GraphicsBackendCreateArgs`; `IGraphicsBackend::SetSwapInterval` virtual method; EasyGL calls `SDL_GL_SetSwapInterval`; SDL_Renderer calls `SDL_SetRenderVSync`; Vulkan picks present mode at swapchain creation; Bgfx uses `resetFlags_` + `bgfx::reset`; runtime change via `GraphicsDevice::SetPresentationParameters`; 10/10 smoke-test PASS (`easygl_present_interval_test.cpp`) |
| Task 221+222 | `PresentationParameters` audit: fixed default dimensions bug (1024×768→800×480); fixed C++ name-hiding bug (`Texture`/`Texture2D`/`RenderTarget2D::Dispose(bool)` hid base `Dispose()`; added `using` declarations); added `SetDisplayOrientation`, `SetDeviceWindowHandle` tests; extended `CloneCopiesAllFields` to all 10 fields; 26/26 unit tests pass |
| Task 220 | `docs/graphics-resource-lifetime.md` created: GPU handle ownership, Dispose(bool) chain, GraphicsDevice tracking, move semantics caveat, backend-specific caveats |
| Task 219 | Resource leak-check: `GetTrackedResourceCount()` NOXNA added; 80 resources created/disposed; 7/7 PASS (`easygl_resource_leak_test.cpp`) |
| Task 218 | `GraphicsDevice::resources_` tracking list; `AddResourceReference`/`RemoveResourceReference`; copy-and-clear disposal pattern; 11/11 PASS (`easygl_device_dispose_order_test.cpp`) |
| Task 217 | `ResourceCreated`/`ResourceDestroyed` events wired through `GraphicsResource` ctor/`Dispose(bool)`; 9/9 PASS (`easygl_resource_events_test.cpp`) |
| Task 216 | Move semantics for `VertexBuffer` and `IndexBuffer`: declared in `.hpp`, `= default` in `.cpp` (pimpl incomplete-type); 21/21 PASS (`easygl_move_semantics_test.cpp`) |
| Task 215 | `Dispose(bool)` override in `VertexBuffer`, `IndexBuffer`, `Texture2D` calls `backend_.reset()` before base; `HasBackend()` NOXNA accessor; 16/16 PASS (`easygl_handle_release_test.cpp`) |

**Files added (Tasks 215–224):**
- `docs/graphics-resource-lifetime.md`
- `examples/easygl_handle_release_test.cpp`
- `examples/easygl_move_semantics_test.cpp`
- `examples/easygl_resource_events_test.cpp`
- `examples/easygl_device_dispose_order_test.cpp`
- `examples/easygl_resource_leak_test.cpp`
- `examples/easygl_present_interval_test.cpp`
- `examples/easygl_fullscreen_field_test.cpp`

**Files modified (Tasks 215–224):**
- `include/Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/Texture.hpp`
- `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp` / `.cpp`
- `include/Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp`
- `include/Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp`
- `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`
- `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp` / `.cpp`
- `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp` / `.cpp`
- `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp` / `.cpp`
- `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp` / `.cpp`
- `tests/Microsoft/Xna/Framework/Graphics/PresentationParametersTests.cpp`
- `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp` (Task 224: soft-fail SDL_SetWindowFullscreen)

---

## 4. Current blocker / main problem

No hard blocker. Two pre-existing test failures exist but are not caused by recent work:

1. **`EasyGL_MRT_TwoAttachments`** (test 1655): Multiple Render Target test fails.
   - Suspected: FBO attachment count or draw buffer setup in EasyGL backend.
   - Affected: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`, `SetRenderTargets`.
   - Not investigated yet.

2. **`easy-gl-resource-smoke-tests`** (test 1694): Upstream easygl library assertion.
   - Assertion: `g_state.last_active_texture == GL_TEXTURE0` in `SmokeResourceTests.cpp:336`.
   - Affected: `vendor/easy-gl` or linked easygl dependency — not in CNA code directly.
   - Not investigated yet.

**Architectural gap discovered in Task 223**: `GraphicsDeviceManager` does not register
itself as `IGraphicsDeviceManager` in `Game::Services_`. This means `Game::DoInitialize()`
cannot call `GDM::CreateDevice()` automatically. Workaround: call `gdm_->ApplyChanges()`
from `Game::Initialize()` after changing GDM preferences. This gap is documented in
`registerServices()` comments (GDM.cpp line 491). It should eventually be fixed but is
low priority since the workaround is well-understood.

---

## 5. Known bugs and limitations

| Status | Item |
|---|---|
| **confirmed pre-existing** | `EasyGL_MRT_TwoAttachments` fails — MRT FBO setup bug in EasyGL backend |
| **confirmed pre-existing** | `easy-gl-resource-smoke-tests` fails — upstream easygl `GL_TEXTURE0` assertion |
| **architectural gap** | `GDM` not registered as `IGraphicsDeviceManager` service — `Game::DoInitialize()` skips `CreateDevice()` |
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
- `ResourceCreated` fires in `GraphicsResource` ctor (after `AddResourceReference`).
- `ResourceDestroyed` fires in `GraphicsResource::Dispose(bool)` (before `RemoveResourceReference`).
- See `docs/graphics-resource-lifetime.md` for the full specification.

### PresentationParameters / GDM wiring

- `GDM` ctor calls `ApplyChanges()` once during construction with default prefs.
- `GDM` does NOT register itself as `IGraphicsDeviceManager` service (documented gap).
- After changing GDM preferences post-construction, `gdm_->ApplyChanges()` must be called explicitly.
- `GraphicsDevice::SetPresentationParameters(pp)` stores the PP AND calls `backend_->SetSwapInterval(toSwapInterval(pp.getPresentationIntervalProperty()))`.
- `PresentInterval::Default/One → swapInterval=1`, `Two → 2`, `Immediate → 0`.

### RenderTarget lifecycle (EasyGL)

`SetRenderTarget(rt)` → backend binds the RT's FBO; sets `currentRtHeight_` to RT height.
`SetRenderTarget(nullptr)` → backend binds FBO 0 (backbuffer); `currentRtHeight_` = 0.
`GetBackBufferData` reads from whatever FBO is currently bound.
`DiscardContents` → `GraphicsDevice::SetRenderTarget` calls `Clear(0,0,0,255)` after binding.
`PreserveContents` → no auto-clear; Vulkan uses `VK_ATTACHMENT_LOAD_OP_LOAD`.

### Critical invariants

- **`Color` has a vtable pointer** — use `uint8_t[]` + `Color(r,g,b,a)` for pixel I/O.
- **Vulkan build: `-j1`** — race condition in SPIR-V header generation.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.
- **FNA is authoritative** — do not deviate from FNA logic without a `//` comment.
- **`Texture`/`Texture2D`/`RenderTarget2D`** must each have `using BaseClass::Dispose;` to prevent C++ name-hiding of `Dispose()` by `Dispose(bool)` overrides.

---

## 7. Useful commands

```bash
# EasyGL — configure + build + all integration tests
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug -R EasyGL --output-on-failure

# EasyGL — unit tests only (no display needed)
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug --exclude-regex "EasyGL|easy-gl" --output-on-failure

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

# Run one specific test
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_present_interval

# Run PresentationParameters unit tests
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/CnaTests --gtest_filter="PresentationParameters*"

# Check git history
git log --oneline -20
```

---

## 8. Next smallest tasks

All tasks follow the same pattern: implement/test → build EasyGL → run relevant test →
update GRAPHICS_TASKS.md + NEXT.md → commit + push.

### Task 225 — GDM service registration gap
**Goal**: Register `GraphicsDeviceManager` as both `IGraphicsDeviceManager` and
`IGraphicsDeviceService` in `registerServices()` so `Game::DoInitialize()` can
call `GDM::CreateDevice()` automatically (matches FNA behavior).
**Files**: `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`,
`include/Microsoft/Xna/Framework/Game.hpp`, `src/Microsoft/Xna/Framework/Game.cpp`
**Verify**: `DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_present_interval`

### Task 227 — Backbuffer resize through PP
**Goal**: Verify that changing `BackBufferWidth`/`BackBufferHeight` via
`SetPresentationParameters` or `GDM::ApplyChanges()` updates the viewport and
backend logical size correctly.
**Files**: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`,
`examples/easygl_backbuffer_resize_test.cpp` (new)
**Verify**: New EasyGL integration test

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
- **Do not make speculative changes to the GDM service registration** until Task 225 is
  formally scoped and tested end-to-end.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: Tasks 1–224 complete. Next unstarted: Task 225
(GDM service registration gap: register GraphicsDeviceManager as IGraphicsDeviceManager
and IGraphicsDeviceService in registerServices() so Game::DoInitialize() can call
GDM::CreateDevice() automatically — matches FNA behavior).

After finishing: build cmake-build-debug, run the affected tests, update
GRAPHICS_TASKS.md (mark task ✅) and NEXT.md, then commit and push to develop.
```
