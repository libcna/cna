# `.xnb` interoperability: what CNA writes, and how strongly each claim is verified

CNA has a native C++ XNB **writer** and source pipeline. Supported native routes build without a
managed XNA/MonoGame build host; some routes still need external services, such as legacy `fxc`
for `.fx` or a configured XMA encoder. This page distinguishes the current implementation from
retained fixture/oracle results and their qualification limits.

The [runtime loader guide](xnb-content-pipeline-support.md) covers loading
already-built `.xnb` files. The writer's design decisions are §12 below; its task history (`XNAP-*`)
is in Git history.

> **Provenance note (2026-09-03) — one implementation, and why the history mentions two.** This
> document, and the plan behind it, were written believing no XNB writer existed in the repository.
> That was true of the `next` branch and false of the repository: a second implementation of the
> same subsystem had already been developed on the `pipeline` branch, and the duplication was only
> discovered *after* the implementation described here had been written. Both histories were then
> reconciled: **one implementation survives** — `CNA::Internal::Xnb`, described in §0 — and the
> features worth keeping from the superseded one (the `Decimal`, `BoundingFrustum` and `Enum<T>`
> writers, the manifest's `rootReaderName` identity, and its architecture documentation) were
> ported into it rather than left behind. The superseded paths were removed and a test fails if
> they reappear.
>
> So the duplicate implementation is **historical provenance, not a current second architecture**.
> The mistake stays on the record — the deleted `plans/plan_xnapipeline.md` §0 and §0.5 (Git history) give the full account,
> including what it cost — because an audit that checks one branch and concludes something about a
> repository is a mistake worth being able to find again. Every capability claim below is about the
> single surviving implementation.

> **The genuine runtime has loaded what CNA writes.** The harness in `tests/interop/xna40/` loads
> CNA-written `.xnb` files through Microsoft's own XNA 4.0 `ContentManager` (runtime 4.0.0.0 under
> Wine with DXVK, on a real GPU) and asserts the fields its public-API adapters expose. Logged `(not asserted)` gaps remain: for
> example SoundEffect checks Duration, Song checks load/Duration without playback, and Video
> checks dimensions/frame rate. The eight committed
> fixtures pass uncompressed and LZX-compressed, and `Effect`, `Song` and `Video`, built from CNA's
> own sources, pass too (`XNAPP-280`/`281`; re-run 2026-10-29, 3/3 CTests). The per-type tables below
> still carry the labels they were written with; §11 says exactly what the runtime has covered.

---

## 0. Where the code is

Three layers, separated so that source decoding never leaks into serialization:

| Layer | Namespace | Location | Role |
|---|---|---|---|
| Format | `CNA::Internal::Xnb` | `modules/content/{include/CNA/Internal/Xnb,src/Xnb}` | Writes the `.xnb` container and object graph. Knows nothing about PNG, WAV or glTF. |
| Pipeline | `CNA::Content::Pipeline` | `modules/content/{include,src}/…/Pipeline` | Importers, processors, canonical content values, the build graph. Shared by both output formats. |
| Build-time only | `CNA::Content::Pipeline` | `modules/content-pipeline/` | Font rasterization, block compression — linked by the compiler, never by the runtime. |
| Tool | — | `tools/content`, `cmake/ToolContentPipeline.cmake` | `cna-content`, `cna_add_content()`. |

```text
                     source asset
                          │
                    ContentImporter          ← one per registered source route
                          │
                   ContentProcessor          ← policy: colour key, mips, resampling, conversion
                          │
              canonical content value        ← CnbTextureData, CnbSpriteFontData, Curve, …
                          │
        ┌─────────────────┴─────────────────┐
   ContentTypeWriter                   XNB pipeline writer
        │                                   │
   Cnb::CnbWriter                      Xnb::XnbWriter
        ▼                                   ▼
      .cnb                                .xnb
```

**`XnbContentPipeline` and `XnbOutputContentPipeline` are not two writers.** The names are close
enough to invite the mistake, so plainly: `XnbContentPipeline` treats `.xnb` as a **source** and
transcodes it into `.cnb`; `XnbOutputContentPipeline` is the **output** side that serializes a
processed value into `.xnb`. One reads the format, one writes it. There is exactly one XNB writer
implementation in the tree — `CNA::Internal::Xnb` — and
`XnbArchitectureUniquenessTest` fails if a second one is reintroduced under the paths a superseded
implementation once used.

The registry/coordinator and many import/processing functions are shared. Processing can still
observe output format, target and profile: font atlas encoding and Reach constraints are examples.
Native conventions and XNA project mappings can also select different defaults. Writers enforce
additional format limits; generic XML and legacy effect routes have XNB writers only. Shared
infrastructure does not mean every source/parameter combination reaches both containers.

