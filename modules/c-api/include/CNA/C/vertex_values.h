// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_VERTEX_VALUES_H
#define CNA_C_VERTEX_VALUES_H

#include "CNA/C/graphics3d.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity for a built-in CNA vertex value. */
typedef uint32_t CNA_VertexType;

/** @brief Selects a position-and-color vertex. */
#define CNA_VERTEX_TYPE_POSITION_COLOR UINT32_C(0)
/** @brief Selects a position, color and texture-coordinate vertex. */
#define CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE UINT32_C(1)
/** @brief Selects a position, normal, tangent and texture-coordinate vertex. */
#define CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE UINT32_C(2)
/** @brief Selects the skinned position, normal, tangent and texture-coordinate vertex extension. */
#define CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED UINT32_C(3)
/** @brief Selects a position, normal and texture-coordinate vertex. */
#define CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE UINT32_C(4)
/** @brief Selects the skinned position, normal and texture-coordinate vertex extension. */
#define CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED UINT32_C(5)
/** @brief Selects a position-and-texture-coordinate vertex. */
#define CNA_VERTEX_TYPE_POSITION_TEXTURE UINT32_C(6)

/** @brief Position-and-color vertex fields. */
typedef struct CNA_VertexPositionColor {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Per-vertex color. */
    CNA_Color color;
} CNA_VertexPositionColor;

/** @brief Position, color and texture-coordinate vertex fields. */
typedef struct CNA_VertexPositionColorTexture {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Per-vertex color. */
    CNA_Color color;
    /** @brief Texture coordinates. */
    CNA_Vector2 texture_coordinate;
} CNA_VertexPositionColorTexture;

/** @brief Position, normal, tangent and texture-coordinate vertex fields. */
typedef struct CNA_VertexPositionNormalTangentTexture {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Surface normal. */
    CNA_Vector3 normal;
    /** @brief Surface tangent and bitangent handedness. */
    CNA_Vector4 tangent;
    /** @brief Texture coordinates. */
    CNA_Vector2 texture_coordinate;
} CNA_VertexPositionNormalTangentTexture;

/** @brief Skinned position, normal, tangent and texture-coordinate vertex extension fields. */
typedef struct CNA_VertexPositionNormalTangentTextureSkinned {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Surface normal. */
    CNA_Vector3 normal;
    /** @brief Surface tangent and bitangent handedness. */
    CNA_Vector4 tangent;
    /** @brief Texture coordinates. */
    CNA_Vector2 texture_coordinate;
    /** @brief Weights for up to four bones. */
    CNA_Vector4 blend_weight;
    /** @brief Indices for up to four bones. */
    uint8_t blend_indices[4];
} CNA_VertexPositionNormalTangentTextureSkinned;

/** @brief Position, normal and texture-coordinate vertex fields. */
typedef struct CNA_VertexPositionNormalTexture {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Surface normal. */
    CNA_Vector3 normal;
    /** @brief Texture coordinates. */
    CNA_Vector2 texture_coordinate;
} CNA_VertexPositionNormalTexture;

/** @brief Skinned position, normal and texture-coordinate vertex extension fields. */
typedef struct CNA_VertexPositionNormalTextureSkinned {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Surface normal. */
    CNA_Vector3 normal;
    /** @brief Texture coordinates. */
    CNA_Vector2 texture_coordinate;
    /** @brief Weights for up to four bones. */
    CNA_Vector4 blend_weight;
    /** @brief Indices for up to four bones. */
    uint8_t blend_indices[4];
} CNA_VertexPositionNormalTextureSkinned;

/** @brief Position-and-texture-coordinate vertex fields. */
typedef struct CNA_VertexPositionTexture {
    /** @brief Position in object space. */
    CNA_Vector3 position;
    /** @brief Texture coordinates. */
    CNA_Vector2 texture_coordinate;
} CNA_VertexPositionTexture;

/** @brief Tagged-operation storage for any built-in CNA vertex value. */
typedef union CNA_VertexValue {
    /** @brief Position-and-color value. */
    CNA_VertexPositionColor position_color;
    /** @brief Position, color and texture-coordinate value. */
    CNA_VertexPositionColorTexture position_color_texture;
    /** @brief Position, normal, tangent and texture-coordinate value. */
    CNA_VertexPositionNormalTangentTexture position_normal_tangent_texture;
    /** @brief Skinned position, normal, tangent and texture-coordinate extension value. */
    CNA_VertexPositionNormalTangentTextureSkinned position_normal_tangent_texture_skinned;
    /** @brief Position, normal and texture-coordinate value. */
    CNA_VertexPositionNormalTexture position_normal_texture;
    /** @brief Skinned position, normal and texture-coordinate extension value. */
    CNA_VertexPositionNormalTextureSkinned position_normal_texture_skinned;
    /** @brief Position-and-texture-coordinate value. */
    CNA_VertexPositionTexture position_texture;
} CNA_VertexValue;

