# The `.cnb` container from C

CNB is CNA's compiled-content format beside `.xnb`. The C surface is declared in
[`CNA/C/cnb.h`](../../modules/c-api/include/CNA/C/cnb.h); the normative wire layout and schema
invariants are in [`cnb-format.md`](../cnb-format.md). This page describes the C ownership,
authoring, loading and error contract rather than duplicating every constant in the header or every
byte offset in the format specification.

## Current scope

The generic container API is complete for container version 1.0: identity values, checked
arithmetic, CRC-32C, compression, read limits, document parsing, bounded readers, primitive
writers, container writers, metadata and external references. It can inspect or construct custom
schema chunks even when no typed C codec exists.

The typed codecs have this boundary:

| Asset | Typed C schema support | Important boundary |
|---|---|---|
| `Texture2D`, `TextureCube`, `Texture3D` | schema 1 | Multi-representation read; kind-specific write. |
| `Model` | schema 1 | Runtime loading includes schema 2; the C data model does not. |
| `SpriteFont` | schema 1 | The atlas is embedded and copied into or out of the font description. |
| `SoundEffect` | schemas 1 and 2 | The encoder writes schema 2; schema 2 adds unsigned 8-bit PCM. |
| `Song`, `Video` | schema 1 | Metadata plus exactly one external stream reference, never embedded media bytes. |
| `Curve` | schema 1 | Decoding returns the ordinary `CNA_CurveHandle`. |
| `AnimationClip` | schema 1 | Decoding returns an owned CNB clip handle because it owns its keyframes. |
| `Effect` | none | The asset identifier is frozen and reserved, but no renderer-neutral wire schema has been defined. |

The dedicated `cna_cnb_decode_model` and `cna_cnb_encode_model` routes therefore mean Model schema
1, not every Model schema the native runtime understands. A Model schema-2 file remains accessible
to the generic document API and can be loaded as a runtime Model through
`cna_content_manager_load_model`; it cannot be converted to `CNA_CnbModelDataHandle`.

## Handles and ownership

Pure identity, checksum, arithmetic, format-query and compression routes use caller-owned values
and bytes. The stateful groups use generation-checked handles:

- documents and readers;
- primitive and container writers;
- texture, Model, SpriteFont, SoundEffect and AnimationClip descriptions;
- Model-from-CNJ and general CNJ compilation results;
- resolved custom loaders.

These data handles do not need a game, graphics device, audio device or window and do not inherit a
game's owner-thread rule. A route that receives a content manager still follows that manager's
thread and lifetime contract.

Destroying a handle invalidates it immediately, and a repeated destroy returns
`CNA_RESULT_INVALID_HANDLE`. The significant non-owning relationships are:

- a reader opened by `cna_cnb_document_open_chunk` borrows the document; document destruction is
  refused until all such readers are destroyed;
- `cna_cnb_reader_create` instead copies the caller's bytes, so the caller may release or modify
  the original buffer immediately;
- setting a SpriteFont atlas copies the texture description, and copying the atlas back creates a
  new independently owned texture-description handle;
- `cna_cnb_model_from_cnj_take_model` transfers the Model description out of the result and may be
  called successfully only once;
- document and content-manager handles passed to a custom loader callback are callback-scoped
  borrows and become invalid when the callback returns.

## Documents, readers and limits

`cna_cnb_document_parse` copies the complete byte image. `cna_cnb_document_parse_file` checks the
declared file ceiling before allocating the file buffer. Both validate the container structure,
reserved fields, header and chunk checksums, table ordering and coverage, alignment and padding,
compressed sizes, total logical size and external-reference table before publishing a handle.

`CNA_CnbReadLimits` is active API, not a future placeholder. After initialization with
`cna_cnb_read_limits_init`, a caller may pass it to byte parsing, file parsing or an independent
reader. A parsed document exposes the limits it used through `cna_cnb_document_get_limits`, and a
container writer accepts limits so it cannot build a file that its paired reader would reject.
The limits cover file and chunk sizes, total post-decompression bytes, chunk count, strings, array
counts and maximum alignment.

