# `.xnb` interoperability: what CNA writes, and how strongly each claim is verified

CNA has a native C++ XNB **writer** and a content pipeline that compiles source assets straight to
`.xnb`, with no XNA Game Studio, MonoGame, FNA build tooling, MSBuild or proprietary source
involved. This document says exactly what it produces and, for every claim, *how that claim was
checked*.

The read side is a separate document: `docs/xnb-content-pipeline-support.md` covers loading
already-built `.xnb` files. The task history and the per-decision record are in
`plans/plan_xnapipeline.md`.

> **Provenance note (2026-09-03).** This document, and the plan behind it, were written believing
> no XNB writer existed in the repository. That was true of `next` and false of the repository: a
> parallel implementation exists on the `pipeline` branch, and much of what is described here
> duplicates it. See `plans/plan_xnapipeline.md` §0.5 for exactly which parts are duplicates and
> which are new. The capability claims below are about **this** implementation and are unaffected.

> **The single most important sentence in this document.** No table below claims verification
> against a genuine Microsoft XNA 4.0 runtime, because no XNA 4.0 runtime — and no Windows, Wine,
> Mono or .NET Framework — exists in the environment this work was done in. A ready-to-run harness
> and a fixture corpus are committed so that anyone with an XNA-capable machine can fill that column
> in; until they do, it stays empty. See **Running the XNA 4.0 harness** at the end.

---

## 1. Confidence vocabulary

Every capability table uses these labels, and "supported" is never written without at least
`cna-rt`.

| Label | Meaning |
|---|---|
| `impl` | Code exists and is exercised by a test. |
| `cna-rt` | CNA writes it and CNA's own independent **reader** loads it back with the expected values. The reader was written first, from the format, and knows nothing about the writer. |
| `spec` | An independent, specification-based parser (`tools/xnb/xnb_conformance.py`, Python, sharing no code and no headers with CNA) parses the bytes and its field values match a hand-written expectation manifest. |
| `golden` | The produced bytes are **identical** to a file produced by external tooling. |
| `xna40` | Loaded by a genuine Microsoft XNA 4.0 runtime, with values asserted. **Nothing has this label yet.** |
| `none` | Not verified. |

These are cumulative in strength but not in coverage: a type can be `cna-rt` without being `spec`
simply because the independent parser has no decoder for it yet, and that is recorded honestly
below rather than rounded up.

---

## 2. The container

### 2.1 Header

```text
offset size  field
0      3     'X' 'N' 'B'
3      1     target platform byte
4      1     format version   (5 = the XNA 4.0-era container; 4 = earlier, XNA 3.x-era)
5      1     flags
6      4     int32 little-endian total file length, including these 10 bytes
```

| Flag bit | Meaning | CNA writer |
|---|---|---|
| `0x01` | Graphics profile: set = HiDef, clear = Reach | written from `XnbFileOptions::graphicsProfile`; **default Reach**, because a Reach asset loads under both profiles and a HiDef one does not |
| `0x40` | A single raw LZ4 block — a later-ecosystem extension, **never** part of XNA 4.0 | **written**, and refused outright on an XNA 4.0 target platform because that pairing describes a file that cannot exist |
| `0x80` | LZX | **not implemented** (`XNAP-81`); requesting it fails with a message naming the task |

LZX is the compression Microsoft XNA 4.0 itself produced, and it is the one CNA cannot write. CNA
*reads* it. Writing it needs an LZX encoder — Huffman trees for three alphabets, delta-encoded tree
transmission, block-type selection, the sliding window's position-slot encoding — which is a
different program from the decoder CNA has. **An uncompressed file loads in every XNA 4.0 runtime**,
so nothing is blocked by this; only file size is affected.

There is a shortcut that is deliberately not taken: LZX's uncompressed block type would produce a
conforming stream with no entropy coding at all, so CNA could set the `0x80` flag today and
compress nothing. That would be honest only if documented so precisely that nobody would want it,
and it would still risk an interoperability failure this environment cannot test for. Asking for
LZX is refused instead.

