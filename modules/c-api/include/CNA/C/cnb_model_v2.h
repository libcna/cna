// SPDX-License-Identifier: MS-PL
#ifndef CNA_C_CNB_MODEL_V2_H
#define CNA_C_CNB_MODEL_V2_H
#include "CNA/C/cnb.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @file cnb_model_v2.h
 * @brief CPU-only schema-2 model snapshots, independent of schema-1 models and GPU resources.
 *
 * Creation copies every input array and UTF-8 string. Snapshots are immutable, independently
 * owned, and must be destroyed once on their creation thread. All handle operations require
 * that thread. No pointer into a snapshot escapes. Resource indices and declaration order are
 * preserved exactly, including duplicate declarations. Element and part arrays use element
 * counts; raw buffers use bytes. Indices are unsigned little-endian values of width 2 or 4.
 *
 * Every structure has struct_size >= sizeof(type), struct_version = 1. Initialize output info
 * prefixes before querying. Required outputs are checked first and remain unchanged on failure;
 * failed handle outputs are invalid. Copy calls report the full count, write no prefix on short
 * capacity, and accept null destinations only at zero capacity. Text excludes any terminator.
 * Input strings are valid UTF-8 without embedded NUL. Input arrays live only through the call.
 * Input array counts must fit both uint32_t schema counts and host-size byte extents; otherwise
 * creation/comparison returns OVERFLOW before accessing the array.
 *
 * Descriptor equality is pure and any-thread; it compares native semantic fields, never padding.
 * Snapshot creation validates descriptor shapes/identities but permits an unfinished graph;
 * encoding performs the canonical graph, resource, effect and draw-window validation.
 * Bone name_is_null is preserved in memory and equality, but schema 2 does not serialize it:
 * decoding preserves the stored name bytes and sets name_is_null false. Bounds are authored, not recomputed.
 */
