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

**What actually landed, versus what D9 planned.** Option 3 was taken, with one deliberate,
contained piece of option 1: the ~200-line block that builds a part's `Effect` and applies its
complete material state was extracted out of `ModelTypeReader::Read` into a file-scope
`BuildPartEffectEXT`, which both the `.cnj` reader and the `.cnb` loader now call. The alternative
was a third copy of the PBR/DualTexture/unlit/lighting wiring in the same file, which would have
drifted. The extraction is mechanical (the two paths differ only in how they arrive at eight
resolved texture asset names), it is confined to one file, and it was verified against the existing
model suites — including `GltfToCnjToolTest`'s own offline-versus-runtime L6 material comparison
and skinning-data sweep, which are precisely the tests a mistake here would break. The rest of
option 1 — a shared `.cnj`/`.cnb` *mesh builder* — remains future work as `CNBF-100`.

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
| CNBF-034 | container fuzz — deterministic LCG mutation, never crashes | ✅ | 17 000 mutated inputs across Curve/AnimationClip/Model plus pure noise |

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
| CNBF-060 | `CnjToCnb` library entry point (JSON in, bytes out) | ✅ | reuses `CNA::Internal::Json` + `CnjEnvelope` |
| CNBF-061 | `Curve` compilation | ✅ | |
| CNBF-062 | `AnimationClip` compilation, both `tracks` and `clipFile` forms | ✅ | absorbs the `.clip.bin` sidecar |
| CNBF-063 | `cna_tool_cnj_to_cnb` CLI + cmake wiring | ✅ | `cmake/ToolCnjToCnb.cmake`, mirroring `cmake/ToolGltfToCnj.cmake` |
| CNBF-064 | compiler tests incl. cross-process determinism | ✅ | |

### Phase E — `Model`

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-070 | `CnbModelData` neutral structs | ✅ | |
| CNBF-071 | `Model` schema v1 chunk set + codec | ✅ | `MDLH/MSTR/MBON/MMSH/MVTX/MIDX/MMRP/MSKL/MANM/MLIT` |
| CNBF-072 | codec round-trip + corruption tests (no GraphicsDevice needed) | ✅ | |
| CNBF-073 | compiler: `.cnj` Model + all binary sidecars → one `.cnb` | ✅ | |
| CNBF-074 | runtime adapter `CnbModelData` → `Graphics::Model` in `ContentManager.cpp` | ✅ | |
| CNBF-075 | CNJ-vs-CNB observable-equivalence tests on a real compiled asset | ✅ | |
| CNBF-084 | compiler refuses (loudly) the documented out-of-scope `.cnj` fields | ✅ | material variants |

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
| CNBF-090 | `docs/cnb-format.md` — authoritative spec incl. annotated hex | ✅ | pinned by `CnbSpecConformanceTests.cpp`, verified to fail on a doc-only edit |
| CNBF-091 | file-count / byte-size / open-count measurement, CNJ vs CNB | ✅ | §8 |
| CNBF-092 | final architectural review pass | ✅ | §10; found and fixed four real issues, one of them a compiler defect |

### Out of scope for v1 (recorded, not started)

| ID | Task | Status | Notes |
|---|---|---|---|
| CNBF-100 | Extract a shared `.cnj`/`.cnb` mesh builder out of `ContentManager.cpp` | ⛔ | see D9; the *effect/material* half of it did land, as `BuildPartEffectEXT` |
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

## 7a. Test and build state at the end of this work

**Suites.** 168 CNB tests across 11 gtest suites, all passing:

| suite | tests | what it holds |
|---|---|---|
| `CnbContainerTest` + `CnbCrc32cTest` | 65 | one negative test per container invariant, plus CRC known answers |
| `CnbCurveCodecTest` | 16 | `Curve` schema |
| `CnbAnimationClipCodecTest` | 13 | `AnimationClip` schema |
| `CnbModelCodecTest` | 28 | `Model` schema, device-free |
| `CnbContentManagerTest` | 13 | the `.cnb` tier, resolution order, custom types |
| `CnbCompilerTest` + `CnbCompilerToolTest` | 11 | the compiler, as a library and as a real process |
| `CnbModelEquivalenceTest` | 5 | `.cnj`-versus-`.cnb` on real assets, plus the measurement sweep |
| `CnbSpecConformanceTest` | 8 | the document against the implementation |
| `CnbContainerFuzzTest` | 4 | 17 000 mutated inputs + 3 000 noise inputs |

