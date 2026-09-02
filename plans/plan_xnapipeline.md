# plan_xnapipeline.md — CNA XNA 4.0 Content Pipeline and XNB writing

> **Status (2026-09-02):** active. This plan owns the **write** side of `.xnb`: a native C++ CNA
> equivalent of the useful parts of the XNA 4.0 Content Pipeline architecture, ending in real
> `.xnb` production. It is a first-class CNA subsystem, not a converter script and not a wrapper
> around MonoGame tooling.
>
> **Boundary with existing plans.**
> * `plans/plan_xnb.md` + `xnb.md` own the **runtime `.xnb` reader**. This plan does not reopen
>   reader design; it treats the reader as an authoritative specification source and as the
>   round-trip oracle.
> * `plans/plan_cnb.md` owns the **frozen CNB compiled format**. This plan does not change a CNB
>   chunk, schema, or guarantee.
> * `plans/plan_content_pipeline.md` owns the **build-time pipeline above CNB** (`CP-001`…`CP-063`:
>   `ContentImporter` → `ContentProcessor` → `ContentTypeWriter` → CNB, the `cna-content` CLI, the
>   build manifest, the CMake integration). This plan **extends** that pipeline with a second
>   output format rather than building a parallel one.
> * `plans/plan_cnj.md` owns `.cnj`. Untouched here.
>
> **Task IDs:** `XNAP-001` … . One task = one commit, matching this repository's convention.

---

## Table of contents

