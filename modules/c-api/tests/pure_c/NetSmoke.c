// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <string.h>

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
        return 1;
    }
    if (!validate_quality_of_service()) {
        return 2;
    }
    if (!validate_properties()) {
        return 3;
    }
    if (!validate_packets()) {
        return 4;
    }
    if (!validate_color_asymmetry()) {
        return 5;
    }
    if (!validate_join_error()) {
        return 6;
    }
    return 0;
}