/** @brief Owned immutable CPU schema-2 graph. */
typedef CNA_Handle CNA_CnbModelV2Handle;
/** @brief Stable stock-effect discriminator. */
typedef uint32_t CNA_CnbModelV2EffectKind;
/** @brief Schema-2 schema version identity. */
#define CNA_CNB_MODEL_V2_SCHEMA_VERSION UINT32_C(2)
/** @brief Schema-2 header stride identity. */
#define CNA_CNB_MODEL_V2_HEADER_STRIDE UINT32_C(64)
/** @brief Schema-2 bone stride identity. */
#define CNA_CNB_MODEL_V2_BONE_STRIDE UINT32_C(72)
/** @brief Schema-2 mesh stride identity. */
#define CNA_CNB_MODEL_V2_MESH_STRIDE UINT32_C(32)
/** @brief Schema-2 part stride identity. */
#define CNA_CNB_MODEL_V2_PART_STRIDE UINT32_C(32)
/** @brief Schema-2 declaration stride identity. */
#define CNA_CNB_MODEL_V2_DECLARATION_STRIDE UINT32_C(16)
/** @brief Schema-2 element stride identity. */
#define CNA_CNB_MODEL_V2_ELEMENT_STRIDE UINT32_C(20)
/** @brief Schema-2 resource stride identity. */
#define CNA_CNB_MODEL_V2_RESOURCE_STRIDE UINT32_C(16)
/** @brief Schema-2 effect stride identity. */
#define CNA_CNB_MODEL_V2_EFFECT_STRIDE UINT32_C(96)
/** @brief Schema-2 no index identity. */
#define CNA_CNB_MODEL_V2_NO_INDEX UINT32_C(4294967295)
/** @brief Schema-2 chunk header identity. */
#define CNA_CNB_MODEL_V2_CHUNK_HEADER UINT32_C(1145582157)
/** @brief Schema-2 chunk strings identity. */
#define CNA_CNB_MODEL_V2_CHUNK_STRINGS UINT32_C(1414738509)
/** @brief Schema-2 chunk bones identity. */
#define CNA_CNB_MODEL_V2_CHUNK_BONES UINT32_C(1312961101)
/** @brief Schema-2 chunk meshes identity. */
#define CNA_CNB_MODEL_V2_CHUNK_MESHES UINT32_C(1397568077)
/** @brief Schema-2 chunk parts identity. */
#define CNA_CNB_MODEL_V2_CHUNK_PARTS UINT32_C(1414541901)
/** @brief Schema-2 chunk vertex declarations identity. */
#define CNA_CNB_MODEL_V2_CHUNK_VERTEX_DECLARATIONS UINT32_C(1146499661)
/** @brief Schema-2 chunk vertex resources identity. */
#define CNA_CNB_MODEL_V2_CHUNK_VERTEX_RESOURCES UINT32_C(1381380685)
/** @brief Schema-2 chunk vertex data identity. */
#define CNA_CNB_MODEL_V2_CHUNK_VERTEX_DATA UINT32_C(1481922125)
/** @brief Schema-2 chunk index resources identity. */
#define CNA_CNB_MODEL_V2_CHUNK_INDEX_RESOURCES UINT32_C(1380528717)
/** @brief Schema-2 chunk index data identity. */
#define CNA_CNB_MODEL_V2_CHUNK_INDEX_DATA UINT32_C(1480870221)
/** @brief Schema-2 chunk effects identity. */
#define CNA_CNB_MODEL_V2_CHUNK_EFFECTS UINT32_C(1480995405)
/** @brief Schema-2 effect basic identity. */
#define CNA_CNB_MODEL_V2_EFFECT_BASIC UINT32_C(0)
/** @brief Schema-2 effect skinned identity. */
#define CNA_CNB_MODEL_V2_EFFECT_SKINNED UINT32_C(1)
/** @brief Schema-2 effect dual texture identity. */
#define CNA_CNB_MODEL_V2_EFFECT_DUAL_TEXTURE UINT32_C(2)
/** @brief Schema-2 effect alpha test identity. */
#define CNA_CNB_MODEL_V2_EFFECT_ALPHA_TEST UINT32_C(3)
/** @brief Schema-2 effect environment map identity. */
#define CNA_CNB_MODEL_V2_EFFECT_ENVIRONMENT_MAP UINT32_C(4)
/** @brief Copied schema-2 element values and counts. */
typedef struct CNA_CnbModelV2Element {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored offset. */
    uint32_t offset;
    /** @brief Authored format. */
    CNA_VertexElementFormat format;
    /** @brief Authored usage. */
    CNA_VertexElementUsage usage;
    /** @brief Authored usage index. */
    uint32_t usage_index;
} CNA_CnbModelV2Element;
/** @brief Copied schema-2 declaration values and counts. */
typedef struct CNA_CnbModelV2DeclarationInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored vertex stride. */
    uint32_t vertex_stride;
    /** @brief Authored element count. */
    uint64_t element_count;
} CNA_CnbModelV2DeclarationInfo;
/** @brief Call-only schema-2 declaration input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2DeclarationDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2DeclarationInfo info;
    /** @brief element_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2Element* elements;
} CNA_CnbModelV2DeclarationDesc;
/** @brief Copied schema-2 vertex buffer values and counts. */
typedef struct CNA_CnbModelV2VertexBufferInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored declaration. */
    uint32_t declaration;
    /** @brief Authored vertex count. */
    uint32_t vertex_count;
    /** @brief Authored byte count. */
    uint64_t byte_count;
} CNA_CnbModelV2VertexBufferInfo;
/** @brief Call-only schema-2 vertex_buffer input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2VertexBufferDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2VertexBufferInfo info;
    /** @brief byte_count readable entries, null only when the count is zero. */
    const uint8_t* bytes;
} CNA_CnbModelV2VertexBufferDesc;
/** @brief Copied schema-2 index buffer values and counts. */
typedef struct CNA_CnbModelV2IndexBufferInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored index element size. */
    uint32_t index_element_size;
    /** @brief Authored index count. */
    uint32_t index_count;
    /** @brief Authored byte count. */
    uint64_t byte_count;
} CNA_CnbModelV2IndexBufferInfo;
/** @brief Call-only schema-2 index_buffer input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2IndexBufferDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2IndexBufferInfo info;
    /** @brief byte_count readable entries, null only when the count is zero. */
    const uint8_t* bytes;
} CNA_CnbModelV2IndexBufferDesc;
/** @brief Copied schema-2 effect values and counts. */
typedef struct CNA_CnbModelV2EffectInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored kind. */
    CNA_CnbModelV2EffectKind kind;
    /** @brief Authored diffuse. */
    float diffuse[3];
    /** @brief Authored emissive. */
    float emissive[3];
    /** @brief Authored specular. */
    float specular[3];
    /** @brief Authored specular power. */
    float specular_power;
    /** @brief Authored alpha. */
    float alpha;
    /** @brief Authored environment map amount. */
    float environment_map_amount;
    /** @brief Authored fresnel factor. */
    float fresnel_factor;
    /** @brief Authored weights per vertex. */
    uint32_t weights_per_vertex;
    /** @brief Authored alpha function. */
    uint32_t alpha_function;
    /** @brief Authored reference alpha. */
    uint32_t reference_alpha;
    /** @brief Authored vertex color enabled. */
    CNA_Bool vertex_color_enabled;
} CNA_CnbModelV2EffectInfo;
/** @brief Call-only schema-2 effect input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2EffectDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2EffectInfo info;
    /** @brief Call-only UTF-8 primary texture; no embedded NUL. */
    CNA_StringView primary_texture;
    /** @brief Call-only UTF-8 secondary texture; no embedded NUL. */
    CNA_StringView secondary_texture;
    /** @brief Call-only UTF-8 cube texture; no embedded NUL. */
    CNA_StringView cube_texture;
} CNA_CnbModelV2EffectDesc;
/** @brief Copied schema-2 bone values and counts. */
typedef struct CNA_CnbModelV2BoneInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored name is null. */
    CNA_Bool name_is_null;
    /** @brief Authored parent. */
    int32_t parent;
    /** @brief Authored transform. */
    float transform[16];
} CNA_CnbModelV2BoneInfo;
/** @brief Call-only schema-2 bone input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2BoneDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2BoneInfo info;
    /** @brief Call-only UTF-8 name; no embedded NUL. */
    CNA_StringView name;
} CNA_CnbModelV2BoneDesc;
/** @brief Copied schema-2 part values and counts. */
typedef struct CNA_CnbModelV2Part {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored vertex offset. */
    uint32_t vertex_offset;
    /** @brief Authored num vertices. */
    uint32_t num_vertices;
    /** @brief Authored start index. */
    uint32_t start_index;
    /** @brief Authored primitive count. */
    uint32_t primitive_count;
    /** @brief Authored vertex buffer. */
    uint32_t vertex_buffer;
    /** @brief Authored index buffer. */
    uint32_t index_buffer;
    /** @brief Authored effect. */
    uint32_t effect;
} CNA_CnbModelV2Part;
/** @brief Copied schema-2 mesh values and counts. */
typedef struct CNA_CnbModelV2MeshInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored parent bone. */
    int32_t parent_bone;
    /** @brief Authored bounding sphere. */
    float bounding_sphere[4];
    /** @brief Authored part count. */
    uint64_t part_count;
} CNA_CnbModelV2MeshInfo;
/** @brief Call-only schema-2 mesh input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2MeshDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2MeshInfo info;
    /** @brief part_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2Part* parts;
    /** @brief Call-only UTF-8 name; no embedded NUL. */
    CNA_StringView name;
} CNA_CnbModelV2MeshDesc;
/** @brief Copied schema-2 document values and counts. */
typedef struct CNA_CnbModelV2DocumentInfo {
    /** @brief Caller structure size in bytes. */
    uint32_t struct_size;
    /** @brief Structure version, 1. */
    uint32_t struct_version;
    /** @brief Authored root bone. */
    uint32_t root_bone;
    /** @brief Authored bone count. */
    uint64_t bone_count;
    /** @brief Authored mesh count. */
    uint64_t mesh_count;
    /** @brief Authored declaration count. */
    uint64_t declaration_count;
    /** @brief Authored vertex buffer count. */
    uint64_t vertex_buffer_count;
    /** @brief Authored index buffer count. */
    uint64_t index_buffer_count;
    /** @brief Authored effect count. */
    uint64_t effect_count;
} CNA_CnbModelV2DocumentInfo;
/** @brief Call-only schema-2 document input; all pointed-to data is copied. */
typedef struct CNA_CnbModelV2DocumentDesc {
    /** @brief Full descriptor size in bytes, including pointers and views. */
    uint32_t struct_size;
    /** @brief Descriptor version, 1. */
    uint32_t struct_version;
    /** @brief Versioned values and array counts. */
    CNA_CnbModelV2DocumentInfo info;
    /** @brief bone_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2BoneDesc* bones;
    /** @brief mesh_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2MeshDesc* meshes;
    /** @brief declaration_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2DeclarationDesc* declarations;
    /** @brief vertex_buffer_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2VertexBufferDesc* vertex_buffers;
    /** @brief index_buffer_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2IndexBufferDesc* index_buffers;
    /** @brief effect_count readable entries, null only when the count is zero. */
    const CNA_CnbModelV2EffectDesc* effects;
} CNA_CnbModelV2DocumentDesc;
/**
 * @brief Compares every element field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_element_equals(
    const CNA_CnbModelV2Element* a,
    const CNA_CnbModelV2Element* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every declaration field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_declaration_equals(
    const CNA_CnbModelV2DeclarationDesc* a,
    const CNA_CnbModelV2DeclarationDesc* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every vertex_buffer field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_vertex_buffer_equals(
    const CNA_CnbModelV2VertexBufferDesc* a,
    const CNA_CnbModelV2VertexBufferDesc* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every index_buffer field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_index_buffer_equals(
    const CNA_CnbModelV2IndexBufferDesc* a,
    const CNA_CnbModelV2IndexBufferDesc* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every effect field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_effect_equals(
    const CNA_CnbModelV2EffectDesc* a,
    const CNA_CnbModelV2EffectDesc* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every bone field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_bone_equals(
    const CNA_CnbModelV2BoneDesc* a,
    const CNA_CnbModelV2BoneDesc* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every part field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_part_equals(
    const CNA_CnbModelV2Part* a,
    const CNA_CnbModelV2Part* b,
    CNA_Bool* out_equal);
/**
 * @brief Compares every mesh field, including ordered arrays and names.
 *
 * @param a First descriptor.
 * @param b Second descriptor.
 * @param out_equal Receives native semantic equality.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_mesh_equals(
    const CNA_CnbModelV2MeshDesc* a,
    const CNA_CnbModelV2MeshDesc* b,
    CNA_Bool* out_equal);
/**
 * @brief Copies a CPU model snapshot without changing declaration or resource identities.
 *
 * @param description Copied graph and resource tables.
 * @param out_model Receives an independently owned snapshot; invalid on failure.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_create(
    const CNA_CnbModelV2DocumentDesc* description,
    CNA_CnbModelV2Handle* out_model);
/**
 * @brief Releases a snapshot and invalidates its token.
 *
 * @param model Owned snapshot to release.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_destroy(
    CNA_CnbModelV2Handle model);
/**
 * @brief Copies document counts and root identity.
 *
 * @param model Snapshot.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_info(
    CNA_CnbModelV2Handle model,
    CNA_CnbModelV2DocumentInfo* out_info);
/**
 * @brief Copies declaration values.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_declaration(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2DeclarationInfo* out_info);
/**
 * @brief Copies an ordered elements array.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller buffer; no partial write.
 * @param capacity Capacity in elements.
 * @param out_count Receives required count even on short capacity.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_declaration_elements(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2Element* destination,
    uint64_t capacity,
    uint64_t* out_count);
/**
 * @brief Copies vertex_buffer values.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_vertex_buffer(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2VertexBufferInfo* out_info);
/**
 * @brief Copies an ordered bytes array.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller buffer; no partial write.
 * @param capacity Capacity in bytes.
 * @param out_count Receives required count even on short capacity.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_vertex_buffer_bytes(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_count);
/**
 * @brief Copies index_buffer values.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_index_buffer(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2IndexBufferInfo* out_info);
/**
 * @brief Copies an ordered bytes array.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller buffer; no partial write.
 * @param capacity Capacity in bytes.
 * @param out_count Receives required count even on short capacity.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_index_buffer_bytes(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_count);
/**
 * @brief Copies effect values.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_effect(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2EffectInfo* out_info);
/**
 * @brief Copies authored primary texture.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller text buffer; no partial write.
 * @param capacity Capacity in bytes, excluding terminator.
 * @param out_byte_count Receives required byte count.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_effect_primary_texture(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);
/**
 * @brief Copies authored secondary texture.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller text buffer; no partial write.
 * @param capacity Capacity in bytes, excluding terminator.
 * @param out_byte_count Receives required byte count.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_effect_secondary_texture(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);
/**
 * @brief Copies authored cube texture.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller text buffer; no partial write.
 * @param capacity Capacity in bytes, excluding terminator.
 * @param out_byte_count Receives required byte count.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_effect_cube_texture(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);
/**
 * @brief Copies bone values.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_bone(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2BoneInfo* out_info);
/**
 * @brief Copies authored name.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller text buffer; no partial write.
 * @param capacity Capacity in bytes, excluding terminator.
 * @param out_byte_count Receives required byte count.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_bone_name(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);
/**
 * @brief Copies mesh values.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param out_info Initialized versioned destination.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_get_mesh(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2MeshInfo* out_info);
/**
 * @brief Copies an ordered parts array.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller buffer; no partial write.
 * @param capacity Capacity in elements.
 * @param out_count Receives required count even on short capacity.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_mesh_parts(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    CNA_CnbModelV2Part* destination,
    uint64_t capacity,
    uint64_t* out_count);
/**
 * @brief Copies authored name.
 *
 * @param model Snapshot.
 * @param index Zero-based table index.
 * @param destination Caller text buffer; no partial write.
 * @param capacity Capacity in bytes, excluding terminator.
 * @param out_byte_count Receives required byte count.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_copy_mesh_name(
    CNA_CnbModelV2Handle model,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);
/**
 * @brief Compares every graph and resource field.
 *
 * @param a First snapshot.
 * @param b Second snapshot.
 * @param out_equal Receives native equality including null-name flags.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_equals(
    CNA_CnbModelV2Handle a,
    CNA_CnbModelV2Handle b,
    CNA_Bool* out_equal);
/**
 * @brief Validates and encodes deterministic schema-2 bytes; name_is_null flags are omitted.
 *
 * @param model Snapshot.
 * @param content_name Optional logical content name.
 * @param destination Caller byte buffer; no partial write.
 * @param capacity Capacity in bytes.
 * @param out_byte_count Receives full encoded size.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_encode(
    CNA_CnbModelV2Handle model,
    CNA_StringView content_name,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);
/**
 * @brief Decodes schema 2 without GPU resources; the container may then be destroyed.
 *
 * @param document Parsed container with its configured read limits.
 * @param out_model Receives an independent CPU snapshot; invalid on failure.
 * @return SUCCESS or the recorded argument, handle, thread, buffer or native failure.
 */
CNA_C_API CNA_Result cna_cnb_model_v2_decode(
    CNA_CnbDocumentHandle document,
    CNA_CnbModelV2Handle* out_model);

#ifdef __cplusplus
}
#endif
#endif
