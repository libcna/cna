# Texture3D / TextureCube Renderer Support — CNA

> **Metal adaptation note (2026-08-09):** the current Metal factory rejects every format except
> `SurfaceFormat::Color`; it no longer silently accepts and ignores unsupported format requests.
> The adapted Objective-C++ path still needs fresh macOS compile/runtime evidence. See
> `docs/metal-renderer.md`.

> Source-inspected against Tasks 271–279 (Phase 33).
> Covers: EasyGL, Vulkan, Bgfx renderers. SDL_Renderer has no 3D texture support at all
> (2D-only renderer, `CreateTexture3D`/`CreateTextureCube` are unreachable from any XNA-level
> API — `Texture3D`/`TextureCube` construction goes through `GraphicsDevice::GetRenderer()`,
> and SDL_Renderer's 3D-facing factory methods throw `ThrowNo3D` the same way vertex buffers do).
>
> Metal column added later (`plans/plan_metal.md METAL-129`, Phase 11: `METAL-120`–`129`). The historical
> predecessor compiled on Apple Clang, but the post-audit adaptation changes the interfaces and has
> no fresh Apple compile. Metal remains marked 🔍 where only source inspection and portable helper
> tests exist; there is still no dedicated native TextureCube/Texture3D round-trip test.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported and verified with a passing test this session. |
| ⚠️ | Works for the common case, with a real, documented caveat. |
| ❌ | Missing or silently wrong; confirmed by reading the implementation. |
| 🔍 | Same code pattern as a confirmed bug elsewhere, but not independently reproduced with a test — flagged as likely, not confirmed. |

---

## Construction (`Texture3D`/`TextureCube` constructors)

| Renderer | `mipMap` respected? | `SurfaceFormat` respected? |
|---------|:---:|:---:|
| EasyGL  | ⚠️ `TextureCube` only (Task 276 fix) — see "Mip levels" below | ❌ always `Rgba8`, `format` parameter ignored |
| Vulkan  | ❌ dropped in the factory itself — `CreateTexture3D`/`CreateTextureCube` take `bool /*mipMap*/` (commented out) | ❌ always `VK_FORMAT_R8G8B8A8_UNORM` |
| Bgfx    | ⚠️ `TextureCube` — parameter is received but the constructor still hardcodes `bgfx::createTextureCube(size, /*hasMips=*/false, ...)`, so it does nothing today (🔍 same class of bug as Task 276, not yet fixed for Bgfx) | ❌ always `bgfx::TextureFormat::RGBA8` |
| Metal   | 🔍 both `Texture3D`/`TextureCube` honor `mipMap` (`mipmapped:mipMap` for `TextureCube`; a computed level count for `Texture3D`) — full chain allocated up front | ⚠️ accepts only `SurfaceFormat::Color` and rejects every other request; both plain texture types use `MTLPixelFormatRGBA8Unorm` |

EasyGL, Vulkan, and Bgfx still silently reduce every format request to 32-bit RGBA. Metal's adapted
boundary instead accepts Color and throws for unsupported formats.

---

## `SetData` — box / rect sub-region uploads

Verified this session (Tasks 273–275) that CNA's public API layer (`Texture3D::SetData`,
`TextureCube::SetData`) correctly computes arbitrary x/y/z (or x/y, for `TextureCube`) sub-region
offsets and forwards them to the renderer — asymmetric, off-origin, single-voxel, and far-corner
boxes all round-trip correctly through **EasyGL**.

| Renderer | Arbitrary sub-region upload | Verified how |
|---------|:---:|---|
| EasyGL  | ✅ | `glTexSubImage3D`/`glTexSubImage2D` per-face, pixel-verified (Tasks 273–275) |
| Vulkan  | 🔍 | `vkCmdCopyBufferToImage` with `imageOffset`/`imageExtent` set from `x,y,z,w,h,depth` — code inspection shows this is correct, but not independently pixel-verified this session (Vulkan has no `GetData` readback to verify against — see below) |
| Bgfx    | 🔍 | `bgfx::updateTexture3D`/`updateTextureCube` forward the same offsets — code inspection only; Bgfx has no readback API to verify against either |
| Metal   | 🔍 | Adapted SetData validates face/level/region/length with overflow-safe helpers, allocates a replacement texture, blit-preserves every untouched face/mip/slice, writes only the requested region in the replacement, then swaps after completion (`METAL-264`). This avoids in-place mutation while prior draws may sample the old object. Source/portable-policy evidence only; no adapted Apple compile/runtime or native round-trip exists. |

