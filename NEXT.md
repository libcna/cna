# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering
to one of four backends: SDL\_Renderer, EasyGL (OpenGL ES 3.2), Vulkan, or Bgfx.

**Current phase**: Phase 24 in progress — stock effects backend parity.
Tasks 1–198 done. Next unstarted: Task 199.

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
- **Tests**: 36/36 EasyGL integration tests pass; ~1540 total tests pass.
- Recently confirmed working:
  - SpriteBatch all overloads, SpriteEffects flip, transformMatrix translation
  - Texture2D partial-rect / startIndex / mip-level SetData/GetData (Tasks 169–171)
  - TextureCube 6-face SetData/GetData round-trip (Task 172)
  - Texture3D z-slice SetData/GetData round-trip (Task 173)
  - RenderTargetUsage DiscardContents/PreserveContents (Task 177)
  - Backbuffer → RT → backbuffer → RT → backbuffer round-trip (Task 180)
  - SkinnedEffect bone transforms: identity / translate / 2-bone blend (Task 193)
  - BasicEffect::EnableDefaultLighting() exact FNA constants (Task 194)
  - BasicEffect linear fog: disabled / 50% / full, verified via pixel readback (Task 195)

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean.
- **Tests**: 9/9 Vulkan integration tests pass.
- MSAA 4×, per-slot SamplerState, custom SPIR-V Effect, all stock effects,
  FillMode::WireFrame, RenderTargetUsage DiscardContents/PreserveContents (Task 178)
  — all confirmed via integration tests.

### Bgfx backend (`cmake-build-bgfx`)
- **Builds**: clean.
- `ClearColorAndDepth` now implemented (Task 179) — no longer throws on `SetRenderTarget`.
- RenderTargetUsage smoke test passes (bind/unbind, no pixel verification).
- EnvironmentMapEffect and ShaderEffect not implemented (falls back / returns nullptr).
- `GetBackBufferData` via `requestScreenShot` not integration-tested.

### What does not work yet
- **Framework.Net** — 0 % (NetworkSession, PacketReader/Writer entirely absent).
- **Content pipeline (.xnb)** — 0 % (ContentManager uses custom JSON/PNG/OGG only).
- **GamerServices** — ~5 % (stubs only).
- **sRGB SurfaceFormats** — silently map to linear GL/Vulkan internal formats.
- **Bgfx pixel readback** — `SetDepthTestEnabled` / `SetBlendEnabled` still throw;
  no 3D draw calls possible in Bgfx integration tests.

---

## 3. Recent changes

