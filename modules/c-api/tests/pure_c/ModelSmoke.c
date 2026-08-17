// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_ModelHandle) == 8U, "CNA model handle size changed");
_Static_assert(sizeof(CNA_ModelTag) == 8U, "CNA model tag size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "ModelSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_Handle device;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_ModelHandle model;
    CNA_Result result;
} WrongThreadState;

static int release_count = 0;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static void release_owner(void* const context)
{
    int* const value = (int*)context;
    ++*value;
    ++release_count;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint64_t count = 0U;
    state->result = cna_model_get_bone_transform_count(state->model, &count);
    return 0;
}

static int validate_default(void)
{
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_ModelBoneCollectionHandle bones = CNA_INVALID_HANDLE;
    CNA_ModelMeshCollectionHandle meshes = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle root = UINT64_MAX;
    CNA_ModelTag tag = UINT64_MAX;
    CNA_Bool has = CNA_TRUE;
    CNA_Matrix identity = {0};
    uint64_t count = UINT64_MAX;
    int owner0 = 0;
    int owner1 = 0;

    REQUIRE(cna_model_create_default(&model) == CNA_RESULT_SUCCESS &&
            cna_model_get_bones(model, &bones) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_get_count(bones, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_model_get_meshes(model, &meshes) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_collection_get_count(meshes, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_model_get_root(model, &has, &root) == CNA_RESULT_SUCCESS &&
            has == CNA_FALSE && root == CNA_INVALID_HANDLE &&
            cna_model_get_tag(model, &tag) == CNA_RESULT_SUCCESS && tag == 0U &&
            cna_model_set_tag(model, UINT64_MAX) == CNA_RESULT_SUCCESS &&
            cna_model_get_tag(model, &tag) == CNA_RESULT_SUCCESS && tag == UINT64_MAX &&
            cna_model_get_bone_transform_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_model_copy_bone_transforms(model, 0, 0U, &count) ==
                CNA_RESULT_SUCCESS && count == 0U &&
            cna_model_copy_absolute_bone_transforms(model, 0, 0U, &count) ==
                CNA_RESULT_SUCCESS && count == 0U &&
            cna_model_set_bone_transforms(model, 0, 0U) == CNA_RESULT_SUCCESS &&
            cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS &&
            cna_model_draw(model, identity, identity, identity) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_model_set_owned_resources(model, &owner0, release_owner) ==
                CNA_RESULT_SUCCESS && owner0 == 0 && release_count == 0 &&
            cna_model_set_owned_resources(model, &owner1, release_owner) ==
                CNA_RESULT_SUCCESS && owner0 == 1 && release_count == 1 &&
            cna_model_set_owned_resources(model, 0, 0) == CNA_RESULT_SUCCESS &&
            owner1 == 1 && release_count == 2 &&
            cna_model_set_owned_resources(model, &owner0, 0) ==
                CNA_RESULT_INVALID_ARGUMENT);

    WrongThreadState wrong_thread = {model, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD &&
            cna_model_bone_collection_destroy(bones) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_collection_destroy(meshes) == CNA_RESULT_SUCCESS &&
            cna_model_destroy(model) == CNA_RESULT_SUCCESS &&
            cna_model_get_tag(model, &tag) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = strlen(text);
    return result;
}

static int string_equals(const char* const buffer, const uint64_t length, const char* const text)
{
    return length == (uint64_t)strlen(text) && memcmp(buffer, text, (size_t)length) == 0;
}

/* The glTF import report a model carries: an all-zero default, a round-trip through the counts,
   and the diagnostics whose derived answers the report computes rather than stores. */
static int validate_gltf_import_report(void)
{
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_GltfImportReportEXT report = {sizeof(CNA_GltfImportReportEXT), UINT32_C(1),
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, CNA_FALSE};
    CNA_GltfImportReportEXT written = {sizeof(CNA_GltfImportReportEXT), UINT32_C(1),
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, CNA_FALSE};
    CNA_GltfImportDiagnosticEXT diagnostic = {
        sizeof(CNA_GltfImportDiagnosticEXT), UINT32_C(1), 0, 0, 0, 0.0, 0};
    CNA_GltfImportDiagnosticEXT malformed = {0, 0, 0, 0, 0, 0.0, 0};
    CNA_StringView details[2];
    CNA_GltfImportDiagnosticDescriptorEXT descriptor;
    char buffer[64];
    uint64_t length = UINT64_MAX;

    REQUIRE(cna_model_create_default(&model) == CNA_RESULT_SUCCESS);

    /* A model from any other content path reads back the all-zero empty default. */
    REQUIRE(cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.node_count == 0U && report.mesh_instance_count == 0U &&
            report.distinct_mesh_count == 0U && report.shared_mesh_count == 0U &&
            report.max_node_depth == 0U && report.camera_node_count == 0U &&
            report.light_node_count == 0U && report.imported_light_count == 0U &&
            report.primitive_count == 0U && report.skin_count == 0U &&
            report.animation_count == 0U && report.clip_count == 0U &&
            report.diagnostic_count == 0U && report.warning_count == 0U &&
            report.dropped_feature_count == 0U && report.approximation_count == 0U &&
            report.anything_lost == CNA_FALSE);

    written.node_count = 11U;
    written.mesh_instance_count = 12U;
    written.distinct_mesh_count = 13U;
    written.shared_mesh_count = 14U;
    written.max_node_depth = 15U;
    written.camera_node_count = 16U;
    written.light_node_count = 17U;
    written.imported_light_count = 18U;
    written.primitive_count = 19U;
    written.skin_count = 20U;
    written.animation_count = 21U;
    written.clip_count = 22U;
    REQUIRE(cna_model_set_gltf_import_report_ext(model, &written) == CNA_RESULT_SUCCESS &&
            cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.node_count == 11U && report.mesh_instance_count == 12U &&
            report.distinct_mesh_count == 13U && report.shared_mesh_count == 14U &&
            report.max_node_depth == 15U && report.camera_node_count == 16U &&
            report.light_node_count == 17U && report.imported_light_count == 18U &&
            report.primitive_count == 19U && report.skin_count == 20U &&
            report.animation_count == 21U && report.clip_count == 22U &&
            report.diagnostic_count == 0U && report.anything_lost == CNA_FALSE);

    /* A derived value on input is refused rather than dropped, and the report stays as it was. */
    written.warning_count = 1U;
    REQUIRE(cna_model_set_gltf_import_report_ext(model, &written) == CNA_RESULT_INVALID_ARGUMENT);
    written.warning_count = 0U;
    written.anything_lost = CNA_TRUE;
    REQUIRE(cna_model_set_gltf_import_report_ext(model, &written) == CNA_RESULT_INVALID_ARGUMENT);
    written.anything_lost = CNA_FALSE;
    REQUIRE(cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.node_count == 11U);

    /* An information entry leaves the report reporting no loss. */
    details[0] = view("baseColorTexture");
    details[1] = view("normalTexture");
    descriptor.code = view("texture-transform-applied");
    descriptor.severity = CNA_GLTF_IMPORT_SEVERITY_INFORMATION_EXT;
    descriptor.kind = CNA_GLTF_IMPORT_KIND_INFORMATION_EXT;
    descriptor.subject = view("material 0");
    descriptor.count = 2U;
    descriptor.worst_magnitude = 0.0;
    descriptor.details = details;
    descriptor.detail_count = 2U;
    descriptor.message = view("Two texture transforms were applied as authored.");
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) == CNA_RESULT_SUCCESS &&
            cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.diagnostic_count == 1U && report.warning_count == 0U &&
            report.dropped_feature_count == 0U && report.approximation_count == 0U &&
            report.anything_lost == CNA_FALSE);

    /* Each warning kind moves exactly the derived answer it belongs to. */
    descriptor.code = view("skin-influences-dropped");
    descriptor.severity = CNA_GLTF_IMPORT_SEVERITY_WARNING_EXT;
    descriptor.kind = CNA_GLTF_IMPORT_KIND_DROPPED_DATA_EXT;
    descriptor.subject = view("mesh 3");
    descriptor.count = 5U;
    descriptor.worst_magnitude = 0.25;
    descriptor.details = 0;
    descriptor.detail_count = 0U;
    descriptor.message = view("Five joint influences beyond the fourth were discarded.");
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) == CNA_RESULT_SUCCESS);
    descriptor.code = view("weights-renormalised");
    descriptor.kind = CNA_GLTF_IMPORT_KIND_APPROXIMATION_EXT;
    descriptor.count = 3U;
    descriptor.worst_magnitude = 0.5;
    descriptor.message = view("Weights were renormalised.");
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) == CNA_RESULT_SUCCESS);
    descriptor.code = view("extension-unsupported");
    descriptor.kind = CNA_GLTF_IMPORT_KIND_UNSUPPORTED_FEATURE_EXT;
    descriptor.count = 7U;
    descriptor.worst_magnitude = 0.0;
    descriptor.message = view("KHR_materials_sheen is not implemented.");
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.diagnostic_count == 4U &&
            report.warning_count == 3U &&
            report.dropped_feature_count == 12U &&
            report.approximation_count == 3U &&
            report.anything_lost == CNA_TRUE);

    /* Entries keep discovery order, and every field of the first one reads back. */
    REQUIRE(cna_model_get_gltf_import_diagnostic_ext(model, 0U, &diagnostic) ==
                CNA_RESULT_SUCCESS &&
            diagnostic.severity == CNA_GLTF_IMPORT_SEVERITY_INFORMATION_EXT &&
            diagnostic.kind == CNA_GLTF_IMPORT_KIND_INFORMATION_EXT &&
            diagnostic.count == 2U && diagnostic.worst_magnitude == 0.0 &&
            diagnostic.detail_count == 2U);
    REQUIRE(cna_model_get_gltf_import_diagnostic_ext(model, 1U, &diagnostic) ==
                CNA_RESULT_SUCCESS &&
            diagnostic.severity == CNA_GLTF_IMPORT_SEVERITY_WARNING_EXT &&
            diagnostic.kind == CNA_GLTF_IMPORT_KIND_DROPPED_DATA_EXT &&
            diagnostic.count == 5U && diagnostic.worst_magnitude == 0.25 &&
            diagnostic.detail_count == 0U);

    /* The code is the identity to branch on; the message is display text. Both round-trip, and
       an exact-capacity copy writes no terminator. */
    REQUIRE(cna_model_get_gltf_import_diagnostic_code_byte_count_ext(model, 0U, &length) ==
                CNA_RESULT_SUCCESS && length == (uint64_t)strlen("texture-transform-applied"));
    memset(buffer, '#', sizeof(buffer));
    REQUIRE(cna_model_copy_gltf_import_diagnostic_code_ext(
                model, 0U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            string_equals(buffer, length, "texture-transform-applied") &&
            buffer[length] == '#');
    REQUIRE(cna_model_copy_gltf_import_diagnostic_subject_ext(
                model, 0U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            string_equals(buffer, length, "material 0") &&
            cna_model_get_gltf_import_diagnostic_subject_byte_count_ext(model, 0U, &length) ==
                CNA_RESULT_SUCCESS && length == (uint64_t)strlen("material 0"));
    REQUIRE(cna_model_get_gltf_import_diagnostic_message_byte_count_ext(model, 0U, &length) ==
                CNA_RESULT_SUCCESS &&
            length == (uint64_t)strlen("Two texture transforms were applied as authored.") &&
            cna_model_copy_gltf_import_diagnostic_message_ext(
                model, 0U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            string_equals(buffer, length,
                          "Two texture transforms were applied as authored."));
    REQUIRE(cna_model_get_gltf_import_diagnostic_detail_byte_count_ext(model, 0U, 1U, &length) ==
                CNA_RESULT_SUCCESS && length == (uint64_t)strlen("normalTexture") &&
            cna_model_copy_gltf_import_diagnostic_detail_ext(
                model, 0U, 1U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            string_equals(buffer, length, "normalTexture"));

    /* An empty subject is a valid answer, not a refusal. */
    REQUIRE(cna_model_copy_gltf_import_diagnostic_subject_ext(
                model, 1U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            length == (uint64_t)strlen("mesh 3"));

    /* Insufficient capacity reports the requirement and performs no partial write. */
    memset(buffer, '#', sizeof(buffer));
    REQUIRE(cna_model_copy_gltf_import_diagnostic_code_ext(model, 0U, buffer, 4U, &length) ==
                CNA_RESULT_BUFFER_TOO_SMALL &&
            length == (uint64_t)strlen("texture-transform-applied") &&
            buffer[0] == '#' && buffer[3] == '#');
    REQUIRE(cna_model_copy_gltf_import_diagnostic_code_ext(model, 0U, 0, 0U, &length) ==
                CNA_RESULT_BUFFER_TOO_SMALL && length ==
            (uint64_t)strlen("texture-transform-applied"));

    /* Out-of-range indices, malformed structures and undefined identities are all refused. */
    REQUIRE(cna_model_get_gltf_import_diagnostic_ext(model, 4U, &diagnostic) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_get_gltf_import_diagnostic_code_byte_count_ext(model, 4U, &length) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_copy_gltf_import_diagnostic_message_ext(
                model, 4U, buffer, sizeof(buffer), &length) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_get_gltf_import_diagnostic_detail_byte_count_ext(model, 1U, 0U, &length) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_copy_gltf_import_diagnostic_detail_ext(
                model, 0U, 2U, buffer, sizeof(buffer), &length) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_model_get_gltf_import_report_ext(model, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_set_gltf_import_report_ext(model, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_get_gltf_import_diagnostic_ext(model, 0U, &malformed) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_add_gltf_import_diagnostic_ext(model, 0) == CNA_RESULT_INVALID_ARGUMENT);
    descriptor.severity = CNA_GLTF_IMPORT_SEVERITY_MAXIMUM_EXT + 1U;
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) ==
            CNA_RESULT_INVALID_ARGUMENT);
    descriptor.severity = CNA_GLTF_IMPORT_SEVERITY_WARNING_EXT;
    descriptor.kind = CNA_GLTF_IMPORT_KIND_MAXIMUM_EXT + 1U;
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) ==
            CNA_RESULT_INVALID_ARGUMENT);
    descriptor.kind = CNA_GLTF_IMPORT_KIND_DROPPED_DATA_EXT;
    descriptor.details = 0;
    descriptor.detail_count = 1U;
    REQUIRE(cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.diagnostic_count == 4U);

    /* Setting the report again clears the diagnostics with it. */
    REQUIRE(cna_model_set_gltf_import_report_ext(model, &written) == CNA_RESULT_SUCCESS &&
            cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_SUCCESS &&
            report.diagnostic_count == 0U && report.anything_lost == CNA_FALSE &&
            report.node_count == 11U);

    /* Argument validation precedes the handle lookup, so the stale-handle check needs a
       descriptor that would otherwise be accepted. */
    descriptor.details = details;
    descriptor.detail_count = 2U;
    REQUIRE(cna_model_destroy(model) == CNA_RESULT_SUCCESS &&
            cna_model_get_gltf_import_report_ext(model, &report) == CNA_RESULT_INVALID_HANDLE &&
            cna_model_add_gltf_import_diagnostic_ext(model, &descriptor) ==
                CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static int validate_aggregate(const CNA_Handle device)
{
    CNA_ModelBoneHandle root = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle child = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle root_alias = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle bone_alias = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle parent_alias = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle bones[2];
    CNA_ModelBoneHandle parents[1];
    CNA_ModelMeshHandle mesh = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle mesh_alias = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle meshes[1];
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_ModelHandle simple = CNA_INVALID_HANDLE;
    CNA_ModelHandle invalid = UINT64_MAX;
    CNA_ModelBoneCollectionHandle bone_collection = CNA_INVALID_HANDLE;
    CNA_ModelMeshCollectionHandle mesh_collection = CNA_INVALID_HANDLE;
    CNA_Matrix local[2];
    CNA_Matrix copied[2];
    CNA_Matrix absolute[2];
    CNA_Matrix sentinel[1];
    CNA_Bool has = CNA_FALSE;
    CNA_RendererInfo renderer = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U};
    uint64_t count = UINT64_MAX;
    int32_t index = -1;

    REQUIRE(cna_model_bone_create(0, string_view("Root"), &root) == CNA_RESULT_SUCCESS &&
            cna_model_bone_create(1, string_view("Child"), &child) == CNA_RESULT_SUCCESS &&
            cna_model_bone_add_child(root, child) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_create_named(
                device, string_view("Mesh"), 0, 0U, &mesh) == CNA_RESULT_SUCCESS);
    bones[0] = root;
    bones[1] = child;
    meshes[0] = mesh;
    parents[0] = child;
    REQUIRE(cna_model_create_with_parents(
                device, bones, 2U, meshes, 1U, parents, 1U, 1U, &model) ==
                CNA_RESULT_SUCCESS &&
            cna_model_create(device, bones, 2U, 0, 0U, &simple) ==
                CNA_RESULT_SUCCESS &&
            cna_model_create_with_parents(
                device, bones, 2U, meshes, 1U, parents, 0U, 2U, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE &&
            cna_model_create_with_parents(
                device, bones, 2U, meshes, 1U, parents, 2U, 0U, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);

    REQUIRE(cna_model_get_root(model, &has, &root_alias) == CNA_RESULT_SUCCESS &&
            has == CNA_TRUE &&
            cna_model_bone_get_index(root_alias, &index) == CNA_RESULT_SUCCESS && index == 1 &&
            cna_model_get_bones(model, &bone_collection) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_get_count(bone_collection, &count) ==
                CNA_RESULT_SUCCESS && count == 2U &&
            cna_model_bone_collection_get_at(bone_collection, 0U, &bone_alias) ==
                CNA_RESULT_SUCCESS &&
            cna_model_bone_get_index(bone_alias, &index) == CNA_RESULT_SUCCESS && index == 0 &&
            cna_model_get_meshes(model, &mesh_collection) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_collection_get_count(mesh_collection, &count) ==
                CNA_RESULT_SUCCESS && count == 1U &&
            cna_model_mesh_collection_get_at(mesh_collection, 0U, &mesh_alias) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_get_parent_bone(mesh_alias, &has, &parent_alias) ==
                CNA_RESULT_SUCCESS && has == CNA_TRUE &&
            cna_model_bone_get_index(parent_alias, &index) == CNA_RESULT_SUCCESS && index == 1);

    REQUIRE(cna_matrix_get_identity(&local[0]) == CNA_RESULT_SUCCESS &&
            cna_matrix_get_identity(&local[1]) == CNA_RESULT_SUCCESS);
    local[0].m41 = 10.0F;
    local[1].m42 = 20.0F;
    sentinel[0] = local[0];
    REQUIRE(cna_model_set_bone_transforms(model, local, 1U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_set_bone_transforms(model, local, 2U) == CNA_RESULT_SUCCESS &&
            cna_model_get_bone_transform_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_model_copy_bone_transforms(model, sentinel, 1U, &count) ==
                CNA_RESULT_BUFFER_TOO_SMALL && count == 2U &&
            memcmp(&sentinel[0], &local[0], sizeof(CNA_Matrix)) == 0 &&
            cna_model_copy_bone_transforms(model, copied, 2U, &count) ==
                CNA_RESULT_SUCCESS && memcmp(copied, local, sizeof(local)) == 0 &&
            cna_model_copy_absolute_bone_transforms(model, absolute, 2U, &count) ==
                CNA_RESULT_SUCCESS && absolute[0].m41 == 10.0F &&
            absolute[0].m42 == 0.0F && absolute[1].m41 == 10.0F &&
            absolute[1].m42 == 20.0F);

    REQUIRE(cna_model_bone_destroy(root) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(child) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_destroy(mesh) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_get_name_byte_count(mesh_alias, &count) == CNA_RESULT_SUCCESS &&
            count == 4U &&
            cna_graphics_device_get_renderer_info(device, &renderer) == CNA_RESULT_SUCCESS);
    if (renderer.renderer_type == CNA_GRAPHICS_RENDERER_SDL_RENDERER) {
        REQUIRE(cna_model_draw(model, local[0], local[0], local[0]) ==
                    CNA_RESULT_NOT_SUPPORTED);
    } else {
        REQUIRE(cna_model_draw(model, local[0], local[0], local[0]) == CNA_RESULT_SUCCESS);
    }

    REQUIRE(cna_model_destroy(model) == CNA_RESULT_SUCCESS &&
            cna_model_destroy(simple) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(root_alias) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(bone_alias) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(parent_alias) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_destroy(bone_collection) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_destroy(mesh_alias) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_collection_destroy(mesh_collection) == CNA_RESULT_SUCCESS);
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CallbackState* const state = (CallbackState*)context;
    (void)out_error;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &state->device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (!validate_aggregate(state->device)) {
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
    static const char Title[] = "C API Model";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!validate_default() || release_count != 2 || !validate_gltf_import_report() ||
        cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 2 ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "ModelSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
