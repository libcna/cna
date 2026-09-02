# The CNA XNA 4.0 Content Pipeline and `.xnb` writing

CNA compiles source assets into `.xnb` files natively, in C++, with no XNA Game Studio, no
MonoGame and no MSBuild anywhere in the process:

```text
player.png ──▶ ImageImporter ──▶ TextureProcessor ──▶ canonical texture ──▶ XnbWriter ──▶ player.xnb
                                                                    └────▶ CnbWriter ──▶ player.cnb
```

```bash
cna-content build Content --output bin --format xnb
```

```cpp
Texture2D player = content.Load<Texture2D>("player");
```

This document is the capability boundary and the extension guide. The engineering record is
[`plans/plan_xnapipeline.md`](../plans/plan_xnapipeline.md); the **read** side is documented
separately in [`docs/xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md), and the
CNB-targeted build system this extends in [`docs/content-pipeline.md`](content-pipeline.md).

---

## 1. Architecture

Three layers, deliberately separated so that source decoding never leaks into serialization:

| Layer | Namespace | Location | Role |
|---|---|---|---|
| Format | `CNA::Content::Xnb` | `modules/content/{include/CNA/Content/Xnb,src/XnbWrite}` | Writes the `.xnb` container and object graph. Knows nothing about PNG, WAV or glTF. |
| Pipeline | `CNA::Content::Pipeline` | `modules/content/{include,src}/…/Pipeline` | Importers, processors, canonical content values, the build graph. Shared by both output formats. |
| Tool | — | `tools/content`, `cmake/ToolContentPipeline.cmake` | `cna-content`, `cna_add_content()`. |

The **importer and the processor are identical for both formats**. Only the serializer differs:

```text
                     source asset
                          │
                    ContentImporter          ← one per source kind, format-agnostic
                          │
                   ContentProcessor          ← policy: colour key, mips, resampling, conversion
                          │
              canonical content value        ← CnbTextureData, CnbSpriteFontData, Curve, …
                          │
        ┌─────────────────┴─────────────────┐
   ContentTypeWriter                  XnbAssetWriter
        │                                   │
   Cnb::CnbWriter                    Xnb::XnbWriter
        ▼                                   ▼
      .cnb                                .xnb
```

Forking processors per output format would be the single worst mistake available here, so a
format-specific limitation is resolved **in the writer**: the XNB texture writer, for instance,
picks an XNA-representable representation out of the canonical value and refuses with a named
reason when none exists. Processors never learn which format they are feeding.

### 1.1 The format layer

| Type | Role |
|---|---|
| `XnbByteWriter` | Checked, host-endianness-independent primitives: 7-bit encoded integers, UTF-8 strings and characters, little-endian numerics, raw payloads. |
| `XnbWriteLimits` / `XnbWriteException` | The write-side mirror of `XnbReadLimits`, so the writer refuses on the way out anything the reader would refuse on the way in. |
| `XnbFileOptions` | Platform byte, container version, Reach/HiDef profile, compression. Validated before a byte is produced. |
| `XnbTypeWriter` / `XnbTypeWriterT<T>` | Serializes one .NET type. Declares the runtime reader it binds to (XNA's `GetRuntimeReader()`), its type version, and whether the type is a value type. |
| `XnbTypeKey<T>` | Trait mapping a C++ type to its serialized .NET type name — the RTTI-free registry key. |
| `XnbTypeWriterRegistry` | Configure → `Freeze()` → concurrent read-only use. |
| `XnbWriter` | The object-graph serializer (XNA's `ContentWriter`): type-reader table, shared resources, object dispatch, external references, container header. |

`XnbWriter` emits in two passes because the type table precedes the body but is only known after
it. That also makes table order a pure function of the object graph, which is what lets the writer
promise **byte-identical output for an identical input**.

### 1.2 The pipeline layer

`ContentBuildRequest::outputFormat` selects `Cnb` (the default, and unchanged) or `Xnb`. For
`Xnb`, `ContentBuildResult::xnbOutput` carries the file; for `Cnb`, `::output` does, exactly as
before. `ContentPipelineRegistry` gained `RegisterXnbWriter()` / `ResolveXnbWriter()`, a selection
space of its own: a processed type may have a CNB writer, an `.xnb` writer, both, or neither, and
one never shadows the other.

---

## 2. Supported asset types

| Asset | Source route | Root reader written | Status |
|---|---|---|---|
| `Texture2D` | PNG/JPG/BMP/TGA/… → `ImageImporter` → `TextureProcessor` | `Texture2DReader` | ✅ Full, `SurfaceFormat.Color` |
| `Texture3D` | `.cnj` volume texture | `Texture3DReader` | ✅ Full, `SurfaceFormat.Color` |
| `TextureCube` | `.cnj` cube texture | `TextureCubeReader` | ✅ Full, six faces, `SurfaceFormat.Color` |
| `SpriteFont` | `.cnj` sprite font | `SpriteFontReader` | ✅ Full: atlas, glyphs, cropping, character map, kerning, spacing, nullable default character |
| `SoundEffect` | `.wav` → sound importer → `SoundEffectProcessor` | `SoundEffectReader` | ✅ PCM16 mono/stereo, `WAVEFORMATEX`, loop region, duration |
| `Curve` | `.cnj` curve | `CurveReader` | ✅ Full |
| `Song` | audio source | `SongReader` | ✅ Streaming file name plus the dispatched `Object: Int32` duration |
| `Video` | video source | `VideoReader` | ✅ All six fields in their dispatched `Object` form |
| `Model` | a canonical schema-2 model (e.g. an imported `.xnb`) | `ModelReader` | ✅ Full graph: bones, the bone-reference width rule, meshes, mesh parts and the shared `VertexBuffer`/`IndexBuffer`/`Effect` resources. ⚠️ The glTF route produces the frozen schema-1 carrier, which has no vertex declaration; see §6 |
| `VertexBuffer`, `IndexBuffer`, `VertexDeclaration` | inside a `Model` | matching readers | ✅ Full |
| `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect` | inside a `Model` | matching readers | ✅ Full, including their external texture references |
| `Effect` (compiled) | a caller-supplied bytecode blob | `EffectReader` | ⚠️ Serialization only — CNA does not compile HLSL/FX; see §6 |

Plain data types are supported as root assets and as members of any of the above: every primitive
(`Byte`…`String`), every XNA math value type (`Vector2/3/4`, `Matrix`, `Quaternion`, `Color`,
`Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`),
`TimeSpan`, `DateTime`, `Decimal`, and closed generic `List<T>`, `T[]`, `Dictionary<K,V>`,
`Nullable<T>` and `Enum<T>`.

---

## 3. Container and compatibility level

Written per the published Microsoft *XNA Game Studio 4.0 Compiled (XNB) Content Format*
specification (see §8):

| Field | What CNA writes |
|---|---|
| Magic | `'X'`, `'N'`, `'B'` |
| Platform | `w` Windows (default), `m` Windows Phone, `d` DesktopGL, `X` macOS, `l` Linux, `i` iOS, `a` Android |
| Version | 5 by default; 4 on request. Both are XNA 4.0-valid |
| Flags | bit `0x01` HiDef when requested, otherwise Reach; bit `0x80` always clear (uncompressed) |
| File size | `UInt32`, counted including the 10-byte header, always exactly the emitted length |
| Type-reader table | 7-bit count, then per entry a 7-bit-length-prefixed UTF-8 reader name and an `Int32` version of `0`. A reader outside `Microsoft.Xna.Framework` is assembly-qualified (`…Texture2DReader, Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, …`) and one inside it is not, matching real XNA content exactly, because a loading runtime resolves them with `Type.GetType()` |
| Shared resources | 7-bit count after the table; entries serialized after the root, referenced by 1-based index |
| Objects | 7-bit `typeId`; `0` = null; otherwise `typeId - 1` indexes the table |
| Value types | Written raw, with no type identifier, wherever the format specifies `Object? T` |
| Strings | 7-bit UTF-8 **byte** count then the bytes; no terminator, no BOM |
| Chars | One UTF-8 encoded character, 1–3 bytes; an unpaired surrogate is refused |
| Collections | `UInt32` count (not 7-bit), matching the specification |
| Enums | 32-bit underlying value |

Enumerations are written in the XNA 4.0 numbering: `SurfaceFormat` `0 = Color` … `19 = HdrBlendable`,
`CurveLoopType`, `CurveContinuity`. A CNA-only `SurfaceFormat` extension (`ColorBgraEXT`, `Bc7EXT`, …)
has no XNA identity and is refused rather than approximated.

**Xbox 360 (`'x'`) is deliberately not writable.** Its `SoundEffect` format block is big-endian and
CNA has no way to verify the result, so claiming the platform would be worse than declining it.

---

## 4. Using the tool

```bash
cna-content build <source-file-or-directory> -o <output> [--format cnb|xnb]
                  [--xnb-platform <name>] [--xnb-version 4|5] [--xnb-profile reach|hidef]
                  [--config <file>] [--workers <1..64>] [--explain] [--quiet]
cna-content clean <output-directory> [--quiet]
```

* `--format` defaults to `cnb`. A single-file build's output path must end in the selected
  format's extension.
* The `--xnb-*` options describe the container and are ignored for `cnb` output.
* The build manifest records the format, so **switching formats retires the previous artifact**
  rather than leaving a stale `.cnb` beside a new `.xnb`.
* An unchanged rebuild skips and the artifact stays byte-identical.
* Failures exit non-zero and name the pipeline stage and component.

From CMake, `cna_add_content()` is unchanged; pass the format through its `CONTENT_EXECUTABLE`
invocation when you need `.xnb` output from a build target.

---

## 5. Extension points

### A custom type inside an existing asset

Specialize `XnbTypeKey<T>` and subclass `XnbTypeWriterT<T>`:

```cpp
namespace CNA::Content::Xnb
{
    template <> struct XnbTypeKey<MyLevelData>
    { static std::string Name() { return "MyGame.Content.MyLevelData"; } };
}

class MyLevelDataWriter final : public XnbTypeWriterT<MyLevelData>
{
public:
    std::string TargetTypeName() const override { return XnbTypeKey<MyLevelData>::Name(); }
    std::string RuntimeReaderName() const override { return "MyGame.Content.MyLevelDataReader"; }
    bool IsValueType() const override { return false; }

    void Write(XnbWriter& output, const MyLevelData& value) const override
    {
        output.WriteInt32(value.roomCount);
        output.WriteString(value.title);
    }
};

registry.Register(std::make_shared<const MyLevelDataWriter>());
```

The reader side of the same pair is documented in
[`docs/xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md) — the reader name here
must be the name that game registers with `ContentTypeReaderManager::AddTypeCreator()`.

