// SPDX-License-Identifier: MS-PL

#include "CNA/C/input_devices.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Input/Clipboard.hpp"
#include "CNA/Input/InputDeviceInfo.hpp"
#include "CNA/Input/InputDevices.hpp"
#include "CNA/Input/Power.hpp"
#include "CNA/Input/PowerState.hpp"
#include "CNA/Input/Sensors.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using CNA::Input::Clipboard;
using CNA::Input::InputDeviceInfoEXT;
using CNA::Input::InputDevices;
using CNA::Input::Power;
using CNA::Input::PowerStateEXT;
using CNA::Input::SensorInfoEXT;
using CNA::Input::Sensors;
using CNA::Input::SensorTypeEXT;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

template<typename T>
[[nodiscard]] CNA_Result ValidateVersionedStructure(
    const T* const structure,
    const char* const message) noexcept
{
    if (structure == nullptr || structure->struct_size < sizeof(T) ||
        structure->struct_version != StructureVersion) {
        return InvalidInput(message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToSensorType(const CNA_SensorType type, SensorTypeEXT* const outType)
{
    if (type > CNA_SENSOR_TYPE_MAXIMUM) {
        return InvalidInput("The sensor kind identity is undefined.");
    }
    *outType = static_cast<SensorTypeEXT>(type);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_SensorInfo MapSensorInfo(const SensorInfoEXT& info)
{
    CNA_SensorInfo mapped = {};
    mapped.struct_size = sizeof(CNA_SensorInfo);
    mapped.struct_version = StructureVersion;
    mapped.id = info.id;
    mapped.type = static_cast<CNA_SensorType>(info.type);
    return mapped;
}

[[nodiscard]] CNA_InputDeviceInfo MapDeviceInfo(const InputDeviceInfoEXT& info)
{
    CNA_InputDeviceInfo mapped = {};
    mapped.struct_size = sizeof(CNA_InputDeviceInfo);
    mapped.struct_version = StructureVersion;
    mapped.id = info.id;
    return mapped;
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The device text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the device text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyBorrowedText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (const CNA_Result result = CNA::C::Detail::CopyStringView(view, false, outText);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
    return CNA_RESULT_SUCCESS;
}

// Each enumeration is a point-in-time snapshot taken by the call, exactly like the haptic and
// joystick ones, so an index is only valid until the device set changes.
template<typename TInfo, typename TEnumerate>
[[nodiscard]] CNA_Result BorrowEnumerated(
    const CNA_Handle gameHandle,
    const uint32_t index,
    const TEnumerate enumerate,
    const char* const message,
    TInfo* const outInfo)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<TInfo> devices = enumerate();
    if (index >= devices.size()) {
        return InvalidInput(message);
    }
    *outInfo = devices[index];
    return CNA_RESULT_SUCCESS;
}

template<typename TEnumerate>
[[nodiscard]] CNA_Result CountEnumerated(
    const CNA_Handle gameHandle,
    uint32_t* const outCount,
    const TEnumerate enumerate,
    const char* const message)
{
    if (outCount == nullptr) {
        return InvalidInput(message);
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCount = static_cast<uint32_t>(enumerate().size());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetDeviceInfoAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    std::vector<InputDeviceInfoEXT> (*enumerate)(),
    CNA_InputDeviceInfo* const outInfo)
{
    if (outInfo == nullptr) {
        return InvalidInput("The input-device descriptor output is null.");
    }
    InputDeviceInfoEXT info;
    if (const CNA_Result result = BorrowEnumerated(
            gameHandle,
            index,
            enumerate,
            "The input-device index is out of range.",
            &info);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outInfo = MapDeviceInfo(info);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetDeviceNameSizeAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    std::vector<InputDeviceInfoEXT> (*enumerate)(),
    uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidInput("The input-device name byte-count output is null.");
    }
    InputDeviceInfoEXT info;
    if (const CNA_Result result = BorrowEnumerated(
            gameHandle,
            index,
            enumerate,
            "The input-device index is out of range.",
            &info);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outBytes = info.name.size();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyDeviceNameAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    std::vector<InputDeviceInfoEXT> (*enumerate)(),
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The input-device name output is invalid.");
    }
    InputDeviceInfoEXT info;
    if (const CNA_Result result = BorrowEnumerated(
            gameHandle,
            index,
            enumerate,
            "The input-device index is out of range.",
            &info);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyText(info.name, destination, capacity, outBytes);
}

/** Which of the four process-wide hot-plug events a registration detaches from. */
enum class DeviceEventKind {
    MouseConnected,
    MouseDisconnected,
    KeyboardConnected,
    KeyboardDisconnected
};

// The canonical events are process-wide static fields, so a registration owns only its
// subscription token and detaches by token. Releasing it after ResetForTests() cleared the field
// simply removes nothing.
class DeviceRegistration final {
public:
    DeviceRegistration(
        const DeviceEventKind kind,
        const System::MulticastAction<std::uint32_t>::Token token)
        : kind_(kind)
        , token_(token)
    {
    }

    DeviceRegistration(const DeviceRegistration&) = delete;
    DeviceRegistration& operator=(const DeviceRegistration&) = delete;

    ~DeviceRegistration()
    {
        if (token_ == System::MulticastAction<std::uint32_t>::InvalidToken) {
            return;
        }
        switch (kind_) {
        case DeviceEventKind::MouseConnected:
            (void)InputDevices::MouseConnectedEXT.Remove(token_);
            break;
        case DeviceEventKind::MouseDisconnected:
            (void)InputDevices::MouseDisconnectedEXT.Remove(token_);
            break;
        case DeviceEventKind::KeyboardConnected:
            (void)InputDevices::KeyboardConnectedEXT.Remove(token_);
            break;
        case DeviceEventKind::KeyboardDisconnected:
            (void)InputDevices::KeyboardDisconnectedEXT.Remove(token_);
            break;
        }
    }

private:
    DeviceEventKind kind_;
    System::MulticastAction<std::uint32_t>::Token token_;
};

[[nodiscard]] CNA_Result SubscribeHotplug(
    System::MulticastAction<std::uint32_t>& event,
    const DeviceEventKind kind,
    const CNA_InputDeviceHotplugCallback callback,
    void* const context,
    CNA_InputDeviceEventRegistrationHandle* const outRegistration)
{
    if (outRegistration == nullptr) {
        return InvalidInput("The input-device registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidInput("The input-device hot-plug callback is null.");
    }
    const auto token = event.Add([callback, context](const std::uint32_t id) {
        callback(id, context);
    });
    const auto resource = std::make_shared<DeviceRegistration>(kind, token);
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        CNA::C::Detail::ObjectKind::InputDeviceEventRegistration,
        resource,
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The input-device hot-plug registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result RaiseHotplug(
    const CNA_Handle gameHandle,
    System::MulticastAction<std::uint32_t>& event,
    const uint32_t id)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    event.Invoke(id);
    return CNA_RESULT_SUCCESS;
}

using SensorRead = bool (*)(Microsoft::Xna::Framework::Vector3&);

[[nodiscard]] CNA_Result ReadSensor(
    const CNA_Handle gameHandle,
    const SensorRead read,
    CNA_Vector3* const outValue,
    CNA_Bool* const outAvailable)
{
    if (outValue == nullptr || outAvailable == nullptr) {
        return InvalidInput("The sensor reading output is null.");
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Microsoft::Xna::Framework::Vector3 value;
    if (!read(value)) {
        // The canonical query leaves its reference untouched when no sensor answers, so C leaves
        // the caller's value untouched too and reports the absence through its own flag.
        *outAvailable = CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    }
    outValue->x = value.X;
    outValue->y = value.Y;
    outValue->z = value.Z;
    *outAvailable = CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_sensor_info_init(CNA_SensorInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The sensor descriptor output is null.");
        }
        *outInfo = MapSensorInfo(SensorInfoEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sensor_info_equals(
    const CNA_SensorInfo* const left,
    const CNA_StringView leftName,
    const CNA_SensorInfo* const right,
    const CNA_StringView rightName,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The sensor descriptor comparison output is null.");
        }
        if (const CNA_Result result =
                ValidateVersionedStructure(left, "The first sensor descriptor is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateVersionedStructure(right, "The second sensor descriptor is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SensorInfoEXT nativeLeft;
        SensorInfoEXT nativeRight;
        if (const CNA_Result result = ToSensorType(left->type, &nativeLeft.type);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToSensorType(right->type, &nativeRight.type);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                leftName,
                "The first sensor name is not valid UTF-8.",
                &nativeLeft.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                rightName,
                "The second sensor name is not valid UTF-8.",
                &nativeRight.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        nativeLeft.id = left->id;
        nativeRight.id = right->id;
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sensors_get_count(const CNA_Handle gameHandle, uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountEnumerated(
            gameHandle,
            outCount,
            &Sensors::GetSensorsEXT,
            "The sensor count output is null.");
    });
}

CNA_Result cna_sensors_get_info_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_SensorInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The sensor descriptor output is null.");
        }
        SensorInfoEXT info;
        if (const CNA_Result result = BorrowEnumerated(
                gameHandle,
                index,
                &Sensors::GetSensorsEXT,
                "The sensor index is out of range.",
                &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = MapSensorInfo(info);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sensors_get_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The sensor name byte-count output is null.");
        }
        SensorInfoEXT info;
        if (const CNA_Result result = BorrowEnumerated(
                gameHandle,
                index,
                &Sensors::GetSensorsEXT,
                "The sensor index is out of range.",
                &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = info.name.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sensors_copy_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The sensor name output is invalid.");
        }
        SensorInfoEXT info;
        if (const CNA_Result result = BorrowEnumerated(
                gameHandle,
                index,
                &Sensors::GetSensorsEXT,
                "The sensor index is out of range.",
                &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(info.name, destination, capacity, outBytes);
    });
}

CNA_Result cna_sensors_get_accelerometer(
    const CNA_Handle gameHandle,
    CNA_Vector3* const outAcceleration,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadSensor(
            gameHandle,
            &Sensors::GetAccelerometerEXT,
            outAcceleration,
            outAvailable);
    });
}

CNA_Result cna_sensors_get_gyroscope(
    const CNA_Handle gameHandle,
    CNA_Vector3* const outAngularVelocity,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReadSensor(
            gameHandle,
            &Sensors::GetGyroscopeEXT,
            outAngularVelocity,
            outAvailable);
    });
}

CNA_Result cna_input_device_info_init(CNA_InputDeviceInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The input-device descriptor output is null.");
        }
        *outInfo = MapDeviceInfo(InputDeviceInfoEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_input_device_info_equals(
    const CNA_InputDeviceInfo* const left,
    const CNA_StringView leftName,
    const CNA_InputDeviceInfo* const right,
    const CNA_StringView rightName,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The input-device comparison output is null.");
        }
        if (const CNA_Result result =
                ValidateVersionedStructure(left, "The first input-device descriptor is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateVersionedStructure(right, "The second input-device descriptor is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        InputDeviceInfoEXT nativeLeft;
        InputDeviceInfoEXT nativeRight;
        if (const CNA_Result result = CopyBorrowedText(
                leftName,
                "The first input-device name is not valid UTF-8.",
                &nativeLeft.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                rightName,
                "The second input-device name is not valid UTF-8.",
                &nativeRight.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        nativeLeft.id = left->id;
        nativeRight.id = right->id;
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_input_devices_get_mouse_count(
    const CNA_Handle gameHandle,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountEnumerated(
            gameHandle,
            outCount,
            &InputDevices::GetMiceEXT,
            "The mouse count output is null.");
    });
}

CNA_Result cna_input_devices_get_mouse_info_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_InputDeviceInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDeviceInfoAt(gameHandle, index, &InputDevices::GetMiceEXT, outInfo);
    });
}

CNA_Result cna_input_devices_get_mouse_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDeviceNameSizeAt(gameHandle, index, &InputDevices::GetMiceEXT, outBytes);
    });
}

CNA_Result cna_input_devices_copy_mouse_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceNameAt(
            gameHandle,
            index,
            &InputDevices::GetMiceEXT,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_input_devices_get_keyboard_count(
    const CNA_Handle gameHandle,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountEnumerated(
            gameHandle,
            outCount,
            &InputDevices::GetKeyboardsEXT,
            "The keyboard count output is null.");
    });
}

CNA_Result cna_input_devices_get_keyboard_info_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_InputDeviceInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDeviceInfoAt(gameHandle, index, &InputDevices::GetKeyboardsEXT, outInfo);
    });
}