### 2.2 Target platforms — three are XNA 4.0, thirteen are not

This is the most common thing to get wrong about XNB, so it is stated plainly: **Microsoft XNA 4.0
shipped for three platforms.** Every other platform byte the format carries was introduced by a
later, non-Microsoft implementation. Writing one of those bytes produces a file that no Microsoft
XNA 4.0 runtime ever produced or consumed, and CNA's API, CLI help and diagnostics all say so.

| `--xnb-platform` | Byte | Category |
|---|---|---|
| `windows` | `w` | **Microsoft XNA 4.0 target** |
| `windowsphone` | `m` | **Microsoft XNA 4.0 target** (Windows Phone 7) |
| `xbox360` | `x` | **Microsoft XNA 4.0 target** |
| `desktopgl` | `d` | extended ecosystem |
| `linux` | `l` | extended ecosystem (legacy alias) |
| `windowsgl` | `g` | extended ecosystem (legacy alias) |
| `ios` | `i` | extended ecosystem |
| `android` | `a` | extended ecosystem |

`XnbFileOptions` exposes this through `IsXna40TargetPlatform()`, and the writer refuses
combinations that are contradictory on their face — LZ4 compression on an XNA 4.0 target platform,
for instance, because that pairing describes a file that cannot exist.

**Writing `x` into the header is not Xbox 360 support, and the writer now refuses to pretend it
is.** The 360 is big-endian. CNA has exactly one piece of Xbox-specific payload handling — the
`SoundEffect` WAVEFORMATEX fields are byte-swapped — and nothing else is: not a texture's `Int32`
dimension fields, not a model's bone table, not even the sound's own sample bytes, which CNA's
**reader** already refuses to transcode out of an Xbox file for exactly that reason.

So `--xnb-platform xbox360` is **refused by default**, in code rather than only in documentation,
with a message naming what is missing. `--xnb-allow-unverified-xbox` (or
`XnbFileOptions::allowUnverifiedXboxPayloads`) produces one anyway — because somebody with real
hardware producing candidate files is the only way this gap ever closes, and a refusal with no
escape hatch would prevent that.

Windows Phone 7 (`m`) is **not** refused. It is a little-endian ARM platform with no known payload
difference from Windows, so the honest statement is narrower: the header byte can be emitted, the
payloads are the same ones Windows gets, and **nothing about it is verified** because no
`m`-platform fixture and no Windows Phone runtime exist here. That is a documentation claim, not a
byte-order one, which is why it is not enforced in code.

### 2.3 Version

Version **5** is the XNA 4.0-era container and is CNA's default. Version **4** is *earlier* XNB
(XNA 3.x era), not a variant of 4.0; CNA can write it under an explicit `--xnb-version 4` opt-in and
applies the inverse of the reader's legacy `SurfaceFormat` numbering, refusing any format version 4
cannot express.

### 2.4 Type-reader names

The type-reader table stores assembly-qualified .NET type names. CNA keeps every spelling in one
data table (`XnbReaderIdentity`) with a **per-entry record of where the evidence came from**, so a
correction is a one-line data change:

| Evidence | Meaning |
|---|---|
| `Xna40Fixture` | Read out of a genuine Microsoft XNA 4.0 `.xnb` committed to this repository. |
| `MonoGameFixture` | Read out of a MonoGame-produced `.xnb` committed to this repository. |
| `DerivedRule` | Not directly observed; produced by the rule below, which every observed file follows. |

The rule, derived from the fixtures: **a reader type is assembly-qualified exactly when it does not
live in `Microsoft.Xna.Framework`; generic arguments are always fully qualified.** No name in this
table was taken from MonoGame or FNA source code.

