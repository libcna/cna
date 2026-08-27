# plan_cnb.md — CNB, the CNA-native compiled binary content format

> **Current state:** §16.2 is the authoritative matrix — wire schema, runtime loader, writer
> API and producer are four different claims and it keeps them apart. Earlier tables in this
> document are historical snapshots and are banner-marked as such.

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

> **Historical snapshot.** The per-component verdicts here were superseded by §12, then §12.1,
> and the authoritative current state is now **§16.2**. Kept for the gate criteria it records.


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

| CNBF-H013 | No way to inspect a `.cnb` without a debugger, and no way for a build script to ask what one depends on. | — | `cna_tool_cnb_info`: a schema-blind inspector and validator. `--refs` prints one dependency per line, which is the container-level `XREF` table's whole purpose made usable; every structural invariant runs while reading, so a non-zero exit makes it a content-pipeline gate on its own. It knows nothing about any schema, which is a design check as much as a feature — anything it cannot report is something the container failed to make self-describing. | 4, spawning the real executable and asserting on its stdout | ✅ |
| CNBF-H014 | **A hardening test depended on a sibling test having run.** `ABuiltInTypeDispatchesOnItsNumberAlone...` relied on some other test constructing a `ContentManager` (which registers the built-in loaders) and passed under a filtered run while failing under `ctest`, which gives each case its own process. | Written against the convenience of a shared process. | The fixture asks for the built-ins explicitly. Found by `ctest`'s per-test isolation, which is exactly what it is for. | — (the fix is to an existing test) | ✅ |

| CNBF-H015 | **`Model` was the one implemented schema with no independent byte-level check.** Its 368-byte material record is the likeliest thing in CNB to drift unnoticed, because a round-trip test encodes and decodes the same wrong value and a changed default is invisible to it. | A round-trip is not an oracle. | A golden vector written from the specification, pinning every material default explicitly, plus the per-component stability reassessment in §12. Writing it paid for itself immediately: the independently-written generator produced a two-entry string table where the writer produces one, because the table deduplicates and both the part and the mesh are named `Hull`. The vector doing exactly its job; the generator was corrected and the dedup is now pinned in the bytes. | 1 golden vector, both directions | ✅ |
| CNBF-H016 | **The morph schema ceilings were applied *after* the fit-in-remaining check.** A count of two million morph weight keys is refusable on sight, but the refusal waited until the file had actually supplied two million elements' worth of bytes — so a hostile count cost the validator real memory first, and it forced the ceiling's own test to build a 40 MB fixture. | Checks written in the order they were thought of rather than in order of cost. | Both morph counts now read the raw value (limit-checked, fit unchecked), apply the schema ceiling, then check the fit explicitly. Found when that fixture failed in a full regression run under external memory pressure — fragile for a reason worth fixing. The two tests no longer supply the bytes at all and assert on the *reason* for the refusal; they could previously have passed on "ran out of bytes" while the ceiling did nothing. | 2 rewritten | ✅ |

| CNBF-H017 | **The public API documented `CMET` as debug-only while the code dispatched on it.** `CnbDocument.hpp` called `CnbMetadata` "debug-only provenance strings" and `assetTypeName` "Diagnostic only. Dispatch is driven by the header's numeric asset type identifier"; `CnbFormat.hpp` called the chunk "optional debug metadata". Since `CNBF-H002` that is false for a custom asset type, where the name is load-bearing and its absence is a hard rejection. | The comments were written before `CNBF-H002` introduced the name check and were not revisited when the behaviour changed underneath them. Comment drift in the one direction that misleads: a game author reading only the header would strip `CMET` from a custom-type file as dead weight and be unable to load it. | Rewrote the `CnbMetadata` and `Metadata()` documentation around the built-in/custom asymmetry, naming §5.1/§7 and `CNBF-H002`, and stating that `present == false` is valid for a built-in type and invalid for a custom one. `contentName` is still described as provenance, because it genuinely is. Fixed the `CnbFormat.hpp` chunk comment. Audited every other mention (`CnbLoaderRegistry.hpp`, `ContentManager.hpp`, `docs/`, `misc/`) — those two files were the only stale ones. | — (documentation of behaviour already covered by 7 `CustomTypeFixture` tests) | ✅ |
| CNBF-H018 | **The specification's §7 contradicted its own §5.1.** §7 opened "The header's `assetTypeId` is what selects a loader. There is no type name in the dispatch path." — flatly untrue since `CNBF-H002`, and contradicted 30 lines later in the same document. | Same root cause as `CNBF-H017`: the sentence predates the check. | §7 rewritten from the implementation rather than from memory: the identifier selects a *candidate*, and the ordered algorithm is spelled out — registry lookup **first**, so an unregistered custom identifier is reported as an unknown type and not as a name mismatch; built-in accepted on the number alone; custom additionally requiring a present, non-empty, exactly-matching `CMET` name. §13 also corrected, where `CMET` was called unqualifiedly "optional". | 1 new conformance test asserting the ordering behaviourally, not just the sentence | ✅ |
| CNBF-H019 | **The IEEE-754 dependency was assumed, not enforced.** Four scattered function-local `static_assert(sizeof(float) == 4)` were the entire guarantee. Size is the weakest of the properties CNB actually relies on: a 4-byte non-binary32 float, or a host whose float and integer byte orders disagree, would pass every one of them and silently write files no other host could read. | The asserts were added where a `bit_cast` was written rather than where the contract lives, so each one only defended its own line. | One contract block at namespace scope in `CnbFormat.hpp`, the header every reader and writer already includes. `is_iec559` is necessary but **not** sufficient — checked, not assumed: it says nothing about float-vs-integer byte order, and GCC keeps it `true` under `-ffast-math` (measured on this toolchain; clang reports `false`). So the block also asserts `radix`/`digits`, the sizes, and — the part `is_iec559` cannot give — `constexpr std::bit_cast<std::uint32_t>(1.0f) == 0x3F800000` and the `f64` equivalent, which is a direct compile-time check of the object representation. Deliberately **not** asserted: host endianness (the container is byte-order independent by §2; asserting little-endian would contradict the specification) and arithmetic behaviour (CNB never computes with a serialized value). `<bit>` and `<limits>` included explicitly; `CnbByteReader.hpp`/`CnbByteWriter.hpp` now include `CnbFormat.hpp` explicitly rather than inheriting it transitively; the four redundant local asserts removed. Teeth verified by breaking the byte-order constant on purpose and watching the build fail. | 1 new conformance test; the compile-time half is proven by the suite building at all | ✅ |
| CNBF-H020 | **Final v1 consistency audit.** Read the frozen implementation against the authoritative specification once more, asking only two questions: does the writer write exactly what the document says, and does the reader accept and reject exactly what the document promises. Found three further drifts and one gap, all documentation-level — **no serialized byte changed**. | Three of the four are the same failure mode as `CNBF-H017`/`H016`: prose that was true when written and was overtaken by a later change. The fourth is an omission. | (a) §4 stated the chunk alignment ceiling as a fixed `1…4096` format constant, but the code compares against the *configurable* `maxChunkAlignment`; corrected to name the limit. (b) §5 presented the uppercase/lowercase chunk-identifier namespace split as a rule; it is an unenforced convention — only printability is checked — and is now labelled as such, which matters because the enforced rule sits in the same paragraph. (c) §13's `CMET` wording, folded into `CNBF-H018`. (d) The specification said nothing whatever about NaN, ±Inf or −0.0; new **§2.1** states what is actually true: the container stores the bit pattern verbatim, never canonicalises, keeps −0.0 distinguishable from +0.0, and explicitly does **not** promise signalling-NaN preservation on every ABI (`ReadF32`/`ReadF64` return by value, so an x87 ABI could quiet one — preserved on x86-64, measured; 32-bit x87 not measurable here, no `-m32` multilib). The schema layer may be narrower and says so. `misc/cnb.md` given a banner marking it the original proposal, not the specification, since its header and `ChunkEntry` sketches differ from what was built. Golden vectors regenerated by the independent Python implementation and byte-compared: `Curve` 316 B, `AnimationClip` 456 B, `Model` 1094 B — all three identical. (e) Two task IDs, `CNBF-H015` and `CNBF-H016`, were cited in commit subjects (`3a806c9e5`, `858ea9db7`) but never given rows in this table; the rows above were reconstructed from those commits, and this pass's own tasks renumbered to `H017`–`H020` so the citations keep pointing where they always did. | 2 new conformance clauses; existing custom-loader and float coverage audited and found already sufficient, so nothing redundant was added | ✅ |

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

