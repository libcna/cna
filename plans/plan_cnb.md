# plan_cnb.md — CNB, the CNA-native compiled binary content format

> **Location note.** The task brief asked for `plan_cnb.md`. Every other engineering plan in this
> repository lives in `plans/` (`plans/plan_cnj.md`, `plans/plan_xnb.md`, `plans/plan_gltf.md`, …) and is
> cited from source comments as `plans/plan_<topic>.md`. This file follows that convention so the
> `plans/plan_cnb.md CNBF-nnn` citations written into the new sources match every other citation in
> the tree.
>
> **Task IDs.** `CNBF-001`+ — deliberately *not* `CNB-n`, which `plans/plan_cnj.md` already used for
> `CNB-1`…`CNB-111` (the historical CNJ work, back when `.cnj` was still called `.cnb`).
> `grep -r 'CNBF-'` therefore returns only this project.
>
> **Companion documents.** `misc/cnb.md` is the original architectural proposal and stays as-is.
> `docs/cnb-format.md` is the authoritative byte-level specification of what is actually
> implemented. This file is the living engineering record: audit, decisions, rejected
> alternatives, task status, measurements.

---

## 1. Current-state audit (2026-08-26)

Everything in this section was read out of the tree at `1e70b234f` (branch `cnb`), not assumed.

### 1.1 Content module physical layout

`modules/content/` is one of the declared physical modules (`modules/CMakeLists.txt`'s
source-partition validator refuses any production `.cpp` outside `modules/<name>/{src,tests,examples}`).

```text
modules/content/include/CNA/Content/            ForeignContentObjectEXT.hpp  (CNAEXT public surface)
modules/content/include/CNA/Internal/           CnjEnvelope.hpp, CnjSourceFile.hpp,
                                                CnjMorphSidecarEXT.hpp, Json.hpp
modules/content/include/CNA/Internal/Xnb/       22 headers — the .xnb subsystem
modules/content/include/CNA/Internal/GltfImport/GltfImportCore.hpp (2168 lines)
modules/content/include/Microsoft/Xna/Framework/Content/  ContentManager/ContentReader/…
modules/content/src/{Xna,Xnb,GltfImport}/
modules/content/tests/{CNA/Internal/{Xnb,GltfImport},Microsoft/Xna/Framework/Content}/
```

`modules/content/CMakeLists.txt` globs `src/*.cpp` recursively (`GLOB_RECURSE … CONFIGURE_DEPENDS`),
so a new `src/Cnb/` directory needs **no CMake edit at all**. `cmake/UnitTests.cmake` globs the test
tree the same way. This is the single most important build-system finding: CNB can be added as a
purely additive set of files.

### 1.2 `ContentManager` resolution order — what is actually implemented

`ContentManager::Load<T>()` (`modules/content/include/Microsoft/Xna/Framework/Content/ContentManager.hpp`):

1. disposed check;
2. cache lookup, keyed by `(std::type_index(typeid(T)), normalizedName)` — `AssetCacheKey`
   (plan_cnj `CNB-36`);
3. **`<name>.xnb` first** — `ResolveExistingAssetPath(BuildAssetPath(assetName) + ".xnb")`; if it
   exists, `LoadXnbAsset<T>()` and return. This happens **before** the per-`T` reader lookup, so
   `.xnb` dispatch works for a `T` with no registered `LooseFileContentTypeReader<T>` at all — the
   file's own type-reader table drives dispatch through the process-wide
   `ContentTypeReaderManager`;
4. reader lookup in `typeReaders_`; missing ⇒ `ContentLoadException`;
5. `ResolveAssetPath(assetName, reader)`:
   a. literal `BuildAssetPath(assetName)` if it exists,
   b. `base + ".cnj"` if it exists,
   c. each `reader.GetExtensions()` in order,
   d. bare `base` as a last resort;
6. `reader.Read(resolvedPath, *this)`, cache, return.

`misc/cnj.md` §"The core rule" documents exactly this and states the reasons per tier. **`.xnb`
always wins** is an explicit, dated (2026-07-16) design decision, not an accident, and
`CnjResolverOrderTests.cpp` / `ContentManagerXnbTests.cpp` pin it.

*Consequence for CNB:* CNB must slot in as a **new tier between `.xnb` and the literal-path check**,
implemented the same way `.xnb` is (before the reader lookup, self-describing dispatch), because a
compiled `.cnb` is CNA's own compiled artifact and must outrank the loose `.cnj`/native sources it
was compiled *from* — while still yielding to a genuine external `.xnb`.

### 1.3 The `.cnj` Model asset really is a multi-file asset

`tools/gltf_to_cnj/gltf_to_cnj.cpp` writes, for one glTF input named `robot`:

| file | written by | read by |
|---|---|---|
| `robot.cnj` | `ConvertGroup` | `ModelTypeReader::Read` |
| `robot_mesh<N>_verts.bin` | `WriteBinaryFile(meshOut.vertexBytes)` | `ReadBinaryFile` + `BuildVertexBufferFromRawBytes` |
| `robot_mesh<N>_idx.bin` | `WriteBinaryFile(meshOut.indexBytes)` | `ReadBinaryFile` + `IndicesFromBytes<u16/u32>` |
| `robot_mesh<N>_morph.bin` | `BuildMorphBytes` | inline morph decoder in `ModelTypeReader::Read` |
| `robot_mesh<N>_variant<V>_verts.bin` / `_morph.bin` | material-variant path | same |
| `robot.skeleton.bin` | inline `AppendInt32`/`AppendMatrix` block | inline decoder in `ModelTypeReader::Read` |
| `robot_<clip>.cnj` | `writeClip` (one standalone `.cnj` AnimationClip per clip) | `AnimationClipTypeReader` via `ReadAnimationClipRefEXT` |
| `robot_tex<N>.png`, normal/MR/emissive/occlusion/specular maps | image extraction | `cm.Load<Texture2D>` |

A four-primitive skinned model with two clips and three maps is therefore
**1 + 4·2 + 1 + 2 + 3 = 15 files**, each one `std::ifstream` open + full read, plus a JSON parse of
the `.cnj` and of each clip `.cnj`. That is the concrete problem `misc/cnb.md` identified, and the
audit confirms it verbatim.

### 1.4 The existing binary sidecar layouts (exact, as implemented)

All are little-endian by `std::memcpy` of native types on the writer side
(`gltf_to_cnj.cpp::AppendInt32/AppendFloat/AppendMatrix`) and `BinReaderEXT::Read<T>()` on the
reader side (`ContentManager.cpp:1407`). `BinReaderEXT::Read<T>` **is** bounds-checked
(`Pos + sizeof(T) > Data.size()` ⇒ `ContentLoadException`) — added by "Task 11.7" after a real
out-of-bounds heap read.

```text
.skeleton.bin      i32 boneCount
                   boneCount × i32   parentIndex
                   boneCount × 16×f32 bindPoseLocal        (row-major, XNA field order)
                   boneCount × 16×f32 inverseBindGlobal
                   [optional] boneCount × 16×f32 parentWorldPrefix   (GLTF-245/247)
                   -- optionality detected by `Remaining() >= boneCount*64`

.clip.bin          f64 durationSeconds
                   i32 trackCount
                   trackCount × { i32 boneIndex, i32 keyCount,
                                  keyCount × { f64 time, 3×f32 T, 4×f32 R, 3×f32 S } }

_morph.bin         i32 targetCount
                   targetCount × { i32 vertexCount,
                                   vertexCount × 3×f32 positionDelta,
                                   i32 hasNormals, [vertexCount × 3×f32 normalDelta] }
                   [optional trailer] i32 magic 'MTAN' (0x4E41544D), i32 version 1,
                                      i32 targetCount,
                                      targetCount × { i32 hasTangents,
                                                      [vertexCount × 3×f32 tangentDelta] }

_verts.bin         raw interleaved vertex bytes; the *stride* lives in the .cnj, not the file,
                   and vertexCount is derived as bytes/stride (silently truncating).

_idx.bin           raw index bytes; element size is **inferred**: 32-bit iff vertexCount > 65535,
                   mirroring XNA's stock ModelProcessor. Not stored anywhere.
```

Findings worth carrying into CNB:

* **`_verts.bin`/`_idx.bin` are untyped.** Element size and count are inferred, not declared. A
  truncated file silently yields fewer vertices rather than an error. CNB must declare
  `vertexCount`, `stride`, `indexCount` and `indexElementSize` explicitly and cross-check them
  against the chunk length.
* **The `.skeleton.bin` optional block is detected by arithmetic on `Remaining()`.** That works, but
  it means "file is 64·boneCount bytes short" and "file deliberately omits the prefix" are the same
  observation. A chunked container removes that ambiguity structurally.
* **The `_morph.bin` trailer already invented an ad-hoc magic+version.** CNB's TOC generalises
  exactly that idea.
* Bone/target/track counts are guarded only by ad-hoc constants
  (`kMaxSaneBoneCount = 100000`, `kMaxSaneTargetCount = 100000`) at three separate call sites.

### 1.5 XNB engineering patterns worth reusing (ideas, not code)

* `CNA/Internal/Xnb/XnbReadLimits.hpp` — one struct of generous sanity bounds
  (`maxFileSize`, `maxStringBytes`, `maxCollectionElementCount`, …) plus a process-wide default.
  **Adopted** as `CnbReadLimits`.
