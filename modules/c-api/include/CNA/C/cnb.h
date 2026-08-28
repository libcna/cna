// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_CNB_H
#define CNA_C_CNB_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cnb.h
 * @brief The `.cnb` container: identities, byte-level constants, checksums, whole-file arithmetic,
 *        read limits and chunk compression.
 *
 * CNB is CNA's own compiled content format, beside `.xnb`. This header carries the part of it that
 * is independent of any asset schema -- what a byte at a given offset means, how a chunk is
 * identified, how a checksum is computed, and what a reader refuses before it allocates anything.
 * The schemas themselves, the document, the byte cursors and the loader registry are separate
 * families and are not published yet; `docs/c-api/CNB.md` says which.
 *
 * Every declaration here maps a member of the `CNA::Content::Cnb` namespace, which has no XNA 4.0
 * counterpart at all. Following `core_ext.h`, a whole header of CNA-namespace surface does not
 * repeat an `_ext` suffix on every route -- the header is the marker.
 *
 * **Nothing here is a handle.** The whole family is pure functions over caller-owned bytes plus one
 * versioned value structure, so there is no lifetime to manage, no thread affinity, and no
 * `_destroy` route. A route that reads bytes takes a pointer and a count and never retains either.
 */

/* --- container constants ------------------------------------------------------------------- */

/** @brief Number of magic bytes at offset 0 of a `.cnb` file. */
#define CNA_CNB_FORMAT_MAGIC_SIZE UINT32_C(4)

/** @brief Size of the fixed container header, in bytes. */
#define CNA_CNB_FORMAT_HEADER_SIZE UINT32_C(64)

/** @brief Size of one table-of-contents entry, in bytes. */
#define CNA_CNB_FORMAT_TOC_ENTRY_SIZE UINT32_C(48)

/** @brief Number of leading header bytes covered by the header checksum. */
#define CNA_CNB_FORMAT_HEADER_CHECKSUM_COVERAGE UINT32_C(44)

/** @brief Byte offset of the header checksum field. */
#define CNA_CNB_FORMAT_HEADER_CHECKSUM_OFFSET UINT32_C(44)

/** @brief Number of reserved, must-be-zero bytes at the end of the header. */
#define CNA_CNB_FORMAT_HEADER_RESERVED_SIZE UINT32_C(16)

/** @brief Container major version this implementation reads and writes. */
#define CNA_CNB_FORMAT_CONTAINER_MAJOR UINT32_C(1)

/** @brief Container minor version this implementation writes. */
#define CNA_CNB_FORMAT_CONTAINER_MINOR UINT32_C(0)

/** @brief Byte offset at which the writer always places the table of contents. */
#define CNA_CNB_FORMAT_DEFAULT_TOC_OFFSET UINT64_C(64)

/* --- chunk identifiers --------------------------------------------------------------------- */

/**
 * @brief A four-character chunk identifier, packed little-endian so its bytes read left-to-right
 *        in a hex dump.
 *
 * The canonical type is a structure holding exactly one `uint32_t` with defaulted equality, so the
 * C form is that integer: two identifiers are compared with C's own `==`, and there is no separate
 * field accessor or equality route to add a second spelling of either.
 *
 * Every byte must be printable ASCII (`0x20`-`0x7E`). An identifier whose first byte is an
 * uppercase ASCII letter is reserved for CNA's own schemas; a game defining its own `.cnb` schema
 * uses one starting with a lowercase letter.
 */
typedef uint32_t CNA_CnbChunkId;

/** @brief `CMET` -- the asset's canonical type name and its source content name. */
#define CNA_CNB_CONTAINER_CHUNK_METADATA UINT32_C(0x54454D43)

/** @brief `XREF` -- optional table of external assets this file refers to by logical name. */
#define CNA_CNB_CONTAINER_CHUNK_EXTERNAL_REFERENCES UINT32_C(0x46455258)

/** @brief No per-chunk flags set. */
#define CNA_CNB_CHUNK_FLAG_NONE UINT32_C(0)

/**
 * @brief The chunk is mandatory: a reader that does not understand its identifier must refuse the
 *        whole file rather than skip it.
 */
#define CNA_CNB_CHUNK_FLAG_MANDATORY UINT32_C(1)

/** @brief Every flag bit this container version defines. Any other bit set is an error. */
#define CNA_CNB_CHUNK_FLAG_ALL CNA_CNB_CHUNK_FLAG_MANDATORY

/* --- compression codecs -------------------------------------------------------------------- */

/**
 * @brief Fixed-width per-chunk compression codec identity.
 *
 * **The numeric values are wire format and frozen.** Codec 2 is Zstandard in every `.cnb` ever
 * written, whether or not a given build implements it. Whether a build can actually *use* one is a
 * separate, runtime question that @ref cna_cnb_is_compression_supported answers.
 *
 * The canonical enumeration also declares `ReservedLz4`, `ReservedZstd` and `ReservedDeflate` as
 * deprecated former names of the three below. They are aliases of the same values rather than
 * distinct enumerators, so they are the same constants here; publishing a second name for one
 * integer would be a second spelling of the identical wire value.
 */
typedef uint32_t CNA_CnbCompression;

/** @brief Stored uncompressed. Always available, and the default. */
#define CNA_CNB_COMPRESSION_NONE UINT32_C(0)
/** @brief LZ4. Identifier assigned; no implementation in this build. */
#define CNA_CNB_COMPRESSION_LZ4 UINT32_C(1)
/** @brief Zstandard. Implemented when CNA is built with libzstd. */
#define CNA_CNB_COMPRESSION_ZSTD UINT32_C(2)
/** @brief Deflate. Identifier assigned; no implementation in this build. */
#define CNA_CNB_COMPRESSION_DEFLATE UINT32_C(3)
/** @brief Highest codec identity this ABI version names. */
#define CNA_CNB_COMPRESSION_MAXIMUM CNA_CNB_COMPRESSION_DEFLATE

/* --- asset type identifiers ---------------------------------------------------------------- */

/** @brief Not a valid asset type; a file declaring it is rejected. */
#define CNA_CNB_ASSET_TYPE_INVALID UINT32_C(0x00000000)
/** @brief `Microsoft.Xna.Framework.Graphics.Texture2D`. */
#define CNA_CNB_ASSET_TYPE_TEXTURE2D UINT32_C(0x00000001)
/** @brief `Microsoft.Xna.Framework.Graphics.Texture3D`. */
#define CNA_CNB_ASSET_TYPE_TEXTURE3D UINT32_C(0x00000002)
/** @brief `Microsoft.Xna.Framework.Graphics.TextureCube`. */
#define CNA_CNB_ASSET_TYPE_TEXTURE_CUBE UINT32_C(0x00000003)
/** @brief `Microsoft.Xna.Framework.Graphics.SpriteFont`; embeds its atlas. */
#define CNA_CNB_ASSET_TYPE_SPRITE_FONT UINT32_C(0x00000004)
/** @brief `Microsoft.Xna.Framework.Graphics.Model`. */
#define CNA_CNB_ASSET_TYPE_MODEL UINT32_C(0x00000005)
/** @brief `Microsoft.Xna.Framework.Graphics.AnimationClipEXT`. */
#define CNA_CNB_ASSET_TYPE_ANIMATION_CLIP UINT32_C(0x00000006)
/** @brief `Microsoft.Xna.Framework.Curve`. */
#define CNA_CNB_ASSET_TYPE_CURVE UINT32_C(0x00000007)
/** @brief `Microsoft.Xna.Framework.Audio.SoundEffect`. */
#define CNA_CNB_ASSET_TYPE_SOUND_EFFECT UINT32_C(0x00000008)
/** @brief `Microsoft.Xna.Framework.Media.Song`; metadata plus a streaming reference. */
#define CNA_CNB_ASSET_TYPE_SONG UINT32_C(0x00000009)
/** @brief `Microsoft.Xna.Framework.Media.Video`; metadata plus a streaming reference. */
#define CNA_CNB_ASSET_TYPE_VIDEO UINT32_C(0x0000000A)
/**
 * @brief `Microsoft.Xna.Framework.Graphics.Effect`. Identifier reserved; **no schema, by design**.
 *
 * CNA has many renderers, so a `.cnb` carrying one API's shader bytecode would be useless on the
 * others. This waits for the shader pipeline to settle rather than for someone to find time.
 */