**Sanitizers.** `build-asan` (`-DCNA_SANITIZE=address,undefined`): all 163 device-independent CNB
tests and all 380 content-module tests pass with **zero** sanitizer reports.

**Regression.** Full `ctest -j8` on `cmake-build-debug`: **8321 tests, 37 failures**. The
pre-existing baseline for this branch, measured before any CNB code existed, was **8157 tests, 29
failures** — 25 of which are the same tests. The difference is accounted for and none of it is CNB:

* +164 tests are CNB's own.
* 9 `EasyGL_*` failures were `(Not Run)` in the baseline because that measurement built only
  `CnaTests`; the final run builds everything, so those renderer executables now actually run.
  They are llvmpipe/Mesa-under-Xvfb failures in code CNB does not touch.
* 3 (`LeaderboardReaderTest`, `StorageDeviceDeleteContainerTest`, one `CueTest`) **pass in
  isolation** — parallel-run flakes in unrelated modules.
* 4 baseline failures (2 `ENetDiscoveryServiceTest`, 1 `WaveBankTest`, 1 `CueTest`) passed this
  time, which is the same networking/audio flakiness from the other direction.

The whole content-module test surface — 1077 tests — passes with exactly the two failures it had
before this work (`GltfLimitationsDoc.CnaextSection32DoesNotClaimMoreThanTheRegistry`,
`GltfRendererPbrFallbackPolicy.EveryPbrRendererHonorsCallerOwnedCullState`).

**Platform boundary gates.** 4 of 5 pass. `tools/platform/sdl_inventory.py --check` reports
`plans/plan_platform.md` §2 out of date — **pre-existing**: that file is byte-identical to its state
at this branch's fork point, no CNB file contains the string `SDL`, and the drift includes one
*fewer* SDL-referencing file, which an addition cannot cause. Left alone as unrelated scope.

**Build artifacts.** `build-asan/` is 3.8 GB and exists only for the sanitizer run above; it can be
deleted. `cmake-build-debug/` is 27 GB and is the normal incremental build directory.

---

## 8. Measurements

Recorded by `CnbModelEquivalenceTest.CompilingAModelReducesItsFileCountAndItsFileOpens`, which
prints every row it measures so a run of the suite *is* the record. Machine: this repository's
`cmake-build-debug` (GCC 14.2, Debug). Assets: real fixtures from the committed glTF conformance
corpus, converted by the real `cna_tool_gltf_to_cnj` and then compiled by `CompileCnjToCnb`.

| asset | `.cnj` form | `.cnb` form | file opens per load |
|---|---|---|---|
| `skin-four-weighted` | 4 files / 2666 B | 1 file / 2704 B | 4 → 1 |
| `morph-eight-targets` | 4 files / 2048 B | 1 file / 1808 B | 4 → 1 |
| `two-primitives-one-buffer` | 5 files / 1482 B | 1 file / 1718 B | 5 → 1 |
| `mat-normal-occlusion-scale` | 4 files / 1197 B | 2 files / 1468 B | 4 → 2 |
| `anim-two-clips` | 5 files / 1834 B | 1 file / 1712 B | 5 → 1 |

**What this shows, stated no more strongly than it supports.** The file count and the number of
filesystem opens per load fall for every asset measured, which is the property CNB was built for
and the one that scales with asset complexity rather than with asset size. The **byte size is a
wash** on fixtures this small: they carry a handful of vertices each, so the fixed-stride material
record (368 B per distinct material) and the container's own 64-byte header plus 48 bytes per chunk
are a large fraction of the file, and two of the five got bigger. On an asset where geometry
dominates -- which is every real model -- the compiled form's geometry chunks are the same bytes
the sidecars held, so the difference converges to the JSON descriptor that is no longer there. That
larger measurement has **not** been taken here and is not claimed.

