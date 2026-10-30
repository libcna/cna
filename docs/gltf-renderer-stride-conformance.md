# glTF renderer stride conformance

Status: verified 2026-08-14 (`GLTF-158`…`GLTF-160`, `GLTF-168`, `GLTF-388`, `GLTF-389`).

This matrix records the complete boundary between CNA's glTF importer and the renderers in
the glTF correctness gate. The source of truth is
`CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`; the importer now constructs a real
`VertexDeclaration` from that table before the first upload. A byte stride is no longer the only
description passed by the glTF path.

## Canonical layouts emitted by glTF

Every usage index is zero except the second `TextureCoordinate`, which is usage index 1. `Color` is
four normalized unsigned bytes and `Byte4` is four unsigned integer bytes; every other format below
is IEEE-754 `float`. The importer selects a row through `VertexLayoutTableEXT` (`GLTF-099`).

| Stride | Declaration order: semantic @ byte offset (`VertexElementFormat`) | Corpus witness |
|---:|---|---|
| 24 | Position @ 0 (`Vector3`), Color @ 12 (`Color`), TextureCoordinate @ 16 (`Vector2`) | `mat-unlit-vertex-color-alpha` |
| 28 | Position @ 0 (`Vector3`), TextureCoordinate @ 12 (`Vector2`), TextureCoordinate1 @ 20 (`Vector2`) | `tex-dual-texture-stride` |
| 32 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), TextureCoordinate @ 24 (`Vector2`) | `mat-unlit` |
| 48 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), Tangent @ 24 (`Vector4`), TextureCoordinate @ 40 (`Vector2`) | `mat-authored-tangent` |
| 52 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), TextureCoordinate @ 24 (`Vector2`), BlendWeight @ 32 (`Vector4`), BlendIndices @ 48 (`Byte4`) | `skin-unlit` |
| 56 | The stride-52 declaration plus Color @ 52 (`Color`) | `skin-vertex-color` |
| 60 | The stride-48 declaration plus TextureCoordinate1 @ 48 (`Vector2`) and Color @ 56 (`Color`) | `uv1-material`, `normalized-u8-color`, `mat-vertex-color-pbr` |
| 68 | Position @ 0 (`Vector3`), Normal @ 12 (`Vector3`), Tangent @ 24 (`Vector4`), TextureCoordinate @ 40 (`Vector2`), BlendWeight @ 48 (`Vector4`), BlendIndices @ 64 (`Byte4`) | `skin-parented-joints` |
| 76 | The stride-68 declaration plus TextureCoordinate1 @ 68 (`Vector2`) | — |
| 80 | The stride-76 declaration plus Color @ 76 (`Color`) | `skin-vertex-color-pbr` |

Strides 16 (`Position` + `Color`) and 20 (`Position` + `TextureCoordinate`) are also in the shared
built-in table, but the current glTF packing policy emits neither. The renderer matrix and the
first two tests below cover the seven strides that the witness test loads (28, 32, 48, 52, 56, 60
and 68); strides 24, 76 and 80 have no witness there.

## Renderer matrix

“Draw boundary” means `Model::Draw` followed by `GraphicsDevice::Present`, so deferred
commands are submitted rather than merely queued. “Native binding” means every declared element
is translated to an API input attribute. It is not meaningful for the two non-rasterizing
renderers, and is marked N/A rather than advertised as rendering support.

| Renderer | All seven uploads and public declarations | All seven reach draw boundary | Every element bound natively | Implementation/audit result |
|---|---|---|---|---|
| `STUB` | ✅ | N/A — `ThreeD=false`, explicit test skip | N/A | The common `VertexBuffer` retains and validates the complete declaration and exact upload bytes; STUB intentionally has no 3D pipeline. |
| `HEADLESS` | ✅ | ✅ validation draw | N/A | Upload/count/index validation and the stock-effect draw route accept all seven. Optional PBR/skinned base textures follow the native renderers' neutral-white contract. No pixels or native attributes exist. |
| `OPENGLES3` | ✅ | ✅ Mesa OpenGL ES 3.2 | ✅ | `EasyGLVertexBufferRenderer::ApplyLayout` takes the non-empty declaration path, binds one attribute per declaration element, maps `Color` to normalized `UNSIGNED_BYTE` and `Byte4` to the integer attribute path, then `RequireDeclarationFitsStockProgramEXT` checks the ordered semantic/format list against the selected shader. The old magic-stride switch remains an empty-declaration compatibility path and is no longer used by glTF. |

