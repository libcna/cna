# The `.cnb` container from C

CNB is CNA's own compiled content format, beside `.xnb`. This page is the C contract for the part
of it that is independent of any asset schema: what a byte at a given offset means, how a chunk is
identified, how a checksum is computed, what a reader refuses before it allocates anything, and how
a chunk payload is compressed and expanded.

Everything here is declared in `CNA/C/cnb.h` and reachable through the umbrella `CNA/C/cna.h`.

## What is bound, and what is not

**Bound.** The container's identities and byte-level constants, chunk identifiers, asset type
identifiers, external-reference name validation, the whole-file arithmetic a reader does on
file-declared numbers, CRC-32C, the read limits, and chunk compression (`CBIND-106`); the parsed
document, its bounded cursor, the primitive writer and the container writer (`CBIND-107`); and the
texture pixel formats with the `Texture2D`, `TextureCube` and `Texture3D` schemas (`CBIND-108`);
and the model schema, with its codec and the `.cnj` compile path (`CBIND-109`).

**Not bound.** The loader registry, and the remaining asset schemas — sprite fonts, sound effects,
media, curves and animation clips.

So a C application can **build a `.cnb` file and parse one back**, walk its table of contents, read
any chunk's bytes, check its metadata and external references, and encode or decode a texture or a
model. It still cannot load a `.cnb` asset through a `ContentManager`. `plans/plan_binding.md`
Phase B10 is the backlog for the rest, and `docs/c-api/CONTENT.md` records the consequence from the
`ContentManager` side.

## Handles, and the parts that have none

The container primitives — identities, constants, checksums, arithmetic, name validation and chunk
compression — are pure functions over caller-owned bytes plus one versioned value structure. There
is no lifetime to manage there, no thread affinity and no `_destroy`.

The document, the writers and the two decoded descriptions do own something, so they are handles:
`CNA_CnbDocumentHandle`, `CNA_CnbReaderHandle`, `CNA_CnbByteWriterHandle`, `CNA_CnbWriterHandle`,
`CNA_CnbTextureDataHandle`, `CNA_CnbModelDataHandle` and `CNA_CnbModelFromCnjHandle`. One borrow
relationship exists and it is the one to know about — see *Documents, cursors and who owns the
bytes* below.

Every route in the family is named `cna_cnb_*` and none carries an `_ext` suffix. The whole of
`CNA::Content::Cnb` is CNA-namespace surface with no XNA 4.0 counterpart, so — following
`core_ext.h` — the header is the marker rather than each route.


## Documents, cursors and who owns the bytes

`cna_cnb_document_parse` copies the whole file into the document, which then owns it. A document
that exists is a container that is structurally sound: parsing applies every invariant — magic,
versions, reserved-field zeroing, both structural checksums, every chunk checksum, overflow-safe
offset arithmetic, alignment, table-of-contents ordering, exact non-overlapping coverage of the
file, and zeroed alignment padding — before any accessor hands out a byte. A schema decoder only
has to worry about its own contents.

Two ways to reach a chunk's bytes, and they differ in who owns them:

```c
/* A copy you own. */
uint64_t needed = 0;
cna_cnb_document_copy_chunk_data(document, index, NULL, 0, &needed);
/* allocate `needed`, then call again */

/* A cursor over the document's own bytes -- no copy. */
CNA_CnbReaderHandle chunk;
cna_cnb_document_open_chunk(document, index, &chunk);
```

**A cursor opened from a document borrows it.** `cna_cnb_document_destroy` answers
`CNA_RESULT_INVALID_STATE` until every reader opened from that document is destroyed. Retaining the
document would already be memory-safe; refusing the release is this ABI's standing rule for a
borrow, and it turns a leaked cursor into a refused call rather than a document that quietly
outlives its handle.

**A cursor created with `cna_cnb_reader_create` copies the bytes it is given.** The canonical C++
cursor never copies — it documents that the caller must keep the region alive. C has no way to be
told that and no way to be caught breaking it, so the ABI takes the copy instead of leaving a
lifetime rule it cannot police. This is the one deliberate deviation in the family.