CNA_Result cna_input_devices_get_keyboard_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDeviceNameSizeAt(gameHandle, index, &InputDevices::GetKeyboardsEXT, outBytes);
    });
}

CNA_Result cna_input_devices_copy_keyboard_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceNameAt(
            gameHandle,
            index,
            &InputDevices::GetKeyboardsEXT,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_input_devices_get_touch_device_count(
    const CNA_Handle gameHandle,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountEnumerated(
            gameHandle,
            outCount,
            &InputDevices::GetTouchDevicesEXT,
            "The touch-device count output is null.");
    });
}

CNA_Result cna_input_devices_get_touch_device_info_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_InputDeviceInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDeviceInfoAt(gameHandle, index, &InputDevices::GetTouchDevicesEXT, outInfo);
    });
}

CNA_Result cna_input_devices_get_touch_device_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GetDeviceNameSizeAt(gameHandle, index, &InputDevices::GetTouchDevicesEXT, outBytes);
    });
}

CNA_Result cna_input_devices_copy_touch_device_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceNameAt(
            gameHandle,
            index,
            &InputDevices::GetTouchDevicesEXT,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_input_devices_subscribe_mouse_connected_ext(
    const CNA_InputDeviceHotplugCallback callback,
    void* const context,
    CNA_InputDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeHotplug(
            InputDevices::MouseConnectedEXT,
            DeviceEventKind::MouseConnected,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_input_devices_subscribe_mouse_disconnected_ext(
    const CNA_InputDeviceHotplugCallback callback,
    void* const context,
    CNA_InputDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeHotplug(
            InputDevices::MouseDisconnectedEXT,
            DeviceEventKind::MouseDisconnected,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_input_devices_subscribe_keyboard_connected_ext(
    const CNA_InputDeviceHotplugCallback callback,
    void* const context,
    CNA_InputDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeHotplug(
            InputDevices::KeyboardConnectedEXT,
            DeviceEventKind::KeyboardConnected,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_input_devices_subscribe_keyboard_disconnected_ext(
    const CNA_InputDeviceHotplugCallback callback,
    void* const context,
    CNA_InputDeviceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeHotplug(
            InputDevices::KeyboardDisconnectedEXT,
            DeviceEventKind::KeyboardDisconnected,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_input_devices_unsubscribe_ext(
    const CNA_InputDeviceEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DeviceRegistration> resource;
        const CNA_Result getResult = CNA::C::Detail::GetRuntimeHandles().Get(
            registration,
            CNA::C::Detail::ObjectKind::InputDeviceEventRegistration,
            &resource);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The input-device registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The input-device registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_input_devices_raise_mouse_connected_ext(
    const CNA_Handle gameHandle,
    const uint32_t id)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return RaiseHotplug(gameHandle, InputDevices::MouseConnectedEXT, id);
    });
}

CNA_Result cna_input_devices_raise_mouse_disconnected_ext(
    const CNA_Handle gameHandle,
    const uint32_t id)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return RaiseHotplug(gameHandle, InputDevices::MouseDisconnectedEXT, id);
    });
}

CNA_Result cna_input_devices_raise_keyboard_connected_ext(
    const CNA_Handle gameHandle,
    const uint32_t id)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return RaiseHotplug(gameHandle, InputDevices::KeyboardConnectedEXT, id);
    });
}

CNA_Result cna_input_devices_raise_keyboard_disconnected_ext(
    const CNA_Handle gameHandle,
    const uint32_t id)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return RaiseHotplug(gameHandle, InputDevices::KeyboardDisconnectedEXT, id);
    });
}

