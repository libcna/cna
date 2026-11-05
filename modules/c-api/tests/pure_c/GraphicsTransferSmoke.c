/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include "CnaRuntimeSmoke.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return CNA_TEST_FAIL(1); } while (0)
static int failures(void)
{
    char bytes[16]; memset(bytes, '!', sizeof(bytes)); uint64_t required = 99;
    CNA_Texture3DTransfer volume = {sizeof(volume), 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1};
    CNA_TextureCubeTransfer cube = {sizeof(cube), 1, 0, 0, CNA_FALSE, {0}, {0,0,0,0}, 0, 0, 1};
    CHECK(cna_texture3d_get_data_typed_ext(0, 4, 0, &volume, bytes, 16, NULL) == CNA_RESULT_INVALID_ARGUMENT && bytes[0] == '!');
    CHECK(cna_texturecube_get_data_typed_ext(0, 4, 0, &cube, bytes, 16, NULL) == CNA_RESULT_INVALID_ARGUMENT && bytes[0] == '!');
    volume.start_index = UINT64_MAX;
    CHECK(cna_texture3d_get_data_typed_ext(0, 4, 0, &volume, bytes, 16, &required) == CNA_RESULT_OVERFLOW && bytes[0] == '!' && required == 99);
    cube.element_count = UINT64_MAX;
    CHECK(cna_texturecube_get_data_typed_ext(0, 4, 0, &cube, bytes, 16, &required) == CNA_RESULT_OVERFLOW && bytes[0] == '!' && required == 99);
    CHECK(cna_texture3d_set_data_typed_ext(0, 4, 0, NULL, bytes, 16) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_texturecube_set_data_typed_ext(0, 4, 0, NULL, bytes, 16) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_index_buffer_get_data_raw_ext(0, 0, 0, 0, 1, bytes, 16) == CNA_RESULT_INVALID_ARGUMENT && bytes[0] == '!');
    CHECK(cna_index_buffer_get_data_raw_ext(0, 0, 1, -1, 1, bytes, 16) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_index_buffer_get_data_raw_ext(0, UINT64_MAX, 1, 0, 1, bytes, 16) == CNA_RESULT_OVERFLOW);
    CHECK(cna_index_buffer_get_data_raw_ext(0, 0, UINT64_MAX, 0, 1, bytes, 16) == CNA_RESULT_OVERFLOW);
    CHECK(cna_index_buffer_get_data_raw_ext(0, 0, 1, 15, 2, bytes, 16) == CNA_RESULT_BUFFER_TOO_SMALL && bytes[0] == '!');
    CHECK(cna_index_buffer_get_data_raw_ext(0, 0, 1, 0, 1, bytes, 16) == CNA_RESULT_INVALID_HANDLE && bytes[0] == '!');
    CNA_BackBufferReadback readback = {sizeof(readback), 1, CNA_FALSE, {0}, {0,0,0,0}, 0, 1};
    CHECK(cna_graphics_device_get_backbuffer_data_raw_ext(0, &readback, 0, bytes, 16) == CNA_RESULT_INVALID_ARGUMENT && bytes[0] == '!');
    CHECK(cna_graphics_device_get_backbuffer_data_raw_ext(0, &readback, UINT64_MAX, bytes, 16) == CNA_RESULT_OVERFLOW && bytes[0] == '!');
    CHECK(cna_graphics_device_get_backbuffer_data_raw_ext(0, &readback, 1, bytes, 16) == CNA_RESULT_INVALID_HANDLE && bytes[0] == '!');
    CNA_Rectangle invalid = {0, 0, 0, 1};
    CHECK(cna_graphics_device_present_region_ext(0, &invalid, NULL, 0) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_graphics_device_present_region_ext(0, NULL, NULL, 0) == CNA_RESULT_INVALID_HANDLE);
    CHECK(cna_graphics_device_draw_instanced_primitives_base_instance_ext(0, 0, 0, 0, 1, 0, 1, 1, -1) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_graphics_device_draw_instanced_primitives_base_instance_ext(0, 0, 0, 0, 1, 0, 1, 1, 0) == CNA_RESULT_INVALID_HANDLE);
    return 0;
}
static int texture_roundtrip(CNA_Handle device)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;
    smoke.device = device;

    const CNA_SurfaceFormat formats[] = {
        CNA_SURFACE_FORMAT_COLOR,        CNA_SURFACE_FORMAT_BGR565,       CNA_SURFACE_FORMAT_BGRA5551,
        CNA_SURFACE_FORMAT_BGRA4444,     CNA_SURFACE_FORMAT_COLOR,        CNA_SURFACE_FORMAT_COLOR,
        CNA_SURFACE_FORMAT_COLOR,        CNA_SURFACE_FORMAT_RGBA1010102,  CNA_SURFACE_FORMAT_RG32,
        CNA_SURFACE_FORMAT_RGBA64,       CNA_SURFACE_FORMAT_ALPHA8,       CNA_SURFACE_FORMAT_SINGLE,
        CNA_SURFACE_FORMAT_VECTOR2,      CNA_SURFACE_FORMAT_VECTOR4,      CNA_SURFACE_FORMAT_HALF_SINGLE,
        CNA_SURFACE_FORMAT_HALF_VECTOR2, CNA_SURFACE_FORMAT_HALF_VECTOR4, CNA_SURFACE_FORMAT_COLOR,
        CNA_SURFACE_FORMAT_COLOR};
    const uint32_t widths[] = {4, 2, 2, 2, 1, 2, 4, 4, 4, 8, 1, 4, 8, 16, 2, 4, 8, 2, 4};
    for (CNA_TextureDataType type = 0; type <= CNA_TEXTURE_DATA_RAW_EXT; ++type) {
        CNA_TextureCubeCreateInfo ci = {sizeof(ci), 1, 2, CNA_TRUE, {0}, formats[type], 0};
        CNA_Texture3DCreateInfo vi = {sizeof(vi), 1, 2, 2, 2, CNA_TRUE, {0}, formats[type], 0};
        CNA_Handle cube = 0, volume = 0;
        /* Declared storage may be refused independently of the element
         * representation. */
        CNA_Result cube_storage =
            SMOKE_OWN(cna_texturecube_create_format_ext(device, &ci, &cube), &cube, cna_texturecube_destroy);
        RUNTIME_CHECK(SMOKE_VALUE(cube_storage) == CNA_RESULT_SUCCESS ||
                      (SMOKE_VALUE(cube_storage) == CNA_RESULT_NOT_SUPPORTED && SMOKE_VALUE(cube) == 0));
        CNA_Result volume_storage =
            SMOKE_OWN(cna_texture3d_create_format_ext(device, &vi, &volume), &volume, cna_texture3d_destroy);
        RUNTIME_CHECK(SMOKE_VALUE(volume_storage) == CNA_RESULT_SUCCESS ||
                      (SMOKE_VALUE(volume_storage) == CNA_RESULT_NOT_SUPPORTED && SMOKE_VALUE(volume) == 0));
        if (cube)
            RUNTIME_CHECK(SMOKE_DROP(cna_texturecube_destroy(cube), cube) == CNA_RESULT_SUCCESS);
        if (volume)
            RUNTIME_CHECK(SMOKE_DROP(cna_texture3d_destroy(volume), volume) == CNA_RESULT_SUCCESS);
        ci.format = widths[type] <= 4 ? CNA_SURFACE_FORMAT_COLOR : CNA_SURFACE_FORMAT_VECTOR4;
        vi.format = ci.format;
        cube_storage =
            SMOKE_OWN(cna_texturecube_create_format_ext(device, &ci, &cube), &cube, cna_texturecube_destroy);
        RUNTIME_CHECK(SMOKE_VALUE(cube_storage) == CNA_RESULT_SUCCESS ||
                      (SMOKE_VALUE(cube_storage) == CNA_RESULT_NOT_SUPPORTED && SMOKE_VALUE(cube) == 0));
        RUNTIME_CHECK(SMOKE_OWN(cna_texture3d_create_format_ext(device, &vi, &volume), &volume,
                                cna_texture3d_destroy) == CNA_RESULT_SUCCESS);
        if (!cube)
            (void)printf("SKIP cube storage for element type %u: native renderer refusal\n", type);
        uint8_t source[512], destination[512];
        for (unsigned i = 0; i < sizeof(source); ++i)
            source[i] = (uint8_t)(i * 17U + 3U);
        int32_t native_format_width = 0;
        RUNTIME_CHECK(SMOKE_API(cna_texture_get_format_size(vi.format, &native_format_width)) ==
                      CNA_RESULT_SUCCESS);
        const uint32_t format_width = (uint32_t)native_format_width;
        const uint32_t raw = type == CNA_TEXTURE_DATA_RAW_EXT ? widths[type] : 0;
        for (int level = 0; level < 2; ++level) {
            const uint64_t texels = level ? 1 : 4;
            uint64_t count = texels * format_width / widths[type];
            for (uint32_t face = 0; cube != 0 && face < 6; ++face) {
                CNA_TextureCubeTransfer ct = {sizeof(ct),   1, face, level, CNA_FALSE, {0},
                                              {0, 0, 0, 0}, 0, 1,    count};
                memset(destination, '!', sizeof(destination));
                uint64_t required = 99;
                RUNTIME_CHECK(SMOKE_API(cna_texturecube_set_data_typed_ext(cube, type, raw, &ct, source,
                                                                           count + 2)) == CNA_RESULT_SUCCESS);
                RUNTIME_CHECK(SMOKE_API(cna_texturecube_get_data_typed_ext(cube, type, raw, &ct, destination,
                                                                           count + 2, &required)) ==
                                  CNA_RESULT_SUCCESS &&
                              SMOKE_VALUE(required) == SMOKE_VALUE(count));
                RUNTIME_CHECK(SMOKE_VALUE(memcmp(source + widths[type], destination + widths[type],
                                                 (size_t)count * widths[type])) == 0 &&
                              SMOKE_VALUE(destination[0]) == '!');
                memset(destination, '!', sizeof(destination));
                RUNTIME_CHECK(SMOKE_API(cna_texturecube_get_data_typed_ext(cube, type, raw, &ct, destination,
                                                                           count, &required)) ==
                                  CNA_RESULT_BUFFER_TOO_SMALL &&
                              SMOKE_VALUE(destination[widths[type]]) == '!');
                ct.has_rectangle = CNA_TRUE;
                ct.rectangle = (CNA_Rectangle){0, 0, 1, 1};
                ct.element_count = format_width / widths[type];
                RUNTIME_CHECK(SMOKE_API(cna_texturecube_set_data_typed_ext(cube, type, raw, &ct, source,
                                                                           count + 2)) == CNA_RESULT_SUCCESS);
                RUNTIME_CHECK(SMOKE_API(cna_texturecube_get_data_typed_ext(cube, type, raw, &ct, destination,
                                                                           count + 2, &required)) ==
                              CNA_RESULT_SUCCESS);
            }
            const int dimension = level ? 1 : 2;
            count *= (uint64_t)dimension;
            CNA_Texture3DTransfer vt = {sizeof(vt), 1, level,     0, 0, dimension,
                                        dimension,  0, dimension, 0, 1, count};
            memset(destination, '!', sizeof(destination));
            uint64_t required = 99;
            RUNTIME_CHECK(SMOKE_API(cna_texture3d_set_data_typed_ext(volume, type, raw, &vt, source,
                                                                     count + 2)) == CNA_RESULT_SUCCESS);
            RUNTIME_CHECK(SMOKE_API(cna_texture3d_get_data_typed_ext(volume, type, raw, &vt, destination,
                                                                     count + 2, &required)) ==
                              CNA_RESULT_SUCCESS &&
                          SMOKE_VALUE(required) == SMOKE_VALUE(count));
            RUNTIME_CHECK(SMOKE_VALUE(memcmp(source + widths[type], destination + widths[type],
                                             (size_t)count * widths[type])) == 0 &&
                          SMOKE_VALUE(destination[0]) == '!');
            memset(destination, '!', sizeof(destination));
            RUNTIME_CHECK(SMOKE_API(cna_texture3d_get_data_typed_ext(volume, type, raw, &vt, destination,
                                                                     count, &required)) ==
                              CNA_RESULT_BUFFER_TOO_SMALL &&
                          SMOKE_VALUE(destination[widths[type]]) == '!');
            vt.right = 1;
            vt.bottom = 1;
            vt.back = 1;
            vt.element_count = format_width / widths[type];
            RUNTIME_CHECK(SMOKE_API(cna_texture3d_set_data_typed_ext(volume, type, raw, &vt, source,
                                                                     count + 2)) == CNA_RESULT_SUCCESS);
            RUNTIME_CHECK(SMOKE_API(cna_texture3d_get_data_typed_ext(volume, type, raw, &vt, destination,
                                                                     count + 2, &required)) ==
                          CNA_RESULT_SUCCESS);
        }
        RUNTIME_CHECK(SMOKE_DROP(cna_texture3d_destroy(volume), volume) == CNA_RESULT_SUCCESS);
        if (cube)
            RUNTIME_CHECK(SMOKE_DROP(cna_texturecube_destroy(cube), cube) == CNA_RESULT_SUCCESS);
    }
    status = 0;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0)
        status = 1;
    return status;
}
static int base_instance_draw(CNA_Handle device)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;
    smoke.device = device;

    CNA_VertexElement mesh_elements[] = {
        {0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0},
        {12, CNA_VERTEX_ELEMENT_FORMAT_COLOR, CNA_VERTEX_ELEMENT_USAGE_COLOR, 0}};
    CNA_VertexElement instance_elements[4];
    for (int i = 0; i < 4; ++i)
        instance_elements[i] = (CNA_VertexElement){i * 16, CNA_VERTEX_ELEMENT_FORMAT_VECTOR4,
                                                   CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE, i + 1};
    CNA_VertexDeclarationHandle mesh_decl = 0, instance_decl = 0;
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_declaration_create_with_stride(16, mesh_elements, 2, &mesh_decl),
                            &mesh_decl, cna_vertex_declaration_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(
        SMOKE_OWN(cna_vertex_declaration_create_with_stride(64, instance_elements, 4, &instance_decl),
                  &instance_decl, cna_vertex_declaration_destroy) == CNA_RESULT_SUCCESS);
    CNA_VertexBufferCreateInfo ci = {sizeof(ci), 1, mesh_decl, 3, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    CNA_VertexBufferHandle mesh = 0, instances = 0;
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_buffer_create(device, &ci, &mesh), &mesh, cna_vertex_buffer_destroy) ==
                  CNA_RESULT_SUCCESS);
    ci.vertex_declaration = instance_decl;
    ci.vertex_count = 2;
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_buffer_create(device, &ci, &instances), &instances,
                            cna_vertex_buffer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_vertex_declaration_destroy(mesh_decl), mesh_decl) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_vertex_declaration_destroy(instance_decl), instance_decl) ==
                  CNA_RESULT_SUCCESS);
    const CNA_VertexPositionColor triangle[] = {{{-0.2F, -0.4F, 0.0F}, {255, 0, 0, 255}},
                                                {{0.2F, -0.4F, 0.0F}, {255, 0, 0, 255}},
                                                {{0.0F, 0.4F, 0.0F}, {255, 0, 0, 255}}};
    const float matrices[2][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.5F, 0, 0, 1},
                                   {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.5F, 0, 0, 1}};
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw(mesh, triangle, sizeof(triangle), 3, 16)) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw(instances, matrices, sizeof(matrices), 2, 64)) ==
                  CNA_RESULT_SUCCESS);
    CNA_IndexBufferCreateInfo ii = {
        sizeof(ii), 1, 3, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    CNA_IndexBufferHandle indices = 0;
    const uint16_t data[] = {0, 1, 2};
    CNA_IndexBufferTransfer it = {sizeof(it),        1, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS,
                                  CNA_SET_DATA_NONE, 0, 3};
    RUNTIME_CHECK(SMOKE_OWN(cna_index_buffer_create(device, &ii, &indices), &indices,
                            cna_index_buffer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data(indices, &it, data, 3)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_index_buffer(device, indices)) == CNA_RESULT_SUCCESS);
    const CNA_VertexBufferBinding bindings[] = {{mesh, 0, 0}, {instances, 0, 1}};
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_vertex_buffers(device, bindings, 2)) ==
                  CNA_RESULT_SUCCESS);
    CNA_RasterizerState rasterizer;
    RUNTIME_CHECK(SMOKE_API(cna_rasterizer_state_init(CNA_RASTERIZER_STATE_PRESET_CULL_NONE, &rasterizer)) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_rasterizer_state(device, &rasterizer)) ==
                  CNA_RESULT_SUCCESS);
    CNA_DepthStencilState depth;
    RUNTIME_CHECK(SMOKE_API(cna_depth_stencil_state_init(CNA_DEPTH_STENCIL_STATE_PRESET_NONE, &depth)) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_depth_stencil_state(device, &depth)) ==
                  CNA_RESULT_SUCCESS);
    CNA_EffectHandle effect = 0;
    RUNTIME_CHECK(SMOKE_OWN(cna_basic_effect_create(device, &effect), &effect, cna_effect_destroy) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_basic_effect_set_vertex_color_enabled(effect, CNA_TRUE)) ==
                  CNA_RESULT_SUCCESS);
    const CNA_Matrix identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    RUNTIME_CHECK(SMOKE_API(cna_effect_matrices_set_world(effect, identity)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_effect_matrices_set_view(effect, identity)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_effect_matrices_set_projection(effect, identity)) == CNA_RESULT_SUCCESS);
    // Base-instance drawing is an EXT feature EasyGL refuses; such renderers must say so and refuse.
    CNA_RendererFeatureSupport base_instance = CNA_RENDERER_FEATURE_SUPPORT_UNKNOWN;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_renderer_feature_support_ext(
                      device, CNA_RENDERER_FEATURE_BASE_INSTANCE_DRAWING, &base_instance)) == CNA_RESULT_SUCCESS);
    const int draws_base_instance = base_instance == CNA_RENDERER_FEATURE_SUPPORT_SUPPORTED;
    if (!draws_base_instance) {
        RUNTIME_CHECK(SMOKE_VALUE(base_instance) == CNA_RENDERER_FEATURE_SUPPORT_UNSUPPORTED);
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_draw_instanced_primitives_base_instance_ext(
                          device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1, 1, 1)) == CNA_RESULT_NOT_SUPPORTED);
    }
    for (int first = 0; draws_base_instance && first < 2; ++first) {
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_clear_rgba(device, 0, 0, 0, 1)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_effect_apply(effect)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_graphics_device_draw_instanced_primitives_base_instance_ext(
                          device, CNA_PRIMITIVE_TRIANGLE_LIST, 0, 0, 3, 0, 1, 1, first)) ==
                      CNA_RESULT_SUCCESS);
        for (int side = 0; side < 2; ++side) {
            CNA_BackBufferReadback rb = {sizeof(rb), 1, CNA_TRUE, {0}, {side ? 48 : 16, 32, 1, 1}, 0, 1};
            CNA_Color pixel = {0, 0, 0, 0};
            RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_backbuffer_data_raw_ext(
                              device, &rb, 4, &pixel, 1)) == CNA_RESULT_SUCCESS);
            RUNTIME_CHECK(SMOKE_VALUE(pixel.g) == 0 && SMOKE_VALUE(pixel.b) == 0 &&
                          SMOKE_VALUE(pixel.r) == SMOKE_VALUE((side == first ? 255 : 0)));
        }
    }
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_vertex_buffers(device, NULL, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_index_buffer(device, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_vertex_buffer_destroy(mesh), mesh) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_vertex_buffer_destroy(instances), instances) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_index_buffer_destroy(indices), indices) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_current_effect(device, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_DROP(cna_effect_destroy(effect), effect) == CNA_RESULT_SUCCESS);
    status = 0;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0)
        status = 1;
    return status;
}
static int runtime(void)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;

    CNA_PresentationParameters pp;
    CNA_Handle device = 0;
    RUNTIME_CHECK(SMOKE_API(cna_presentation_parameters_init(&pp)) == CNA_RESULT_SUCCESS);
    pp.is_full_screen = CNA_FALSE;
    pp.back_buffer_width = 64;
    pp.back_buffer_height = 64;
    RUNTIME_CHECK(SMOKE_OWN(cna_graphics_device_create(0, CNA_GRAPHICS_PROFILE_HI_DEF, &pp, &device), &device,
                            cna_graphics_device_destroy) == CNA_RESULT_SUCCESS);
    /* Refresh Helpers.ValidateOrientation accepts a single applied value, not flags 1|2. */
    pp.display_orientation = CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT | CNA_DISPLAY_ORIENTATION_LANDSCAPE_RIGHT;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_reset_with_parameters(device, &pp, NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    CNA_PresentationParameters after_refusal = pp;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_presentation_parameters(device, &after_refusal)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(after_refusal.display_orientation) == CNA_DISPLAY_ORIENTATION_DEFAULT);
    pp.display_orientation = CNA_DISPLAY_ORIENTATION_DEFAULT;
    RUNTIME_CHECK(SMOKE_VALUE(texture_roundtrip(device)) == 0);
    RUNTIME_CHECK(SMOKE_VALUE(base_instance_draw(device)) == 0);
    CNA_IndexBufferCreateInfo ci = {
        sizeof(ci), 1, 8, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    CNA_IndexBufferHandle index = 0;
    RUNTIME_CHECK(SMOKE_OWN(cna_index_buffer_create(device, &ci, &index), &index, cna_index_buffer_destroy) ==
                  CNA_RESULT_SUCCESS);
    uint16_t source[] = {0x1100, 0x3322, 0x5544, 0x7766, 0x9988, 0xbbaa, 0xddcc, 0xffee};
    CNA_IndexBufferTransfer it = {sizeof(it),        1, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS,
                                  CNA_SET_DATA_NONE, 0, 8};
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data(index, &it, source, 8)) == CNA_RESULT_SUCCESS);
    uint8_t result[32];
    memset(result, '!', sizeof(result));
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_get_data_raw_ext(index, 3, 3, 1, 2, result, 10)) ==
                  CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(result[0]) == '!' && SMOKE_VALUE(result[2]) == '!' &&
                  SMOKE_VALUE(memcmp(result + 3, (const uint8_t*)source + 3, 6)) == 0 &&
                  SMOKE_VALUE(result[9]) == '!');
    RUNTIME_CHECK(SMOKE_DROP(cna_index_buffer_destroy(index), index) == CNA_RESULT_SUCCESS);
    CNA_BackBufferReadback rb = {sizeof(rb), 1, CNA_TRUE, {0}, {0, 0, 1, 1}, 1, 4};
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_clear_rgba(device, 31.0F / 255.0F, 63.0F / 255.0F,
                                                           127.0F / 255.0F, 1)) == CNA_RESULT_SUCCESS);
    memset(result, '!', sizeof(result));
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_backbuffer_data_raw_ext(
                      device, &rb, 1, result, sizeof(result))) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(result[0]) == '!' && SMOKE_VALUE(result[1]) == 31 &&
                  SMOKE_VALUE(result[2]) == 63 && SMOKE_VALUE(result[3]) == 127 &&
                  SMOKE_VALUE(result[4]) == 255 && SMOKE_VALUE(result[5]) == '!');
    /* Refresh Texture.ValidateTotalSize IL_005e-0076 requires equality, not minimum capacity.
       Existing 1x1 Color data are 4 bytes; 3 is short, 5 is excess. All output bytes survive. */
    memset(result, '!', sizeof(result));
    rb.element_count = 3;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_backbuffer_data_raw_ext(
                      device, &rb, 1, result, sizeof(result))) == CNA_RESULT_BUFFER_TOO_SMALL);
    rb.element_count = 5;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_backbuffer_data_raw_ext(
                      device, &rb, 1, result, sizeof(result))) == CNA_RESULT_INVALID_ARGUMENT);
    rb.element_count = 1;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_backbuffer_data_raw_ext(
                      device, &rb, 3, result, sizeof(result))) == CNA_RESULT_INVALID_ARGUMENT);
    for (size_t i = 0; i != sizeof(result); ++i) RUNTIME_CHECK(SMOKE_VALUE(result[i]) == '!');
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_present_region_ext(device, NULL, NULL, 0)) ==
                  CNA_RESULT_SUCCESS);
    CNA_Rectangle rect = {0, 0, 32, 32};
    CNA_Result present = cna_graphics_device_present_region_ext(device, &rect, &rect, 0);
    RUNTIME_CHECK(SMOKE_VALUE(present) == CNA_RESULT_SUCCESS ||
                  SMOKE_VALUE(present) == CNA_RESULT_NOT_SUPPORTED);
    if (present == CNA_RESULT_NOT_SUPPORTED)
        puts("SKIP region presentation pixels: native renderer refusal");
    RUNTIME_CHECK(SMOKE_DROP(cna_graphics_device_destroy(device), device) == CNA_RESULT_SUCCESS);
    status = 0;
cleanup:
    if (cna_smoke_cleanup(&smoke) != 0)
        status = 1;
    return status;
}
int main(int argc, char** argv)
{
    CHECK(failures() == 0);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0) CHECK(runtime() == 0);
    puts("GraphicsTransferSmoke: PASS"); return 0;
}
