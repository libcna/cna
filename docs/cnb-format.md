# CNB v1 — the CNA compiled binary content format

**Status: implemented.** This document describes the bytes the implementation in
`modules/content/{include/CNA/Content/Cnb,src/Cnb}` actually reads and writes, not an aspiration.
`modules/content/tests/CNA/Content/Cnb/CnbSpecConformanceTests.cpp` fails if the two drift apart:
every constant in §3, §4 and §7 below is asserted against the code, and §14's hex dump is
regenerated and byte-compared.

* The original architectural proposal and its rationale are `misc/cnb.md`.
* The engineering record — audit, decisions, rejected alternatives, task status — is
  `plans/plan_cnb.md`.
* This file is the authoritative description of the format.

---

## 1. Goals and non-goals

CNB is CNA's own compiled runtime content format. It exists because CNA's editable content format,
`.cnj`, represents one logical asset as many files: a modest skinned model is a `.cnj` document
plus a vertex, index and morph sidecar per primitive, a skeleton sidecar, one file per animation
clip, and one file per texture. CNB compiles that into a single asset file with the shared,
reusable assets left external.

**Goals.**

* One logical asset is one file, with genuinely shared assets (textures, effects) still shared.
* Loading is a bounded parse of typed binary structures, not a JSON parse plus a directory of
  follow-up opens.
* Malformed input is rejected with a clear error, never with a crash or a silent misread.
* Compilation is deterministic: identical inputs produce byte-identical output.
* The container's version and each asset schema's version evolve independently.
* Nothing in the format depends on the C++ ABI, on `sizeof` a runtime type, on struct padding, on
  host endianness, or on `std::type_index`.

**Non-goals.**

* CNB is not a package or archive format. One `.cnb` is one asset. Bundling many assets into one
  file is a different format and a different project.
* CNB is not an interchange format. glTF, PNG and WAV are how content arrives; `.cnj` is how it is
  edited; `.cnb` is how it ships.
* CNB is not `.xnb` and shares no code with it. It has no assembly-qualified type names, no
  reflection, no reader-version negotiation, no shared-resource fixup protocol and no platform
  identifier byte.
* The per-chunk checksums detect **accidental** corruption. CNB has no authenticity story and must
  never be presented as having one: anyone who can rewrite a chunk can rewrite its checksum.

---

## 2. Primitives and byte order

Every multi-byte value is **little-endian**. Values are assembled from and decomposed into
individual bytes by `CnbByteReader`/`CnbByteWriter`, so the encoding does not depend on the host's
byte order; floats go through `std::bit_cast` from an explicitly little-endian integer, so they do
not depend on the host's floating-point storage order either.

| name | size | encoding |
|---|---|---|
| `u8` | 1 | unsigned |
| `u16` | 2 | unsigned, little-endian |
| `u32` | 4 | unsigned, little-endian |
| `u64` | 8 | unsigned, little-endian |
| `i32` | 4 | two's complement, little-endian |
| `f32` | 4 | IEEE-754 binary32, little-endian |
| `f64` | 8 | IEEE-754 binary64, little-endian |
| `String` | 4 + n | `u32` byte length, then that many UTF-8 bytes, no terminator |

A `String`'s bytes are validated as well-formed UTF-8 on read **and** on write: no overlong
encodings, no surrogate code points (`U+D800`–`U+DFFF`), nothing above `U+10FFFF`, no truncated
sequence. A `.cnb` string can become a filesystem path or an effect name, so letting malformed
UTF-8 through would push the problem into code far less prepared for it. `U+0000` inside a string
is legal.

Byte order is fixed rather than negotiable because every target CNA supports — x86-64, aarch64,
wasm32 — is little-endian, and a byte-order flag no build could exercise would be untestable dead
weight.

---

## 3. File header