EasyGL consumes the declaration directly. STUB and HEADLESS keep honest non-rendering capability boundaries.

## Automated evidence

- `GltfStrideAndBuffer.EveryImportedGltfStrideCarriesItsCanonicalVertexDeclaration` (the witness
  test) loads eight corpus witnesses of those seven strides and compares stride, element count,
  ordered offsets, formats, semantics and usage indices with the canonical table.
- `RendererStrideConformance.EveryGltfStrideReachesTheNativeDrawBoundary` draws and presents all
  eight on every renderer with `ThreeD`; STUB's capability skip is deliberate and checked in CI.
  Since `GLTF-473` its contract is **reach the draw boundary, or refuse with a named layout
  incompatibility** rather than a plain `EXPECT_NO_THROW` — which is stricter, because a
  fixed-function renderer's failure mode is not throwing.
- `RendererStrideConformance.AColourCarryingPbrPrimitiveEitherDrawsOrRefusesByName` (`GLTF-472`)
  draws both colour-carrying PBR records (strides 60 and 80) through a **live device**, and
  `RendererStrideConformance.EveryPbrAndSkinnedFixtureImportsToItsCanonicalStride` pins the stride
  every PBR and skinned fixture imports to. These are standing renderer-conformance gates, not
  artefacts of the glTF campaign that found the bugs, and the reason to keep them is empirical:
  between them they found three defects (`SDLGPU`'s and `DILIGENT`'s (retired 2026-09-17)
  unreachable `COLOR_0` shaders and `OPENGLES1`'s (retired 2026-09-17) misread PBR records) that
  **every static inventory in this repository reported as correct**, and the search they prompted
  found two more (`GLTF-475`). A source audit can only see what a renderer declares; only a draw
  sees what it reads.
- `CrossRendererContractTest.NoRendererPaintsAVertexAttributeInsteadOfTheEffectsDiffuseColour`
  (`GLTF-475`) is the same tier applied one level lower: not "does the draw arrive?" but "is the
  answer the one the caller asked for?". It draws a plain XNA `VertexPositionNormalTangentTexture`
  buffer (stride 48) with a `BasicEffect` whose `DiffuseColor` is opaque red, then reads the centre
  pixel back on every renderer in the build. A renderer may render it -- the answer must be exactly
  that red -- or refuse it by name; a third answer is a defect. It found two, in two renderers,
  giving two different wrong colours: `OPENGL4` painted the record's `NORMAL` and `DILIGENT` drew
  black (both renderers have since been retired), where the plan row that opened the investigation had predicted one renderer. It needs a
  multi-renderer build (`CNA_MULTI_RENDERER`) and colour readback, so it is not in the CI matrix
  below; it runs in `cmake-build-multi`.
- `.github/workflows/gltf-renderer-stride-ci.yml` builds and runs those tests for `STUB`,
  `HEADLESS`, `OPENGLES3`, `SDLGPU` and `SOFTWARE` on every relevant push and pull request.
- `scripts/gltf-renderer-parity.sh` compares each renderer against the same committed L1–L5
  goldens. The 2026-08-14 run was byte-identical across renderers for all 42 selected L1–L5 tests;
  HEADLESS and OPENGLES3 also had identical outcomes for all 507 tests in `*Gltf*`.

This is an input-layout and submit oracle, not an image oracle. Corpus-wide pixel tolerances and
reference PNGs remain the separate L7 work tracked by `GLTF-009` and `GLTF-390`.
