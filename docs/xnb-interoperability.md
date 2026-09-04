# `.xnb` interoperability: what CNA writes, and how strongly each claim is verified

CNA has a native C++ XNB **writer** and a content pipeline that compiles source assets straight to
`.xnb`, with no XNA Game Studio, MonoGame, FNA build tooling, MSBuild or proprietary source
involved. This document says exactly what it produces and, for every claim, *how that claim was
checked*.

The read side is a separate document: `docs/xnb-content-pipeline-support.md` covers loading
already-built `.xnb` files. The task history and the per-decision record are in
`plans/plan_xnapipeline.md`.

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
> The mistake stays on the record — `plans/plan_xnapipeline.md` §0 and §0.5 give the full account,
> including what it cost — because an audit that checks one branch and concludes something about a
> repository is a mistake worth being able to find again. Every capability claim below is about the
> single surviving implementation.

> **The single most important sentence in this document.** No table below claims verification
> against a genuine Microsoft XNA 4.0 runtime, because no XNA 4.0 runtime — and no Windows, Wine,
> Mono or .NET Framework — exists in the environment this work was done in. A ready-to-run harness
> and a fixture corpus are committed so that anyone with an XNA-capable machine can fill that column
> in; until they do, it stays empty. See **Running the XNA 4.0 harness** at the end.

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
                    ContentImporter          ← one per source kind, format-agnostic
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

The **importer and the processor are identical for both formats**; only the serializer differs.
Forking processors per output format is the one mistake this design exists to prevent, so a
format-specific limitation is resolved **in the writer**: the XNB texture writer picks an
XNA-representable representation out of the canonical value and refuses with a named reason when
none exists. A processor never learns which container it is feeding.

`docs/content-pipeline.md` documents the build system itself — component selection, parameters,
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
| Uncompressed LZX blocks | They would save nothing and need the bitstream-realignment dance around them. This is also the shortcut that is **not** taken: setting `0x80` over unentropy-coded bytes would be technically conforming and worthless. |
| Intel `E8` call translation | The header bit is always zero. No `.xnb` uses it, and CNA's decoder — like FNA's, which never finished that transform — refuses a non-zero file size there. |

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
predating this encoder by years, shipped against real Microsoft-produced files) round-trips every
case; `tools/xnb/xnb_conformance.py` carries **its own LZX decoder**, written from the format
description in another language sharing no code, constants or tables, and it decodes every
CNA-produced LZX fixture down to asset values — the same decoder reproduces both externally
produced LZX fixtures byte for byte against FNA's own reference output, which is what earns it the
right to judge CNA's encoder; and the container framing is asserted field by field against the
layout those two external fixtures demonstrate.

**What LZX does not give you** is integrity checking: neither LZX nor the XNB container carries a
checksum, so a truncated or corrupted stream can decode to the declared byte count and be wrong.
That is a property of the format. The tested contract is that such a stream either refuses or
produces different bytes — it never quietly passes for the original.

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
| `List<Int32>`, `List<Char>`, `List<Rectangle>`, `List<Vector3>`, `List<Matrix>` | ✅ | ✅ | The instantiations CNA's own runtime reader registry resolves. A test asserts that *every* built-in writer's reader name resolves there, so the two registries cannot drift. |
| `Dictionary<String, Int32>` | ✅ | — | Written in deterministic key order. The independent parser has no dictionary decoder yet. |
| `Vector3[]` | ✅ | — | The one array instantiation with a built-in reader — a real XNA `Model` names `ArrayReader<Vector3>` in its own type table. Written through the `XnbArray<T>` carrier, which is how a registry keyed by C++ type tells `Vector3[]` from `List<Vector3>`: both are `std::vector<Vector3>`. |
| `Nullable<T>`, other `T[]` | ⚠️ `impl` | — | The writer templates exist and work, and the documented extension path is exercised end to end (`ANullableInstantiationRoundTripsThroughTheDocumentedExtensionPath`, `AnArrayTypedGenericArgumentSurvivesTheWholeRoundTrip`). No further instantiation is registered *by default*, because no built-in CNA **reader** resolves one and a writer with no reader produces a file CNA itself could not load. |
| `Enum<T>` | ✅ | — | Written as the underlying `Int32`, under `EnumReader\`1[[<enum>]]`, read back by `EnumTypeReader<T>`. Not registered by default: the enum's .NET name cannot be recovered from a C++ enum, so it is supplied at registration — `RegisterXnbEnumWriter<T>(registry, "Namespace.Enum", assembly)`. A value can never reach the wrong enum's writer, because the registry is keyed by the C++ type. |

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
observed from the write rather than declared beside it (`rootReaderName`, manifest schema 9). An
`.xnb` has no asset type id and no schema version, so that reader name is its compatibility
identity: a build that changes which reader an asset dispatches to invalidates the artifact instead
of being skipped as unchanged.

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
| **Compiling `.fx` to shader bytecode** | CNA does not *embed* an HLSL compiler, and that remains a standing decision (`plans/plan_fx.md`: "Out of scope; CNA will not embed an HLSL source compiler"). What it now does instead is **drive an external one**: the `.fx` route resolves the include tree, records every included file as a build dependency, invokes a compiler behind the `EffectCompilerService` interface, and refuses anything that comes back that is not an Effect Framework 9.1 container. The only compiler known to produce that container is Microsoft's own legacy `fxc` at profile `fx_2_0`, which cannot be vendored; supply it with `--fx-compiler` (or `CNA_FXC`), and `--fx-compiler-launcher wine` (or `CNA_FXC_LAUNCHER`) on a non-Windows build machine — see `docs/content-pipeline.md`, "Effects: `.fxb` and `.fx`", for the full precedence order. **Not verified against a real `fxc`** — none exists in the environment this was written in, so no test uses one: the unit tests substitute the compiler at the `EffectCompilerService` seam and the product-route tests drive a project-owned stand-in across a real process boundary, which proves CNA's own integration and nothing about `fxc` (`XNAP-A4`). It will still never fake a compile by embedding source text in an `Effect` asset. |
| **Aligned-offset and uncompressed LZX blocks** | CNA *reads* both and emits neither: verbatim blocks are a conforming subset and the other two buy nothing here. See §2.1.1. |
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
| `.fx` (HLSL effect source) | `EffectSourceImporter` → `EffectSourceProcessor` → Effect writer | — | ⚠️ `impl` `Effect`, and only with an external `fxc` — see §6 |
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
identifier and has no schema for it, because CNA has many renderers and a `.cnb` carrying one API's
shader bytecode would be useless on the others. A `.fx` build with no compiler configured fails at
the first effect with one complete explanation — naming the discovery order it tried — rather than
once per asset or with a misleading shader error.

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
`List<String>`, `List<Int32>`, `List<Char>`, `List<Rectangle>`, `List<Vector3>`, `List<Matrix>`,
`Dictionary<String,Int32>` and `Vector3[]` — so that a file this writer produces always has a reader
on the other side, and a test asserts exactly that for every built-in writer. `List<Single>` is not
a gap; it is one `registry.Register(...)` call plus its reader.

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