`--xnb-reader-names` selects between `xna40` (the table above, the default and the most compatible)
and `portable` (bare names). CNA, FNA and MonoGame all normalize the assembly qualifier away before
lookup, so portable names load *there*; **a Microsoft XNA 4.0 runtime is not known to accept them**,
which is why they are not the default.

---

## 3. Type coverage and how each is verified

`cna-rt` means a round trip through CNA's own reader with values asserted. `spec` means
`tools/xnb/xnb_conformance.py` parses the file and matches a hand-written expectation manifest.
`golden` means byte-identical to externally produced output.

### 3.1 Primitives and framework value types

| Type | Reader written | cna-rt | spec | XNA 4.0 |
|---|---|---|---|---|
| `Boolean`, `Byte`, `SByte`, `Int16`, `UInt16`, `Int32`, `UInt32`, `Int64`, `UInt64`, `Single`, `Double` | matching `…Reader` | ✅ | ✅ | — |
| `Char` | `CharReader` (UTF-8 encoded; a lone surrogate is refused) | ✅ | ✅ | — |
| `String` | `StringReader` (7-bit-encoded UTF-8 byte length; invalid UTF-8 is refused) | ✅ | ✅ | — |
| `TimeSpan`, `DateTime` | matching `…Reader` | ✅ | — | `DateTime`'s top two `Kind` bits are written as Unspecified, because `System::DateTime` does not model a kind; the reader masks them for the same reason. |
| `Decimal` | `DecimalReader` | ✅ | — | Four Int32 words — lo, mid, hi, flags — behind `SHARP_RUNTIME_HAS_NATIVE_INT128`; see below. |
| `Vector3`, `Matrix`, `Rectangle` | matching `…Reader` | ✅ | ✅ | — |
| `Vector2`, `Vector4`, `Quaternion`, `Color`, `Point`, `Plane`, `BoundingBox`, `BoundingSphere`, `Ray` | matching `…Reader` | ✅ | — | The independent parser has no decoder for these yet; that is a gap in the parser, not in the writer. |
| `BoundingFrustum` | `BoundingFrustumReader` | ✅ | — | The one .NET *class* in this group, so it is serialized by reference. The payload is the source `Matrix` alone; the planes and corners are recomputed on the reading side. |
| `CurveKey`, `Curve` | `CurveReader` | ✅ | ✅ | — |

`Decimal` needs `System::Decimal`, which sharp-runtime provides only where it reports
`SHARP_RUNTIME_HAS_NATIVE_INT128`. Both the writer and the reader carry that same conditional, so
on a toolchain without a native 128-bit integer neither side exists and a `Decimal` cannot be
written at all — the type is absent rather than silently degraded.

### 3.2 Collections

| Type | cna-rt | spec | Notes |
|---|---|---|---|
| `List<String>` | ✅ | ✅ | Also **`golden`**: see §4. |
| `List<Int32>`, `List<Char>`, `List<Rectangle>`, `List<Vector3>` | ✅ | ✅ | The instantiations CNA's own runtime reader registry resolves. |
| `Dictionary<String, Int32>` | ✅ | — | Written in deterministic key order. The independent parser has no dictionary decoder yet. |
| `Nullable<T>`, `T[]` | ⚠️ `impl` | — | The writer templates exist and work; no instantiation is registered by default, because no built-in CNA **reader** resolves one. Registering one is the documented extension point. |
| `Enum<T>` | ✅ | — | Written as the underlying `Int32`, under `EnumReader\`1[[<enum>]]`, read back by `EnumTypeReader<T>`. Not registered by default: the enum's .NET name cannot be recovered from a C++ enum, so it is supplied at registration — `RegisterXnbEnumWriter<T>(registry, "Namespace.Enum", assembly)`. A value can never reach the wrong enum's writer, because the registry is keyed by the C++ type. |

Arbitrary nesting works: the writer interns readers by formatted name on first use, so
`List<Dictionary<String,Int32>>` needs only its own registration, not a new mechanism.

### 3.3 Assets

