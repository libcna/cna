// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_SkinningDataHandle) == 8U,
               "CNA SkinningData handle size changed");
_Static_assert(sizeof(CNA_AnimationPlayerHandle) == 8U,
               "CNA AnimationPlayer handle size changed");
_Static_assert(sizeof(CNA_SkinningDataDescriptor) == 64U,
               "CNA SkinningData descriptor size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "AnimationPlayerSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct WrongThreadState {
    CNA_AnimationPlayerHandle player;
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
    double position = 0.0;
    state->result = cna_animation_player_get_current_position(
        state->player, &position);
    return 0;
}

static int validate_all(void)
{
    static const char expected_type[] =
        "Microsoft.Xna.Framework.Graphics.SkinningData";
    int32_t hierarchy[2] = {-1, 0};
    int32_t copied_hierarchy[2] = {99, 99};
    CNA_Matrix bind_pose[2] = {identity_matrix(), identity_matrix()};
    CNA_Matrix inverse_bind_pose[2] = {identity_matrix(), identity_matrix()};
    CNA_Matrix root_prefix[2] = {identity_matrix(), identity_matrix()};
    CNA_Matrix transforms[2];
    CNA_Matrix sentinel = identity_matrix();
    CNA_KeyframeEXT keys[2] = {
        {0.0, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
         {1.0F, 1.0F, 1.0F}},
        {1.0, {2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
         {1.0F, 1.0F, 1.0F}}};
    CNA_BoneTrackEXTDescriptor track = {0, 0U, keys, 2U};
    CNA_AnimationClipEXTDescriptor clip = {1.0, &track, 1U};
    CNA_NamedAnimationClipEXTDescriptor named = {0};
    CNA_SkinningDataDescriptor descriptor = {0};
    CNA_SkinningDataHandle data = CNA_INVALID_HANDLE;
    CNA_SkinningDataHandle invalid = UINT64_MAX;
    CNA_AnimationPlayerHandle player = CNA_INVALID_HANDLE;
    CNA_SkinnedModelEXTHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_KeyframeEXT copied_keys[2];
    CNA_Bool has_clip = CNA_TRUE;
    CNA_Bool found = CNA_FALSE;
    uint64_t count = UINT64_MAX;
    uint64_t track_count = UINT64_MAX;
    uint64_t byte_count = UINT64_MAX;
    double duration = -1.0;
    double position = -1.0;
    int32_t bone_index = -99;
    char buffer[sizeof(expected_type) - 1U];

    bind_pose[1].m42 = 1.0F;
    root_prefix[0].m43 = 3.0F;
    named.name = string_view("Walk");
    named.clip = clip;
    descriptor.bone_count = 2;
    descriptor.skeleton_hierarchy = hierarchy;
    descriptor.bind_pose = bind_pose;
    descriptor.inverse_bind_pose = inverse_bind_pose;
    descriptor.skeleton_root_prefix = root_prefix;
    descriptor.skeleton_root_prefix_count = 2U;
    descriptor.clips = &named;
    descriptor.clip_count = 1U;

    descriptor.skeleton_root_prefix_count = 1U;
    REQUIRE(cna_skinning_data_create(&descriptor, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);
    descriptor.skeleton_root_prefix_count = 2U;
    descriptor.reserved = 1U;
    invalid = UINT64_MAX;
    REQUIRE(cna_skinning_data_create(&descriptor, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);
    descriptor.reserved = 0U;
    REQUIRE(cna_skinning_data_create(&descriptor, &data) == CNA_RESULT_SUCCESS);

    hierarchy[1] = -1;
    bind_pose[1].m42 = 9.0F;
    root_prefix[0].m43 = 9.0F;
    keys[1].translation.x = 9.0F;
    REQUIRE(cna_skinning_data_get_type_name_byte_count(data, &byte_count) ==
                CNA_RESULT_SUCCESS && byte_count == sizeof(expected_type) - 1U &&
            cna_skinning_data_copy_type_name(
                data, buffer, sizeof(buffer), &byte_count) == CNA_RESULT_SUCCESS &&
            memcmp(buffer, expected_type, sizeof(buffer)) == 0 &&
            cna_skinning_data_get_bone_count(data, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_skinning_data_copy_skeleton_hierarchy(
                data, copied_hierarchy, 2U, &count) == CNA_RESULT_SUCCESS &&
            copied_hierarchy[0] == -1 && copied_hierarchy[1] == 0 &&
            cna_skinning_data_copy_bind_pose(
                data, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            transforms[1].m42 == 1.0F &&
            cna_skinning_data_copy_inverse_bind_pose(
                data, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            transforms[1].m44 == 1.0F &&
            cna_skinning_data_copy_skeleton_root_prefix(
                data, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            count == 2U && transforms[0].m43 == 3.0F);

    REQUIRE(cna_skinning_data_get_clip_count(data, &count) == CNA_RESULT_SUCCESS &&
            count == 1U &&
            cna_skinning_data_get_clip_name_byte_count_at(
                data, 0U, &byte_count) == CNA_RESULT_SUCCESS && byte_count == 4U &&
            cna_skinning_data_copy_clip_name_at(
                data, 0U, buffer, 4U, &byte_count) == CNA_RESULT_SUCCESS &&
            memcmp(buffer, "Walk", 4U) == 0 &&
            cna_skinning_data_get_clip_info(
                data, string_view("Walk"), &found, &duration, &track_count) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE && duration == 1.0 &&
            track_count == 1U &&
            cna_skinning_data_copy_clip_track(
                data, string_view("Walk"), 0U, &bone_index,
                copied_keys, 2U, &count) == CNA_RESULT_SUCCESS &&
            bone_index == 0 && count == 2U && copied_keys[1].translation.x == 2.0F);

    /* CBIND-051C: which index space a clip's bone indices are in. Every clip built before rigid
       node animation existed was a joint-palette clip, which is why that is the default; the two
       spaces must never be silently interchanged. */
    {
        CNA_ClipTargetSpaceEXT space = UINT32_MAX;
        REQUIRE(cna_skinning_data_get_clip_target_space_ext(data, 0U, &space) ==
                    CNA_RESULT_SUCCESS && space == CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT &&
                cna_skinning_data_set_clip_target_space_ext(
                    data, 0U, CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT) == CNA_RESULT_SUCCESS &&
                cna_skinning_data_get_clip_target_space_ext(data, 0U, &space) ==
                    CNA_RESULT_SUCCESS && space == CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT &&
                cna_skinning_data_set_clip_target_space_ext(
                    data, 0U, CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_skinning_data_get_clip_target_space_ext(data, 1U, &space) ==
                    CNA_RESULT_INVALID_ARGUMENT &&
                cna_skinning_data_set_clip_target_space_ext(
                    data, 1U, CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT) ==
                    CNA_RESULT_INVALID_ARGUMENT &&
                cna_skinning_data_set_clip_target_space_ext(
                    data, 0U, CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT + 1U) ==
                    CNA_RESULT_INVALID_ARGUMENT &&
                cna_skinning_data_get_clip_target_space_ext(data, 0U, 0) ==
                    CNA_RESULT_INVALID_ARGUMENT);
    }

    REQUIRE(cna_animation_player_create(data, &player) == CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_position(player, &position) ==
                CNA_RESULT_SUCCESS && position == 0.0 &&
            cna_animation_player_get_current_clip_info(
                player, &has_clip, &duration, &track_count) == CNA_RESULT_SUCCESS &&
            has_clip == CNA_FALSE && duration == 0.0 && track_count == 0U &&
            cna_animation_player_get_current_clip_name_byte_count(
                player, &byte_count) == CNA_RESULT_SUCCESS && byte_count == 0U &&
            cna_animation_player_copy_bone_transforms(
                player, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            transforms[1].m42 == 1.0F &&
            cna_animation_player_copy_world_transforms(
                player, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            transforms[0].m43 == 3.0F && transforms[1].m42 == 1.0F &&
            transforms[1].m43 == 3.0F &&
            cna_animation_player_copy_skin_transforms(
                player, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            transforms[0].m43 == 3.0F);

    REQUIRE(cna_animation_player_start_clip(player, string_view("Missing")) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_animation_player_start_clip(player, string_view("Walk")) ==
                CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_clip_info(
                player, &has_clip, &duration, &track_count) == CNA_RESULT_SUCCESS &&
            has_clip == CNA_TRUE && duration == 1.0 && track_count == 1U &&
            cna_animation_player_get_current_clip_name_byte_count(
                player, &byte_count) == CNA_RESULT_SUCCESS && byte_count == 4U &&
            cna_animation_player_copy_current_clip_name(
                player, buffer, 4U, &byte_count) == CNA_RESULT_SUCCESS &&
            memcmp(buffer, "Walk", 4U) == 0);

    sentinel.m41 = 77.0F;
    REQUIRE(cna_animation_player_update(player, 0.5, CNA_TRUE, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_position(player, &position) ==
                CNA_RESULT_SUCCESS && position == 0.5 &&
            cna_animation_player_copy_bone_transforms(
                player, &sentinel, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 2U && sentinel.m41 == 77.0F &&
            cna_animation_player_copy_bone_transforms(
                player, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(transforms[0].m41, 1.0F) &&
            cna_animation_player_copy_world_transforms(
                player, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(transforms[0].m41, 1.0F) && transforms[0].m43 == 3.0F &&
            nearly_equal(transforms[1].m41, 1.0F) && transforms[1].m42 == 1.0F &&
            transforms[1].m43 == 3.0F &&
            cna_animation_player_copy_skin_transforms(
                player, transforms, 2U, &count) == CNA_RESULT_SUCCESS &&
            nearly_equal(transforms[1].m41, 1.0F));

    REQUIRE(cna_animation_player_update(player, 2.0, CNA_FALSE, CNA_FALSE) ==
                CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_position(player, &position) ==
                CNA_RESULT_SUCCESS && position == 1.0 &&
            cna_animation_player_update(player, 1.5, CNA_FALSE, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_position(player, &position) ==
                CNA_RESULT_SUCCESS && position == 0.5 &&
            cna_animation_player_update(player, NAN, CNA_FALSE, CNA_TRUE) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_animation_player_update(player, 0.0, (CNA_Bool)2U, CNA_TRUE) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_animation_player_update(player, 0.0, CNA_FALSE, (CNA_Bool)2U) ==
                CNA_RESULT_INVALID_ARGUMENT);

    WrongThreadState wrong_thread = {player, CNA_RESULT_SUCCESS};
    thrd_t thread_id;
    REQUIRE(thrd_create(&thread_id, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread_id, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD &&
            cna_skinned_model_ext_create_default(&wrong_kind) == CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_position(wrong_kind, &position) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_skinned_model_ext_destroy(wrong_kind) == CNA_RESULT_SUCCESS &&
            cna_skinning_data_destroy(data) == CNA_RESULT_SUCCESS &&
            cna_animation_player_update(player, 0.25, CNA_FALSE, CNA_FALSE) ==
                CNA_RESULT_SUCCESS &&
            cna_animation_player_destroy(player) == CNA_RESULT_SUCCESS &&
            cna_animation_player_get_current_position(player, &position) ==
                CNA_RESULT_INVALID_HANDLE);
    return 1;
}

/* CBIND-051E: the clips a glTF scene declares apart from any one skeleton, the rig root a file
   names, and the two helpers that pose a model directly. */
static int validate_model_animations_ext(void)
{
    const CNA_KeyframeEXT keys[2] = {
        {0.0, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
        {1.0, {2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}};
    const CNA_BoneTrackEXTDescriptor tracks[1] = {{0, 0U, keys, 2U}};
    const CNA_AnimationClipEXTDescriptor clip = {1.0, tracks, 1U};
    const CNA_NamedAnimationClipEXTDescriptor named[2] = {
        {{"Walk", 4U}, {1.0, tracks, 1U}},
        {{"Idle", 4U}, {2.0, tracks, 1U}}};
    CNA_ModelAnimationsEXTHandle animations = CNA_INVALID_HANDLE;
    CNA_ModelAnimationsEXTHandle empty = CNA_INVALID_HANDLE;
    CNA_ModelHandle model = CNA_INVALID_HANDLE;
    CNA_SkinningDataHandle data = CNA_INVALID_HANDLE;
    CNA_ClipTargetSpaceEXT space = UINT32_MAX;
    char buffer[64];
    double duration = -1.0;
    uint64_t count = UINT64_MAX;
    uint64_t length = UINT64_MAX;
    uint64_t posed = UINT64_MAX;
    int32_t root = INT32_MIN;

    (void)clip;

    REQUIRE(cna_model_animations_ext_create(0, 0U, &empty) == CNA_RESULT_SUCCESS &&
            cna_model_animations_ext_get_clip_count(empty, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_model_animations_ext_destroy(empty) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_model_animations_ext_create(named, 2U, &animations) == CNA_RESULT_SUCCESS &&
            cna_model_animations_ext_get_clip_count(animations, &count) == CNA_RESULT_SUCCESS &&
            count == 2U);

    /* The canonical type keeps clips in a hash map, so the C order is sorted rather than
       whatever the map happens to traverse. */
    REQUIRE(cna_model_animations_ext_copy_clip_name_at(
                animations, 0U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            length == 4U && memcmp(buffer, "Idle", 4U) == 0 &&
            cna_model_animations_ext_copy_clip_name_at(
                animations, 1U, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            memcmp(buffer, "Walk", 4U) == 0 &&
            cna_model_animations_ext_get_clip_name_byte_count_at(animations, 1U, &length) ==
                CNA_RESULT_SUCCESS && length == 4U);

    REQUIRE(cna_model_animations_ext_get_clip_info_at(
                animations, 0U, &duration, &count, &space) == CNA_RESULT_SUCCESS &&
            duration == 2.0 && count == 1U &&
            space == CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT &&
            cna_model_animations_ext_get_clip_info_at(
                animations, 1U, &duration, &count, &space) == CNA_RESULT_SUCCESS &&
            duration == 1.0);

    REQUIRE(cna_model_animations_ext_get_type_name_byte_count(animations, &length) ==
                CNA_RESULT_SUCCESS && length > 0U &&
            cna_model_animations_ext_copy_type_name(
                animations, buffer, sizeof(buffer), &count) == CNA_RESULT_SUCCESS &&
            count == length &&
            memcmp(buffer, "Microsoft.Xna.Framework.Graphics.ModelAnimationsEXT",
                   (size_t)count) == 0);

    /* A joint-palette clip applied to Model::Bones is refused, not silently mis-posed. */
    REQUIRE(cna_model_create_default(&model) == CNA_RESULT_SUCCESS &&
            cna_model_apply_clip_to_bones_ext(model, animations, 1U, 0.5) ==
                CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_model_animations_ext_set_clip_target_space_at(
                animations, 1U, CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT) == CNA_RESULT_SUCCESS &&
            cna_model_animations_ext_get_clip_info_at(
                animations, 1U, &duration, &count, &space) == CNA_RESULT_SUCCESS &&
            space == CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT &&
            cna_model_apply_clip_to_bones_ext(model, animations, 1U, 0.5) ==
                CNA_RESULT_SUCCESS);

    /* The declared rig root a file names, which older paths leave unset. */
    {
        const CNA_Matrix bind[1] = {{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                     0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
        const int32_t hierarchy[1] = {-1};
        const CNA_SkinningDataDescriptor skinning = {
            1, 0U, hierarchy, bind, bind, 0, 0U, 0, 0U};
        REQUIRE(cna_skinning_data_create(&skinning, &data) == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_skinning_data_get_skeleton_root_node_index_ext(data, &root) ==
                CNA_RESULT_SUCCESS && root == -1 &&
            cna_skinning_data_get_skeleton_root_name_byte_count_ext(data, &length) ==
                CNA_RESULT_SUCCESS && length == 0U &&
            cna_skinning_data_set_skeleton_root_node_index_ext(data, 4) == CNA_RESULT_SUCCESS &&
            cna_skinning_data_get_skeleton_root_node_index_ext(data, &root) ==
                CNA_RESULT_SUCCESS && root == 4 &&
            cna_skinning_data_set_skeleton_root_name_ext(data, string_view("Armature")) ==
                CNA_RESULT_SUCCESS &&
            cna_skinning_data_copy_skeleton_root_name_ext(
                data, buffer, sizeof(buffer), &length) == CNA_RESULT_SUCCESS &&
            length == 8U && memcmp(buffer, "Armature", 8U) == 0);
    memset(buffer, '#', sizeof(buffer));
    REQUIRE(cna_skinning_data_copy_skeleton_root_name_ext(data, buffer, 2U, &length) ==
                CNA_RESULT_BUFFER_TOO_SMALL && length == 8U && buffer[0] == '#');

    /* A model with no skinned effects poses none, and says so rather than failing. */
    REQUIRE(cna_model_apply_bind_pose_bone_transforms_ext(model, data, &posed) ==
                CNA_RESULT_SUCCESS && posed == 0U);

    /* Out-of-range indices, undefined identities, null outputs and stale handles are refused. */
    REQUIRE(cna_model_animations_ext_get_clip_info_at(
                animations, 2U, &duration, &count, &space) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_animations_ext_copy_clip_name_at(
                animations, 2U, buffer, sizeof(buffer), &length) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_animations_ext_set_clip_target_space_at(
                animations, 0U, CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT + 1U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_animations_ext_get_clip_count(animations, 0) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_animations_ext_create(0, 1U, &empty) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_animations_ext_create(named, 2U, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_apply_clip_to_bones_ext(model, animations, 5U, 0.0) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_apply_bind_pose_bone_transforms_ext(model, data, 0) ==
                CNA_RESULT_INVALID_ARGUMENT);
    {
        /* Duplicate clip names are refused rather than silently collapsing to one. */
        const CNA_NamedAnimationClipEXTDescriptor duplicate[2] = {
            {{"Walk", 4U}, {1.0, tracks, 1U}},
            {{"Walk", 4U}, {1.0, tracks, 1U}}};
        REQUIRE(cna_model_animations_ext_create(duplicate, 2U, &empty) ==
                    CNA_RESULT_INVALID_ARGUMENT && empty == CNA_INVALID_HANDLE);
    }

    REQUIRE(cna_model_animations_ext_destroy(animations) == CNA_RESULT_SUCCESS &&
            cna_model_animations_ext_get_clip_count(animations, &count) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_model_apply_clip_to_bones_ext(model, animations, 0U, 0.0) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_skinning_data_destroy(data) == CNA_RESULT_SUCCESS &&
            cna_model_destroy(model) == CNA_RESULT_SUCCESS);
    return 1;
}

int main(void)
{
    return validate_all() && validate_model_animations_ext() ? 0 : 1;
}
