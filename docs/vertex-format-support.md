# VertexElementFormat / VertexElementUsage Renderer Support — CNA

> Revised 2026-09-30 (cna-car-simulator CCS-3): the stride-keyed description this page used to give
> was true in Phase 30 and had long stopped being true on EasyGL.
> Covers: EasyGL, Vulkan, SDL_Renderer renderers.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported; correct GPU mapping. |
| ⚠️ | Partially supported; works with caveats documented below. |
| ❌ | Unsupported; refused with an exception. |
| — | Not applicable (renderer has no 3D vertex pipeline). |

---

## How vertex layout selection works

**EasyGL** binds a stock effect's inputs from the bound buffers' `VertexDeclaration`s, element by
element, by XNA usage and usage index (SAMPLE-005, `8b4e6ec30`;
`EasyGLRenderer::ConfigureDeclarationForStockProgramEXT`). The program is chosen from the effect's
state, not from the stride, so any declared layout -- a 36-byte Position+Normal+Color+Texture, a
40-byte dual-UV vertex, an instance stream -- reaches the program's inputs. A buffer without a
declaration binds the known layout of its stride; an unknown stride without a declaration is refused
(GLTF-157, `7b9fdec0a`).

**Vulkan** still builds its native pipeline layout from the stride, but a declared layout is checked
against it (`RequireFaithfulVertexDeclaration`) and a buffer with neither a declaration nor a known
stride is refused (VULKAN-165), so a layout it cannot honour fails by name instead of drawing the
wrong bytes.

A user draw (`DrawUserPrimitives` / `DrawUserIndexedPrimitives`) of anything but the four stock
vertex structures needs its `VertexDeclaration` argument; the call without one does not compile
(CCS-2).

| Stride | Vertex type                   | EasyGL | Vulkan | SDL_Renderer |
|-------:|-------------------------------|:------:|:------:|:------------:|
| 16     | `VertexPositionColor`         | ✅     | ✅     | —            |
| 20     | `VertexPositionTexture`       | ✅     | ✅     | —            |
| 24     | `VertexPositionColorTexture`  | ✅     | ✅     | —            |
| 32     | `VertexPositionNormalTexture` | ✅     | ✅     | —            |
| 52     | Skinned (custom)              | ✅     | ✅     | —            |
| Other, with a declaration    | Custom | ✅     | ⚠️ faithful layouts only | — |
| Other, without a declaration | Custom | ❌     | ❌     | —            |

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

**EasyGL**: each element is bound with its declared format.

**Vulkan caveat**: `VulkanVertexFormatHelper::VertexElementFormatToVk()` provides a correct
per-format `VkFormat`, but the active pipeline is selected by stride and a declaration it cannot
honour is refused.

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

The EasyGL column is Phase 30's and is kept for Vulkan comparison only: EasyGL now resolves every
stock input by usage and usage index, Tangent included (the PBR programs). Vulkan resolves the slot
from its stride-keyed layout.

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
| Derive vertex layout from `VertexDeclaration` in Vulkan (per-declaration pipeline) | open |
| Support `Depth`, `Fog`, `PointSize` usages (XNA legacy semantics) | Low priority |
