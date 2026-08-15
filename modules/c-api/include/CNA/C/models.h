// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MODELS_H
#define CNA_C_MODELS_H

#include "CNA/C/core.h"
#include "CNA/C/effects.h"
#include "CNA/C/index_resources.h"
#include "CNA/C/math_values.h"
#include "CNA/C/vertex_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned stable handle for a model bone. */
typedef CNA_Handle CNA_ModelBoneHandle;

/** @brief Owned live view of a model-bone collection. */
typedef CNA_Handle CNA_ModelBoneCollectionHandle;

/** @brief Owned stable handle for a model mesh part. */
typedef CNA_Handle CNA_ModelMeshPartHandle;

/** @brief Owned stable snapshot or live view of model mesh parts. */
typedef CNA_Handle CNA_ModelMeshPartCollectionHandle;

/** @brief C-owned opaque tag value associated with a model mesh part. */
typedef uint64_t CNA_ModelMeshPartTag;

/** @brief Owned stable handle for a model mesh. */
typedef CNA_Handle CNA_ModelMeshHandle;

/** @brief Owned snapshot or live view of model meshes. */
typedef CNA_Handle CNA_ModelMeshCollectionHandle;

/** @brief Owned live view of effects associated with a model mesh. */
typedef CNA_Handle CNA_ModelEffectCollectionHandle;

/** @brief C-owned opaque tag value associated with a model mesh. */
typedef uint64_t CNA_ModelMeshTag;

/** @brief Owned stable handle for a top-level model. */
typedef CNA_Handle CNA_ModelHandle;

/** @brief C-owned opaque tag value associated with a model. */
typedef uint64_t CNA_ModelTag;

/**
 * @brief Releases caller state retained as a model-owned resource bundle.
 * @param context Opaque caller context supplied during registration.
 */
typedef void (*CNA_ModelOwnedResourcesReleaseCallback)(void* context);

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

/**
 * @brief Creates an owned empty model mesh part.
 * @param out_part Receives the owned part handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_create_default(
    CNA_ModelMeshPartHandle* out_part);

/**
 * @brief Creates an owned model mesh part from optional retained buffers and scalar state.
 * @param vertex_buffer VertexBuffer handle or `CNA_INVALID_HANDLE`.
 * @param index_buffer IndexBuffer handle or `CNA_INVALID_HANDLE`.
 * @param num_vertices Native signed vertex count preserved verbatim.
 * @param primitive_count Native signed primitive count preserved verbatim.
 * @param start_index Native signed starting index preserved verbatim.
 * @param vertex_offset Native signed vertex offset preserved verbatim.
 * @param out_part Receives the owned part handle.
 * @return A CNA result code; non-null resources must belong to one graphics device.
 */
CNA_C_API CNA_Result cna_model_mesh_part_create(
    CNA_VertexBufferHandle vertex_buffer,
    CNA_IndexBufferHandle index_buffer,
    int32_t num_vertices,
    int32_t primitive_count,
    int32_t start_index,
    int32_t vertex_offset,
    CNA_ModelMeshPartHandle* out_part);

/** @brief Releases an owned model-mesh-part handle. */
CNA_C_API CNA_Result cna_model_mesh_part_destroy(CNA_ModelMeshPartHandle part);

/** @brief Gets the signed model-mesh-part vertex count. */
CNA_C_API CNA_Result cna_model_mesh_part_get_num_vertices(
    CNA_ModelMeshPartHandle part,
    int32_t* out_value);

/** @brief Sets the signed model-mesh-part vertex count. */
CNA_C_API CNA_Result cna_model_mesh_part_set_num_vertices(
    CNA_ModelMeshPartHandle part,
    int32_t value);

/** @brief Gets the signed model-mesh-part primitive count. */
CNA_C_API CNA_Result cna_model_mesh_part_get_primitive_count(
    CNA_ModelMeshPartHandle part,
    int32_t* out_value);

/** @brief Sets the signed model-mesh-part primitive count. */
CNA_C_API CNA_Result cna_model_mesh_part_set_primitive_count(
    CNA_ModelMeshPartHandle part,
    int32_t value);

