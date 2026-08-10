# VertexElementFormat / VertexElementUsage Renderer Support — CNA

> Source-inspected against Tasks 248–250 (Phase 30).
> Covers: EasyGL, Vulkan, Bgfx, SDL_Renderer renderers.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported; correct GPU mapping. |
| ⚠️ | Partially supported; works with caveats documented below. |
| ❌ | Unsupported; throws, silently ignored, or falls back to a wrong type. |
| — | Not applicable (renderer has no 3D vertex pipeline). |

---

## How vertex layout selection works (current state)

All four renderers select their GPU vertex attribute layout from the **byte stride** of the
bound `VertexBuffer`, not from the `VertexDeclaration` elements.  The `VertexDeclaration`
is stored in the XNA layer and used for stride auto-computation and API conformance, but
it is not forwarded to `IGraphicsRenderer::DrawPrimitivesEx`.

The practical consequence is that only the **five hardcoded strides** described below are
rendered correctly.  Any other stride triggers a fallback that may produce incorrect shading
or a crash-free but visually wrong draw.

---

## Supported vertex strides per renderer

| Stride | Vertex type                   | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|-------:|-------------------------------|:------:|:------:|:----:|:------------:|
| 16     | `VertexPositionColor`         | ✅     | ✅     | ✅   | —            |
| 20     | `VertexPositionTexture`       | ✅     | ✅     | ⚠️   | —            |
| 24     | `VertexPositionColorTexture`  | ✅     | ✅     | ⚠️   | —            |
| 32     | `VertexPositionNormalTexture` | ✅     | ✅     | ⚠️   | —            |
| 52     | Skinned (custom)              | ✅     | ✅     | ✅   | —            |
| Other  | Custom                        | ⚠️     | ❌     | ⚠️   | —            |

**EasyGL other-stride fallback**: position-only (float3 at offset 0), warning logged via
`CNA_RENDER_LOG`.  No crash, but color/UV attributes are missing.

**Vulkan other-stride fallback**: no pipeline is compiled for unknown strides; the draw call
is silently skipped (guard in `DrawPrimitivesEx`).

**Bgfx stride 20/24/32 caveat**: `MakeBgfxLayout` falls back to
`Position(float3) + Color0(ubyte4) + skip(stride-16)` — the position attribute is correct
but UV/normal attributes are mapped as padding bytes, so the draw appears with wrong colors
or no shading if the shader reads from those attributes.

---

## VertexElementFormat — renderer mapping table

Each row shows what GPU type the XNA format maps to in each renderer.

| XNA format            | Byte size | EasyGL                       | Vulkan                      | Bgfx                              | SDL_Renderer |
|-----------------------|:---------:|------------------------------|-----------------------------|-----------------------------------|:------------:|
| `Single`              | 4         | float (1 comp.)              | `VK_FORMAT_R32_SFLOAT`      | `Float×1`                         | —            |
| `Vector2`             | 8         | float (2 comp.)              | `VK_FORMAT_R32G32_SFLOAT`   | `Float×2`                         | —            |
| `Vector3`             | 12        | float (3 comp.)              | `VK_FORMAT_R32G32B32_SFLOAT`| `Float×3`                         | —            |
| `Vector4`             | 16        | float (4 comp.)              | `VK_FORMAT_R32G32B32A32_SFLOAT` | `Float×4`                     | —            |
| `Color`               | 4         | ubyte (4 comp., normalized)  | `VK_FORMAT_R8G8B8A8_UNORM`  | `Uint8×4, normalized`             | —            |
| `Byte4`               | 4         | ubyte (4 comp., not norm.)   | `VK_FORMAT_R8G8B8A8_UINT`   | `Uint8×4, asInt`                  | —            |
| `Short2`              | 4         | short (2 comp.)              | `VK_FORMAT_R16G16_SINT`     | `Int16×2, asInt`                  | —            |
| `Short4`              | 8         | short (4 comp.)              | `VK_FORMAT_R16G16B16A16_SINT` | `Int16×4, asInt`                | —            |
| `NormalizedShort2`    | 4         | short (2 comp., normalized)  | `VK_FORMAT_R16G16_SNORM`    | `Int16×2, normalized`             | —            |
| `NormalizedShort4`    | 8         | short (4 comp., normalized)  | `VK_FORMAT_R16G16B16A16_SNORM` | `Int16×4, normalized`          | —            |
| `HalfVector2`         | 4         | half (2 comp.)               | `VK_FORMAT_R16G16_SFLOAT`   | `Half×2`                          | —            |
| `HalfVector4`         | 8         | half (4 comp.)               | `VK_FORMAT_R16G16B16A16_SFLOAT` | `Half×4`                      | —            |

