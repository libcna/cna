# Texture3D / TextureCube Renderer Support — CNA

> Source-inspected against Tasks 271–279 (Phase 33). Covers: EasyGL.

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

EasyGL still silently reduces every format request to 32-bit RGBA.

---

## `SetData` — box / rect sub-region uploads

Verified this session (Tasks 273–275) that CNA's public API layer (`Texture3D::SetData`,
`TextureCube::SetData`) correctly computes arbitrary x/y/z (or x/y, for `TextureCube`) sub-region
offsets and forwards them to the renderer — asymmetric, off-origin, single-voxel, and far-corner
boxes all round-trip correctly through **EasyGL**.

| Renderer | Arbitrary sub-region upload | Verified how |
|---------|:---:|---|
| EasyGL  | ✅ | `glTexSubImage3D`/`glTexSubImage2D` per-face, pixel-verified (Tasks 273–275) |

---

## `GetData` — readback

`GetData` of a `Texture3D` or `TextureCube` depends entirely on the renderer's
`ITexture3DRenderer`/`ITextureCubeRenderer::GetData` implementation: unlike `Texture2D`'s
`cpuPixels_`, no CPU-side copy is read back. (A `Color` `TextureCube` does keep a level-0 copy of
each face, which only WebGPU's device-loss restore uses.)

| Renderer | `GetData` implemented? |
|---------|:---:|
| EasyGL  | ✅ — per-slice (`Texture3D`) or per-face (`TextureCube`) temporary FBO + `glReadPixels`. Pixel-verified round-trip (Tasks 273–275). |

---

## Mip levels (`level` > 0)

| Renderer | `Texture3D` level>0 | `TextureCube` level>0 |
|---------|:---:|:---:|
| EasyGL  | 🔍 same bug class as the confirmed `TextureCube` bug below, not yet fixed — tracked as Task 862 | ✅ fixed and pixel-verified (Task 276): constructor now pre-allocates every mip level per face |

Task 276 confirmed (via a failing-then-fixed test) that this exact bug shape — "constructor never
allocates GPU storage past level 0, so a sub-image write to `level>0` silently goes nowhere" —
is real for `EasyGLTextureCubeRenderer`. The EasyGL `Texture3D` cell has the **identical code
shape**, so the same failure is highly likely there, but only the one cell was actually reproduced
with a test and fixed. Tracked as `plans/plan_graphics.md` Task 862.

---

## `CubeMapFace` validation

Fixed this session (Task 279): `TextureCube::SetData`/`GetData` now throw `std::out_of_range` for
an out-of-range `CubeMapFace` value, at the public API layer, on all renderers (the check lives in
`TextureCube.cpp`, above the renderer dispatch). FNA itself never validates this — confirmed via
`TextureCube.cs` — so this is a CNA safety extra. The renderers already guarded against an invalid
face internally too (`if (face < 0 || face >= 6) return;`), so this was already memory-safe before
Task 279; the fix only changes silent-no-op into a clear exception.

---

## Sampling in shaders

| Use case | EasyGL |
|---|:---:|
| `TextureCube` in `EnvironmentMapEffect` | ✅ (pre-existing, reconfirmed) |
| `Texture3D` in any effect, stock or custom | ❌ |

`Texture3D` sampling is not implemented on any renderer for a structural reason, not a per-renderer
gap: `Texture3D`/`TextureCube` don't inherit `Texture` in CNA (unlike FNA's `Texture3D : Texture`),
so neither can be placed into `GraphicsDevice.Textures[slot]`, and custom `ShaderEffect` has no
texture-binding API of any kind (Task 277 finding, tracked as Task 863). `TextureCube` sampling
*does* work for `EnvironmentMapEffect` specifically because that stock effect bypasses
`GraphicsDevice.Textures` entirely via a dedicated `GpuDrawParams::envMap` field that every renderer's
draw dispatch consumes directly — but no stock effect needs `Texture3D`, and no custom-effect
workaround exists for it.

---

## `DDSFromStreamEXT` (`TextureCube` only — no `Texture3D` equivalent in FNA)

**Implemented.** This section described a non-functional 1×1 stub long after Task 663 replaced it;
the stale text is corrected here rather than deleted, because a reader who remembers the stub needs
to see that it went.

It is implemented once, in the shared XNA-layer `TextureCube.cpp`, not per-renderer. It parses the
DDS header mirroring FNA's `Texture.ParseDDS`, refuses a non-cube file with
`System::FormatException("This file does not contain cube data!")` and malformed or out-of-scope
input with `System::NotSupportedException`, and decodes every face and mip level of a
DXT1/DXT3/DXT5 or byte-aligned 24-bit RGB888/BGR888 cube map to RGBA8. The 24-bit channel masks
select the byte order and alpha is filled with 255. DX10 extended headers, other packed layouts,
and HDR variants remain unsupported.

CNA converts to RGBA8 on the CPU and uploads `SurfaceFormat::Color`, where FNA uploads the
compressed blocks to a real compressed GPU format. That is a deliberate deviation matching
`Texture2D::DDSFromStreamEXT`'s stream-decoding contract. Some CNA renderers now accept explicit native
compressed-texture transfers, but this portable stream route still normalizes its supported source
encodings before upload.

Since `plans/plan_cnb.md` `CNBF-113`, the parsing and decompression live in
`CNA::Internal::Graphics::DecodeDdsCube` — a pure-CPU component needing no `GraphicsDevice` — and
`DDSFromStreamEXT` is a thin consumer that decodes and uploads. The same decoder is what lets the
headless CNB content compiler produce a `TextureCube.cnb`. Behaviour is unchanged in both
directions; only the location moved.

---

## Future work

(`TextureCube::DDSFromStreamEXT` used to be listed here as Task 663. It is implemented; see
the section above.)

| Area | Task |
|------|------|
| Fix `EasyGLTexture3DRenderer`'s mip-level allocation (same bug as Task 276, `Texture3D` only) | Task 862 |
| Wire `Texture3D`/`TextureCube` sampling into shaders (architecture change — inherit `Texture`, or a parallel binding path) | Task 863 |

