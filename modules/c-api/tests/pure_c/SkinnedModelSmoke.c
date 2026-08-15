// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_SkinnedModelEXTHandle) == 8U,
               "CNA skinned-model handle size changed");
_Static_assert(sizeof(CNA_KeyframeEXT) == 48U,
               "CNA skinned keyframe size changed");
_Static_assert(sizeof(CNA_BoneTrackEXTDescriptor) == 24U,
               "CNA skinned bone-track descriptor size changed");
_Static_assert(sizeof(CNA_AnimationClipEXTDescriptor) == 24U,
               "CNA skinned animation-clip descriptor size changed");
_Static_assert(sizeof(CNA_NamedAnimationClipEXTDescriptor) == 40U,
               "CNA named clip descriptor size changed");
_Static_assert(sizeof(CNA_SkinnedModelEXTDescriptor) == 48U,
               "CNA skinned-model descriptor size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "SkinnedModelSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_Handle device;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_SkinnedModelEXTHandle model;
    CNA_Result result;
} WrongThreadState;

static CNA_Matrix identity_matrix(void)
{
    const CNA_Matrix result = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    return result;
}

static CNA_StringView string_view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

static int nearly_equal(const float left, const float right)
{
    return fabsf(left - right) < 0.0001F;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint64_t count = 0U;
    state->result = cna_skinned_model_ext_get_bone_count(state->model, &count);
    return 0;
}