The [native pipeline guide](content-pipeline.md) documents the build system itself — component selection, parameters,
dependencies, the manifest, incremental builds, publication — all of which applies to both formats
unchanged.

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
| `xna40` | Loaded by a genuine Microsoft XNA 4.0 runtime, with values asserted. The tables do not use it per row; §11 lists what the runtime has loaded. |
| `none` | Not verified. |

These are complementary evidence types, not a universal strength ladder. Each applies to named
fixtures, asserted fields and configurations. A parser or CNA round trip can miss a genuine-XNA
requirement (the LZX trailer is the recorded example); a byte golden does not qualify every input
or platform. Labels summarize retained results, not automatic proof that an optional test ran now.

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
| `0x80` | LZX — the compression Microsoft XNA 4.0 itself produced | **written** (`XNAP-81`), and the only compressed form an XNA 4.0 target may carry |

### 2.1.1 What CNA's LZX encoder emits

`--xnb-compress lzx` produces a genuinely compressed LZX stream, not a compressed flag around
unchanged bytes. Per output frame it emits **one LZX verbatim block** carrying:

* freshly transmitted **main and length Huffman trees**, delta-coded against the previous block's
  and length-limited to the decoder's own table width (12 bits), so every code resolves in the
  direct-lookup path and every transmitted tree is complete;
* **repeated-offset matching** through the real `R0`/`R1`/`R2` LRU queue, preferred over an
  explicit slot at equal match length because it costs no offset bits at all;
* **position-slot offset encoding** over the full 64 KiB window;
* greedy longest-match search over a bounded hash chain, with matches up to LZX's own 257-byte
  ceiling.

Three things it deliberately does **not** emit, each stated so nobody has to read the code to find
out:

| Not emitted | Why |
|---|---|
| Aligned-offset blocks | A fourth Huffman tree that saves a few bits on offset-heavy data. Verbatim-only is a conforming subset; CNA's reader and the independent parser both decode aligned blocks, so a file from another producer that uses them still loads. |
| Uncompressed LZX blocks | The encoder chooses verbatim blocks only. Uncompressed blocks are valid and could reduce overhead on incompressible input, but their emission/realignment path is not implemented here. |
| Intel `E8` call translation | CNA writes the header bit as zero. The retained fixtures do not establish a need for it; CNA/FNA decoding refuses a non-zero transform file size rather than implementing the transform. |

An LZX block and an output frame coincide, one block per frame. That costs a little ratio (trees
are retransmitted every 32 KiB, and a match may not reach across a frame boundary) and buys
correctness against the reference framing loop, whose run accounting has no guard for a match that
overshoots its own frame.

**Compression is deterministic**: the hash function, the search depth, the Huffman tie-breaking and
the block partitioning are all fixed, so the same payload always compresses to the same bytes and
an incremental build never rewrites an unchanged compressed asset. The compression selection is
part of the build fingerprint, so changing it rebuilds rather than reusing stale bytes.

Compression is a **container** concern and changes no observed value: the LZX and uncompressed
fixture corpora share their expectation manifests byte for byte, and a test asserts it.

Measured on the committed corpus and on a 64×64 RGBA texture: a 16 571-byte uncompressed texture
`.xnb` becomes 12 181 bytes; 200 KB of repetitive text compresses below 2 %; uniform random bytes
**grow** by roughly 0.4 % plus the trees, which is what incompressible data costs in any format —
build such an asset uncompressed.

**Verification.** Three independent oracles, deliberately not one: CNA's own LZX decoder (a port
predating this encoder, tested against retained external files) round-trips every
case; `tools/xnb/xnb_conformance.py` carries **its own LZX decoder**, written from the format
description in another language sharing no code, constants or tables, and it decodes every
CNA-produced LZX fixture down to asset values — the same decoder reproduces both externally
produced LZX fixtures byte for byte against FNA's own reference output, which is what earns it the
right to judge CNA's encoder; and the container framing is asserted field by field against the
layout those two external fixtures demonstrate. Those decoders still accepted the missing-trailer
output that genuine XNA rejected; the separate runtime evidence in §11 is essential to that case.

**What LZX does not give you** is integrity checking: neither LZX nor the XNB container carries a
checksum, so a truncated or corrupted stream can decode to the declared byte count and be wrong.
That is a property of the format. A mutation may be rejected, change decoded values, or affect only unused/padding bytes. Neither
successful decoding nor the finite fuzz suite provides an integrity guarantee.

### 2.2 Target platforms — writer choices and runtime qualification

