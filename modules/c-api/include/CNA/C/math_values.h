// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MATH_VALUES_H
#define CNA_C_MATH_VALUES_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width containment classification. */
typedef uint32_t CNA_ContainmentType;
/** @brief Bounding volumes do not overlap. */
#define CNA_CONTAINMENT_DISJOINT UINT32_C(0)
/** @brief One bounding volume contains the other. */
#define CNA_CONTAINMENT_CONTAINS UINT32_C(1)
/** @brief Bounding volumes partially overlap. */
#define CNA_CONTAINMENT_INTERSECTS UINT32_C(2)

/** @brief Fixed-width plane-intersection classification. */
typedef uint32_t CNA_PlaneIntersectionType;
/** @brief The volume is in the plane's front half-space. */
#define CNA_PLANE_INTERSECTION_FRONT UINT32_C(0)
/** @brief The volume is in the plane's back half-space. */
#define CNA_PLANE_INTERSECTION_BACK UINT32_C(1)
/** @brief The plane intersects the volume. */
#define CNA_PLANE_INTERSECTION_INTERSECTING UINT32_C(2)

/** @brief Fixed-width curve continuity identity. */
typedef uint32_t CNA_CurveContinuity;
/** @brief Smooth interpolation is permitted. */
#define CNA_CURVE_CONTINUITY_SMOOTH UINT32_C(0)
/** @brief The preceding key value is held as a step. */
#define CNA_CURVE_CONTINUITY_STEP UINT32_C(1)

/** @brief Fixed-width curve loop identity. */
typedef uint32_t CNA_CurveLoopType;
/** @brief Clamp to the nearest endpoint. */
#define CNA_CURVE_LOOP_CONSTANT UINT32_C(0)
/** @brief Repeat the curve. */
#define CNA_CURVE_LOOP_CYCLE UINT32_C(1)
/** @brief Repeat and offset each cycle. */
#define CNA_CURVE_LOOP_CYCLE_OFFSET UINT32_C(2)
/** @brief Alternate traversal direction each cycle. */
#define CNA_CURVE_LOOP_OSCILLATE UINT32_C(3)
/** @brief Extrapolate linearly. */
#define CNA_CURVE_LOOP_LINEAR UINT32_C(4)

/** @brief Fixed-width curve tangent identity. */
typedef uint32_t CNA_CurveTangent;
/** @brief Use a zero tangent. */
#define CNA_CURVE_TANGENT_FLAT UINT32_C(0)
/** @brief Use a linear tangent. */
#define CNA_CURVE_TANGENT_LINEAR UINT32_C(1)
/** @brief Use a neighbor-aware smooth tangent. */
#define CNA_CURVE_TANGENT_SMOOTH UINT32_C(2)

/** @brief Two-component signed integer point. */
typedef struct CNA_Point {
    /** @brief Horizontal component. */
    int32_t x;
    /** @brief Vertical component. */
    int32_t y;
} CNA_Point;

/** @brief Four-component single-precision vector. */
typedef struct CNA_Vector4 {
    /** @brief X component. */
    float x;
    /** @brief Y component. */
    float y;
    /** @brief Z component. */
    float z;
    /** @brief W component. */
    float w;
} CNA_Vector4;

/** @brief Four-component single-precision quaternion. */
typedef struct CNA_Quaternion {
    /** @brief X component. */
    float x;
    /** @brief Y component. */
    float y;
    /** @brief Z component. */
    float z;
    /** @brief W component. */
    float w;
} CNA_Quaternion;