#define CNA_CNB_ASSET_TYPE_EFFECT UINT32_C(0x0000000B)
/** @brief Lowest identifier reserved for future CNA use. */
#define CNA_CNB_ASSET_TYPE_RESERVED_RANGE_FIRST UINT32_C(0x40000000)
/** @brief Lowest identifier available to game-defined asset types. */
#define CNA_CNB_ASSET_TYPE_CUSTOM_RANGE_FIRST UINT32_C(0x80000000)

/* --- checksum ------------------------------------------------------------------------------ */

/**
 * @brief The starting value for a running CRC-32C built up with @ref cna_cnb_crc32c_continue.
 *
 * The canonical `Crc32cSeed()` is a `constexpr` function returning this value, and a compile-time
 * constant is how C says the same thing -- it is usable in a static initializer, which a route
 * would not be.
 */
#define CNA_CNB_CRC32C_SEED UINT32_C(0)

/* --- read limits --------------------------------------------------------------------------- */

/** @brief Version of @ref CNA_CnbReadLimits this header declares. */
#define CNA_CNB_READ_LIMITS_STRUCT_VERSION UINT32_C(1)

/**
 * @brief Sanity bounds consulted by every count-driven `.cnb` read.
 *
 * A correctly bounds-checked binary reader can still be told, by one corrupted or adversarial count
 * field, to allocate an enormous buffer before any further validation rejects the file. These
 * limits exist to fail fast with a clear message instead of attempting that allocation. They are
 * generous relative to any real asset, not tuned to a fixture.
 *
 * Initialize with @ref cna_cnb_read_limits_init, then lower whatever a caller wants tighter.
 */
typedef struct CNA_CnbReadLimits {
    /** @brief Size of this structure, in bytes. */
    uint32_t struct_size;
    /** @brief Structure version; `CNA_CNB_READ_LIMITS_STRUCT_VERSION`. */
    uint32_t struct_version;
    /** @brief Largest `.cnb` file this reader will open, in bytes. */
    uint64_t max_file_size;
    /** @brief Largest single chunk this reader will accept, in bytes. */
    uint64_t max_chunk_size;
    /**
     * @brief Largest total of every chunk's **logical** (post-decompression) size, in bytes.
     *
     * The per-chunk ceilings alone do not bound a file's total expansion: `max_chunk_size` caps one
     * chunk and `max_chunk_count` caps how many there are, so their product is what a reader
     * without this limit would be willing to allocate for a file of a few kilobytes of individually
     * legal compressed frames. Every chunk counts toward the total, compressed or not.
     *
     * The default is deliberately larger than @ref max_file_size, so compression can genuinely
     * expand a file rather than being cancelled out by this bound.
     */
    uint64_t max_total_uncompressed_size;
    /** @brief Largest number of table-of-contents entries this reader will accept. */
    uint32_t max_chunk_count;
    /** @brief Largest single serialized string this reader will allocate, in bytes. */
    uint32_t max_string_bytes;
    /** @brief Largest element count this reader will reserve for any single serialized array. */
    uint32_t max_array_element_count;
    /** @brief Largest chunk alignment a table-of-contents entry may declare, in bytes. */
    uint32_t max_chunk_alignment;
} CNA_CnbReadLimits;

/**
 * @brief Fills @p out_limits with the process-wide default read limits.
 *
 * @param out_limits Structure whose `struct_size` and `struct_version` the caller has already set;
 *                   every other field is overwritten.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_read_limits_init(CNA_CnbReadLimits* out_limits);

/* --- container identity routes ------------------------------------------------------------- */

/**
 * @brief Copies the four magic bytes a `.cnb` file begins with.
 *
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives `CNA_CNB_FORMAT_MAGIC_SIZE`; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_copy_format_magic(
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Packs four bytes into a chunk identifier.
 *
 * The canonical overload takes `char`, whose signedness is implementation-defined and therefore
 * not an ABI type; it casts each argument to `unsigned char` before packing, so `uint8_t` is the
 * type it actually uses. A character literal still passes unchanged.
 *
 * @param a First byte (lowest-addressed in the file).
 * @param b Second byte.
 * @param c Third byte.
 * @param d Fourth byte.
 * @param out_id Receives the packed identifier.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_make_chunk_id(
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint8_t d,
    CNA_CnbChunkId* out_id);

/**
 * @brief Gets the byte count of a chunk identifier's diagnostic rendering.
 *
 * Always `CNA_CNB_FORMAT_MAGIC_SIZE`; the route exists so a caller can size a buffer the same way
 * it sizes every other text this ABI produces, rather than by remembering a constant.
 *
 * @param id The identifier to render.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_get_chunk_id_string_size(
    CNA_CnbChunkId id,
    uint64_t* out_byte_count);

/**
 * @brief Renders a chunk identifier as its four characters, for diagnostics.
 *
 * Any byte outside printable ASCII is rendered as `?`, so a corrupt identifier cannot inject
 * control characters into a log line. The text is UTF-8 and carries no terminator.
 *
 * @param id The identifier to render.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_copy_chunk_id_string(
    CNA_CnbChunkId id,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Whether every byte of @p id is printable ASCII, as the format requires.
 *
 * @param id The identifier to check.
 * @param out_well_formed Receives `CNA_TRUE` when the identifier is well-formed.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_is_well_formed_chunk_id(
    CNA_CnbChunkId id,
    CNA_Bool* out_well_formed);

/* --- asset type identifiers ---------------------------------------------------------------- */

/**
 * @brief Mints the custom asset type identifier for a game-defined type name.
 *
 * The identifier is `FNV-1a-32(name) | CNA_CNB_ASSET_TYPE_CUSTOM_RANGE_FIRST`, i.e. 31 usable bits,
 * so collisions are possible in principle. The optional `CMET` chunk carries the type name for
 * exactly that reason: a loader can report a mismatch rather than decode the wrong asset.
 *
 * @param name UTF-8 type name, e.g. `"MyGame.Level"`; must not be empty.
 * @param out_asset_type_id Receives the minted identifier.
 * @return A CNA result code; an empty name is `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_cnb_asset_type_id_from_name(
    CNA_StringView name,
    uint32_t* out_asset_type_id);

/**
 * @brief Whether @p asset_type_id lies in the game-defined custom range.
 *
 * @param asset_type_id The identifier to classify.
 * @param out_custom Receives `CNA_TRUE` when the identifier is game-defined.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_is_custom_asset_type_id(
    uint32_t asset_type_id,
    CNA_Bool* out_custom);

/**
 * @brief Gets the byte count of an asset type identifier's human-readable name.
 *
 * @param asset_type_id The identifier to render.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_get_asset_type_name_size(
    uint32_t asset_type_id,
    uint64_t* out_byte_count);

/**
 * @brief Renders an asset type identifier as a human-readable name for diagnostics.
 *
 * A built-in type gives its name; a custom or unrecognized one gives a hexadecimal rendering. The
 * text is UTF-8 and carries no terminator.
 *
 * @param asset_type_id The identifier to render.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_copy_asset_type_name(
    uint32_t asset_type_id,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/* --- external-reference names -------------------------------------------------------------- */