---

## 12. Stability reassessment after the hardening pass

> **Historical snapshot.** Superseded by §12.1 and then by **§16.2**, which is the one table to
> read for current state. Kept because the *reasoning* for splitting stability per component is
> still the reasoning that applies.


§9 declared "the implemented parts of CNB are v1-stable" as a single verdict. That was too coarse:
the byte-level format and the C++ extension API are different things with different audiences and
different costs to change, and this pass changed one of them. Reassessed per component.

| component | verdict | why |
|---|---|---|
| **Container byte format** | **Frozen.** | Unchanged by this pass. Now pinned by golden vectors generated from the specification text by a *separate* implementation, in both directions, plus a sweep that flips one bit of every byte and requires each to be refused. Every invariant has a dedicated negative test; 17 000 fuzzed inputs escape as nothing but `ContentLoadException`; clean under ASan+UBSan and TSan. |
| **`Curve` schema 1** | **Frozen.** | Bytes unchanged. Golden vector in both directions. Complete negative coverage. |
| **`AnimationClip` schema 1** | **Frozen.** | Bytes unchanged. Golden vector in both directions. Complete negative coverage. |
| **`Model` schema 1** | **Frozen, with the caveat stated.** | Bytes unchanged, and now has its own golden vector covering the 368-byte material record's every default — the likeliest thing to drift in silence, since a round-trip test encodes and decodes the same wrong value. Two *reader* rules were added (parent-before-child, `primitiveCount` cross-check, index range), which make the reader stricter without changing any byte a valid producer emits. The caveat: this is the schema most likely to need *extension* (material variants and the import report are both out of v1), and extension means a schema-version bump, not a container one. |
| **Custom loader C++ API** | **NOT stable — changed by this pass.** | `Find()` now returns `std::optional<LoaderFn>` rather than a raw pointer; `ResolveForDocument()` is new and is the correct entry point for loading; `debugTypeName` became `canonicalTypeName` and is load-bearing for custom types. Anyone building on `CnbLoaderRegistry` should expect further movement. The *file-format* half of the custom-type contract — a `CMET` canonical name that must match — **is** settled and belongs to the frozen container. |
| **Compiler CLI (`cna_tool_cnj_to_cnb`)** | **Not promised yet.** | Unchanged this pass and small (`<input> [output] --content-root --name --quiet`), but nothing depends on it in-tree except tests, and no compatibility promise is worth making until something does. |
| **Inspector CLI (`cna_tool_cnb_info`)** | **New this pass; not promised.** | `--refs`' one-name-per-line output is the part a build script would depend on and the part worth keeping stable; the human-readable form is free to change. |

**What this means in practice.** A `.cnb` written today will be readable by future CNA. A program
compiled against `CnbLoaderRegistry` today may need a small edit. Those are different promises and
the branch should keep saying so separately.

**Format-breaking changes are still allowed on this branch** and this is still the right moment for
them — CNB is unmerged and unreleased. Two contract changes were made here for exactly that reason
(mandatory canonical names for custom types; parent-before-child bone graphs). Neither changed a
byte any valid producer emits, which is why the golden vectors still match.

### 12.1 After the final consistency pass (`CNBF-H017`–`H020`)

Reassessed again, per component, against the repository rather than against the paragraph above.

