# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering
to one of four backends: SDL\_Renderer, EasyGL (OpenGL ES 3.2), Vulkan, or Bgfx.

**Current development phase**: Phase 30 — VertexDeclaration and vertex format accuracy
(Tasks 241–250). All tasks complete ✅.

**Key architectural decisions**:
- Backend selected at **compile time** via `CNA_GRAPHICS_BACKEND` CMake option.
- `IGraphicsBackend` is the sole contract between the XNA API layer and any backend.
- `Color` inherits `IPackedVectorT<UInt32>` (virtual base) — vtable pointer precedes
  the packed pixel; never cast `Color*` to `uint8_t*` for GL pixel I/O.
- SharpRuntime (`/rv/data/development/github.com/openeggbert/sharp-runtime`) provides
  .NET primitive type aliases and `System.*` stubs.
- FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src` is the authoritative
  XNA 4.0 API reference; all logic is ported line-by-line from there.
- `NOXNA` macro tags any member that is not part of the XNA 4.0 public API.

---

## 2. Current status

### EasyGL backend (`cmake-build-debug`) — primary backend
- **Builds**: clean.
- **Unit tests (CnaTests)**: 1715/1715 pass.
- **Integration tests**: 60 EasyGL integration test executables built; 2 pre-existing
  failures exist (see Known bugs).
- **Vulkan tests**: 11/11 pass (in `cmake-build-vulkan`).

### Recently verified working (Phases 27–30, Tasks 211–250):
- `GraphicsResource` base class wired to all 8 major resource types; disposal chain correct.
- `VertexBuffer`, `DynamicVertexBuffer`, `IndexBuffer`, `DynamicIndexBuffer`:
  FNA API-conformant; `SetDataOptions` (Discard/NoOverwrite) wired in EasyGL;
  disposed-buffer guards throw `ObjectDisposedException`.
- `PresentationParameters`: correct 800×480 defaults, all 10 fields, fully tested.
- `PresentationInterval` → VSync mapping across all 4 backends.
- GDM registers as `IGraphicsDeviceManager` + `IGraphicsDeviceService`; `Game::DoInitialize()` calls `CreateDevice()` automatically.
- `IsFullScreen`: stored in PP regardless of backend capability; fullscreen failure soft-skips.
- `VertexDeclaration`, `VertexElement`, `VertexElementFormat`, `VertexElementUsage`:
  FNA-conformant; all 4 built-in vertex structs have `Equals`, `GetHashCode`, `ToString`;
  enum numeric values 0–11/0–12 tested; `GetTypeName` fixed (was returning escaped-quoted string).
- Vulkan depth bias (`RasterizerState.DepthBias` / `SlopeScaleDepthBias`): wired end-to-end;
  `ReadBackbuffer` stable (checks `SubmitFrame()` return value; 20/20 passes).
- Vulkan vertex format helper: `VertexElementFormatToVk()` + `VertexElementFormatSize()` for
  all 12 `VertexElementFormat` values; 30/30 pixel-readback tests pass.
- Bgfx vertex format helper: `VertexElementFormatToBgfx()` + `VertexElementUsageToBgfxAttrib()` +
  `VertexElementFormatSize()` for all 12 VEF/13 VEU values; `Bgfx_VertexFormatMapping` 1/1 PASS.
- `docs/vertex-format-support.md`: per-backend format/usage tables, stride fallback behavior,
  SDL_Renderer limitations, future-work section.

### What does not work yet
- **MRT (Multiple Render Targets)**: `EasyGL_MRT_TwoAttachments` fails — pre-existing FBO bug.
- **`VertexBuffer::GetData` / `IndexBuffer::GetData`**: not in the CNA API; no VBO readback.
- **`RasterizerState.DepthBias` / `SlopeScaleDepthBias`** on EasyGL and Bgfx: accepted but ignored (no `glPolygonOffset` wiring).
- **`FillMode::WireFrame`** on EasyGL/GLES3: not supported (`glPolygonMode` unavailable on GLES).
- **Bgfx `SetDepthTestEnabled` / `SetBlendEnabled`**: still throw.
- **SpriteBatch multiple Begin/End per frame**: only the last batch renders (Vulkan; others unknown).
- **Framework.Net** (NetworkSession, PacketReader/Writer): 0%.
- **Content pipeline (.xnb)**: 0% — CNA uses custom JSON/PNG/OGG descriptors.
- **GamerServices**: ~5% (Guide.Show no-op only).
- **sRGB SurfaceFormats**: silently mapped to linear GL/Vulkan internal formats.
- **Bgfx pixel readback**: not integration-tested.
- **WebGPU backend**: phases 56–69 planned; vendor headers added (`vendor/wgpu-native/`) but no CMake integration yet.

---

## 3. Recent changes

### Task 250 (Phase 30, this session)
- **`docs/vertex-format-support.md`** (new): per-backend tables for all 12 `VertexElementFormat` and 13 `VertexElementUsage` values across EasyGL/Vulkan/Bgfx/SDL_Renderer; stride-keyed layout fallback behavior documented; SDL_Renderer limitations; future-work section.
- **`GRAPHICS_TASKS.md`**: Task 250 marked ✅. Phase 30 complete.

### Task 249 (Phase 30, this session)
- **`include/CNA/Internal/Backends/Bgfx/BgfxVertexFormatHelper.hpp`** (new): `BgfxAttribInfo` struct; `VertexElementFormatToBgfx()` for all 12 VEF values; `VertexElementUsageToBgfxAttrib()` for all 13 VEU values (unsupported usages return `bgfx::Attrib::Count`); `VertexElementFormatSize()` matching FNA sizes.
- **`examples/bgfx_vertex_format_test.cpp`** (new): 47 mapping/size checks + 4 VertexBuffer creation smoke tests (stride 16/20/24/32); `Bgfx_VertexFormatMapping` 1/1 PASS.
- **`CMakeLists.txt`**: `cna_test_bgfx_vertex_format` + `Bgfx_VertexFormatMapping` ctest added.
- **`GRAPHICS_TASKS.md`**: Task 249 marked ✅.

### Task 247 (Phase 30, this session — not yet committed)
- **`examples/easygl_vertex_formats_test.cpp`** (new): 4 sub-tests — stride=16 (Vector3+Color), stride=20 (Vector3+Vector2), stride=24 (Vector3+Color+Vector2), stride=32 (Vector3+Vector3+Vector2); all via VertexBuffer+DrawPrimitives; 4/4 PASS, centre=(255,0,0) each.
- **`CMakeLists.txt`**: `cna_test_easygl_vertex_formats` + `EasyGL_VertexFormats_AllStrides` ctest added.
- **`GRAPHICS_TASKS.md`**: Task 247 marked ✅.
- 1745/1745 unit tests pass.

### Task 246 (Phase 30, this session — not yet committed)
- **`tests/…/VertexDeclarationTests.cpp`**: 6 new tests — Tangent/Binormal usage stored and retrieved, auto-stride for Tangent/Binormal Vector3, Pos+Normal+Tangent declaration, full PBR vertex (Pos+Normal+Tangent+Binormal+TexCoord, stride=56).
- **`GRAPHICS_TASKS.md`**: Task 246 marked ✅.
- 1745/1745 unit tests pass.

### Task 245 (Phase 30, this session — not yet committed)
- **`tests/…/VertexDeclarationTests.cpp`**: 11 new tests — per-format auto-stride for all 8 compact formats (Color, Byte4, Short2, Short4, NormalizedShort2, NormalizedShort4, HalfVector2, HalfVector4) + 3 combination tests (Vector3+Byte4, Vector3+NormalizedShort4, Vector3+HalfVector2).
- **`GRAPHICS_TASKS.md`**: Task 245 marked ✅.
- 1739/1739 unit tests pass.

### Task 244 (Phase 30, this session — not yet committed)
- **`tests/…/VertexDeclarationTests.cpp`**: 6 new tests — usageIndex 0/1/2 stored independently for TextureCoordinate, usageIndex independent of other usages, auto-stride with 3 TexCoord channels, mixed decl with 2 TexCoord channels.
- **`GRAPHICS_TASKS.md`**: Task 244 marked ✅.
- 1728/1728 unit tests pass.

### Task 243 (Phase 30, this session — not yet committed)
- **`tests/…/VertexDeclarationTests.cpp`**: 7 new tests — non-zero starting offset, leading padding, inter-element gap, out-of-order offsets (stride still correct), insertion order preservation, explicit stride with trailing padding, explicit stride with non-zero-start element.
- **`GRAPHICS_TASKS.md`**: Task 243 marked ✅.
- 1722/1722 unit tests pass.

### Task 662 (cross-cutting, this session — not yet committed)
- **`sharp-runtime/include/System/Object.hpp`**: fixed `GetTypeNameCPP` macro — changed `#NAME` to `NAME` so that a quoted string literal argument is passed through verbatim (no longer wrapped in extra escaped quotes by `#` stringization).
- **`src/Microsoft/Xna/Framework/Audio/SoundEffectInstance.cpp`**: fixed unquoted caller → `"Microsoft.Xna.Framework.Audio.SoundEffectInstance"`.
- **`src/Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.cpp`**: fixed unquoted caller → `"Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance"`.
- **`GRAPHICS_TASKS.md`**: Task 662 added and marked ✅.
- 1715/1715 unit tests pass.