## Reading a chunk

Every read is checked against the region's end before a byte is touched, and a truncation is
`CNA_RESULT_IO` naming the region and the offset. Integers are assembled byte by byte and floats
come from an explicitly little-endian integer, so a decoded value never depends on the host.

```c
uint32_t count = 0;
cna_cnb_reader_read_count(chunk, sizeof(float) * 3, (CNA_StringView){"points", 6}, &count);
for (uint32_t i = 0; i < count; ++i) {
    float x, y, z;
    cna_cnb_reader_read_f32(chunk, &x);
    cna_cnb_reader_read_f32(chunk, &y);
    cna_cnb_reader_read_f32(chunk, &z);
}
cna_cnb_reader_require_exhausted(chunk);   /* trailing bytes mean you and the file disagree */
```

`cna_cnb_reader_read_count` is not a plain `u32` read: it checks the declared count against
`max_array_element_count` **and** against how many elements could actually fit in what remains, so
a corrupt count fails before anything is allocated for it.

**Strings need two calls, because reading is destructive and copying is not.**

```c
uint64_t bytes = 0;
cna_cnb_reader_read_string(chunk, &bytes);        /* consumes; reports the size */
cna_cnb_reader_copy_string(chunk, buffer, capacity, &bytes);  /* repeatable */
```

A single route taking a destination could not report a capacity that was too small without either
losing the string it had already consumed or consuming it twice. Copying before any read is
`CNA_RESULT_INVALID_STATE`, so "nothing read yet" cannot be mistaken for "an empty string".

`cna_cnb_reader_read_bytes` needs only one route, because its size is your own argument rather than
something the file declares: a capacity too small is settled before the cursor advances, so a
refused call consumes nothing and can simply be repeated.

`cna_cnb_reader_fail` exists so a schema decoder's own refusals read exactly like the cursor's —
same context, same offset, same shape.

## Writing a container

`CNA_CnbByteWriterHandle` emits the primitives; `CNA_CnbWriterHandle` assembles the file.

```c
CNA_CnbByteWriterHandle bytes;
cna_cnb_byte_writer_create(&bytes);
cna_cnb_byte_writer_write_u32(bytes, 3);
cna_cnb_byte_writer_write_f32(bytes, 1.5f);

CNA_CnbWriterHandle writer;
cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_CURVE, 1, &writer);
cna_cnb_writer_set_metadata(writer, name, content);
cna_cnb_writer_add_chunk(writer, id, payload, payload_size, CNA_CNB_CHUNK_FLAG_MANDATORY, 4);
cna_cnb_writer_build(writer, image, capacity, &image_size);
```

The writer is deterministic: no clock, no random source, no pointer value. Identical inputs produce
byte-identical output.

Four things it refuses, each because accepting them would produce a file its own reader rejects:

- the container's own identifiers (`CMET`, `XREF`) as schema chunks — the writer emits each at most
  once, from `cna_cnb_writer_set_metadata` and `cna_cnb_writer_add_external_reference`;
- a chunk identifier with a byte outside printable ASCII;
- an external-reference name the reader would refuse — the writer applies the *same* rule, which is
  what stops the two ends drifting apart;
- a custom asset type with no matching canonical name, since a custom identifier is a 31-bit hash
  and the name is what settles identity.

`cna_cnb_writer_set_limits` is worth knowing about even if you never call it. Compression breaks the
intuition that a file a writer built is a file a reader can open: a highly compressible document
serializes to very little and expands to a great deal, so the writer enforces the reader's limits at
build time. The producer is the right place to find that out.

**The container-level chunks are always emitted first**, ahead of the schema's own, regardless of
when they were set. A schema must therefore address chunks by type — `cna_cnb_document_find_all`,
`_find_single`, `_require_single` — and never by table-of-contents index.


## Textures

Three asset types share one chunk layout — `TEXH` for the shape, `TEXR` for the representation
table, one `TEXD` per level — and differ only in how the header's face count and depth are
constrained. The container header's asset type is what tells them apart, which is what it is for.