| Asset | Reader written | cna-rt | spec | Notes |
|---|---|---|---|---|
| `Texture2D` | `Texture2DReader` | ✅ | ✅ | Every mip level; uncompressed and DXT block bytes. |
| `Texture3D` | `Texture3DReader` | ✅ | ✅ | |
| `TextureCube` | `TextureCubeReader` | ✅ | ✅ | Six faces, asserted distinct. |
| `SpriteFont` | `SpriteFontReader` | ✅ | ✅ | Nested `Texture2D` plus four nested list readers; mismatched list lengths are refused. |
| `SoundEffect` | `SoundEffectReader` | ✅ | ✅ | WAVEFORMATEX extension bytes and loop region; Xbox 360 byte-swap has its own test. |
| `Song` | `SongReader` | ✅ | ✅ | Bare media-path string plus a duration dispatched through `Int32Reader` — see §5. |
| `Video` | `VideoReader` | ✅ | ✅ | |
| `Curve` | `CurveReader` | ✅ | ✅ | |
| `Model` | `ModelReader` | ✅ | ✅ | Bones, hierarchy, meshes, parts, tags, shared vertex/index/effect resources, the byte-versus-uint32 bone-reference width rule, and the `VertexDeclarationReader` a real `Model` file lists but never dispatches to. |
| `VertexDeclaration`, `VertexBuffer`, `IndexBuffer` | matching `…Reader` | ✅ | ✅ | Verified through the `Model` fixture, which carries all three as shared resources. |
| `BasicEffect` | `BasicEffectReader` | ✅ | ✅ | Verified through the `Model` fixture. |
| `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect` | matching `…Reader` | ✅ | — | Including each one's external texture reference. The independent parser can decode all four; no committed fixture exercises them yet. |
| `Effect` (already-compiled bytecode) | `EffectReader` | ✅ | ✅ | The bytecode is written verbatim; an empty payload is refused rather than written as a loadable asset. **CNA does not compile `.fx` — see §6.** |
| `EffectMaterial` | `EffectMaterialReader` | ✅ | ✅ | An inline effect reference plus a `Dictionary<String,Object>` whose values each carry their own dispatch index. `Boolean`, `Int32`, `Single`, `Vector2`, `Vector3`, `Vector4`, `Matrix`, `Quaternion` and a boxed external reference are written; **array-valued parameters are not** — see below. |
| `ExternalReference<T>` | `ExternalReferenceReader`, and the inline form | ✅ | ✅ | Both forms: inline in a known field (no dispatch index, what every stock effect uses) and boxed where the static type is `object`. Absolute and escaping references are refused in both. |

---

## 4. The strongest results: byte-identical to two other producers' output

Three assets in this repository are written by CNA and compared, byte for byte, against files
produced by somebody else. That is the strongest verification available without running a real
runtime, because it compares output to output rather than output to a description.

| Fixture | Producer | What it proves |
|---|---|---|
| `ContentManifestListStrings.xnb` | official **XNA 4.0** `BuildContent` | All 10 header bytes, the type-reader table spelling, the 7-bit encodings, the object-dispatch protocol and the string encoding, against Microsoft's own Content Pipeline. |
| `white-1.xnb` | MonoGame (`mgcb`) | The `Texture2D` writer's field layout and its reader name, against a second independent producer. One opaque white pixel — the smallest thing that can carry the whole shape. |
| `audio/tone_mono_44khz_16bit.xnb` | MonoGame (`mgcb`) | The `SoundEffect` framing: a length-prefixed WAVEFORMATEX block written field by field, then the samples, then loop region and duration. |

The tests are `XnbWriterTest.GoldenXna40ListOfStringsIsByteIdentical`,
`.GoldenMonoGameTexture2DIsByteIdentical` and `.GoldenMonoGameSoundEffectIsByteIdentical`.