**`IsValueType()` must match the reader's own shape.** It decides whether the type is written
inline or with a leading type identifier wherever the format says `Object? T` — a collection
element, most struct fields. CNA's reader infers the same distinction from C++: a
`std::shared_ptr<T>` element is a reference type, anything else is a value type. So a C# `struct`
is `IsValueType() == true` with a plain `T` reader, and a C# `class` is `IsValueType() == false`
with a `std::shared_ptr<T>` reader (the read side's `RegisterShared()`). Getting this wrong emits
or consumes one extra identifier per element and desynchronises everything after it; it is the
single easiest custom-type mistake to make.

### A custom asset in the build

Implement `XnbAssetWriter` and register it with `ContentPipelineRegistry::RegisterXnbWriter()`,
alongside the importer and processor the CNB route already uses. A user-built compiler links
`CNA::ContentCompiler` and calls `RunContentCompiler()`, exactly as for CNB; see
`modules/content/examples/custom-content-compiler.cpp`.

### Closed generics

`List<T>`, `T[]`, `Dictionary<K,V>`, `Nullable<T>` and enums are registered per closed
combination, mirroring the reader side, because C++ cannot resolve `typeof(T)` at run time:

```cpp
RegisterXnbListWriter(registry, XnbTypeKey<Rectangle>::Name());
RegisterXnbDictionaryWriter(registry, XnbTypeKey<std::string>::Name(),
                            XnbTypeKey<std::int32_t>::Name());
RegisterXnbEnumWriter(registry, "MyGame.Content.Mode");
```

### Null and shared resources

An empty `std::any` is this API's spelling of .NET `null`; a value type refuses it. Shared
resources are registered under a caller-chosen stable key, because C++ values are copied rather
than referenced and XNA's object-identity deduplication has no direct equivalent:

```cpp
writer.WriteSharedResource("vertexBuffer:0", vertexBufferTypeName, std::any(buffer));
```

---

## 6. Known limitations

| Area | Status |
|---|---|
| Compression (LZX, LZ4) | ❌ Uncompressed only. Every XNA-compatible runtime reads uncompressed `.xnb` unconditionally, so this costs compatibility nothing — only file size. LZX **compression** is a separate algorithm from the decompressor CNA already has, and is gated on clean provenance (`plans/plan_xnapipeline.md` `XNAP-023`). |
| DXT/BC compression on write | ❌ CNA has a DXT *decompressor*, not a compressor, so textures are written as `SurfaceFormat.Color`. A pre-compressed BC source can be written once a block encoder exists. |
| `Model` from glTF | ⚠️ The `Model` **writer** is complete and verified against a real, externally produced `Model` `.xnb` (decode → write → decode reproduces the graph exactly). What is missing is the *source* route: CNA's glTF import produces the frozen CNB schema-1 carrier, which stores a vertex stride but **no vertex declaration**, and an `.xnb` `VertexBuffer` requires the full declaration. The writer refuses a schema-1 carrier with exactly that reason rather than inventing element offsets, formats and usages that were never authored. The lossless schema-2 carrier — which an imported `.xnb` produces — converts and writes end to end today. |
| `Effect` compilation | ❌ **Serializing an `Effect` `.xnb` is not the same as compiling HLSL/FX.** The payload is XNA D3D9 Effect Framework bytecode, which CNA does not produce. The writer stores bytecode a caller already has and refuses an empty payload; it never claims to have compiled anything. Integrating CNA's own FX infrastructure is a separate subsystem (`plans/plan_fx.md`). |
| `ReflectiveWriter` | ❌ Not reproduced. C++ has no runtime reflection; the reader side solves the equivalent problem with an explicit field-list builder, and the writer side would do the same (`XNAP-027`). |
| `.contentproj` | ❌ Not supported, and evaluated as a deliberate no: `.cna-content.json` already covers per-asset importer/processor/writer/parameter configuration without making XML and MSBuild an architectural dependency. |
| Xbox 360 platform | ❌ Not writable, by design — see §3. |
| Runtime/pipeline library split | ⚠️ `cna_content` still contains both the runtime reader and the build-time pipeline. That predates this work and is tracked by `plans/plan_content_pipeline.md`; the `.xnb` writer adds **no** new third-party dependency, and its files move as a unit whenever that split happens. |

---

## 7. Relationship with CNB

Shared: source assets, importers, processors, canonical content values, the build graph,
dependency tracking, the manifest, the CLI, atomic publication, determinism.

Separate, permanently: the serializers. `Cnb::CnbWriter` and `Xnb::XnbWriter` share no code and no
data layout. No CNB chunk, schema version, asset-type identifier or byte layout changed for this
work, and no XNB concept (type-reader table, shared-resource fixups, 7-bit integers,
`WAVEFORMATEX`) leaks into CNB. Where one format can express something the other cannot — CNB's
multi-representation textures, XNB's shared-resource graph — the difference is resolved in that
format's writer, with a diagnostic, never by weakening the other format.

---

## 8. Provenance

Specification-first, clean implementation. The sources used were:

1. The published Microsoft **“XNA Game Studio 4.0 Compiled (XNB) Content Format”** document from
   the XNAGameStudio archive (`Samples/XNA_XNB_Format/XNB Format.docx`) — the normative reference
   for the container, the object forms, and every built-in type's field layout.
2. CNA's own `.xnb` reader (`CNA::Internal::Xnb`, `Microsoft::Xna::Framework::Content::ContentReader`),
   which decides ties, because CNA must be able to read back what it writes.
3. Existing legitimately licensed `.xnb` fixtures under `tests/assets/xnb/`, used strictly as
   black-box behavioural evidence.

**Not used, at all**: MonoGame Content Pipeline implementation source, FNA content-building
implementation source, decompiled Microsoft assemblies, or any test asset whose licence was not
separately established.

One reader bug was found and fixed this way rather than encoded around: `SongReader` read the
duration as a bare `Int32` where the format writes `Object: Int32`, which shifted the value by one
byte (a real MonoGame fixture decoded as 769282 ms instead of 3005 ms, leaving one byte of the file
unread and its `Int32Reader` table entry unused). See `plans/plan_xnapipeline.md` `XNAP-G6`.

---

## 9. Verification

Run from the repository root:

```bash
cmake --preset unit
cmake --build cmake-build-unit --target CnaContentTests
SDL_AUDIODRIVER=dummy ./cmake-build-unit/CnaContentTests --gtest_filter='Xnb*:ContentOutputFormat*'
```

Coverage: the container itself (encoding boundaries, header fields, table ordering and
de-duplication, shared-resource indices, determinism, every limit refusal); a round trip of every
supported type through CNA's own unmodified runtime readers; each asset writer against the headless
canonical decoder; and the full `PNG → pipeline → .xnb → ContentManager::Load<Texture2D>()` route
with pixel equality.

External-runtime interoperability — the same file loaded by a real XNA 4.0 or MonoGame runtime —
cannot run on a Linux CI host and is not claimed here; see
[`docs/xnb-interoperability.md`](xnb-interoperability.md) for the fixtures and the procedure.
