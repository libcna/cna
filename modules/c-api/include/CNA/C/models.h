// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MODELS_H
#define CNA_C_MODELS_H

#include "CNA/C/content_readers.h"
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

/** @brief Owned stable handle for CNA morph-target data. */
typedef CNA_Handle CNA_MorphTargetDataEXTHandle;

/** @brief Borrowed float arrays describing one morph-weight keyframe. */
typedef struct CNA_MorphWeightKeyframeEXTDescriptor {
    /** @brief Finite keyframe time in seconds representable by a native TimeSpan. */
    double time_seconds;
    /** @brief Weight values borrowed for the call. */
    const float* weights;
    /** @brief Number of weight values. */
    uint64_t weight_count;
    /** @brief Optional incoming tangent values borrowed for the call. */
    const float* in_tangents;
    /** @brief Number of incoming tangent values. */
    uint64_t in_tangent_count;
    /** @brief Optional outgoing tangent values borrowed for the call. */
    const float* out_tangents;
    /** @brief Number of outgoing tangent values. */
    uint64_t out_tangent_count;
} CNA_MorphWeightKeyframeEXTDescriptor;

/** @brief Borrowed keyframe array and interpolation flags describing a morph-weight track. */
typedef struct CNA_MorphWeightTrackEXTDescriptor {
    /** @brief Keyframes borrowed for the call. */
    const CNA_MorphWeightKeyframeEXTDescriptor* keyframes;
    /** @brief Number of keyframes. */
    uint64_t keyframe_count;
    /** @brief Whether evaluation holds the lower keyframe value. */
    CNA_Bool step_interpolation;
    /** @brief Whether evaluation uses available Hermite tangents. */
    CNA_Bool cubic_spline;
} CNA_MorphWeightTrackEXTDescriptor;

/** @brief Borrowed position and optional normal delta arrays for one morph target. */
typedef struct CNA_MorphTargetDeltaEXTDescriptor {
    /** @brief Position deltas borrowed for the call. */
    const CNA_Vector3* position_deltas;
    /** @brief Number of position deltas. */
    uint64_t position_delta_count;
    /** @brief Optional normal deltas borrowed for the call. */
    const CNA_Vector3* normal_deltas;
    /** @brief Number of normal deltas. */
    uint64_t normal_delta_count;
} CNA_MorphTargetDeltaEXTDescriptor;

/**
 * @brief Fixed-width identity for which index space a clip's bone indices are in.
 *
 * CNA extension. The two spaces are deliberately distinct and must never be interchanged: a
 * joint's palette slot has nothing to do with its position in the scene, and a rigid scene node
 * has no palette slot at all. The values match
 * `Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT`.
 */
typedef uint32_t CNA_ClipTargetSpaceEXT;
/** @brief Track bone indices are skinning-palette slots. */
#define CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT UINT32_C(0)
/** @brief Track bone indices are scene-node indices. */
#define CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT UINT32_C(1)
/** @brief Highest defined clip-target-space identity. */
#define CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT

/**
 * @brief Complete copied construction state for morph-target data.
 */
typedef struct CNA_MorphTargetDataEXTDescriptor {
    /** @brief Base-pose vertex bytes borrowed for the call. */
    const uint8_t* base_vertex_bytes;
    /** @brief Number of base-pose bytes. */
    uint64_t base_vertex_byte_count;
    /** @brief Byte stride of one base-pose vertex. */
    int32_t stride;
    /** @brief Morph-target delta descriptors borrowed for the call. */
    const CNA_MorphTargetDeltaEXTDescriptor* targets;
    /** @brief Number of morph targets. */
    uint64_t target_count;
    /** @brief Current weights borrowed for the call. */
    const float* weights;
    /** @brief Number of current weights. */
    uint64_t weight_count;
    /** @brief Optional animation track copied by the call. */
    CNA_MorphWeightTrackEXTDescriptor weight_track;
} CNA_MorphTargetDataEXTDescriptor;

/** @brief Owned stable handle for a CNA GPU-skinned model extension. */
typedef CNA_Handle CNA_SkinnedModelEXTHandle;

/** @brief Fixed-layout bone animation keyframe. */
typedef struct CNA_KeyframeEXT {
    /** @brief Finite keyframe time in seconds representable by a native TimeSpan. */
    double time_seconds;
    /** @brief Bone-local translation. */
    CNA_Vector3 translation;
    /** @brief Bone-local rotation quaternion. */
    CNA_Quaternion rotation;
    /** @brief Bone-local scale. */
    CNA_Vector3 scale;
} CNA_KeyframeEXT;

/** @brief Borrowed keyframe array driving one skeleton bone. */
typedef struct CNA_BoneTrackEXTDescriptor {
    /** @brief Signed bone index; out-of-range tracks retain native skip behavior. */
    int32_t bone_index;
    /** @brief Reserved padding; initialize to zero. */
    uint32_t reserved;
    /** @brief Keyframes borrowed for the call. */
    const CNA_KeyframeEXT* keyframes;
    /** @brief Number of keyframes. */
    uint64_t keyframe_count;
} CNA_BoneTrackEXTDescriptor;

/** @brief Borrowed bone-track array and finite clip duration. */
typedef struct CNA_AnimationClipEXTDescriptor {
    /** @brief Finite clip duration in seconds. */
    double duration_seconds;
    /** @brief Bone tracks borrowed for the call. */
    const CNA_BoneTrackEXTDescriptor* tracks;
    /** @brief Number of bone tracks. */
    uint64_t track_count;
} CNA_AnimationClipEXTDescriptor;

/** @brief Copied UTF-8 clip name paired with a borrowed clip descriptor. */
typedef struct CNA_NamedAnimationClipEXTDescriptor {
    /** @brief Exact UTF-8 clip name copied by the call. */
    CNA_StringView name;
    /** @brief Clip state copied by the call. */
    CNA_AnimationClipEXTDescriptor clip;
} CNA_NamedAnimationClipEXTDescriptor;

/** @brief Complete copied skeleton and clip state for skinned-model construction. */
typedef struct CNA_SkinnedModelEXTDescriptor {
    /** @brief Non-negative skeleton bone count. */
    int32_t bone_count;
    /** @brief Reserved padding; initialize to zero. */
    uint32_t reserved;
    /** @brief Parent indices borrowed for the call; one per bone. */
    const int32_t* parent_bone_indices;
    /** @brief Local bind-pose matrices borrowed for the call; one per bone. */
    const CNA_Matrix* bind_pose_local;
    /** @brief Inverse global bind-pose matrices borrowed for the call; one per bone. */
    const CNA_Matrix* inverse_bind_pose_global;
    /** @brief Named animation clips borrowed for the call. */
    const CNA_NamedAnimationClipEXTDescriptor* clips;
    /** @brief Number of named clips. */
    uint64_t clip_count;
} CNA_SkinnedModelEXTDescriptor;

/** @brief Owned stable handle for copied skeletal animation data. */
typedef CNA_Handle CNA_SkinningDataHandle;

/** @brief Owned stable handle for a skeletal animation player. */
typedef CNA_Handle CNA_AnimationPlayerHandle;

/** @brief Complete copied construction state for SkinningData. */
typedef struct CNA_SkinningDataDescriptor {
    /** @brief Non-negative skeleton bone count. */
    int32_t bone_count;
    /** @brief Reserved padding; initialize to zero. */
    uint32_t reserved;
    /** @brief Parent indices borrowed for the call; one per bone. */
    const int32_t* skeleton_hierarchy;
    /** @brief Local bind-pose matrices borrowed for the call; one per bone. */
    const CNA_Matrix* bind_pose;
    /** @brief Inverse global bind-pose matrices borrowed for the call; one per bone. */
    const CNA_Matrix* inverse_bind_pose;
    /** @brief Optional root-prefix matrices borrowed for the call. */
    const CNA_Matrix* skeleton_root_prefix;
    /** @brief Root-prefix count; must be zero or equal to bone_count. */
    uint64_t skeleton_root_prefix_count;
    /** @brief Named animation clips borrowed for the call. */
    const CNA_NamedAnimationClipEXTDescriptor* clips;
    /** @brief Number of named clips. */
    uint64_t clip_count;
} CNA_SkinningDataDescriptor;

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

