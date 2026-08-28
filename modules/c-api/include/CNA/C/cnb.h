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

#ifdef __cplusplus
}
#endif

#endif /* CNA_C_CNB_H */
