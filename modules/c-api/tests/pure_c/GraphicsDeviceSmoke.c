// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

static const CNA_Matrix Identity = {
    1.0F, 0.0F, 0.0F, 0.0F,
    0.0F, 1.0F, 0.0F, 0.0F,
    0.0F, 0.0F, 1.0F, 0.0F,
    0.0F, 0.0F, 0.0F, 1.0F};

static int near_float(const float left, const float right)
{
    return fabsf(left - right) <= 0.0001F;
}

static int viewport_equals(const CNA_Viewport left, const CNA_Viewport right)
{
    return left.x == right.x && left.y == right.y && left.width == right.width &&
        left.height == right.height && left.min_depth == right.min_depth &&
        left.max_depth == right.max_depth;
}

static int rectangle_equals(const CNA_Rectangle left, const CNA_Rectangle right)
{
    return left.x == right.x && left.y == right.y && left.width == right.width &&
        left.height == right.height;
}

static int validate_identities(void)
{
    /* The native declarations expose bitwise operators; C applies its own to the alias. */
    CNA_ClearOptions options = CNA_CLEAR_OPTION_TARGET;
    options |= CNA_CLEAR_OPTION_DEPTH_BUFFER;
    if (options != UINT32_C(3) ||
        (options & CNA_CLEAR_OPTION_STENCIL) != UINT32_C(0) ||
        (options & CNA_CLEAR_OPTION_TARGET) != CNA_CLEAR_OPTION_TARGET) {
        return 0;
    }
    options &= ~CNA_CLEAR_OPTION_TARGET;
    if (options != CNA_CLEAR_OPTION_DEPTH_BUFFER) {
        return 0;
    }
    options = CNA_CLEAR_OPTION_TARGET | CNA_CLEAR_OPTION_DEPTH_BUFFER |
        CNA_CLEAR_OPTION_STENCIL;
    if (options != UINT32_C(7)) {
        return 0;
    }

    CNA_SpriteEffects effects = CNA_SPRITE_EFFECT_NONE;
    effects |= CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY;
    effects |= CNA_SPRITE_EFFECT_FLIP_VERTICALLY;
    if (effects != UINT32_C(3) ||
        (effects & CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY) !=
            CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY) {
        return 0;
    }
    effects &= CNA_SPRITE_EFFECT_FLIP_VERTICALLY;
    if (effects != CNA_SPRITE_EFFECT_FLIP_VERTICALLY) {
        return 0;
    }

    if (CNA_GRAPHICS_DEVICE_STATUS_NORMAL != UINT32_C(0) ||
        CNA_GRAPHICS_DEVICE_STATUS_LOST != UINT32_C(1) ||
        CNA_GRAPHICS_DEVICE_STATUS_NOT_RESET != UINT32_C(2) ||
        CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW != UINT32_C(0) ||
        CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB != UINT32_C(1)) {
        return 0;
    }
    return 1;
}

