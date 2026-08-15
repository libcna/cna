// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_MorphTargetDataEXTHandle) == 8U,
               "CNA morph-target-data handle size changed");
_Static_assert(sizeof(CNA_MorphWeightKeyframeEXTDescriptor) == 56U,
               "CNA morph keyframe descriptor size changed");
_Static_assert(sizeof(CNA_MorphWeightTrackEXTDescriptor) == 24U,
               "CNA morph track descriptor size changed");
_Static_assert(sizeof(CNA_MorphTargetDeltaEXTDescriptor) == 32U,
               "CNA morph delta descriptor size changed");
_Static_assert(sizeof(CNA_MorphTargetDataEXTDescriptor) == 80U,
               "CNA morph data descriptor size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "MorphTargetSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_MorphTargetDataEXTHandle data;
    CNA_Handle device;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_MorphTargetDataEXTHandle data;
    CNA_Result result;
} WrongThreadState;

static int nearly_equal(const float left, const float right)
{
    return fabsf(left - right) < 0.00001F;
}

static CNA_MorphTargetDataEXTDescriptor make_descriptor(
    uint8_t* const base,
    CNA_MorphTargetDeltaEXTDescriptor* const targets,
    float* const initial_weights,
    CNA_MorphWeightKeyframeEXTDescriptor* const keys)
{
    static const float key0_weights[2] = {0.0F, 0.0F};
    static const float key1_weights[2] = {1.0F, 1.0F};
    static const float zero_tangents[2] = {0.0F, 0.0F};
    static const CNA_Vector3 target0_positions[1] = {{0.0F, 0.0F, 1.0F}};
    static const CNA_Vector3 target1_positions[1] = {{2.0F, 0.0F, 0.0F}};
    static const CNA_Vector3 target1_normals[1] = {{1.0F, 0.0F, 0.0F}};
    const float vertex[8] = {
        0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.25F, 0.75F};

    memcpy(base, vertex, sizeof(vertex));
    targets[0] = (CNA_MorphTargetDeltaEXTDescriptor){
        target0_positions, 1U, 0, 0U};
    targets[1] = (CNA_MorphTargetDeltaEXTDescriptor){
        target1_positions, 1U, target1_normals, 1U};
    initial_weights[0] = 0.0F;
    initial_weights[1] = 0.0F;
    keys[0] = (CNA_MorphWeightKeyframeEXTDescriptor){
        0.0, key0_weights, 2U, zero_tangents, 2U, zero_tangents, 2U};
    keys[1] = (CNA_MorphWeightKeyframeEXTDescriptor){
        1.0, key1_weights, 2U, zero_tangents, 2U, zero_tangents, 2U};
    return (CNA_MorphTargetDataEXTDescriptor){
        base, 32U, 32, targets, 2U, initial_weights, 2U,
        {keys, 2U, CNA_FALSE, CNA_TRUE}};
}