The document gives either a caller-owned chunk copy or a borrowed bounded reader:

```c
uint64_t needed = 0;
cna_cnb_document_copy_chunk_data(document, index, NULL, 0, &needed);

CNA_CnbReaderHandle reader = CNA_INVALID_HANDLE;
cna_cnb_document_open_chunk(document, index, &reader);
```

Fixed-width reader operations decode little-endian values and leave their output untouched on
truncation. `cna_cnb_reader_read_count` checks both the configured array ceiling and whether the
declared elements can fit in the remaining region before a decoder allocates them.

Strings intentionally use a consuming read followed by a repeatable copy. Copying before a string
has been read is `CNA_RESULT_INVALID_STATE`. Byte reads use a caller-supplied size, so a short
destination is rejected without moving the cursor. `cna_cnb_reader_require_exhausted` turns an
unexpected suffix into a content error, and `cna_cnb_reader_fail` lets a custom schema produce the
same contextual diagnostic shape as the built-in reader.

## Writers and deterministic output

`CNA_CnbByteWriterHandle` emits little-endian primitives and length-prefixed UTF-8 strings. Its
copy operation is non-destructive; `cna_cnb_byte_writer_take` empties the writer only after a
successful full copy. `CNA_CnbWriterHandle` assembles the complete file and can either copy its
image or write it to a path.

The container writer is deterministic: identical values and chunk order produce identical bytes.
At build time it refuses conditions that the reader would reject, including invalid chunk IDs,
reserved container IDs used as schema chunks, illegal external-reference names, inconsistent
custom type identity and limits exceeded after compression. `CMET` and `XREF` are emitted before
schema chunks regardless of call order, so schema code must find chunks by type and ordinal rather
than by absolute table-of-contents index.

A custom type ID is the deterministic 31-bit identity minted from its canonical name. The numeric
ID alone cannot distinguish a hash collision, so a custom file must carry the canonical name in
`CMET`; the writer and loader registry both require the name and ID to agree.

## Identities, diagnostics and compression

Chunk, asset, texture-format, audio-format and compression values are fixed-width wire identities.
Diagnostic rendering routes accept unknown values so malformed input can still be described:
non-printable chunk bytes become `?`, unknown asset types distinguish reserved from custom ranges,
and unknown codecs or formats render their number instead of failing.

Logical external-reference names must be nonempty, relative, `/`-separated, well-formed UTF-8 and
contain no `..` segment. The two `logical_name_problem` routes deliberately accept malformed UTF-8
because reporting that defect is their purpose. Other C string inputs retain the ordinary
`CNA_RESULT_ENCODING` boundary.

CRC routes implement CRC-32C/Castagnoli and expose both the portable definition and the selected
hardware path. They detect accidental corruption, not malicious modification. Checked add and
multiply return `CNA_RESULT_OVERFLOW` without changing their output.

Compression is per chunk and disabled by default. The numeric identities are frozen even where a
codec is unavailable: stored data is always supported, Zstandard depends on the build, and the
current LZ4 and Deflate identities have no implementation. Query support before selecting a codec.
The writer may keep a chunk stored when compression would not reduce it.

The low-level `cna_cnb_copy_decompressed` route intentionally preserves one compatibility rule:
for `CNA_CNB_COMPRESSION_NONE` it copies the stored bytes and does not consult the declared
uncompressed size or ceiling. This does not bypass document safety; full document parsing applies
the configured chunk and total-size limits to stored and compressed chunks alike. Compressed
streams must expand to exactly their declared size or parsing returns `CNA_RESULT_IO`.

Compression's measured size and loading tradeoffs are kept separately in
[`cnb-compression-measurements.md`](../cnb-compression-measurements.md).

## Texture descriptions

CNB uses its own frozen `CNA_CnbTextureFormat` numbering rather than serializing the reorderable
`CNA_SurfaceFormat` enumeration. Conversion routes bridge the two. Block formats use whole 4×4
blocks, so even a 1×1 BC7 level occupies one 16-byte block.

