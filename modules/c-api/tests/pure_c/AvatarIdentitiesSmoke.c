// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* The six contiguous avatar identities, written out in canonical order: an entry that does not
   sit at its own index is a moved ordinal. */

static const CNA_AvatarBodyType avatarBodyType[] = {
    CNA_AVATAR_BODY_TYPE_FEMALE,
    CNA_AVATAR_BODY_TYPE_MALE,
};

static const CNA_AvatarRendererState avatarRendererState[] = {
    CNA_AVATAR_RENDERER_STATE_LOADING,
    CNA_AVATAR_RENDERER_STATE_READY,
    CNA_AVATAR_RENDERER_STATE_UNAVAILABLE,
};

static const CNA_AvatarEyebrow avatarEyebrow[] = {
    CNA_AVATAR_EYEBROW_NEUTRAL,
    CNA_AVATAR_EYEBROW_SAD,
    CNA_AVATAR_EYEBROW_ANGRY,
    CNA_AVATAR_EYEBROW_CONFUSED,
    CNA_AVATAR_EYEBROW_RAISED,
};

static const CNA_AvatarEye avatarEye[] = {
    CNA_AVATAR_EYE_NEUTRAL,
    CNA_AVATAR_EYE_SAD,
    CNA_AVATAR_EYE_ANGRY,
    CNA_AVATAR_EYE_CONFUSED,
    CNA_AVATAR_EYE_LAUGHING,
    CNA_AVATAR_EYE_SHOCKED,
    CNA_AVATAR_EYE_HAPPY,
    CNA_AVATAR_EYE_YAWNING,
    CNA_AVATAR_EYE_SLEEPING,
    CNA_AVATAR_EYE_LOOK_UP,
    CNA_AVATAR_EYE_LOOK_DOWN,
    CNA_AVATAR_EYE_LOOK_LEFT,
    CNA_AVATAR_EYE_LOOK_RIGHT,
    CNA_AVATAR_EYE_BLINK,
};

static const CNA_AvatarMouth avatarMouth[] = {
    CNA_AVATAR_MOUTH_NEUTRAL,
    CNA_AVATAR_MOUTH_SAD,
    CNA_AVATAR_MOUTH_ANGRY,
    CNA_AVATAR_MOUTH_CONFUSED,
    CNA_AVATAR_MOUTH_LAUGHING,
    CNA_AVATAR_MOUTH_SHOCKED,
    CNA_AVATAR_MOUTH_HAPPY,
    CNA_AVATAR_MOUTH_PHONETIC_O,
    CNA_AVATAR_MOUTH_PHONETIC_AI,
    CNA_AVATAR_MOUTH_PHONETIC_EE,
    CNA_AVATAR_MOUTH_PHONETIC_FV,
    CNA_AVATAR_MOUTH_PHONETIC_W,
    CNA_AVATAR_MOUTH_PHONETIC_L,
    CNA_AVATAR_MOUTH_PHONETIC_DTH,
};

static const CNA_AvatarAnimationPreset avatarAnimationPreset[] = {
    CNA_AVATAR_ANIMATION_PRESET_STAND_0,
    CNA_AVATAR_ANIMATION_PRESET_STAND_1,
    CNA_AVATAR_ANIMATION_PRESET_STAND_2,
    CNA_AVATAR_ANIMATION_PRESET_STAND_3,
    CNA_AVATAR_ANIMATION_PRESET_STAND_4,
    CNA_AVATAR_ANIMATION_PRESET_STAND_5,
    CNA_AVATAR_ANIMATION_PRESET_STAND_6,
    CNA_AVATAR_ANIMATION_PRESET_STAND_7,
    CNA_AVATAR_ANIMATION_PRESET_CLAP,
    CNA_AVATAR_ANIMATION_PRESET_WAVE,
    CNA_AVATAR_ANIMATION_PRESET_CELEBRATE,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_CHECK_NAILS,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_LOOK_AROUND,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_SHIFT_WEIGHT,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_FIX_SHOE,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_ANGRY,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_CONFUSED,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_LAUGH,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_CRY,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_SHOCKED,
    CNA_AVATAR_ANIMATION_PRESET_FEMALE_YAWN,
    CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_LOOK_AROUND,
    CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_STRETCH,
    CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_SHIFT_WEIGHT,
    CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_CHECK_HAND,
    CNA_AVATAR_ANIMATION_PRESET_MALE_ANGRY,
    CNA_AVATAR_ANIMATION_PRESET_MALE_CONFUSED,
    CNA_AVATAR_ANIMATION_PRESET_MALE_LAUGH,
    CNA_AVATAR_ANIMATION_PRESET_MALE_CRY,
    CNA_AVATAR_ANIMATION_PRESET_MALE_SURPRISED,
    CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN,
};