### Tasks 241–242 (Phase 30 — committed in `312a1f6`)
- **`include/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp`**: added `Equals`, `GetHashCode`.
- **`include/Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp`**: added `Equals`, `GetHashCode`.
- **`include/Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp`**: added `Equals`, `GetHashCode`.
- **`include/Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp`**: added `operator==`, `operator!=`, `Equals`, `GetHashCode`, `ToString` declaration (were missing; other 3 structs already had them).
- **`src/Microsoft/Xna/Framework/Graphics/VertexPositionColor.cpp`**: new file — `ToString` implementation.
- **`src/Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.cpp`**: fixed `ToString` to use actual field values (was a type-placeholder string).
- **`src/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.cpp`**: fixed `ToString` (same).
- **`src/Microsoft/Xna/Framework/Graphics/VertexPositionTexture.cpp`**: fixed `ToString` (same).
- **`src/Microsoft/Xna/Framework/Graphics/VertexDeclaration.cpp`**: fixed `GetTypeName()` — `GetTypeNameCPP` macro's `#NAME` stringizes a quoted arg with escaped quotes; replaced with manual implementation matching the pattern used by other classes.
- **`tests/…/VertexPositionColorTests.cpp`**: added Equals, GetHashCode, ToString tests.
- **`tests/…/VertexPositionNormalTextureTests.cpp`**: added Equals, GetHashCode, ToString tests.
- **`tests/…/VertexPositionTextureTests.cpp`**: added Equals, GetHashCode, ToString tests.
- **`tests/…/VertexPositionColorTextureTests.cpp`**: added Equals, GetHashCode, ToString tests.
- **`tests/…/VertexElementTests.cpp`**: added 12 `VertexElementFormat` numeric value tests (0–11) and 13 `VertexElementUsage` numeric value tests (0–12).
- **`tests/…/VertexDeclarationTests.cpp`**: added `GetTypeName` test.

