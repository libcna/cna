// SPDX-License-Identifier: MS-PL

#include <stdio.h>
#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Every route below is a separate claim, and a bare exit code cannot say which one broke.
 * REPORT names the stage the moment it fails, the way the sibling smoke programs do.
 */
#define REPORT(condition) ((condition) ? 1 : (fprintf(stderr, \
    "Draw3DSmoke failure at line %d: %s\n", __LINE__, #condition), 0))

typedef struct Draw3DState {
    CNA_Bool supports_3d;
    int has_readback;
    int drew_geometry;
    int drew_user_primitives;
    int drew_indexed;
    int drew_model;
    int validated;
} Draw3DState;

static const CNA_Matrix Identity = {
    1.0F, 0.0F, 0.0F, 0.0F,
    0.0F, 1.0F, 0.0F, 0.0F,
    0.0F, 0.0F, 1.0F, 0.0F,
    0.0F, 0.0F, 0.0F, 1.0F};

/* A large clip-space triangle whose interior covers the middle of the backbuffer. */
static const CNA_VertexPositionColor Triangle[3] = {
    {{-0.9F, -0.9F, 0.0F}, {255U, 0U, 0U, 255U}},
    {{0.9F, -0.9F, 0.0F}, {0U, 255U, 0U, 255U}},
    {{0.0F, 0.9F, 0.0F}, {0U, 0U, 255U, 255U}}};

static const uint16_t TriangleIndices[3] = {0U, 1U, 2U};

static int make_declaration(CNA_VertexDeclarationHandle* out_declaration)
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

static int make_basic_effect(CNA_Handle graphics_device, CNA_EffectHandle* out_effect)
{
    return cna_basic_effect_create(graphics_device, out_effect) == CNA_RESULT_SUCCESS &&
        cna_basic_effect_set_vertex_color_enabled(*out_effect, CNA_TRUE) ==
            CNA_RESULT_SUCCESS &&
        cna_effect_matrices_set_world(*out_effect, Identity) == CNA_RESULT_SUCCESS &&
        cna_effect_matrices_set_view(*out_effect, Identity) == CNA_RESULT_SUCCESS &&
        cna_effect_matrices_set_projection(*out_effect, Identity) == CNA_RESULT_SUCCESS &&
        cna_effect_apply(*out_effect) == CNA_RESULT_SUCCESS;
}

/*
 * Reads the exact center pixel of the logical backbuffer.
 *
 * Returns the raw result so a backend without honest readback (HEADLESS) is distinguished from a
 * genuine failure: 3D support and pixel readback are independent capabilities.
 */
static CNA_Result read_center_pixel(CNA_Handle graphics_device, CNA_Color* out_pixel)
{
    CNA_BackBufferInfo info = {sizeof(CNA_BackBufferInfo), UINT32_C(1), 0U, 0U, 0U, 0U};
    if (cna_graphics_device_get_backbuffer_info(graphics_device, &info) != CNA_RESULT_SUCCESS ||
        info.width < 2U || info.height < 2U) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_BackBufferReadback readback = {
        sizeof(CNA_BackBufferReadback), UINT32_C(1), CNA_TRUE, {0U, 0U, 0U},
        {(int32_t)(info.width / 2U), (int32_t)(info.height / 2U), 1, 1}, 0U, 1U};
    return cna_graphics_device_get_backbuffer_data_window(
        graphics_device, &readback, out_pixel, 1U);
}

