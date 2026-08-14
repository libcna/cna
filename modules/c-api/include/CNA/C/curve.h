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

/** @brief Owned handle for an independent CurveKeyCollection. */
typedef CNA_Handle CNA_CurveKeyCollectionHandle;

/**
 * @brief Creates an empty mutable curve-key collection.
 *
 * @param out_collection Receives the owned handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_create(
    CNA_CurveKeyCollectionHandle* out_collection);

/**
 * @brief Destroys an owned curve-key collection.
 *
 * @param collection Collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_destroy(
    CNA_CurveKeyCollectionHandle collection);

/**
 * @brief Gets the number of keys.
 *
 * @param collection Collection handle.
 * @param out_count Receives the key count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_get_count(
    CNA_CurveKeyCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Gets whether the collection is read-only.
 *
 * @param collection Collection handle.
 * @param out_is_read_only Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_get_is_read_only(
    CNA_CurveKeyCollectionHandle collection,
    CNA_Bool* out_is_read_only);

/**
 * @brief Gets a key by index.
 *
 * @param collection Collection handle.
 * @param index Zero-based key index.
 * @param out_key Receives the key.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_get(
    CNA_CurveKeyCollectionHandle collection,
    int32_t index,
    CNA_CurveKey* out_key);

/**
 * @brief Replaces a key and preserves ascending position order.
 *
 * @param collection Collection handle.
 * @param index Zero-based key index.
 * @param key Replacement key.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_set(
    CNA_CurveKeyCollectionHandle collection,
    int32_t index,
    CNA_CurveKey key);

/**
 * @brief Adds a key in ascending position order.
 *
 * @param collection Collection handle.
 * @param key Key to add.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_add(
    CNA_CurveKeyCollectionHandle collection,
    CNA_CurveKey key);

/**
 * @brief Removes all keys.
 *
 * @param collection Collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_clear(
    CNA_CurveKeyCollectionHandle collection);

/**
 * @brief Creates an independent deep copy of a collection.
 *
 * @param collection Source collection handle.
 * @param out_collection Receives the owned clone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_clone(
    CNA_CurveKeyCollectionHandle collection,
    CNA_CurveKeyCollectionHandle* out_collection);

/**
 * @brief Tests whether a collection contains a key.
 *
 * @param collection Collection handle.
 * @param key Key to find.
 * @param out_contains Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_contains(
    CNA_CurveKeyCollectionHandle collection,
    CNA_CurveKey key,
    CNA_Bool* out_contains);

/**
 * @brief Copies all keys into a caller array at a destination index.
 *
 * @param collection Collection handle.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Total destination element capacity.
 * @param destination_index First destination index to write.
 * @param out_count Receives the number of collection keys.
 * @return A CNA result code; validation and capacity failures write no key.
 */
CNA_C_API CNA_Result cna_curve_key_collection_copy_to(
    CNA_CurveKeyCollectionHandle collection,
    CNA_CurveKey* destination,
    uint64_t capacity,
    int32_t destination_index,
    uint64_t* out_count);

/**
 * @brief Finds a key index or returns -1.
 *
 * @param collection Collection handle.
 * @param key Key to find.
 * @param out_index Receives the index or -1.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_index_of(
    CNA_CurveKeyCollectionHandle collection,
    CNA_CurveKey key,
    int32_t* out_index);

/**
 * @brief Removes the first equal key.
 *
 * @param collection Collection handle.
 * @param key Key to remove.
 * @param out_removed Receives whether a key was removed.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_remove(
    CNA_CurveKeyCollectionHandle collection,
    CNA_CurveKey key,
    CNA_Bool* out_removed);

/**
 * @brief Removes a key by index.
 *
 * @param collection Collection handle.
 * @param index Zero-based key index.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_key_collection_remove_at(
    CNA_CurveKeyCollectionHandle collection,
    int32_t index);

/** @brief Owned handle for a Curve. */
typedef CNA_Handle CNA_CurveHandle;