| component | verdict after this pass | what moved |
|---|---|---|
| **Container byte format** | **Frozen, unchanged.** | Nothing in the wire format was touched. Verified rather than asserted: the independent Python generator was re-run and its three vectors byte-compared against the committed arrays — 316 B, 456 B and 1094 B, all identical. What *was* added is a **host** requirement, not a format one: the `f32`/`f64` representation contract is now enforced at compile time (`CNBF-H019`), so a host that cannot produce conforming bytes fails to build instead of writing files nobody else can read. That tightens who may build CNB; it changes no byte. |
| **`Curve` / `AnimationClip` / `Model` schemas 1** | **Frozen, unchanged.** | No encoder, decoder or stride was modified. The `Model` extension caveat above still stands unaltered. |
| **Custom type *serialized* identity** | **Frozen.** | `FNV-1a-32(name) | 0x80000000` in the header plus a matching `CMET` canonical name. Settled since `CNBF-H002`; this pass only made the documentation say so (`CNBF-H017`, `CNBF-H018`). Worth separating from the row below because the two halves of the custom-type contract have opposite stability: the file's identity is frozen, the C++ that consumes it is not. |
| **Custom loader C++ API** | **Still not stable — but unchanged this pass.** | No signature moved. The instability recorded above is a forward-looking expectation, not something this pass exercised. |
| **Compiler / inspector CLIs** | **Still not promised; unchanged.** | Neither tool was touched. |

**Specification authority**, verified as part of this pass and stated once so it is not re-derived:
`docs/cnb-format.md` is authoritative and the code conforms to it; `plans/plan_cnb.md` is the
engineering record of how it got there; `misc/cnb.md` is the original proposal, kept for its
rationale, and now carries a banner saying so because its header and `ChunkEntry` sketches differ
from what was built; the golden vector arrays pin the result. Where any two disagree,
`docs/cnb-format.md` wins — and the conformance suite exists so that they cannot disagree quietly.

---

## 13. Remaining `CNBF-*` work

> **Historical snapshot — superseded by §15.5 and then by §16.** Several rows here were closed or
> reclassified afterwards; do not read this table as a to-do list.


Unchanged in substance from §6's out-of-scope table, restated with what the hardening pass learned.
**Superseded by §15**, which breaks these rows into implementable tasks and fixes their order.

| ID | Work | Note |
|---|---|---|
| CNBF-100 | Shared `.cnj`/`.cnb` **mesh** builder | The *effect/material* half landed as `BuildPartEffectEXT` and was re-audited this pass: its parameters are all pre-existing CNA types, it contains zero CNB types, and both paths are covered. The mesh half remains. |
| CNBF-101 | `Texture2D`/`Texture3D`/`TextureCube` schemas, incl. multi-representation selection | Ids reserved, layouts undesigned. |
| CNBF-102 | `SpriteFont` schema | Id reserved. |
| CNBF-103 | `SoundEffect`/`Song`/`Video` schemas, incl. external streaming payloads | Ids reserved. |
| CNBF-104 | `Effect` schema | Id reserved. |
| CNBF-105 | Chunk compression | Field reserved; reader rejects any codec but 0. |
| CNBF-106 | Direct glTF/PNG/WAV → `.cnb` importers | Only `.cnj` → `.cnb` exists. |
| CNBF-107 | `.cnapak` package format | A different format and a different project. |
| CNBF-108 | Memory-mapped / zero-copy chunk access | The `alignment` field and the 16-byte geometry alignment exist for it; the golden Model vector pins that alignment. |

**Recommended next step:** see §15. That assessment stands corrected: broadening *is* now the
work, because the container is finished and the asset ecosystem is not.

---

## 14. Verification state at the end of the hardening pass

> **Historical snapshot**, dated to the hardening pass. Later sessions added schemas, producers
> and tests, so these counts are not current. Current verification is in §16 and the session
> reports.


**Branch `cnb`, working tree clean, no merge performed.** `git log --merges 1e70b234f..HEAD` is
empty. Every file touched since the branch point is under `modules/content/`, `docs/cnb-format.md`,
`misc/cnj.md`, `plans/plan_cnb.md`, `tools/{cnj_to_cnb,cnb,cnb_info}/`, two new `cmake/Tool*.cmake`
files, `cmake/UnitTests.cmake` and `CMakeLists.txt` — nothing else.

**CNB suites: 221 tests across 16 suites, all passing.** 164 before this pass, +57 added by it.

| suite | tests |
|---|---|
| `CnbContainerTest` + `CnbCrc32cTest` | 65 |
| `CnbCurveCodecTest` | 16 |
| `CnbAnimationClipCodecTest` | 13 |
| `CnbModelCodecTest` | 28 |
| `CnbContentManagerTest` | 13 |
| `CnbCompilerTest` + `CnbCompilerToolTest` | 11 |
| `CnbModelEquivalenceTest` | 5 |
| `CnbSpecConformanceTest` | 10 |
| `CnbContainerFuzzTest` | 4 |
| **`CnbHardeningTest` + `CustomTypeFixture`** *(new)* | **24** |
| **`CnbCompilerStrictnessTest`** *(new)* | **16** |
| **`CnbGoldenVectorTest`** *(new)* | **9** |
| **`CnbInfoToolTest`** *(new)* | **4** |

**Sanitizers.** All clean, on the final code:

* **ASan + UBSan** — 221 CNB tests, 0 reports. Content module: 382 tests, 0 reports.
* **TSan** — 189 device-independent CNB tests including the 8-thread registry test, 0 warnings.
  Verified to have teeth: removing the registry's locks produces real `unordered_map` data races.

**Full regression: `ctest -j3`, 8378 tests, 29 non-passing, and NONE of them is a CNB test.**
The pre-existing baseline for this branch was 8321 tests / 37 non-passing. 27 of the 29 are on that
list; the two that are not (`StockEffectContentTypeReaderTest.DualTextureEffectReaderParses…`,
`AudioEngineTest.UpdateSweepsFinished…`) are a flake that passes in isolation and a timing-dependent
audio test that was in the original pre-CNB baseline. Ten baseline failures were absent this run —
the same audio/ENet flakiness from the other direction.