* `CNA/Internal/Xnb/XnbArithmetic.hpp` — `CheckedMultiplyOrThrow` checks each multiplication via
  division *before* performing it. **Adopted** as `CNA::Content::Cnb::CheckedMultiply`/`CheckedAdd`.
* `XnbHeader.hpp` — validate-and-throw (no partial state) for binary headers, with the deviation
  from FNA documented in the header comment. **Adopted** as the house style for `CnbHeader`.
* `XnbContainerFuzzTests.cpp` — deterministic LCG (`state*6364136223846793005+1442695040888963407`),
  no `std::random_device`, mutates a real file and asserts *"loads, or throws one of a small set of
  clean exception types; never crashes"*. **Adopted** for `CnbContainerFuzzTests.cpp`.
* `ContentTypeReaderManager` — a process-wide `unordered_map<key, factory>` with
  `Add`/`Remove`/`Clear`/`IsRegistered`, `Clear` primarily for test isolation. **Adopted** in shape
  by `CnbLoaderRegistry`.

Explicitly **not** carried over: assembly-qualified names, `.NET` generic type parsing, reader
version negotiation, the shared-resource fixup protocol, platform identifier bytes, LZX.

### 1.6 Other facts that constrain the design

* `CNA::Internal::Json` (`Json.hpp`, 649 lines) is a complete recursive-descent JSON parser *and*
  writer, already used by every `.cnj` reader. The CNJ→CNB compiler reuses it rather than
  re-implementing JSON.
* `ContentLoadException` (`std::runtime_error` subclass) is the established failure type across the
  whole content subsystem. CNB throws it for every malformed-input case, so a game's existing
  `catch (const ContentLoadException&)` keeps working.
* `Curve` / `CurveKey` / `CurveLoopType`(0..4) / `CurveContinuity`(0..1) live in `modules/math`.
  `AnimationClipEXT` / `BoneTrackEXT` / `KeyframeEXT` / `ClipTargetSpaceEXT` live in
  `modules/graphics` (`SkinnedModelEXT.hpp`). `SkinningData` lives in `AnimationPlayer.hpp`.
* `ModelTypeReader::Read` is **1117 lines** (`ContentManager.cpp:3951-5068`) with JSON parsing and
  runtime `Model` construction fully interleaved, inside an anonymous namespace, sharing
  file-scope helpers (`ModelResources`, `BuildVertexBufferFromRawBytes`,
  `AppendPositionsForMeshBoundsEXT`, `WidenedIndicesEXT`, `ApplyPunctualLightsEXT`) with the
  1800-line runtime-glTF reader. This drives decision **D9** below.
* `tools/gltf_to_cnj` is built unconditionally by `cmake/ToolGltfToCnj.cmake`, linking `CNA` +
  sharp-runtime, and `cmake/UnitTests.cmake:493` passes its path to `CnaTests` via
  `CNA_GLTF_TO_CNJ_TOOL_PATH` so `GltfToCnjToolTests.cpp` can `posix_spawn` it. **The CNB compiler
  tool copies this exact wiring.**

---

## 2. Validating `misc/cnb.md` against the tree

| `misc/cnb.md` claim | verdict | note |
|---|---|---|
| "gltf_to_cnj produces `robot.cnj` + many `.bin` sidecars" | **confirmed** | §1.3; 15 files for a modest skinned model |
| "`ContentManager.cpp` contains custom binary readers for `.clip.bin`, `.skeleton.bin`, morph" | **confirmed** | `BinReaderEXT`, `ReadAnimationClipFileEXT`, inline skeleton/morph decoders |
| "part of CNB already exists de facto" | **confirmed** | including an ad-hoc magic+version trailer in `_morph.bin` |
| chunk-based container, not object serialisation | **adopted** | §3 |
| separate container version from asset schema version | **adopted** | §3.2 |
| never `memcpy` C++ structs; define `u8/u16le/…` | **adopted** | §3.3 |
| "not binary JSON" | **adopted** | Model carries a compiled descriptor table + raw buffer chunks, not a JSON DOM |
| CNJ stays useful | **adopted** | CNB ranks *above* `.cnj`, never replaces it; `.cnj` remains the editable source |
| `Compression::None` only in v1 | **adopted** | one reserved `compression` field, reader rejects ≠ 0 |
| CRC32 per chunk | **adopted with a change** | CRC-**32C** (Castagnoli), see D5 |
| first compiler is CNJ→CNB | **adopted** | `cna_tool_cnj_to_cnb` |
| type id = `u64 stableTypeId` + optional debug name | **changed** | `u32` id (D6) |
| header field order/widths as sketched | **changed** | 64-byte fixed header, different order/widths (D2) |
| `ChunkEntry` as sketched (36 bytes, unaligned) | **changed** | 48-byte entry, all fields naturally aligned (D3) |
| order Curve → AnimationClip → Model → SpriteFont → Texture3D → Texture2D → … | **partially adopted** | Curve → AnimationClip → Model; the rest is future work (D10) |
| `Model` chunk names `VB00`/`IB00`/`MORPH00` | **rejected** | duplicate chunk *types* addressed by TOC index instead (D4) |
| "~150–300 h for a solid CNB 1.0" | not evaluated | irrelevant to the design |

