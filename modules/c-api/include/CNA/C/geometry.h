// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GEOMETRY_H
#define CNA_C_GEOMETRY_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initializes a zero plane. @param out_value Receives the plane. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_init(CNA_Plane* out_value);
/** @brief Initializes a plane from Vector4. @param value XYZ normal and W distance. @param out_value Receives the plane. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_init_vector4(CNA_Vector4 value, CNA_Plane* out_value);
/** @brief Initializes a plane from normal and distance. @param normal Plane normal. @param d Plane distance. @param out_value Receives the plane. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_init_normal_d(
    CNA_Vector3 normal,
    float d,
    CNA_Plane* out_value);
/** @brief Initializes a plane through three points. @param a First point. @param b Second point. @param c Third point. @param out_value Receives the plane. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_init_points(
    CNA_Vector3 a,
    CNA_Vector3 b,
    CNA_Vector3 c,
    CNA_Plane* out_value);
/** @brief Initializes a plane from four coefficients. @param a Normal X. @param b Normal Y. @param c Normal Z. @param d Distance. @param out_value Receives the plane. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_init_abcd(
    float a,
    float b,
    float c,
    float d,
    CNA_Plane* out_value);
/** @brief Computes a plane/Vector4 dot product. @param plane Source plane. @param value Source vector. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_dot(
    CNA_Plane plane,
    CNA_Vector4 value,
    float* out_value);
/** @brief Computes normal dot coordinate plus D. @param plane Source plane. @param value Coordinate. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_dot_coordinate(
    CNA_Plane plane,
    CNA_Vector3 value,
    float* out_value);
/** @brief Computes a plane-normal dot product. @param plane Source plane. @param value Direction. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_dot_normal(
    CNA_Plane plane,
    CNA_Vector3 value,
    float* out_value);
/** @brief Normalizes a plane in place. @param value Plane to mutate. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_normalize_in_place(CNA_Plane* value);
/** @brief Classifies a box against a plane. @param plane Source plane. @param box Box to classify. @param out_intersection Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_intersects_box(
    CNA_Plane plane,
    CNA_BoundingBox box,
    CNA_PlaneIntersectionType* out_intersection);
/** @brief Classifies a sphere against a plane. @param plane Source plane. @param sphere Sphere to classify. @param out_intersection Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_intersects_sphere(
    CNA_Plane plane,
    CNA_BoundingSphere sphere,
    CNA_PlaneIntersectionType* out_intersection);
/** @brief Classifies a frustum against a plane. @param plane Source plane. @param frustum Frustum to classify. @param out_intersection Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_intersects_frustum(
    CNA_Plane plane,
    CNA_BoundingFrustum frustum,
    CNA_PlaneIntersectionType* out_intersection);
/** @brief Tests plane equality. @param left First plane. @param right Second plane. @param out_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_equals(CNA_Plane left, CNA_Plane right, CNA_Bool* out_equal);
/** @brief Tests plane inequality. @param left First plane. @param right Second plane. @param out_not_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_not_equals(
    CNA_Plane left,
    CNA_Plane right,
    CNA_Bool* out_not_equal);
/** @brief Computes a plane hash. @param value Source plane. @param out_hash Receives the hash. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_get_hash_code(CNA_Plane value, int32_t* out_hash);
/** @brief Gets a plane string byte count. @param value Plane to format. @param out_bytes Receives the count. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_get_string_size(CNA_Plane value, uint64_t* out_bytes);
/** @brief Copies the canonical plane UTF-8 string without a terminator. @param value Plane to format. @param destination Destination bytes, or null only for zero capacity. @param capacity Destination capacity. @param out_bytes Receives the required count. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_copy_string(
    CNA_Plane value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);
/** @brief Returns a normalized plane. @param value Source plane. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_normalize(CNA_Plane value, CNA_Plane* out_value);
/** @brief Transforms a plane by a matrix. @param value Source plane. @param matrix Transformation matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_transform_matrix(
    CNA_Plane value,
    CNA_Matrix matrix,
    CNA_Plane* out_value);
/** @brief Transforms a plane by a quaternion. @param value Source plane. @param rotation Quaternion rotation. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_plane_transform_quaternion(
    CNA_Plane value,
    CNA_Quaternion rotation,
    CNA_Plane* out_value);

/** @brief Initializes a zero ray. @param out_value Receives the ray. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_init(CNA_Ray* out_value);
/** @brief Initializes a ray. @param position Ray origin. @param direction Ray direction. @param out_value Receives the ray. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_init_position_direction(
    CNA_Vector3 position,
    CNA_Vector3 direction,
    CNA_Ray* out_value);
/** @brief Tests ray equality. @param left First ray. @param right Second ray. @param out_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_equals(CNA_Ray left, CNA_Ray right, CNA_Bool* out_equal);
/** @brief Tests ray inequality. @param left First ray. @param right Second ray. @param out_not_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_not_equals(CNA_Ray left, CNA_Ray right, CNA_Bool* out_not_equal);
/** @brief Computes a ray hash. @param value Source ray. @param out_hash Receives the hash. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_get_hash_code(CNA_Ray value, int32_t* out_hash);

/**
 * @brief Intersects a ray with a box.
 * @param ray Source ray.
 * @param box Box to test.
 * @param out_hit Receives whether an intersection exists.
 * @param out_distance Receives the distance, or zero when no intersection exists.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_ray_intersects_box(
    CNA_Ray ray,
    CNA_BoundingBox box,
    CNA_Bool* out_hit,
    float* out_distance);
/** @brief Intersects a ray with a sphere. @param ray Source ray. @param sphere Sphere to test. @param out_hit Receives whether an intersection exists. @param out_distance Receives the distance, or zero on no hit. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_intersects_sphere(
    CNA_Ray ray,
    CNA_BoundingSphere sphere,
    CNA_Bool* out_hit,
    float* out_distance);
/** @brief Intersects a ray with a plane. @param ray Source ray. @param plane Plane to test. @param out_hit Receives whether an intersection exists. @param out_distance Receives the distance, or zero on no hit. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_intersects_plane(
    CNA_Ray ray,
    CNA_Plane plane,
    CNA_Bool* out_hit,
    float* out_distance);
/** @brief Intersects a ray with a frustum. @param ray Source ray. @param frustum Frustum to test. @param out_hit Receives whether an intersection exists. @param out_distance Receives the distance, or zero on no hit. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_intersects_frustum(
    CNA_Ray ray,
    CNA_BoundingFrustum frustum,
    CNA_Bool* out_hit,
    float* out_distance);
/** @brief Gets a ray string byte count. @param value Ray to format. @param out_bytes Receives the count. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_get_string_size(CNA_Ray value, uint64_t* out_bytes);
/** @brief Copies the canonical ray UTF-8 string without a terminator. @param value Ray to format. @param destination Destination bytes, or null only for zero capacity. @param capacity Destination capacity. @param out_bytes Receives the required count. @return A CNA result code. */
CNA_C_API CNA_Result cna_ray_copy_string(
    CNA_Ray value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Fixed number of corners returned for a bounding box. */
#define CNA_BOUNDING_BOX_CORNER_COUNT UINT32_C(8)

/** @brief Initializes a zero bounding box. @param out_value Receives the box. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_init(CNA_BoundingBox* out_value);
/** @brief Initializes a bounding box from minimum and maximum corners. @param min Minimum corner. @param max Maximum corner. @param out_value Receives the box. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_init_min_max(
    CNA_Vector3 min,
    CNA_Vector3 max,
    CNA_BoundingBox* out_value);
/** @brief Classifies a box relative to this box. @param value Source box. @param box Box to classify. @param out_containment Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_contains_box(
    CNA_BoundingBox value,
    CNA_BoundingBox box,
    CNA_ContainmentType* out_containment);
/** @brief Classifies a sphere relative to this box. @param value Source box. @param sphere Sphere to classify. @param out_containment Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_contains_sphere(
    CNA_BoundingBox value,
    CNA_BoundingSphere sphere,
    CNA_ContainmentType* out_containment);
/** @brief Classifies a point relative to this box. @param value Source box. @param point Point to classify. @param out_containment Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_contains_point(
    CNA_BoundingBox value,
    CNA_Vector3 point,
    CNA_ContainmentType* out_containment);
/** @brief Classifies a frustum relative to this box. @param value Source box. @param frustum Frustum to classify. @param out_containment Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_contains_frustum(
    CNA_BoundingBox value,
    CNA_BoundingFrustum frustum,
    CNA_ContainmentType* out_containment);

/**
 * @brief Copies all eight corners in XNA order.
 * @param value Source box.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination element capacity.
 * @param out_count Receives the required element count.
 * @return A CNA result code; insufficient capacity writes no corner.
 */
CNA_C_API CNA_Result cna_bounding_box_copy_corners(
    CNA_BoundingBox value,
    CNA_Vector3* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Intersects a box with a ray. @param value Source box. @param ray Ray to test. @param out_hit Receives whether a hit exists. @param out_distance Receives distance, or zero on no hit. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_intersects_ray(
    CNA_BoundingBox value,
    CNA_Ray ray,
    CNA_Bool* out_hit,
    float* out_distance);
/** @brief Tests box/frustum intersection. @param value Source box. @param frustum Frustum to test. @param out_intersects Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_intersects_frustum(
    CNA_BoundingBox value,
    CNA_BoundingFrustum frustum,
    CNA_Bool* out_intersects);
/** @brief Tests box/sphere intersection. @param value Source box. @param sphere Sphere to test. @param out_intersects Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_intersects_sphere(
    CNA_BoundingBox value,
    CNA_BoundingSphere sphere,
    CNA_Bool* out_intersects);
/** @brief Tests two boxes for intersection. @param value Source box. @param box Other box. @param out_intersects Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_intersects_box(
    CNA_BoundingBox value,
    CNA_BoundingBox box,
    CNA_Bool* out_intersects);
/** @brief Classifies a box against a plane. @param value Source box. @param plane Plane to test. @param out_intersection Receives the classification. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_intersects_plane(
    CNA_BoundingBox value,
    CNA_Plane plane,
    CNA_PlaneIntersectionType* out_intersection);
/** @brief Tests bounding-box equality. @param left First box. @param right Second box. @param out_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_equals(
    CNA_BoundingBox left,
    CNA_BoundingBox right,
    CNA_Bool* out_equal);
/** @brief Tests bounding-box inequality. @param left First box. @param right Second box. @param out_not_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_not_equals(
    CNA_BoundingBox left,
    CNA_BoundingBox right,
    CNA_Bool* out_not_equal);
/** @brief Computes a bounding-box hash. @param value Source box. @param out_hash Receives the hash. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_get_hash_code(
    CNA_BoundingBox value,
    int32_t* out_hash);
/** @brief Gets a bounding-box string byte count. @param value Box to format. @param out_bytes Receives the count. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_get_string_size(
    CNA_BoundingBox value,
    uint64_t* out_bytes);
/** @brief Copies the canonical bounding-box UTF-8 string without a terminator. @param value Box to format. @param destination Destination bytes, or null only for zero capacity. @param capacity Destination capacity. @param out_bytes Receives the required count. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_copy_string(
    CNA_BoundingBox value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Creates the smallest box enclosing a point array. @param points Source points, non-null and nonempty. @param count Point count. @param out_value Receives the box. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_create_from_points(
    const CNA_Vector3* points,
    uint64_t count,
    CNA_BoundingBox* out_value);
/** @brief Creates the smallest box enclosing a sphere. @param sphere Source sphere. @param out_value Receives the box. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_create_from_sphere(
    CNA_BoundingSphere sphere,
    CNA_BoundingBox* out_value);
/** @brief Creates the smallest box enclosing two boxes. @param original First box. @param additional Second box. @param out_value Receives the merged box. @return A CNA result code. */
CNA_C_API CNA_Result cna_bounding_box_create_merged(
    CNA_BoundingBox original,
    CNA_BoundingBox additional,
    CNA_BoundingBox* out_value);

/**
 * @brief Initializes a zero bounding sphere.
 *
 * @param out_value Receives the sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_init(CNA_BoundingSphere* out_value);

/**
 * @brief Initializes a bounding sphere from its center and radius.
 *
 * @param center Sphere center.
 * @param radius Sphere radius.
 * @param out_value Receives the sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_init_center_radius(
    CNA_Vector3 center,
    float radius,
    CNA_BoundingSphere* out_value);

/**
 * @brief Transforms a bounding sphere by a matrix.
 *
 * @param value Source sphere.
 * @param matrix Transformation matrix.
 * @param out_value Receives the transformed sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_transform(
    CNA_BoundingSphere value,
    CNA_Matrix matrix,
    CNA_BoundingSphere* out_value);

/**
 * @brief Classifies a box relative to a sphere.
 *
 * @param value Source sphere.
 * @param box Box to classify.
 * @param out_containment Receives the classification.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_contains_box(
    CNA_BoundingSphere value,
    CNA_BoundingBox box,
    CNA_ContainmentType* out_containment);

/**
 * @brief Classifies a frustum relative to a sphere.
 *
 * @param value Source sphere.
 * @param frustum Frustum to classify.
 * @param out_containment Receives the classification.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_contains_frustum(
    CNA_BoundingSphere value,
    CNA_BoundingFrustum frustum,
    CNA_ContainmentType* out_containment);

/**
 * @brief Classifies another sphere relative to a sphere.
 *
 * @param value Source sphere.
 * @param sphere Sphere to classify.
 * @param out_containment Receives the classification.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_contains_sphere(
    CNA_BoundingSphere value,
    CNA_BoundingSphere sphere,
    CNA_ContainmentType* out_containment);

/**
 * @brief Classifies a point relative to a sphere.
 *
 * @param value Source sphere.
 * @param point Point to classify.
 * @param out_containment Receives the classification.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_contains_point(
    CNA_BoundingSphere value,
    CNA_Vector3 point,
    CNA_ContainmentType* out_containment);

/**
 * @brief Tests bounding-sphere equality.
 *
 * @param left First sphere.
 * @param right Second sphere.
 * @param out_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_equals(
    CNA_BoundingSphere left,
    CNA_BoundingSphere right,
    CNA_Bool* out_equal);

/**
 * @brief Tests bounding-sphere inequality.
 *
 * @param left First sphere.
 * @param right Second sphere.
 * @param out_not_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_not_equals(
    CNA_BoundingSphere left,
    CNA_BoundingSphere right,
    CNA_Bool* out_not_equal);

/**
 * @brief Creates the smallest sphere enclosing a box.
 *
 * @param box Source box.
 * @param out_value Receives the sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_create_from_box(
    CNA_BoundingBox box,
    CNA_BoundingSphere* out_value);

/**
 * @brief Creates a sphere enclosing a frustum.
 *
 * @param frustum Source frustum.
 * @param out_value Receives the sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_create_from_frustum(
    CNA_BoundingFrustum frustum,
    CNA_BoundingSphere* out_value);

/**
 * @brief Creates a sphere enclosing a point array.
 *
 * @param points Source points, non-null and nonempty.
 * @param count Point count.
 * @param out_value Receives the sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_create_from_points(
    const CNA_Vector3* points,
    uint64_t count,
    CNA_BoundingSphere* out_value);

/**
 * @brief Creates the smallest sphere enclosing two spheres.
 *
 * @param original First sphere.
 * @param additional Second sphere.
 * @param out_value Receives the merged sphere.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_create_merged(
    CNA_BoundingSphere original,
    CNA_BoundingSphere additional,
    CNA_BoundingSphere* out_value);

/**
 * @brief Tests sphere/box intersection.
 *
 * @param value Source sphere.
 * @param box Box to test.
 * @param out_intersects Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_intersects_box(
    CNA_BoundingSphere value,
    CNA_BoundingBox box,
    CNA_Bool* out_intersects);

/**
 * @brief Tests sphere/frustum intersection.
 *
 * @param value Source sphere.
 * @param frustum Frustum to test.
 * @param out_intersects Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_intersects_frustum(
    CNA_BoundingSphere value,
    CNA_BoundingFrustum frustum,
    CNA_Bool* out_intersects);

/**
 * @brief Tests two spheres for intersection.
 *
 * @param value Source sphere.
 * @param sphere Other sphere.
 * @param out_intersects Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_intersects_sphere(
    CNA_BoundingSphere value,
    CNA_BoundingSphere sphere,
    CNA_Bool* out_intersects);

/**
 * @brief Intersects a sphere with a ray.
 *
 * @param value Source sphere.
 * @param ray Ray to test.
 * @param out_hit Receives whether a hit exists.
 * @param out_distance Receives distance, or zero on no hit.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_intersects_ray(
    CNA_BoundingSphere value,
    CNA_Ray ray,
    CNA_Bool* out_hit,
    float* out_distance);

/**
 * @brief Classifies a sphere against a plane.
 *
 * @param value Source sphere.
 * @param plane Plane to test.
 * @param out_intersection Receives the classification.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_intersects_plane(
    CNA_BoundingSphere value,
    CNA_Plane plane,
    CNA_PlaneIntersectionType* out_intersection);

/**
 * @brief Computes a bounding-sphere hash.
 *
 * @param value Source sphere.
 * @param out_hash Receives the hash.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_get_hash_code(
    CNA_BoundingSphere value,
    int32_t* out_hash);

/**
 * @brief Gets a bounding-sphere string byte count.
 *
 * @param value Sphere to format.
 * @param out_bytes Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_get_string_size(
    CNA_BoundingSphere value,
    uint64_t* out_bytes);

/**
 * @brief Copies the canonical bounding-sphere UTF-8 string without a terminator.
 *
 * @param value Sphere to format.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity.
 * @param out_bytes Receives the required count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_bounding_sphere_copy_string(
    CNA_BoundingSphere value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
