/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return CNA_TEST_FAIL(1); } while (0)
CNA_Bool cna_test_volume_profile(uint32_t, uint32_t);
int32_t cna_test_fragment_sampler_count(void);
int cna_test_resource_worker(int (*function)(void));
static int sampler(void)
{
    for (uint32_t profile = 0; profile < 2; ++profile) {
        for (uint32_t format = 0; format <= CNA_SURFACE_FORMAT_USHORT_EXT; ++format) {
            CNA_Bool result = 99;
            CHECK(cna_texture_is_volume_format_allowed_by_profile_ext(profile, format, &result) == CNA_RESULT_SUCCESS);
            CHECK(result == cna_test_volume_profile(profile, format) && result <= CNA_TRUE);
        }
        int32_t count = -1;
        CHECK(cna_sampler_state_collection_get_active_count_ext(CNA_SHADER_STAGE_VERTEX_EXT, profile, &count) == CNA_RESULT_SUCCESS && count == (profile ? 4 : 0));
        CHECK(cna_sampler_state_collection_get_active_count_ext(CNA_SHADER_STAGE_FRAGMENT_EXT, profile, &count) == CNA_RESULT_SUCCESS && count == cna_test_fragment_sampler_count());
    }
    const uint32_t invalid[] = {2, INT32_MAX, UINT32_C(0x80000000), UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        CNA_Bool value = 99;
        int32_t count = -1;
        CHECK(cna_texture_is_volume_format_allowed_by_profile_ext(invalid[i], 0, &value) == CNA_RESULT_INVALID_ARGUMENT && value == 99);
        CHECK(cna_texture_is_volume_format_allowed_by_profile_ext(0, UINT32_MAX, &value) == CNA_RESULT_INVALID_ARGUMENT && value == 99);
        CHECK(cna_sampler_state_collection_get_active_count_ext(1, invalid[i], &count) == CNA_RESULT_INVALID_ARGUMENT && count == -1);
        CHECK(cna_sampler_state_collection_get_active_count_ext(UINT32_MAX, 0, &count) == CNA_RESULT_INVALID_ARGUMENT && count == -1);
    }
    int32_t count = -1;
    CHECK(cna_sampler_state_collection_get_active_count_ext(0, 0, &count) == CNA_RESULT_INVALID_ARGUMENT && count == -1);
    CHECK(cna_sampler_state_collection_get_active_count_ext(CNA_SHADER_STAGE_COUNT_EXT, 0, &count) == CNA_RESULT_INVALID_ARGUMENT && count == -1);
    CHECK(cna_sampler_state_collection_get_active_count_ext(1, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_texture_is_volume_format_allowed_by_profile_ext(0, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    return 0;
}
static int buffer_names(void)
{
    CNA_PresentationParameters pp;
    CNA_Handle device = 0;
    CHECK(cna_presentation_parameters_init(&pp) == CNA_RESULT_SUCCESS);
    pp.is_full_screen = CNA_FALSE; pp.back_buffer_width = 64; pp.back_buffer_height = 64;
    CHECK(cna_graphics_device_create(0, CNA_GRAPHICS_PROFILE_HI_DEF, &pp, &device) == CNA_RESULT_SUCCESS);
    for (CNA_Bool dynamic = 0; dynamic <= CNA_TRUE; ++dynamic) {
        CNA_IndexBufferCreateInfo ii = {sizeof(ii), 1, 4, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, dynamic, {0}};
        CNA_IndexBufferHandle index = 0;
        CHECK(cna_index_buffer_create(device, &ii, &index) == CNA_RESULT_SUCCESS);
        const char* expected = dynamic ? "Microsoft.Xna.Framework.Graphics.DynamicIndexBuffer" : CNA_INDEX_BUFFER_TYPE_NAME;
        char name[100]; memset(name, '!', sizeof(name)); uint64_t size = 0;
        CHECK(cna_index_buffer_get_runtime_type_name_byte_count_ext(index, &size) == CNA_RESULT_SUCCESS && size == strlen(expected));
        CHECK(cna_index_buffer_copy_runtime_type_name_ext(index, name, size - 1, &size) == CNA_RESULT_BUFFER_TOO_SMALL && name[0] == '!');
        CHECK(cna_index_buffer_copy_runtime_type_name_ext(index, name, sizeof(name), &size) == CNA_RESULT_SUCCESS && memcmp(name, expected, (size_t)size) == 0);
        CHECK(cna_index_buffer_copy_type_name(index, name, sizeof(name), &size) == CNA_RESULT_SUCCESS && size == strlen(CNA_INDEX_BUFFER_TYPE_NAME));
        CNA_VertexElement element = {0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0};
        CNA_VertexDeclarationHandle declaration = 0; CNA_VertexBufferHandle vertex = 0;
        CHECK(cna_vertex_declaration_create_with_stride(12, &element, 1, &declaration) == CNA_RESULT_SUCCESS);
        CNA_VertexBufferCreateInfo vi = {sizeof(vi), 1, declaration, 4, CNA_BUFFER_USAGE_NONE, dynamic, {0}};
        CHECK(cna_vertex_buffer_create(device, &vi, &vertex) == CNA_RESULT_SUCCESS);
        CHECK(cna_vertex_declaration_destroy(declaration) == CNA_RESULT_SUCCESS);
        expected = dynamic ? "Microsoft.Xna.Framework.Graphics.DynamicVertexBuffer" : CNA_VERTEX_BUFFER_TYPE_NAME;
        CHECK(cna_vertex_buffer_get_runtime_type_name_byte_count_ext(vertex, &size) == CNA_RESULT_SUCCESS && size == strlen(expected));
        memset(name, '!', sizeof(name));
        CHECK(cna_vertex_buffer_copy_runtime_type_name_ext(vertex, name, size - 1, &size) == CNA_RESULT_BUFFER_TOO_SMALL && name[0] == '!');
        CHECK(cna_vertex_buffer_copy_runtime_type_name_ext(vertex, name, sizeof(name), &size) == CNA_RESULT_SUCCESS && memcmp(name, expected, (size_t)size) == 0);
        size = 99;
        CHECK(cna_index_buffer_get_runtime_type_name_byte_count_ext(vertex, &size) == CNA_RESULT_INVALID_HANDLE && size == 99);
        CHECK(cna_vertex_buffer_get_runtime_type_name_byte_count_ext(index, &size) == CNA_RESULT_INVALID_HANDLE && size == 99);
        CHECK(cna_graphics_resource_dispose(index) == CNA_RESULT_SUCCESS);
        CHECK(cna_graphics_resource_dispose(vertex) == CNA_RESULT_SUCCESS);
        CHECK(cna_index_buffer_get_runtime_type_name_byte_count_ext(index, &size) == CNA_RESULT_INVALID_STATE && size == 99);
        CHECK(cna_vertex_buffer_get_runtime_type_name_byte_count_ext(vertex, &size) == CNA_RESULT_INVALID_STATE && size == 99);
        CHECK(cna_index_buffer_destroy(index) == CNA_RESULT_SUCCESS);
        CHECK(cna_vertex_buffer_destroy(vertex) == CNA_RESULT_SUCCESS);
        CHECK(cna_index_buffer_copy_runtime_type_name_ext(index, name, sizeof(name), &size) == CNA_RESULT_INVALID_HANDLE && size == 99);
        CHECK(cna_vertex_buffer_copy_runtime_type_name_ext(vertex, name, sizeof(name), &size) == CNA_RESULT_INVALID_HANDLE && size == 99);
    }
    CHECK(cna_graphics_device_destroy(device) == CNA_RESULT_SUCCESS);
    return 0;
}
int main(int argc, char** argv)
{
    CHECK(sampler() == 0); CHECK(cna_test_resource_worker(sampler) == 0);
    uint64_t size = 99; char data = '!';
    CHECK(cna_index_buffer_get_runtime_type_name_byte_count_ext(0, &size) == CNA_RESULT_INVALID_HANDLE && size == 99);
    CHECK(cna_vertex_buffer_get_runtime_type_name_byte_count_ext(0, &size) == CNA_RESULT_INVALID_HANDLE && size == 99);
    CHECK(cna_index_buffer_copy_runtime_type_name_ext(0, &data, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT && data == '!');
    CHECK(cna_vertex_buffer_copy_runtime_type_name_ext(0, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT && size == 99);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0) CHECK(buffer_names() == 0);
    puts("GraphicsFamilySmoke: PASS"); return 0;
}