**A note on parallelism, because it materially changed the numbers.** An earlier run of the same
suite at `-j8` reported 57 non-passing and one at `-j4` reported 167 — almost entirely `Not Run`
and unrelated graphics/input suites, all of which pass on re-run. The machine's
`xdg-desktop-portal` process was holding **16.6 GB RSS** (20 hours uptime, a leak in the user's
desktop session, unrelated to this work), leaving ~10 GB for a 346 MB test binary times N. The
`-j3` figure above is the one taken with headroom and is the one to compare against the baseline.
Recorded rather than quietly re-run, because a reader who runs `ctest -j8` on a loaded machine
should know why their number differs.

**Platform boundary gates: 4 of 5 pass.** `tools/platform/sdl_inventory.py --check` reports
`plans/plan_platform.md` §2 out of date — **pre-existing and not CNB's**: that file is byte-identical
to its state at this branch's fork point, no CNB file contains the string `SDL`, and the drift
includes one *fewer* SDL-referencing file, which an addition cannot cause.

**End-to-end, on a real asset.** `skin-four-weighted.gltf` → `cna_tool_gltf_to_cnj` → 4 files →
`cna_tool_cnj_to_cnb` → one 2704-byte `.cnb` absorbing all four → `cna_tool_cnb_info` reports its
ten chunks → `--quiet` validates it. `CnbModelEquivalenceTest` loads 15 corpus fixtures through both
the `.cnj` and `.cnb` paths and compares them field by field.

**CNJ and XNB have not regressed.** The whole content-module surface passes with exactly the two
failures it had before any CNB work existed, and the `.cnj` model/material suites — including
`GltfToCnjToolTest`'s offline-versus-runtime L6 material comparison and skinning-data sweep, the
tests a `BuildPartEffectEXT` regression would break — are green.

---

## 15. Gap analysis against `misc/cnb.md`, and the roadmap that follows from it

> **Historical snapshot of the gap analysis**, with its state columns updated once. The roadmap
> it produced is largely complete; **§16.2 is the authoritative current-state table.** Kept
> because the analysis is what justified the order the work was done in.


Recorded 2026-08-27, after the owner compared the delivered branch against the original proposal.
The finding is not that anything is broken — it is that **two different things were both being
called "CNB is done"**, and only one of them is:

> The CNB **container** is essentially finished. CNB as a **complete replacement for XNB across
> every asset type** is not.

Everything in this section is a *gap*, not a defect. No row here contradicts §12; they measure
different axes.

### 15.1 Where the original phases actually stand

`misc/cnb.md` proposed Phase 0–8. Measured against the tree rather than against memory:

| Area from `misc/cnb.md` | State | Where |
|---|---|---|
| CNB specification | ✅ done, authoritative | `docs/cnb-format.md` |
| chunk container, TOC, alignment | ✅ done, **frozen** | `CnbWriter`/`CnbDocument` |
| reader / writer | ✅ done | `CnbByteReader`/`CnbByteWriter` |
| CRC-32C | ✅ done | `CnbCrc32c` |
| bounds / overflow validation | ✅ extensive | `CnbArithmetic`, `CnbReadLimits` |
| fuzz / adversarial tests | ✅ done | `CnbContainerFuzzTests` |
| deterministic output | ✅ done | pinned by golden vectors |
| `Curve` | ✅ schema 1 **frozen** | `CnbCurveCodec` |
| `AnimationClip` | ✅ schema 1 **frozen** | `CnbAnimationClipCodec` |
| `.cnj` → `.cnb` compiler | ✅ done | `cna_tool_cnj_to_cnb` |
| `Model` | ✅ schema 1 **frozen** | `CnbModelCodec` |
| custom asset registry | ✅ done; C++ API still experimental | `CnbLoaderRegistry` |
| `SpriteFont` | ✅ schema 1 (§17), atlas embedded | `CnbSpriteFontCodec` |
| `Texture2D` | ✅ schema 1 (§16) | `CnbTextureCodec` |
| `Texture3D` | ✅ schema 1 (§16) | `CnbTextureCodec` |
| `TextureCube` | ✅ schema 1 (§16), golden vector | `CnbTextureCodec` |
| `SoundEffect` | ✅ schema 1 (§18) | `CnbSoundEffectCodec` |
| `Song` | ✅ schema 1 (§19), stream reference | `CnbMediaCodec` |
| `Video` | ✅ schema 1 (§19), stream reference | `CnbMediaCodec` |
| `Effect` | ❌ **id reserved, no schema** | — |
| direct glTF → `.cnb` | ✅ one command, one interpretation | `cna_tool_gltf_to_cnb` |
| chunk compression | ❌ reserved; reader rejects codec ≠ 0 | by design |
| mmap / zero-copy | ❌ future; the `alignment` field exists for it | by design |
| `.cnapak` | ❌ a different format, a different project | by design |

So **Phase 0–4 are complete and Phase 7 largely is; Phase 5, 6 and 8 are not started.**

> **Updated 2026-08-27, later the same day.** Phases 5, 6 and 8 are now done: `Texture2D`,
> `TextureCube`, `Texture3D`, `SpriteFont`, `SoundEffect`, `Song`, `Video` all have schema 1, and
> `cna_tool_gltf_to_cnb` compiles glTF in one command. **Ten of eleven** reserved built-in
> identifiers now have a schema; only `Effect` does not, and §15.5 records why it waits. The table
> above is left as it was measured, with the state column brought up to date, because the gap
> analysis is what justified the order the work was then done in.

### 15.2 The honest completeness number

Two numbers, because one number hides the answer:

