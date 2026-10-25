# CNJ: CNA's editable content format

A `.cnj` file is one content asset described as a JSON document: a sprite font, a model, an effect's
parameters, an animation clip, or metadata layered over an ordinary image or sound. It is the format
people and tools edit. It can be read and diffed by hand, written by a script, and loaded by
`ContentManager` as it is. For shipping, `cna_tool_cnj_to_cnb` compiles it into a single-file `.cnb`
([`cnb-format.md`](cnb-format.md)): glTF, PNG and WAV are how content arrives, `.cnj` is how it is
edited, `.cnb` is how it ships.

## CNA's content formats at a glance

| Format | What it is | Documented in |
|---|---|---|
| `.xnb` | XNA's compiled binary format. CNA reads it, LZX and LZ4 included, and its content pipeline writes it. | [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md), [`xnb-interoperability.md`](xnb-interoperability.md) |
| `.cnb` | CNA's compiled binary format: one asset per file, a bounded binary parse. | [`cnb-format.md`](cnb-format.md) |
| `.cnj` | CNA's editable JSON format. | this page |
| native files | PNG, JPEG, BMP, GIF, TGA, QOI, DDS, WAV and others, loaded by extension | the reader of each type |

`cna_tool_gltf_to_cnj` converts a glTF 2.0 file into a `Model` `.cnj` with its binary sidecars and
one `AnimationClip` `.cnj` per clip.

## How `ContentManager` finds an asset

`Load<T>("Textures/player")` tries these candidates in order and takes the first that exists:

1. **`player.xnb`.** A genuine, externally produced compiled asset wins over everything CNA offers.
2. **`player.cnb`.** CNA's own compiled form, ranked above the sources it was compiled from but
   below a real `.xnb`.
3. **The literal name**, when the caller passed a full file name with an extension.
4. **`player.cnj`.**
5. **`player.<native extension>`**, for each extension the reader of `T` declares.
6. Otherwise the asset is not found.

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
`ValidateCnjEnvelope`/`ValidateCnjEnvelopeBaseline` (`CNA/Internal/CnjEnvelope.hpp`). Members a
reader does not know are ignored.

## References to other assets

A document can name other assets by logical name or relative path: a font its glyph atlas, a model
its textures and animation clips. Each reference is loaded through the same `ContentManager` and
the same resolution order, so it is cached and unloaded like any other asset, and a texture two
models share is loaded once.

## `sourceFile`: metadata over a native file

With `sourceFile`, CNA loads the named file the normal way and applies the document's other members
on top of it. The reference is checked before anything is opened; a violation is a
`ContentLoadException` that names both files:

- it must be relative;
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
| `Texture3D` | `width`, `height`, `depth`, and `data`, a raw RGBA8 sidecar file; one mip level |
| `Curve` | `preLoop`, `postLoop`, and `keys`, each with `position`, `value`, `tangentIn`, `tangentOut` and `continuity` |
| `AnimationClip` | either inline `tracks` or `clipFile`, a reference to a `.clip.bin` sidecar; loaded with `Load<Graphics::AnimationClipEXT>()` and shareable between models |
| `Effect` | a `ShaderEffect` built from GLSL `vertex` and `fragment` files; whether a renderer runs it is that renderer's capability |
| `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect` | the stock effect's parameters (colours, texture references, lighting); an omitted member keeps the effect constructor's own default |
| `Model` | meshes with binary vertex and index sidecars, bones, materials and animations; see the next section |
| anything else | a game-registered loader (see [Custom loaders](#custom-loaders)) |

Two boundaries are deliberate. A compiled XNA effect (shader bytecode) has no `.cnj` form; it comes
from `.xnb` or `.fxb`. And `SkinnedModelEXT` keeps its own `.skinnedmodel.json` format rather than
`.cnj`, because existing cross-language tooling depends on it.

## Model documents

### Versions

A version-1 `Model` has no node hierarchy: every mesh sits in its own local space under one identity
bone. **Version 2** adds two members, written by `cna_tool_gltf_to_cnj` since the glTF campaign
(`plans/plan_gltf.md`, `GLTF-455`):

| Member | Where | Purpose |
|---|---|---|
| `bones` | top level | the node hierarchy, `{ "name", "parent", "transform" }` per node, parent before child |
| `parentBone` | each `meshes` entry | which node places this mesh |

A version-1 document still loads, with exactly the placement it had when it was written. It does
not gain correct placement by being loaded again; re-convert the source glTF for that.

### Optional members

Everything below is written **only when it differs from its default**, so the document of an
ordinary opaque triangle-list asset is byte-identical to what the converter wrote before the member
existed. A reader that does not know a member gets the earlier behaviour for that asset.

| Member | Default when absent | Notes |
|---|---|---|
| `primitiveTopology` | `"TRIANGLES"` | so lines and points are not drawn as triangles |
| `partOfMesh` | the placement is its own mesh | groups the parts of a multi-primitive mesh |
| `alphaMode` | `"OPAQUE"` | |
| `alphaCutoff` | `0.5` | reaches the shader's alpha test for `MASK` materials |
| `doubleSided` | `false` | carried, not applied: culling is a `RasterizerState` the application sets |
| `unlit`, with `diffuseColor`/`alpha` | lit | `KHR_materials_unlit`; the base colour travels with the flag |
| `vertexColorEnabled` | `false` | vertex colours on the non-PBR path |
| `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `diffuseColor`, `alpha` | PBR entries only | for `PbrEffect`/`SkinnedPbrEffect` |
| `normalMap`, `metallicRoughnessMap`, `emissiveMap`, `occlusionMap` | no map | PBR entries only |
| `materialVariantNames` | no variants | the `KHR_materials_variants` names, in source order, at the root |
| `variantOf`, `materialVariant` | an ordinary mesh part | an alternative material state for the entry `variantOf` names, selected by index into `materialVariantNames` |
| `morphTargets`, `morphWeights`, `morphWeightTrack` | no morph targets | the `_morph.bin` sidecar, its default weights and, when animated, its weight track |
| `lights` | no imported lights | `KHR_lights_punctual`, at most three directional |
| `animations` | none | each entry names a standalone `AnimationClip` `.cnj` |

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

A wrong type, a negative, fractional, non-finite or overflowing count, a non-finite magnitude or an
unknown name is rejected. The report never contains the converting machine's absolute paths, so the
output stays relocatable and reproducible.

### The morph sidecar

A `_morph.bin` starts with the original layout: the target count, then each target's position
deltas and optional normal deltas. Tangent deltas follow in an optional trailer that begins with the
little-endian magic `MTAN`, a version (`1`) and the target count again; an older reader stops before
it and keeps its former behaviour. Tangent handedness is not repeated, because `TANGENT.w` lives in
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

The factory receives the whole document as text. The first registration for a `T` installs a
generic reader that validates the envelope and dispatches on `type`, so `Load<T>()` itself does not
change. The contract is strict:

- an empty `typeName` or an empty `factory` throws `std::invalid_argument`;
- registering the same `(T, typeName)` twice throws `std::logic_error` rather than replacing the
  earlier factory;
- registering for a `T` that already has a reader throws `std::logic_error`, because that reader
  would never consult the table.

`tests/Microsoft/Xna/Framework/Content/CnjCustomLoaderTests.cpp` runs this example.

## Related documents

- [`cnb-format.md`](cnb-format.md): the compiled format a `.cnj` becomes.
- [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md): reading `.xnb`.
- [`model-content-pipeline-support.md`](model-content-pipeline-support.md): `Model` loading.
- [`gltf-conformance.md`](gltf-conformance.md) and [`gltf-limitations.md`](gltf-limitations.md): the
  glTF import behind `Model` documents.
- [`../plans/plan_cnj.md`](../plans/plan_cnj.md): the task history of the format.