Exactly 64 bytes at offset 0. Every field is naturally aligned within the header.

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `magic` | `43 4E 42 1A` — `"CNB"` then `0x1A` |
| 4 | 2 | `containerMajor` (`u16`) | 1 |
| 6 | 2 | `containerMinor` (`u16`) | 0 |
| 8 | 4 | `headerFlags` (`u32`) | reserved; must be 0 |
| 12 | 4 | `assetTypeId` (`u32`) | see §7 |
| 16 | 4 | `assetSchemaVersion` (`u32`) | ≥ 1 |
| 20 | 4 | `chunkCount` (`u32`) | number of table-of-contents entries |
| 24 | 8 | `fileSize` (`u64`) | must equal the file's real byte count |
| 32 | 8 | `tocOffset` (`u64`) | ≥ 64; the writer always emits 64 |
| 40 | 4 | `tocChecksum` (`u32`) | CRC-32C of the table-of-contents bytes |
| 44 | 4 | `headerChecksum` (`u32`) | CRC-32C of bytes `[0, 44)` |
| 48 | 16 | `reserved` | must be all zero |

The trailing `0x1A` of the magic is a DOS end-of-file byte, which stops `type`/`cat` from spilling
binary into a terminal — the same trick PNG uses.

`headerChecksum` covers everything before it, including `tocChecksum`, and is verified **before any
offset in the header is used for arithmetic**. A corrupt header is precisely the case in which the
offsets cannot be believed.

`fileSize` must equal the real file length. This one check closes every "the file declares more
bytes than it has" attack at the door.

---

## 4. Table of contents

`chunkCount` entries of exactly 48 bytes each, starting at `tocOffset`. Every field is naturally
aligned within an entry.

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `type` (`u32`) | four printable-ASCII bytes; see §5 |
| 4 | 4 | `flags` (`u32`) | bit 0 = `Mandatory`; every other bit must be 0 |
| 8 | 8 | `offset` (`u64`) | absolute file offset of the chunk's stored bytes |
| 16 | 8 | `storedSize` (`u64`) | bytes the chunk occupies in the file |
| 24 | 8 | `uncompressedSize` (`u64`) | equal to `storedSize` in CNB v1 |
| 32 | 4 | `checksum` (`u32`) | CRC-32C of the chunk's stored bytes |
| 36 | 4 | `compression` (`u32`) | 0 = none; see §8 |
| 40 | 4 | `alignment` (`u32`) | power of two, 1…4096; `offset % alignment == 0` |
| 44 | 4 | `reserved` (`u32`) | must be 0 |

Entries **must** be sorted by ascending `offset`. That makes overlap detection one linear pass and
makes writing deterministic by construction, and it means a reader never has to sort
attacker-controlled data before any bound is known.

### 4.1 Layout invariant

The header (`[0, 64)`), the table of contents (`[tocOffset, tocOffset + 48·chunkCount)`) and every
**non-empty** chunk form a set of pairwise-disjoint ranges. Read in ascending order, the gaps
between them must be entirely zero bytes, and the last range must end at `fileSize` — except that
zero bytes after it are tolerated as end-of-file padding, so a future writer may pad a file out to
a page boundary for memory mapping.

That single invariant subsumes "no overlapping chunks", "no chunk past end of file", "no trailing
junk" and "no data hidden in alignment padding".

A **zero-length chunk is legal** (an empty index buffer, a clip with no tracks). It occupies no
bytes, so it takes no part in that partition; its `offset` is still range- and alignment-checked.
Two zero-length chunks may therefore share an offset.

---

## 5. Chunk identifiers

A chunk identifier is four bytes stored little-endian, so `MakeChunkId('M','D','L','H')` appears in
a hex dump as `4D 44 4C 48` and reads left to right as `MDLH`. Every byte must be printable ASCII
(`0x20`–`0x7E`).

* An identifier whose **first byte is an uppercase ASCII letter is reserved for CNA**.
* A game defining its own `.cnb` schema uses an identifier whose first byte is a **lowercase**
  letter.

**A chunk type may appear more than once.** One chunk refers to another by its **ordinal within its
own type** — "the third `MVTX` chunk" — never by a table-of-contents index. A raw index would shift
whenever a container-level chunk was added or removed ahead of the payload chunks; an ordinal is
stable under any such insertion. A schema declares which of *its* chunk types are singletons.