/**
 * @brief Fixed-width severity identity for one glTF import diagnostic.
 *
 * CNA extension: XNA has no import diagnostics. The values match
 * `Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticSeverityEXT`.
 */
typedef uint32_t CNA_GltfImportDiagnosticSeverityEXT;
/** @brief Informational conversion or generated data; no authored result was lost. */
#define CNA_GLTF_IMPORT_SEVERITY_INFORMATION_EXT UINT32_C(0)
/** @brief The imported result differs from, or omits, authored data. */
#define CNA_GLTF_IMPORT_SEVERITY_WARNING_EXT UINT32_C(1)
/** @brief Highest defined diagnostic-severity identity. */
#define CNA_GLTF_IMPORT_SEVERITY_MAXIMUM_EXT CNA_GLTF_IMPORT_SEVERITY_WARNING_EXT

/**
 * @brief Fixed-width identity for what one glTF import diagnostic describes.
 *
 * CNA extension. The values match
 * `Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticKindEXT`.
 */
typedef uint32_t CNA_GltfImportDiagnosticKindEXT;
/** @brief An exact conversion or other useful import note. */
#define CNA_GLTF_IMPORT_KIND_INFORMATION_EXT UINT32_C(0)
/** @brief CNA generated data that the source did not provide. */
#define CNA_GLTF_IMPORT_KIND_GENERATED_DATA_EXT UINT32_C(1)
/** @brief The source data is suspicious, but its actual values were still imported. */
#define CNA_GLTF_IMPORT_KIND_INVALID_SOURCE_DATA_EXT UINT32_C(2)
/** @brief Authored data was represented approximately. */
#define CNA_GLTF_IMPORT_KIND_APPROXIMATION_EXT UINT32_C(3)
/** @brief Authored data could not be carried and was discarded. */
#define CNA_GLTF_IMPORT_KIND_DROPPED_DATA_EXT UINT32_C(4)
/** @brief CNA does not implement the named optional feature. */
#define CNA_GLTF_IMPORT_KIND_UNSUPPORTED_FEATURE_EXT UINT32_C(5)
/** @brief Highest defined diagnostic-kind identity. */
#define CNA_GLTF_IMPORT_KIND_MAXIMUM_EXT CNA_GLTF_IMPORT_KIND_UNSUPPORTED_FEATURE_EXT

/**
 * @brief Structured summary of how one model's source scene was imported.
 *
 * CNA extension. The counts describe the source scene this Model represents. A model that came
 * from another content path, or from a document predating the report, reads back all zeros.
 *
 * The four trailing values are derived rather than stored, and are answered here so a caller does
 * not have to walk the diagnostics to learn whether anything was lost. `diagnostic_count` bounds
 * the index accepted by `cna_model_get_gltf_import_diagnostic_ext`.
 */
typedef struct CNA_GltfImportReportEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Nodes imported from the represented source scene. */
    uint64_t node_count;

    /** @brief Mesh placements imported from source nodes. */
    uint64_t mesh_instance_count;

    /** @brief Distinct source meshes referenced by those placements. */
    uint64_t distinct_mesh_count;

    /** @brief Distinct source meshes referenced by more than one placement. */
    uint64_t shared_mesh_count;

    /** @brief Longest imported root-to-leaf node chain. */
    uint64_t max_node_depth;

    /** @brief Imported scene nodes that reference a camera. */
    uint64_t camera_node_count;

    /** @brief Imported scene nodes that reference a punctual light. */
    uint64_t light_node_count;

    /** @brief Punctual lights that reached a CNA effect light slot. */
    uint64_t imported_light_count;

    /** @brief Source primitives represented by this model, excluding material variants. */
    uint64_t primitive_count;

    /** @brief Independent skins represented by this model. */
    uint64_t skin_count;

    /** @brief Source animations inspected while producing this model. */
    uint64_t animation_count;

    /** @brief Animation clips actually retained by this model. */
    uint64_t clip_count;

    /** @brief Ordered import outcomes available by index; only outcomes that occurred. */
    uint64_t diagnostic_count;

    /** @brief Warning entries, not the sum of their occurrence counts. */
    uint64_t warning_count;

    /** @brief Sum of the dropped-data and unsupported-feature occurrence counts. */
    uint64_t dropped_feature_count;

    /** @brief Sum of the approximation occurrence counts. */
    uint64_t approximation_count;

    /** @brief Whether at least one warning is present, so the result may differ from the source. */
    CNA_Bool anything_lost;
} CNA_GltfImportReportEXT;

/**
 * @brief One programmatically reachable outcome of importing a glTF asset.
 *
 * CNA extension. The four strings this entry carries are read separately, because a string of
 * unbounded length never goes in a fixed structure in this ABI. `detail_count` bounds the index
 * accepted by the per-detail routes.
 */
typedef struct CNA_GltfImportDiagnosticEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Whether this is a note or an observable fidelity warning. */
    CNA_GltfImportDiagnosticSeverityEXT severity;

    /** @brief Whether the outcome generated, approximated, dropped or did not support data. */
    CNA_GltfImportDiagnosticKindEXT kind;

    /** @brief Number of occurrences represented by this entry. */
    uint64_t count;

    /** @brief Largest measured magnitude associated with the entry, or 0 when none applies. */
    double worst_magnitude;

    /** @brief Individual affected names available by index, such as texture maps. */
    uint64_t detail_count;
} CNA_GltfImportDiagnosticEXT;

/**
 * @brief A diagnostic to append, with its strings borrowed for the duration of the call.
 *
 * CNA extension. Appending is how the list is built, rather than passing an array of entries: each
 * entry carries four independent strings, and an array of those in a fixed structure would need a
 * second level of borrowed pointers that a caller has to keep alive for exactly one call.
 */
typedef struct CNA_GltfImportDiagnosticDescriptorEXT {
    /** @brief Stable lower-case, hyphen-separated diagnostic identifier. */
    CNA_StringView code;

    /** @brief One `CNA_GLTF_IMPORT_SEVERITY_*_EXT` identity. */
    CNA_GltfImportDiagnosticSeverityEXT severity;

    /** @brief One `CNA_GLTF_IMPORT_KIND_*_EXT` identity. */
    CNA_GltfImportDiagnosticKindEXT kind;

    /** @brief Primitive, node, clip or extension this entry concerns; may be empty. */
    CNA_StringView subject;

    /** @brief Number of occurrences represented by this entry. */
    uint64_t count;

    /** @brief Largest measured magnitude associated with the entry, or 0 when none applies. */
    double worst_magnitude;

    /** @brief Individual affected names borrowed for the call, or null for none. */
    const CNA_StringView* details;

    /** @brief Number of entries beginning at @ref details. */
    uint64_t detail_count;

    /** @brief Human-readable explanation suitable for a log or diagnostics overlay. */
    CNA_StringView message;
} CNA_GltfImportDiagnosticDescriptorEXT;

/**
 * @brief Gets the glTF import report for a model.
 * @param model Model handle.
 * @param out_report Receives the report; its size and version headers must be set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_gltf_import_report_ext(
    CNA_ModelHandle model,
    CNA_GltfImportReportEXT* out_report);

/**
 * @brief Replaces a model's glTF import report counts and clears its diagnostics.
 *
 * The four derived values and `diagnostic_count` are outputs of the report rather than state, so
 * they are not read from @p report; a caller must set them to zero, and any other value is
 * refused rather than quietly dropped. Build the diagnostics afterwards with
 * `cna_model_add_gltf_import_diagnostic_ext`.
 *
 * @param model Model handle.
 * @param report The counts to record.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for a null, malformed or derived-value-bearing report.
 */
CNA_C_API CNA_Result cna_model_set_gltf_import_report_ext(
    CNA_ModelHandle model,
    const CNA_GltfImportReportEXT* report);

/**
 * @brief Appends one diagnostic to a model's glTF import report.
 * @param model Model handle.
 * @param descriptor The diagnostic to copy in; its strings are borrowed for the call only.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for a null descriptor, an undefined severity or kind, or
 *         an invalid string view.
 */
