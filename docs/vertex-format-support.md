# VertexElementFormat / VertexElementUsage Renderer Support — CNA

> Source-inspected against Tasks 248–250 (Phase 30).
> Covers: EasyGL, Vulkan, SDL_Renderer renderers.

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

All three renderers select their GPU vertex attribute layout from the **byte stride** of the
bound `VertexBuffer`, not from the `VertexDeclaration` elements.  The `VertexDeclaration`
is stored in the XNA layer and used for stride auto-computation and API conformance, but
it is not forwarded to `IGraphicsRenderer::DrawPrimitivesEx`.

The practical consequence is that only the **five hardcoded strides** described below are
rendered correctly.  Any other stride triggers a fallback that may produce incorrect shading
or a crash-free but visually wrong draw.

---

## Supported vertex strides per renderer

| Stride | Vertex type                   | EasyGL | Vulkan | SDL_Renderer |
|-------:|-------------------------------|:------:|:------:|:------------:|
| 16     | `VertexPositionColor`         | ✅     | ✅     | —            |
| 20     | `VertexPositionTexture`       | ✅     | ✅     | —            |
| 24     | `VertexPositionColorTexture`  | ✅     | ✅     | —            |
| 32     | `VertexPositionNormalTexture` | ✅     | ✅     | —            |
| 52     | Skinned (custom)              | ✅     | ✅     | —            |
| Other  | Custom                        | ⚠️     | ❌     | —            |

**EasyGL other-stride fallback**: position-only (float3 at offset 0), warning logged via
`CNA_RENDER_LOG`.  No crash, but color/UV attributes are missing.

**Vulkan other-stride fallback**: no pipeline is compiled for unknown strides; the draw call
is silently skipped (guard in `DrawPrimitivesEx`).

---

## VertexElementFormat — renderer mapping table

Each row shows what GPU type the XNA format maps to in each renderer.

| XNA format            | Byte size | EasyGL                       | Vulkan                      | SDL_Renderer |
|-----------------------|:---------:|------------------------------|-----------------------------|:------------:|
| `Single`              | 4         | float (1 comp.)              | `VK_FORMAT_R32_SFLOAT`      | —            |
| `Vector2`             | 8         | float (2 comp.)              | `VK_FORMAT_R32G32_SFLOAT`   | —            |
| `Vector3`             | 12        | float (3 comp.)              | `VK_FORMAT_R32G32B32_SFLOAT`| —            |
| `Vector4`             | 16        | float (4 comp.)              | `VK_FORMAT_R32G32B32A32_SFLOAT` | —        |
| `Color`               | 4         | ubyte (4 comp., normalized)  | `VK_FORMAT_R8G8B8A8_UNORM`  | —            |
| `Byte4`               | 4         | ubyte (4 comp., not norm.)   | `VK_FORMAT_R8G8B8A8_UINT`   | —            |
| `Short2`              | 4         | short (2 comp.)              | `VK_FORMAT_R16G16_SINT`     | —            |
| `Short4`              | 8         | short (4 comp.)              | `VK_FORMAT_R16G16B16A16_SINT` | —          |
| `NormalizedShort2`    | 4         | short (2 comp., normalized)  | `VK_FORMAT_R16G16_SNORM`    | —            |
| `NormalizedShort4`    | 8         | short (4 comp., normalized)  | `VK_FORMAT_R16G16B16A16_SNORM` | —         |
| `HalfVector2`         | 4         | half (2 comp.)               | `VK_FORMAT_R16G16_SFLOAT`   | —            |
| `HalfVector4`         | 8         | half (4 comp.)               | `VK_FORMAT_R16G16B16A16_SFLOAT` | —        |

**EasyGL caveat**: the `ApplyLayout` function selects attribute types by stride, not by the
declared `VertexElementFormat`.  As long as the stride matches one of the five hardcoded
cases, the correct GL type is used for that slot.  Individual format values within a stride
are not inspected — a custom layout using `Short4` at offset 0 with stride 32 would still
receive the `float3 position` binding.

**Vulkan caveat**: `VulkanVertexFormatHelper::VertexElementFormatToVk()` provides a correct
per-format `VkFormat`, but the active pipeline is selected by stride.  Shader attribute
locations are hardcoded per-stride, so the format mapping table is only an audit reference
until per-declaration pipeline compilation is implemented.

---

## VertexElementUsage — renderer mapping table

| XNA usage             | EasyGL                  | Vulkan                  | SDL_Renderer |
|-----------------------|-------------------------|-------------------------|:------------:|
| `Position`            | location 0              | location 0              | —            |
| `Color` (index 0–3)  | location 1 (index 0)    | location 1 (index 0)    | —            |
| `TextureCoordinate` (0–7) | location 1 or 2    | location 1              | —            |
| `Normal`              | location 1              | location 1              | —            |
| `Tangent`             | ❌ (no shader slot)     | ❌ (no shader slot)     | —            |
| `Binormal`            | ❌ (no shader slot)     | ❌ (no shader slot)     | —            |
| `BlendIndices`        | location 4 (stride 52)  | location 4 (stride 52)  | —            |
| `BlendWeight`         | location 3 (stride 52)  | location 3 (stride 52)  | —            |
| `Depth`               | ❌                      | ❌                      | —            |
| `Fog`                 | ❌                      | ❌                      | —            |
| `PointSize`           | ❌                      | ❌                      | —            |
| `Sample`              | ❌                      | ❌                      | —            |
| `TessellateFactor`    | ❌                      | ❌                      | —            |

EasyGL and Vulkan resolve the usage slot from the **stride-keyed hardcoded layout**, not
from the `VertexElementUsage` enum.

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
| Support `Tangent` / `Binormal` attributes in EasyGL / Vulkan shaders | Phase 34+ |
| Support `Depth`, `Fog`, `PointSize` usages (XNA legacy semantics) | Low priority |