This is the most common thing to get wrong about XNB, so it is stated plainly: **Microsoft XNA 4.0
shipped for three platforms.** Every other platform byte the format carries was introduced by a
later, non-Microsoft implementation. Such identifiers describe an extended ecosystem, not a Microsoft target. The writer currently
exposes eight choices below; the runtime reader accepts a wider 16-byte whitelist. Neither an
emitted nor an accepted header byte establishes native-platform execution.

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

**Writing `x` into the header does not qualify Xbox 360 runtime support.** CNA has explicit
SoundEffect WAVEFORMATEX byte swaps, but its writer still refuses by default where payload
qualification is missing. This is current conservative policy, not a measured rule that every XNB
integer/field must use the console CPU's native byte order. The retained XML/compiler comparison
already reproduces a supported Xbox-target payload byte for byte. The target also selects Compact
Framework assembly identities (§2.4), and console textures/effects/audio need their own evidence.

So `--xnb-platform xbox360` is **refused by default**, in code rather than only in documentation,
with a message naming what is missing. `--xnb-allow-unverified-xbox` (or
`XnbFileOptions::allowUnverifiedXboxPayloads`) produces one anyway — because somebody with real
hardware producing candidate files is the only way this gap ever closes, and a refusal with no
escape hatch would prevent that.

Windows Phone 7 (`m`) is not refused. Target-specific Compact Framework qualifiers are verified
by retained compiler outputs, including `phone_font_description.xnb`; most payload processing is
shared with the Windows route. No Phone runtime/device qualification is recorded. Those finite
compiler/name witnesses are stronger than no evidence and weaker than a working Phone game.

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

For built-in identities, the observed rule qualifies readers outside `Microsoft.Xna.Framework`
and fully qualifies generic arguments. System arguments use target-specific mscorlib identities:
desktop 4.0.0.0, or Compact Framework 3.7.0.0 with distinct Xbox/Phone public-key tokens. These
are retained build witnesses, not native-device results. User-defined names need their own assembly
identity; do not infer one from the built-in rule.

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
| `TimeSpan` | `TimeSpanReader` | ✅ ticks | — | — |
| `DateTime` | `DateTimeReader` | ✅ ticks and Kind | — | `golden` for all three Kinds; local payloads carry UTC ticks and are converted to the receiving local zone. See §13 for measured scope. |
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
| `List<Int32>`, `List<Char>`, `List<Rectangle>`, `List<Vector3>`, `List<Matrix>` | ✅ | ✅ | The instantiations CNA's own runtime reader registry resolves. A test asserts that *every* built-in writer's reader name resolves there, so the two registries cannot drift. |
| `Dictionary<String, Int32>` | ✅ | — | Written in deterministic key order. The independent parser has no dictionary decoder yet. |
| `Vector3[]` | ✅ | — | The one array instantiation with a built-in reader — a real XNA `Model` names `ArrayReader<Vector3>` in its own type table. Written through the `XnbArray<T>` carrier, which is how a registry keyed by C++ type tells `Vector3[]` from `List<Vector3>`: both are `std::vector<Vector3>`. |
| `Nullable<T>`, other `T[]` | ⚠️ `impl` | — | The writer templates exist and work, and the documented extension path is exercised end to end (`ANullableInstantiationRoundTripsThroughTheDocumentedExtensionPath`, `AnArrayTypedGenericArgumentSurvivesTheWholeRoundTrip`). No further instantiation is registered *by default*, because no built-in CNA **reader** resolves one and a writer with no reader produces a file CNA itself could not load. |
| `Enum<T>` | ✅ | — | Written with the enum's declared 1-, 2-, 4- or 8-byte signed/unsigned underlying type, under `EnumReader\`1[[<enum>]]`, and read back by `EnumTypeReader<T>` through the matching primitive width. Not registered by default: the enum's .NET name cannot be recovered from a C++ enum, so it is supplied at registration — `RegisterXnbEnumWriter<T>(registry, "Namespace.Enum", assembly)`. A value can never reach the wrong enum's writer, because the registry is keyed by the C++ type. |

Arbitrary nesting works: the writer interns readers by formatted name on first use, so
`List<Dictionary<String,Int32>>` needs only its own registration, not a new mechanism.

**A reader's generic arguments are not always its target type's.** `ListReader<T>` produces
`List<T>`, `DictionaryReader<K,V>` produces `Dictionary<K,V>` and `NullableReader<T>` produces
`Nullable<T>` — reader and target share one argument list. `EnumReader<TEnum>` produces the plain,
non-generic `TEnum`, and `ArrayReader<T>` produces `T[]`, whose element type is already spelled in
the target name. `XnbReaderIdentity::targetSharesGenericArguments` records the difference; without
it a nested identity spelled `SurfaceFormat[[SurfaceFormat]]` or
`System.Int32[][[System.Int32]]` — names no runtime resolves. `XnbReaderIdentityTest` covers the two
direct cases, ten nested combinations, and a structural invariant walked over every built-in
identity, and asserts those two malformed spellings absent.