/** @brief Gets the signed model-mesh-part starting index. */
CNA_C_API CNA_Result cna_model_mesh_part_get_start_index(
    CNA_ModelMeshPartHandle part,
    int32_t* out_value);

/** @brief Sets the signed model-mesh-part starting index. */
CNA_C_API CNA_Result cna_model_mesh_part_set_start_index(
    CNA_ModelMeshPartHandle part,
    int32_t value);

/** @brief Gets the signed model-mesh-part vertex offset. */
CNA_C_API CNA_Result cna_model_mesh_part_get_vertex_offset(
    CNA_ModelMeshPartHandle part,
    int32_t* out_value);

/** @brief Sets the signed model-mesh-part vertex offset. */
CNA_C_API CNA_Result cna_model_mesh_part_set_vertex_offset(
    CNA_ModelMeshPartHandle part,
    int32_t value);

/**
 * @brief Gets the retained material effect handle.
 * @param part Model-mesh-part handle.
 * @param out_has_effect Receives whether an effect is assigned.
 * @param out_effect Receives the effect handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_get_effect(
    CNA_ModelMeshPartHandle part,
    CNA_Bool* out_has_effect,
    CNA_EffectHandle* out_effect);

/** @brief Assigns a same-device Effect, or clears it with the invalid handle. */
CNA_C_API CNA_Result cna_model_mesh_part_set_effect(
    CNA_ModelMeshPartHandle part,
    CNA_EffectHandle effect);

/**
 * @brief Gets the retained vertex-buffer handle.
 * @param part Model-mesh-part handle.
 * @param out_has_buffer Receives whether a buffer is assigned.
 * @param out_buffer Receives the buffer handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_get_vertex_buffer(
    CNA_ModelMeshPartHandle part,
    CNA_Bool* out_has_buffer,
    CNA_VertexBufferHandle* out_buffer);

/** @brief Assigns a same-device VertexBuffer, or clears it with the invalid handle. */
CNA_C_API CNA_Result cna_model_mesh_part_set_vertex_buffer(
    CNA_ModelMeshPartHandle part,
    CNA_VertexBufferHandle vertex_buffer);

/**
 * @brief Gets the retained index-buffer handle.
 * @param part Model-mesh-part handle.
 * @param out_has_buffer Receives whether a buffer is assigned.
 * @param out_buffer Receives the buffer handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_get_index_buffer(
    CNA_ModelMeshPartHandle part,
    CNA_Bool* out_has_buffer,
    CNA_IndexBufferHandle* out_buffer);

/** @brief Assigns a same-device IndexBuffer, or clears it with the invalid handle. */
CNA_C_API CNA_Result cna_model_mesh_part_set_index_buffer(
    CNA_ModelMeshPartHandle part,
    CNA_IndexBufferHandle index_buffer);

/** @brief Gets the C-owned opaque model-mesh-part tag. */
CNA_C_API CNA_Result cna_model_mesh_part_get_tag(
    CNA_ModelMeshPartHandle part,
    CNA_ModelMeshPartTag* out_tag);

/** @brief Sets the C-owned opaque model-mesh-part tag. */
CNA_C_API CNA_Result cna_model_mesh_part_set_tag(
    CNA_ModelMeshPartHandle part,
    CNA_ModelMeshPartTag tag);

/**
 * @brief Creates an owned collection snapshot retaining the supplied parts.
 * @param parts Caller-owned array of valid part handles, or null only for zero count.
 * @param part_count Number of handles in @p parts.
 * @param out_collection Receives the collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_collection_create(
    const CNA_ModelMeshPartHandle* parts,
    uint64_t part_count,
    CNA_ModelMeshPartCollectionHandle* out_collection);

/** @brief Releases an owned model-mesh-part collection handle. */
CNA_C_API CNA_Result cna_model_mesh_part_collection_destroy(
    CNA_ModelMeshPartCollectionHandle collection);

