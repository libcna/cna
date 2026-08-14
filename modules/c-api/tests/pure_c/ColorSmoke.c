// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stddef.h>
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

typedef struct NamedColorCase {
    CNA_Color color;
    uint32_t packed;
} NamedColorCase;

static int validate_named_colors(void)
{
    const NamedColorCase cases[] = {
        {CNA_COLOR_TRANSPARENT, UINT32_C(0x00000000)},
        {CNA_COLOR_ALICE_BLUE, UINT32_C(0xfffff8f0)},
        {CNA_COLOR_ANTIQUE_WHITE, UINT32_C(0xffd7ebfa)},
        {CNA_COLOR_AQUA, UINT32_C(0xffffff00)},
        {CNA_COLOR_AQUAMARINE, UINT32_C(0xffd4ff7f)},
        {CNA_COLOR_AZURE, UINT32_C(0xfffffff0)},
        {CNA_COLOR_BEIGE, UINT32_C(0xffdcf5f5)},
        {CNA_COLOR_BISQUE, UINT32_C(0xffc4e4ff)},
        {CNA_COLOR_BLACK, UINT32_C(0xff000000)},
        {CNA_COLOR_BLANCHED_ALMOND, UINT32_C(0xffcdebff)},
        {CNA_COLOR_BLUE, UINT32_C(0xffff0000)},
        {CNA_COLOR_BLUE_VIOLET, UINT32_C(0xffe22b8a)},
        {CNA_COLOR_BROWN, UINT32_C(0xff2a2aa5)},
        {CNA_COLOR_BURLY_WOOD, UINT32_C(0xff87b8de)},
        {CNA_COLOR_CADET_BLUE, UINT32_C(0xffa09e5f)},
        {CNA_COLOR_CHARTREUSE, UINT32_C(0xff00ff7f)},
        {CNA_COLOR_CHOCOLATE, UINT32_C(0xff1e69d2)},
        {CNA_COLOR_CORAL, UINT32_C(0xff507fff)},
        {CNA_COLOR_CORNFLOWER_BLUE, UINT32_C(0xffed9564)},
        {CNA_COLOR_CORNSILK, UINT32_C(0xffdcf8ff)},
        {CNA_COLOR_CRIMSON, UINT32_C(0xff3c14dc)},
        {CNA_COLOR_CYAN, UINT32_C(0xffffff00)},
        {CNA_COLOR_DARK_BLUE, UINT32_C(0xff8b0000)},
        {CNA_COLOR_DARK_CYAN, UINT32_C(0xff8b8b00)},
        {CNA_COLOR_DARK_GOLDENROD, UINT32_C(0xff0b86b8)},
        {CNA_COLOR_DARK_GRAY, UINT32_C(0xffa9a9a9)},
        {CNA_COLOR_DARK_GREEN, UINT32_C(0xff006400)},
        {CNA_COLOR_DARK_KHAKI, UINT32_C(0xff6bb7bd)},
        {CNA_COLOR_DARK_MAGENTA, UINT32_C(0xff8b008b)},
        {CNA_COLOR_DARK_OLIVE_GREEN, UINT32_C(0xff2f6b55)},
        {CNA_COLOR_DARK_ORANGE, UINT32_C(0xff008cff)},
        {CNA_COLOR_DARK_ORCHID, UINT32_C(0xffcc3299)},
        {CNA_COLOR_DARK_RED, UINT32_C(0xff00008b)},
        {CNA_COLOR_DARK_SALMON, UINT32_C(0xff7a96e9)},
        {CNA_COLOR_DARK_SEA_GREEN, UINT32_C(0xff8bbc8f)},
        {CNA_COLOR_DARK_SLATE_BLUE, UINT32_C(0xff8b3d48)},
        {CNA_COLOR_DARK_SLATE_GRAY, UINT32_C(0xff4f4f2f)},
        {CNA_COLOR_DARK_TURQUOISE, UINT32_C(0xffd1ce00)},
        {CNA_COLOR_DARK_VIOLET, UINT32_C(0xffd30094)},
        {CNA_COLOR_DEEP_PINK, UINT32_C(0xff9314ff)},
        {CNA_COLOR_DEEP_SKY_BLUE, UINT32_C(0xffffbf00)},
        {CNA_COLOR_DIM_GRAY, UINT32_C(0xff696969)},
        {CNA_COLOR_DODGER_BLUE, UINT32_C(0xffff901e)},
        {CNA_COLOR_FIREBRICK, UINT32_C(0xff2222b2)},
        {CNA_COLOR_FLORAL_WHITE, UINT32_C(0xfff0faff)},
        {CNA_COLOR_FOREST_GREEN, UINT32_C(0xff228b22)},
        {CNA_COLOR_FUCHSIA, UINT32_C(0xffff00ff)},
        {CNA_COLOR_GAINSBORO, UINT32_C(0xffdcdcdc)},
        {CNA_COLOR_GHOST_WHITE, UINT32_C(0xfffff8f8)},
        {CNA_COLOR_GOLD, UINT32_C(0xff00d7ff)},
        {CNA_COLOR_GOLDENROD, UINT32_C(0xff20a5da)},
        {CNA_COLOR_GRAY, UINT32_C(0xff808080)},
        {CNA_COLOR_GREEN, UINT32_C(0xff008000)},
        {CNA_COLOR_GREEN_YELLOW, UINT32_C(0xff2fffad)},
        {CNA_COLOR_HONEYDEW, UINT32_C(0xfff0fff0)},
        {CNA_COLOR_HOT_PINK, UINT32_C(0xffb469ff)},
        {CNA_COLOR_INDIAN_RED, UINT32_C(0xff5c5ccd)},
        {CNA_COLOR_INDIGO, UINT32_C(0xff82004b)},
        {CNA_COLOR_IVORY, UINT32_C(0xfff0ffff)},
        {CNA_COLOR_KHAKI, UINT32_C(0xff8ce6f0)},
        {CNA_COLOR_LAVENDER, UINT32_C(0xfffae6e6)},
        {CNA_COLOR_LAVENDER_BLUSH, UINT32_C(0xfff5f0ff)},
        {CNA_COLOR_LAWN_GREEN, UINT32_C(0xff00fc7c)},
        {CNA_COLOR_LEMON_CHIFFON, UINT32_C(0xffcdfaff)},
        {CNA_COLOR_LIGHT_BLUE, UINT32_C(0xffe6d8ad)},
        {CNA_COLOR_LIGHT_CORAL, UINT32_C(0xff8080f0)},
        {CNA_COLOR_LIGHT_CYAN, UINT32_C(0xffffffe0)},
        {CNA_COLOR_LIGHT_GOLDENROD_YELLOW, UINT32_C(0xffd2fafa)},
        {CNA_COLOR_LIGHT_GRAY, UINT32_C(0xffd3d3d3)},
        {CNA_COLOR_LIGHT_GREEN, UINT32_C(0xff90ee90)},
        {CNA_COLOR_LIGHT_PINK, UINT32_C(0xffc1b6ff)},
        {CNA_COLOR_LIGHT_SALMON, UINT32_C(0xff7aa0ff)},
        {CNA_COLOR_LIGHT_SEA_GREEN, UINT32_C(0xffaab220)},
        {CNA_COLOR_LIGHT_SKY_BLUE, UINT32_C(0xffface87)},
        {CNA_COLOR_LIGHT_SLATE_GRAY, UINT32_C(0xff998877)},
        {CNA_COLOR_LIGHT_STEEL_BLUE, UINT32_C(0xffdec4b0)},
        {CNA_COLOR_LIGHT_YELLOW, UINT32_C(0xffe0ffff)},
        {CNA_COLOR_LIME, UINT32_C(0xff00ff00)},
        {CNA_COLOR_LIME_GREEN, UINT32_C(0xff32cd32)},
        {CNA_COLOR_LINEN, UINT32_C(0xffe6f0fa)},
        {CNA_COLOR_MAGENTA, UINT32_C(0xffff00ff)},
        {CNA_COLOR_MAROON, UINT32_C(0xff000080)},
        {CNA_COLOR_MEDIUM_AQUAMARINE, UINT32_C(0xffaacd66)},
        {CNA_COLOR_MEDIUM_BLUE, UINT32_C(0xffcd0000)},
        {CNA_COLOR_MEDIUM_ORCHID, UINT32_C(0xffd355ba)},
        {CNA_COLOR_MEDIUM_PURPLE, UINT32_C(0xffdb7093)},
        {CNA_COLOR_MEDIUM_SEA_GREEN, UINT32_C(0xff71b33c)},
        {CNA_COLOR_MEDIUM_SLATE_BLUE, UINT32_C(0xffee687b)},
        {CNA_COLOR_MEDIUM_SPRING_GREEN, UINT32_C(0xff9afa00)},
        {CNA_COLOR_MEDIUM_TURQUOISE, UINT32_C(0xffccd148)},
        {CNA_COLOR_MEDIUM_VIOLET_RED, UINT32_C(0xff8515c7)},
        {CNA_COLOR_MIDNIGHT_BLUE, UINT32_C(0xff701919)},
        {CNA_COLOR_MINT_CREAM, UINT32_C(0xfffafff5)},
        {CNA_COLOR_MISTY_ROSE, UINT32_C(0xffe1e4ff)},
        {CNA_COLOR_MOCCASIN, UINT32_C(0xffb5e4ff)},
        {CNA_COLOR_NAVAJO_WHITE, UINT32_C(0xffaddeff)},
        {CNA_COLOR_NAVY, UINT32_C(0xff800000)},
        {CNA_COLOR_OLD_LACE, UINT32_C(0xffe6f5fd)},
        {CNA_COLOR_OLIVE, UINT32_C(0xff008080)},
        {CNA_COLOR_OLIVE_DRAB, UINT32_C(0xff238e6b)},
        {CNA_COLOR_ORANGE, UINT32_C(0xff00a5ff)},
        {CNA_COLOR_ORANGE_RED, UINT32_C(0xff0045ff)},
        {CNA_COLOR_ORCHID, UINT32_C(0xffd670da)},
        {CNA_COLOR_PALE_GOLDENROD, UINT32_C(0xffaae8ee)},
        {CNA_COLOR_PALE_GREEN, UINT32_C(0xff98fb98)},
        {CNA_COLOR_PALE_TURQUOISE, UINT32_C(0xffeeeeaf)},
        {CNA_COLOR_PALE_VIOLET_RED, UINT32_C(0xff9370db)},
        {CNA_COLOR_PAPAYA_WHIP, UINT32_C(0xffd5efff)},
        {CNA_COLOR_PEACH_PUFF, UINT32_C(0xffb9daff)},
        {CNA_COLOR_PERU, UINT32_C(0xff3f85cd)},
        {CNA_COLOR_PINK, UINT32_C(0xffcbc0ff)},
        {CNA_COLOR_PLUM, UINT32_C(0xffdda0dd)},
        {CNA_COLOR_POWDER_BLUE, UINT32_C(0xffe6e0b0)},
        {CNA_COLOR_PURPLE, UINT32_C(0xff800080)},
        {CNA_COLOR_RED, UINT32_C(0xff0000ff)},
        {CNA_COLOR_ROSY_BROWN, UINT32_C(0xff8f8fbc)},
        {CNA_COLOR_ROYAL_BLUE, UINT32_C(0xffe16941)},
        {CNA_COLOR_SADDLE_BROWN, UINT32_C(0xff13458b)},
        {CNA_COLOR_SALMON, UINT32_C(0xff7280fa)},
        {CNA_COLOR_SANDY_BROWN, UINT32_C(0xff60a4f4)},
        {CNA_COLOR_SEA_GREEN, UINT32_C(0xff578b2e)},
        {CNA_COLOR_SEA_SHELL, UINT32_C(0xffeef5ff)},
        {CNA_COLOR_SIENNA, UINT32_C(0xff2d52a0)},
        {CNA_COLOR_SILVER, UINT32_C(0xffc0c0c0)},
        {CNA_COLOR_SKY_BLUE, UINT32_C(0xffebce87)},
        {CNA_COLOR_SLATE_BLUE, UINT32_C(0xffcd5a6a)},
        {CNA_COLOR_SLATE_GRAY, UINT32_C(0xff908070)},
        {CNA_COLOR_SNOW, UINT32_C(0xfffafaff)},
        {CNA_COLOR_SPRING_GREEN, UINT32_C(0xff7fff00)},
        {CNA_COLOR_STEEL_BLUE, UINT32_C(0xffb48246)},
        {CNA_COLOR_TAN, UINT32_C(0xff8cb4d2)},
        {CNA_COLOR_TEAL, UINT32_C(0xff808000)},
        {CNA_COLOR_THISTLE, UINT32_C(0xffd8bfd8)},
        {CNA_COLOR_TOMATO, UINT32_C(0xff4763ff)},
        {CNA_COLOR_TURQUOISE, UINT32_C(0xffd0e040)},
        {CNA_COLOR_VIOLET, UINT32_C(0xffee82ee)},
        {CNA_COLOR_WHEAT, UINT32_C(0xffb3def5)},
        {CNA_COLOR_WHITE, UINT32_C(0xffffffff)},
        {CNA_COLOR_WHITE_SMOKE, UINT32_C(0xfff5f5f5)},
        {CNA_COLOR_YELLOW, UINT32_C(0xff00ffff)},
        {CNA_COLOR_YELLOW_GREEN, UINT32_C(0xff32cd9a)},
    };
    _Static_assert(
        sizeof(cases) / sizeof(cases[0]) == 141U,
        "the complete named-color table must remain covered");

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint32_t packed = 0U;
        if (cna_color_get_packed_value(cases[index].color, &packed) != CNA_RESULT_SUCCESS ||
            packed != cases[index].packed) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    return validate_construction() && validate_properties_and_strings() &&
            validate_value_operations() && validate_named_colors() ? 0 : 1;
}