Load *time* was deliberately not measured: on assets this small the numbers would be dominated by
process and device setup, and a timing claim that cannot be reproduced is worse than no claim.

---

## 9. Stability gate

CNB v1 is declared stable only when all of the following hold; the current answer is recorded
against each.

| criterion | status |
|---|---|
| every container invariant in §4 has a dedicated negative test | ✅ 63 container tests, one per invariant |
| at least three real CNA asset types implemented end to end | ✅ `Curve`, `AnimationClip`, `Model` |
| a real (not synthetic) asset compiles and loads equivalently | ✅ 15 real corpus fixtures plus a hand-written hierarchy-less one, `CNBF-075` |
| writer output is byte-deterministic in-process and cross-process | ✅ `CNBF-033`, `CNBF-064` |
| documentation matches the bytes the implementation writes | ✅ `CnbSpecConformanceTests.cpp`, verified to fail on a doc-only edit |
| container fuzzing finds no crash | ✅ 17 000 mutated inputs + 3 000 noise inputs, clean under ASan+UBSan |

**Verdict: the implemented parts of CNB are v1-stable.** The container layout (§3–§6 of
`docs/cnb-format.md`), the asset-type identifier ranges and the three implemented schemas are
frozen: a future CNA must keep reading a file written today. What is *not* frozen — because it does
not exist — is every schema in §15 of that document; those identifiers are reserved and their
layouts are undesigned.

Two things earn that verdict rather than merely accompanying it. Every invariant has a negative
test that makes a valid file and breaks exactly that one thing, so "the reader rejected it" can
never be confused with "the fixture was never valid". And two of the gates were checked for teeth
by deliberately breaking them: making the Model loader apply an identity bone transform instead of
the stored one fails the equivalence tests, and editing two numbers in the specification alone
fails two of the eight conformance tests by name.

---

## 10. Final review (`CNBF-092`)

Worked through against the implementation, not from memory. Four real issues were found and
fixed rather than written down — one of them a defect in the compiler that no existing test could
have caught.

| question | answer |
|---|---|
| Is the format tied to the C++ ABI? | No. Every value is assembled from individual bytes; the only `sizeof` uses are `static_assert(sizeof(float) == 4)` guards; no `std::type_index`, no struct `memcpy`, no padding dependency. |
| Are all serialized widths explicit? | Yes — `u8`/`u16`/`u32`/`u64`/`i32`/`f32`/`f64` and a length-prefixed UTF-8 `String`, nothing else. |
| Is endianness explicit? | Yes, little-endian, asserted byte-for-byte by `PrimitiveEncodingIsLittleEndianAndAbiIndependent`. |
| Are integer operations overflow-safe? | Yes. Two file-declared values are always combined through `CheckedAdd`/`CheckedMultiply`; a `u32` count times a compile-time stride is widened to `u64` first, where no overflow is representable. |
| Can malformed files cause excessive allocations? | **Found and fixed.** Three allocation sites were bounded only by the generic 16-million-element array limit while their in-memory footprint was many times their encoded size. Morph target and morph weight-key counts gained schema-level ceilings matching `ContentManager`'s own `.cnj` readers, and the Model part count is now cross-checked against the actual `MVTX`/`MIDX` chunk count *before* any per-part allocation — which is both a stronger correctness check and a much tighter bound. Three tests added. |
| Are unknown chunks handled correctly? | Yes: unknown + optional is skipped, unknown + mandatory is refused by name. Both tested, at container level and per schema. |
| Can future schema versions evolve? | Yes. A higher container *minor* version is accepted; real incompatibility travels as a mandatory chunk. `RequireAsset` accepts a range of schema versions per type. |
| Is container versioning separate from asset schema versioning? | Yes, two independent header fields, tested. |
| Are type IDs stable? | Yes, frozen and pinned by `CnbSpecConformanceTests`. Custom-id collisions are refused at registration rather than resolved silently. |
| Is writing deterministic? | Yes, in-process and across two OS processes. |
| Did CNB stay independent of XNB? | Yes. `grep` over the whole CNB subsystem finds two *comments* naming XNB and zero includes, symbols or types. |
| Did CNJ remain usable? | Yes. `.cnj` still loads on its own with no `.cnb` present (asserted in `ACurveCnbOutranksASameNamedCurveCnj`), and the whole content-module suite is 1077 passing with the same two pre-existing failures as before this work. |
| Did we avoid binary JSON? | Yes. A Model `.cnb` is flat fixed-stride descriptor tables plus raw geometry chunks; there is no JSON DOM anywhere in the file or the reader. |
| Did we avoid unnecessary `ContentManager` churn? | Mostly. The public surface grew by two additive members and one `if` in `Load<T>`. One ~200-line block was *moved* out of `ModelTypeReader` into a shared helper — justified in `D9`, and the reason the alternative was worse. |
| Are embedded versus external resources coherent? | Yes. Data the asset owns is embedded; shared assets stay external through `XREF`, which is discoverable without understanding the schema. |
| Can Model `.cnb` actually eliminate the sidecars? | Yes, proven by loading the compiled asset from a directory that contains nothing else. |
| Are the tests enough to freeze what is implemented? | Yes — see §9. |
| Does the documentation match the bytes? | Yes, and the check fails if either side moves alone. |

