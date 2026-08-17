// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_PACKED_VECTORS_H
#define CNA_C_PACKED_VECTORS_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity for a concrete packed-vector storage format. */
typedef uint32_t CNA_PackedVectorFormat;

/** @brief Selects the 8-bit normalized alpha format. */
#define CNA_PACKED_VECTOR_FORMAT_ALPHA8 UINT32_C(0)
/** @brief Selects the 16-bit normalized BGR 5:6:5 format. */
#define CNA_PACKED_VECTOR_FORMAT_BGR565 UINT32_C(1)
/** @brief Selects the 16-bit normalized BGRA 4:4:4:4 format. */
#define CNA_PACKED_VECTOR_FORMAT_BGRA4444 UINT32_C(2)
/** @brief Selects the 16-bit normalized BGRA 5:5:5:1 format. */
#define CNA_PACKED_VECTOR_FORMAT_BGRA5551 UINT32_C(3)
/** @brief Selects the four-channel unsigned-byte format. */
#define CNA_PACKED_VECTOR_FORMAT_BYTE4 UINT32_C(4)
/** @brief Selects the single-channel half-precision format. */
#define CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE UINT32_C(5)
/** @brief Selects the two-channel half-precision format. */
#define CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2 UINT32_C(6)
/** @brief Selects the four-channel half-precision format. */
#define CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4 UINT32_C(7)
/** @brief Selects the two-channel normalized signed-byte format. */
#define CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2 UINT32_C(8)
/** @brief Selects the four-channel normalized signed-byte format. */
#define CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4 UINT32_C(9)
/** @brief Selects the two-channel normalized signed-short format. */
#define CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2 UINT32_C(10)
/** @brief Selects the four-channel normalized signed-short format. */
#define CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4 UINT32_C(11)
/** @brief Selects the two-channel normalized unsigned-short format. */
#define CNA_PACKED_VECTOR_FORMAT_RG32 UINT32_C(12)
/** @brief Selects the normalized RGBA 10:10:10:2 format. */
#define CNA_PACKED_VECTOR_FORMAT_RGBA1010102 UINT32_C(13)
/** @brief Selects the four-channel normalized unsigned-short format. */
#define CNA_PACKED_VECTOR_FORMAT_RGBA64 UINT32_C(14)
/** @brief Selects the two-channel signed-short format. */
#define CNA_PACKED_VECTOR_FORMAT_SHORT2 UINT32_C(15)
/** @brief Selects the four-channel signed-short format. */
#define CNA_PACKED_VECTOR_FORMAT_SHORT4 UINT32_C(16)

/**
 * @brief Packs a Vector4 using a concrete CNA packed-vector format.
 *
 * Only components consumed by the selected native format are read. Non-finite components are
 * accepted by half-precision formats and rejected by integer formats.
 *
 * @param format Packed-vector format identity.
 * @param vector Vector components to pack.
 * @param out_packed Receives the packed bits in the low storage-width bits.
 * @return A CNA result code; failure does not overwrite @p out_packed.
 */
CNA_C_API CNA_Result cna_packed_vector_pack(
    CNA_PackedVectorFormat format,
    CNA_Vector4 vector,
    uint64_t* out_packed);

/**
 * @brief Expands packed bits to the Vector4 representation of a concrete format.
 *
 * @param format Packed-vector format identity.
 * @param packed Packed bits; bits above the selected format's storage width must be zero.
 * @param out_vector Receives the unpacked Vector4.
 * @return A CNA result code; failure does not overwrite @p out_vector.
 */
CNA_C_API CNA_Result cna_packed_vector_unpack(
    CNA_PackedVectorFormat format,
    uint64_t packed,
    CNA_Vector4* out_vector);

/**
 * @brief Tests two packed values for equality in a concrete format.
 *
 * @param format Packed-vector format identity.
 * @param left First packed value.
 * @param right Second packed value.
 * @param out_equal Receives the equality result.
 * @return A CNA result code; failure does not overwrite @p out_equal.
 */
CNA_C_API CNA_Result cna_packed_vector_equals(
    CNA_PackedVectorFormat format,
    uint64_t left,
    uint64_t right,
    CNA_Bool* out_equal);

/**
 * @brief Tests two packed values for inequality in a concrete format.
 *
 * @param format Packed-vector format identity.
 * @param left First packed value.
 * @param right Second packed value.
 * @param out_not_equal Receives the inequality result.
 * @return A CNA result code; failure does not overwrite @p out_not_equal.
 */
CNA_C_API CNA_Result cna_packed_vector_not_equals(
    CNA_PackedVectorFormat format,
    uint64_t left,
    uint64_t right,
    CNA_Bool* out_not_equal);

/**
 * @brief Converts a single-precision value to IEEE 754 binary16 bits.
 *
 * @param value Value to convert.
 * @param out_half Receives the half-precision bits.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_half_from_single(float value, uint16_t* out_half);

/**
 * @brief Converts an IEEE 754 binary32 bit pattern to binary16 bits.
 *
 * @param single_bits Signed integer containing the source binary32 bit pattern.
 * @param out_half Receives the half-precision bits.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_half_from_single_bits(int32_t single_bits, uint16_t* out_half);

/**
 * @brief Converts IEEE 754 binary16 bits to a single-precision value.
 *
 * @param half Half-precision bits.
 * @param out_value Receives the converted single-precision value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_half_to_single(uint16_t half, float* out_value);

#ifdef __cplusplus
}
#endif

#endif
