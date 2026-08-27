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

### 2.1 What `f32` and `f64` guarantee

The representation contract is enforced **at compile time**, in `CnbFormat.hpp`: a build whose
`float`/`double` are not IEEE-754 binary32/binary64, or whose floating-point and integer object
representations disagree in byte order, fails to compile rather than silently emitting a file no
other CNA could read. The byte-order half is checked with a `constexpr` `std::bit_cast` of a known
constant, because no standard trait reports floating-point endianness.

**The container stores the bit pattern verbatim.** It does not canonicalise and it does not reject
any pattern, so `+0.0`, `-0.0`, both infinities and every NaN encoding are representable and
distinguishable — `-0.0` and `+0.0` produce different bytes. Verified bit-for-bit by
`CnbHardeningTest.FloatingPointValuesRoundTripBitForBit`, which compares *bits* rather than values
precisely because `NaN != NaN` and `-0.0 == 0.0` would hide a defect.

One limit is stated rather than promised away. `CnbByteReader::ReadF32`/`ReadF64` return by value,
so on an ABI that passes floating-point returns through an x87 register stack a **signalling** NaN
could be quieted between the file and the caller. The bytes in the file are unaffected, and on
x86-64 a signalling payload was measured to survive intact; a 32-bit x87 target was not measurable
on the machine this was written on. CNB therefore guarantees the *file* holds exactly the bits
given to it, and guarantees value-identical round-tripping for every non-signalling pattern —
it does not promise signalling-NaN preservation on every ABI.

An asset schema may be narrower than the primitive layer, and one is: `AnimationClip` refuses a
non-finite duration or key time (§10), because `System::TimeSpan` cannot represent one. That is a
schema rule. Nothing at the primitive level rejects a NaN, and nothing should — deciding what a
value means is the schema's job.

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
| 40 | 4 | `alignment` (`u32`) | power of two, at most `maxChunkAlignment` (§12, default 4096); `offset % alignment == 0` |
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

That split is a **convention, not an enforced invariant**: the reader and the writer check only
that all four bytes are printable. It is written down so CNA can add a chunk to an existing schema
without colliding with an identifier a game already shipped — a guarantee that costs nothing to
honour and cannot be recovered later. Stated explicitly because the printable-byte rule beside it
*is* enforced, and a reader would otherwise reasonably assume both are.

**A chunk type may appear more than once.** One chunk refers to another by its **ordinal within its
own type** — "the third `MVTX` chunk" — never by a table-of-contents index. A raw index would shift
whenever a container-level chunk was added or removed ahead of the payload chunks; an ordinal is
stable under any such insertion. A schema declares which of *its* chunk types are singletons.

### 5.1 Container-defined chunks

These two are understood by every reader, whatever the asset type, and are always emitted before a
schema's own chunks.

**`CMET` — at most one. Type identity and provenance.**

```text
u32     flags            reserved, must be 0
String  assetTypeName    the type's canonical name, e.g. "Microsoft.Xna.Framework.Curve"
String  contentName      the logical asset name at compile time, or empty
```

Whether this chunk is optional depends on the asset type, and the difference is load-bearing:

* For a **built-in** asset type the chunk is **optional and diagnostic**. CNA assigns those
  identifiers itself and freezes them, so the header's number is a proof of identity on its own.
* For a **custom** asset type the chunk is **required**, and `assetTypeName` must be exactly the
  string the identifier was minted from. A custom identifier is a 31-bit hash (§7), so two
  unrelated game types can legitimately collide, and a numeric match is *not* a proof of identity.
  A reader must refuse a custom-typed file that carries no name, and must refuse one whose name
  disagrees with the name its loader was registered under. A writer must refuse to produce either.

Both halves of that rule are implemented: `CnbLoaderRegistry::ResolveForDocument` enforces it on
read and `CnbWriter::Build` enforces it on write, so a custom file that could never be loaded
cannot be produced in the first place.

**`XREF` — optional, at most one, marked `Mandatory`. External asset references.**

```text
u32 count
count × {
    u32     flags                reserved, must be 0
    u32     expectedAssetTypeId  the type the referring schema expects, or 0
    String  logicalName          a ContentManager logical asset name
}
```

`logicalName` is validated on read **and on write**, by one shared function
(`CnbLogicalNameProblem()`): non-empty, well-formed UTF-8, no backslash, not starting with `/`, not
drive-qualified (`X:`), and containing no `..` segment. `ContentManager`'s own containment checks
still run afterwards; this is defence in depth, so a compiled file can never hand path-traversal
input to the resolver in the first place. Sharing the rule is what stops a writer producing a file
its own reader refuses — the reader used to be alone in applying it.

`XREF` is marked `Mandatory` on purpose: a reader that could not see the names an asset depends on
would load a visibly incomplete asset and say nothing.