### 5.1 Container-defined chunks

These two are understood by every reader, whatever the asset type, and are always emitted before a
schema's own chunks.

**`CMET` — optional, at most one. Debug metadata.** Diagnostic only; nothing dispatches on it.

```text
u32     flags            reserved, must be 0
String  assetTypeName    e.g. "Microsoft.Xna.Framework.Curve"
String  contentName      the logical asset name at compile time, or empty
```

**`XREF` — optional, at most one, marked `Mandatory`. External asset references.**

```text
u32 count
count × {
    u32     flags                reserved, must be 0
    u32     expectedAssetTypeId  the type the referring schema expects, or 0
    String  logicalName          a ContentManager logical asset name
}
```

`logicalName` is validated on read: non-empty, well-formed UTF-8, no backslash, not starting with
`/`, not drive-qualified (`X:`), and containing no `..` segment. `ContentManager`'s own containment
checks still run afterwards; this is defence in depth, so a compiled file can never hand
path-traversal input to the resolver in the first place.

`XREF` is marked `Mandatory` on purpose: a reader that could not see the names an asset depends on
would load a visibly incomplete asset and say nothing.

---

## 6. Versioning and compatibility

Container version and asset schema version are **independent**. A change to the `Model` schema does
not bump the container version, and a container change does not invalidate a schema.

| situation | behaviour |
|---|---|
| `containerMajor` ≠ 1 | reject |
| `containerMinor` > 0 | **accept** — minor bumps are additive-only by definition |
| any bit set in `headerFlags`, or a non-zero reserved byte | reject |
| unknown chunk type **with** `Mandatory` | reject, naming the type |
| unknown chunk type **without** `Mandatory` | skip silently |
| `compression` ≠ 0 | reject, naming the codec |
| `assetSchemaVersion` outside `[1, max for that type]` | reject |
| duplicate of a schema-declared singleton chunk | reject |

Accepting a higher minor version while rejecting an unknown *mandatory chunk* is what makes
additive evolution possible: a newer writer that adds data an older reader can safely ignore marks
it optional, and a newer writer that adds data an older reader must not ignore marks it mandatory
and is refused by name.

---

## 7. Asset type identifiers

The header's `assetTypeId` is what selects a loader. There is no type name in the dispatch path.

```text
0x00000000              invalid; a file declaring it is rejected
0x00000001-0x3FFFFFFF   CNA built-in asset types, frozen once v1 ships
0x40000000-0x7FFFFFFF   reserved for future CNA use
0x80000000-0xFFFFFFFF   game-defined custom types
```

| id | type | v1 schema |
|---|---|---|
| 1 | `Texture2D` | reserved, not implemented |
| 2 | `Texture3D` | reserved, not implemented |
| 3 | `TextureCube` | reserved, not implemented |
| 4 | `SpriteFont` | reserved, not implemented |
| 5 | `Model` | **version 1**, §11 |
| 6 | `AnimationClip` | **version 1**, §10 |
| 7 | `Curve` | **version 1**, §9 |
| 8 | `SoundEffect` | reserved, not implemented |
| 9 | `Song` | reserved, not implemented |
| 10 | `Video` | reserved, not implemented |
| 11 | `Effect` | reserved, not implemented |

A custom identifier is `CnbAssetTypeIdFromName(utf8)` = `FNV-1a-32(name) | 0x80000000`, i.e. 31
usable bits. **Collisions are possible** and are handled rather than wished away:
`CnbLoaderRegistry::Register` refuses to register one identifier under two different type names, so
two colliding game types fail loudly at startup instead of loading each other's files; and the
optional `CMET` chunk carries the type name so a mismatch can be reported.

Runtime type identity (`std::type_index`) is deliberately not used: its value is not stable across
processes, let alone builds, so it is not a serialisation ABI.

---

## 8. Compression

`compression` is a per-chunk `u32` codec identifier.

