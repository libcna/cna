// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>
#include <threads.h>

typedef struct DevicesSmokeState {
    int validated;
} DevicesSmokeState;

typedef struct HotplugState {
    uint32_t mouse_connected_id;
    uint32_t mouse_disconnected_id;
    uint32_t keyboard_connected_id;
    uint32_t keyboard_disconnected_id;
    int mouse_connected_calls;
    int mouse_disconnected_calls;
    int keyboard_connected_calls;
    int keyboard_disconnected_calls;
} HotplugState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_Result sensor_result;
    CNA_Result clipboard_result;
    CNA_Result power_result;
} WrongThreadState;

/* Every enumeration in this header answers through the same four routes, so one driver proves the
   protocol for mice, keyboards, touch devices and sensors alike. */
typedef CNA_Result (*CountRoute)(CNA_Handle, uint32_t*);
typedef CNA_Result (*InfoRoute)(CNA_Handle, uint32_t, CNA_InputDeviceInfo*);
typedef CNA_Result (*NameSizeRoute)(CNA_Handle, uint32_t, uint64_t*);
typedef CNA_Result (*CopyNameRoute)(CNA_Handle, uint32_t, char*, uint64_t, uint64_t*);

static const CNA_StringView empty_text = {0, UINT64_C(0)};

static CNA_StringView text_of(const char* const value)
{
    CNA_StringView view;
    view.data = value;
    view.byte_length = (uint64_t)strlen(value);
    return view;
}

