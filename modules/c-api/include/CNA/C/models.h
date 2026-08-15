// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MODELS_H
#define CNA_C_MODELS_H

#include "CNA/C/core.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned stable handle for a model bone. */
typedef CNA_Handle CNA_ModelBoneHandle;

/** @brief Owned live view of a model-bone collection. */
typedef CNA_Handle CNA_ModelBoneCollectionHandle;

/**
 * @brief Creates an owned default model bone.
 * @param out_bone Receives the owned bone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_create_default(CNA_ModelBoneHandle* out_bone);

/**
 * @brief Creates an owned model bone with an index and copied UTF-8 name.
 * @param index Bone index preserved verbatim.
 * @param name Bone name copied by the call.
 * @param out_bone Receives the owned bone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_create(
    int32_t index,
    CNA_StringView name,
    CNA_ModelBoneHandle* out_bone);

/**
 * @brief Releases an owned model-bone handle.
 * @param bone Model-bone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_destroy(CNA_ModelBoneHandle bone);

/** @brief Gets the exact UTF-8 model-bone name byte count. */
CNA_C_API CNA_Result cna_model_bone_get_name_byte_count(
    CNA_ModelBoneHandle bone,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact UTF-8 model-bone name without a terminator.
 * @param bone Model-bone handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_bone_copy_name(
    CNA_ModelBoneHandle bone,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the model-bone index. */
CNA_C_API CNA_Result cna_model_bone_get_index(
    CNA_ModelBoneHandle bone,
    int32_t* out_index);

/** @brief Gets the model-bone transform relative to its parent. */
CNA_C_API CNA_Result cna_model_bone_get_transform(
    CNA_ModelBoneHandle bone,
    CNA_Matrix* out_transform);

/** @brief Sets the model-bone transform relative to its parent. */
CNA_C_API CNA_Result cna_model_bone_set_transform(
    CNA_ModelBoneHandle bone,
    CNA_Matrix transform);

/**
 * @brief Gets an owned stable view of the current parent bone.
 * @param bone Model-bone handle.
 * @param out_has_parent Receives whether a live parent exists.
 * @param out_parent Receives an owned parent view, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_get_parent(
    CNA_ModelBoneHandle bone,
    CNA_Bool* out_has_parent,
    CNA_ModelBoneHandle* out_parent);

/**
 * @brief Gets an owned live view of the child-bone collection.
 * @param bone Model-bone handle.
 * @param out_children Receives the collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_get_children(
    CNA_ModelBoneHandle bone,
    CNA_ModelBoneCollectionHandle* out_children);

/**
 * @brief Adds a child and retains it for the parent hierarchy.
 *
 * Self-parenting and ancestor cycles are rejected at the C safety boundary.
 *
 * @param bone Parent model-bone handle.
 * @param child Child model-bone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_add_child(
    CNA_ModelBoneHandle bone,
    CNA_ModelBoneHandle child);

/**
 * @brief Creates an owned empty model-bone collection.
 * @param out_collection Receives the collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_collection_create(
    CNA_ModelBoneCollectionHandle* out_collection);

/** @brief Releases an owned model-bone collection handle. */
CNA_C_API CNA_Result cna_model_bone_collection_destroy(
    CNA_ModelBoneCollectionHandle collection);

/** @brief Gets the current number of bones in a collection. */
CNA_C_API CNA_Result cna_model_bone_collection_get_count(
    CNA_ModelBoneCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Gets an owned stable bone view at an index.
 * @param collection Model-bone collection handle.
 * @param index Zero-based bone index within the collection.
 * @param out_bone Receives the owned bone view.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_collection_get_at(
    CNA_ModelBoneCollectionHandle collection,
    uint64_t index,
    CNA_ModelBoneHandle* out_bone);

/**
 * @brief Finds the first bone with an exact UTF-8 name.
 * @param collection Model-bone collection handle.
 * @param name Exact UTF-8 name.
 * @param out_found Receives whether a bone was found.
 * @param out_bone Receives an owned bone view, or the invalid handle.
 * @return A CNA result code; absence is a successful false result.
 */
CNA_C_API CNA_Result cna_model_bone_collection_find(
    CNA_ModelBoneCollectionHandle collection,
    CNA_StringView name,
    CNA_Bool* out_found,
    CNA_ModelBoneHandle* out_bone);

/**
 * @brief Tests whether a collection contains the exact bone object.
 * @param collection Model-bone collection handle.
 * @param bone Model-bone handle to test.
 * @param out_contains Receives true when the exact object is present.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_bone_collection_contains(
    CNA_ModelBoneCollectionHandle collection,
    CNA_ModelBoneHandle bone,
    CNA_Bool* out_contains);

#ifdef __cplusplus
}
#endif

#endif
