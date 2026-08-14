// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <string.h>

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
        cna_point_get_hash_code(point, &hash) != CNA_RESULT_SUCCESS || hash != (3 ^ -2)) {
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
        hash != (1 ^ 2 ^ 10 ^ 8)) {
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

int main(void)
{
    return validate_point() && validate_rectangle() ? 0 : 1;
}