---

## `GetData` — readback

**This is the most significant finding of this documentation task.** `Texture3D`/`TextureCube` have
no CPU-side shadow buffer (unlike `Texture2D`'s `cpuPixels_`) — `GetData` depends entirely on the
renderer's `ITexture3DRenderer`/`ITextureCubeRenderer::GetData` implementation.

| Renderer | `GetData` implemented? |
|---------|:---:|
| EasyGL  | ✅ — per-slice (`Texture3D`) or per-face (`TextureCube`) temporary FBO + `glReadPixels`. Pixel-verified round-trip (Tasks 273–275). |
| Vulkan  | ❌ **total no-op.** Neither `VulkanTexture3DRenderer` nor `VulkanTextureCubeRenderer` overrides `GetData` — both fall through to `ITexture3DRenderer`/`ITextureCubeRenderer`'s base-class default (`virtual void GetData(...) const {}`, an empty body). Calling `Texture3D::GetData`/`TextureCube::GetData` on Vulkan silently leaves the caller's output buffer **completely untouched** — not zeroed, just whatever was already there. No exception, no error, no log message. |
| Bgfx    | ❌ **total no-op**, same reason (no override, falls through to the same empty base-class default). Consistent with Bgfx's already-documented project-wide "no GPU readback API" limitation, but this is the first place that limitation is confirmed to apply specifically to `Texture3D`/`TextureCube`. |
| Metal   | 🔍 real, not a no-op — both override `GetData` via the aligned `blitTextureToClientBuffer()` staging helper shared with Metal render targets. Backbuffer readback is a separate deliberately unsupported path and throws rather than using this helper. Cube/3D readback pads Metal buffer rows to 256 bytes, then de-pads RGBA rows/slices after synchronous completion (`METAL-264`). The historical predecessor compiled on Apple, but the adapted Objective-C++ has no Apple compile/runtime evidence and no dedicated native round-trip pixel test (`METAL-127`/`128` remain open). |

**Why the existing test suite didn't already catch this:** `Texture3DTests.cpp`/`TextureCubeTests.cpp`'s
`GetData*` unit tests are argument-guard tests only (null data throws, negative `startIndex` throws,
out-of-bounds rect throws, "WithinBoundsDoesNotThrow") — none of them assert on the *value* `GetData`
actually returns. Running the full suite against `cmake-build-vulkan`/`cmake-build-bgfx` reports
"34/34 `TextureCubeTest` pass" (confirmed during Task 279) — which is entirely consistent with
`GetData` being a silent no-op, since no existing test checks the returned pixels. Only EasyGL has a
dedicated pixel-readback integration test (`modules/renderers/easygl/examples/easygl_texturecube_faces_test.cpp`, etc.), which
is why this went unnoticed until this task cross-checked the Vulkan/Bgfx renderer source directly.

Not fixed here — this is a real feature gap (Vulkan needs a staging-buffer-based
`vkCmdCopyImageToBuffer` readback path; Bgfx has no such API in this project by design), tracked as
new `plans/plan_graphics.md` Task 865.

---

## Mip levels (`level` > 0)

| Renderer | `Texture3D` level>0 | `TextureCube` level>0 |
|---------|:---:|:---:|
| EasyGL  | 🔍 same bug class as the confirmed `TextureCube` bug below, not yet fixed — tracked as Task 862 | ✅ fixed and pixel-verified (Task 276): constructor now pre-allocates every mip level per face |
| Vulkan  | 🔍 `imgInfo.mipLevels = 1` hardcoded at image creation, `mipMap` param dropped by the factory | 🔍 same — `imgInfo.mipLevels = 1` hardcoded |
| Bgfx    | 🔍 `bgfx::createTexture3D(..., /*hasMips=*/false, ...)` — `mipMap` param received but ignored | 🔍 same — `bgfx::createTextureCube(..., /*hasMips=*/false, ...)` |
| Metal   | 🔍 the public XNA mip chain is computed from width/height only; depth halves inside those existing levels but never creates extra levels (so mipmapped `1x1x8` has only level 0). The descriptor allocates exactly that count. | 🔍 real allocation via `mipmapped:mipMap`'s full-chain behavior; neither cell has adapted native pixel proof. |

Task 276 confirmed (via a failing-then-fixed test) that this exact bug shape — "constructor never
allocates GPU storage past level 0, so a sub-image write to `level>0` silently goes nowhere" —
is real for `EasyGLTextureCubeRenderer`. Every other cell in this table has the **identical code
shape** (mip level count/flag hardcoded to "1 level only", `mipMap` parameter unused), so the same
failure is highly likely everywhere else, but only the one cell was actually reproduced with a
test and fixed. Tracked as new `plans/plan_graphics.md` Task 862 (EasyGL `Texture3D`, already tracked)
and Task 864 (Vulkan + Bgfx, both types — new).

---

## `CubeMapFace` validation

Fixed this session (Task 279): `TextureCube::SetData`/`GetData` now throw `std::out_of_range` for
an out-of-range `CubeMapFace` value, at the public API layer, on all renderers (the check lives in
`TextureCube.cpp`, above the renderer dispatch). FNA itself never validates this — confirmed via
`TextureCube.cs` — so this is a CNA safety extra. All 3 renderers already guarded against an invalid
face internally too (`if (face < 0 || face >= 6) return;`), so this was already memory-safe before
Task 279; the fix only changes silent-no-op into a clear exception. Applies to Metal automatically
(shared `TextureCube.cpp` code, above any per-renderer dispatch — no Metal-specific work needed).

---

## Sampling in shaders

| Use case | EasyGL | Vulkan | Bgfx | Metal |
|---|:---:|:---:|:---:|:---:|
| `TextureCube` in `EnvironmentMapEffect` | ✅ (pre-existing, reconfirmed) | ✅ (pre-existing, reconfirmed) | ✅ (Task 278 — was a silent no-reflection fallback, now fixed) | 🔍 real, world-space cube-map reflection (flat + Fresnel-weighted blend, lit+fogged), landed and source-complete (`plans/plan_metal.md` Phase 6, `METAL-64`–`71`) — not independently pixel-verified against a known-reflective scene |
| `Texture3D` in any effect, stock or custom | ❌ | ❌ | ❌ | ❌ same structural gap |

`Texture3D` sampling is not implemented on any renderer for a structural reason, not a per-renderer
gap: `Texture3D`/`TextureCube` don't inherit `Texture` in CNA (unlike FNA's `Texture3D : Texture`),
so neither can be placed into `GraphicsDevice.Textures[slot]`, and custom `ShaderEffect` has no
texture-binding API of any kind (Task 277 finding, tracked as Task 863). `TextureCube` sampling
*does* work for `EnvironmentMapEffect` specifically because that stock effect bypasses
`GraphicsDevice.Textures` entirely via a dedicated `GpuDrawParams::envMap` field that every renderer's
draw dispatch consumes directly — but no stock effect needs `Texture3D`, and no custom-effect
workaround exists for it. Metal custom effects are deliberately unsupported after adaptation, so
they provide no alternate binding route.