**Rejected outright:** nothing in `misc/cnb.md` was found to be wrong in intent. The changes above
are all refinements of encoding detail.

---

## 3. Design decisions

### D1 — Chunked container, little-endian, no host-ABI dependency

`.cnb` is a fixed 64-byte header, a table of contents of fixed 48-byte entries, and chunk payloads.
Nothing in the format depends on `sizeof` a C++ type, struct padding, `std::type_index`, or the host
endianness: every field is read and written byte-by-byte through
`CnbByteReader`/`CnbByteWriter`, which assemble/disassemble integers from individual `std::uint8_t`
values. Floats go through `std::bit_cast<float, std::uint32_t>` of an explicitly little-endian
`u32`, so a big-endian host produces and consumes identical bytes.

**Rejected:** big-endian or a runtime-selectable endianness (every CNA target — x86-64, aarch64,
wasm32 — is little-endian; a byte-order flag is untestable dead weight); a text/JSON container
(that is `.cnj`, which continues to exist); MessagePack/CBOR (a generic object encoding is exactly
the "binary JSON" outcome `misc/cnb.md` warns against and buys nothing over a typed descriptor
table).

### D2 — 64-byte header

See `docs/cnb-format.md` §3 for the byte table. Design points:

* magic is `43 4E 42 1A` — `"CNB"` plus `0x1A` (DOS EOF), which stops `type`/`cat` mid-file and is
  the same trick PNG uses;
* every multi-byte field is naturally aligned inside the header, so a future zero-copy reader can
  overlay it on a little-endian host;
* `headerChecksum` (CRC-32C of bytes `[0,44)`) and `tocChecksum` (CRC-32C of the TOC bytes) are
  verified **before any offset in them is trusted**;
* 16 reserved bytes, required to be zero, so a v1 reader rejects a file using a field it cannot see;
* `fileSize` must equal the real file size — this is the check that stops every "declared length
  larger than the file" attack at the door (`plans/plan_xnb.md XNB-43` had to add exactly this check to
  `.xnb` after the fact).

### D3 — 48-byte TOC entry, TOC ordered by offset

`{ type:u32, flags:u32, offset:u64, storedSize:u64, uncompressedSize:u64, checksum:u32,
compression:u32, alignment:u32, reserved:u32 }`. All naturally aligned. TOC entries **must** be
sorted by ascending `offset`, which makes overlap detection a single linear pass and makes writing
deterministic by construction.

The reader builds one region list — header, TOC, every non-empty chunk — and requires it to
partition `[0, fileSize)` exactly, with all inter-region gap bytes zero. That single invariant
subsumes "no overlapping chunks", "no chunk past EOF", "no trailing junk" and "no hidden data in
alignment padding".

**Rejected:** TOC at end-of-file (single-pass streaming writing is irrelevant — CNB files are built
in memory), and an unordered TOC (would need O(n log n) sorting inside the reader on
attacker-controlled data before any bound is known).

### D4 — Duplicate chunk types are legal; chunks are referenced by per-type ordinal

`misc/cnb.md` sketched `VB00`, `VB01`, `MORPH00`. That caps a model at 100 primitives and puts an
index inside a type identifier. Instead: any chunk type may appear more than once, in file order,
and one chunk refers to another by its **ordinal within its own chunk type** — "the 3rd `MVTX`
chunk", not "TOC entry 7". A raw TOC index was considered and rejected: it would silently shift
whenever a container-level chunk (`CMET`, `XREF`) is added or removed ahead of the payload chunks,
which is precisely the kind of coupling a self-describing format should not have. A per-type
ordinal is stable under any such insertion and is validated against
`CnbDocument::FindAll(type).size()` before use.

A schema declares which of *its* chunk types are singletons; `CnbDocument::RequireSingle()`
enforces that.

Chunk ids are four printable-ASCII bytes stored little-endian, so they read left-to-right in a hex
dump. Ids whose first byte is an uppercase ASCII letter are reserved for CNA; a game defining its
own schema uses an id starting with a lowercase letter. The reader rejects any id containing a
non-printable byte.

### D5 — CRC-32C, not CRC-32

