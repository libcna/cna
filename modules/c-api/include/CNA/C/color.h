// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_COLOR_H
#define CNA_C_COLOR_H

#include "CNA/C/core.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes a color from a unit-range Vector4.
 *
 * @param vector Source RGBA vector.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_vector4(CNA_Vector4 vector, CNA_Color* out_color);

/**
 * @brief Initializes an opaque color from a unit-range Vector3.
 *
 * @param vector Source RGB vector.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_vector3(CNA_Vector3 vector, CNA_Color* out_color);

/**
 * @brief Initializes an opaque color from unit-range floating-point channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_float_rgb(
    float r,
    float g,
    float b,
    CNA_Color* out_color);

/**
 * @brief Initializes a color from unit-range floating-point channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param a Alpha channel.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_float_rgba(
    float r,
    float g,
    float b,
    float a,
    CNA_Color* out_color);

/**
 * @brief Initializes an opaque color from clamped integer channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_int_rgb(
    int32_t r,
    int32_t g,
    int32_t b,
    CNA_Color* out_color);

/**
 * @brief Initializes a color from clamped integer channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param a Alpha channel.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_int_rgba(
    int32_t r,
    int32_t g,
    int32_t b,
    int32_t a,
    CNA_Color* out_color);

/**
 * @brief Initializes an opaque color from byte channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_bytes_rgb(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    CNA_Color* out_color);

/**
 * @brief Initializes a color from byte channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param a Alpha channel.
 * @param out_color Receives the color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_init_bytes_rgba(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a,
    CNA_Color* out_color);

/**
 * @brief Gets the packed AABBGGRR value.
 *
 * @param color Source color.
 * @param out_packed_value Receives the packed value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_get_packed_value(
    CNA_Color color,
    uint32_t* out_packed_value);

/**
 * @brief Replaces a color from a packed AABBGGRR value.
 *
 * @param color Color to mutate.
 * @param packed_value Packed value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_set_packed_value(
    CNA_Color* color,
    uint32_t packed_value);

/**
 * @brief Gets the exact byte count of the debug-display string.
 *
 * @param color Source color.
 * @param out_byte_count Receives the byte count, excluding any terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_get_debug_string_byte_count(
    CNA_Color color,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact debug-display string without a terminator.
 *
 * @param color Source color.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_color_copy_debug_string(
    CNA_Color color,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Tests color equality.
 *
 * @param left First color.
 * @param right Second color.
 * @param out_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_equals(
    CNA_Color left,
    CNA_Color right,
    CNA_Bool* out_equal);

/**
 * @brief Tests color inequality.
 *
 * @param left First color.
 * @param right Second color.
 * @param out_not_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_not_equals(
    CNA_Color left,
    CNA_Color right,
    CNA_Bool* out_not_equal);

/**
 * @brief Converts a color to a unit-range Vector3.
 *
 * @param color Source color.
 * @param out_vector Receives the RGB vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_to_vector3(CNA_Color color, CNA_Vector3* out_vector);

/**
 * @brief Converts a color to a unit-range Vector4.
 *
 * @param color Source color.
 * @param out_vector Receives the RGBA vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_to_vector4(CNA_Color color, CNA_Vector4* out_vector);

/**
 * @brief Computes a color hash code.
 *
 * @param color Source color.
 * @param out_hash Receives the hash code.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_get_hash_code(CNA_Color color, int32_t* out_hash);

/**
 * @brief Gets the exact byte count of the formatted color string.
 *
 * @param color Source color.
 * @param out_byte_count Receives the byte count, excluding any terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_get_string_byte_count(
    CNA_Color color,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact formatted color string without a terminator.
 *
 * @param color Source color.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_color_copy_string(
    CNA_Color color,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Linearly interpolates two colors with a clamped amount.
 *
 * @param value1 Source color.
 * @param value2 Destination color.
 * @param amount Interpolation amount.
 * @param out_color Receives the interpolated color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_lerp(
    CNA_Color value1,
    CNA_Color value2,
    float amount,
    CNA_Color* out_color);

/**
 * @brief Premultiplies a non-premultiplied Vector4 color.
 *
 * @param vector Non-premultiplied RGBA vector.
 * @param out_color Receives the premultiplied color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_from_non_premultiplied_vector4(
    CNA_Vector4 vector,
    CNA_Color* out_color);

/**
 * @brief Premultiplies non-premultiplied integer channels.
 *
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 * @param a Alpha channel.
 * @param out_color Receives the premultiplied color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_from_non_premultiplied_int(
    int32_t r,
    int32_t g,
    int32_t b,
    int32_t a,
    CNA_Color* out_color);

/**
 * @brief Multiplies every color channel by a scalar.
 *
 * @param color Source color.
 * @param scale Channel scale.
 * @param out_color Receives the multiplied color.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_multiply(
    CNA_Color color,
    float scale,
    CNA_Color* out_color);

/**
 * @brief Packs a Vector4 into an existing color using native packed-vector conversion behavior.
 *
 * @param color Color to mutate.
 * @param vector Source vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_color_pack_from_vector4(
    CNA_Color* color,
    CNA_Vector4 vector);

#ifdef __cplusplus
}
#endif

#endif