Auditing that model found two defects on the *reading* side, both fixed: `XnbTypeName` could not
parse a .NET array type name at all (`System.Int32[]` read as `System.Int32` plus a malformed
argument list), so a genuine `.xnb` naming `List<int[]>` failed while parsing its type table; and
the writer's `XnbArray<T>` carrier was not marked as a .NET reference type, so a nested array was
written without the dispatch index its reader consumes.

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
| `Effect` (already-compiled bytecode) | `EffectReader` | ✅ | ✅ | The bytecode is written verbatim; an empty payload is refused rather than written as a loadable asset. CNA does not *embed* an HLSL compiler, but can drive an external one — see §6. |
| `EffectMaterial` | `EffectMaterialReader` | ✅ | ✅ | An inline effect reference plus a `Dictionary<String,Object>` whose values each carry their own dispatch index. `Boolean`, `Int32`, `Single`, `Vector2`, `Vector3`, `Vector4`, `Matrix`, `Quaternion` and a boxed external reference are written; **array-valued parameters are not** — see below. |
| `ExternalReference<T>` | `ExternalReferenceReader`, and the inline form | ✅ | ✅ | Both forms: inline in a known field (no dispatch index, what every stock effect uses) and boxed where the static type is `object`. Absolute and escaping references are refused in both. |

Every one of these is recorded in the build manifest by the reader its root object dispatches to,
observed from the write (`rootReaderName`, introduced in manifest schema 9; current schema 10).
XNB has per-reader version fields, but no CNB-style global asset type/schema pair. The root reader
name is part of the artifact identity; component/codec/schema identities must still change when
implementation compatibility changes. It is not a substitute for every version or dependency.

---

## 4. The strongest results: byte-identical to two other producers' output

Three early writer fixtures compare CNA bytes with external output. Later XML/font/model/sample
witnesses extend the evidence elsewhere; this is not a count of every golden asset in the tree. That is the strongest verification available without running a real
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
an observed producer beats matching none, so the writer now emits 18. The ordinary PCM framing changed by two bytes; this is not a fixed-size claim for every codec's
format extension.

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
| **No embedded HLSL compiler** | The `.fx` source route drives a configured external EffectCompilerService, resolves includes as dependencies and validates the returned legacy effect payload. Supply `--fx-compiler`/`CNA_FXC` and a launcher where needed; CNA does not ship Microsoft's legacy fxc. Real June 2010 `fx_2_0`/Wine comparisons now exist (XNAPP-191/192): seventeen effects account for compiler-version-string/padding differences, and pass-dependent XNA headers are emitted. Tests without that configured compiler can skip; stand-in process tests prove integration only. See [effect compilation](fx-compiled-effects.md) and the native guide for scope and precedence. |
| **Aligned-offset and uncompressed LZX blocks** | CNA *reads* both and emits neither: verbatim blocks are a conforming subset and the other two buy nothing here. See §2.1.1. |
| **Xbox 360 runtime qualification** | The writer remains opt-in for unverified Xbox payloads. It has target-specific reader identities and finite genuine-compiler XML/font witnesses, plus SoundEffect format-field swaps. This is not native-console qualification, proof of correct texture tiling/audio samples, or a Xenos effect compiler. `--xnb-allow-unverified-xbox` is a testing escape hatch. |
| **Windows Phone runtime qualification** | The `m` header and target-specific Compact Framework reader qualifiers can be written and have retained compiler/name witnesses. No working Phone runtime/device run is recorded; shared Windows payload paths do not establish complete Phone semantics. |
| **Array-valued effect parameters** | An `EffectMaterial` parameter that is `float[]`, `int[]` or `Matrix[]` is not written. Which reader instantiation XNA writes for one — `ArrayReader` or `ListReader`, over which element type — is not established by any fixture available here, and a guess would produce a file that loads into the wrong shape rather than one that fails to load. CNA's **reader** applies them if some other producer writes them. |
| **`Decimal`** | Conditional on Sharp Runtime's native 128-bit integer support, on the read side too. |
| **Local-time conversion scope** | DateTime uses Sharp Runtime’s current-zone conversions. The genuine-XNA fixture measurement uses a fixed UTC+2 winter date; ambiguous DST identity and native Windows behavior are not qualified by that measurement. |

---