### Tasks 231–240, 248, 327–328 (previous session — committed in `7c5a5a9`)
- Buffer API audit: `VertexBuffer`, `DynamicVertexBuffer`, `IndexBuffer`, `DynamicIndexBuffer` FNA-conformant.
- Vulkan depth bias end-to-end; `ReadBackbuffer` stability fix.
- Vulkan vertex format helper + 30 pixel-readback tests.
- EasyGL/metagl API compatibility fixes (metagl renaming: `TextureWrap→TextureWrapMode`, `TextureFilter→BlitFilter`, `FramebufferAttachment→to_framebuffer_attachment(ColorAttachment::*)`, etc.).

---

## 4. Current blocker / main problem

**No hard blocker.** The session ended cleanly with 1715/1715 unit tests passing.

Two pre-existing integration test failures remain (not caused by recent work):

1. **`EasyGL_MRT_TwoAttachments`**: Multiple Render Target FBO attachment bug.
   - Symptom: pixel readback after MRT draw reads zeros.
   - Affected: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`, `SetRenderTargets`.
   - Not yet investigated.

2. **`easy-gl-resource-smoke-tests`**: Upstream easygl assertion.
   - Assertion: `g_state.last_active_texture == GL_TEXTURE0` in `vendor/easy-gl` smoke tests.
   - Not a CNA code issue; not investigated.

**Pending commit**: Task 662 (GetTypeNameCPP fix) changes are unstaged in working tree.

---

## 5. Known bugs and limitations

| Status | Item |
|---|---|
| **confirmed pre-existing** | `EasyGL_MRT_TwoAttachments` fails — MRT FBO setup bug in EasyGL backend |
| **confirmed pre-existing** | `easy-gl-resource-smoke-tests` fails — upstream easygl `GL_TEXTURE0` assertion |
| **fixed (Task 662)** | `GetTypeNameCPP` macro: `#NAME` replaced by `NAME` in sharp-runtime; `SoundEffectInstance` and `DynamicSoundEffectInstance` unquoted callers corrected. Remaining Audio callers (`AudioEngine`, `Cue`, `SoundBank`, `WaveBank`, `Accelerometer`) still use `::` namespace separator instead of `.` — inconsistent with Graphics callers but not broken. |
| **confirmed bug (Vulkan)** | `SpriteBatch` multiple `Begin/End` per frame: only the last batch renders; earlier batches silently discarded. Workaround: merge all sprites into one `Begin/End`. See `known_bugs.md`. |
| **missing NOXNA tags** | 7 non-XNA members on `GraphicsDevice` are missing `NOXNA` (documented in `docs/graphicsdevice-fna-audit.md`) |
| **missing XNA methods** | `Present(Rectangle?,Rectangle?,IntPtr)`, `Clear(ClearOptions,Vector4,float,int)`, `GetRenderTargetsNoAllocEXT` |
| **incomplete** | `VertexBuffer::GetData` / `IndexBuffer::GetData` not in CNA API; no VBO/IBO readback |
| **known limit** | EasyGL `FillMode::WireFrame` — no `glPolygonMode` on GLES3 |
| **known limit** | `RasterizerState.DepthBias`/`SlopeScaleDepthBias` applied on Vulkan only; EasyGL/Bgfx accept but ignore |
| **known limit** | Bgfx `SetDepthTestEnabled` / `SetBlendEnabled` / `SetDepthWriteEnabled` still throw |
| **known limit** | Vulkan `SetSwapInterval` does not recreate swapchain at runtime (applied only at creation) |
| **known limit** | SDL_Renderer `PresentInterval::Two` maps to VSync=1 (SDL3 has no swap interval 2) |
| **incomplete** | sRGB `SurfaceFormats` silently map to linear GL/Vulkan internal formats |
| **0 %** | Framework.Net (NetworkSession, PacketReader/Writer) |
| **0 %** | XNA binary `.xnb` content pipeline |
| **~5 %** | GamerServices (Guide.Show no-op only) |
| **planned, not started** | WebGPU backend (Phases 56–69); `vendor/wgpu-native/` headers present but no CMake wiring |

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
- GPU handle freed when `Dispose()` is called (via `backend_.reset()` in `Dispose(bool)`),
  not when the C++ object is destroyed.
