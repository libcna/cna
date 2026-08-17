// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <threads.h>

_Static_assert(sizeof(CNA_ModelMeshPartHandle) == 8U,
               "CNA model-mesh-part handle size changed");
_Static_assert(sizeof(CNA_ModelMeshPartCollectionHandle) == 8U,
               "CNA model-mesh-part collection handle size changed");
_Static_assert(sizeof(CNA_ModelMeshPartTag) == 8U,
               "CNA model-mesh-part tag size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "ModelMeshPartSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_Handle device;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_ModelMeshPartHandle part;
    CNA_Result result;
} WrongThreadState;

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    int32_t value = 0;
    state->result = cna_model_mesh_part_get_num_vertices(state->part, &value);
    return 0;
}

static int validate_scalar_state(void)
{
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle initialized = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle alias = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle invalid = UINT64_MAX;
    CNA_ModelMeshPartCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartCollectionHandle empty = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle parts[2];
    CNA_Bool has = CNA_TRUE;
    CNA_Handle resource = UINT64_MAX;
    CNA_ModelMeshPartTag tag = UINT64_MAX;
    uint64_t count = UINT64_MAX;
    int32_t value = INT32_MIN;

    REQUIRE(cna_model_mesh_part_create_default(&part) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_num_vertices(part, &value) == CNA_RESULT_SUCCESS &&
            value == 0 &&
            cna_model_mesh_part_get_primitive_count(part, &value) == CNA_RESULT_SUCCESS &&
            value == 0 &&
            cna_model_mesh_part_get_start_index(part, &value) == CNA_RESULT_SUCCESS &&
            value == 0 &&
            cna_model_mesh_part_get_vertex_offset(part, &value) == CNA_RESULT_SUCCESS &&
            value == 0 &&
            cna_model_mesh_part_get_tag(part, &tag) == CNA_RESULT_SUCCESS && tag == 0U &&
            cna_model_mesh_part_get_effect(part, &has, &resource) == CNA_RESULT_SUCCESS &&
            has == CNA_FALSE && resource == CNA_INVALID_HANDLE &&
            cna_model_mesh_part_get_vertex_buffer(part, &has, &resource) ==
                CNA_RESULT_SUCCESS && has == CNA_FALSE && resource == CNA_INVALID_HANDLE &&
            cna_model_mesh_part_get_index_buffer(part, &has, &resource) ==
                CNA_RESULT_SUCCESS && has == CNA_FALSE && resource == CNA_INVALID_HANDLE);

    REQUIRE(cna_model_mesh_part_set_num_vertices(part, INT32_MIN) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_primitive_count(part, -7) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_start_index(part, INT32_MAX) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_vertex_offset(part, -9) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_tag(part, UINT64_MAX) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_num_vertices(part, &value) == CNA_RESULT_SUCCESS &&
            value == INT32_MIN &&
            cna_model_mesh_part_get_primitive_count(part, &value) == CNA_RESULT_SUCCESS &&
            value == -7 &&
            cna_model_mesh_part_get_start_index(part, &value) == CNA_RESULT_SUCCESS &&
            value == INT32_MAX &&
            cna_model_mesh_part_get_vertex_offset(part, &value) == CNA_RESULT_SUCCESS &&
            value == -9 &&
            cna_model_mesh_part_get_tag(part, &tag) == CNA_RESULT_SUCCESS &&
            tag == UINT64_MAX);

    REQUIRE(cna_model_mesh_part_create(
                CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, -1, -2, -3, -4,
                &initialized) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_num_vertices(initialized, &value) ==
                CNA_RESULT_SUCCESS && value == -1 &&
            cna_model_mesh_part_get_primitive_count(initialized, &value) ==
                CNA_RESULT_SUCCESS && value == -2 &&
            cna_model_mesh_part_get_start_index(initialized, &value) ==
                CNA_RESULT_SUCCESS && value == -3 &&
            cna_model_mesh_part_get_vertex_offset(initialized, &value) ==
                CNA_RESULT_SUCCESS && value == -4);

    parts[0] = part;
    parts[1] = initialized;
    REQUIRE(cna_model_mesh_part_collection_create(parts, 2U, &collection) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_get_count(collection, &count) ==
                CNA_RESULT_SUCCESS && count == 2U &&
            cna_model_mesh_part_collection_get_at(collection, 0U, &alias) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_num_vertices(alias, 42) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_num_vertices(alias, &value) == CNA_RESULT_SUCCESS &&
            value == 42 &&
            cna_model_mesh_part_collection_get_at(collection, 2U, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);
    REQUIRE(cna_model_mesh_part_collection_create(0, 0U, &empty) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_get_count(empty, &count) ==
                CNA_RESULT_SUCCESS && count == 0U &&
            cna_model_mesh_part_collection_create(0, 1U, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE &&
            cna_model_mesh_part_collection_create(parts, UINT64_MAX, &invalid) ==
                CNA_RESULT_OVERFLOW && invalid == CNA_INVALID_HANDLE &&
            cna_model_mesh_part_get_num_vertices(collection, &value) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_mesh_part_set_effect(alias, collection) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_mesh_part_set_vertex_buffer(alias, collection) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_mesh_part_set_index_buffer(alias, collection) ==
                CNA_RESULT_INVALID_HANDLE);

    WrongThreadState wrong_thread = {alias, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD &&
            cna_model_mesh_part_destroy(alias) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_destroy(empty) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_destroy(collection) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(initialized) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_num_vertices(alias, &value) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static CNA_Result create_vertex_buffer(
    const CNA_Handle device,
    CNA_VertexBufferHandle* const outBuffer)
{
    const CNA_VertexBufferCreateInfo info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), CNA_INVALID_HANDLE,
        0, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    return cna_vertex_buffer_create(device, &info, outBuffer);
}

static CNA_Result create_index_buffer(
    const CNA_Handle device,
    CNA_IndexBufferHandle* const outBuffer)
{
    const CNA_IndexBufferCreateInfo info = {
        sizeof(CNA_IndexBufferCreateInfo), UINT32_C(1), 3,
        CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE,
        CNA_FALSE, {0U, 0U, 0U}};
    return cna_index_buffer_create(device, &info, outBuffer);
}


/* CBIND-051C: the topology this part's indices already describe, and the seven sampler slots the
   PBR texture identities span -- five material maps plus the two KHR_materials_specular maps,
   which the canonical API keeps as two separate arrays. */
static int validate_topology_and_samplers(void)
{
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_SamplerState state = {sizeof(CNA_SamplerState), UINT32_C(1),
                              0, 0, 0, 0, 0, 0, 0.0F, 0U};
    CNA_SamplerState readback = {sizeof(CNA_SamplerState), UINT32_C(1),
                                 0, 0, 0, 0, 0, 0, 0.0F, 0U};
    CNA_SamplerState malformed = {0, 0, 0, 0, 0, 0, 0, 0, 0.0F, 0U};
    CNA_PrimitiveType topology = UINT32_MAX;

    REQUIRE(cna_model_mesh_part_create_default(&part) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_primitive_type_ext(part, &topology) == CNA_RESULT_SUCCESS &&
            topology == CNA_PRIMITIVE_TRIANGLE_LIST);
    for (uint32_t value = 0U; value <= CNA_PRIMITIVE_POINT_LIST_EXT; ++value) {
        REQUIRE(cna_model_mesh_part_set_primitive_type_ext(part, value) == CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_get_primitive_type_ext(part, &topology) ==
                    CNA_RESULT_SUCCESS && topology == value);
    }
    REQUIRE(cna_model_mesh_part_set_primitive_type_ext(
                part, CNA_PRIMITIVE_POINT_LIST_EXT + 1U) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_mesh_part_get_primitive_type_ext(part, 0) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_sampler_state_init(CNA_SAMPLER_STATE_PRESET_POINT_CLAMP, &state) == CNA_RESULT_SUCCESS);
    for (uint32_t slot = 0U; slot <= CNA_PBR_TEXTURE_MAXIMUM; ++slot) {
        REQUIRE(cna_model_mesh_part_get_sampler_state_ext(part, slot, &readback) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_set_sampler_state_ext(part, slot, &state) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_get_sampler_state_ext(part, slot, &readback) ==
                    CNA_RESULT_SUCCESS &&
                readback.filter == state.filter &&
                readback.address_u == state.address_u &&
                readback.address_v == state.address_v);
    }
    REQUIRE(cna_model_mesh_part_get_sampler_state_ext(
                part, CNA_PBR_TEXTURE_MAXIMUM + 1U, &readback) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_mesh_part_set_sampler_state_ext(
                part, CNA_PBR_TEXTURE_MAXIMUM + 1U, &state) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_mesh_part_get_sampler_state_ext(
                part, CNA_PBR_TEXTURE_BASE_COLOR, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_mesh_part_set_sampler_state_ext(
                part, CNA_PBR_TEXTURE_BASE_COLOR, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_mesh_part_set_sampler_state_ext(
                part, CNA_PBR_TEXTURE_BASE_COLOR, &malformed) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_primitive_type_ext(part, &topology) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_mesh_part_set_sampler_state_ext(
                part, CNA_PBR_TEXTURE_BASE_COLOR, &state) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static int validate_resources(const CNA_Handle device)
{
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle constructed = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle alias = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    CNA_VertexBufferHandle vertex = CNA_INVALID_HANDLE;
    CNA_IndexBufferHandle index = CNA_INVALID_HANDLE;
    CNA_Handle returned = UINT64_MAX;
    CNA_Bool has = CNA_FALSE;
    const CNA_Result vertex_result = create_vertex_buffer(device, &vertex);
    const CNA_Result index_result = create_index_buffer(device, &index);

    REQUIRE(cna_model_mesh_part_create_default(&part) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_create(device, &effect) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_effect(part, effect) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_effect(part, effect) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_effect(part, &has, &returned) == CNA_RESULT_SUCCESS &&
            has == CNA_TRUE && returned == effect &&
            cna_effect_destroy(effect) == CNA_RESULT_INVALID_STATE &&
            cna_effect_dispose(effect) == CNA_RESULT_INVALID_STATE &&
            cna_graphics_resource_dispose(effect) == CNA_RESULT_INVALID_STATE &&
            cna_model_mesh_part_collection_create(&part, 1U, &collection) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_get_at(collection, 0U, &alias) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_destroy(collection) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(effect) == CNA_RESULT_INVALID_STATE);
    part = alias;
    REQUIRE(
            cna_model_mesh_part_set_effect(part, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(effect) == CNA_RESULT_SUCCESS);

    REQUIRE((vertex_result == CNA_RESULT_SUCCESS && index_result == CNA_RESULT_SUCCESS) ||
            (vertex_result == CNA_RESULT_NOT_SUPPORTED &&
             index_result == CNA_RESULT_NOT_SUPPORTED));
    if (vertex_result == CNA_RESULT_SUCCESS) {
        REQUIRE(cna_model_mesh_part_set_vertex_buffer(part, vertex) == CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_set_index_buffer(part, index) == CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_get_vertex_buffer(part, &has, &returned) ==
                    CNA_RESULT_SUCCESS && has == CNA_TRUE && returned == vertex &&
                cna_model_mesh_part_get_index_buffer(part, &has, &returned) ==
                    CNA_RESULT_SUCCESS && has == CNA_TRUE && returned == index &&
                cna_vertex_buffer_destroy(vertex) == CNA_RESULT_INVALID_STATE &&
                cna_index_buffer_destroy(index) == CNA_RESULT_INVALID_STATE &&
                cna_graphics_resource_dispose(vertex) == CNA_RESULT_INVALID_STATE &&
                cna_graphics_resource_dispose(index) == CNA_RESULT_INVALID_STATE &&
                cna_model_mesh_part_create(vertex, index, 3, 1, 0, 0, &constructed) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_destroy(constructed) == CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_set_vertex_buffer(part, CNA_INVALID_HANDLE) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_set_index_buffer(part, CNA_INVALID_HANDLE) ==
                    CNA_RESULT_SUCCESS &&
                cna_vertex_buffer_destroy(vertex) == CNA_RESULT_SUCCESS &&
                cna_index_buffer_destroy(index) == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS);
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const gameTime,
    void* const context,
    CNA_CallbackError* const outError)
{
    CallbackState* const state = (CallbackState*)context;
    (void)outError;
    if (gameTime != 0 ||
        cna_game_get_graphics_device(game, &state->device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (!validate_resources(state->device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API ModelMeshPart";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!validate_scalar_state() || !validate_topology_and_samplers() ||
        cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 2 ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "ModelMeshPartSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