## 7. Source assets: what compiles to `.xnb`

The same coordinator selects registered routes and format-specific writers. Processors can observe
format/target/profile, and some routes deliberately refuse one container. This table summarizes
supported native/convention routes; XNA project importer/processor pairs have their own mappings
and parameter defaults. See the native pipeline and migration guides.

| Source | Route | `.cnb` | `.xnb` |
|---|---|---|---|
| PNG, JPEG, BMP, TGA, … | `ImageImporter` → `TextureProcessor` → texture writer | ✅ | ✅ `Texture2D` |
| `.spritefont` + a font file | `FontDescriptionImporter` → `FontDescriptionProcessor` → SpriteFont writer | ✅ | ✅ `SpriteFont` |
| `.wav` | `WavImporter` → `SoundEffectProcessor` → SoundEffect writer | ✅ | ✅ `SoundEffect` |
| `.gltf`, `.glb` | glTF importer → model processor → model writer | ✅ | ✅ `Model` |
| `.cnj` | CNJ importer, then the matching processor | ✅ | ✅ |
| `.fxb` (already-compiled effect) | `CompiledEffectImporter` → `CompiledEffectProcessor` → Effect writer | — | ✅ `Effect` |
| `.fx` (HLSL effect source) | `EffectSourceImporter` → `EffectSourceProcessor` → Effect writer | — | Conditional external-compiler route with retained real-fxc evidence — see §6 |
| `.x`, `.fbx` | Native model-source route / XNA component mapping | Supported material/format subset | `Model`; finite importer and compiler witnesses |
| `.mp3`, `.wma` | Native Song streaming-copy route; project pairs can select Song or SoundEffect processing | Route-dependent | Route-dependent Song/SoundEffect; optional decoding/encoding services apply |
| `.wmv` | Probe/Video route | External-media Video metadata | External-media Video metadata |
| Intermediate `.xml` | XmlImporter → typed PassThroughProcessor → registered XNB writer | Explicit refusal | Registered built-in or game-defined type |
| `.xnb` | XNB importer (transcode to `.cnb`) | ✅ | — |

Texture policy parameters apply in one documented, deterministic order: **colour key → resize →
premultiply → mip chain → block compression.** `colorKey` matches on RGB and sets alpha to zero;
`resizeToPowerOfTwo` rounds level zero up; `premultiplyAlpha` multiplies colour by alpha;
`generateMipmaps` builds the chain from whatever level zero then is; `textureFormat` selects the
final representation.

**`premultiplyAlpha` defaults to `true`, matching XNA 4.0's own
`TextureProcessor.PremultiplyAlpha`** (`XNAP-96`). `BlendState::AlphaBlend` — what
`SpriteBatch::Begin()` selects when given no blend state, in XNA and in CNA alike — is the
premultiplied blend, so a texture that is *not* premultiplied renders with dark fringes under the
default blend state. A game that wants straight alpha sets `premultiplyAlpha` to `false` and selects
`BlendState::NonPremultiplied` itself.

Because premultiplication runs *before* mip generation, a distant mip never averages the colour of
invisible texels into visible ones; and because it runs *after* the colour key, a keyed texel comes
out transparent **black**, which is what XNA 4.0 produces for one.

Two importers override the default, because their source already defines the answer: a `.cnj`
document (CNJ version 1 has no `premultiplyAlpha` member and its compiled result is defined as
straight alpha, which `CompileCnjToCnb` pins) and an already-built `.xnb` being transcoded (its
pixels have had one alpha policy applied already; applying a second is corruption, not policy). An
explicit parameter beats both. The processor itself has exactly one behaviour per parameter set and
is **not** forked per output container: `TextureProcessor`'s build identity moved to version 3 with
this change, so every incremental build that used the old default rebuilds instead of being skipped
as unchanged.

Neither `.fxb` nor `.fx` has a `.cnb` route, deliberately: the CNB container reserves an `Effect`
identifier and has no schema for it, because CNA has not chosen whether native CNB `Effect` means
XNA Effect Framework bytecode, `ShaderEffect` dialect payloads, or a new renderer-neutral
contract. XNA bytecode already has this XNB route and optional compiled-effect runtimes across
several renderer families; the CNB route refuses instead of silently freezing that compatibility
payload as CNB's native contract. A `.fx` build with no compiler configured fails at the first
effect with one complete explanation — naming the discovery order it tried — rather than once per
asset or with a misleading shader error.

The native WAV source route accepts PCM8/16/24/32 and IEEE-float input. PCM8 and PCM16 retain
their width (CNB SoundEffect schema 2 and XNB WAVEFORMATEX); wider/float input is normalized to
PCM16 with the documented conversion warning. That source route refuses compressed WAV tags;
the runtime XNB reader's ADPCM handling is a separate path. Project processor/quality selection
can choose other conversions. See the [media guide](xna-content-pipeline-media.md).

