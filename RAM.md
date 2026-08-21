# RAM Usage Analysis — mobile-eggbert

## Vulkan: why mobile-eggbert went from ~28 MB to ~78 MB

| Component | Size | Present before? |
|-----------|------|-----------------|
| App base + SDL3 + Vulkan ICD/loader | ~10–15 MB | Yes |
| Khronos validation layer (debug builds) | ~20–30 MB | Yes (may have been excluded from old measurement) |
| Sprite VB/IB ring buffers (HOST\_VISIBLE, permanently mapped) | ~2 MB | Yes |
| **`cpuPixels_` — one CPU copy of all texture pixel data** | **~34 MB** | **No — added by task 32** |
| 3D ring buffers (lazy — only if 3D draw issued) | 0 MB for 2D | Eagerly allocated before; now lazy |

The dominant new cost is `cpuPixels_`: task 32 added `= default` copy constructors to
`Texture2D`, which required every texture to retain a CPU-side RGBA buffer so that
`ContentManager::Load<T>()` could return copies by value. `Texture2D::GetData()` also
requires this buffer.

The Khronos validation layer (~20–30 MB) has always been present in debug builds and
absent in release builds. If the old ~28 MB figure was a **release** measurement and the
new ~78 MB figure is a **debug** measurement, the effective regression is:

| Build | Before task 32 | After task 32 + fixes |
|-------|---------------|-----------------------|
| Debug | ~48 MB | ~78 MB (+30 MB = cpuPixels_) |
| Release | ~28 MB | ~48 MB (+20 MB = cpuPixels_ net of validation layer) |

In short: **~34 MB of the increase is `cpuPixels_`** (one shared copy, required for
the XNA `GetData` API). The rest is the validation layer being included in the
measurement. A release build of mobile-eggbert on Vulkan is expected to use
approximately **~45–50 MB** after all current fixes.

---

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

## Fixes applied

### Fix 1 — ContentManager: `cpuPixels_` shared across copies ✅ **(~34 MB saved, both backends)**

`cpuPixels_` is now `std::shared_ptr<std::vector<uint8_t>>`. Copies of `Texture2D`
(cache entry + game field) share the same underlying vector via `shared_ptr` reference
counting. No pixel duplication on cache return.

### Fix 2 — Vulkan: lazy 3D ring buffer allocation ✅ **(~10 MB saved for 2D games)**

`frame3DVB_` / `frame3DIB_` are now allocated on the first actual 3D draw call via
`EnsureFrame3DBuffers()`. A pure 2D game like mobile-eggbert never pays this cost.

### Fix 3 — EasyGL: remove `image_data_` duplicate in `EasyGLTextureRenderer` ✅ **(~34 MB saved)**

`EasyGLTextureRenderer` (named `EasyGLTextureBackend` at the time of this fix, renamed
since) previously stored a full `ImageData image_data_` copy of all pixel data
separately from `Texture2D::cpuPixels_`. This was the largest remaining RAM
regression after Fix 1:

- Each texture kept **two** independent CPU copies of its pixels:
  one in `cpuPixels_` (shared via `shared_ptr` across `Texture2D` copies) and
  one in `EasyGLTextureRenderer::image_data_` (unique per backend instance).

- This also caused the ~1.5 MB-per-world-entry RAM growth in mobile-eggbert:
  world-specific textures accumulated in the `ContentManager` cache, each costing
  2× pixels instead of 1×.

**Replaced** `image_data_` with `std::shared_ptr<std::vector<uint8_t>> pixels_` that
points back to `Texture2D::cpuPixels_` via `ITextureBackend::ShareCpuPixels()`. This
shared reference is sufficient for OpenGL context-loss restoration while eliminating
the duplicate allocation.

**Also** changed texture-loading constructors (`Texture2D(path, device)`, `FromStream`,
`CreateFromPixels`) to **move** `ImageData.pixels` into `cpuPixels_` instead of
copying, cutting peak RAM during texture loading from 3× to 2× (data in transit +
GPU staging buffer).

### Fix 4 — Validation layer

Already correctly disabled in release builds. No action needed.

### Fix 5 — Reduce sprite ring buffer sizes

`MaxSpriteVertices = 32768` (2 MB × 2) may be larger than needed for mobile-eggbert.
A smaller default (e.g. 8192) would save ~1.5 MB. Low priority, not yet done.

---

## Remaining overhead

| Source | Approx. | Notes |
|--------|---------|-------|
| `cpuPixels_` (all textures) | ~34 MB | Required for `Texture2D::GetData` (XNA API) |
| Mesa driver shadow copies | ~34 MB | Mesa keeps CPU copies for `glGetTexImage`/context recovery; hard to avoid |
| Vulkan ICD + loader | ~5–15 MB | Inherent to the API choice |
| Sprite ring buffers | ~2 MB | Bounded; Fix 5 can trim slightly |
| Validation layer (debug only) | ~20–30 MB | Already guarded by `#ifdef NDEBUG` |

### Remaining per-world RAM growth

`ContentManager` caches all loaded assets permanently (until `Unload()` is called).
If mobile-eggbert loads world-specific textures without calling `Unload()` between
worlds, those textures accumulate in the cache. After Fix 3, each accumulated texture
costs 1× pixels (down from 2×), so the growth rate is halved.

The correct fix at the game level is to call `content.Unload()` when leaving a world
and reload the next world's assets. Alternatively, a future CNA improvement could add
a NOXNA `ContentManager::Unload(const std::string& assetName)` per-asset eviction API.

---

## Summary

| Fix | Saved | Status |
|-----|-------|--------|
| ContentManager `cpuPixels_` sharing | ~34 MB | ✅ Done (commit a5c4274) |
| Vulkan lazy 3D buffers | ~10 MB | ✅ Done (commit a5c4274) |
| EasyGL `image_data_` removal | ~34 MB | ✅ Done (this commit) |
| Mesa driver shadow copies | ~34 MB | ⚠️ Hard to avoid on Mesa |
| Sprite ring buffer sizing | ~1.5 MB | Low priority |