CNA_C_API CNA_Result cna_model_add_gltf_import_diagnostic_ext(
    CNA_ModelHandle model,
    const CNA_GltfImportDiagnosticDescriptorEXT* descriptor);

/**
 * @brief Gets one import diagnostic by discovery order.
 * @param model Model handle.
 * @param index Diagnostic index below the report's `diagnostic_count`.
 * @param out_diagnostic Receives the diagnostic; its size and version headers must be set.
 * @return `CNA_RESULT_INVALID_ARGUMENT` when @p index is not below the diagnostic count.
 */
CNA_C_API CNA_Result cna_model_get_gltf_import_diagnostic_ext(
    CNA_ModelHandle model,
    uint64_t index,
    CNA_GltfImportDiagnosticEXT* out_diagnostic);

/**
 * @brief Gets the exact byte count of a diagnostic's stable identifier.
 *
 * The code is the machine-readable identity: branch on it, never on the message, which is written
 * for people and may gain detail or wording fixes without becoming an ABI break.
 *
 * @param model Model handle.
 * @param index Diagnostic index.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_gltf_import_diagnostic_code_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies a diagnostic's stable lower-case, hyphen-separated identifier.
 * @param model Model handle.
 * @param index Diagnostic index.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_gltf_import_diagnostic_code_ext(
    CNA_ModelHandle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the exact byte count of the primitive, node, clip or extension concerned. */