/**
 * @brief Initializes the canonical default value for a built-in vertex type.
 *
 * @param type Built-in vertex type identity.
 * @param out_value Receives the default value in the matching union member.
 * @return A CNA result code; failure does not overwrite @p out_value.
 */
CNA_C_API CNA_Result cna_vertex_value_init_default(
    CNA_VertexType type,
    CNA_VertexValue* out_value);

/**
 * @brief Tests two built-in vertex values for equality.
 *
 * @param type Built-in vertex type identity and active union member.
 * @param left First vertex value.
 * @param right Second vertex value.
 * @param out_equal Receives the equality result.
 * @return A CNA result code; failure does not overwrite @p out_equal.
 */
CNA_C_API CNA_Result cna_vertex_value_equals(
    CNA_VertexType type,
    const CNA_VertexValue* left,
    const CNA_VertexValue* right,
    CNA_Bool* out_equal);

/**
 * @brief Tests two built-in vertex values for inequality.
 *
 * @param type Built-in vertex type identity and active union member.
 * @param left First vertex value.
 * @param right Second vertex value.
 * @param out_not_equal Receives the inequality result.
 * @return A CNA result code; failure does not overwrite @p out_not_equal.
 */
CNA_C_API CNA_Result cna_vertex_value_not_equals(
    CNA_VertexType type,
    const CNA_VertexValue* left,
    const CNA_VertexValue* right,
    CNA_Bool* out_not_equal);

/**
 * @brief Computes the canonical hash code for a built-in vertex value.
 *
 * @param type Built-in vertex type identity and active union member.
 * @param value Vertex value.
 * @param out_hash Receives the hash code.
 * @return A CNA result code; failure does not overwrite @p out_hash.
 */
CNA_C_API CNA_Result cna_vertex_value_get_hash_code(
    CNA_VertexType type,
    const CNA_VertexValue* value,
    int32_t* out_hash);

/**
 * @brief Gets the exact byte count of a built-in vertex value's formatted string.
 *
 * @param type Built-in vertex type identity and active union member.
 * @param value Vertex value.
 * @param out_byte_count Receives the UTF-8 byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_vertex_value_get_string_byte_count(
    CNA_VertexType type,
    const CNA_VertexValue* value,
    uint64_t* out_byte_count);

/**
 * @brief Copies a built-in vertex value's formatted string without a terminator.
 *
 * @param type Built-in vertex type identity and active union member.
 * @param value Vertex value.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required UTF-8 byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_value_copy_string(
    CNA_VertexType type,
    const CNA_VertexValue* value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the packed GPU stream stride for a built-in vertex type.
 *
 * @param type Built-in vertex type identity.
 * @param out_stride Receives the stride in bytes.
 * @return A CNA result code; failure does not overwrite @p out_stride.
 */
CNA_C_API CNA_Result cna_vertex_type_get_stride(
    CNA_VertexType type,
    uint32_t* out_stride);

/**
 * @brief Copies the canonical declaration elements for a built-in vertex type.
 *
 * @param type Built-in vertex type identity.
 * @param destination Destination elements, or null only for zero capacity.
 * @param capacity Destination capacity measured in elements.
 * @param out_element_count Receives the required element count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_type_copy_elements(
    CNA_VertexType type,
    CNA_VertexElement* destination,
    uint64_t capacity,
    uint64_t* out_element_count);

/**
 * @brief Tests two vertex declaration elements for equality.
 *
 * @param left First element.
 * @param right Second element.
 * @param out_equal Receives the equality result.
 * @return A CNA result code; invalid identities or null output preserve @p out_equal.
 */
CNA_C_API CNA_Result cna_vertex_element_equals(
    CNA_VertexElement left,
    CNA_VertexElement right,
    CNA_Bool* out_equal);

/**
 * @brief Tests two vertex declaration elements for inequality.
 *
 * @param left First element.
 * @param right Second element.
 * @param out_not_equal Receives the inequality result.
 * @return A CNA result code; invalid identities or null output preserve @p out_not_equal.
 */
CNA_C_API CNA_Result cna_vertex_element_not_equals(
    CNA_VertexElement left,
    CNA_VertexElement right,
    CNA_Bool* out_not_equal);

/**
 * @brief Computes the canonical hash code for a vertex declaration element.
 *
 * @param value Vertex element.
 * @param out_hash Receives the hash code.
 * @return A CNA result code; failure does not overwrite @p out_hash.
 */
CNA_C_API CNA_Result cna_vertex_element_get_hash_code(
    CNA_VertexElement value,
    int32_t* out_hash);

/**
 * @brief Gets the exact byte count of a vertex element's formatted string.
 *
 * @param value Vertex element.
 * @param out_byte_count Receives the UTF-8 byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_vertex_element_get_string_byte_count(
    CNA_VertexElement value,
    uint64_t* out_byte_count);

/**
 * @brief Copies a vertex element's formatted string without a terminator.
 *
 * @param value Vertex element.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required UTF-8 byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_element_copy_string(
    CNA_VertexElement value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

#ifdef __cplusplus
}
#endif

#endif