- **Container / framework: ~95–100 % of what CNB 1.0 should be.** versioning, chunks, TOC,
  alignment, CRC, limits, UTF-8 validation, overflow validation, determinism, unknown-chunk
  semantics, custom types, `XREF`, `CMET`, golden vectors, fuzz, ASan/UBSan/TSan. Nothing here
  should be creatively redesigned.
- **Asset ecosystem: ~30–40 %.** Three of eleven reserved built-in types are implemented. The
  fraction understates it — `Model` is by far the hardest of the eleven — but it does not
  understate it *enough* to call the pipeline universal.

### 15.3 What glTF can and cannot do today

Today's route works and is not theoretical:

```text
model.gltf → cna_tool_gltf_to_cnj → model.cnj + sidecars
           → cna_tool_cnj_to_cnb  → model.cnb
           → ContentManager::Load<Model>()
```

`Model` schema 1 already carries vertex and index data, multiple mesh parts, topology, bone
hierarchy, parent bone attachment, skeleton, skinning, embedded animation clips, morph targets,
morph tangent deltas, morph weight tracks, `BasicEffect`/`SkinnedEffect`/`DualTextureEffect`/PBR/
skinned-PBR selection, material parameters, sampler state, external texture and `Effect`
references, and punctual lights. That is a serious 3D runtime model.

Two things it does not do:

1. **There is no direct `.gltf` → `.cnb`.** `.cnj` is always the intermediary.
2. **A model's textures are not yet `.cnb`.** They are `XREF` logical names — good architecture,
   because one texture is shared between models — but the thing on the other end of the reference
   cannot itself be a `.cnb` until `Texture2D` schema 1 exists. So a shipped content tree is not
   yet purely `.cnb`.

Deliberately out of `Model` schema 1, and correctly so: material variants and the glTF import
report. The report is authoring/debug information a runtime asset does not need. Variants belong
in a future **`Model` schema 2**, never in a container change.

### 15.4 The order to build in, and why it differs from `misc/cnb.md`

`misc/cnb.md` put `SpriteFont` early. That was written before the architecture was real. With the
architecture in hand the dependency is the other way round — a `SpriteFont` is a texture atlas plus
tables — so `Texture2D` must come first and `SpriteFont` can then reuse its payload codec.

```text
CNBF-100  shared .cnj/.cnb model builder      ← removes "CNJ model differs from CNB model" entirely
   ↓
CNBF-101A Texture2D schema 1                  ← the single highest-value missing type
   ↓
CNBF-101B TextureCube    CNBF-101C Texture3D
   ↓
CNBF-102  SpriteFont schema 1                 ← reuses the texture payload codec
   ↓
CNBF-106  direct glTF → .cnb, via ONE shared importer
   ↓
CNBF-103A SoundEffect schema 1
   ↓
CNBF-103B Song / Video: metadata + streaming reference, NOT a 300 MB blob
   ↓
CNBF-104  Effect                              ← last; needs the shader pipeline to settle first
   ↓
CNBF-105  compression                         ← only if benchmarks justify it
   ↓
CNBF-108  mmap / zero-copy                    ← only after benchmarks
```

`CNBF-107` (`.cnapak`) is deliberately **outside** this sequence. CNB is one logical asset; a pak
is many. Conflating them would damage both.

### 15.5 The tasks