A texture description owns one or more representations in preference order. Levels within each
representation are face-major, then mip. The selection callback runs synchronously on the calling
thread, must not re-enter the C ABI, and returns the first accepted representation; no accepted
format is the successful answer `out_found == CNA_FALSE`.

The schema-1 writer boundary is intentionally narrower than the readable format enumeration:

- a standalone `Texture2D` may encode RGBA8 and preferred BC1, BC2 or BC3 representations;
- `TextureCube`, `Texture3D` and an embedded SpriteFont atlas encode RGBA8 only;
- decoders preserve every schema-valid named representation for caller selection.

Current Texture2D content production can place BC1/BC2/BC3 first and exact RGBA8 second, allowing a
capable renderer to use the compressed payload while every renderer retains a portable fallback.

## Model schema 1

`CNA_CnbModelDataHandle` owns the whole schema-1 graph. Bones, parts, meshes, materials, optional
morph data, an optional skeleton, animations, lights and cameras are addressed by index rather than
by borrowed child handles. Cross-references use the same indices the wire format stores.

Setters intentionally allow a graph to be assembled in any order. The encoder is the point that
validates byte counts against declared vertex/index counts, mesh part indices, skeleton shapes,
animation tracks, camera values and other cross-field invariants.

A material has two related but different index spaces:

- eight named texture asset slots, addressed by `CNA_CnbMaterialTextureSlot`;
- seven importer/PBR slots for coordinate sets, UV transforms and samplers, addressed by a plain
  index below `CNA_CNB_TEXTURE_SLOT_COUNT`.

Coordinate-set values are limited to 0 or 1 when encoding. Optional morph and skeleton state has a
presence query; requesting absent data is an argument error, while clearing already absent state is
successful. Model animations reuse `CNA_AnimationClipEXTDescriptor` on input and carry target space
separately because the descriptor itself has no such field.

The dedicated `.cnj` Model front end returns a schema-1 graph plus absorbed-file and external-
reference lists. Taking its Model transfers ownership. It is distinct from the general
`cna_cnb_compile_cnj` result, which owns the finished file bytes instead.

## Other typed schemas

SpriteFont descriptions reuse `CNA_SpriteFontGlyph` rows. Their character map must be strictly
ascending and the default character, when present, must name a glyph; encoding checks these rules.
The atlas is embedded rather than listed as an external asset.

SoundEffect descriptions contain headerless little-endian PCM bytes. A schema-1 document accepts
PCM16 only; schema 2 accepts PCM16 or PCM8, and the current encoder always stamps schema 2. Float,
ADPCM and Vorbis identities remain reserved and cannot be encoded. Frame count, payload length,
channel count, sample rate and loop range must agree.

Song and Video documents contain metadata plus exactly one `XREF` stream reference. Curves decode
straight into the ordinary runtime Curve handle. Standalone and Model-embedded animation clips
share the published 48-byte keyframe representation; the reader reports an unrepresentable time as
file I/O failure, while the writer reports an invalid caller argument.

## Source import and CNJ compilation

The source importers are headless data tools:

- images decode to an RGBA8 `Texture2D`; an optional color key preserves RGB and sets matching
  pixels' alpha to zero;
- DDS cube maps decode DXT1/3/5 or byte-aligned RGB/BGR input to portable RGBA8 faces;
- WAV accepts uncompressed 8-, 16-, 24- and 32-bit integer PCM and 32-bit IEEE float. Eight-bit
  samples remain PCM8; 16-bit remains PCM16; wider integer and float input is deterministically
  narrowed to PCM16. Compressed WAV formats are refused.

`cna_cnb_compile_cnj` supports exactly eight descriptor types: `Curve`, `AnimationClip`, `Model`,
`Texture2D`, `Texture3D`, `TextureCube`, `SpriteFont` and `SoundEffect`. Its result owns the complete
CNB image, the canonical asset identity, absorbed source-file paths and external asset names. The
`.cnj` itself is the first absorbed file; authored relative paths are preserved so a build graph can
match them without losing subdirectory identity.