static int validate_data(const CNA_MorphTargetDataEXTHandle data)
{
    static const char expected_name[] =
        "Microsoft.Xna.Framework.Graphics.MorphTargetDataEXT";
    const float changed_weights[2] = {0.25F, 0.75F};
    const float blend_weights[2] = {1.0F, 1.0F};
    uint8_t base[32];
    uint8_t blended[32];
    uint8_t sentinel[1] = {0xA5U};
    CNA_Vector3 deltas[1];
    float copied_weights[2] = {-1.0F, -1.0F};
    float key_weights[2];
    float in_tangents[2];
    float out_tangents[2];
    char type_name[sizeof(expected_name) - 1U];
    CNA_Bool step = CNA_TRUE;
    CNA_Bool cubic = CNA_FALSE;
    uint64_t count = UINT64_MAX;
    uint64_t count1 = UINT64_MAX;
    uint64_t count2 = UINT64_MAX;
    double seconds = -1.0;
    int32_t stride = 0;
    float position[3];
    float normal[3];
    const float replacement_weights[2] = {0.5F, 0.25F};
    const CNA_MorphWeightKeyframeEXTDescriptor replacement_key = {
        2.0, replacement_weights, 2U, 0, 0U, 0, 0U};
    CNA_MorphWeightTrackEXTDescriptor replacement_track = {
        &replacement_key, 1U, CNA_TRUE, CNA_FALSE};

    REQUIRE(cna_morph_target_data_ext_get_type_name_byte_count(data, &count) ==
                CNA_RESULT_SUCCESS && count == sizeof(expected_name) - 1U &&
            cna_morph_target_data_ext_copy_type_name(
                data, type_name, sizeof(type_name), &count) == CNA_RESULT_SUCCESS &&
            memcmp(type_name, expected_name, sizeof(type_name)) == 0 &&
            cna_morph_target_data_ext_get_stride(data, &stride) == CNA_RESULT_SUCCESS &&
            stride == 32 &&
            cna_morph_target_data_ext_get_base_vertex_byte_count(data, &count) ==
                CNA_RESULT_SUCCESS && count == 32U &&
            cna_morph_target_data_ext_copy_base_vertex_bytes(
                data, sentinel, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 32U && sentinel[0] == 0xA5U &&
            cna_morph_target_data_ext_copy_base_vertex_bytes(
                data, base, sizeof(base), &count) == CNA_RESULT_SUCCESS &&
            cna_morph_target_data_ext_get_target_count(data, &count) ==
                CNA_RESULT_SUCCESS && count == 2U);

    REQUIRE(cna_morph_target_data_ext_copy_position_deltas(
                data, 0U, deltas, 1U, &count) == CNA_RESULT_SUCCESS &&
            count == 1U && deltas[0].z == 1.0F &&
            cna_morph_target_data_ext_copy_normal_deltas(
                data, 0U, 0, 0U, &count) == CNA_RESULT_SUCCESS && count == 0U &&
            cna_morph_target_data_ext_copy_position_deltas(
                data, 1U, deltas, 1U, &count) == CNA_RESULT_SUCCESS &&
            deltas[0].x == 2.0F &&
            cna_morph_target_data_ext_copy_normal_deltas(
                data, 1U, deltas, 1U, &count) == CNA_RESULT_SUCCESS &&
            deltas[0].x == 1.0F &&
            cna_morph_target_data_ext_copy_position_deltas(
                data, 2U, deltas, 1U, &count) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_morph_target_data_ext_copy_weights(
                data, copied_weights, 2U, &count) == CNA_RESULT_SUCCESS &&
            count == 2U && copied_weights[0] == 0.0F && copied_weights[1] == 0.0F &&
            cna_morph_target_data_ext_set_weights(data, changed_weights, 1U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_morph_target_data_ext_set_weights(data, changed_weights, 2U) ==
                CNA_RESULT_SUCCESS &&
            cna_morph_target_data_ext_copy_weights(
                data, copied_weights, 2U, &count) == CNA_RESULT_SUCCESS &&
            copied_weights[0] == 0.25F && copied_weights[1] == 0.75F);

    REQUIRE(cna_morph_target_data_ext_get_weight_track_info(
                data, &count, &step, &cubic) == CNA_RESULT_SUCCESS &&
            count == 2U && step == CNA_FALSE && cubic == CNA_TRUE &&
            cna_morph_target_data_ext_copy_weight_keyframe(
                data, 1U, &seconds, key_weights, 2U, &count,
                in_tangents, 2U, &count1, out_tangents, 2U, &count2) ==
                CNA_RESULT_SUCCESS && seconds == 1.0 && count == 2U &&
            count1 == 2U && count2 == 2U && key_weights[0] == 1.0F &&
            in_tangents[0] == 0.0F && out_tangents[1] == 0.0F &&
            cna_morph_target_data_ext_copy_weight_keyframe(
                data, 0U, &seconds, key_weights, 1U, &count,
                in_tangents, 2U, &count1, out_tangents, 2U, &count2) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(cna_morph_target_data_ext_set_weight_track(data, &replacement_track) ==
                CNA_RESULT_SUCCESS &&
            cna_morph_target_data_ext_get_weight_track_info(
                data, &count, &step, &cubic) == CNA_RESULT_SUCCESS &&
            count == 1U && step == CNA_TRUE && cubic == CNA_FALSE &&
            cna_morph_target_data_ext_copy_weight_keyframe(
                data, 0U, &seconds, key_weights, 2U, &count,
                0, 0U, &count1, 0, 0U, &count2) == CNA_RESULT_SUCCESS &&
            seconds == 2.0 && count == 2U && count1 == 0U && count2 == 0U &&
            key_weights[0] == 0.5F && key_weights[1] == 0.25F);
    replacement_track.step_interpolation = (CNA_Bool)2U;
    REQUIRE(cna_morph_target_data_ext_set_weight_track(data, &replacement_track) ==
                CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_morph_target_data_ext_blend(
                data, blend_weights, 2U, sentinel, 1U, &count) ==
                CNA_RESULT_BUFFER_TOO_SMALL && sentinel[0] == 0xA5U && count == 32U &&
            cna_morph_target_data_ext_blend(
                data, blend_weights, 2U, blended, sizeof(blended), &count) ==
                CNA_RESULT_SUCCESS && count == 32U);
    memcpy(position, blended, sizeof(position));
    memcpy(normal, blended + 12U, sizeof(normal));
    REQUIRE(position[0] == 2.0F && position[1] == 0.0F && position[2] == 1.0F &&
            nearly_equal(normal[0], 0.70710678F) && normal[1] == 0.0F &&
            nearly_equal(normal[2], 0.70710678F) &&
            memcmp(base + 24U, blended + 24U, 8U) == 0);
    return 1;
}

static int validate_tracks(void)
{
    const float start[1] = {0.0F};
    const float end[1] = {1.0F};
    const float zero[1] = {0.0F};
    CNA_MorphWeightKeyframeEXTDescriptor keys[2] = {
        {0.0, start, 1U, zero, 1U, zero, 1U},
        {1.0, end, 1U, zero, 1U, zero, 1U}};
    CNA_MorphWeightTrackEXTDescriptor track = {
        keys, 2U, CNA_FALSE, CNA_FALSE};
    float output[1] = {-1.0F};
    uint64_t count = UINT64_MAX;

    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.25, output, 1U, &count) == CNA_RESULT_SUCCESS &&
            count == 1U && nearly_equal(output[0], 0.25F));
    track.step_interpolation = CNA_TRUE;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.75, output, 1U, &count) == CNA_RESULT_SUCCESS &&
            output[0] == 0.0F);
    track.step_interpolation = CNA_FALSE;
    track.cubic_spline = CNA_TRUE;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.25, output, 1U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(output[0], 0.15625F) &&
            cna_morph_weight_track_ext_evaluate(
                &track, -1.0, output, 1U, &count) == CNA_RESULT_SUCCESS &&
            output[0] == 0.0F &&
            cna_morph_weight_track_ext_evaluate(
                &track, 2.0, output, 1U, &count) == CNA_RESULT_SUCCESS &&
            output[0] == 1.0F);
    track.step_interpolation = (CNA_Bool)2U;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.5, output, 1U, &count) == CNA_RESULT_INVALID_ARGUMENT);
    track.step_interpolation = CNA_FALSE;
    keys[1].time_seconds = -1.0;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.5, output, 1U, &count) == CNA_RESULT_INVALID_ARGUMENT);
    keys[1].time_seconds = 1.0;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, NAN, output, 1U, &count) == CNA_RESULT_INVALID_ARGUMENT);
    keys[1].time_seconds = NAN;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.5, output, 1U, &count) == CNA_RESULT_INVALID_ARGUMENT);
    keys[1].time_seconds = 1.0;
    keys[1].in_tangent_count = 0U;
    REQUIRE(cna_morph_weight_track_ext_evaluate(
                &track, 0.5, output, 0U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 1U);
    return 1;
}