CNA_C_API CNA_Result cna_model_get_gltf_import_diagnostic_subject_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies the subject this diagnostic concerns, which may be empty.
 * @param model Model handle.
 * @param index Diagnostic index.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_gltf_import_diagnostic_subject_ext(
    CNA_ModelHandle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the exact byte count of the human-readable explanation. */
CNA_C_API CNA_Result cna_model_get_gltf_import_diagnostic_message_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies the human-readable explanation, suitable for a log or overlay.
 *
 * Display only. A consumer that needs to recognise an outcome branches on the code.
 *
 * @param model Model handle.
 * @param index Diagnostic index.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_gltf_import_diagnostic_message_ext(
    CNA_ModelHandle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the exact byte count of one individual affected name. */
CNA_C_API CNA_Result cna_model_get_gltf_import_diagnostic_detail_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t detail_index,
    uint64_t* out_byte_count);

/**
 * @brief Copies one individual affected name, such as a texture map or custom attribute.
 * @param model Model handle.
 * @param index Diagnostic index.
 * @param detail_index Detail index below the diagnostic's `detail_count`.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_gltf_import_diagnostic_detail_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t detail_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Creates owned morph-target data by deeply copying a fixed C descriptor.
 * @param descriptor Complete borrowed source descriptor.
 * @param out_data Receives the owned data handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_create(
    const CNA_MorphTargetDataEXTDescriptor* descriptor,
    CNA_MorphTargetDataEXTHandle* out_data);

/**
 * @brief Releases an owned morph-target-data handle.
 * @param data Morph-target-data handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_destroy(
    CNA_MorphTargetDataEXTHandle data);

/** @brief Gets the exact UTF-8 byte count of the native morph-target-data type name. */
CNA_C_API CNA_Result cna_morph_target_data_ext_get_type_name_byte_count(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t* out_byte_count);

/**
 * @brief Copies the native morph-target-data type name without a terminator.
 * @param data Morph-target-data handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_type_name(
    CNA_MorphTargetDataEXTHandle data,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the current morph-target base-vertex stride. */
CNA_C_API CNA_Result cna_morph_target_data_ext_get_stride(
    CNA_MorphTargetDataEXTHandle data,
    int32_t* out_stride);

/** @brief Gets the exact base-pose vertex byte count. */
CNA_C_API CNA_Result cna_morph_target_data_ext_get_base_vertex_byte_count(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t* out_byte_count);

/**
 * @brief Copies all base-pose vertex bytes atomically.
 * @param data Morph-target-data handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_base_vertex_bytes(
    CNA_MorphTargetDataEXTHandle data,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the current number of morph targets. */
CNA_C_API CNA_Result cna_morph_target_data_ext_get_target_count(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t* out_target_count);

/**
 * @brief Copies one target's position deltas atomically.
 * @param data Morph-target-data handle.
 * @param target_index Zero-based target index.
 * @param destination Destination values, or null only for zero capacity.
 * @param capacity Destination capacity in vectors.
 * @param out_delta_count Receives the required vector count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_position_deltas(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t target_index,
    CNA_Vector3* destination,
    uint64_t capacity,
    uint64_t* out_delta_count);

/**
 * @brief Copies one target's optional normal deltas atomically.
 * @param data Morph-target-data handle.
 * @param target_index Zero-based target index.
 * @param destination Destination values, or null only for zero capacity.
 * @param capacity Destination capacity in vectors.
 * @param out_delta_count Receives the required vector count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_normal_deltas(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t target_index,
    CNA_Vector3* destination,
    uint64_t capacity,
    uint64_t* out_delta_count);

/**
 * @brief Replaces the current weights with a copied caller array.
 * @param data Morph-target-data handle.
 * @param weights Weight values, or null only for zero count.
 * @param weight_count Number of weights; must equal the target count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_set_weights(
    CNA_MorphTargetDataEXTHandle data,
    const float* weights,
    uint64_t weight_count);

/**
 * @brief Copies current morph-target weights atomically.
 * @param data Morph-target-data handle.
 * @param destination Destination values, or null only for zero capacity.
 * @param capacity Destination capacity in floats.
 * @param out_weight_count Receives the required float count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_weights(
    CNA_MorphTargetDataEXTHandle data,
    float* destination,
    uint64_t capacity,
    uint64_t* out_weight_count);

/**
 * @brief Replaces the optional animation track by deeply copying a fixed descriptor.
 * @param data Morph-target-data handle.
 * @param track Borrowed track descriptor.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_set_weight_track(
    CNA_MorphTargetDataEXTHandle data,
    const CNA_MorphWeightTrackEXTDescriptor* track);

/**
 * @brief Gets the track key count and interpolation flags.
 * @param data Morph-target-data handle.
 * @param out_keyframe_count Receives the keyframe count.
 * @param out_step_interpolation Receives the STEP flag.
 * @param out_cubic_spline Receives the CUBICSPLINE flag.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_get_weight_track_info(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t* out_keyframe_count,
    CNA_Bool* out_step_interpolation,
    CNA_Bool* out_cubic_spline);

/**
 * @brief Copies one stored keyframe's time, weights and tangents atomically per output array.
 * @param data Morph-target-data handle.
 * @param keyframe_index Zero-based keyframe index.
 * @param out_time_seconds Receives the keyframe time in seconds.
 * @param weights Destination weights, or null only for zero capacity.
 * @param weight_capacity Weight destination capacity.
 * @param out_weight_count Receives the required weight count.
 * @param in_tangents Destination incoming tangents, or null only for zero capacity.
 * @param in_tangent_capacity Incoming-tangent destination capacity.
 * @param out_in_tangent_count Receives the required incoming-tangent count.
 * @param out_tangents Destination outgoing tangents, or null only for zero capacity.
 * @param out_tangent_capacity Outgoing-tangent destination capacity.
 * @param out_out_tangent_count Receives the required outgoing-tangent count.
 * @return A CNA result code; insufficient capacity performs no writes.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_weight_keyframe(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t keyframe_index,
    double* out_time_seconds,
    float* weights,
    uint64_t weight_capacity,
    uint64_t* out_weight_count,
    float* in_tangents,
    uint64_t in_tangent_capacity,
    uint64_t* out_in_tangent_count,
    float* out_tangents,
    uint64_t out_tangent_capacity,
    uint64_t* out_out_tangent_count);

/**
 * @brief Blends the data's base pose and deltas into caller-owned bytes.
 * @param data Morph-target-data handle.
 * @param weights Weight values, or null only for zero count.
 * @param weight_count Number of weights; must equal the target count.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_blend(
    CNA_MorphTargetDataEXTHandle data,
    const float* weights,
    uint64_t weight_count,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Evaluates a borrowed morph-weight track into caller-owned floats.
 * @param track Borrowed track descriptor copied before evaluation.
 * @param time_seconds Finite evaluation time in seconds.
 * @param destination Destination weights, or null only for zero capacity.
 * @param capacity Destination capacity in floats.
 * @param out_weight_count Receives the required float count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_morph_weight_track_ext_evaluate(
    const CNA_MorphWeightTrackEXTDescriptor* track,
    double time_seconds,
    float* destination,
    uint64_t capacity,
    uint64_t* out_weight_count);

/**
 * @brief Attaches retained morph-target data to a model mesh part, or clears it.
 * @param part Model-mesh-part handle.
 * @param data Morph-target-data handle, or `CNA_INVALID_HANDLE` to clear it.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_set_morph_target_data_ext(
    CNA_ModelMeshPartHandle part,
    CNA_MorphTargetDataEXTHandle data);

/**
 * @brief Gets an owned alias of morph-target data attached to a model mesh part.
 * @param part Model-mesh-part handle.
 * @param out_has_data Receives whether data is attached.
 * @param out_data Receives an owned alias, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_get_morph_target_data_ext(
    CNA_ModelMeshPartHandle part,
    CNA_Bool* out_has_data,
    CNA_MorphTargetDataEXTHandle* out_data);

/**
 * @brief Re-blends and uploads a model mesh part using its attached morph-target data.
 * @param part Model-mesh-part handle with attached data and a vertex buffer.
 * @param weights Weight values, or null only for zero count.
 * @param weight_count Number of weights; must equal the target count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_set_morph_weights_ext(
    CNA_ModelMeshPartHandle part,
    const float* weights,
    uint64_t weight_count);

/** @brief Creates an owned empty skinned model. */
CNA_C_API CNA_Result cna_skinned_model_ext_create_default(
    CNA_SkinnedModelEXTHandle* out_model);

/**
 * @brief Creates an owned skinned model by deeply copying skeleton and clip descriptors.
 * @param descriptor Complete borrowed construction state.
 * @param out_model Receives the owned model handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_create(
    const CNA_SkinnedModelEXTDescriptor* descriptor,
    CNA_SkinnedModelEXTHandle* out_model);

/** @brief Releases an owned skinned-model handle. */
CNA_C_API CNA_Result cna_skinned_model_ext_destroy(CNA_SkinnedModelEXTHandle model);

/**
 * @brief Move-constructs a new model and leaves the source valid but empty.
 * @param source Source model consumed by native move construction.
 * @param out_model Receives the new owned model handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_create_move(
    CNA_SkinnedModelEXTHandle source,
    CNA_SkinnedModelEXTHandle* out_model);

/**
 * @brief Move-assigns one model into another and leaves the source valid but empty.
 * @param destination Existing destination model.
 * @param source Source model consumed by native move assignment.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_move_assign(
    CNA_SkinnedModelEXTHandle destination,
    CNA_SkinnedModelEXTHandle source);

/**
 * @brief Atomically replaces skeleton arrays while retaining clips and parts.
 * @param model Skinned-model handle.
 * @param bone_count Non-negative number of bones.
 * @param parent_bone_indices Parent indices; one per bone.
 * @param bind_pose_local Local bind matrices; one per bone.
 * @param inverse_bind_pose_global Inverse global bind matrices; one per bone.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_set_skeleton(
    CNA_SkinnedModelEXTHandle model,
    int32_t bone_count,
    const int32_t* parent_bone_indices,
    const CNA_Matrix* bind_pose_local,
    const CNA_Matrix* inverse_bind_pose_global);

/** @brief Gets the skinned model's bone count. */
CNA_C_API CNA_Result cna_skinned_model_ext_get_bone_count(
    CNA_SkinnedModelEXTHandle model,
    uint64_t* out_bone_count);

/** @brief Copies all parent-bone indices atomically. */
CNA_C_API CNA_Result cna_skinned_model_ext_copy_parent_bone_indices(
    CNA_SkinnedModelEXTHandle model,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies all local bind-pose matrices atomically. */
CNA_C_API CNA_Result cna_skinned_model_ext_copy_bind_pose_local(
    CNA_SkinnedModelEXTHandle model,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies all inverse global bind-pose matrices atomically. */
CNA_C_API CNA_Result cna_skinned_model_ext_copy_inverse_bind_pose_global(
    CNA_SkinnedModelEXTHandle model,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Inserts or replaces a named animation clip by deep copy.
 * @param model Skinned-model handle.
 * @param name Exact UTF-8 clip name.
 * @param clip Borrowed clip descriptor.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_set_clip(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView name,
    const CNA_AnimationClipEXTDescriptor* clip);

/**
 * @brief Removes a named animation clip if present.
 * @param model Skinned-model handle.
 * @param name Exact UTF-8 clip name.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_remove_clip(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView name);

/** @brief Gets the number of animation clips. */
CNA_C_API CNA_Result cna_skinned_model_ext_get_clip_count(
    CNA_SkinnedModelEXTHandle model,
    uint64_t* out_clip_count);

/** @brief Gets the exact byte count of a sorted clip name at an index. */
CNA_C_API CNA_Result cna_skinned_model_ext_get_clip_name_byte_count_at(
    CNA_SkinnedModelEXTHandle model,
    uint64_t clip_index,
    uint64_t* out_byte_count);

/**
 * @brief Copies a sorted clip name at an index without a terminator.
 * @param model Skinned-model handle.
 * @param clip_index Zero-based index in lexicographically sorted clip names.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_copy_clip_name_at(
    CNA_SkinnedModelEXTHandle model,
    uint64_t clip_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets one named clip's duration and track count.
 * @param model Skinned-model handle.
 * @param name Exact UTF-8 clip name.
 * @param out_found Receives whether the clip exists.
 * @param out_duration_seconds Receives its duration, or zero when absent.
 * @param out_track_count Receives its track count, or zero when absent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_get_clip_info(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView name,
    CNA_Bool* out_found,
    double* out_duration_seconds,
    uint64_t* out_track_count);

/**
 * @brief Copies one named clip track and all of its keyframes atomically.
 * @param model Skinned-model handle.
 * @param name Exact UTF-8 clip name.
 * @param track_index Zero-based track index.
 * @param out_bone_index Receives the signed driven-bone index.
 * @param destination Destination keyframes, or null only for zero capacity.
 * @param capacity Destination capacity in keyframes.
 * @param out_keyframe_count Receives the required keyframe count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_copy_clip_track(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView name,
    uint64_t track_index,
    int32_t* out_bone_index,
    CNA_KeyframeEXT* destination,
    uint64_t capacity,
    uint64_t* out_keyframe_count);

/**
 * @brief Computes final skinning matrices for a named clip.
 * @param model Skinned-model handle.
 * @param clip_name Exact UTF-8 clip name.
 * @param position_seconds Finite playback position in seconds.
 * @param loop Whether to wrap instead of clamp; must be a canonical C boolean.
 * @param destination Destination matrices, or null only for zero capacity.
 * @param capacity Destination capacity in matrices.
 * @param out_bone_count Receives the required matrix count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_compute_bone_transforms(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView clip_name,
    double position_seconds,
    CNA_Bool loop,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_bone_count);

/**
 * @brief Adds and retains one renderable part and its owned graphics resources.
 * @param model Skinned-model handle.
 * @param name Copied UTF-8 part name.
 * @param vertex_buffer Required same-device VertexBuffer handle.
 * @param index_buffer Required same-device IndexBuffer handle.
 * @param part Required unowned ModelMeshPart handle.
 * @param texture Optional same-device Texture2D handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_add_part(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView name,
    CNA_VertexBufferHandle vertex_buffer,
    CNA_IndexBufferHandle index_buffer,
    CNA_ModelMeshPartHandle part,
    CNA_Handle texture);

/** @brief Moves all parts from a same-skeleton model with replace-by-name semantics. */
CNA_C_API CNA_Result cna_skinned_model_ext_attach_parts(
    CNA_SkinnedModelEXTHandle model,
    CNA_SkinnedModelEXTHandle other);

/** @brief Removes every named part and releases its retained graphics resources. */
CNA_C_API CNA_Result cna_skinned_model_ext_remove_part(
    CNA_SkinnedModelEXTHandle model,
    CNA_StringView name);

/** @brief Gets the current number of renderable parts. */
CNA_C_API CNA_Result cna_skinned_model_ext_get_part_count(
    CNA_SkinnedModelEXTHandle model,
    uint64_t* out_part_count);

/** @brief Gets the exact byte count of a part name at an index. */
CNA_C_API CNA_Result cna_skinned_model_ext_get_part_name_byte_count_at(
    CNA_SkinnedModelEXTHandle model,
    uint64_t part_index,
    uint64_t* out_byte_count);

/** @brief Copies a part name at an index without a terminator. */
CNA_C_API CNA_Result cna_skinned_model_ext_copy_part_name_at(
    CNA_SkinnedModelEXTHandle model,
    uint64_t part_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets an owned part alias and optional retained texture handle at an index.
 * @param model Skinned-model handle.
 * @param part_index Zero-based part index.
 * @param out_part Receives an owned ModelMeshPart alias.
 * @param out_has_texture Receives whether the part has a texture.
 * @param out_texture Receives the retained texture handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_get_part_at(
    CNA_SkinnedModelEXTHandle model,
    uint64_t part_index,
    CNA_ModelMeshPartHandle* out_part,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Gets owned-resource counts in native testing-method order.
 * @param model Skinned-model handle.
 * @param out_vertex_buffers Receives the owned vertex-buffer count.
 * @param out_index_buffers Receives the owned index-buffer count.
 * @param out_parts Receives the owned mesh-part count.
 * @param out_textures Receives the owned texture count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_model_ext_get_owned_resource_counts(
    CNA_SkinnedModelEXTHandle model,
    uint64_t* out_vertex_buffers,
    uint64_t* out_index_buffers,
    uint64_t* out_parts,
    uint64_t* out_textures);

/**
 * @brief Creates owned SkinningData by deeply copying all descriptor state.
 * @param descriptor Complete borrowed construction state.
 * @param out_data Receives the owned SkinningData handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinning_data_create(
    const CNA_SkinningDataDescriptor* descriptor,
    CNA_SkinningDataHandle* out_data);

/** @brief Releases an owned SkinningData handle. */
CNA_C_API CNA_Result cna_skinning_data_destroy(CNA_SkinningDataHandle data);

/** @brief Gets the exact byte count of the SkinningData type name. */
CNA_C_API CNA_Result cna_skinning_data_get_type_name_byte_count(
    CNA_SkinningDataHandle data,
    uint64_t* out_byte_count);

/** @brief Copies the SkinningData type name without a terminator. */
CNA_C_API CNA_Result cna_skinning_data_copy_type_name(
    CNA_SkinningDataHandle data,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the SkinningData bone count. */
CNA_C_API CNA_Result cna_skinning_data_get_bone_count(
    CNA_SkinningDataHandle data,
    uint64_t* out_bone_count);

/** @brief Copies every skeleton parent index atomically. */
CNA_C_API CNA_Result cna_skinning_data_copy_skeleton_hierarchy(
    CNA_SkinningDataHandle data,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies every local bind-pose matrix atomically. */
CNA_C_API CNA_Result cna_skinning_data_copy_bind_pose(
    CNA_SkinningDataHandle data,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies every inverse global bind-pose matrix atomically. */
CNA_C_API CNA_Result cna_skinning_data_copy_inverse_bind_pose(
    CNA_SkinningDataHandle data,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies the optional root-prefix matrix array atomically. */
CNA_C_API CNA_Result cna_skinning_data_copy_skeleton_root_prefix(
    CNA_SkinningDataHandle data,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Gets the number of named SkinningData animation clips. */
CNA_C_API CNA_Result cna_skinning_data_get_clip_count(
    CNA_SkinningDataHandle data,
    uint64_t* out_clip_count);

/** @brief Gets the exact byte count of a sorted SkinningData clip name. */
CNA_C_API CNA_Result cna_skinning_data_get_clip_name_byte_count_at(
    CNA_SkinningDataHandle data,
    uint64_t clip_index,
    uint64_t* out_byte_count);

/** @brief Copies a sorted SkinningData clip name without a terminator. */
CNA_C_API CNA_Result cna_skinning_data_copy_clip_name_at(
    CNA_SkinningDataHandle data,
    uint64_t clip_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets one SkinningData clip's duration and track count.
 * @param data SkinningData handle.
 * @param name Exact UTF-8 clip name.
 * @param out_found Receives whether the clip exists.
 * @param out_duration_seconds Receives its duration, or zero when absent.
 * @param out_track_count Receives its track count, or zero when absent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinning_data_get_clip_info(
    CNA_SkinningDataHandle data,
    CNA_StringView name,
    CNA_Bool* out_found,
    double* out_duration_seconds,
    uint64_t* out_track_count);

/** @brief Copies one SkinningData animation track and its keyframes atomically. */
CNA_C_API CNA_Result cna_skinning_data_copy_clip_track(
    CNA_SkinningDataHandle data,
    CNA_StringView name,
    uint64_t track_index,
    int32_t* out_bone_index,
    CNA_KeyframeEXT* destination,
    uint64_t capacity,
    uint64_t* out_keyframe_count);

/**
 * @brief Creates an animation player retaining the supplied SkinningData.
 * @param data Valid copied skeleton and clip data.
 * @param out_player Receives the owned player handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_animation_player_create(
    CNA_SkinningDataHandle data,
    CNA_AnimationPlayerHandle* out_player);

/** @brief Releases an owned animation-player handle. */
CNA_C_API CNA_Result cna_animation_player_destroy(CNA_AnimationPlayerHandle player);

/** @brief Starts a named retained clip from position zero. */
CNA_C_API CNA_Result cna_animation_player_start_clip(
    CNA_AnimationPlayerHandle player,
    CNA_StringView clip_name);

/**
 * @brief Advances or seeks the current animation and recomputes transforms.
 * @param player Animation-player handle.
 * @param time_seconds Finite elapsed or absolute position in seconds.
 * @param relative_to_current_time Whether to add to the current position.
 * @param loop Whether to wrap rather than clamp.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_animation_player_update(
    CNA_AnimationPlayerHandle player,
    double time_seconds,
    CNA_Bool relative_to_current_time,
    CNA_Bool loop);

/** @brief Gets the current playback position in seconds. */
CNA_C_API CNA_Result cna_animation_player_get_current_position(
    CNA_AnimationPlayerHandle player,
    double* out_position_seconds);

/**
 * @brief Gets current-clip presence, duration and track count.
 * @param player Animation-player handle.
 * @param out_has_clip Receives whether StartClip has selected a clip.
 * @param out_duration_seconds Receives its duration, or zero when absent.
 * @param out_track_count Receives its track count, or zero when absent.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_animation_player_get_current_clip_info(
    CNA_AnimationPlayerHandle player,
    CNA_Bool* out_has_clip,
    double* out_duration_seconds,
    uint64_t* out_track_count);

/** @brief Gets the current clip-name byte count, or zero when absent. */
CNA_C_API CNA_Result cna_animation_player_get_current_clip_name_byte_count(
    CNA_AnimationPlayerHandle player,
    uint64_t* out_byte_count);

/** @brief Copies the current clip name without a terminator. */
CNA_C_API CNA_Result cna_animation_player_copy_current_clip_name(
    CNA_AnimationPlayerHandle player,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Copies all current bone-local transforms atomically. */
CNA_C_API CNA_Result cna_animation_player_copy_bone_transforms(
    CNA_AnimationPlayerHandle player,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies all current model-space transforms atomically. */
CNA_C_API CNA_Result cna_animation_player_copy_world_transforms(
    CNA_AnimationPlayerHandle player,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Copies all current final skin transforms atomically. */
CNA_C_API CNA_Result cna_animation_player_copy_skin_transforms(
    CNA_AnimationPlayerHandle player,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Gets the topology this part's index buffer describes.
 *
 * CNA extension: real XNA carries the topology as a draw argument rather than part state.
 *
 * @param part Model-mesh-part handle.
 * @param out_value Receives one `CNA_PRIMITIVE_*` identity.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_get_primitive_type_ext(
    CNA_ModelMeshPartHandle part,
    CNA_PrimitiveType* out_value);

/**
 * @brief Sets the topology this part's index buffer describes.
 *
 * Setting this does not reinterpret the index data; it states what that data already means.
 *
 * @param part Model-mesh-part handle.
 * @param value One `CNA_PRIMITIVE_*` identity.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_model_mesh_part_set_primitive_type_ext(
    CNA_ModelMeshPartHandle part,
    CNA_PrimitiveType value);

/**
 * @brief Gets one texture slot's sampler state for this part.
 *
 * The seven slots are the `CNA_PBR_TEXTURE_*` identities: the five material maps followed by the
 * two `KHR_materials_specular` maps. The canonical API keeps these as two separate arrays; the
 * slot identity spans both so a caller has one vocabulary for a texture and its sampler.
 *
 * @param part Model-mesh-part handle.
 * @param slot One of the `CNA_PBR_TEXTURE_*` identities.
 * @param out_state Receives the sampler state; its size and version headers must be set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_mesh_part_get_sampler_state_ext(
    CNA_ModelMeshPartHandle part,
    CNA_PbrTextureSlot slot,
    CNA_SamplerState* out_state);

/**
 * @brief Sets one texture slot's sampler state for this part.
 * @param part Model-mesh-part handle.
 * @param slot One of the `CNA_PBR_TEXTURE_*` identities.
 * @param state The sampler state to apply when this part's texture is bound to that slot.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for an undefined slot or a malformed state.
 */
CNA_C_API CNA_Result cna_model_mesh_part_set_sampler_state_ext(
    CNA_ModelMeshPartHandle part,
    CNA_PbrTextureSlot slot,
    const CNA_SamplerState* state);

/**
 * @brief Gets which index space one clip's bone indices are in.
 * @param data SkinningData handle.
 * @param clip_index Clip index in the order reported by `cna_skinning_data_copy_clip_name_at`.
 * @param out_value Receives one `CNA_CLIP_TARGET_SPACE_*_EXT` identity.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinning_data_get_clip_target_space_ext(
    CNA_SkinningDataHandle data,
    uint64_t clip_index,
    CNA_ClipTargetSpaceEXT* out_value);

/**
 * @brief States which index space one clip's bone indices are in.
 *
 * A separate route rather than a field on the creation descriptor, because that descriptor is
 * published without a size or version header and growing it would move every field after it.
 *
 * @param data SkinningData handle.
 * @param clip_index Clip index in the order reported by `cna_skinning_data_copy_clip_name_at`.
 * @param value One `CNA_CLIP_TARGET_SPACE_*_EXT` identity.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or an out-of-range index.
 */
CNA_C_API CNA_Result cna_skinning_data_set_clip_target_space_ext(
    CNA_SkinningDataHandle data,
    uint64_t clip_index,
    CNA_ClipTargetSpaceEXT value);

/**
 * @brief Copies one target's optional tangent deltas atomically.
 *
 * glTF morphs the tangent direction only, so these are three-component vectors: the handedness
 * describes the UV winding and cannot be interpolated, so it stays on the base vertex.
 *
 * @param data Morph-target-data handle.
 * @param target_index Zero-based target index.
 * @param destination Destination values, or null only for zero capacity.
 * @param capacity Destination capacity in vectors.
 * @param out_delta_count Receives the required vector count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_tangent_deltas(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t target_index,
    CNA_Vector3* destination,
    uint64_t capacity,
    uint64_t* out_delta_count);

/**
 * @brief Reports whether the blend recomputes flat normals from the morphed positions.
 *
 * @param data Morph-target-data handle.
 * @param out_recompute Receives `CNA_TRUE` when flat normals are recomputed.
 * @return A CNA result code.
 *
 * A primitive whose base mesh specifies no normals needs them calculated per face for each target,
 * because a position delta can rotate a face and a normal baked at rest is only right at weight
 * zero. False by default.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_get_recompute_flat_normals_ext(
    CNA_MorphTargetDataEXTHandle data,
    CNA_Bool* out_recompute);

/**
 * @brief Sets whether the blend recomputes flat normals from the morphed positions.
 *
 * @param data Morph-target-data handle.
 * @param recompute `CNA_TRUE` or `CNA_FALSE`; any other value is refused.
 * @return A CNA result code.
 *
 * Enabling this without supplying triangle indices leaves the blend nothing to recompute from; set
 * them with `cna_morph_target_data_ext_set_triangle_indices_ext`.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_set_recompute_flat_normals_ext(
    CNA_MorphTargetDataEXTHandle data,
    CNA_Bool recompute);

/**
 * @brief Copies the triangle list the flat-normal recomputation reads, three indices per face.
 *
 * @param data Morph-target-data handle.
 * @param destination Destination indices, or null only for zero capacity.
 * @param capacity Destination capacity in indices.
 * @param out_index_count Receives the required index count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 *
 * Empty unless flat-normal recomputation is enabled, so an ordinary morph target costs nothing for
 * it. Held here rather than read back from the part's index buffer because index readback is not
 * something every renderer can offer.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_copy_triangle_indices_ext(
    CNA_MorphTargetDataEXTHandle data,
    uint32_t* destination,
    uint64_t capacity,
    uint64_t* out_index_count);

/**
 * @brief Replaces the triangle list from a copied array.
 *
 * @param data Morph-target-data handle.
 * @param indices Caller-owned indices, or null only for a zero count.
 * @param index_count Number of indices; must be a multiple of three.
 * @return A CNA result code; a count that is not a multiple of three is
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_set_triangle_indices_ext(
    CNA_MorphTargetDataEXTHandle data,
    const uint32_t* indices,
    uint64_t index_count);

/**
 * @brief Replaces one target's tangent deltas from a copied array.
 *
 * A separate route rather than a field on the creation descriptor, for the same reason as the
 * clip target space: that descriptor carries no size or version header.
 *
 * @param data Morph-target-data handle.
 * @param target_index Zero-based target index.
 * @param deltas Caller-owned values, or null only for a zero count.
 * @param delta_count Number of vectors, either zero or the target's vertex count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_morph_target_data_ext_set_tangent_deltas(
    CNA_MorphTargetDataEXTHandle data,
    uint64_t target_index,
    const CNA_Vector3* deltas,
    uint64_t delta_count);

/**
 * @brief A camera imported from a source scene, without its name.
 *
 * CNA extension. The name is read separately, because a string of unbounded length never goes in
 * a fixed structure in this ABI.
 */
typedef struct CNA_ModelCameraEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Index of the scene node that carries this camera, or -1 when unknown. */
    int32_t scene_node_index;

    /** @brief Whether the projection is perspective rather than orthographic. */
    CNA_Bool is_perspective;

    /** @brief Whether the perspective projection has no far plane. */
    CNA_Bool has_infinite_far_plane;

    /** @brief Whether the source declared an aspect ratio of its own. */
    CNA_Bool has_authored_aspect_ratio;

    /** @brief The projection matrix as imported. */
    CNA_Matrix projection;

    /** @brief The camera's world transform as imported. */
    CNA_Matrix world_transform;

    /** @brief Aspect ratio; 1 when the source declared none. */
    float aspect_ratio;

    /** @brief Vertical field of view in radians; 0 for an orthographic camera. */
    float field_of_view;

    /** @brief Near plane distance. */
    float near_plane_distance;

    /** @brief Far plane distance; meaningless when the far plane is infinite. */
    float far_plane_distance;
} CNA_ModelCameraEXT;

/** @brief A camera to append, with its name borrowed for the duration of the call. */
typedef struct CNA_ModelCameraDescriptorEXT {
    /** @brief The source camera's display name; may be empty. */
    CNA_StringView name;

    /** @brief The camera state to copy in. */
    CNA_ModelCameraEXT camera;
} CNA_ModelCameraDescriptorEXT;

/** @brief Gets how many imported cameras this model carries. */
CNA_C_API CNA_Result cna_model_get_camera_count_ext(
    CNA_ModelHandle model,
    uint64_t* out_count);

/**
 * @brief Gets one imported camera by source order.
 * @param model Model handle.
 * @param index Camera index below the camera count.
 * @param out_camera Receives the camera; its size and version headers must be set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_camera_ext(
    CNA_ModelHandle model,
    uint64_t index,
    CNA_ModelCameraEXT* out_camera);

/** @brief Gets the exact UTF-8 byte count of one camera's name. */
CNA_C_API CNA_Result cna_model_get_camera_name_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies one camera's display name without a terminator.
 * @param model Model handle.
 * @param index Camera index.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_camera_name_ext(
    CNA_ModelHandle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Removes every imported camera from this model. */
CNA_C_API CNA_Result cna_model_clear_cameras_ext(CNA_ModelHandle model);

/**
 * @brief Appends one imported camera to this model.
 * @param model Model handle.
 * @param descriptor The camera to copy in; its name is borrowed for the call only.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_add_camera_ext(
    CNA_ModelHandle model,
    const CNA_ModelCameraDescriptorEXT* descriptor);

/** @brief Gets how many independent skins this model carries. */
CNA_C_API CNA_Result cna_model_get_skin_count_ext(
    CNA_ModelHandle model,
    uint64_t* out_count);

/**
 * @brief Gets whether one skin names a skeleton, and how many meshes it poses.
 * @param model Model handle.
 * @param index Skin index below the skin count.
 * @param out_has_data Receives whether the skin names a skeleton.
 * @param out_mesh_count Receives how many mesh placements consume this skin's palette.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_skin_ext(
    CNA_ModelHandle model,
    uint64_t index,
    CNA_Bool* out_has_data,
    uint64_t* out_mesh_count);

/**
 * @brief Creates a new owned handle for one skin's skeleton.
 *
 * A fresh handle rather than the one the caller passed to `cna_model_add_skin_ext`, because that
 * handle may have been destroyed since. The model keeps the skeleton alive for as long as the
 * skin exists, so destroying either handle never destroys the other's object.
 *
 * @param model Model handle.
 * @param index Skin index.
 * @param out_data Receives an owned SkinningData handle the caller destroys.
 * @return `CNA_RESULT_INVALID_STATE` when the skin names no skeleton.
 */
CNA_C_API CNA_Result cna_model_create_skin_skeleton_handle_ext(
    CNA_ModelHandle model,
    uint64_t index,
    CNA_SkinningDataHandle* out_data);

/** @brief Gets the exact UTF-8 byte count of one skin's name. */
CNA_C_API CNA_Result cna_model_get_skin_name_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies one skin's display name without a terminator.
 * @param model Model handle.
 * @param index Skin index.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_skin_name_ext(
    CNA_ModelHandle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets which of this model's meshes one skin poses.
 * @param model Model handle.
 * @param index Skin index.
 * @param mesh_index Index below the skin's mesh count.
 * @param out_model_mesh_index Receives the index into this model's own mesh collection.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_skin_mesh_index_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t mesh_index,
    uint64_t* out_model_mesh_index);

/** @brief Removes every skin from this model and releases the SkinningData it retained. */
CNA_C_API CNA_Result cna_model_clear_skins_ext(CNA_ModelHandle model);

/**
 * @brief Appends one skin to this model, retaining its skeleton.
 *
 * The meshes are named by index into this model's own mesh collection rather than by handle: the
 * model already owns those meshes, so an index cannot outlive what it points at. The SkinningData
 * is retained for as long as the skin exists, so destroying the caller's handle afterwards is
 * safe.
 *
 * @param model Model handle.
 * @param name The skin's display name; may be empty.
 * @param data SkinningData handle, or `CNA_INVALID_HANDLE` for a skin with no skeleton.
 * @param mesh_indices Indices into this model's mesh collection, borrowed for the call.
 * @param mesh_index_count Number of indices, which may be zero.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range mesh index or a null array.
 */
CNA_C_API CNA_Result cna_model_add_skin_ext(
    CNA_ModelHandle model,
    CNA_StringView name,
    CNA_SkinningDataHandle data,
    const uint64_t* mesh_indices,
    uint64_t mesh_index_count);

/**
 * @brief Gets the sphere containing every mesh's bounding sphere.
 * @param model Model handle.
 * @param out_has_value Receives whether the model has any mesh at all.
 * @param out_sphere Receives the merged sphere when one exists.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_get_bounding_sphere_ext(
    CNA_ModelHandle model,
    CNA_Bool* out_has_value,
    CNA_BoundingSphere* out_sphere);

/** @brief Gets how many material variants the imported asset declared. */
CNA_C_API CNA_Result cna_model_get_material_variant_count_ext(
    CNA_ModelHandle model,
    uint64_t* out_count);

/** @brief Gets the exact UTF-8 byte count of one material-variant name. */
CNA_C_API CNA_Result cna_model_get_material_variant_name_byte_count_ext(
    CNA_ModelHandle model,
    uint64_t index,
    uint64_t* out_byte_count);

/**
 * @brief Copies one material-variant name without a terminator.
 * @param model Model handle.
 * @param index Variant index in source order.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_copy_material_variant_name_ext(
    CNA_ModelHandle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the selected material-variant index, or -1 for default materials. */
CNA_C_API CNA_Result cna_model_get_material_variant_ext(
    CNA_ModelHandle model,
    int32_t* out_value);

/**
 * @brief Selects one imported material variant, or restores defaults with -1.
 * @param model Model handle.
 * @param value Variant index in source order, or -1.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for a value below -1 or past the declared variants.
 */
CNA_C_API CNA_Result cna_model_set_material_variant_ext(
    CNA_ModelHandle model,
    int32_t value);

/**
 * @brief Builds a perspective projection with no far plane.
 *
 * CNA extension: XNA's perspective factories all take a far plane. A glTF camera may declare
 * none, and clamping one in would move geometry the source meant to remain visible.
 *
 * @param field_of_view Vertical field of view in radians.
 * @param aspect_ratio Width divided by height.
 * @param near_plane_distance Near plane distance.
 * @param out_matrix Receives the projection.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for a null output or an argument the native factory
 *         rejects.
 */
CNA_C_API CNA_Result cna_matrix_create_infinite_perspective_field_of_view_ext(
    float field_of_view,
    float aspect_ratio,
    float near_plane_distance,
    CNA_Matrix* out_matrix);

/** @brief Owned stable handle for a copied set of named animation clips. */
typedef CNA_Handle CNA_ModelAnimationsEXTHandle;

/**
 * @brief Creates an owned set of named animation clips.
 *
 * CNA extension: the clips a glTF scene declares, separate from any one skeleton, so a model
 * whose animations target scene nodes rather than a joint palette has somewhere to keep them.
 *
 * @param clips Named clips borrowed for the call, or null for an empty set.
 * @param clip_count Number of named clips.
 * @param out_animations Receives the owned handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_animations_ext_create(
    const CNA_NamedAnimationClipEXTDescriptor* clips,
    uint64_t clip_count,
    CNA_ModelAnimationsEXTHandle* out_animations);

/** @brief Releases an owned ModelAnimationsEXT handle. */
CNA_C_API CNA_Result cna_model_animations_ext_destroy(CNA_ModelAnimationsEXTHandle animations);

/** @brief Gets the exact byte count of the ModelAnimationsEXT type name. */
CNA_C_API CNA_Result cna_model_animations_ext_get_type_name_byte_count(
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t* out_byte_count);

/** @brief Copies the ModelAnimationsEXT type name without a terminator. */
CNA_C_API CNA_Result cna_model_animations_ext_copy_type_name(
    CNA_ModelAnimationsEXTHandle animations,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets how many clips this set carries. */
CNA_C_API CNA_Result cna_model_animations_ext_get_clip_count(
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t* out_count);

/** @brief Gets the exact byte count of one clip name, in sorted order. */
CNA_C_API CNA_Result cna_model_animations_ext_get_clip_name_byte_count_at(
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t clip_index,
    uint64_t* out_byte_count);

/**
 * @brief Copies one clip name without a terminator, in sorted order.
 *
 * Sorted rather than in insertion order: the canonical type keeps its clips in a hash map, whose
 * traversal order is not something a C consumer may depend on.
 *
 * @param animations ModelAnimationsEXT handle.
 * @param clip_index Clip index below the clip count.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_model_animations_ext_copy_clip_name_at(
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t clip_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets one clip's duration, track count and target space.
 * @param animations ModelAnimationsEXT handle.
 * @param clip_index Clip index below the clip count.
 * @param out_duration_seconds Receives the clip duration in seconds.
 * @param out_track_count Receives the number of bone tracks.
 * @param out_target_space Receives one `CNA_CLIP_TARGET_SPACE_*_EXT` identity.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_animations_ext_get_clip_info_at(
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t clip_index,
    double* out_duration_seconds,
    uint64_t* out_track_count,
    CNA_ClipTargetSpaceEXT* out_target_space);

/** @brief States which index space one clip's bone indices are in. */
CNA_C_API CNA_Result cna_model_animations_ext_set_clip_target_space_at(
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t clip_index,
    CNA_ClipTargetSpaceEXT value);

/**
 * @brief Poses a model's bones from one scene-node clip at a point in time.
 *
 * Each track's bone index selects a `Model::Bones` entry directly, so the clip must state
 * `CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT`: applying a joint-palette clip's indices to `Model::Bones`
 * would pose the wrong bones without saying so, which is why it is refused here instead.
 *
 * @param model Model handle.
 * @param animations ModelAnimationsEXT handle holding the clip.
 * @param clip_index Clip index below the clip count.
 * @param time_seconds The time to evaluate at, clamped to the clip's duration.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for a joint-palette clip or an out-of-range index.
 */
CNA_C_API CNA_Result cna_model_apply_clip_to_bones_ext(
    CNA_ModelHandle model,
    CNA_ModelAnimationsEXTHandle animations,
    uint64_t clip_index,
    double time_seconds);

/**
 * @brief Poses every skinned effect on a model with its skeleton's bind pose.
 *
 * A model from an older or non-glTF path has no per-skin mapping and retains the historical
 * apply-to-all behaviour. This holds no state of its own: animating afterwards simply overwrites
 * the palette.
 *
 * @param model Model handle whose parts carry skinned effects.
 * @param data SkinningData handle, normally the skeleton the model was built with.
 * @param out_posed_count Receives how many skinned effects were posed; 0 for a model with none.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_model_apply_bind_pose_bone_transforms_ext(
    CNA_ModelHandle model,
    CNA_SkinningDataHandle data,
    uint64_t* out_posed_count);

/** @brief Gets the declared rig root's scene-node index, or -1 when the file declares none. */
CNA_C_API CNA_Result cna_skinning_data_get_skeleton_root_node_index_ext(
    CNA_SkinningDataHandle data,
    int32_t* out_value);

/** @brief States the declared rig root's scene-node index, or -1 for none. */
CNA_C_API CNA_Result cna_skinning_data_set_skeleton_root_node_index_ext(
    CNA_SkinningDataHandle data,
    int32_t value);

/** @brief Gets the exact byte count of the declared rig root's node name. */
CNA_C_API CNA_Result cna_skinning_data_get_skeleton_root_name_byte_count_ext(
    CNA_SkinningDataHandle data,
    uint64_t* out_byte_count);

/**
 * @brief Copies the declared rig root's node name without a terminator.
 * @param data SkinningData handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_skinning_data_copy_skeleton_root_name_ext(
    CNA_SkinningDataHandle data,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief States the declared rig root's node name; may be empty. */
CNA_C_API CNA_Result cna_skinning_data_set_skeleton_root_name_ext(
    CNA_SkinningDataHandle data,
    CNA_StringView name);

/* --- CBIND-118: the Model an XNA game gets from ContentManager.Load<Model> -------------------- */

/**
 * @brief Loads a `Model` from a compiled `.xnb` asset.
 *
 * @param content_manager The content manager.
 * @param asset_name The asset name, without extension.
 * @param out_model Receives the owned model handle.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_IO` for a missing or malformed asset and for one whose
 *         root object is not a `Model`; or a documented argument/handle failure.
 *
 * This maps the canonical `Load<Model>` specialization -- the route an XNA game's
 * `ContentManager.Load<Model>` takes -- and it is what makes every other model route reachable for
 * content: before it, every `CNA_ModelHandle` was built by hand from `cna_model_create_*`, so a
 * game written in C could construct a model but never open one.
 *
 * What comes back is an ordinary model handle. Bones, meshes, parts, the root bone and the parent
 * links all answer as they do for a model built by hand, and so do each part's effect, vertex
 * buffer and index buffer.
 *
 * **The model owns the handles it publishes.** A loaded part's effect and buffers are objects the
 * model already owned, and the handles this route creates for them are released when the model is
 * destroyed -- do not release them by hand, and do not keep one past
 * @ref cna_model_destroy. For the same reason a model-owned effect refuses `cna_effect_destroy`:
 * disposing it would reach inside an asset the content manager is still caching.
 *
 * The asset is cached by name exactly as every other load is, so a second call re-publishes handles
 * over the same underlying model rather than re-reading the file.
 */
CNA_C_API CNA_Result cna_content_manager_load_model(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_ModelHandle* out_model);

/* --- CBIND-118: the Tag the content pipeline wrote, beside the C-owned one -------------------- */

/**
 * @brief Gets a model's content-loaded `Tag` as a `Dictionary<string, object>`.
 *
 * @param model Model handle.
 * @param out_has_tag Receives whether the model carries a dictionary tag.
 * @param out_dictionary Receives a new owned dictionary handle when it does, otherwise
 *        `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 *
 * **This is a different tag from @ref cna_model_get_tag.** That one is a C-owned opaque value a
 * caller sets and reads back; this one is what a custom `ContentProcessor` wrote into the `.xnb`,
 * and it is read-only. Both exist because they answer different questions.
 *
 * The shape XNA's own `TrianglePickingSample` uses: its processor tags every model with the model's
 * world-space triangle vertices and a `BoundingSphere`, and the game reads them back off the tag to
 * pick against real triangles rather than against a bounding volume. That sample is the reason this
 * route exists, and reading it from C is the same three steps it is in C#: load the model, take the
 * tag, ask each entry for its value.
 *
 * @p out_has_tag is false, with a success result, when the model has no tag or carries one of
 * another shape -- an unset tag is not an error, exactly as XNA's `null` `Tag` is not.
 *
 * The handle is **owned and outlives the model**: release it with
 * @ref cna_object_dictionary_ext_destroy. It keeps the loaded model's data alive on its own, so
 * destroying the model first is safe and does not invalidate it.
 */
CNA_C_API CNA_Result cna_model_get_content_tag_dictionary_ext(
    CNA_ModelHandle model,
    CNA_Bool* out_has_tag,
    CNA_ObjectDictionaryHandle* out_dictionary);

/**
 * @brief Gets a model's content-loaded `Tag` as an object a caller's own reflective reader made.
 *
 * @param model Model handle.
 * @param out_has_tag Receives whether the model carries such a tag.
 * @param out_object Receives the pointer the caller's object factory returned, otherwise null.
 * @return A CNA result code.
 *
 * The other half of @ref cna_reflective_type_reader_builder_register_shared, and the one place its
 * reference shape is not merely cosmetic: `ModelReader`'s tag path takes a reference and refuses a
 * value, so a type registered the value-shaped way fails the load here rather than arriving in the
 * wrong form.
 *
 * The pointer is the caller's own -- CNA never dereferences, copies or frees it -- and it stays
 * valid as long as the caller keeps it valid.
 *
 * @p out_has_tag is false, with a success result, when the model has no tag or carries one of
 * another shape.
 */
CNA_C_API CNA_Result cna_model_get_content_tag_foreign_object_ext(
    CNA_ModelHandle model,
    CNA_Bool* out_has_tag,
    void** out_object);

#ifdef __cplusplus
}
#endif

#endif
