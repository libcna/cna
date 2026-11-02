# CNJ: CNA's editable content format

A `.cnj` file is one content asset described as a JSON document: a sprite font, a model, an effect's
parameters, an animation clip, or metadata layered over an ordinary image or sound. It is the format
people and tools edit. It can be read and diffed by hand, written by a script, and loaded by
`ContentManager` within the selected type/renderer's support. For shipping,
`cna_tool_cnj_to_cnb` compiles supported built-in shapes to `.cnb`
([`cnb-format.md`](cnb-format.md)); external asset references can remain separate dependencies,
so one logical compiled asset does not mean an entire model bundle is self-contained.
CNA's pipeline can also ship XNB. Custom runtime CNJ factories do not automatically gain a CNB
compiler/codec.

## CNA's content formats at a glance

| Format | What it is | Documented in |
|---|---|---|
| `.xnb` | XNA's compiled binary format. CNA reads it, LZX and LZ4 included, and its content pipeline writes it. | [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md), [`xnb-interoperability.md`](xnb-interoperability.md) |
| `.cnb` | CNA's compiled binary format: one asset per file, a bounded binary parse. | [`cnb-format.md`](cnb-format.md) |
| `.cnj` | CNA's editable JSON format. | this page |
| native files | PNG, JPEG, BMP, GIF, TGA, QOI, DDS, WAV and others, loaded by extension; glTF 2.0 (`.gltf`, `.glb`) for `Model` | the reader of each type; [`gltf-limitations.md`](gltf-limitations.md) for glTF |

A `Model` loads straight from a `.gltf` or `.glb` file, with no conversion step: they are its native
extensions, tried after `.cnj` like any other. A same-named `.cnj` wins for extensionless loose lookup; an explicit existing glTF path retains
the literal-path rule below.
To work offline instead, `cna_tool_gltf_to_cnj` converts a glTF file into a `Model` `.cnj` with its
binary sidecars and one `AnimationClip` `.cnj` per clip.

## How `ContentManager` finds an asset

On an ordinary cache miss, `Load<T>("Textures/player")` follows these tiers. Compiled XNB is
opened through the virtual `OpenStream` seam; only the base missing-XNB condition permits CNA's
lower tiers. Override refusal/I/O/decode errors are not swallowed. Explicit stream-only managers
do not use the loose fallback.

1. **`player.xnb`.** A compiled XNB stream (including CNA-produced XNB) has first priority.
2. **`player.cnb`.** CNA's own compiled form, ranked above the sources it was compiled from but
   below a real `.xnb`.
3. **The literal name**, if it exists, whether or not it has an extension.
4. **`player.cnj`.**
5. **`player.<native extension>`**, for each extension the reader of `T` declares.
6. The bare path is handed to the reader for its final handling/error.

The cache is keyed by normalized logical name and C++ type; a hit does not rerun these probes.
Case/extension/root handling follows [the path contract](filesystem-path-model.md). A literal
old filename is not automatic schema migration.

A `.xnb` or `.cnb` that is present but malformed or unsupported is an error. Loading does not fall
through to the next candidate, because a broken compiled asset should not be quietly replaced by a
source that happens to sit beside it.

`.cnj` ranks above native files on purpose: that is what lets it enrich one. `ahoj.png` loads on its
own; add `ahoj.cnj` with `"sourceFile": "ahoj.png"` and a colour key, and the same name now loads
the PNG with the colour key applied. The flip side is an authoring trap: an unrelated `ahoj.cnj`
next to `ahoj.png` shadows the image too, and nothing but the file's content says which kind it is.

## The envelope

Every `.cnj` is a single JSON object with a common envelope and type-specific members:

```json
{
  "cnjVersion": 1,
  "type": "Texture2D",
  "sourceFile": "ahoj.png",
  "colorKey": [255, 0, 255]
}
```

- **`cnjVersion`**, required. The schema version of the document. The highest version a reader
  accepts is set **per type**: `Model` accepts 1 and 2, every other type accepts 1 only. A version
  above that ceiling is rejected by name, and so is a non-integer one such as `1.5`. Raise a type's
  ceiling only for a change an older reader cannot ignore. Prefer new optional members written only
  when they differ from their default, which need no new version at all.