Both are a 1 KiB table and ~40 lines; neither adds a dependency. CRC-32C (Castagnoli,
reflected poly `0x82F63B78`) is chosen because it has a strictly better minimum Hamming distance
over the payload sizes CNB actually stores and because `SSE4.2`/`ARMv8-CRC` can accelerate it later
without changing a byte of the format. The implementation is a portable software table — no
intrinsics, identical output everywhere. Known-answer test: `"123456789"` ⇒ `0xE3069283`.

Checksums detect **accidental** corruption only. This is stated in the spec; CNB has no
authenticity story and must not be presented as having one.

**Rejected:** no checksum at all (the `_morph.bin` experience shows silent misparse is the real
failure mode); SHA-256/BLAKE3 (cryptographic cost for a non-cryptographic goal); Adler-32 (much
weaker on short chunks).

### D6 — `u32` asset type ids

`misc/cnb.md` suggested `u64`. `u32` is used instead: it keeps the header at 64 bytes with room to
spare, and 2³¹ custom ids is far more than a content pipeline will ever mint. Ranges:

```text
0x00000000            reserved, invalid
0x00000001-0x3FFFFFFF CNA built-in asset types (stable once v1 ships)
0x40000000-0x7FFFFFFF reserved for future CNA use
0x80000000-0xFFFFFFFF custom/game asset types
```

A custom id is `CnbAssetTypeId::FromName(utf8)` = `FNV-1a-32(name) | 0x80000000`, i.e. 31 usable
bits. Collision behaviour is documented and *enforced*: `CnbLoaderRegistry::Register` throws if the
id is already registered under a different debug name, and the optional `CMET` chunk carries the
debug name so a mismatch is reported rather than silently mis-loaded.

**Rejected:** `std::type_index` (not a serialisation ABI — its value is not stable across
processes, let alone builds); assembly-qualified strings (that is the XNB complexity CNB exists to
avoid); pure hashing with no reserved range (built-ins would be unstable if a name were ever
tidied).

### D7 — Unknown chunks / versions

* `containerMajor != 1` ⇒ reject.
* `containerMinor > 0` ⇒ **accept**; minor bumps are additive-only by definition.
* Any bit set in `headerFlags` or a non-zero reserved byte ⇒ reject (a v1 reader cannot know what it
  would be agreeing to).
* Unknown chunk type with `CnbChunkFlags::Mandatory` set ⇒ reject, naming the type.
* Unknown chunk type without it ⇒ skipped.
* `compression != 0` ⇒ reject with the codec id named.
* `assetSchemaVersion` outside `[1, maxSupportedForThatType]` ⇒ reject.
* Duplicate of a schema-declared singleton chunk ⇒ reject.

### D8 — One `.cnb` = one logical asset; external references stay external

CNB has an optional container-level `XREF` chunk: a flat table of
`{flags:u32, expectedAssetTypeId:u32, logicalName:String}` entries. A schema refers to another
asset by **index into `XREF`**, never by an inline path string, so:

* every external dependency of an asset is discoverable without understanding the schema
  (`cna_tool_cnb_info --refs`), and
* `ContentManager`'s cache and sharing keep working — a texture referenced by a hundred models is
  still loaded once.