The third one changed the writer. CNA emitted the 16-byte `PCMWAVEFORMAT` block, which omits
`cbSize`; the observed producer emits the full 18-byte `WAVEFORMATEX` with `cbSize` present and
zero. The field is length-prefixed so both are legal and CNA's reader accepts both — but matching
an observed producer beats matching none, so the writer now emits 18. Every `SoundEffect` CNA
writes is two bytes longer than it was.

---

## 5. A defect the writer found in the reader

Building a writer against a reader exposes disagreements the reader alone cannot show. CNA's
`SongReader` consumed the duration's **dispatch byte** as the low byte of the duration: a real
content pipeline writes a Song as a bare media-path string followed by an `Int32` dispatched
through `Int32Reader`, and CNA read a bare `Int32`. The committed MonoGame fixture therefore
reported 769282 ms for a three-second clip — and its own provenance manifest had recorded that
misparse as fact.

It is fixed, the encoding is now selected from the type-reader table size exactly as `VideoReader`
already did, and the fixture manifest records 3005 ms, corroborated independently from the
companion `.ogg` file's last Ogg page granule position (132480 samples at 44100 Hz = 3004.08 ms)
rather than from CNA's own reader.

---

## 6. What CNA does **not** do

Stated plainly, because a gap named is worth more than a gap rounded up.

| Not supported | Detail |
|---|---|
| **Compiling `.fx` to shader bytecode** | CNA does not host an HLSL compiler, and that is a standing decision rather than an oversight (`plans/plan_fx.md`: "Out of scope; CNA will not embed an HLSL source compiler"). It will not fake it by embedding source text in an `Effect` asset. What it *does* do is build an already-compiled `.fxb` into an `Effect` `.xnb` with the bytecode byte for byte — see §7. Compile the `.fx` with `fxc` at profile `fx_2_0`, which is what XNA's own Content Pipeline used. |
| **LZX compressed output** | The scheme XNA 4.0 itself produced. CNA reads it and cannot write it; see §2.1. An uncompressed file loads everywhere LZX does, so this costs file size and nothing else. |
| **Xbox 360 semantics** | Beyond the `SoundEffect` WAVEFORMATEX byte swap, nothing is endian-corrected or tiled — and the writer refuses an `x` target rather than emitting Windows bytes under an Xbox header. `--xnb-allow-unverified-xbox` overrides it for hardware testing. |
| **Windows Phone semantics** | The header byte is written and the payloads are Windows's, which is very likely correct for a little-endian ARM target and is nonetheless **unverified**: no `m` fixture and no Windows Phone runtime exist here. |
| **Array-valued effect parameters** | An `EffectMaterial` parameter that is `float[]`, `int[]` or `Matrix[]` is not written. Which reader instantiation XNA writes for one — `ArrayReader` or `ListReader`, over which element type — is not established by any fixture available here, and a guess would produce a file that loads into the wrong shape rather than one that fails to load. CNA's **reader** applies them if some other producer writes them. |
| **`Decimal`** | Conditional on sharp-runtime's 128-bit integer support, on the read side too. |
| **A block-compressed `.cnb`** | CNB texture schema 1 is frozen to `Rgba8`. A `.cnb` build that asks for DXT keeps the uncompressed pixels and warns; the `.xnb` build gets the compressed texture. |

---

## 7. Source assets: what compiles to `.xnb`

The output format is chosen at the **writer** boundary only, so every source route reaches both
containers and no importer or processor is duplicated.

| Source | Route | `.cnb` | `.xnb` |
|---|---|---|---|
| PNG, JPEG, BMP, TGA, … | `ImageImporter` → `TextureProcessor` → texture writer | ✅ | ✅ `Texture2D` |
| `.spritefont` + a font file | `FontDescriptionImporter` → `FontDescriptionProcessor` → SpriteFont writer | ✅ | ✅ `SpriteFont` |
| `.wav` | `WavImporter` → `SoundEffectProcessor` → SoundEffect writer | ✅ | ✅ `SoundEffect` |
| `.gltf`, `.glb` | glTF importer → model processor → model writer | ✅ | ✅ `Model` |
| `.cnj` | CNJ importer, then the matching processor | ✅ | ✅ |
| `.fxb` (already-compiled effect) | `CompiledEffectImporter` → `CompiledEffectProcessor` → Effect writer | — | ✅ `Effect` |
| `.xnb` | XNB importer (transcode to `.cnb`) | ✅ | — |