/**
 * @brief Gets the byte count of the description of why @p logical_name is not a legal `.cnb`
 *        external-reference name.
 *
 * **A byte count of zero means the name is acceptable.** That is the whole answer, and it is why
 * there is no separate boolean route to give a second spelling of it.
 *
 * A name is legal when it is non-empty, well-formed UTF-8, relative (does not begin with `/` and is
 * not drive-qualified like `C:`), `/`-separated (no backslash), and contains no `..` segment.
 *
 * **@p logical_name is not validated as UTF-8 by this route, unlike every other input string in
 * this ABI.** Malformed UTF-8 is one of the verdicts this function exists to *report*, so refusing
 * it at the boundary would withhold the answer the caller asked for. Only the pointer/length pair
 * itself is checked.
 *
 * @param logical_name The candidate name, as raw bytes.
 * @param out_byte_count Receives the byte count; zero when the name is acceptable.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_get_logical_name_problem_size(
    CNA_StringView logical_name,
    uint64_t* out_byte_count);

/**
 * @brief Copies the description of why @p logical_name is not a legal external-reference name.
 *
 * Copies nothing and reports a byte count of zero when the name is acceptable. The text is UTF-8
 * and carries no terminator. See @ref cna_cnb_get_logical_name_problem_size for why the input is
 * not itself validated as UTF-8.
 *
 * @param logical_name The candidate name, as raw bytes.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_copy_logical_name_problem(
    CNA_StringView logical_name,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/* --- whole-file arithmetic ----------------------------------------------------------------- */

/**
 * @brief Adds two file-declared 64-bit values, refusing instead of wrapping around.
 *
 * Every `offset + size` computation in a `.cnb` reader combines two values the file itself
 * declares. Unsigned wrap-around is well defined but produces a *small* result from two huge
 * inputs, which then passes a naive `<= file_size` bound check -- the classic way a bounds-checked
 * parser still reads out of range.
 *
 * @param a First addend.
 * @param b Second addend.
 * @param out_sum Receives the exact sum; untouched when the sum is not representable.
 * @return A CNA result code; `CNA_RESULT_OVERFLOW` when the sum does not fit in 64 bits.
 */
CNA_C_API CNA_Result cna_cnb_checked_add(uint64_t a, uint64_t b, uint64_t* out_sum);

/**
 * @brief Multiplies two file-declared 64-bit values, refusing instead of wrapping around.
 *
 * The counterpart to @ref cna_cnb_checked_add for `element_count * element_size` computations.
 *
 * @param a First factor.
 * @param b Second factor.
 * @param out_product Receives the exact product; untouched when it is not representable.
 * @return A CNA result code; `CNA_RESULT_OVERFLOW` when the product does not fit in 64 bits.
 */
CNA_C_API CNA_Result cna_cnb_checked_multiply(uint64_t a, uint64_t b, uint64_t* out_product);

/* --- checksums ----------------------------------------------------------------------------- */

/**
 * @brief Computes the CRC-32C (Castagnoli) checksum used by every `.cnb` header, table of contents
 *        and chunk.
 *
 * Reflected polynomial `0x82F63B78`, initial register `0xFFFFFFFF`, reflected input and output,
 * final XOR `0xFFFFFFFF` -- the same parameter set as iSCSI and SSE4.2's `CRC32C`. The value is
 * bit-identical on every platform CNA targets, which is what a stored-in-the-file checksum
 * requires, whether or not a hardware instruction computed it.
 *
 * This detects **accidental** corruption. It is not a message authentication code and must never be
 * presented as one: anyone who can rewrite a chunk can trivially rewrite its checksum.
 *
 * @param data Bytes to checksum, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @param out_checksum Receives the checksum.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_crc32c(
    const uint8_t* data,
    uint64_t byte_count,
    uint32_t* out_checksum);

/**
 * @brief Continues a running CRC-32C over a further span of bytes.
 *
 * Lets a caller checksum a logically contiguous region that is physically split across two buffers
 * without concatenating them first.
 *
 * @param previous A value returned by a previous call, or `CNA_CNB_CRC32C_SEED` to start.
 * @param data The next bytes of the same logical region, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @param out_checksum Receives the running checksum after appending @p data.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_crc32c_continue(
    uint32_t previous,
    const uint8_t* data,
    uint64_t byte_count,
    uint32_t* out_checksum);

/**
 * @brief Whether this process is folding CRC-32C with a hardware instruction.
 *
 * Detected once at runtime rather than chosen at build time, so one binary runs on a machine with
 * the instruction and one without. The *result* is identical either way, so nothing about
 * correctness depends on this -- but a benchmark that does not know which path it measured is a
 * benchmark that will eventually mislead someone.
 *
 * @param out_uses_hardware Receives `CNA_TRUE` when the SSE4.2 or ARMv8 CRC32 path is in use.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_crc32c_uses_hardware(CNA_Bool* out_uses_hardware);

/**
 * @brief The portable table-driven CRC-32C, bypassing any hardware path.
 *
 * The definition of correct, kept reachable so a caller can prove the hardware path agrees with it
 * rather than merely agreeing with itself.
 *
 * @param data Bytes to checksum, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @param out_checksum Receives the checksum.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_crc32c_portable(
    const uint8_t* data,
    uint64_t byte_count,
    uint32_t* out_checksum);

/* --- chunk compression --------------------------------------------------------------------- */

/**
 * @brief Whether this build can compress and decompress @p codec.
 *
 * `CNA_CNB_COMPRESSION_NONE` is always supported. Zstandard depends on a build option that is
 * automatic and quietly off when the system library is missing, so ask rather than assume. An
 * identity outside the named set is not an error here -- it answers `CNA_FALSE`, because "this
 * build does not implement it" is exactly true of a codec that does not exist.
 *
 * @param codec The codec to query.
 * @param out_supported Receives `CNA_TRUE` when this build implements the codec both ways.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_is_compression_supported(
    CNA_CnbCompression codec,
    CNA_Bool* out_supported);

/**
 * @brief Gets the byte count of a codec identity's diagnostic name.
 *
 * @param codec The codec to render.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_get_compression_name_size(
    CNA_CnbCompression codec,
    uint64_t* out_byte_count);

/**
 * @brief Renders a codec identity for diagnostics.
 *
 * A named codec gives its name; any other value gives `unknown codec N`, so a corrupt
 * table-of-contents entry still produces a readable line. The text is UTF-8 and carries no
 * terminator.
 *
 * @param codec The codec to render.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_copy_compression_name(
    CNA_CnbCompression codec,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the byte count @ref cna_cnb_copy_compressed would produce for the same input.
 *
 * Compression is performed to answer this, exactly as encoding an image is to answer its own byte
 * count, so a caller that already has a large enough buffer should call
 * @ref cna_cnb_copy_compressed directly rather than sizing first.
 *
 * @param raw Bytes to compress, or null only for a zero count.
 * @param raw_byte_count Number of input bytes.
 * @param codec The codec to use; `CNA_CNB_COMPRESSION_NONE` answers @p raw_byte_count.
 * @param level Codec-specific effort. For Zstandard, 1-19; 3 is the measured sweet spot. A value
 *              outside the codec's range is **clamped**, not refused, matching the canonical
 *              implementation.
 * @param out_byte_count Receives the compressed byte count.
 * @return A CNA result code; a codec this build does not implement is
 *         `CNA_RESULT_NOT_SUPPORTED`.
 */
CNA_C_API CNA_Result cna_cnb_get_compressed_byte_count(
    const uint8_t* raw,
    uint64_t raw_byte_count,
    CNA_CnbCompression codec,
    int32_t level,
    uint64_t* out_byte_count);

