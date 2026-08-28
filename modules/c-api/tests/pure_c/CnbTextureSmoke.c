// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-108 -- the `.cnb` texture pixel formats and the Texture2D /
 * TextureCube / Texture3D schemas.
 *
 * Like the document suite this builds what it then reads, so the encoder and the decoder are
 * checked against each other rather than against a blob. Two things get more than a round trip,
 * because a round trip cannot see them: the **format numbering**, which is wire format and would
 * survive being wrong in both directions at once, and the **block-compressed size rule**, where a
 * 1x1 BC7 level is a whole 16-byte block rather than a fraction of one.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CnbTextureSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

_Static_assert(sizeof(CNA_CnbTextureFormat) == sizeof(uint32_t),
               "CNA_CnbTextureFormat must remain fixed width");
_Static_assert(CNA_CNB_TEXTURE_FORMAT_UNKNOWN == UINT32_C(0) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA8 == UINT32_C(1) &&
                   CNA_CNB_TEXTURE_FORMAT_BC1 == UINT32_C(22) &&
                   CNA_CNB_TEXTURE_FORMAT_BC7_SRGB == UINT32_C(27) &&
                   CNA_CNB_TEXTURE_FORMAT_MAXIMUM == UINT32_C(27),
               "CNB texture format identifiers are wire format and must not move");

static CNA_StringView view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

/* ---- pixel formats ---------------------------------------------------------------------------- */

static int validate_format_identities(void)
{
    CNA_Bool flag = UINT8_C(9);
    uint32_t unit = UINT32_MAX;
    uint64_t size = UINT64_MAX;
    char text[64];

    REQUIRE(cna_cnb_is_known_texture_format(CNA_CNB_TEXTURE_FORMAT_UNKNOWN, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_FALSE);
    REQUIRE(cna_cnb_is_known_texture_format(CNA_CNB_TEXTURE_FORMAT_RGBA8, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_TRUE);
    REQUIRE(cna_cnb_is_known_texture_format(CNA_CNB_TEXTURE_FORMAT_MAXIMUM, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_TRUE);
    REQUIRE(cna_cnb_is_known_texture_format(CNA_CNB_TEXTURE_FORMAT_MAXIMUM + 1U, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_FALSE);
    REQUIRE(cna_cnb_is_known_texture_format(0U, 0) == CNA_RESULT_INVALID_ARGUMENT);

    /* A corrupt file's format field is rendered rather than refused. */
    REQUIRE(cna_cnb_get_texture_format_name_size(UINT32_C(999), &size) == CNA_RESULT_SUCCESS);
    REQUIRE(size != 0U && size <= sizeof(text));
    REQUIRE(cna_cnb_copy_texture_format_name(UINT32_C(999), text, sizeof(text), &size) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_copy_texture_format_name(CNA_CNB_TEXTURE_FORMAT_RGBA8, text, sizeof(text),
                                             &size) == CNA_RESULT_SUCCESS);
    REQUIRE(size == 5U && memcmp(text, "Rgba8", 5U) == 0);
    REQUIRE(cna_cnb_copy_texture_format_name(CNA_CNB_TEXTURE_FORMAT_RGBA8, text, 2U, &size) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(size == 5U);

    /* Only the six BC identifiers are block-compressed, and nothing below them is. */
    for (uint32_t format = 1U; format <= CNA_CNB_TEXTURE_FORMAT_MAXIMUM; ++format) {
        const int expect_block = format >= CNA_CNB_TEXTURE_FORMAT_BC1;
        REQUIRE(cna_cnb_is_block_compressed_texture_format(format, &flag) == CNA_RESULT_SUCCESS);
        REQUIRE((flag == CNA_TRUE) == (expect_block != 0));
        REQUIRE(cna_cnb_get_texture_format_unit_bytes(format, &unit) == CNA_RESULT_SUCCESS);
        REQUIRE(unit != 0U);
    }
    REQUIRE(cna_cnb_get_texture_format_unit_bytes(CNA_CNB_TEXTURE_FORMAT_UNKNOWN, &unit) ==
            CNA_RESULT_SUCCESS && unit == 0U);
    REQUIRE(cna_cnb_get_texture_format_unit_bytes(CNA_CNB_TEXTURE_FORMAT_RGBA8, &unit) ==
            CNA_RESULT_SUCCESS && unit == 4U);

    /* An uncompressed level is texels times unit size. */
    REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_RGBA8, 4U, 4U, 1U, &size) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(size == 4U * 4U * 4U);
    REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_RGBA8, 4U, 4U, 2U, &size) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(size == 4U * 4U * 2U * 4U);

    /* A block-compressed level rounds each dimension up to a whole 4-texel block, which is what
       makes a 1x1 BC7 level a full block rather than a fraction of one. Asserted against the
       format's own unit size rather than a transcribed constant. */
    {
        uint32_t bc7_unit = 0U;
        uint32_t bc1_unit = 0U;
        REQUIRE(cna_cnb_get_texture_format_unit_bytes(CNA_CNB_TEXTURE_FORMAT_BC7, &bc7_unit) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_get_texture_format_unit_bytes(CNA_CNB_TEXTURE_FORMAT_BC1, &bc1_unit) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_BC7, 1U, 1U, 1U,
                                                    &size) == CNA_RESULT_SUCCESS);
        REQUIRE(size == (uint64_t)bc7_unit);
        REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_BC7, 4U, 4U, 1U,
                                                    &size) == CNA_RESULT_SUCCESS);
        REQUIRE(size == (uint64_t)bc7_unit);
        /* Five texels across is two blocks, not one and a quarter. */
        REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_BC1, 5U, 4U, 1U,
                                                    &size) == CNA_RESULT_SUCCESS);
        REQUIRE(size == 2U * (uint64_t)bc1_unit);
    }

    /* A zero dimension and an unknown format are both content problems. */
    REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_RGBA8, 0U, 4U, 1U, &size) ==
            CNA_RESULT_IO);
    REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_UNKNOWN, 4U, 4U, 1U,
                                                &size) == CNA_RESULT_IO);
    REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_RGBA8, 4U, 4U, 1U, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