static int validate_pure_sensor_info(void)
{
    CNA_SensorInfo info;
    CNA_SensorInfo other;
    CNA_Bool equal = UINT8_C(9);
    CNA_SensorType type = CNA_SENSOR_TYPE_UNKNOWN;

    memset(&info, 9, sizeof(info));
    if (cna_sensor_info_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sensor_info_init(&info) != CNA_RESULT_SUCCESS ||
        info.struct_size != sizeof(CNA_SensorInfo) || info.struct_version != UINT32_C(1) ||
        info.id != UINT32_C(0) || info.type != CNA_SENSOR_TYPE_UNKNOWN) {
        return 0;
    }

    other = info;
    if (cna_sensor_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    /* All three canonical fields separate: identifier, name and kind. */
    other.id = UINT32_C(4);
    if (cna_sensor_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = info;
    other.type = CNA_SENSOR_TYPE_GYROSCOPE;
    if (cna_sensor_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = info;
    if (cna_sensor_info_equals(&info, empty_text, &other, text_of("gyro"), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_sensor_info_equals(&info, text_of("gyro"), &other, text_of("gyro"), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }

    for (type = CNA_SENSOR_TYPE_UNKNOWN; type <= CNA_SENSOR_TYPE_MAXIMUM; ++type) {
        other = info;
        other.type = type;
        if (cna_sensor_info_equals(&other, empty_text, &other, empty_text, &equal) !=
                CNA_RESULT_SUCCESS ||
            equal != CNA_TRUE) {
            return 0;
        }
    }
    other = info;
    other.type = CNA_SENSOR_TYPE_MAXIMUM + UINT32_C(1);
    if (cna_sensor_info_equals(&other, empty_text, &info, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = info;
    other.struct_version = UINT32_C(2);
    if (cna_sensor_info_equals(&other, empty_text, &info, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_sensor_info_equals(0, empty_text, &info, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_sensor_info_equals(&info, empty_text, 0, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_sensor_info_equals(&info, empty_text, &info, empty_text, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_pure_device_info(void)
{
    CNA_InputDeviceInfo info;
    CNA_InputDeviceInfo other;
    CNA_Bool equal = UINT8_C(9);

    memset(&info, 9, sizeof(info));
    if (cna_input_device_info_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_input_device_info_init(&info) != CNA_RESULT_SUCCESS ||
        info.struct_size != sizeof(CNA_InputDeviceInfo) || info.struct_version != UINT32_C(1) ||
        info.id != UINT64_C(0)) {
        return 0;
    }

    other = info;
    if (cna_input_device_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    /* The identifier is 64-bit here, so a value above the 32-bit range must survive the round
       trip and still separate. */
    other.id = UINT64_C(0x1FFFFFFFF);
    if (cna_input_device_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_input_device_info_equals(&other, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    other = info;
    if (cna_input_device_info_equals(&info, empty_text, &other, text_of("mouse"), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_input_device_info_equals(&info, text_of("mouse"), &other, text_of("mouse"), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    other = info;
    other.struct_size = UINT32_C(4);
    if (cna_input_device_info_equals(&other, empty_text, &info, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    {
        static const char malformed[2] = {(char)0xE0, (char)0x80};
        CNA_StringView bad;
        bad.data = malformed;
        bad.byte_length = UINT64_C(2);
        if (cna_input_device_info_equals(&info, bad, &info, empty_text, &equal) !=
            CNA_RESULT_ENCODING) {
            return 0;
        }
    }
    return cna_input_device_info_equals(0, empty_text, &info, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_input_device_info_equals(&info, empty_text, 0, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_input_device_info_equals(&info, empty_text, &info, empty_text, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_device_enumeration(
    const CNA_Handle game,
    const CountRoute count_route,
    const InfoRoute info_route,
    const NameSizeRoute name_size_route,
    const CopyNameRoute copy_name_route)
{
    CNA_InputDeviceInfo info;
    uint32_t count = UINT32_C(9);
    uint64_t bytes = UINT64_C(9);
    char name[256];

    if (count_route(game, &count) != CNA_RESULT_SUCCESS ||
        count_route(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An index at or past the count is refused, which pins the empty case too. */
    if (info_route(game, count, &info) != CNA_RESULT_INVALID_ARGUMENT ||
        name_size_route(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        copy_name_route(game, count, name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        info_route(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        name_size_route(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        copy_name_route(game, UINT32_C(0), name, (uint64_t)sizeof(name), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Whatever this machine reports is enumerated fully; zero devices is an ordinary answer. */
    for (uint32_t index = UINT32_C(0); index < count; ++index) {
        memset(name, 0, sizeof(name));
        if (info_route(game, index, &info) != CNA_RESULT_SUCCESS ||
            info.struct_size != sizeof(CNA_InputDeviceInfo) ||
            info.struct_version != UINT32_C(1) ||
            name_size_route(game, index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(name) ||
            copy_name_route(game, index, name, (uint64_t)sizeof(name), &bytes) !=
                CNA_RESULT_SUCCESS ||
            (uint64_t)strlen(name) != bytes) {
            return 0;
        }
        /* A short capacity reports the requirement and writes nothing. */
        if (bytes > UINT64_C(0)) {
            char guard[8];
            uint64_t needed = UINT64_C(0);
            memset(guard, 0x7F, sizeof(guard));
            if (copy_name_route(game, index, guard, bytes - UINT64_C(1), &needed) !=
                    CNA_RESULT_BUFFER_TOO_SMALL ||
                needed != bytes || guard[0] != 0x7F) {
                return 0;
            }
        }
    }
    return 1;
}

static int validate_sensor_family(const CNA_Handle game)
{
    CNA_SensorInfo info;
    CNA_Vector3 acceleration;
    CNA_Vector3 angular_velocity;
    CNA_Bool available = UINT8_C(9);
    uint32_t count = UINT32_C(9);
    uint64_t bytes = UINT64_C(9);
    char name[256];

    if (cna_sensors_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        cna_sensors_get_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_sensors_get_info_at(game, count, &info) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sensors_get_name_size_at(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sensors_copy_name_at(game, count, name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_sensors_get_info_at(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sensors_get_name_size_at(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sensors_copy_name_at(game, UINT32_C(0), name, (uint64_t)sizeof(name), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    for (uint32_t index = UINT32_C(0); index < count; ++index) {
        memset(name, 0, sizeof(name));
        if (cna_sensors_get_info_at(game, index, &info) != CNA_RESULT_SUCCESS ||
            info.type > CNA_SENSOR_TYPE_MAXIMUM ||
            cna_sensors_get_name_size_at(game, index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(name) ||
            cna_sensors_copy_name_at(game, index, name, (uint64_t)sizeof(name), &bytes) !=
                CNA_RESULT_SUCCESS ||
            (uint64_t)strlen(name) != bytes) {
            return 0;
        }
    }

    /* Availability is separate from the answer, and the reading is left untouched when nothing
       answered — exactly as the canonical query leaves its reference. */
    acceleration.x = 1.5F;
    acceleration.y = 2.5F;
    acceleration.z = 3.5F;
    if (cna_sensors_get_accelerometer(game, &acceleration, &available) != CNA_RESULT_SUCCESS ||
        (available != CNA_FALSE && available != CNA_TRUE)) {
        return 0;
    }
    if (available == CNA_FALSE &&
        (acceleration.x != 1.5F || acceleration.y != 2.5F || acceleration.z != 3.5F)) {
        return 0;
    }
    angular_velocity.x = -1.0F;
    angular_velocity.y = -2.0F;
    angular_velocity.z = -3.0F;
    if (cna_sensors_get_gyroscope(game, &angular_velocity, &available) != CNA_RESULT_SUCCESS ||
        (available != CNA_FALSE && available != CNA_TRUE)) {
        return 0;
    }
    if (available == CNA_FALSE &&
        (angular_velocity.x != -1.0F || angular_velocity.y != -2.0F ||
         angular_velocity.z != -3.0F)) {
        return 0;
    }
    return cna_sensors_get_accelerometer(game, 0, &available) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_sensors_get_accelerometer(game, &acceleration, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_sensors_get_gyroscope(game, 0, &available) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_sensors_get_gyroscope(game, &angular_velocity, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static void on_mouse_connected(const uint32_t id, void* const context)
{
    HotplugState* const state = (HotplugState*)context;
    state->mouse_connected_id = id;
    ++state->mouse_connected_calls;
}

static void on_mouse_disconnected(const uint32_t id, void* const context)
{
    HotplugState* const state = (HotplugState*)context;
    state->mouse_disconnected_id = id;
    ++state->mouse_disconnected_calls;
}

static void on_keyboard_connected(const uint32_t id, void* const context)
{
    HotplugState* const state = (HotplugState*)context;
    state->keyboard_connected_id = id;
    ++state->keyboard_connected_calls;
}

static void on_keyboard_disconnected(const uint32_t id, void* const context)
{
    HotplugState* const state = (HotplugState*)context;
    state->keyboard_disconnected_id = id;
    ++state->keyboard_disconnected_calls;
}

static int validate_hotplug_events(const CNA_Handle game)
{
    HotplugState hotplug;
    CNA_InputDeviceEventRegistrationHandle mouse_connected = CNA_INVALID_HANDLE;
    CNA_InputDeviceEventRegistrationHandle mouse_disconnected = CNA_INVALID_HANDLE;
    CNA_InputDeviceEventRegistrationHandle keyboard_connected = CNA_INVALID_HANDLE;
    CNA_InputDeviceEventRegistrationHandle keyboard_disconnected = CNA_INVALID_HANDLE;
    CNA_InputDeviceEventRegistrationHandle rejected = CNA_INVALID_HANDLE;

    memset(&hotplug, 0, sizeof(hotplug));
    if (cna_input_devices_subscribe_mouse_connected_ext(0, &hotplug, &mouse_connected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_input_devices_subscribe_mouse_connected_ext(on_mouse_connected, &hotplug, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_input_devices_subscribe_mouse_connected_ext(
            on_mouse_connected, &hotplug, &mouse_connected) != CNA_RESULT_SUCCESS ||
        cna_input_devices_subscribe_mouse_disconnected_ext(
            on_mouse_disconnected, &hotplug, &mouse_disconnected) != CNA_RESULT_SUCCESS ||
        cna_input_devices_subscribe_keyboard_connected_ext(
            on_keyboard_connected, &hotplug, &keyboard_connected) != CNA_RESULT_SUCCESS ||
        cna_input_devices_subscribe_keyboard_disconnected_ext(
            on_keyboard_disconnected, &hotplug, &keyboard_disconnected) != CNA_RESULT_SUCCESS ||
        mouse_connected == CNA_INVALID_HANDLE || mouse_disconnected == CNA_INVALID_HANDLE ||
        keyboard_connected == CNA_INVALID_HANDLE ||
        keyboard_disconnected == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Each of the four events reaches exactly its own handler, with its own identifier. */
    if (cna_input_devices_raise_mouse_connected_ext(game, UINT32_C(21)) != CNA_RESULT_SUCCESS ||
        cna_input_devices_raise_mouse_disconnected_ext(game, UINT32_C(22)) !=
            CNA_RESULT_SUCCESS ||
        cna_input_devices_raise_keyboard_connected_ext(game, UINT32_C(23)) !=
            CNA_RESULT_SUCCESS ||
        cna_input_devices_raise_keyboard_disconnected_ext(game, UINT32_C(24)) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (hotplug.mouse_connected_calls != 1 || hotplug.mouse_connected_id != UINT32_C(21) ||
        hotplug.mouse_disconnected_calls != 1 ||
        hotplug.mouse_disconnected_id != UINT32_C(22) ||
        hotplug.keyboard_connected_calls != 1 ||
        hotplug.keyboard_connected_id != UINT32_C(23) ||
        hotplug.keyboard_disconnected_calls != 1 ||
        hotplug.keyboard_disconnected_id != UINT32_C(24)) {
        return 0;
    }

    /* One shared release route detaches exactly the subscription it was handed. */
    if (cna_input_devices_unsubscribe_ext(mouse_connected) != CNA_RESULT_SUCCESS ||
        cna_input_devices_unsubscribe_ext(mouse_connected) != CNA_RESULT_INVALID_HANDLE ||
        cna_input_devices_raise_mouse_connected_ext(game, UINT32_C(25)) != CNA_RESULT_SUCCESS ||
        hotplug.mouse_connected_calls != 1 ||
        cna_input_devices_raise_keyboard_connected_ext(game, UINT32_C(26)) !=
            CNA_RESULT_SUCCESS ||
        hotplug.keyboard_connected_calls != 2 ||
        hotplug.keyboard_connected_id != UINT32_C(26)) {
        return 0;
    }

    /* The canonical reset clears all four events at once, and a registration released afterwards
       removes nothing rather than failing. */
    if (cna_input_devices_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_input_devices_raise_mouse_disconnected_ext(game, UINT32_C(27)) !=
            CNA_RESULT_SUCCESS ||
        cna_input_devices_raise_keyboard_connected_ext(game, UINT32_C(28)) !=
            CNA_RESULT_SUCCESS ||
        cna_input_devices_raise_keyboard_disconnected_ext(game, UINT32_C(29)) !=
            CNA_RESULT_SUCCESS ||
        hotplug.mouse_disconnected_calls != 1 || hotplug.keyboard_connected_calls != 2 ||
        hotplug.keyboard_disconnected_calls != 1) {
        return 0;
    }
    if (cna_input_devices_unsubscribe_ext(mouse_disconnected) != CNA_RESULT_SUCCESS ||
        cna_input_devices_unsubscribe_ext(keyboard_connected) != CNA_RESULT_SUCCESS ||
        cna_input_devices_unsubscribe_ext(keyboard_disconnected) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_input_devices_unsubscribe_ext(rejected) == CNA_RESULT_INVALID_HANDLE;
}

/* The clipboard is process-external state this test does not own, so it captures what was there,
   probes, and puts it back. Whether a platform honors the write is not something a test can
   assume, so the assertion is a relationship: if the write took effect, the read must return
   exactly what was written. */
static int validate_clipboard(const CNA_Handle game)
{
    static const char probe[] = "cna clipboard probe \xC3\xA1\xC4\x8D";
    char original[4096];
    char text[4096];
    uint64_t original_bytes = UINT64_C(0);
    uint64_t bytes = UINT64_C(9);
    CNA_Bool has_text = UINT8_C(9);
    int writable = 0;

    if (cna_clipboard_get_text_size(game, &original_bytes) != CNA_RESULT_SUCCESS ||
        original_bytes >= (uint64_t)sizeof(original)) {
        return 0;
    }
    memset(original, 0, sizeof(original));
    if (cna_clipboard_copy_text(game, original, (uint64_t)sizeof(original), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != original_bytes) {
        return 0;
    }

    if (cna_clipboard_set_text(game, text_of(probe)) != CNA_RESULT_SUCCESS ||
        cna_clipboard_get_has_text(game, &has_text) != CNA_RESULT_SUCCESS ||
        (has_text != CNA_FALSE && has_text != CNA_TRUE) ||
        cna_clipboard_get_text_size(game, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text)) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_clipboard_copy_text(game, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(text) != bytes) {
        return 0;
    }
    writable = (bytes == (uint64_t)strlen(probe)) && (strcmp(text, probe) == 0);

    /* Non-empty text and the presence flag must agree in either direction. */
    if ((bytes > UINT64_C(0)) != (has_text == CNA_TRUE)) {
        return 0;
    }

    if (writable) {
        /* Only a platform that actually stores text can prove the empty case. */
        if (cna_clipboard_set_text(game, empty_text) != CNA_RESULT_SUCCESS ||
            cna_clipboard_get_has_text(game, &has_text) != CNA_RESULT_SUCCESS ||
            has_text != CNA_FALSE ||
            cna_clipboard_get_text_size(game, &bytes) != CNA_RESULT_SUCCESS ||
            bytes != UINT64_C(0)) {
            return 0;
        }
        /* A short capacity reports the requirement and writes nothing. */
        if (cna_clipboard_set_text(game, text_of(probe)) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        {
            char guard[8];
            uint64_t needed = UINT64_C(0);
            memset(guard, 0x7F, sizeof(guard));
            if (cna_clipboard_copy_text(game, guard, UINT64_C(1), &needed) !=
                    CNA_RESULT_BUFFER_TOO_SMALL ||
                needed != (uint64_t)strlen(probe) || guard[0] != 0x7F) {
                return 0;
            }
        }
    }

    {
        static const char malformed[3] = {(char)0xF0, (char)0x28, (char)0x8C};
        CNA_StringView bad;
        bad.data = malformed;
        bad.byte_length = UINT64_C(3);
        if (cna_clipboard_set_text(game, bad) != CNA_RESULT_ENCODING) {
            return 0;
        }
    }
    if (cna_clipboard_get_text_size(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_clipboard_get_has_text(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_clipboard_copy_text(game, 0, UINT64_C(4), &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_clipboard_copy_text(game, text, (uint64_t)sizeof(text), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Put back whatever was there before this test ran. */
    {
        CNA_StringView restore;
        restore.data = original_bytes == UINT64_C(0) ? 0 : original;
        restore.byte_length = original_bytes;
        if (cna_clipboard_set_text(game, restore) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    return 1;
}

static int validate_power(const CNA_Handle game)
{
    CNA_PowerState state = UINT32_C(99);
    int32_t seconds_left = 12345;
    int32_t percent = 12345;

    if (cna_power_get_info(game, &state, &seconds_left, &percent) != CNA_RESULT_SUCCESS ||
        state > CNA_POWER_STATE_CHARGED) {
        return 0;
    }
    /* Both numbers are always written, and -1 means unknown rather than empty. */
    if (seconds_left < -1 || percent < -1 || percent > 100) {
        return 0;
    }
    return cna_power_get_info(game, 0, &seconds_left, &percent) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_power_get_info(game, &state, 0, &percent) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_power_get_info(game, &state, &seconds_left, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_device_family(const CNA_Handle game)
{
    if (!validate_device_enumeration(
            game,
            cna_input_devices_get_mouse_count,
            cna_input_devices_get_mouse_info_at,
            cna_input_devices_get_mouse_name_size_at,
            cna_input_devices_copy_mouse_name_at)) {
        return 0;
    }
    if (!validate_device_enumeration(
            game,
            cna_input_devices_get_keyboard_count,
            cna_input_devices_get_keyboard_info_at,
            cna_input_devices_get_keyboard_name_size_at,
            cna_input_devices_copy_keyboard_name_at)) {
        return 0;
    }
    if (!validate_device_enumeration(
            game,
            cna_input_devices_get_touch_device_count,
            cna_input_devices_get_touch_device_info_at,
            cna_input_devices_get_touch_device_name_size_at,
            cna_input_devices_copy_touch_device_name_at)) {
        return 0;
    }
    if (!validate_sensor_family(game)) {
        return 0;
    }
    if (!validate_clipboard(game)) {
        return 0;
    }
    if (!validate_power(game)) {
        return 0;
    }
    return validate_hotplug_events(game);
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    DevicesSmokeState* const state = (DevicesSmokeState*)context;
    if (game_time == 0 || !validate_device_family(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_Vector3 reading = {0.0F, 0.0F, 0.0F};
    CNA_Bool available = CNA_FALSE;
    uint64_t bytes = UINT64_C(0);
    CNA_PowerState power = CNA_POWER_STATE_UNKNOWN;
    int32_t seconds_left = 0;
    int32_t percent = 0;
    state->sensor_result = cna_sensors_get_accelerometer(state->game, &reading, &available);
    state->clipboard_result = cna_clipboard_get_text_size(state->game, &bytes);
    state->power_result = cna_power_get_info(state->game, &power, &seconds_left, &percent);
    return 0;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_pure_sensor_info()) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_pure_device_info()) {
        return CNA_TEST_FAIL(2);
    }

    DevicesSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API input devices smoke", UINT64_C(25)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return CNA_TEST_FAIL(3);
    }

    /* Every host query is thread-affine, exactly like the device captures. */
    WrongThreadState wrong_thread = {
        game, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS
    };
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.sensor_result != CNA_RESULT_THREAD ||
        wrong_thread.clipboard_result != CNA_RESULT_THREAD ||
        wrong_thread.power_result != CNA_RESULT_THREAD) {
        return CNA_TEST_FAIL(4);
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(5);
    }
    return 0;
}