- `GraphicsDevice::resources_` tracks all live resources; disposal uses copy-and-clear pattern.
- `Texture`, `Texture2D`, `RenderTarget2D` must each have `using BaseClass::Dispose;` to
  prevent C++ name-hiding.

### PresentationParameters / GDM wiring
- `GDM(Game*)` ctor: registers as `IGraphicsDeviceManager` + `IGraphicsDeviceService`; calls `ApplyChanges()`.
- `Game::DoInitialize()` → `Services_.GetService<IGraphicsDeviceManager>()` → `CreateDevice()`.
- `GraphicsDevice::SetPresentationParameters(pp)` stores the PP AND calls `backend_->SetSwapInterval(...)`.
- `IsFullScreen` is stored in the PP before `SDL_SetWindowFullscreen`. Backend failure is non-fatal.

### Vertex type conventions
- `VertexPositionColor`, `VertexPositionTexture`, `VertexPositionNormalTexture`, `VertexPositionColorTexture`
  all have `operator==`, `operator!=`, `Equals`, `GetHashCode` (returns 0, FNA TODO), `ToString`.
- `ToString` format: `{{Position:X Y Z Color:R G B A}}` (double braces, no quotes).
- `VertexDeclaration::GetTypeName()`: returns `"Microsoft.Xna.Framework.Graphics.VertexDeclaration"`.
- **Warning**: `GetTypeNameCPP(CLASS, "...")` macro is broken when the NAME arg is a quoted string
  literal — the `#NAME` stringization adds extra escaped quotes. Use manual `GetTypeName()` implementations.

