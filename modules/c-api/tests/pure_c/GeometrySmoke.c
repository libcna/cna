// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <string.h>

static int near_float(float left, float right)
{
    return fabsf(left - right) <= 0.0001F;
}

static int vector3_near(CNA_Vector3 value, float x, float y, float z)
{
    return near_float(value.x, x) && near_float(value.y, y) && near_float(value.z, z);
}

static int plane_near(CNA_Plane value, float x, float y, float z, float d)
{
    return vector3_near(value.normal, x, y, z) && near_float(value.d, d);
}

static int validate_plane(void)
{
    const CNA_Plane ground = {{0.0F, 1.0F, 0.0F}, 0.0F};
    CNA_Plane value = {{9.0F, 9.0F, 9.0F}, 9.0F};
    float scalar = 0.0F;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    if (cna_plane_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_plane_init(&value) != CNA_RESULT_SUCCESS ||
        !plane_near(value, 0.0F, 0.0F, 0.0F, 0.0F) ||
        cna_plane_init_vector4((CNA_Vector4){1.0F, 2.0F, 3.0F, 4.0F}, &value) !=
            CNA_RESULT_SUCCESS || !plane_near(value, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_plane_init_normal_d((CNA_Vector3){0.0F, 1.0F, 0.0F}, -2.0F, &value) !=
            CNA_RESULT_SUCCESS || !plane_near(value, 0.0F, 1.0F, 0.0F, -2.0F) ||
        cna_plane_init_points(
            (CNA_Vector3){0.0F, 0.0F, 0.0F},
            (CNA_Vector3){1.0F, 0.0F, 0.0F},
            (CNA_Vector3){0.0F, 0.0F, 1.0F},
            &value) != CNA_RESULT_SUCCESS ||
        !near_float(fabsf(value.normal.y), 1.0F) || !near_float(value.d, 0.0F) ||
        cna_plane_init_abcd(0.0F, 1.0F, 0.0F, -2.0F, &value) != CNA_RESULT_SUCCESS ||
        !plane_near(value, 0.0F, 1.0F, 0.0F, -2.0F)) {
        return 0;
    }

    if (cna_plane_dot(value, (CNA_Vector4){0.0F, 3.0F, 0.0F, 2.0F}, &scalar) !=
            CNA_RESULT_SUCCESS || !near_float(scalar, -1.0F) ||
        cna_plane_dot_coordinate(value, (CNA_Vector3){0.0F, 5.0F, 0.0F}, &scalar) !=
            CNA_RESULT_SUCCESS || !near_float(scalar, 3.0F) ||
        cna_plane_dot_normal(value, (CNA_Vector3){0.0F, 4.0F, 0.0F}, &scalar) !=
            CNA_RESULT_SUCCESS || !near_float(scalar, 4.0F) ||
        cna_plane_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_plane_not_equals(value, ground, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_plane_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        cna_plane_get_hash_code(value, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash) {
        return 0;
    }

    CNA_Plane normalized = {{0.0F, 2.0F, 0.0F}, 4.0F};
    if (cna_plane_normalize_in_place(&normalized) != CNA_RESULT_SUCCESS ||
        !plane_near(normalized, 0.0F, 1.0F, 0.0F, 2.0F) ||
        cna_plane_normalize_in_place(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_plane_normalize((CNA_Plane){{0.0F, 3.0F, 0.0F}, 6.0F}, &normalized) !=
            CNA_RESULT_SUCCESS || !plane_near(normalized, 0.0F, 1.0F, 0.0F, 2.0F)) {
        return 0;
    }

    const CNA_BoundingBox back_box = {
        {-1.0F, -2.0F, -1.0F}, {1.0F, -1.0F, 1.0F}
    };
    const CNA_BoundingSphere front_sphere = {{0.0F, 2.0F, 0.0F}, 0.5F};
    CNA_PlaneIntersectionType intersection = CNA_PLANE_INTERSECTION_INTERSECTING;
    CNA_Matrix projection;
    if (cna_matrix_create_perspective_field_of_view(
            1.57079632679F, 1.0F, 1.0F, 10.0F, &projection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    const CNA_BoundingFrustum frustum = {projection};
    if (cna_plane_intersects_box(ground, back_box, &intersection) != CNA_RESULT_SUCCESS ||
        intersection != CNA_PLANE_INTERSECTION_BACK ||
        cna_plane_intersects_sphere(ground, front_sphere, &intersection) != CNA_RESULT_SUCCESS ||
        intersection != CNA_PLANE_INTERSECTION_FRONT ||
        cna_plane_intersects_frustum(ground, frustum, &intersection) != CNA_RESULT_SUCCESS ||
        intersection > CNA_PLANE_INTERSECTION_INTERSECTING) {
        return 0;
    }

    CNA_Matrix translation;
    if (cna_matrix_create_translation_xyz(0.0F, 2.0F, 0.0F, &translation) !=
            CNA_RESULT_SUCCESS ||
        cna_plane_transform_matrix(ground, translation, &normalized) != CNA_RESULT_SUCCESS ||
        !plane_near(normalized, 0.0F, 1.0F, 0.0F, -2.0F) ||
        cna_plane_transform_quaternion(
            ground,
            (CNA_Quaternion){0.0F, 0.0F, 0.70710678F, 0.70710678F},
            &normalized) != CNA_RESULT_SUCCESS ||
        !plane_near(normalized, -1.0F, 0.0F, 0.0F, 0.0F)) {
        return 0;
    }

    value = (CNA_Plane){{0.0F, 1.0F, 0.0F}, -2.0F};
    static const char Expected[] = "{Normal:{X:0 Y:1 Z:0} D:-2}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'p';
    if (cna_plane_get_string_size(value, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_plane_copy_string(value, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'p' ||
        cna_plane_copy_string(value, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_ray(void)
{
    CNA_Ray zero = {{9.0F, 9.0F, 9.0F}, {9.0F, 9.0F, 9.0F}};
    CNA_Ray ray;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    if (cna_ray_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_ray_init(&zero) != CNA_RESULT_SUCCESS ||
        !vector3_near(zero.position, 0.0F, 0.0F, 0.0F) ||
        !vector3_near(zero.direction, 0.0F, 0.0F, 0.0F) ||
        cna_ray_init_position_direction(
            (CNA_Vector3){0.0F, 0.0F, 5.0F},
            (CNA_Vector3){0.0F, 0.0F, -1.0F},
            &ray) != CNA_RESULT_SUCCESS ||
        cna_ray_equals(ray, ray, &predicate) != CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_ray_not_equals(ray, zero, &predicate) != CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_ray_get_hash_code(ray, &hash) != CNA_RESULT_SUCCESS ||
        cna_ray_get_hash_code(ray, &equal_hash) != CNA_RESULT_SUCCESS || hash != equal_hash) {
        return 0;
    }

    const CNA_BoundingBox box = {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const CNA_BoundingSphere sphere = {{0.0F, 0.0F, 0.0F}, 1.0F};
    const CNA_Plane plane = {{0.0F, 0.0F, 1.0F}, 0.0F};
    CNA_Bool hit = CNA_FALSE;
    float distance = -1.0F;
    if (cna_ray_intersects_box(ray, box, &hit, &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_TRUE || !near_float(distance, 4.0F) ||
        cna_ray_intersects_sphere(ray, sphere, &hit, &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_TRUE || !near_float(distance, 4.0F) ||
        cna_ray_intersects_plane(ray, plane, &hit, &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_TRUE || !near_float(distance, 5.0F) ||
        cna_ray_intersects_plane(
            (CNA_Ray){{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, 1.0F}},
            plane,
            &hit,
            &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_FALSE || distance != 0.0F ||
        cna_ray_intersects_box(ray, box, 0, &distance) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Matrix projection;
    if (cna_matrix_create_perspective_field_of_view(
            1.57079632679F, 1.0F, 1.0F, 10.0F, &projection) != CNA_RESULT_SUCCESS ||
        cna_ray_intersects_frustum(
            (CNA_Ray){{0.0F, 0.0F, -2.0F}, {0.0F, 0.0F, -1.0F}},
            (CNA_BoundingFrustum){projection},
            &hit,
            &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_TRUE || distance != 0.0F) {
        return 0;
    }

    static const char Expected[] =
        "{{Position:{X:0 Y:0 Z:5} Direction:{X:0 Y:0 Z:-1}}}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'r';
    if (cna_ray_get_string_size(ray, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_ray_copy_string(ray, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'r' ||
        cna_ray_copy_string(ray, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_bounding_box(void)
{
    CNA_BoundingBox zero = {{9.0F, 9.0F, 9.0F}, {9.0F, 9.0F, 9.0F}};
    CNA_BoundingBox box;
    if (CNA_BOUNDING_BOX_CORNER_COUNT != 8U ||
        cna_bounding_box_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_bounding_box_init(&zero) != CNA_RESULT_SUCCESS ||
        !vector3_near(zero.min, 0.0F, 0.0F, 0.0F) ||
        !vector3_near(zero.max, 0.0F, 0.0F, 0.0F) ||
        cna_bounding_box_init_min_max(
            (CNA_Vector3){-1.0F, -2.0F, -3.0F},
            (CNA_Vector3){4.0F, 5.0F, 6.0F},
            &box) != CNA_RESULT_SUCCESS ||
        !vector3_near(box.min, -1.0F, -2.0F, -3.0F) ||
        !vector3_near(box.max, 4.0F, 5.0F, 6.0F)) {
        return 0;
    }

    const CNA_BoundingBox outer = {{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};
    const CNA_BoundingBox inner = {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const CNA_BoundingSphere sphere = {{0.0F, 0.0F, 0.0F}, 0.5F};
    CNA_ContainmentType containment = CNA_CONTAINMENT_DISJOINT;
    CNA_Matrix projection;
    if (cna_matrix_create_perspective_field_of_view(
            1.57079632679F, 1.0F, 1.0F, 10.0F, &projection) != CNA_RESULT_SUCCESS ||
        cna_bounding_box_contains_box(outer, inner, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_box_contains_sphere(outer, sphere, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_box_contains_point(
            outer, (CNA_Vector3){0.0F, 0.0F, 0.0F}, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_box_contains_frustum(
            (CNA_BoundingBox){{-20.0F, -20.0F, -20.0F}, {20.0F, 20.0F, 1.0F}},
            (CNA_BoundingFrustum){projection},
            &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS) {
        return 0;
    }

    CNA_Vector3 corners[CNA_BOUNDING_BOX_CORNER_COUNT];
    CNA_Vector3 sentinel = {77.0F, 88.0F, 99.0F};
    uint64_t corner_count = 0U;
    corners[0] = sentinel;
    if (cna_bounding_box_copy_corners(box, corners, 1U, &corner_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        corner_count != CNA_BOUNDING_BOX_CORNER_COUNT ||
        !vector3_near(corners[0], 77.0F, 88.0F, 99.0F) ||
        cna_bounding_box_copy_corners(
            box, corners, CNA_BOUNDING_BOX_CORNER_COUNT, &corner_count) != CNA_RESULT_SUCCESS ||
        !vector3_near(corners[0], -1.0F, 5.0F, 6.0F) ||
        !vector3_near(corners[7], -1.0F, -2.0F, -3.0F)) {
        return 0;
    }

    CNA_Bool predicate = CNA_FALSE;
    CNA_Bool hit = CNA_FALSE;
    float distance = -1.0F;
    CNA_PlaneIntersectionType plane_intersection = CNA_PLANE_INTERSECTION_FRONT;
    if (cna_bounding_box_intersects_ray(
            inner,
            (CNA_Ray){{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, -1.0F}},
            &hit,
            &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_TRUE || !near_float(distance, 4.0F) ||
        cna_bounding_box_intersects_ray(
            inner,
            (CNA_Ray){{5.0F, 5.0F, 5.0F}, {1.0F, 0.0F, 0.0F}},
            &hit,
            &distance) != CNA_RESULT_SUCCESS ||
        hit != CNA_FALSE || !near_float(distance, 0.0F) ||
        cna_bounding_box_intersects_frustum(inner, (CNA_BoundingFrustum){projection}, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_bounding_box_intersects_sphere(inner, sphere, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_box_intersects_box(outer, inner, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_box_intersects_plane(
            inner, (CNA_Plane){{0.0F, 1.0F, 0.0F}, 0.0F}, &plane_intersection) !=
            CNA_RESULT_SUCCESS || plane_intersection != CNA_PLANE_INTERSECTION_INTERSECTING) {
        return 0;
    }

    int32_t hash = 0;
    int32_t equal_hash = 1;
    if (cna_bounding_box_equals(box, box, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_box_not_equals(box, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_box_get_hash_code(box, &hash) != CNA_RESULT_SUCCESS ||
        cna_bounding_box_get_hash_code(box, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash) {
        return 0;
    }

    static const char Expected[] =
        "{{Min:{X:-1 Y:-2 Z:-3} Max:{X:4 Y:5 Z:6}}}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'b';
    if (cna_bounding_box_get_string_size(box, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_bounding_box_copy_string(box, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'b' ||
        cna_bounding_box_copy_string(box, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }

    const CNA_Vector3 points[] = {
        {-2.0F, 3.0F, 1.0F}, {4.0F, -5.0F, 6.0F}, {0.0F, 1.0F, -7.0F}
    };
    CNA_BoundingBox created = box;
    if (cna_bounding_box_create_from_points(points, 3U, &created) != CNA_RESULT_SUCCESS ||
        !vector3_near(created.min, -2.0F, -5.0F, -7.0F) ||
        !vector3_near(created.max, 4.0F, 3.0F, 6.0F) ||
        cna_bounding_box_create_from_sphere(
            (CNA_BoundingSphere){{1.0F, 1.0F, 1.0F}, 2.0F}, &created) !=
            CNA_RESULT_SUCCESS ||
        !vector3_near(created.min, -1.0F, -1.0F, -1.0F) ||
        !vector3_near(created.max, 3.0F, 3.0F, 3.0F) ||
        cna_bounding_box_create_merged(inner, created, &created) != CNA_RESULT_SUCCESS ||
        !vector3_near(created.min, -1.0F, -1.0F, -1.0F) ||
        !vector3_near(created.max, 3.0F, 3.0F, 3.0F)) {
        return 0;
    }

    created = box;
    if (cna_bounding_box_create_from_points(0, 0U, &created) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !vector3_near(created.min, box.min.x, box.min.y, box.min.z) ||
        !vector3_near(created.max, box.max.x, box.max.y, box.max.z)) {
        return 0;
    }
    return 1;
}

static int validate_bounding_sphere(void)
{
    CNA_BoundingSphere zero = {{9.0F, 9.0F, 9.0F}, 9.0F};
    CNA_BoundingSphere sphere;
    if (cna_bounding_sphere_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_bounding_sphere_init(&zero) != CNA_RESULT_SUCCESS ||
        !vector3_near(zero.center, 0.0F, 0.0F, 0.0F) || !near_float(zero.radius, 0.0F) ||
        cna_bounding_sphere_init_center_radius(
            (CNA_Vector3){1.0F, 2.0F, 3.0F}, 2.0F, &sphere) != CNA_RESULT_SUCCESS ||
        !vector3_near(sphere.center, 1.0F, 2.0F, 3.0F) || !near_float(sphere.radius, 2.0F)) {
        return 0;
    }

    const CNA_Matrix transform = {
        2.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 3.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 4.0F, 0.0F,
        5.0F, -2.0F, 1.0F, 1.0F
    };
    CNA_BoundingSphere transformed;
    if (cna_bounding_sphere_transform(sphere, transform, &transformed) != CNA_RESULT_SUCCESS ||
        !vector3_near(transformed.center, 7.0F, 4.0F, 13.0F) ||
        !near_float(transformed.radius, 8.0F)) {
        return 0;
    }

    const CNA_BoundingSphere outer = {{0.0F, 0.0F, 0.0F}, 5.0F};
    const CNA_BoundingSphere inner = {{0.0F, 0.0F, 0.0F}, 1.0F};
    const CNA_BoundingBox box = {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    CNA_Matrix projection;
    CNA_ContainmentType containment = CNA_CONTAINMENT_DISJOINT;
    if (cna_matrix_create_perspective_field_of_view(
            1.57079632679F, 1.0F, 1.0F, 10.0F, &projection) != CNA_RESULT_SUCCESS ||
        cna_bounding_sphere_contains_box(outer, box, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_sphere_contains_frustum(
            (CNA_BoundingSphere){{0.0F, 0.0F, 0.0F}, 100.0F},
            (CNA_BoundingFrustum){projection},
            &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_sphere_contains_sphere(outer, inner, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_sphere_contains_point(
            outer, (CNA_Vector3){5.0F, 0.0F, 0.0F}, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_INTERSECTS) {
        return 0;
    }

    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    if (cna_bounding_sphere_equals(sphere, sphere, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_sphere_not_equals(sphere, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_sphere_get_hash_code(sphere, &hash) != CNA_RESULT_SUCCESS ||
        cna_bounding_sphere_get_hash_code(sphere, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash) {
        return 0;
    }

    CNA_BoundingSphere created;
    if (cna_bounding_sphere_create_from_box(box, &created) != CNA_RESULT_SUCCESS ||
        !vector3_near(created.center, 0.0F, 0.0F, 0.0F) ||
        !near_float(created.radius, sqrtf(3.0F)) ||
        cna_bounding_sphere_create_from_frustum(
            (CNA_BoundingFrustum){projection}, &created) != CNA_RESULT_SUCCESS ||
        !isfinite(created.radius) || created.radius <= 0.0F) {
        return 0;
    }

    const CNA_Vector3 points[] = {
        {-2.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}
    };
    if (cna_bounding_sphere_create_from_points(points, 3U, &created) != CNA_RESULT_SUCCESS ||
        !vector3_near(created.center, 0.0F, 0.0F, 0.0F) || !near_float(created.radius, 2.0F) ||
        cna_bounding_sphere_create_merged(
            (CNA_BoundingSphere){{-2.0F, 0.0F, 0.0F}, 1.0F},
            (CNA_BoundingSphere){{2.0F, 0.0F, 0.0F}, 1.0F},
            &created) != CNA_RESULT_SUCCESS ||
        !vector3_near(created.center, 0.0F, 0.0F, 0.0F) || !near_float(created.radius, 3.0F)) {
        return 0;
    }

    CNA_Bool hit = CNA_FALSE;
    float distance = -1.0F;
    CNA_PlaneIntersectionType plane_intersection = CNA_PLANE_INTERSECTION_BACK;
    if (cna_bounding_sphere_intersects_box(inner, box, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_sphere_intersects_frustum(
            (CNA_BoundingSphere){{0.0F, 0.0F, -2.0F}, 0.5F},
            (CNA_BoundingFrustum){projection},
            &predicate) != CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_bounding_sphere_intersects_sphere(
            (CNA_BoundingSphere){{0.0F, 0.0F, 0.0F}, 2.0F},
            (CNA_BoundingSphere){{4.0F, 0.0F, 0.0F}, 2.0F},
            &predicate) != CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_bounding_sphere_intersects_ray(
            inner,
            (CNA_Ray){{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, -1.0F}},
            &hit,
            &distance) != CNA_RESULT_SUCCESS || hit != CNA_TRUE || !near_float(distance, 4.0F) ||
        cna_bounding_sphere_intersects_ray(
            inner,
            (CNA_Ray){{5.0F, 5.0F, 5.0F}, {1.0F, 0.0F, 0.0F}},
            &hit,
            &distance) != CNA_RESULT_SUCCESS || hit != CNA_FALSE || !near_float(distance, 0.0F) ||
        cna_bounding_sphere_intersects_plane(
            (CNA_BoundingSphere){{0.0F, 2.0F, 0.0F}, 0.5F},
            (CNA_Plane){{0.0F, 1.0F, 0.0F}, 0.0F},
            &plane_intersection) != CNA_RESULT_SUCCESS ||
        plane_intersection != CNA_PLANE_INTERSECTION_FRONT) {
        return 0;
    }

    static const char Expected[] = "{Center:{X:1 Y:2 Z:3} Radius:2}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 's';
    if (cna_bounding_sphere_get_string_size(sphere, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_bounding_sphere_copy_string(sphere, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 's' ||
        cna_bounding_sphere_copy_string(sphere, bytes, sizeof(bytes), &byte_count) !=
            CNA_RESULT_SUCCESS || byte_count != sizeof(bytes) ||
        memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }

    created = sphere;
    if (cna_bounding_sphere_create_from_points(0, 0U, &created) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !vector3_near(created.center, sphere.center.x, sphere.center.y, sphere.center.z) ||
        !near_float(created.radius, sphere.radius)) {
        return 0;
    }
    return 1;
}

static int validate_bounding_frustum(void)
{
    CNA_Matrix identity;
    CNA_BoundingFrustum frustum;
    if (CNA_BOUNDING_FRUSTUM_CORNER_COUNT != 8U ||
        cna_matrix_get_identity(&identity) != CNA_RESULT_SUCCESS ||
        cna_bounding_frustum_init_matrix(identity, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_bounding_frustum_init_matrix(identity, &frustum) != CNA_RESULT_SUCCESS ||
        memcmp(&frustum.matrix, &identity, sizeof(identity)) != 0) {
        return 0;
    }

    CNA_Plane near_plane;
    CNA_Plane far_plane;
    CNA_Plane left_plane;
    CNA_Plane right_plane;
    CNA_Plane top_plane;
    CNA_Plane bottom_plane;
    if (cna_bounding_frustum_get_near(frustum, &near_plane) != CNA_RESULT_SUCCESS ||
        !plane_near(near_plane, 0.0F, 0.0F, -1.0F, 0.0F) ||
        cna_bounding_frustum_get_far(frustum, &far_plane) != CNA_RESULT_SUCCESS ||
        !plane_near(far_plane, 0.0F, 0.0F, 1.0F, -1.0F) ||
        cna_bounding_frustum_get_left(frustum, &left_plane) != CNA_RESULT_SUCCESS ||
        !plane_near(left_plane, -1.0F, 0.0F, 0.0F, -1.0F) ||
        cna_bounding_frustum_get_right(frustum, &right_plane) != CNA_RESULT_SUCCESS ||
        !plane_near(right_plane, 1.0F, 0.0F, 0.0F, -1.0F) ||
        cna_bounding_frustum_get_top(frustum, &top_plane) != CNA_RESULT_SUCCESS ||
        !plane_near(top_plane, 0.0F, 1.0F, 0.0F, -1.0F) ||
        cna_bounding_frustum_get_bottom(frustum, &bottom_plane) != CNA_RESULT_SUCCESS ||
        !plane_near(bottom_plane, 0.0F, -1.0F, 0.0F, -1.0F)) {
        return 0;
    }

    CNA_ContainmentType containment = CNA_CONTAINMENT_DISJOINT;
    const CNA_BoundingBox box = {{-0.5F, -0.5F, 0.25F}, {0.5F, 0.5F, 0.75F}};
    const CNA_BoundingSphere sphere = {{0.0F, 0.0F, 0.5F}, 0.1F};
    if (cna_bounding_frustum_contains_frustum(frustum, frustum, &containment) !=
            CNA_RESULT_SUCCESS || containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_frustum_contains_box(frustum, box, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_frustum_contains_sphere(frustum, sphere, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_frustum_contains_point(
            frustum, (CNA_Vector3){0.0F, 0.0F, 0.5F}, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_CONTAINS ||
        cna_bounding_frustum_contains_point(
            frustum, (CNA_Vector3){0.0F, 0.0F, 0.0F}, &containment) != CNA_RESULT_SUCCESS ||
        containment != CNA_CONTAINMENT_INTERSECTS) {
        return 0;
    }

    CNA_Vector3 corners[CNA_BOUNDING_FRUSTUM_CORNER_COUNT];
    CNA_Vector3 sentinel = {71.0F, 72.0F, 73.0F};
    uint64_t corner_count = 0U;
    corners[0] = sentinel;
    if (cna_bounding_frustum_copy_corners(frustum, corners, 1U, &corner_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        corner_count != CNA_BOUNDING_FRUSTUM_CORNER_COUNT ||
        !vector3_near(corners[0], 71.0F, 72.0F, 73.0F) ||
        cna_bounding_frustum_copy_corners(
            frustum, corners, CNA_BOUNDING_FRUSTUM_CORNER_COUNT, &corner_count) !=
            CNA_RESULT_SUCCESS ||
        !vector3_near(corners[0], -1.0F, 1.0F, 0.0F) ||
        !vector3_near(corners[3], -1.0F, -1.0F, 0.0F) ||
        !vector3_near(corners[4], -1.0F, 1.0F, 1.0F) ||
        !vector3_near(corners[7], -1.0F, -1.0F, 1.0F)) {
        return 0;
    }

    CNA_Bool predicate = CNA_FALSE;
    CNA_PlaneIntersectionType plane_intersection = CNA_PLANE_INTERSECTION_FRONT;
    if (cna_bounding_frustum_intersects_frustum(frustum, frustum, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_bounding_frustum_intersects_box(frustum, box, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_frustum_intersects_sphere(frustum, sphere, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_frustum_intersects_plane(
            frustum, (CNA_Plane){{0.0F, 1.0F, 0.0F}, 0.0F}, &plane_intersection) !=
            CNA_RESULT_SUCCESS || plane_intersection != CNA_PLANE_INTERSECTION_INTERSECTING) {
        return 0;
    }

    CNA_Bool hit = CNA_FALSE;
    float distance = -1.0F;
    if (cna_bounding_frustum_intersects_ray(
            frustum,
            (CNA_Ray){{0.0F, 0.0F, 0.5F}, {0.0F, 0.0F, 1.0F}},
            &hit,
            &distance) != CNA_RESULT_SUCCESS || hit != CNA_TRUE || !near_float(distance, 0.0F) ||
        cna_bounding_frustum_intersects_ray(
            frustum,
            (CNA_Ray){{0.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 1.0F}},
            &hit,
            &distance) != CNA_RESULT_SUCCESS || hit != CNA_FALSE || !near_float(distance, 0.0F)) {
        return 0;
    }
    hit = CNA_TRUE;
    distance = 77.0F;
    if (cna_bounding_frustum_intersects_ray(
            frustum,
            (CNA_Ray){{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
            &hit,
            &distance) != CNA_RESULT_NOT_SUPPORTED ||
        hit != CNA_TRUE || !near_float(distance, 77.0F)) {
        return 0;
    }

    CNA_Matrix projection;
    CNA_BoundingFrustum other;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    if (cna_matrix_create_perspective_field_of_view(
            1.57079632679F, 1.0F, 1.0F, 10.0F, &projection) != CNA_RESULT_SUCCESS ||
        cna_bounding_frustum_init_matrix(projection, &other) != CNA_RESULT_SUCCESS ||
        cna_bounding_frustum_equals(frustum, frustum, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_frustum_not_equals(frustum, other, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_bounding_frustum_get_hash_code(frustum, &hash) != CNA_RESULT_SUCCESS ||
        cna_bounding_frustum_get_hash_code(frustum, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash) {
        return 0;
    }

    static const char Expected[] =
        "{Near:{Normal:{X:-0 Y:-0 Z:-1} D:-0} Far:{Normal:{X:0 Y:0 Z:1} D:-1} "
        "Left:{Normal:{X:-1 Y:-0 Z:-0} D:-1} Right:{Normal:{X:1 Y:0 Z:0} D:-1} "
        "Top:{Normal:{X:0 Y:1 Z:0} D:-1} Bottom:{Normal:{X:-0 Y:-1 Z:-0} D:-1}}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'f';
    if (cna_bounding_frustum_get_string_size(frustum, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_bounding_frustum_copy_string(frustum, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'f' ||
        cna_bounding_frustum_copy_string(frustum, bytes, sizeof(bytes), &byte_count) !=
            CNA_RESULT_SUCCESS || byte_count != sizeof(bytes) ||
        memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

int main(void)
{
    return CNA_TEST_STAGE(validate_plane()) && CNA_TEST_STAGE(validate_ray()) && CNA_TEST_STAGE(validate_bounding_box()) &&
            CNA_TEST_STAGE(validate_bounding_sphere()) && CNA_TEST_STAGE(validate_bounding_frustum()) ? 0 : 1;
}