**Pixel formats are a separate, frozen numbering, and the reason matters.** `CNA_CnbTextureFormat`
exists instead of serializing `CNA_SurfaceFormat` because the canonical `SurfaceFormat` enumerators
carry no explicit values: inserting one would renumber everything after it and silently change the
meaning of every `.cnb` already written. So these 27 values never move, and
`cna_cnb_texture_format_to_surface_format` / `_from_surface_format` are the deliberate bridge
between them.

```c
CNA_CnbTextureDataHandle texture;
cna_cnb_texture_data_create_rgba8(width, height, rgba, width * height * 4, &texture);

uint64_t needed = 0;
cna_cnb_encode_texture2d(texture, name, NULL, 0, &needed);
/* allocate `needed`, then call again */
```

A texture may carry the same image several times over — once as `RGBA8`, once as `BC7` — so a
runtime can pick whichever its GPU supports without a second asset. Each is a **representation**,
and its levels are ordered face-major then mip: index `face * mip_count + mip`.

```c
uint64_t index = 0;
CNA_Bool found = CNA_FALSE;
cna_cnb_texture_data_select_representation(texture, my_gpu_supports, &state, &found, &index);
```

The predicate is called synchronously, once per representation, in the order the writer recorded
them — which is preference order, so the first accepted one is the author's intended choice. No
supported format is an ordinary answer (`found` false), not a refusal.

Two rules worth knowing before you compute a buffer size yourself:

- **A block-compressed level rounds each dimension up to a whole 4-texel block.** A 1×1 BC7 level is
  a full 16-byte block, not a fraction of one. `cna_cnb_get_texture_level_byte_size` applies that
  rule; open-coding `width * height * unit` does not.
- **Every identifier can be decoded; schema 1 only ever encodes `RGBA8`.** Encoding any other format
  is refused, which is better than writing a file no reader of this schema would accept.

`cna_cnb_writer_append_embedded_texture2d` puts a texture's chunks into a document being written for
a *different* asset type — that is how a sprite font carries its glyph atlas, with exactly the
chunks, strides, alignment and validation a standalone texture would use. An atlas normally belongs
to one font, so embedding is right there; a model's textures are shared and are referenced through
`XREF` instead.

## Models

A model is the largest schema in the format and the one whose C shape is a decision rather than a
transcription. The canonical description is a graph of nested vectors — bones, parts each with a
material and optional morph data, meshes, an optional skeleton, animations, lights — and C cannot
hold one by value.

**One handle for the whole graph; its nodes are reached by index.** The alternative was a borrowed
handle per node, and it was rejected for three reasons: a bone or a part has no lifetime of its own,
a handle each would multiply the registry by the size of the model and leave a caller with one
release per node to get right, and every cross-reference the *format itself* writes is already an
index — a mesh's part indices, a bone's parent, the skeleton's hierarchy.

```c
CNA_CnbModelDataHandle model;
cna_cnb_model_create(&model);

float transform[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
uint64_t root = 0;
cna_cnb_model_add_bone(model, name, -1, transform, &root);

CNA_CnbModelPartInfo part = { .struct_size = sizeof part,
                              .struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION };
/* fill in strides and counts */
cna_cnb_model_add_part(model, &part, part_name, no_external_effect, NULL);
cna_cnb_model_set_part_vertex_bytes(model, 0, vertices, vertex_byte_count);
```

A part's bytes and its declared counts are **not checked against each other until the model is
encoded**, which is what lets a caller fill a part in any order. `cna_cnb_encode_model` is where an
inconsistency becomes `CNA_RESULT_IO`.

### The material carries two texture orderings, and they are different

This is the one trap on this page. A material names **eight** textures — CNA's own effect slots,
including `DualTextureEffect`'s second layer, which glTF has no counterpart for — and carries
**seven** per-slot arrays for coordinate sets, UV transforms and samplers, in the *importer's* slot
order. The two are addressed differently on purpose:

