// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MATH_H
#define CNA_C_MATH_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes a point to zero.
 *
 * @param out_value Receives the initialized point.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_init(CNA_Point* out_value);

/**
 * @brief Initializes a point from integer coordinates.
 *
 * @param x Horizontal coordinate.
 * @param y Vertical coordinate.
 * @param out_value Receives the initialized point.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_init_xy(int32_t x, int32_t y, CNA_Point* out_value);

/**
 * @brief Gets the canonical zero point.
 *
 * @param out_value Receives `{0, 0}`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_get_zero(CNA_Point* out_value);

/**
 * @brief Tests two points for component equality.
 *
 * @param left First point.
 * @param right Second point.
 * @param out_equal Receives `CNA_TRUE` when both components match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_equals(
    CNA_Point left,
    CNA_Point right,
    CNA_Bool* out_equal);

/**
 * @brief Tests two points for component inequality.
 *
 * @param left First point.
 * @param right Second point.
 * @param out_not_equal Receives `CNA_TRUE` when any component differs.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_not_equals(
    CNA_Point left,
    CNA_Point right,
    CNA_Bool* out_not_equal);

/**
 * @brief Computes the canonical Point hash code.
 *
 * @param value Point to hash.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_get_hash_code(CNA_Point value, int32_t* out_hash);

/**
 * @brief Gets the UTF-8 byte count of the canonical Point string.
 *
 * @param value Point to format.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_get_string_size(CNA_Point value, uint64_t* out_bytes);

/**
 * @brief Copies the canonical Point string as UTF-8 bytes without a terminator.
 *
 * @param value Point to format.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or an argument error.
 */