| value | codec | status |
|---|---|---|
| 0 | none | the only codec CNB v1 defines |
| 1 | LZ4 | reserved, rejected |
| 2 | Zstandard | reserved, rejected |
| 3 | Deflate | reserved, rejected |

CNB v1 stores every chunk uncompressed and requires `uncompressedSize == storedSize`. The field
exists so a future codec is an additive change to one entry field rather than a container version
bump. No compression library was added: most of what a game ships is already compressed (PNG, Ogg),
and a compressed vertex buffer would trade the direct, aligned access §4 exists to preserve for a
saving that has not been measured to matter.

---

## 9. `Curve`, schema version 1

Asset type 7. Chunk types: `CRVH`, `CRVK` — both mandatory, both exactly once.

**`CRVH` — 12 bytes.**

```text
u32 preLoop     Microsoft::Xna::Framework::CurveLoopType, 0-4
u32 postLoop    Microsoft::Xna::Framework::CurveLoopType, 0-4
u32 keyCount
```

**`CRVK` — `keyCount` × 20 bytes, and its length must equal that product exactly.**

```text
f32 position
f32 value
f32 tangentIn
f32 tangentOut
u32 continuity  Microsoft::Xna::Framework::CurveContinuity, 0-1
```

The keys are one flat, fixed-stride array: the whole key set is addressable without walking
anything, which is the property that makes this a compiled representation rather than a re-encoded
document.

---

## 10. `AnimationClip`, schema version 1

Asset type 6. Chunk types: `ACLH`, `ACLT`, `ACLK` — all mandatory, all exactly once.

**`ACLH` — 20 bytes.**

```text
f64 durationSeconds
u32 targetSpace     Graphics::ClipTargetSpaceEXT, 0 = JointPalette, 1 = SceneNode
u32 trackCount
u32 totalKeyCount
```

**`ACLT` — `trackCount` × 12 bytes, exact.**

```text
i32 boneIndex
u32 firstKey        index into the ACLK array
u32 keyCount        firstKey + keyCount must not exceed totalKeyCount
```

**`ACLK` — `totalKeyCount` × 48 bytes, exact. This is CNB's one keyframe encoding, shared with
`MANM` in §11.**

```text
f64 timeSeconds
f32 translation[3]
f32 rotation[4]     x, y, z, w
f32 scale[3]
```

Every keyframe of every track lives in one contiguous run; a track names a range of it. Two tracks
may name the same range — an unusual but valid encoding — so a reader must not assume the ranges
partition the array.

`durationSeconds` and every `timeSeconds` are range-checked before reaching `System::TimeSpan`:
non-finite values, and values outside what a `TimeSpan` can hold, are rejected as a
`ContentLoadException` naming the file rather than escaping as the `System::ArgumentException` or
`System::OverflowException` `TimeSpan::FromSeconds` would otherwise raise from deep inside the
decoder.

---

## 11. `Model`, schema version 1

Asset type 5. This is the schema CNB exists for: it absorbs the vertex, index, morph, skeleton and
animation sidecars a `.cnj` Model spreads across the filesystem, while leaving textures and
game-supplied effects as external references so `ContentManager`'s cache keeps working.

| chunk | required | count | contents |
|---|---|---|---|
| `MDLH` | mandatory | 1 | flags and the counts everything else is checked against |
| `MSTR` | mandatory | 1 | the deduplicated string table |
| `MBON` | mandatory when `boneCount` > 0 | ≤ 1 | the scene-graph bone table |
| `MMSH` | mandatory | 1 | mesh rows, part rows and the mesh→part slot array |
| `MMAT` | mandatory | 1 | the material table |
| `MVTX` | mandatory | = `partCount` | raw vertex bytes, one chunk per part, 16-byte aligned |
| `MIDX` | mandatory | = `partCount` | raw index bytes, one chunk per part, 16-byte aligned |
| `MMRP` | mandatory | ≤ `partCount` | morph data, one chunk per morphed part, 8-byte aligned |
| `MSKL` | mandatory | ≤ 1 | the skinning skeleton |
| `MANM` | mandatory | ≤ 1 | embedded animation clips, 8-byte aligned |
| `MLIT` | mandatory | ≤ 1 | punctual lights |