| What | Addressed by | Range |
|---|---|---|
| Texture asset names | `CNA_CnbMaterialTextureSlot` | 0 … `CNA_CNB_MATERIAL_TEXTURE_MAXIMUM` (8 values) |
| Coordinate sets, transforms, samplers | a plain index | 0 … `CNA_CNB_TEXTURE_SLOT_COUNT - 1` (7 values) |

A binding that crossed them would still round-trip, because both halves would be wrong together.
`CnbModelSmoke.c` writes a distinguishable value into all fifteen and reads them all back for
exactly that reason.

One canonical limit to know: a coordinate set must be **0 or 1**. CNA's vertex layouts carry two UV
sets, and the encoder refuses a model naming anything else — the setter stores what you give it and
the encode is where it is checked.

### Optional parts are absences, not failures

Morph data and the skeleton are `std::optional` canonically. In C, presence is asked for rather
than discovered from a refusal:

```c
CNA_Bool has_morph = CNA_FALSE;
cna_cnb_model_has_morph(model, part_index, &has_morph);
```

`cna_cnb_model_get_info` reports `has_skeleton` the same way. Reading either when it is absent is
`CNA_RESULT_INVALID_ARGUMENT` — a caller's mistake, since the presence query answers first — and
clearing something already absent is still a success.

The skeleton's three matrix arrays and a morph target's three delta streams are each one route
selected by an identity (`CNA_CnbSkeletonMatrixSet`, `CNA_CnbMorphDeltaStream`,
`CNA_CnbMorphKeyStream`) rather than three near-identical routes.

`has_root_prefix` deserves its own note: the old `.skeleton.bin` sidecar signalled the per-joint
scene-ancestry prefix by whether any bytes were left over, which made *deliberately absent* and
*file truncated* the same observation. The compiled form states it, and so does this ABI.

### Animations reuse the clip shape the ABI already has

`cna_cnb_model_add_animation` takes the same `CNA_AnimationClipEXTDescriptor` the `SkinnedModelEXT`
routes take, rather than a second clip shape. Reading a clip back is indexed instead, because a
decoded clip belongs to the model and cannot be lent out as a borrowed descriptor.

The **target space is a separate argument**, because the descriptor does not carry one: a
`SkinnedModelEXT` clip is always a joint palette, while a compiled model may hold either a joint
palette or a scene node. Applying one as the other is a silent corruption rather than a visible
error, which is why it is stated rather than defaulted.

### Compiling a `.cnj`

```c
CNA_CnbModelFromCnjHandle compiled;
cna_cnb_build_model_from_cnj(cnj_path, content_root, &compiled);

CNA_CnbModelDataHandle model;
cna_cnb_model_from_cnj_take_model(compiled, &model);   /* transferred, not borrowed */
```

The result also carries two file lists a build system needs and that are deliberately kept apart:
`absorbed_file` is what the compile consumed, so a dependency graph knows when to rebuild, and
`external_reference` is what it left external, so the same graph knows those assets must also be
compiled. The model is **taken** rather than borrowed so that releasing the result cannot destroy a
model a caller is still holding; taking it twice is refused.

## Which failure means what

- `CNA_RESULT_INVALID_ARGUMENT` — **you** got something wrong: a null output, an unknown structure
  version, or a chunk or reference index past the end. The canonical layer throws a content
  exception for an out-of-range index; the C layer separates it, because a caller fixes an index
  rather than re-downloading the asset.
- `CNA_RESULT_IO` — **the file** is wrong: a broken container invariant, a truncated read, an
  unknown mandatory chunk, a wrong asset type, a name the format forbids.
- `CNA_RESULT_INVALID_STATE` — the **order** is wrong: destroying a document that still has readers,
  or copying a string before reading one.
- `CNA_RESULT_NOT_SUPPORTED` — this **build** cannot: a compression codec it was not built with.
- `CNA_RESULT_BUFFER_TOO_SMALL` — your buffer is short. `out_byte_count` says how much is needed and
  nothing is written; where the call would otherwise consume or empty something, it does not.

## Identities

`CNA_CnbChunkId` is a `uint32_t`. The canonical type is a structure holding exactly one `uint32_t`
with defaulted equality, so the typedef *is* the value: two identifiers are compared with C's own
`==`, and there is no field accessor or equality route. Build one with `cna_cnb_make_chunk_id`,
which packs four bytes little-endian so they read left to right in a hex dump:

```c
CNA_CnbChunkId id;
if (cna_cnb_make_chunk_id('C', 'M', 'E', 'T', &id) == CNA_RESULT_SUCCESS &&
    id == CNA_CNB_CONTAINER_CHUNK_METADATA) {
    /* the metadata chunk */
}
```

`CNA_CnbCompression` is a `uint32_t` at the canonical ordinals, and **those numbers are wire format
and frozen**: codec 2 is Zstandard in every `.cnb` ever written, whether or not a given build
implements it. The canonical enumeration also declares `ReservedLz4`, `ReservedZstd` and
`ReservedDeflate` — deprecated former names that are *aliases of the same values*, so they are the
same constants here. A second C name for one wire value would invite a caller to believe the two
mean different things in a file.

Whether a build can actually use a codec is a separate, runtime question:

```c
CNA_Bool supported = CNA_FALSE;
cna_cnb_is_compression_supported(CNA_CNB_COMPRESSION_ZSTD, &supported);
```

An identity outside the named set answers `CNA_FALSE` rather than failing, because "this build does
not implement it" is exactly true of a codec that does not exist.

## Diagnostics accept what a corrupt file can contain

Three routes render a value for a log line, and all three accept **any** input rather than refusing
one that is out of range:

- `cna_cnb_copy_chunk_id_string` renders any byte outside printable ASCII as `?`, so a corrupt
  identifier cannot inject a control character into a log;
- `cna_cnb_copy_asset_type_name` renders an unrecognized identifier as `unknown type 0x…` or
  `custom type 0x…`, saying which kind of unknown it is;
- `cna_cnb_copy_compression_name` renders an unnamed codec as `unknown codec N`.

A route that refused the very values a corrupt file contains would be useless for diagnosing one.

## External-reference names, and the one deviation on this page

`cna_cnb_get_logical_name_problem_size` and `cna_cnb_copy_logical_name_problem` answer *why* a name
is not a legal `.cnb` external reference. **A byte count of zero means the name is acceptable** —
that is the whole answer, and it is why there is no separate boolean route.

```c
uint64_t bytes = 0;
if (cna_cnb_get_logical_name_problem_size(name, &bytes) == CNA_RESULT_SUCCESS && bytes == 0) {
    /* legal */
}
```

A name is legal when it is non-empty, well-formed UTF-8, relative (does not begin with `/` and is
not drive-qualified like `C:`), `/`-separated, and contains no `..` segment.

**These two routes do not validate their input as UTF-8, unlike every other input string in this
ABI.** Malformed UTF-8 is one of the verdicts they exist to *report*, so refusing it at the boundary
would withhold the answer the caller asked for: they return `CNA_RESULT_SUCCESS` with the text
`is not well-formed UTF-8`. Only the pointer/length pair itself is checked. Every other string
input in this ABI — including `cna_cnb_asset_type_id_from_name` on the same page — still answers
`CNA_RESULT_ENCODING` for malformed bytes.

## Arithmetic on numbers a file declared

Every `offset + size` computation in a reader combines two values the file itself declares. Unsigned
wrap-around is well defined in C but produces a *small* result from two huge inputs, which then
passes a naive `<= file_size` bound check — the classic way a bounds-checked parser still reads out
of range.

`cna_cnb_checked_add` and `cna_cnb_checked_multiply` answer `CNA_RESULT_OVERFLOW` instead, and
**leave the output untouched** so a refused call cannot be mistaken for a computed zero.

## Checksums

`cna_cnb_crc32c` computes CRC-32C (Castagnoli) with the iSCSI parameter set — reflected polynomial
`0x82F63B78`, initial register `0xFFFFFFFF`, reflected input and output, final XOR `0xFFFFFFFF`. The
value is bit-identical on every platform CNA targets, which is what a stored-in-the-file checksum
requires, and identical whether or not a hardware instruction computed it.