| Task / Commit | What changed |
|---|---|
| Task 198 | PackedVector bug fixes: `HalfTypeHelper::Convert(float)` uint32_t exp underflow (0.0f→infinity fixed); `NormalizedByte2/4` and `NormalizedShort2/4` Pack truncation→`std::lroundf`; golden file corrected for -1.0 inputs; 20 new exact-value tests; 1638/1640 pass |
| Task 197 | PackedVector golden values: computed FNA bit-packing formulas for all 17 compound types (Alpha8→Short4) via Python; saved reference table to `tests/PackedVectorGolden.md` |
| Task 196 | Backend parity table: added §7 to `docs/xna-4-api-coverage.md` — per-effect EasyGL/Vulkan/Bgfx/SDL status table for all 6 stock effects + ShaderEffect; known-gaps table |
| Task 195 | `BasicEffect` linear fog (EasyGL): added fog fields to `GpuDrawParams`, fog uniforms+logic to 4 shaders (colored/textured/col+textured/lit+textured); `easygl_basiceffect_fog_test.cpp`; 3 PASS; 36/36 EasyGL |
| Task 194 | `BasicEffect::EnableDefaultLighting()` exact constants (EasyGL): fixed Light2.SpecularColor bug (was Zero, now `(0.3231373,0.3607844,0.3937255)`) and Light2.DiffuseColor.Y typo; `easygl_basiceffect_default_lighting_test.cpp`; 14/14 PASS; 35/35 EasyGL |
| Task 193 | `SkinnedEffect` bone count tests (EasyGL): `easygl_skinned_effect_bones_test.cpp`; 3 sub-tests: (a) 1 bone identity, (b) 1 bone translate(+0.5), (c) 2-bone 50/50 blend; 8/8 pixel checks PASS; 34/34 EasyGL |
| Task 192 | `EnvironmentMapEffect` parameter accuracy (EasyGL): extended `easygl_env_map_test.cpp` with 4 sub-tests: EmissiveColor=red→red, EmissiveColor=green→green, EnvMapSpecular=blue→blue, EnvMapAmount=1 blue cube→blue; 4/4 PASS; 33/33 EasyGL |
| Task 191 | `DualTextureEffect` pixel tests (EasyGL): 4 sub-tests prove both texture slots and diffuse multiplier work; decisive test yellow×cyan→green; 4/4 PASS; 33/33 EasyGL |
| Task 190 | `AlphaTestEffect` all 8 `CompareFunction` modes (EasyGL): pixel.a=128/255, ref=128; drawn: Always/LessEqual/Equal/GreaterEqual; discarded: Never/Less/NotEqual/Greater; 8/8 PASS; 32/32 EasyGL |
| Task 189 | `BasicEffect` pixel integration tests (EasyGL): 5 sub-tests covering vertex-color-only (stride 16), texture-only (stride 20), diffuse tint (stride 20), color×texture (stride 24), directional lighting (stride 32); fog skipped — no fog in EasyGL GpuDrawParams; 5/5 PASS; 31/31 EasyGL |
| Task 188 | `EffectAnnotation` + `EffectAnnotationCollection`: added `cachedString` ctor param; 31 tests cover all `GetValue*` types, string round-trip, collection indexing/iteration, technique/pass annotations start empty |
| Task 187 | `SetValueTranspose` edge cases: 6 new tests verify raw layout (col-major) differs from `SetValue` (row-major), `GetValueMatrix` returns `Transpose(m)`, equivalence with `SetValue(Transpose(m))`; 52/52 pass |
| Task 186 | `EffectParameter` array guards: FNA silently ignores type mismatch and excess elements; NaN stored without throw (FNA non-debug mode); 6 new tests, 46/46 EffectParameter tests pass |
| Task 185 | `Effect::CurrentTechnique` + collection semantics: added `GetParameterBySemantic`; new `EffectCollectionTests.cpp` (38 tests); EasyGL test verifies get/set + `Passes[0].Apply()`; 7/7 PASS; 30/30 EasyGL |
| Task 184 | `Effect::Clone()` independence: `AlphaTestEffect` clone has distinct pointer, Alpha+DiffuseColor match, mutations in both directions stay isolated; 7/7 PASS; 29/29 EasyGL |
| Task 183 | `DeviceResetting`/`DeviceReset` events: integration test verifies GDM events fire in order (Resetting→Reset) on second `ApplyChanges()`; 5/5 PASS; 28/28 EasyGL |
| Task 182 | `PresentationParameters` round-trip: fixed `applyToExistingBackend` to call `SetPresentationParameters(pp)`; added NOXNA `GraphicsDevice::SetPresentationParameters`; 5/5 PASS; 27/27 EasyGL |
| Task 181 | `RenderTargetBinding` unit tests: all 6 CubeMapFace values, distinct array slices, cube ctor arraySlice=0, default face; 13/13 PASS |
| Task 180 | EasyGL RT round-trip: backbuffer→RT→backbuffer→RT→backbuffer; direct FBO readback; 3/3 PASS; 26/26 EasyGL |
| Task 179 | Bgfx: implemented `ClearColorAndDepth` (delegates to `Clear`); `BindAsRenderTarget` calls `setViewClear(BGFX_CLEAR_NONE)` for PreserveContents; smoke test PASS |
| Task 178 | Vulkan: added `rtRenderPassLoad_` (LOAD_OP_LOAD); `preserveContents_` in `VulkanRenderTargetBackend`; `CreateRenderTarget2D` bool propagated from XNA layer; 3/3 PASS |
| Task 177 | EasyGL: `GraphicsDevice::SetRenderTarget` calls `Clear(0,0,0,255)` on DiscardContents; PreserveContents skips clear; 3/3 PASS |
| Task 176 | `Texture::ValidateFormat` throws `std::runtime_error` for non-Color formats; called in Texture2D/3D/Cube ctors |
| Task 175 | DxtUtil golden decode test found pre-existing, 6/6 PASS |
| Task 174 | `docs/surface-format-support.md` — EasyGL/Vulkan/Bgfx/SDL format support matrix |
| Task 173 | Texture3D z-slice round-trip; Color→uint8\_t bug fixed; ~Texture3D() moved to .cpp |
| Task 172 | TextureCube 6-face round-trip; Color→uint8\_t conversion bug fixed |

**Files added (recent):**
- `tests/PackedVectorGolden.md` (Task 197)
- `examples/easygl_basiceffect_fog_test.cpp` (Task 195)
- `examples/easygl_basiceffect_default_lighting_test.cpp` (Task 194)
- `examples/easygl_skinned_effect_bones_test.cpp` (Task 193)

