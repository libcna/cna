// SPDX-License-Identifier: MS-PL

#include "CNA/C/input_devices.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
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
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;

using CNA::Input::Clipboard;
using CNA::Input::InputDeviceInfoEXT;
using CNA::Input::InputDevices;
using CNA::Input::Power;
using CNA::Input::PowerStateEXT;
using CNA::Input::SensorInfoEXT;
using CNA::Input::Sensors;
using CNA::Input::SensorTypeEXT;

[[nodiscard]] CNA_Result ToSensorType(const CNA_SensorType type, SensorTypeEXT* const outType)
{
    if (type > CNA_SENSOR_TYPE_MAXIMUM) {
        return InvalidArgument("The sensor kind identity is undefined.");
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
        return InvalidArgument(message);
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
        return InvalidArgument(message);
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
        return InvalidArgument("The input-device descriptor output is null.");
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
        return InvalidArgument("The input-device name byte-count output is null.");
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
        return InvalidArgument("The input-device name output is invalid.");
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
        return InvalidArgument("The input-device registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidArgument("The input-device hot-plug callback is null.");
    }
    const auto token = event.Add([callback, context](const std::uint32_t id) {
        callback(id, context);
    });
    const auto resource = std::make_shared<DeviceRegistration>(kind, token);
    return CreateHandle(CNA::C::Detail::ObjectKind::InputDeviceEventRegistration,
        resource,
        outRegistration);
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
        return InvalidArgument("The sensor reading output is null.");
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

[[nodiscard]] CNA_Result SensorInfoEquals(
    const CNA_SensorInfo* const left,
    const CNA_StringView leftName,
    const CNA_SensorInfo* const right,
    const CNA_StringView rightName,
    CNA_Bool* const outEqual)
{
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
    if (const CNA_Result result = ReadText(
            leftName, "The first sensor name is not valid UTF-8.", &nativeLeft.name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            rightName, "The second sensor name is not valid UTF-8.", &nativeRight.name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    nativeLeft.id = left->id;
    nativeRight.id = right->id;
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SensorsGetInfoAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_SensorInfo* const outInfo)
{
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
}

[[nodiscard]] CNA_Result SensorsGetNameSizeAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
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
}

[[nodiscard]] CNA_Result SensorsCopyNameAt(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if ((destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidArgument("The sensor name output is invalid.");
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
}

[[nodiscard]] CNA_Result InputDeviceInfoEquals(
    const CNA_InputDeviceInfo* const left,
    const CNA_StringView leftName,
    const CNA_InputDeviceInfo* const right,
    const CNA_StringView rightName,
    CNA_Bool* const outEqual)
{
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
    if (const CNA_Result result = ReadText(
            leftName, "The first input-device name is not valid UTF-8.", &nativeLeft.name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            rightName, "The second input-device name is not valid UTF-8.", &nativeRight.name);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    nativeLeft.id = left->id;
    nativeRight.id = right->id;
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result InputDevicesUnsubscribeExt(
    const CNA_InputDeviceEventRegistrationHandle registration)
{
    std::shared_ptr<DeviceRegistration> resource;
    const CNA_Result getResult = LookupHandle(
        registration, CNA::C::Detail::ObjectKind::InputDeviceEventRegistration, &resource);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    return ReleaseHandle(registration);
}

[[nodiscard]] CNA_Result PowerGetInfo(
    const CNA_Handle gameHandle,
    CNA_PowerState* const outState,
    int32_t* const outSecondsLeft,
    int32_t* const outPercent)
{
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
}

CNA_Result ClipboardMime(CNA_Handle game, uint64_t index, char* destination,
    uint64_t capacity, uint64_t* outBytes, bool sizeOnly)
{
    if (const auto result = CNA::C::Detail::ValidateCopyOutput(destination, capacity, outBytes);
        result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ValidateActiveGameHandle(game); result != CNA_RESULT_SUCCESS) return result;
    const auto types = Clipboard::GetMimeTypesEXT();
    if (index >= types.size()) return InvalidArgument("MIME index is outside the clipboard list.");
    if (sizeOnly) { *outBytes = types[index].size(); return CNA_RESULT_SUCCESS; }
    return CopyText(types[index], destination, capacity, outBytes);
}

CNA_Result ClipboardData(CNA_Handle game, CNA_StringView mime, uint8_t* destination,
    uint64_t capacity, uint64_t* outBytes, bool sizeOnly)
{
    if (const auto result = CNA::C::Detail::ValidateCopyOutput(destination, capacity, outBytes);
        result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ValidateActiveGameHandle(game); result != CNA_RESULT_SUCCESS) return result;
    std::string type;
    if (const auto result = ReadText(mime, "Invalid MIME identifier.", &type, CNA::C::Detail::EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) return result;
    const auto bytes = Clipboard::GetDataEXT(type);
    if (sizeOnly) { *outBytes = bytes.size(); return CNA_RESULT_SUCCESS; }
    return CNA::C::Detail::CopyBytes(bytes, destination, capacity, outBytes);
}

CNA_Result ClipboardFormats(CNA_Handle game, const CNA_ClipboardOffer* formats,
    uint64_t count, CNA_Bool* outAccepted)
{
    if (count > SIZE_MAX / sizeof(CNA_ClipboardOffer)) return CNA::C::Detail::Overflow("Too many MIME offers.");
    if (count && !formats) return InvalidArgument("Null MIME offers.");
    if (const auto result = ValidateActiveGameHandle(game); result != CNA_RESULT_SUCCESS) return result;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> offers;
    if (count > offers.max_size()) return CNA::C::Detail::Overflow("Too many MIME offers.");
    offers.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        const auto& input = formats[i];
        std::string mime;
        if (const auto result = ReadText(input.mime_type, "Invalid MIME identifier.", &mime, CNA::C::Detail::EmbeddedNul::Refused);
            result != CNA_RESULT_SUCCESS) return result;
        if (mime.empty()) return InvalidArgument("Empty MIME identifier.");
        for (const auto& offer : offers) if (offer.first == mime) return InvalidArgument("Duplicate MIME identifier.");
        std::vector<uint8_t> bytes;
        if (input.byte_count > bytes.max_size()) return CNA::C::Detail::Overflow("MIME payload is too large.");
        if (input.byte_count && !input.data) return InvalidArgument("Null MIME payload.");
        if (input.byte_count) bytes.assign(input.data, input.data + static_cast<std::size_t>(input.byte_count));
        offers.emplace_back(std::move(mime), std::move(bytes));
    }
    *outAccepted = ToCBool(Clipboard::SetDataEXT(offers));
    return CNA_RESULT_SUCCESS;
}

CNA_Result ClipboardDataSet(CNA_Handle game, CNA_StringView mime, const uint8_t* bytes,
    uint64_t count, CNA_Bool* outAccepted)
{
    const CNA_ClipboardOffer offer{mime, bytes, count};
    return ClipboardFormats(game, &offer, 1, outAccepted);
}

} // namespace

// The routes generated from modules/c-api/spec/InputDevices.toml.
#include "generated/InputDevices.inc"