1. [Current CNA content architecture audit](#1-current-cna-content-architecture-audit)
2. [Existing XNB reader capabilities](#2-existing-xnb-reader-capabilities)
3. [Existing CNB/CNJ architecture and reusable components](#3-existing-cnbcnj-architecture-and-reusable-components)
4. [Target XNA Content Pipeline architecture](#4-target-xna-content-pipeline-architecture)
5. [Proposed CNA C++ architecture](#5-proposed-cna-c-architecture)
6. [Mapping of XNA C# pipeline concepts to CNA C++ equivalents](#6-mapping-of-xna-c-pipeline-concepts-to-cna-c-equivalents)
7. [XNB writer design](#7-xnb-writer-design)
8. [ContentTypeWriter architecture](#8-contenttypewriter-architecture)
9. [ContentImporter architecture](#9-contentimporter-architecture)
10. [ContentProcessor architecture](#10-contentprocessor-architecture)
11. [Intermediate content object model](#11-intermediate-content-object-model)
12. [External asset decoding / import dependencies](#12-external-asset-decoding--import-dependencies)
13. [XNB compatibility requirements](#13-xnb-compatibility-requirements)
14. [XNA 4.0 compatibility requirements](#14-xna-40-compatibility-requirements)
15. [Relationship between XNB and CNB](#15-relationship-between-xnb-and-cnb)
16. [Testing strategy](#16-testing-strategy)
17. [Interoperability strategy](#17-interoperability-strategy)
18. [Licensing and provenance rules](#18-licensing-and-provenance-rules)
19. [Implementation phases](#19-implementation-phases)
20. [Risks and unknowns](#20-risks-and-unknowns)
21. [Completion criteria](#21-completion-criteria)
22. [Task log](#22-task-log)

---

## 1. Current CNA content architecture audit

Audited at `next` = `7560966` (`fix(SAMPLE-152): validate XNA effect render states`).

### 1.1 Physical layout

Everything content-related lives in one module, `modules/content` (target `cna_content`):

```text
modules/content/include/
  Microsoft/Xna/Framework/Content/     XNA runtime API: ContentManager, ContentReader,
                                       ContentTypeReader, ContentTypeReaderManager,
                                       ReflectiveTypeReader, ResourceContentManager, …
  CNA/Internal/Xnb/                    .xnb read-side internals: XnbHeader, XnbTypeReaderTable,
                                       XnbReadLimits, XnbArithmetic, XnbDecompression,
                                       LzxDecoder, XnbCanonicalData, per-type readers
  CNA/Content/Cnb/                     the frozen CNB compiled format: reader/writer/document,
                                       texture/spritefont/soundeffect/model/curve/media codecs
  CNA/Content/Pipeline/                the build-time pipeline (CP-001…CP-063)
  CNA/Internal/GltfImport/             glTF import core
modules/content/src/{Xna,Xnb,Cnb,Pipeline,Internal,GltfImport}/
modules/content/tests/…                GoogleTest suites mirroring the namespace path
tools/content/content.cpp              the `cna-content` CLI implementation (a static library)
tools/content/content_main.cpp         the CLI entry point
cmake/ToolContentPipeline.cmake        `cna_content_compiler` + `cna_content_tool` + `cna_add_content()`
```

### 1.2 The existing build-time pipeline (`CNA::Content::Pipeline`)

`modules/content/include/CNA/Content/Pipeline/ContentPipeline.hpp` already defines a complete,
deterministic, **CNB-targeted** pipeline. Verbatim inventory of its public types:

| Type | Role |
|---|---|
| `ContentComponentIdentity` | stable `{name, version}` — no RTTI, no ABI spelling |
| `ContentWriterSchemaIdentity` | `{assetTypeId, assetSchemaVersion, assetTypeName, codec}` — **CNB-specific** |
| `ContentPipelineStage` | `Selection/Import/Process/Write/Graph/Publish` |
| `ContentBuildLogger`, `ContentLogMessage`, `ContentLogLevel` | build logging |
| `ContentDependency`, `ContentDependencyKind`, `ContentDependencyCollector` | build-time dependency capture |
| `RuntimeContentReference` | a runtime XREF (becomes a CNB XREF entry) |
| `ContentDeploymentFile` | a non-CNB file copied beside compiled content |
| `ContentProcessorParameters` | typed processor parameters |
| `ContentValue` | type-erased value tagged with a **stable type string** |
| `ContentImporterContext`, `ContentProcessorContext` | call-scoped services |
| `ContentImporter` | `Identity()`, `SourceExtensions()`, `OutputTypes()`, `Import()` |
| `ContentProcessor` | `Identity()`, `InputType()`, `OutputType()`, `ValidateParameters()`, `Process()` |
| `ContentTypeWriter` | `Identity()`, `OutputSchemaIdentities()`, `InputType()`, `Write()` → **CNB bytes** |
| `ContentPipelineRegistry` | explicit freeze-then-use registry, selection by stable name/type |
| `ContentBuildRequest` / `ContentBuildResult` / `ContentPipelineError` / `ContentPipeline` | coordinator |
| `ContentSourceRootCapabilities` | named read-only external source roots |

Registered built-ins today (`RegisterBuiltInContentPipeline`):

| Slice | Importer | Processor | Writer | Processed stable type |
|---|---|---|---|---|
| `Texture2DContentPipeline` | `ImageImporter` | `TextureProcessor` | `Texture2DContentWriter` | `CNA.Content.Cnb.Texture2DData` |
| `CnjContentPipeline` | `.cnj` importers | Texture3D/Cube, Curve, AnimationClip, SpriteFont | matching writers | `…Texture3DData`, `…TextureCubeData`, `CNA.Content.Compiled.Curve`, `…AnimationClipData`, `…SpriteFontData` |
| `SoundEffectContentPipeline` | WAV importer | `SoundEffectProcessor` | writer | `CNA.Content.Cnb.SoundEffectData` |
| `SongContentPipeline` | song importer | processor | writer | `CNA.Content.Cnb.SongData` |
| `VideoContentPipeline` | video importer | processor | writer | `CNA.Content.Cnb.VideoData` |
| `ModelContentPipeline` | glTF importer | model processor | model writer | `CNA.Content.Pipeline.ProcessedModelBundle` |
| `XnbContentPipeline` | **`XnbImporter`** (`.xnb` → canonical value) | `XnbVideoProcessor` | (reuses the above) | — |

**Key finding.** The `.xnb` → CNB direction already exists (`CP-031`…`CP-038`): `XnbImporter` decodes
a supported built-in XNB root headlessly and feeds the *existing* processors/writers. The missing
direction is the write side. That is exactly the gap this plan fills, and it means the importer /
processor / intermediate layers are already built and proven — only serialization is new.

### 1.3 The `cna-content` CLI

`cna-content build <source-file-or-directory> -o <output> [--config <file>] [--workers 1..64]
[--explain] [--quiet]` and `cna-content clean <output-directory>`. It has a content-hashed build
manifest, deterministic parallel scheduling, atomic publication, `.cna-content.json` per-asset
configuration (importer/processor/writer/parameters/logicalName + named source roots), and a
`cna_add_content()` CMake function. Output today is always `.cnb`.

### 1.4 Findings that constrain this plan

* **F1.** `ContentTypeWriter::Write()` returns `ContentWriteResult`, whose fields (`assetTypeId`,
  `assetSchemaVersion`, `assetTypeName`) are CNB container concepts. It cannot be reused verbatim
  for XNB without corrupting its meaning. A second, XNB-shaped writer contract is required.
* **F2.** `ContentValue`'s stable-type-string tagging is exactly the RTTI-free typed dispatch this
  plan needs. Reuse it; do not invent a second type-erasure scheme.
* **F3.** The runtime reader and the build-time pipeline are **already in one library**
  (`cna_content`). Splitting them is a `plans/plan_content_pipeline.md`-scoped modularization
  question, not an XNB-writing question. This plan must not make that worse: the XNB writer adds
  **zero** new third-party dependencies, and the new code is confined to files that a future split
  can move as a unit. Recorded as a known limitation in §21.
* **F4.** `CNA::Internal::Xnb::XnbCanonicalData` decodes every supported XNB root **headlessly**
  (no `GraphicsDevice`) into plain structs, and `DecodeXnbCanonicalAsset(path)` does a whole file.
  This is the natural inverse target and the round-trip oracle.
* **F5.** `docs/xnb-content-pipeline-support.md` currently states that producing `.xnb` is
  *"Out of scope, permanently"*. That statement is now wrong and must be corrected (§19 Phase 9).

---

## 2. Existing XNB reader capabilities

From `docs/xnb-content-pipeline-support.md` and the source, re-verified against the tree:

* **Container**: 3-byte magic, platform byte (16 FNA-accepted identifiers), version 4/5, flags byte
  (`0x80` LZX, `0x40` LZ4, both = rejected), `int32` total file length. `ParseXnbHeader()`.
* **Type-reader table**: 7-bit count; per entry a length-prefixed assembly-qualified name plus an
  `int32` reader version. Names are normalized (assembly qualifiers stripped, generics parsed) by
  `NormalizeXnbTypeReaderName()`.
* **Shared resources**: 7-bit count after the table; two-pass fixups; `ReadSharedResource<T>()`.
* **Object dispatch**: 7-bit `typeId`; `0` = null; otherwise `typeId - 1` indexes the table.
* **Primitives / math / `Decimal` / `DateTime` / `TimeSpan` / `Curve`**: full.
* **Textures**: `Texture2D` (`Color`, `NormalizedByte2`, `NormalizedByte4`, `Dxt1/3/5`),
  `Texture3D` and `TextureCube` (`Color`, `Dxt1/3/5`).
* **`SpriteFont`**, **`SoundEffect`** (PCM8/16, IEEE float, MS-ADPCM, IMA-ADPCM; XMA2 rejected),
  **`Song`**, **`Video`**, the 5 stock effects, `EffectMaterial`, `ExternalReference`,
  `VertexDeclaration`/`VertexBuffer`/`IndexBuffer`, `Model`, general `EffectReader`.
* **Collections**: `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>`/`NullableReader<T>` as
  C++ templates, registered per closed combination.
* **`ReflectiveReader<T>`**: supported via `ReflectiveTypeReaderBuilder<T>` (game declares fields).
* **Compression**: LZX decode (full), LZ4 raw-block decode (MonoGame variant).
* **Hardening**: `XnbReadLimits`, checked multiplication (`XnbArithmetic`),
  `ReadBytesExactOrThrow()`, `CheckCollectionElementCount()`, `CheckDecodedByteSize()`,
  whole-container fuzzing.

**Reader-derived write requirements** (the reader is a specification the writer must satisfy):

* Root type-reader table entry version must be `0` (`DecodeXnbCanonicalAsset` rejects non-zero).
* Header `totalLength` must equal the real file size exactly.
* Every reader name emitted must be one the reader registry resolves.
* Collection counts, byte counts and dimensions must stay inside `DefaultXnbReadLimits()`, or CNA
  itself refuses to load its own output.

---

## 3. Existing CNB/CNJ architecture and reusable components

### 3.1 CNB (frozen)

`docs/cnb-format.md`, `plans/plan_cnb.md`. A chunked container (`CMET` metadata, per-asset chunk
families), CRC32C-protected, deterministic, optional zstd chunk compression, `CnbByteWriter` /
`CnbByteReader` primitives with explicit little-endian decomposition and `std::bit_cast` floats.

**Directly reusable for this plan** — the *canonical decoded data structures*, which are the real
intermediate content objects the pipeline already produces:

| CNB struct | Shape |
|---|---|
| `Cnb::CnbTextureData` | `{width,height,depth,faceCount,mipCount, representations[]{format, levels[]}}` |
| `Cnb::CnbSpriteFontData` | `{atlas, glyphBounds[], cropping[], kerning[], characters[], lineSpacing, spacing, defaultCharacter}` |
| `Cnb::CnbSoundEffectData` | WAVEFORMATEX-equivalent fields + samples + loop points |
| `Cnb::CnbSongData`, `CnbVideoData` | external-media identity + metadata |
| `Cnb::CnbModelData` / `CnbModelV2Data` | bones, meshes, mesh parts, vertex/index buffers, materials |
| `Microsoft::Xna::Framework::Curve` | the runtime type itself |

**Style reusable, code not**: `CnbByteWriter` is the model for `XnbByteWriter` (byte-decomposed
little-endian, `bit_cast` floats, no host-endianness dependence, no clock/pointer/random input →
byte-deterministic output). XNB's primitive encoding differs (7-bit-prefixed strings, no chunk
framing), so it needs its own class rather than an extension of `CnbByteWriter`.

### 3.2 CNJ

`misc/cnj.md`, `plans/plan_cnj.md`. A JSON authoring format with binary sidecars; already an
importer route into the pipeline. Nothing about `.cnj` needs to change; `.cnj` sources gain XNB
output for free because they share the processor/intermediate layers.

---

## 4. Target XNA Content Pipeline architecture

The XNA 4.0 build-time model (public API, `Microsoft.Xna.Framework.Content.Pipeline.*`):

```text
source file
   │  ContentImporter<T>            [ContentImporterAttribute: extensions, default processor]
   ▼
intermediate content object          NodeContent / MeshContent / TextureContent / AudioContent / …
   │  ContentProcessor<TIn,TOut>    [ContentProcessorAttribute; parameters as properties]
   ▼
processed content object
   │  ContentTypeWriter<T>          [ContentTypeWriterAttribute; GetRuntimeReader()]
   ▼  ContentWriter (object graph serializer)
.xnb
   │
   ▼  ContentManager.Load<T>() → ContentReader → ContentTypeReader<T>
runtime object
```

Supporting concepts: `ContentItem` (identity + opaque data + name), `ContentIdentity`
(source filename / fragment / component), `ExternalReference<T>`, `ContentBuildLogger`,
`ContentImporterContext`, `ContentProcessorContext` (`BuildAsset`, `BuildAndLoadAsset`,
`Convert`, `AddDependency`, `Parameters`, `TargetPlatform`, `TargetProfile`), `ContentCompiler`,
`OpaqueDataDictionary`.

The **binding rule** that makes the format work: a `ContentTypeWriter<T>` declares
`GetRuntimeReader(TargetPlatform)` — the assembly-qualified name of the `ContentTypeReader` that
will read it. That string is what lands in the file's type-reader table. Everything else is
mechanical.

---

## 5. Proposed CNA C++ architecture

Two new layers, strictly separated, plus one additive extension of the existing pipeline.

```text
                     ┌──────────────────────── existing, unchanged ─────────────────────────┐
 source asset ──▶ ContentImporter ──▶ ContentProcessor ──▶ ContentValue (canonical/processed)
                     └──────────────────────────────────────────────────────────────────────┘
                                                   │
                        ┌──────────────────────────┴──────────────────────────┐
                        ▼                                                     ▼
        ContentTypeWriter (existing, CNB)                    XnbAssetWriter (NEW, pipeline layer)
                        │                                                     │
                        ▼                                                     ▼
                 Cnb::CnbWriter                                CNA::Content::Xnb::XnbWriter  (NEW)
                        │                                        ├── XnbTypeWriter registry (NEW)
                        ▼                                        └── XnbByteWriter            (NEW)
                     .cnb                                                     │
                                                                              ▼
                                                                            .xnb
```

### 5.1 Layer A — `CNA::Content::Xnb` (format layer; knows nothing about source assets)

`modules/content/include/CNA/Content/Xnb/`, `modules/content/src/XnbWrite/`.

| Type | Purpose |
|---|---|
| `XnbByteWriter` | checked little-endian primitive emitter: `WriteByte/Int16/UInt16/Int32/UInt32/Int64/UInt64/Single/Double/Boolean/Char/String/7BitEncodedInt/Bytes`. Buffer-backed, deterministic, bounded. |
| `XnbWriteLimits` | the write-side mirror of `XnbReadLimits`; defaults chosen so CNA can always read back what it writes. |
| `XnbTargetPlatform` | strongly typed enum over the platform identifier byte. |
| `XnbFileOptions` | `{platform, version, profile (Reach/HiDef), compression}`. |
| `XnbTypeWriter` | abstract: `TargetTypeName()`, `RuntimeReaderName()`, `TypeVersion()`, `IsValueType()`, `Write(XnbWriter&, const std::any&)`. |
| `XnbTypeWriterT<T>` | typed convenience base (`Write(XnbWriter&, const T&)`). |
| `XnbTypeKey<T>` | trait mapping a C++ type to its stable serialized .NET type name — the RTTI-free registry key. |
| `XnbTypeWriterRegistry` | explicit freeze-then-use registry keyed by serialized type name; `RegisterBuiltInXnbTypeWriters()` for the stock set. |
| `XnbWriter` | the object-graph serializer: owns the type-writer table, shared-resource table, object dispatch, root, header patch-up. XNA's `ContentWriter`. |
| `XnbSharedResourceHandle` | opaque token returned by `WriteSharedResource()`. |
| `XnbWriteException` | one exception type for every write-side failure. |

Two-pass emission (required because the type table precedes the body but is only known after it):

1. Serialize the root object and every shared resource into a body buffer, registering type-writer
   table entries and shared resources as they are first used.
2. Emit header + type table + shared-resource count, then the body, then patch `totalLength`.

### 5.2 Layer B — `CNA::Content::Pipeline` XNB output route (additive)

| Type | Purpose |
|---|---|
| `ContentOutputFormat` | `{ Cnb, Xnb }` |
| `XnbWriteResult` | `{bytes, rootReaderName, platform, version, compression, additionalOutputs}` |
| `XnbAssetWriter` | abstract: `Identity()`, `InputType()` (stable processed type), `RootReaderName()`, `Write(const ContentValue&, const XnbFileOptions&, const std::string& logicalName)` |
| `ContentPipelineRegistry::RegisterXnbWriter/ResolveXnbWriter` | same explicit selection rules as the CNB writers |
| `ContentBuildRequest::outputFormat`, `::xnbOptions` | request-level selection; default `Cnb` keeps every existing caller byte-identical |
| `ContentBuildResult::outputFormat`, `::xnbOutput` | populated only for `Xnb` |

Built-in `XnbAssetWriter`s reuse the **already-registered** importers and processors — one per
processed stable type. No importer or processor is duplicated, modified, or forked.

### 5.3 Layer C — CLI

`cna-content build … --format cnb|xnb` (default `cnb`), plus `--xnb-platform`, `--xnb-version`,
`--xnb-profile`. Output extension follows the format. The build manifest records the format so a
format switch correctly invalidates cached output.

### 5.4 Naming decision

The repository's established build-time namespace is `CNA::Content::Pipeline` with CNA-flavoured
component names (`ContentImporter`, `ContentProcessor`, `ContentTypeWriter`). This plan **follows
that**, and does **not** introduce a competing `Microsoft::Xna::Framework::Content::Pipeline`
namespace, for three reasons: (a) the XNA pipeline is a *build-time* assembly whose API is
reflection- and attribute-driven and does not translate to C++ without distortion; (b) CNA already
shipped and documented the CNA-flavoured names through `CP-001`…`CP-063`; (c) `CLAUDE.md`'s
namespace rule protects the *runtime* XNA API surface, which this plan does not touch. The XNA↔CNA
name mapping is documented in §6 and in `docs/xna-content-pipeline.md`.

---

## 6. Mapping of XNA C# pipeline concepts to CNA C++ equivalents

| XNA 4.0 concept | CNA C++ equivalent | Status |
|---|---|---|
| `ContentImporter<T>` | `CNA::Content::Pipeline::ContentImporter` + `ContentValue` stable type | exists (CP) |
| `ContentImporterAttribute` (extensions, default processor) | `SourceExtensions()`, `OutputTypes()` + registry routing | exists (CP) |
| `ContentImporterContext` | `ContentImporterContext` | exists (CP) |
| `ContentProcessor<TIn,TOut>` | `ContentProcessor` with stable `InputType()`/`OutputType()` | exists (CP) |
| `ContentProcessorContext` | `ContentProcessorContext` | exists (CP) |
| processor properties / `OpaqueDataDictionary` | `ContentProcessorParameters` (typed) | exists (CP) |
| `ContentBuildLogger` | `ContentBuildLogger` + `ContentLogMessage` | exists (CP) |
| `ContentIdentity` | `ContentLogMessage{source, logicalName, stage, component}` + `ContentPipelineError` | exists (CP), deliberately not a separate object |
| `ContentItem` (name + opaque data) | `ContentValue` + `ContentProcessorParameters` | exists (CP) |
| `ExternalReference<T>` | `RuntimeContentReference` (build graph) + `XnbWriter::WriteExternalReference()` (wire) | NEW wire side |
| `ContentTypeWriter<T>` | **two** roles, deliberately split: `XnbTypeWriter` (format/wire) and `XnbAssetWriter` (pipeline/asset) | NEW |
| `ContentTypeWriter.GetRuntimeReader()` | `XnbTypeWriter::RuntimeReaderName()` | NEW |
| `ContentTypeWriter.TypeVersion` | `XnbTypeWriter::TypeVersion()` | NEW |
| `ContentWriter` | `CNA::Content::Xnb::XnbWriter` | NEW |
| `ContentCompiler` | `RunContentCompiler()` / `cna-content` | exists (CP) |
| `TargetPlatform` | `XnbTargetPlatform` | NEW |
| `GraphicsProfile` (Reach/HiDef header bit) | `XnbGraphicsProfile` in `XnbFileOptions` | NEW |
| `TextureContent` / `Texture2DContent` / `TextureCubeContent` / `Texture3DContent` | `Cnb::CnbTextureData` (+ `XnbTextureKind`) | exists, reused |
| `MipmapChain` | `CnbTextureRepresentation::levels` (face-major, then mip) | exists, reused |
| `BitmapContent` / `PixelBitmapContent<T>` | `Pipeline::ImportedImage` (RGBA8) + `CnbTextureFormat` representations | exists, reused |
| `FontDescription` / `SpriteFontContent` | `Cnb::CnbSpriteFontData` | exists, reused |
| `AudioContent` | `Cnb::CnbSoundEffectData` / `Import::ImportedSound` | exists, reused |
| `SongContent` / `VideoContent` | `Cnb::CnbSongData` / `Cnb::CnbVideoData` | exists, reused |
| `NodeContent` / `MeshContent` / `GeometryContent` | `Cnb::CnbModelData` / `CnbModelV2Data` | exists, reused |
| `MaterialContent` / `BasicMaterialContent` / `EffectMaterialContent` | `CnbModel*` material records + `XnbBasicEffectData`-shaped writers | partial, Phase 7 |
| `CurveContent` | `Microsoft::Xna::Framework::Curve` | exists, reused |
| `ContentSerializerAttribute` / `ReflectiveWriter` | **not reproduced** — C++ has no runtime reflection; the reader side already resolves this with `ReflectiveTypeReaderBuilder<T>`, and the writer side offers an explicit field-list builder instead (Phase 8, optional) | documented omission |
| MSBuild `.contentproj` | `.cna-content.json` (already exists); optional read-only `.contentproj` ingestion evaluated in Phase 10 | evaluated |

**Deliberate omissions** (recorded, not accidental): `ContentBuildLogger.LogImportantMessage`
severity tiers beyond info/warning/error; `ContentProcessorContext.Convert` (superseded by direct
processor composition); `IContentProcessor` non-generic reflection surface; attribute-driven
discovery (CNA registers explicitly, by design — see `ContentPipelineExtensionApiIsExperimental`).

---

## 7. XNB writer design

### 7.1 Specification sources

1. **“Microsoft XNA Game Studio 4.0 — Compiled (XNB) Content Format”**, the official Microsoft
   format document published in the XNAGameStudio archive
   (`Samples/XNA_XNB_Format/XNB Format.docx`). This is a *published format specification*, on the
   allowed-source list, and is the primary normative reference for every layout in §7.3.
2. **CNA's own reader** (`CNA::Internal::Xnb`, `Microsoft::Xna::Framework::Content::ContentReader`)
   — the second normative source, and the one that decides ties, because CNA's output must be
   loadable by CNA.
3. Independently produced `.xnb` fixtures under `tests/assets/xnb/…`, used strictly as **black-box**
   behavioural fixtures.

No prohibited source (MonoGame/FNA content-building implementation code, decompiled Microsoft
assemblies) is consulted. See §18.

### 7.2 Container

```text
offset  size  field
0       3     'X' 'N' 'B'
3       1     target platform byte
4       1     format version (5; 4 accepted on read)
5       1     flags: 0x01 = HiDef profile, 0x80 = compressed
6       4     UInt32 total file size on disk, including this 10-byte header
[10     4     UInt32 decompressed size — only when the compressed flag is set]
…             body: type-reader table, shared-resource count, root object, shared resources
```

Body (uncompressed case, which is what this plan implements first):

```text
7BitEncodedInt  type reader count
repeat {
    String      assembly-qualified ContentTypeReader name
    Int32       reader version (0 for every writer here)
}
7BitEncodedInt  shared resource count
Object          root asset
repeat { Object shared resource }
```

### 7.3 Value forms

| Form | Encoding |
|---|---|
| Raw value | the type's payload with no type metadata; cannot be null; used for value types |
| Polymorphic object | `7BitEncodedInt typeId`; `0` = null; else `typeId-1` indexes the type table, then the raw payload |
| Shared resource | `7BitEncodedInt resourceId`; `0` = null; else `resourceId-1` indexes the shared-resource list serialized after the root |
| `Object? T` | raw if `T` is a value type, polymorphic if `T` is a reference type |

`7BitEncodedInt` is .NET's `BinaryWriter.Write7BitEncodedInt`: little-endian base-128 groups, high
bit set on every non-final byte. `String` is a `7BitEncodedInt` UTF-8 **byte** count followed by
the bytes. `Char` is a single UTF-8-encoded code point (1–4 bytes).

### 7.4 Determinism

* No clock, no random source, no pointer value, no hash-order iteration ever influences output.
* Type-table order is **first-use order** during body serialization — a pure function of the object
  graph, hence reproducible.
* Shared-resource order is first-registration order, likewise pure.
* Dictionaries are written in the canonical order of the source container (CNA's canonical types
  use ordered containers).
* Floats go through `std::bit_cast` to a fixed-width integer and are decomposed byte by byte, so
  the output does not depend on host endianness.

### 7.5 Robustness (write-side mirror of the reader's hardening)

* Every count/size is range-checked before it is emitted and before any buffer is grown.
* All size arithmetic goes through checked helpers (`XnbCheckedAdd`/`XnbCheckedMultiply`), reusing
  the existing `CNA::Internal::Xnb::XnbArithmetic` discipline.
* Narrowing is explicit and validated (`std::int32_t` file size, `std::uint32_t` counts).
* Recursion depth for nested objects is bounded (`XnbWriteLimits::maxObjectDepth`).
* The writer refuses to emit a file it can prove CNA's own reader would reject (over-limit
  collection counts, an over-limit string, a total size past `std::int32_t`).

### 7.6 Compression

Phase 8, gated on provenance. Uncompressed output stays a first-class, permanently supported mode.
LZX **compression** (as opposed to CNA's already-implemented decompression) is a genuinely separate
algorithm; it will only be implemented if it can be written from public specification without
touching a prohibited implementation. Not required for interoperability: XNA/FNA/MonoGame runtimes
all read uncompressed `.xnb` unconditionally.

---

## 8. ContentTypeWriter architecture

### 8.1 Requirements

* Extensible without editing a central switch.
* No RTTI-only dispatch.
* Deterministic selection, diagnosable failure.
* A writer must be able to declare the runtime reader name it binds to, and its type version.

### 8.2 Design

```cpp
namespace CNA::Content::Xnb
{
    class XnbTypeWriter
    {
    public:
        virtual ~XnbTypeWriter() = default;
        [[nodiscard]] virtual std::string TargetTypeName()   const = 0;  // serialized .NET type
        [[nodiscard]] virtual std::string RuntimeReaderName() const = 0; // table entry
        [[nodiscard]] virtual std::int32_t TypeVersion()     const = 0;  // table entry version
        [[nodiscard]] virtual bool IsValueType()             const = 0;  // Object? T resolution
        virtual void Write(XnbWriter& output, const std::any& value) const = 0;
    };

    template<typename T> struct XnbTypeKey;              // trait: stable serialized type name
    template<typename T> class XnbTypeWriterT : public XnbTypeWriter { … };
}
```

* `XnbTypeKey<T>::Name` is the registry key. Built-in specializations cover every primitive, math
  and asset type; a game adds one specialization plus one `XnbTypeWriterT<T>` subclass for a custom
  type. That is a **typed** registry: the compiler resolves `T` → key, the registry resolves key →
  writer. No `type_index`, no `dynamic_cast`, no name mangling in the lookup path.
* `XnbWriter::WriteObject<T>(v)` uses the trait. `XnbWriter::WriteObjectDynamic(typeName, any)` is
  the type-erased escape hatch for genuinely polymorphic slots (a `Model`'s `Effect`, a
  `Dictionary<String,Object>` value), mirroring the reader's `std::any`-returning `ReadObject()`.
* `XnbTypeWriterRegistry` follows `ContentPipelineRegistry`'s proven shape: configure, `Freeze()`,
  then concurrent read-only use.

### 8.3 Built-in writer set (target)

| Group | Writers |
|---|---|
| Primitives | `Byte`, `SByte`, `Int16`, `UInt16`, `Int32`, `UInt32`, `Int64`, `UInt64`, `Single`, `Double`, `Boolean`, `Char`, `String` |
| System | `Enum<T>`, `Nullable<T>`, `Array<T>`, `List<T>`, `Dictionary<K,V>`, `TimeSpan`, `DateTime`, `Decimal`, `ExternalReference` |
| Math | `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`, `Curve` |
| Graphics | `Texture2D`, `Texture3D`, `TextureCube`, `IndexBuffer`, `VertexBuffer`, `VertexDeclaration`, `Effect`, the 5 stock effects, `EffectMaterial`, `SpriteFont`, `Model` |
| Media | `SoundEffect`, `Song`, `Video` |

---

## 9. ContentImporter architecture

**Unchanged.** `CNA::Content::Pipeline::ContentImporter` already provides identity, extension
routing, declared output types and a scoped context with dependency capture and source-root
containment. The XNB route reuses every existing importer as-is:

| Source | Importer | Produces |
|---|---|---|
| `.png/.jpg/.bmp/.tga/…` | `ImageImporter` | `ImportedImage` |
| `.wav` | sound importer | `ImportedSound` |
| `.cnj` | `.cnj` importers | texture / curve / sprite-font / animation values |
| `.gltf/.glb` | glTF importer | model bundle |
| `.xnb` | `XnbImporter` | canonical value (enables `.xnb` → `.xnb` normalization) |

New importer work in this plan is limited to what a new *output* type demands (Phase 4: a font
source route), and is additive.

---

## 10. ContentProcessor architecture

**Unchanged**, and deliberately so: a processor's job (colour keying, premultiplication, mip
generation, format selection, resampling, sound conversion, model transformation) is
format-agnostic. Forking processors per output format would be the single worst architectural
mistake available here.

One addition: a processor may need to know the target format when a *policy* genuinely differs
(e.g. XNB `SurfaceFormat` has no `Bc7`, so a CNB-preferred representation must degrade). This is
handled **in the writer**, not the processor: `XnbAssetWriter` selects the best XNB-representable
representation from the canonical value's representation list and fails with a precise diagnostic
if none exists. Processors stay unaware of the output format.

---

## 11. Intermediate content object model

**Decision: reuse CNA's existing canonical processed types as the intermediate content object
model.** Do not build a second, XNA-named parallel hierarchy.

Rationale: those types already are the pipeline's neutral currency; they are already produced by
every importer/processor, already round-tripped by CNB, already covered by tests, and already the
decode target of the XNB reader (`XnbCanonicalData`). Introducing `Texture2DContent`,
`MipmapChain`, `PixelBitmapContent<T>` etc. as separate classes would create a duplicate object
model whose only function is to be converted to and from the real one — precisely the
"giant half-implemented API shell" the task forbids.

The XNA↔CNA correspondence is documented in §6 and in `docs/xna-content-pipeline.md`, so a
migrating XNA developer can find the concept they know.

Where the canonical type is genuinely missing information the XNB format needs, the gap is closed
in the smallest possible way:

- [ ] **XNAP-G1** `CnbTextureData` carries `CnbTextureFormat`, not XNA `SurfaceFormat`. The writer
      needs a total, explicit, well-diagnosed mapping (`Rgba8` → `Color`, `Bc1/2/3` → `Dxt1/3/5`,
      everything else → refuse with a named reason). Recorded as a writer-owned mapping table.
- [ ] **XNAP-G2** XNB `Texture2D` stores level bytes in a **BGRA-independent, XNA `Color`** layout,
      which is R,G,B,A byte order — identical to `Rgba8`. Verified against the reader
      (`Texture2DReader` builds `Color(bytes[o+0..3])`). No swizzle needed; assert it in a test.
- [ ] **XNAP-G3** `SpriteFont`'s XNB payload requires an ascending character map; `CnbSpriteFontData`
      already guarantees this. Re-validate at write time rather than trusting the invariant.

---

## 12. External asset decoding / import dependencies

No new external dependency is introduced by this plan.

| Need | Provider | Already present? |
|---|---|---|
| PNG/JPG/BMP/TGA decode | vendored `stb_image.h` via `CNA::Internal::Graphics::ImageLoader` | yes |
| WAV decode / ADPCM | SDL3 WAV path + CNA's shared WAV import | yes |
| glTF import | vendored `cgltf.h` + `GltfImportCore` | yes |
| DXT decompression | `CNA::Internal::Graphics::DxtUtil` | yes |
| DXT **compression** | **absent** | Phase 3 records this: CNA can write `Color` textures today; emitting `Dxt1/3/5` requires a block compressor that does not exist in the tree. Deferred with an explicit task, not silently. |
| Font rasterization (TTF → glyph atlas) | see Phase 4 | evaluated there |
| LZX compression | absent | Phase 8, provenance-gated |

---

## 13. XNB compatibility requirements

Normative, from §7:

- [ ] Magic `'X','N','B'`, then a valid platform byte.
- [ ] Version byte `5` by default (`4` writable on request; both are reader-accepted).
- [ ] Flags: `0x01` set for HiDef, clear for Reach; `0x80` clear while uncompressed.
- [ ] `UInt32` total file size, counted **including** the 10-byte header, exactly equal to the
      emitted file length.
- [ ] Type-reader table: 7-bit count; per entry a 7-bit-length-prefixed UTF-8 assembly-qualified
      name and an `Int32` version.
- [ ] Reader names must be spellings CNA's `NormalizeXnbTypeReaderName()` maps to a registered
      reader, and that a real XNA/FNA runtime resolves. Generic names use the
      ``Name`N[[Arg1],[Arg2]]`` form.
- [ ] 7-bit shared-resource count immediately after the table.
- [ ] Root object in polymorphic form.
- [ ] Shared resources serialized after the root, in index order.
- [ ] `typeId`/`resourceId` `0` means null.
- [ ] Value types written raw; reference types written polymorphically.
- [ ] Strings are 7-bit-length-prefixed UTF-8 with no terminator or BOM.
- [ ] `Char` is UTF-8, 1–4 bytes, never UTF-16.
- [ ] Collections use a **`UInt32`** count (not 7-bit) — spec §Array/List/Dictionary.
- [ ] `Nullable<T>` is `Boolean` then, only when true, the value.
- [ ] Enum payload width follows the enum's underlying type (32-bit for every enum CNA writes).

---

## 14. XNA 4.0 compatibility requirements

- [ ] Emitted reader names are the real XNA 4.0 names (§8.3), not CNA-invented ones.
- [ ] `SurfaceFormat` values written as the XNA 4.0 numbering (`0 = Color … 19 = HdrBlendable`).
- [ ] `VertexElementFormat` / `VertexElementUsage` written as the XNA 4.0 numbering.
- [ ] `CurveLoopType` / `CurveContinuity` written as the XNA 4.0 numbering.
- [ ] `Model` bone references use the XNA size rule: `Byte` when bone count < 255, else `UInt32`.
- [ ] `SoundEffect` format block is a real `WAVEFORMATEX` (little-endian on every platform CNA
      writes; the Xbox `'x'` big-endian variant is refused rather than guessed).
- [ ] `SpriteFont` field order exactly as §7 (`Texture, Glyphs, Cropping, CharacterMap,
      LineSpacing, Spacing, Kerning, DefaultCharacter`).
- [ ] A CNA-written `.xnb` loads through CNA's own unmodified `ContentManager::Load<T>()`.

---

## 15. Relationship between XNB and CNB

**Shared:** source assets, importers, processors, canonical/intermediate content objects, the build
graph, dependency tracking, the manifest, the CLI, atomic publication, determinism discipline.

**Separate, permanently:** the serializers. `Cnb::CnbWriter` and `Xnb::XnbWriter` share no code and
no data layout. CNB keeps its chunked, CRC-protected, versioned, zstd-capable container and every
frozen-schema guarantee. XNB keeps its XNA-mandated layout.

```text
                          canonical / processed content value
                                        │
                     ┌──────────────────┴──────────────────┐
                     ▼                                     ▼
             ContentTypeWriter                       XnbAssetWriter
                     │                                     │
                Cnb::CnbWriter                     Xnb::XnbWriter
                     ▼                                     ▼
                   .cnb                                  .xnb
```

Explicitly **not** done: no CNB chunk, schema version, asset-type ID, or byte layout changes; no
XNB concept (type-reader table, shared-resource fixups, 7-bit ints, `WAVEFORMATEX`) leaks into CNB;
no CNB concept (CRC32C, chunk framing, XREF table) leaks into XNB. A capability one format has and
the other lacks (CNB's multi-representation textures; XNB's shared-resource graph) is resolved in
the writer for that format, with a diagnostic, never by weakening the other format.

---

## 16. Testing strategy

Tests live in `modules/content/tests/CNA/Content/Xnb/` and
`modules/content/tests/CNA/Content/Pipeline/`, GoogleTest, mirroring the namespace path.

**Tier 1 — format unit tests** (`XnbByteWriterTests`, `XnbWriterTests`)
- [ ] header field-by-field; version 4 and 5; every accepted platform byte; Reach/HiDef flag
- [ ] declared total size equals the real byte count, including for an empty body
- [ ] 7-bit encoded integers: `0, 1, 0x7F, 0x80, 0x3FFF, 0x4000, 0x1FFFFF, 0x200000,
      0x0FFFFFFF, 0x7FFFFFFF, -1` and byte-exact expected encodings; round trip through
      `BinaryReader::Read7BitEncodedInt`
- [ ] strings: empty, ASCII, multi-byte UTF-8, embedded NUL, at and past the size limit
- [ ] type-writer table: first-use ordering, de-duplication, generic name spelling, version field
- [ ] null object (`typeId 0`), non-null dispatch, unregistered type → precise failure
- [ ] shared resources: registration, index assignment, `0` for null, resources referencing
      resources, a resource referencing itself
- [ ] overflow/limit refusals: oversized collection, oversized string, file past `int32`,
      recursion past `maxObjectDepth`
- [ ] byte-for-byte determinism: writing the same graph twice yields identical bytes

**Tier 2 — reader/writer round trips** (`XnbRoundTripTests`)
- [ ] every primitive, math type, `Curve`, `TimeSpan`, `DateTime`, `Decimal`
- [ ] arrays, lists, dictionaries, nullable, enum
- [ ] `Write(X)` → `DecodeXnbCanonicalAsset` → semantic equality with `X`, for `Texture2D`,
      `Texture3D`, `TextureCube`, `SpriteFont`, `SoundEffect`, `Song`, `Video`, `Curve`
- [ ] `Read(fixture)` → `Write` → `Read` produces an identical canonical value for every real
      external fixture under `tests/assets/xnb/` that decodes to a supported root (idempotence)

**Tier 3 — end-to-end pipeline**
- [ ] `PNG → ImageImporter → TextureProcessor → XnbAssetWriter → .xnb → ContentManager::Load<Texture2D>()`
      with pixel equality against the source image
- [ ] the same for `SpriteFont`, `SoundEffect`, `Curve`
- [ ] CLI: `cna-content build … --format xnb` produces `.xnb` files; manifest invalidation on a
      format switch; non-zero exit on failure; `--explain` output

**Tier 4 — adversarial**
- [ ] the writer never produces a file its own reader rejects, for every generated case
- [ ] a fuzz-style loop over randomized canonical values (bounded, deterministic seed) asserting
      write → read → compare

**No test may require a proprietary XNA SDK installation.**

---

## 17. Interoperability strategy

1. **Primary gate (automatable here):** CNA writes → CNA reads, semantically equal. Necessary, and
   the only gate that runs in CI on this machine.
2. **Structural gate:** a CNA-written file and a real, externally produced fixture for the same
   asset must agree field-for-field after decoding, and the CNA-written file's header/table/body
   must satisfy §13's checklist mechanically (a dedicated `XnbConformanceTests` suite that parses
   CNA's own output with the *container* parser rather than the high-level reader).
3. **External runtime gate (deferred, scripted):** no XNA 4.0/MonoGame/FNA runtime is installable
   on this machine, and none may be vendored. Deliverable instead: `tools/xnb/` scripts plus
   `docs/xnb-interoperability.md` describing exactly how to validate CNA-produced `.xnb` files
   against a real runtime, and a generated fixture set committed under `tests/assets/xnb/cna/`
   with manifests, so the check is a matter of running the script when such a runtime is available.
4. **Non-goal:** byte-for-byte identity with XNA-produced files. Legal ordering differences
   (type-table order, shared-resource order) are permitted by the format; semantic compatibility is
   the requirement.

---

## 18. Licensing and provenance rules

**Allowed and used:**
* The official Microsoft **“XNA Game Studio 4.0 Compiled (XNB) Content Format”** document
  (XNAGameStudio archive, `Samples/XNA_XNB_Format/XNB Format.docx`) — a published format
  specification. Facts from it are implemented; its prose is not copied into the repository.
* CNA's own XNB reader and CNB/CNJ implementations.
* Public Microsoft XNA API documentation, public type/member names.
* Existing legitimately available `.xnb` fixtures in this repository, used **black-box**.

**Not used, at all:**
* MonoGame Content Pipeline implementation source, in any form.
* FNA content-building implementation source.
* Decompiled Microsoft XNA pipeline or runtime assemblies.
* Test assets from XNA/MonoGame/FNA/Assimp whose licence has not been separately established.

**Gap procedure** (when a detail is not derivable from an allowed source): record it in §22 with an
`XNAP-G…` ID, derive it from CNA's reader if possible, otherwise cover it with a black-box fixture
test, otherwise implement the smallest reasonable independent behaviour and document it. Never
resolve it by consulting a prohibited implementation.

**Open provenance items:**
- [ ] **XNAP-G4** LZX *compression* — Phase 8. Only from public specification; otherwise the
      feature stays unimplemented and uncompressed output remains the supported mode.
- [ ] **XNAP-G5** Any font source used in a `SpriteFont` test must have an explicit redistributable
      licence recorded in a manifest, or the test must synthesize its glyph atlas instead.

---

## 19. Implementation phases

### Phase 1 — Plan and audit
- [x] **XNAP-001** Audit the tree; write this plan.

### Phase 2 — XNB write infrastructure
- [x] **XNAP-002** `XnbByteWriter` + `XnbWriteLimits` + checked arithmetic; unit tests.
- [x] **XNAP-003** `XnbFileOptions`, `XnbTargetPlatform`, `XnbGraphicsProfile`, header emission
      with exact `totalLength`; unit tests.
- [x] **XNAP-004** `XnbTypeWriter`, `XnbTypeWriterT<T>`, `XnbTypeKey<T>`, `XnbTypeWriterRegistry`.
- [x] **XNAP-005** `XnbWriter`: two-pass body/table emission, object dispatch, null, shared
      resources and fixup ordering, `ExternalReference`, bounded recursion; unit tests.

### Phase 3 — Basic content
- [x] **XNAP-006** Primitive and `String`/`Char` writers; round-trip tests.
- [x] **XNAP-007** Math value-type writers (`Vector2/3/4`, `Matrix`, `Quaternion`, `Color`,
      `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`).
- [x] **XNAP-008** `Array<T>`, `List<T>`, `Dictionary<K,V>`, `Nullable<T>`, `Enum<T>`.
- [x] **XNAP-009** `TimeSpan`, `DateTime`, `Decimal`, `Curve`.

### Phase 4 — Texture2D end to end
- [ ] **XNAP-010** `SurfaceFormat` ↔ `CnbTextureFormat` mapping table with total, diagnosed
      coverage (`XNAP-G1`).
- [ ] **XNAP-011** `Texture2D`/`Texture3D`/`TextureCube` XNB type writers.
- [ ] **XNAP-012** Pipeline layer: `ContentOutputFormat`, `XnbWriteResult`, `XnbAssetWriter`,
      registry and `ContentPipeline::Build()` route.
- [ ] **XNAP-013** `Texture2DXnbAssetWriter` + registration; PNG → `.xnb` → `ContentManager::Load`
      end-to-end test.

### Phase 5 — SpriteFont
- [ ] **XNAP-014** `SpriteFont` XNB type writer (nested `Texture2D` object, three `List` objects,
      `Nullable<Char>`), round trip against the canonical decoder.
- [ ] **XNAP-015** `SpriteFontXnbAssetWriter` + `.cnj` sprite-font source end-to-end test.

### Phase 6 — Audio and media
- [ ] **XNAP-016** `SoundEffect` XNB type writer (`WAVEFORMATEX` block, samples, loop points,
      duration) + asset writer + WAV → `.xnb` end-to-end test.
- [ ] **XNAP-017** `Song` and `Video` writers (external streaming filename + metadata).

### Phase 7 — Model and effects
- [ ] **XNAP-018** `VertexDeclaration`, `VertexBuffer`, `IndexBuffer` writers.
- [ ] **XNAP-019** `Model` writer: bone table, bone-reference width rule, mesh/mesh-part graph,
      shared `VertexBuffer`/`IndexBuffer`/`Effect` resources.
- [ ] **XNAP-020** Stock-effect writers (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
      `EnvironmentMapEffect`, `SkinnedEffect`) and `EffectMaterial`.
- [ ] **XNAP-021** General `Effect` writer (opaque compiled bytecode blob). **Serialization only** —
      this plan does not claim HLSL/FX compilation. Documented separately.
- [ ] **XNAP-022** `ModelXnbAssetWriter` + glTF → `.xnb` end-to-end test.

### Phase 8 — Compression (provenance-gated)
- [ ] **XNAP-023** Investigate LZX compression provenance; implement only if clean. Uncompressed
      output remains supported regardless.

### Phase 9 — Tooling and documentation
- [ ] **XNAP-024** `cna-content build … --format xnb` (+ `--xnb-platform/--xnb-version/
      --xnb-profile`), manifest format awareness, CLI tests.
- [ ] **XNAP-025** `docs/xna-content-pipeline.md` (architecture, concept mapping, extension
      points, capability matrix); update `docs/xnb-content-pipeline-support.md`'s permanent-scope
      statement (`F5`); update `docs/content-pipeline.md`, `README.md`, `xnb.md`,
      `plans/plan_xnb.md` cross-references.
- [ ] **XNAP-026** `docs/xnb-interoperability.md` + generated fixture corpus + validation script.

### Phase 10 — Evaluated, decided later
- [ ] **XNAP-027** Custom-type writer support (an explicit field-list builder mirroring
      `ReflectiveTypeReaderBuilder<T>`).
- [ ] **XNAP-028** Evaluate read-only `.contentproj` ingestion. Default answer: **no** — XML/MSBuild
      must not become a mandatory architectural dependency; `.cna-content.json` already covers the
      need. Revisit only with a concrete migration request.
- [ ] **XNAP-029** Runtime/pipeline library split so a game links only the reader
      (`F3`; owned by `plans/plan_content_pipeline.md`, cross-referenced here).

---

## 20. Risks and unknowns

| # | Risk | Mitigation |
|---|---|---|
| R1 | Reader/writer asymmetry: CNA's reader may accept a spelling real XNA does not, so a round trip that passes locally could still be non-conformant | §17 gate 2 checks CNA's output against the *specification checklist* mechanically, not just against the reader; §13/§14 checklists are explicit |
| R2 | No XNA 4.0 runtime available on this machine | Scripted, documented deferred validation (§17.3); fixtures committed so the check is one command later |
| R3 | DXT compression absent, so texture output is `Color` only | Explicit, documented limitation with a named task; `Color` is fully valid XNB |
| R4 | LZX compression provenance | Phase 8 gate; uncompressed output is complete and sufficient |
| R5 | `Model` shared-resource graph is the most intricate part of the format | Phase 7; the reader's `ModelReader` + canonical `XnbModelData` give an exact inverse target and a round-trip oracle |
| R6 | Scope creep into a parallel XNA-named object model | §11 decision: reuse canonical types; document the mapping |
| R7 | Regressing the existing CNB pipeline | Every pipeline change is additive with a `Cnb` default; the existing suites must stay green |
| R8 | `Effect` bytecode compatibility (XNA D3D9 Effect Framework) is out of CNA's control | `XNAP-021` serializes an opaque blob and says so; no claim of shader compilation |

---

## 21. Completion criteria

This plan is complete when **all** of the following hold:

1. `CNA::Content::Xnb` provides a documented, tested XNB writer that emits a spec-conformant
   uncompressed container: header, type-writer table, shared resources, root object, polymorphic
   and raw value forms, 7-bit integers, strings, null representation.
2. A typed, RTTI-free, extensible `XnbTypeWriter` registry exists, with built-in writers for every
   primitive, system, math and media type in §8.3, and for `Texture2D`/`Texture3D`/`TextureCube`,
   `SpriteFont`, `SoundEffect`, `Song`, `Video`, `Curve`.
3. The pipeline can build `source → importer → processor → XNB` for at least `Texture2D`,
   `SpriteFont`, `SoundEffect` and `Curve`, through the same registry the CNB route uses.
4. `cna-content build … --format xnb` works end to end, with dependency tracking, deterministic
   output, useful diagnostics and non-zero failure exit codes.
5. Every produced `.xnb` loads correctly through CNA's own unmodified runtime `ContentManager`.
6. Round-trip and conformance tests from §16 pass, and the pre-existing content suites stay green.
7. CNB's format, guarantees and tests are unchanged.
8. Documentation matches actual capability, including the corrected permanent-scope statement, an
   explicit capability/limitation matrix and the provenance record.
9. This plan's task log accurately reflects what landed, what was deferred and why.

**Known limitations accepted at completion** (each must be documented, not hidden): no DXT/BC
compression on write; no LZX/LZ4 compression on write; no HLSL/FX compilation; `Model` and
stock-effect writing status as recorded in §22; no automated external-runtime interoperability run
on this machine; runtime and pipeline still share one library (`F3`).

---

## 22. Task log

Status legend: `[ ]` open · `[x]` complete · `[~]` partial (scope recorded) · `[⏸]` deferred.

| Task | Status | Notes |
|---|---|---|
| XNAP-001 | [x] | Audit + this plan. |
| XNAP-002 | [x] | `XnbByteWriter`, `XnbWriteLimits`, `XnbWriteException`, checked add/multiply. |
| XNAP-003 | [x] | `XnbFileOptions`, `XnbTargetPlatform` (7 writable identifiers; Xbox 360 excluded because its `SoundEffect` block is big-endian and unverifiable here), `XnbGraphicsProfile`, header emission with a patched total size. |
| XNAP-004 | [x] | `XnbTypeWriter`, `XnbTypeWriterT<T>`, `XnbTypeKey<T>`, `XnbTypeWriterRegistry` (configure → freeze → lock-free lookup). |
| XNAP-005 | [x] | `XnbWriter`: two-pass table/body emission, polymorphic/raw/`Object? T` forms, an empty `std::any` as .NET null, shared resources with caller-supplied identity keys, `ExternalReference`, bounded depth. |
| XNAP-006 | [x] | 13 primitive writers, round-tripped through the runtime readers at their extremes. |
| XNAP-007 | [x] | 13 math writers (`Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`). |
| XNAP-008 | [x] | Closed generic `List<T>`, `T[]`, `Dictionary<K,V>`, `Nullable<T>`, `Enum<T>` writers, registered per combination exactly as the reader side is. |
| XNAP-009 | [x] | `TimeSpan`, `DateTime`, `Decimal`, `Curve`. Written from the sharp-runtime types the readers produce, so the two sides are exact inverses; `DateTimeKind` is emitted as `Unspecified` because CNA's `System::DateTime` does not carry one and the reader already discards it. |