**Files modified (recent):**
- `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` — added fog fields to `GpuDrawParams`
- `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp` — added `loc_fog_*` to `Prog3D`
- `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` — fog uniforms in 4 shaders + `BindDrawParams`
- `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` — `FillGpuDrawParams` populates fog; fixed `EnableDefaultLighting` Light2.SpecularColor bug
- `examples/easygl_dualtexture_test.cpp` (Task 191)
- `examples/easygl_alphatest_modes_test.cpp` (Task 190)
- `examples/easygl_basiceffect_combinations_test.cpp` (Task 189)
- `examples/easygl_effect_current_technique_test.cpp` (Task 185)
- `tests/Microsoft/Xna/Framework/Graphics/EffectCollectionTests.cpp` (Task 185)
- `examples/easygl_effect_clone_test.cpp` (Task 184)
- `examples/easygl_device_reset_events_test.cpp` (Task 183)
- `examples/easygl_rt_roundtrip_test.cpp` (Task 180)
- `examples/bgfx_render_target_usage_test.cpp` (Task 179)
- `examples/easygl_render_target_usage_test.cpp` (Task 177)
- `docs/surface-format-support.md` (Task 174)

**Files modified (recent):**
- `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` — `ClearColorAndDepth` implemented; `BindAsRenderTarget` for PreserveContents
- `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` — `rtRenderPassLoad_` added
- `src/Microsoft/Xna/Framework/Graphics/RenderTarget2D.cpp` — passes `preserveContents` to backend
- `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` — `CreateRenderTarget2D` gains `bool preserveContents`
- `CMakeLists.txt` — 3 new integration test targets (EasyGL RT usage, EasyGL RT roundtrip, Bgfx RT usage)

---

## 4. Current blocker / main problem

No active blocker. All three backends build clean.
- 36/36 EasyGL integration tests pass.
- 9/9 Vulkan integration tests pass.
- Bgfx smoke tests pass.

Next task: Task 199 (PackedVector edge-case tests: inputs −1.0, 0.0, 1.0, values slightly outside [0,1] or [−1,1] range, `NaN`, `+Inf`, `−Inf`, half-float special values).

---

## 5. Known bugs and limitations

| Status | Item |
|---|---|
| **confirmed bug (fixed)** | `getMipBuffer(0)` left empty after `MaybeFreeCpuPixels`; caused UB on partial-rect SetData |
| **confirmed bug (fixed)** | SetData/GetData guard `startIndex + elementCount > w*h` rejected valid non-zero startIndex |
| **confirmed bug (fixed)** | EasyGL FlushBatch: `orthoM * transform_` applied projection before user transform |
| **confirmed bug (fixed)** | `TextureCube/Texture3D::SetData/GetData` passed raw `Color*` (vtable at offset 0) to GL |
| **confirmed bug (fixed)** | `BgfxGraphicsBackend::ClearColorAndDepth` threw unconditionally; now delegates to `Clear` |
| **confirmed working** | `UpdatePixelsLevel` (mip > 0 upload) — Task 171 mip round-trip |
| **confirmed working** | `TextureCube::GetData` round-trip — Task 172 |
| **confirmed working** | `Texture3D::GetData` round-trip — Task 173 |
| **confirmed working** | `RenderTargetUsage` DiscardContents/PreserveContents — EasyGL (177), Vulkan (178), Bgfx (179) |
| **known limit** | EasyGL `FillMode::WireFrame` — no `glPolygonMode` on GLES3 |
| **known limit** | Bgfx `SetDepthTestEnabled` / `SetBlendEnabled` / `SetDepthWriteEnabled` still throw |
| **known limit** | Bgfx `GetBackBufferData` via `requestScreenShot` — not integration-tested |
| **incomplete** | sRGB SurfaceFormats silently map to linear GL/Vulkan internal formats |
| **0 %** | Framework.Net (NetworkSession, PacketReader/Writer) — no headers, no stubs |
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

### RenderTarget lifecycle (EasyGL)

`SetRenderTarget(rt)` → backend binds the RT's FBO; sets `currentRtHeight_` to RT height.
`SetRenderTarget(nullptr)` → backend binds FBO 0 (backbuffer); `currentRtHeight_` = 0.
`GetBackBufferData` reads from whatever FBO is currently bound:
- `currentRtHeight_ != 0` → reads from RT attachment (direct FBO readback).
- `currentRtHeight_ == 0` → sets `GL_BACK` as read buffer, reads from FBO 0.

`DiscardContents` → `GraphicsDevice::SetRenderTarget` calls `Clear(0,0,0,255)` after binding.
`PreserveContents` → no auto-clear; Vulkan uses `VK_ATTACHMENT_LOAD_OP_LOAD`.

### Texture CPU-side shadow copy invariant

`Texture2D` maintains a CPU-side shadow in `cpuPixels_` (shared_ptr).
After any constructor or 2-arg `SetData`, `MaybeFreeCpuPixels()` is called:
- if `contextRecoveryEnabled_ == false` (default) → `cpuPixels_` is freed.
- The 5-arg `SetData(level, rect, data, start, count)` does NOT call
  `MaybeFreeCpuPixels`, so the shadow survives across chained calls.
