# VertexElementFormat / VertexElementUsage Renderer Support — CNA

> Revised 2026-09-30 (cna-car-simulator CCS-3): the stride-keyed description this page used to give
> was true in Phase 30 and had long stopped being true on EasyGL.
> Covers: EasyGL.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported; correct GPU mapping. |
| ❌ | Unsupported; refused with an exception. |

---

## How vertex layout selection works

**EasyGL** binds a stock effect's inputs from the bound buffers' `VertexDeclaration`s, element by
element, by XNA usage and usage index (SAMPLE-005, `8b4e6ec30`;
`EasyGLRenderer::ConfigureDeclarationForStockProgramEXT`). The program is chosen from the effect's
state, not from the stride, so any declared layout -- a 36-byte Position+Normal+Color+Texture, a
40-byte dual-UV vertex, an instance stream -- reaches the program's inputs. A buffer without a
declaration binds the known layout of its stride; an unknown stride without a declaration is refused
(GLTF-157, `7b9fdec0a`).

A user draw (`DrawUserPrimitives` / `DrawUserIndexedPrimitives`) of anything but the four stock
vertex structures needs its `VertexDeclaration` argument; the call without one does not compile
(CCS-2).

| Stride | Vertex type                   | EasyGL |
|-------:|-------------------------------|:------:|
| 16     | `VertexPositionColor`         | ✅     |
| 20     | `VertexPositionTexture`       | ✅     |
| 24     | `VertexPositionColorTexture`  | ✅     |
| 32     | `VertexPositionNormalTexture` | ✅     |
| 52     | Skinned (custom)              | ✅     |
| Other, with a declaration    | Custom | ✅     |
| Other, without a declaration | Custom | ❌     |

---

## VertexElementFormat — renderer mapping table

Each row shows what GPU type the XNA format maps to in each renderer.

| XNA format            | Byte size | EasyGL                       |
|-----------------------|:---------:|------------------------------|
| `Single`              | 4         | float (1 comp.)              |
| `Vector2`             | 8         | float (2 comp.)              |
| `Vector3`             | 12        | float (3 comp.)              |
| `Vector4`             | 16        | float (4 comp.)              |
| `Color`               | 4         | ubyte (4 comp., normalized)  |
| `Byte4`               | 4         | ubyte (4 comp., not norm.)   |
| `Short2`              | 4         | short (2 comp.)              |
| `Short4`              | 8         | short (4 comp.)              |
| `NormalizedShort2`    | 4         | short (2 comp., normalized)  |
| `NormalizedShort4`    | 8         | short (4 comp., normalized)  |
| `HalfVector2`         | 4         | half (2 comp.)               |
| `HalfVector4`         | 8         | half (4 comp.)               |

**EasyGL**: each element is bound with its declared format.

---

## VertexElementUsage

EasyGL resolves every stock input by usage and usage index, Tangent included (the PBR programs).

---

## Future work

| Area | Task |
|------|------|
| Support `Depth`, `Fog`, `PointSize` usages (XNA legacy semantics) | Low priority |