| ID | Task | Design constraints that are part of the task | Status |
|---|---|---|---|
| CNBF-100 | **Shared `.cnj`/`.cnb` model builder.** The effect/material half already landed as `BuildPartEffectEXT`; the *mesh/runtime* half has not. Both front-ends must funnel through one neutral representation and one builder. | Done, and it turned out to be **three** front-ends, not two: the runtime glTF reader duplicated the same steps. `ModelPartGeometryEXT` is the neutral description each has once it holds raw vertex bytes, raw index bytes and a topology; `BuildModelMeshPartGeometryEXT()` turns it into the `VertexBuffer`/`IndexBuffer`/`ModelMeshPart`, and `ApplyPartSamplerStatesEXT()` owns the slot-0-4/5+ specular split. `setPrimitiveTypeEXTProperty` now appears **once** in `ContentManager.cpp`, down from three; so does the specular slot boundary. One genuine asymmetry is kept and named: a `.cnb` *supplies* its primitive count, because the file states it and the decoder has already cross-checked it against the topology (`CNBF-H012`), so re-deriving it would throw that check away. `SkinnedModelEXT` is deliberately exempt — different asset type, 16-bit-only indices, its own index-range validation, no topology or sampler concept. **Verified:** 1137 tests across the CNB, CNJ, Model and glTF conformance suites, with exactly the four pre-existing failures and no new ones; clean under ASan+UBSan. | ✅ |
| CNBF-100-guard | **Stop the duplication regrowing.** | `CnbModelEquivalenceTest` compares *outputs*, so three copies that currently agree pass it — which is precisely how this drift risk survived until now. `CnbSharedModelBuilderTests` therefore counts the construction steps in the source: each must appear once, and all three front-ends must call the shared builder. Teeth checked by reintroducing a duplicate topology assignment on purpose and watching the guard fail. | ✅ |
| CNBF-101A | **`Texture2D` schema 1.** | Landed as `docs/cnb-format.md` §16, shared with cube and volume: `TEXH` + `TEXR` + one `TEXD` per level. `Rgba8` is the only format schema 1 *writes*, as planned; the reader accepts every identifier and the runtime refuses, by name, one it cannot upload. Representation **selection** is implemented from the start, so a file offering `Bc7` first and `Rgba8` second already loads here. Payloads are 16-byte aligned for `CNBF-108`. | ✅ |
| CNBF-101A-fmt | **`CnbTextureFormat`, a serialization enum of its own.** | Done. 27 explicitly-numbered identifiers covering every `SurfaceFormat` CNA defines, so the numbering never needs extending for a format that already exists. One table drives both directions — two switch statements would eventually disagree — and a `static_assert` pins each row to its own identifier slot, since a misplaced row would make every lookup wrong at once. A conformance test walks §16.4's table row by row against the code. | ✅ |
| CNBF-101B | **`TextureCube` schema 1.** | Done. Faces in `CubeMapFace` order, levels face-major then mip. This is the shape the **golden vector** pins, deliberately rather than a flat 2D texture: it is the only one of the three whose payload *order* and descriptor tiling are load-bearing. The independently-written Python generator and the C++ writer agreed on all 720 bytes. | ✅ |
| CNBF-101C | **`Texture3D` schema 1.** | Done. Depth counts towards a level's byte size and halves per mip like the other two dimensions. Boxed as `std::shared_ptr<Texture3D>` by the loader because `Texture3D` is non-copyable — the same shape the `.xnb` reader registers, and a mismatch here is a `bad_any_cast` rather than a diagnosable error. | ✅ |
| CNBF-102 | **`SpriteFont` schema 1.** | Landed as §17, exactly as scoped: `FONT`/`GLYP`/`CROP`/`KERN`/`CHAR` plus an **embedded** atlas that reuses §16's `TEXH`/`TEXR`/`TEXD` — the texture codec's chunk layer was factored out for it rather than copied, so the atlas gets byte-identical strides, alignment and validation. The rule worth having is the character map's: `SpriteFont` binary-searches it, so an unsorted map does not fail loudly, it returns the wrong glyph. Ordering is therefore a format requirement checked on write **and** on read, since no length or checksum test can catch a reordering. Same for the `hasDefaultCharacter` flag: "no default" and "U+0000" are different fonts. | ✅ |
| CNBF-103A | **`SoundEffect` schema 1.** | Landed as §18, exactly as scoped, with `CnbAudioFormat` following the texture precedent. Samples are **headerless** little-endian PCM, which is what the runtime's raw-buffer constructor takes. The loop-bounds check is the rule that earns its keep: an over-long loop is structurally valid — every length and checksum correct — and without it becomes an out-of-range read inside the mixer at playback time. **Found and fixed a real gap while wiring it**: `Load<SoundEffect>` has its own specialisation that never consulted the `.cnb` tier, so the documented precedence held for every asset type except this one. Boxed as `shared_ptr` because `SoundEffect` is move-only and `std::any` cannot hold it at all. | ✅ |
| CNBF-103B | **`Song` / `Video`: metadata plus a streaming reference.** | Landed as §19, built exactly on that split. The reference lives in `XREF` rather than in the schema chunk, so `cnb_info --refs` shows a build script that the `.ogg` has to ship without it knowing anything about the `Song` schema. Exactly one entry is required. **Found a real asymmetry while testing it**: the reader validates every `XREF` name against traversal, but the writer did not — so an encoder could produce a file its own decoder refuses. Fixed in the media codec rather than in `CnbWriter`, leaving the frozen container untouched. The frame-rate rule is enforced on both sides because NaN is a well-formed `f32` the container stores verbatim by §2.1. | ✅ |
| CNBF-104 | **`Effect` schema.** | Deliberately late. CNA has many renderers, so a `.cnb` carrying only D3D bytecode is useless on Vulkan/GL/Metal/WebGPU. The eventual shape is metadata + parameters + *per-profile* shader payloads (GLSL / SPIR-V / DXIL / MSL …) or a CNA IR. **Do not start until the FX/shader pipeline and renderer abstraction have settled.** | ⬜ |
| CNBF-105 | **Chunk compression.** | Measured first, as the task required, and the measurement changed the answer. Full numbers and recipe in `docs/cnb-compression-measurements.md`. **The premise was wrong**: "PNG/OGG are already compressed" is true of the *source* files and false of what CNB stores — a PNG becomes raw `Rgba8` at compile time and an Ogg becomes raw `Pcm16`, so CNB is exactly where the data is uncompressed. zstd-3 on real content: **51 %** off a photographic texture, **27 %** off audio, **15 %** off vertex data. But decompression only *saves load time* below 456–1469 MB/s, and this machine reads at 2.5 GB/s — so on desktop NVMe compression makes loading **slower**. Size always wins; time only sometimes does. Hence: Zstandard implemented as codec 2, **off by default**, chosen **per chunk** — which retroactively justifies the per-chunk `compression` field. A chunk is compressed only if it actually shrank; `CMET`/`XREF` never are, so an inspector can read a file's identity without the codec. Every golden vector is still byte-identical, so the default is provably inert. | ✅ |
| CNBF-105-fixture | **A methodology finding, kept because it nearly produced a wrong answer.** | The first run used the repo's own image fixture as "the photograph" and reported **0.6 %** — a 160× saving. That fixture is a synthetic placeholder (405 unique colours across 120 000 pixels; two others are a *single colour*), so compression looked like the most valuable feature in the project when it is actually 51 %. The tell was visible before the content was checked: zstd-3 produced a *larger* output than zstd-1 and zstd-9 larger still, and non-monotonic ratios across levels mean degenerate input, not a strange codec. Recorded in the measurements document so future performance work here checks what a fixture contains before quoting a number from it. | ✅ |
| CNBF-106A | **Direct glTF → `.cnb`.** (Renamed from `CNBF-106`, whose title said *glTF/PNG/WAV* and which was marked done when only the glTF third existed. The PNG and WAV thirds are `CNBF-109` and `CNBF-110`; the commit history is left alone and the engineering record corrected here.) | Landed as `cna_tool_gltf_to_cnb`. The requirement was negative — no second interpretation of glTF — and it is met the strongest way available: the tool is **built from `gltf_to_cnj.cpp` itself**, with its `main()` suppressed, so the two front-ends share a translation unit rather than a library. There is one implementation, so they cannot disagree. Proven, not asserted: a test compiles the same fixture both ways and requires the bytes to be **identical** (2704 B on `skin-four-weighted.gltf`). The staging `.cnj` is written to a temporary directory the tool removes, so a project keeps no `.cnj` it does not want; `--keep-cnj` makes it visible on request. | ✅ |
| CNBF-106B | **Collapse the `.cnj` staging step into an in-memory canonical form.** | Deliberately **not** done with `CNBF-106`, and the reason is worth recording rather than rediscovering. The conversion writes its sidecars from a dozen call sites inside one working function, so hoisting them into a returned value is a change to the **`.cnj` path** — which is the reference `CnbModelEquivalenceTest` compares the `.cnb` path against. Doing it under the same task would have meant changing the oracle and the thing under test together. The user-facing behaviour does not depend on it: the byte-equality test above is what "one interpretation" actually means operationally. | ⬜ |
| CNBF-107 | **`.cnapak`.** Many logical assets in one file. Outside this sequence; a separate project. | ⬜ |
| CNBF-108 | **mmap / zero-copy chunk access.** | Benchmarked, and **closed as the wrong optimisation** — not deferred. Full numbers in `docs/cnb-mmap-measurements.md`. On a 32 MiB `.cnb`, reading the file costs 7.0 ms and mmap+touch 2.3 ms, so mmap could save **4.7 ms** — while verifying the file's CRC-32C cost **62.5 ms**, nine times the I/O it protects, because the checksum was a byte-at-a-time table at ~512 MB/s. Folding the same CRC in hardware saves **59.2 ms**, i.e. **12.6× the mmap win**, for a change to one function instead of the container's whole ownership model. The `mmap`-alone row is why this needed measuring: mapping 32 MiB takes 5 µs and looks like the whole load vanished, but nothing has been read — and CNB touches every byte immediately, because it verifies them. §4 of the measurements records what would change the answer. | ✅ closed, not implemented |
| CNBF-108-crc | **Hardware CRC-32C, which is what the `CNBF-108` benchmark actually found.** | SSE4.2 on x86, the ARMv8 CRC32 extension on aarch64, detected **at runtime** so one binary still runs without the instruction — a build flag would trade a crash for a speed-up nobody asked for. Polynomial, seed and every produced value unchanged: all four golden vectors are still byte-identical, and a test proves the hardware path agrees with the table at every length modulo 8 and at every resume point, because a wide fold that is right in one pass can still be wrong when resumed off-stride. End to end through `cna_tool_cnb_info` on the same file: **127.3 ms → 19.8 ms, 6.4× faster.** | ✅ |
| CNBF-108-bench | **A benchmark bug worth remembering.** | The first run reported CRC-32C at **0.000 ms for 32 MiB** — the compiler had removed a call whose result was discarded. Believing it would have produced the exact opposite conclusion: *verification is free, so the read is everything, so implement mmap*. An implausibly good benchmark number is a bug report about the benchmark. | ✅ |