### Using it

```bash
cna-content build Content -o bin/Content --format xnb \
    --xnb-platform windows --xnb-version 5 --xnb-profile reach
```

Per-asset overrides live in `.cna-content.json`. Its root/asset `outputFormat` value can override
the CLI format choice; the JSON's `format` identifies its configuration schema and is not the CLI
option spelled as a key.

---

## 8. Writing a type CNA does not know about

The type-writer registry is keyed by C++ type and takes registrations from anywhere, so a game can
put its own asset into an `.xnb` without changing CNA. Two halves are needed, and neither is
optional: a writer, and a reader that gives the written bytes meaning.

```cpp
// The writer half. Use the bounded byte/string/object APIs and explicitly validate custom
// collection counts. These APIs do not sandbox arbitrary component code.
class WaypointListXnbWriter final : public XnbTypeWriter<WaypointList>
{
public:
    [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
    {
        XnbReaderIdentity identity;
        identity.readerBaseName = "ExampleGame.Content.WaypointListReader";
        identity.readerAssembly = XnbAssembly::None;   // bare name for this CNA-only example
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

This sketch assumes the Xnb namespace/ContentReader aliases, a WaypointList with Vector3 points,
and its matching reader have been supplied. Bare custom names (`XnbAssembly::None`) demonstrate
CNA registration, not automatic genuine-XNA assembly resolution. For an existing managed game,
the reader assembly/name and payload need separate verification; the [migration guide](xna-content-pipeline-migration.md)
§7 shows the XNA-shaped ContentTypeWriter route with an explicit runtime reader name.

`XnbWriterTest.AGameCanRegisterItsOwnTypeWriterAndReaderAndRoundTripThroughThem` exercises this,
run end to end, including the check that a custom writer inherits the same ceilings the built-ins
have.

The same mechanism is how you register a generic instantiation CNA does not ship. Only the closed
instantiations CNA's own runtime reader registry resolves are registered by default —
`List<String>`, `List<Int32>`, `List<Char>`, `List<Rectangle>`, `List<Vector3>`, `List<Matrix>`,
`Dictionary<String,Int32>` and `Vector3[]` — so that a file this writer produces always has a reader
on the other side, and a test asserts exactly that for every built-in writer. `List<Single>` is not
a gap; it is one `registry.Register(...)` call plus its reader.

At the canonical native pipeline level, `CNA::Content::Pipeline::ContentTypeWriter` reaches `.xnb` by returning
`ContentOutputFormat::Xnb` from `OutputFormat()`. `docs/content-pipeline.md`'s **Custom
extensions** section covers the rest of that contract — stable component identities, schema and
codec versioning, and the source/toolchain compatibility model.

---

## 9. Provenance

The original writer campaign recorded the following provenance categories. Later target/compiler
witnesses and the current source/tests supplement that historical record; this section is not an
independent audit of every algorithm's authorship.

| Source | What came from it |
|---|---|
| **Committed fixtures, read as bytes** | Every reader type-name spelling and assembly qualifier, the object-dispatch protocol, the `Song` duration encoding, and the per-type field layouts. Each name carries a `XnbNameEvidence` value recording whether a genuine XNA 4.0 file, a MonoGame file, or the derived rule is behind it. |
| **CNA's own reader** | The counterpart of every writer: the writer was built against the reader, which was written first and independently, and the round-trip tests are the check that they agree. |
| **Published format descriptions** | The container header, 7-bit encoding, .NET `BinaryWriter` string encoding, the S3TC block layouts, the LZ4 block grammar, and the RIFF/WAVE chunk rules. |

The original writer/build-tool campaign used fixture/specification-based implementations rather
than copying MonoGame/FNA content-building source or decompiling Microsoft's pipeline methods. The MonoGame `.xnb` files in `tests/assets/xnb/` are used as
**black-box fixtures** — bytes to read and compare against — and each carries a manifest recording
its origin and licence (they are Ms-PL, the same licence as CNA).

Third-party dependencies the pipeline introduces are recorded in `THIRD_PARTY_NOTICES.md`:
**FreeType** (FreeType License; system-provided, linked only by the build-time module, and only for
the `.spritefont` route) and the vendored **Liberation Mono** test font (SIL OFL 1.1, with
`tests/assets/fonts/PROVENANCE.json` carrying upstream, distributor, SHA-256 and the reason it is
in the tree). Both are build-time and test-time only; no CNA runtime module links or embeds either.

### 9.1 Source rules for the content-pipeline parity work

These are the historical clean-room source rules of the XNA Content Pipeline parity campaign
(`XNAPP-*`), whose plan was deleted on 2026-10-29. Its section numbers (§18, §23, §25) refer to
that plan in Git history. They concern deriving content-building implementations; the project's
current [AGENTS.md](../AGENTS.md) governs ongoing work, including the normal local FNA runtime
reference and approved exact-runtime IL measurements.

**Allowed, and actually used:**

1. Public metadata of the legally obtained XNA Game Studio 4.0 Refresh reference assemblies, read
   by `System.Reflection` — names, signatures, attributes, enum values, inheritance
   (`tools/xna-pipeline-oracle/PipelineApiOracle.cs`).
2. The official IntelliSense documentation `Microsoft.Xna.Framework.Content.Pipeline.xml`
   shipped in the same SDK — summaries, parameter text, declared exceptions
   (`merge_xml_docs.py`).
3. Black-box execution of Microsoft's own tools: constructing built-in processors and reading
   their public properties; running the `BuildContent` MSBuild task under Wine on synthetic
   fixtures and observing the `.xnb` it writes (§23); loading CNA-written `.xnb` in the genuine
   runtime (§25).
4. The public MSBuild schema in `Microsoft.Xna.GameStudio.ContentPipeline.targets` and the
   `.contentproj` files of the public XNA samples — item metadata names, property names, the
   platform/profile spellings (§18).
5. CNA's existing reader, writer and pipeline, and CNA's own independently authored code.
6. Published XNB format documentation and the committed black-box fixtures
   (`tests/assets/xnb/xna40/`, `tests/assets/xnb/monogame/`).

**Forbidden within that content-building campaign:** decompiling pipeline method bodies; deriving an algorithm from IL or
native code; reading or copying MonoGame's or FNA's content-*building* source; copying another
clean-room implementation; committing a Microsoft DLL, EXE, font or SDK component; inventing an
API member because it "probably existed". `XNAPP-315` is the gate: no file under `tests/` or
`tools/` may contain a Microsoft binary, and the oracle's compiled output lives only under the
ignored `build/`.

The local **FNA reference tree** is the normal runtime/read-side reference. It supplies no
corresponding XNA content-building implementation; the build-side campaign instead used the
metadata, black-box tools and public sample sources above. No fixed checkout path is required. Where XNA and FNA
disagree, XNA wins (`CLAUDE.md`, decided 2026-09-04).

The sample `.xnb` sweep (`XNASWEEP-*`, [`tools/xna-sample-sweep/README.md`](../tools/xna-sample-sweep/README.md))
kept these rules unchanged, with one addition this campaign needs: **the public
C# source of a sample's own importer, processor or writer may be read**, because it is the sample's
published source and is exactly what a game shipping that component would have. Microsoft's own
pipeline implementation is still black-box only.

---

## 10. Reproducing the native checks

```bash
# Build the existing native test targets (maximum eight compiler jobs; four here).
cmake --build cmake-build-multi --target CnaContentTests CnaContentPipelineTests --parallel 4