Geometry gets 16-byte alignment because it is the data a future memory-mapped reader would address
in place, and 16 is the widest vector load any CNA target uses.

**`MDLH` — 24 bytes.**

```text
u32 flags       bit 0 = the model carries a real scene-node hierarchy
                bit 1 = its materials were authored under glTF's lighting conventions
u32 boneCount
u32 partCount
u32 meshCount
u32 lightCount
u32 animationCount
```

Flag bit 1 records a property of the content, not a source-format version: a model imported from
glTF expects the importer's lighting rig applied to every effect, including its "no light was
declared, so light it by default" fallback, whereas a hand-authored model expects XNA's own
defaults where `BasicEffect` starts unlit. The two look visibly different, so which applies has to
travel with the asset rather than be guessed from whether any lights are present.

**`MSTR`.** `u32 count`, then `count` `String`s. Every name in the schema is a `u32` index into
this table; `0xFFFFFFFF` means "no string".

**`MBON` — `boneCount` × 72 bytes, exact.**

```text
u32 nameIndex
i32 parent          -1 for the root; otherwise < boneCount
f32 transform[16]   XNA row-major field order M11..M44
```

**`MMSH`** — three sections, in order:

```text
meshCount × {                     16 bytes each
    u32 nameIndex
    i32 parentBone                -1, or an index into MBON
    u32 firstSlot                 index into the slot array below
    u32 slotCount
}
partCount × {                     56 bytes each
    u32 nameIndex
    u32 vertexChunkOrdinal        the n-th MVTX chunk
    u32 indexChunkOrdinal         the n-th MIDX chunk
    u32 morphChunkOrdinal         the n-th MMRP chunk, or 0xFFFFFFFF for none
    u32 vertexStride              1..4096
    u32 vertexCount               vertexStride * vertexCount == the MVTX chunk's length
    u32 indexCount                indexElementSize * indexCount == the MIDX chunk's length
    u32 indexElementSize          2 or 4
    u32 primitiveTopology         glTF primitive mode, 0..6
    u32 primitiveCount
    u32 effectKind                0 Basic, 1 Skinned, 2 DualTexture, 3 Pbr, 4 SkinnedPbr, 5 External
    u32 externalEffectRef         XREF index when effectKind == 5, else 0xFFFFFFFF
    u32 materialIndex             index into MMAT
    u32 flags                     bit 0 vertexColorEnabled, bit 1 unlit
}
u32 slotCount
slotCount × u32 partIndex
```