/**
 * @brief Creates an empty curve with constant pre-loop and post-loop behavior.
 *
 * @param out_curve Receives the owned handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_create(CNA_CurveHandle* out_curve);

/**
 * @brief Destroys an owned curve handle.
 *
 * @param curve Curve handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_destroy(CNA_CurveHandle curve);

/**
 * @brief Gets whether a curve contains at most one key.
 *
 * @param curve Curve handle.
 * @param out_is_constant Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_get_is_constant(
    CNA_CurveHandle curve,
    CNA_Bool* out_is_constant);

/**
 * @brief Creates a mutable collection-view handle for a curve's keys.
 *
 * The returned handle retains the curve and must be released with
 * cna_curve_key_collection_destroy. Mutations through the view affect the curve.
 *
 * @param curve Curve handle.
 * @param out_keys Receives the owned collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_get_keys(
    CNA_CurveHandle curve,
    CNA_CurveKeyCollectionHandle* out_keys);

/**
 * @brief Gets the behavior before the first key.
 *
 * @param curve Curve handle.
 * @param out_loop_type Receives the pre-loop behavior.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_get_pre_loop(
    CNA_CurveHandle curve,
    CNA_CurveLoopType* out_loop_type);

/**
 * @brief Sets the behavior before the first key.
 *
 * @param curve Curve handle.
 * @param loop_type New pre-loop behavior.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_set_pre_loop(
    CNA_CurveHandle curve,
    CNA_CurveLoopType loop_type);

/**
 * @brief Gets the behavior after the last key.
 *
 * @param curve Curve handle.
 * @param out_loop_type Receives the post-loop behavior.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_get_post_loop(
    CNA_CurveHandle curve,
    CNA_CurveLoopType* out_loop_type);

/**
 * @brief Sets the behavior after the last key.
 *
 * @param curve Curve handle.
 * @param loop_type New post-loop behavior.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_set_post_loop(
    CNA_CurveHandle curve,
    CNA_CurveLoopType loop_type);

/**
 * @brief Creates an independent deep copy of a curve.
 *
 * @param curve Source curve handle.
 * @param out_curve Receives the owned clone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_clone(
    CNA_CurveHandle curve,
    CNA_CurveHandle* out_curve);

/**
 * @brief Evaluates a curve at a position.
 *
 * @param curve Curve handle.
 * @param position Position to evaluate.
 * @param out_value Receives the evaluated value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_evaluate(
    CNA_CurveHandle curve,
    float position,
    float* out_value);

/**
 * @brief Computes matching incoming and outgoing tangents for every key.
 *
 * @param curve Curve handle.
 * @param tangent_type Tangent type for both directions.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_compute_tangents(
    CNA_CurveHandle curve,
    CNA_CurveTangent tangent_type);

/**
 * @brief Computes separate incoming and outgoing tangents for every key.
 *
 * @param curve Curve handle.
 * @param tangent_in_type Incoming tangent type.
 * @param tangent_out_type Outgoing tangent type.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_compute_tangents_in_out(
    CNA_CurveHandle curve,
    CNA_CurveTangent tangent_in_type,
    CNA_CurveTangent tangent_out_type);

/**
 * @brief Computes matching incoming and outgoing tangents for one key.
 *
 * @param curve Curve handle.
 * @param key_index Zero-based key index.
 * @param tangent_type Tangent type for both directions.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_compute_tangent(
    CNA_CurveHandle curve,
    int32_t key_index,
    CNA_CurveTangent tangent_type);

/**
 * @brief Computes separate incoming and outgoing tangents for one key.
 *
 * @param curve Curve handle.
 * @param key_index Zero-based key index.
 * @param tangent_in_type Incoming tangent type.
 * @param tangent_out_type Outgoing tangent type.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_curve_compute_tangent_in_out(
    CNA_CurveHandle curve,
    int32_t key_index,
    CNA_CurveTangent tangent_in_type,
    CNA_CurveTangent tangent_out_type);

#ifdef __cplusplus
}
#endif

#endif