static int colors_equal(const CNA_Color left, const CNA_Color right)
{
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

/* Every 3D route must refuse deterministically on a backend without the capability. */
static int validate_refusal(CNA_Handle graphics_device)
{
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    if (!make_declaration(&declaration)) {
        return 0;
    }
    const CNA_VertexBufferCreateInfo buffer_info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), declaration, 3,
        CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    CNA_VertexBufferHandle buffer = CNA_INVALID_HANDLE;
    const int refused_buffer =
        cna_vertex_buffer_create(graphics_device, &buffer_info, &buffer) ==
            CNA_RESULT_NOT_SUPPORTED &&
        buffer == CNA_INVALID_HANDLE;

    const CNA_UserPrimitives primitives = {
        sizeof(CNA_UserPrimitives), UINT32_C(1), CNA_PRIMITIVE_TRIANGLE_LIST,
        CNA_USER_VERTEX_SOURCE_POSITION_COLOR, Triangle, CNA_INVALID_HANDLE, 0, 3, 1, 0U};
    const CNA_UserIndices indices = {
        sizeof(CNA_UserIndices), UINT32_C(1), CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, 0,
        TriangleIndices};
    const int refused_draws =
        cna_graphics_device_draw_primitives(
            graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 1) == CNA_RESULT_NOT_SUPPORTED &&
        cna_graphics_device_draw_indexed_primitives(
            graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_graphics_device_draw_instanced_primitives(
            graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1, 2) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_graphics_device_draw_user_primitives(graphics_device, &primitives) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_graphics_device_draw_user_indexed_primitives(
            graphics_device, &primitives, &indices) == CNA_RESULT_NOT_SUPPORTED;

    return cna_vertex_declaration_destroy(declaration) == CNA_RESULT_SUCCESS &&
        refused_buffer && refused_draws;
}

/*
 * Clears to a known color. When the backend has honest readback this also proves the center pixel
 * really holds that color, so a later "changed" assertion means the draw did it.
 */
static int clear_and_confirm(
    CNA_Handle graphics_device,
    const Draw3DState* state,
    CNA_Color clear_color)
{
    CNA_Color pixel = {0U, 0U, 0U, 0U};
    /*
     * Depth as well as colour, and that is the whole of what was wrong with this test. It used to
     * clear CNA_CLEAR_OPTION_TARGET alone, which leaves the depth buffer holding whatever the
     * window system handed over -- undefined before the first clear in both D3D and GL.
     * DepthStencilState.Default keeps depth testing on and the triangle sits at z = 0, so against
     * undefined depth the LessEqual test could discard every fragment: the draw then reported
     * success while changing no pixel, which reads exactly like a broken draw path and is not one.
     * Clearing target, depth and stencil together is what XNA's own Clear(Color) does.
     */
    if (cna_graphics_device_clear_options(
            graphics_device,
            CNA_CLEAR_OPTION_TARGET | CNA_CLEAR_OPTION_DEPTH_BUFFER | CNA_CLEAR_OPTION_STENCIL,
            clear_color, 1.0F, 0) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (!state->has_readback) {
        return 1;
    }
    if (read_center_pixel(graphics_device, &pixel) != CNA_RESULT_SUCCESS) {
        printf("clear_and_confirm: the centre pixel could not be read back\n");
        return 0;
    }
    if (!colors_equal(pixel, clear_color)) {
        printf("clear_and_confirm: centre pixel is (%u,%u,%u,%u), expected the clear colour "
               "(%u,%u,%u,%u)\n",
               pixel.r, pixel.g, pixel.b, pixel.a,
               clear_color.r, clear_color.g, clear_color.b, clear_color.a);
        return 0;
    }
    return 1;
}

/* Confirms the center pixel no longer holds the clear color, where readback exists. */
static int confirm_drawn(
    CNA_Handle graphics_device,
    const Draw3DState* state,
    CNA_Color clear_color)
{
    CNA_Color pixel = {0U, 0U, 0U, 0U};
    if (!state->has_readback) {
        return 1;
    }
    if (read_center_pixel(graphics_device, &pixel) != CNA_RESULT_SUCCESS) {
        printf("confirm_drawn: the centre pixel could not be read back\n");
        return 0;
    }
    if (colors_equal(pixel, clear_color)) {
        printf("confirm_drawn: centre pixel is still the clear colour (%u,%u,%u,%u) -- the draw "
               "reported success and changed nothing\n",
               pixel.r, pixel.g, pixel.b, pixel.a);
        return 0;
    }
    return 1;
}

/* Draws real geometry and proves the backbuffer changed where the triangle covers it. */
static int validate_real_output(CNA_Handle graphics_device, Draw3DState* state)
{
    const CNA_Color clear_color = {8U, 16U, 24U, 255U};

    /* Winding must not decide whether this evidence exists, so culling is switched off. */
    CNA_RasterizerState rasterizer;
    if (cna_rasterizer_state_init(CNA_RASTERIZER_STATE_PRESET_CULL_NONE, &rasterizer) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_rasterizer_state(graphics_device, &rasterizer) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    if (!make_basic_effect(graphics_device, &effect)) {
        return 0;
    }

    /* 1. A user-primitive draw with converted built-in vertices. */
    int ok = clear_and_confirm(graphics_device, state, clear_color);
    if (ok) {
        const CNA_UserPrimitives primitives = {
            sizeof(CNA_UserPrimitives), UINT32_C(1), CNA_PRIMITIVE_TRIANGLE_LIST,
            CNA_USER_VERTEX_SOURCE_POSITION_COLOR, Triangle, CNA_INVALID_HANDLE, 0, 3, 1, 0U};
        ok = REPORT(cna_graphics_device_draw_user_primitives(
                        graphics_device, &primitives) == CNA_RESULT_SUCCESS) &&
            REPORT(confirm_drawn(graphics_device, state, clear_color));
        state->drew_user_primitives = ok;
    }

    /* 2. The same geometry through the indexed user-primitive route. */
    if (ok) {
        const CNA_UserPrimitives primitives = {
            sizeof(CNA_UserPrimitives), UINT32_C(1), CNA_PRIMITIVE_TRIANGLE_LIST,
            CNA_USER_VERTEX_SOURCE_POSITION_COLOR, Triangle, CNA_INVALID_HANDLE, 0, 3, 1, 0U};
        const CNA_UserIndices indices = {
            sizeof(CNA_UserIndices), UINT32_C(1), CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, 0,
            TriangleIndices};
        ok = REPORT(clear_and_confirm(graphics_device, state, clear_color)) &&
            REPORT(cna_graphics_device_draw_user_indexed_primitives(
                       graphics_device, &primitives, &indices) == CNA_RESULT_SUCCESS) &&
            REPORT(confirm_drawn(graphics_device, state, clear_color));
        state->drew_indexed = ok;
    }

    /* 3. Buffered geometry through owned vertex and index buffers. */
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    CNA_VertexBufferHandle vertex_buffer = CNA_INVALID_HANDLE;
    CNA_IndexBufferHandle index_buffer = CNA_INVALID_HANDLE;
    if (ok) {
        ok = make_declaration(&declaration);
    }
    if (ok) {
        const CNA_VertexBufferCreateInfo buffer_info = {
            sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), declaration, 3,
            CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
        const CNA_VertexBufferTransfer transfer = {
            sizeof(CNA_VertexBufferTransfer), UINT32_C(1),
            CNA_VERTEX_TYPE_POSITION_COLOR, CNA_SET_DATA_NONE, 0, 3};
        ok = cna_vertex_buffer_create(graphics_device, &buffer_info, &vertex_buffer) ==
                CNA_RESULT_SUCCESS &&
            cna_vertex_buffer_set_data(vertex_buffer, &transfer, Triangle, 3U) ==
                CNA_RESULT_SUCCESS;
    }
    if (ok) {
        const CNA_IndexBufferCreateInfo index_info = {
            sizeof(CNA_IndexBufferCreateInfo), UINT32_C(1), 3,
            CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, CNA_FALSE,
            {0U, 0U, 0U}};
        const CNA_IndexBufferTransfer transfer = {
            sizeof(CNA_IndexBufferTransfer), UINT32_C(1),
            CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_SET_DATA_NONE, 0, 3};
        ok = cna_index_buffer_create(graphics_device, &index_info, &index_buffer) ==
                CNA_RESULT_SUCCESS &&
            cna_index_buffer_set_data(index_buffer, &transfer, TriangleIndices, 3U) ==
                CNA_RESULT_SUCCESS;
    }
    if (ok) {
        ok = clear_and_confirm(graphics_device, state, clear_color) &&
            cna_graphics_device_set_vertex_buffer(graphics_device, vertex_buffer) ==
                CNA_RESULT_SUCCESS &&
            cna_graphics_device_set_index_buffer(graphics_device, index_buffer) ==
                CNA_RESULT_SUCCESS &&
            REPORT(cna_graphics_device_draw_primitives(
                       graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 1) ==
                   CNA_RESULT_SUCCESS) &&
            REPORT(confirm_drawn(graphics_device, state, clear_color));
    }
    if (ok) {
        ok = clear_and_confirm(graphics_device, state, clear_color) &&
            REPORT(cna_graphics_device_draw_indexed_primitives(
                       graphics_device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1) ==
                   CNA_RESULT_SUCCESS) &&
            REPORT(confirm_drawn(graphics_device, state, clear_color));
        state->drew_geometry = ok;
    }

    /* 4. The same geometry through an owned Model, exercising the model draw route. */
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle mesh = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle root = CNA_INVALID_HANDLE;
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    if (ok) {
        ok = cna_model_mesh_part_create(
                 vertex_buffer, index_buffer, 3, 1, 0, 0, &part) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_effect(part, effect) == CNA_RESULT_SUCCESS;
    }
    if (ok) {
        const CNA_StringView mesh_name = {"triangle", 8U};
        ok = cna_model_mesh_create_named(graphics_device, mesh_name, &part, 1U, &mesh) ==
            CNA_RESULT_SUCCESS;
    }
    if (ok) {
        const CNA_StringView root_name = {"root", 4U};
        ok = cna_model_bone_create(0, root_name, &root) == CNA_RESULT_SUCCESS;
    }
    if (ok) {
        ok = cna_model_create(graphics_device, &root, 1U, &mesh, 1U, &model) ==
            CNA_RESULT_SUCCESS;
    }
    if (ok) {
        ok = clear_and_confirm(graphics_device, state, clear_color) &&
            REPORT(cna_model_draw(model, Identity, Identity, Identity) ==
                   CNA_RESULT_SUCCESS) &&
            REPORT(confirm_drawn(graphics_device, state, clear_color));
        state->drew_model = ok;
    }

    if (model != CNA_INVALID_HANDLE) {
        ok = cna_model_destroy(model) == CNA_RESULT_SUCCESS && ok;
    }
    if (root != CNA_INVALID_HANDLE) {
        ok = cna_model_bone_destroy(root) == CNA_RESULT_SUCCESS && ok;
    }
    if (mesh != CNA_INVALID_HANDLE) {
        ok = cna_model_mesh_destroy(mesh) == CNA_RESULT_SUCCESS && ok;
    }
    if (part != CNA_INVALID_HANDLE) {
        ok = cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS && ok;
    }
    if (index_buffer != CNA_INVALID_HANDLE) {
        ok = cna_graphics_device_set_index_buffer(graphics_device, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_index_buffer_destroy(index_buffer) == CNA_RESULT_SUCCESS && ok;
    }
    if (vertex_buffer != CNA_INVALID_HANDLE) {
        ok = cna_graphics_device_set_vertex_buffer(graphics_device, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_vertex_buffer_destroy(vertex_buffer) == CNA_RESULT_SUCCESS && ok;
    }
    if (declaration != CNA_INVALID_HANDLE) {
        ok = cna_vertex_declaration_destroy(declaration) == CNA_RESULT_SUCCESS && ok;
    }
    ok = cna_graphics_device_set_current_effect(graphics_device, CNA_INVALID_HANDLE) ==
            CNA_RESULT_SUCCESS &&
        cna_effect_destroy(effect) == CNA_RESULT_SUCCESS && ok;
    return ok;
}

static CNA_Result on_draw(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    (void)game_time;
    (void)out_error;
    Draw3DState* const state = (Draw3DState*)context;
    if (state->validated != 0) {
        return CNA_RESULT_SUCCESS;
    }

    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_supports_capability(
            graphics_device, CNA_GRAPHICS_CAPABILITY_THREE_D, &state->supports_3d) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    /* Pixel readback is a separate capability from 3D: HEADLESS draws but cannot be read back. */
    CNA_Color probe = {0U, 0U, 0U, 0U};
    const CNA_Result readback = read_center_pixel(graphics_device, &probe);
    if (readback != CNA_RESULT_SUCCESS && readback != CNA_RESULT_NOT_SUPPORTED) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->has_readback = readback == CNA_RESULT_SUCCESS;

    const int ok = state->supports_3d == CNA_TRUE
        ? validate_real_output(graphics_device, state)
        : validate_refusal(graphics_device);
    if (!ok) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    Draw3DState state = {CNA_FALSE, 0, 0, 0, 0, 0, 0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, 0, on_draw, 0, 0, &state};
    static const char title[] = "C API 3D draw";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        return 1;
    }
    const int ran = cna_game_run_one_frame(game) == CNA_RESULT_SUCCESS && state.validated == 1;
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS || !ran) {
        return 2;
    }

    /* A 3D-capable backend must have produced observable output on every route. */
    if (state.supports_3d == CNA_TRUE &&
        (state.drew_user_primitives == 0 || state.drew_indexed == 0 ||
         state.drew_geometry == 0 || state.drew_model == 0)) {
        return 3;
    }
    return 0;
}
