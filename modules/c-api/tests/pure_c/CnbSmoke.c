// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-106 -- the `.cnb` container's identities, checksums, arithmetic,
 * read limits and chunk compression.
 *
 * This family owns no handle and needs no game, so the whole suite is a plain `main()`. What it
 * must still prove is the part a route count cannot: that every refusal is the *right* refusal and
 * leaves the caller's outputs alone, that a short buffer writes nothing, and that the two
 * behaviours this ABI deliberately inherits rather than smooths over -- a stored chunk consulting
 * neither size, and a logical name whose malformed UTF-8 is an answer rather than an error -- are
 * still what they are.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CnbSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

_Static_assert(sizeof(CNA_CnbChunkId) == sizeof(uint32_t),
               "CNA_CnbChunkId must remain fixed width");
_Static_assert(sizeof(CNA_CnbCompression) == sizeof(uint32_t),
               "CNA_CnbCompression must remain fixed width");
_Static_assert(CNA_CNB_COMPRESSION_NONE == UINT32_C(0) &&
                   CNA_CNB_COMPRESSION_LZ4 == UINT32_C(1) &&
                   CNA_CNB_COMPRESSION_ZSTD == UINT32_C(2) &&
                   CNA_CNB_COMPRESSION_DEFLATE == UINT32_C(3) &&
                   CNA_CNB_COMPRESSION_MAXIMUM == CNA_CNB_COMPRESSION_DEFLATE,
               "CNB codec identifiers are wire format and must not move");
_Static_assert(CNA_CNB_CHUNK_FLAG_ALL == CNA_CNB_CHUNK_FLAG_MANDATORY,
               "CNB defines exactly one chunk flag in container version 1");

static CNA_StringView view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

/* Every route that fills a buffer promises no partial write when the capacity is short, which is
   only checkable against a destination whose prior contents are known. */
static int untouched(const unsigned char* const bytes, const size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        if (bytes[index] != 0x5AU) {
            return 0;
        }
    }
    return 1;
}

