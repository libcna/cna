// SPDX-License-Identifier: MS-PL

#include "CNA/C/input_joystick.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Input/JoystickCapabilities.hpp"
#include "CNA/Input/JoystickHatPosition.hpp"
#include "CNA/Input/JoystickInfo.hpp"
#include "CNA/Input/JoystickState.hpp"
#include "CNA/Input/JoystickType.hpp"
#include "CNA/Input/Joysticks.hpp"
#include "CNA/Input/PowerState.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using CNA::Input::JoystickCapabilitiesEXT;
using CNA::Input::JoystickHatPositionEXT;
using CNA::Input::JoystickInfoEXT;
using CNA::Input::Joysticks;
using CNA::Input::JoystickStateEXT;
using CNA::Input::JoystickTypeEXT;
using CNA::Input::PowerStateEXT;

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

[[nodiscard]] CNA_Result ToJoystickType(const CNA_JoystickType type, JoystickTypeEXT* const outType)
{
    if (type > CNA_JOYSTICK_TYPE_MAXIMUM) {
        return InvalidInput("The joystick type identity is undefined.");
    }
    *outType = static_cast<JoystickTypeEXT>(type);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToPowerState(const CNA_PowerState state, PowerStateEXT* const outState)
{
    switch (state) {
    case CNA_POWER_STATE_ERROR: *outState = PowerStateEXT::Error; return CNA_RESULT_SUCCESS;
    case CNA_POWER_STATE_UNKNOWN: *outState = PowerStateEXT::Unknown; return CNA_RESULT_SUCCESS;
    case CNA_POWER_STATE_ON_BATTERY:
        *outState = PowerStateEXT::OnBattery;
        return CNA_RESULT_SUCCESS;
    case CNA_POWER_STATE_NO_BATTERY:
        *outState = PowerStateEXT::NoBattery;
        return CNA_RESULT_SUCCESS;
    case CNA_POWER_STATE_CHARGING: *outState = PowerStateEXT::Charging; return CNA_RESULT_SUCCESS;
    case CNA_POWER_STATE_CHARGED: *outState = PowerStateEXT::Charged; return CNA_RESULT_SUCCESS;
    default: return InvalidInput("The power-state identity is undefined.");
    }
}

[[nodiscard]] CNA_JoystickInfo MapInfo(const JoystickInfoEXT& info)
{
    CNA_JoystickInfo mapped = {};
    mapped.struct_size = sizeof(CNA_JoystickInfo);
    mapped.struct_version = StructureVersion;
    mapped.id = info.id;
    mapped.type = static_cast<CNA_JoystickType>(info.type);
    return mapped;
}

[[nodiscard]] CNA_JoystickCapabilities MapCapabilities(const JoystickCapabilitiesEXT& capabilities)
{
    CNA_JoystickCapabilities mapped = {};
    mapped.struct_size = sizeof(CNA_JoystickCapabilities);
    mapped.struct_version = StructureVersion;
    mapped.axis_count = static_cast<int32_t>(capabilities.axisCount);
    mapped.button_count = static_cast<int32_t>(capabilities.buttonCount);
    mapped.hat_count = static_cast<int32_t>(capabilities.hatCount);
    mapped.ball_count = static_cast<int32_t>(capabilities.ballCount);
    mapped.type = static_cast<CNA_JoystickType>(capabilities.type);
    mapped.power_state = static_cast<CNA_PowerState>(capabilities.powerState);
    mapped.power_percent = static_cast<int32_t>(capabilities.powerPercent);
    mapped.is_connected = capabilities.isConnected ? CNA_TRUE : CNA_FALSE;
    return mapped;
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The joystick text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the joystick text.");
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

// One capture is one snapshot: every array in it came from the same instant, which is exactly what
// a caller cannot reconstruct from four independent per-array queries.
[[nodiscard]] CNA_Result BorrowState(
    const CNA_JoystickStateHandle handle,
    std::shared_ptr<JoystickStateEXT>* const outState)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
        handle,
        CNA::C::Detail::ObjectKind::JoystickState,
        outState);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The joystick snapshot handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TSource, typename TDestination, typename TMap>
[[nodiscard]] CNA_Result CopyElements(
    const std::vector<TSource>& source,
    TDestination* const destination,
    const uint64_t capacity,
    uint64_t* const outCount,
    const TMap map)
{
    if (outCount == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The joystick snapshot copy output is invalid.");
    }
    *outCount = static_cast<uint64_t>(source.size());
    if (capacity < static_cast<uint64_t>(source.size())) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the joystick snapshot array.");
    }
    for (std::size_t index = 0U; index < source.size(); ++index) {
        destination[index] = map(source[index]);
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TSize>
[[nodiscard]] CNA_Result CountElements(
    const CNA_JoystickStateHandle handle,
    uint32_t* const outCount,
    const TSize size)
{
    if (outCount == nullptr) {
        return InvalidInput("The joystick snapshot count output is null.");
    }
    std::shared_ptr<JoystickStateEXT> state;
    if (const CNA_Result result = BorrowState(handle, &state); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCount = static_cast<uint32_t>(size(*state));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowEnumeratedJoystick(
    const CNA_Handle gameHandle,
    const uint32_t index,
    JoystickInfoEXT* const outInfo)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<JoystickInfoEXT> joysticks = Joysticks::GetJoysticksEXT();
    if (index >= joysticks.size()) {
        return InvalidInput("The joystick index is out of range.");
    }
    *outInfo = joysticks[index];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowCapabilities(
    const CNA_Handle gameHandle,
    const uint32_t id,
    JoystickCapabilitiesEXT* const outCapabilities)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCapabilities = Joysticks::GetCapabilitiesEXT(id);
    return CNA_RESULT_SUCCESS;
}

/** Which of the two process-wide hot-plug events a registration detaches from. */
enum class JoystickEventKind { Connected, Disconnected };

// The canonical events are process-wide static fields, so a registration owns only its
// subscription token and detaches by token. Releasing it after ResetForTests() cleared the field
// simply removes nothing.
class JoystickRegistration final {
public:
    JoystickRegistration(
        const JoystickEventKind kind,
        const System::MulticastAction<std::uint32_t>::Token token)
        : kind_(kind)
        , token_(token)
    {
    }

    JoystickRegistration(const JoystickRegistration&) = delete;
    JoystickRegistration& operator=(const JoystickRegistration&) = delete;

    ~JoystickRegistration()
    {
        if (token_ == System::MulticastAction<std::uint32_t>::InvalidToken) {
            return;
        }
        switch (kind_) {
        case JoystickEventKind::Connected:
            (void)Joysticks::ConnectedEXT.Remove(token_);
            break;
        case JoystickEventKind::Disconnected:
            (void)Joysticks::DisconnectedEXT.Remove(token_);
            break;
        }
    }

private:
    JoystickEventKind kind_;
    System::MulticastAction<std::uint32_t>::Token token_;
};

[[nodiscard]] CNA_Result SubscribeHotplug(
    System::MulticastAction<std::uint32_t>& event,
    const JoystickEventKind kind,
    const CNA_JoystickHotplugCallback callback,
    void* const context,
    CNA_JoystickEventRegistrationHandle* const outRegistration)
{
    if (outRegistration == nullptr) {
        return InvalidInput("The joystick registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidInput("The joystick hot-plug callback is null.");
    }
    const auto token = event.Add([callback, context](const std::uint32_t id) {
        callback(id, context);
    });
    const auto resource = std::make_shared<JoystickRegistration>(kind, token);
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        CNA::C::Detail::ObjectKind::JoystickEventRegistration,
        resource,
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The joystick hot-plug registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_joystick_info_init(CNA_JoystickInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The joystick descriptor output is null.");
        }
        *outInfo = MapInfo(JoystickInfoEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joystick_info_equals(
    const CNA_JoystickInfo* const left,
    const CNA_StringView leftName,
    const CNA_JoystickInfo* const right,
    const CNA_StringView rightName,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The joystick descriptor comparison output is null.");
        }
        if (const CNA_Result result =
                ValidateVersionedStructure(left, "The first joystick descriptor is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ValidateVersionedStructure(right, "The second joystick descriptor is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        JoystickInfoEXT nativeLeft;
        JoystickInfoEXT nativeRight;
        if (const CNA_Result result = ToJoystickType(left->type, &nativeLeft.type);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToJoystickType(right->type, &nativeRight.type);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                leftName,
                "The first joystick name is not valid UTF-8.",
                &nativeLeft.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                rightName,
                "The second joystick name is not valid UTF-8.",
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

CNA_Result cna_joystick_capabilities_init(CNA_JoystickCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCapabilities == nullptr) {
            return InvalidInput("The joystick capabilities output is null.");
        }
        *outCapabilities = MapCapabilities(JoystickCapabilitiesEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joystick_capabilities_equals(
    const CNA_JoystickCapabilities* const left,
    const CNA_StringView leftName,
    const CNA_StringView leftGuid,
    const CNA_JoystickCapabilities* const right,
    const CNA_StringView rightName,
    const CNA_StringView rightGuid,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The joystick capabilities comparison output is null.");
        }
        if (const CNA_Result result = ValidateVersionedStructure(
                left,
                "The first joystick capabilities structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateVersionedStructure(
                right,
                "The second joystick capabilities structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        JoystickCapabilitiesEXT nativeLeft;
        JoystickCapabilitiesEXT nativeRight;
        if (const CNA_Result result = ToJoystickType(left->type, &nativeLeft.type);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToJoystickType(right->type, &nativeRight.type);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToPowerState(left->power_state, &nativeLeft.powerState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToPowerState(right->power_state, &nativeRight.powerState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                leftName,
                "The first joystick name is not valid UTF-8.",
                &nativeLeft.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                rightName,
                "The second joystick name is not valid UTF-8.",
                &nativeRight.name);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                leftGuid,
                "The first joystick GUID is not valid UTF-8.",
                &nativeLeft.guid);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CopyBorrowedText(
                rightGuid,
                "The second joystick GUID is not valid UTF-8.",
                &nativeRight.guid);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        nativeLeft.isConnected = left->is_connected != CNA_FALSE;
        nativeLeft.axisCount = left->axis_count;
        nativeLeft.buttonCount = left->button_count;
        nativeLeft.hatCount = left->hat_count;
        nativeLeft.ballCount = left->ball_count;
        nativeLeft.powerPercent = left->power_percent;
        nativeRight.isConnected = right->is_connected != CNA_FALSE;
        nativeRight.axisCount = right->axis_count;
        nativeRight.buttonCount = right->button_count;
        nativeRight.hatCount = right->hat_count;
        nativeRight.ballCount = right->ball_count;
        nativeRight.powerPercent = right->power_percent;
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_get_count(const CNA_Handle gameHandle, uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The joystick count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint32_t>(Joysticks::GetJoysticksEXT().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_get_info_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    CNA_JoystickInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The joystick descriptor output is null.");
        }
        JoystickInfoEXT info;
        if (const CNA_Result result = BorrowEnumeratedJoystick(gameHandle, index, &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = MapInfo(info);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_get_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The joystick name byte-count output is null.");
        }
        JoystickInfoEXT info;
        if (const CNA_Result result = BorrowEnumeratedJoystick(gameHandle, index, &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = info.name.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_copy_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The joystick name output is invalid.");
        }
        JoystickInfoEXT info;
        if (const CNA_Result result = BorrowEnumeratedJoystick(gameHandle, index, &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(info.name, destination, capacity, outBytes);
    });
}

CNA_Result cna_joysticks_get_capabilities(
    const CNA_Handle gameHandle,
    const uint32_t id,
    CNA_JoystickCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCapabilities == nullptr) {
            return InvalidInput("The joystick capabilities output is null.");
        }
        JoystickCapabilitiesEXT capabilities;
        if (const CNA_Result result = BorrowCapabilities(gameHandle, id, &capabilities);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCapabilities = MapCapabilities(capabilities);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_get_capabilities_name_size(
    const CNA_Handle gameHandle,
    const uint32_t id,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The joystick device-name byte-count output is null.");
        }
        JoystickCapabilitiesEXT capabilities;
        if (const CNA_Result result = BorrowCapabilities(gameHandle, id, &capabilities);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = capabilities.name.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_copy_capabilities_name(
    const CNA_Handle gameHandle,
    const uint32_t id,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The joystick device-name output is invalid.");
        }
        JoystickCapabilitiesEXT capabilities;
        if (const CNA_Result result = BorrowCapabilities(gameHandle, id, &capabilities);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(capabilities.name, destination, capacity, outBytes);
    });
}

CNA_Result cna_joysticks_get_capabilities_guid_size(
    const CNA_Handle gameHandle,
    const uint32_t id,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The joystick GUID byte-count output is null.");
        }
        JoystickCapabilitiesEXT capabilities;
        if (const CNA_Result result = BorrowCapabilities(gameHandle, id, &capabilities);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = capabilities.guid.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_copy_capabilities_guid(
    const CNA_Handle gameHandle,
    const uint32_t id,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The joystick GUID output is invalid.");
        }
        JoystickCapabilitiesEXT capabilities;
        if (const CNA_Result result = BorrowCapabilities(gameHandle, id, &capabilities);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(capabilities.guid, destination, capacity, outBytes);
    });
}

CNA_Result cna_joysticks_capture_state(
    const CNA_Handle gameHandle,
    const uint32_t id,
    CNA_JoystickStateHandle* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The joystick snapshot output is null.");
        }
        *outState = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<JoystickStateEXT>(Joysticks::GetStateEXT(id));
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            CNA::C::Detail::ObjectKind::JoystickState,
            std::move(resource),
            outState);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The joystick snapshot handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joystick_state_get_axis_count(
    const CNA_JoystickStateHandle state,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountElements(state, outCount, [](const JoystickStateEXT& value) {
            return value.axes.size();
        });
    });
}

CNA_Result cna_joystick_state_copy_axes(
    const CNA_JoystickStateHandle state,
    int16_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<JoystickStateEXT> snapshot;
        if (const CNA_Result result = BorrowState(state, &snapshot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyElements(
            snapshot->axes,
            destination,
            capacity,
            outCount,
            [](const std::int16_t value) { return static_cast<int16_t>(value); });
    });
}

CNA_Result cna_joystick_state_get_button_count(
    const CNA_JoystickStateHandle state,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountElements(state, outCount, [](const JoystickStateEXT& value) {
            return value.buttons.size();
        });
    });
}

CNA_Result cna_joystick_state_copy_buttons(
    const CNA_JoystickStateHandle state,
    CNA_Bool* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<JoystickStateEXT> snapshot;
        if (const CNA_Result result = BorrowState(state, &snapshot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyElements(
            snapshot->buttons,
            destination,
            capacity,
            outCount,
            [](const bool value) { return value ? CNA_TRUE : CNA_FALSE; });
    });
}

CNA_Result cna_joystick_state_get_hat_count(
    const CNA_JoystickStateHandle state,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountElements(state, outCount, [](const JoystickStateEXT& value) {
            return value.hats.size();
        });
    });
}

CNA_Result cna_joystick_state_copy_hats(
    const CNA_JoystickStateHandle state,
    CNA_JoystickHatPosition* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<JoystickStateEXT> snapshot;
        if (const CNA_Result result = BorrowState(state, &snapshot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyElements(
            snapshot->hats,
            destination,
            capacity,
            outCount,
            [](const JoystickHatPositionEXT value) {
                return static_cast<CNA_JoystickHatPosition>(value);
            });
    });
}

CNA_Result cna_joystick_state_get_ball_count(
    const CNA_JoystickStateHandle state,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CountElements(state, outCount, [](const JoystickStateEXT& value) {
            return value.balls.size();
        });
    });
}

CNA_Result cna_joystick_state_copy_balls(
    const CNA_JoystickStateHandle state,
    CNA_Point* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<JoystickStateEXT> snapshot;
        if (const CNA_Result result = BorrowState(state, &snapshot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyElements(
            snapshot->balls,
            destination,
            capacity,
            outCount,
            [](const Microsoft::Xna::Framework::Point& value) {
                CNA_Point point = {};
                point.x = static_cast<int32_t>(value.X);
                point.y = static_cast<int32_t>(value.Y);
                return point;
            });
    });
}

CNA_Result cna_joystick_state_equals(
    const CNA_JoystickStateHandle left,
    const CNA_JoystickStateHandle right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The joystick snapshot comparison output is null.");
        }
        std::shared_ptr<JoystickStateEXT> nativeLeft;
        std::shared_ptr<JoystickStateEXT> nativeRight;
        if (const CNA_Result result = BorrowState(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowState(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = *nativeLeft == *nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joystick_state_destroy(const CNA_JoystickStateHandle state)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<JoystickStateEXT> snapshot;
        if (const CNA_Result result = BorrowState(state, &snapshot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(state);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The joystick snapshot handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_subscribe_connected_ext(
    const CNA_JoystickHotplugCallback callback,
    void* const context,
    CNA_JoystickEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeHotplug(
            Joysticks::ConnectedEXT,
            JoystickEventKind::Connected,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_joysticks_subscribe_disconnected_ext(
    const CNA_JoystickHotplugCallback callback,
    void* const context,
    CNA_JoystickEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeHotplug(
            Joysticks::DisconnectedEXT,
            JoystickEventKind::Disconnected,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_joysticks_unsubscribe_ext(
    const CNA_JoystickEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<JoystickRegistration> resource;
        const CNA_Result getResult = CNA::C::Detail::GetRuntimeHandles().Get(
            registration,
            CNA::C::Detail::ObjectKind::JoystickEventRegistration,
            &resource);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The joystick registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The joystick registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_raise_connected_ext(const CNA_Handle gameHandle, const uint32_t id)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Joysticks::ConnectedEXT.Invoke(id);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_raise_disconnected_ext(const CNA_Handle gameHandle, const uint32_t id)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Joysticks::DisconnectedEXT.Invoke(id);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_joysticks_reset_for_tests_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Joysticks::ResetForTests();
        return CNA_RESULT_SUCCESS;
    });
}