**EasyGL caveat**: the `ApplyLayout` function selects attribute types by stride, not by the
declared `VertexElementFormat`.  As long as the stride matches one of the five hardcoded
cases, the correct GL type is used for that slot.  Individual format values within a stride
are not inspected — a custom layout using `Short4` at offset 0 with stride 32 would still
receive the `float3 position` binding.

**Vulkan caveat**: `VulkanVertexFormatHelper::VertexElementFormatToVk()` provides a correct
per-format `VkFormat`, but the active pipeline is selected by stride.  Shader attribute
locations are hardcoded per-stride, so the format mapping table is only an audit reference
until per-declaration pipeline compilation is implemented.

**Bgfx caveat**: `BgfxVertexFormatHelper::VertexElementFormatToBgfx()` provides correct
bgfx attrib parameters for all 12 formats, but `MakeBgfxLayout()` in
`BgfxRenderer.cpp` still uses the stride-keyed fallback rather than the helper.
Migrating `MakeBgfxLayout` to use the `VertexDeclaration` elements is a future task.

---

## VertexElementUsage — renderer mapping table

| XNA usage             | EasyGL                  | Vulkan                  | Bgfx                    | SDL_Renderer |
|-----------------------|-------------------------|-------------------------|-------------------------|:------------:|
| `Position`            | location 0              | location 0              | `bgfx::Attrib::Position`| —            |
| `Color` (index 0–3)  | location 1 (index 0)    | location 1 (index 0)    | `Color0`–`Color3`       | —            |
| `TextureCoordinate` (0–7) | location 1 or 2    | location 1              | `TexCoord0`–`TexCoord7` | —            |
| `Normal`              | location 1              | location 1              | `bgfx::Attrib::Normal`  | —            |
| `Tangent`             | ❌ (no shader slot)     | ❌ (no shader slot)     | `bgfx::Attrib::Tangent` | —            |
| `Binormal`            | ❌ (no shader slot)     | ❌ (no shader slot)     | `bgfx::Attrib::Bitangent`| —           |
| `BlendIndices`        | location 4 (stride 52)  | location 4 (stride 52)  | `bgfx::Attrib::Indices` | —            |
| `BlendWeight`         | location 3 (stride 52)  | location 3 (stride 52)  | `bgfx::Attrib::Weight`  | —            |
| `Depth`               | ❌                      | ❌                      | ❌ (`Count` sentinel)   | —            |
| `Fog`                 | ❌                      | ❌                      | ❌ (`Count` sentinel)   | —            |
| `PointSize`           | ❌                      | ❌                      | ❌ (`Count` sentinel)   | —            |
| `Sample`              | ❌                      | ❌                      | ❌ (`Count` sentinel)   | —            |
| `TessellateFactor`    | ❌                      | ❌                      | ❌ (`Count` sentinel)   | —            |

EasyGL and Vulkan resolve the usage slot from the **stride-keyed hardcoded layout**, not
from the `VertexElementUsage` enum.  Bgfx exposes a proper per-usage mapping via
`VertexElementUsageToBgfxAttrib()`, but the fallback in `MakeBgfxLayout` does not yet call
it.

---

## SDL_Renderer renderer

SDL_Renderer has no 3D vertex pipeline.  `CreateVertexBuffer()` throws immediately
(`ThrowNo3D`).  All vertex-related APIs (`DrawPrimitives`, `DrawIndexedPrimitives`, etc.)
are unsupported.  SpriteBatch 2D rendering via `SDL_RenderTexture` is the only supported
draw path.

---

## Future work

| Area | Task |
|------|------|
| Derive vertex layout from `VertexDeclaration` in EasyGL | Phase 34+ |
| Derive vertex layout from `VertexDeclaration` in Vulkan (per-declaration pipeline) | Phase 34+ |
| Wire `BgfxVertexFormatHelper` into `MakeBgfxLayout` | Phase 34+ |
| Support `Tangent` / `Binormal` attributes in EasyGL / Vulkan shaders | Phase 34+ |
| Support `Depth`, `Fog`, `PointSize` usages (XNA legacy semantics) | Low priority |