### Critical invariants
- **`Color` has a vtable pointer** — use `uint8_t[]` + `Color(r,g,b,a)` for pixel I/O, never cast.
- **Vulkan build: `-j1`** — race condition in SPIR-V header generation.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.
- **FNA is authoritative** — do not deviate from FNA logic without a `//` comment.
- **sharp-runtime inner-exception ctors use `std::exception_ptr`** — not `const std::exception&`.
- **`GLOB_RECURSE "src/*.cpp"`** in `CMakeLists.txt` — new `.cpp` files under `src/` are auto-included.

---

## 7. Useful commands

```bash
# EasyGL — configure + build
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug -j$(nproc)

# Unit tests only (no display needed)
./cmake-build-debug/CnaTests

# Run unit tests via ctest (excludes EasyGL integration tests)
ctest --test-dir cmake-build-debug --exclude-regex "EasyGL|easy-gl"

# EasyGL integration tests (requires display)
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug -R EasyGL --output-on-failure

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

# Run specific test suites
./cmake-build-debug/CnaTests --gtest_filter="VertexPosition*:VertexDeclaration*:VertexElement*"
./cmake-build-debug/CnaTests --gtest_filter="PresentationParameters*"

# Check git history
git log --oneline -15

# Commit pending work (Tasks 241-242)
git -c commit.gpgsign=false add \
  include/Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp \
  include/Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp \
  include/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp \
  include/Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp \
  src/Microsoft/Xna/Framework/Graphics/VertexPositionColor.cpp \
  src/Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.cpp \
  src/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.cpp \
  src/Microsoft/Xna/Framework/Graphics/VertexPositionTexture.cpp \
  src/Microsoft/Xna/Framework/Graphics/VertexDeclaration.cpp \
  tests/Microsoft/Xna/Framework/Graphics/ \
  GRAPHICS_TASKS.md NEXT.md
git -c commit.gpgsign=false commit -m "feat(Tasks 241-242): vertex type audit — Equals/GetHashCode/ToString + enum numeric tests"
```

---

## 8. Next smallest tasks

### Task 329 — Vulkan scissor test enable/disable interaction
**Goal**: Pixel-readback test verifying that enabling `ScissorTestEnable` on
`RasterizerState` and setting `GraphicsDevice::ScissorRectangle` clips correctly.
**Files**: `examples/vulkan_scissor_test.cpp` (new), `CMakeLists.txt`
**Verify**: `DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-vulkan -R vulkan_scissor`

---

## 9. Do not do yet

- **Do not investigate the MRT bug** (`EasyGL_MRT_TwoAttachments`) until a minimal
  reproduction is written first — the current failure is inside a large integration test.
- **Do not investigate `easy-gl-resource-smoke-tests`** — it is an upstream easygl issue.
- **Do not implement `VertexBuffer::GetData` / `IndexBuffer::GetData`** — requires
  VBO readback which EasyGL does not expose; adding it would need new `IGraphicsBackend` methods.
- **Do not start the WebGPU backend** — `vendor/wgpu-native/` headers are present but
  no CMake wiring exists; starting it now would destabilize the existing build.
- **Do not fix the SpriteBatch multiple Begin/End Vulkan bug** until a focused
  minimal reproduction test is written.
- **Do not refactor `IGraphicsBackend`** — any interface change breaks all 4 backends at once.
- **Do not change the `Color` memory layout** — packed ABGR order relied on by all backends.
- **Do not convert integration tests to unit tests without a mock device** — there is no
  fake `GraphicsDevice`; integration tests using `Game` + EasyGL are the established pattern.
- **Do not implement Framework.Net or the .xnb content pipeline** — out of scope.
- **Audio callers of `GetTypeNameCPP`** still use `::` namespace separator instead of `.` — inconsistent but not broken; do not fix unless normalizing all callers in a dedicated task.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: Phase 30 complete (Tasks 241–250); 1745/1745 unit tests pass.

Next: Task 329 (Vulkan scissor test) or start Phase 31 (DrawUserPrimitives variants).

After finishing: build cmake-build-debug, run the affected tests, update
GRAPHICS_TASKS.md (mark task ✅) and NEXT.md, then commit.
```
