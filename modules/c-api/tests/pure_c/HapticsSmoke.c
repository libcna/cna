// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>
#include <threads.h>

typedef struct HapticsState {
    int validated;
} HapticsState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_HapticDeviceHandle device;
    CNA_Result count_result;
    CNA_Result device_result;
} WrongThreadState;

/* Pure value operations need no runtime at all, so they run before a game exists. */
static int validate_pure_direction(void)
{
    CNA_HapticDirection direction;
    CNA_HapticDirection other;
    CNA_Bool equal = UINT8_C(9);

    memset(&direction, 9, sizeof(direction));
    if (cna_haptic_direction_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_direction_init(&direction) != CNA_RESULT_SUCCESS ||
        direction.type != CNA_HAPTIC_DIRECTION_TYPE_POLAR ||
        direction.values[0] != 0 || direction.values[1] != 0 || direction.values[2] != 0) {
        return 0;
    }

    other = direction;
    if (cna_haptic_direction_equals(&direction, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    /* Every component is compared whatever the type says is meaningful, exactly as the canonical
       comparison does: a polar direction still separates on its unused Z component. */
    other.values[2] = 1;
    if (cna_haptic_direction_equals(&direction, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = direction;
    other.type = CNA_HAPTIC_DIRECTION_TYPE_CARTESIAN;
    if (cna_haptic_direction_equals(&direction, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }

    other.type = CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM + UINT32_C(1);
    return cna_haptic_direction_equals(&direction, &other, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_haptic_direction_equals(0, &direction, &equal) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_haptic_direction_equals(&direction, 0, &equal) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_haptic_direction_equals(&direction, &direction, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int effect_is_default(const CNA_HapticEffect* const effect)
{
    return effect->struct_size == sizeof(CNA_HapticEffect) &&
        effect->struct_version == UINT32_C(1) &&
        effect->type == CNA_HAPTIC_EFFECT_TYPE_CONSTANT &&
        effect->reserved == UINT32_C(0) && effect->reserved2 == UINT8_C(0) &&
        effect->direction.type == CNA_HAPTIC_DIRECTION_TYPE_POLAR &&
        effect->length == UINT32_C(0) && effect->delay == UINT16_C(0) &&
        effect->button == UINT16_C(0) && effect->interval == UINT16_C(0) &&
        effect->level == 0 && effect->period == UINT16_C(0) && effect->magnitude == 0 &&
        effect->offset == 0 && effect->phase == UINT16_C(0) &&
        effect->ramp_start == 0 && effect->ramp_end == 0 &&
        effect->right_saturation[0] == UINT16_C(0) && effect->left_saturation[2] == UINT16_C(0) &&
        effect->right_coefficient[1] == 0 && effect->left_coefficient[0] == 0 &&
        effect->deadband[2] == UINT16_C(0) && effect->center[1] == 0 &&
        effect->large_magnitude == UINT16_C(0) && effect->small_magnitude == UINT16_C(0) &&
        effect->custom_channels == UINT8_C(0) && effect->custom_period == UINT16_C(0) &&
        effect->attack_length == UINT16_C(0) && effect->attack_level == UINT16_C(0) &&
        effect->fade_length == UINT16_C(0) && effect->fade_level == UINT16_C(0);
}

static int validate_pure_effect(void)
{
    static const uint16_t samples[4] = {1U, 2U, 3U, 4U};
    static const uint16_t other_samples[4] = {1U, 2U, 3U, 5U};
    CNA_HapticEffect effect;
    CNA_HapticEffect other;
    CNA_Bool equal = UINT8_C(9);
    CNA_HapticEffectType type = CNA_HAPTIC_EFFECT_TYPE_CONSTANT;

    memset(&effect, 9, sizeof(effect));
    if (cna_haptic_effect_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_effect_init(&effect) != CNA_RESULT_SUCCESS ||
        !effect_is_default(&effect)) {
        return 0;
    }

    other = effect;
    if (cna_haptic_effect_equals(&effect, 0, UINT64_C(0), &other, 0, UINT64_C(0), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }

    /* Every field is compared regardless of which family the type selects, so two effects that
       would play identically still separate on a field their family ignores. */
    other.large_magnitude = UINT16_C(7);
    if (cna_haptic_effect_equals(&effect, 0, UINT64_C(0), &other, 0, UINT64_C(0), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }

    /* The custom sample buffer travels beside the value and is part of the comparison. */
    other = effect;
    if (cna_haptic_effect_equals(
            &effect, samples, UINT64_C(4), &other, samples, UINT64_C(4), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE ||
        cna_haptic_effect_equals(
            &effect, samples, UINT64_C(4), &other, other_samples, UINT64_C(4), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_haptic_effect_equals(
            &effect, samples, UINT64_C(4), &other, samples, UINT64_C(3), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_haptic_effect_equals(&effect, samples, UINT64_C(4), &other, 0, UINT64_C(0), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }

    /* Every defined family identity is accepted; the first undefined one is refused. */
    for (type = CNA_HAPTIC_EFFECT_TYPE_CONSTANT;
         type <= CNA_HAPTIC_EFFECT_TYPE_MAXIMUM;
         ++type) {
        other = effect;
        other.type = type;
        if (cna_haptic_effect_equals(&other, 0, UINT64_C(0), &other, 0, UINT64_C(0), &equal) !=
                CNA_RESULT_SUCCESS ||
            equal != CNA_TRUE) {
            return 0;
        }
    }
    other = effect;
    other.type = CNA_HAPTIC_EFFECT_TYPE_MAXIMUM + UINT32_C(1);
    if (cna_haptic_effect_equals(&other, 0, UINT64_C(0), &other, 0, UINT64_C(0), &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = effect;
    other.direction.type = CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM + UINT32_C(1);
    if (cna_haptic_effect_equals(&other, 0, UINT64_C(0), &other, 0, UINT64_C(0), &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = effect;
    other.struct_version = UINT32_C(2);
    if (cna_haptic_effect_equals(&other, 0, UINT64_C(0), &effect, 0, UINT64_C(0), &equal) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_effect_equals(&effect, 0, UINT64_C(1), &effect, 0, UINT64_C(0), &equal) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_effect_equals(&effect, 0, UINT64_C(0), &effect, 0, UINT64_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_effect_equals(0, 0, UINT64_C(0), &effect, 0, UINT64_C(0), &equal) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_pure_capabilities(void)
{
    const CNA_StringView empty = {"", UINT64_C(0)};
    const CNA_StringView wheel = {"wheel", UINT64_C(5)};
    const CNA_StringView invalid = {"\xC0\xAF", UINT64_C(2)};
    CNA_HapticCapabilities capabilities;
    CNA_HapticCapabilities other;
    CNA_Bool equal = UINT8_C(9);

    memset(&capabilities, 9, sizeof(capabilities));
    if (cna_haptic_capabilities_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_capabilities_init(&capabilities) != CNA_RESULT_SUCCESS ||
        capabilities.struct_size != sizeof(CNA_HapticCapabilities) ||
        capabilities.struct_version != UINT32_C(1) ||
        capabilities.is_open != CNA_FALSE ||
        capabilities.rumble_supported != CNA_FALSE ||
        capabilities.features != CNA_HAPTIC_FEATURE_NONE ||
        capabilities.axis_count != 0 ||
        capabilities.reserved[0] != UINT8_C(0) || capabilities.reserved[1] != UINT8_C(0)) {
        return 0;
    }
    /* The two effect counts default to -1, not zero: a closed device reports "unknown", which is
       deliberately distinguishable from "none". */
    if (capabilities.max_effects != -1 || capabilities.max_effects_playing != -1) {
        return 0;
    }

    other = capabilities;
    if (cna_haptic_capabilities_equals(&capabilities, empty, &other, empty, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    /* The name is part of the canonical comparison even though the C value does not carry it. */
    if (cna_haptic_capabilities_equals(&capabilities, empty, &other, wheel, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other.features = CNA_HAPTIC_FEATURE_CONSTANT | CNA_HAPTIC_FEATURE_PAUSE;
    if (cna_haptic_capabilities_equals(&capabilities, empty, &other, empty, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }

    other = capabilities;
    other.struct_version = UINT32_C(2);
    return cna_haptic_capabilities_equals(&capabilities, empty, &other, empty, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_haptic_capabilities_equals(&capabilities, invalid, &other, empty, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_haptic_capabilities_equals(&capabilities, empty, &capabilities, empty, 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_haptic_capabilities_equals(0, empty, &capabilities, empty, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

/* The whole family is probed by behavior, never by renderer identity: no verification tree has
   force-feedback hardware, so a closed device is the expected answer -- but every route must still
   behave correctly on it, and the success path is exercised whenever a backend does supply one. */
static int validate_open_device(const CNA_HapticDeviceHandle device)
{
    static const uint16_t samples[2] = {11U, 22U};
    CNA_HapticCapabilities capabilities;
    CNA_HapticEffect effect;
    CNA_Bool open = UINT8_C(9);
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    int32_t effect_id = 0;
    char name[128];

    if (cna_haptic_effect_init(&effect) != CNA_RESULT_SUCCESS ||
        cna_haptic_capabilities_init(&capabilities) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_haptic_device_get_is_open(device, &open) != CNA_RESULT_SUCCESS ||
        (open != CNA_FALSE && open != CNA_TRUE) ||
        cna_haptic_device_get_is_open(device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The name uses the project count/copy protocol; a closed device reports zero bytes. */
    memset(name, 0, sizeof(name));
    if (cna_haptic_device_get_name_size(device, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(name) ||
        cna_haptic_device_copy_name(device, name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(name) != bytes ||
        cna_haptic_device_get_name_size(device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_copy_name(device, name, (uint64_t)sizeof(name), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (open == CNA_FALSE && bytes != UINT64_C(0)) {
        return 0;
    }
    if (bytes != UINT64_C(0) &&
        cna_haptic_device_copy_name(device, name, bytes - UINT64_C(1), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }

    /* Capabilities agree with the open state, and a closed device keeps the canonical unknowns. */
    if (cna_haptic_device_get_capabilities(device, &capabilities) != CNA_RESULT_SUCCESS ||
        capabilities.is_open != open ||
        (capabilities.features & ~CNA_HAPTIC_FEATURE_ALL) != UINT32_C(0) ||
        capabilities.reserved[0] != UINT8_C(0) || capabilities.reserved[1] != UINT8_C(0)) {
        return 0;
    }
    if (open == CNA_FALSE &&
        (capabilities.max_effects != -1 || capabilities.max_effects_playing != -1 ||
         capabilities.features != CNA_HAPTIC_FEATURE_NONE ||
         capabilities.rumble_supported != CNA_FALSE)) {
        return 0;
    }
    {
        CNA_HapticCapabilities invalid = capabilities;
        invalid.struct_version = UINT32_C(2);
        if (cna_haptic_device_get_capabilities(device, &invalid) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_haptic_device_get_capabilities(device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    /* Every query and action answers through its output; a closed device answers false rather
       than failing, which is what makes this family usable with no hardware present. */
    if (cna_haptic_device_get_is_effect_supported(
            device, &effect, 0, UINT64_C(0), &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_haptic_device_get_is_effect_supported(
            device, &effect, samples, UINT64_C(2), &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_get_is_effect_supported(
            device, 0, 0, UINT64_C(0), &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_get_is_effect_supported(
            device, &effect, 0, UINT64_C(1), &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_get_is_effect_supported(
            device, &effect, 0, UINT64_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (open == CNA_FALSE && flag != CNA_FALSE) {
        return 0;
    }

    if (cna_haptic_device_init_rumble(device, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_haptic_device_play_rumble(device, 0.5F, UINT32_C(10), &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_stop_rumble(device, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_init_rumble(device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_play_rumble(device, 0.5F, UINT32_C(10), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_stop_rumble(device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The strength is passed through unvalidated, exactly as the canonical operation passes it. */
    if (cna_haptic_device_play_rumble(device, -1.0F, UINT32_C(0), &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_play_rumble(device, 5.0F, UINT32_C(0), &flag) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A device that cannot store the effect reports -1 rather than failing. */
    effect_id = 12345;
    if (cna_haptic_device_create_effect(device, &effect, 0, UINT64_C(0), &effect_id) !=
            CNA_RESULT_SUCCESS ||
        cna_haptic_device_create_effect(device, &effect, 0, UINT64_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_create_effect(device, 0, 0, UINT64_C(0), &effect_id) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (open == CNA_FALSE && effect_id != -1) {
        return 0;
    }

    if (cna_haptic_device_update_effect(device, effect_id, &effect, 0, UINT64_C(0), &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_haptic_device_run_effect(device, effect_id, UINT32_C(1), &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_haptic_device_get_effect_status(device, effect_id, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_stop_effect(device, effect_id, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_stop_all_effects(device, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_destroy_effect(device, effect_id) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Freeing an unknown identifier is a successful no-op, because the canonical operation
       reports nothing at all. */
    if (cna_haptic_device_destroy_effect(device, -1) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_destroy_effect(device, 99999) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_haptic_device_update_effect(device, effect_id, &effect, 0, UINT64_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_update_effect(device, effect_id, 0, 0, UINT64_C(0), &flag) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_run_effect(device, effect_id, UINT32_C(1), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_get_effect_status(device, effect_id, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_stop_effect(device, effect_id, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_stop_all_effects(device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Gain and autocenter are passed through unvalidated too. */
    if (cna_haptic_device_set_gain(device, 50, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_set_gain(device, -5, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_set_gain(device, 1000, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_set_autocenter(device, 50, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_pause(device, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_resume(device, &flag) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_set_gain(device, 50, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_set_autocenter(device, 50, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_pause(device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptic_device_resume(device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_haptics_family(const CNA_Handle game)
{
    CNA_HapticDeviceHandle device = CNA_INVALID_HANDLE;
    CNA_HapticDeviceHandle from_mouse = CNA_INVALID_HANDLE;
    CNA_HapticDeviceHandle from_joystick = CNA_INVALID_HANDLE;
    CNA_HapticDeviceHandle rejected = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    uint32_t count = UINT32_C(9);
    uint32_t id = UINT32_C(0);
    uint64_t bytes = UINT64_C(0);
    char name[128];

    if (cna_haptics_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        cna_haptics_get_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An index at or past the count is refused, which pins the empty case too. */
    if (cna_haptics_get_id_at(game, count, &id) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptics_get_name_size_at(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptics_copy_name_at(game, count, name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptics_get_id_at(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Whatever this machine has is enumerated fully; zero devices is an ordinary answer. */
    for (uint32_t index = UINT32_C(0); index < count; ++index) {
        memset(name, 0, sizeof(name));
        if (cna_haptics_get_id_at(game, index, &id) != CNA_RESULT_SUCCESS ||
            cna_haptics_get_name_size_at(game, index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(name) ||
            cna_haptics_copy_name_at(game, index, name, (uint64_t)sizeof(name), &bytes) !=
                CNA_RESULT_SUCCESS ||
            (uint64_t)strlen(name) != bytes) {
            return 0;
        }
        if (cna_haptics_open(game, id, &device) != CNA_RESULT_SUCCESS ||
            device == CNA_INVALID_HANDLE ||
            !validate_open_device(device) ||
            cna_haptic_device_destroy(device) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* Opening a device that does not exist is not an error: it hands back a real handle that
       reports itself closed, exactly as the canonical factory returns a closed device. */
    if (cna_haptics_open(game, UINT32_C(0xFFFFFFFF), &device) != CNA_RESULT_SUCCESS ||
        device == CNA_INVALID_HANDLE ||
        cna_haptic_device_get_is_open(device, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        !validate_open_device(device)) {
        return 0;
    }
    /* Disposal is idempotent and leaves the handle usable and closed; release is not. */
    if (cna_haptic_device_dispose(device) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_dispose(device) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_get_is_open(device, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_haptic_device_destroy(device) != CNA_RESULT_SUCCESS ||
        cna_haptic_device_destroy(device) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    if (cna_haptics_open_from_mouse(game, &from_mouse) != CNA_RESULT_SUCCESS ||
        from_mouse == CNA_INVALID_HANDLE ||
        !validate_open_device(from_mouse) ||
        cna_haptic_device_destroy(from_mouse) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_haptics_open_from_joystick(game, UINT32_C(0), &from_joystick) !=
            CNA_RESULT_SUCCESS ||
        from_joystick == CNA_INVALID_HANDLE ||
        !validate_open_device(from_joystick) ||
        cna_haptic_device_destroy(from_joystick) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_haptics_open(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptics_open_from_mouse(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptics_open_from_joystick(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_haptics_get_is_mouse_haptic(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_haptics_get_is_joystick_haptic(game, UINT32_C(0), &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_haptics_get_is_mouse_haptic(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_haptics_get_is_joystick_haptic(game, UINT32_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* A handle that was never created is refused by every device route. */
    return cna_haptic_device_get_is_open(rejected, &flag) == CNA_RESULT_INVALID_HANDLE &&
        cna_haptic_device_dispose(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_haptic_device_destroy(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_haptic_device_get_name_size(rejected, &bytes) == CNA_RESULT_INVALID_HANDLE &&
        cna_haptic_device_destroy_effect(rejected, 0) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    HapticsState* const state = (HapticsState*)context;
    if (game_time == 0 || !validate_haptics_family(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int capture_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint32_t count = UINT32_C(0);
    CNA_Bool open = CNA_FALSE;
    state->count_result = cna_haptics_get_count(state->game, &count);
    state->device_result = cna_haptic_device_get_is_open(state->device, &open);
    return 0;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_pure_direction()) {
        return 1;
    }
    if (!validate_pure_effect()) {
        return 2;
    }
    if (!validate_pure_capabilities()) {
        return 3;
    }

    HapticsState haptics_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &haptics_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API haptics smoke", UINT64_C(19)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        haptics_state.validated != 1) {
        return 4;
    }

    /* Both the facade and a device handle are thread-affine. */
    CNA_HapticDeviceHandle device = CNA_INVALID_HANDLE;
    if (cna_haptics_open(game, UINT32_C(0), &device) != CNA_RESULT_SUCCESS) {
        return 5;
    }
    WrongThreadState wrong_thread = {game, device, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, capture_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.count_result != CNA_RESULT_THREAD ||
        wrong_thread.device_result != CNA_RESULT_THREAD) {
        return 6;
    }
    if (cna_haptic_device_destroy(device) != CNA_RESULT_SUCCESS) {
        return 7;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 8;
    }
    return 0;
}
