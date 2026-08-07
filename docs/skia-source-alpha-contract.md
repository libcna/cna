# Skia source-alpha contract

Status: implemented guardrail for SKIA-119

XNA `BlendState` contains factors and functions, not a flag saying whether a caller's texture
bytes are straight or already premultiplied. Skia, however, evaluates image shaders and blenders
in premultiplied working colour. The backend therefore declares the conversion for every accepted
route; it never infers one from the numeric relationship between RGB and alpha.

## Storage-to-working-colour table

| Source | Owned/public storage | Requested route | Bytes received by the Skia blender |
|---|---|---|---|
| `Texture2D` | Canonical top-row-first RGBA bytes; `GetData` is exact. | Premultiplied | Preserve RGBA components. Invalid-premultiplied values such as RGB greater than alpha are intentionally not “fixed.” |
| `Texture2D` | Same canonical bytes. | Straight | Multiply RGB by source alpha exactly once; preserve alpha. |
| `RenderTarget2D` | Premultiplied `SkSurface`; public readback unpremultiplies. | Either | Reuse the one premultiplied snapshot. Relabelling it as straight would multiply alpha twice. |
| Tagged `CNA_SKIA_SKSL_V1` effect | Premultiplied child values and reflected uniforms. | Explicit ABI | `main` returns one premultiplied colour. The effect cannot silently change the following blender's source contract. |

`SkiaSourceStorageAlpha`, `ResolveSkiaWorkingSourceRoute`, and each image backend's
`StorageAlphaEXT()` encode this table. `Texture2D::SnapshotImage()` selects its two labelled views;
`RenderTarget2D::SnapshotImage()` proves both requested routes resolve to its existing surface.

Tint is part of the same conversion. For a premultiplied-labelled source, tint RGB and alpha remain
independent (`source.rgb * tint.rgb`, `source.a * tint.a`). For a straight-labelled texture, image
evaluation has already supplied the source-alpha RGB factor, so `MakeSkiaTintScale()` folds tint
alpha into working RGB exactly once. The same four scale values are passed to the custom-effect
`cnaTint` uniform.

## Accepted blend routes

| Mapping | Source label | Working interpretation |
|---|---|---|
| `Opaque` | Premultiplied | Preserve the source vector; replace destination. |
| `AlphaBlend` | Premultiplied | Preserve source RGB and apply inverse source alpha to destination. |
| `NonPremultiplied` | Straight | Texture evaluation supplies the RGB source-alpha factor; the runtime blender computes independent output alpha. |
| `Additive` | Straight | Texture evaluation supplies the RGB source-alpha factor; the runtime blender adds destination and computes independent alpha. |
| `DestinationColorPrototype` | Premultiplied | Preserve source components for the one opaque pixel-proven custom tuple. |
| `Generated` | Premultiplied | Preserve Texture2D/effect working components, reuse target surface components, evaluate every selected factor explicitly, then encode the logical result for SkSurface. |

The five optimized entries carry `SkiaSourceAlphaConvention` in `kSkiaBlendMappings`; the generic
route declares `kSkiaGeneratedBlendSourceAlphaConvention`. SKIA-120's scalar oracle and SKIA-122's
all-mode Texture2D/RenderTarget2D/tagged-effect pixels prove that boundary. Invalid raw selectors
still reject rather than guessing a nearby preset.

`Skia_SourceAlpha_Policy` is display-free. It reads raw premultiplied Skia bytes for discriminating
translucent Texture2D, RenderTarget2D and custom-effect sources, verifies tint scales, and audits
that every accepted mapping names exactly one convention.
