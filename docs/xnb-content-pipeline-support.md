# `.xnb` binary content pipeline: support and limitations

Covers CNA's binary XNB runtime loader: `Microsoft::Xna::Framework::Content::ContentManager`,
`ContentReader`, `ContentTypeReaderManager` and the individual readers under `CNA::Internal::Xnb`.
Reader implementation does not establish support for every payload on every renderer/platform.
The numbered task history (`XNB-*`) is in Git history.

A `.xnb` file is not a source asset in a wrapper. It is the XNA Content Pipeline's serialized object
graph: a table of type readers, then a root object, then shared resources that earlier nodes refer
back to. .NET finds each reader by resolving its type name through reflection, which C++ does not
have, so CNA keeps a registry of readers, each registered in code under its normalized .NET type
name; a game registers its own readers the same way (see "Custom readers" below).

XNB is separate from CNA's [CNB container](cnb-format.md), [CNJ JSON assets](cnj-format.md) and
loose-source routes. The `.model.json` path and real binary ModelReader are distinct loaders that
can produce a Model; see [model content](model-content-pipeline-support.md).
`ContentManager::Load<T>()` resolves `.xnb`, `.cnb`, a literal path, `.cnj`, then native fallbacks,
subject to the selected type's readers and containment rules. Use separate content roots when
comparing formats so a higher-priority asset does not hide the one being tested.

## Scope

**In scope**: loading (deserializing) an already-built, real `.xnb` file at runtime — matching
FNA's own `ContentManager`/`ContentReader`/`ContentTypeReaderManager` read-side API and protocol.

**Out of scope for this page**: producing XNB. CNA's native C++ writer and source pipeline are
documented in [XNB interoperability](xnb-interoperability.md) and the
[native pipeline guide](content-pipeline.md). Writer/container agreement is separate from read-side
and genuine-runtime qualification.

## Built-in reader registration

Normal typed `ContentManager::Load<T>()` registers the built-in family needed by T; plain values
use the primitive/math family, and an unknown game-defined graph can require all built-ins.
Game declares built-ins for lazy installation, and registry queries install declared readers.
An application does not have to call the all-family registration function before ordinary loads.
These boundaries keep a texture-only consumer from linking unrelated model importers/codecs;
see [link-size architecture](link-size-architecture.md).

The process-wide reader table still has no general CLR reflection fallback. Explicitly registering
all built-ins remains available for hosts that want that complete link/registration boundary:

```cpp
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"

CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
```

This installs the built-in reader set (idempotent when called repeatedly). It does **not** register
generic collection readers for arbitrary element
types (see "Generic collections" below) or any custom, game-specific reader (see "Custom readers"
below) — both need their own explicit registration.

## Supported readers

The entries below describe available decoding paths. Graphics readers still need a suitable
GraphicsDevice/format capability; compiled effects and media playback have additional build/runtime
requirements. "Full" is not a guarantee for every possible XNB object graph or platform.

