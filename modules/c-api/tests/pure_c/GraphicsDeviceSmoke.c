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
        !validate_device_events(graphics_device, state)) {
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
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.validated != 1) {
        return 0;
    }

    /* The borrowed device handle expires when its callback returns. */
    CNA_Bool disposed = CNA_FALSE;
    if (cna_graphics_device_get_is_disposed(state.stale_device, &disposed) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_device_get_status(state.stale_device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Destroying the game disposes the device, which reaches the live Disposing subscription. */
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS || state.counters.disposing != 1 ||
        state.counters.observed_device != state.stale_device ||
        state.counters.device_lost != 0 || state.counters.device_reset != 0 ||
        state.counters.device_resetting != 0) {
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
