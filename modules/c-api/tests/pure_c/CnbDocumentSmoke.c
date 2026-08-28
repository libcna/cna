// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-107 -- the `.cnb` document, its bounded cursor and the two writers.
 *
 * The suite needs no fixture file, and that is deliberate: it **builds** a container with the
 * writer and then parses it back, so the two ends of the format are checked against each other
 * rather than against a blob somebody once produced. A fixture can only prove the reader still
 * reads it; a round trip proves the writer still produces something the reader accepts, which is
 * the invariant the whole format rests on.
 *
 * Three properties get more attention than the rest, because they are the ones a C caller cannot
 * see for itself: that a reader created over a caller's buffer holds a **copy**, that a reader
 * opened from a document **blocks that document's release**, and that a refused call consumed
 * nothing.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CnbDocumentSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static CNA_StringView view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

static int untouched(const unsigned char* const bytes, const size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        if (bytes[index] != 0x5AU) {
            return 0;
        }
    }
    return 1;
}

/* ---- the primitive writer -------------------------------------------------------------------- */

static int validate_byte_writer(void)
{
    CNA_CnbByteWriterHandle writer = CNA_INVALID_HANDLE;
    uint64_t size = UINT64_MAX;
    uint64_t bytes = UINT64_MAX;
    uint8_t buffer[128];
    uint8_t sentinel[128];

    REQUIRE(cna_cnb_byte_writer_create(0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_byte_writer_create(&writer) == CNA_RESULT_SUCCESS);
    REQUIRE(writer != CNA_INVALID_HANDLE);

    REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_SUCCESS && size == 0U);

    /* Little-endian, byte by byte: the whole point of the encoding is that it does not depend on
       the host, so the bytes are asserted rather than the value round-tripping. */
    REQUIRE(cna_cnb_byte_writer_write_u32(writer, UINT32_C(0x11223344)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_copy_bytes(writer, buffer, sizeof(buffer), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 4U);
    REQUIRE(buffer[0] == 0x44U && buffer[1] == 0x33U && buffer[2] == 0x22U && buffer[3] == 0x11U);

    REQUIRE(cna_cnb_byte_writer_write_u8(writer, 0x7FU) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u16(writer, UINT16_C(0xBEEF)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u64(writer, UINT64_C(0x0102030405060708)) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_i32(writer, INT32_C(-2)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_f32(writer, 1.5F) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_f64(writer, -0.25) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_string(writer, view("hi")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_zeros(writer, 3U) == CNA_RESULT_SUCCESS);
    {
        static const unsigned char raw[] = {0xAAU, 0xBBU};
        REQUIRE(cna_cnb_byte_writer_write_bytes(writer, raw, sizeof(raw)) == CNA_RESULT_SUCCESS);
    }
    /* 4 + 1 + 2 + 8 + 4 + 4 + 8 + (4 + 2) + 3 + 2 */
    REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_SUCCESS && size == 42U);

    /* A malformed string is refused, and refusing it appends nothing. */
    {
        const char bad[] = {'a', (char)0xFFU};
        const CNA_StringView malformed = {bad, sizeof(bad)};
        REQUIRE(cna_cnb_byte_writer_write_string(writer, malformed) == CNA_RESULT_ENCODING);
        REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_SUCCESS && size == 42U);
    }

    memset(buffer, 0x5A, sizeof(buffer));
    memcpy(sentinel, buffer, sizeof(sentinel));
    (void)sentinel;
    bytes = UINT64_MAX;
    REQUIRE(cna_cnb_byte_writer_copy_bytes(writer, buffer, 4U, &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == 42U && untouched(buffer, sizeof(buffer)));

    /* Taking is destructive, so a capacity too small must leave the bytes where they are. */
    REQUIRE(cna_cnb_byte_writer_take(writer, buffer, 4U, &bytes) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(bytes == 42U && untouched(buffer, sizeof(buffer)));
    REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_SUCCESS && size == 42U);

    REQUIRE(cna_cnb_byte_writer_take(writer, buffer, sizeof(buffer), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(bytes == 42U);
    REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_SUCCESS && size == 0U);

    REQUIRE(cna_cnb_byte_writer_destroy(writer) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_get_size(writer, &size) == CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_byte_writer_destroy(writer) == CNA_RESULT_INVALID_HANDLE);

    /* A writer seeded from existing bytes continues after them. */
    {
        static const unsigned char initial[] = {1U, 2U, 3U};
        CNA_CnbByteWriterHandle seeded = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_byte_writer_create_from_bytes(initial, sizeof(initial), &seeded) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_write_u8(seeded, 4U) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_byte_writer_copy_bytes(seeded, buffer, sizeof(buffer), &bytes) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(bytes == 4U && buffer[0] == 1U && buffer[3] == 4U);
        REQUIRE(cna_cnb_byte_writer_destroy(seeded) == CNA_RESULT_SUCCESS);
    }
    return 1;
}

/* ---- the bounded cursor ---------------------------------------------------------------------- */

static int validate_reader(void)
{
    CNA_CnbByteWriterHandle writer = CNA_INVALID_HANDLE;
    CNA_CnbReaderHandle reader = CNA_INVALID_HANDLE;
    uint8_t encoded[128];
    uint64_t bytes = UINT64_MAX;
    uint64_t value64 = 0U;
    uint32_t value32 = 0U;
    uint16_t value16 = 0U;
    uint8_t value8 = 0U;
    int32_t signed32 = 0;
    float single = 0.0F;
    double doubled = 0.0;
    char text[64];

    REQUIRE(cna_cnb_byte_writer_create(&writer) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u8(writer, 0x7FU) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u16(writer, UINT16_C(0xBEEF)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u32(writer, UINT32_C(0x11223344)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u64(writer, UINT64_C(0x0102030405060708)) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_i32(writer, INT32_C(-2)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_f32(writer, 1.5F) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_f64(writer, -0.25) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_string(writer, view("chunk")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u32(writer, UINT32_C(2)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u16(writer, UINT16_C(7)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_write_u16(writer, UINT16_C(9)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_take(writer, encoded, sizeof(encoded), &bytes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_byte_writer_destroy(writer) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_reader_create(encoded, bytes, view("smoke region"), 0, &reader) ==
            CNA_RESULT_SUCCESS);

    /* The copy is the deviation this route documents: overwriting the caller's buffer now must not
       change a single value the cursor goes on to read. Without this assertion the copy and a
       borrow are indistinguishable from C. */
    memset(encoded, 0, sizeof(encoded));

    REQUIRE(cna_cnb_reader_get_size(reader, &value64) == CNA_RESULT_SUCCESS && value64 == bytes);
    REQUIRE(cna_cnb_reader_get_position(reader, &value64) == CNA_RESULT_SUCCESS && value64 == 0U);
    REQUIRE(cna_cnb_reader_get_remaining(reader, &value64) == CNA_RESULT_SUCCESS &&
            value64 == bytes);
    REQUIRE(cna_cnb_reader_get_context_size(reader, &value64) == CNA_RESULT_SUCCESS &&
            value64 == 12U);
    REQUIRE(cna_cnb_reader_copy_context(reader, text, sizeof(text), &value64) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value64 == 12U && memcmp(text, "smoke region", 12U) == 0);

    REQUIRE(cna_cnb_reader_read_u8(reader, &value8) == CNA_RESULT_SUCCESS && value8 == 0x7FU);
    REQUIRE(cna_cnb_reader_read_u16(reader, &value16) == CNA_RESULT_SUCCESS &&
            value16 == UINT16_C(0xBEEF));
    REQUIRE(cna_cnb_reader_read_u32(reader, &value32) == CNA_RESULT_SUCCESS &&
            value32 == UINT32_C(0x11223344));
    REQUIRE(cna_cnb_reader_read_u64(reader, &value64) == CNA_RESULT_SUCCESS &&
            value64 == UINT64_C(0x0102030405060708));
    REQUIRE(cna_cnb_reader_read_i32(reader, &signed32) == CNA_RESULT_SUCCESS && signed32 == -2);
    REQUIRE(cna_cnb_reader_read_f32(reader, &single) == CNA_RESULT_SUCCESS && single == 1.5F);
    REQUIRE(cna_cnb_reader_read_f64(reader, &doubled) == CNA_RESULT_SUCCESS && doubled == -0.25);

    /* Reading a string consumes; copying it does not, which is why they are two routes. */
    REQUIRE(cna_cnb_reader_read_string(reader, &value64) == CNA_RESULT_SUCCESS && value64 == 5U);
    REQUIRE(cna_cnb_reader_copy_string(reader, text, 2U, &value64) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(value64 == 5U);
    REQUIRE(cna_cnb_reader_copy_string(reader, text, sizeof(text), &value64) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value64 == 5U && memcmp(text, "chunk", 5U) == 0);

    /* A count is checked against what could actually follow it. */
    REQUIRE(cna_cnb_reader_read_count(reader, 2U, view("samples"), &value32) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value32 == 2U);

    /* The size is the caller's own argument, so a short capacity is settled before the cursor
       moves: the position afterwards proves nothing was consumed. */
    {
        uint8_t taken[4];
        uint64_t before = 0U;
        uint64_t after = 0U;
        REQUIRE(cna_cnb_reader_get_position(reader, &before) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_reader_read_bytes(reader, 4U, taken, 2U, &value64) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
        REQUIRE(value64 == 4U);
        REQUIRE(cna_cnb_reader_get_position(reader, &after) == CNA_RESULT_SUCCESS);
        REQUIRE(before == after);
        REQUIRE(cna_cnb_reader_read_bytes(reader, 4U, taken, sizeof(taken), &value64) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(value64 == 4U && taken[0] == 7U && taken[2] == 9U);
    }

    REQUIRE(cna_cnb_reader_require_exhausted(reader) == CNA_RESULT_SUCCESS);

    /* Past the end is a content problem, and it leaves the caller's value alone. */
    value32 = UINT32_C(0xABCDEF01);
    REQUIRE(cna_cnb_reader_read_u32(reader, &value32) == CNA_RESULT_IO);
    REQUIRE(value32 == UINT32_C(0xABCDEF01));
    REQUIRE(cna_cnb_reader_skip(reader, 1U) == CNA_RESULT_IO);

    /* A schema decoder's own refusal reads exactly like the cursor's. */
    REQUIRE(cna_cnb_reader_fail(reader, view("the track index is out of range")) ==
            CNA_RESULT_IO);

    REQUIRE(cna_cnb_reader_destroy(reader) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_reader_get_size(reader, &value64) == CNA_RESULT_INVALID_HANDLE);

    /* Copying a string before any read is a state error, not an empty answer. */
    {
        CNA_CnbReaderHandle fresh = CNA_INVALID_HANDLE;
        static const unsigned char one[] = {1U};
        REQUIRE(cna_cnb_reader_create(one, sizeof(one), view("fresh"), 0, &fresh) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_reader_copy_string(fresh, text, sizeof(text), &value64) ==
                CNA_RESULT_INVALID_STATE);
        REQUIRE(cna_cnb_reader_destroy(fresh) == CNA_RESULT_SUCCESS);
    }

    /* A supplied limit is honoured: a one-byte string ceiling refuses a longer one. */
    {
        CNA_CnbReaderHandle limited = CNA_INVALID_HANDLE;
        CNA_CnbReadLimits limits;
        static const unsigned char encoded_string[] = {5U, 0U, 0U, 0U, 'c', 'h', 'u', 'n', 'k'};
        memset(&limits, 0, sizeof(limits));
        limits.struct_size = (uint32_t)sizeof(limits);
        limits.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
        REQUIRE(cna_cnb_read_limits_init(&limits) == CNA_RESULT_SUCCESS);
        limits.max_string_bytes = 1U;
        REQUIRE(cna_cnb_reader_create(encoded_string, sizeof(encoded_string), view("limited"),
                                      &limits, &limited) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_reader_read_string(limited, &value64) == CNA_RESULT_IO);
        REQUIRE(cna_cnb_reader_destroy(limited) == CNA_RESULT_SUCCESS);
    }
    return 1;
}

static int validate_utf8_check(void)
{
    CNA_Bool ok = UINT8_C(9);
    const char bad[] = {'a', (char)0xFFU};
    const CNA_StringView malformed = {bad, sizeof(bad)};

    REQUIRE(cna_cnb_is_well_formed_utf8(view("plain"), &ok) == CNA_RESULT_SUCCESS &&
            ok == CNA_TRUE);
    REQUIRE(cna_cnb_is_well_formed_utf8(view(""), &ok) == CNA_RESULT_SUCCESS && ok == CNA_TRUE);
    /* Malformed input is the question, not an error -- the same rule the logical-name routes
       follow, and for the same reason. */
    REQUIRE(cna_cnb_is_well_formed_utf8(malformed, &ok) == CNA_RESULT_SUCCESS && ok == CNA_FALSE);
    REQUIRE(cna_cnb_is_well_formed_utf8(view("x"), 0) == CNA_RESULT_INVALID_ARGUMENT);
    {
        const CNA_StringView null_with_length = {0, 3U};
        REQUIRE(cna_cnb_is_well_formed_utf8(null_with_length, &ok) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }
    return 1;
}

/* ---- the container round trip ----------------------------------------------------------------- */

static int build_document(uint8_t* const image, const uint64_t capacity, uint64_t* const outSize)
{
    CNA_CnbWriterHandle writer = CNA_INVALID_HANDLE;
    CNA_CnbChunkId head = 0U;
    CNA_CnbChunkId body = 0U;
    CNA_CnbExternalReference reference;
    static const unsigned char head_bytes[] = {1U, 2U, 3U, 4U};
    static const unsigned char body_bytes[] = {9U, 8U, 7U};
    uint64_t count = UINT64_MAX;

    REQUIRE(cna_cnb_make_chunk_id('H', 'E', 'A', 'D', &head) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_make_chunk_id('B', 'O', 'D', 'Y', &body) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_CURVE, UINT32_C(1), &writer) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_get_schema_chunk_count(writer, &count) == CNA_RESULT_SUCCESS &&
            count == 0U);

    REQUIRE(cna_cnb_writer_set_metadata(writer, view("Microsoft.Xna.Framework.Curve"),
                                        view("curves/wobble")) == CNA_RESULT_SUCCESS);

    memset(&reference, 0, sizeof(reference));
    reference.struct_size = (uint32_t)sizeof(reference);
    reference.struct_version = CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION;
    reference.expected_asset_type_id = CNA_CNB_ASSET_TYPE_TEXTURE2D;
    REQUIRE(cna_cnb_writer_add_external_reference(writer, &reference, view("Textures/hero.png")) ==
            CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_writer_add_chunk(writer, head, head_bytes, sizeof(head_bytes),
                                     CNA_CNB_CHUNK_FLAG_MANDATORY, 4U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_add_chunk(writer, body, body_bytes, sizeof(body_bytes),
                                     CNA_CNB_CHUNK_FLAG_NONE, 16U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_get_schema_chunk_count(writer, &count) == CNA_RESULT_SUCCESS &&
            count == 2U);

    /* The container's own identifiers belong to the container, not to a schema: adding one here
       would produce a file carrying two of a singleton the reader requires to be unique. */
    REQUIRE(cna_cnb_writer_add_chunk(writer, CNA_CNB_CONTAINER_CHUNK_METADATA, head_bytes,
                                     sizeof(head_bytes), CNA_CNB_CHUNK_FLAG_NONE, 4U) ==
            CNA_RESULT_IO);
    {
        CNA_CnbChunkId malformed = 0U;
        REQUIRE(cna_cnb_make_chunk_id(0x01U, 'B', 'A', 'D', &malformed) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_add_chunk(writer, malformed, head_bytes, sizeof(head_bytes),
                                         CNA_CNB_CHUNK_FLAG_NONE, 4U) == CNA_RESULT_IO);
    }
    REQUIRE(cna_cnb_writer_get_schema_chunk_count(writer, &count) == CNA_RESULT_SUCCESS &&
            count == 2U);

    /* Assembling is non-destructive, so the two-call sizing pattern works. */
    REQUIRE(cna_cnb_writer_build(writer, 0, 0U, outSize) == CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(*outSize > CNA_CNB_FORMAT_HEADER_SIZE && *outSize <= capacity);
    REQUIRE(cna_cnb_writer_build(writer, image, capacity, outSize) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_writer_destroy(writer) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_document(void)
{
    uint8_t image[4096];
    uint64_t imageSize = 0U;
    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    CNA_CnbChunkEntry entry;
    CNA_CnbMetadata metadata;
    CNA_CnbExternalReference reference;
    CNA_CnbReadLimits limits;
    CNA_CnbChunkId head = 0U;
    CNA_CnbChunkId body = 0U;
    CNA_CnbChunkId absent = 0U;
    CNA_Bool flag = UINT8_C(9);
    uint64_t value = UINT64_MAX;
    uint16_t version = UINT16_MAX;
    uint32_t number = UINT32_MAX;
    uint8_t data[64];
    char text[128];

    REQUIRE(build_document(image, sizeof(image), &imageSize));
    REQUIRE(cna_cnb_make_chunk_id('H', 'E', 'A', 'D', &head) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_make_chunk_id('B', 'O', 'D', 'Y', &body) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_make_chunk_id('N', 'O', 'P', 'E', &absent) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_has_magic(image, imageSize, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE);
    {
        static const unsigned char garbage[] = {1U, 2U, 3U, 4U};
        REQUIRE(cna_cnb_has_magic(garbage, sizeof(garbage), &flag) == CNA_RESULT_SUCCESS &&
                flag == CNA_FALSE);
        /* Too few bytes to hold the magic is an answer, not an error. */
        REQUIRE(cna_cnb_has_magic(image, 2U, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE);
    }

    REQUIRE(cna_cnb_document_parse(image, imageSize, view("round-trip.cnb"), 0, &document) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(document != CNA_INVALID_HANDLE);

    REQUIRE(cna_cnb_document_get_origin_size(document, &value) == CNA_RESULT_SUCCESS &&
            value == 14U);
    REQUIRE(cna_cnb_document_copy_origin(document, text, sizeof(text), &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value == 14U && memcmp(text, "round-trip.cnb", 14U) == 0);

    REQUIRE(cna_cnb_document_get_container_major(document, &version) == CNA_RESULT_SUCCESS);
    REQUIRE(version == (uint16_t)CNA_CNB_FORMAT_CONTAINER_MAJOR);
    REQUIRE(cna_cnb_document_get_container_minor(document, &version) == CNA_RESULT_SUCCESS);
    REQUIRE(version == (uint16_t)CNA_CNB_FORMAT_CONTAINER_MINOR);
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &number) == CNA_RESULT_SUCCESS &&
            number == CNA_CNB_ASSET_TYPE_CURVE);
    REQUIRE(cna_cnb_document_get_asset_schema_version(document, &number) == CNA_RESULT_SUCCESS &&
            number == UINT32_C(1));

    /* Two schema chunks plus the container's own `CMET` and `XREF`, which are always emitted
       first -- which is exactly why a schema addresses chunks by type rather than by index. */
    REQUIRE(cna_cnb_document_get_chunk_count(document, &value) == CNA_RESULT_SUCCESS &&
            value == 4U);

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = (uint32_t)sizeof(entry);
    entry.struct_version = CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION;
    {
        uint64_t headIndex = UINT64_MAX;
        REQUIRE(cna_cnb_document_require_single(document, head, &headIndex) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_document_get_chunk(document, headIndex, &entry) == CNA_RESULT_SUCCESS);
        REQUIRE(entry.type == head);
        REQUIRE(entry.stored_size == 4U && entry.uncompressed_size == 4U);
        REQUIRE(entry.compression == CNA_CNB_COMPRESSION_NONE);
        REQUIRE(entry.alignment == 4U && (entry.offset % 4U) == 0U);
        REQUIRE(entry.flags == CNA_CNB_CHUNK_FLAG_MANDATORY);
        REQUIRE(cna_cnb_chunk_entry_is_mandatory(&entry, &flag) == CNA_RESULT_SUCCESS &&
                flag == CNA_TRUE);
        /* The stored checksum is the CRC-32C of the stored bytes, which is checkable from C using
           the routes CBIND-106 published -- the two halves of the format agreeing. */
        REQUIRE(cna_cnb_crc32c(image + entry.offset, entry.stored_size, &number) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(number == entry.checksum);

        REQUIRE(cna_cnb_document_copy_chunk_data(document, headIndex, data, sizeof(data),
                                                 &value) == CNA_RESULT_SUCCESS);
        REQUIRE(value == 4U && data[0] == 1U && data[3] == 4U);
        REQUIRE(cna_cnb_document_copy_chunk_data(document, headIndex, data, 2U, &value) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
        REQUIRE(value == 4U);

        /* A reader opened from the document borrows its bytes, so the document cannot be released
           while it lives. That is the whole lifetime contract of this family, seen from C. */
        {
            CNA_CnbReaderHandle chunkReader = CNA_INVALID_HANDLE;
            REQUIRE(cna_cnb_document_open_chunk(document, headIndex, &chunkReader) ==
                    CNA_RESULT_SUCCESS);
            REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_INVALID_STATE);
            REQUIRE(cna_cnb_reader_read_u8(chunkReader, &data[0]) == CNA_RESULT_SUCCESS &&
                    data[0] == 1U);
            REQUIRE(cna_cnb_reader_get_remaining(chunkReader, &value) == CNA_RESULT_SUCCESS &&
                    value == 3U);
            REQUIRE(cna_cnb_reader_destroy(chunkReader) == CNA_RESULT_SUCCESS);
        }
    }

    {
        uint64_t indices[8];
        REQUIRE(cna_cnb_document_find_all(document, body, indices, 8U, &value) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(value == 1U);
        REQUIRE(cna_cnb_document_find_all(document, absent, indices, 8U, &value) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(value == 0U);
        REQUIRE(cna_cnb_document_find_all(document, body, indices, 0U, &value) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
        REQUIRE(value == 1U);
    }

    /* Absence is an ordinary answer; more than one would be a malformed file. */
    REQUIRE(cna_cnb_document_find_single(document, absent, &flag, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(flag == CNA_FALSE);
    REQUIRE(cna_cnb_document_find_single(document, body, &flag, &value) == CNA_RESULT_SUCCESS);
    REQUIRE(flag == CNA_TRUE);
    REQUIRE(cna_cnb_document_require_single(document, absent, &value) == CNA_RESULT_IO);

    /* An unknown *mandatory* chunk costs the file; an unknown optional one is ignored, which is
       what lets a newer writer add data an older reader can skip. */
    {
        const CNA_CnbChunkId knownBoth[] = {head, body};
        const CNA_CnbChunkId knownOptionalOnly[] = {body};
        REQUIRE(cna_cnb_document_require_mandatory_chunks_understood(document, knownBoth, 2U) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_document_require_mandatory_chunks_understood(
                    document, knownOptionalOnly, 1U) == CNA_RESULT_IO);
    }

    memset(&metadata, 0, sizeof(metadata));
    metadata.struct_size = (uint32_t)sizeof(metadata);
    metadata.struct_version = CNA_CNB_METADATA_STRUCT_VERSION;
    REQUIRE(cna_cnb_document_get_metadata(document, &metadata) == CNA_RESULT_SUCCESS);
    REQUIRE(metadata.present == CNA_TRUE && metadata.flags == 0U);
    REQUIRE(cna_cnb_document_get_metadata_asset_type_name_size(document, &value) ==
            CNA_RESULT_SUCCESS && value == 29U);
    REQUIRE(cna_cnb_document_copy_metadata_asset_type_name(document, text, sizeof(text), &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value == 29U && memcmp(text, "Microsoft.Xna.Framework.Curve", 29U) == 0);
    REQUIRE(cna_cnb_document_get_metadata_content_name_size(document, &value) ==
            CNA_RESULT_SUCCESS && value == 13U);
    REQUIRE(cna_cnb_document_copy_metadata_content_name(document, text, sizeof(text), &value) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(value == 13U && memcmp(text, "curves/wobble", 13U) == 0);

    REQUIRE(cna_cnb_document_get_external_reference_count(document, &value) ==
            CNA_RESULT_SUCCESS && value == 1U);
    memset(&reference, 0, sizeof(reference));
    reference.struct_size = (uint32_t)sizeof(reference);
    reference.struct_version = CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION;
    REQUIRE(cna_cnb_document_get_external_reference(document, 0U, view("the curve's texture"),
                                                    &reference) == CNA_RESULT_SUCCESS);
    REQUIRE(reference.flags == 0U);
    REQUIRE(reference.expected_asset_type_id == CNA_CNB_ASSET_TYPE_TEXTURE2D);
    REQUIRE(cna_cnb_document_get_external_reference_name_size(document, 0U, &value) ==
            CNA_RESULT_SUCCESS && value == 17U);
    REQUIRE(cna_cnb_document_copy_external_reference_name(document, 0U, text, sizeof(text),
                                                          &value) == CNA_RESULT_SUCCESS);
    REQUIRE(value == 17U && memcmp(text, "Textures/hero.png", 17U) == 0);

    REQUIRE(cna_cnb_document_require_asset(document, CNA_CNB_ASSET_TYPE_CURVE, UINT32_C(1)) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_require_asset(document, CNA_CNB_ASSET_TYPE_MODEL, UINT32_C(1)) ==
            CNA_RESULT_IO);
    REQUIRE(cna_cnb_document_require_asset(document, CNA_CNB_ASSET_TYPE_CURVE, UINT32_C(0)) ==
            CNA_RESULT_IO);

    memset(&limits, 0, sizeof(limits));
    limits.struct_size = (uint32_t)sizeof(limits);
    limits.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
    REQUIRE(cna_cnb_document_get_limits(document, &limits) == CNA_RESULT_SUCCESS);
    REQUIRE(limits.max_chunk_alignment == UINT32_C(4096));

    /* An index a caller supplied is an argument, not a corrupt file, and the two are different
       results because a caller acts on them differently. */
    REQUIRE(cna_cnb_document_get_chunk(document, 99U, &entry) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_document_copy_chunk_data(document, 99U, data, sizeof(data), &value) ==
            CNA_RESULT_INVALID_ARGUMENT);
    {
        CNA_CnbReaderHandle nope = CNA_INVALID_HANDLE;
        REQUIRE(cna_cnb_document_open_chunk(document, 99U, &nope) == CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(nope == CNA_INVALID_HANDLE);
    }
    REQUIRE(cna_cnb_document_get_external_reference(document, 99U, view("x"), &reference) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_document_get_external_reference_name_size(document, 99U, &value) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* An unknown structure version is refused whatever else is right. */
    entry.struct_version = CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION + 1U;
    REQUIRE(cna_cnb_document_get_chunk(document, 0U, &entry) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_chunk_count(document, &value) == CNA_RESULT_INVALID_HANDLE);

    /* A byte image that is not a container at all is a content problem, not an argument one. */
    {
        CNA_CnbDocumentHandle broken = UINT64_MAX;
        static const unsigned char garbage[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
        REQUIRE(cna_cnb_document_parse(garbage, sizeof(garbage), view("garbage"), 0, &broken) ==
                CNA_RESULT_IO);
        REQUIRE(broken == CNA_INVALID_HANDLE);
    }
    /* And one truncated in the middle is refused too, rather than half-read. */
    {
        CNA_CnbDocumentHandle broken = UINT64_MAX;
        REQUIRE(cna_cnb_document_parse(image, imageSize - 1U, view("short"), 0, &broken) ==
                CNA_RESULT_IO);
        REQUIRE(broken == CNA_INVALID_HANDLE);
    }
    return 1;
}

static int validate_writer_limits_and_files(void)
{
    static const char* const path = "cna_c_api_cnb_round_trip.cnb";
    uint8_t image[4096];
    uint64_t imageSize = 0U;
    CNA_CnbWriterHandle writer = CNA_INVALID_HANDLE;
    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    CNA_CnbReadLimits limits;
    CNA_CnbChunkId body = 0U;
    static const unsigned char body_bytes[] = {5U, 5U, 5U, 5U};
    uint64_t value = UINT64_MAX;
    CNA_Bool supported = UINT8_C(9);

    REQUIRE(cna_cnb_make_chunk_id('B', 'O', 'D', 'Y', &body) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_CURVE, UINT32_C(1), &writer) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_add_chunk(writer, body, body_bytes, sizeof(body_bytes),
                                     CNA_CNB_CHUNK_FLAG_NONE, 4U) == CNA_RESULT_SUCCESS);

    memset(&limits, 0, sizeof(limits));
    limits.struct_size = (uint32_t)sizeof(limits);
    limits.struct_version = CNA_CNB_READ_LIMITS_STRUCT_VERSION;
    REQUIRE(cna_cnb_writer_get_limits(writer, &limits) == CNA_RESULT_SUCCESS);
    REQUIRE(limits.max_file_size == UINT64_C(512) * 1024U * 1024U);

    /* The producer is the right place to find out that a reader would refuse the file. */
    limits.max_file_size = 16U;
    REQUIRE(cna_cnb_writer_set_limits(writer, &limits) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_build(writer, image, sizeof(image), &value) == CNA_RESULT_IO);
    REQUIRE(cna_cnb_writer_get_limits(writer, &limits) == CNA_RESULT_SUCCESS);
    REQUIRE(limits.max_file_size == 16U);

    REQUIRE(cna_cnb_read_limits_init(&limits) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_set_limits(writer, &limits) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_build(writer, image, sizeof(image), &imageSize) == CNA_RESULT_SUCCESS);

    /* A codec with an identifier and no implementation is not a bad argument. */
    REQUIRE(cna_cnb_writer_set_compression(writer, CNA_CNB_COMPRESSION_LZ4, 3) ==
            CNA_RESULT_NOT_SUPPORTED);
    REQUIRE(cna_cnb_writer_set_compression(writer, CNA_CNB_COMPRESSION_NONE, 3) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_is_compression_supported(CNA_CNB_COMPRESSION_ZSTD, &supported) ==
            CNA_RESULT_SUCCESS);
    if (supported == CNA_TRUE) {
        uint8_t compressed[4096];
        uint64_t compressedSize = 0U;
        CNA_CnbDocumentHandle roundTrip = CNA_INVALID_HANDLE;
        uint8_t chunk[16];
        REQUIRE(cna_cnb_writer_set_compression(writer, CNA_CNB_COMPRESSION_ZSTD, 3) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_build(writer, compressed, sizeof(compressed), &compressedSize) ==
                CNA_RESULT_SUCCESS);
        /* Whether this tiny chunk actually compressed is the writer's per-chunk decision; what
           must hold either way is that the file still parses and the logical bytes come back. */
        REQUIRE(cna_cnb_document_parse(compressed, compressedSize, view("compressed"), 0,
                                       &roundTrip) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_document_copy_chunk_data(roundTrip, 0U, chunk, sizeof(chunk), &value) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(value == sizeof(body_bytes) && memcmp(chunk, body_bytes, sizeof(body_bytes)) == 0);
        REQUIRE(cna_cnb_document_destroy(roundTrip) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_set_compression(writer, CNA_CNB_COMPRESSION_NONE, 3) ==
                CNA_RESULT_SUCCESS);
    }

    /* Through the filesystem, which is the path a tool actually takes. */
    REQUIRE(cna_cnb_writer_write_to_file(writer, view(path)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_write_to_file(writer, view("")) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_document_parse_file(view(path), 0, &document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_get_chunk_count(document, &value) == CNA_RESULT_SUCCESS &&
            value == 1U);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_parse_file(view("cna_c_api_cnb_absent.cnb"), 0, &document) ==
            CNA_RESULT_IO);
    (void)remove(path);

    /* Clearing the table and re-appending is the canonical whole-table setter in two calls. */
    {
        CNA_CnbExternalReference reference;
        memset(&reference, 0, sizeof(reference));
        reference.struct_size = (uint32_t)sizeof(reference);
        reference.struct_version = CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION;
        REQUIRE(cna_cnb_writer_add_external_reference(writer, &reference, view("a/b.png")) ==
                CNA_RESULT_SUCCESS);
        /* A name the reader would refuse is refused by the writer too -- one rule, both ends. */
        REQUIRE(cna_cnb_writer_add_external_reference(writer, &reference, view("../escape")) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_build(writer, image, sizeof(image), &value) == CNA_RESULT_IO);
        REQUIRE(cna_cnb_writer_clear_external_references(writer) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_build(writer, image, sizeof(image), &value) == CNA_RESULT_SUCCESS);
    }

    REQUIRE(cna_cnb_writer_destroy(writer) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_get_schema_chunk_count(writer, &value) == CNA_RESULT_INVALID_HANDLE);

    /* A custom asset type must carry the canonical name that mints its identifier, and the writer
       refuses to produce a file that could later be decoded as the wrong type. */
    {
        CNA_CnbWriterHandle custom = CNA_INVALID_HANDLE;
        uint32_t minted = 0U;
        REQUIRE(cna_cnb_asset_type_id_from_name(view("MyGame.Level"), &minted) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_create(minted, UINT32_C(1), &custom) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_add_chunk(custom, body, body_bytes, sizeof(body_bytes),
                                         CNA_CNB_CHUNK_FLAG_NONE, 4U) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_build(custom, image, sizeof(image), &value) == CNA_RESULT_IO);
        REQUIRE(cna_cnb_writer_set_metadata(custom, view("MyGame.Level"), view("")) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_build(custom, image, sizeof(image), &value) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_cnb_writer_destroy(custom) == CNA_RESULT_SUCCESS);
    }

    /* An asset type of zero is not a file this format can describe. */
    {
        CNA_CnbWriterHandle invalid = UINT64_MAX;
        REQUIRE(cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_INVALID, UINT32_C(1), &invalid) ==
                CNA_RESULT_IO);
        REQUIRE(invalid == CNA_INVALID_HANDLE);
        REQUIRE(cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_CURVE, UINT32_C(0), &invalid) ==
                CNA_RESULT_IO);
        REQUIRE(invalid == CNA_INVALID_HANDLE);
    }
    return 1;
}

int main(void)
{
    if (!validate_byte_writer()) { return 1; }
    if (!validate_reader()) { return 2; }
    if (!validate_utf8_check()) { return 3; }
    if (!validate_document()) { return 4; }
    if (!validate_writer_limits_and_files()) { return 5; }
    return 0;
}
