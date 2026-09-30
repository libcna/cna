// SPDX-License-Identifier: MS-PL

/*
 * BINDFIX-040: the effect a content-loaded Model publishes is an ordinary Effect handle, so every
 * generic Effect route has to work on it. It was published without the C-side state those routes
 * read, and cna_effect_get_parameters, _get_techniques and _get_current_technique dereferenced
 * null -- a segfault on every renderer (reported by cna-ruby). The model still owns the effect:
 * disposing it stays refused. The model handle is deliberately left to its content manager, the
 * way a game that never unloads its models does, and the game must still be destroyable after it.
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

#ifndef CNA_C_API_TEST_ASSET_DIR
#error "CNA_C_API_TEST_ASSET_DIR must be defined by the build"
#endif

static const char ContentRoot[] = CNA_C_API_TEST_ASSET_DIR "/xnb/monogame/windows/uncompressed";

typedef struct CallbackState {
    int validated;
} CallbackState;

static CNA_StringView view(const char* const text)
{
    const CNA_StringView value = {text, (uint64_t)strlen(text)};
    return value;
}

static int validate_generic_routes(const CNA_EffectHandle effect)
{
    CNA_EffectParameterCollectionHandle parameters = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueCollectionHandle techniques = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle current = CNA_INVALID_HANDLE;
    uint64_t parameter_count = 0U;
    uint64_t technique_count = 0U;
    const int ok =
        CNA_TEST_STAGE(cna_effect_get_parameters(effect, &parameters) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_effect_parameter_collection_get_count(parameters, &parameter_count) ==
            CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(parameter_count > 0U) &&
        CNA_TEST_STAGE(cna_effect_get_techniques(effect, &techniques) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_effect_technique_collection_get_count(techniques, &technique_count) ==
            CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(technique_count > 0U) &&
        CNA_TEST_STAGE(cna_effect_get_current_technique(effect, &current) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(current != CNA_INVALID_HANDLE) &&
        /* The model owns it: a caller may use the effect but not dispose it. */
        CNA_TEST_STAGE(cna_effect_destroy(effect) == CNA_RESULT_INVALID_STATE);
    if (current != CNA_INVALID_HANDLE) {
        (void)cna_effect_technique_destroy(current);
    }
    if (techniques != CNA_INVALID_HANDLE) {
        (void)cna_effect_technique_collection_destroy(techniques);
    }
    if (parameters != CNA_INVALID_HANDLE) {
        (void)cna_effect_parameter_collection_destroy(parameters);
    }
    return ok;
}

static int validate_loaded_model(const CNA_Handle graphics_device)
{
    const CNA_ContentManagerCreateInfo create_info = {
        sizeof(CNA_ContentManagerCreateInfo), UINT32_C(1), view(ContentRoot), 0U
    };
    CNA_Handle manager = CNA_INVALID_HANDLE;
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_ModelMeshCollectionHandle meshes = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle mesh = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartCollectionHandle parts = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    CNA_Bool has_effect = CNA_FALSE;
    const int ok =
        CNA_TEST_STAGE(cna_content_manager_create(graphics_device, &create_info, &manager) ==
            CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_content_manager_load_model(
            manager, view("BlenderDefaultCube"), &model) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_model_get_meshes(model, &meshes) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_model_mesh_collection_get_at(meshes, 0U, &mesh) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_model_mesh_get_mesh_parts(mesh, &parts) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_model_mesh_part_collection_get_at(parts, 0U, &part) ==
            CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_model_mesh_part_get_effect(part, &has_effect, &effect) ==
            CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(has_effect == CNA_TRUE) &&
        validate_generic_routes(effect);
    const int torn_down =
        (part == CNA_INVALID_HANDLE ||
            CNA_TEST_STAGE(cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS)) &&
        (parts == CNA_INVALID_HANDLE ||
            CNA_TEST_STAGE(cna_model_mesh_part_collection_destroy(parts) == CNA_RESULT_SUCCESS)) &&
        (mesh == CNA_INVALID_HANDLE ||
            CNA_TEST_STAGE(cna_model_mesh_destroy(mesh) == CNA_RESULT_SUCCESS)) &&
        (meshes == CNA_INVALID_HANDLE ||
            CNA_TEST_STAGE(cna_model_mesh_collection_destroy(meshes) == CNA_RESULT_SUCCESS)) &&
        (manager == CNA_INVALID_HANDLE ||
            CNA_TEST_STAGE(cna_content_manager_destroy(manager) == CNA_RESULT_SUCCESS));
    return ok && torn_down;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CallbackState* const state = (CallbackState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Bool three_d = CNA_FALSE;
    (void)game_time;
    (void)out_error;
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_supports_capability(
            device, CNA_GRAPHICS_CAPABILITY_THREE_D, &three_d) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* A 2D-only renderer has no models to load; that is its answer, not this route's. */
    state->validated = three_d == CNA_TRUE ? validate_loaded_model(device) : 2;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    static const char Title[] = "C API content model effect";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (!CNA_TEST_STAGE(cna_game_create(&create_info, &game) == CNA_RESULT_SUCCESS) ||
        !CNA_TEST_STAGE(cna_game_run_one_frame(game) == CNA_RESULT_SUCCESS) ||
        !CNA_TEST_STAGE(state.validated != 0) ||
        !CNA_TEST_STAGE(cna_game_destroy(game) == CNA_RESULT_SUCCESS)) {
        return CNA_TEST_FAIL(1);
    }
    return state.validated == 2 ? 77 : 0;
}