static int validate_construction(void)
{
    CNA_Viewport viewport = {7, 7, 7, 7, 7.0F, 7.0F};
    if (cna_viewport_init(&viewport) != CNA_RESULT_SUCCESS ||
        !viewport_equals(viewport, (CNA_Viewport){0, 0, 0, 0, 0.0F, 1.0F})) {
        return 0;
    }

    if (cna_viewport_init_bounds(3, -4, 800, 600, &viewport) != CNA_RESULT_SUCCESS ||
        !viewport_equals(viewport, (CNA_Viewport){3, -4, 800, 600, 0.0F, 1.0F})) {
        return 0;
    }

    const CNA_Rectangle bounds = {10, 20, 320, 240};
    if (cna_viewport_init_from_rectangle(bounds, &viewport) != CNA_RESULT_SUCCESS ||
        !viewport_equals(viewport, (CNA_Viewport){10, 20, 320, 240, 0.0F, 1.0F})) {
        return 0;
    }

    if (cna_viewport_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_viewport_init_bounds(0, 0, 1, 1, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_viewport_init_from_rectangle(bounds, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_properties(void)
{
    CNA_Viewport viewport = {0, 0, 800, 600, 0.25F, 0.75F};
    float aspect = -1.0F;
    if (cna_viewport_get_aspect_ratio(viewport, &aspect) != CNA_RESULT_SUCCESS ||
        !near_float(aspect, 800.0F / 600.0F)) {
        return 0;
    }

    const CNA_Viewport zero_width = {0, 0, 0, 600, 0.0F, 1.0F};
    const CNA_Viewport zero_height = {0, 0, 800, 0, 0.0F, 1.0F};
    if (cna_viewport_get_aspect_ratio(zero_width, &aspect) != CNA_RESULT_SUCCESS ||
        aspect != 0.0F ||
        cna_viewport_get_aspect_ratio(zero_height, &aspect) != CNA_RESULT_SUCCESS ||
        aspect != 0.0F ||
        cna_viewport_get_aspect_ratio(viewport, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Rectangle rectangle = {-1, -1, -1, -1};
    viewport = (CNA_Viewport){5, 6, 320, 240, 0.25F, 0.75F};
    if (cna_viewport_get_bounds(viewport, &rectangle) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(rectangle, (CNA_Rectangle){5, 6, 320, 240}) ||
        cna_viewport_get_title_safe_area(viewport, &rectangle) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(rectangle, (CNA_Rectangle){5, 6, 320, 240}) ||
        cna_viewport_get_bounds(viewport, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_viewport_get_title_safe_area(viewport, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Setting bounds replaces position and size while preserving the depth range. */
    if (cna_viewport_set_bounds(&viewport, (CNA_Rectangle){1, 2, 3, 4}) !=
            CNA_RESULT_SUCCESS ||
        !viewport_equals(viewport, (CNA_Viewport){1, 2, 3, 4, 0.25F, 0.75F}) ||
        cna_viewport_set_bounds(0, rectangle) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_transforms(void)
{
    const CNA_Viewport viewport = {0, 0, 800, 600, 0.0F, 1.0F};
    CNA_Vector3 projected = {-1.0F, -1.0F, -1.0F};
    if (cna_viewport_project(
            viewport, (CNA_Vector3){0.0F, 0.0F, 0.0F}, Identity, Identity, Identity,
            &projected) != CNA_RESULT_SUCCESS ||
        !near_float(projected.x, 400.0F) || !near_float(projected.y, 300.0F) ||
        !near_float(projected.z, 0.0F)) {
        return 0;
    }

    /* The depth range scales the projected Z between min_depth and max_depth. */
    const CNA_Viewport deep = {0, 0, 800, 600, 0.25F, 0.75F};
    if (cna_viewport_project(
            deep, (CNA_Vector3){0.0F, 0.0F, 1.0F}, Identity, Identity, Identity,
            &projected) != CNA_RESULT_SUCCESS ||
        !near_float(projected.z, 0.75F)) {
        return 0;
    }

    /* The clip-space corners land on the viewport rectangle corners. */
    if (cna_viewport_project(
            viewport, (CNA_Vector3){-1.0F, 1.0F, 0.0F}, Identity, Identity, Identity,
            &projected) != CNA_RESULT_SUCCESS ||
        !near_float(projected.x, 0.0F) || !near_float(projected.y, 0.0F)) {
        return 0;
    }

    CNA_Vector3 unprojected = {-1.0F, -1.0F, -1.0F};
    if (cna_viewport_unproject(
            viewport, (CNA_Vector3){400.0F, 300.0F, 0.0F}, Identity, Identity, Identity,
            &unprojected) != CNA_RESULT_SUCCESS ||
        !near_float(unprojected.x, 0.0F) || !near_float(unprojected.y, 0.0F) ||
        !near_float(unprojected.z, 0.0F)) {
        return 0;
    }

    /* A perspective round trip returns the original world-space point. */
    CNA_Matrix projection = Identity;
    CNA_Matrix view = Identity;
    if (cna_matrix_create_perspective_field_of_view(
            CNA_MATH_PI_OVER_4, 800.0F / 600.0F, 1.0F, 100.0F, &projection) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_create_look_at(
            (CNA_Vector3){0.0F, 0.0F, 10.0F}, (CNA_Vector3){0.0F, 0.0F, 0.0F},
            (CNA_Vector3){0.0F, 1.0F, 0.0F}, &view) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    const CNA_Vector3 world_point = {1.5F, -2.0F, 3.0F};
    if (cna_viewport_project(
            viewport, world_point, projection, view, Identity, &projected) !=
            CNA_RESULT_SUCCESS ||
        cna_viewport_unproject(
            viewport, projected, projection, view, Identity, &unprojected) !=
            CNA_RESULT_SUCCESS ||
        !near_float(unprojected.x, world_point.x) ||
        !near_float(unprojected.y, world_point.y) ||
        !near_float(unprojected.z, world_point.z)) {
        return 0;
    }

    if (cna_viewport_project(
            viewport, world_point, projection, view, Identity, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_viewport_unproject(
            viewport, world_point, projection, view, Identity, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_string(void)
{
    static const char Expected[] =
        "{X:1 Y:2 Width:3 Height:4 MinDepth:0.250000 MaxDepth:0.750000}";
    const size_t expected_length = sizeof(Expected) - 1U;
    const CNA_Viewport viewport = {1, 2, 3, 4, 0.25F, 0.75F};

    uint64_t byte_count = 0U;
    if (cna_viewport_get_string_size(viewport, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != (uint64_t)expected_length ||
        cna_viewport_get_string_size(viewport, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    char bytes[sizeof(Expected)];
    memset(bytes, 'z', sizeof(bytes));
    byte_count = 0U;
    if (cna_viewport_copy_string(viewport, bytes, (uint64_t)expected_length, &byte_count) !=
            CNA_RESULT_SUCCESS ||
        byte_count != (uint64_t)expected_length ||
        memcmp(bytes, Expected, expected_length) != 0 ||
        bytes[expected_length] != 'z') {
        return 0;
    }

    /* An undersized destination reports the requirement and writes nothing. */
    char guarded = 'q';
    byte_count = 0U;
    if (cna_viewport_copy_string(viewport, &guarded, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        byte_count != (uint64_t)expected_length || guarded != 'q' ||
        cna_viewport_copy_string(viewport, 0, 0U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        byte_count != (uint64_t)expected_length ||
        cna_viewport_copy_string(viewport, 0, 4U, &byte_count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_viewport_copy_string(viewport, bytes, sizeof(bytes), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

int main(void)
{
    return validate_identities() && validate_construction() && validate_properties() &&
            validate_transforms() && validate_string()
        ? 0
        : 1;
}