| FNA reader | CNA support | Notes |
|---|---|---|
| Primitives (`Byte`/`SByte`/`Int16`/`UInt16`/`Int32`/`UInt32`/`Int64`/`UInt64`/`Single`/`Double`/`Boolean`/`Char`/`String`) | ✅ Full | |
| Math (`Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`) | ✅ Full | |
| `Decimal`/`TimeSpan` | ✅ field/tick decoding | Decimal is conditional on Sharp Runtime's native Int128 support; neither uses a lossy double shortcut. |
| `DateTime` | ✅ ticks and Kind | Unspecified/UTC retain ticks and Kind. Local payloads carry UTC ticks and are converted through Sharp Runtime’s current zone; undefined Kind 3 is refused. Genuine-XNA compiler fixtures and reader observations are retained under `tests/reference/xna40/datetime-kind/`. |
| `Curve` | ✅ Full | |
| `Texture2DReader` | ✅ all 20 classic XNA formats | All 17 uncompressed formats preserve the serialized format and exact mip bytes. DXT1/3/5 blocks stay compressed when the active renderer supports exact compressed transfer; other renderers software-decode them to `Color`, matching FNA's backend-capability fallback. A non-power-of-two DXT texture under Reach takes the same `Color` path: the genuine Platformer `Fonts/Hud.xnb` is a 128×132 Dxt3 atlas that XNA loads under Reach, while the public `Texture2D` constructor still refuses Reach NPOT DXT (`SAMPLE-013`) |
| `Texture3DReader` | ✅ all 15 legal XNA HiDef volume formats | Preserves the serialized uncompressed format and each mip's exact bytes; DXT1/3/5 are correctly refused because XNA excludes compressed volume textures. Reader tests cover synthetic streams, format bytes and a depth-dominant mip chain; CNA also writes volume fixtures. Distinguish these self-authored witnesses from an independently produced Microsoft volume asset |
| `TextureCubeReader` | ✅ all 18 XNA-permitted cube formats | Preserves the 15 legal uncompressed formats and exact face/mip bytes; `NormalizedByte2/4` are rejected because XNA excludes them from ordinary cubes. DXT1/3/5 use the same exact-transfer-or-Color-fallback policy as 2D. A real MonoGame fixture covers all 6 faces and a full DXT1 mip chain, including sub-4×4 block rounding |
| `SpriteFontReader` | ✅ Full | Depends on `Texture2DReader` and 3 closed generic-collection readers (see below) |
| `SoundEffectReader` | ✅ PCM8/16, IEEE float, MS-ADPCM, IMA-ADPCM | Decoding/conversion uses the shared audio-import path and supplies the runtime's PCM16 samples. XMA2 is recognized but rejected because CNA has no runtime XMA2 decoding route. See the support matrix below. |
| `SongReader` | ✅ external media reference | Song data names a separate media file; playback/codec availability is a runtime requirement, not embedded PCM in XNB. |
| `VideoReader` | ✅ external reference and serialized metadata | Registered independently of runtime video decoding. A no-video build can load metadata; playback reports its unsupported capability separately. |
| `AlphaTestEffectReader`, `BasicEffectReader`, `DualTextureEffectReader`, `EnvironmentMapEffectReader`, `SkinnedEffectReader` (the 5 stock effects) | ✅ Full | Each targets the common `shared_ptr<Effect>` base so `ModelReader`'s polymorphic `Effect` slot dispatches correctly regardless of which concrete stock effect a model references |
| `EffectMaterialReader`, `ExternalReferenceReader`, `DictionaryReader<String,Object>` | ✅ Full | A parameter dictionary's type-erased external reference is loaded according to the referenced XNB's concrete root reader, so `Texture2D`, `Texture3D`, and `TextureCube` parameters retain their real type and lifetime |
| `VertexDeclarationReader`, `VertexBufferReader`, `IndexBufferReader` | ✅ Full | |
| `ModelReader` | ✅ Full bone hierarchy, mesh/mesh-part, shared-resource (`VertexBuffer`/`IndexBuffer`/`Effect`) resolution | Verified against a real, externally-produced multi-bone fixture. This is a **different, real binary path** from the older `.model.json`-based `ModelTypeReader` — see the scope note above |
| The general `EffectReader` (compiled XNA Effect Framework bytecode) | ✅ on FNA3D; SDLGPU, EasyGL, WEBGPU, SOFTWARE and DIRECTX9 with their compiled-effect build options | Reads a bounded signed length and exact payload, constructs the same reflected `Effect` as the byte-array constructor, and preserves asset context in failures. The payload is XNA/FNA D3D9 Effect Framework bytecode, not MGFX or `.fx` source. |
| `ReflectiveReader<T>` (any custom `.xnb` type compiled *without* an explicit `ContentTypeWriter`/reader pair, relying on XNA's content-pipeline reflection fallback) | ✅ Supported since SAMPLE-044 via `ReflectiveTypeReaderBuilder<T>` — the game declares its field list once, CNA builds the reader | See "`ReflectiveReader<T>`" below |

### Generic collections

`ArrayReader<T>`, `ListReader<T>`, `DictionaryReader<TKey,TValue>`, and `NullableReader<T>` are C++
templates, not pre-registered for every possible `T` — CNA has no reflection to resolve `typeof(T)`
from a file's own generic-argument name at runtime the way FNA does, so only specific, hand-chosen
closed combinations exist (e.g. `SpriteFontReader` registers exactly the 3 combinations it needs:
`ListReader<Rectangle>`, `ListReader<Vector3>`, `ListReader<char>`). A `.xnb` file referencing a
collection-of-`T` combination nothing has registered fails with a clear "unregistered reader"
`ContentLoadException`, the same as any other unsupported reader name.

Other registered combinations include `ArrayReader<Vector3>`, `ListReader<Matrix>`,
`ListReader<int>`, `ListReader<string>`, primitive string-key dictionaries and model/material
dictionary shapes. Their registration lives in the primitive, math, font and model reader families;
the three font combinations are not the entire generic inventory.
A combination over a **game's own** type -- `List<MyKeyframe>`, say -- is the game's to
register, because only the game has the C++ type; see the `RegisterShared()` note below for the
shape such an element reader has to have.

### Custom readers (XNB-42)

A CNA game can register its own custom, non-built-in `.xnb` reader — no CNA-side involvement or
special-casing needed. `ContentTypeReaderManager::AddTypeCreator()` (already public/`CNAEXT`) is the
extension point, matching FNA's own real internal method of the same name/shape and playing the
same role `.cnj`'s `RegisterCnjLoader<T>()` plays for CNA's JSON-based format:

The following is a value-shaped root example. Include the ContentReader, ContentTypeReader and
ContentTypeReaderManager headers under `Microsoft/Xna/Framework/Content/`; define MyLevelData
with an integer `roomCount`, and introduce aliases for ContentReader/ContentTypeReaderManager.
Registration runs in application code before loading the asset. Reference-shaped graphs need the
matching shared-pointer carrier rather than assuming a copied value preserves object identity.

```cpp
class MyLevelDataReader : public Microsoft::Xna::Framework::Content::ContentTypeReader<MyLevelData>
{
public:
    MyLevelDataReader() : ContentTypeReader<MyLevelData>("MyGame.Content.MyLevelData") {}
protected:
    MyLevelData Read(ContentReader& input, std::optional<MyLevelData> existingInstance) override
    {
        MyLevelData data = existingInstance.value_or(MyLevelData{});
        data.roomCount = input.ReadInt32();
        return data;
    }
};

ContentTypeReaderManager::AddTypeCreator(
    "MyGame.Content.MyLevelDataReader", [] { return std::make_unique<MyLevelDataReader>(); });
```

Then `content.Load<MyLevelData>("level1")` works exactly like any built-in type, going through the
same `.xnb` → `.cnb` → literal path → `.cnj` → native resolution order. See
[`CustomContentTypeReaderTests.cpp`](../modules/content/tests/Microsoft/Xna/Framework/Content/CustomContentTypeReaderTests.cpp) for a complete, runnable
example (including the correct canonical-name string a real `.xnb` file compiled against your
custom `ContentTypeWriter` would reference).

**Real-world confirmation**: a genuinely independent, third-party MonoGame-based game framework
(`prime31/Nez`) ships its own custom `.xnb` reader (`Nez.BitmapFonts.BitmapFontReader`) for a
non-standard bitmap-font format. A real, unmodified `.xnb` file produced against it and loaded by
CNA without registering an equivalent reader fails cleanly with `'NezDefaultBMFont' references an
unregistered .xnb content type reader 'Nez.BitmapFonts.BitmapFontReader'.` — confirming this
extension point's design against real third-party content, not just a synthetic example.

### `ReflectiveReader<T>` (XNB-42A)

Real XNA/MonoGame content pipelines can compile a custom type's `.xnb` reader two ways: an explicit
hand-written `ContentTypeReader<T>` subclass (a real, named class with a real assembly-qualified
name in the file's type-reader table), or an implicit, compiler-generated `ReflectiveReader<T>`
that uses .NET reflection at load time to walk the target type's fields automatically, with no
separate game-specific reader class. The table names the framework's `ReflectiveReader<T>` with
the game's target type as its generic argument; FNA then reflects over that target's members.

**CNA decision**: CNA does not attempt automatic runtime reflection. A game explicitly declares
the serialized member list because C++ cannot discover arbitrary fields by name and type at run
time. A correctly described, supported payload can then be read without rebuilding the content.
This is not automatic support for arbitrary reflection attributes, member kinds or graphs.

**What a game author does instead** (updated 2026-08-27, SAMPLE-044): declare the type's fields
once and let CNA build the reader. `ReflectiveTypeReaderBuilder<T>` is the middle ground between
"no reflection" and "rebuild your content":

Include `Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp` and introduce the builder's
namespace alias. These fragments assume the game-defined types/members already exist; the
complete cases are in
[`ReflectiveTypeReaderTests.cpp`](../modules/content/tests/Microsoft/Xna/Framework/Content/ReflectiveTypeReaderTests.cpp).

```cpp
ReflectiveTypeReaderBuilder<ParticleSystemSettings>("ParticlesSettings.ParticleSystemSettings")
    .Field(&ParticleSystemSettings::MinNumParticles)
    .Field(&ParticleSystemSettings::TextureFilename)
    .EnumField(&ParticleSystemSettings::AccelerationMode, "ParticlesSettings.AccelerationMode")
    .Register();
```

`Register()` is the right call when the type is read **directly** -- as an `.xnb`'s root asset, or
inline as a value type. When the type is a C# `class` that something *dispatches* to -- a list
element, a dictionary value, or a `Model.Tag` -- call **`RegisterShared()`** instead, which
registers a reader handing the object back as a `std::shared_ptr`:

```cpp
ReflectiveTypeReaderBuilder<ModelKeyframe>("CustomModelAnimation.ModelKeyframe")
    .Field(&ModelKeyframe::bone)
    .Field(&ModelKeyframe::time)
    .Field(&ModelKeyframe::transform)
    .RegisterShared();

ReflectiveTypeReaderBuilder<ModelData>("CustomModelAnimation.ModelData")
    /* ... */
    .RegisterShared<System::Object>();   // Model.Tag is stored as a System::Object*
```

The two are not interchangeable. XNA writes a reference type with its own 1-based type-reader
index in front, and `ListReader<T>`, `DictionaryReader<TKey, TValue>` and `ModelReader::ReadTag`
use serialized reference/value traits. Those traits recognize shared/pointer-shaped custom
objects and known built-in reference types such as strings, collections and by-value asset wrappers.
A custom identity-bearing class registered as a plain value can therefore read a nested payload
one index short. The reader key describes the serialized .NET type, not its C++ carrier; choose
the correct carrier before registering the graph.

A field marked `[ContentSerializer(SharedResource = true)]` uses the shared-resource table rather
than the ordinary object-dispatch form. Declare it with `SharedResourceField()` and register the
containing C# class through `RegisterShared()` so the deferred fixup target remains alive:

```cpp
ReflectiveTypeReaderBuilder<ModelPart>("CustomModelSample.CustomModel+ModelPart")
    .Field(&ModelPart::triangleCount)
    .Field(&ModelPart::vertexCount)
    .Field(&ModelPart::vertexBuffer)
    .Field(&ModelPart::indexBuffer)
    .SharedResourceField(&ModelPart::effect)
    .RegisterShared();
```

The assignment runs only after the root object and every shared resource have been read, matching
XNA's two-pass fixup order. Calling value-shaped `Register()` after declaring a shared-resource
field is rejected because its temporary target could not safely outlive the root read.

The list must match the **binary writer's serialized member order**, including any base-type and
property layout. XNB's ReflectiveWriter contract is separate from IntermediateSerializer's XML
format; a field-only example's declaration order must not be generalized to every reflected class.
Each member is dispatched by its C++ type:
arithmetic types and the XNA math structs are read inline as XNA writes a value type, and anything
else goes through `ContentReader::ReadObject`, which consumes the reference type's own reader
index first. `Register()` derives the canonical reader name — the assembly qualifiers are stripped,
so the builder supplies that reader key — and registers an `EnumReader` for every
`EnumField`, because **an `.xnb`'s type-reader table must resolve in full before any object is
read**, including readers the reflective payload never dispatches to.

This is still not reflection: the field list comes from the game, since C++ cannot introspect a
type at run time. What it removes is the duplication of that list in a hand-written reader.

The reflective payload's format, verified byte for byte against pipeline output on SAMPLE-044:
value-type fields inline in declaration order, a reference-type field preceded by the 1-based index
of its own type reader. Rebuilding the content with an explicit `ContentTypeWriter`/reader pair
remains available and is still the right answer when you control the pipeline.

## Compression support

| Compression | Support |
|---|---|
| None (uncompressed) | ✅ Full |
| LZX (real XNA/MonoGame's primary compressed format) | ✅ Full — a faithful, line-by-line port of FNA's own `LzxDecoder.cs`, verified byte-for-byte against FNA's own decoder run under Mono, plus a 2000-iteration deterministic mutation fuzzer (found and fixed a real heap-buffer-overflow in `MakeDecodeTable()`'s long-code table-growth path) |
| LZ4 (MonoGame's own alternate compressed format, `ContentCompressedLz4`) | ✅ Full raw-block decode. MonoGame writes one raw LZ4 block after the ordinary 4-byte decompressed-size field; this is not generic LZ4 frame parsing. Input and output sizes, every literal/match extent, offsets, final consumption, and the shared XNB read limit are checked before use. Runtime and headless transcoding call the same decoder. |
| Both compression bits set simultaneously | ❌ `XnbCompression::Unknown` — throws a precise `ContentLoadException` |

## Platform support

**Not every accepted platform byte is a Microsoft XNA 4.0 target.** XNA 4.0 itself shipped for
three: Windows (`w`), Windows Phone 7 (`m`) and Xbox 360 (`x`). Every other identifier in the list
below was introduced by a later, non-Microsoft implementation of the format; a file carrying one is
part of the extended XNB ecosystem and was never produced or consumed by a Microsoft XNA 4.0
runtime. CNA accepts those header bytes subject to its whitelist; payload/reader support and
execution on that platform are separate questions. [XNB interoperability](xnb-interoperability.md)
keeps these categories apart on the write side as well.

CNA's accepted platform-identifier list (`CNA::Internal::Xnb::XnbAcceptedPlatforms()`) matches
FNA's own `ContentManager.targetPlatformIdentifiers` **exactly** (16 identifiers: Windows, Xbox360,
WindowsPhone, iOS, Android, DesktopGL, MacOSX, WindowsStoreApp, NativeClient, Ouya,
PlayStationMobile, WindowsPhone8, RaspberryPi, PlayStation4, plus the deprecated
WindowsGL/Linux aliases FNA itself still accepts) — not MonoGame's own, larger list. Platform
identifiers MonoGame added after FNA's fork point (WebAssembly/Bridge.NET `'b'`, PlayStation5
`'5'`, XboxOne `'O'`, Nintendo Switch `'S'`, DesktopVK `'V'`) are **not** accepted, matching FNA
exactly rather than MonoGame's newer surface.

**Real-world confirmation**: a genuinely independent MacOSX-platform (`'X'`) `.xnb` texture (from
`prime31/Nez`, MIT-licensed) loads correctly end-to-end through `Texture2DReader` — real evidence
platform handling isn't Windows-only in practice, not just in the acceptance list. A genuinely
independent WebAssembly-platform (`'b'`) `.xnb` file (from a real game project) is correctly,
cleanly rejected with `'...' has an unrecognized XNB platform identifier 'b'.` — confirming CNA's
FNA-faithful (not MonoGame-faithful) platform policy against real content, not just a synthetic
header. These retained fixture results do not qualify Mac or WebAssembly execution. See
[`XnbHeaderTests.cpp`](../modules/content/tests/CNA/Internal/Xnb/XnbHeaderTests.cpp)'s
`MonoGameWebAssemblyPlatformIsNotAcceptedMatchingFnaExactly`.

## Audio (`SoundEffectReader`) support matrix

| Wave format | Support |
|---|---|
| PCM, 16-bit | ✅ Supported directly |
| PCM, 8-bit | ✅ Converted to PCM16 through the shared WAV-import path |
| IEEE float | ✅ Converted to PCM16 through SDL3's WAV decoder |
| MS-ADPCM | ✅ Converted to PCM16 through SDL3's WAV decoder; the standard coefficient table is supplied when an XNB omits it |
| IMA-ADPCM | ✅ Converted to PCM16 through SDL3's WAV decoder; loop points are translated from compressed bytes to decoded frames |
| XMA2 | ❌ Explicitly rejected; format-extension fields are parsed for stream-position correctness, but no runtime XMA2 decoding route is wired here. |

`Song` is always treated as an external audio file reference (matching FNA — XNA/MonoGame never
embed `Song` PCM data directly in a `.xnb`), so this matrix does not apply to it.

## Malformed/adversarial-file hardening

A whole-container deterministic fuzz suite (`XnbContainerFuzzTests.cpp`) mutates real fixtures'
entire byte stream (header, type-reader table, shared-resource region, root object body) and loads
them through the `ContentManager::Load<T>()` path, with four typed cases of 1500 mutations each.
The recorded hardening work also used `-DCNA_SANITIZE=address,undefined`; a normal run is not a
fresh sanitizer qualification. That work found and fixed a
heap-buffer-overflow: `VertexBufferReader`/`IndexBufferReader` (and, by the same pattern,
`Texture2D`/`Texture3D`/`TextureCube`/`SoundEffect`'s raw-byte-blob reads) read a declared byte
count via `ReadBytes()`, which — matching real .NET's own non-throwing contract — silently trims
its result on a truncated stream instead of throwing, and then used the *original declared* count
rather than the actual (possibly shorter) result size when handing the buffer to a raw
pointer+count API. Fixed via `ContentReader::ReadBytesExactOrThrow()`, now used by every such
reader instead of raw `ReadBytes()`.

Also hardened: `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<TKey,TValue>` now reject an
adversarial/negative declared element count before allocating/reserving
(`ContentReader::CheckCollectionElementCount()`), and a malformed generic-argument type name in the
type-reader table now surfaces as the same `ContentLoadException` every other malformed-input case
uses, instead of leaking `XnbTypeName`'s own lower-level `std::invalid_argument`.

`BinaryReader::ReadString()`/`ReadBytes(int)` (in `sharp-runtime`, used by every `.xnb`/`.cnj`
reader) were also hardened: a seekable stream's own remaining length now bounds the eager
allocation both methods used to make directly from an attacker-controlled length/count prefix,
closing an allocation-bomb vector (a single 5-byte 7-bit-encoded prefix can declare up to ~2GB)
without changing either method's observable behavior for any valid input.

The typed runtime loader, type-erased external-reference loader, canonical importer and content
manifest now share one container decoder. It enforces `XnbReadLimits::maxFileSize` before parsing
or filesystem allocation where the input size is known, validates the declared container range,
and applies the same compressed/decompressed limits to both LZX and LZ4. A custom `OpenStream()` is
also bounded while it is read, before its bytes reach the decoder.

## Content manifest (CNAEXT)

`ContentManager` can describe what its content root holds, for tools such as asset validators or an
editor's content view. None of this is XNA API.

* `RefreshContentManifest()` scans the root and rebuilds the manifest. It is not called by the
  constructor; the first `GetContentManifest()` or `GetXnbReaderUsageSummary()` runs it lazily. A
  scan is a snapshot, with no file watching. CNA's own pipeline bookkeeping (`.cna-content.lock`,
  `.cna-content-manifest.json`, `.cna-contentproj` intermediate directories) is excluded.
* `GetContentManifest()` returns one `ContentManifestEntry` per logical asset name (the relative
  path without its extension): whether a `.xnb` and a `.cnj` exist, every other extension found,
  and `xnbReaderNames`, the type-reader names the `.xnb`'s reader table lists.
  CNB is currently reported among `nativeExtensions`; there is no separate `hasCnb` field.
* `GetXnbReaderUsageSummary()` aggregates those names into one `ContentManifestReaderUsage` row each:
  how many files use the reader, and whether `ContentTypeReaderManager` has it registered -- the
  quick answer to "which readers would this content need that the game has not registered".

A malformed `.xnb` is not a scan failure; its entry just has no reader names. LZX- and
LZ4-compressed files are decompressed and inspected through the same bounded container decoder as
runtime loading. The first manifest scan therefore pays the full decompression cost for each
compressed `.xnb`; there is no partial-frame shortcut.

This inventory is not a full asset validator: reader-table registration says nothing about a
payload's semantic validity, external dependencies or the active renderer's capabilities.

## Known gaps / limitations

| Area | Status |
|---|---|
| Automatic reflection over an undeclared C++ type | ❌ Not supported by design; implicit `ReflectiveReader<T>` payloads are supported when the game declares their serialized members through `ReflectiveTypeReaderBuilder<T>` |
| General `EffectReader` on a renderer without `CompiledEffects` | ❌ loading fails with an asset-specific capability diagnostic rather than a silent shader fallback. FNA3D supports it unconditionally; SDLGPU, the EasyGL family, WEBGPU, SOFTWARE and DIRECTX9 support it behind their compiled-effect build options |
| LZ4 compression | ✅ MonoGame's raw-block XNB representation is supported; generic LZ4 frames are intentionally not an XNB format |
| Generic collection readers for an unregistered `T` combination | ❌ Not supported — each closed combination needs its own explicit registration |
| `SoundEffect` XMA2 and unknown codecs | ❌ Not supported — see the audio support matrix above |
| Local-time conversion scope | DateTime delegates to Sharp Runtime’s current-zone conversion. The genuine-XNA measurement covers a fixed UTC+2 winter date; ambiguous DST identity and native Windows are not qualified by it. |
| Ecosystem identifiers outside the accepted whitelist, including `'b'`/`'5'`/`'O'`/`'S'`/`'V'` | ❌ Rejected by the current CNA/FNA-compatible header policy; header acceptance alone would not establish payload or platform support. |

## Test fixture corpus

Real, externally-produced `.xnb` fixtures live under
`tests/assets/xnb/monogame/windows/{uncompressed,lzx}/`. The `lz4/` oracle retains the exact body
of MonoGame's external `white-1.xnb`, compressed by upstream liblz4 and wrapped according to
MonoGame `ContentWriter.WriteCompressedStream`; its manifest distinguishes that derivation.
Every fixture has a `.manifest.json` documenting its producer, exact expected fields, and why it
was chosen. Broader real-world
compatibility was additionally verified against two independent third-party sources
(`prime31/Nez`, `openeggbert/speedyblupi.com`) without vendoring their larger binaries into this
repo — see the "Platform support" and "Custom readers" sections above for what was found.

## Current verification

On 2026-11-02, the DateTime Kind/local-UTC repair passes seven focused cases in each of
UTC+2, UTC and New York, and all 313 selected XNB reader/writer/container cases pass privately
with dummy audio. Three committed DateTime files reproduce byte for byte from the genuine XNA
compiler; an isolated genuine reader measurement under Wine confirms the local conversion and
undefined-Kind refusal. See [the writer guide](xnb-interoperability.md#13-current-verification)
for the exact measurement boundary and [the XNA/FNA decision](xna-fna-behavior-decisions.md).

The earlier 2026-11-01 documentation pass reconciled typed/lazy registration, reader/reference dispatch, reflective
builders, media/manifest routes, container limits and the retained fixture metadata. The existing
`CnaContentTests` target builds; 456 selected XNB/reader/reflective/manifest/ownership cases plus
eight LZX decoder/differential/fuzz cases passed privately on OPENGLES3/dummy audio. The LZX
differential cases compare retained FNA/Mono-produced reference bytes; they do not rerun Mono.
The custom-reader fragment passes a C++ syntax check with its stated type/header/alias setup.
All 16 accepted platform bytes match local FNA, and all 14 retained MonoGame Windows fixtures have
their own manifests. No fresh Microsoft/Xbox/Phone runtime, sanitizer or cross-renderer campaign
was run; model fuzzing can skip on renderers without a 3D pipeline.