The reader validates each `logicalName`: non-empty, valid UTF-8, no backslash, not absolute, no `..`
segment. (`ContentManager`'s own containment checks still run afterwards; this is defence in depth.)

Packaging many assets into one file is explicitly **out of scope** — that is a hypothetical
`.cnapak`, a different project.

### D9 — Model support: scope, and why the reader lives where it lives

`ModelTypeReader::Read` interleaves JSON parsing with runtime `Model` construction across 1117
lines and shares six file-scope helpers with the 1800-line runtime-glTF reader
(§1.6). Three options were considered:

1. **Extract a `CnjModelMeshEntry` struct + builder out of `ContentManager.cpp`** so the `.cnj` and
   `.cnb` paths share one builder. Architecturally the right end state, but it is ~1100 lines of
   surgery on the file with the largest test surface in the repository, and the brief is explicit:
   *"avoid heavily rewriting `ContentManager` early"*, *"Add CNB; do not use CNB as an excuse to
   redesign unrelated content infrastructure."*
2. **Duplicate the builder in a new module.** Guarantees drift between two model builders.
3. **Chosen:** the CNB *codec* (bytes ⇄ a plain, schema-shaped `CnbModelData` struct) lives in
   `modules/content/src/Cnb/CnbModelCodec.cpp` and is fully unit-testable with no `GraphicsDevice`.
   The ~120-line *adapter* that turns a decoded `CnbModelData` into a runtime `Graphics::Model`
   lives beside its `.cnj` sibling inside `ContentManager.cpp`'s anonymous namespace, where it
   reuses the existing `ModelResources`, `BuildVertexBufferFromRawBytes`, `WidenedIndicesEXT`,
   `AppendPositionsForMeshBoundsEXT` and `ApplyPunctualLightsEXT` helpers instead of copying them.

Option 1 is recorded as future work (`CNBF-090`), to be done as its own isolated task with its own
regression run — not smuggled into this one.

**Model v1 schema scope** (everything the compiler can express *and* the reader can honour):
bone hierarchy, per-part geometry (vertex bytes + declared stride/count, index bytes + declared
element size/count, primitive topology), part→mesh grouping and parent-bone attachment, the stock
effect selection, the full PBR/`MaterialOut` scalar set, per-slot sampler state, texture/effect
external references, morph targets incl. the tangent deltas and the weight track, the skeleton
(all four matrix blocks), embedded animation clips, and punctual lights.

**Deliberately out of v1 Model scope, documented as such:** material *variants*
(`variantOf`/`materialVariant`/`materialVariantNames`) and the `gltfImportReport` diagnostics
block. Both are `.cnj`-authoring/diagnostic concerns rather than runtime geometry, and both are
carried by fields the compiler refuses loudly (`CNBF-084`) rather than dropping silently.

### D10 — Asset types in v1

`Curve` (7) and `AnimationClip` (6) prove the container end to end on real CNA types with real
`ContentManager` integration. `Model` (5) is the milestone that justifies the format. `Texture2D`,
`SpriteFont`, `SoundEffect`, `Song`, `Video`, `Effect` have **reserved ids** and **no v1 schema** —
the ids are frozen, the schemas are future work. This follows the brief's *"Do not implement dozens
of asset types before the container architecture has proven itself."*

For textures specifically the reserved design note is: a `Texture2D` `.cnb` will carry one or more
`TEXn` chunks each declaring `SurfaceFormat` + width/height/mipCount, with RGBA8 as the mandatory
portable baseline and additional block-compressed representations optional and selected at load
time by renderer capability. Nothing in the v1 container prevents that; nothing in v1 implements it.

### D11 — Determinism

The writer takes no wall-clock, no RNG, no pointer values, no environment. Chunks are emitted in
the order the schema encoder adds them; the TOC is written in that same order (which is also
ascending-offset order); alignment padding is zero-filled. The `CMET` chunk carries only
input-derived strings. `CNBF-041` asserts byte-identical output across two independent writer runs,
and `CNBF-063` asserts the compiler is byte-deterministic across two separate process invocations.

---

## 4. Format invariants (the list the reader enforces)

1. file ≥ 64 bytes and ≤ `limits.maxFileSize`
2. magic == `43 4E 42 1A`
3. `containerMajor == 1`
4. `headerFlags == 0`, `reserved[16] == 0`
5. `headerChecksum` matches CRC-32C of bytes `[0,44)`
6. `fileSize` == actual byte count
7. `chunkCount <= limits.maxChunkCount`
8. `tocOffset >= 64`, `tocOffset + 48*chunkCount` computed overflow-safe, `<= fileSize`
9. `tocChecksum` matches CRC-32C of the TOC bytes
10. per entry: `compression == 0`; `storedSize == uncompressedSize`; `alignment` is a power of two
    in `[1, 4096]`; `reserved == 0`
11. per entry: `offset + storedSize` overflow-safe and `<= fileSize`; `offset % alignment == 0`;
    `storedSize <= limits.maxChunkSize`
12. TOC sorted by ascending `offset`
13. header, TOC and all non-empty chunk ranges are pairwise disjoint and, together with zero-filled
    gaps, cover `[0, fileSize)` exactly
14. every chunk's `checksum` matches CRC-32C of its stored bytes
15. every chunk carrying `Mandatory` must be understood by the reader
16. `assetTypeId != 0`; `assetSchemaVersion` within the reader's supported range for that type
17. schema-declared singleton chunks appear exactly once
18. every `String` length ≤ `limits.maxStringBytes`, is well-formed UTF-8 (no overlong forms, no
    surrogates, no > U+10FFFF), and fits inside its chunk
19. every count × element-size product is computed overflow-safe and must match the chunk's exact
    remaining length where the schema says so
20. every intra-file index (TOC index, string index, `XREF` index, bone parent, track key range) is
    range-checked before use

---

## 5. Compatibility strategy

* **Nothing existing changes.** `.xnb` keeps winning. `.cnj` keeps working, including as a metadata
  sidecar. Native/loose files keep working. Every pre-existing test keeps passing unchanged.
* **New tier:** `<name>.cnb` is checked after `<name>.xnb` and before the reader-driven
  `ResolveAssetPath`. `CnbResolverOrderTests.cpp` pins the full order
  `xnb > cnb > literal > cnj > native`.
* **Additive `ContentManager` surface only:** two new methods
  (`RegisterCnbLoaderEXT`, and the private `LoadCnbAsset<T>`), one new `if` block in `Load<T>()`,
  one new `#include`. No signature changes, no behaviour changes on any existing path.
* **Experimental → stable gate:** while `CNB_EXPERIMENTAL` was in force the container refused to
  claim stability. Section 9 records the gate criteria and where the format stands.

---

## 6. Implementation phases and task log

Status: ✅ done · 🚧 in progress · ⬜ not started · ⛔ deliberately out of scope

### Phase A — container primitives

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-001 | `plans/plan_cnb.md` — audit + plan | ✅ | this file |
| CNBF-002 | `CnbCrc32c` (portable table, KAT-tested) | ✅ | `CNA/Content/Cnb/CnbCrc32c.hpp` |
| CNBF-003 | `CnbReadLimits` + process default | ✅ | modelled on `XnbReadLimits` |
| CNBF-004 | `CnbArithmetic` — `CheckedAdd`/`CheckedMultiply` on `u64` | ✅ | |
| CNBF-005 | `CnbFormat` — magic, sizes, chunk ids, flags, compression, asset type ids | ✅ | |
| CNBF-006 | `CnbByteReader` — bounded LE primitive cursor incl. UTF-8-validated strings | ✅ | |
| CNBF-007 | `CnbByteWriter` — LE primitive emitter | ✅ | |
| CNBF-008 | `CnbDocument` — parse+validate header/TOC, chunk access, `RequireSingle` | ✅ | invariants 1–17, 20 |
| CNBF-009 | `CnbWriter` — deterministic container writer with alignment/checksums | ✅ | |
| CNBF-010 | `XREF` external-reference table codec | ✅ | |
| CNBF-011 | `CMET` metadata chunk codec | ✅ | |

### Phase A-tests

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-020 | CRC-32C known answers | ✅ | |
| CNBF-021 | header/TOC round trip, chunk lookup, alignment | ✅ | |
| CNBF-022 | truncated header / TOC / chunk | ✅ | |
| CNBF-023 | bad magic, bad major, non-zero flags/reserved | ✅ | |
| CNBF-024 | bad header checksum / TOC checksum / chunk checksum | ✅ | |
| CNBF-025 | offset+size overflow, offset past EOF, unaligned offset | ✅ | |
| CNBF-026 | overlapping chunks, non-monotonic TOC, non-zero padding, trailing junk | ✅ | |
| CNBF-027 | chunk-count/file-size/string-length limits | ✅ | |
| CNBF-028 | unknown optional chunk skipped; unknown mandatory chunk rejected | ✅ | |
| CNBF-029 | duplicate singleton rejected; legitimate duplicate types accepted | ✅ | |
| CNBF-030 | invalid UTF-8 in strings rejected (overlong, surrogate, >U+10FFFF, truncated) | ✅ | |
| CNBF-031 | zero-length chunks accepted; zero-chunk file accepted | ✅ | |
| CNBF-032 | `XREF` path-safety rejections (absolute, `..`, backslash, empty) | ✅ | |
| CNBF-033 | deterministic byte-identical writing | ✅ | |
| CNBF-034 | container fuzz — deterministic LCG mutation, never crashes | ⬜ | |

### Phase B — `Curve`

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-040 | `Curve` schema v1 (`CRVH`/`CRVK`) encode/decode | ✅ | |
| CNBF-041 | `Curve` round-trip + determinism tests | ✅ | |
| CNBF-042 | `Curve` corruption tests (bad enum, count/size mismatch, missing chunk) | ✅ | |

### Phase C — `AnimationClip`

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-050 | `AnimationClip` schema v1 (`ACLH`/`ACLT`/`ACLK`) encode/decode | ✅ | flat key array; tracks hold `(firstKey,count)` |
| CNBF-051 | round-trip + determinism tests | ✅ | |
| CNBF-052 | corruption tests (track range past key array, count/size mismatch, bad space) | ✅ | |

### Phase D — CNJ → CNB compiler

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-060 | `CnjToCnb` library entry point (JSON in, bytes out) | ⬜ | reuses `CNA::Internal::Json` + `CnjEnvelope` |
| CNBF-061 | `Curve` compilation | ⬜ | |
| CNBF-062 | `AnimationClip` compilation, both `tracks` and `clipFile` forms | ⬜ | absorbs the `.clip.bin` sidecar |
| CNBF-063 | `cna_tool_cnj_to_cnb` CLI + cmake wiring | ⬜ | mirrors `cmake/ToolGltfToCnj.cmake` |
| CNBF-064 | compiler tests incl. cross-process determinism | ⬜ | |

### Phase E — `Model`

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-070 | `CnbModelData` neutral structs | ⬜ | |
| CNBF-071 | `Model` schema v1 chunk set + codec | ⬜ | `MDLH/MSTR/MBON/MMSH/MVTX/MIDX/MMRP/MSKL/MANM/MLIT` |
| CNBF-072 | codec round-trip + corruption tests (no GraphicsDevice needed) | ⬜ | |
| CNBF-073 | compiler: `.cnj` Model + all binary sidecars → one `.cnb` | ⬜ | |
| CNBF-074 | runtime adapter `CnbModelData` → `Graphics::Model` in `ContentManager.cpp` | ⬜ | |
| CNBF-075 | CNJ-vs-CNB observable-equivalence tests on a real compiled asset | ⬜ | |
| CNBF-084 | compiler refuses (loudly) the documented out-of-scope `.cnj` fields | ⬜ | material variants |

### Phase G — `ContentManager` integration

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-080 | `CnbLoaderRegistry` + built-in registrations | ✅ | |
| CNBF-081 | `ContentManager::Load<T>` `.cnb` tier | ✅ | additive `if`, mirrors the `.xnb` tier |
| CNBF-082 | `RegisterCnbLoaderEXT<T>` for custom game types | ✅ | |
| CNBF-083 | resolution-order tests `xnb > cnb > literal > cnj > native` | ✅ | |

### Phase H — documentation & measurement

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-090 | `docs/cnb-format.md` — authoritative spec incl. annotated hex | ⬜ | |
| CNBF-091 | file-count / byte-size / open-count measurement, CNJ vs CNB | ⬜ | §8 |
| CNBF-092 | final architectural review pass | ⬜ | §9 |

### Out of scope for v1 (recorded, not started)

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-100 | Extract a shared `.cnj`/`.cnb` model builder out of `ContentManager.cpp` | ⛔ | see D9 |
| CNBF-101 | `Texture2D`/`TextureCube`/`Texture3D` schemas incl. multi-representation | ⛔ | id reserved, see D10 |
| CNBF-102 | `SpriteFont` schema | ⛔ | id reserved |
| CNBF-103 | `SoundEffect` / `Song` / `Video` schemas incl. external streaming payloads | ⛔ | id reserved |
| CNBF-104 | `Effect` schema | ⛔ | id reserved |
| CNBF-105 | Chunk compression (LZ4/Zstd) | ⛔ | field reserved, reader rejects ≠ 0 |
| CNBF-106 | Direct glTF/PNG/WAV → `.cnb` importers | ⛔ | |
| CNBF-107 | `.cnapak` package format | ⛔ | different project |
| CNBF-108 | Memory-mapped / zero-copy chunk access | ⛔ | alignment field reserved for it |

---

## 7. Testing strategy

* **Unit, no device.** Everything in Phases A–D and `CNBF-070`–`073` is testable with no
  `GraphicsDevice`, so those suites run under `SDL_VIDEODRIVER=dummy`.
* **Adversarial by construction.** Every container invariant in §4 has at least one test that
  *makes a valid file and then breaks exactly that invariant*, asserting a `ContentLoadException`
  rather than a crash. The helper `MutateValidFile(f)` in `CnbContainerTests.cpp` builds the valid
  file once per test so the "before" state is never in doubt.
* **Fuzz.** `CnbContainerFuzzTests.cpp` reuses `XnbContainerFuzzTests.cpp`'s deterministic LCG and
  mutation shape: no `std::random_device`, fixed seed, thousands of mutations of real Curve /
  AnimationClip / Model `.cnb` files, asserting *loads or throws cleanly*.
* **Equivalence.** `CnbModelEquivalenceTests.cpp` compiles a real `.cnj` model (produced by the
  real `cna_tool_gltf_to_cnj` from a real glTF fixture) to `.cnb`, loads both through
  `ContentManager`, and compares the observable model: bone count/names/parents/transforms, mesh
  and part counts, per-part vertex/index counts and primitive counts, effect types, morph target
  data, skeleton arrays and clip contents.
* **Determinism.** Byte-comparison of two writer runs (in-process) and of two compiler runs
  (separate OS processes, via `posix_spawn`, matching `GltfToCnjToolTests.cpp`'s approach).

---

## 8. Measurements

Recorded by `CNBF-091`; see §8 of the final report and `docs/cnb-format.md` §12.

---

## 9. Stability gate

CNB v1 is declared stable only when all of the following hold; the current answer is recorded
against each.

| criterion | status |
|---|---|
| every container invariant in §4 has a dedicated negative test | ⬜ |
| at least three real CNA asset types implemented end to end | ⬜ Curve, AnimationClip, Model |
| a real (not synthetic) asset compiles and loads equivalently | ⬜ `CNBF-075` |
| writer output is byte-deterministic in-process and cross-process | ⬜ `CNBF-033`, `CNBF-064` |
| documentation matches the bytes the implementation writes | ⬜ `CNBF-090` + `CnbSpecConformanceTests.cpp` |
| container fuzzing finds no crash | ⬜ `CNBF-034` |