/**
 * @brief Compresses a chunk payload into a caller-owned buffer.
 *
 * @param raw Bytes to compress, or null only for a zero count.
 * @param raw_byte_count Number of input bytes.
 * @param codec The codec to use; `CNA_CNB_COMPRESSION_NONE` copies @p raw unchanged.
 * @param level Codec-specific effort; clamped to the codec's range rather than refused.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write, and a codec this
 *         build does not implement is `CNA_RESULT_NOT_SUPPORTED`.
 */
CNA_C_API CNA_Result cna_cnb_copy_compressed(
    const uint8_t* raw,
    uint64_t raw_byte_count,
    CNA_CnbCompression codec,
    int32_t level,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Decompresses a stored chunk payload, which must expand to exactly @p uncompressed_size.
 *
 * The exact-size requirement is the whole safety story. @p uncompressed_size comes from a file's
 * table of contents, so it is attacker-controlled: it is checked against @p max_uncompressed_size
 * **before anything is allocated**, which is what stops a few kilobytes of hostile input from
 * asking for gigabytes. The codec is then required to produce exactly that many bytes, so a stream
 * that expands to a different size is a corrupt file rather than a short read someone later treats
 * as data.
 *
 * @param stored The chunk's stored bytes, or null only for a zero count.
 * @param stored_byte_count Number of stored bytes.
 * @param codec The codec named by the chunk's table-of-contents entry.
 * @param uncompressed_size The size the chunk must expand to.
 * @param max_uncompressed_size The configured ceiling, normally
 *                              `CNA_CnbReadLimits::max_chunk_size`.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code. A codec this build does not implement is `CNA_RESULT_NOT_SUPPORTED`;
 *         an @p uncompressed_size above the ceiling is `CNA_RESULT_INVALID_ARGUMENT`, refused
 *         before any allocation; a stream that fails to decode or expands to a different size is
 *         `CNA_RESULT_IO`; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_copy_decompressed(
    const uint8_t* stored,
    uint64_t stored_byte_count,
    CNA_CnbCompression codec,
    uint64_t uncompressed_size,
    uint64_t max_uncompressed_size,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/* --- handles ------------------------------------------------------------------------------- */

/**
 * @brief A bounded, little-endian cursor over one region of a `.cnb` file.
 *
 * Every read is checked against the region's end before a byte is touched, and a truncation is a
 * `CNA_RESULT_IO` naming the region and the offset -- never undefined behaviour and never a
 * silently truncated value. Integers are assembled from individual bytes and floats are produced
 * from an explicitly little-endian integer, so a decoded value does not depend on the host's byte
 * order or floating-point storage order.
 *
 * **Where the bytes come from decides the lifetime.** A reader from
 * @ref cna_cnb_document_open_chunk borrows its document's bytes and keeps that document alive; a
 * reader from @ref cna_cnb_reader_create copies the bytes it is given, so the caller's buffer need
 * not outlive it. The canonical cursor never copies, and this is the one place the C form deviates:
 * a C caller has no way to be told "keep that buffer alive until you destroy this", so the ABI
 * takes the copy rather than leaving a lifetime rule it cannot police.
 */
typedef CNA_Handle CNA_CnbReaderHandle;

/* --- the parsed document -------------------------------------------------------------------- */

/**
 * @brief A parsed, fully validated `.cnb` container.
 *
 * A document that exists is a container that is structurally sound: parsing applies every container
 * invariant -- magic, versions, reserved-field zeroing, both structural checksums, every chunk
 * checksum, overflow-safe offset arithmetic, alignment, table-of-contents ordering, exact
 * non-overlapping coverage of the file, and zeroed alignment padding -- before any accessor can
 * hand out a byte. A schema decoder only has to worry about its own contents.
 *
 * The document owns its bytes and is immutable once parsed. A reader opened from it with
 * @ref cna_cnb_document_open_chunk **borrows** those bytes: the document is kept alive and
 * @ref cna_cnb_document_destroy is refused until every such reader is destroyed.
 */
typedef CNA_Handle CNA_CnbDocumentHandle;

/** @brief Version of @ref CNA_CnbChunkEntry this header declares. */
#define CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION UINT32_C(1)

/** @brief One parsed table-of-contents entry. */
typedef struct CNA_CnbChunkEntry {
    /** @brief Size of this structure, in bytes. */
    uint32_t struct_size;
    /** @brief Structure version; `CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION`. */
    uint32_t struct_version;
    /** @brief Absolute byte offset of the chunk's stored bytes within the file. */
    uint64_t offset;
    /** @brief Number of bytes the chunk occupies in the file. */
    uint64_t stored_size;
    /**
     * @brief Number of bytes the chunk expands to once decompressed -- its **logical** size.
     *
     * Equal to @ref stored_size exactly when @ref compression is `CNA_CNB_COMPRESSION_NONE`, which
     * the reader requires. This is the size @ref cna_cnb_document_copy_chunk_data produces.
     */
    uint64_t uncompressed_size;
    /** @brief The chunk's four-character identifier. */
    CNA_CnbChunkId type;
    /** @brief Per-chunk flags; see `CNA_CNB_CHUNK_FLAG_*`. */
    uint32_t flags;
    /** @brief CRC-32C of the chunk's stored bytes. */
    uint32_t checksum;
    /** @brief The codec used to store the chunk. */
    CNA_CnbCompression compression;
    /** @brief Power-of-two alignment, in bytes, that @ref offset satisfies. */
    uint32_t alignment;
    /** @brief Reserved; always written as zero. */
    uint32_t reserved;
} CNA_CnbChunkEntry;

/** @brief Version of @ref CNA_CnbExternalReference this header declares. */
#define CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION UINT32_C(1)

/**
 * @brief One entry of a `.cnb` file's optional `XREF` external-reference table, without its name.
 *
 * The logical name is a string, so it does not live in the structure: read it with
 * @ref cna_cnb_document_get_external_reference_name_size and
 * @ref cna_cnb_document_copy_external_reference_name, or supply it beside this value when writing.
 */
typedef struct CNA_CnbExternalReference {
    /** @brief Size of this structure, in bytes. */
    uint32_t struct_size;
    /** @brief Structure version; `CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION`. */
    uint32_t struct_version;
    /** @brief Reserved for future use; must be zero in CNB v1. */
    uint32_t flags;
    /**
     * @brief The asset type the referring schema expects at this logical name, or
     *        `CNA_CNB_ASSET_TYPE_INVALID` when the schema does not constrain it.
     */
    uint32_t expected_asset_type_id;
} CNA_CnbExternalReference;

/** @brief Version of @ref CNA_CnbMetadata this header declares. */
#define CNA_CNB_METADATA_STRUCT_VERSION UINT32_C(1)

/**
 * @brief The `CMET` chunk's flags and presence, without its two strings.
 *
 * The names are read with `cna_cnb_document_*_metadata_asset_type_name*` and
 * `..._content_name*`. `present` being false is valid for a built-in asset type and invalid for a
 * custom one, whose canonical name is part of loader identity rather than a label.
 */
typedef struct CNA_CnbMetadata {
    /** @brief Size of this structure, in bytes. */
    uint32_t struct_size;
    /** @brief Structure version; `CNA_CNB_METADATA_STRUCT_VERSION`. */
    uint32_t struct_version;
    /** @brief Whether a `CMET` chunk was present at all. */
    CNA_Bool present;
    /** @brief Reserved; always written as zero. */
    uint8_t reserved[3];
    /** @brief Reserved for future use; must be zero in CNB v1. */
    uint32_t flags;
} CNA_CnbMetadata;

/**
 * @brief Whether a chunk entry carries the mandatory flag.
 *
 * A mandatory chunk a decoder does not understand costs the whole file; see
 * @ref cna_cnb_document_require_mandatory_chunks_understood.
 *
 * @param entry The entry to inspect.
 * @param out_mandatory Receives `CNA_TRUE` when the entry is mandatory.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_chunk_entry_is_mandatory(
    const CNA_CnbChunkEntry* entry,
    CNA_Bool* out_mandatory);

/**
 * @brief Whether @p bytes begins with the `.cnb` magic.
 *
 * A cheap pre-check for a tool that inspects files by content rather than extension. It says
 * nothing about whether the rest of the file is valid.
 *
 * @param bytes Bytes to inspect, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @param out_has_magic Receives `CNA_TRUE` when the first four bytes are the CNB magic.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_has_magic(
    const uint8_t* bytes,
    uint64_t byte_count,
    CNA_Bool* out_has_magic);

/**
 * @brief Parses and validates a complete `.cnb` byte image.
 *
 * The bytes are copied into the document, which then owns them.
 *
 * @param bytes The whole file, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @param origin Name used in diagnostics, normally the file path.
 * @param limits Sanity bounds to apply, or null for the defaults.
 * @param out_document Receives the parsed document.
 * @return A CNA result code; a file violating any container invariant is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_document_parse(
    const uint8_t* bytes,
    uint64_t byte_count,
    CNA_StringView origin,
    const CNA_CnbReadLimits* limits,
    CNA_CnbDocumentHandle* out_document);

/**
 * @brief Reads a `.cnb` file from disk and parses it.
 *
 * The file's size is checked against `CNA_CnbReadLimits::max_file_size` before it is read, so an
 * oversized file is refused without ever being allocated.
 *
 * @param path Filesystem path to read.
 * @param limits Sanity bounds to apply, or null for the defaults.
 * @param out_document Receives the parsed document.
 * @return A CNA result code; an unopenable, oversized or invalid file is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_document_parse_file(
    CNA_StringView path,
    const CNA_CnbReadLimits* limits,
    CNA_CnbDocumentHandle* out_document);

/**
 * @brief Releases a parsed document.
 *
 * @param document The document to release.
 * @return A CNA result code; `CNA_RESULT_INVALID_STATE` while any reader opened from this document
 *         is still alive.
 */
CNA_C_API CNA_Result cna_cnb_document_destroy(CNA_CnbDocumentHandle document);

/**
 * @brief Gets the byte count of the name this document reports in diagnostics.
 *
 * @param document The document.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_origin_size(
    CNA_CnbDocumentHandle document,
    uint64_t* out_byte_count);

/**
 * @brief Copies the name this document reports in diagnostics.
 *
 * @param document The document.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_document_copy_origin(
    CNA_CnbDocumentHandle document,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief The container major version the file declares.
 *
 * @param document The document.
 * @param out_major Receives the major version.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_container_major(
    CNA_CnbDocumentHandle document,
    uint16_t* out_major);

/**
 * @brief The container minor version the file declares.
 *
 * @param document The document.
 * @param out_minor Receives the minor version.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_container_minor(
    CNA_CnbDocumentHandle document,
    uint16_t* out_minor);

/**
 * @brief The asset type the file holds.
 *
 * @param document The document.
 * @param out_asset_type_id Receives the asset type identifier.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_asset_type_id(
    CNA_CnbDocumentHandle document,
    uint32_t* out_asset_type_id);

/**
 * @brief The asset schema version the file was written to.
 *
 * @param document The document.
 * @param out_schema_version Receives the schema version.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_asset_schema_version(
    CNA_CnbDocumentHandle document,
    uint32_t* out_schema_version);

/**
 * @brief Number of chunks in the table of contents.
 *
 * @param document The document.
 * @param out_count Receives the chunk count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_chunk_count(
    CNA_CnbDocumentHandle document,
    uint64_t* out_count);

/**
 * @brief The table-of-contents entry at @p index.
 *
 * @param document The document.
 * @param index Zero-based table-of-contents index.
 * @param out_entry Structure whose `struct_size` and `struct_version` the caller has already set.
 * @return A CNA result code; an out-of-range index is `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_cnb_document_get_chunk(
    CNA_CnbDocumentHandle document,
    uint64_t index,
    CNA_CnbChunkEntry* out_entry);

/**
 * @brief Copies the **logical** bytes of the chunk at @p index.
 *
 * Decompressed when the chunk was stored compressed, and exactly
 * `CNA_CnbChunkEntry::uncompressed_size` bytes long either way. Every chunk is expanded once at
 * parse time, so this copy is from memory the document already holds.
 *
 * @param document The document.
 * @param index Zero-based table-of-contents index.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write, and an out-of-range
 *         index is `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_cnb_document_copy_chunk_data(
    CNA_CnbDocumentHandle document,
    uint64_t index,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Opens a bounded reader positioned at the start of the chunk at @p index.
 *
 * The reader **borrows** the document's bytes rather than copying them, so it keeps the document
 * alive and @ref cna_cnb_document_destroy is refused until every reader opened from this document
 * is destroyed.
 *
 * @param document The document.
 * @param index Zero-based table-of-contents index.
 * @param out_reader Receives the reader.
 * @return A CNA result code; an out-of-range index is `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_cnb_document_open_chunk(
    CNA_CnbDocumentHandle document,
    uint64_t index,
    CNA_CnbReaderHandle* out_reader);

/**
 * @brief Every table-of-contents index whose entry has type @p type, in file order.
 *
 * @param document The document.
 * @param type The chunk identifier to look for.
 * @param destination Destination indices, or null only for zero capacity.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives the number of matches; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_document_find_all(
    CNA_CnbDocumentHandle document,
    CNA_CnbChunkId type,
    uint64_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief The single chunk of type @p type, if the file has exactly one.
 *
 * Absence is an ordinary answer rather than a failure, so it is reported through @p out_found. More
 * than one is a malformed file.
 *
 * @param document The document.
 * @param type The chunk identifier to look for.
 * @param out_found Receives `CNA_TRUE` when the file has exactly one such chunk.
 * @param out_index Receives its index when @p out_found is `CNA_TRUE`; untouched otherwise.
 * @return A CNA result code; more than one such chunk is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_document_find_single(
    CNA_CnbDocumentHandle document,
    CNA_CnbChunkId type,
    CNA_Bool* out_found,
    uint64_t* out_index);

/**
 * @brief The single chunk of type @p type, which must be present exactly once.
 *
 * @param document The document.
 * @param type The chunk identifier to look for.
 * @param out_index Receives its index.
 * @return A CNA result code; zero or more than one such chunk is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_document_require_single(
    CNA_CnbDocumentHandle document,
    CNA_CnbChunkId type,
    uint64_t* out_index);

/**
 * @brief Enforces the mandatory-chunk rule for a schema decoder.
 *
 * A chunk carrying the mandatory flag whose identifier is neither container-defined nor listed in
 * @p known_types means the file relies on something this build cannot honour, so the whole file is
 * refused. A mandatory chunk that *is* known, and any non-mandatory chunk, passes -- an unknown
 * optional chunk is simply ignored, which is what lets a newer writer add data an older reader can
 * safely skip.
 *
 * @param document The document.
 * @param known_types Every chunk identifier this decoder understands, or null only for a zero count.
 * @param known_type_count Number of identifiers.
 * @return A CNA result code; an unknown mandatory chunk is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_document_require_mandatory_chunks_understood(
    CNA_CnbDocumentHandle document,
    const CNA_CnbChunkId* known_types,
    uint64_t known_type_count);

/**
 * @brief The file's `CMET` metadata flags and presence.
 *
 * @param document The document.
 * @param out_metadata Structure whose `struct_size` and `struct_version` the caller has already set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_metadata(
    CNA_CnbDocumentHandle document,
    CNA_CnbMetadata* out_metadata);

/**
 * @brief Gets the byte count of the asset type's canonical name from the `CMET` chunk.
 *
 * Zero when the file has no `CMET` chunk. **Load-bearing for a custom asset type, diagnostic for a
 * built-in one**: a custom identifier is a 31-bit hash of the name, so a numeric match is only a
 * candidate and the name settles it.
 *
 * @param document The document.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_metadata_asset_type_name_size(
    CNA_CnbDocumentHandle document,
    uint64_t* out_byte_count);

/**
 * @brief Copies the asset type's canonical name from the `CMET` chunk.
 *
 * @param document The document.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_document_copy_metadata_asset_type_name(
    CNA_CnbDocumentHandle document,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the byte count of the logical content name the compiler was given.
 *
 * Provenance only -- nothing reads it to make a decision -- and zero when absent.
 *
 * @param document The document.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_metadata_content_name_size(
    CNA_CnbDocumentHandle document,
    uint64_t* out_byte_count);

/**
 * @brief Copies the logical content name the compiler was given.
 *
 * @param document The document.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_document_copy_metadata_content_name(
    CNA_CnbDocumentHandle document,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Number of entries in the file's `XREF` external-reference table.
 *
 * @param document The document.
 * @param out_count Receives the entry count; zero when the file has no `XREF` chunk.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_external_reference_count(
    CNA_CnbDocumentHandle document,
    uint64_t* out_count);

/**
 * @brief One external-reference entry's flags and expected asset type.
 *
 * @param document The document.
 * @param index Index into the `XREF` table.
 * @param what_for_diagnostics Noun naming what referred to it, used in the diagnostic; may be empty.
 * @param out_reference Structure whose `struct_size` and `struct_version` the caller has set.
 * @return A CNA result code; an out-of-range index is `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_cnb_document_get_external_reference(
    CNA_CnbDocumentHandle document,
    uint64_t index,
    CNA_StringView what_for_diagnostics,
    CNA_CnbExternalReference* out_reference);

/**
 * @brief Gets the byte count of one external reference's logical name.
 *
 * @param document The document.
 * @param index Index into the `XREF` table.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code; an out-of-range index is `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_cnb_document_get_external_reference_name_size(
    CNA_CnbDocumentHandle document,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies one external reference's logical name.
 *
 * Always relative, always `/`-separated, never containing a `..` segment -- parsing enforces all
 * three before the name can reach any path-resolution code.
 *
 * @param document The document.
 * @param index Index into the `XREF` table.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_document_copy_external_reference_name(
    CNA_CnbDocumentHandle document,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Requires the file's asset type and schema version to be what a decoder expects.
 *
 * @param document The document.
 * @param expected_asset_type_id The asset type the caller is prepared to decode.
 * @param max_schema_version The highest schema version the caller understands; version 1 is always
 *                           the lowest accepted.
 * @return A CNA result code; a mismatch or an out-of-range schema version is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_document_require_asset(
    CNA_CnbDocumentHandle document,
    uint32_t expected_asset_type_id,
    uint32_t max_schema_version);

/**
 * @brief The limits this document was parsed with.
 *
 * @param document The document.
 * @param out_limits Structure whose `struct_size` and `struct_version` the caller has already set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_document_get_limits(
    CNA_CnbDocumentHandle document,
    CNA_CnbReadLimits* out_limits);

/* --- the bounded reader --------------------------------------------------------------------- */

/**
 * @brief Creates a cursor over a copy of @p data.
 *
 * @param data The region to read, or null only for a zero count. Copied.
 * @param byte_count Number of bytes.
 * @param context Text prefixed to every diagnostic, naming the region (e.g. `"'walk.cnb' chunk ACLK"`).
 * @param limits Sanity bounds applied to string lengths and element counts, or null for the defaults.
 * @param out_reader Receives the cursor.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_reader_create(
    const uint8_t* data,
    uint64_t byte_count,
    CNA_StringView context,
    const CNA_CnbReadLimits* limits,
    CNA_CnbReaderHandle* out_reader);

/**
 * @brief Releases a cursor, and with it any borrow it holds on a document.
 *
 * @param reader The cursor to release.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_reader_destroy(CNA_CnbReaderHandle reader);

/**
 * @brief Number of bytes not yet consumed.
 *
 * @param reader The cursor.
 * @param out_remaining Receives the remaining byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_reader_get_remaining(
    CNA_CnbReaderHandle reader,
    uint64_t* out_remaining);

/**
 * @brief Current read offset within the region.
 *
 * @param reader The cursor.
 * @param out_position Receives the offset in bytes from the start of the region.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_reader_get_position(
    CNA_CnbReaderHandle reader,
    uint64_t* out_position);

/**
 * @brief Total size of the region.
 *
 * @param reader The cursor.
 * @param out_size Receives the region size in bytes.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_reader_get_size(
    CNA_CnbReaderHandle reader,
    uint64_t* out_size);

/**
 * @brief Gets the byte count of the context string this cursor prefixes to its diagnostics.
 *
 * @param reader The cursor.
 * @param out_byte_count Receives the byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_reader_get_context_size(
    CNA_CnbReaderHandle reader,
    uint64_t* out_byte_count);

/**
 * @brief Copies the context string this cursor prefixes to its diagnostics.
 *
 * @param reader The cursor.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_reader_copy_context(
    CNA_CnbReaderHandle reader,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Reads one unsigned byte.
 * @param reader The cursor.
 * @param out_value Receives the byte; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_u8(CNA_CnbReaderHandle reader, uint8_t* out_value);

/**
 * @brief Reads a little-endian unsigned 16-bit integer.
 * @param reader The cursor.
 * @param out_value Receives the value; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_u16(CNA_CnbReaderHandle reader, uint16_t* out_value);

/**
 * @brief Reads a little-endian unsigned 32-bit integer.
 * @param reader The cursor.
 * @param out_value Receives the value; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_u32(CNA_CnbReaderHandle reader, uint32_t* out_value);

/**
 * @brief Reads a little-endian unsigned 64-bit integer.
 * @param reader The cursor.
 * @param out_value Receives the value; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_u64(CNA_CnbReaderHandle reader, uint64_t* out_value);

/**
 * @brief Reads a little-endian two's-complement signed 32-bit integer.
 * @param reader The cursor.
 * @param out_value Receives the value; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_i32(CNA_CnbReaderHandle reader, int32_t* out_value);

/**
 * @brief Reads a little-endian IEEE-754 binary32 value.
 * @param reader The cursor.
 * @param out_value Receives the value; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_f32(CNA_CnbReaderHandle reader, float* out_value);

/**
 * @brief Reads a little-endian IEEE-754 binary64 value.
 * @param reader The cursor.
 * @param out_value Receives the value; untouched on truncation.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_f64(CNA_CnbReaderHandle reader, double* out_value);

/**
 * @brief Reads a length-prefixed UTF-8 string and holds it for @ref cna_cnb_reader_copy_string.
 *
 * **Reading is destructive and copying is not, so they are two routes.** A single route taking a
 * destination buffer could not report a capacity that was too small without either losing the
 * string it had already consumed or consuming it twice; this reads once, reports the size, and
 * keeps the decoded bytes in the cursor until the next read replaces them.
 *
 * The declared length is checked against `CNA_CnbReadLimits::max_string_bytes` and against the
 * region's remaining size before any allocation, and the bytes are validated as well-formed UTF-8.
 * A `.cnb` string can end up as a filesystem path or an effect name, so letting malformed UTF-8
 * through would push the problem into code far less prepared for it.
 *
 * @param reader The cursor.
 * @param out_byte_count Receives the decoded string's byte count.
 * @return A CNA result code; truncation, an over-long declared length or malformed UTF-8 is
 *         `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_string(
    CNA_CnbReaderHandle reader,
    uint64_t* out_byte_count);

/**
 * @brief Copies the string most recently read by @ref cna_cnb_reader_read_string.
 *
 * Non-destructive, so it may be called twice -- once to size and once to copy. The text is UTF-8
 * and carries no terminator.
 *
 * @param reader The cursor.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write, and calling this
 *         before any successful read is `CNA_RESULT_INVALID_STATE`.
 */
CNA_C_API CNA_Result cna_cnb_reader_copy_string(
    CNA_CnbReaderHandle reader,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Reads an element count and checks it against the limits and against what could fit.
 *
 * @param reader The cursor.
 * @param element_size Size in bytes of one element that follows the count. Pass 0 when the elements
 *                     are variable-length, which skips the fit check.
 * @param what_is_being_counted Noun used in the diagnostic (e.g. `"tracks"`); may be empty.
 * @param out_count Receives the validated element count; untouched on refusal.
 * @return A CNA result code; a count above the limit or one that cannot fit in the remaining bytes
 *         is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_count(
    CNA_CnbReaderHandle reader,
    uint64_t element_size,
    CNA_StringView what_is_being_counted,
    uint32_t* out_count);

/**
 * @brief Copies the next @p byte_count bytes and advances past them.
 *
 * The size is an input rather than something the file declares, so a capacity too small to hold it
 * is refused **before** the cursor advances: a refused call consumes nothing.
 *
 * @param reader The cursor.
 * @param byte_count Number of bytes to take.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives @p byte_count; always written on a valid output.
 * @return A CNA result code; insufficient capacity is `CNA_RESULT_BUFFER_TOO_SMALL` and consumes
 *         nothing, and truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_read_bytes(
    CNA_CnbReaderHandle reader,
    uint64_t byte_count,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Advances the cursor without reading.
 *
 * @param reader The cursor.
 * @param byte_count Number of bytes to skip.
 * @return A CNA result code; truncation is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_skip(CNA_CnbReaderHandle reader, uint64_t byte_count);

/**
 * @brief Requires that the region has been consumed exactly.
 *
 * Used at the end of every fixed-layout chunk decoder: trailing bytes mean the file and the decoder
 * disagree about the layout, which must be an error rather than something silently ignored.
 *
 * @param reader The cursor.
 * @return A CNA result code; remaining bytes are `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_reader_require_exhausted(CNA_CnbReaderHandle reader);

/**
 * @brief Fails with a diagnostic built from this cursor's context, its current offset and @p detail.
 *
 * Always fails; it exists so a schema decoder's own refusals read exactly like the cursor's.
 *
 * @param reader The cursor.
 * @param detail Description of what was wrong.
 * @return `CNA_RESULT_IO`, or an argument error when @p reader or @p detail is invalid.
 */
CNA_C_API CNA_Result cna_cnb_reader_fail(CNA_CnbReaderHandle reader, CNA_StringView detail);

/**
 * @brief Whether the bytes of @p text are well-formed UTF-8.
 *
 * Published separately from the string read so a writer can reject a malformed string before
 * committing it to a file, keeping both ends of the format honest. The input is **not** validated
 * by the ABI before the check, for the same reason
 * @ref cna_cnb_get_logical_name_problem_size does not: the answer is the point of the call.
 *
 * @param text Bytes to validate.
 * @param out_well_formed Receives `CNA_TRUE` when @p text is well-formed UTF-8.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_is_well_formed_utf8(
    CNA_StringView text,
    CNA_Bool* out_well_formed);

/* --- the primitive writer ------------------------------------------------------------------- */

/**
 * @brief Emits `.cnb` primitives in their canonical little-endian encoding.
 *
 * The exact counterpart of the reader: integers are decomposed into individual bytes and floats go
 * through an integer first, so the bytes produced never depend on the host's byte order or
 * floating-point storage order. Nothing here consults the clock, a random source or a pointer
 * value, which is what makes a built `.cnb` byte-deterministic.
 */
typedef CNA_Handle CNA_CnbByteWriterHandle;

/**
 * @brief Creates an empty primitive writer.
 * @param out_writer Receives the writer.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_create(CNA_CnbByteWriterHandle* out_writer);

/**
 * @brief Creates a primitive writer that appends to a copy of an existing buffer.
 *
 * @param initial Bytes already written, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @param out_writer Receives the writer.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_create_from_bytes(
    const uint8_t* initial,
    uint64_t byte_count,
    CNA_CnbByteWriterHandle* out_writer);

/**
 * @brief Releases a primitive writer.
 * @param writer The writer to release.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_destroy(CNA_CnbByteWriterHandle writer);

/**
 * @brief Appends one byte.
 * @param writer The writer.
 * @param value The byte to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_u8(
    CNA_CnbByteWriterHandle writer,
    uint8_t value);

/**
 * @brief Appends a little-endian unsigned 16-bit integer.
 * @param writer The writer.
 * @param value The value to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_u16(
    CNA_CnbByteWriterHandle writer,
    uint16_t value);

/**
 * @brief Appends a little-endian unsigned 32-bit integer.
 * @param writer The writer.
 * @param value The value to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_u32(
    CNA_CnbByteWriterHandle writer,
    uint32_t value);

/**
 * @brief Appends a little-endian unsigned 64-bit integer.
 * @param writer The writer.
 * @param value The value to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_u64(
    CNA_CnbByteWriterHandle writer,
    uint64_t value);

/**
 * @brief Appends a little-endian two's-complement signed 32-bit integer.
 * @param writer The writer.
 * @param value The value to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_i32(
    CNA_CnbByteWriterHandle writer,
    int32_t value);

/**
 * @brief Appends a little-endian IEEE-754 binary32 value.
 * @param writer The writer.
 * @param value The value to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_f32(
    CNA_CnbByteWriterHandle writer,
    float value);

/**
 * @brief Appends a little-endian IEEE-754 binary64 value.
 * @param writer The writer.
 * @param value The value to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_f64(
    CNA_CnbByteWriterHandle writer,
    double value);

/**
 * @brief Appends a byte length followed by @p value's UTF-8 bytes.
 *
 * @param writer The writer.
 * @param value The string to append; must be well-formed UTF-8 and no longer than 4294967295 bytes.
 * @return A CNA result code; a malformed or over-long string is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_string(
    CNA_CnbByteWriterHandle writer,
    CNA_StringView value);

/**
 * @brief Appends raw bytes verbatim.
 *
 * @param writer The writer.
 * @param bytes The bytes to append, or null only for a zero count.
 * @param byte_count Number of bytes.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_bytes(
    CNA_CnbByteWriterHandle writer,
    const uint8_t* bytes,
    uint64_t byte_count);

/**
 * @brief Appends @p byte_count zero bytes.
 *
 * @param writer The writer.
 * @param byte_count Number of zero bytes to append.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_write_zeros(
    CNA_CnbByteWriterHandle writer,
    uint64_t byte_count);

/**
 * @brief Number of bytes written so far.
 *
 * @param writer The writer.
 * @param out_size Receives the current buffer size.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_get_size(
    CNA_CnbByteWriterHandle writer,
    uint64_t* out_size);

/**
 * @brief Copies everything written so far, leaving the writer unchanged.
 *
 * @param writer The writer.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_copy_bytes(
    CNA_CnbByteWriterHandle writer,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Takes the written bytes, leaving the writer empty.
 *
 * A capacity too small is refused **before** the writer is emptied, so a refused call takes nothing
 * and the bytes are still there to ask for again.
 *
 * @param writer The writer.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity is `CNA_RESULT_BUFFER_TOO_SMALL` and takes
 *         nothing.
 */
CNA_C_API CNA_Result cna_cnb_byte_writer_take(
    CNA_CnbByteWriterHandle writer,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/* --- the container writer ------------------------------------------------------------------- */

/**
 * @brief Builds a complete, valid `.cnb` byte image.
 *
 * Deterministic by construction: it reads no clock, no random source and no pointer value, it emits
 * chunks in the order they were added, it lays the table of contents out in that same order, and it
 * zero-fills every alignment gap. Given identical inputs it produces byte-identical output.
 *
 * The container-level `CMET` and `XREF` chunks are always emitted first, ahead of the schema's own
 * chunks, regardless of when they were set. A schema must therefore address another's chunks by
 * ordinal within a chunk type (@ref cna_cnb_document_find_all), never by table-of-contents index.
 */
typedef CNA_Handle CNA_CnbWriterHandle;

/**
 * @brief Starts a new `.cnb` image for one asset.
 *
 * @param asset_type_id The asset type this file will declare; must not be
 *                      `CNA_CNB_ASSET_TYPE_INVALID`.
 * @param asset_schema_version The schema version this file will declare; must be at least 1.
 * @param out_writer Receives the writer.
 * @return A CNA result code; either argument out of range is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_writer_create(
    uint32_t asset_type_id,
    uint32_t asset_schema_version,
    CNA_CnbWriterHandle* out_writer);

/**
 * @brief Releases a container writer.
 * @param writer The writer to release.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_destroy(CNA_CnbWriterHandle writer);

/**
 * @brief Sets the `CMET` metadata chunk.
 *
 * Diagnostic for a **built-in** asset type, whose numeric identifier CNA assigns and freezes. Not
 * optional for a **custom** one: a custom identifier is a 31-bit hash of the type name, so the load
 * path proves identity by comparing this name against the one the loader was registered under, and
 * @ref cna_cnb_writer_build refuses a custom-typed file without it.
 *
 * @param writer The writer.
 * @param asset_type_name The type's canonical name, e.g. `"Microsoft.Xna.Framework.Curve"`.
 * @param content_name The logical content name being compiled, or empty.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_set_metadata(
    CNA_CnbWriterHandle writer,
    CNA_StringView asset_type_name,
    CNA_StringView content_name);

/**
 * @brief Appends one entry to the optional `XREF` external-reference table.
 *
 * The canonical setter takes the whole table at once, which C cannot express as one argument
 * because each entry carries a string; appending one at a time is the same operation with the
 * order the schema's own indices expect preserved by the call order.
 *
 * Each name is validated when the file is assembled, against the container's own rule -- the same
 * function the reader applies. Sharing the rule is what stops the writer producing a file its own
 * reader would refuse.
 *
 * @param writer The writer.
 * @param reference The entry's flags and expected asset type.
 * @param logical_name The referenced asset's logical name.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_add_external_reference(
    CNA_CnbWriterHandle writer,
    const CNA_CnbExternalReference* reference,
    CNA_StringView logical_name);

/**
 * @brief Empties the `XREF` external-reference table.
 *
 * With @ref cna_cnb_writer_add_external_reference this is the canonical whole-table setter: clear,
 * then append in order.
 *
 * @param writer The writer.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_clear_external_references(CNA_CnbWriterHandle writer);

/**
 * @brief Appends one schema chunk.
 *
 * The container-defined identifiers `CMET` and `XREF` are **refused** here: the writer emits each
 * of them at most once, and a schema adding one as an ordinary chunk would produce a file carrying
 * two of a singleton the reader requires to be unique.
 *
 * @param writer The writer.
 * @param type The chunk's identifier; every byte must be printable ASCII, and it must not be
 *             container-defined.
 * @param data The chunk's bytes, or null only for a zero count. Copied.
 * @param byte_count Number of bytes.
 * @param flags Chunk flags; see `CNA_CNB_CHUNK_FLAG_*`.
 * @param alignment Power-of-two byte alignment the chunk's offset will satisfy.
 * @return A CNA result code; a malformed or container-defined identifier, or invalid flags or
 *         alignment, is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_writer_add_chunk(
    CNA_CnbWriterHandle writer,
    CNA_CnbChunkId type,
    const uint8_t* data,
    uint64_t byte_count,
    uint32_t flags,
    uint32_t alignment);

/**
 * @brief Number of schema chunks added so far, excluding the container-level ones.
 *
 * @param writer The writer.
 * @param out_count Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_get_schema_chunk_count(
    CNA_CnbWriterHandle writer,
    uint64_t* out_count);

/**
 * @brief Compresses this document's schema chunks with @p codec.
 *
 * Off by default. A chunk is emitted compressed only when compression actually made it **smaller**;
 * otherwise it is stored, because a chunk that grew would cost both bytes and decompression time.
 * That decision is per chunk. Container-level chunks are always stored, because a codec is pure
 * overhead on a chunk that small.
 *
 * Enabling this raises the file's minimum runtime: a build without the codec cannot open the file
 * at all, not even to read its identity, because parsing refuses an unimplemented codec while
 * reading the table of contents.
 *
 * @param writer The writer.
 * @param codec The codec to apply. `CNA_CNB_COMPRESSION_NONE` restores the default.
 * @param level Codec-specific effort; for Zstandard, 1-19. 3 is the measured sweet spot.
 * @return A CNA result code; a codec this build does not implement is `CNA_RESULT_NOT_SUPPORTED`.
 */
CNA_C_API CNA_Result cna_cnb_writer_set_compression(
    CNA_CnbWriterHandle writer,
    CNA_CnbCompression codec,
    int32_t level);

/**
 * @brief Bounds the file this writer will produce, so it cannot exceed what a reader with @p limits
 *        will open.
 *
 * It matters because compression breaks the intuition that a file a writer built is a file a reader
 * can open: a highly compressible document serializes to very little and expands to a great deal,
 * so without this it could build successfully and then be refused by a default parse for exceeding
 * the aggregate expansion budget. The producer is the right place to find that out.
 *
 * @param writer The writer.
 * @param limits The reader limits this writer must stay inside.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_set_limits(
    CNA_CnbWriterHandle writer,
    const CNA_CnbReadLimits* limits);

/**
 * @brief The limits the build will enforce; the defaults unless @ref cna_cnb_writer_set_limits says
 *        otherwise.
 *
 * @param writer The writer.
 * @param out_limits Structure whose `struct_size` and `struct_version` the caller has already set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_cnb_writer_get_limits(
    CNA_CnbWriterHandle writer,
    CNA_CnbReadLimits* out_limits);

/**
 * @brief Assembles the finished `.cnb` image into a caller-owned buffer.
 *
 * Every file this produces is loadable by @ref cna_cnb_document_parse: the external-reference names,
 * the chunk identifiers and the custom-type rule are all checked here or at the call that supplied
 * them, so the writer has no path to a file its own reader refuses. That guarantee is bounded by
 * @ref cna_cnb_writer_set_limits as well as by the format.
 *
 * Assembling is non-destructive, so the two-call sizing pattern works: it builds again rather than
 * remembering.
 *
 * @param writer The writer.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write, and a document that
 *         exceeds the format or the configured limits is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_writer_build(
    CNA_CnbWriterHandle writer,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Assembles the image and writes it to @p path.
 *
 * @param writer The writer.
 * @param path Filesystem path to create or overwrite.
 * @return A CNA result code; a file that cannot be written is `CNA_RESULT_IO`.
 */
CNA_C_API CNA_Result cna_cnb_writer_write_to_file(
    CNA_CnbWriterHandle writer,
    CNA_StringView path);

#ifdef __cplusplus
}
#endif

#endif /* CNA_C_CNB_H */
