// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int near_float(float left, float right)
{
    return fabsf(left - right) <= 0.00001F;
}

static int validate_math_helper(void)
{
    float value = 0.0F;
    int32_t integer = -1;
    CNA_Bool predicate = CNA_FALSE;
    /* Oracle: MathHelper.cs mathematical constants; the framework epsilon is the
       separate literal contract in CNA/C/math.h (2^-24). */
    if (CNA_MATH_E != 2.71828175F || CNA_MATH_LOG10_E != 0.4342945F ||
        CNA_MATH_LOG2_E != 1.442695F || CNA_MATH_PI != 3.14159274F ||
        CNA_MATH_PI_OVER_2 != 1.57079637F || CNA_MATH_PI_OVER_4 != 0.7853982F ||
        CNA_MATH_TWO_PI != 6.28318548F ||
        CNA_MATH_MACHINE_EPSILON_FLOAT != 5.96046448E-8F ||
        cna_math_barycentric(1.0F, 3.0F, 5.0F, 0.25F, 0.5F, &value) !=
            CNA_RESULT_SUCCESS || !near_float(value, 3.5F) ||
        cna_math_catmull_rom(0.0F, 1.0F, 2.0F, 3.0F, 0.5F, &value) !=
            CNA_RESULT_SUCCESS || !near_float(value, 1.5F) ||
        cna_math_clamp_float(3.0F, 0.0F, 2.0F, &value) != CNA_RESULT_SUCCESS ||
        value != 2.0F ||
        cna_math_distance(-2.0F, 3.0F, &value) != CNA_RESULT_SUCCESS || value != 5.0F ||
        cna_math_hermite(0.0F, 0.0F, 10.0F, 0.0F, 0.5F, &value) !=
            CNA_RESULT_SUCCESS || !near_float(value, 5.0F) ||
        cna_math_lerp(0.0F, 10.0F, 0.25F, &value) != CNA_RESULT_SUCCESS || value != 2.5F ||
        cna_math_max(-1.0F, 2.0F, &value) != CNA_RESULT_SUCCESS || value != 2.0F ||
        cna_math_min(-1.0F, 2.0F, &value) != CNA_RESULT_SUCCESS || value != -1.0F ||
        cna_math_smooth_step(0.0F, 10.0F, 0.5F, &value) != CNA_RESULT_SUCCESS ||
        !near_float(value, 5.0F) ||
        cna_math_smooth_step(0.0F, 10.0F, 2.0F, &value) != CNA_RESULT_SUCCESS ||
        value != 10.0F ||
        cna_math_to_degrees(CNA_MATH_PI, &value) != CNA_RESULT_SUCCESS ||
        !near_float(value, 180.0F) ||
        cna_math_to_radians(180.0F, &value) != CNA_RESULT_SUCCESS ||
        !near_float(value, CNA_MATH_PI) ||
        cna_math_wrap_angle(-CNA_MATH_PI, &value) != CNA_RESULT_SUCCESS ||
        !near_float(value, CNA_MATH_PI) ||
        cna_math_clamp_int32(12, -3, 7, &integer) != CNA_RESULT_SUCCESS || integer != 7 ||
        cna_math_within_epsilon(
            0.0F, CNA_MATH_MACHINE_EPSILON_FLOAT * 0.5F, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_math_within_epsilon(0.0F, CNA_MATH_MACHINE_EPSILON_FLOAT, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_FALSE) {
        return 0;
    }

    if (cna_math_closest_msaa_power(0, &integer) != CNA_RESULT_SUCCESS || integer != 0 ||
        cna_math_closest_msaa_power(1, &integer) != CNA_RESULT_SUCCESS || integer != 0 ||
        cna_math_closest_msaa_power(3, &integer) != CNA_RESULT_SUCCESS || integer != 2 ||
        cna_math_closest_msaa_power(8, &integer) != CNA_RESULT_SUCCESS || integer != 8 ||
        cna_math_closest_msaa_power(INT32_MAX, &integer) != CNA_RESULT_SUCCESS ||
        integer != INT32_C(1073741824)) {
        return 0;
    }

    integer = 77;
    if (cna_math_closest_msaa_power(-1, &integer) != CNA_RESULT_INVALID_ARGUMENT ||
        integer != 77 ||
        cna_math_closest_msaa_power(2, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_math_barycentric(0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_math_clamp_float(NAN, 0.0F, 1.0F, &value) != CNA_RESULT_SUCCESS ||
        !isnan(value) ||
        cna_math_wrap_angle(INFINITY, &value) != CNA_RESULT_SUCCESS || !isnan(value)) {
        return 0;
    }

    /* The other two clamp outcomes, after the upper one above. Oracle: FNA MathHelper's
       internal Clamp(int, int, int) tests max first and min second, so a reversed range
       (min 10 > max 0) yields min. */
    integer = 99;
    if (cna_math_clamp_int32(-5, -3, 7, &integer) != CNA_RESULT_SUCCESS || integer != -3 ||
        cna_math_clamp_int32(4, -3, 7, &integer) != CNA_RESULT_SUCCESS || integer != 4 ||
        cna_math_clamp_int32(5, 10, 0, &integer) != CNA_RESULT_SUCCESS || integer != 10 ||
        cna_math_clamp_int32(INT32_MIN, INT32_MIN + 1, INT32_MAX, &integer) !=
            CNA_RESULT_SUCCESS || integer != INT32_MIN + 1 ||
        cna_math_clamp_int32(1, 0, 2, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* Refresh MathHelper arithmetic: CatmullRom IL_0005,000a; Hermite
   IL_0006-0043; SmoothStep IL_0013-0024; Lerp IL_0000-0007;
   Barycentric IL_0000-000e. Expectations are IL-derived binary32 words,
   independent of native CNA results. Each finite precision call overwrites a sentinel. */
static int validate_tq072_interpolation_bits(void)
{
    float value = 99.0F;
    if (cna_math_lerp(0x1p25F, 1, 1, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x3f800000)) return 0;
    value = 99.0F;
    if (cna_math_barycentric(0x1p25F, 1, 0x1p25F, 1, 1, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x3f800000)) return 0;
    value = 99.0F;
    if (cna_math_catmull_rom(0, 0, 0, 2, 0x1.5a0f30p-1F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0xbe179d9c)) return 0;
    value = 99.0F;
    if (cna_math_hermite(1, 0, 0, 0, 0x1.c140d2p-1F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x3d2979e0)) return 0;
    value = 99.0F;
    if (cna_math_smooth_step(0, 1, 0x1.9660b6p-5F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x3be9e88a)) return 0;
    if (cna_math_catmull_rom(0, 0, 0, 2, 0x1p64F, &value) != CNA_RESULT_SUCCESS ||
        !isnan(value)) return 0;
    if (cna_math_catmull_rom(0, 0, 0, 0, 0x1p43F, &value) != CNA_RESULT_SUCCESS ||
        !isnan(value)) return 0;
    if (cna_math_hermite(1, 0, 0, 0, 0x1p64F, &value) != CNA_RESULT_SUCCESS ||
        !isnan(value)) return 0;
    if (cna_math_hermite(-0.0F, 0, 0, 0, 0, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0)) return 0;
    return 1;
}

/* Refresh ToDegrees/ToRadians IL_0001-0007: Single constants and return.
   WrapAngle IL_0001-0011: double IEEERemainder by widened Single TwoPi.
   Distance IL_0002-0008: Single Abs. All finite expectations are exact words. */
static int validate_tq072_angle_distance_bits(void)
{
    float value = 99.0F;
    if (cna_math_to_degrees(1, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x42652ee0)) return 0;
    if (cna_math_to_radians(0x1.ad7f2ap-24F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x30efe050)) return 0;
    if (cna_math_to_degrees(-0.0F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x80000000)) return 0;
    if (cna_math_to_radians(-0.0F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x80000000)) return 0;
    if (cna_math_wrap_angle(0x1.fffffep127F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0x3fddb0f8)) return 0;
    if (cna_math_wrap_angle(-0x1.fffffep127F, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0xbfddb0f8)) return 0;
    if (cna_math_distance(-0.0F, 0, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(0)) return 0;
    if (cna_math_distance(0x1p-149F, 0, &value) != CNA_RESULT_SUCCESS ||
        float_bits(value) != UINT32_C(1)) return 0;
    if (cna_math_distance(INFINITY, INFINITY, &value) != CNA_RESULT_SUCCESS ||
        !isnan(value)) return 0;
    return 1;
}

/* Oracle: the XNA 4.0 Microsoft.Xna.Framework.dll MathHelper literal fields (E, Log10E, Log2E,
   Pi, PiOver2, PiOver4, TwoPi; ikdasm `.field public static literal float32`), whose round-trip
   decimals are exactly these IEEE-754 single words. The framework epsilon has no XNA field; its
   literal in CNA/C/math.h, 5.96046448E-8F, is hand-derived to be exactly 2^-24 (biased exponent
   127 - 24 = 103 = 0x67, empty mantissa: 0x33800000). Comparing bit words, not decimal spellings,
   keeps a nearby float (2.718282F is 0x402DF855) from passing. */
static int validate_math_constant_bits(void)
{
    return float_bits(CNA_MATH_E) == UINT32_C(0x402DF854) &&
        float_bits(CNA_MATH_LOG10_E) == UINT32_C(0x3EDE5BD9) &&
        float_bits(CNA_MATH_LOG2_E) == UINT32_C(0x3FB8AA3B) &&
        float_bits(CNA_MATH_PI) == UINT32_C(0x40490FDB) &&
        float_bits(CNA_MATH_PI_OVER_2) == UINT32_C(0x3FC90FDB) &&
        float_bits(CNA_MATH_PI_OVER_4) == UINT32_C(0x3F490FDB) &&
        float_bits(CNA_MATH_TWO_PI) == UINT32_C(0x40C90FDB) &&
        float_bits(CNA_MATH_MACHINE_EPSILON_FLOAT) == UINT32_C(0x33800000);
}

static int validate_math_bounds_bits(void)
{
    const uint32_t samples[] = {UINT32_C(0xff800000), UINT32_C(0xbf800000),
        UINT32_C(0x80000000), 0, UINT32_C(0x3f800000), UINT32_C(0x7f800000),
        UINT32_C(0x7fc00001), UINT32_C(0xffc12345)};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        for (unsigned j = 0; j < sizeof(samples) / sizeof(samples[0]); ++j) {
            float a, b, result;
            memcpy(&a, &samples[i], sizeof(a));
            memcpy(&b, &samples[j], sizeof(b));
            const float minimum = isnan(a) || a < b ? a : b;
            const float maximum = isnan(a) || a > b ? a : b;
            if (cna_math_min(a, b, &result) != CNA_RESULT_SUCCESS ||
                float_bits(result) != float_bits(minimum) ||
                cna_math_max(a, b, &result) != CNA_RESULT_SUCCESS ||
                float_bits(result) != float_bits(maximum)) return 0;
        }
    }
    float result;
    return cna_math_clamp_float(5, 10, 0, &result) == CNA_RESULT_SUCCESS && result == 10;
}

static int point_equals(CNA_Point left, CNA_Point right)
{
    return left.x == right.x && left.y == right.y;
}

static int rectangle_equals(CNA_Rectangle left, CNA_Rectangle right)
{
    return left.x == right.x && left.y == right.y &&
        left.width == right.width && left.height == right.height;
}

static int validate_point(void)
{
    CNA_Point zero = {99, 99};
    CNA_Point point = {99, 99};
    CNA_Point result = {77, 88};
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    if (cna_point_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_point_init(&zero) != CNA_RESULT_SUCCESS || !point_equals(zero, (CNA_Point){0, 0}) ||
        cna_point_init_xy(3, -2, &point) != CNA_RESULT_SUCCESS ||
        !point_equals(point, (CNA_Point){3, -2}) ||
        cna_point_get_zero(&zero) != CNA_RESULT_SUCCESS ||
        cna_point_equals(point, point, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_point_not_equals(point, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_point_equals(point, zero, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_point_get_hash_code(point, &hash) != CNA_RESULT_SUCCESS || hash != 1) {
        return 0;
    }

    /* Oracle: XNA Point.get_Zero returns the static _zero that Point..cctor initobj-clears
       (IL_0000-IL_0005), so the output is {0, 0} whatever the destination held. Equals compares
       X, then Y (Equals(Point) IL_0000-IL_001e); op_Inequality is true when either differs
       (IL_0000-IL_0025). The Y-only and X-only pairs keep a one-component comparison from
       passing. */
    CNA_Point zero_out = {7, -7};
    if (cna_point_get_zero(&zero_out) != CNA_RESULT_SUCCESS ||
        !point_equals(zero_out, (CNA_Point){0, 0}) ||
        cna_point_get_zero(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_point_equals(point, (CNA_Point){3, 5}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_point_equals(point, (CNA_Point){4, -2}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_point_equals(point, (CNA_Point){3, -2}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_point_not_equals(point, (CNA_Point){3, -2}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_point_not_equals(point, (CNA_Point){3, 5}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_point_not_equals(point, (CNA_Point){4, -2}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_point_not_equals(point, zero, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    static const char Expected[] = "{X:3 Y:-2}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small[2] = {'q', 'r'};
    if (cna_point_get_string_size(point, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_point_copy_string(point, too_small, sizeof(too_small), &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        too_small[0] != 'q' || too_small[1] != 'r' ||
        cna_point_copy_string(point, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0 ||
        cna_point_copy_string(point, 0, 1U, &byte_count) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_point_add((CNA_Point){INT32_MAX, 2}, (CNA_Point){1, 3}, &result) !=
            CNA_RESULT_SUCCESS ||
        !point_equals(result, (CNA_Point){INT32_MIN, 5}) ||
        cna_point_subtract((CNA_Point){INT32_MIN, 3}, (CNA_Point){1, 5}, &result) !=
            CNA_RESULT_SUCCESS ||
        !point_equals(result, (CNA_Point){INT32_MAX, -2}) ||
        cna_point_multiply((CNA_Point){INT32_MAX, -3}, (CNA_Point){2, 4}, &result) !=
            CNA_RESULT_SUCCESS ||
        !point_equals(result, (CNA_Point){-2, -12}) ||
        cna_point_divide((CNA_Point){12, -9}, (CNA_Point){3, 2}, &result) !=
            CNA_RESULT_SUCCESS ||
        !point_equals(result, (CNA_Point){4, -4})) {
        return 0;
    }

    result = (CNA_Point){77, 88};
    if (cna_point_divide(point, (CNA_Point){1, 0}, &result) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !point_equals(result, (CNA_Point){77, 88}) ||
        cna_point_divide((CNA_Point){INT32_MIN, 1}, (CNA_Point){-1, 1}, &result) !=
            CNA_RESULT_OVERFLOW ||
        !point_equals(result, (CNA_Point){77, 88}) ||
        cna_point_add(point, zero, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_rectangle(void)
{
    CNA_Rectangle empty = {1, 1, 1, 1};
    CNA_Rectangle rectangle = {1, 2, 10, 8};
    CNA_Rectangle result = {0, 0, 0, 0};
    CNA_Point point = {0, 0};
    CNA_Bool predicate = CNA_FALSE;
    int32_t scalar = 0;
    if (cna_rectangle_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_rectangle_init(&empty) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(empty, (CNA_Rectangle){0, 0, 0, 0}) ||
        cna_rectangle_init_xywh(1, 2, 10, 8, &result) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, rectangle) ||
        cna_rectangle_get_empty(&empty) != CNA_RESULT_SUCCESS ||
        cna_rectangle_get_left(rectangle, &scalar) != CNA_RESULT_SUCCESS || scalar != 1 ||
        cna_rectangle_get_right(rectangle, &scalar) != CNA_RESULT_SUCCESS || scalar != 11 ||
        cna_rectangle_get_top(rectangle, &scalar) != CNA_RESULT_SUCCESS || scalar != 2 ||
        cna_rectangle_get_bottom(rectangle, &scalar) != CNA_RESULT_SUCCESS || scalar != 10 ||
        cna_rectangle_get_right((CNA_Rectangle){INT32_MAX, 0, 1, 0}, &scalar) !=
            CNA_RESULT_SUCCESS || scalar != INT32_MIN ||
        cna_rectangle_get_location(rectangle, &point) != CNA_RESULT_SUCCESS ||
        !point_equals(point, (CNA_Point){1, 2}) ||
        cna_rectangle_get_center(rectangle, &point) != CNA_RESULT_SUCCESS ||
        !point_equals(point, (CNA_Point){6, 6}) ||
        cna_rectangle_get_is_empty(empty, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE) {
        return 0;
    }

    if (cna_rectangle_contains_xy(rectangle, 1, 2, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_rectangle_contains_xy(rectangle, 11, 9, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_rectangle_contains_point(rectangle, (CNA_Point){10, 9}, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_rectangle_contains_rectangle(
            rectangle, (CNA_Rectangle){2, 3, 2, 2}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_rectangle_contains_rectangle(
            rectangle, (CNA_Rectangle){0, 3, 2, 2}, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE) {
        return 0;
    }

    result = rectangle;
    if (cna_rectangle_set_location(&result, (CNA_Point){3, 4}) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, (CNA_Rectangle){3, 4, 10, 8}) ||
        cna_rectangle_offset_point(&result, (CNA_Point){2, -1}) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, (CNA_Rectangle){5, 3, 10, 8}) ||
        cna_rectangle_offset_xy(&result, -5, -3) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, (CNA_Rectangle){0, 0, 10, 8}) ||
        cna_rectangle_inflate(&result, 2, 3) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, (CNA_Rectangle){-2, -3, 14, 14}) ||
        cna_rectangle_set_location(0, point) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_rectangle_offset_xy(0, 1, 1) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_rectangle_inflate(0, 1, 1) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    int32_t hash = 0;
    if (cna_rectangle_equals(rectangle, rectangle, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_rectangle_not_equals(rectangle, result, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_rectangle_get_hash_code(rectangle, &hash) != CNA_RESULT_SUCCESS ||
        hash != 21) {
        return 0;
    }

    static const char Expected[] = "{X:1 Y:2 Width:10 Height:8}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'z';
    if (cna_rectangle_get_string_size(rectangle, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_rectangle_copy_string(rectangle, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'z' ||
        cna_rectangle_copy_string(rectangle, bytes, sizeof(bytes), &byte_count) !=
            CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }

    const CNA_Rectangle overlap = {5, 5, 10, 10};
    const CNA_Rectangle touching = {11, 2, 2, 2};
    if (cna_rectangle_intersects(rectangle, overlap, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_rectangle_intersects(rectangle, touching, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_rectangle_intersect(rectangle, overlap, &result) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, (CNA_Rectangle){5, 5, 6, 5}) ||
        cna_rectangle_intersect(rectangle, touching, &result) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, empty) ||
        cna_rectangle_union(rectangle, overlap, &result) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(result, (CNA_Rectangle){1, 2, 14, 13}) ||
        cna_rectangle_intersect(rectangle, overlap, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_rectangle_union(rectangle, overlap, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_integer_hashes(void)
{
    const int32_t samples[] = {INT32_MIN, INT32_MAX, -2, -1, 0, 1, 3, 7};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        for (unsigned j = 0; j < sizeof(samples) / sizeof(samples[0]); ++j) {
            const int32_t x = samples[i], y = samples[j];
            const uint32_t point_sum = (uint32_t)x + (uint32_t)y;
            int32_t hash;
            if (cna_point_get_hash_code((CNA_Point){x, y}, &hash) != CNA_RESULT_SUCCESS ||
                (uint32_t)hash != point_sum) return 0;
            for (unsigned k = 0; k < sizeof(samples) / sizeof(samples[0]); ++k) {
                for (unsigned l = 0; l < sizeof(samples) / sizeof(samples[0]); ++l) {
                    const int32_t width = samples[k], height = samples[l];
                    const uint32_t rectangle_sum = point_sum + (uint32_t)width + (uint32_t)height;
                    if (cna_rectangle_get_hash_code((CNA_Rectangle){x, y, width, height}, &hash) !=
                        CNA_RESULT_SUCCESS || (uint32_t)hash != rectangle_sum) return 0;
                }
            }
        }
    }
    return 1;
}

static int validate_rectangle_union_aliases(void)
{
    const CNA_Rectangle origin = {0, 0, 12, 12};
    CNA_Rectangle left = {10, 10, 5, 5}, right = left, all = left;
    return cna_rectangle_union(left, origin, &left) == CNA_RESULT_SUCCESS &&
        rectangle_equals(left, (CNA_Rectangle){0, 0, 15, 15}) &&
        cna_rectangle_union(origin, right, &right) == CNA_RESULT_SUCCESS &&
        rectangle_equals(right, (CNA_Rectangle){0, 0, 15, 15}) &&
        cna_rectangle_union(all, all, &all) == CNA_RESULT_SUCCESS &&
        rectangle_equals(all, (CNA_Rectangle){10, 10, 5, 5});
}

/* Hand h=2^127: opposite midpoint is 0 and barycentric .25 step is h/2. */
static int validate_tq044_contract(void)
{
    const float h=0x1p127F; float output=99;
    if (cna_math_lerp(h,-h,.5F,&output) != CNA_RESULT_SUCCESS || output != 0) return 0;
    if (cna_math_barycentric(h,-h,h,.25F,.25F,&output) != CNA_RESULT_SUCCESS || output != h/2) return 0;
    /* Catmull(-h,0,h,0,.5)=5h/8 by the Refresh polynomial. */
    if (cna_math_catmull_rom(-h,0,h,0,.5F,&output) != CNA_RESULT_SUCCESS || output != 0x1.4p126F) return 0;
    return cna_math_smooth_step(h,-h,.5F,&output) == CNA_RESULT_SUCCESS && output == 0;
}

/* Refresh Rectangle.Intersect(value,value) IL_00a0-00a6 and ref/ref/out
   IL_0090-0096 require positive endpoint intersection, even when Intersects=true. */
static int validate_tq051_degenerate_intersection(void)
{
    const CNA_Rectangle outer={0,0,10,10};
    const CNA_Rectangle cases[]={{5,5,0,2},{5,5,2,0},{5,5,0,0},{5,5,-2,2},{5,5,2,-2},{5,5,-2,-2}};
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        CNA_Bool intersects=CNA_FALSE; CNA_Rectangle out={99,98,97,96};
        if(cna_rectangle_intersects(outer,cases[i],&intersects) != CNA_RESULT_SUCCESS || intersects != CNA_TRUE ||
           cna_rectangle_intersect(outer,cases[i],&out) != CNA_RESULT_SUCCESS ||
           !rectangle_equals(out,(CNA_Rectangle){0,0,0,0}) ||
           cna_rectangle_intersect(cases[i],outer,&out) != CNA_RESULT_SUCCESS ||
           !rectangle_equals(out,(CNA_Rectangle){0,0,0,0})) return 0;
        out=cases[i];
        if(cna_rectangle_intersect(outer,out,&out) != CNA_RESULT_SUCCESS ||
           !rectangle_equals(out,(CNA_Rectangle){0,0,0,0})) return 0;
    }
    return 1;
}

static int validate_tq051_rectangle_wrapping(void)
{
    /* Refresh get_Right/Bottom IL_000c; Center IL_000d/001c div then add;
       Offset and Inflate unchecked add/sub/mul. Literal modular arithmetic. */
    const CNA_Rectangle r={INT32_MAX-1,INT32_MAX-2,4,6};
    int32_t edge=99; CNA_Point center; CNA_Bool predicate=CNA_TRUE;
    if(cna_rectangle_get_right(r,&edge) != CNA_RESULT_SUCCESS || edge != INT32_MIN+2 ||
       cna_rectangle_get_bottom(r,&edge) != CNA_RESULT_SUCCESS || edge != INT32_MIN+3 ||
       cna_rectangle_get_center(r,&center) != CNA_RESULT_SUCCESS || center.x != INT32_MIN || center.y != INT32_MIN ||
       cna_rectangle_contains_xy(r,r.x,r.y,&predicate) != CNA_RESULT_SUCCESS || predicate != CNA_FALSE ||
       cna_rectangle_contains_point(r,(CNA_Point){r.x,r.y},&predicate) != CNA_RESULT_SUCCESS || predicate != CNA_FALSE ||
       cna_rectangle_contains_rectangle(r,r,&predicate) != CNA_RESULT_SUCCESS || predicate != CNA_TRUE) return 0;
    const CNA_Rectangle wide={INT32_MIN,INT32_MIN,-1,-1};
    if(cna_rectangle_contains_xy(wide,0,0,&predicate) != CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
       cna_rectangle_contains_point(wide,(CNA_Point){INT32_MAX,0},&predicate) != CNA_RESULT_SUCCESS || predicate != CNA_FALSE) return 0;
    CNA_Rectangle by_point={INT32_MAX,INT32_MIN,7,-9}, by_xy=by_point;
    if(cna_rectangle_offset_point(&by_point,(CNA_Point){1,-1}) != CNA_RESULT_SUCCESS ||
       !rectangle_equals(by_point,(CNA_Rectangle){INT32_MIN,INT32_MAX,7,-9}) ||
       cna_rectangle_offset_xy(&by_xy,1,-1) != CNA_RESULT_SUCCESS ||
       !rectangle_equals(by_xy,(CNA_Rectangle){INT32_MIN,INT32_MAX,7,-9})) return 0;
    CNA_Rectangle inflated={1,2,3,4};
    if(cna_rectangle_inflate(&inflated,1073741824,INT32_MIN) != CNA_RESULT_SUCCESS ||
       !rectangle_equals(inflated,(CNA_Rectangle){-1073741823,INT32_MIN+2,INT32_MIN+3,4})) return 0;
    /* Union/Intersect subtraction wraps too: MAX-MIN = 2^32-1 -> -1. */
    CNA_Rectangle out;
    return cna_rectangle_union((CNA_Rectangle){INT32_MIN,INT32_MIN,1,1},
                              (CNA_Rectangle){INT32_MAX-1,INT32_MAX-1,1,1},&out) == CNA_RESULT_SUCCESS &&
        rectangle_equals(out,wide) &&
        cna_rectangle_intersect(wide,wide,&out) == CNA_RESULT_SUCCESS && rectangle_equals(out,wide);
}

static int validate_tq051_point_wrapping(void)
{
    /* math.h point add/subtract/multiply: component-wise unchecked int32 wrapping.
       These arithmetic routes are CNA extensions; Refresh Point has only ==/!=. */
    CNA_Point out={99,98};
    return cna_point_add((CNA_Point){INT32_MAX,INT32_MIN},(CNA_Point){1,-1},&out) == CNA_RESULT_SUCCESS &&
        out.x==INT32_MIN && out.y==INT32_MAX &&
        cna_point_subtract((CNA_Point){INT32_MIN,INT32_MAX},(CNA_Point){1,-1},&out) == CNA_RESULT_SUCCESS &&
        out.x==INT32_MAX && out.y==INT32_MIN &&
        cna_point_multiply((CNA_Point){65536,INT32_MIN},(CNA_Point){65536,-1},&out) == CNA_RESULT_SUCCESS &&
        out.x==0 && out.y==INT32_MIN;
}

int main(void)
{
    const int tq051_degenerate = CNA_TEST_STAGE(validate_tq051_degenerate_intersection());
    const int tq051_wrapping = CNA_TEST_STAGE(validate_tq051_rectangle_wrapping());
    const int tq051_point = CNA_TEST_STAGE(validate_tq051_point_wrapping());
    if (!CNA_TEST_STAGE(validate_tq044_contract())) return 1;
    /* Keep every adjacent control visible when one compatibility group fails. */
    const int tq072_interpolation = CNA_TEST_STAGE(validate_tq072_interpolation_bits());
    const int tq072_angles = CNA_TEST_STAGE(validate_tq072_angle_distance_bits());
    const int math = CNA_TEST_STAGE(validate_math_helper());
    const int constants = CNA_TEST_STAGE(validate_math_constant_bits());
    const int point = CNA_TEST_STAGE(validate_point());
    const int rectangle = CNA_TEST_STAGE(validate_rectangle());
    const int bounds = CNA_TEST_STAGE(validate_math_bounds_bits());
    const int hashes = CNA_TEST_STAGE(validate_integer_hashes());
    const int aliases = CNA_TEST_STAGE(validate_rectangle_union_aliases());
    return tq072_interpolation && tq072_angles && math && constants && point && rectangle && bounds && hashes && aliases && tq051_degenerate && tq051_wrapping && tq051_point ? 0 : 1;
}