# Writer round trips and golden comparisons: always use the private GPU/window runner.
tools/platform/run_gpu_tests_private.sh --exec env CNA_GRAPHICS_RENDERER=OPENGLES3 \
    SDL_AUDIODRIVER=dummy cmake-build-multi/CnaContentTests \
    --gtest_filter='Xnb*Writer*:XnbConformance*:XnbInteropCorpus*'

# Build-time block/font/compressed-texture tests need the same private environment.
tools/platform/run_gpu_tests_private.sh --exec env CNA_GRAPHICS_RENDERER=OPENGLES3 \
    SDL_AUDIODRIVER=dummy cmake-build-multi/CnaContentPipelineTests

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

`run-interop-harness.sh` builds and runs it under Wine (`mcs` against the SDK reference assemblies,
the prefix `~/.wine-cna-xna40` with `XnaNative.dll` and Direct3D 9 through DXVK, a private X
display). Three CTests drive it, and each skips rather than fails on a machine without that setup:

| CTest | What the genuine runtime loads |
|---|---|
| `XnaPipelineGenuineRuntimeInterop` | the eight committed fixtures, uncompressed: `texture2d_color_mips`, `texturecube_six_faces`, `texture3d_two_slices`, `soundeffect_pcm16_mono_22050`, `spritefont_two_glyphs`, `curve_two_keys`, `list_of_strings`, `model_triangle_basiceffect` |
| `XnaPipelineGenuineRuntimeInteropLzx` | the same eight, LZX-compressed |
| `XnaPipelineGenuineRuntimeBuiltFamilies` | `Effect`, `Song` and `Video`, built from CNA's own committed sources by `run-built-families-interop.sh` |

