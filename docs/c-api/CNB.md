# The `.cnb` container from C

CNB is CNA's own compiled content format, beside `.xnb`. This page is the C contract for the part
of it that is independent of any asset schema: what a byte at a given offset means, how a chunk is
identified, how a checksum is computed, what a reader refuses before it allocates anything, and how
a chunk payload is compressed and expanded.

Everything here is declared in `CNA/C/cnb.h` and reachable through the umbrella `CNA/C/cna.h`.

## What is bound, and what is not

**Bound.** The container's identities and byte-level constants, chunk identifiers, asset type
identifiers, external-reference name validation, the whole-file arithmetic a reader does on
file-declared numbers, CRC-32C, the read limits, and chunk compression.

**Not bound.** The `CnbDocument` a reader parses a file into, the `CnbByteReader`/`CnbByteWriter`
cursors every schema reads and writes with, `CnbWriter`, the loader registry, and every asset
schema — textures, sprite fonts, models, sound effects, media, curves and animation clips.

So **a C application still cannot load a `.cnb` asset.** What it can do is understand, validate and
transform the container itself: check a file's magic and header checksum, classify its chunks and
asset type, mint a custom asset type identifier, decide whether an external-reference name is legal,
and compress or expand a chunk payload. `plans/plan_binding.md` Phase B10 is the backlog for the
rest; `CBIND-107` through `CBIND-111` are the slices, and `docs/c-api/CONTENT.md` records the
consequence from the `ContentManager` side.

## Nothing here is a handle

The whole family is pure functions over caller-owned bytes plus one versioned value structure.
There is no lifetime to manage, no thread affinity and no `_destroy` route; a route that reads bytes
takes a pointer and a count and never retains either. That is unusual for this ABI and is worth
saying once rather than repeating in thirty places.

Every route in the family is named `cna_cnb_*` and none carries an `_ext` suffix. The whole of
`CNA::Content::Cnb` is CNA-namespace surface with no XNA 4.0 counterpart, so — following
`core_ext.h` — the header is the marker rather than each route.

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