CNA_C_API CNA_Result cna_point_copy_string(
    CNA_Point value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Adds two points component-wise with unchecked 32-bit wraparound.
 *
 * @param left First point.
 * @param right Second point.
 * @param out_value Receives the sum.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_add(
    CNA_Point left,
    CNA_Point right,
    CNA_Point* out_value);

/**
 * @brief Subtracts two points component-wise with unchecked 32-bit wraparound.
 *
 * @param left First point.
 * @param right Second point.
 * @param out_value Receives the difference.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_subtract(
    CNA_Point left,
    CNA_Point right,
    CNA_Point* out_value);

/**
 * @brief Multiplies two points component-wise with unchecked 32-bit wraparound.
 *
 * @param left First point.
 * @param right Second point.
 * @param out_value Receives the product.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_multiply(
    CNA_Point left,
    CNA_Point right,
    CNA_Point* out_value);

/**
 * @brief Divides two points component-wise.
 *
 * @param left Dividend point.
 * @param right Divisor point; neither component may be zero.
 * @param out_value Receives the quotient and remains unchanged on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a zero divisor, or
 * `CNA_RESULT_OVERFLOW` for an unrepresentable signed quotient.
 */
CNA_C_API CNA_Result cna_point_divide(
    CNA_Point left,
    CNA_Point right,
    CNA_Point* out_value);

/**
 * @brief Initializes a rectangle to the canonical empty value.
 *
 * @param out_value Receives the initialized rectangle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_init(CNA_Rectangle* out_value);

/**
 * @brief Initializes a rectangle from position and size.
 *
 * @param x Top-left X coordinate.
 * @param y Top-left Y coordinate.
 * @param width Width value.
 * @param height Height value.
 * @param out_value Receives the initialized rectangle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_init_xywh(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    CNA_Rectangle* out_value);

/**
 * @brief Gets the canonical empty rectangle.
 *
 * @param out_value Receives `{0, 0, 0, 0}`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_empty(CNA_Rectangle* out_value);

/**
 * @brief Gets the left edge.
 *
 * @param value Rectangle to query.
 * @param out_edge Receives the edge coordinate.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_left(CNA_Rectangle value, int32_t* out_edge);

/**
 * @brief Gets the right edge using unchecked 32-bit wraparound.
 *
 * @param value Rectangle to query.
 * @param out_edge Receives `x + width`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_right(CNA_Rectangle value, int32_t* out_edge);

/**
 * @brief Gets the top edge.
 *
 * @param value Rectangle to query.
 * @param out_edge Receives the edge coordinate.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_top(CNA_Rectangle value, int32_t* out_edge);

/**
 * @brief Gets the bottom edge using unchecked 32-bit wraparound.
 *
 * @param value Rectangle to query.
 * @param out_edge Receives `y + height`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_bottom(CNA_Rectangle value, int32_t* out_edge);

/**
 * @brief Gets the rectangle's top-left location.
 *
 * @param value Rectangle to query.
 * @param out_location Receives the location.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_location(
    CNA_Rectangle value,
    CNA_Point* out_location);

/**
 * @brief Replaces the rectangle's top-left location.
 *
 * @param value Rectangle to mutate.
 * @param location New location.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null rectangle.
 */
CNA_C_API CNA_Result cna_rectangle_set_location(
    CNA_Rectangle* value,
    CNA_Point location);

/**
 * @brief Gets the center point using the canonical integer division semantics.
 *
 * @param value Rectangle to query.
 * @param out_center Receives the center.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_center(
    CNA_Rectangle value,
    CNA_Point* out_center);

/**
 * @brief Tests whether all four rectangle fields are zero.
 *
 * @param value Rectangle to query.
 * @param out_is_empty Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_is_empty(
    CNA_Rectangle value,
    CNA_Bool* out_is_empty);

/**
 * @brief Tests whether integer coordinates lie within the rectangle's half-open bounds.
 *
 * @param value Containing rectangle.
 * @param x X coordinate to test.
 * @param y Y coordinate to test.
 * @param out_contains Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_contains_xy(
    CNA_Rectangle value,
    int32_t x,
    int32_t y,
    CNA_Bool* out_contains);

/**
 * @brief Tests whether a point lies within the rectangle's half-open bounds.
 *
 * @param value Containing rectangle.
 * @param point Point to test.
 * @param out_contains Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_contains_point(
    CNA_Rectangle value,
    CNA_Point point,
    CNA_Bool* out_contains);

/**
 * @brief Tests whether a rectangle lies fully within another rectangle.
 *
 * @param value Containing rectangle.
 * @param candidate Rectangle to test.
 * @param out_contains Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_contains_rectangle(
    CNA_Rectangle value,
    CNA_Rectangle candidate,
    CNA_Bool* out_contains);

/**
 * @brief Offsets a rectangle by a point with unchecked 32-bit wraparound.
 *
 * @param value Rectangle to mutate.
 * @param offset Offset to add.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null rectangle.
 */
CNA_C_API CNA_Result cna_rectangle_offset_point(
    CNA_Rectangle* value,
    CNA_Point offset);

/**
 * @brief Offsets a rectangle by separate coordinates with unchecked 32-bit wraparound.
 *
 * @param value Rectangle to mutate.
 * @param offset_x Horizontal offset.
 * @param offset_y Vertical offset.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null rectangle.
 */
CNA_C_API CNA_Result cna_rectangle_offset_xy(
    CNA_Rectangle* value,
    int32_t offset_x,
    int32_t offset_y);

/**
 * @brief Inflates a rectangle on each side with unchecked 32-bit wraparound.
 *
 * @param value Rectangle to mutate.
 * @param horizontal_value Horizontal expansion per side.
 * @param vertical_value Vertical expansion per side.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null rectangle.
 */
CNA_C_API CNA_Result cna_rectangle_inflate(
    CNA_Rectangle* value,
    int32_t horizontal_value,
    int32_t vertical_value);

/**
 * @brief Tests two rectangles for field equality.
 *
 * @param left First rectangle.
 * @param right Second rectangle.
 * @param out_equal Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_equals(
    CNA_Rectangle left,
    CNA_Rectangle right,
    CNA_Bool* out_equal);

/**
 * @brief Tests two rectangles for field inequality.
 *
 * @param left First rectangle.
 * @param right Second rectangle.
 * @param out_not_equal Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_not_equals(
    CNA_Rectangle left,
    CNA_Rectangle right,
    CNA_Bool* out_not_equal);

/**
 * @brief Computes the canonical Rectangle hash code.
 *
 * @param value Rectangle to hash.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_hash_code(
    CNA_Rectangle value,
    int32_t* out_hash);

/**
 * @brief Gets the UTF-8 byte count of the canonical Rectangle string.
 *
 * @param value Rectangle to format.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_get_string_size(
    CNA_Rectangle value,
    uint64_t* out_bytes);

/**
 * @brief Copies the canonical Rectangle string as UTF-8 bytes without a terminator.
 *
 * @param value Rectangle to format.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or an argument error.
 */
CNA_C_API CNA_Result cna_rectangle_copy_string(
    CNA_Rectangle value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Tests two rectangles for strict positive-area overlap.
 *
 * @param left First rectangle.
 * @param right Second rectangle.
 * @param out_intersects Receives the result.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_intersects(
    CNA_Rectangle left,
    CNA_Rectangle right,
    CNA_Bool* out_intersects);

/**
 * @brief Computes the intersection of two rectangles.
 *
 * @param left First rectangle.
 * @param right Second rectangle.
 * @param out_value Receives the intersection or the canonical empty rectangle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_intersect(
    CNA_Rectangle left,
    CNA_Rectangle right,
    CNA_Rectangle* out_value);

/**
 * @brief Computes the smallest rectangle containing both inputs.
 *
 * @param left First rectangle.
 * @param right Second rectangle.
 * @param out_value Receives the union.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_rectangle_union(
    CNA_Rectangle left,
    CNA_Rectangle right,
    CNA_Rectangle* out_value);

#ifdef __cplusplus
}
#endif

#endif