Prepare the reference assemblies and runtime prefix explicitly with `CNA_XNA40_REFERENCES` and
`CNA_XNA40_WINEPREFIX`. The wrapper uses exit 77 for missing prerequisites, registered as a CTest
skip; a skipped test is not runtime qualification. The three CTests share an `xna-interop-scratch`
resource lock because their wrappers clear/use the same scratch directory. Run them through the
private display runner, and let its DISPLAY reach Wine (an old `CNA_XNA40_DISPLAY` override can
otherwise select a different display):

```bash
tools/platform/run_gpu_tests_private.sh --exec env -u CNA_XNA40_DISPLAY \
    ctest --test-dir cmake-build-multi \
    -R '^XnaPipelineGenuineRuntime(Interop|InteropLzx|BuiltFamilies)$' \
    --output-on-failure -j1
```

The recorded device is HiDef because XNA refuses a `Texture3D` under Reach. Running it found
three defects nothing else could: the LZX encoder's missing end-of-stream trailer, which the runtime needs
(`LzxEncoderTest.EveryCompressedPayloadEndsWithTheTrailerXnaRequires`), and two in the built
families, among them an unqualified `VideoReader` name (`XNAPP-281`). All three pass as of
2026-10-29.

## 12. Design decisions

* **`XnbByteWriter` rather than `System::IO::BinaryWriter`.** XNB writing needs an in-memory body
  that the header is prepended to, and a UTF-8 `charcs` overload `BinaryWriter` does not have;
  `CnbByteWriter` set the precedent of a CNA-owned deterministic writer beside sharp-runtime's
  reader.
* **`<FontName>` is a font family, and the reproducible routes come first.** XNA asks Windows to
  resolve the family through GDI+, so the same `.spritefont` builds differently on different
  machines, and not at all without the font. CNA looks, in order, for a font file of that name
  beside the description (`Console`, `Console.ttf`, `Console.otf`, `Console.ttc`); a font file
  beside it whose own family table answers the name the way Windows matches it (`Peric.ttf` is
  the Pericles family; `XNASWEEP-100`/`105`); the directories a build passes with
  `--font-directory`, then `CNA_FONT_PATH`; and only then the installed fonts, which warns that
  the build now depends on what this machine has installed.
* **Build-time components live in `modules/content-pipeline/`.** FreeType, the block-compression
  encoder and any future importer stay out of the link closure of a game that only loads content:
  `cna_content_pipeline` is linked by `cna_content_compiler` and nothing else, so the dependency
  graph enforces that build-time boundary (`CnaXnbDependencyBoundary`). This does not mean runtime
  asset families have no native importers; ContentManager's family-specific link paths are
  documented in [link-size architecture](link-size-architecture.md).
* **Block compression is reached through a callable.** `cna_content` declares the encoder's shape
  (`TextureBlockEncoder`) and never links one; `cna_content_pipeline` supplies it. A registry
  without an encoder builds every uncompressed texture and refuses a compressed `textureFormat`
  with a message naming the configuration that would provide one, rather than quietly writing
  uncompressed pixels.

## 13. Current verification

The 2026-11-02 DateTime repair preserves Unspecified/UTC/Local Kind and converts Local
payloads through UTC using Sharp Runtime’s current zone. The retained genuine-XNA compiler
fixtures reproduce byte for byte, and the isolated genuine reader measurement under Wine
confirms the Local conversion and invalid-Kind refusal. Seven focused cases pass in each of
UTC+2, UTC and New York; all 313 selected XNB native cases pass privately with dummy audio.
The local-zone behavior deliberately follows measured XNA rather than FNA’s direct Local
construction; see [the decision ledger](xna-fna-behavior-decisions.md). This does not qualify
ambiguous DST identity or native Windows execution.

The earlier 2026-11-01 documentation pass reconciled writer/identity/canonical data, processing
and manifest contracts, the independent parser, retained compiler fixtures and genuine-runtime
harness adapters/wrappers.
Existing native content/pipeline targets build. The related selected XNB/reader/writer/manifest
run passes 456 cases, with eight LZX decoder/differential/fuzz cases; five XML source-route cases
also pass privately, including comparisons with retained target-specific compiler output.
All 16 uncompressed/LZX fixture files parse independently and match their explicit expectation
manifests, and nine pipeline metadata gates pass. These are finite native/fixture checks: no new
Microsoft runtime, real-fxc, Phone/Xbox device or sanitizer campaign was run in that documentation
review. The later DateTime repair and its measured scope are recorded above.
