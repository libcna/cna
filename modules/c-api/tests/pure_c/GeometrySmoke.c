// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

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

int main(void)
{
    return validate_plane() && validate_ray() ? 0 : 1;
}
