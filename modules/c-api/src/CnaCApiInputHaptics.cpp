// SPDX-License-Identifier: MS-PL

#include "CNA/C/input_haptics.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiOutput.hpp"
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
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;

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

[[nodiscard]] CNA_Result ValidateSampleBuffer(
    const uint16_t* const data,
    const uint64_t count)
{
    if (data == nullptr && count != UINT64_C(0)) {
        return InvalidArgument("The custom sample buffer is null.");
    }
    if (count > UINT64_C(0x00FFFFFF)) {
        return InvalidArgument("The custom sample count is implausibly large.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToDirection(
    const CNA_HapticDirection& direction,
    HapticDirectionEXT* const outDirection)
{
    if (direction.type > CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM) {
        return InvalidArgument("The haptic direction type is undefined.");
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
        return InvalidArgument("The haptic effect type is undefined.");
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
    mapped.is_open = ToCBool(capabilities.isOpen);
    mapped.rumble_supported = ToCBool(capabilities.rumbleSupported);
    return mapped;
}

[[nodiscard]] CNA_Result BorrowDevice(
    const CNA_HapticDeviceHandle handle,
    std::shared_ptr<HapticDevice>* const outDevice)
{
    const CNA_Result result = LookupHandle(handle, CNA::C::Detail::ObjectKind::HapticDevice, outDevice);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishDevice(
    HapticDevice&& device,
    CNA_HapticDeviceHandle* const outDevice)
{
    auto resource = std::make_shared<HapticDevice>(std::move(device));
    return CreateHandle(CNA::C::Detail::ObjectKind::HapticDevice,
        std::move(resource),
        outDevice);
}

using DeviceAction = bool (HapticDevice::*)();

[[nodiscard]] CNA_Result ApplyDeviceAction(
    const CNA_HapticDeviceHandle handle,
    const DeviceAction action,
    CNA_Bool* const outApplied,
    const char* const message)
{
    if (outApplied == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<HapticDevice> device;
    if (const CNA_Result result = BorrowDevice(handle, &device);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outApplied = ToCBool(((*device).*action)());
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
        return InvalidArgument("The haptic device index is out of range.");
    }
    *outInfo = haptics[index];
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_haptic_direction_equals(
    const CNA_HapticDirection* const left,
    const CNA_HapticDirection* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outEqual == nullptr) {
            return InvalidArgument("The haptic direction comparison argument is null.");
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
        *outEqual = ToCBool(nativeLeft == nativeRight);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_haptic_effect_init(CNA_HapticEffect* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return InvalidArgument("The haptic effect output is null.");
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
            return InvalidArgument("The haptic effect comparison output is null.");
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
        *outEqual = ToCBool(nativeLeft == nativeRight);
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
            return InvalidArgument("The haptic capabilities comparison output is null.");
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
        *outEqual = ToCBool(nativeLeft == nativeRight);
        return CNA_RESULT_SUCCESS;
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
            return InvalidArgument("The effect-supported output is null.");
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
        *outSupported = ToCBool(device->IsEffectSupportedEXT(nativeEffect));
        return CNA_RESULT_SUCCESS;
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
            return InvalidArgument("The effect identifier output is null.");
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
            return InvalidArgument("The effect update output is null.");
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
        *outApplied = ToCBool(device->UpdateEffectEXT(effectId, nativeEffect));
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/InputHaptics.toml.
#include "generated/InputHaptics.inc"
