// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdint.h>
#include <string.h>

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static CNA_OptionalInt32 present(const int32_t value)
{
    const CNA_OptionalInt32 result = {CNA_TRUE, {0U, 0U, 0U}, value};
    return result;
}

static CNA_OptionalInt32 absent(void)
{
    const CNA_OptionalInt32 result = {CNA_FALSE, {0U, 0U, 0U}, 0};
    return result;
}

static int same_optional(const CNA_OptionalInt32 left, const CNA_OptionalInt32 right)
{
    return left.has_value == right.has_value && left.value == right.value;
}

static int validate_identities(void)
{
    return CNA_NETWORK_SESSION_END_REASON_CLIENT_SIGNED_OUT == UINT32_C(0) &&
        CNA_NETWORK_SESSION_END_REASON_HOST_ENDED_SESSION == UINT32_C(1) &&
        CNA_NETWORK_SESSION_END_REASON_REMOVED_BY_HOST == UINT32_C(2) &&
        CNA_NETWORK_SESSION_END_REASON_DISCONNECTED == UINT32_C(3) &&
        CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_FOUND == UINT32_C(0) &&
        CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_JOINABLE == UINT32_C(1) &&
        CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_FULL == UINT32_C(2) &&
        CNA_NETWORK_SESSION_STATE_LOBBY == UINT32_C(0) &&
        CNA_NETWORK_SESSION_STATE_PLAYING == UINT32_C(1) &&
        CNA_NETWORK_SESSION_STATE_ENDED == UINT32_C(2) &&
        CNA_NETWORK_SESSION_TYPE_LOCAL == UINT32_C(0) &&
        CNA_NETWORK_SESSION_TYPE_SYSTEM_LINK == UINT32_C(1) &&
        CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH == UINT32_C(2) &&
        CNA_NETWORK_SESSION_TYPE_RANKED == UINT32_C(3) &&
        CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS == UINT32_C(4) &&
        CNA_SEND_DATA_OPTIONS_NONE == UINT32_C(0) &&
        CNA_SEND_DATA_OPTIONS_RELIABLE == UINT32_C(1) &&
        CNA_SEND_DATA_OPTIONS_IN_ORDER == UINT32_C(2) &&
        CNA_SEND_DATA_OPTIONS_RELIABLE_IN_ORDER == UINT32_C(3) &&
        CNA_SEND_DATA_OPTIONS_CHAT == UINT32_C(4);
}