**Both container chunks are singletons, and the writer cannot emit a second of either.** `CMET` and
`XREF` come from `CnbWriter::SetMetadata()`/`SetExternalReferences()`; passing one of their
identifiers to `CnbWriter::AddChunk()` is refused, because a schema adding one as an ordinary chunk
would produce a file the writer accepted and the reader rejects.

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

The header's `assetTypeId` selects the **candidate** loader. Whether that candidate is accepted
depends on which range the identifier is in, and the two answers are different:

* For a **built-in** asset type the numeric identifier is sufficient. CNA assigns those identifiers
  itself and freezes them, so a match is a proof of identity; the file's `CMET` type name is never
  consulted for dispatch.
* For a **custom** asset type the numeric identifier is *not* sufficient (§5.1). The file must
  carry a `CMET` canonical type name, and that name must equal exactly the canonical name the
  candidate loader was registered under. A missing name, an empty name, or a differing name is a
  rejection.

Precisely, as implemented by `CnbLoaderRegistry::ResolveForDocument`, in this order:

```text
1. Look the header's assetTypeId up in the loader registry.
   No entry            -> reject: "no .cnb loader for <type>".
                          (This comes FIRST, so an unregistered CUSTOM identifier is
                          reported as an unknown type, not as a name mismatch. The
                          file's CMET name, if any, is quoted in the message to help
                          identify what it was.)
2. If the identifier is built-in (< 0x80000000), accept the candidate.
3. If the identifier is custom (>= 0x80000000):
   a. no CMET chunk, or an empty assetTypeName -> reject.
   b. assetTypeName != the registered canonical name -> reject as a collision.
   c. otherwise accept the candidate.
```

```text
0x00000000              invalid; a file declaring it is rejected
0x00000001-0x3FFFFFFF   CNA built-in asset types, frozen once v1 ships
0x40000000-0x7FFFFFFF   reserved for future CNA use
0x80000000-0xFFFFFFFF   game-defined custom types
```

| id | type | v1 schema |
|---|---|---|
| 1 | `Texture2D` | **version 1**, §16 |
| 2 | `Texture3D` | **version 1**, §16 |
| 3 | `TextureCube` | **version 1**, §16 |
| 4 | `SpriteFont` | **version 1**, §17 |
| 5 | `Model` | **version 1**, §11 |
| 6 | `AnimationClip` | **version 1**, §10 |
| 7 | `Curve` | **version 1**, §9 |
| 8 | `SoundEffect` | **version 1**, §18 |
| 9 | `Song` | **version 1**, §19 |
| 10 | `Video` | **version 1**, §19 |
| 11 | `Effect` | reserved, not implemented |

A custom identifier is `CnbAssetTypeIdFromName(utf8)` = `FNV-1a-32(name) | 0x80000000`, i.e. 31
usable bits. **Collisions are possible**, and the format handles them rather than wishing them
away. Three separate checks, each catching the mistake at a different moment:

1. `CnbLoaderRegistry::Register` refuses an identifier that the supplied canonical name does not
   actually hash to — catching a hand-written or mistyped identifier at the registration that is
   wrong, rather than as a baffling collision error at some later load.
2. `CnbLoaderRegistry::Register` refuses to register one identifier under two different names, so
   two colliding types in the same process fail loudly at startup instead of loading each other's
   files.
3. **Dispatch itself compares names, not just numbers** (§5.1). This is the check that matters for
   a file produced by a *different* program, where neither of the first two can help: a `.cnb`
   whose identifier matches but whose `CMET` name does not is refused as a collision.

The rule is deliberately asymmetric — built-in types dispatch on the number alone. Requiring a name
match for them would make the metadata chunk load-bearing for every asset in existence and break
any file whose type-name string was ever tidied, in exchange for nothing: CNA controls those
identifiers and does not reuse them.

Runtime type identity (`std::type_index`) is deliberately not used: its value is not stable across
processes, let alone builds, so it is not a serialisation ABI.

---

## 8. Compression

`compression` is a per-chunk `u32` codec identifier.

| value | codec | status |
|---|---|---|
| 0 | none | always available; the default |
| 1 | LZ4 | reserved, no implementation |
| 2 | Zstandard | **implemented**, opt-in, when CNA is built with libzstd |
| 3 | Deflate | reserved, no implementation |

A reader refuses a codec it does not implement, **naming it**, so the answer is "build CNA with
that codec" rather than "the file is broken". `IsCnbCompressionSupported()` is the public query;
whether a given build has a codec is not something a consumer should learn from a macro.

For a compressed chunk, `storedSize` is the compressed length, `uncompressedSize` is the expanded
length, and `checksum` covers the **stored** bytes — so a corrupt file is caught before anything
reaches a decompressor. `uncompressedSize` is checked against `maxChunkSize` (§12) **before any
allocation**, which is what stops a few kilobytes of hostile input from asking for gigabytes, and
the stream must expand to exactly `uncompressedSize`: a shorter expansion would leave the tail of
the buffer as zeroes that later code reads as data.