---

## `DDSFromStreamEXT` (`TextureCube` only — no `Texture3D` equivalent in FNA)

Confirmed non-functional stub on all renderers (it's implemented once, in the shared XNA-layer
`TextureCube.cpp`, not per-renderer): ignores the `stream` argument entirely and always returns a
blank 1×1 `Color` cube map. Task 272 finding, tracked as `plans/plan_graphics.md` Task 663. Applies to
Metal too, same shared code, no per-renderer work possible until Task 663 lands.

---

## Future work

| Area | Task |
|------|------|
| Fix `EasyGLTexture3DRenderer`'s mip-level allocation (same bug as Task 276, `Texture3D` only) | Task 862 |
| Wire `Texture3D`/`TextureCube` sampling into shaders (architecture change — inherit `Texture`, or a parallel binding path) | Task 863 |
| Fix Vulkan's and Bgfx's mip-level allocation for both `Texture3D` and `TextureCube` (`mipLevels`/`hasMips` hardcoded to 1/false) | Task 864 (new) |
| Implement real GPU readback for `Texture3D`/`TextureCube::GetData` on Vulkan (staging-buffer `vkCmdCopyImageToBuffer`); document Bgfx's as an accepted no-readback limitation | Task 865 (new) |
| Implement `TextureCube::DDSFromStreamEXT` for real (DDS header parsing + per-face/per-level DXT decode) | Task 663 |

Metal's own remaining `Texture3D`/`TextureCube` work (real `CTest`s for `SetData`/`GetData` round-trips and cube-face sampling) is tracked directly in `plans/plan_metal.md` Phase 11 (`METAL-126`–`129`), not duplicated here as a separate task list.