/** @brief Gets the number of parts in a collection. */
CNA_C_API CNA_Result cna_model_mesh_part_collection_get_count(
    CNA_ModelMeshPartCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Gets an owned stable part view at an index.
 * @param collection Model-mesh-part collection handle.
 * @param index Zero-based part index.
 * @param out_part Receives the owned part view.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_collection_get_at(
    CNA_ModelMeshPartCollectionHandle collection,
    uint64_t index,
    CNA_ModelMeshPartHandle* out_part);

/**
 * @brief Creates an unnamed model mesh from retained parts.
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param parts Caller-owned part-handle array, or null only for zero count.
 * @param part_count Number of handles in @p parts.
 * @param out_mesh Receives the owned mesh handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_create(
    CNA_Handle graphics_device,
    const CNA_ModelMeshPartHandle* parts,
    uint64_t part_count,
    CNA_ModelMeshHandle* out_mesh);

/**
 * @brief Creates a named model mesh from retained parts and copied UTF-8.
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param name Mesh name copied by the call.
 * @param parts Caller-owned part-handle array, or null only for zero count.
 * @param part_count Number of handles in @p parts.
 * @param out_mesh Receives the owned mesh handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_create_named(
    CNA_Handle graphics_device,
    CNA_StringView name,
    const CNA_ModelMeshPartHandle* parts,
    uint64_t part_count,
    CNA_ModelMeshHandle* out_mesh);

/**
 * @brief Releases an owned model-mesh handle.
 * @param mesh Model-mesh handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_destroy(CNA_ModelMeshHandle mesh);

/**
 * @brief Gets the model-mesh bounding sphere.
 * @param mesh Model-mesh handle.
 * @param out_value Receives the copied sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_get_bounding_sphere(
    CNA_ModelMeshHandle mesh,
    CNA_BoundingSphere* out_value);

/**
 * @brief Sets the model-mesh bounding sphere.
 * @param mesh Model-mesh handle.
 * @param value Sphere copied by the call.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_set_bounding_sphere(
    CNA_ModelMeshHandle mesh,
    CNA_BoundingSphere value);

/**
 * @brief Gets an owned live view of the mesh-part collection.
 * @param mesh Model-mesh handle.
 * @param out_parts Receives the collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_get_mesh_parts(
    CNA_ModelMeshHandle mesh,
    CNA_ModelMeshPartCollectionHandle* out_parts);

/**
 * @brief Gets an owned live view of the mesh-effect collection.
 * @param mesh Model-mesh handle.
 * @param out_effects Receives the collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_get_effects(
    CNA_ModelMeshHandle mesh,
    CNA_ModelEffectCollectionHandle* out_effects);

/**
 * @brief Gets the exact UTF-8 mesh-name byte count.
 * @param mesh Model-mesh handle.
 * @param out_byte_count Receives the exact byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_get_name_byte_count(
    CNA_ModelMeshHandle mesh,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact UTF-8 mesh name without a terminator.
 * @param mesh Model-mesh handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_mesh_copy_name(
    CNA_ModelMeshHandle mesh,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets an owned optional parent-bone view.
 * @param mesh Model-mesh handle.
 * @param out_has_parent Receives whether a parent is assigned.
 * @param out_parent Receives the parent view, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_get_parent_bone(
    CNA_ModelMeshHandle mesh,
    CNA_Bool* out_has_parent,
    CNA_ModelBoneHandle* out_parent);

/**
 * @brief Assigns a retained parent bone, or clears it with the invalid handle.
 * @param mesh Model-mesh handle.
 * @param parent Parent-bone handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_set_parent_bone(
    CNA_ModelMeshHandle mesh,
    CNA_ModelBoneHandle parent);

/**
 * @brief Gets the C-owned opaque mesh tag.
 * @param mesh Model-mesh handle.
 * @param out_tag Receives the tag.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_get_tag(
    CNA_ModelMeshHandle mesh,
    CNA_ModelMeshTag* out_tag);

/**
 * @brief Sets the C-owned opaque mesh tag.
 * @param mesh Model-mesh handle.
 * @param tag Opaque tag value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_set_tag(
    CNA_ModelMeshHandle mesh,
    CNA_ModelMeshTag tag);

/**
 * @brief Draws all drawable parts through the native ModelMesh operation.
 * @param mesh Model-mesh handle.
 * @return A CNA result code, including an explicit renderer limitation.
 */
CNA_C_API CNA_Result cna_model_mesh_draw(CNA_ModelMeshHandle mesh);

/**
 * @brief Creates an owned collection snapshot retaining supplied meshes.
 * @param meshes Caller-owned mesh-handle array, or null only for zero count.
 * @param mesh_count Number of handles in @p meshes.
 * @param out_collection Receives the collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_collection_create(
    const CNA_ModelMeshHandle* meshes,
    uint64_t mesh_count,
    CNA_ModelMeshCollectionHandle* out_collection);

/**
 * @brief Releases an owned model-mesh collection handle.
 * @param collection Model-mesh collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_collection_destroy(
    CNA_ModelMeshCollectionHandle collection);

/**
 * @brief Gets the mesh count.
 * @param collection Model-mesh collection handle.
 * @param out_count Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_collection_get_count(
    CNA_ModelMeshCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Gets an owned stable mesh view at an index.
 * @param collection Model-mesh collection handle.
 * @param index Zero-based mesh index.
 * @param out_mesh Receives the owned mesh view.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_collection_get_at(
    CNA_ModelMeshCollectionHandle collection,
    uint64_t index,
    CNA_ModelMeshHandle* out_mesh);

/**
 * @brief Finds the first mesh with an exact UTF-8 name.
 * @param collection Model-mesh collection handle.
 * @param name Exact UTF-8 name.
 * @param out_found Receives whether a mesh was found.
 * @param out_mesh Receives the owned mesh view, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_collection_find(
    CNA_ModelMeshCollectionHandle collection,
    CNA_StringView name,
    CNA_Bool* out_found,
    CNA_ModelMeshHandle* out_mesh);

/**
 * @brief Tests whether a collection contains the exact mesh object.
 * @param collection Model-mesh collection handle.
 * @param mesh Mesh handle to test.
 * @param out_contains Receives the identity result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_collection_contains(
    CNA_ModelMeshCollectionHandle collection,
    CNA_ModelMeshHandle mesh,
    CNA_Bool* out_contains);

/**
 * @brief Releases an owned model-effect collection view.
 * @param collection Model-effect collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_effect_collection_destroy(
    CNA_ModelEffectCollectionHandle collection);

/**
 * @brief Gets the current effect count.
 * @param collection Model-effect collection handle.
 * @param out_count Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_effect_collection_get_count(
    CNA_ModelEffectCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Gets the retained effect handle at an index.
 * @param collection Model-effect collection handle.
 * @param index Zero-based effect index.
 * @param out_effect Receives the effect handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_effect_collection_get_at(
    CNA_ModelEffectCollectionHandle collection,
    uint64_t index,
    CNA_EffectHandle* out_effect);

/**
 * @brief Tests whether the collection contains an exact effect object.
 * @param collection Model-effect collection handle.
 * @param effect Effect handle to test.
 * @param out_contains Receives the identity result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_effect_collection_contains(
    CNA_ModelEffectCollectionHandle collection,
    CNA_EffectHandle effect,
    CNA_Bool* out_contains);

/**
 * @brief Adds and retains an effect using native duplicate-preserving semantics.
 * @param collection Model-effect collection handle.
 * @param effect Same-device effect handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_effect_collection_add(
    CNA_ModelEffectCollectionHandle collection,
    CNA_EffectHandle effect);

/**
 * @brief Removes the first matching effect if present.
 * @param collection Model-effect collection handle.
 * @param effect Effect handle to remove.
 * @return A CNA result code; absence is successful.
 */
CNA_C_API CNA_Result cna_model_effect_collection_remove(
    CNA_ModelEffectCollectionHandle collection,
    CNA_EffectHandle effect);

/**
 * @brief Creates an owned empty model independent of a graphics device.
 * @param out_model Receives the owned model handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_create_default(CNA_ModelHandle* out_model);

/**
 * @brief Creates a model retaining the supplied bones and same-device meshes.
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param bones Caller-owned bone-handle array, or null only for zero count.
 * @param bone_count Number of handles in @p bones.
 * @param meshes Caller-owned mesh-handle array, or null only for zero count.
 * @param mesh_count Number of handles in @p meshes.
 * @param out_model Receives the owned model handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_create(
    CNA_Handle graphics_device,
    const CNA_ModelBoneHandle* bones,
    uint64_t bone_count,
    const CNA_ModelMeshHandle* meshes,
    uint64_t mesh_count,
    CNA_ModelHandle* out_model);

/**
 * @brief Creates a model with explicit mesh parents and root selection.
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param bones Caller-owned bone-handle array, or null only for zero count.
 * @param bone_count Number of handles in @p bones.
 * @param meshes Caller-owned mesh-handle array, or null only for zero count.
 * @param mesh_count Number of handles in @p meshes.
 * @param mesh_parents Optional parent handles; invalid entries represent null parents.
 * @param mesh_parent_count Zero or exactly @p mesh_count.
 * @param root_bone_index Root index when bones are non-empty.
 * @param out_model Receives the owned model handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_create_with_parents(
    CNA_Handle graphics_device,
    const CNA_ModelBoneHandle* bones,
    uint64_t bone_count,
    const CNA_ModelMeshHandle* meshes,
    uint64_t mesh_count,
    const CNA_ModelBoneHandle* mesh_parents,
    uint64_t mesh_parent_count,
    uint64_t root_bone_index,
    CNA_ModelHandle* out_model);

/**
 * @brief Releases an owned model handle.
 * @param model Model handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_destroy(CNA_ModelHandle model);

/**
 * @brief Gets an owned immutable view of the model bones.
 * @param model Model handle.
 * @param out_bones Receives the collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_bones(
    CNA_ModelHandle model,
    CNA_ModelBoneCollectionHandle* out_bones);

/**
 * @brief Gets an owned immutable view of the model meshes.
 * @param model Model handle.
 * @param out_meshes Receives the collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_meshes(
    CNA_ModelHandle model,
    CNA_ModelMeshCollectionHandle* out_meshes);

/**
 * @brief Gets an owned optional root-bone view.
 * @param model Model handle.
 * @param out_has_root Receives whether a root exists.
 * @param out_root Receives the root handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_root(
    CNA_ModelHandle model,
    CNA_Bool* out_has_root,
    CNA_ModelBoneHandle* out_root);

/**
 * @brief Gets the C-owned opaque model tag.
 * @param model Model handle.
 * @param out_tag Receives the tag.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_tag(
    CNA_ModelHandle model,
    CNA_ModelTag* out_tag);

/**
 * @brief Sets the C-owned opaque model tag.
 * @param model Model handle.
 * @param tag Opaque tag value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_set_tag(
    CNA_ModelHandle model,
    CNA_ModelTag tag);

/**
 * @brief Replaces the model-owned C resource bundle.
 * @param model Model handle.
 * @param context Opaque context retained until replacement or model destruction.
 * @param release Required release callback when @p context is non-null; null clears the bundle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_set_owned_resources(
    CNA_ModelHandle model,
    void* context,
    CNA_ModelOwnedResourcesReleaseCallback release);

/**
 * @brief Gets the number of bone transforms used by bulk operations.
 * @param model Model handle.
 * @param out_count Receives the bone count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_bone_transform_count(
    CNA_ModelHandle model,
    uint64_t* out_count);

/**
 * @brief Copies absolute bone transforms atomically.
 * @param model Model handle.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity in matrices.
 * @param out_count Receives the required matrix count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_copy_absolute_bone_transforms(
    CNA_ModelHandle model,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Copies local transforms from a caller array into all model bones.
 * @param model Model handle.
 * @param source Source matrix array.
 * @param count Number of matrices; must cover every model bone.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_set_bone_transforms(
    CNA_ModelHandle model,
    const CNA_Matrix* source,
    uint64_t count);

/**
 * @brief Copies local bone transforms atomically.
 * @param model Model handle.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity in matrices.
 * @param out_count Receives the required matrix count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_copy_bone_transforms(
    CNA_ModelHandle model,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Draws every model mesh after applying world, view and projection matrices.
 * @param model Model handle.
 * @param world World transform.
 * @param view View transform.
 * @param projection Projection transform.
 * @return A CNA result code, including explicit renderer limitations.
 */
CNA_C_API CNA_Result cna_model_draw(
    CNA_ModelHandle model,
    CNA_Matrix world,
    CNA_Matrix view,
    CNA_Matrix projection);

#ifdef __cplusplus
}
#endif

#endif
