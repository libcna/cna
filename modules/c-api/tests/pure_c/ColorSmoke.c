// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

static int nearly_equal(const float left, const float right)
{
    return fabsf(left - right) <= 0.00001F;
}

static int color_equals_channels(
    const CNA_Color value,
    const uint8_t r,
    const uint8_t g,
    const uint8_t b,
    const uint8_t a)
{
    return value.r == r && value.g == g && value.b == b && value.a == a;
}

static int validate_construction(void)
{
    CNA_Color value = {1U, 2U, 3U, 4U};
    if (cna_color_init_vector4((CNA_Vector4){0.0F, 0.5F, 1.0F, 2.0F}, &value) !=
            CNA_RESULT_SUCCESS || !color_equals_channels(value, 0U, 127U, 255U, 255U) ||
        cna_color_init_vector3((CNA_Vector3){1.0F, -1.0F, 0.25F}, &value) !=
            CNA_RESULT_SUCCESS || !color_equals_channels(value, 255U, 0U, 63U, 255U) ||
        cna_color_init_float_rgb(0.1F, 0.2F, 0.3F, &value) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 25U, 51U, 76U, 255U) ||
        cna_color_init_float_rgba(0.1F, 0.2F, 0.3F, 0.4F, &value) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 25U, 51U, 76U, 102U) ||
        cna_color_init_int_rgb(-1, 128, 300, &value) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 0U, 128U, 255U, 255U) ||
        cna_color_init_int_rgba(-1, 128, 300, 64, &value) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 0U, 128U, 255U, 64U) ||
        cna_color_init_bytes_rgb(1U, 2U, 3U, &value) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 1U, 2U, 3U, 255U) ||
        cna_color_init_bytes_rgba(1U, 2U, 3U, 4U, &value) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 1U, 2U, 3U, 4U)) {
        return 0;
    }

    const CNA_Color sentinel = value;
    if (cna_color_init_vector4((CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F}, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !color_equals_channels(sentinel, 1U, 2U, 3U, 4U)) {
        return 0;
    }
    return 1;
}

static int validate_properties_and_strings(void)
{
    CNA_Color value = {1U, 2U, 3U, 4U};
    uint32_t packed = 0U;
    if (cna_color_get_packed_value(value, &packed) != CNA_RESULT_SUCCESS ||
        packed != UINT32_C(0x04030201) ||
        cna_color_set_packed_value(&value, UINT32_C(0xa1b2c3d4)) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(value, 0xd4U, 0xc3U, 0xb2U, 0xa1U) ||
        cna_color_get_packed_value(value, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_color_set_packed_value(0, 0U) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    value.r = 1U;
    value.g = 2U;
    value.b = 3U;
    value.a = 4U;
    const char expected_debug[] = "1 2 3 4";
    const char expected_string[] = "{R:1 G:2 B:3 A:4}";
    uint64_t bytes = UINT64_MAX;
    char buffer[32] = {0};
    if (cna_color_get_debug_string_byte_count(value, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof(expected_debug) - 1U ||
        cna_color_copy_debug_string(value, buffer, sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof(expected_debug) - 1U ||
        memcmp(buffer, expected_debug, sizeof(expected_debug) - 1U) != 0 ||
        cna_color_get_string_byte_count(value, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof(expected_string) - 1U ||
        cna_color_copy_string(value, buffer, sizeof(buffer), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof(expected_string) - 1U ||
        memcmp(buffer, expected_string, sizeof(expected_string) - 1U) != 0) {
        return 0;
    }

    memset(buffer, 'x', sizeof(buffer));
    if (cna_color_copy_string(value, buffer, 2U, &bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != sizeof(expected_string) - 1U || buffer[0] != 'x' || buffer[1] != 'x' ||
        cna_color_copy_debug_string(value, 0, 1U, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_color_copy_debug_string(value, 0, 0U, &bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != sizeof(expected_debug) - 1U ||
        cna_color_get_string_byte_count(value, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_value_operations(void)
{
    const CNA_Color value = {64U, 128U, 255U, 32U};
    const CNA_Color equal = {64U, 128U, 255U, 32U};
    const CNA_Color other = {64U, 128U, 254U, 32U};
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    if (cna_color_equals(value, equal, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_color_not_equals(value, other, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_color_equals(value, equal, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_color_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        hash != (int32_t)UINT32_C(0x20ff8040)) {
        return 0;
    }

    CNA_Vector3 vector3 = {0.0F, 0.0F, 0.0F};
    CNA_Vector4 vector4 = {0.0F, 0.0F, 0.0F, 0.0F};
    if (cna_color_to_vector3(value, &vector3) != CNA_RESULT_SUCCESS ||
        !nearly_equal(vector3.x, 64.0F / 255.0F) ||
        !nearly_equal(vector3.y, 128.0F / 255.0F) || !nearly_equal(vector3.z, 1.0F) ||
        cna_color_to_vector4(value, &vector4) != CNA_RESULT_SUCCESS ||
        !nearly_equal(vector4.x, 64.0F / 255.0F) ||
        !nearly_equal(vector4.y, 128.0F / 255.0F) || !nearly_equal(vector4.z, 1.0F) ||
        !nearly_equal(vector4.w, 32.0F / 255.0F) ||
        cna_color_to_vector3(value, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Color result = value;
    const CNA_Color black = {0U, 0U, 0U, 255U};
    const CNA_Color white = {255U, 255U, 255U, 255U};
    if (cna_color_lerp(black, white, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 127U, 127U, 127U, 255U) ||
        cna_color_lerp(black, white, 2.0F, &result) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 255U, 255U, 255U, 255U) ||
        cna_color_multiply(value, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 32U, 64U, 127U, 16U) ||
        cna_color_multiply(value, 0.5F, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_color_from_non_premultiplied_vector4(
            (CNA_Vector4){0.8F, 0.4F, 0.2F, 0.5F}, &result) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 102U, 51U, 25U, 127U) ||
        cna_color_from_non_premultiplied_int(255, 128, 64, 128, &result) !=
            CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 128U, 64U, 32U, 128U) ||
        cna_color_from_non_premultiplied_int(
            INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX, &result) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 0U, 0U, 0U, 255U)) {
        return 0;
    }

    result = value;
    if (cna_color_pack_from_vector4(
            &result, (CNA_Vector4){2.0F, -1.0F, NAN, INFINITY}) != CNA_RESULT_SUCCESS ||
        !color_equals_channels(result, 254U, 1U, 0U, 0U) ||
        cna_color_pack_from_vector4(0, vector4) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

int main(void)
{
    return validate_construction() && validate_properties_and_strings() &&
            validate_value_operations() ? 0 : 1;
}
