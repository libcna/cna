// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_GraphicsResourceTag) == 8U,
               "CNA_GraphicsResourceTag size changed");
_Static_assert(sizeof(CNA_GraphicsResourceEventRegistrationHandle) == 8U,
               "CNA_GraphicsResourceEventRegistrationHandle size changed");

typedef struct DisposingState {
    CNA_Handle expected_resource;
    int count;
    int callback_valid;
} DisposingState;

typedef struct WrongThreadState {
    CNA_Handle resource;
    CNA_GraphicsResourceEventRegistrationHandle registration;
    CNA_Result query_result;
    CNA_Result unsubscribe_result;
} WrongThreadState;

typedef struct LifecycleState {
    CNA_Handle borrowed_device;
    int disposing_count;
    int validated;
} LifecycleState;

static void on_disposing(const CNA_Handle resource, void* const context)
{
    DisposingState* const state = (DisposingState*)context;
    CNA_Bool disposed = CNA_TRUE;
    state->callback_valid = resource == state->expected_resource &&
        cna_graphics_resource_get_is_disposed(resource, &disposed) == CNA_RESULT_SUCCESS &&
        disposed == CNA_FALSE;
    ++state->count;
}

static void on_texture_disposing(const CNA_Handle resource, void* const context)
{
    LifecycleState* const state = (LifecycleState*)context;
    CNA_Bool disposed = CNA_TRUE;
    if (cna_graphics_resource_get_is_disposed(resource, &disposed) == CNA_RESULT_SUCCESS &&
        disposed == CNA_FALSE) {
        ++state->disposing_count;
    }
}

static int use_resource_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_Bool disposed = CNA_FALSE;
    state->query_result =
        cna_graphics_resource_get_is_disposed(state->resource, &disposed);
    state->unsubscribe_result =
        cna_graphics_resource_unsubscribe_disposing(state->registration);
    return 0;
}