### Issues found by this pass and fixed

1. **Unbounded-ish allocation on three Model decode paths** (above). Fixed, with three tests.
2. **A behaviour change smuggled in by the effect-block extraction.** The first version of
   `BuildPartEffectEXT` took eight *pre-resolved* texture asset names, which meant the `.cnj` path
   started resolving (and therefore containment-checking) fields the constructed effect would never
   read — turning a silently-ignored bad path into a load failure. Real, narrow, and not this
   task's call to make. The helper now takes a resolver callback and each field is resolved at
   exactly the point the original code resolved it.
3. **A real defect in the compiler, found by closing a coverage gap the review noticed.** Every
   fixture `cna_tool_gltf_to_cnj` produces is `cnjVersion` 2 with an explicit `bones` array, so the
   equivalence sweep never exercised the other shape a Model `.cnj` can have: a hand-written
   version-1 document with no hierarchy, where the runtime reader synthesises a root plus one child
   bone per mesh. A test for that shape was added — and it failed. `CnbModelBone{name, parent, {}}`
   is aggregate initialisation, and supplying `{}` for the transform member **suppresses** that
   member's identity default-member-initialiser and value-initialises the matrix to all zeros
   instead. Every synthesised bone therefore reached the runtime with a zero transform where the
   `.cnj` path gave it `ModelBone`'s identity. Fixed at both construction sites, with the reason
   written into the source so the pattern is not copied.
4. **An overstated claim in the specification.** §12 said *every* `count × elementSize` computation
   goes through `CheckedMultiply`; the constant-stride ones do not, because they cannot overflow.
   Corrected to say what is actually true and why.

### Known limitations, stated rather than implied

* Only three asset types have schemas. Eight more identifiers are reserved with no layout.
* The Model schema does not express glTF material variants or the glTF import diagnostic report.
  The compiler refuses the former by name and drops the latter; both are documented.
* The compiler's Model front end is the one place that re-reads a `.cnj` shape rather than going
  through the reader that already understands it (`D9`). The drift risk is real and is mitigated by
  the equivalence tests rather than removed.
* Byte size is not a measured win on small assets (§8). File count and opens-per-load are.
* Load *time* has not been measured at all, and no claim is made about it.
* The `.clip.bin`/`.skeleton.bin`/`_morph.bin` sidecar readers CNB's compiler front end contains
  assume little-endian, where the pre-existing `.cnj` readers use native order. Identical on every
  target CNA supports; noted because it is a difference rather than an accident.

---

## 11. Hardening pass (`CNBF-H001`–`CNBF-H011`)

A second, independent engineering review of the branch — deliberately not trusting §10's own
verdict. Its premise was that §10 found four issues *by reading the code*, which means the suites
that should have defended those properties did not exist; so this pass looked for the same shape of
gap, and fixed what it found.