/* The skeleton is the exception: fifty-five bones spread sparsely over ordinals 0 to 70, so
   each one is pinned against the ordinal the canonical skeleton actually gives it. */
static const uint32_t avatarBone[][2] = {
    {CNA_AVATAR_BONE_ROOT, UINT32_C(0)},
    {CNA_AVATAR_BONE_BACK_LOWER, UINT32_C(1)},
    {CNA_AVATAR_BONE_HIP_LEFT, UINT32_C(2)},
    {CNA_AVATAR_BONE_HIP_RIGHT, UINT32_C(3)},
    {CNA_AVATAR_BONE_BACK_UPPER, UINT32_C(5)},
    {CNA_AVATAR_BONE_KNEE_LEFT, UINT32_C(6)},
    {CNA_AVATAR_BONE_KNEE_RIGHT, UINT32_C(8)},
    {CNA_AVATAR_BONE_ANKLE_LEFT, UINT32_C(11)},
    {CNA_AVATAR_BONE_COLLAR_LEFT, UINT32_C(12)},
    {CNA_AVATAR_BONE_NECK, UINT32_C(14)},
    {CNA_AVATAR_BONE_ANKLE_RIGHT, UINT32_C(15)},
    {CNA_AVATAR_BONE_COLLAR_RIGHT, UINT32_C(16)},
    {CNA_AVATAR_BONE_HEAD, UINT32_C(19)},
    {CNA_AVATAR_BONE_SHOULDER_LEFT, UINT32_C(20)},
    {CNA_AVATAR_BONE_TOE_LEFT, UINT32_C(21)},
    {CNA_AVATAR_BONE_SHOULDER_RIGHT, UINT32_C(22)},
    {CNA_AVATAR_BONE_TOE_RIGHT, UINT32_C(23)},
    {CNA_AVATAR_BONE_ELBOW_LEFT, UINT32_C(25)},
    {CNA_AVATAR_BONE_ELBOW_RIGHT, UINT32_C(28)},
    {CNA_AVATAR_BONE_WRIST_LEFT, UINT32_C(33)},
    {CNA_AVATAR_BONE_WRIST_RIGHT, UINT32_C(36)},
    {CNA_AVATAR_BONE_FINGER_INDEX_LEFT, UINT32_C(37)},
    {CNA_AVATAR_BONE_FINGER_MIDDLE_LEFT, UINT32_C(38)},
    {CNA_AVATAR_BONE_FINGER_RING_LEFT, UINT32_C(39)},
    {CNA_AVATAR_BONE_FINGER_SMALL_LEFT, UINT32_C(40)},
    {CNA_AVATAR_BONE_PROP_LEFT, UINT32_C(41)},
    {CNA_AVATAR_BONE_SPECIAL_LEFT, UINT32_C(42)},
    {CNA_AVATAR_BONE_FINGER_THUMB_LEFT, UINT32_C(43)},
    {CNA_AVATAR_BONE_FINGER_INDEX_RIGHT, UINT32_C(44)},
    {CNA_AVATAR_BONE_FINGER_MIDDLE_RIGHT, UINT32_C(45)},
    {CNA_AVATAR_BONE_FINGER_RING_RIGHT, UINT32_C(46)},
    {CNA_AVATAR_BONE_FINGER_SMALL_RIGHT, UINT32_C(47)},
    {CNA_AVATAR_BONE_PROP_RIGHT, UINT32_C(48)},
    {CNA_AVATAR_BONE_SPECIAL_RIGHT, UINT32_C(49)},
    {CNA_AVATAR_BONE_FINGER_THUMB_RIGHT, UINT32_C(50)},
    {CNA_AVATAR_BONE_FINGER_INDEX_2_LEFT, UINT32_C(51)},
    {CNA_AVATAR_BONE_FINGER_MIDDLE_2_LEFT, UINT32_C(52)},
    {CNA_AVATAR_BONE_FINGER_RING_2_LEFT, UINT32_C(53)},
    {CNA_AVATAR_BONE_FINGER_SMALL_2_LEFT, UINT32_C(54)},
    {CNA_AVATAR_BONE_FINGER_THUMB_2_LEFT, UINT32_C(55)},
    {CNA_AVATAR_BONE_FINGER_INDEX_2_RIGHT, UINT32_C(56)},
    {CNA_AVATAR_BONE_FINGER_MIDDLE_2_RIGHT, UINT32_C(57)},
    {CNA_AVATAR_BONE_FINGER_RING_2_RIGHT, UINT32_C(58)},
    {CNA_AVATAR_BONE_FINGER_SMALL_2_RIGHT, UINT32_C(59)},
    {CNA_AVATAR_BONE_FINGER_THUMB_2_RIGHT, UINT32_C(60)},
    {CNA_AVATAR_BONE_FINGER_INDEX_3_LEFT, UINT32_C(61)},
    {CNA_AVATAR_BONE_FINGER_MIDDLE_3_LEFT, UINT32_C(62)},
    {CNA_AVATAR_BONE_FINGER_RING_3_LEFT, UINT32_C(63)},
    {CNA_AVATAR_BONE_FINGER_SMALL_3_LEFT, UINT32_C(64)},
    {CNA_AVATAR_BONE_FINGER_THUMB_3_LEFT, UINT32_C(65)},
    {CNA_AVATAR_BONE_FINGER_INDEX_3_RIGHT, UINT32_C(66)},
    {CNA_AVATAR_BONE_FINGER_MIDDLE_3_RIGHT, UINT32_C(67)},
    {CNA_AVATAR_BONE_FINGER_RING_3_RIGHT, UINT32_C(68)},
    {CNA_AVATAR_BONE_FINGER_SMALL_3_RIGHT, UINT32_C(69)},
    {CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT, UINT32_C(70)},
};