/** @brief Row-major four-by-four single-precision matrix. */
typedef struct CNA_Matrix {
    /** @brief Row 1, column 1. */
    float m11;
    /** @brief Row 1, column 2. */
    float m12;
    /** @brief Row 1, column 3. */
    float m13;
    /** @brief Row 1, column 4. */
    float m14;
    /** @brief Row 2, column 1. */
    float m21;
    /** @brief Row 2, column 2. */
    float m22;
    /** @brief Row 2, column 3. */
    float m23;
    /** @brief Row 2, column 4. */
    float m24;
    /** @brief Row 3, column 1. */
    float m31;
    /** @brief Row 3, column 2. */
    float m32;
    /** @brief Row 3, column 3. */
    float m33;
    /** @brief Row 3, column 4. */
    float m34;
    /** @brief Row 4, column 1. */
    float m41;
    /** @brief Row 4, column 2. */
    float m42;
    /** @brief Row 4, column 3. */
    float m43;
    /** @brief Row 4, column 4. */
    float m44;
} CNA_Matrix;

/** @brief Plane represented by a normal and distance. */
typedef struct CNA_Plane {
    /** @brief Plane normal. */
    CNA_Vector3 normal;
    /** @brief Distance from the origin along the normal. */
    float d;
} CNA_Plane;

/** @brief Ray represented by an origin and direction. */
typedef struct CNA_Ray {
    /** @brief Ray origin. */
    CNA_Vector3 position;
    /** @brief Ray direction. */
    CNA_Vector3 direction;
} CNA_Ray;

/** @brief Axis-aligned bounding box represented by minimum and maximum corners. */
typedef struct CNA_BoundingBox {
    /** @brief Minimum corner. */
    CNA_Vector3 min;
    /** @brief Maximum corner. */
    CNA_Vector3 max;
} CNA_BoundingBox;

/** @brief Bounding sphere represented by center and radius. */
typedef struct CNA_BoundingSphere {
    /** @brief Sphere center. */
    CNA_Vector3 center;
    /** @brief Sphere radius. */
    float radius;
} CNA_BoundingSphere;

/** @brief Bounding frustum represented by its defining matrix. */
typedef struct CNA_BoundingFrustum {
    /** @brief Matrix from which native frustum planes and corners are derived. */
    CNA_Matrix matrix;
} CNA_BoundingFrustum;

/** @brief Raw packed storage for Alpha8. */
typedef uint8_t CNA_PackedAlpha8;
/** @brief Raw packed storage for Bgr565. */
typedef uint16_t CNA_PackedBgr565;
/** @brief Raw packed storage for Bgra4444. */
typedef uint16_t CNA_PackedBgra4444;
/** @brief Raw packed storage for Bgra5551. */
typedef uint16_t CNA_PackedBgra5551;
/** @brief Raw packed storage for Byte4. */
typedef uint32_t CNA_PackedByte4;
/** @brief Raw packed storage for HalfSingle. */
typedef uint16_t CNA_PackedHalfSingle;
/** @brief Raw packed storage for HalfVector2. */
typedef uint32_t CNA_PackedHalfVector2;
/** @brief Raw packed storage for HalfVector4. */
typedef uint64_t CNA_PackedHalfVector4;
/** @brief Raw packed storage for NormalizedByte2. */
typedef uint16_t CNA_PackedNormalizedByte2;
/** @brief Raw packed storage for NormalizedByte4. */
typedef uint32_t CNA_PackedNormalizedByte4;
/** @brief Raw packed storage for NormalizedShort2. */
typedef uint32_t CNA_PackedNormalizedShort2;
/** @brief Raw packed storage for NormalizedShort4. */
typedef uint64_t CNA_PackedNormalizedShort4;
/** @brief Raw packed storage for Rg32. */
typedef uint32_t CNA_PackedRg32;
/** @brief Raw packed storage for Rgba1010102. */
typedef uint32_t CNA_PackedRgba1010102;
/** @brief Raw packed storage for Rgba64. */
typedef uint64_t CNA_PackedRgba64;
/** @brief Raw packed storage for Short2. */
typedef uint32_t CNA_PackedShort2;
/** @brief Raw packed storage for Short4. */
typedef uint64_t CNA_PackedShort4;

#ifdef __cplusplus
}
#endif

#endif