CNA_Result cna_input_devices_reset_for_tests_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        InputDevices::ResetForTests();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clipboard_get_text_size(const CNA_Handle gameHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The clipboard text byte-count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = Clipboard::GetTextEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clipboard_copy_text(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The clipboard text output is invalid.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(Clipboard::GetTextEXT(), destination, capacity, outBytes);
    });
}

CNA_Result cna_clipboard_set_text(const CNA_Handle gameHandle, const CNA_StringView text)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string value;
        if (const CNA_Result result = CopyBorrowedText(
                text,
                "The clipboard text is not valid UTF-8.",
                &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Clipboard::SetTextEXT(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clipboard_get_has_text(const CNA_Handle gameHandle, CNA_Bool* const outHasText)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasText == nullptr) {
            return InvalidInput("The clipboard text-presence output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasText = Clipboard::HasTextEXT() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_power_get_info(
    const CNA_Handle gameHandle,
    CNA_PowerState* const outState,
    int32_t* const outSecondsLeft,
    int32_t* const outPercent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr || outSecondsLeft == nullptr || outPercent == nullptr) {
            return InvalidInput("The power information output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        int secondsLeft = -1;
        int percent = -1;
        const PowerStateEXT state = Power::GetInfoEXT(secondsLeft, percent);
        *outState = static_cast<CNA_PowerState>(state);
        *outSecondsLeft = static_cast<int32_t>(secondsLeft);
        *outPercent = static_cast<int32_t>(percent);
        return CNA_RESULT_SUCCESS;
    });
}