static int ordinals_are_canonical(const uint32_t* const values, const uint64_t count,
                                  const uint32_t maximum)
{
    uint64_t index;
    for (index = UINT64_C(0); index < count; ++index) {
        if (values[index] != (uint32_t)index) {
            return 0;
        }
    }
    return count != UINT64_C(0) && maximum == (uint32_t)(count - UINT64_C(1));
}

static int bones_are_canonical(void)
{
    uint64_t index;
    for (index = UINT64_C(0); index < COUNT_OF(avatarBone); ++index) {
        if (avatarBone[index][0] != avatarBone[index][1]) {
            return 0;
        }
        /* Sparse, but still strictly ascending: a duplicate or a reordering is a break. */
        if (index != UINT64_C(0) && avatarBone[index][1] <= avatarBone[index - 1][1]) {
            return 0;
        }
    }
    return COUNT_OF(avatarBone) == UINT64_C(55) &&
        CNA_AVATAR_BONE_MAXIMUM == UINT32_C(70) &&
        CNA_AVATAR_BONE_MAXIMUM == CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT;
}

/* Both name routes are pure value operations: no gamer, no handle, no game. */
static int names_are_canonical(void)
{
    uint64_t size = UINT64_C(0);
    char text[64];

    /* An animation clip name is the identity's own canonical spelling. */
    if (cna_avatar_animation_preset_get_clip_name_size_ext(
            CNA_AVATAR_ANIMATION_PRESET_WAVE, &size) != CNA_RESULT_SUCCESS ||
        size != UINT64_C(4) ||
        cna_avatar_animation_preset_copy_clip_name_ext(
            CNA_AVATAR_ANIMATION_PRESET_WAVE, text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS ||
        memcmp(text, "Wave", (size_t)size) != 0) {
        return 0;
    }
    /* A body-type name is a content path instead, so it is not the identity spelling. */
    if (cna_avatar_body_type_get_content_name_size_ext(CNA_AVATAR_BODY_TYPE_MALE, &size) !=
            CNA_RESULT_SUCCESS ||
        size == UINT64_C(0) || size > sizeof(text) ||
        cna_avatar_body_type_copy_content_name_ext(
            CNA_AVATAR_BODY_TYPE_MALE, text, sizeof(text), &size) != CNA_RESULT_SUCCESS ||
        memcmp(text, "avatar/male/", (size_t)12) != 0) {
        return 0;
    }
    /* A buffer that cannot hold the whole name is refused with nothing written. */
    if (cna_avatar_animation_preset_copy_clip_name_ext(
            CNA_AVATAR_ANIMATION_PRESET_WAVE, text, UINT64_C(1), &size) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        size != UINT64_C(4)) {
        return 0;
    }
    /* An undefined identity is refused rather than mapped to something plausible. */
    return cna_avatar_animation_preset_get_clip_name_size_ext(UINT32_C(9999), &size) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_animation_preset_copy_clip_name_ext(
            UINT32_C(9999), text, sizeof(text), &size) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_body_type_get_content_name_size_ext(UINT32_C(9999), &size) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_body_type_copy_content_name_ext(
            UINT32_C(9999), text, sizeof(text), &size) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_animation_preset_get_clip_name_size_ext(
            CNA_AVATAR_ANIMATION_PRESET_WAVE, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_avatar_body_type_get_content_name_size_ext(
            CNA_AVATAR_BODY_TYPE_MALE, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

int main(void)
{
    if (!ordinals_are_canonical(avatarBodyType, COUNT_OF(avatarBodyType),
                                CNA_AVATAR_BODY_TYPE_MAXIMUM)) {
        return CNA_TEST_FAIL(1);
    }
    if (!ordinals_are_canonical(avatarRendererState, COUNT_OF(avatarRendererState),
                                CNA_AVATAR_RENDERER_STATE_MAXIMUM)) {
        return CNA_TEST_FAIL(2);
    }
    if (!ordinals_are_canonical(avatarEyebrow, COUNT_OF(avatarEyebrow),
                                CNA_AVATAR_EYEBROW_MAXIMUM)) {
        return CNA_TEST_FAIL(3);
    }
    if (!ordinals_are_canonical(avatarEye, COUNT_OF(avatarEye),
                                CNA_AVATAR_EYE_MAXIMUM)) {
        return CNA_TEST_FAIL(4);
    }
    if (!ordinals_are_canonical(avatarMouth, COUNT_OF(avatarMouth),
                                CNA_AVATAR_MOUTH_MAXIMUM)) {
        return CNA_TEST_FAIL(5);
    }
    if (!ordinals_are_canonical(avatarAnimationPreset, COUNT_OF(avatarAnimationPreset),
                                CNA_AVATAR_ANIMATION_PRESET_MAXIMUM)) {
        return CNA_TEST_FAIL(6);
    }
    if (!bones_are_canonical()) {
        return CNA_TEST_FAIL(7);
    }
    if (!names_are_canonical()) {
        return CNA_TEST_FAIL(8);
    }
    return sizeof(CNA_AvatarBone) == sizeof(uint32_t) &&
            sizeof(CNA_AvatarAnimationPreset) == sizeof(uint32_t)
        ? 0
        : 9;
}
