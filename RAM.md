# RAM Usage Analysis — mobile-eggbert

## Observed figures

| Backend | RAM usage | Condition |
|---------|-----------|-----------|
| Vulkan  | ~24 MB    | Before SpriteBatch/viewport fixes — app crashed with SIGSEGV during the first `Draw`, so `LoadContent` never completed; only `wait.png` was uploaded |
| Vulkan  | ~100 MB   | After fixes — app runs correctly and completes `LoadContent` |
| EasyGL  | ~48 MB    | Earlier measurement — app ran correctly but RAM was much lower |
| EasyGL  | ~158 MB   | Current measurement — **larger than Vulkan despite lighter API** (see below) |

Both backends show the same pattern: a past low-water mark followed by a large increase.
The Vulkan jump (24 MB → 100 MB) is partly explained by the app previously crashing before
`LoadContent` completed, so only one texture was ever uploaded. The EasyGL jump (48 MB →
158 MB), however, happened while the app was already running correctly — meaning some
change in the XNA/content layer (not the backend itself) caused the regression in both
backends simultaneously. The most likely culprit is the addition of `= default` copy
constructors to `Texture2D` (task 32), which made `ContentManager::Load<T>()` start
returning deep copies of `cpuPixels_` that were previously either impossible or avoided.

---

## Why Vulkan uses ~100 MB

### 1. ContentManager double-copy of CPU texture data — ~68 MB

This is the **single largest contributor** and it affects both backends equally.

`ContentManager::Load<T>()` stores the value in `cache_` as `std::any` and then returns
`std::any_cast<T>(cache_[key])` — a **copy by value**. For `Texture2D` that copy includes
`cpuPixels_` (a `std::vector<uint8_t>` holding the full RGBA raster).

So every texture occupies CPU RAM twice:
- once inside `ContentManager::cache_` (the cached original)
- once in the `Pixmap` member field (`bitmapBlupi`, `bitmapElement`, etc.)

The two copies share the same GPU backend via `shared_ptr<ITextureBackend>`, but
`cpuPixels_` is a plain `std::vector` and is **duplicated in full**.

Textures loaded at startup (`RESOLUTION_SCALE = 1`, i.e. `icons/` + `backgrounds/`):

| Texture | Size | Single copy | Double copy |
|---------|------|-------------|-------------|
| blupi.png | 600×2040 | 4.67 MB | **9.34 MB** |
| blupi1.png | 600×2040 | 4.67 MB | **9.34 MB** |
| explo.png | 1440×1440 | 7.91 MB | **15.82 MB** |
| object-m.png | 1301×1431 | 7.10 MB | **14.20 MB** |
| element.png | 600×1740 | 3.98 MB | **7.97 MB** |
| pad.png | 1120×420 | 1.79 MB | 3.59 MB |
| button.png | 240×1040 | 0.95 MB | 1.90 MB |
| wait.png | 640×480 | 1.17 MB | 2.34 MB |
| text.png | 512×256 | 0.50 MB | 1.00 MB |
| speedyblupi.png | 640×160 | 0.39 MB | 0.78 MB |
| blupiyoupie.png | 410×380 | 0.59 MB | 1.19 MB |
| gear.png | 226×226 | 0.19 MB | 0.39 MB |
| jauge.png | 124×88 | 0.04 MB | 0.08 MB |
| **Total** | | **~34 MB** | **~68 MB** |

### 2. Khronos Validation Layer (debug builds only) — ~20–30 MB

`sEnableValidation = true` in debug builds loads `VK_LAYER_KHRONOS_validation`
(`libVkLayer_khronos_validation.so`). This layer hooks every Vulkan API call, maintains
full object tracking tables, and is a large library. It is disabled in release builds
(`#ifdef NDEBUG`) so this overhead does not affect shipped binaries.

### 3. Pre-allocated HOST_VISIBLE ring buffers — ~12 MB

All six ring buffers are allocated at backend startup with
`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`
and permanently `vkMapMemory`-mapped. They therefore count as **resident process RAM**,
unlike OpenGL VBOs which live server-side and are not visible in the process RSS.