### 15.6 The one-line answers

- **3D glTF models:** yes, today, via `glTF → CNJ → CNB → Model`, with skeleton, animation, morphs, PBR and lights.
- **Direct glTF → CNB:** yes — `cna_tool_gltf_to_cnb`, byte-identical to the two-step route.
- **`SpriteFont`:** yes — §17, with its atlas embedded in the same file.
- **`Texture2D`/`3D`/`Cube`:** yes — §16, one shared layout, `Rgba8` in schema 1.
- **`Curve`, `AnimationClip`, `Model`:** yes. `Model` is the most capable CNB asset by a distance.
- **`SoundEffect`, `Song`, `Video`:** yes — §18 and §19. A sound effect owns its samples; a song or video carries metadata and a stream reference.
- **`Effect`:** not as a standalone `.cnb`, though `Model` schema 1 can select a stock effect and reference an external one.
- **Custom game assets:** infrastructure yes (custom id + canonical name + registry); the C++ extension API is still deliberately unstable.

**Summary, as measured.** CNB was a mature binary container with a strong `Model` implementation
but not yet a universal content pipeline, and the next real jump was named as `Texture2D`,
`SpriteFont` and direct glTF→CNB.

**Summary, after doing them.** All three landed, along with `TextureCube`, `Texture3D`,
`SoundEffect`, `Song` and `Video`. Ten of eleven reserved built-in asset types now have a schema.
What remains is `Effect` — which waits on the shader pipeline by design, not by neglect — plus
`CNBF-107` (`.cnapak`, a different project) and one internal refactor, `CNBF-106B`. `CNBF-105` and
`CNBF-108` were both settled by measurement: compression implemented as opt-in, mmap closed as the
wrong optimisation.

---

## 16. Producer/tooling completeness (`CNBF-109`–`CNBF-113`)

Recorded 2026-08-27. An audit of the tree found the asymmetry this section closes:

> CNB's runtime/schema side had grown much broader than its producer side.

Ten of eleven built-in identifiers had a wire schema, a runtime loader and a writer API — and
`cna_tool_cnj_to_cnb` still compiled exactly three of them. "10/11 schemas" was true and misleading
at the same time, because a schema nobody can produce an asset for is not a usable format.

**The architectural rule this work follows, and the reason it is worth stating:** a content
compiler must be *headless*. No `GraphicsDevice`, no audio device, no window, no renderer — it runs
on a build machine, in a container, in CI. The tempting shortcut is to reuse the runtime loaders:
construct a `Texture2D`, read the pixels back off the GPU, encode those. That would make the
compiler depend on a working display and driver, and it is exactly what was avoided. The path is
always:

```text
source bytes  ->  canonical CPU representation  ->  Cnb*Data  ->  Encode*ToCnb()
```