static int validate_quality_of_service(void)
{
    CNA_QualityOfService quality = {
        sizeof(CNA_QualityOfService), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(-1), INT64_C(-1), INT32_C(-1), INT32_C(-1)
    };
    CNA_QualityOfService rejected = {
        UINT32_C(4), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(0), INT64_C(0), INT32_C(0), INT32_C(0)
    };

    /* The unmeasured factory still reports availability, matching the canonical stub exactly. */
    if (cna_quality_of_service_init(&quality) != CNA_RESULT_SUCCESS ||
        quality.is_available != CNA_TRUE || quality.average_roundtrip_ticks != INT64_C(0) ||
        quality.minimum_roundtrip_ticks != INT64_C(0) ||
        quality.bytes_per_second_downstream != INT32_C(0) ||
        quality.bytes_per_second_upstream != INT32_C(0)) {
        return 0;
    }
    /* One exchange yields one sample, so the average and minimum are the same measurement. */
    if (cna_quality_of_service_init_measured(INT64_C(123456), &quality) != CNA_RESULT_SUCCESS ||
        quality.is_available != CNA_TRUE ||
        quality.average_roundtrip_ticks != INT64_C(123456) ||
        quality.minimum_roundtrip_ticks != INT64_C(123456) ||
        quality.bytes_per_second_downstream != INT32_C(0) ||
        quality.bytes_per_second_upstream != INT32_C(0)) {
        return 0;
    }
    return cna_quality_of_service_init(0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_quality_of_service_init(&rejected) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_quality_of_service_init_measured(INT64_C(1), &rejected) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_properties_mutation(const CNA_NetworkSessionPropertiesHandle properties)
{
    CNA_OptionalInt32 value = absent();
    CNA_Bool flag = CNA_FALSE;
    int32_t count = -1;
    int32_t index = 0;

    if (cna_network_session_properties_get_count(properties, &count) != CNA_RESULT_SUCCESS ||
        count != 0 ||
        cna_network_session_properties_get_is_read_only(properties, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    if (cna_network_session_properties_add(properties, present(7)) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_add(properties, absent()) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_add(properties, present(9)) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_count(properties, &count) != CNA_RESULT_SUCCESS ||
        count != 3) {
        return 0;
    }
    if (cna_network_session_properties_get_item(properties, 0, &value) != CNA_RESULT_SUCCESS ||
        !same_optional(value, present(7)) ||
        cna_network_session_properties_get_item(properties, 1, &value) != CNA_RESULT_SUCCESS ||
        !same_optional(value, absent()) ||
        cna_network_session_properties_get_item(properties, 3, &value) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_network_session_properties_index_of(properties, present(9), &index) !=
            CNA_RESULT_SUCCESS ||
        index != 2 ||
        cna_network_session_properties_index_of(properties, present(42), &index) !=
            CNA_RESULT_SUCCESS ||
        index != -1) {
        return 0;
    }
    if (cna_network_session_properties_contains(properties, absent(), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_network_session_properties_contains(properties, present(42), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_network_session_properties_set_item(properties, 1, present(5)) !=
            CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_item(properties, 1, &value) != CNA_RESULT_SUCCESS ||
        !same_optional(value, present(5))) {
        return 0;
    }
    /* The canonical setter appends instead of extending when the index is past the end. */
    if (cna_network_session_properties_set_item(properties, 99, present(1)) !=
            CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_count(properties, &count) != CNA_RESULT_SUCCESS ||
        count != 4 ||
        cna_network_session_properties_get_item(properties, 3, &value) != CNA_RESULT_SUCCESS ||
        !same_optional(value, present(1))) {
        return 0;
    }
    if (cna_network_session_properties_insert(properties, 0, present(3)) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_item(properties, 0, &value) != CNA_RESULT_SUCCESS ||
        !same_optional(value, present(3)) ||
        cna_network_session_properties_insert(properties, 99, present(3)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_network_session_properties_remove_at(properties, 0) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_remove_at(properties, 99) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_session_properties_get_count(properties, &count) != CNA_RESULT_SUCCESS ||
        count != 4) {
        return 0;
    }
    if (cna_network_session_properties_remove(properties, present(5), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_network_session_properties_remove(properties, present(999), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_network_session_properties_get_count(properties, &count) != CNA_RESULT_SUCCESS ||
        count != 3) {
        return 0;
    }
    return 1;
}

static int validate_properties_copy(const CNA_NetworkSessionPropertiesHandle properties)
{
    CNA_OptionalInt32 destination[8];
    uint64_t copied = UINT64_C(0);

    memset(destination, 0, sizeof(destination));
    if (cna_network_session_properties_copy_to(properties, destination, UINT64_C(0), 0, &copied) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        copied != UINT64_C(3)) {
        return 0;
    }
    if (cna_network_session_properties_copy_to(
            properties,
            destination,
            (uint64_t)(sizeof(destination) / sizeof(destination[0])),
            2,
            &copied) != CNA_RESULT_SUCCESS ||
        copied != UINT64_C(3) || !same_optional(destination[2], present(7)) ||
        !same_optional(destination[3], present(9)) || !same_optional(destination[4], present(1))) {
        return 0;
    }
    return cna_network_session_properties_copy_to(
        properties,
        destination,
        (uint64_t)(sizeof(destination) / sizeof(destination[0])),
        -1,
        &copied) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_properties_enumeration(const CNA_NetworkSessionPropertiesHandle properties)
{
    CNA_NetworkSessionPropertyEnumeratorHandle enumerator = CNA_INVALID_HANDLE;
    CNA_OptionalInt32 value = absent();
    CNA_Bool has_current = CNA_TRUE;

    if (cna_network_session_properties_create_enumerator(properties, &enumerator) !=
            CNA_RESULT_SUCCESS ||
        enumerator == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The canonical enumerator starts before the first element, so reading now is a state error
       rather than an out-of-bounds dereference. */
    if (cna_network_session_property_enumerator_get_current(enumerator, &value) !=
        CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_network_session_property_enumerator_move_next(enumerator, &has_current) !=
            CNA_RESULT_SUCCESS ||
        has_current != CNA_TRUE ||
        cna_network_session_property_enumerator_get_current(enumerator, &value) !=
            CNA_RESULT_SUCCESS ||
        !same_optional(value, present(7))) {
        return 0;
    }
    if (cna_network_session_property_enumerator_move_next(enumerator, &has_current) !=
            CNA_RESULT_SUCCESS ||
        has_current != CNA_TRUE ||
        cna_network_session_property_enumerator_get_current(enumerator, &value) !=
            CNA_RESULT_SUCCESS ||
        !same_optional(value, present(9))) {
        return 0;
    }
    if (cna_network_session_property_enumerator_move_next(enumerator, &has_current) !=
            CNA_RESULT_SUCCESS ||
        has_current != CNA_TRUE ||
        cna_network_session_property_enumerator_move_next(enumerator, &has_current) !=
            CNA_RESULT_SUCCESS ||
        has_current != CNA_FALSE ||
        cna_network_session_property_enumerator_get_current(enumerator, &value) !=
            CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_network_session_property_enumerator_reset(enumerator) != CNA_RESULT_SUCCESS ||
        cna_network_session_property_enumerator_get_current(enumerator, &value) !=
            CNA_RESULT_INVALID_STATE ||
        cna_network_session_property_enumerator_move_next(enumerator, &has_current) !=
            CNA_RESULT_SUCCESS ||
        has_current != CNA_TRUE) {
        return 0;
    }
    /* An enumerator observes the live list, so the list outlives it. */
    if (cna_network_session_properties_destroy(properties) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    return cna_network_session_property_enumerator_destroy(enumerator) == CNA_RESULT_SUCCESS &&
        cna_network_session_property_enumerator_destroy(enumerator) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_properties(void)
{
    CNA_NetworkSessionPropertiesHandle properties = CNA_INVALID_HANDLE;
    int32_t count = -1;

    if (cna_network_session_properties_create(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_session_properties_create(&properties) != CNA_RESULT_SUCCESS ||
        properties == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (!validate_properties_mutation(properties) || !validate_properties_copy(properties) ||
        !validate_properties_enumeration(properties)) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    if (cna_network_session_properties_clear(properties) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_count(properties, &count) != CNA_RESULT_SUCCESS ||
        count != 0) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    return cna_network_session_properties_destroy(properties) == CNA_RESULT_SUCCESS &&
        cna_network_session_properties_destroy(properties) == CNA_RESULT_INVALID_HANDLE &&
        cna_network_session_properties_get_count(properties, &count) ==
            CNA_RESULT_INVALID_HANDLE;
}

static int write_packet(const CNA_PacketWriterHandle writer)
{
    CNA_Matrix matrix;
    const CNA_Vector2 vector2 = {1.5F, 2.5F};
    const CNA_Vector3 vector3 = {3.5F, 4.5F, 5.5F};
    const CNA_Vector4 vector4 = {6.5F, 7.5F, 8.5F, 9.5F};
    const CNA_Quaternion quaternion = {0.25F, 0.5F, 0.75F, 1.0F};

    for (int index = 0; index < 16; ++index) {
        ((float*)&matrix)[index] = (float)(index + 1);
    }
    return cna_packet_writer_write_vector3(writer, vector3) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_write_single(writer, 11.25F) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_write_double(writer, 12.5) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_write_vector2(writer, vector2) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_write_vector4(writer, vector4) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_write_quaternion(writer, quaternion) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_write_matrix(writer, matrix) == CNA_RESULT_SUCCESS;
}

static int read_packet(const CNA_PacketReaderHandle reader)
{
    CNA_Vector2 vector2 = {0.0F, 0.0F};
    CNA_Vector3 vector3 = {0.0F, 0.0F, 0.0F};
    CNA_Vector4 vector4 = {0.0F, 0.0F, 0.0F, 0.0F};
    CNA_Quaternion quaternion = {0.0F, 0.0F, 0.0F, 0.0F};
    CNA_Matrix matrix;
    float single = 0.0F;
    double value = 0.0;

    memset(&matrix, 0, sizeof(matrix));
    if (cna_packet_reader_read_vector3(reader, &vector3) != CNA_RESULT_SUCCESS ||
        vector3.x != 3.5F || vector3.y != 4.5F || vector3.z != 5.5F) {
        return 0;
    }
    if (cna_packet_reader_read_single(reader, &single) != CNA_RESULT_SUCCESS ||
        single != 11.25F ||
        cna_packet_reader_read_double(reader, &value) != CNA_RESULT_SUCCESS || value != 12.5) {
        return 0;
    }
    if (cna_packet_reader_read_vector2(reader, &vector2) != CNA_RESULT_SUCCESS ||
        vector2.x != 1.5F || vector2.y != 2.5F) {
        return 0;
    }
    if (cna_packet_reader_read_vector4(reader, &vector4) != CNA_RESULT_SUCCESS ||
        vector4.x != 6.5F || vector4.y != 7.5F || vector4.z != 8.5F || vector4.w != 9.5F) {
        return 0;
    }
    if (cna_packet_reader_read_quaternion(reader, &quaternion) != CNA_RESULT_SUCCESS ||
        quaternion.x != 0.25F || quaternion.y != 0.5F || quaternion.z != 0.75F ||
        quaternion.w != 1.0F) {
        return 0;
    }
    return cna_packet_reader_read_matrix(reader, &matrix) == CNA_RESULT_SUCCESS &&
        matrix.m11 == 1.0F && matrix.m14 == 4.0F && matrix.m22 == 6.0F && matrix.m33 == 11.0F &&
        matrix.m41 == 13.0F && matrix.m44 == 16.0F;
}

static int validate_color_asymmetry(void)
{
    CNA_PacketWriterHandle writer = CNA_INVALID_HANDLE;
    CNA_PacketReaderHandle reader = CNA_INVALID_HANDLE;
    const CNA_Color written = {UINT8_C(10), UINT8_C(20), UINT8_C(30), UINT8_C(40)};
    CNA_Color read = {UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    uint8_t buffer[64];
    uint64_t bytes = UINT64_C(0);
    int ok = 0;

    if (cna_packet_writer_create(0, &writer) != CNA_RESULT_SUCCESS ||
        cna_packet_reader_create(0, &reader) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The canonical writer emits four bytes while the canonical reader consumes four floats, so a
       direct round trip cannot work and the C API preserves that rather than symmetrizing it. */
    ok = cna_packet_writer_write_color(writer, written) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_copy_data_ext(writer, buffer, (uint64_t)sizeof(buffer), &bytes) ==
            CNA_RESULT_SUCCESS &&
        bytes == UINT64_C(4) &&
        buffer[0] == UINT8_C(10) && buffer[1] == UINT8_C(20) && buffer[2] == UINT8_C(30) &&
        buffer[3] == UINT8_C(40) &&
        cna_packet_reader_set_data_ext(reader, buffer, bytes) == CNA_RESULT_SUCCESS &&
        cna_packet_reader_read_color(reader, &read) == CNA_RESULT_IO;

    /* Four floats do round-trip into a color through the canonical float constructor. */
    if (ok) {
        ok = cna_packet_writer_set_position(writer, 0) == CNA_RESULT_SUCCESS &&
            cna_packet_writer_write_single(writer, 1.0F) == CNA_RESULT_SUCCESS &&
            cna_packet_writer_write_single(writer, 0.0F) == CNA_RESULT_SUCCESS &&
            cna_packet_writer_write_single(writer, 1.0F) == CNA_RESULT_SUCCESS &&
            cna_packet_writer_write_single(writer, 0.0F) == CNA_RESULT_SUCCESS &&
            cna_packet_writer_copy_data_ext(writer, buffer, (uint64_t)sizeof(buffer), &bytes) ==
                CNA_RESULT_SUCCESS &&
            bytes == UINT64_C(16) &&
            cna_packet_reader_set_data_ext(reader, buffer, bytes) == CNA_RESULT_SUCCESS &&
            cna_packet_reader_read_color(reader, &read) == CNA_RESULT_SUCCESS &&
            read.r == UINT8_C(255) && read.g == UINT8_C(0) && read.b == UINT8_C(255) &&
            read.a == UINT8_C(0);
    }
    return cna_packet_writer_destroy(writer) == CNA_RESULT_SUCCESS &&
        cna_packet_reader_destroy(reader) == CNA_RESULT_SUCCESS && ok;
}

static int validate_packets(void)
{
    CNA_PacketWriterHandle writer = CNA_INVALID_HANDLE;
    CNA_PacketReaderHandle reader = CNA_INVALID_HANDLE;
    uint8_t buffer[256];
    uint64_t bytes = UINT64_C(0);
    int32_t length = -1;
    int32_t position = -1;
    float single = 0.0F;

    if (cna_packet_writer_create(-1, &writer) != CNA_RESULT_INVALID_ARGUMENT ||
        writer != CNA_INVALID_HANDLE ||
        cna_packet_reader_create(-1, &reader) != CNA_RESULT_INVALID_ARGUMENT ||
        reader != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_packet_writer_create(0, &writer) != CNA_RESULT_SUCCESS ||
        cna_packet_reader_create(64, &reader) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_packet_writer_get_length(writer, &length) != CNA_RESULT_SUCCESS || length != 0 ||
        cna_packet_writer_get_position(writer, &position) != CNA_RESULT_SUCCESS ||
        position != 0) {
        return 0;
    }
    if (!write_packet(writer)) {
        return 0;
    }
    /* 12 + 4 + 8 + 8 + 16 + 16 + 64 bytes. */
    if (cna_packet_writer_get_length(writer, &length) != CNA_RESULT_SUCCESS || length != 128 ||
        cna_packet_writer_copy_data_ext(writer, buffer, UINT64_C(0), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != UINT64_C(128)) {
        return 0;
    }
    if (cna_packet_writer_copy_data_ext(writer, buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(128)) {
        return 0;
    }
    if (cna_packet_reader_set_data_ext(reader, buffer, bytes) != CNA_RESULT_SUCCESS ||
        cna_packet_reader_get_length(reader, &length) != CNA_RESULT_SUCCESS || length != 128 ||
        cna_packet_reader_get_position(reader, &position) != CNA_RESULT_SUCCESS ||
        position != 0) {
        return 0;
    }
    if (!read_packet(reader)) {
        return 0;
    }
    if (cna_packet_reader_get_position(reader, &position) != CNA_RESULT_SUCCESS ||
        position != 128 ||
        cna_packet_reader_read_single(reader, &single) != CNA_RESULT_IO) {
        return 0;
    }
    if (cna_packet_reader_set_position(reader, 0) != CNA_RESULT_SUCCESS ||
        cna_packet_reader_read_single(reader, &single) != CNA_RESULT_SUCCESS ||
        cna_packet_reader_set_data_ext(reader, 0, UINT64_C(4)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_packet_reader_set_data_ext(reader, 0, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
        cna_packet_reader_get_length(reader, &length) != CNA_RESULT_SUCCESS || length != 0) {
        return 0;
    }
    return cna_packet_writer_destroy(writer) == CNA_RESULT_SUCCESS &&
        cna_packet_writer_destroy(writer) == CNA_RESULT_INVALID_HANDLE &&
        cna_packet_reader_destroy(reader) == CNA_RESULT_SUCCESS &&
        cna_packet_reader_destroy(reader) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_gamer_defaults(const CNA_NetworkGamerHandle gamer)
{
    CNA_Bool flag = CNA_TRUE;
    uint8_t id = UINT8_C(9);
    int64_t ticks = INT64_C(-1);
    CNA_Handle session = UINT64_C(9);

    if (cna_network_gamer_get_has_left_session(gamer, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_network_gamer_get_has_voice(gamer, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_network_gamer_get_is_guest(gamer, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_network_gamer_get_is_host(gamer, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_network_gamer_get_is_muted_by_local_user(gamer, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_network_gamer_get_is_private_slot(gamer, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_network_gamer_get_is_ready(gamer, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_network_gamer_get_is_talking(gamer, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    /* A plain network gamer is not a local gamer; only the local subtype reports otherwise. */
    if (cna_network_gamer_get_is_local(gamer, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE) {
        return 0;
    }
    if (cna_network_gamer_get_id(gamer, &id) != CNA_RESULT_SUCCESS || id != UINT8_C(0) ||
        cna_network_gamer_get_roundtrip_ticks(gamer, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(0)) {
        return 0;
    }
    return cna_network_gamer_get_session(gamer, &session) == CNA_RESULT_SUCCESS &&
        session == CNA_INVALID_HANDLE &&
        cna_network_gamer_get_id(gamer, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_gamer_mutation(const CNA_NetworkGamerHandle gamer)
{
    CNA_Bool flag = CNA_FALSE;
    uint8_t id = UINT8_C(0);
    int64_t ticks = INT64_C(0);

    if (cna_network_gamer_set_has_left_session_ext(gamer, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_get_has_left_session(gamer, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    if (cna_network_gamer_set_id_ext(gamer, UINT8_C(42)) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_get_id(gamer, &id) != CNA_RESULT_SUCCESS || id != UINT8_C(42)) {
        return 0;
    }
    if (cna_network_gamer_set_is_host_ext(gamer, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_get_is_host(gamer, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    if (cna_network_gamer_set_is_ready(gamer, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_get_is_ready(gamer, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    return cna_network_gamer_set_roundtrip_ticks_ext(gamer, INT64_C(5000)) ==
            CNA_RESULT_SUCCESS &&
        cna_network_gamer_get_roundtrip_ticks(gamer, &ticks) == CNA_RESULT_SUCCESS &&
        ticks == INT64_C(5000);
}

static int validate_machine(const CNA_NetworkGamerHandle gamer)
{
    CNA_NetworkMachineHandle machine = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle copied = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle view = UINT64_C(9);
    int32_t count = -1;

    if (cna_network_machine_create(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_machine_create(&machine) != CNA_RESULT_SUCCESS ||
        machine == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* Only a session populates a machine's roster, so a standalone machine reports none. */
    if (cna_network_machine_get_gamer_count(machine, &count) != CNA_RESULT_SUCCESS ||
        count != 0 ||
        cna_network_machine_get_gamer(machine, 0, &view) != CNA_RESULT_INVALID_ARGUMENT ||
        view != CNA_INVALID_HANDLE ||
        cna_network_machine_get_gamer(machine, -1, &view) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical removal is a declared placeholder that always throws. */
    if (cna_network_machine_remove_from_session(machine) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    if (cna_network_gamer_set_machine(gamer, machine) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_set_machine(gamer, CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    /* The canonical setter copies by value, so the C route hands back an independent copy. */
    if (cna_network_gamer_copy_machine(gamer, &copied) != CNA_RESULT_SUCCESS ||
        copied == CNA_INVALID_HANDLE || copied == machine ||
        cna_network_machine_get_gamer_count(copied, &count) != CNA_RESULT_SUCCESS || count != 0) {
        return 0;
    }
    return cna_network_machine_destroy(copied) == CNA_RESULT_SUCCESS &&
        cna_network_machine_destroy(machine) == CNA_RESULT_SUCCESS &&
        cna_network_machine_destroy(machine) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_event_descriptions(const CNA_NetworkGamerHandle gamer)
{
    CNA_GameEndedEventInfo ended = {sizeof(CNA_GameEndedEventInfo), UINT32_C(1)};
    CNA_GameStartedEventInfo started = {sizeof(CNA_GameStartedEventInfo), UINT32_C(1)};
    CNA_GamerJoinedEventInfo joined = {
        sizeof(CNA_GamerJoinedEventInfo), UINT32_C(1), CNA_INVALID_HANDLE};
    CNA_GamerLeftEventInfo left = {
        sizeof(CNA_GamerLeftEventInfo), UINT32_C(1), CNA_INVALID_HANDLE};
    CNA_HostChangedEventInfo host = {
        sizeof(CNA_HostChangedEventInfo), UINT32_C(1), CNA_INVALID_HANDLE, CNA_INVALID_HANDLE};
    CNA_NetworkSessionEndedEventInfo session_ended = {
        sizeof(CNA_NetworkSessionEndedEventInfo), UINT32_C(1), UINT32_C(0), {0U, 0U, 0U, 0U}};
    CNA_WriteLeaderboardsEventInfo leaderboards = {
        sizeof(CNA_WriteLeaderboardsEventInfo), UINT32_C(1), CNA_INVALID_HANDLE, CNA_FALSE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}};

    if (cna_game_ended_event_info_init(&ended) != CNA_RESULT_SUCCESS ||
        cna_game_started_event_info_init(&started) != CNA_RESULT_SUCCESS ||
        cna_game_ended_event_info_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_gamer_joined_event_info_init(gamer, &joined) != CNA_RESULT_SUCCESS ||
        joined.gamer != gamer ||
        cna_gamer_left_event_info_init(gamer, &left) != CNA_RESULT_SUCCESS ||
        left.gamer != gamer) {
        return 0;
    }
    if (cna_host_changed_event_info_init(CNA_INVALID_HANDLE, gamer, &host) !=
            CNA_RESULT_SUCCESS ||
        host.old_host != CNA_INVALID_HANDLE || host.new_host != gamer) {
        return 0;
    }
    /* A payload can never name a handle that was never a gamer. */
    if (cna_gamer_joined_event_info_init(UINT64_C(1234), &joined) != CNA_RESULT_INVALID_HANDLE ||
        cna_host_changed_event_info_init(UINT64_C(1234), gamer, &host) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    if (cna_network_session_ended_event_info_init(
            CNA_NETWORK_SESSION_END_REASON_REMOVED_BY_HOST,
            &session_ended) != CNA_RESULT_SUCCESS ||
        session_ended.end_reason != CNA_NETWORK_SESSION_END_REASON_REMOVED_BY_HOST ||
        cna_network_session_ended_event_info_init(UINT32_C(9), &session_ended) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_write_leaderboards_event_info_init(gamer, CNA_TRUE, &leaderboards) !=
            CNA_RESULT_SUCCESS ||
        leaderboards.gamer != gamer || leaderboards.is_leaving != CNA_TRUE) {
        return 0;
    }
    ended.struct_version = UINT32_C(2);
    return cna_game_ended_event_info_init(&ended) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_gamers(void)
{
    CNA_NetworkGamerHandle gamer = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle rejected = UINT64_C(9);

    if (cna_network_gamer_create(CNA_INVALID_HANDLE, view("Tester"), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_gamer_create(UINT64_C(1234), view("Tester"), &rejected) !=
            CNA_RESULT_INVALID_HANDLE ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_network_gamer_create(CNA_INVALID_HANDLE, view("Tester"), &gamer) !=
            CNA_RESULT_SUCCESS ||
        gamer == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (!validate_gamer_defaults(gamer) || !validate_gamer_mutation(gamer) ||
        !validate_machine(gamer) || !validate_event_descriptions(gamer)) {
        (void)cna_network_gamer_destroy(gamer);
        return 0;
    }
    return cna_network_gamer_destroy(gamer) == CNA_RESULT_SUCCESS &&
        cna_network_gamer_destroy(gamer) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_AvailableNetworkSessionCreateInfo make_session_info(
    const char* const host,
    const int32_t gamers,
    const CNA_NetworkSessionPropertiesHandle properties)
{
    CNA_AvailableNetworkSessionCreateInfo info = {
        sizeof(CNA_AvailableNetworkSessionCreateInfo), UINT32_C(1), gamers, INT32_C(2),
        INT32_C(6), CNA_NETWORK_SESSION_TYPE_SYSTEM_LINK, UINT16_C(27015), {0U, 0U, 0U, 0U, 0U, 0U},
        {0, UINT64_C(0)}, {0, UINT64_C(0)}, properties
    };
    info.host_gamertag = view(host);
    info.host_address = view("127.0.0.1");
    return info;
}

static int validate_available_session_values(const CNA_AvailableNetworkSessionHandle session)
{
    CNA_QualityOfService quality = {
        sizeof(CNA_QualityOfService), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(0), INT64_C(0), INT32_C(0), INT32_C(0)
    };
    CNA_NetworkSessionPropertiesHandle properties = CNA_INVALID_HANDLE;
    CNA_OptionalInt32 value = absent();
    CNA_NetworkSessionType type = CNA_NETWORK_SESSION_TYPE_LOCAL;
    char buffer[64];
    uint64_t bytes = UINT64_C(0);
    int32_t number = -1;
    uint16_t port = UINT16_C(0);

    if (cna_available_network_session_get_current_gamer_count(session, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 3 ||
        cna_available_network_session_get_open_private_gamer_slots(session, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 2 ||
        cna_available_network_session_get_open_public_gamer_slots(session, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 6) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_available_network_session_get_host_gamertag_size(session, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(4) ||
        cna_available_network_session_copy_host_gamertag(
            session, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(buffer, "Host") != 0 ||
        cna_available_network_session_copy_host_gamertag(session, buffer, UINT64_C(1), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_available_network_session_get_connect_address_size_ext(session, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(9) ||
        cna_available_network_session_copy_connect_address_ext(
            session, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(buffer, "127.0.0.1") != 0 ||
        cna_available_network_session_get_connect_port_ext(session, &port) !=
            CNA_RESULT_SUCCESS ||
        port != UINT16_C(27015) ||
        cna_available_network_session_get_session_type_ext(session, &type) !=
            CNA_RESULT_SUCCESS ||
        type != CNA_NETWORK_SESSION_TYPE_SYSTEM_LINK) {
        return 0;
    }
    /* Only the round-trip sample can be carried in, because that is all the canonical type
       accepts; availability is still reported true, as its unmeasured factory does. */
    if (cna_available_network_session_get_quality_of_service(session, &quality) !=
            CNA_RESULT_SUCCESS ||
        quality.is_available != CNA_TRUE ||
        quality.average_roundtrip_ticks != INT64_C(4242) ||
        quality.minimum_roundtrip_ticks != INT64_C(4242) ||
        quality.bytes_per_second_downstream != INT32_C(0)) {
        return 0;
    }
    /* The properties come back as an independent copy, not an alias into the description. */
    if (cna_available_network_session_copy_session_properties(session, &properties) !=
            CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_count(properties, &number) != CNA_RESULT_SUCCESS ||
        number != 1 ||
        cna_network_session_properties_get_item(properties, 0, &value) != CNA_RESULT_SUCCESS ||
        !same_optional(value, present(77))) {
        return 0;
    }
    return cna_network_session_properties_add(properties, present(1)) == CNA_RESULT_SUCCESS &&
        cna_network_session_properties_destroy(properties) == CNA_RESULT_SUCCESS;
}

static int validate_available_session_collection(
    const CNA_AvailableNetworkSessionHandle first,
    const CNA_AvailableNetworkSessionHandle second)
{
    CNA_AvailableNetworkSessionHandle sessions[2];
    CNA_AvailableNetworkSessionCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_AvailableNetworkSessionHandle copied = CNA_INVALID_HANDLE;
    CNA_AvailableNetworkSessionHandle rejected = UINT64_C(9);
    CNA_Bool flag = CNA_TRUE;
    int32_t count = -1;
    char buffer[64];
    uint64_t bytes = UINT64_C(0);

    sessions[0] = first;
    sessions[1] = second;
    if (cna_available_network_session_collection_create_ext(sessions, UINT64_C(2), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_available_network_session_collection_create_ext(0, UINT64_C(2), &collection) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_available_network_session_collection_create_ext(
            sessions, UINT64_C(2), &collection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_available_network_session_collection_get_count(collection, &count) !=
            CNA_RESULT_SUCCESS ||
        count != 2 ||
        cna_available_network_session_collection_get_is_disposed(collection, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    /* An element is copied out, so it survives the collection it came from. */
    if (cna_available_network_session_collection_copy_session(collection, 1, &copied) !=
            CNA_RESULT_SUCCESS ||
        copied == CNA_INVALID_HANDLE || copied == second ||
        cna_available_network_session_collection_copy_session(collection, 2, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_available_network_session_collection_copy_session(collection, -1, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_available_network_session_collection_dispose(collection) != CNA_RESULT_SUCCESS ||
        cna_available_network_session_collection_get_is_disposed(collection, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_available_network_session_collection_dispose(collection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_available_network_session_copy_host_gamertag(
            copied, buffer, (uint64_t)sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(buffer, "Other") != 0) {
        return 0;
    }
    return cna_available_network_session_destroy(copied) == CNA_RESULT_SUCCESS &&
        cna_available_network_session_collection_destroy(collection) == CNA_RESULT_SUCCESS &&
        cna_available_network_session_collection_destroy(collection) ==
            CNA_RESULT_INVALID_HANDLE;
}

static int validate_available_sessions(void)
{
    CNA_NetworkSessionPropertiesHandle properties = CNA_INVALID_HANDLE;
    CNA_AvailableNetworkSessionHandle first = CNA_INVALID_HANDLE;
    CNA_AvailableNetworkSessionHandle second = CNA_INVALID_HANDLE;
    CNA_AvailableNetworkSessionHandle rejected = UINT64_C(9);
    const CNA_QualityOfService quality = {
        sizeof(CNA_QualityOfService), UINT32_C(1), CNA_TRUE, {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(4242), INT64_C(4242), INT32_C(0), INT32_C(0)
    };
    CNA_Bool flag = CNA_TRUE;
    int ok = 0;

    if (cna_network_session_properties_create(&properties) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_add(properties, present(77)) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_AvailableNetworkSessionCreateInfo info = make_session_info("Host", 3, properties);
    info.reserved[0] = 1U;
    if (cna_available_network_session_create_ext(&info, &quality, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    info.reserved[0] = 0U;
    info.session_type = UINT32_C(9);
    if (cna_available_network_session_create_ext(&info, &quality, &rejected) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    info.session_type = CNA_NETWORK_SESSION_TYPE_SYSTEM_LINK;
    if (cna_available_network_session_create_ext(&info, &quality, &first) != CNA_RESULT_SUCCESS) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }

    CNA_AvailableNetworkSessionCreateInfo other = make_session_info("Other", 5, properties);
    if (cna_available_network_session_create_ext(&other, 0, &second) != CNA_RESULT_SUCCESS) {
        (void)cna_available_network_session_destroy(first);
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }

    ok = validate_available_session_values(first) &&
        cna_available_network_session_equals(first, first, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        cna_available_network_session_equals(first, second, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE &&
        cna_available_network_session_not_equals(first, second, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        validate_available_session_collection(first, second);

    return cna_available_network_session_destroy(second) == CNA_RESULT_SUCCESS &&
        cna_available_network_session_destroy(first) == CNA_RESULT_SUCCESS &&
        cna_available_network_session_destroy(first) == CNA_RESULT_INVALID_HANDLE &&
        cna_network_session_properties_destroy(properties) == CNA_RESULT_SUCCESS && ok;
}

static int validate_session_state(const CNA_NetworkSessionHandle session)
{
    CNA_NetworkSessionState state = CNA_NETWORK_SESSION_STATE_ENDED;
    CNA_NetworkSessionType type = CNA_NETWORK_SESSION_TYPE_RANKED;
    CNA_NetworkSessionPropertiesHandle properties = CNA_INVALID_HANDLE;
    CNA_Bool flag = CNA_TRUE;
    char buffer[80];
    uint64_t bytes = UINT64_C(0);
    int32_t number = -1;
    int64_t ticks = INT64_C(-1);
    float loss = -1.0F;

    if (cna_network_session_get_is_disposed(session, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_network_session_get_session_type(session, &type) != CNA_RESULT_SUCCESS ||
        type != CNA_NETWORK_SESSION_TYPE_LOCAL ||
        cna_network_session_get_session_state(session, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_NETWORK_SESSION_STATE_LOBBY) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_network_session_get_type_name_size(session, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen("Microsoft.Xna.Framework.Net.NetworkSession") ||
        cna_network_session_copy_type_name(session, buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(buffer, "Microsoft.Xna.Framework.Net.NetworkSession") != 0 ||
        cna_network_session_copy_type_name(session, buffer, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    if (cna_network_session_set_allow_host_migration(session, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_allow_host_migration(session, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_network_session_set_allow_join_in_progress(session, CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        cna_network_session_get_allow_join_in_progress(session, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    if (cna_network_session_set_max_gamers(session, 8) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_max_gamers(session, &number) != CNA_RESULT_SUCCESS ||
        number != 8 ||
        cna_network_session_set_private_gamer_slots(session, 2) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_private_gamer_slots(session, &number) != CNA_RESULT_SUCCESS ||
        number != 2) {
        return 0;
    }
    if (cna_network_session_set_simulated_latency_ticks(session, INT64_C(1000)) !=
            CNA_RESULT_SUCCESS ||
        cna_network_session_get_simulated_latency_ticks(session, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(1000) ||
        cna_network_session_set_simulated_packet_loss(session, 0.25F) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_simulated_packet_loss(session, &loss) != CNA_RESULT_SUCCESS ||
        loss != 0.25F) {
        return 0;
    }
    if (cna_network_session_get_bytes_per_second_received(session, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 0 ||
        cna_network_session_get_bytes_per_second_sent(session, &number) != CNA_RESULT_SUCCESS ||
        number != 0 ||
        cna_network_session_get_is_host(session, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* Session properties come back as an independent copy, like every other copied collection. */
    if (cna_network_session_copy_session_properties(session, &properties) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_get_count(properties, &number) != CNA_RESULT_SUCCESS ||
        number != 0) {
        return 0;
    }
    return cna_network_session_properties_destroy(properties) == CNA_RESULT_SUCCESS &&
        cna_network_session_reset_ready(session) == CNA_RESULT_SUCCESS &&
        cna_network_session_get_is_everyone_ready(session, &flag) == CNA_RESULT_SUCCESS;
}

static int validate_session_rosters(const CNA_NetworkSessionHandle session)
{
    CNA_NetworkGamerHandle view = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle rejected = UINT64_C(9);
    CNA_NetworkGamerHandle found = CNA_INVALID_HANDLE;
    CNA_Bool flag = CNA_FALSE;
    int32_t number = -1;
    uint8_t id = UINT8_C(0);
    uint64_t owned = UINT64_C(9);

    if (cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_ALL, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 1 ||
        cna_network_session_get_gamer_count(session, UINT32_C(9), &number) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_ALL, 9, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* CBIND-065: the session's host, which nothing named while the matrix recorded it implemented.
       A session with a roster has one, and it is a gamer in that roster -- the property XNA code
       reads to decide whether it is the authority. */
    {
        CNA_NetworkGamerHandle host = CNA_INVALID_HANDLE;
        CNA_Bool host_is_host = CNA_FALSE;
        if (cna_network_session_get_host(session, &host) != CNA_RESULT_SUCCESS ||
            host == CNA_INVALID_HANDLE ||
            cna_network_gamer_get_is_host(host, &host_is_host) != CNA_RESULT_SUCCESS ||
            host_is_host != CNA_TRUE ||
            cna_network_session_get_host(session, 0) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_network_gamer_destroy(host) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    /* Signed-in gamers have no C representation yet, so only the no-gamer form is accepted. */
    if (cna_network_session_add_local_gamer(session, UINT64_C(1234)) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_network_session_add_local_gamer(session, CNA_INVALID_HANDLE) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 2 ||
        cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_ALL, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 2 ||
        cna_network_session_get_owned_gamer_count_ext(session, &owned) != CNA_RESULT_SUCCESS ||
        owned != UINT64_C(2)) {
        return 0;
    }
    if (cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &view) !=
            CNA_RESULT_SUCCESS ||
        view == CNA_INVALID_HANDLE ||
        cna_network_gamer_get_is_local(view, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_network_gamer_get_id(view, &id) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* A borrowed view keeps its session alive, so the session cannot be released first. */
    if (cna_network_session_destroy(session) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    CNA_NetworkGamerHandle missing = UINT64_C(9);
    if (cna_network_session_find_gamer_by_id(session, id, &found) != CNA_RESULT_SUCCESS ||
        found == CNA_INVALID_HANDLE ||
        cna_network_session_find_gamer_by_id(session, UINT8_C(200), &missing) !=
            CNA_RESULT_SUCCESS ||
        missing != CNA_INVALID_HANDLE) {
        return 0;
    }
    return cna_network_gamer_destroy(found) == CNA_RESULT_SUCCESS &&
        cna_network_gamer_destroy(view) == CNA_RESULT_SUCCESS;
}

static int validate_session_events(const CNA_NetworkSessionHandle session)
{
    static const uint8_t payload[4] = {1U, 2U, 3U, 4U};
    CNA_NetworkSessionState state = CNA_NETWORK_SESSION_STATE_ENDED;
    CNA_NetworkEventInfo event = {
        sizeof(CNA_NetworkEventInfo), UINT32_C(1), CNA_NETWORK_EVENT_TYPE_PACKET_SEND,
        CNA_SEND_DATA_OPTIONS_RELIABLE, CNA_NETWORK_SESSION_STATE_LOBBY,
        CNA_NETWORK_SESSION_END_REASON_DISCONNECTED, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
        payload, UINT64_C(4)
    };

    /* The canonical start and end only queue a state change; the pump applies it. */
    if (cna_network_session_start_game(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_session_state(session, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_NETWORK_SESSION_STATE_LOBBY ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_session_state(session, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_NETWORK_SESSION_STATE_PLAYING) {
        return 0;
    }
    if (cna_network_session_end_game(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_session_state(session, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_NETWORK_SESSION_STATE_LOBBY) {
        return 0;
    }
    if (cna_network_session_send_network_event_ext(session, &event) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    event.type = UINT32_C(9);
    if (cna_network_session_send_network_event_ext(session, &event) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    event.type = CNA_NETWORK_EVENT_TYPE_PACKET_SEND;
    event.struct_version = UINT32_C(2);
    return cna_network_session_send_network_event_ext(session, &event) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_network_session_send_network_event_ext(session, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_session_remote_gamers(const CNA_NetworkSessionHandle session)
{
    CNA_NetworkGamerHandle remote = CNA_INVALID_HANDLE;
    CNA_Handle owner = CNA_INVALID_HANDLE;
    int32_t number = -1;

    if (cna_network_gamer_create(session, view("Remote"), &remote) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_get_session(remote, &owner) != CNA_RESULT_SUCCESS ||
        owner != session) {
        return 0;
    }
    if (cna_network_session_add_remote_gamer_ext(session, remote) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_REMOTE, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 1) {
        return 0;
    }
    /* The canonical call does not take ownership, so the C layer retains the gamer: releasing the
       caller's own handle must not invalidate the session's roster. */
    if (cna_network_gamer_destroy(remote) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    CNA_NetworkGamerHandle view_handle = CNA_INVALID_HANDLE;
    if (cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_REMOTE, 0,
            &view_handle) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    const int removed =
        cna_network_session_remove_gamer_ext(
            session, view_handle, CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) ==
            CNA_RESULT_SUCCESS &&
        cna_network_session_remove_gamer_ext(session, view_handle, UINT32_C(9)) ==
            CNA_RESULT_INVALID_ARGUMENT;
    if (!removed || cna_network_gamer_destroy(view_handle) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_network_session_get_gamer_count(
            session, CNA_NETWORK_SESSION_ROSTER_REMOTE, &number) == CNA_RESULT_SUCCESS &&
        number == 0;
}

typedef struct SessionEventCounters {
    int started;
    int ended;
    int joined;
    int left;
    int host_changed;
    int session_ended;
    int leaderboards;
    int invites;
    int joined_gamer_ok;
} SessionEventCounters;

static void on_game_started(
    const CNA_NetworkSessionHandle session,
    const CNA_GameStartedEventInfo* const info,
    void* const context)
{
    (void)session;
    (void)info;
    ((SessionEventCounters*)context)->started += 1;
}

static void on_game_ended(
    const CNA_NetworkSessionHandle session,
    const CNA_GameEndedEventInfo* const info,
    void* const context)
{
    (void)session;
    (void)info;
    ((SessionEventCounters*)context)->ended += 1;
}

static void on_gamer_joined(
    const CNA_NetworkSessionHandle session,
    const CNA_GamerJoinedEventInfo* const info,
    void* const context)
{
    SessionEventCounters* const counters = (SessionEventCounters*)context;
    CNA_Bool local = CNA_FALSE;
    counters->joined += 1;
    (void)session;
    /* The payload handle is live for the duration of the callback and no longer. */
    if (info->gamer != CNA_INVALID_HANDLE &&
        cna_network_gamer_get_is_local(info->gamer, &local) == CNA_RESULT_SUCCESS) {
        counters->joined_gamer_ok += 1;
    }
}

static void on_gamer_left(
    const CNA_NetworkSessionHandle session,
    const CNA_GamerLeftEventInfo* const info,
    void* const context)
{
    (void)session;
    (void)info;
    ((SessionEventCounters*)context)->left += 1;
}

static void on_host_changed(
    const CNA_NetworkSessionHandle session,
    const CNA_HostChangedEventInfo* const info,
    void* const context)
{
    (void)session;
    (void)info;
    ((SessionEventCounters*)context)->host_changed += 1;
}

static void on_session_ended(
    const CNA_NetworkSessionHandle session,
    const CNA_NetworkSessionEndedEventInfo* const info,
    void* const context)
{
    (void)session;
    (void)info;
    ((SessionEventCounters*)context)->session_ended += 1;
}

static void on_write_leaderboards(
    const CNA_NetworkSessionHandle session,
    const CNA_WriteLeaderboardsEventInfo* const info,
    void* const context)
{
    (void)session;
    (void)info;
    ((SessionEventCounters*)context)->leaderboards += 1;
}

static int validate_invite_accepted_info(void)
{
    CNA_InviteAcceptedEventInfo info;
    int index = 0;

    /* Caller-initialised, like every versioned structure in this ABI. */
    memset(&info, 9, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    if (cna_invite_accepted_event_info_init(CNA_INVALID_HANDLE, CNA_TRUE, &info) !=
            CNA_RESULT_SUCCESS ||
        info.struct_size != (uint32_t)sizeof(info) || info.struct_version != UINT32_C(1) ||
        info.gamer != CNA_INVALID_HANDLE || info.is_current_session != CNA_TRUE) {
        return 0;
    }
    for (index = 0; index < (int)(sizeof(info.reserved) / sizeof(info.reserved[0])); ++index) {
        if (info.reserved[index] != 0U) {
            return 0;
        }
    }
    /* The other flag value, an undefined one, and a null output. */
    if (cna_invite_accepted_event_info_init(CNA_INVALID_HANDLE, CNA_FALSE, &info) !=
            CNA_RESULT_SUCCESS ||
        info.is_current_session != CNA_FALSE ||
        cna_invite_accepted_event_info_init(CNA_INVALID_HANDLE, CNA_TRUE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* CBIND-065: a flag outside {CNA_FALSE, CNA_TRUE} is refused rather than stored. This route
       writes into a structure an event *subscriber* then reads, so a byte of 9 accepted here
       would leave a reader with a Boolean that is neither true nor false. */
    if (cna_invite_accepted_event_info_init(CNA_INVALID_HANDLE, UINT8_C(9), &info) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        info.is_current_session != CNA_FALSE) {
        return 0;
    }
    return 1;
}

static void on_invite_accepted(const CNA_InviteAcceptedEventInfo* const info, void* const context)
{
    (void)info;
    ((SessionEventCounters*)context)->invites += 1;
}

static int subscribe_all(
    const CNA_NetworkSessionHandle session,
    SessionEventCounters* const counters,
    CNA_NetworkSessionEventRegistrationHandle registrations[10])
{
    for (int index = 0; index < 10; ++index) {
        registrations[index] = CNA_INVALID_HANDLE;
    }
    return cna_network_session_subscribe_game_started(
            session, on_game_started, counters, &registrations[0]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_game_ended(
            session, on_game_ended, counters, &registrations[1]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_gamer_joined(
            session, on_gamer_joined, counters, &registrations[2]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_gamer_left(
            session, on_gamer_left, counters, &registrations[3]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_host_changed(
            session, on_host_changed, counters, &registrations[4]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_session_ended(
            session, on_session_ended, counters, &registrations[5]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_write_arbitrated_leaderboard(
            session, on_write_leaderboards, counters, &registrations[6]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_write_unarbitrated_leaderboard(
            session, on_write_leaderboards, counters, &registrations[7]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_write_true_skill(
            session, on_write_leaderboards, counters, &registrations[8]) == CNA_RESULT_SUCCESS &&
        cna_network_session_subscribe_invite_accepted(
            on_invite_accepted, counters, &registrations[9]) == CNA_RESULT_SUCCESS;
}

static int validate_session_subscriptions(const CNA_NetworkSessionHandle session)
{
    SessionEventCounters counters = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    CNA_NetworkSessionEventRegistrationHandle registrations[10];
    CNA_NetworkSessionEventRegistrationHandle rejected = UINT64_C(9);
    CNA_NetworkGamerHandle remote = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local_view = CNA_INVALID_HANDLE;
    CNA_NetworkEventInfo event = {
        sizeof(CNA_NetworkEventInfo), UINT32_C(1), CNA_NETWORK_EVENT_TYPE_HOST_CHANGE,
        CNA_SEND_DATA_OPTIONS_NONE, CNA_NETWORK_SESSION_STATE_LOBBY,
        CNA_NETWORK_SESSION_END_REASON_DISCONNECTED, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
        0, UINT64_C(0)
    };
    int32_t number = -1;

    if (cna_network_session_subscribe_game_started(session, 0, 0, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        /* CBIND-065: the constructor for the argument that event carries. Callers build one to
           raise the event themselves, and nothing named it while the matrix recorded it
           implemented -- so the versioned structure it fills in went unproved. */
        !validate_invite_accepted_info() ||
        cna_network_session_subscribe_invite_accepted(on_invite_accepted, 0, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Drain anything earlier phases queued so every count below is exact. */
    if (cna_network_session_update(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_ALL, &number) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (!subscribe_all(session, &counters, registrations)) {
        return 0;
    }
    /* The canonical gamer-joined event replays itself for every gamer already present. */
    if (counters.joined != number || counters.joined_gamer_ok != number) {
        return 0;
    }

    if (cna_network_gamer_create(session, view("Late"), &remote) != CNA_RESULT_SUCCESS ||
        cna_network_session_add_remote_gamer_ext(session, remote) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_destroy(remote) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS ||
        counters.joined != number + 1) {
        return 0;
    }
    if (cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_REMOTE, 0, &remote) !=
            CNA_RESULT_SUCCESS ||
        cna_network_session_remove_gamer_ext(
            session, remote, CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) != CNA_RESULT_SUCCESS ||
        cna_network_gamer_destroy(remote) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS || counters.left != 1) {
        return 0;
    }

    if (cna_network_session_start_game(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS || counters.started != 1 ||
        cna_network_session_end_game(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS || counters.ended != 1) {
        return 0;
    }

    if (cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local_view) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    event.gamer = local_view;
    if (cna_network_session_send_network_event_ext(session, &event) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS || counters.host_changed != 1 ||
        cna_network_gamer_destroy(local_view) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    event.type = CNA_NETWORK_EVENT_TYPE_STATE_CHANGE;
    event.state = CNA_NETWORK_SESSION_STATE_ENDED;
    event.gamer = CNA_INVALID_HANDLE;
    if (cna_network_session_send_network_event_ext(session, &event) != CNA_RESULT_SUCCESS ||
        cna_network_session_update(session) != CNA_RESULT_SUCCESS ||
        counters.session_ended != 1) {
        return 0;
    }
    /* Nothing in the canonical implementation raises the leaderboard or invite events yet, so
       their subscriptions are exercised through registration and release only. */
    if (counters.leaderboards != 0 || counters.invites != 0) {
        return 0;
    }

    for (int index = 0; index < 10; ++index) {
        if (cna_network_session_unsubscribe(registrations[index]) != CNA_RESULT_SUCCESS ||
            cna_network_session_unsubscribe(registrations[index]) != CNA_RESULT_INVALID_HANDLE) {
            return 0;
        }
    }
    return 1;
}

static int validate_secondary_sessions(const CNA_SignedInGamerHandle signed_in)
{
    CNA_NetworkSessionPropertiesHandle properties = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkSessionPropertiesHandle copied = CNA_INVALID_HANDLE;
    int32_t number = -1;
    int ok = 0;

    if (cna_network_session_properties_create(&properties) != CNA_RESULT_SUCCESS ||
        cna_network_session_properties_add(properties, present(11)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The properties overload takes its local gamers from the published collection. */
    if (cna_network_session_create_with_properties(
            CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 8, 2, properties, &session) !=
        CNA_RESULT_SUCCESS) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    ok = cna_network_session_get_private_gamer_slots(session, &number) == CNA_RESULT_SUCCESS &&
        number == 2 &&
        cna_network_session_copy_session_properties(session, &copied) == CNA_RESULT_SUCCESS &&
        cna_network_session_properties_get_count(copied, &number) == CNA_RESULT_SUCCESS &&
        number == 1 &&
        cna_network_session_properties_destroy(copied) == CNA_RESULT_SUCCESS;
    if (!ok || cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }

    /* The explicit local-gamer overload does not consult the published collection at all. */
    if (cna_network_session_create_with_local_gamers(
            CNA_NETWORK_SESSION_TYPE_LOCAL, &signed_in, UINT64_C(1), 8, 0, properties,
            &session) != CNA_RESULT_SUCCESS) {
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    ok = cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, &number) ==
            CNA_RESULT_SUCCESS &&
        number == 1;
    if (!ok) {
        (void)cna_network_session_destroy(session);
        (void)cna_network_session_properties_destroy(properties);
        return 0;
    }
    return cna_network_session_destroy(session) == CNA_RESULT_SUCCESS &&
        cna_network_session_properties_destroy(properties) == CNA_RESULT_SUCCESS;
}

static int validate_local_gamer_packets(const CNA_NetworkGamerHandle local)
{
    static const uint8_t payload[4] = {0x11U, 0x22U, 0x33U, 0x44U};
    CNA_NetworkEventInfo event = {
        sizeof(CNA_NetworkEventInfo), UINT32_C(1), CNA_NETWORK_EVENT_TYPE_PACKET_SEND,
        CNA_SEND_DATA_OPTIONS_RELIABLE, CNA_NETWORK_SESSION_STATE_LOBBY,
        CNA_NETWORK_SESSION_END_REASON_DISCONNECTED, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
        payload, UINT64_C(4)
    };
    CNA_PacketReaderHandle reader = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle sender = UINT64_C(9);
    uint8_t buffer[8];
    uint64_t received = UINT64_C(9);
    CNA_Bool flag = CNA_TRUE;

    event.gamer = local;
    if (cna_local_network_gamer_get_is_data_available(local, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_local_network_gamer_enqueue_packet_ext(local, &event) != CNA_RESULT_SUCCESS ||
        cna_local_network_gamer_get_is_data_available(local, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_local_network_gamer_receive_data(local, buffer, UINT64_C(4), &sender, &received) !=
            CNA_RESULT_SUCCESS ||
        received != UINT64_C(4) || memcmp(buffer, payload, 4U) != 0) {
        return 0;
    }
    if (sender != CNA_INVALID_HANDLE &&
        cna_network_gamer_destroy(sender) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* An empty queue reports zero bytes rather than failing. */
    if (cna_local_network_gamer_receive_data(local, buffer, UINT64_C(4), &sender, &received) !=
            CNA_RESULT_SUCCESS ||
        received != UINT64_C(0)) {
        return 0;
    }

    /* The canonical offset overload consumes the packet before it validates the offset. */
    if (cna_local_network_gamer_enqueue_packet_ext(local, &event) != CNA_RESULT_SUCCESS ||
        cna_local_network_gamer_receive_data_at(
            local, buffer, UINT64_C(4), 3, &sender, &received) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_local_network_gamer_get_is_data_available(local, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_local_network_gamer_enqueue_packet_ext(local, &event) != CNA_RESULT_SUCCESS ||
        cna_local_network_gamer_receive_data_at(
            local, buffer, (uint64_t)sizeof(buffer), 2, &sender, &received) !=
            CNA_RESULT_SUCCESS ||
        received != UINT64_C(4) || memcmp(buffer + 2, payload, 4U) != 0) {
        return 0;
    }
    if (sender != CNA_INVALID_HANDLE &&
        cna_network_gamer_destroy(sender) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The canonical packet-reader overload always reports zero, even with a packet available. */
    if (cna_packet_reader_create(0, &reader) != CNA_RESULT_SUCCESS ||
        cna_local_network_gamer_enqueue_packet_ext(local, &event) != CNA_RESULT_SUCCESS ||
        cna_local_network_gamer_receive_data_into_packet_reader(
            local, reader, &sender, &received) != CNA_RESULT_SUCCESS ||
        received != UINT64_C(0)) {
        (void)cna_packet_reader_destroy(reader);
        return 0;
    }
    if (sender != CNA_INVALID_HANDLE &&
        cna_network_gamer_destroy(sender) != CNA_RESULT_SUCCESS) {
        (void)cna_packet_reader_destroy(reader);
        return 0;
    }
    return cna_packet_reader_destroy(reader) == CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_clear_packet_queue_ext(local) == CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_get_is_data_available(local, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
}

static int validate_local_gamer_sends(const CNA_NetworkGamerHandle local)
{
    static const uint8_t payload[4] = {1U, 2U, 3U, 4U};
    CNA_PacketWriterHandle writer = CNA_INVALID_HANDLE;
    int ok = 0;

    if (cna_packet_writer_create(0, &writer) != CNA_RESULT_SUCCESS ||
        cna_packet_writer_write_single(writer, 1.5F) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    ok = cna_local_network_gamer_send_data(
            local, payload, UINT64_C(4), CNA_SEND_DATA_OPTIONS_RELIABLE) == CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_data_range(
            local, payload, UINT64_C(4), 1, 2, CNA_SEND_DATA_OPTIONS_IN_ORDER) ==
            CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_data_to(
            local, payload, UINT64_C(4), CNA_SEND_DATA_OPTIONS_NONE, local) ==
            CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_data_range_to(
            local, payload, UINT64_C(4), 0, 4, CNA_SEND_DATA_OPTIONS_CHAT, local) ==
            CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_packet_writer(
            local, writer, CNA_SEND_DATA_OPTIONS_RELIABLE_IN_ORDER) == CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_packet_writer_to(
            local, writer, CNA_SEND_DATA_OPTIONS_RELIABLE, local) == CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_data(local, payload, UINT64_C(4), UINT32_C(9)) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_local_network_gamer_send_data(local, 0, UINT64_C(4), CNA_SEND_DATA_OPTIONS_NONE) ==
            CNA_RESULT_INVALID_ARGUMENT;
    return cna_packet_writer_destroy(writer) == CNA_RESULT_SUCCESS && ok;
}

static int validate_local_gamers(
    const CNA_NetworkSessionHandle session,
    const CNA_SignedInGamerHandle signed_in)
{
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle remote = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle standalone = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle backing = CNA_INVALID_HANDLE;
    char buffer[32];
    uint64_t bytes = UINT64_C(0);
    CNA_Bool flag = CNA_FALSE;
    int ok = 0;

    if (cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* A plain network gamer is not a local gamer, and the routes say so instead of guessing. */
    if (cna_network_gamer_create(CNA_INVALID_HANDLE, view("Plain"), &remote) !=
            CNA_RESULT_SUCCESS ||
        cna_local_network_gamer_get_is_data_available(remote, &flag) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_network_gamer_destroy(remote) != CNA_RESULT_SUCCESS) {
        (void)cna_network_gamer_destroy(local);
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    ok = cna_local_network_gamer_get_signed_in_gamer(local, &backing) == CNA_RESULT_SUCCESS &&
        backing != CNA_INVALID_HANDLE &&
        /* CBIND-065: the size half of the two-call pair, which was never asked for even though
           the copy half was -- so nothing proved the two agree. */
        cna_signed_in_gamer_get_gamertag_size(backing, &bytes) == CNA_RESULT_SUCCESS &&
        bytes == (uint64_t)strlen("Player") &&
        cna_signed_in_gamer_get_gamertag_size(backing, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_signed_in_gamer_copy_gamertag(backing, buffer, (uint64_t)sizeof(buffer), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(buffer, "Player") == 0 &&
        cna_signed_in_gamer_destroy(backing) == CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_enable_send_voice(local, CNA_INVALID_HANDLE, CNA_TRUE) ==
            CNA_RESULT_SUCCESS &&
        cna_local_network_gamer_send_party_invites(local) == CNA_RESULT_SUCCESS &&
        validate_local_gamer_packets(local) && validate_local_gamer_sends(local);
    if (!ok || cna_network_gamer_destroy(local) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A local gamer created outside a session is not in any roster and has no queued packets. */
    if (cna_local_network_gamer_create_ext(signed_in, CNA_INVALID_HANDLE, &standalone) !=
            CNA_RESULT_SUCCESS ||
        cna_network_gamer_get_is_local(standalone, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_local_network_gamer_get_is_data_available(standalone, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        (void)cna_network_gamer_destroy(standalone);
        return 0;
    }
    return cna_network_gamer_destroy(standalone) == CNA_RESULT_SUCCESS;
}

static void on_async_completed(void* const context)
{
    *(int*)context += 1;
}

static int destroy_session_and_check(const CNA_NetworkSessionHandle session, const int ok)
{
    return cna_network_session_destroy(session) == CNA_RESULT_SUCCESS && ok;
}

static int validate_session_discovery(const CNA_SignedInGamerHandle signed_in)
{
    CNA_AvailableNetworkSessionCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_AvailableNetworkSessionHandle available = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkSessionType type = CNA_NETWORK_SESSION_TYPE_LOCAL;
    CNA_Bool host = CNA_TRUE;
    int completions = 0;
    int32_t number = -1;

    /* The canonical search refuses a local-only session type outright, and only a SystemLink
       search reaches real discovery, so a PlayerMatch search is empty by design. */
    if (cna_network_session_find(
            CNA_NETWORK_SESSION_TYPE_LOCAL, 1, CNA_INVALID_HANDLE, &collection) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_session_find(
            CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH, 1, CNA_INVALID_HANDLE, &collection) !=
            CNA_RESULT_SUCCESS ||
        cna_available_network_session_collection_get_count(collection, &number) !=
            CNA_RESULT_SUCCESS ||
        number != 0 ||
        cna_available_network_session_collection_destroy(collection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_find_with_local_gamers(
            CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH, &signed_in, UINT64_C(1), CNA_INVALID_HANDLE,
            &collection) != CNA_RESULT_SUCCESS ||
        cna_available_network_session_collection_destroy(collection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_find_async(
            CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH, 1, CNA_INVALID_HANDLE, on_async_completed,
            &completions, &collection) != CNA_RESULT_SUCCESS ||
        completions != 1 ||
        cna_available_network_session_collection_destroy(collection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_find_with_local_gamers_async(
            CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH, &signed_in, UINT64_C(1), CNA_INVALID_HANDLE,
            on_async_completed, &completions, &collection) != CNA_RESULT_SUCCESS ||
        completions != 2 ||
        cna_available_network_session_collection_destroy(collection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_find(CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH, 1, CNA_INVALID_HANDLE, 0) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The canonical end step substitutes its own gamer limit instead of forwarding the caller's. */
    if (cna_network_session_create_async(
            CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 8, on_async_completed, &completions, &session) !=
            CNA_RESULT_SUCCESS ||
        completions != 3 ||
        cna_network_session_get_max_gamers(session, &number) != CNA_RESULT_SUCCESS ||
        number == 8) {
        return destroy_session_and_check(session, 0);
    }
    if (cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_create_with_properties_async(
            CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 8, 2, CNA_INVALID_HANDLE, on_async_completed,
            &completions, &session) != CNA_RESULT_SUCCESS ||
        completions != 4 ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_create_with_local_gamers_async(
            CNA_NETWORK_SESSION_TYPE_LOCAL, &signed_in, UINT64_C(1), 8, 0, CNA_INVALID_HANDLE,
            on_async_completed, &completions, &session) != CNA_RESULT_SUCCESS ||
        completions != 5 ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_AvailableNetworkSessionCreateInfo info = make_session_info("Host", 1, CNA_INVALID_HANDLE);
    info.session_type = CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH;
    if (cna_available_network_session_create_ext(&info, 0, &available) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_join(available, &session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_session_type(session, &type) != CNA_RESULT_SUCCESS ||
        type != CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH ||
        cna_network_session_get_is_host(session, &host) != CNA_RESULT_SUCCESS ||
        host != CNA_FALSE ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        (void)cna_available_network_session_destroy(available);
        return 0;
    }
    if (cna_network_session_join_async(available, on_async_completed, &completions, &session) !=
            CNA_RESULT_SUCCESS ||
        completions != 6 ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS ||
        cna_available_network_session_destroy(available) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The canonical invite path builds its session from fixed values, not from live invite state. */
    if (cna_network_session_join_invited(1, &session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_session_type(session, &type) != CNA_RESULT_SUCCESS ||
        type != CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_join_invited_with_local_gamers(
            &signed_in, UINT64_C(1), &session) != CNA_RESULT_SUCCESS ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_network_session_join_invited_async(
            1, on_async_completed, &completions, &session) != CNA_RESULT_SUCCESS ||
        completions != 7 ||
        cna_network_session_destroy(session) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_network_session_join_invited_with_local_gamers_async(
            &signed_in, UINT64_C(1), on_async_completed, &completions, &session) ==
            CNA_RESULT_SUCCESS &&
        completions == 8 &&
        cna_network_session_destroy(session) == CNA_RESULT_SUCCESS &&
        cna_network_session_join_invited(1, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_sessions(void)
{
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle rejected = UINT64_C(9);
    CNA_SignedInGamerHandle signed_in = CNA_INVALID_HANDLE;
    CNA_Bool flag = CNA_FALSE;
    int32_t instances = -1;
    int32_t actions = -1;
    int32_t number = -1;

    if (cna_network_session_get_instance_count_ext(&instances) != CNA_RESULT_SUCCESS ||
        instances != 0 ||
        cna_network_session_get_active_action_count_ext(&actions) != CNA_RESULT_SUCCESS ||
        actions != 0) {
        return 0;
    }
    /* The canonical constructor selects a host from its local gamers, so a session cannot exist
       while no gamer is signed in. */
    if (cna_gamer_get_signed_in_gamer_count(&number) != CNA_RESULT_SUCCESS || number != 0 ||
        cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_network_session_create(UINT32_C(9), 1, 4, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_signed_in_gamer_create_ext(
            view("Player"), CNA_FALSE, CNA_FALSE, CNA_PLAYER_INDEX_ONE, &signed_in) !=
            CNA_RESULT_SUCCESS ||
        cna_gamer_set_signed_in_gamers_ext(&signed_in, UINT64_C(1)) != CNA_RESULT_SUCCESS ||
        cna_gamer_get_signed_in_gamer_count(&number) != CNA_RESULT_SUCCESS || number != 1) {
        return 0;
    }
    /* A published gamer is retained by the process-wide collection until it is replaced. */
    if (cna_signed_in_gamer_destroy(signed_in) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }

    if (cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 2, 8, &session) !=
            CNA_RESULT_SUCCESS ||
        session == CNA_INVALID_HANDLE ||
        cna_network_session_get_instance_count_ext(&instances) != CNA_RESULT_SUCCESS ||
        instances != 1) {
        return 0;
    }

    const int ok = validate_session_state(session) && validate_session_rosters(session) &&
        validate_session_events(session) && validate_session_remote_gamers(session) &&
        validate_session_subscriptions(session) && validate_local_gamers(session, signed_in);
    if (!ok) {
        (void)cna_network_session_destroy(session);
        return 0;
    }

    if (cna_network_session_dispose(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_get_is_disposed(session, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_network_session_update(session) != CNA_RESULT_INVALID_STATE) {
        (void)cna_network_session_destroy(session);
        return 0;
    }
    if (cna_network_session_destroy(session) != CNA_RESULT_SUCCESS ||
        cna_network_session_destroy(session) != CNA_RESULT_INVALID_HANDLE ||
        cna_network_session_get_instance_count_ext(&instances) != CNA_RESULT_SUCCESS ||
        instances != 0) {
        return 0;
    }

    if (!validate_secondary_sessions(signed_in) || !validate_session_discovery(signed_in)) {
        return 0;
    }
    return cna_gamer_set_signed_in_gamers_ext(0, UINT64_C(0)) == CNA_RESULT_SUCCESS &&
        cna_gamer_get_signed_in_gamer_count(&number) == CNA_RESULT_SUCCESS && number == 0 &&
        cna_signed_in_gamer_destroy(signed_in) == CNA_RESULT_SUCCESS;
}

static int validate_join_error(void)
{
    CNA_NetworkSessionJoinError join_error = CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_FULL;
    CNA_Bool has_join_error = CNA_TRUE;

    /* The most recent failure on this thread was an ordinary handle failure, not a join failure. */
    if (cna_packet_writer_destroy(CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    if (cna_net_get_last_join_error(&join_error, &has_join_error) != CNA_RESULT_SUCCESS ||
        has_join_error != CNA_FALSE ||
        join_error != CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_FOUND) {
        return 0;
    }
    return cna_net_get_last_join_error(0, &has_join_error) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_net_get_last_join_error(&join_error, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

int main(void)
{
    if (!validate_identities()) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_quality_of_service()) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_properties()) {
        return CNA_TEST_FAIL(3);
    }
    if (!validate_packets()) {
        return CNA_TEST_FAIL(4);
    }
    if (!validate_color_asymmetry()) {
        return CNA_TEST_FAIL(5);
    }
    if (!validate_join_error()) {
        return CNA_TEST_FAIL(6);
    }
    if (!validate_gamers()) {
        return CNA_TEST_FAIL(7);
    }
    if (!validate_available_sessions()) {
        return CNA_TEST_FAIL(8);
    }
    if (!validate_sessions()) {
        return CNA_TEST_FAIL(9);
    }
    return 0;
}
