// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <string.h>

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static int completions;

static void on_complete(void* const context)
{
    *(int*)context += 1;
}

static int validate_values(void)
{
    CNA_AvatarExpression expression;
    CNA_AvatarAppearanceEXT appearance;

    if (cna_avatar_expression_init(&expression) != CNA_RESULT_SUCCESS ||
        cna_avatar_expression_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A default face is neutral in every part. */
    if (expression.mouth != CNA_AVATAR_MOUTH_NEUTRAL ||
        expression.left_eye != CNA_AVATAR_EYE_NEUTRAL ||
        expression.right_eye != CNA_AVATAR_EYE_NEUTRAL ||
        expression.left_eyebrow != CNA_AVATAR_EYEBROW_NEUTRAL ||
        expression.right_eyebrow != CNA_AVATAR_EYEBROW_NEUTRAL) {
        return 0;
    }
    if (cna_avatar_appearance_init_ext(&appearance) != CNA_RESULT_SUCCESS ||
        cna_avatar_appearance_init_ext(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The default colors are opaque, which is what makes them usable without further setup. */
    return appearance.skin_color.a == UINT8_C(255) && appearance.hair_color.a == UINT8_C(255) &&
        appearance.shoes_color.a == UINT8_C(255);
}

static int validate_description(const CNA_SignedInGamerHandle gamer)
{
    CNA_AvatarDescriptionHandle random = CNA_INVALID_HANDLE;
    CNA_AvatarDescriptionHandle male = CNA_INVALID_HANDLE;
    CNA_AvatarDescriptionHandle empty = CNA_INVALID_HANDLE;
    CNA_AvatarDescriptionHandle from_gamer = CNA_INVALID_HANDLE;
    CNA_AvatarDescriptionInfo info = {sizeof(CNA_AvatarDescriptionInfo), UINT32_C(1), UINT32_C(0),
                                      0.0F, UINT64_C(0), UINT8_C(0),
                                      {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    static uint8_t bytes[2048];
    uint64_t size = UINT64_C(0);
    int ok;

    if (cna_avatar_description_create_random(&random) != CNA_RESULT_SUCCESS ||
        random == CNA_INVALID_HANDLE ||
        cna_avatar_description_create_random(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A description is always exactly one size; its height and body type are constant on this
       runtime, because the canonical format carries neither. */
    ok = cna_avatar_description_get_info(random, &info) == CNA_RESULT_SUCCESS &&
        info.description_byte_count == CNA_AVATAR_DESCRIPTION_BYTE_COUNT &&
        info.body_type == CNA_AVATAR_BODY_TYPE_FEMALE && info.height == 0.0F;

    ok = ok && cna_avatar_description_copy_description(random, bytes, sizeof(bytes), &size) ==
                   CNA_RESULT_SUCCESS &&
        size == info.description_byte_count;
    ok = ok && cna_avatar_description_copy_description(random, bytes, UINT64_C(1), &size) ==
                   CNA_RESULT_BUFFER_TOO_SMALL;

    /* Asking for a male body still reports female, because the format carries no body type at all. */
    ok = ok && cna_avatar_description_create_random_for_body_type(CNA_AVATAR_BODY_TYPE_MALE,
                                                                 &male) == CNA_RESULT_SUCCESS &&
        cna_avatar_description_get_info(male, &info) == CNA_RESULT_SUCCESS &&
        info.body_type == CNA_AVATAR_BODY_TYPE_FEMALE;
    ok = ok && cna_avatar_description_create_random_for_body_type(UINT32_C(9999), &empty) ==
                   CNA_RESULT_INVALID_ARGUMENT;

    /* A description must be exactly one size, and validity is decided by its first byte. */
    ok = ok && cna_avatar_description_create(bytes, UINT64_C(4), &empty) ==
                   CNA_RESULT_INVALID_ARGUMENT;
    memset(bytes, 0, (size_t)CNA_AVATAR_DESCRIPTION_BYTE_COUNT);
    ok = ok && cna_avatar_description_create(bytes, CNA_AVATAR_DESCRIPTION_BYTE_COUNT, &empty) ==
                   CNA_RESULT_SUCCESS &&
        cna_avatar_description_get_info(empty, &info) == CNA_RESULT_SUCCESS &&
        info.is_valid == CNA_FALSE;
    if (ok && empty != CNA_INVALID_HANDLE) {
        ok = cna_avatar_description_destroy(empty) == CNA_RESULT_SUCCESS;
        empty = CNA_INVALID_HANDLE;
    }
    bytes[0] = UINT8_C(1);
    ok = ok && cna_avatar_description_create(bytes, CNA_AVATAR_DESCRIPTION_BYTE_COUNT, &empty) ==
                   CNA_RESULT_SUCCESS &&
        cna_avatar_description_get_info(empty, &info) == CNA_RESULT_SUCCESS &&
        info.is_valid == CNA_TRUE;

    /* Reading a gamer's description is one synchronous call that still runs the callback. */
    completions = 0;
    ok = ok && cna_avatar_description_get_from_gamer(gamer, &on_complete, &completions,
                                                     &from_gamer) == CNA_RESULT_SUCCESS &&
        completions == 1 && from_gamer != CNA_INVALID_HANDLE;

    if (from_gamer != CNA_INVALID_HANDLE) {
        ok = (cna_avatar_description_destroy(from_gamer) == CNA_RESULT_SUCCESS) && ok;
    }
    if (empty != CNA_INVALID_HANDLE) {
        ok = (cna_avatar_description_destroy(empty) == CNA_RESULT_SUCCESS) && ok;
    }
    if (male != CNA_INVALID_HANDLE) {
        ok = (cna_avatar_description_destroy(male) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = (cna_avatar_description_destroy(random) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_avatar_description_get_info(random, &info) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_animation(void)
{
    CNA_AvatarAnimationHandle animation = CNA_INVALID_HANDLE;
    CNA_AvatarAnimationHandle rejected = CNA_INVALID_HANDLE;
    CNA_AvatarAnimationInfo info = {sizeof(CNA_AvatarAnimationInfo), UINT32_C(1), 0, UINT8_C(0),
                                    {0U, 0U, 0U}, INT64_C(0), INT64_C(0)};
    CNA_AvatarExpression expression;
    CNA_Matrix transform;
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    if (cna_avatar_animation_create(CNA_AVATAR_ANIMATION_PRESET_WAVE, &animation) !=
            CNA_RESULT_SUCCESS ||
        animation == CNA_INVALID_HANDLE ||
        cna_avatar_animation_create(UINT32_C(9999), &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* A preset carries the whole skeleton but **no timeline**: its length is zero until a real clip
       is loaded, which is why advancing it below moves nothing. */
    ok = cna_avatar_animation_get_info(animation, &info) == CNA_RESULT_SUCCESS &&
        info.is_disposed == CNA_FALSE &&
        info.bone_transform_count == CNA_AVATAR_RENDERER_BONE_COUNT &&
        info.length_ticks == INT64_C(0) && info.current_position_ticks == INT64_C(0);

    ok = ok && cna_avatar_animation_get_bone_transform_at(animation, 0, &transform) ==
                   CNA_RESULT_SUCCESS &&
        cna_avatar_animation_get_bone_transform_at(animation, info.bone_transform_count,
                                                   &transform) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_animation_get_bone_transform_at(animation, -1, &transform) ==
            CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_avatar_animation_get_expression(animation, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_avatar_expression_init(&expression) == CNA_RESULT_SUCCESS &&
        cna_avatar_animation_get_expression(animation, &expression) == CNA_RESULT_SUCCESS &&
        expression.mouth <= CNA_AVATAR_MOUTH_MAXIMUM;

    /* Advancing a zero-length animation is accepted and leaves the position where it was. */
    ok = ok && cna_avatar_animation_update(animation, INT64_C(100000), CNA_TRUE) ==
                   CNA_RESULT_SUCCESS &&
        cna_avatar_animation_get_info(animation, &info) == CNA_RESULT_SUCCESS &&
        info.current_position_ticks == INT64_C(0);
    ok = ok && cna_avatar_animation_set_current_position(animation, INT64_C(0)) ==
                   CNA_RESULT_SUCCESS &&
        cna_avatar_animation_get_info(animation, &info) == CNA_RESULT_SUCCESS &&
        info.current_position_ticks == INT64_C(0);
    ok = ok && cna_avatar_animation_update(animation, INT64_C(1), UINT8_C(9)) ==
                   CNA_RESULT_INVALID_ARGUMENT;

    /* A preset's clip name is the identity's own spelling, and it can be replaced. */
    ok = ok && cna_avatar_animation_get_real_clip_name_size_ext(animation, &size) ==
                   CNA_RESULT_SUCCESS &&
        size <= sizeof(text);
    if (ok) {
        const CNA_Result copied =
            cna_avatar_animation_copy_real_clip_name_ext(animation, text, sizeof(text), &size);
        ok = copied == CNA_RESULT_SUCCESS && size == UINT64_C(4) &&
            memcmp(text, "Wave", (size_t)4) == 0;
    }
    ok = ok && cna_avatar_animation_set_real_clip_name_ext(animation, view("CustomWave")) ==
                   CNA_RESULT_SUCCESS;
    if (ok) {
        const CNA_Result copied =
            cna_avatar_animation_copy_real_clip_name_ext(animation, text, sizeof(text), &size);
        ok = copied == CNA_RESULT_SUCCESS && size == UINT64_C(10) &&
            memcmp(text, "CustomWave", (size_t)10) == 0;
    }

    ok = (cna_avatar_animation_destroy(animation) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_avatar_animation_destroy(animation) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_renderer(void)
{
    CNA_AvatarDescriptionHandle description = CNA_INVALID_HANDLE;
    CNA_AvatarRendererHandle renderer = CNA_INVALID_HANDLE;
    CNA_AvatarAnimationHandle animation = CNA_INVALID_HANDLE;
    CNA_AvatarRendererInfo info = {sizeof(CNA_AvatarRendererInfo), UINT32_C(1), UINT32_C(0),
                                   UINT8_C(0), UINT8_C(0), {0U, 0U}};
    CNA_AvatarExpression expression;
    CNA_AvatarAppearanceEXT appearance;
    CNA_Matrix world;
    CNA_Matrix view_matrix;
    CNA_Matrix projection;
    CNA_Matrix bones[CNA_AVATAR_RENDERER_BONE_COUNT];
    CNA_Vector3 light;
    CNA_Vector3 direction;
    CNA_Vector3 ambient;
    int32_t parent = -99;
    int32_t index;
    int ok;

    if (cna_avatar_description_create_random(&description) != CNA_RESULT_SUCCESS ||
        cna_avatar_renderer_create(description, CNA_FALSE, &renderer) != CNA_RESULT_SUCCESS ||
        renderer == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The renderer keeps the description alive, so releasing the caller's handle is safe. */
    ok = cna_avatar_description_destroy(description) == CNA_RESULT_SUCCESS;
    ok = ok && cna_avatar_renderer_get_info(renderer, &info) == CNA_RESULT_SUCCESS &&
        info.is_disposed == CNA_FALSE && info.state <= CNA_AVATAR_RENDERER_STATE_MAXIMUM &&
        info.is_real_rendering_enabled == CNA_FALSE;

    ok = ok && cna_avatar_renderer_get_transforms(renderer, &world, &view_matrix, &projection) ==
                   CNA_RESULT_SUCCESS;
    /* Each transform is optional, so a caller may ask for only what it needs. */
    ok = ok && cna_avatar_renderer_get_transforms(renderer, &world, 0, 0) == CNA_RESULT_SUCCESS;
    world.m41 = 5.0F;
    ok = ok && cna_avatar_renderer_set_transforms(renderer, &world, 0, 0) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_get_transforms(renderer, &world, 0, 0) == CNA_RESULT_SUCCESS &&
        world.m41 == 5.0F;
    ok = ok && cna_avatar_renderer_set_transforms(renderer, 0, 0, 0) == CNA_RESULT_SUCCESS;

    ok = ok && cna_avatar_renderer_get_lighting(renderer, &light, &direction, &ambient) ==
                   CNA_RESULT_SUCCESS;
    light.x = 0.5F;
    ok = ok && cna_avatar_renderer_set_lighting(renderer, &light, 0, 0) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_get_lighting(renderer, &light, 0, 0) == CNA_RESULT_SUCCESS &&
        light.x == 0.5F;
    light.x = (float)INFINITY;
    ok = ok && cna_avatar_renderer_set_lighting(renderer, &light, 0, 0) ==
                   CNA_RESULT_INVALID_ARGUMENT;

    /* The skeleton always has the same bone count, and every index outside it is refused. */
    for (index = 0; ok && index < CNA_AVATAR_RENDERER_BONE_COUNT; ++index) {
        ok = cna_avatar_renderer_get_parent_bone_at(renderer, index, &parent) ==
             CNA_RESULT_SUCCESS;
        /* Start every bone from identity: the bind pose is not readable while the avatar's assets
           are unavailable, which is what this runtime always reports. */
        memset(&bones[index], 0, sizeof(bones[index]));
        bones[index].m11 = 1.0F;
        bones[index].m22 = 1.0F;
        bones[index].m33 = 1.0F;
        bones[index].m44 = 1.0F;
    }
    ok = ok && cna_avatar_renderer_get_parent_bone_at(renderer, CNA_AVATAR_RENDERER_BONE_COUNT,
                                                      &parent) == CNA_RESULT_INVALID_ARGUMENT;
    /* The first bone is the root, so it has no parent. */
    ok = ok && cna_avatar_renderer_get_parent_bone_at(renderer, 0, &parent) == CNA_RESULT_SUCCESS &&
        parent < 0;
    /* The bind pose needs assets this runtime has not got, so it refuses with a state failure --
       which is a different answer from an index outside the skeleton. */
    ok = ok && info.state == CNA_AVATAR_RENDERER_STATE_UNAVAILABLE &&
        cna_avatar_renderer_get_bind_pose_at(renderer, 0, &world) == CNA_RESULT_INVALID_STATE;

    ok = ok && cna_avatar_expression_init(&expression) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_draw_bones(renderer, bones,
                                       (uint64_t)CNA_AVATAR_RENDERER_BONE_COUNT,
                                       &expression) == CNA_RESULT_SUCCESS;
    /* A pose with the wrong number of bones is refused, and so is an undefined expression. */
    ok = ok && cna_avatar_renderer_draw_bones(renderer, bones, UINT64_C(3), &expression) ==
                   CNA_RESULT_INVALID_ARGUMENT;
    expression.mouth = UINT32_C(9999);
    ok = ok && cna_avatar_renderer_draw_bones(renderer, bones,
                                              (uint64_t)CNA_AVATAR_RENDERER_BONE_COUNT,
                                              &expression) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_renderer_draw_bones(renderer, bones,
                                       (uint64_t)CNA_AVATAR_RENDERER_BONE_COUNT, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_avatar_animation_create(CNA_AVATAR_ANIMATION_PRESET_STAND_0, &animation) ==
                   CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_draw_animation(renderer, animation) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_draw_animation(renderer, CNA_INVALID_HANDLE) ==
            CNA_RESULT_INVALID_HANDLE;

    ok = ok && cna_avatar_appearance_init_ext(&appearance) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_set_appearance_ext(renderer, &appearance) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_set_appearance_ext(renderer, 0) == CNA_RESULT_INVALID_ARGUMENT;

    /* Real rendering needs a device and a model; refusing an invalid one is the boundary this
       container can exercise. */
    ok = ok && cna_avatar_renderer_enable_real_rendering_ext(renderer, CNA_INVALID_HANDLE,
                                                             CNA_INVALID_HANDLE) ==
                   CNA_RESULT_INVALID_HANDLE;
    /* Drawing the real model without one is a state failure rather than a silent no-op. */
    ok = ok && cna_avatar_renderer_draw_real_ext(renderer, view("Wave"), INT64_C(0), CNA_FALSE) ==
                   CNA_RESULT_INVALID_STATE;

    if (animation != CNA_INVALID_HANDLE) {
        ok = (cna_avatar_animation_destroy(animation) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = (cna_avatar_renderer_destroy(renderer) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_avatar_renderer_get_info(renderer, &info) == CNA_RESULT_INVALID_HANDLE;
}

int main(void)
{
    CNA_SignedInGamerHandle gamer = CNA_INVALID_HANDLE;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    int status = CNA_TEST_FAIL(0);

    if (!validate_values()) {
        return CNA_TEST_FAIL(1);
    }
    if (cna_signed_in_gamer_create_ext(view("CnaCApiAvatar"), CNA_FALSE, CNA_FALSE,
                                       CNA_PLAYER_INDEX_ONE, &gamer) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_description(gamer)) {
        status = CNA_TEST_FAIL(3);
    }
    if (status == 0 && !validate_animation()) {
        status = CNA_TEST_FAIL(4);
    }
    if (status == 0 && !validate_renderer()) {
        status = CNA_TEST_FAIL(5);
    }
    /* The change notification is static: it is about descriptions in general, not about one. */
    if (status == 0 &&
        (cna_avatar_description_subscribe_changed_ext(&on_complete, &completions,
                                                      &registration) != CNA_RESULT_SUCCESS ||
         registration == CNA_INVALID_HANDLE ||
         cna_avatar_description_subscribe_changed_ext(0, &completions, &rejected) !=
             CNA_RESULT_INVALID_ARGUMENT ||
         rejected != CNA_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(6);
    }
    /* One unsubscribe route releases every gamer-services registration, this one included. */
    if (status == 0 &&
        (cna_gamer_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS ||
         cna_gamer_unsubscribe_ext(registration) != CNA_RESULT_INVALID_HANDLE)) {
        status = CNA_TEST_FAIL(7);
    }
    if (status == 0 && cna_signed_in_gamer_destroy(gamer) != CNA_RESULT_SUCCESS) {
        status = CNA_TEST_FAIL(8);
    }
    return status;
}