Texture policy parameters (`textureFormat`, `generateMipmaps`, `premultiplyAlpha`,
`resizeToPowerOfTwo`, `colorKey`) apply in that documented order. **`premultiplyAlpha` defaults to
`false` here where XNA 4.0's `TextureProcessor` defaults it to `true`** — CNA's default is
conservative because flipping it changes the bytes of every alpha-bearing texture already built by
this pipeline. `BlendState::AlphaBlend`, which `SpriteBatch::Begin()` selects by default in both
frameworks, is the premultiplied blend, so a project reproducing XNA's appearance should set the
parameter. This is tracked as `XNAP-96`.

A `.fxb` has no `.cnb` route, deliberately: the CNB container reserves an `Effect` identifier and
has no schema for it, because CNA has many renderers and a `.cnb` carrying one API's shader
bytecode would be useless on the others. A `.fx` has no importer at all, so a build tree containing
one reports that nothing imports it — which is the honest answer when there is no compiler.

Audio sources may be 8-bit unsigned, 16-bit, 24-bit or 32-bit PCM, or 32-bit IEEE float. Everything
wider than 16 bits is narrowed to the Pcm16 both containers store, deterministically, **with a
warning naming the loss**. Compressed WAV tags are refused rather than guessed at.

### Using it

```bash
cna-content build Content -o bin/Content --format xnb \
    --xnb-platform windows --xnb-version 5 --xnb-profile reach
```

Per-asset overrides, including `--format`, live in `.cna-content.json` beside the source root.

---

## 8. Writing a type CNA does not know about

The type-writer registry is keyed by C++ type and takes registrations from anywhere, so a game can
put its own asset into an `.xnb` without changing CNA. Two halves are needed, and neither is
optional: a writer, and a reader that gives the written bytes meaning.

```cpp
// The writer half. RequireCollectionCount(), the nesting-depth guard, the string ceilings and
// every other limit apply to it exactly as they do to a built-in, because it writes *through*
// XnbWriter rather than around it.
class WaypointListXnbWriter final : public XnbTypeWriter<WaypointList>
{
public:
    [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
    {
        XnbReaderIdentity identity;
        identity.readerBaseName = "ExampleGame.Content.WaypointListReader";
        identity.readerAssembly = XnbAssembly::None;   // not an XNA assembly, so unqualified
        identity.targetBaseName = "ExampleGame.WaypointList";
        identity.targetAssembly = XnbAssembly::None;
        identity.evidence = XnbNameEvidence::DerivedRule;
        return identity;
    }

    [[nodiscard]] bool IsSerializedByReference() const noexcept override { return true; }

protected:
    void Write(XnbWriter& output, const WaypointList& value) const override
    {
        output.RequireCollectionCount(value.points.size(), "WaypointListWriter");
        output.WriteInt32(static_cast<std::int32_t>(value.points.size()));
        for (const Vector3& point : value.points) { output.WriteVector3(point); }
    }
};

XnbTypeWriterRegistry registry;
RegisterBuiltInXnbWriters(registry);
registry.Register(std::make_shared<const WaypointListXnbWriter>());

// The reader half, registered under exactly the name the writer emits.
ContentTypeReaderManager::AddTypeCreator(
    "ExampleGame.Content.WaypointListReader",
    [] { return std::make_unique<WaypointListXnbReader>(); });

const std::vector<std::uint8_t> file = WriteXnbAsset(waypoints, {}, "waypoints", registry);
```

