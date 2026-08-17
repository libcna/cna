// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS3D_H
#define CNA_C_GRAPHICS3D_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width graphics-buffer usage identity. */
typedef uint32_t CNA_BufferUsage;
/** @brief No special buffer usage. */
#define CNA_BUFFER_USAGE_NONE UINT32_C(0)
/** @brief Buffer optimized for rendering and writes. */
#define CNA_BUFFER_USAGE_WRITE_ONLY UINT32_C(1)

/** @brief Fixed-width index-element size identity. */
typedef uint32_t CNA_IndexElementSize;
/** @brief 16-bit index elements. */
#define CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS UINT32_C(0)
/** @brief 32-bit index elements. */
#define CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS UINT32_C(1)

/** @brief Fixed-width primitive-topology identity. */
typedef uint32_t CNA_PrimitiveType;
/** @brief Independent triangles. */
#define CNA_PRIMITIVE_TRIANGLE_LIST UINT32_C(0)
/** @brief Connected triangle strip. */
#define CNA_PRIMITIVE_TRIANGLE_STRIP UINT32_C(1)
/** @brief Independent line segments. */
#define CNA_PRIMITIVE_LINE_LIST UINT32_C(2)
/** @brief Connected line strip. */
#define CNA_PRIMITIVE_LINE_STRIP UINT32_C(3)
/** @brief CNA point-list extension. */
#define CNA_PRIMITIVE_POINT_LIST_EXT UINT32_C(4)

/** @brief Fixed-width SetData update identity. */
typedef uint32_t CNA_SetDataOptions;
/** @brief May overwrite existing buffer data. */
#define CNA_SET_DATA_NONE UINT32_C(0)
/** @brief Discards the previous buffer contents. */
#define CNA_SET_DATA_DISCARD UINT32_C(1)
/** @brief Promises not to overwrite data still in use. */
#define CNA_SET_DATA_NO_OVERWRITE UINT32_C(2)

/** @brief Fixed-width vertex-element storage identity. */
typedef uint32_t CNA_VertexElementFormat;
/** @brief One 32-bit float. */
#define CNA_VERTEX_ELEMENT_FORMAT_SINGLE UINT32_C(0)
/** @brief Two 32-bit floats. */
#define CNA_VERTEX_ELEMENT_FORMAT_VECTOR2 UINT32_C(1)
/** @brief Three 32-bit floats. */
#define CNA_VERTEX_ELEMENT_FORMAT_VECTOR3 UINT32_C(2)
/** @brief Four 32-bit floats. */
#define CNA_VERTEX_ELEMENT_FORMAT_VECTOR4 UINT32_C(3)
/** @brief Packed BGRA color. */
#define CNA_VERTEX_ELEMENT_FORMAT_COLOR UINT32_C(4)
/** @brief Four unsigned bytes. */
#define CNA_VERTEX_ELEMENT_FORMAT_BYTE4 UINT32_C(5)
/** @brief Two signed 16-bit integers. */
#define CNA_VERTEX_ELEMENT_FORMAT_SHORT2 UINT32_C(6)
/** @brief Four signed 16-bit integers. */
#define CNA_VERTEX_ELEMENT_FORMAT_SHORT4 UINT32_C(7)
/** @brief Two normalized signed 16-bit integers. */
#define CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT2 UINT32_C(8)
/** @brief Four normalized signed 16-bit integers. */
#define CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT4 UINT32_C(9)
/** @brief Two 16-bit floats. */
#define CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR2 UINT32_C(10)
/** @brief Four 16-bit floats. */
#define CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4 UINT32_C(11)

/** @brief Fixed-width vertex semantic identity. */
typedef uint32_t CNA_VertexElementUsage;
/** @brief Position data. */
#define CNA_VERTEX_ELEMENT_USAGE_POSITION UINT32_C(0)
/** @brief Color data. */
#define CNA_VERTEX_ELEMENT_USAGE_COLOR UINT32_C(1)
/** @brief Texture-coordinate or user data. */
#define CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE UINT32_C(2)
/** @brief Normal data. */
#define CNA_VERTEX_ELEMENT_USAGE_NORMAL UINT32_C(3)
/** @brief Binormal data. */
#define CNA_VERTEX_ELEMENT_USAGE_BINORMAL UINT32_C(4)
/** @brief Tangent data. */
#define CNA_VERTEX_ELEMENT_USAGE_TANGENT UINT32_C(5)
/** @brief Blend-index data. */
#define CNA_VERTEX_ELEMENT_USAGE_BLEND_INDICES UINT32_C(6)
/** @brief Blend-weight data. */
#define CNA_VERTEX_ELEMENT_USAGE_BLEND_WEIGHT UINT32_C(7)
/** @brief Depth data. */
#define CNA_VERTEX_ELEMENT_USAGE_DEPTH UINT32_C(8)
/** @brief Fog data. */
#define CNA_VERTEX_ELEMENT_USAGE_FOG UINT32_C(9)
/** @brief Point-size data. */
#define CNA_VERTEX_ELEMENT_USAGE_POINT_SIZE UINT32_C(10)
/** @brief Displacement-sampler data. */
#define CNA_VERTEX_ELEMENT_USAGE_SAMPLE UINT32_C(11)
/** @brief Tessellation-factor data. */
#define CNA_VERTEX_ELEMENT_USAGE_TESSELLATE_FACTOR UINT32_C(12)

/** @brief Fixed-layout vertex-element declaration entry. */
typedef struct CNA_VertexElement {
    /** @brief Byte offset from the start of one vertex. */
    int32_t offset;
    /** @brief Element storage format. */
    CNA_VertexElementFormat format;
    /** @brief Shader semantic. */
    CNA_VertexElementUsage usage;
    /** @brief Zero-based semantic usage index. */
    int32_t usage_index;
} CNA_VertexElement;

#ifdef __cplusplus
}
#endif

#endif