| Buffer | Per-frame | × 2 frames |
|--------|-----------|-----------|
| Sprite VB (32768 vertices × 32 B) | 1 MB | 2 MB |
| Sprite IB (49152 × 2 B) | 96 KB | 192 KB |
| 3D VB (`kFrame3DVBSize = 4 MB`) | 4 MB | **8 MB** |
| 3D IB (`kFrame3DIBSize = 1 MB`) | 1 MB | **2 MB** |
| **Total** | | **~12.2 MB** |

Of this, the **3D ring buffers (10 MB)** are allocated unconditionally at startup even
for pure 2D games like mobile-eggbert that never issue a single 3D draw call.

### 4. Vulkan ICD + loader overhead — ~5–15 MB

Loading `libvulkan.so` + the platform ICD (`libvulkan_radeon.so`, `libvulkan_intel.so`,
etc.) has a larger resident footprint than EGL/OpenGL in the same process.

---

## Why EasyGL uses even more (~158 MB)

The EasyGL backend has not been investigated in depth. EasyGL also has the ContentManager
double-copy problem, but it somehow uses **more** RAM than the heavier Vulkan backend.
Possible additional factors:

- OpenGL drivers (Mesa RadeonSI, Intel ANV, Nouveau) commonly keep a **CPU shadow copy**
  of all uploaded texture data to support `glGetTexImage`, state save/restore, and context
  loss recovery. Combined with the ContentManager duplicate, this means **three** CPU
  copies of the raster per texture (~102 MB for 13 textures) rather than two — matching
  the measured 158 MB when base runtime overhead is included.
- `pending_vertices_` in the EasyGL sprite batch is a `std::vector` that may grow and
  never shrink across a scene with many different textures.
- SDL/EGL surface and window management overhead.

The EasyGL RAM problem is at least as serious as Vulkan and shares the same root cause
(ContentManager copy-by-value), with additional driver-level overhead on top.

---

## Can these be fixed? Yes — ranked by impact

### Fix 1 — ContentManager: avoid copying `cpuPixels_` on cache return **(~34 MB saved, both backends)**

The cache should return a value that shares the GPU backend without duplicating CPU pixels.
Options:
- Change `cache_` to store `std::shared_ptr<Texture2D>` and return by shared_ptr, or
- Move the `Texture2D` out of the cache on first access and keep only the GPU-side
  `shared_ptr<ITextureBackend>` in the cache for subsequent loads, or
- Make `cpuPixels_` inside `Texture2D` lazy/optional and drop it after GPU upload if
  `GetData` is not required.

XNA 4.0 requires `Texture2D::GetData` to work, so the CPU copy cannot be unconditionally
dropped, but it can at least stop being duplicated.

### Fix 2 — Vulkan: lazy 3D ring buffer allocation **(~10 MB saved for 2D games)**

Allocate `frame3DVB_` / `frame3DIB_` on the first actual 3D draw call rather than at
backend startup. A pure 2D game like mobile-eggbert would never pay this cost.

### Fix 3 — OpenGL: investigate driver shadow copies

If Mesa keeps CPU-side texture shadow copies, it may be possible to hint the driver
(e.g. `GL_ARB_pixel_buffer_object` upload path, or driver-specific hints) to drop them.
Alternatively, the same `cpuPixels_` consolidation from Fix 1 reduces the impact since
less data is uploaded to begin with.

### Fix 4 — Validation layer

Already correctly disabled in release builds. No action needed.

### Fix 5 — Reduce sprite ring buffer sizes

`MaxSpriteVertices = 32768` (2 MB × 2) may be larger than needed for mobile-eggbert.
A smaller default (e.g. 8192) would save ~1.5 MB. Low priority.

---

## Summary

The dominant, fixable issue is **Fix 1** (ContentManager double-copy): it wastes ~34 MB
on both backends with a relatively straightforward architectural change. Fix 2 (lazy 3D
buffers) provides an additional 10 MB saving on Vulkan at no cost for 2D games.
The remaining overhead (driver libraries, validation in debug) is mostly inherent to the
choice of API and build configuration.
