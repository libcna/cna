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

typedef struct DeviceEventCounters {
    int disposing;
    int device_lost;
    int device_reset;
    int device_resetting;
    int resource_created;
    int resource_created_with_object;
    int resource_destroyed;
    int resource_destroyed_named;
    CNA_Handle observed_device;
} DeviceEventCounters;

typedef struct DeviceState {
    DeviceEventCounters counters;
    CNA_GraphicsDeviceEventRegistrationHandle disposing_registration;
    CNA_GraphicsDeviceEventRegistrationHandle lost_registration;
    CNA_GraphicsDeviceEventRegistrationHandle reset_registration;
    CNA_GraphicsDeviceEventRegistrationHandle resetting_registration;
    CNA_GraphicsDeviceEventRegistrationHandle created_registration;
    CNA_GraphicsDeviceEventRegistrationHandle destroyed_registration;
    CNA_Handle stale_device;
    int validated;
} DeviceState;

static void on_device_event(CNA_Handle graphics_device, void* context)
{
    DeviceEventCounters* const counters = (DeviceEventCounters*)context;
    counters->observed_device = graphics_device;
    ++counters->disposing;
}

static void on_device_lost(CNA_Handle graphics_device, void* context)
{
    (void)graphics_device;
    ++((DeviceEventCounters*)context)->device_lost;
}

static void on_device_reset(CNA_Handle graphics_device, void* context)
{
    (void)graphics_device;
    ++((DeviceEventCounters*)context)->device_reset;
}

static void on_device_resetting(CNA_Handle graphics_device, void* context)
{
    (void)graphics_device;
    ++((DeviceEventCounters*)context)->device_resetting;
}

static void on_resource_created(
    CNA_Handle graphics_device,
    const CNA_ResourceCreatedEventInfo* info,
    void* context)
{
    DeviceEventCounters* const counters = (DeviceEventCounters*)context;
    counters->observed_device = graphics_device;
    if (info == 0 || info->struct_size != sizeof(CNA_ResourceCreatedEventInfo) ||
        info->struct_version != UINT32_C(1)) {
        return;
    }
    ++counters->resource_created;
    if (info->has_resource == CNA_TRUE) {
        ++counters->resource_created_with_object;
    }
}

static void on_resource_destroyed(
    CNA_Handle graphics_device,
    const CNA_ResourceDestroyedEventInfo* info,
    void* context)
{
    DeviceEventCounters* const counters = (DeviceEventCounters*)context;
    counters->observed_device = graphics_device;
    if (info == 0 || info->struct_size != sizeof(CNA_ResourceDestroyedEventInfo) ||
        info->struct_version != UINT32_C(1)) {
        return;
    }
    ++counters->resource_destroyed;
    if (info->has_tag == CNA_FALSE && info->name.byte_length == 0U && info->name.data == 0) {
        ++counters->resource_destroyed_named;
    }
}