**Four defects confirmed, three of them memory- or correctness-unsafe.** Four further problems were
found while fixing and testing them — `CNBF-H004`, `CNBF-H008` and the two in `CNBF-H012`, of which
`CNBF-H008` and `CNBF-H012` are correctness bugs in their own right. One reported concern turned out
not to be a defect at all. Every fix ships
with the test that would have caught it, and where a gate could be checked for teeth it was.

| ID | Issue | Root cause | Fix | Tests | Status |
|---|---|---|---|---|---|
| CNBF-H001 | **Dangling read-limit pointer.** `CnbDocument` and `CnbByteReader` each stored `const CnbReadLimits*` into caller-owned storage, so `Parse(bytes, name, CnbReadLimits{})` — the obvious call — left the document holding the address of a temporary that died at the end of that full-expression. Every later chunk read and `Limits()` call dereferenced freed stack. | A reference-semantics choice for a 24-byte trivially-copyable configuration struct, where the API gave the caller no way to know lifetime mattered. | Store **by value** in both. No heap and no `shared_ptr`: the struct is smaller than the `std::string` the reader already holds. | 4 (temporary, out-of-scope named local, direct cursor construction, cursor-inherits-document-limits) | ✅ |
| CNBF-H002 | **Custom asset types dispatched on 31 bits of hash alone.** A custom id is `FNV-1a-32(name) \| 0x80000000`; two unrelated game types can collide, and the loader was looked up by number with the file's own type name used only in an error message. A colliding file was decoded by the wrong loader in silence. | The collision defence was designed (`Register` refuses two names per id) but only covered types registered *in the same process* — not a file produced by a different program. | The canonical type name is now load-bearing for custom types: `ResolveForDocument` requires it present and equal to the registered name; `Register` refuses a name that does not hash to its id; `CnbWriter::Build` refuses to **produce** a custom file that could never load. Built-ins unchanged — CNA assigns and freezes those ids, so a numeric match *is* proof of identity there. | 7 | ✅ |
| CNBF-H003 | **Registry data race and pointer invalidation.** A `static std::unordered_map` was mutated without synchronisation while *every* `ContentManager` constructor registers into it, and `Find()` returned a pointer into it that any later registration could invalidate. | Written for a single-threaded assumption that the API does not state and `ContentManager` does not honour. | `std::shared_mutex`; lookups return the loader **by value**. | 2, incl. an 8-thread × 200-iteration test asserting only interleaving-independent outcomes so it can never flake | ✅ |
| CNBF-H004 | **`const` accessors mutated shared state.** `Metadata()`/`ExternalReferences()` decoded lazily into `mutable` members, which the `const` dispatch path in CNBF-H002 cannot use safely. | Lazy decoding chosen for a cost that does not exist — both chunks are tiny. | Decoded during `Parse()`. A document is now immutable once it exists, and a malformed `CMET` or root-escaping `XREF` is a **parse** failure rather than a surprise from whichever call touched it first. | 2 existing tests moved to the earlier, fail-fast throw | ✅ |
| CNBF-H005 | **Overflow contract not enforced where it was claimed.** `ReadCount` multiplied its `u32` count by a caller-supplied `u64` element size directly. | Safe for every current caller, but that is a property of the callers while the specification promised the *operation* was safe. | Through `CheckedMultiply`. | 1, using an element size no real caller would pass | ✅ |
| CNBF-H006 | **Compiler strictness against malformed sidecars was untested.** | — (a coverage gap, not a defect: **15 of the 16 new tests passed on first run**; the checks were written and never exercised. The one failure was the new test's own assertion.) | 16 tests pinning truncation, trailing bytes, ragged geometry, absurd/negative counts, non-finite times, bad tangent-trailer magic/version/count, missing files, root-escaping paths — with a positive control that the uncorrupted fixture compiles, and an explicit statement that CNB is stricter here than the runtime `.cnj` reader. | 16 | ✅ |
| CNBF-H007 | **Conformance rested on textual scraping.** `CnbSpecConformanceTests` pinned the document to the code, which catches a document that drifts but not the two drifting together, and said nothing about actual bytes. | — | Golden byte vectors generated by a **separate Python implementation of the specification** (`tools/cnb/gen_golden_vectors.py`), asserted in both directions; plus a test that flips one bit of every byte of a vector in turn and requires each to be refused. | 6 | ✅ |
| CNBF-H008 | **A bone table not ordered parent-before-child was accepted and silently produced wrong world transforms.** `Model::CopyAbsoluteBoneTransformsTo` composes in one ascending pass reading `dest[parentIndex]`, so a forward parent reads a slot not yet written. It does not crash, which is why it had to be refused. | Only the range `parent < boneCount` was validated; the ordering the consuming code assumes was not. | Parent-before-child enforced on encode and decode, for `MBON` and `MSKL`. Also makes cycles structurally impossible. Confirmed against all 15 real corpus fixtures — no authored asset violates it. | 3 | ✅ |
| CNBF-H009 | **The aggregate-initialisation hazard had no permanent guard.** `CnbModelBone{name, parent, {}}` suppresses the identity default-member-initialiser (the §10 defect). | — | A test that pins the default *and* asserts the braced form still differs, so if someone designs the hazard away the test says so rather than silently passing. | 1 | ✅ |
| CNBF-H010 | Container coverage gaps: per-type ordinals were only ever exercised with 2–3 chunks despite existing to remove a primitive-count cap; a chunk overlapping the *table of contents* was untested; float bit-exactness was never asserted. | — | 500-chunk ordinal test; chunk-inside-TOC test with all three checksums repaired so the overlap is the only fault; NaN/±Inf/−0/denormal bit-exact round trip, compared as bits because `NaN != NaN` and `-0.0 == 0.0`. | 4 | ✅ |
| CNBF-H012 | **Two more Model holes found while testing the above.** `primitiveCount` was stored but never cross-checked, so a part could claim more primitives than its indices describe and draw past its own index buffer. And index *values* were never range-checked, so an index at or above `vertexCount` reached the GPU as an out-of-range fetch. | Redundant data trusted rather than verified; and a validation pass nobody had asked for because the compiler always produced consistent files. | `primitiveCount` cross-checked against the topology on encode and decode, using a derivation written independently of the importer's own so the check is not a tautology. Every index range-checked at decode — one pass over bytes already being copied. | 4, incl. a per-topology sweep so the rule is not assumed to be "triangles" | ✅ |
| CNBF-H011 | Specification updated for the contract changes, with the new clauses pinned **behaviourally** as well as textually. | — | §5.1, §7, §11, §12, §13 rewritten. Two new conformance tests assert each clause against real behaviour, not only against the sentence. | 2 | ✅ |

### Reported concern that was NOT a defect

The review brief asked whether `count * elementSize` arithmetic was unsafe *in general*. Audited
across the container, all three schemas, the compiler and the writer: every other such computation
multiplies a `u32` count by a **compile-time** stride into a `u64`, where the largest representable
product is under 2⁴¹ and no overflow exists. Only `ReadCount`'s caller-supplied element size was a
real gap (`CNBF-H005`). The claim in `docs/cnb-format.md` §12 was already accurate and was left
alone.

### Gates checked for teeth

Not assumed to work — each was broken on purpose and observed to fail:

* Removing the registry's locks makes the concurrency test report real `unordered_map` data races
  under ThreadSanitizer (`hashtable.h` `_M_find_before_node`, `size`). Restoring them: zero reports.
* Editing two numbers in `docs/cnb-format.md` alone fails two spec-conformance tests by name.
* The new `primitiveCount` and index-range checks immediately failed four of this branch's own
  synthetic fixtures, which had been asserting that a byte-ramp index buffer was fine. Those
  fixtures now build real index runs — the tests were weaker than they looked.
* Deleting either new contract clause from the document fails its new conformance test by name.

### Did any serialized CNB v1 byte change?

**No.** The golden vectors — generated independently from the specification — match the writer
byte-for-byte, and every pre-existing round-trip and determinism test passes unchanged. What
changed is what a reader *accepts* (stricter: forward bone parents, unnamed custom types) and what
the writer *refuses to produce* (custom types without a matching canonical name). Files the previous
state could legitimately produce all still load.
