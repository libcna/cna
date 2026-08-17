// SPDX-License-Identifier: MS-PL

#include "CNA/C/input_haptics.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/Input/HapticCapabilities.hpp"
#include "CNA/Input/HapticDevice.hpp"
#include "CNA/Input/HapticDirection.hpp"
#include "CNA/Input/HapticEffect.hpp"
#include "CNA/Input/HapticEffectType.hpp"
#include "CNA/Input/HapticFeature.hpp"
#include "CNA/Input/HapticInfo.hpp"
#include "CNA/Input/Haptics.hpp"

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

using CNA::Input::HapticCapabilitiesEXT;
using CNA::Input::HapticDevice;
using CNA::Input::HapticDirectionEXT;
using CNA::Input::HapticDirectionTypeEXT;
using CNA::Input::HapticEffectEXT;
using CNA::Input::HapticEffectTypeEXT;
using CNA::Input::HapticFeatureEXT;
using CNA::Input::HapticInfoEXT;
using CNA::Input::Haptics;

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

[[nodiscard]] CNA_Result ValidateSampleBuffer(
    const uint16_t* const data,
    const uint64_t count)
{
    if (data == nullptr && count != UINT64_C(0)) {
        return InvalidInput("The custom sample buffer is null.");
    }
    if (count > UINT64_C(0x00FFFFFF)) {
        return InvalidInput("The custom sample count is implausibly large.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The haptic text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the haptic text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToDirection(
    const CNA_HapticDirection& direction,
    HapticDirectionEXT* const outDirection)
{
    if (direction.type > CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM) {
        return InvalidInput("The haptic direction type is undefined.");
    }
    outDirection->type = static_cast<HapticDirectionTypeEXT>(direction.type);
    outDirection->values = {direction.values[0], direction.values[1], direction.values[2]};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_HapticDirection MapDirection(const HapticDirectionEXT& direction)
{
    CNA_HapticDirection mapped = {};
    mapped.type = static_cast<CNA_HapticDirectionType>(direction.type);
    mapped.values[0] = direction.values[0];
    mapped.values[1] = direction.values[1];
    mapped.values[2] = direction.values[2];
    return mapped;
}

template<typename TNative, typename TC, std::size_t TCount>
void CopyAxes(const std::array<TNative, TCount>& source, TC* const destination)
{
    for (std::size_t index = 0U; index < TCount; ++index) {
        destination[index] = static_cast<TC>(source[index]);
    }
}

template<typename TNative, typename TC, std::size_t TCount>
void CopyAxesToNative(const TC* const source, std::array<TNative, TCount>* const destination)
{
    for (std::size_t index = 0U; index < TCount; ++index) {
        (*destination)[index] = static_cast<TNative>(source[index]);
    }
}

// The custom waveform crosses beside the value rather than inside it, so the value stays a plain
// copyable POD. Everything else is a field-for-field transfer.
[[nodiscard]] CNA_Result ToEffect(
    const CNA_HapticEffect* const effect,
    const uint16_t* const customData,
    const uint64_t customSampleCount,
    HapticEffectEXT* const outEffect)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(effect, "The haptic effect structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (effect->type > CNA_HAPTIC_EFFECT_TYPE_MAXIMUM) {
        return InvalidInput("The haptic effect type is undefined.");
    }
    if (const CNA_Result result = ValidateSampleBuffer(customData, customSampleCount);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ToDirection(effect->direction, &outEffect->direction);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outEffect->type = static_cast<HapticEffectTypeEXT>(effect->type);
    outEffect->length = effect->length;
    outEffect->delay = effect->delay;
    outEffect->button = effect->button;
    outEffect->interval = effect->interval;
    outEffect->level = effect->level;
    outEffect->period = effect->period;
    outEffect->magnitude = effect->magnitude;
    outEffect->offset = effect->offset;
    outEffect->phase = effect->phase;
    outEffect->rampStart = effect->ramp_start;
    outEffect->rampEnd = effect->ramp_end;
    CopyAxesToNative(effect->right_saturation, &outEffect->rightSaturation);
    CopyAxesToNative(effect->left_saturation, &outEffect->leftSaturation);
    CopyAxesToNative(effect->right_coefficient, &outEffect->rightCoefficient);
    CopyAxesToNative(effect->left_coefficient, &outEffect->leftCoefficient);
    CopyAxesToNative(effect->deadband, &outEffect->deadband);
    CopyAxesToNative(effect->center, &outEffect->center);
    outEffect->largeMagnitude = effect->large_magnitude;
    outEffect->smallMagnitude = effect->small_magnitude;
    outEffect->customChannels = effect->custom_channels;
    outEffect->customPeriod = effect->custom_period;
    outEffect->customData.assign(customData, customData + customSampleCount);
    outEffect->attackLength = effect->attack_length;
    outEffect->attackLevel = effect->attack_level;
    outEffect->fadeLength = effect->fade_length;
    outEffect->fadeLevel = effect->fade_level;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_HapticCapabilities MapCapabilities(const HapticCapabilitiesEXT& capabilities)
{
    CNA_HapticCapabilities mapped = {};
    mapped.struct_size = sizeof(CNA_HapticCapabilities);
    mapped.struct_version = StructureVersion;
    mapped.features = static_cast<CNA_HapticFeature>(capabilities.features);
    mapped.axis_count = static_cast<int32_t>(capabilities.axisCount);
    mapped.max_effects = static_cast<int32_t>(capabilities.maxEffects);
    mapped.max_effects_playing = static_cast<int32_t>(capabilities.maxEffectsPlaying);
    mapped.is_open = capabilities.isOpen ? CNA_TRUE : CNA_FALSE;
    mapped.rumble_supported = capabilities.rumbleSupported ? CNA_TRUE : CNA_FALSE;
    return mapped;
}

[[nodiscard]] CNA_Result BorrowDevice(
    const CNA_HapticDeviceHandle handle,
    std::shared_ptr<HapticDevice>* const outDevice)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
        handle,
        CNA::C::Detail::ObjectKind::HapticDevice,
        outDevice);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The haptic device handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishDevice(
    HapticDevice&& device,
    CNA_HapticDeviceHandle* const outDevice)
{
    auto resource = std::make_shared<HapticDevice>(std::move(device));
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        CNA::C::Detail::ObjectKind::HapticDevice,
        std::move(resource),
        outDevice);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The haptic device handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

using DeviceAction = bool (HapticDevice::*)();

[[nodiscard]] CNA_Result ApplyDeviceAction(
    const CNA_HapticDeviceHandle handle,
    const DeviceAction action,
    CNA_Bool* const outApplied,
    const char* const message)
{
    if (outApplied == nullptr) {
        return InvalidInput(message);
    }
    std::shared_ptr<HapticDevice> device;
    if (const CNA_Result result = BorrowDevice(handle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outApplied = ((*device).*action)() ? CNA_TRUE : CNA_FALSE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowEnumeratedHaptic(
    const CNA_Handle gameHandle,
    const uint32_t index,
    HapticInfoEXT* const outInfo)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::vector<HapticInfoEXT> haptics = Haptics::GetHapticsEXT();
    if (index >= haptics.size()) {
        return InvalidInput("The haptic device index is out of range.");
    }
    *outInfo = haptics[index];
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_haptic_direction_init(CNA_HapticDirection* const outDirection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDirection == nullptr) {
            return InvalidInput("The haptic direction output is null.");
        }
        *outDirection = MapDirection(HapticDirectionEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_direction_equals(
    const CNA_HapticDirection* const left,
    const CNA_HapticDirection* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outEqual == nullptr) {
            return InvalidInput("The haptic direction comparison argument is null.");
        }
        HapticDirectionEXT nativeLeft;
        HapticDirectionEXT nativeRight;
        if (const CNA_Result result = ToDirection(*left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToDirection(*right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_effect_init(CNA_HapticEffect* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidInput("The haptic effect output is null.");
        }
        const HapticEffectEXT nativeEffect;
        CNA_HapticEffect effect = {};
        effect.struct_size = sizeof(CNA_HapticEffect);
        effect.struct_version = StructureVersion;
        effect.type = static_cast<CNA_HapticEffectType>(nativeEffect.type);
        effect.direction = MapDirection(nativeEffect.direction);
        effect.length = nativeEffect.length;
        effect.delay = nativeEffect.delay;
        effect.button = nativeEffect.button;
        effect.interval = nativeEffect.interval;
        effect.level = nativeEffect.level;
        effect.period = nativeEffect.period;
        effect.magnitude = nativeEffect.magnitude;
        effect.offset = nativeEffect.offset;
        effect.phase = nativeEffect.phase;
        effect.ramp_start = nativeEffect.rampStart;
        effect.ramp_end = nativeEffect.rampEnd;
        CopyAxes(nativeEffect.rightSaturation, effect.right_saturation);
        CopyAxes(nativeEffect.leftSaturation, effect.left_saturation);
        CopyAxes(nativeEffect.rightCoefficient, effect.right_coefficient);
        CopyAxes(nativeEffect.leftCoefficient, effect.left_coefficient);
        CopyAxes(nativeEffect.deadband, effect.deadband);
        CopyAxes(nativeEffect.center, effect.center);
        effect.large_magnitude = nativeEffect.largeMagnitude;
        effect.small_magnitude = nativeEffect.smallMagnitude;
        effect.custom_channels = nativeEffect.customChannels;
        effect.custom_period = nativeEffect.customPeriod;
        effect.attack_length = nativeEffect.attackLength;
        effect.attack_level = nativeEffect.attackLevel;
        effect.fade_length = nativeEffect.fadeLength;
        effect.fade_level = nativeEffect.fadeLevel;
        *outEffect = effect;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_effect_equals(
    const CNA_HapticEffect* const left,
    const uint16_t* const leftCustomData,
    const uint64_t leftCustomCount,
    const CNA_HapticEffect* const right,
    const uint16_t* const rightCustomData,
    const uint64_t rightCustomCount,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The haptic effect comparison output is null.");
        }
        HapticEffectEXT nativeLeft;
        HapticEffectEXT nativeRight;
        if (const CNA_Result result =
                ToEffect(left, leftCustomData, leftCustomCount, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToEffect(right, rightCustomData, rightCustomCount, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_capabilities_init(CNA_HapticCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCapabilities == nullptr) {
            return InvalidInput("The haptic capabilities output is null.");
        }
        *outCapabilities = MapCapabilities(HapticCapabilitiesEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_capabilities_equals(
    const CNA_HapticCapabilities* const left,
    const CNA_StringView leftName,
    const CNA_HapticCapabilities* const right,
    const CNA_StringView rightName,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The haptic capabilities comparison output is null.");
        }
        if (const CNA_Result result = ValidateVersionedStructure(
                left,
                "The first haptic capabilities structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateVersionedStructure(
                right,
                "The second haptic capabilities structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string leftText;
        std::string rightText;
        if (const CNA_Result result =
                CNA::C::Detail::CopyStringView(leftName, false, &leftText);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The first haptic device name is not valid UTF-8.");
        }
        if (const CNA_Result result =
                CNA::C::Detail::CopyStringView(rightName, false, &rightText);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The second haptic device name is not valid UTF-8.");
        }
        HapticCapabilitiesEXT nativeLeft;
        nativeLeft.isOpen = left->is_open != CNA_FALSE;
        nativeLeft.name = std::move(leftText);
        nativeLeft.features = static_cast<HapticFeatureEXT>(left->features);
        nativeLeft.axisCount = left->axis_count;
        nativeLeft.maxEffects = left->max_effects;
        nativeLeft.maxEffectsPlaying = left->max_effects_playing;
        nativeLeft.rumbleSupported = left->rumble_supported != CNA_FALSE;
        HapticCapabilitiesEXT nativeRight;
        nativeRight.isOpen = right->is_open != CNA_FALSE;
        nativeRight.name = std::move(rightText);
        nativeRight.features = static_cast<HapticFeatureEXT>(right->features);
        nativeRight.axisCount = right->axis_count;
        nativeRight.maxEffects = right->max_effects;
        nativeRight.maxEffectsPlaying = right->max_effects_playing;
        nativeRight.rumbleSupported = right->rumble_supported != CNA_FALSE;
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptics_get_count(const CNA_Handle gameHandle, uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The haptic device count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint32_t>(Haptics::GetHapticsEXT().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptics_get_id_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint32_t* const outId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outId == nullptr) {
            return InvalidInput("The haptic device identifier output is null.");
        }
        HapticInfoEXT info;
        if (const CNA_Result result = BorrowEnumeratedHaptic(gameHandle, index, &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outId = info.id;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptics_get_name_size_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The haptic device name byte-count output is null.");
        }
        HapticInfoEXT info;
        if (const CNA_Result result = BorrowEnumeratedHaptic(gameHandle, index, &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = info.name.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptics_copy_name_at(
    const CNA_Handle gameHandle,
    const uint32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The haptic device name output is invalid.");
        }
        HapticInfoEXT info;
        if (const CNA_Result result = BorrowEnumeratedHaptic(gameHandle, index, &info);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(info.name, destination, capacity, outBytes);
    });
}

CNA_Result cna_haptics_open(
    const CNA_Handle gameHandle,
    const uint32_t id,
    CNA_HapticDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDevice == nullptr) {
            return InvalidInput("The haptic device output is null.");
        }
        *outDevice = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishDevice(Haptics::OpenEXT(id), outDevice);
    });
}

CNA_Result cna_haptics_open_from_joystick(
    const CNA_Handle gameHandle,
    const uint32_t joystickId,
    CNA_HapticDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDevice == nullptr) {
            return InvalidInput("The haptic device output is null.");
        }
        *outDevice = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishDevice(Haptics::OpenFromJoystickEXT(joystickId), outDevice);
    });
}

CNA_Result cna_haptics_open_from_mouse(
    const CNA_Handle gameHandle,
    CNA_HapticDeviceHandle* const outDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDevice == nullptr) {
            return InvalidInput("The haptic device output is null.");
        }
        *outDevice = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishDevice(Haptics::OpenFromMouseEXT(), outDevice);
    });
}

CNA_Result cna_haptics_get_is_joystick_haptic(
    const CNA_Handle gameHandle,
    const uint32_t joystickId,
    CNA_Bool* const outHaptic)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHaptic == nullptr) {
            return InvalidInput("The joystick haptic-capability output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHaptic = Haptics::IsJoystickHapticEXT(joystickId) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptics_get_is_mouse_haptic(
    const CNA_Handle gameHandle,
    CNA_Bool* const outHaptic)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHaptic == nullptr) {
            return InvalidInput("The mouse haptic-capability output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHaptic = Haptics::IsMouseHapticEXT() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_get_is_open(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_Bool* const outOpen)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOpen == nullptr) {
            return InvalidInput("The haptic device open-state output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOpen = device->IsOpenEXT() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_get_name_size(
    const CNA_HapticDeviceHandle deviceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The haptic device name byte-count output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = device->GetNameEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_copy_name(
    const CNA_HapticDeviceHandle deviceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The haptic device name output is invalid.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(device->GetNameEXT(), destination, capacity, outBytes);
    });
}

CNA_Result cna_haptic_device_get_capabilities(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_HapticCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateVersionedStructure(
                outCapabilities,
                "The haptic capabilities output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCapabilities = MapCapabilities(device->GetCapabilitiesEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_get_is_effect_supported(
    const CNA_HapticDeviceHandle deviceHandle,
    const CNA_HapticEffect* const effect,
    const uint16_t* const customData,
    const uint64_t customSampleCount,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The effect-supported output is null.");
        }
        HapticEffectEXT nativeEffect;
        if (const CNA_Result result =
                ToEffect(effect, customData, customSampleCount, &nativeEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = device->IsEffectSupportedEXT(nativeEffect) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_init_rumble(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ApplyDeviceAction(
            deviceHandle,
            &HapticDevice::InitRumbleEXT,
            outApplied,
            "The rumble initialization output is null.");
    });
}

CNA_Result cna_haptic_device_play_rumble(
    const CNA_HapticDeviceHandle deviceHandle,
    const float strength,
    const uint32_t lengthMs,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The rumble playback output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = device->PlayRumbleEXT(strength, lengthMs) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_stop_rumble(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ApplyDeviceAction(
            deviceHandle,
            &HapticDevice::StopRumbleEXT,
            outApplied,
            "The rumble stop output is null.");
    });
}

CNA_Result cna_haptic_device_create_effect(
    const CNA_HapticDeviceHandle deviceHandle,
    const CNA_HapticEffect* const effect,
    const uint16_t* const customData,
    const uint64_t customSampleCount,
    int32_t* const outEffectId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffectId == nullptr) {
            return InvalidInput("The effect identifier output is null.");
        }
        HapticEffectEXT nativeEffect;
        if (const CNA_Result result =
                ToEffect(effect, customData, customSampleCount, &nativeEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEffectId = static_cast<int32_t>(device->CreateEffectEXT(nativeEffect));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_update_effect(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t effectId,
    const CNA_HapticEffect* const effect,
    const uint16_t* const customData,
    const uint64_t customSampleCount,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The effect update output is null.");
        }
        HapticEffectEXT nativeEffect;
        if (const CNA_Result result =
                ToEffect(effect, customData, customSampleCount, &nativeEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = device->UpdateEffectEXT(effectId, nativeEffect) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_run_effect(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t effectId,
    const uint32_t iterations,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The effect playback output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = device->RunEffectEXT(effectId, iterations) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_stop_effect(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t effectId,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The effect stop output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = device->StopEffectEXT(effectId) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_destroy_effect(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t effectId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        device->DestroyEffectEXT(effectId);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_get_effect_status(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t effectId,
    CNA_Bool* const outPlaying)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlaying == nullptr) {
            return InvalidInput("The effect status output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPlaying = device->GetEffectStatusEXT(effectId) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_stop_all_effects(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ApplyDeviceAction(
            deviceHandle,
            &HapticDevice::StopAllEffectsEXT,
            outApplied,
            "The stop-all-effects output is null.");
    });
}

CNA_Result cna_haptic_device_set_gain(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t gain,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The gain output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = device->SetGainEXT(gain) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_set_autocenter(
    const CNA_HapticDeviceHandle deviceHandle,
    const int32_t autocenter,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The autocenter output is null.");
        }
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = device->SetAutocenterEXT(autocenter) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_pause(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ApplyDeviceAction(
            deviceHandle,
            &HapticDevice::PauseEXT,
            outApplied,
            "The pause output is null.");
    });
}

CNA_Result cna_haptic_device_resume(
    const CNA_HapticDeviceHandle deviceHandle,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ApplyDeviceAction(
            deviceHandle,
            &HapticDevice::ResumeEXT,
            outApplied,
            "The resume output is null.");
    });
}

CNA_Result cna_haptic_device_dispose(const CNA_HapticDeviceHandle deviceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        device->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_device_destroy(const CNA_HapticDeviceHandle deviceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<HapticDevice> device;
        if (const CNA_Result result = BorrowDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(deviceHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The haptic device handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}