`indexElementSize` is **declared, not inferred**. The `.cnj` pipeline derives the index width from
the vertex count (32-bit above 65535, matching XNA's stock model processor) and derives the counts
themselves by truncating division, which means a truncated sidecar silently decodes as a shorter
mesh. Storing both removes that guess.

**`MMAT`.** `u32 count`, then `count` × 368 bytes, exact. Materials are interned by their encoded
bytes, so parts sharing a material share one record.

```text
u32 textureRef[8]           XREF indices, or 0xFFFFFFFF: baseColor, texture2, normal,
                            metallicRoughness, emissive, occlusion, specular, specularColor
f32 baseColorFactor[4]
f32 emissiveFactor[3]
f32 specularColorFactor[3]
f32 metallicFactor, roughnessFactor, ior, specularFactor
f32 normalScale, occlusionStrength, alphaCutoff
u32 alphaMode               Graphics::AlphaModeEXT, 0-2
u32 flags                   bit 0 doubleSided
u8  textureCoordinateSets[7]  each 0 or 1
u8  reserved                must be 0
7 × { f32 offsetX, offsetY, scaleX, scaleY, rotation }     the per-slot UV transforms
7 × { u32 filter, addressU, addressV, declared }           the per-slot sampler states
```

The eight texture reference slots are CNA's own effect slots, which include `DualTextureEffect`'s
second layer. The seven-element arrays are in the importer's `TextureSlotEXT` order: base colour,
normal, metallic-roughness, occlusion, emissive, specular, specular colour. The two lists are not
the same list.

**`MMRP` — one per morphed part.**

```text
u32 vertexCount             0, or the owning part's vertexCount
u32 flags                   bit 0 = recompute flat normals from the morphed positions
u32 targetCount
targetCount × u32 presence  bit 0 positions, bit 1 normals, bit 2 tangents
for each target, for each present stream in that order:
    vertexCount × f32[3]
u32 weightCount
weightCount × f32
u32 trackFlags              bit 0 step interpolation, bit 1 cubic spline
u32 keyCount
keyCount × {
    f64 timeSeconds
    u32 n, n × f32          weights
    u32 n, n × f32          in-tangents
    u32 n, n × f32          out-tangents
}
```

**`MSKL`.**

```text
u32 jointCount
u32 flags                       bit 0 = a root-prefix block follows
jointCount × i32 parent         -1, or < jointCount
jointCount × f32[16] bindPose
jointCount × f32[16] inverseBindPose
[jointCount × f32[16] rootPrefix]   present iff flag bit 0
```

The chunk's length must equal exactly what those counts imply. The `.skeleton.bin` sidecar this
replaces signalled the optional third block by whether any bytes were left over, which made
"deliberately absent" and "file truncated" the same observation; here it is stated.

**`MANM`.**

```text
u32 clipCount               must equal MDLH's animationCount
u32 reserved                must be 0; keeps each clip row's f64 8-byte aligned
clipCount × {               24 bytes each
    u32 nameIndex
    u32 targetSpace         0 JointPalette, 1 SceneNode
    u32 firstTrack
    u32 trackCount
    f64 durationSeconds
}
u32 trackCount
trackCount × { i32 boneIndex, u32 firstKey, u32 keyCount }
u32 keyCount
keyCount × <the 48-byte keyframe of §10>
```

**`MLIT`.** `u32 count` (must equal `MDLH`'s `lightCount`), then `count` × `{ f32 direction[3],
f32 diffuseColor[3] }`.

### 11.1 What the v1 Model schema deliberately does not carry

* **glTF material variants** (`materialVariantNames` / `variantOf` / `materialVariant`). The
  compiler **refuses** a `.cnj` using them, naming the feature and this document, rather than
  compiling the asset into something quietly less capable than its source.
* **The glTF import diagnostic report.** It is an authoring-time record, not runtime geometry.

---

## 12. Error handling and limits

Every malformed-input failure is a
`Microsoft::Xna::Framework::Content::ContentLoadException`, the same type the rest of CNA's content
subsystem throws, so a game's existing `catch` keeps working.

`CnbReadLimits` bounds every count-driven read before anything is allocated. The process-wide
defaults:

| limit | default |
|---|---|
| `maxFileSize` | 512 MiB |
| `maxChunkCount` | 65536 |
| `maxChunkSize` | 384 MiB |
| `maxStringBytes` | 1 MiB |
| `maxArrayElementCount` | 16777216 |
| `maxChunkAlignment` | 4096 |

Every `offset + size` and `count × elementSize` computation goes through `CheckedAdd`/
`CheckedMultiply`, which test before performing the operation, so an overflow never occurs. Unsigned
wrap-around is well-defined in C++ but produces a *small* result from two huge inputs, which then
passes a naive bound check — that is exactly how a bounds-checked parser still reads out of range.

Every count is also checked against how many elements could physically fit in the bytes that
remain, so a declared count of four billion in a twelve-byte chunk fails immediately rather than
after an enormous allocation.

---

## 13. Determinism

Given identical inputs, `CnbWriter` produces byte-identical output. It reads no clock, no random
source, no pointer value and no environment; chunks are emitted in the order the schema encoder
adds them; the table of contents is written in that same order, which is also ascending-offset
order; alignment gaps are zero-filled; string and material tables intern in first-seen order. The
optional `CMET` chunk carries only input-derived strings.

This is asserted in-process (`CnbContainerTest.WritingTheSameInputTwiceProducesIdenticalBytes`,
`CnbModelCodecTest.EncodingIsDeterministic`) and across two separate OS processes
(`CnbCompilerToolTest.TwoSeparateProcessRunsProduceByteIdenticalOutput`) — two processes share no
allocator state, static-initialisation order or warm heap, which makes it a much stronger claim.

---

## 14. An annotated example

The smallest useful file: a `Curve` with no keys, no metadata and no external references. 172
bytes. `CnbSpecConformanceTests.cpp` regenerates this and byte-compares it against the dump below,
so the document cannot drift from the implementation.

```text
offset  bytes                                            meaning
------  -----------------------------------------------  ---------------------------------------
000000  43 4E 42 1A                                      magic "CNB\x1A"
000004  01 00                                            containerMajor = 1
000006  00 00                                            containerMinor = 0
000008  00 00 00 00                                      headerFlags = 0
00000C  07 00 00 00                                      assetTypeId = 7 (Curve)
000010  01 00 00 00                                      assetSchemaVersion = 1
000014  02 00 00 00                                      chunkCount = 2
000018  AC 00 00 00 00 00 00 00                          fileSize = 172
000020  40 00 00 00 00 00 00 00                          tocOffset = 64
000028  <crc>                                            tocChecksum (CRC-32C of [64,160))
00002C  <crc>                                            headerChecksum (CRC-32C of [0,44))
000030  00 * 16                                          reserved, zero

000040  43 52 56 48                                      chunk 0 type = "CRVH"
000044  01 00 00 00                                      flags = Mandatory
000048  A0 00 00 00 00 00 00 00                          offset = 160
000050  0C 00 00 00 00 00 00 00                          storedSize = 12
000058  0C 00 00 00 00 00 00 00                          uncompressedSize = 12
000060  <crc>                                            checksum
000064  00 00 00 00                                      compression = 0 (none)
000068  04 00 00 00                                      alignment = 4
00006C  00 00 00 00                                      reserved

000070  43 52 56 4B                                      chunk 1 type = "CRVK"
000074  01 00 00 00                                      flags = Mandatory
000078  AC 00 00 00 00 00 00 00                          offset = 172 (a zero-length chunk)
000080  00 00 00 00 00 00 00 00                          storedSize = 0
000088  00 00 00 00 00 00 00 00                          uncompressedSize = 0
000090  00 00 00 00                                      checksum of nothing = 0
000094  00 00 00 00                                      compression = 0
000098  04 00 00 00                                      alignment = 4
00009C  00 00 00 00                                      reserved

0000A0  00 00 00 00                                      CRVH.preLoop  = 0 (Constant)
0000A4  00 00 00 00                                      CRVH.postLoop = 0 (Constant)
0000A8  00 00 00 00                                      CRVH.keyCount = 0
0000AC                                                   end of file
```

The `CRVK` chunk is zero-length and sits at the end-of-file offset, which §4.1 permits: an empty
chunk occupies no bytes and takes no part in the layout partition.

---

## 15. Not implemented in v1

Recorded so the boundary is a decision rather than an omission. Each is tracked in
`plans/plan_cnb.md`.

* `Texture2D` / `Texture3D` / `TextureCube`, `SpriteFont`, `SoundEffect`, `Song`, `Video` and
  `Effect` schemas. Their identifiers are frozen; their layouts are not designed.
  For textures the intended shape is one `TEXn` chunk per representation, each declaring a
  `SurfaceFormat` with RGBA8 as the mandatory portable baseline, so a runtime can pick the best
  representation its renderer supports. Nothing in the container prevents that; nothing in v1
  implements it.
* Chunk compression (§8).
* Direct glTF / PNG / WAV → `.cnb` importers. The only compiler is `.cnj` → `.cnb`.
* Memory-mapped, zero-copy chunk access. The `alignment` field exists for it.
* A package format bundling many assets. That would be a different format.