| ID | Task | Notes | Status |
|---|---|---|---|
| CNBF-109 | **Direct image → `Texture2D` `.cnb`.** | Through `CNA::Internal::Graphics::ImageLoader`, which was already a pure-data RGBA8 decoder with no GPU dependency — so there is **no second image decoder**, and a compiled `.cnb` holds the pixels the runtime would have loaded from the same file. Colour keying is applied **only when explicitly requested**: the `.cnj` route has a document to ask, a direct compile does not, and silently rewriting someone's art is a worse default than making them say so. A keyed pixel keeps its RGB and loses only its alpha, matching the runtime rule — zeroing the colour too would change what a bilinear filter blends toward at a keyed edge. | ✅ |
| CNBF-110 | **Direct WAV → `SoundEffect` `.cnb`.** | A **pure-data** RIFF/WAVE parser had to be written, and the reason is recorded because "don't duplicate a decoder" was the standing rule: CNA's runtime WAV path decodes through the mixer engine (`GetMixer()`), which needs an audio device a compiler cannot have. The parser is deliberately narrow rather than a second general decoder — it accepts the PCM formats that convert to `Pcm16` **exactly** (16-bit as-is, 8-bit unsigned widened by `(s-128)<<8`) and refuses 24-bit, 32-bit, IEEE float and ADPCM **by name**, because every one of those is a lossy conversion and that is an authoring decision rather than a compiler's. A `smpl` chunk's first loop entry becomes the loop region, using the rules the runtime already applies. | ✅ |
| CNBF-111 | **Extend `cna_tool_cnj_to_cnb` to every type whose `.cnj` has a coherent representation.** | `Texture2D`, `Texture3D`, `SpriteFont` and `SoundEffect` added; supported types go from **3 to 7**. Sidecars resolve through `ResolveCnjSourceFileSafely`, the same containment helper the runtime readers use — a compiler that resolved paths more permissively would be the soft way into a file the runtime refuses. **`SpriteFont` compiles to one self-contained `.cnb`**: its atlas is decoded through `CNBF-109`'s image path and embedded, and a test loads the result from a directory holding nothing else. | ✅ |
| CNBF-112 | **One CLI front end for direct source compilation: `cna_tool_source_to_cnb`.** | One executable rather than seven near-identical ones; the input's extension already says what it is. Images → `Texture2D`, WAV → `SoundEffect`, and `--as song`/`--as video` for the metadata-plus-reference schemas. Song/Video metadata is **arguments, not guesses**: duration, frame size and frame rate would need a multimedia decoder CNA does not expose headlessly, so the tool requires them rather than inventing them. | ✅ |
| CNBF-113 | **`TextureCube` producer.** | **Deliberately open.** It is the one implemented schema with no producer. Its source format is DDS and CNA's only DDS decoder lives inside `TextureCube::DDSFromStreamEXT`, which takes a `GraphicsDevice`. Writing a second DDS parser to get around that would be a fragile duplicate of a fiddly format, and the compiler refuses `TextureCube` with a message that says *why* rather than reading like an unknown type. Correct partial completion beats a second DDS implementation. | ⬜ |

### 16.1 What changed, in one table

| | before this session | after |
|---|---|---|
| `cnj_to_cnb` types | Curve, AnimationClip, Model | + Texture2D, Texture3D, SpriteFont, SoundEffect (**3 → 7**) |
| direct source tools | `gltf_to_cnb` | + `source_to_cnb` (image, WAV, Song, Video) |
| schemas with **no** producer | Texture2D, Texture3D, TextureCube, SpriteFont, SoundEffect, Song, Video (**7**) | TextureCube (**1**) |
| PNG → `.cnb` | no | **yes** |
| WAV → `.cnb` | no | **yes** |
| SpriteFont `.cnj` → one file | no | **yes**, atlas embedded |

### 16.2 The authoritative current-state matrix

Superseding every earlier table in this document. "Supported" is four different claims and this
table keeps them apart, because conflating them is what produced the gap in the first place.

| asset type | wire schema | runtime loader | writer API | `.cnj` → `.cnb` | direct source → `.cnb` | golden vector | negative tests |
|---|---|---|---|---|---|---|---|
| `Texture2D` | ✅ §16 | ✅ | ✅ | ✅ | ✅ image | via `TextureCube` | ✅ |
| `Texture3D` | ✅ §16 | ✅ | ✅ | ✅ | — (raw sidecar is a `.cnj` concept) | via `TextureCube` | ✅ |
| `TextureCube` | ✅ §16 | ✅ | ✅ | ⬜ `CNBF-113` | ⬜ `CNBF-113` | ✅ | ✅ |
| `SpriteFont` | ✅ §17 | ✅ | ✅ | ✅ atlas embedded | — (a font is authored, not a single source file) | — | ✅ |
| `Model` | ✅ §11 | ✅ | ✅ | ✅ | ✅ glTF | ✅ | ✅ |
| `AnimationClip` | ✅ §10 | ✅ | ✅ | ✅ | ✅ glTF | ✅ | ✅ |
| `Curve` | ✅ §9 | ✅ | ✅ | ✅ | — | ✅ | ✅ |
| `SoundEffect` | ✅ §18 | ✅ | ✅ | ✅ | ✅ WAV | — | ✅ |
| `Song` | ✅ §19 | ✅ | ✅ | — (no `Song` `.cnj`) | ✅ wraps a media file | — | ✅ |
| `Video` | ✅ §19 | ✅ | ✅ | — (no `Video` `.cnj`) | ✅ explicit metadata | — | ✅ |
| `Effect` | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | — | — |

### 16.3 Four separate completeness numbers

Stated separately on purpose. A single percentage is what let "10/11 schemas" read as "usable
content pipeline" when seven of those ten had no way to produce an asset.

| axis | state |
|---|---|
| **Container** | Frozen and finished. Nothing in this session touched a serialized byte. |
| **Wire schemas** | **10 of 11.** `Effect` waits on the FX/shader architecture, by design. |
| **Runtime loaders** | **10 of 10** implemented schemas load through `ContentManager`. |
| **Producer ecosystem** | **9 of 10.** Only `TextureCube` has no supported way to produce one. |

An honest sentence: a game can now compile textures, fonts, sounds, curves, animation clips and
models — from `.cnj` documents, from glTF, from images and from WAV files — with a headless
deterministic toolchain. It cannot yet compile a cube map.