- `getMipBuffer(0)` auto-sizes to `mipDim(w,0) * mipDim(h,0) * 4` when empty.

### EasyGL sprite batch matrix convention

`FlushBatch` computes: `combined = transform_ * orthoM` (XNA row-major order).
`transform_` is the user matrix from `SpriteBatch::Begin`; `orthoM` is the
screen-space orthographic projection.

### Vulkan pipeline key encoding

All 3D pipeline creation functions encode `drawMsaa` as the last bool argument.
The MSAA render pass (`renderPassMsaa_`) uses 3 attachments and 3 clear values;
the non-MSAA path uses 2. The active render pass is selected per-frame in
`RecordCommandBuffer`.
RT render pass `rtRenderPass_` uses `LOAD_OP_CLEAR` (DiscardContents);
`rtRenderPassLoad_` uses `LOAD_OP_LOAD` (PreserveContents).

### Critical invariants

- **`Color` has a vtable pointer** — use `uint8_t[]` + `Color(r,g,b,a)` for pixel I/O.
- **Vulkan build: `-j1`** — race condition in SPIR-V header generation.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.
- **FNA is authoritative** — do not deviate from FNA logic without a `//` comment.

---

## 7. Useful commands

```bash
# EasyGL — configure + build + all integration tests
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug -R EasyGL --output-on-failure

# EasyGL — unit tests only
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug --exclude-regex EasyGL --output-on-failure

# Vulkan — build (use -j1 to avoid SPIR-V race)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan -j1
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-vulkan -R Vulkan --output-on-failure

# Bgfx — build + smoke tests
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-bgfx
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-bgfx -R Bgfx --output-on-failure

# Run one specific integration test
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_rt_roundtrip

# Bgfx — recompile 3D shaders (run from repo root)
python3 src/CNA/Internal/Backends/Bgfx/shaders/compile_shaders.py \
    cmake-build-bgfx/_deps/bgfx_cmake-build/cmake/bgfx/shaderc \
    cmake-build-bgfx/_deps/bgfx_cmake-src/bgfx/src
```

---

## 8. Next smallest tasks

All following the same pattern: EasyGL integration test or unit test in `tests/` or
`examples/`, registered in `CMakeLists.txt`, GRAPHICS\_TASKS.md marked ✅, NEXT.md updated.

### Task 197 — PackedVector golden values ✅

**Done**: `tests/PackedVectorGolden.md` created (corrected in Task 198).

### Task 198 — PackedVector bug fixes ✅

**Bugs fixed:**
- `HalfTypeHelper::Convert(float)`: `uint32_t exp` wrapped on negative exponents; 0.0f was packed as 0x7C00 (infinity). Fixed by rewriting to use `int32_t` arithmetic matching FNA's `Convert(int)`.
- `NormalizedByte2/4::Pack`: used C++ truncation (`int8_t(v*127.0f)`); FNA uses `Math.Round()`. For x=0.5, CNA gave 63, FNA gives 64. Fixed with `std::lroundf`.
- `NormalizedShort2/4::Pack`: same truncation bug. For x=0.5, CNA gave 16383, FNA gives 16384. Fixed with `std::lroundf`.
- `tests/PackedVectorGolden.md`: corrected -1.0 input rows (Python `+0.5` formula disagrees with C# `Math.Round` for negative values; correct packed byte for -1.0 is 0x81 not 0x82).

**20 new tests** covering HalfSingle, HalfVector2, NormalizedByte2/4, NormalizedShort2/4 with exact packed-value assertions from the golden file.

Next: Task 199 — PackedVector edge-case inputs.

---

## 9. Do not do yet

- **Do not implement Framework.Net** — out of scope for current phase.
- **Do not add .xnb content pipeline** — custom descriptor format is the current contract.
- **Do not refactor IGraphicsBackend** — changing the interface breaks all 4 backends at once.
- **Do not change the `Color` memory layout** — packed ABGR order is relied on by all backends.
- **Do not convert integration tests to unit tests without a mock device** — there is no
  fake `GraphicsDevice`; integration tests using `Game` + EasyGL are the established pattern.
- **Do not implement Bgfx 3D state** (`SetDepthTestEnabled`, `SetBlendEnabled`, etc.) until
  Phase 22 (Tasks 174–183) is fully complete.
- **Do not start Tasks 201–500** (deep conformance, golden-image, FNA harness) until
  Phase 22 and Phase 23 (effect conformance) are done.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: Tasks 1–197 complete. Next unstarted: Task 198
(compare CNA PackedVector output against Task 197 golden values; fix bugs).

Task 197 was a documentation/analysis task: computed FNA bit-packing golden values
for all 17 PackedVector types via Python; saved to `tests/PackedVectorGolden.md`.
No code changed; no build needed.
```