Neither import nor compilation requires a graphics or audio device. Invoking a loader is different:
the canonical loader signature requires a content manager so external references can resolve
through its cache.

## Loader registry and ContentManager

Custom loader registrations are process-wide and accept custom asset IDs only. Re-registering the
same ID and name is idempotent; pairing an existing ID with a different name is refused as a hash
collision. A resolved loader handle is a copy and remains valid after the registration is removed.

A C application has two valid custom-load paths:

1. resolve a document, then call `cna_cnb_loader_invoke` with a content manager;
2. register the loader and call `cna_content_manager_load_foreign_ext` for an ordinary cached
   content load.

There is no separate C spelling of `ContentManager::RegisterCnbLoaderEXT` because that C++ helper
registers into the same process-wide registry. The manager path is fully reachable from C and is
regression-tested; it was not true that custom CNB loaders could only be invoked directly.

The callback receives borrowed document and manager handles plus the logical asset name. It returns
an opaque caller-owned pointer that CNA never dereferences or frees. The callback context must
outlive the registration.

`cna_cnb_loader_registry_register_builtins` installs only the device-independent Curve and
AnimationClip loaders. A content manager installs the complete built-in set. Directly invoking a
built-in loader through `cna_cnb_loader_invoke` returns `CNA_RESULT_NOT_SUPPORTED`, because it
produces a native C++ object rather than the opaque wrapper used by C-registered loaders. Use an
available typed content-manager route or a typed CNB decoder instead.

## Result codes

- `CNA_RESULT_INVALID_ARGUMENT`: invalid pointer/value/structure version, bad index or unsupported
  enum value at an authoring boundary.
- `CNA_RESULT_ENCODING`: malformed UTF-8 at an ordinary string boundary.
- `CNA_RESULT_IO`: missing or malformed source/file data, an inconsistent encoded description, a
  broken container/schema invariant, wrong asset type/version or unresolved loader identity.
- `CNA_RESULT_INVALID_STATE`: invalid operation order, such as destroying a borrowed document or
  copying a reader string before reading one.
- `CNA_RESULT_NOT_SUPPORTED`: a codec absent from the build or direct invocation of a built-in
  loader whose native result cannot be represented as an opaque C object.
- `CNA_RESULT_OVERFLOW`: checked file arithmetic overflow; the output is untouched.
- `CNA_RESULT_BUFFER_TOO_SMALL`: a copy destination is short; the required count is reported and
  no partial write occurs.

## Verification and current limitations

The focused strict-C regressions are:

- `CApi_CnbSmoke`: identities, diagnostics, arithmetic, CRC, limits and compression;
- `CApi_CnbDocumentSmoke`: byte readers/writers, document borrows, file parsing and container
  writing;
- `CApi_CnbTextureSmoke`: texture formats, representations, all three texture kinds and embedded
  atlas data;
- `CApi_CnbModelSmoke`: the schema-1 Model graph, cameras, codecs and CNJ ownership transfer;
- `CApi_CnbSchemaSmoke`: SpriteFont, SoundEffect, Song, Video, Curve and AnimationClip codecs;
- `CApi_CnbToolingSmoke`: image/DDS/WAV import, CNJ compilation and loader registry behavior;
- `CApi_ContentSmoke`: custom `.cnb` loading through `ContentManager` and the foreign-object cache.

The native CNB suite separately covers Model schema 2, compressed Texture2D selection, content-
manager loading, wide PCM source conversion, malformed-input hardening and format golden vectors.

The remaining C-surface boundaries are deliberate and explicit: Model schema 2 has no typed C
authoring/inspection handle, the reserved Effect identity has no wire schema, unavailable
compression identities remain queryable but unusable, and built-in loader results cannot be
returned through the opaque custom-object invocation route.
