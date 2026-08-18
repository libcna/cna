// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <string.h>

static const char ContainerName[] = "ReaderRoot";
static const char AssetFile[] = "graph.bin";
static const char EffectReaderName[] = "Microsoft.Xna.Framework.Content.EffectReader";
static const char PlaceholderName[] = "cna.Placeholder";

typedef struct ReaderFixture {
    CNA_StorageDeviceHandle device;
    CNA_StorageContainerHandle container;
} ReaderFixture;

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static void push_byte(uint8_t* const data, size_t* const offset, const uint8_t value)
{
    data[(*offset)++] = value;
}

static void push_float(uint8_t* const data, size_t* const offset, const float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    push_byte(data, offset, (uint8_t)(bits & UINT32_C(0xff)));
    push_byte(data, offset, (uint8_t)((bits >> 8U) & UINT32_C(0xff)));
    push_byte(data, offset, (uint8_t)((bits >> 16U) & UINT32_C(0xff)));
    push_byte(data, offset, (uint8_t)((bits >> 24U) & UINT32_C(0xff)));
}

/* 3 protocol bytes + 2+3+4+4+16 floats + 4 color bytes + 4 sphere floats + 4 raw bytes. */
#define ASSET_BYTE_COUNT 143U

static size_t build_asset(uint8_t data[ASSET_BYTE_COUNT])
{
    size_t offset = 0U;
    push_byte(data, &offset, 0x00U); /* type-reader count */
    push_byte(data, &offset, 0x00U); /* shared-resource count */
    push_byte(data, &offset, 0x00U); /* null object reference */

    push_float(data, &offset, 1.0F);
    push_float(data, &offset, 2.0F);

    push_float(data, &offset, 3.0F);
    push_float(data, &offset, 4.0F);
    push_float(data, &offset, 5.0F);

    push_float(data, &offset, 6.0F);
    push_float(data, &offset, 7.0F);
    push_float(data, &offset, 8.0F);
    push_float(data, &offset, 9.0F);

    push_float(data, &offset, 0.25F);
    push_float(data, &offset, 0.5F);
    push_float(data, &offset, 0.75F);
    push_float(data, &offset, 1.0F);

    for (int index = 1; index <= 16; ++index) {
        push_float(data, &offset, (float)index);
    }

    push_byte(data, &offset, 10U);
    push_byte(data, &offset, 20U);
    push_byte(data, &offset, 30U);
    push_byte(data, &offset, 40U);

    push_float(data, &offset, 11.0F);
    push_float(data, &offset, 12.0F);
    push_float(data, &offset, 13.0F);
    push_float(data, &offset, 14.0F);

    push_byte(data, &offset, 0xAAU);
    push_byte(data, &offset, 0xBBU);
    push_byte(data, &offset, 0xCCU);
    push_byte(data, &offset, 0xDDU);
    return offset;
}