`XnbWriterTest.AGameCanRegisterItsOwnTypeWriterAndReaderAndRoundTripThroughThem` is exactly this,
run end to end, including the check that a custom writer inherits the same ceilings the built-ins
have.

The same mechanism is how you register a generic instantiation CNA does not ship. Only the closed
instantiations CNA's own runtime reader registry resolves are registered by default —
`List<String>`, `List<Int32>`, `List<Char>`, `List<Rectangle>`, `List<Vector3>` and
`Dictionary<String,Int32>` — so that a file this writer produces always has a reader on the other
side. `List<Single>` is not a gap; it is one `registry.Register(...)` call plus its reader.

At the pipeline level, a custom `ContentTypeWriter` reaches `.xnb` by returning
`ContentOutputFormat::Xnb` from `OutputFormat()`. `docs/content-pipeline.md`'s **Custom
extensions** section covers the rest of that contract — stable component identities, schema and
codec versioning, and the source/toolchain compatibility model.

---

## 9. Provenance

Every wire-format decision in this writer comes from one of three places, and nothing comes from a
fourth.

| Source | What came from it |
|---|---|
| **Committed fixtures, read as bytes** | Every reader type-name spelling and assembly qualifier, the object-dispatch protocol, the `Song` duration encoding, and the per-type field layouts. Each name carries a `XnbNameEvidence` value recording whether a genuine XNA 4.0 file, a MonoGame file, or the derived rule is behind it. |
| **CNA's own reader** | The counterpart of every writer: the writer was built against the reader, which was written first and independently, and the round-trip tests are the check that they agree. |
| **Published format descriptions** | The container header, 7-bit encoding, .NET `BinaryWriter` string encoding, the S3TC block layouts, the LZ4 block grammar, and the RIFF/WAVE chunk rules. |

Nothing in the writer, the block-compression encoder, the LZ4 encoder, the `.spritefont` route or
the independent conformance parser is derived from MonoGame's or FNA's implementation, and no
Microsoft binary was decompiled. The MonoGame `.xnb` files in `tests/assets/xnb/` are used as
**black-box fixtures** — bytes to read and compare against — and each carries a manifest recording
its origin and licence (they are Ms-PL, the same licence as CNA).

Third-party dependencies the pipeline introduces are recorded in `THIRD_PARTY_NOTICES.md`:
**FreeType** (FreeType License; system-provided, linked only by the build-time module, and only for
the `.spritefont` route) and the vendored **Liberation Mono** test font (SIL OFL 1.1, with
`tests/assets/fonts/PROVENANCE.json` carrying upstream, distributor, SHA-256 and the reason it is
in the tree). Both are build-time and test-time only; no CNA runtime module links or embeds either.

---

## 10. Reproducing every claim in this document

```bash
# The writer's own unit tests, the round trips, and the golden comparison.
./CnaContentTests --gtest_filter='Xnb*Writer*:XnbConformance*:XnbInteropCorpus*'

# The build-time module: block compression, the .spritefont route, the compressed texture route.
./CnaContentPipelineTests

# The independent, specification-based parser, run by hand over any .xnb.
python3 tools/xnb/xnb_conformance.py --json path/to/asset.xnb
python3 tools/xnb/xnb_conformance.py --expect path/to/asset.expected.json path/to/asset.xnb
```

The parser shares no code, no headers and no constants with CNA. It was written from the format
description, and it has already earned its keep: it caught a hand-written expectation manifest that
claimed `CurveLoopType.Linear` was ordinal 1 when the file — correctly — said 4.

## 11. Running the XNA 4.0 harness

`tests/interop/xna40/` contains a .NET 4.0 console harness that loads every fixture in
`tests/assets/xnb/cna/windows/uncompressed/` through a real `ContentManager` and asserts field
values, not merely that `Load<T>()` returned. Its README carries the build and run instructions and
the protocol for recording the result.

**It has never been run against a real XNA runtime.** Until someone runs it and records the outcome,
no table in this repository may claim XNA 4.0 verification, and none does.