This detects **accidental** corruption — a truncated download, a half-written build artifact, a bad
offset. It is not a message authentication code and must never be presented as one: anyone who can
rewrite a chunk can trivially rewrite its checksum.

A region split across two buffers checksums without concatenating them:

```c
uint32_t crc = CNA_CNB_CRC32C_SEED;
cna_cnb_crc32c_continue(crc, first, first_size, &crc);
cna_cnb_crc32c_continue(crc, second, second_size, &crc);
```

`cna_cnb_crc32c_portable` is the table-driven definition of correct, published so a caller can prove
the shipping path agrees with it rather than merely agreeing with itself;
`cna_cnb_crc32c_uses_hardware` says which path this process took. The *result* is the same either
way, so nothing about correctness depends on asking — but a benchmark that does not know which path
it measured will eventually mislead someone.

## Read limits

`CNA_CnbReadLimits` carries the sanity bounds a count-driven read consults. A correctly
bounds-checked binary reader can still be told, by one corrupted or adversarial count field, to
allocate an enormous buffer before further validation rejects the file; these limits fail fast with
a clear message instead of attempting that allocation. They are generous relative to any real asset.

```c
CNA_CnbReadLimits limits = {0};
limits.struct_size = (uint32_t)sizeof(limits);
limits.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
cna_cnb_read_limits_init(&limits);
limits.max_file_size = 32u * 1024u * 1024u;   /* tighten whatever you want tighter */
```

It follows the documented prefix rule: a future caller's larger structure is accepted, an older
caller's smaller one is refused. Two of its defaults are relationships rather than numbers —
`max_chunk_size` is below `max_file_size`, and `max_total_uncompressed_size` is *above*
`max_file_size` so compression can genuinely expand a file rather than being cancelled out by the
bound that exists to cap the expansion.

The structure has no consumer in this ABI yet; the routes that will take it arrive with the document
and the byte cursors.

## Chunk compression

Compression is **opt-in and off by default**, and that is a measured decision rather than caution:
roughly half off a texture payload, three quarters off audio and six sevenths off vertex data — but
decompression only *saves load time* on storage slower than 456–1469 MB/s, so on desktop NVMe it
makes loading slower. Size always wins; time only sometimes does.

Producing compressed bytes follows the same count/copy shape as encoding an image, because the size
is only known by producing them:

```c
uint64_t needed = 0;
cna_cnb_get_compressed_byte_count(raw, raw_size, CNA_CNB_COMPRESSION_ZSTD, 3, &needed);
/* allocate `needed`, then: */
cna_cnb_copy_compressed(raw, raw_size, CNA_CNB_COMPRESSION_ZSTD, 3,
                        buffer, capacity, &needed);
```

`cna_cnb_copy_decompressed` needs no count route: the size is an input, because the file's table of
contents declares it.

Three behaviours are contract rather than incidental, and a C reader must agree with a C++ one about
the same file:

1. **A codec this build does not implement is `CNA_RESULT_NOT_SUPPORTED`**, not an argument error. A
   caller retries with another codec; it does not fix its numbers.
2. **A declared unpacked size above the ceiling is `CNA_RESULT_INVALID_ARGUMENT`, refused before
   anything is allocated.** That is what stops a few kilobytes of hostile input from asking for
   gigabytes. When a chunk is *both* over the ceiling and in an unimplemented codec, the ceiling is
   the answer — the checks run in that order.
3. **`CNA_CNB_COMPRESSION_NONE` consults neither size.** A stored chunk's bytes are the answer, so
   `uncompressed_size` and `max_uncompressed_size` are not looked at.

The codec must then produce **exactly** `uncompressed_size` bytes. A stream that expands to a
different size is a corrupt file (`CNA_RESULT_IO`), not a short read for later code to treat as data.

`level` is a **correction, not a contract**: a value outside the codec's range is clamped exactly as
the canonical implementation clamps it, never refused. For Zstandard the range is 1–19 and 3 is the
measured sweet spot.

A compressed chunk cannot be read by a CNA built without the codec, or by any CNA from before it
existed. Enabling compression on a file raises that file's minimum runtime.