A writer compresses a chunk only when doing so actually made it **smaller**; one that grew is
stored, because it would otherwise cost both bytes and decompression time. `CMET` and `XREF` are
never compressed, so an inspector can read a file's identity without the codec.

The field is per **chunk**, not per file. That was a guess when it was written and it has since
been justified by measurement: a build can compress a 4 MB texture payload and leave a 200-byte
header alone, and different platforms can make different choices about the same asset.

**The original justification for having no codec was wrong, and is recorded here rather than
quietly deleted.** It read: *"most of what a game ships is already compressed (PNG, Ogg)"*. True of
the source files, false of what CNB stores — a PNG becomes raw `Rgba8` at compile time and an Ogg
becomes raw `Pcm16`, so CNB is exactly where the data is *not* compressed. Measured on real
content (`docs/cnb-compression-measurements.md`), zstd level 3 gives:

| payload | ratio | break-even device read speed |
|---|---|---|
| `Rgba8` photograph | 51 % | 456 MB/s |
| `Pcm16` audio | 27 % | 1469 MB/s |
| `f32` vertex data | 15 % | 1073 MB/s |

So the size saving is large and unconditional, while the *load-time* saving exists only below those
read speeds — on desktop NVMe (2.5 GB/s measured) compression makes loading slower. Hence the
design: the codec exists, defaults to off, and is chosen per chunk.

**A compressed chunk cannot be read by a CNA older than the codec.** Every reader written before a
codec landed rejects a non-zero `compression`, so enabling it raises the minimum runtime version
for that file. That is a deliberate property of an opt-in feature, not an oversight.

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
i32 parent          -1 for the root; otherwise an EARLIER index in this table
f32 transform[16]   XNA row-major field order M11..M44
```

The bone table **must be ordered parent-before-child**: a bone's `parent` is either `-1` or
strictly less than the bone's own index. This is not a tidiness rule and it is not optional.
`Model::CopyAbsoluteBoneTransformsTo` composes world transforms in a single ascending pass, reading
`dest[parentIndex]` as it goes; a bone whose parent came later would be composed against a slot not
yet written and would place its geometry somewhere it does not belong — quietly, without an error.
Requiring the order also makes a cycle structurally impossible. Both source formats already specify
it (`.cnj` bone arrays are documented parent-before-child, and `SkinningData::SkeletonHierarchy` is
documented topological), so nothing that can be authored today is refused by stating it here.

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
    u32 primitiveCount            MUST equal what topology + indexCount imply (see below)
    u32 effectKind                0 Basic, 1 Skinned, 2 DualTexture, 3 Pbr, 4 SkinnedPbr, 5 External
    u32 externalEffectRef         XREF index when effectKind == 5, else 0xFFFFFFFF
    u32 materialIndex             index into MMAT
    u32 flags                     bit 0 vertexColorEnabled, bit 1 unlit
}
u32 slotCount
slotCount × u32 partIndex
```

`primitiveCount` is redundant — the topology and the index count determine it — and redundant data
in a binary format is only safe if it is cross-checked, so a reader **must** verify it rather than
trust it. A part claiming more primitives than its indices describe would draw past the end of its
own index buffer. The rule per topology, over `n` indices:

| topology | primitives |
|---|---|
| 0 points | `n` |
| 1 lines | `n / 2` |
| 2 line loop, 3 line strip | `n - 1`, or 0 when `n` is 0 |
| 4 triangles | `n / 3` |
| 5 triangle strip, 6 triangle fan | `n - 2`, or 0 when `n` is below 3 |

**Every index must be less than the part's `vertexCount`.** A `ModelMeshPart` draws `vertexCount`
vertices starting at zero, so an index at or above that is out of range by construction and reaches
the GPU as an out-of-range fetch. A reader validates the whole index buffer, which costs one pass
over bytes it is already copying.

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
jointCount × i32 parent         -1, or an EARLIER index (parent-before-child, as for MBON)
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

The container-level `CMET` and `XREF` chunks are decoded during parsing rather than on first use,
so a parsed document is immutable and every accessor is a plain `const` read. A malformed `CMET`,
or an `XREF` naming a path outside the content root, is therefore a **parse** failure — reported
when the file is opened rather than at whichever later call happened to touch it first.

`CnbReadLimits` bounds every count-driven read before anything is allocated. The limits are held
**by value** by both `CnbDocument` and `CnbByteReader`: they are a small trivially-copyable struct,
and `Parse(bytes, name, CnbReadLimits{})` is the natural call, so retaining the caller's address
would make lifetime the caller's problem for no benefit. The process-wide defaults:

| limit | default |
|---|---|
| `maxFileSize` | 512 MiB |
| `maxChunkCount` | 65536 |
| `maxChunkSize` | 384 MiB |
| `maxTotalUncompressedSize` | 1024 MiB |
| `maxStringBytes` | 1 MiB |
| `maxArrayElementCount` | 16777216 |
| `maxChunkAlignment` | 4096 |

Arithmetic that combines two *file-declared* values — `offset + storedSize`,
`vertexStride × vertexCount`, `firstKey + keyCount` — goes through `CheckedAdd`/`CheckedMultiply`,
which test before performing the operation, so the overflow never occurs. Unsigned wrap-around is
well-defined in C++ but produces a *small* result from two huge inputs, which then passes a naive
bound check — that is exactly how a bounds-checked parser still reads out of range. Arithmetic that
multiplies one `u32` count by a *compile-time* stride (`keyCount × 48`, `boneCount × 72`, …) is
widened to `u64` first, where the largest possible product is under 2⁴¹ and no overflow is
representable.

Every count is also checked against how many elements could physically fit in the bytes that
remain, so a declared count of four billion in a twelve-byte chunk fails immediately rather than
after an enormous allocation.

`maxTotalUncompressedSize` bounds something no per-item limit can: the sum of every chunk's
**logical** (post-decompression) size. `maxChunkSize` caps one chunk and `maxChunkCount` caps how
many there are, so without an aggregate their product — 24 PiB at the defaults — is what a reader
would be willing to allocate for a file holding a few kilobytes of individually legal compressed
frames. Every chunk counts toward the sum, compressed or not, so the invariant reads the same way
whatever a file's codecs are; for a wholly uncompressed file the sum is bounded by `maxFileSize`
regardless, because chunks do not overlap. The sum is accumulated with `CheckedAdd` while the table
of contents is read and compared to the ceiling **before any chunk's bytes are allocated or handed
to a decompressor**. The default is deliberately larger than `maxFileSize`, so compression can
genuinely expand a file rather than be cancelled out by this bound.

### 12.1 What the limits do and do not guarantee

Stated plainly, because the difference matters to anyone deciding whether these bounds are enough
for their situation.

**They do guarantee** that no single count, length or offset read from a file can cause an
allocation, a read or an arithmetic operation that the file's own size does not justify. Every
count is checked against its configured ceiling *and* against how many elements could physically
fit in the bytes that remain, before anything is reserved.

**They do not constitute a total memory budget.** The limits are per-item, not a running account,
and CNB deliberately does not implement an accounting allocator. Two consequences follow, and both
are properties of the format rather than oversights:

* A file bounded by `maxFileSize` can decode to **more** memory than its own size. That is inherent
  to a compiled format: 16-bit indices widen to 32-bit for morph normal recomputation, a
  `CnbModelPart` is a much larger object than its 56-byte descriptor row, and a string table row is
  a `std::string`. The expansion is bounded and proportional — a small multiple, not unbounded —
  because every array's element count is tied to bytes actually present in the file.
* Nothing bounds the number of `.cnb` files a program loads at once. That is `ContentManager`'s
  concern and the application's, not the container's.

An application with a hard memory ceiling should therefore set `CnbReadLimits::maxFileSize` from
that ceiling with headroom, rather than assuming the per-item limits add up to one.

Two counts additionally carry a **schema-level** ceiling, because their in-memory footprint is many
times their encoded size and the generic array limit alone would still allow an unreasonable
allocation. Both match the ceilings `ContentManager`'s own `.cnj` readers already apply, so nothing
a `.cnj` can express is refused:

| count | ceiling |
|---|---|
| a part's morph targets (`MMRP`) | 100000 |
| a part's morph weight keys (`MMRP`) | 1000000 |

A `Model`'s part count is bounded by a stronger check still: it must equal the number of `MVTX` and
`MIDX` chunks the file actually holds, which the table-of-contents limit already caps.

---

## 13. Determinism

