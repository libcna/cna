# `.xnb` binary content pipeline: support and limitations

Covers CNA's real, binary-`.xnb`-compatible content loader (`Microsoft::Xna::Framework::Content::
ContentManager`/`ContentReader`/`ContentTypeReaderManager`, plus the individual readers under
`CNA::Internal::Xnb`) — the closing documentation task for `plans/plan_xnb.md`'s Phase G (XNB-45),
written once Phase A through Phase G's other tasks (XNB-42/42A/43/44/46) had landed. See
`xnb.md` for the original design rationale and `plans/plan_xnb.md` for the full, numbered task history.

This is **CNA's second, independent content format** alongside `.cnj` (see `cnj.md`) and the older,
CNA-original `.model.json` loose-file format (see `docs/model-content-pipeline-support.md`, which
predates this document and does not cover the real binary `Model` reader described below —
`ModelTypeReader`'s `.model.json` path and `ModelReader`'s real `.xnb` binary path are two
genuinely separate systems that happen to both produce a `Model`). Per `cnj.md`'s "Core rule",
`ContentManager::Load<T>()` always tries a `.xnb` file first, ahead of a literal caller-given path
or a `.cnj` sidecar/file.

## Scope

**In scope**: loading (deserializing) an already-built, real `.xnb` file at runtime — matching
FNA's own `ContentManager`/`ContentReader`/`ContentTypeReaderManager` read-side API and protocol.

**Out of scope, permanently**: anything that *produces* `.xnb` files (`ContentCompiler`,
`ContentImporter`, `ContentProcessor`, `ContentTypeWriter`, MSBuild/XNA project build-tool
integration). CNA only ever consumes `.xnb` files produced by real external tooling (XNA, MonoGame,
FNA); it has no need to generate them itself.

## Getting started: registering the built-in readers

`ContentTypeReaderManager`'s registry (the process-wide `AddTypeCreator`/`CreateReader` table) is
empty by default — CNA has no reflection to fall back on, unlike FNA, so every reader must be
registered explicitly before the first `.xnb` load. Call this once at game startup:

```cpp
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"

CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
```

This registers every reader in the "Supported readers" table below in one call (idempotent — safe
to call more than once). It does **not** register generic collection readers for arbitrary element
types (see "Generic collections" below) or any custom, game-specific reader (see "Custom readers"
below) — both need their own explicit registration.

## Supported readers

| FNA reader | CNA support | Notes |
|---|---|---|
| Primitives (`Byte`/`SByte`/`Int16`/`UInt16`/`Int32`/`UInt32`/`Int64`/`UInt64`/`Single`/`Double`/`Boolean`/`Char`/`String`) | ✅ Full | |
| Math (`Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray`) | ✅ Full | |
| `Decimal`/`DateTime`/`TimeSpan` | ✅ Full | Faithful field-for-field decoding, not a lossy `double` shortcut |
| `Curve` | ✅ Full | |
| `Texture2DReader` | ✅ `SurfaceFormat.Color`/`NormalizedByte4`/`Dxt1`/`Dxt3`/`Dxt5` | `NormalizedByte4` preserves its exact signed packed texels and requires a renderer that promotes it (EasyGL ES 3-class profiles or Skia); compressed formats are software-decompressed to `Color` |
| `Texture3DReader` | ✅ `SurfaceFormat.Color`/`Dxt1`/`Dxt3`/`Dxt5` | No real fixture exists anywhere in the available test-asset library (volume textures are rare in real XNA content) — verified via a hand-constructed stream instead, field-by-field against FNA's own `Texture3DReader.cs` |
| `TextureCubeReader` | ✅ `SurfaceFormat.Color`/`Dxt1`/`Dxt3`/`Dxt5` | Verified against a real MonoGame fixture covering all 6 faces and a full DXT1 mip chain (including the sub-4×4 block-rounding edge cases) |
| `SpriteFontReader` | ✅ Full | Depends on `Texture2DReader` and 3 closed generic-collection readers (see below) |
| `SoundEffectReader` | ✅ 16-bit PCM only | 8-bit PCM, IEEE float, MS-ADPCM, IMA-ADPCM, and XMA2 are recognized but explicitly rejected — no decode path exists yet. See the support matrix below |
| `SongReader` | ✅ Full | `Song` is always an external audio file reference (`.ogg`/etc, matching FNA), never embedded PCM |
| `AlphaTestEffectReader`, `BasicEffectReader`, `DualTextureEffectReader`, `EnvironmentMapEffectReader`, `SkinnedEffectReader` (the 5 stock effects) | ✅ Full | Each targets the common `shared_ptr<Effect>` base so `ModelReader`'s polymorphic `Effect` slot dispatches correctly regardless of which concrete stock effect a model references |
| `VertexDeclarationReader`, `VertexBufferReader`, `IndexBufferReader` | ✅ Full | |
| `ModelReader` | ✅ Full bone hierarchy, mesh/mesh-part, shared-resource (`VertexBuffer`/`IndexBuffer`/`Effect`) resolution | Verified against a real, externally-produced multi-bone fixture. This is a **different, real binary path** from the older `.model.json`-based `ModelTypeReader` — see the scope note above |
| The general `EffectReader` (compiled XNA Effect Framework bytecode) | ✅ on FNA3D; explicit renderer rejection elsewhere | Reads a bounded signed length and exact payload, constructs the same reflected `Effect` as the byte-array constructor, and preserves asset context in failures. The payload is XNA/FNA D3D9 Effect Framework bytecode, not MGFX or `.fx` source. |
| `ReflectiveReader<T>` (any custom `.xnb` type compiled *without* an explicit `ContentTypeWriter`/reader pair, relying on XNA's content-pipeline reflection fallback) | ✅ Supported since SAMPLE-044 via `ReflectiveTypeReaderBuilder<T>` — the game declares its field list once, CNA builds the reader | See "`ReflectiveReader<T>`" below |

### Generic collections

`ArrayReader<T>`, `ListReader<T>`, `DictionaryReader<TKey,TValue>`, and `NullableReader<T>` are C++
templates, not pre-registered for every possible `T` — CNA has no reflection to resolve `typeof(T)`
from a file's own generic-argument name at runtime the way FNA does, so only specific, hand-chosen
closed combinations exist (e.g. `SpriteFontReader` registers exactly the 3 combinations it needs:
`ListReader<Rectangle>`, `ListReader<Vector3>`, `ListReader<char>`). A `.xnb` file referencing a
collection-of-`T` combination nothing has registered fails with a clear "unregistered reader"
`ContentLoadException`, the same as any other unsupported reader name.

Registered so far, over and above `SpriteFont`'s three: `ArrayReader<Vector3>` (SAMPLE-048, a
processor tagging a model with its world-space triangle vertices), and `ListReader<Matrix>` plus
`ListReader<int>` (SAMPLE-051, a skinned model's bind pose, inverse bind pose and skeleton
hierarchy). A combination over a **game's own** type -- `List<MyKeyframe>`, say -- is the game's to
register, because only the game has the C++ type; see the `RegisterShared()` note below for the
shape such an element reader has to have.

### Custom readers (XNB-42)

A CNA game can register its own custom, non-built-in `.xnb` reader — no CNA-side involvement or
special-casing needed. `ContentTypeReaderManager::AddTypeCreator()` (already public/`CNAEXT`) is the
extension point, matching FNA's own real internal method of the same name/shape and playing the
same role `.cnj`'s `RegisterCnjLoader<T>()` plays for CNA's JSON-based format:

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
same `.xnb` → literal path → `.cnj` resolution order. See
`tests/Microsoft/Xna/Framework/Content/CustomContentTypeReaderTests.cpp` for a complete, runnable
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
separate reader class or name of its own ever appearing in the file's type-reader table under a
game-specific name at all — instead the file just names the target type directly, letting FNA's
`ReflectiveReader<T>` machinery introspect it.

**CNA decision**: only the explicit, named-reader path is supported. CNA has no runtime reflection
of the kind `ReflectiveReader<T>` needs (walking arbitrary field lists by name/type at load time),
and implementing a general reflection-driven fallback is out of scope — this mirrors the same
explicit-registration-only decision `.cnj`'s `RegisterCnjLoader<T>` already made for CNA's own
format. A `.xnb` file whose content pipeline used an implicit `ReflectiveReader<T>` for some type
cannot be loaded by CNA at all today.

**What a game author does instead** (updated 2026-08-27, SAMPLE-044): declare the type's fields
once and let CNA build the reader. `ReflectiveTypeReaderBuilder<T>` is the middle ground between
"no reflection" and "rebuild your content":

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
all decide between the inline and the dispatched form by whether `T` is `shared_ptr`-shaped --
so the value-shaped reader reads the payload one index short and desynchronises everything after
it. The registry key is the same either way: the `.xnb` names the serialized type, not the C++
representation.

The list must be in the type's **declaration order**, because that is the order
`IntermediateSerializer` wrote the fields in. Each member is dispatched by its C++ type:
arithmetic types and the XNA math structs are read inline as XNA writes a value type, and anything
else goes through `ContentReader::ReadObject`, which consumes the reference type's own reader
index first. `Register()` derives the canonical reader name — the assembly qualifiers are stripped,
so a game never has to reproduce that string — and registers an `EnumReader` for every
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
| LZ4 (MonoGame's own alternate compressed format, `ContentCompressedLz4`) | ❌ Recognized (the header's flag bit is decoded into a real `XnbCompression::Lz4` enum value), explicitly unsupported — throws a precise `ContentLoadException` rather than mis-decoding. No CNA decoder exists yet; deferred to a future broad-compatibility pass (`plans/plan_xnb.md` XNB-30C) |
| Both compression bits set simultaneously | ❌ `XnbCompression::Unknown` — throws a precise `ContentLoadException` |

## Platform support

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
header. See `tests/CNA/Internal/Xnb/XnbHeaderTests.cpp`'s
`MonoGameWebAssemblyPlatformIsNotAcceptedMatchingFnaExactly`.

## Audio (`SoundEffectReader`) support matrix

| Wave format | Support |
|---|---|
| PCM, 16-bit | ✅ Supported — CNA's `SoundEffect` PCM constructors are `SDL_AUDIO_S16LE`-only |
| PCM, 8-bit | ❌ Explicitly rejected, no conversion path |
| IEEE float | ❌ Explicitly rejected |
| MS-ADPCM | ❌ Explicitly rejected |
| IMA-ADPCM | ❌ Explicitly rejected |
| XMA2 | ❌ Explicitly rejected (format-extension fields are still parsed for stream-position correctness, since CNA has no XMA2 decoder) |

`Song` is always treated as an external audio file reference (matching FNA — XNA/MonoGame never
embed `Song` PCM data directly in a `.xnb`), so this matrix does not apply to it.

## Malformed/adversarial-file hardening

A whole-container deterministic fuzz test (`XnbContainerFuzzTests.cpp`) mutates a real fixture's
entire byte stream (header, type-reader table, shared-resource region, root object body) and loads
it through the exact `ContentManager::Load<T>()` path a real game uses, 1500 iterations per run,
also exercised under `-DCNA_SANITIZE=address,undefined`. This found and fixed a real
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

## Known gaps / limitations

| Area | Status |
|---|---|
| `ReflectiveReader<T>` (implicit custom readers) | ❌ Not supported, by design — see above |
| General `EffectReader` on a renderer without `CompiledEffects` | ❌ loading fails with an asset-specific capability diagnostic rather than a silent shader fallback. True for every identity except FNA3D, and SDL_GPU / the EasyGL family with their own build option on |
| LZ4 compression | ❌ Not supported yet — deferred (`plans/plan_xnb.md` XNB-30C) |
| Generic collection readers for an unregistered `T` combination | ❌ Not supported — each closed combination needs its own explicit registration |
| Texture formats beyond `Texture2D`'s `Color`/`NormalizedByte4`/`Dxt1`/`Dxt3`/`Dxt5` and `Texture3D`/`TextureCube`'s existing Color/DXT scope | ❌ Not supported yet |
| `SoundEffect` formats beyond 16-bit PCM | ❌ Not supported yet — see the audio support matrix above |
| Platform identifiers MonoGame added after FNA's fork point (`'b'`/`'5'`/`'O'`/`'S'`/`'V'`) | ❌ Not accepted, matching FNA exactly (deliberate, not an oversight) |

## Test fixture corpus

Real, externally-produced `.xnb` fixtures (never generated by CNA itself) live under
`tests/assets/xnb/monogame/windows/{uncompressed,lzx}/`, each with a `.manifest.json` documenting
its producer, exact expected fields, and (where relevant) why it was chosen. Broader real-world
compatibility was additionally verified against two independent third-party sources
(`prime31/Nez`, `openeggbert/speedyblupi.com`) without vendoring their larger binaries into this
repo — see the "Platform support" and "Custom readers" sections above for what was found.