static int validate_animation_state(void)
{
    int32_t parents[2] = {-1, 0};
    int32_t copied_parents[2] = {99, 99};
    CNA_Matrix bind_pose[2] = {identity_matrix(), identity_matrix()};
    CNA_Matrix inverse_bind_pose[2] = {identity_matrix(), identity_matrix()};
    CNA_Matrix copied_matrices[2];
    CNA_Matrix computed[2];
    CNA_Matrix sentinel = identity_matrix();
    CNA_KeyframeEXT keys[2] = {
        {0.0, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
         {1.0F, 1.0F, 1.0F}},
        {1.0, {2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
         {1.0F, 1.0F, 1.0F}}};
    CNA_BoneTrackEXTDescriptor track = {0, 0U, keys, 2U};
    CNA_AnimationClipEXTDescriptor clip = {1.0, &track, 1U};
    CNA_NamedAnimationClipEXTDescriptor named = {0};
    CNA_SkinnedModelEXTDescriptor descriptor = {0};
    CNA_SkinnedModelEXTHandle model = CNA_INVALID_HANDLE;
    CNA_SkinnedModelEXTHandle moved = CNA_INVALID_HANDLE;
    CNA_SkinnedModelEXTHandle destination = CNA_INVALID_HANDLE;
    CNA_SkinnedModelEXTHandle other = CNA_INVALID_HANDLE;
    CNA_SkinnedModelEXTHandle invalid = UINT64_MAX;
    CNA_ModelMeshPartHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_KeyframeEXT copied_keys[2];
    CNA_Bool found = CNA_FALSE;
    uint64_t count = UINT64_MAX;
    uint64_t track_count = UINT64_MAX;
    uint64_t name_size = UINT64_MAX;
    double duration = -1.0;
    int32_t bone_index = -99;
    char name[4] = {0, 0, 0, 0};

    bind_pose[1].m42 = 1.0F;
    named.name = string_view("Walk");
    named.clip = clip;
    descriptor.bone_count = 2;
    descriptor.parent_bone_indices = parents;
    descriptor.bind_pose_local = bind_pose;
    descriptor.inverse_bind_pose_global = inverse_bind_pose;
    descriptor.clips = &named;
    descriptor.clip_count = 1U;

    descriptor.reserved = 1U;
    REQUIRE(cna_skinned_model_ext_create(&descriptor, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);
    descriptor.reserved = 0U;
    REQUIRE(cna_skinned_model_ext_create(&descriptor, &model) == CNA_RESULT_SUCCESS);
    keys[1].translation.x = 9.0F;
    parents[1] = -1;
    bind_pose[1].m42 = 9.0F;

    REQUIRE(cna_skinned_model_ext_get_bone_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_skinned_model_ext_copy_parent_bone_indices(
                model, copied_parents, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 2U && copied_parents[0] == 99 && copied_parents[1] == 99 &&
            cna_skinned_model_ext_copy_parent_bone_indices(
                model, copied_parents, 2U, &count) == CNA_RESULT_SUCCESS &&
            copied_parents[0] == -1 && copied_parents[1] == 0 &&
            cna_skinned_model_ext_copy_bind_pose_local(
                model, copied_matrices, 2U, &count) == CNA_RESULT_SUCCESS &&
            copied_matrices[1].m42 == 1.0F &&
            cna_skinned_model_ext_copy_inverse_bind_pose_global(
                model, copied_matrices, 2U, &count) == CNA_RESULT_SUCCESS &&
            copied_matrices[1].m44 == 1.0F);

    REQUIRE(cna_skinned_model_ext_get_clip_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 1U &&
            cna_skinned_model_ext_get_clip_name_byte_count_at(
                model, 0U, &name_size) == CNA_RESULT_SUCCESS && name_size == 4U &&
            cna_skinned_model_ext_copy_clip_name_at(
                model, 0U, name, sizeof(name), &name_size) == CNA_RESULT_SUCCESS &&
            memcmp(name, "Walk", 4U) == 0 &&
            cna_skinned_model_ext_get_clip_info(
                model, string_view("Walk"), &found, &duration, &track_count) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE && duration == 1.0 &&
            track_count == 1U &&
            cna_skinned_model_ext_get_clip_info(
                model, string_view("Missing"), &found, &duration, &track_count) ==
                CNA_RESULT_SUCCESS && found == CNA_FALSE && duration == 0.0 &&
            track_count == 0U);

    REQUIRE(cna_skinned_model_ext_copy_clip_track(
                model, string_view("Walk"), 0U, &bone_index,
                copied_keys, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 2U && bone_index == -99 &&
            cna_skinned_model_ext_copy_clip_track(
                model, string_view("Walk"), 0U, &bone_index,
                copied_keys, 2U, &count) == CNA_RESULT_SUCCESS &&
            bone_index == 0 && count == 2U && copied_keys[1].translation.x == 2.0F);

    sentinel.m41 = 77.0F;
    REQUIRE(cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Walk"), 0.5, CNA_FALSE,
                &sentinel, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 2U && sentinel.m41 == 77.0F &&
            cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Walk"), 0.5, CNA_FALSE,
                computed, 2U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(computed[0].m41, 1.0F) &&
            nearly_equal(computed[1].m41, 1.0F) &&
            nearly_equal(computed[1].m42, 1.0F) &&
            cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Walk"), 1.5, CNA_TRUE,
                computed, 2U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(computed[0].m41, 1.0F) &&
            cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Walk"), -1.0, CNA_FALSE,
                computed, 2U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(computed[0].m41, 0.0F) &&
            cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Missing"), 0.0, CNA_FALSE,
                computed, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Walk"), NAN, CNA_FALSE,
                computed, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_model_ext_compute_bone_transforms(
                model, string_view("Walk"), 0.0, (CNA_Bool)2U,
                computed, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT);

    track.reserved = 1U;
    REQUIRE(cna_skinned_model_ext_set_clip(model, string_view("Bad"), &clip) ==
                CNA_RESULT_INVALID_ARGUMENT);
    track.reserved = 0U;
    keys[0].time_seconds = 2.0;
    REQUIRE(cna_skinned_model_ext_set_clip(model, string_view("Bad"), &clip) ==
                CNA_RESULT_INVALID_ARGUMENT);
    keys[0].time_seconds = 0.0;

    clip.duration_seconds = 2.0;
    clip.tracks = 0;
    clip.track_count = 0U;
    REQUIRE(cna_skinned_model_ext_set_clip(model, string_view("Idle"), &clip) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_clip_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_skinned_model_ext_copy_clip_name_at(
                model, 0U, name, sizeof(name), &name_size) == CNA_RESULT_SUCCESS &&
            memcmp(name, "Idle", 4U) == 0 &&
            cna_skinned_model_ext_remove_clip(model, string_view("Idle")) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_clip_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 1U);

    parents[0] = -1;
    parents[1] = 1;
    bind_pose[1] = identity_matrix();
    REQUIRE(cna_skinned_model_ext_set_skeleton(
                model, 2, parents, bind_pose, inverse_bind_pose) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_model_ext_get_bone_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 2U);
    parents[1] = 0;

    WrongThreadState wrong_thread = {model, CNA_RESULT_SUCCESS};
    thrd_t thread_id;
    REQUIRE(thrd_create(&thread_id, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread_id, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD &&
            cna_model_mesh_part_create_default(&wrong_kind) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_bone_count(wrong_kind, &count) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_mesh_part_destroy(wrong_kind) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_skinned_model_ext_create_move(model, &moved) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_bone_count(model, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_skinned_model_ext_get_bone_count(moved, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_skinned_model_ext_create_default(&destination) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_move_assign(destination, moved) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_bone_count(moved, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_skinned_model_ext_get_bone_count(destination, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_skinned_model_ext_move_assign(destination, destination) ==
                CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_skinned_model_ext_create_default(&other) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_attach_parts(destination, other) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_model_ext_set_skeleton(
                other, 2, parents, bind_pose, inverse_bind_pose) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_attach_parts(destination, other) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_destroy(other) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_destroy(moved) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_destroy(model) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_destroy(destination) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_bone_count(destination, &count) ==
                CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static CNA_Result create_vertex_buffer(
    const CNA_Handle device,
    CNA_VertexBufferHandle* const out_buffer)
{
    const CNA_VertexBufferCreateInfo info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), CNA_INVALID_HANDLE,
        0, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    return cna_vertex_buffer_create(device, &info, out_buffer);
}

static CNA_Result create_index_buffer(
    const CNA_Handle device,
    CNA_IndexBufferHandle* const out_buffer)
{
    const CNA_IndexBufferCreateInfo info = {
        sizeof(CNA_IndexBufferCreateInfo), UINT32_C(1), 3,
        CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE,
        CNA_FALSE, {0U, 0U, 0U}};
    return cna_index_buffer_create(device, &info, out_buffer);
}

static int validate_resources(const CNA_Handle device)
{
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    CNA_SkinnedModelEXTHandle model = CNA_INVALID_HANDLE;
    CNA_SkinnedModelEXTHandle other = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle replacement_part = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle alias = CNA_INVALID_HANDLE;
    CNA_VertexBufferHandle vertex = CNA_INVALID_HANDLE;
    CNA_VertexBufferHandle replacement_vertex = CNA_INVALID_HANDLE;
    CNA_IndexBufferHandle index = CNA_INVALID_HANDLE;
    CNA_IndexBufferHandle replacement_index = CNA_INVALID_HANDLE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle returned_texture = UINT64_MAX;
    CNA_Bool has_texture = CNA_FALSE;
    uint64_t vertex_count = UINT64_MAX;
    uint64_t index_count = UINT64_MAX;
    uint64_t part_count = UINT64_MAX;
    uint64_t texture_count = UINT64_MAX;
    uint64_t byte_count = UINT64_MAX;
    char part_name[4];
    const CNA_Result vertex_result = create_vertex_buffer(device, &vertex);
    const CNA_Result index_result = create_index_buffer(device, &index);

    REQUIRE((vertex_result == CNA_RESULT_SUCCESS && index_result == CNA_RESULT_SUCCESS) ||
            (vertex_result == CNA_RESULT_NOT_SUPPORTED &&
             index_result == CNA_RESULT_NOT_SUPPORTED));
    if (vertex_result == CNA_RESULT_NOT_SUPPORTED) {
        return 1;
    }
    REQUIRE(cna_texture2d_create(device, &texture_info, &texture) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_create_default(&part) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_create_default(&model) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_add_part(
                model, string_view("Body"), vertex, index, part, texture) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS &&
            cna_vertex_buffer_destroy(vertex) == CNA_RESULT_INVALID_STATE &&
            cna_index_buffer_destroy(index) == CNA_RESULT_INVALID_STATE &&
            cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE &&
            cna_graphics_resource_dispose(texture) == CNA_RESULT_INVALID_STATE &&
            cna_skinned_model_ext_get_part_count(model, &part_count) == CNA_RESULT_SUCCESS &&
            part_count == 1U &&
            cna_skinned_model_ext_get_part_name_byte_count_at(
                model, 0U, &byte_count) == CNA_RESULT_SUCCESS && byte_count == 4U &&
            cna_skinned_model_ext_copy_part_name_at(
                model, 0U, part_name, sizeof(part_name), &byte_count) ==
                CNA_RESULT_SUCCESS && memcmp(part_name, "Body", 4U) == 0 &&
            cna_skinned_model_ext_get_owned_resource_counts(
                model, &vertex_count, &index_count, &part_count, &texture_count) ==
                CNA_RESULT_SUCCESS && vertex_count == 1U && index_count == 1U &&
            part_count == 1U && texture_count == 1U &&
            cna_skinned_model_ext_get_part_at(
                model, 0U, &alias, &has_texture, &returned_texture) ==
                CNA_RESULT_SUCCESS && has_texture == CNA_TRUE &&
            returned_texture == texture &&
            cna_model_mesh_part_set_vertex_buffer(alias, CNA_INVALID_HANDLE) ==
                CNA_RESULT_INVALID_STATE &&
            cna_model_mesh_part_destroy(alias) == CNA_RESULT_SUCCESS &&
            create_vertex_buffer(device, &replacement_vertex) == CNA_RESULT_SUCCESS &&
            create_index_buffer(device, &replacement_index) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_create_default(&replacement_part) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_create_default(&other) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_add_part(
                other, string_view("Body"), replacement_vertex, replacement_index,
                replacement_part, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(replacement_part) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_attach_parts(model, other) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_part_count(other, &part_count) == CNA_RESULT_SUCCESS &&
            part_count == 0U &&
            cna_vertex_buffer_destroy(vertex) == CNA_RESULT_SUCCESS &&
            cna_index_buffer_destroy(index) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_owned_resource_counts(
                model, &vertex_count, &index_count, &part_count, &texture_count) ==
                CNA_RESULT_SUCCESS && vertex_count == 1U && index_count == 1U &&
            part_count == 1U && texture_count == 0U &&
            cna_skinned_model_ext_get_part_at(
                model, 0U, &alias, &has_texture, &returned_texture) ==
                CNA_RESULT_SUCCESS && has_texture == CNA_FALSE &&
            returned_texture == CNA_INVALID_HANDLE &&
            cna_skinned_model_ext_remove_part(model, string_view("Body")) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_get_part_count(model, &part_count) == CNA_RESULT_SUCCESS &&
            part_count == 0U &&
            cna_model_mesh_part_destroy(alias) == CNA_RESULT_SUCCESS &&
            cna_vertex_buffer_destroy(replacement_vertex) == CNA_RESULT_SUCCESS &&
            cna_index_buffer_destroy(replacement_index) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_destroy(other) == CNA_RESULT_SUCCESS &&
            cna_skinned_model_ext_destroy(model) == CNA_RESULT_SUCCESS);
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
    static const char title[] = "C API SkinnedModelEXT";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {title, sizeof(title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!validate_animation_state() ||
        cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 2 ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "SkinnedModelSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