static int validate_surface_format_mapping(void)
{
    CNA_SurfaceFormat surface = UINT32_MAX;
    CNA_CnbTextureFormat back = UINT32_MAX;

    REQUIRE(cna_cnb_texture_format_to_surface_format(CNA_CNB_TEXTURE_FORMAT_RGBA8, &surface) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(surface == CNA_SURFACE_FORMAT_COLOR);

    /* Every identifier maps to a surface format and back to itself. That is the property the whole
       enumeration exists for: the numbering is frozen so a `.cnb` keeps its meaning, and the
       mapping is a function somebody has to edit rather than a cast that follows the runtime enum's
       declaration order. A round trip over all 27 is what would catch a mapping that had drifted. */
    for (uint32_t format = 1U; format <= CNA_CNB_TEXTURE_FORMAT_MAXIMUM; ++format) {
        surface = UINT32_MAX;
        back = UINT32_MAX;
        REQUIRE(cna_cnb_texture_format_to_surface_format(format, &surface) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_texture_format_from_surface_format(surface, &back) == CNA_RESULT_SUCCESS);
        REQUIRE(back == format);
    }

    REQUIRE(cna_cnb_texture_format_to_surface_format(CNA_CNB_TEXTURE_FORMAT_UNKNOWN, &surface) ==
            CNA_RESULT_IO);
    REQUIRE(cna_cnb_texture_format_to_surface_format(UINT32_C(999), &surface) == CNA_RESULT_IO);
    REQUIRE(cna_cnb_texture_format_to_surface_format(CNA_CNB_TEXTURE_FORMAT_RGBA8, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_texture_format_from_surface_format(CNA_SURFACE_FORMAT_COLOR, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

/* ---- the texture description ------------------------------------------------------------------ */

static CNA_Bool accept_rgba8(const CNA_CnbTextureFormat format, void* const context)
{
    int* const calls = (int*)context;
    if (calls != 0) { ++*calls; }
    return format == CNA_CNB_TEXTURE_FORMAT_RGBA8 ? CNA_TRUE : CNA_FALSE;
}

static CNA_Bool accept_nothing(const CNA_CnbTextureFormat format, void* const context)
{
    (void)format;
    (void)context;
    return CNA_FALSE;
}

static int validate_texture_data(void)
{
    static const unsigned char rgba[2U * 2U * 4U] = {
        1U, 2U, 3U, 4U,      5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U,   13U, 14U, 15U, 16U};
    CNA_CnbTextureDataHandle texture = CNA_INVALID_HANDLE;
    CNA_CnbTextureInfo info;
    CNA_CnbTextureFormat format = UINT32_MAX;
    CNA_Bool found = UINT8_C(9);
    uint64_t value = UINT64_MAX;
    uint32_t width = 0U;
    uint32_t height = 0U;
    uint32_t depth = 0U;
    uint8_t level[64];
    int calls = 0;

    REQUIRE(cna_cnb_texture_data_create_rgba8(2U, 2U, rgba, sizeof(rgba), &texture) ==
            CNA_RESULT_SUCCESS);

    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_TEXTURE_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_texture_data_get_info(texture, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.width == 2U && info.height == 2U && info.depth == 1U);
    REQUIRE(info.face_count == 1U && info.mip_count == 1U && info.representation_count == 1U);

    REQUIRE(cna_cnb_texture_data_get_representation_count(texture, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(value == 1U);
    REQUIRE(cna_cnb_texture_data_get_representation_format(texture, 0U, &format) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(format == CNA_CNB_TEXTURE_FORMAT_RGBA8);
    REQUIRE(cna_cnb_texture_data_get_level_count(texture, 0U, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(value == 1U);
    REQUIRE(cna_cnb_texture_data_copy_level(texture, 0U, 0U, level, sizeof(level), &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value == sizeof(rgba) && memcmp(level, rgba, sizeof(rgba)) == 0);
    REQUIRE(cna_cnb_texture_data_copy_level(texture, 0U, 0U, level, 4U, &value) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(value == sizeof(rgba));

    /* An index a caller supplied is an argument error, the same rule the document's accessors
       follow, and it is checked at both levels of nesting. */
    REQUIRE(cna_cnb_texture_data_get_representation_format(texture, 9U, &format) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_texture_data_get_level_count(texture, 9U, &value) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_texture_data_copy_level(texture, 0U, 9U, level, sizeof(level), &value) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_texture_data_set_level(texture, 9U, 0U, rgba, sizeof(rgba)) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* Mip 0 is the declared size and every later level halves without falling below 1. */
    REQUIRE(cna_cnb_texture_data_get_level_dimensions(texture, 0U, &width, &height, &depth) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(width == 2U && height == 2U && depth == 1U);
    REQUIRE(cna_cnb_texture_data_get_level_dimensions(texture, 1U, &width, &height, &depth) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(width == 1U && height == 1U && depth == 1U);
    REQUIRE(cna_cnb_texture_data_get_level_dimensions(texture, 9U, &width, &height, &depth) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(width == 1U && height == 1U && depth == 1U);

    /* The predicate is called once per representation, in order, and the first accepted one wins. */
    REQUIRE(cna_cnb_texture_data_select_representation(texture, accept_rgba8, &calls, &found,
                                                       &value) == CNA_RESULT_SUCCESS);
    REQUIRE(found == CNA_TRUE && value == 0U && calls == 1);
    /* Nothing supported is an ordinary answer, not a refusal, and it leaves the index alone. */
    value = UINT64_C(777);
    REQUIRE(cna_cnb_texture_data_select_representation(texture, accept_nothing, 0, &found,
                                                       &value) == CNA_RESULT_SUCCESS);
    REQUIRE(found == CNA_FALSE && value == UINT64_C(777));
    REQUIRE(cna_cnb_texture_data_select_representation(texture, 0, 0, &found, &value) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* A second representation is appended after the first, sized for the declared shape. */
    REQUIRE(cna_cnb_texture_data_add_representation(texture, CNA_CNB_TEXTURE_FORMAT_BC7, &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value == 1U);
    REQUIRE(cna_cnb_texture_data_get_level_count(texture, 1U, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(value == 1U);
    REQUIRE(cna_cnb_texture_data_get_representation_count(texture, &value) ==
            CNA_RESULT_SUCCESS && value == 2U);

    REQUIRE(cna_cnb_texture_data_destroy(texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_get_representation_count(texture, &value) ==
            CNA_RESULT_INVALID_HANDLE);

    /* A length that is not exactly width * height * 4 is a content problem. */
    REQUIRE(cna_cnb_texture_data_create_rgba8(2U, 2U, rgba, sizeof(rgba) - 1U, &texture) ==
            CNA_RESULT_IO);
    REQUIRE(texture == CNA_INVALID_HANDLE);
    REQUIRE(cna_cnb_texture_data_create_rgba8(0U, 2U, rgba, 0U, &texture) == CNA_RESULT_IO);
    REQUIRE(cna_cnb_texture_data_create(2U, 2U, 1U, 0U, 1U, &texture) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_texture_data_create(2U, 2U, 1U, 1U, 0U, &texture) ==
            CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

/* ---- the three schemas ------------------------------------------------------------------------- */

static int fill_rgba8(const CNA_CnbTextureDataHandle texture, const uint64_t representation,
                      const uint32_t faceCount, const uint32_t mipCount)
{
    for (uint32_t face = 0U; face < faceCount; ++face) {
        for (uint32_t mip = 0U; mip < mipCount; ++mip) {
            uint32_t width = 0U;
            uint32_t height = 0U;
            uint32_t depth = 0U;
            uint64_t needed = 0U;
            unsigned char payload[4096];
            REQUIRE(cna_cnb_texture_data_get_level_dimensions(texture, mip, &width, &height,
                                                              &depth) == CNA_RESULT_SUCCESS);
            REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_RGBA8, width,
                                                        height, depth, &needed) ==
                    CNA_RESULT_SUCCESS);
            REQUIRE(needed <= sizeof(payload));
            for (uint64_t i = 0U; i < needed; ++i) {
                payload[i] = (unsigned char)(face * 31U + mip * 7U + i);
            }
            REQUIRE(cna_cnb_texture_data_set_level(texture, representation,
                                                   (uint64_t)face * mipCount + mip, payload,
                                                   needed) == CNA_RESULT_SUCCESS);
        }
    }
    return 1;
}

static int expect_same_texture(const CNA_CnbTextureDataHandle left,
                               const CNA_CnbTextureDataHandle right)
{
    CNA_CnbTextureInfo a;
    CNA_CnbTextureInfo b;
    uint64_t levels = 0U;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.struct_size = (uint32_t)sizeof(a);
    a.struct_version = CNA_CNB_TEXTURE_INFO_STRUCT_VERSION;
    b.struct_size = (uint32_t)sizeof(b);
    b.struct_version = CNA_CNB_TEXTURE_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_texture_data_get_info(left, &a) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_get_info(right, &b) == CNA_RESULT_SUCCESS);
    REQUIRE(a.width == b.width && a.height == b.height && a.depth == b.depth);
    REQUIRE(a.face_count == b.face_count && a.mip_count == b.mip_count);
    REQUIRE(a.representation_count == b.representation_count);

    REQUIRE(cna_cnb_texture_data_get_level_count(left, 0U, &levels) == CNA_RESULT_SUCCESS);
    for (uint64_t level = 0U; level < levels; ++level) {
        uint8_t x[4096];
        uint8_t y[4096];
        uint64_t xs = 0U;
        uint64_t ys = 0U;
        REQUIRE(cna_cnb_texture_data_copy_level(left, 0U, level, x, sizeof(x), &xs) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_texture_data_copy_level(right, 0U, level, y, sizeof(y), &ys) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(xs == ys && memcmp(x, y, (size_t)xs) == 0);
    }
    return 1;
}

static int validate_schemas(void)
{
    uint8_t image[16384];
    uint64_t imageSize = 0U;
    CNA_CnbTextureDataHandle texture = CNA_INVALID_HANDLE;
    CNA_CnbTextureDataHandle decoded = CNA_INVALID_HANDLE;
    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    CNA_CnbChunkId id = 0U;
    uint64_t value = UINT64_MAX;
    uint32_t number = UINT32_MAX;

    /* The published chunk identifiers are what packing their letters gives. */
    REQUIRE(cna_cnb_make_chunk_id('T', 'E', 'X', 'H', &id) == CNA_RESULT_SUCCESS);
    REQUIRE(id == CNA_CNB_TEXTURE_CHUNK_HEADER);
    REQUIRE(cna_cnb_make_chunk_id('T', 'E', 'X', 'R', &id) == CNA_RESULT_SUCCESS);
    REQUIRE(id == CNA_CNB_TEXTURE_CHUNK_REPRESENTATIONS);
    REQUIRE(cna_cnb_make_chunk_id('T', 'E', 'X', 'D', &id) == CNA_RESULT_SUCCESS);
    REQUIRE(id == CNA_CNB_TEXTURE_CHUNK_PAYLOAD);

    /* --- 2D, with a real mip chain --- */
    REQUIRE(cna_cnb_texture_data_create(4U, 4U, 1U, 1U, 3U, &texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_add_representation(texture, CNA_CNB_TEXTURE_FORMAT_RGBA8,
                                                    &value) == CNA_RESULT_SUCCESS);
    REQUIRE(fill_rgba8(texture, 0U, 1U, 3U));

    REQUIRE(cna_cnb_encode_texture2d(texture, view("textures/hero"), 0, 0U, &imageSize) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(imageSize > CNA_CNB_FORMAT_HEADER_SIZE && imageSize <= sizeof(image));
    REQUIRE(cna_cnb_encode_texture2d(texture, view("textures/hero"), image, sizeof(image),
                                     &imageSize) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_document_parse(image, imageSize, view("hero.cnb"), 0, &document) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &number) == CNA_RESULT_SUCCESS);
    REQUIRE(number == CNA_CNB_ASSET_TYPE_TEXTURE2D);
    REQUIRE(cna_cnb_document_get_asset_schema_version(document, &number) == CNA_RESULT_SUCCESS);
    REQUIRE(number == CNA_CNB_TEXTURE_SCHEMA_VERSION);
    /* One header, one representation table, and one payload per level. */
    REQUIRE(cna_cnb_document_require_single(document, CNA_CNB_TEXTURE_CHUNK_HEADER, &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_require_single(document, CNA_CNB_TEXTURE_CHUNK_REPRESENTATIONS,
                                            &value) == CNA_RESULT_SUCCESS);
    {
        uint64_t payloads[16];
        REQUIRE(cna_cnb_document_find_all(document, CNA_CNB_TEXTURE_CHUNK_PAYLOAD, payloads, 16U,
                                          &value) == CNA_RESULT_SUCCESS);
        REQUIRE(value == 3U);
    }
    /* The header chunk really is the published stride, which is the sort of number a schema
       change would move silently. */
    {
        CNA_CnbChunkEntry entry;
        uint64_t headerIndex = 0U;
        memset(&entry, 0, sizeof(entry));
        entry.struct_size = (uint32_t)sizeof(entry);
        entry.struct_version = CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION;
        REQUIRE(cna_cnb_document_require_single(document, CNA_CNB_TEXTURE_CHUNK_HEADER,
                                                &headerIndex) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_document_get_chunk(document, headerIndex, &entry) == CNA_RESULT_SUCCESS);
        REQUIRE(entry.uncompressed_size == (uint64_t)CNA_CNB_TEXTURE_HEADER_STRIDE);
    }

    REQUIRE(cna_cnb_decode_texture2d(document, &decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(expect_same_texture(texture, decoded));
    /* Asking the wrong schema of the right file is a content problem, not a crash. */
    {
        CNA_CnbTextureDataHandle wrong = UINT64_MAX;
        REQUIRE(cna_cnb_decode_texture_cube(document, &wrong) == CNA_RESULT_IO);
        REQUIRE(wrong == CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_decode_texture3d(document, &wrong) == CNA_RESULT_IO);
    }
    REQUIRE(cna_cnb_texture_data_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);

    /* Encoding a one-faced texture as a cube is refused for the shape, not silently reinterpreted. */
    REQUIRE(cna_cnb_encode_texture_cube(texture, view(""), image, sizeof(image), &value) ==
            CNA_RESULT_IO);
    REQUIRE(cna_cnb_texture_data_destroy(texture) == CNA_RESULT_SUCCESS);

    /* --- cube --- */
    REQUIRE(cna_cnb_texture_data_create(4U, 4U, 1U, CNA_CNB_TEXTURE_CUBE_FACE_COUNT, 1U,
                                        &texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_add_representation(texture, CNA_CNB_TEXTURE_FORMAT_RGBA8,
                                                    &value) == CNA_RESULT_SUCCESS);
    REQUIRE(fill_rgba8(texture, 0U, CNA_CNB_TEXTURE_CUBE_FACE_COUNT, 1U));
    REQUIRE(cna_cnb_encode_texture_cube(texture, view(""), image, sizeof(image), &imageSize) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_parse(image, imageSize, view("sky.cnb"), 0, &document) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &number) == CNA_RESULT_SUCCESS);
    REQUIRE(number == CNA_CNB_ASSET_TYPE_TEXTURE_CUBE);
    REQUIRE(cna_cnb_decode_texture_cube(document, &decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(expect_same_texture(texture, decoded));
    REQUIRE(cna_cnb_texture_data_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(texture) == CNA_RESULT_SUCCESS);

    /* --- 3D, where depth halves per level like the other two dimensions --- */
    REQUIRE(cna_cnb_texture_data_create(4U, 4U, 4U, 1U, 2U, &texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_add_representation(texture, CNA_CNB_TEXTURE_FORMAT_RGBA8,
                                                    &value) == CNA_RESULT_SUCCESS);
    REQUIRE(fill_rgba8(texture, 0U, 1U, 2U));
    REQUIRE(cna_cnb_encode_texture3d(texture, view(""), image, sizeof(image), &imageSize) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_parse(image, imageSize, view("volume.cnb"), 0, &document) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &number) == CNA_RESULT_SUCCESS);
    REQUIRE(number == CNA_CNB_ASSET_TYPE_TEXTURE3D);
    REQUIRE(cna_cnb_decode_texture3d(document, &decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(expect_same_texture(texture, decoded));
    REQUIRE(cna_cnb_texture_data_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(texture) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_schema1_format_restriction(void)
{
    uint8_t image[4096];
    uint64_t value = 0U;
    CNA_CnbTextureDataHandle texture = CNA_INVALID_HANDLE;
    unsigned char block[16];

    /* Every identifier can be *decoded*; schema 1 only ever *encodes* Rgba8. Refusing here rather
       than writing a file no reader of this schema would accept is the contract, and it is the one
       asymmetry in the family worth pinning. */
    REQUIRE(cna_cnb_texture_data_create(4U, 4U, 1U, 1U, 1U, &texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_add_representation(texture, CNA_CNB_TEXTURE_FORMAT_BC7, &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_get_texture_level_byte_size(CNA_CNB_TEXTURE_FORMAT_BC7, 4U, 4U, 1U, &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value == sizeof(block));
    memset(block, 0x11, sizeof(block));
    REQUIRE(cna_cnb_texture_data_set_level(texture, 0U, 0U, block, sizeof(block)) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_encode_texture2d(texture, view(""), image, sizeof(image), &value) ==
            CNA_RESULT_IO);
    REQUIRE(cna_cnb_texture_data_destroy(texture) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_embedded_atlas(void)
{
    static const unsigned char rgba[2U * 2U * 4U] = {
        1U, 2U, 3U, 4U,      5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U,   13U, 14U, 15U, 16U};
    uint8_t image[4096];
    uint64_t imageSize = 0U;
    CNA_CnbTextureDataHandle atlas = CNA_INVALID_HANDLE;
    CNA_CnbTextureDataHandle readBack = CNA_INVALID_HANDLE;
    CNA_CnbWriterHandle writer = CNA_INVALID_HANDLE;
    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    CNA_CnbChunkId body = 0U;
    uint32_t number = UINT32_MAX;

    /* A sprite font embeds its atlas with exactly the chunks a standalone texture would use, in a
       file whose asset type is not a texture at all -- so the round trip has to work from a
       document the texture decoders would refuse. */
    REQUIRE(cna_cnb_texture_data_create_rgba8(2U, 2U, rgba, sizeof(rgba), &atlas) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_make_chunk_id('S', 'F', 'N', 'T', &body) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_SPRITE_FONT, UINT32_C(1), &writer) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_add_chunk(writer, body, rgba, 4U, CNA_CNB_CHUNK_FLAG_NONE, 4U) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_append_embedded_texture2d(writer, atlas, view("SpriteFont")) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_build(writer, image, sizeof(image), &imageSize) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_document_parse(image, imageSize, view("font.cnb"), 0, &document) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &number) == CNA_RESULT_SUCCESS);
    REQUIRE(number == CNA_CNB_ASSET_TYPE_SPRITE_FONT);
    REQUIRE(cna_cnb_document_read_embedded_texture2d(document, view("SpriteFont"), &readBack) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(expect_same_texture(atlas, readBack));
    /* The texture decoders still refuse it, because its asset type is a font. */
    {
        CNA_CnbTextureDataHandle wrong = UINT64_MAX;
        REQUIRE(cna_cnb_decode_texture2d(document, &wrong) == CNA_RESULT_IO);
        REQUIRE(wrong == CNA_INVALID_HANDLE);
    }

    REQUIRE(cna_cnb_texture_data_destroy(readBack) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_destroy(writer) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_texture_data_destroy(atlas) == CNA_RESULT_SUCCESS);
    return 1;
}

int main(void)
{
    if (!validate_format_identities()) { return 1; }
    if (!validate_surface_format_mapping()) { return 2; }
    if (!validate_texture_data()) { return 3; }
    if (!validate_schemas()) { return 4; }
    if (!validate_schema1_format_restriction()) { return 5; }
    if (!validate_embedded_atlas()) { return 6; }
    return 0;
}