static int validate_format_constants(void)
{
    uint8_t magic[4];
    uint8_t sentinel[4];
    uint64_t bytes = UINT64_MAX;

    memset(magic, 0x5A, sizeof(magic));
    memcpy(sentinel, magic, sizeof(sentinel));

    /* A short capacity reports the requirement and writes nothing. */
    REQUIRE(cna_cnb_copy_format_magic(magic, 0U, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == (uint64_t)CNA_CNB_FORMAT_MAGIC_SIZE);
    REQUIRE(memcmp(magic, sentinel, sizeof(magic)) == 0);

    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_format_magic(magic, sizeof(magic), &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == (uint64_t)CNA_CNB_FORMAT_MAGIC_SIZE);
    REQUIRE(magic[0] == 0x43U && magic[1] == 0x4EU && magic[2] == 0x42U && magic[3] == 0x1AU);

    REQUIRE(cna_cnb_copy_format_magic(magic, sizeof(magic), 0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_copy_format_magic(0, 4U, &bytes) == CNA_RESULT_INVALID_ARGUMENT);
    /* A null destination is legal for a zero capacity: that is how a caller asks for the size. */
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_format_magic(0, 0U, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == (uint64_t)CNA_CNB_FORMAT_MAGIC_SIZE);

    /* The offsets and sizes the container's own layout is made of. The checksum covers everything
       before itself, which is what makes the two constants equal rather than a copy-paste. */
    REQUIRE(CNA_CNB_FORMAT_HEADER_SIZE == UINT32_C(64));
    REQUIRE(CNA_CNB_FORMAT_TOC_ENTRY_SIZE == UINT32_C(48));
    REQUIRE(CNA_CNB_FORMAT_HEADER_CHECKSUM_COVERAGE == CNA_CNB_FORMAT_HEADER_CHECKSUM_OFFSET);
    REQUIRE(CNA_CNB_FORMAT_HEADER_CHECKSUM_OFFSET + CNA_CNB_FORMAT_HEADER_RESERVED_SIZE +
                sizeof(uint32_t) == CNA_CNB_FORMAT_HEADER_SIZE);
    REQUIRE(CNA_CNB_FORMAT_DEFAULT_TOC_OFFSET == (uint64_t)CNA_CNB_FORMAT_HEADER_SIZE);
    REQUIRE(CNA_CNB_FORMAT_CONTAINER_MAJOR == UINT32_C(1));
    REQUIRE(CNA_CNB_FORMAT_CONTAINER_MINOR == UINT32_C(0));
    return 1;
}

static int validate_chunk_identifiers(void)
{
    CNA_CnbChunkId id = UINT32_MAX;
    CNA_Bool well_formed = UINT8_C(9);
    char text[4];
    char sentinel[4];
    uint64_t bytes = UINT64_MAX;

    /* The two identifiers the container itself defines are exactly what packing their letters
       gives, which is the check that the published constants are not transcribed by hand. */
    REQUIRE(cna_cnb_make_chunk_id('C', 'M', 'E', 'T', &id) == CNA_RESULT_SUCCESS);
    REQUIRE(id == CNA_CNB_CONTAINER_CHUNK_METADATA);
    REQUIRE(cna_cnb_make_chunk_id('X', 'R', 'E', 'F', &id) == CNA_RESULT_SUCCESS);
    REQUIRE(id == CNA_CNB_CONTAINER_CHUNK_EXTERNAL_REFERENCES);
    REQUIRE(cna_cnb_make_chunk_id('C', 'M', 'E', 'T', 0) == CNA_RESULT_INVALID_ARGUMENT);

    /* Little-endian packing is the point of the type: the bytes read left to right in a dump. */
    REQUIRE(cna_cnb_make_chunk_id('C', 'M', 'E', 'T', &id) == CNA_RESULT_SUCCESS);
    REQUIRE((id & 0xFFU) == (uint32_t)'C');
    REQUIRE(((id >> 24) & 0xFFU) == (uint32_t)'T');

    REQUIRE(cna_cnb_get_chunk_id_string_size(id, &bytes) == CNA_RESULT_SUCCESS && bytes == 4U);
    REQUIRE(cna_cnb_get_chunk_id_string_size(id, 0) == CNA_RESULT_INVALID_ARGUMENT);

    memset(text, 0x5A, sizeof(text));
    memcpy(sentinel, text, sizeof(sentinel));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_chunk_id_string(id, text, 3U, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == 4U && memcmp(text, sentinel, sizeof(text)) == 0);
    REQUIRE(cna_cnb_copy_chunk_id_string(id, text, sizeof(text), &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 4U && memcmp(text, "CMET", 4U) == 0);

    REQUIRE(cna_cnb_is_well_formed_chunk_id(id, &well_formed) == CNA_RESULT_SUCCESS);
    REQUIRE(well_formed == CNA_TRUE);
    REQUIRE(cna_cnb_is_well_formed_chunk_id(id, 0) == CNA_RESULT_INVALID_ARGUMENT);

    /* A corrupt identifier is rendered, not refused: a byte outside printable ASCII becomes '?'
       so it cannot carry a control character into a log line. */
    REQUIRE(cna_cnb_make_chunk_id(0x01U, 'M', 'E', 0x7FU, &id) == CNA_RESULT_SUCCESS);
    well_formed = UINT8_C(9);
    REQUIRE(cna_cnb_is_well_formed_chunk_id(id, &well_formed) == CNA_RESULT_SUCCESS);
    REQUIRE(well_formed == CNA_FALSE);
    REQUIRE(cna_cnb_copy_chunk_id_string(id, text, sizeof(text), &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 4U && memcmp(text, "?ME?", 4U) == 0);
    return 1;
}

static int expect_asset_type_name(const uint32_t asset_type_id, const char* const expected)
{
    char text[64];
    uint64_t bytes = UINT64_MAX;
    const uint64_t expected_bytes = (uint64_t)strlen(expected);

    REQUIRE(cna_cnb_get_asset_type_name_size(asset_type_id, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == expected_bytes);
    REQUIRE(expected_bytes <= sizeof(text));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_asset_type_name(asset_type_id, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == expected_bytes && memcmp(text, expected, (size_t)expected_bytes) == 0);
    return 1;
}

static int validate_asset_type_identifiers(void)
{
    uint32_t minted = UINT32_MAX;
    uint32_t again = UINT32_MAX;
    CNA_Bool custom = UINT8_C(9);
    char text[64];
    char sentinel[64];
    uint64_t bytes = UINT64_MAX;

    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_INVALID, "Invalid"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_TEXTURE2D, "Texture2D"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_TEXTURE3D, "Texture3D"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_TEXTURE_CUBE, "TextureCube"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_SPRITE_FONT, "SpriteFont"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_MODEL, "Model"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_ANIMATION_CLIP, "AnimationClip"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_CURVE, "Curve"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_SOUND_EFFECT, "SoundEffect"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_SONG, "Song"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_VIDEO, "Video"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_EFFECT, "Effect"));
    /* An identifier in neither the built-in nor the custom range is still rendered, and says which
       kind of unknown it is. */
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_RESERVED_RANGE_FIRST,
                                   "unknown type 0x40000000"));
    REQUIRE(expect_asset_type_name(CNA_CNB_ASSET_TYPE_CUSTOM_RANGE_FIRST,
                                   "custom type 0x80000000"));

    REQUIRE(cna_cnb_is_custom_asset_type_id(CNA_CNB_ASSET_TYPE_TEXTURE2D, &custom) ==
            CNA_RESULT_SUCCESS && custom == CNA_FALSE);
    REQUIRE(cna_cnb_is_custom_asset_type_id(CNA_CNB_ASSET_TYPE_RESERVED_RANGE_FIRST, &custom) ==
            CNA_RESULT_SUCCESS && custom == CNA_FALSE);
    REQUIRE(cna_cnb_is_custom_asset_type_id(CNA_CNB_ASSET_TYPE_CUSTOM_RANGE_FIRST, &custom) ==
            CNA_RESULT_SUCCESS && custom == CNA_TRUE);
    REQUIRE(cna_cnb_is_custom_asset_type_id(UINT32_MAX, &custom) == CNA_RESULT_SUCCESS &&
            custom == CNA_TRUE);
    REQUIRE(cna_cnb_is_custom_asset_type_id(0U, 0) == CNA_RESULT_INVALID_ARGUMENT);

    /* Minting is deterministic and always lands in the custom range -- the two properties a
       compiler, a runtime and a third-party tool all depend on agreeing about. */
    REQUIRE(cna_cnb_asset_type_id_from_name(view("MyGame.Level"), &minted) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_asset_type_id_from_name(view("MyGame.Level"), &again) == CNA_RESULT_SUCCESS);
    REQUIRE(minted == again && minted >= CNA_CNB_ASSET_TYPE_CUSTOM_RANGE_FIRST);
    REQUIRE(cna_cnb_is_custom_asset_type_id(minted, &custom) == CNA_RESULT_SUCCESS &&
            custom == CNA_TRUE);
    /* A different name mints a different identifier; the hash is 31 usable bits, not a constant. */
    REQUIRE(cna_cnb_asset_type_id_from_name(view("MyGame.Other"), &again) == CNA_RESULT_SUCCESS);
    REQUIRE(again != minted);

    /* An empty name is refused, and the refusal leaves the output alone. */
    again = UINT32_C(0xABCDEF01);
    REQUIRE(cna_cnb_asset_type_id_from_name(view(""), &again) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(again == UINT32_C(0xABCDEF01));
    REQUIRE(cna_cnb_asset_type_id_from_name(view("MyGame.Level"), 0) ==
            CNA_RESULT_INVALID_ARGUMENT);
    {
        /* A type name is an ordinary input string, so malformed UTF-8 is an encoding error --
           unlike a logical name below, where it is one of the answers. */
        const char bad[] = {(char)0xFFU};
        const CNA_StringView malformed = {bad, sizeof(bad)};
        again = UINT32_C(0xABCDEF01);
        REQUIRE(cna_cnb_asset_type_id_from_name(malformed, &again) == CNA_RESULT_ENCODING);
        REQUIRE(again == UINT32_C(0xABCDEF01));
    }

    memset(text, 0x5A, sizeof(text));
    memcpy(sentinel, text, sizeof(sentinel));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_asset_type_name(CNA_CNB_ASSET_TYPE_TEXTURE2D, text, 2U, &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == 9U && memcmp(text, sentinel, sizeof(text)) == 0);
    REQUIRE(cna_cnb_get_asset_type_name_size(CNA_CNB_ASSET_TYPE_MODEL, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

static int expect_logical_name_problem(const CNA_StringView name, const int expect_problem)
{
    char text[128];
    uint64_t bytes = UINT64_MAX;

    REQUIRE(cna_cnb_get_logical_name_problem_size(name, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE((bytes != 0U) == (expect_problem != 0));
    REQUIRE(bytes <= sizeof(text));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_logical_name_problem(name, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE((bytes != 0U) == (expect_problem != 0));
    return 1;
}

static int validate_logical_names(void)
{
    char text[128];
    char sentinel[128];
    uint64_t bytes = UINT64_MAX;

    /* A byte count of zero is the whole "this name is acceptable" answer. */
    REQUIRE(expect_logical_name_problem(view("Textures/hero.png"), 0));
    REQUIRE(expect_logical_name_problem(view("hero.png"), 0));
    REQUIRE(expect_logical_name_problem(view("a/b/../c"), 1) == 1);

    REQUIRE(expect_logical_name_problem(view(""), 1));
    REQUIRE(expect_logical_name_problem(view("/absolute"), 1));
    REQUIRE(expect_logical_name_problem(view("C:/drive"), 1));
    REQUIRE(expect_logical_name_problem(view("back\\slash"), 1));
    REQUIRE(expect_logical_name_problem(view("../escape"), 1));
    REQUIRE(expect_logical_name_problem(view(".."), 1));

    {
        /* The deviation this route exists to keep: malformed UTF-8 is a *verdict*, not a boundary
           error, so the call succeeds and the problem text describes it. Validating the input the
           way every other string route does would withhold the answer the caller asked for. */
        const char bad[] = {'a', (char)0xFFU, 'b'};
        const CNA_StringView malformed = {bad, sizeof(bad)};
        REQUIRE(cna_cnb_get_logical_name_problem_size(malformed, &bytes) == CNA_RESULT_SUCCESS);
        REQUIRE(bytes != 0U && bytes <= sizeof(text));
        REQUIRE(cna_cnb_copy_logical_name_problem(malformed, text, sizeof(text), &bytes) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(memcmp(text, "is not well-formed UTF-8", 24U) == 0);
    }

    {
        const CNA_StringView null_with_length = {0, 4U};
        REQUIRE(cna_cnb_get_logical_name_problem_size(null_with_length, &bytes) ==
                CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(cna_cnb_copy_logical_name_problem(null_with_length, text, sizeof(text), &bytes) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }
    REQUIRE(cna_cnb_get_logical_name_problem_size(view("x"), 0) == CNA_RESULT_INVALID_ARGUMENT);

    memset(text, 0x5A, sizeof(text));
    memcpy(sentinel, text, sizeof(sentinel));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_logical_name_problem(view("/absolute"), text, 2U, &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes != 0U && memcmp(text, sentinel, sizeof(text)) == 0);
    return 1;
}

static int validate_checked_arithmetic(void)
{
    uint64_t result = UINT64_C(0x1234);

    REQUIRE(cna_cnb_checked_add(2U, 3U, &result) == CNA_RESULT_SUCCESS && result == 5U);
    REQUIRE(cna_cnb_checked_add(UINT64_MAX, 0U, &result) == CNA_RESULT_SUCCESS &&
            result == UINT64_MAX);

    /* One past the boundary, and the refused call leaves the previous answer standing rather than
       clobbering it -- the failure mode the whole helper exists to prevent, seen from C. */
    result = UINT64_C(0x1234);
    REQUIRE(cna_cnb_checked_add(UINT64_MAX, 1U, &result) == CNA_RESULT_OVERFLOW);
    REQUIRE(result == UINT64_C(0x1234));
    REQUIRE(cna_cnb_checked_add(UINT64_MAX, UINT64_MAX, &result) == CNA_RESULT_OVERFLOW);
    REQUIRE(result == UINT64_C(0x1234));
    REQUIRE(cna_cnb_checked_add(1U, 1U, 0) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_cnb_checked_multiply(6U, 7U, &result) == CNA_RESULT_SUCCESS && result == 42U);
    REQUIRE(cna_cnb_checked_multiply(0U, UINT64_MAX, &result) == CNA_RESULT_SUCCESS &&
            result == 0U);
    REQUIRE(cna_cnb_checked_multiply(UINT64_MAX, 1U, &result) == CNA_RESULT_SUCCESS &&
            result == UINT64_MAX);

    result = UINT64_C(0x1234);
    REQUIRE(cna_cnb_checked_multiply(UINT64_MAX, 2U, &result) == CNA_RESULT_OVERFLOW);
    REQUIRE(result == UINT64_C(0x1234));
    REQUIRE(cna_cnb_checked_multiply((UINT64_MAX / 3U) + 1U, 3U, &result) == CNA_RESULT_OVERFLOW);
    REQUIRE(result == UINT64_C(0x1234));
    REQUIRE(cna_cnb_checked_multiply(2U, 2U, 0) == CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

static int validate_checksums(void)
{
    static const unsigned char check[] = "123456789";
    uint32_t value = UINT32_MAX;
    uint32_t running = UINT32_MAX;
    uint32_t portable = UINT32_MAX;
    CNA_Bool hardware = UINT8_C(9);

    /* CRC-32/ISCSI's published check value. A stored-in-the-file checksum is only worth anything
       if it is the one every other implementation computes, so this is pinned against the standard
       rather than against whatever this build happens to produce. */
    REQUIRE(cna_cnb_crc32c(check, 9U, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(value == UINT32_C(0xE3069283));

    /* The seed is the checksum of nothing, which is what makes it usable as a starting value. */
    REQUIRE(cna_cnb_crc32c(0, 0U, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(value == CNA_CNB_CRC32C_SEED);

    /* A logically contiguous region split across two buffers checksums to the same value. */
    REQUIRE(cna_cnb_crc32c_continue(CNA_CNB_CRC32C_SEED, check, 5U, &running) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_crc32c_continue(running, check + 5, 4U, &running) == CNA_RESULT_SUCCESS);
    REQUIRE(running == UINT32_C(0xE3069283));

    /* The portable path is the definition of correct, so the shipping path must agree with it --
       on this machine and on one without the instruction alike. Asserting they agree is the only
       reason the portable route is published at all. */
    REQUIRE(cna_cnb_crc32c_portable(check, 9U, &portable) == CNA_RESULT_SUCCESS);
    REQUIRE(portable == UINT32_C(0xE3069283));
    REQUIRE(cna_cnb_crc32c_uses_hardware(&hardware) == CNA_RESULT_SUCCESS);
    REQUIRE(hardware == CNA_TRUE || hardware == CNA_FALSE);
    {
        /* Long enough to reach whatever block-at-a-time path either implementation takes. */
        unsigned char block[1024];
        uint32_t folded = UINT32_MAX;
        for (size_t index = 0U; index < sizeof(block); ++index) {
            block[index] = (unsigned char)(index * 7U + 11U);
        }
        REQUIRE(cna_cnb_crc32c(block, sizeof(block), &value) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_crc32c_portable(block, sizeof(block), &folded) == CNA_RESULT_SUCCESS);
        REQUIRE(value == folded);
    }

    REQUIRE(cna_cnb_crc32c(check, 9U, 0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_crc32c(0, 9U, &value) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_crc32c_continue(0U, 0, 9U, &value) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_crc32c_continue(0U, check, 9U, 0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_crc32c_portable(0, 9U, &value) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_crc32c_portable(check, 9U, 0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_crc32c_uses_hardware(0) == CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

static int validate_read_limits(void)
{
    CNA_CnbReadLimits limits;
    CNA_CnbReadLimits scratch;

    memset(&limits, 0, sizeof(limits));
    limits.struct_size = (uint32_t)sizeof(limits);
    limits.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
    REQUIRE(cna_cnb_read_limits_init(&limits) == CNA_RESULT_SUCCESS);

    REQUIRE(limits.max_file_size == UINT64_C(512) * 1024U * 1024U);
    REQUIRE(limits.max_chunk_size == UINT64_C(384) * 1024U * 1024U);
    REQUIRE(limits.max_total_uncompressed_size == UINT64_C(1024) * 1024U * 1024U);
    REQUIRE(limits.max_chunk_count == UINT32_C(65536));
    REQUIRE(limits.max_string_bytes == UINT32_C(1024) * 1024U);
    REQUIRE(limits.max_array_element_count == UINT32_C(16) * 1024U * 1024U);
    REQUIRE(limits.max_chunk_alignment == UINT32_C(4096));

    /* The two relationships the defaults exist to hold, rather than seven numbers on their own:
       a single chunk cannot fill a whole file, and the expansion ceiling is above the file ceiling
       so compression can genuinely expand a file instead of being cancelled out by it. */
    REQUIRE(limits.max_chunk_size < limits.max_file_size);
    REQUIRE(limits.max_total_uncompressed_size > limits.max_file_size);

    REQUIRE(cna_cnb_read_limits_init(0) == CNA_RESULT_INVALID_ARGUMENT);

    /* The documented prefix rule: a future caller's larger structure is accepted, an older
       caller's smaller one is refused, and an unknown version is refused whatever its size. */
    memset(&scratch, 0, sizeof(scratch));
    scratch.struct_size = (uint32_t)sizeof(scratch) + 8U;
    scratch.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
    REQUIRE(cna_cnb_read_limits_init(&scratch) == CNA_RESULT_SUCCESS);
    REQUIRE(scratch.max_chunk_alignment == UINT32_C(4096));

    memset(&scratch, 0, sizeof(scratch));
    scratch.struct_size = (uint32_t)sizeof(scratch) - 1U;
    scratch.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
    REQUIRE(cna_cnb_read_limits_init(&scratch) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(scratch.max_chunk_alignment == 0U);

    memset(&scratch, 0, sizeof(scratch));
    scratch.struct_size = (uint32_t)sizeof(scratch);
    scratch.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION + 1U;
    REQUIRE(cna_cnb_read_limits_init(&scratch) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(scratch.max_chunk_alignment == 0U);
    return 1;
}

static int expect_compression_name(const CNA_CnbCompression codec, const char* const expected)
{
    char text[64];
    uint64_t bytes = UINT64_MAX;
    const uint64_t expected_bytes = (uint64_t)strlen(expected);

    REQUIRE(cna_cnb_get_compression_name_size(codec, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == expected_bytes && expected_bytes <= sizeof(text));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_compression_name(codec, text, sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == expected_bytes && memcmp(text, expected, (size_t)expected_bytes) == 0);
    return 1;
}

static int validate_compression(void)
{
    static const unsigned char payload[] =
        "cnb chunk payload, repeated so a codec has something to find; "
        "cnb chunk payload, repeated so a codec has something to find; "
        "cnb chunk payload, repeated so a codec has something to find.";
    const uint64_t payload_size = (uint64_t)sizeof(payload) - 1U;
    CNA_Bool supported = UINT8_C(9);
    CNA_Bool zstd_supported = UINT8_C(9);
    uint8_t buffer[512];
    uint8_t sentinel[512];
    uint64_t bytes = UINT64_MAX;

    REQUIRE(expect_compression_name(CNA_CNB_COMPRESSION_NONE, "none"));
    REQUIRE(expect_compression_name(CNA_CNB_COMPRESSION_LZ4, "LZ4"));
    REQUIRE(expect_compression_name(CNA_CNB_COMPRESSION_ZSTD, "Zstandard"));
    REQUIRE(expect_compression_name(CNA_CNB_COMPRESSION_DEFLATE, "Deflate"));
    /* A codec identity a corrupt file can name is rendered rather than refused, so a diagnostic
       about it is still readable. */
    REQUIRE(expect_compression_name(UINT32_C(7), "unknown codec 7"));
    REQUIRE(cna_cnb_get_compression_name_size(CNA_CNB_COMPRESSION_NONE, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_cnb_is_compression_supported(CNA_CNB_COMPRESSION_NONE, &supported) ==
            CNA_RESULT_SUCCESS && supported == CNA_TRUE);
    REQUIRE(cna_cnb_is_compression_supported(UINT32_C(7), &supported) == CNA_RESULT_SUCCESS &&
            supported == CNA_FALSE);
    REQUIRE(cna_cnb_is_compression_supported(CNA_CNB_COMPRESSION_ZSTD, &zstd_supported) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(zstd_supported == CNA_TRUE || zstd_supported == CNA_FALSE);
    REQUIRE(cna_cnb_is_compression_supported(CNA_CNB_COMPRESSION_NONE, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* Stored: the bytes are the answer, whatever the level says. */
    REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_NONE,
                                              3, &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == payload_size);
    REQUIRE(cna_cnb_copy_compressed(payload, payload_size, CNA_CNB_COMPRESSION_NONE, 3,
                                    buffer, sizeof(buffer), &bytes) == CNA_RESULT_SUCCESS);
    REQUIRE(bytes == payload_size && memcmp(buffer, payload, (size_t)payload_size) == 0);

    memset(buffer, 0x5A, sizeof(buffer));
    memcpy(sentinel, buffer, sizeof(sentinel));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_compressed(payload, payload_size, CNA_CNB_COMPRESSION_NONE, 3,
                                    buffer, 4U, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == payload_size && untouched(buffer, sizeof(buffer)));

    /* A codec with an assigned identifier and no implementation is NOT_SUPPORTED, which is a
       different answer from a corrupt argument and has to stay that way: a caller retries with
       another codec, it does not fix its numbers. */
    REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_LZ4,
                                              3, &bytes) == CNA_RESULT_NOT_SUPPORTED);
    REQUIRE(cna_cnb_copy_compressed(payload, payload_size, CNA_CNB_COMPRESSION_DEFLATE, 3,
                                    buffer, sizeof(buffer), &bytes) == CNA_RESULT_NOT_SUPPORTED);
    REQUIRE(cna_cnb_copy_compressed(payload, payload_size, UINT32_C(7), 3,
                                    buffer, sizeof(buffer), &bytes) == CNA_RESULT_NOT_SUPPORTED);

    REQUIRE(cna_cnb_get_compressed_byte_count(0, payload_size, CNA_CNB_COMPRESSION_NONE, 3,
                                              &bytes) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_NONE, 3,
                                              0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_copy_compressed(payload, payload_size, CNA_CNB_COMPRESSION_NONE, 3,
                                    0, 4U, &bytes) == CNA_RESULT_INVALID_ARGUMENT);

    /* Stored decompression consults neither size. That is the canonical order -- `None` is
       answered before the ceiling is looked at -- and preserving it is what makes a C reader agree
       with the C++ one about the same file. */
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_decompressed(payload, payload_size, CNA_CNB_COMPRESSION_NONE,
                                      0U, 0U, buffer, sizeof(buffer), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == payload_size && memcmp(buffer, payload, (size_t)payload_size) == 0);

    memset(buffer, 0x5A, sizeof(buffer));
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_copy_decompressed(payload, payload_size, CNA_CNB_COMPRESSION_NONE,
                                      0U, 0U, buffer, 4U, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == payload_size && untouched(buffer, sizeof(buffer)));

    /* An unsupported codec whose declared size is also over the ceiling reports the ceiling, not
       the codec: that is the canonical order of the two checks, and reversing it would answer a
       different question about the same corrupt file. */
    REQUIRE(cna_cnb_copy_decompressed(payload, payload_size, CNA_CNB_COMPRESSION_LZ4,
                                      1024U, 16U, buffer, sizeof(buffer), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_copy_decompressed(payload, payload_size, CNA_CNB_COMPRESSION_LZ4,
                                      16U, 1024U, buffer, sizeof(buffer), &bytes) ==
            CNA_RESULT_NOT_SUPPORTED);

    if (zstd_supported == CNA_TRUE) {
        uint8_t compressed[512];
        uint64_t compressed_size = UINT64_MAX;

        REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD,
                                                  3, &compressed_size) == CNA_RESULT_SUCCESS);
        REQUIRE(compressed_size != 0U && compressed_size <= sizeof(compressed));
        bytes = UINT64_MAX;
        REQUIRE(cna_cnb_copy_compressed(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD, 3,
                                        compressed, sizeof(compressed), &bytes) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(bytes == compressed_size);
        /* This payload repeats, so a working codec must actually shrink it. Without this the
           round trip below would pass just as happily against a codec that stored the bytes. */
        REQUIRE(compressed_size < payload_size);

        memset(buffer, 0x5A, sizeof(buffer));
        bytes = UINT64_MAX;
        REQUIRE(cna_cnb_copy_decompressed(compressed, compressed_size, CNA_CNB_COMPRESSION_ZSTD,
                                          payload_size, payload_size, buffer, sizeof(buffer),
                                          &bytes) == CNA_RESULT_SUCCESS);
        REQUIRE(bytes == payload_size && memcmp(buffer, payload, (size_t)payload_size) == 0);

        /* The exact-size requirement: a stream that expands to a different length than the entry
           declares is a corrupt file, not a short read for later code to treat as data. */
        REQUIRE(cna_cnb_copy_decompressed(compressed, compressed_size, CNA_CNB_COMPRESSION_ZSTD,
                                          payload_size - 1U, payload_size, buffer,
                                          sizeof(buffer), &bytes) == CNA_RESULT_IO);
        /* Above the ceiling, refused before anything is allocated. */
        REQUIRE(cna_cnb_copy_decompressed(compressed, compressed_size, CNA_CNB_COMPRESSION_ZSTD,
                                          payload_size, payload_size - 1U, buffer,
                                          sizeof(buffer), &bytes) == CNA_RESULT_INVALID_ARGUMENT);
        /* Bytes that are not a valid frame. */
        REQUIRE(cna_cnb_copy_decompressed(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD,
                                          payload_size, payload_size, buffer, sizeof(buffer),
                                          &bytes) == CNA_RESULT_IO);

        /* The level is a correction, not a contract: one outside the codec's range is clamped, so
           both extremes still produce something the round trip accepts. */
        bytes = UINT64_MAX;
        REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD,
                                                  -1000, &bytes) == CNA_RESULT_SUCCESS);
        REQUIRE(bytes != 0U);
        REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD,
                                                  1000, &bytes) == CNA_RESULT_SUCCESS);
        REQUIRE(bytes != 0U);
    } else {
        /* The layer-absent arm, asserted rather than skipped: without the codec every route that
           needs it refuses by name, and refuses the same way in both directions. */
        REQUIRE(cna_cnb_get_compressed_byte_count(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD,
                                                  3, &bytes) == CNA_RESULT_NOT_SUPPORTED);
        REQUIRE(cna_cnb_copy_decompressed(payload, payload_size, CNA_CNB_COMPRESSION_ZSTD,
                                          payload_size, payload_size, buffer, sizeof(buffer),
                                          &bytes) == CNA_RESULT_NOT_SUPPORTED);
    }
    return 1;
}

int main(void)
{
    if (!validate_format_constants()) { return 1; }
    if (!validate_chunk_identifiers()) { return 2; }
    if (!validate_asset_type_identifiers()) { return 3; }
    if (!validate_logical_names()) { return 4; }
    if (!validate_checked_arithmetic()) { return 5; }
    if (!validate_checksums()) { return 6; }
    if (!validate_read_limits()) { return 7; }
    if (!validate_compression()) { return 8; }
    return 0;
}