static int validate_device_state(CNA_Handle graphics_device)
{
    CNA_Bool disposed = CNA_TRUE;
    CNA_GraphicsDeviceStatus status = UINT32_MAX;
    CNA_GraphicsProfile profile = UINT32_MAX;
    uint32_t adapter_index = UINT32_MAX;
    uint64_t adapter_count = 0U;
    if (cna_graphics_device_get_is_disposed(graphics_device, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE ||
        cna_graphics_device_get_status(graphics_device, &status) != CNA_RESULT_SUCCESS ||
        status != CNA_GRAPHICS_DEVICE_STATUS_NORMAL ||
        cna_graphics_device_get_graphics_profile(
            graphics_device, &profile) != CNA_RESULT_SUCCESS ||
        (profile != CNA_GRAPHICS_PROFILE_REACH && profile != CNA_GRAPHICS_PROFILE_HI_DEF) ||
        cna_graphics_device_get_adapter_index(
            graphics_device, &adapter_index) != CNA_RESULT_SUCCESS ||
        cna_graphics_adapter_get_count(graphics_device, &adapter_count) != CNA_RESULT_SUCCESS ||
        (uint64_t)adapter_index >= adapter_count) {
        return 0;
    }

    /* Every query rejects a null output without touching the device. */
    if (cna_graphics_device_get_is_disposed(graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_status(graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_graphics_profile(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_adapter_index(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Viewport viewport = {0, 0, 0, 0, 0.0F, 0.0F};
    CNA_Viewport applied = {0, 0, 0, 0, 0.0F, 0.0F};
    if (cna_graphics_device_get_viewport(graphics_device, &viewport) != CNA_RESULT_SUCCESS ||
        viewport.width <= 0 || viewport.height <= 0) {
        return 0;
    }
    const CNA_Viewport requested = {4, 6, 40, 30, 0.25F, 0.75F};
    if (cna_graphics_device_set_viewport(graphics_device, requested) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_viewport(graphics_device, &applied) != CNA_RESULT_SUCCESS ||
        !viewport_equals(applied, requested) ||
        cna_graphics_device_get_viewport(graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* A non-finite depth range is rejected before the device is touched. */
    CNA_Viewport invalid = requested;
    invalid.max_depth = INFINITY;
    if (cna_graphics_device_set_viewport(graphics_device, invalid) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_viewport(graphics_device, &applied) != CNA_RESULT_SUCCESS ||
        !viewport_equals(applied, requested)) {
        return 0;
    }
    invalid.max_depth = 1.0F;
    invalid.min_depth = NAN;
    if (cna_graphics_device_set_viewport(graphics_device, invalid) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_graphics_device_set_viewport(graphics_device, viewport) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_Rectangle scissor = {0, 0, 0, 0};
    const CNA_Rectangle requested_scissor = {2, 3, 20, 15};
    if (cna_graphics_device_get_scissor_rectangle(
            graphics_device, &scissor) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_scissor_rectangle(
            graphics_device, requested_scissor) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_scissor_rectangle(
            graphics_device, &scissor) != CNA_RESULT_SUCCESS ||
        !rectangle_equals(scissor, requested_scissor) ||
        cna_graphics_device_get_scissor_rectangle(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Color blend_factor = {0U, 0U, 0U, 0U};
    const CNA_Color requested_factor = {12U, 34U, 56U, 78U};
    if (cna_graphics_device_get_blend_factor(
            graphics_device, &blend_factor) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_blend_factor(
            graphics_device, requested_factor) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_blend_factor(
            graphics_device, &blend_factor) != CNA_RESULT_SUCCESS ||
        blend_factor.r != requested_factor.r || blend_factor.g != requested_factor.g ||
        blend_factor.b != requested_factor.b || blend_factor.a != requested_factor.a ||
        cna_graphics_device_get_blend_factor(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    int32_t mask = 0;
    int32_t reference = -1;
    if (cna_graphics_device_get_multi_sample_mask(
            graphics_device, &mask) != CNA_RESULT_SUCCESS ||
        mask != -1 ||
        cna_graphics_device_set_multi_sample_mask(graphics_device, 15) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_multi_sample_mask(
            graphics_device, &mask) != CNA_RESULT_SUCCESS ||
        mask != 15 ||
        cna_graphics_device_get_multi_sample_mask(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_reference_stencil(
            graphics_device, &reference) != CNA_RESULT_SUCCESS ||
        reference != 0 ||
        cna_graphics_device_set_reference_stencil(graphics_device, 7) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_reference_stencil(
            graphics_device, &reference) != CNA_RESULT_SUCCESS ||
        reference != 7 ||
        cna_graphics_device_get_reference_stencil(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    static const char ExpectedType[] = "Microsoft.Xna.Framework.Graphics.GraphicsDevice";
    const uint64_t expected_type_length = (uint64_t)(sizeof(ExpectedType) - 1U);
    uint64_t type_bytes = 0U;
    char type_name[sizeof(ExpectedType)];
    memset(type_name, 'z', sizeof(type_name));
    if (cna_graphics_device_get_type_name_size(
            graphics_device, &type_bytes) != CNA_RESULT_SUCCESS ||
        type_bytes != expected_type_length ||
        cna_graphics_device_get_type_name_size(
            graphics_device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_copy_type_name(
            graphics_device, type_name, expected_type_length, &type_bytes) !=
            CNA_RESULT_SUCCESS ||
        type_bytes != expected_type_length ||
        memcmp(type_name, ExpectedType, (size_t)expected_type_length) != 0 ||
        type_name[expected_type_length] != 'z') {
        return 0;
    }

    char guard = 'q';
    type_bytes = 0U;
    if (cna_graphics_device_copy_type_name(graphics_device, &guard, 1U, &type_bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        type_bytes != expected_type_length || guard != 'q' ||
        cna_graphics_device_copy_type_name(
            graphics_device, type_name, sizeof(type_name), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The active game owns the device, so C cannot dispose it. */
    if (cna_graphics_device_dispose(graphics_device) != CNA_RESULT_NOT_SUPPORTED ||
        cna_graphics_device_get_is_disposed(graphics_device, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE) {
        return 0;
    }
    return 1;
}

static int validate_device_events(CNA_Handle graphics_device, DeviceState* state)
{
    CNA_GraphicsDeviceEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    if (cna_graphics_device_subscribe_event(
            graphics_device, UINT32_C(4), on_device_event, &state->counters, &registration) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        registration != CNA_INVALID_HANDLE ||
        cna_graphics_device_subscribe_event(
            graphics_device, CNA_GRAPHICS_DEVICE_EVENT_DISPOSING, 0, &state->counters,
            &registration) != CNA_RESULT_INVALID_ARGUMENT ||
        registration != CNA_INVALID_HANDLE ||
        cna_graphics_device_subscribe_event(
            graphics_device, CNA_GRAPHICS_DEVICE_EVENT_DISPOSING, on_device_event,
            &state->counters, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_subscribe_resource_created(
            graphics_device, 0, &state->counters, &registration) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_subscribe_resource_destroyed(
            graphics_device, 0, &state->counters, &registration) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_graphics_device_subscribe_event(
            graphics_device, CNA_GRAPHICS_DEVICE_EVENT_DISPOSING, on_device_event,
            &state->counters, &state->disposing_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_subscribe_event(
            graphics_device, CNA_GRAPHICS_DEVICE_EVENT_DEVICE_LOST, on_device_lost,
            &state->counters, &state->lost_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_subscribe_event(
            graphics_device, CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESET, on_device_reset,
            &state->counters, &state->reset_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_subscribe_event(
            graphics_device, CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESETTING, on_device_resetting,
            &state->counters, &state->resetting_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_subscribe_resource_created(
            graphics_device, on_resource_created, &state->counters,
            &state->created_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_subscribe_resource_destroyed(
            graphics_device, on_resource_destroyed, &state->counters,
            &state->destroyed_registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Creating and destroying a real graphics resource drives both payload events. */
    const CNA_Texture2DCreateInfo create_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 2U, 2U, CNA_FALSE, {0U, 0U, 0U},
        CNA_SURFACE_FORMAT_COLOR
    };
    CNA_Handle texture = CNA_INVALID_HANDLE;
    if (cna_texture2d_create(graphics_device, &create_info, &texture) != CNA_RESULT_SUCCESS ||
        state->counters.resource_created != 1 ||
        state->counters.resource_created_with_object != 1 ||
        state->counters.observed_device != graphics_device ||
        state->counters.resource_destroyed != 0 ||
        cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS ||
        state->counters.resource_destroyed != 1 ||
        state->counters.resource_destroyed_named != 1 ||
        state->counters.disposing != 0 || state->counters.device_lost != 0 ||
        state->counters.device_reset != 0 || state->counters.device_resetting != 0) {
        return 0;
    }

    /* An unsubscribed registration stops receiving events. */
    if (cna_graphics_device_unsubscribe(state->created_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unsubscribe(state->created_registration) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_device_unsubscribe(CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_device_unsubscribe(graphics_device) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    state->created_registration = CNA_INVALID_HANDLE;

    CNA_Handle second_texture = CNA_INVALID_HANDLE;
    if (cna_texture2d_create(graphics_device, &create_info, &second_texture) !=
            CNA_RESULT_SUCCESS ||
        state->counters.resource_created != 1 ||
        cna_texture2d_destroy(second_texture) != CNA_RESULT_SUCCESS ||
        state->counters.resource_destroyed != 2) {
        return 0;
    }
    return 1;
}

static int read_slot(
    CNA_Handle graphics_device,
    CNA_ShaderStage stage,
    uint32_t slot,
    CNA_TextureSlotInfo* out_info)
{
    memset(out_info, 0, sizeof(*out_info));
    out_info->struct_size = sizeof(CNA_TextureSlotInfo);
    out_info->struct_version = UINT32_C(1);
    return cna_graphics_device_get_texture(graphics_device, stage, slot, out_info) ==
        CNA_RESULT_SUCCESS;
}

static int validate_texture_collections(CNA_Handle graphics_device)
{
    CNA_TextureSlotInfo info;
    for (uint32_t slot = 0U; slot < CNA_TEXTURE_COLLECTION_MAX_TEXTURES; ++slot) {
        if (!read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL, slot, &info) ||
            info.bound != CNA_FALSE || info.texture != CNA_INVALID_HANDLE ||
            !read_slot(graphics_device, CNA_SHADER_STAGE_VERTEX, slot, &info) ||
            info.bound != CNA_FALSE || info.texture != CNA_INVALID_HANDLE) {
            return 0;
        }
    }

    /* Unknown stages, out-of-range slots and malformed structures are rejected. */
    CNA_TextureSlotInfo malformed = {0U, 0U, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U},
                                     CNA_INVALID_HANDLE};
    if (read_slot(graphics_device, UINT32_C(2), 0U, &info) ||
        read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL,
                  CNA_TEXTURE_COLLECTION_MAX_TEXTURES, &info) ||
        cna_graphics_device_get_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 0U, &malformed) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 0U, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_texture(
            graphics_device, UINT32_C(7), 0U, CNA_INVALID_HANDLE) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_VERTEX, CNA_TEXTURE_COLLECTION_MAX_TEXTURES,
            CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    const CNA_Texture2DCreateInfo create_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 2U, 2U, CNA_FALSE, {0U, 0U, 0U},
        CNA_SURFACE_FORMAT_COLOR
    };
    CNA_Handle texture = CNA_INVALID_HANDLE;
    if (cna_texture2d_create(graphics_device, &create_info, &texture) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A bound slot reports the owning C handle on both stages. */
    if (cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 3U, texture) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_VERTEX, 0U, texture) != CNA_RESULT_SUCCESS ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL, 3U, &info) ||
        info.bound != CNA_TRUE || info.texture != texture ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_VERTEX, 0U, &info) ||
        info.bound != CNA_TRUE || info.texture != texture ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL, 4U, &info) ||
        info.bound != CNA_FALSE) {
        cna_texture2d_destroy(texture);
        return 0;
    }

    /* An invalid handle empties the slot; unbinding clears every slot at once. */
    if (cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 3U, CNA_INVALID_HANDLE) !=
            CNA_RESULT_SUCCESS ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL, 3U, &info) ||
        info.bound != CNA_FALSE || info.texture != CNA_INVALID_HANDLE ||
        cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 5U, texture) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unbind_texture(graphics_device, texture) != CNA_RESULT_SUCCESS ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL, 5U, &info) ||
        info.bound != CNA_FALSE ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_VERTEX, 0U, &info) ||
        info.bound != CNA_FALSE ||
        cna_graphics_device_unbind_texture(graphics_device, texture) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unbind_texture(graphics_device, CNA_INVALID_HANDLE) !=
            CNA_RESULT_INVALID_HANDLE) {
        cna_texture2d_destroy(texture);
        return 0;
    }

    /* Destroying a bound texture unbinds it, matching canonical disposal. */
    if (cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 1U, texture) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS ||
        !read_slot(graphics_device, CNA_SHADER_STAGE_PIXEL, 1U, &info) ||
        info.bound != CNA_FALSE || info.texture != CNA_INVALID_HANDLE ||
        cna_graphics_device_set_texture(
            graphics_device, CNA_SHADER_STAGE_PIXEL, 1U, texture) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    /* A texture bound as a render target cannot also be sampled. */
    const CNA_RenderTarget2DCreateInfo target_info = {
        sizeof(CNA_RenderTarget2DCreateInfo), UINT32_C(1), 8U, 8U, CNA_FALSE, {0U, 0U, 0U},
        CNA_SURFACE_FORMAT_COLOR, CNA_DEPTH_FORMAT_NONE, 0,
        CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS, 0U
    };
    CNA_Handle render_target = CNA_INVALID_HANDLE;
    const CNA_Result created =
        cna_render_target2d_create(graphics_device, &target_info, &render_target);
    if (created == CNA_RESULT_SUCCESS) {
        const CNA_Result bound =
            cna_graphics_device_set_render_target2d(graphics_device, render_target);
        if (bound == CNA_RESULT_SUCCESS) {
            if (cna_graphics_device_set_texture(
                    graphics_device, CNA_SHADER_STAGE_PIXEL, 2U, render_target) !=
                    CNA_RESULT_INVALID_STATE ||
                cna_graphics_device_set_render_target2d(
                    graphics_device, CNA_INVALID_HANDLE) != CNA_RESULT_SUCCESS ||
                cna_graphics_device_set_texture(
                    graphics_device, CNA_SHADER_STAGE_PIXEL, 2U, render_target) !=
                    CNA_RESULT_SUCCESS ||
                cna_graphics_device_unbind_texture(
                    graphics_device, render_target) != CNA_RESULT_SUCCESS) {
                cna_render_target_destroy(render_target);
                return 0;
            }
        } else if (bound != CNA_RESULT_NOT_SUPPORTED) {
            cna_render_target_destroy(render_target);
            return 0;
        }
        if (cna_render_target_destroy(render_target) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    } else if (created != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    return 1;
}

static int create_vertex_buffer(
    CNA_Handle graphics_device,
    CNA_VertexBufferHandle* out_vertex_buffer)
{
    CNA_VertexElement elements[8];
    uint64_t element_count = 0U;
    uint32_t stride = 0U;
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    if (cna_vertex_type_get_stride(CNA_VERTEX_TYPE_POSITION_COLOR, &stride) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_type_copy_elements(
            CNA_VERTEX_TYPE_POSITION_COLOR, elements, 8U, &element_count) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_create_with_stride(
            (int32_t)stride, elements, element_count, &declaration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    const CNA_VertexBufferCreateInfo info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), declaration, 3,
        CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    const CNA_Result result =
        cna_vertex_buffer_create(graphics_device, &info, out_vertex_buffer);
    if (cna_vertex_declaration_destroy(declaration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return result == CNA_RESULT_SUCCESS && *out_vertex_buffer != CNA_INVALID_HANDLE;
}

static int is_supported(const CNA_Result result)
{
    return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_NOT_SUPPORTED;
}

static int validate_frame_control(CNA_Handle graphics_device, DeviceState* state)
{
    const CNA_Color color = {10U, 20U, 30U, 40U};
    if (cna_graphics_device_clear_rgba(graphics_device, 0.25F, 0.5F, 0.75F, 1.0F) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_clear_rgba(graphics_device, 0.0F, 0.0F, 0.0F, NAN) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_clear_rgba(graphics_device, INFINITY, 0.0F, 0.0F, 1.0F) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !is_supported(cna_graphics_device_clear_color_depth(graphics_device, color, 1.0F)) ||
        cna_graphics_device_clear_color_depth(graphics_device, color, NAN) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !is_supported(cna_graphics_device_clear_options(
            graphics_device, CNA_CLEAR_OPTION_TARGET, color, 1.0F, 0)) ||
        !is_supported(cna_graphics_device_clear_options(
            graphics_device,
            CNA_CLEAR_OPTION_TARGET | CNA_CLEAR_OPTION_DEPTH_BUFFER | CNA_CLEAR_OPTION_STENCIL,
            color, 1.0F, 3)) ||
        cna_graphics_device_clear_options(
            graphics_device, UINT32_C(8), color, 1.0F, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_clear_options(
            graphics_device, CNA_CLEAR_OPTION_TARGET, color, INFINITY, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !is_supported(cna_graphics_device_present(graphics_device))) {
        return 0;
    }

    /* A successful reset raises both canonical reset events exactly once. */
    const int previous_resetting = state->counters.device_resetting;
    const int previous_reset = state->counters.device_reset;
    const CNA_Result reset = cna_graphics_device_reset(graphics_device);
    if (!is_supported(reset)) {
        return 0;
    }
    if (reset == CNA_RESULT_SUCCESS &&
        (state->counters.device_resetting != previous_resetting + 1 ||
         state->counters.device_reset != previous_reset + 1)) {
        return 0;
    }

    CNA_PresentationParameters parameters = {0};
    parameters.struct_size = sizeof(CNA_PresentationParameters);
    parameters.struct_version = UINT32_C(1);
    const uint32_t adapter_index = UINT32_MAX;
    if (cna_graphics_device_get_presentation_parameters(
            graphics_device, &parameters) != CNA_RESULT_SUCCESS ||
        !is_supported(cna_graphics_device_reset_with_parameters(
            graphics_device, &parameters, 0)) ||
        cna_graphics_device_reset_with_parameters(graphics_device, 0, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_reset_with_parameters(
            graphics_device, &parameters, &adapter_index) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_backbuffer_window(CNA_Handle graphics_device)
{
    CNA_BackBufferInfo backbuffer = {
        sizeof(CNA_BackBufferInfo), UINT32_C(1), 0U, 0U, 0U, 0U};
    if (cna_graphics_device_get_backbuffer_info(graphics_device, &backbuffer) !=
            CNA_RESULT_SUCCESS ||
        backbuffer.width == 0U || backbuffer.height == 0U) {
        return 0;
    }

    CNA_Color pixels[8];
    memset(pixels, 0x5A, sizeof(pixels));
    CNA_BackBufferReadback readback = {
        sizeof(CNA_BackBufferReadback), UINT32_C(1), CNA_TRUE, {0U, 0U, 0U},
        {0, 0, 2, 2}, 2U, 4U};

    /* Capacity and structure validation happens before any native readback. */
    CNA_BackBufferReadback malformed = readback;
    malformed.struct_version = UINT32_C(2);
    CNA_BackBufferReadback undersized = readback;
    undersized.element_count = 3U;
    if (cna_graphics_device_get_backbuffer_data_window(
            graphics_device, &malformed, pixels, 8U) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_backbuffer_data_window(graphics_device, 0, pixels, 8U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_backbuffer_data_window(
            graphics_device, &readback, pixels, 5U) != CNA_RESULT_BUFFER_TOO_SMALL ||
        cna_graphics_device_get_backbuffer_data_window(graphics_device, &readback, 0, 8U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_get_backbuffer_data_window(
            graphics_device, &undersized, pixels, 8U) != CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    for (size_t index = 0U; index < sizeof(pixels); ++index) {
        if (((const unsigned char*)pixels)[index] != 0x5AU) {
            return 0;
        }
    }

    const CNA_Result windowed = cna_graphics_device_get_backbuffer_data_window(
        graphics_device, &readback, pixels, 8U);
    if (windowed == CNA_RESULT_NOT_SUPPORTED) {
        /* A refusing backend must leave the destination untouched. */
        for (size_t index = 0U; index < sizeof(pixels); ++index) {
            if (((const unsigned char*)pixels)[index] != 0x5AU) {
                return 0;
            }
        }
        return 1;
    }
    if (windowed != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Only the requested window is written. */
    if (pixels[0].r != 0x5AU || pixels[1].a != 0x5AU || pixels[6].g != 0x5AU ||
        pixels[7].b != 0x5AU) {
        return 0;
    }

    CNA_BackBufferReadback full = readback;
    full.has_source_rectangle = CNA_FALSE;
    full.start_index = 0U;
    full.element_count = 1U;
    /* A full-buffer request that cannot hold the whole back buffer is a capacity error, decided
       before any native read. */
    return cna_graphics_device_get_backbuffer_data_window(
               graphics_device, &full, pixels, 8U) == CNA_RESULT_BUFFER_TOO_SMALL;
}

static int validate_buffer_binding(CNA_Handle graphics_device)
{
    CNA_VertexBufferHandle vertex_buffer = CNA_INVALID_HANDLE;
    CNA_IndexBufferHandle index_buffer = CNA_INVALID_HANDLE;
    uint64_t count = UINT64_MAX;
    CNA_VertexBufferHandle bound_vertex = UINT64_C(7);
    CNA_IndexBufferHandle bound_index = UINT64_C(7);

    /* Every route answers before any buffer exists. */
    if (cna_graphics_device_get_vertex_buffer_count(graphics_device, &count) !=
            CNA_RESULT_SUCCESS ||
        count != 0U ||
        cna_graphics_device_get_vertex_buffer(graphics_device, &bound_vertex) !=
            CNA_RESULT_SUCCESS ||
        bound_vertex != CNA_INVALID_HANDLE ||
        cna_graphics_device_get_index_buffer(graphics_device, &bound_index) !=
            CNA_RESULT_SUCCESS ||
        bound_index != CNA_INVALID_HANDLE ||
        cna_graphics_device_get_vertex_buffer_count(graphics_device, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_vertex_buffer_offset(
            graphics_device, CNA_INVALID_HANDLE, -1) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_vertex_buffers(graphics_device, 0, 1U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (!create_vertex_buffer(graphics_device, &vertex_buffer)) {
        /* A backend without 3D support refuses buffer creation; nothing else to bind. */
        return 1;
    }
    const CNA_IndexBufferCreateInfo index_info = {
        sizeof(CNA_IndexBufferCreateInfo), UINT32_C(1), 6,
        CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U}};
    if (cna_index_buffer_create(graphics_device, &index_info, &index_buffer) !=
        CNA_RESULT_SUCCESS) {
        cna_vertex_buffer_destroy(vertex_buffer);
        return 0;
    }

    CNA_VertexBufferBinding bindings[2] = {
        {CNA_INVALID_HANDLE, 0, 0}, {CNA_INVALID_HANDLE, 0, 0}};
    int ok =
        cna_graphics_device_set_vertex_buffer(graphics_device, vertex_buffer) ==
            CNA_RESULT_SUCCESS &&
        cna_graphics_device_get_vertex_buffer(graphics_device, &bound_vertex) ==
            CNA_RESULT_SUCCESS &&
        bound_vertex == vertex_buffer &&
        cna_graphics_device_get_vertex_buffer_count(graphics_device, &count) ==
            CNA_RESULT_SUCCESS &&
        count == 1U &&
        cna_graphics_device_copy_vertex_buffers(graphics_device, bindings, 2U, &count) ==
            CNA_RESULT_SUCCESS &&
        count == 1U && bindings[0].vertex_buffer == vertex_buffer &&
        bindings[0].vertex_offset == 0 && bindings[0].instance_frequency == 0 &&
        cna_graphics_device_copy_vertex_buffers(graphics_device, bindings, 0U, &count) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        count == 1U &&
        cna_graphics_device_set_vertex_buffer_offset(graphics_device, vertex_buffer, 1) ==
            CNA_RESULT_SUCCESS &&
        cna_graphics_device_copy_vertex_buffers(graphics_device, bindings, 2U, &count) ==
            CNA_RESULT_SUCCESS &&
        count == 1U && bindings[0].vertex_offset == 1;

    if (ok) {
        const CNA_VertexBufferBinding requested[1] = {{vertex_buffer, 2, 0}};
        const CNA_VertexBufferBinding invalid[1] = {{vertex_buffer, -3, 0}};
        ok = cna_graphics_device_set_vertex_buffers(graphics_device, requested, 1U) ==
                CNA_RESULT_SUCCESS &&
            cna_graphics_device_copy_vertex_buffers(graphics_device, bindings, 2U, &count) ==
                CNA_RESULT_SUCCESS &&
            count == 1U && bindings[0].vertex_offset == 2 &&
            cna_graphics_device_set_vertex_buffers(graphics_device, invalid, 1U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_graphics_device_copy_vertex_buffers(graphics_device, bindings, 2U, &count) ==
                CNA_RESULT_SUCCESS &&
            count == 1U && bindings[0].vertex_offset == 2 &&
            cna_graphics_device_set_vertex_buffers(graphics_device, 0, 0U) ==
                CNA_RESULT_SUCCESS &&
            cna_graphics_device_get_vertex_buffer_count(graphics_device, &count) ==
                CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_graphics_device_get_vertex_buffer(graphics_device, &bound_vertex) ==
                CNA_RESULT_SUCCESS &&
            bound_vertex == CNA_INVALID_HANDLE;
    }

    if (ok) {
        ok = cna_graphics_device_set_index_buffer(graphics_device, index_buffer) ==
                CNA_RESULT_SUCCESS &&
            cna_graphics_device_get_index_buffer(graphics_device, &bound_index) ==
                CNA_RESULT_SUCCESS &&
            bound_index == index_buffer &&
            cna_graphics_device_set_index_buffer(graphics_device, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_graphics_device_get_index_buffer(graphics_device, &bound_index) ==
                CNA_RESULT_SUCCESS &&
            bound_index == CNA_INVALID_HANDLE &&
            cna_graphics_device_set_index_buffer(graphics_device, vertex_buffer) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_graphics_device_set_vertex_buffer(graphics_device, index_buffer) ==
                CNA_RESULT_INVALID_HANDLE;
    }

    if (cna_index_buffer_destroy(index_buffer) != CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_destroy(vertex_buffer) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return ok;
}

static int make_position_color_declaration(CNA_VertexDeclarationHandle* out_declaration)
{
    CNA_VertexElement elements[8];
    uint64_t element_count = 0U;
    uint32_t stride = 0U;
    return cna_vertex_type_get_stride(CNA_VERTEX_TYPE_POSITION_COLOR, &stride) ==
            CNA_RESULT_SUCCESS &&
        cna_vertex_type_copy_elements(
            CNA_VERTEX_TYPE_POSITION_COLOR, elements, 8U, &element_count) ==
            CNA_RESULT_SUCCESS &&
        cna_vertex_declaration_create_with_stride(
            (int32_t)stride, elements, element_count, out_declaration) == CNA_RESULT_SUCCESS;
}

static int validate_device_extensions(CNA_Handle graphics_device);

static int validate_draw_and_extensions(CNA_Handle graphics_device)
{
    int32_t vertex_count = 0;
    if (cna_primitive_type_get_vertex_count(
            CNA_PRIMITIVE_TRIANGLE_LIST, 2, &vertex_count) != CNA_RESULT_SUCCESS ||
        vertex_count != 6 ||
        cna_primitive_type_get_vertex_count(
            CNA_PRIMITIVE_TRIANGLE_STRIP, 2, &vertex_count) != CNA_RESULT_SUCCESS ||
        vertex_count != 4 ||
        cna_primitive_type_get_vertex_count(
            CNA_PRIMITIVE_LINE_LIST, 3, &vertex_count) != CNA_RESULT_SUCCESS ||
        vertex_count != 6 ||
        cna_primitive_type_get_vertex_count(
            CNA_PRIMITIVE_LINE_STRIP, 3, &vertex_count) != CNA_RESULT_SUCCESS ||
        vertex_count != 4 ||
        cna_primitive_type_get_vertex_count(
            CNA_PRIMITIVE_POINT_LIST_EXT, 5, &vertex_count) != CNA_RESULT_SUCCESS ||
        vertex_count != 5 ||
        cna_primitive_type_get_vertex_count(CNA_PRIMITIVE_TRIANGLE_LIST, 1, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Unknown topologies are rejected before any native draw. */
    if (cna_graphics_device_draw_primitives(
            graphics_device, UINT32_C(9), 0, 1) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_indexed_primitives(
            graphics_device, UINT32_C(9), 0, 0, 3, 0, 1) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_instanced_primitives(
            graphics_device, UINT32_C(9), 0, 0, 3, 0, 1, 1) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Without bindings, every buffered draw route fails; a 2D-only backend refuses it outright. */
    CNA_Bool supports_3d = CNA_FALSE;
    if (cna_graphics_device_supports_capability(
            graphics_device, CNA_GRAPHICS_CAPABILITY_THREE_D, &supports_3d) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    const CNA_Result expected_without_bindings =
        supports_3d == CNA_TRUE ? CNA_RESULT_INTERNAL : CNA_RESULT_NOT_SUPPORTED;
    if (cna_graphics_device_draw_primitives(
            graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 1) !=
            expected_without_bindings ||
        cna_graphics_device_draw_indexed_primitives(
            graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1) !=
            expected_without_bindings ||
        cna_graphics_device_draw_instanced_primitives(
            graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1, 2) !=
            expected_without_bindings) {
        return 0;
    }

    const CNA_VertexPositionColor vertices[3] = {
        {{0.0F, 0.0F, 0.0F}, {255U, 0U, 0U, 255U}},
        {{1.0F, 0.0F, 0.0F}, {0U, 255U, 0U, 255U}},
        {{0.0F, 1.0F, 0.0F}, {0U, 0U, 255U, 255U}}};
    const uint16_t indices16[3] = {0U, 1U, 2U};
    const uint32_t indices32[3] = {0U, 1U, 2U};

    CNA_UserPrimitives primitives = {
        sizeof(CNA_UserPrimitives), UINT32_C(1), CNA_PRIMITIVE_TRIANGLE_LIST,
        CNA_USER_VERTEX_SOURCE_POSITION_COLOR, vertices, CNA_INVALID_HANDLE, 0, 3, 1, 0U};
    CNA_UserIndices user_indices = {
        sizeof(CNA_UserIndices), UINT32_C(1), CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, 0, indices16};

    /* Structural validation happens before the device is reached. */
    CNA_UserPrimitives malformed = primitives;
    malformed.vertex_data = 0;
    CNA_UserPrimitives bad_count = primitives;
    bad_count.primitive_count = 0;
    CNA_UserPrimitives bad_source = primitives;
    bad_source.vertex_source = UINT32_C(9);
    CNA_UserIndices bad_indices = user_indices;
    bad_indices.index_element_size = UINT32_C(4);
    if (cna_graphics_device_draw_user_primitives(graphics_device, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_primitives(graphics_device, &malformed) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_primitives(graphics_device, &bad_count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_primitives(graphics_device, &bad_source) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_indexed_primitives(
            graphics_device, &primitives, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_indexed_primitives(
            graphics_device, &primitives, &bad_indices) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_primitives(0, &primitives) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    if (supports_3d != CNA_TRUE) {
        /* A 2D-only backend refuses every draw route; nothing further is observable here. */
        return cna_graphics_device_draw_user_primitives(graphics_device, &primitives) ==
            CNA_RESULT_NOT_SUPPORTED &&
            cna_graphics_device_draw_user_indexed_primitives(
                graphics_device, &primitives, &user_indices) == CNA_RESULT_NOT_SUPPORTED &&
            validate_device_extensions(graphics_device);
    }

    /* A raw stream without a declaration would read native objects, so it is refused. */
    CNA_UserPrimitives raw = primitives;
    raw.vertex_source = CNA_USER_VERTEX_SOURCE_RAW_STREAM;
    if (cna_graphics_device_draw_user_primitives(graphics_device, &raw) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_draw_user_indexed_primitives(
            graphics_device, &raw, &user_indices) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Every user-primitive route needs a current effect and reaches the backend with one. */
    CNA_EffectHandle basic_effect = CNA_INVALID_HANDLE;
    if (cna_basic_effect_create(graphics_device, &basic_effect) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_graphics_device_set_current_effect(graphics_device, basic_effect) !=
        CNA_RESULT_SUCCESS) {
        (void)cna_effect_destroy(basic_effect);
        return 0;
    }

    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    int drew = make_position_color_declaration(&declaration);
    float stream[3 * 4];
    for (size_t index = 0U; drew && index < 2U; ++index) {
        /* Slot 0 uses the converting typed route; slot 1 uses a raw GPU stream. */
        CNA_UserPrimitives request = primitives;
        if (index == 1U) {
            for (size_t vertex = 0U; vertex < 3U; ++vertex) {
                stream[vertex * 4U + 0U] = vertices[vertex].position.x;
                stream[vertex * 4U + 1U] = vertices[vertex].position.y;
                stream[vertex * 4U + 2U] = vertices[vertex].position.z;
                stream[vertex * 4U + 3U] = 0.0F;
            }
            request.vertex_source = CNA_USER_VERTEX_SOURCE_RAW_STREAM;
            request.vertex_data = stream;
            request.vertex_declaration = declaration;
        }
        user_indices.index_element_size = CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS;
        user_indices.index_data = indices16;
        drew = is_supported(
                   cna_graphics_device_draw_user_primitives(graphics_device, &request)) &&
            is_supported(cna_graphics_device_draw_user_indexed_primitives(
                graphics_device, &request, &user_indices));
        if (drew) {
            user_indices.index_element_size = CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS;
            user_indices.index_data = indices32;
            drew = is_supported(cna_graphics_device_draw_user_indexed_primitives(
                graphics_device, &request, &user_indices));
        }
    }

    if (declaration != CNA_INVALID_HANDLE) {
        (void)cna_vertex_declaration_destroy(declaration);
    }
    if (cna_graphics_device_set_current_effect(graphics_device, CNA_INVALID_HANDLE) !=
            CNA_RESULT_SUCCESS ||
        cna_effect_destroy(basic_effect) != CNA_RESULT_SUCCESS || !drew) {
        return 0;
    }

    return validate_device_extensions(graphics_device);
}

static int validate_device_extensions(CNA_Handle graphics_device)
{
    /* CNA extension state. */
    uint64_t tracked = UINT64_MAX;
    CNA_Unsupported3DGraphicsCallBehavior behavior = UINT32_MAX;
    CNA_Bool supports_3d = CNA_FALSE;
    if (cna_graphics_device_supports_capability(
            graphics_device, CNA_GRAPHICS_CAPABILITY_THREE_D, &supports_3d) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The pipeline-state extensions need a 3D backend and refuse one that has none. */
    const CNA_Result expected_pipeline_state =
        supports_3d == CNA_TRUE ? CNA_RESULT_SUCCESS : CNA_RESULT_NOT_SUPPORTED;
    if (cna_graphics_device_get_tracked_resource_count(graphics_device, &tracked) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_tracked_resource_count(graphics_device, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_depth_test_enabled(graphics_device, CNA_TRUE) !=
            expected_pipeline_state ||
        cna_graphics_device_set_depth_test_enabled(graphics_device, (CNA_Bool)2) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_blend_enabled(graphics_device, CNA_FALSE) !=
            expected_pipeline_state ||
        cna_graphics_device_set_depth_write_enabled(graphics_device, CNA_TRUE) !=
            expected_pipeline_state ||
        cna_graphics_device_set_context_recovery_enabled(graphics_device, CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_graphics_profile_ext(
            graphics_device, CNA_GRAPHICS_PROFILE_REACH) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_graphics_profile_ext(graphics_device, UINT32_C(5)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    static const char marker[] = "cna-c-api";
    const CNA_StringView marker_view = {marker, sizeof(marker) - 1U};
    const CNA_StringView invalid_marker = {"\xC0\x80", 2U};
    if (cna_graphics_device_set_string_marker_ext(graphics_device, marker_view) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_string_marker_ext(graphics_device, invalid_marker) !=
            CNA_RESULT_ENCODING) {
        return 0;
    }

    if (cna_graphics_device_get_unsupported_3d_call_behavior(graphics_device, &behavior) !=
            CNA_RESULT_SUCCESS ||
        behavior != CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW ||
        cna_graphics_device_set_unsupported_3d_call_behavior(
            graphics_device, CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_unsupported_3d_call_behavior(graphics_device, &behavior) !=
            CNA_RESULT_SUCCESS ||
        behavior != CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB ||
        cna_graphics_device_set_unsupported_3d_call_behavior(graphics_device, UINT32_C(7)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_set_unsupported_3d_call_behavior(
            graphics_device, CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A retained current effect stays valid after its own handle is destroyed. */
    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    if (cna_effect_create_empty(graphics_device, &effect) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_graphics_device_set_current_effect(graphics_device, effect) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_current_effect(graphics_device, CNA_INVALID_HANDLE) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_current_effect(graphics_device, graphics_device) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_effect_destroy(effect) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    return cna_graphics_device_recreate_renderer_for_multi_sample_count_ext(
               graphics_device, -1) == CNA_RESULT_INVALID_ARGUMENT;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    (void)out_error;
    DeviceState* const state = (DeviceState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        !validate_device_state(graphics_device) ||
        !validate_texture_collections(graphics_device) ||
        !validate_device_events(graphics_device, state) ||
        !validate_frame_control(graphics_device, state) ||
        !validate_backbuffer_window(graphics_device) ||
        !validate_buffer_binding(graphics_device) ||
        !validate_draw_and_extensions(graphics_device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stale_device = graphics_device;
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int validate_device(void)
{
    DeviceState state = {
        {0, 0, 0, 0, 0, 0, 0, 0, CNA_INVALID_HANDLE},
        CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        0
    };
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    static const char title[] = "C API graphics device";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.validated != 1) {
        /* Never leave a live game behind: process teardown with one still owned is undefined. */
        (void)cna_game_destroy(game);
        return 0;
    }

    /* The borrowed device handle expires when its callback returns. */
    CNA_Bool disposed = CNA_FALSE;
    if (cna_graphics_device_get_is_disposed(state.stale_device, &disposed) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_device_get_status(state.stale_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Destroying the game disposes the device, which reaches the live Disposing subscription.
       Every successful reset raised its resetting/reset pair; the device was never lost. */
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS || state.counters.disposing != 1 ||
        state.counters.observed_device != state.stale_device ||
        state.counters.device_lost != 0 ||
        state.counters.device_reset != state.counters.device_resetting) {
        return 0;
    }

    /* Surviving registration handles release cleanly once the device is gone. */
    if (cna_graphics_device_unsubscribe(state.disposing_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unsubscribe(state.lost_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unsubscribe(state.reset_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unsubscribe(state.resetting_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unsubscribe(state.destroyed_registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_unsubscribe(state.destroyed_registration) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!validate_identities()) {
        return 1;
    }
    if (!validate_construction()) {
        return 2;
    }
    if (!validate_properties()) {
        return 3;
    }
    if (!validate_transforms()) {
        return 4;
    }
    if (!validate_string()) {
        return 5;
    }
    if (!validate_device()) {
        return 6;
    }
    return 0;
}