static int validate_strings(const CNA_VertexDeclarationHandle declaration)
{
    static const char TypeName[] =
        "Microsoft.Xna.Framework.Graphics.VertexDeclaration";
    static const char Utf8Name[] = {'v', (char)0xc3, (char)0xa9, 'r', 't', 'e', 'x'};
    static const char InvalidUtf8[] = {(char)0xc3, '('};
    static const char EmbeddedNull[] = {'a', '\0', 'b'};
    uint64_t count = UINT64_MAX;
    char bytes[sizeof(TypeName) - 1U];
    char too_small = 'x';

    if (cna_graphics_resource_get_name_byte_count(declaration, &count) !=
            CNA_RESULT_SUCCESS || count != 0U ||
        cna_graphics_resource_copy_name(declaration, 0, 0U, &count) !=
            CNA_RESULT_SUCCESS || count != 0U ||
        cna_graphics_resource_get_string_byte_count(declaration, &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(TypeName) - 1U ||
        cna_graphics_resource_copy_string(
            declaration, &too_small, 1U, &count) != CNA_RESULT_BUFFER_TOO_SMALL ||
        too_small != 'x' || count != sizeof(TypeName) - 1U ||
        cna_graphics_resource_copy_string(
            declaration, bytes, sizeof(bytes), &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(bytes) || memcmp(bytes, TypeName, sizeof(bytes)) != 0) {
        return 0;
    }

    if (cna_graphics_resource_set_name(
            declaration, (CNA_StringView){Utf8Name, sizeof(Utf8Name)}) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_name_byte_count(declaration, &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(Utf8Name) ||
        cna_graphics_resource_copy_name(
            declaration, bytes, sizeof(bytes), &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(Utf8Name) || memcmp(bytes, Utf8Name, sizeof(Utf8Name)) != 0 ||
        cna_graphics_resource_get_string_byte_count(declaration, &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(Utf8Name) ||
        cna_graphics_resource_copy_string(
            declaration, bytes, sizeof(bytes), &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(Utf8Name) || memcmp(bytes, Utf8Name, sizeof(Utf8Name)) != 0) {
        return 0;
    }

    if (cna_graphics_resource_set_name(
            declaration, (CNA_StringView){InvalidUtf8, sizeof(InvalidUtf8)}) !=
            CNA_RESULT_ENCODING ||
        cna_graphics_resource_set_name(
            declaration, (CNA_StringView){EmbeddedNull, sizeof(EmbeddedNull)}) !=
            CNA_RESULT_ENCODING ||
        cna_graphics_resource_get_name_byte_count(declaration, &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(Utf8Name)) {
        return 0;
    }
    return 1;
}

static int validate_standalone_resource(void)
{
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    CNA_Handle graphics_device = UINT64_MAX;
    CNA_Bool disposed = CNA_TRUE;
    CNA_GraphicsResourceTag tag = UINT64_MAX;
    DisposingState disposing = {CNA_INVALID_HANDLE, 0, 0};
    CNA_GraphicsResourceEventRegistrationHandle registration = CNA_INVALID_HANDLE;

    if (cna_vertex_declaration_create_empty(&declaration) != CNA_RESULT_SUCCESS ||
        declaration == CNA_INVALID_HANDLE ||
        cna_graphics_resource_get_graphics_device(declaration, &graphics_device) !=
            CNA_RESULT_SUCCESS || graphics_device != CNA_INVALID_HANDLE ||
        cna_graphics_resource_get_is_disposed(declaration, &disposed) !=
            CNA_RESULT_SUCCESS || disposed != CNA_FALSE || !validate_strings(declaration) ||
        cna_graphics_resource_get_tag(declaration, &tag) != CNA_RESULT_SUCCESS || tag != 0U ||
        cna_graphics_resource_set_tag(
            declaration, UINT64_C(0xfedcba9876543210)) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_tag(declaration, &tag) != CNA_RESULT_SUCCESS ||
        tag != UINT64_C(0xfedcba9876543210)) {
        return 0;
    }

    disposing.expected_resource = declaration;
    if (cna_graphics_resource_subscribe_disposing(
            declaration, on_disposing, &disposing, &registration) != CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE ||
        cna_graphics_resource_dispose(declaration) != CNA_RESULT_SUCCESS ||
        disposing.count != 1 || disposing.callback_valid != 1 ||
        cna_graphics_resource_get_is_disposed(declaration, &disposed) !=
            CNA_RESULT_SUCCESS || disposed != CNA_TRUE ||
        cna_graphics_resource_dispose(declaration) != CNA_RESULT_SUCCESS ||
        disposing.count != 1 ||
        cna_graphics_resource_unsubscribe_disposing(registration) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_unsubscribe_disposing(registration) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_vertex_declaration_destroy(declaration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_destroy_event_and_threads(void)
{
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    CNA_GraphicsResourceEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    DisposingState disposing = {CNA_INVALID_HANDLE, 0, 0};
    if (cna_vertex_declaration_create_empty(&declaration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    disposing.expected_resource = declaration;
    if (cna_graphics_resource_subscribe_disposing(
            declaration, on_disposing, &disposing, &registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    WrongThreadState wrong_thread = {
        declaration, registration, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, use_resource_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.query_result != CNA_RESULT_THREAD ||
        wrong_thread.unsubscribe_result != CNA_RESULT_THREAD ||
        cna_vertex_declaration_destroy(declaration) != CNA_RESULT_SUCCESS ||
        disposing.count != 1 || disposing.callback_valid != 1 ||
        cna_graphics_resource_unsubscribe_disposing(registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_failures(void)
{
    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_Handle graphics_device = UINT64_MAX;
    CNA_Bool disposed = CNA_TRUE;
    uint64_t count = UINT64_C(77);
    CNA_GraphicsResourceTag tag = UINT64_C(77);
    CNA_GraphicsResourceEventRegistrationHandle registration = UINT64_MAX;
    char byte = 'x';
    if (cna_curve_create(&wrong_kind) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_graphics_device(wrong_kind, &graphics_device) !=
            CNA_RESULT_INVALID_HANDLE || graphics_device != CNA_INVALID_HANDLE ||
        cna_graphics_resource_get_is_disposed(wrong_kind, &disposed) !=
            CNA_RESULT_INVALID_HANDLE || disposed != CNA_TRUE ||
        cna_graphics_resource_get_name_byte_count(wrong_kind, &count) !=
            CNA_RESULT_INVALID_HANDLE || count != UINT64_C(77) ||
        cna_graphics_resource_copy_name(wrong_kind, &byte, 1U, &count) !=
            CNA_RESULT_INVALID_HANDLE || byte != 'x' || count != UINT64_C(77) ||
        cna_graphics_resource_set_name(wrong_kind, (CNA_StringView){"x", 1U}) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_resource_get_string_byte_count(wrong_kind, &count) !=
            CNA_RESULT_INVALID_HANDLE || count != UINT64_C(77) ||
        cna_graphics_resource_copy_string(wrong_kind, &byte, 1U, &count) !=
            CNA_RESULT_INVALID_HANDLE || byte != 'x' || count != UINT64_C(77) ||
        cna_graphics_resource_get_tag(wrong_kind, &tag) != CNA_RESULT_INVALID_HANDLE ||
        tag != UINT64_C(77) ||
        cna_graphics_resource_set_tag(wrong_kind, UINT64_C(1)) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_resource_dispose(wrong_kind) != CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_resource_subscribe_disposing(
            wrong_kind, on_disposing, 0, &registration) != CNA_RESULT_INVALID_HANDLE ||
        registration != CNA_INVALID_HANDLE) {
        return 0;
    }

    registration = UINT64_MAX;
    if (cna_graphics_resource_get_graphics_device(wrong_kind, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_resource_get_is_disposed(wrong_kind, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_resource_get_name_byte_count(wrong_kind, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_resource_copy_name(wrong_kind, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || count != UINT64_C(77) ||
        cna_graphics_resource_get_string_byte_count(wrong_kind, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_resource_copy_string(wrong_kind, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || count != UINT64_C(77) ||
        cna_graphics_resource_get_tag(wrong_kind, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_resource_subscribe_disposing(
            wrong_kind, 0, 0, &registration) != CNA_RESULT_INVALID_ARGUMENT ||
        registration != CNA_INVALID_HANDLE ||
        cna_graphics_resource_subscribe_disposing(
            wrong_kind, on_disposing, 0, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_resource_unsubscribe_disposing(CNA_INVALID_HANDLE) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_curve_destroy(wrong_kind) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)out_error;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    CNA_Handle resource_device = CNA_INVALID_HANDLE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle render_target_2d = CNA_INVALID_HANDLE;
    CNA_Handle render_target_cube = CNA_INVALID_HANDLE;
    CNA_GraphicsResourceEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    const CNA_RenderTarget2DCreateInfo target_2d_info = {
        sizeof(CNA_RenderTarget2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS, 0U};
    const CNA_RenderTargetCubeCreateInfo target_cube_info = {
        sizeof(CNA_RenderTargetCubeCreateInfo), UINT32_C(1), 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS};
    CNA_GraphicsResourceTag tag = 0U;
    static const char Name[] = "callback texture";
    uint64_t count = 0U;
    char copied[sizeof(Name) - 1U];

    if (cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        cna_texture2d_create(graphics_device, &texture_info, &texture) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_graphics_device(texture, &resource_device) !=
            CNA_RESULT_SUCCESS || resource_device != graphics_device ||
        cna_graphics_resource_set_name(
            texture, (CNA_StringView){Name, sizeof(Name) - 1U}) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_copy_name(
            texture, copied, sizeof(copied), &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(copied) || memcmp(copied, Name, sizeof(copied)) != 0 ||
        cna_graphics_resource_set_tag(texture, UINT64_C(42)) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_tag(texture, &tag) != CNA_RESULT_SUCCESS || tag != 42U ||
        cna_graphics_resource_subscribe_disposing(
            texture, on_texture_disposing, state, &registration) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS ||
        state->disposing_count != 1 ||
        cna_graphics_resource_unsubscribe_disposing(registration) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_render_target2d_create(
            graphics_device, &target_2d_info, &render_target_2d) != CNA_RESULT_SUCCESS ||
        cna_render_target_cube_create(
            graphics_device, &target_cube_info, &render_target_cube) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_graphics_device(render_target_2d, &resource_device) !=
            CNA_RESULT_SUCCESS || resource_device != graphics_device ||
        cna_graphics_resource_get_graphics_device(render_target_cube, &resource_device) !=
            CNA_RESULT_SUCCESS || resource_device != graphics_device ||
        cna_graphics_resource_set_tag(render_target_2d, UINT64_C(84)) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_tag(render_target_2d, &tag) != CNA_RESULT_SUCCESS ||
        tag != 84U ||
        cna_graphics_resource_set_tag(render_target_cube, UINT64_C(126)) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_tag(render_target_cube, &tag) != CNA_RESULT_SUCCESS ||
        tag != 126U ||
        cna_render_target_destroy(render_target_2d) != CNA_RESULT_SUCCESS ||
        cna_render_target_destroy(render_target_cube) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->borrowed_device = graphics_device;
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int validate_lifecycle_resource(void)
{
    LifecycleState state = {CNA_INVALID_HANDLE, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API graphics resource";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {Title, sizeof(Title) - 1U},
        &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_RendererInfo stale = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U};
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.validated != 1 ||
        state.disposing_count != 1 || state.borrowed_device == CNA_INVALID_HANDLE ||
        cna_graphics_device_get_renderer_info(state.borrowed_device, &stale) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!validate_standalone_resource()) {
        return 1;
    }
    if (!validate_destroy_event_and_threads()) {
        return 2;
    }
    if (!validate_failures()) {
        return 3;
    }
    return validate_lifecycle_resource() ? 0 : 4;
}