static int create_fixture(ReaderFixture* const fixture)
{
    uint8_t asset[ASSET_BYTE_COUNT];
    CNA_StorageStreamHandle writer = CNA_INVALID_HANDLE;

    fixture->device = CNA_INVALID_HANDLE;
    fixture->container = CNA_INVALID_HANDLE;
    if (build_asset(asset) != sizeof(asset)) {
        return 0;
    }
    if (cna_storage_set_app_name_ext(view("cna-c-api-content-reader-smoke")) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_device_show_selector(0, 0, &fixture->device) != CNA_RESULT_SUCCESS ||
        cna_storage_device_delete_container(fixture->device, view(ContainerName)) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_container_open(
            fixture->device,
            view(ContainerName),
            0,
            0,
            &fixture->container) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_storage_container_create_file(fixture->container, view(AssetFile), &writer) ==
            CNA_RESULT_SUCCESS &&
        cna_storage_stream_write(writer, asset, (uint64_t)sizeof(asset)) == CNA_RESULT_SUCCESS &&
        cna_storage_stream_close(writer) == CNA_RESULT_SUCCESS;
}

static int open_asset_stream(
    const ReaderFixture* const fixture,
    CNA_StorageStreamHandle* const outStream)
{
    return cna_storage_container_open_file(
        fixture->container,
        view(AssetFile),
        CNA_FILE_MODE_OPEN,
        outStream) == CNA_RESULT_SUCCESS;
}

static int destroy_fixture(const ReaderFixture* const fixture)
{
    return cna_storage_container_destroy(fixture->container) == CNA_RESULT_SUCCESS &&
        cna_storage_device_delete_container(fixture->device, view(ContainerName)) ==
            CNA_RESULT_SUCCESS &&
        cna_storage_device_destroy(fixture->device) == CNA_RESULT_SUCCESS;
}

static CNA_ContentReaderCreateInfo make_create_info(const CNA_StorageStreamHandle stream)
{
    const CNA_ContentReaderCreateInfo create_info = {
        sizeof(CNA_ContentReaderCreateInfo), UINT32_C(1), CNA_INVALID_HANDLE, stream,
        {"graph", UINT64_C(5)}, INT32_C(5), (uint8_t)'w', {0U, 0U, 0U}
    };
    return create_info;
}

static int validate_identity(const CNA_ContentReaderHandle reader)
{
    char buffer[32];
    uint64_t bytes = 0U;
    int32_t version = 0;
    uint8_t platform = 0U;
    CNA_Handle manager = UINT64_C(7);

    if (cna_content_reader_get_asset_name_size(reader, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(5)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_reader_copy_asset_name(reader, buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(5) || strcmp(buffer, "graph") != 0 ||
        cna_content_reader_copy_asset_name(reader, buffer, UINT64_C(1), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    if (cna_content_reader_get_version(reader, &version) != CNA_RESULT_SUCCESS || version != 5 ||
        cna_content_reader_get_platform(reader, &platform) != CNA_RESULT_SUCCESS ||
        platform != (uint8_t)'w') {
        return 0;
    }
    /* A standalone reader reports no manager rather than inventing one. */
    return cna_content_reader_get_content_manager(reader, &manager) == CNA_RESULT_SUCCESS &&
        manager == CNA_INVALID_HANDLE &&
        cna_content_reader_get_version(reader, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_content_reader_get_version(CNA_INVALID_HANDLE, &version) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_values(const CNA_ContentReaderHandle reader)
{
    CNA_Vector2 vector2 = {0.0F, 0.0F};
    CNA_Vector3 vector3 = {0.0F, 0.0F, 0.0F};
    CNA_Vector4 vector4 = {0.0F, 0.0F, 0.0F, 0.0F};
    CNA_Quaternion quaternion = {0.0F, 0.0F, 0.0F, 0.0F};
    CNA_Matrix matrix;
    CNA_Color color = {0U, 0U, 0U, 0U};
    CNA_BoundingSphere sphere = {{0.0F, 0.0F, 0.0F}, 0.0F};
    CNA_Bool has_value = CNA_TRUE;

    memset(&matrix, 0, sizeof(matrix));

    /* The stream opens on the compiled type-reader table, so the protocol steps run first. */
    if (cna_content_reader_initialize_type_readers(reader) != CNA_RESULT_SUCCESS ||
        cna_content_reader_read_shared_resources(reader) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_content_reader_read_object_tag(reader, &has_value) != CNA_RESULT_SUCCESS ||
        has_value != CNA_FALSE) {
        return 0;
    }

    if (cna_content_reader_read_vector2(reader, &vector2) != CNA_RESULT_SUCCESS ||
        vector2.x != 1.0F || vector2.y != 2.0F) {
        return 0;
    }
    if (cna_content_reader_read_vector3(reader, &vector3) != CNA_RESULT_SUCCESS ||
        vector3.x != 3.0F || vector3.y != 4.0F || vector3.z != 5.0F) {
        return 0;
    }
    if (cna_content_reader_read_vector4(reader, &vector4) != CNA_RESULT_SUCCESS ||
        vector4.x != 6.0F || vector4.y != 7.0F || vector4.z != 8.0F || vector4.w != 9.0F) {
        return 0;
    }
    if (cna_content_reader_read_quaternion(reader, &quaternion) != CNA_RESULT_SUCCESS ||
        quaternion.x != 0.25F || quaternion.y != 0.5F || quaternion.z != 0.75F ||
        quaternion.w != 1.0F) {
        return 0;
    }
    if (cna_content_reader_read_matrix(reader, &matrix) != CNA_RESULT_SUCCESS ||
        matrix.m11 != 1.0F || matrix.m14 != 4.0F || matrix.m22 != 6.0F || matrix.m33 != 11.0F ||
        matrix.m41 != 13.0F || matrix.m44 != 16.0F) {
        return 0;
    }
    if (cna_content_reader_read_color(reader, &color) != CNA_RESULT_SUCCESS || color.r != 10U ||
        color.g != 20U || color.b != 30U || color.a != 40U) {
        return 0;
    }
    return cna_content_reader_read_bounding_sphere(reader, &sphere) == CNA_RESULT_SUCCESS &&
        sphere.center.x == 11.0F && sphere.center.y == 12.0F && sphere.center.z == 13.0F &&
        sphere.radius == 14.0F;
}

static int validate_limits_and_bytes(const CNA_ContentReaderHandle reader)
{
    uint8_t buffer[8];
    uint64_t bytes = 0U;

    if (cna_content_reader_check_collection_element_count(reader, INT64_C(5), view("R")) !=
            CNA_RESULT_SUCCESS ||
        cna_content_reader_check_collection_element_count(reader, INT64_C(-1), view("R")) !=
            CNA_RESULT_IO) {
        return 0;
    }
    if (cna_content_reader_check_decoded_byte_size(reader, INT64_C(1024), view("R")) !=
            CNA_RESULT_SUCCESS ||
        cna_content_reader_check_decoded_byte_size(reader, INT64_C(-1), view("R")) !=
            CNA_RESULT_IO) {
        return 0;
    }

    /* Capacity is decided before the stream is touched, so a refusal costs no bytes. */
    if (cna_content_reader_read_bytes_exact(
            reader, INT32_C(4), view("R"), buffer, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != UINT64_C(4)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_reader_read_bytes_exact(
            reader, INT32_C(4), view("R"), buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(4) || buffer[0] != 0xAAU || buffer[1] != 0xBBU || buffer[2] != 0xCCU ||
        buffer[3] != 0xDDU) {
        return 0;
    }
    if (cna_content_reader_read_bytes_exact(
            reader, INT32_C(-1), view("R"), buffer, (uint64_t)sizeof(buffer), &bytes) !=
        CNA_RESULT_IO) {
        return 0;
    }
    /* The stream is exhausted, so a further exact read is a truncation failure. */
    return cna_content_reader_read_bytes_exact(
        reader, INT32_C(4), view("R"), buffer, (uint64_t)sizeof(buffer), &bytes) == CNA_RESULT_IO;
}

static int validate_reader(const ReaderFixture* const fixture)
{
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    CNA_ContentReaderHandle reader = CNA_INVALID_HANDLE;
    CNA_ContentReaderHandle rejected = UINT64_C(9);

    if (!open_asset_stream(fixture, &stream)) {
        return 0;
    }
    CNA_ContentReaderCreateInfo create_info = make_create_info(stream);

    create_info.reserved[0] = 1U;
    if (cna_content_reader_create(&create_info, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    create_info.reserved[0] = 0U;
    create_info.stream = CNA_INVALID_HANDLE;
    if (cna_content_reader_create(&create_info, &rejected) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    create_info.stream = stream;
    if (cna_content_reader_create(0, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_reader_create(&create_info, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_content_reader_create(&create_info, &reader) != CNA_RESULT_SUCCESS ||
        reader == CNA_INVALID_HANDLE) {
        return 0;
    }

    const int ok = validate_identity(reader) && validate_values(reader) &&
        validate_limits_and_bytes(reader) &&
        /* A borrowed stream cannot be closed out from under its reader. */
        cna_storage_stream_close(stream) == CNA_RESULT_INVALID_STATE;
    if (!ok) {
        (void)cna_content_reader_destroy(reader);
        (void)cna_storage_stream_close(stream);
        return 0;
    }
    /* Destroying the reader closes the stream it borrowed, matching the canonical BinaryReader
       contract; the stream handle itself is still released explicitly. */
    return cna_content_reader_destroy(reader) == CNA_RESULT_SUCCESS &&
        cna_content_reader_destroy(reader) == CNA_RESULT_INVALID_HANDLE &&
        cna_storage_stream_close(stream) == CNA_RESULT_SUCCESS &&
        cna_storage_stream_close(stream) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_type_readers(const ReaderFixture* const fixture)
{
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    CNA_ContentReaderHandle reader = CNA_INVALID_HANDLE;
    CNA_ContentTypeReaderHandle type_reader = CNA_INVALID_HANDLE;
    CNA_ContentTypeReaderHandle placeholder = CNA_INVALID_HANDLE;
    CNA_ContentTypeReaderHandle rejected = UINT64_C(9);
    CNA_Bool flag = CNA_TRUE;
    char buffer[128];
    uint64_t bytes = 0U;
    int32_t version = -1;

    if (cna_content_register_known_unsupported_xnb_readers() != CNA_RESULT_SUCCESS ||
        cna_content_register_known_unsupported_xnb_readers() != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_content_type_reader_manager_get_is_registered(view("cna.NoSuchReader"), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_content_type_reader_manager_create_reader(view("cna.NoSuchReader"), &rejected) !=
            CNA_RESULT_NOT_SUPPORTED ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* CBIND-052A: the known-unsupported hook registers nothing today, and asserting that is the
       point. Its one entry was the general EffectReader, and the compiled Effect Framework work
       replaced that placeholder with a reader that really decodes -- registered by an internal
       entry point with no C form. So the hook stays the published extension point, idempotent
       and empty, and this negative is what would catch an entry silently reappearing in it. */
    if (cna_content_type_reader_manager_get_is_registered(view(EffectReaderName), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_content_type_reader_manager_create_reader(view(EffectReaderName), &rejected) !=
            CNA_RESULT_NOT_SUPPORTED ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Every reader-contract route below is therefore driven through a placeholder the caller
       builds itself, which is the one recognized-but-unsupported reader C can still produce. An
       undefined reason is refused before anything is constructed. */
    if (cna_known_unsupported_content_type_reader_create(
            view(PlaceholderName), UINT32_C(9), &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_known_unsupported_content_type_reader_create(
            view(PlaceholderName),
            CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE,
            &type_reader) != CNA_RESULT_SUCCESS ||
        type_reader == CNA_INVALID_HANDLE) {
        return 0;
    }

    memset(buffer, 0, sizeof(buffer));
    if (cna_content_type_reader_get_target_type_name_size(type_reader, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen(PlaceholderName) ||
        cna_content_type_reader_copy_target_type_name(
            type_reader, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(buffer, PlaceholderName) != 0 ||
        cna_content_type_reader_copy_target_type_name(type_reader, buffer, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    if (cna_content_type_reader_get_type_version(type_reader, &version) != CNA_RESULT_SUCCESS ||
        version != 0 ||
        cna_content_type_reader_supports_version(type_reader, 0, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_content_type_reader_supports_version(type_reader, 1, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_content_type_reader_get_can_deserialize_into_existing_object(type_reader, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_content_type_reader_initialize(type_reader) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (!open_asset_stream(fixture, &stream)) {
        return 0;
    }
    const CNA_ContentReaderCreateInfo create_info = make_create_info(stream);
    if (cna_content_reader_create(&create_info, &reader) != CNA_RESULT_SUCCESS) {
        (void)cna_storage_stream_close(stream);
        return 0;
    }
    /* A recognized but unsupported reader refuses with its canonical diagnostic. */
    const int refused =
        cna_content_type_reader_read_untyped(type_reader, reader, &flag) == CNA_RESULT_IO;
    const int released = cna_content_reader_destroy(reader) == CNA_RESULT_SUCCESS &&
        cna_storage_stream_close(stream) == CNA_RESULT_SUCCESS;
    if (!refused || !released) {
        return 0;
    }

    /* A second placeholder of its own is released twice to prove the handle contract, separately
       from the one carrying the reader-contract routes above. */
    if (cna_known_unsupported_content_type_reader_create(
            view("cna.SecondPlaceholder"),
            CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE,
            &placeholder) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_type_reader_copy_target_type_name(
            placeholder, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(buffer, "cna.SecondPlaceholder") != 0 ||
        cna_content_type_reader_destroy(placeholder) != CNA_RESULT_SUCCESS ||
        cna_content_type_reader_destroy(placeholder) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    if (cna_content_type_reader_destroy(type_reader) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Clearing the process-wide registry succeeds and leaves nothing resolvable, and the
       known-unsupported hook still succeeds afterwards without putting anything back. */
    if (cna_content_type_reader_manager_clear_type_creators() != CNA_RESULT_SUCCESS ||
        cna_content_type_reader_manager_get_is_registered(view(EffectReaderName), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    return cna_content_register_known_unsupported_xnb_readers() == CNA_RESULT_SUCCESS &&
        cna_content_type_reader_manager_get_is_registered(view(EffectReaderName), &flag) ==
            CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
}

/* --------------------------------------------------------------------------------------------
   CBIND-056: a caller-supplied content type reader, driven end to end.

   The registry could previously only hand back readers this library was built with, which made
   the canonical content pipeline extensible in name only. What is asserted here is the whole
   contract: registration and its refusals, one fresh instance per creation, the reader answering
   its own declared identity, the read callback receiving a usable borrowed ContentReader and
   being able to *fail* the read, and unregistration really freeing the name.
   -------------------------------------------------------------------------------------------- */

#define ForeignReaderName "CNA.Test.ForeignReader"
#define ForeignTargetName "CNA.Test.ForeignType"

typedef struct ForeignReaderState {
    int create_calls;
    int read_calls;
    int destroy_calls;
    int refuse_read;
    int borrowed_reader_usable;
    int instance_seal;
} ForeignReaderState;

static CNA_Result foreign_create(void* const context, void** const out_reader_context)
{
    ForeignReaderState* const state = (ForeignReaderState*)context;
    ++state->create_calls;
    *out_reader_context = context;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result foreign_read(
    void* const reader_context,
    const CNA_ContentReaderHandle input,
    void* const existing_object,
    void** const out_object)
{
    ForeignReaderState* const state = (ForeignReaderState*)reader_context;
    int32_t version = -1;
    (void)existing_object;
    ++state->read_calls;
    /* The handle is real and callback-scoped: an ordinary read route answers through it, and it
       refuses to be destroyed from in here. */
    state->borrowed_reader_usable =
        cna_content_reader_get_version(input, &version) == CNA_RESULT_SUCCESS &&
        cna_content_reader_destroy(input) == CNA_RESULT_INVALID_STATE;
    if (state->refuse_read) {
        return CNA_RESULT_IO;
    }
    *out_object = &state->instance_seal;
    return CNA_RESULT_SUCCESS;
}

static void foreign_destroy(void* const reader_context)
{
    ++((ForeignReaderState*)reader_context)->destroy_calls;
}

static CNA_ContentTypeReaderCallbacks make_foreign_callbacks(ForeignReaderState* const state)
{
    CNA_ContentTypeReaderCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.target_type_name = view(ForeignTargetName);
    callbacks.type_version = INT32_C(3);
    callbacks.can_deserialize_into_existing_object = CNA_FALSE;
    callbacks.create = foreign_create;
    callbacks.read = foreign_read;
    callbacks.destroy = foreign_destroy;
    callbacks.context = state;
    return callbacks;
}

static int validate_foreign_registration_refusals(ForeignReaderState* const state)
{
    CNA_Handle rejected = UINT64_C(9);
    CNA_ContentTypeReaderCallbacks callbacks = make_foreign_callbacks(state);

    if (cna_content_type_reader_manager_register(view(ForeignReaderName), &callbacks, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_content_type_reader_manager_register(view(ForeignReaderName), 0, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    callbacks.struct_version = UINT32_C(0);
    if (cna_content_type_reader_manager_register(view(ForeignReaderName), &callbacks, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    callbacks.struct_version = UINT32_C(1);
    callbacks.reserved[1] = 1U;
    if (cna_content_type_reader_manager_register(view(ForeignReaderName), &callbacks, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    callbacks.reserved[1] = 0U;
    callbacks.create = 0;
    if (cna_content_type_reader_manager_register(view(ForeignReaderName), &callbacks, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    callbacks.create = foreign_create;
    callbacks.read = 0;
    if (cna_content_type_reader_manager_register(view(ForeignReaderName), &callbacks, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    callbacks.read = foreign_read;
    if (cna_content_type_reader_manager_register(view(""), &callbacks, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    callbacks.target_type_name = view("");
    if (cna_content_type_reader_manager_register(view(ForeignReaderName), &callbacks, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Not one of those refusals may leave a registration behind. */
    return rejected == CNA_INVALID_HANDLE && state->create_calls == 0;
}

static int validate_foreign_reader(const ReaderFixture* const fixture)
{
    ForeignReaderState state;
    CNA_ContentTypeReaderCallbacks callbacks;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle duplicate = UINT64_C(9);
    CNA_ContentTypeReaderHandle type_reader = CNA_INVALID_HANDLE;
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    CNA_ContentReaderHandle reader = CNA_INVALID_HANDLE;
    CNA_ContentReaderCreateInfo create_info;
    CNA_Bool flag = CNA_TRUE;
    CNA_Bool has_value = CNA_FALSE;
    char buffer[128];
    uint64_t bytes = 0U;
    int32_t version = -1;

    memset(&state, 0, sizeof(state));
    if (!validate_foreign_registration_refusals(&state)) {
        return 0;
    }

    callbacks = make_foreign_callbacks(&state);
    if (cna_content_type_reader_manager_get_is_registered(view(ForeignReaderName), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_content_type_reader_manager_register(
            view(ForeignReaderName), &callbacks, &registration) != CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE ||
        cna_content_type_reader_manager_get_is_registered(view(ForeignReaderName), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* A second registration of a live name is refused rather than silently ignored: a caller
       whose factory is never called would otherwise find out only from wrongly-typed assets. */
    if (cna_content_type_reader_manager_register(
            view(ForeignReaderName), &callbacks, &duplicate) != CNA_RESULT_INVALID_STATE ||
        duplicate != CNA_INVALID_HANDLE) {
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }

    /* Creating by name runs the factory and produces a reader that answers its own identity. */
    memset(buffer, 0, sizeof(buffer));
    if (cna_content_type_reader_manager_create_reader(view(ForeignReaderName), &type_reader) !=
            CNA_RESULT_SUCCESS ||
        type_reader == CNA_INVALID_HANDLE || state.create_calls != 1 ||
        cna_content_type_reader_get_target_type_name_size(type_reader, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen(ForeignTargetName) ||
        cna_content_type_reader_copy_target_type_name(
            type_reader, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(buffer, ForeignTargetName) != 0 ||
        cna_content_type_reader_get_type_version(type_reader, &version) != CNA_RESULT_SUCCESS ||
        version != INT32_C(3) ||
        cna_content_type_reader_supports_version(type_reader, INT32_C(3), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_content_type_reader_supports_version(type_reader, INT32_C(2), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_content_type_reader_get_can_deserialize_into_existing_object(type_reader, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        (void)cna_content_type_reader_destroy(type_reader);
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }

    /* Reading drives the callback with a real, borrowed ContentReader. */
    if (!open_asset_stream(fixture, &stream)) {
        (void)cna_content_type_reader_destroy(type_reader);
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }
    create_info = make_create_info(stream);
    if (cna_content_reader_create(&create_info, &reader) != CNA_RESULT_SUCCESS ||
        cna_content_type_reader_read_untyped(type_reader, reader, &has_value) !=
            CNA_RESULT_SUCCESS ||
        has_value != CNA_TRUE || state.read_calls != 1 || state.borrowed_reader_usable != 1) {
        (void)cna_content_reader_destroy(reader);
        (void)cna_storage_stream_close(stream);
        (void)cna_content_type_reader_destroy(type_reader);
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }
    /* A refusing read fails the operation rather than producing an object, which is the whole
       reason this callback returns a result. */
    state.refuse_read = 1;
    if (cna_content_type_reader_read_untyped(type_reader, reader, &has_value) ==
        CNA_RESULT_SUCCESS) {
        (void)cna_content_reader_destroy(reader);
        (void)cna_storage_stream_close(stream);
        (void)cna_content_type_reader_destroy(type_reader);
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }
    state.refuse_read = 0;

    if (cna_content_reader_destroy(reader) != CNA_RESULT_SUCCESS ||
        cna_storage_stream_close(stream) != CNA_RESULT_SUCCESS) {
        (void)cna_content_type_reader_destroy(type_reader);
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }
    /* Destroying the reader instance releases the caller's per-instance context exactly once. */
    if (cna_content_type_reader_destroy(type_reader) != CNA_RESULT_SUCCESS ||
        state.destroy_calls != 1) {
        (void)cna_content_type_reader_manager_unregister(registration);
        return 0;
    }

    /* Unregistering frees the name and the handle, and is not repeatable. */
    if (cna_content_type_reader_manager_unregister(registration) != CNA_RESULT_SUCCESS ||
        cna_content_type_reader_manager_unregister(registration) != CNA_RESULT_INVALID_HANDLE ||
        cna_content_type_reader_manager_get_is_registered(view(ForeignReaderName), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    /* And the freed name really is free, which a shadowed registration would not be. */
    if (cna_content_type_reader_manager_register(
            view(ForeignReaderName), &callbacks, &registration) != CNA_RESULT_SUCCESS ||
        cna_content_type_reader_manager_unregister(registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_identities(void)
{
    return CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE ==
        UINT32_C(0);
}

int main(void)
{
    ReaderFixture fixture;

    if (!validate_identities()) {
        return 1;
    }
    if (!create_fixture(&fixture)) {
        return 2;
    }
    if (!validate_reader(&fixture)) {
        (void)destroy_fixture(&fixture);
        return 3;
    }
    if (!validate_type_readers(&fixture)) {
        (void)destroy_fixture(&fixture);
        return 4;
    }
    if (!validate_foreign_reader(&fixture)) {
        (void)destroy_fixture(&fixture);
        return 5;
    }
    if (!destroy_fixture(&fixture)) {
        return 6;
    }
    return 0;
}