- **`type`**, required. A stable name for what the document is. `Load<T>()` already knows the C++
  type it wants, so `type` is a cross-check: asking for a `SpriteFont` and finding a `Model` fails
  with a clear message instead of a confusing field error. For tools with no compile-time type
  (validators, converters), it is the dispatch key, and for game-registered loaders it selects the
  loader (see [Custom loaders](#custom-loaders)).
- **`sourceFile`**, optional. When present, the document is metadata about another file rather than
  a self-contained description (see below).

The envelope is parsed by `CNA::Internal::ParseCnjEnvelope` and checked by
`ValidateCnjEnvelope`/`ValidateCnjEnvelopeBaseline` (`CNA/Internal/CnjEnvelope.hpp`). Built-in
readers generally ignore unknown optional members; a custom factory can define stricter rules.
The generic custom dispatch still accepts version 1 only. Schema versions do not negotiate every
optional feature or vertex stride, so a newly authored asset can still require a newer runtime.

Envelope extraction retains the raw numeric version and narrows it only when it fits the `int`
range; unsupported and fractional versions are then rejected by validation. Inline AnimationClip
`boneIndex` values must be finite integers representable as Int32 before conversion. Standalone
clips retain signed indices, as binary sidecars do; a player ignores indices outside its current
bone/node collection. Extreme-version and fractional/out-of-range index regressions cover these
conversions. This does not claim complete hostile-JSON or numeric-range safety for every field.

A document may nest at most 128 objects/arrays; deeper input is refused before the parser recurses
(`CNA::Internal::ParseJson`). Integer members of `Model` documents (bone parents, `vertexStride`,
mesh-part grouping, sampler fields) and animation durations/key times must be finite values of
their field's range, checked before conversion; the runtime loader and `cna_tool_cnj_to_cnb` apply
the same checks, and a model's referenced AnimationClip goes through the one canonical reader.

## References to other assets

There are two mechanisms. Asset references such as a SpriteFont atlas or a model's external
texture/effect go through `ContentManager` resolution and caching. Binary geometry, raw pixels,
GLSL sources, skeletons and raw `.clip.bin` are sidecar file reads, not independently cached assets.
A standalone AnimationClip `.cnj` is cached, but returned clip values are copies rather than one
mutable shared clip object.

| Reference | Resolution base |
|---|---|
| `sourceFile` | Referring CNJ's directory, then confined to the manager's content root |
| Model vertex/index/skeleton/morph/raw-clip paths, AnimationClip `clipFile`, Texture3D `data`, Effect `vertex`/`fragment` | Content root, with contained-relative-path checks |
| Built-in asset references (atlas, textures, external effects, standalone clips) | Root-relative logical names through `ContentManager`, after the applicable path check |
| Custom factory references | The factory's explicit contract |

Moving a native sidecar CNJ to a subdirectory and moving a root-relative Model bundle are therefore
different operations. For the separate `.skinnedmodel.json` container, see
[SkinnedModelEXT](skinned-model-ext.md), whose manifest references use its own bundle rules.

## `sourceFile`: metadata over a native file

With `sourceFile`, the built-in reader checks the embedded path, loads its logical name through
`ContentManager`, then applies supported metadata. Path violations raise `ContentLoadException`
with the referring file/reason:

- it must be nonempty and relative to the CNJ's own directory;
- it must stay inside the content root, through `../` or a symbolic link alike;
- it must not resolve to another `.cnj`, by name or because the resolver would pick a sibling
  `.cnj`, so a sidecar can never chain to another sidecar or back to itself.

Which types accept it:

| Type | `sourceFile` | Members applied on top |
|---|---|---|
| `Texture2D` | **required** | `colorKey`: `[r, g, b]`; matching pixels become transparent |
| `SoundEffect` | **required**, a `.wav` | none yet |
| `TextureCube` | **required**, a `.dds` | none yet |
| every other built-in type | **rejected** | these documents are self-contained |
| a game-registered loader | up to the loader | it receives the whole document |

`colorKey` is the only metadata the runtime `Texture2D` CNJ applies: three integral bytes;
matching RGB values retain their RGB and receive alpha zero. Premultiplication, atlas cropping
and mip policy are not defined by this descriptor. Their absence is not a claim that CNA lacks
every such runtime/pipeline capability; pipeline processing options have their own contract.
The broad loose-image decoder is also distinct from classic
[`Texture2D::FromStream`](texture-stream-formats.md)'s measured PNG/JPEG/GIF allowlist.

## One extension for every type

All types share the `.cnj` extension, so a font and a model cannot both be called `Cursor`. That is
how XNA behaved too: its pipeline built one `<name>.xnb` per logical asset and read the type from
inside the file. The cost is that a folder of `.cnj` files does not show what each one is; tools
read `type`.

## The types

| `type` | Members |
|---|---|
| `Texture2D`, `SoundEffect`, `TextureCube` | `sourceFile` plus metadata, as above |
| `SpriteFont` | `texture` (the glyph atlas, an ordinary image), `lineSpacing`, `spacing`, `defaultCharacter`, and `glyphs`, each with `char` (an integer code), `source`, `crop` and `kerning` |
| `Texture3D` | `width`, `height`, `depth`, and `data`, a raw RGBA8 sidecar file; one mip level, no DXT |
| `Curve` | `preLoop`, `postLoop`, and `keys`, each with `position`, `value`, `tangentIn`, `tangentOut` and `continuity` |
| `AnimationClip` | exactly one of `tracks` or `clipFile`; inline tracks require numeric `duration` in seconds, each track has `boneIndex` and `keys`, each key has time in seconds and optional translation/rotation/scale. `targetSpace: "SceneNode"` selects rigid-node indices; otherwise JointPalette is the default. Load with `Load<Graphics::AnimationClipEXT>()`; raw clip sidecars default to JointPalette. |
| `Effect` | a `ShaderEffect` built from GLSL `vertex` and `fragment` files; whether a renderer runs it is that renderer's capability |
| `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect` | the stock effect's parameters (colours, texture references, lighting); an omitted member keeps the effect constructor's own default |
| `Model` | meshes with binary vertex and index sidecars, bones, materials and animations; see the next section |
| anything else | a game-registered loader (see [Custom loaders](#custom-loaders)) |

A compiled XNA effect has no CNJ descriptor here: use supported Effect XNB or the bytecode Effect
API with bytes from an `.fxb`; the loose Effect reader declares `.cnj`, not automatic `.fxb`
loading. `SkinnedModelEXT` retains its separate `.skinnedmodel.json` format for its existing
offline tooling ([guide](skinned-model-ext.md)).

Raw `clipFile` and model `.clip.bin` references share one bounded reader. It checks the default
CNB file/count limits and every track/key byte range before output allocation, then requires
representable, nondecreasing times, finite transforms and no trailing bytes. Equal key times are
legal. This validation belongs to raw sidecars; inline clip loading is not a substitute for the
target-space and key validation performed by the animation samplers. See the
[skinned-model guide](skinned-model-ext.md#loose-content-format) for limits and memory scope.

### A `SpriteFont` document

```json
{
  "cnjVersion": 1,
  "type": "SpriteFont",
  "texture": "Fonts/Consolas_atlas.png",
  "lineSpacing": 24,
  "spacing": 0.0,
  "defaultCharacter": "?",
  "glyphs": [
    { "char": 65, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] }
  ]
}
```

`char` is the character's integer code. `source` (the glyph's rectangle in the atlas) and `crop` are
integer `[x, y, width, height]` arrays, and `kerning` is `[left bearing, advance, right bearing]`.
`lineSpacing`, `spacing` and `defaultCharacter` are optional.

### Older CNA JSON files

Before `.cnj`, CNA's JSON readers each had their own extension: `.font.json` for sprite fonts,
`.model.json` for models and `.shader.json` for effects. None of them is read any more. To convert
such a document, add `"cnjVersion": 1` and its matching `"type"`, save it as `.cnj`, and
verify the current type-specific fields/paths. The old envelope is not recognized automatically,
even if the literal file resolver can find an old suffix. A `Model` converted this way is a version-1 document,
without a bone hierarchy (see below).

## Model documents

### The document

A `Model` document describes its geometry in JSON and keeps the bulk data in binary sidecar files.
Every path in it is relative to the content root and must stay inside it; texture and effect
references are logical asset names, loaded and cached through the same `ContentManager`.

```json
{
  "cnjVersion": 2,
  "type": "Model",
  "bones": [
    { "name": "Root" },
    { "name": "Hull", "parent": 0, "transform": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,2,0,1] }
  ],
  "meshes": [
    {
      "name": "Hull",
      "vertices": "Models/ship_hull_verts.bin",
      "indices": "Models/ship_hull_idx.bin",
      "vertexStride": 32,
      "effect": "BasicEffect",
      "texture": "Models/ship_diffuse",
      "parentBone": 1
    }
  ]
}
```

Members at the root:

| Member | Meaning |
|---|---|
| `meshes` | geometry entries, normally one per primitive (several can form one mesh through `partOfMesh`). The legacy runtime reader can return an empty model when geometry is absent/skipped; it is not a strict nonempty-asset validator. |
| `bones` | version 2: the node hierarchy as `{ "name", "parent", "transform" }` entries, parent before child. Entry 0 is the root, of which only the name is read; every other entry needs a valid `parent`. `transform` is 16 numbers in XNA's row-major `Matrix` order, so the translation is the 13th to 15th |
| `skeleton` | optional `.skeleton.bin` sidecar for a skinned model; with `animations`, its skinning data is attached to `Model::Tag`, as XNA's Skinned Model sample does |
| `animations` | optional `{ "name", "clip" }` entries; `clip` names a `.clip.bin` sidecar or a standalone `AnimationClip` `.cnj` |
| `lights`, `cameras`, `materialVariantNames`, `gltfImportReport` | optional; see below |

Members of each `meshes` entry:

| Member | Meaning |
|---|---|
| `name` | the mesh's name |
| `vertices` | sidecar of raw vertex records, `vertexStride` bytes each |
| `indices` | sidecar of indices: 16-bit, or 32-bit once the mesh has more than 65,535 vertices, as XNA's own model processor chooses |
| `vertexStride` | positive supported layout size (default 16), listed below; unrecognized positive layouts are refused. A nonpositive value or missing vertices/indices makes the legacy runtime reader skip that entry. |
| `effect` | empty or `BasicEffect` (the default), `SkinnedEffect`, `DualTextureEffect`, `PbrEffect`, `SkinnedPbrEffect`, or the asset name of any other effect. A custom effect gets no texture bound by the loader |
| `texture` | the diffuse texture. `BasicEffect` gets texturing enabled; `SkinnedEffect` always samples it |
| `texture2` | `DualTextureEffect`'s second texture |
| `parentBone` | version 2: the index in `bones` of the node that places this mesh |

The vertex layouts, in declaration order. `Color` is four normalized bytes, `Byte4` four integer
bytes and everything else 32-bit floats; elements follow each other without padding.

| `vertexStride` | Elements | Effect it suits |
|---:|---|---|
| 16 | Position, Color | `BasicEffect` (vertex colours) |
| 20 | Position, TextureCoordinate | `BasicEffect` |
| 24 | Position, Color, TextureCoordinate | `BasicEffect` |
| 28 | Position, TextureCoordinate, TextureCoordinate1 | `DualTextureEffect` |
| 32 | Position, Normal, TextureCoordinate | `BasicEffect` (lit) |
| 48 | Position, Normal, Tangent (`Vector4`), TextureCoordinate | `PbrEffect` |
| 52 | Position, Normal, TextureCoordinate, BlendWeight (`Vector4`), BlendIndices (`Byte4`) | `SkinnedEffect` |
| 56 | the 52-byte layout, then Color | `SkinnedEffect` (vertex colours) |
| 60 | the 48-byte layout, then TextureCoordinate1 and Color | `PbrEffect` (two UV sets) |
| 68 | Position, Normal, Tangent, TextureCoordinate, BlendWeight, BlendIndices | `SkinnedPbrEffect` |
| 76 | the 68-byte layout, then TextureCoordinate1 | `SkinnedPbrEffect` (two UV sets) |
| 80 | the 76-byte layout, then Color | `SkinnedPbrEffect` (two UV sets, vertex colours) |

The canonical layouts and scope of source/import/draw checks are documented in
[`gltf-renderer-stride-conformance.md`](gltf-renderer-stride-conformance.md).

What the loader builds from this: the bone tree from `bones`, each mesh's `ParentBone` from
`parentBone`, a `BoundingSphere` per mesh computed from its vertex positions, and a morph-target
`Tag` on each part that has one.

### Versions

A hierarchyless version-1 `Model` gives each mesh its own identity child bone of Root.
**Version 2** defines the hierarchy fields, written by `cna_tool_gltf_to_cnj` since the glTF campaign
(`GLTF-455`):

| Member | Where | Purpose |
|---|---|---|
| `bones` | top level | the node hierarchy, `{ "name", "parent", "transform" }` per node, parent before child |
| `parentBone` | each `meshes` entry | which node places this mesh |

A hierarchyless old document does not gain the source scene's placement merely by loading again;
reconvert the original glTF. The current reader consumes a supplied bones array even under
version 1, so version 1 is not itself an enforcement that the field is absent; author hierarchy
documents as version 2. Root entry 0 stays at identity in both the loose loader and native
schema-1 CNB loader. A stored canonical root transform is not applied on those paths; this is
different from schema-2 CNB's exact stored XNB-style graph/root contract.

### Optional members

The converter generally omits default optional settings. That does not promise byte-identical
assets across converter versions or correct appearance on a reader that ignores a feature. A new
layout can also be refused by an old reader even when `cnjVersion` is unchanged.

| Member | Default when absent | Notes |
|---|---|---|
| `primitiveTopology` | `"TRIANGLES"` | so lines and points are not drawn as triangles |
| `partOfMesh` | the placement is its own mesh | groups the parts of a multi-primitive mesh |
| `alphaMode` | `"OPAQUE"` | |
| `alphaCutoff` | `0.5` | reaches the shader's alpha test for `MASK` materials |
| `doubleSided` | `false` | carried, not installed by Model.Draw: the application sets culling; likewise BLEND needs application BlendState |
| `unlit`, with `diffuseColor`/`alpha` | lit | `KHR_materials_unlit`; the base colour travels with the flag |
| `vertexColorEnabled` | `false` | vertex colours on the non-PBR path |
| `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `diffuseColor`, `alpha` | glTF's defaults | for `PbrEffect`/`SkinnedPbrEffect` |
| `normalMap`, `metallicRoughnessMap`, `emissiveMap`, `occlusionMap` | no map | PBR entries only |
| `normalScale`, `occlusionStrength` | `1` | scale the normal map and the occlusion map |
| `ior`, `specularFactor`, `specularColorFactor`, `specularMap`, `specularColorMap` | glTF's defaults (`ior` 1.5) | `KHR_materials_ior` and `KHR_materials_specular` |
| `textureCoordinateSets`, `specularTextureCoordinateSets` | set 0 for every map | which UV set each map samples |
| `textureTransforms`, `specularTextureTransforms` | identity | `KHR_texture_transform`, per map |
| `morphFlatNormals` | `false` | for a morphing part with flat normals |
| `materialVariantNames` | no variants | the `KHR_materials_variants` names, in source order, at the root |
| `variantOf`, `materialVariant` | an ordinary mesh part | an alternative material state for the entry `variantOf` names, selected by index into `materialVariantNames` |
| `morphTargets`, `morphWeights`, `morphWeightTrack` | no morph targets | the `_morph.bin` sidecar, its default weights and, when animated, its weight track |
| `lights` | no imported lights | root: `KHR_lights_punctual`, at most three directional, each `{ "direction", "diffuseColor" }` |
| `cameras` | no cameras | root: `{ "name", "sceneNode", "worldTransform" }` plus either `perspective: true` (the default) with `yfov`, `aspectRatio`, `znear`, `zfar`, or `perspective: false` with `xmag`, `ymag`, `znear`, `zfar` |

### The glTF import report

`cna_tool_gltf_to_cnj` also writes an optional root object, `gltfImportReport`. Documents written
before it existed load with an empty report, and it does not raise `cnjVersion`. Its counters are
non-negative integers: `nodeCount`, `meshInstanceCount`, `distinctMeshCount`, `sharedMeshCount`,
`maxNodeDepth`, `cameraNodeCount`, `lightNodeCount`, `importedLightCount`, `primitiveCount`,
`skinCount`, `animationCount` and `clipCount`. `diagnostics` is an ordered array of entries with:

| Member | Meaning |
|---|---|
| `code` | stable lower-case identifier |
| `severity` | `Information` or `Warning` |
| `kind` | `Information`, `GeneratedData`, `InvalidSourceData`, `Approximation`, `DroppedData` or `UnsupportedFeature` |
| `subject` | the affected primitive, node, clip or extension; may be empty |
| `count` | non-negative number of occurrences |
| `worstMagnitude` | finite measured maximum, or zero where the code defines none |
| `details` | names of the affected maps, attributes or extensions |
| `message` | display text; its wording is not a stable API |

The report reader rejects wrong field types, negative/fractional/non-finite/overflowing counts,
non-finite magnitudes and unknown severity/kind names. Diagnostic codes and text are strings, not
a closed enum. The converter avoids machine-path provenance in its generated diagnostics; the
reader does not redact arbitrary strings supplied by a hand-authored document. Reproducibility
still requires matched converter/input versions.

### The morph sidecar

A `_morph.bin` starts with the original layout: the target count, then each target's position
deltas and optional normal deltas. Tangent deltas follow in an optional trailer that begins with the
little-endian magic `MTAN`, a version (`1`) and the target count again. Keeping the legacy prefix
does not make an older reader apply the added tangent deltas; match the target runtime's feature
support. Tangent handedness is not repeated, because `TANGENT.w` lives in
the base vertices and blending never changes it. A target without positions, which glTF allows, gets
a zero-filled position stream, since the original layout sizes every later stream from it.

## Custom loaders

`ContentManager::RegisterTypeReader<T>()` attaches one reader to one C++ type. A game whose data
files share a C++ type but differ in shape needs a different parser for each, chosen by the file.
`RegisterCnjLoader<T>(typeName, factory)` does that, keyed by the document's `type`:

```cpp
struct GameData { std::string kind; };

content.RegisterCnjLoader<GameData>("EnemyDefinition",
    [](const std::string& json, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });
content.RegisterCnjLoader<GameData>("LootTable",
    [](const std::string& json, ContentManager&) { GameData d; d.kind = "Loot"; return d; });

// goblin.cnj: {"cnjVersion": 1, "type": "EnemyDefinition"}
// chest.cnj:  {"cnjVersion": 1, "type": "LootTable"}
GameData enemy = content.Load<GameData>("goblin");   // kind == "Enemy"
GameData loot  = content.Load<GameData>("chest");    // kind == "Loot"
```

The factory is a `CnjLoaderFn<T>`, that is
`std::function<T(const std::string& cnjJson, ContentManager& cm)>`: it receives the whole document
as text and may load further assets through `cm`. The first registration for a `T` installs a
generic reader that validates the envelope and dispatches on `type`, so `Load<T>()` itself does not
change. The contract is strict:

- an empty `typeName` or an empty `factory` throws `std::invalid_argument`;
- registering the same `(T, typeName)` twice throws `std::logic_error` rather than replacing the
  earlier factory;
- registering for a `T` that already has a reader throws `std::logic_error`, because that reader
  would never consult the table.

The example is covered by
`modules/content/tests/Microsoft/Xna/Framework/Content/CnjCustomLoaderTests.cpp`.
Registration is manager-local, and built-in types already have their own reader even before a
first load. A custom factory is responsible for its own sourceFile interpretation, validation,
dependency cycles and resource ownership.

## Related documents

- Current verification (2026-11-01): the content target and glTF-to-CNJ tool build. Privately on
  OPENGLES3 with dummy audio selected before process startup, 140/142 envelope, per-type CNJ,
  resolver/custom-loader and offline round-trip tests pass; two external large-reference budget
  cases skip for absent asset configuration. Runtime and canonical builders, paths, registration,
  version/field parsing and layouts were source-checked. This is not every-renderer pixel parity,
  a fresh large-asset performance campaign or complete hostile-input safety.
- [`cnb-format.md`](cnb-format.md): the compiled format a `.cnj` becomes.
- [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md): reading `.xnb`.
- [`model-content-pipeline-support.md`](model-content-pipeline-support.md): `Model` loading.
- [`xna-content-pipeline-migration.md`](xna-content-pipeline-migration.md): bringing an XNA game's
  content to CNA.
- [`gltf-renderer-stride-conformance.md`](gltf-renderer-stride-conformance.md): canonical vertex
  layouts and the exact renderer validation scope.
- [`gltf-conformance.md`](gltf-conformance.md) and [`gltf-limitations.md`](gltf-limitations.md): the
  glTF import behind `Model` documents.
- The task history of the format, `CNB-1`..`CNB-110` (2026-07), was `plans/plan_cnj.md`, deleted
  on 2026-10-28 (Git history only). Source comments cite those rows as "CNJ CNB-n": the `CNB-`
  prefix dates from when `.cnj` was called `.cnb` and has nothing to do with the binary format in
  [`cnb-format.md`](cnb-format.md), whose tasks are `CNBF-*`.
