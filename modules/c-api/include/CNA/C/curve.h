// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_CURVE_H
#define CNA_C_CURVE_H

#include "CNA/C/core.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-layout C representation of an XNA CurveKey. */
typedef struct CNA_CurveKey {
    /** @brief Position of the key on the curve. */
    float position;
    /** @brief Value of the key. */
    float value;
    /** @brief Tangent approaching the key. */
    float tangent_in;
    /** @brief Tangent leaving the key. */
    float tangent_out;
    /** @brief Smooth or stepped segment continuity. */
    CNA_CurveContinuity continuity;
} CNA_CurveKey;

/**
 * @brief Initializes a smooth key with zero tangents.
 *
 * @param position Position on the curve.
 * @param value Key value.
 * @param out_key Receives the key.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_init_position_value(
    float position,
    float value,
    CNA_CurveKey* out_key);

/**
 * @brief Initializes a smooth key with explicit tangents.
 *
 * @param position Position on the curve.
 * @param value Key value.
 * @param tangent_in Incoming tangent.
 * @param tangent_out Outgoing tangent.
 * @param out_key Receives the key.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_init_tangents(
    float position,
    float value,
    float tangent_in,
    float tangent_out,
    CNA_CurveKey* out_key);

/**
 * @brief Initializes a key with explicit tangents and continuity.
 *
 * @param position Position on the curve.
 * @param value Key value.
 * @param tangent_in Incoming tangent.
 * @param tangent_out Outgoing tangent.
 * @param continuity Segment continuity.
 * @param out_key Receives the key.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_init_full(
    float position,
    float value,
    float tangent_in,
    float tangent_out,
    CNA_CurveContinuity continuity,
    CNA_CurveKey* out_key);

/**
 * @brief Gets key continuity.
 *
 * @param key Source key.
 * @param out_continuity Receives the continuity.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_get_continuity(
    CNA_CurveKey key,
    CNA_CurveContinuity* out_continuity);

/**
 * @brief Sets key continuity.
 *
 * @param key Key to mutate.
 * @param continuity New continuity.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_set_continuity(
    CNA_CurveKey* key,
    CNA_CurveContinuity continuity);

/**
 * @brief Gets the key position.
 *
 * @param key Source key.
 * @param out_position Receives the position.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_get_position(CNA_CurveKey key, float* out_position);

/**
 * @brief Gets the incoming tangent.
 *
 * @param key Source key.
 * @param out_tangent Receives the tangent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_get_tangent_in(CNA_CurveKey key, float* out_tangent);

/**
 * @brief Sets the incoming tangent.
 *
 * @param key Key to mutate.
 * @param tangent New tangent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_set_tangent_in(CNA_CurveKey* key, float tangent);

/**
 * @brief Gets the outgoing tangent.
 *
 * @param key Source key.
 * @param out_tangent Receives the tangent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_get_tangent_out(CNA_CurveKey key, float* out_tangent);

/**
 * @brief Sets the outgoing tangent.
 *
 * @param key Key to mutate.
 * @param tangent New tangent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_set_tangent_out(CNA_CurveKey* key, float tangent);

/**
 * @brief Gets the key value.
 *
 * @param key Source key.
 * @param out_value Receives the value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_get_value(CNA_CurveKey key, float* out_value);

/**
 * @brief Sets the key value.
 *
 * @param key Key to mutate.
 * @param value New value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_set_value(CNA_CurveKey* key, float value);

/**
 * @brief Clones a curve key.
 *
 * @param key Source key.
 * @param out_key Receives the clone.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_clone(CNA_CurveKey key, CNA_CurveKey* out_key);

/**
 * @brief Compares two keys by position.
 *
 * @param value Source key.
 * @param other Key to compare.
 * @param out_comparison Receives a negative, zero or positive result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_compare_to(
    CNA_CurveKey value,
    CNA_CurveKey other,
    int32_t* out_comparison);

/**
 * @brief Tests curve-key equality.
 *
 * @param left First key.
 * @param right Second key.
 * @param out_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_equals(
    CNA_CurveKey left,
    CNA_CurveKey right,
    CNA_Bool* out_equal);

/**
 * @brief Tests curve-key inequality.
 *
 * @param left First key.
 * @param right Second key.
 * @param out_not_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_not_equals(
    CNA_CurveKey left,
    CNA_CurveKey right,
    CNA_Bool* out_not_equal);

/**
 * @brief Computes a curve-key hash.
 *
 * @param key Source key.
 * @param out_hash Receives the hash.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_get_hash_code(CNA_CurveKey key, int32_t* out_hash);

#ifdef __cplusplus
}
#endif

#endif