Given identical inputs, `CnbWriter` produces byte-identical output. It reads no clock, no random
source, no pointer value and no environment; chunks are emitted in the order the schema encoder
adds them; the table of contents is written in that same order, which is also ascending-offset
order; alignment gaps are zero-filled; string and material tables intern in first-seen order. The
`CMET` chunk carries only input-derived strings (it is optional for a built-in asset type and
required for a custom one, §5.1; either way its contents come from the compiler's inputs).

This is asserted in-process (`CnbContainerTest.WritingTheSameInputTwiceProducesIdenticalBytes`,
`CnbModelCodecTest.EncodingIsDeterministic`) and across two separate OS processes
(`CnbCompilerToolTest.TwoSeparateProcessRunsProduceByteIdenticalOutput`) — two processes share no
allocator state, static-initialisation order or warm heap, which makes it a much stronger claim.

It is also asserted against bytes this implementation did not produce.
`CnbGoldenVectorTests.cpp` holds golden byte vectors generated by a separate Python implementation
of this specification (`tools/cnb/gen_golden_vectors.py`), and requires the writer to reproduce
them exactly and the reader to decode them to the documented values. A golden file produced by the
implementation under test would prove only that the implementation agrees with itself; these prove
it agrees with the specification as an independent reader understood it.

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

**Read this list against §15.1.** "Implemented" is four separate claims — a wire schema, a runtime
loader, a writer API, and a command-line producer — and this document has previously blurred them.
A type can be fully readable at runtime and still have no supported way to *produce* one.

* The `Effect` schema. Its identifier is frozen; its layout is not designed, and deliberately so:
  CNA has many renderers, and a `.cnb` carrying one API's shader bytecode would be useless on the
  others. It waits for the shader pipeline and renderer abstraction to settle.
* **Embedded** audio or video streams. §19 stores a reference, by design.
* Block-compressed texture **payloads**. §16 defines the identifiers and the multi-representation
  structure that carries them, and the reader accepts a file that uses them, but no writer in this
  build produces one and this build cannot upload one.
* ~~Chunk compression (§8).~~ **Implemented** — Zstandard, codec 2, opt-in per chunk and off by
  default, available when CNA is built with libzstd. See §8.
* Memory-mapped, zero-copy chunk access. **Measured and rejected**, not pending: mmap would save
  4.7 ms of a 32 MiB load while the CRC verification it cannot avoid cost 62.5 ms, so the effort
  went into hardware CRC-32C instead and made the same load 6.4× faster. See
  `docs/cnb-mmap-measurements.md`, including what would change the answer. The `alignment` field
  keeps its meaning regardless.
* A package format bundling many assets. That would be a different format.

### 15.1 Four different meanings of "supported"

| asset type | wire schema | runtime loader | writer API | CLI producer |
|---|---|---|---|---|
| `Texture2D` | §16 | yes | `EncodeTexture2DToCnb` | **yes** — image source, and `.cnj` |
| `TextureCube` | §16 | yes | `EncodeTextureCubeToCnb` | **yes** — DDS source, and `.cnj` |
| `Texture3D` | §16 | yes | `EncodeTexture3DToCnb` | **yes** — `.cnj` (raw RGBA sidecar) |
| `SpriteFont` | §17 | yes | `EncodeSpriteFontToCnb` | **yes** — `.cnj`, atlas absorbed |
| `Model` | §11 | yes | `EncodeModelToCnb` | **yes** — glTF direct, and `.cnj` |
| `AnimationClip` | §10 | yes | `EncodeAnimationClipToCnb` | **yes** — `.cnj` |
| `Curve` | §9 | yes | `EncodeCurveToCnb` | **yes** — `.cnj` |
| `SoundEffect` | §18 | yes | `EncodeSoundEffectToCnb` | **yes** — WAV source, and `.cnj`. PCM16 and 8-bit unsigned PCM only |
| `Song` | §19 | yes | `EncodeSongToCnb` | **yes** — wraps a media file; no `Song` `.cnj` exists |
| `Video` | §19 | yes | `EncodeVideoToCnb` | **yes**, with required metadata arguments; no `Video` `.cnj` exists |
| `Effect` | — | — | — | — |

Every implemented schema now has a producer. `TextureCube` was the last gap: its source is DDS, and
the DDS decoder used to sit inside `TextureCube::DDSFromStreamEXT` where only code that could
create a GPU texture could reach it. It was **extracted rather than duplicated** — the parsing and
DXT decompression were already pure CPU work, so they moved into
`CNA::Internal::Graphics::DecodeDdsCube` and both the runtime and the compiler now sit above the
same decoder. A compiled cube map holds decompressed `Rgba8`, which is what the runtime path
produces anyway.

`Video`'s producer cannot invent metadata: duration, frame size and frame rate would need a
multimedia decoder CNA does not expose headlessly, so they are **required arguments** rather than
values guessed from the file.

### 15.2 What produces what

| producer | reads | writes |
|---|---|---|
| `cna_tool_cnj_to_cnb` | a `.cnj` document and its sidecars | `Curve`, `AnimationClip`, `Model`, `Texture2D`, `Texture3D`, `TextureCube`, `SpriteFont`, `SoundEffect` |
| `cna_tool_gltf_to_cnb` | `.gltf`/`.glb` | `Model.cnb`, byte-identical to the two-step route |
| `cna_tool_source_to_cnb` | PNG/JPEG/… , DDS, WAV, and media files | `Texture2D`/`TextureCube`/`SoundEffect`/`Song`/`Video` `.cnb` |
| `cna_tool_cnb_info` | any `.cnb` | nothing; inspects and validates |

Every one of them is **headless and deterministic**: no `GraphicsDevice`, no audio device, no
clock, no randomness. Given identical source bytes, options and logical name, each produces
identical output bytes — asserted across two separate OS processes, not merely two calls in one.

Every numeric option is parsed **strictly**: the whole token must be consumed, a sign is refused
for an unsigned value, the range is checked before any narrowing, and a NaN or an infinity is
refused by name. An option that does not apply to the asset kind being produced is an error rather
than something ignored, and a failing invocation exits non-zero without leaving a partial file
behind.

---

## 16. `Texture2D`, `TextureCube` and `Texture3D`, schema version 1

Three asset types, one chunk layout. They differ only in what the header's `faceCount` and `depth`
are allowed to be, so repeating the layout three times would be three chances to let it drift. The
asset type identifier in the file header is what distinguishes them, which is what that field is
for.

| chunk | count | flags | contents |
|---|---|---|---|
| `TEXH` | exactly 1 | Mandatory | dimensions and counts |
| `TEXR` | exactly 1 | Mandatory | `representationCount` × 24-byte descriptors |
| `TEXD` | one per level per representation | Mandatory | one mip level's payload bytes |

### 16.1 `TEXH`

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `width` | level 0 width in texels; ≥ 1 |
| 4 | 4 | `height` | level 0 height in texels; ≥ 1 |
| 8 | 4 | `depth` | level 0 depth in texels; **must be 1** unless the asset is a `Texture3D` |
| 12 | 4 | `faceCount` | **1** for `Texture2D`/`Texture3D`, **6** for `TextureCube` |
| 16 | 4 | `mipCount` | 1…16 |
| 20 | 4 | `representationCount` | 1…8 |

A `TextureCube` additionally requires `width == height`, because a cube face is square.

Mip level *n* has dimensions `max(1, width >> n)` × `max(1, height >> n)` × `max(1, depth >> n)`.
Each dimension floors at 1 independently, so an 8×4 texture's chain is 8×4, 4×2, 2×1, 1×1 — the
height reaches 1 two levels before the width does.

### 16.2 `TEXR`, 24 bytes per descriptor

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `format` | a `CnbTextureFormat` identifier, §16.4 |
| 4 | 4 | `firstPayloadOrdinal` | index of this representation's first `TEXD`, in `TEXD` ordinal order |
| 8 | 4 | `payloadCount` | must equal `faceCount * mipCount` |
| 12 | 4 | `flags` | reserved; must be zero |
| 16 | 8 | `totalPayloadBytes` | must equal the sum of this representation's level sizes |

**The descriptors must tile the `TEXD` list exactly**: the first starts at ordinal 0, each
subsequent one starts where the previous ended, and together they account for every `TEXD` chunk
in the file. A payload owned by no representation would be data the reader silently ignores, which
is precisely the property a container should not have, so it is refused.

Within a representation the levels are ordered **face-major, then mip**: face 0 mip 0, face 0
mip 1, …, then face 1 mip 0. For a cube map the faces are in the fixed order +X, −X, +Y, −Y, +Z,
−Z, matching `CubeMapFace`.

`TEXD` payloads are 16-byte aligned. Block-compressed formats have 8- and 16-byte units, and a
future memory-mapped reader wants a payload start at least as aligned as the largest unit.

### 16.3 Why several representations

The same image may appear more than once in one file, in different formats, so a runtime can pick
whichever encoding its GPU actually supports rather than shipping one asset per platform. The
writer records them in preference order and a reader takes the first one it can upload.

Schema 1 **writes exactly one representation, always `Rgba8`**. The structure is nevertheless part
of the frozen layout from the start, because adding it later would be a schema break while adding a
second *writer* is not. A file that offers `Bc7` first and `Rgba8` second already loads correctly on
this build, by falling through to the second representation.

### 16.4 `CnbTextureFormat`

The `format` field is **not** a `SurfaceFormat` value. `SurfaceFormat`'s enumerators carry no
explicit numbers, so inserting one — an ordinary thing to do to an XNA-shaped enum — would renumber
everything after it and silently change the meaning of every file already written. A file format
cannot be hostage to the declaration order of a runtime enum, so CNB has its own frozen numbering
and an explicit mapping that has to be edited deliberately.

| id | name | unit bytes | `SurfaceFormat` |
|---|---|---|---|
| 0 | invalid | — | — |
| 1 | `Rgba8` | 4 | `Color` |
| 2 | `Bgra8` | 4 | `ColorBgraEXT` |
| 3 | `Rgba8Srgb` | 4 | `ColorSrgbEXT` |
| 4 | `Bgr565` | 2 | `Bgr565` |
| 5 | `Bgra5551` | 2 | `Bgra5551` |
| 6 | `Bgra4444` | 2 | `Bgra4444` |
| 7 | `Alpha8` | 1 | `Alpha8` |
| 8 | `R8` | 1 | `ByteEXT` |
| 9 | `R16` | 2 | `UShortEXT` |
| 10 | `Rg16` | 4 | `Rg32` |
| 11 | `Rgba16` | 8 | `Rgba64` |
| 12 | `Rg8Snorm` | 2 | `NormalizedByte2` |
| 13 | `Rgba8Snorm` | 4 | `NormalizedByte4` |
| 14 | `Rgb10A2` | 4 | `Rgba1010102` |
| 15 | `R32Float` | 4 | `Single` |
| 16 | `Rg32Float` | 8 | `Vector2` |
| 17 | `Rgba32Float` | 16 | `Vector4` |
| 18 | `R16Float` | 2 | `HalfSingle` |
| 19 | `Rg16Float` | 4 | `HalfVector2` |
| 20 | `Rgba16Float` | 8 | `HalfVector4` |
| 21 | `HdrBlendable` | 8 | `HdrBlendable` |
| 22 | `Bc1` | 8 per 4×4 block | `Dxt1` |
| 23 | `Bc2` | 16 per 4×4 block | `Dxt3` |
| 24 | `Bc3` | 16 per 4×4 block | `Dxt5` |
| 25 | `Bc3Srgb` | 16 per 4×4 block | `Dxt5SrgbEXT` |
| 26 | `Bc7` | 16 per 4×4 block | `Bc7EXT` |
| 27 | `Bc7Srgb` | 16 per 4×4 block | `Bc7SrgbEXT` |

Every identifier CNA's `SurfaceFormat` can name is assigned here, so the numbering never has to be
extended for a format that already exists. That is separate from what schema 1 encodes.

A `TEXD` payload's length must equal the level's exact size: `unitBytes × width × height × depth`
for an uncompressed format, and `unitBytes × ceil(width/4) × ceil(height/4) × depth` for a
block-compressed one. **The rounding is up, to whole blocks** — a 1×1 `Bc7` level is one 16-byte
block, not a fraction of one, which is what makes the tail of a compressed mip chain correct.

---

## 17. `SpriteFont`, schema version 1

A font is a glyph atlas plus four parallel per-glyph tables. **The atlas is embedded**, using the
same `TEXH`/`TEXR`/`TEXD` chunks §16 defines, with the same strides, alignment and validation — not
a second encoding of the same idea. An atlas normally belongs to exactly one font, which is the
opposite of a model's textures, and that difference is why one is embedded and the other is `XREF`d.

| chunk | count | flags | contents |
|---|---|---|---|
| `FONT` | exactly 1 | Mandatory | glyph count, spacing, default character |
| `GLYP` | exactly 1 | Mandatory | `glyphCount` × 16 bytes: source rectangles |
| `CROP` | exactly 1 | Mandatory | `glyphCount` × 16 bytes: cropping rectangles |
| `KERN` | exactly 1 | Mandatory | `glyphCount` × 12 bytes: bearings |
| `CHAR` | exactly 1 | Mandatory | `glyphCount` × 4 bytes: the character map |
| `TEXH`/`TEXR`/`TEXD` | §16 | Mandatory | the embedded atlas, `faceCount` 1, `depth` 1 |

### 17.1 `FONT`

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `glyphCount` | 1…65536 |
| 4 | 4 | `lineSpacing` | signed; vertical distance between lines, in pixels |
| 8 | 4 | `spacing` | `f32`; extra horizontal spacing between characters |
| 12 | 4 | `hasDefaultCharacter` | 0 or 1; any other value is rejected |
| 16 | 4 | `defaultCharacter` | UTF-16 code unit; meaningless when the flag is 0 |
| 20 | 4 | `flags` | reserved; must be zero |

The presence flag is not decoration. "No default character" and "the default character is U+0000"
are different fonts — one throws on a missing glyph, the other renders a NUL — and a bare code unit
cannot distinguish them.

### 17.2 The per-glyph tables

`GLYP` and `CROP` hold four `i32` each: `x`, `y`, `width`, `height`. `KERN` holds three `f32`:
left bearing, width, right bearing. `CHAR` holds one `u32` per glyph, each a UTF-16 code unit; a
value above `0xFFFF` is rejected.

All four tables have exactly `glyphCount` entries. They are parallel — entry *n* of each describes
the same glyph — and a length disagreement is refused rather than truncated, because it would
otherwise become an out-of-range read at render time rather than a load error.

### 17.3 The character map must be strictly ascending

`SpriteFont` looks a character up by **binary search**. An unsorted map therefore does not fail
loudly: it silently returns the wrong glyph, or none. So the order is a format requirement, checked
on write *and* on read — a file need not have come from this writer, and no length or checksum test
can catch a reordering, since both orderings are the same number of well-formed bytes.

Strictly ascending also forbids duplicates, which would make one of the two entries unreachable.
A `defaultCharacter` that is not in the map is likewise refused.

---

## 18. `SoundEffect`, schema version 1

| chunk | count | flags | contents |
|---|---|---|---|
| `AUDH` | exactly 1 | Mandatory | format, rate, channels, frame count, loop points |
| `AUDD` | exactly 1 | Mandatory | the sample bytes, 16-byte aligned |

### 18.1 `AUDH`

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `format` | a `CnbAudioFormat` identifier, §18.2 |
| 4 | 4 | `sampleRate` | Hz; 1…384000 |
| 8 | 4 | `channels` | 1 (mono) or 2 (stereo); XNA's `AudioChannels` has no third value |
| 12 | 4 | `frameCount` | sample frames, i.e. samples per channel |
| 16 | 4 | `loopStart` | first frame of the loop region |
| 20 | 4 | `loopLength` | frames in the loop region; 0 means no loop |
| 24 | 4 | `flags` | reserved; must be zero |

`AUDD`'s length must be exactly `frameCount × frameBytes`, and `loopStart + loopLength` must not
exceed `frameCount`. **The loop check is the one that earns its keep**: an over-long loop is not a
malformed file in any structural sense — every length and checksum is correct — and without the
rule it becomes an out-of-range read inside the mixer, at playback time, on someone else's machine.

### 18.2 `CnbAudioFormat`

Its own numbering, for the same reason `CnbTextureFormat` has one: no audio backend's enumerators
are a serialisation ABI.

| id | name | frame bytes | v1 |
|---|---|---|---|
| 0 | invalid | — | rejected |
| 1 | `Pcm16` | `2 × channels` | **written and read** |
| 2 | `Pcm8` | `1 × channels` | reserved |
| 3 | `PcmFloat32` | `4 × channels` | reserved |
| 4 | `Adpcm` | block format, no fixed frame size | reserved |
| 5 | `Vorbis` | packet format, no fixed frame size | reserved |

Samples are **headerless** little-endian PCM, not a WAV or other container's raw bytes. That is
what the runtime's raw-buffer constructor takes, so storing a container would mean parsing one at
load time for no benefit.

`Adpcm` and `Vorbis` have no fixed frame size, so the `AUDD` length rule above cannot apply to
them. Neither has a v1 codec; whichever gains one first needs its own length rule rather than
inheriting this one.

---

## 19. `Song` and `Video`, schema version 1

| chunk | count | flags | contents |
|---|---|---|---|
| `SNGH` | exactly 1 (`Song`) | Mandatory | duration, flags, display name |
| `VIDH` | exactly 1 (`Video`) | Mandatory | duration, frame size, rate, soundtrack type |
| `XREF` | exactly 1 entry | Mandatory | the media file to stream |

### 19.1 Why these carry a reference and not the media

A `SoundEffect` owns its samples (§18). A song or a video does not, and the difference is not
stylistic. Such a file can be hundreds of megabytes and wants streaming, seeking and buffering;
embedding it would push the whole thing through the container's chunk machinery and into memory
just to play its first second. So a `Song`/`Video` `.cnb` says **what to stream and how**, and the
media file stays beside it.

The reference lives in `XREF` rather than in the schema chunk, which is what makes the dependency
visible to `cna_tool_cnb_info --refs` — a build script can therefore discover that the `.ogg` has to
ship without knowing anything about the `Song` schema. Exactly one entry is required: the media
file is the whole point of the asset, and a second reference would be a dependency nothing knows
how to interpret. `expectedAssetTypeId` is `0`, because the target is a media file on disk rather
than a CNA asset with an identifier of its own, and `flags` is `0` because none are defined. Both
are **enforced on read**, not merely written: a row that names an expected CNA type is describing a
dependency this schema cannot honour.

### 19.2 `SNGH`

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `durationMs` | 0 when the compiler could not determine it; at most `0x7FFFFFFF` |
| 4 | 4 | `flags` | reserved; must be zero |
| 8 | … | `name` | length-prefixed UTF-8 display name; may be empty |

### 19.3 `VIDH`

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `durationMs` | 0 when unknown; at most `0x7FFFFFFF` |
| 4 | 4 | `width` | 1…65536 |
| 8 | 4 | `height` | 1…65536 |
| 12 | 4 | `framesPerSecond` | `f32`; must be finite and greater than zero |
| 16 | 4 | `soundtrackType` | `VideoSoundtrackType`: 0 Music, 1 Dialog, 2 MusicAndDialog |
| 20 | 4 | `flags` | reserved; must be zero |

The frame-rate rule is checked on **both** sides. A NaN or infinite `f32` is a perfectly
well-formed bit pattern that §2.1 says the container stores verbatim, so the schema layer is the
only one that can refuse it — and it would otherwise divide badly inside a player.

`durationMs` is a `u32` on the wire but is bounded at `0x7FFFFFFF` on both sides, because `Song`'s
and `Video`'s constructors take a signed 32-bit millisecond count: a larger value would arrive
negative and every later `Duration`/`PlayPosition` comparison would read backwards. `INT32_MAX`
milliseconds is about 24.8 days, so nothing real is refused.