static CNA_Result create_vertex_buffer(
    const CNA_Handle device,
    CNA_VertexBufferHandle* const out_buffer)
{
    const CNA_VertexBufferCreateInfo info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), CNA_INVALID_HANDLE,
        1, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    return cna_vertex_buffer_create(device, &info, out_buffer);
}

static int validate_part(CallbackState* const state)
{
    const float uploaded_weights[2] = {0.5F, 0.25F};
    const float detached_weights[2] = {0.25F, 0.5F};
    CNA_ModelMeshPartHandle part = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle mesh = CNA_INVALID_HANDLE;
    CNA_MorphTargetDataEXTHandle alias = CNA_INVALID_HANDLE;
    CNA_VertexBufferHandle buffer = CNA_INVALID_HANDLE;
    CNA_Bool has = CNA_FALSE;
    float copied[2] = {0.0F, 0.0F};
    uint64_t count = 0U;
    const CNA_Result buffer_result = create_vertex_buffer(state->device, &buffer);

    REQUIRE(cna_model_mesh_part_create_default(&part) == CNA_RESULT_SUCCESS &&
            cna_morph_target_data_ext_get_target_count(part, &count) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_mesh_part_get_morph_target_data_ext(part, &has, &alias) ==
                CNA_RESULT_SUCCESS && has == CNA_FALSE && alias == CNA_INVALID_HANDLE &&
            cna_model_mesh_part_set_morph_weights_ext(part, uploaded_weights, 2U) ==
                CNA_RESULT_INVALID_STATE &&
            cna_model_mesh_part_set_morph_target_data_ext(part, state->data) ==
                CNA_RESULT_SUCCESS &&
            cna_morph_target_data_ext_destroy(state->data) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_morph_target_data_ext(part, &has, &alias) ==
                CNA_RESULT_SUCCESS && has == CNA_TRUE && alias != CNA_INVALID_HANDLE &&
            cna_model_mesh_part_set_morph_weights_ext(part, uploaded_weights, 2U) ==
                CNA_RESULT_INVALID_STATE);
    state->data = CNA_INVALID_HANDLE;

    REQUIRE(buffer_result == CNA_RESULT_SUCCESS ||
            buffer_result == CNA_RESULT_NOT_SUPPORTED);
    if (buffer_result == CNA_RESULT_SUCCESS) {
        REQUIRE(cna_model_mesh_part_set_vertex_buffer(part, buffer) == CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_set_morph_weights_ext(
                    part, uploaded_weights, 2U) == CNA_RESULT_SUCCESS &&
                cna_morph_target_data_ext_copy_weights(
                    alias, copied, 2U, &count) == CNA_RESULT_SUCCESS &&
                count == 2U && copied[0] == 0.5F && copied[1] == 0.25F &&
                cna_model_mesh_create(state->device, &part, 1U, &mesh) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_destroy(mesh) == CNA_RESULT_SUCCESS &&
                cna_model_mesh_part_set_morph_weights_ext(
                    part, detached_weights, 2U) == CNA_RESULT_SUCCESS &&
                cna_morph_target_data_ext_copy_weights(
                    alias, copied, 2U, &count) == CNA_RESULT_SUCCESS &&
                copied[0] == 0.25F && copied[1] == 0.5F &&
                cna_model_mesh_part_set_vertex_buffer(part, CNA_INVALID_HANDLE) ==
                    CNA_RESULT_SUCCESS &&
                cna_vertex_buffer_destroy(buffer) == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_morph_target_data_ext_destroy(alias) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_morph_target_data_ext(
                part, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_morph_target_data_ext(part, &has, &alias) ==
                CNA_RESULT_SUCCESS && has == CNA_FALSE && alias == CNA_INVALID_HANDLE &&
            cna_model_mesh_part_destroy(part) == CNA_RESULT_SUCCESS);
    return 1;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint64_t count = 0U;
    state->result = cna_morph_target_data_ext_get_target_count(state->data, &count);
    return 0;
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
    if (!validate_part(state)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    uint8_t base[32];
    CNA_MorphTargetDeltaEXTDescriptor targets[2];
    float initial_weights[2];
    CNA_MorphWeightKeyframeEXTDescriptor keys[2];
    CNA_MorphTargetDataEXTDescriptor descriptor =
        make_descriptor(base, targets, initial_weights, keys);
    CallbackState state = {CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, 0};
    WrongThreadState wrong_thread;
    thrd_t thread;
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char title[] = "C API MorphTarget";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {title, sizeof(title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_MorphTargetDataEXTHandle invalid = UINT64_MAX;
    uint64_t count = 0U;

    if (cna_morph_target_data_ext_create(&descriptor, &state.data) !=
            CNA_RESULT_SUCCESS ||
        !validate_data(state.data) || !validate_tracks()) {
        return 1;
    }
    descriptor.stride = 31;
    REQUIRE(cna_morph_target_data_ext_create(&descriptor, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);
    descriptor.stride = 32;
    descriptor.weight_count = 1U;
    REQUIRE(cna_morph_target_data_ext_create(&descriptor, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);
    descriptor.weight_count = 2U;

    wrong_thread = (WrongThreadState){state.data, CNA_RESULT_SUCCESS};
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD);
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 2 ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "MorphTargetSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    REQUIRE(cna_morph_target_data_ext_get_target_count(state.data, &count) ==
                CNA_RESULT_INVALID_HANDLE);
    return 0;
}
