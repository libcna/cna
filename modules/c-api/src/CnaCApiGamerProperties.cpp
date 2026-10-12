// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardOutcome.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"

#include <any>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using CNA::C::Detail::BorrowSignedIn;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::GamerServices::GameDefaults;
using Microsoft::Xna::Framework::GamerServices::LeaderboardOutcome;
using Microsoft::Xna::Framework::GamerServices::PropertyDictionary;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

[[nodiscard]] CNA_Result BorrowDictionary(
    const CNA_Handle handle,
    std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource>* const outDictionary)
{
    return LookupHandle(handle, ObjectKind::PropertyDictionary, outDictionary);
}

// The canonical dictionary stores a boxed value; this is what turns the box back into something a C
// caller can name before choosing a getter.
[[nodiscard]] CNA_PropertyValueKind KindOf(const std::any& value) noexcept
{
    if (value.type() == typeid(System::DateTime)) {
        return CNA_PROPERTY_VALUE_KIND_DATE_TIME;
    }
    if (value.type() == typeid(double)) {
        return CNA_PROPERTY_VALUE_KIND_DOUBLE;
    }
    if (value.type() == typeid(int)) {
        return CNA_PROPERTY_VALUE_KIND_INT32;
    }
    if (value.type() == typeid(long long)) {
        return CNA_PROPERTY_VALUE_KIND_INT64;
    }
    if (value.type() == typeid(LeaderboardOutcome)) {
        return CNA_PROPERTY_VALUE_KIND_OUTCOME;
    }
    if (value.type() == typeid(float)) {
        return CNA_PROPERTY_VALUE_KIND_SINGLE;
    }
    if (value.type() == typeid(System::IO::Stream*)) {
        return CNA_PROPERTY_VALUE_KIND_STREAM;
    }
    if (value.type() == typeid(std::string)) {
        return CNA_PROPERTY_VALUE_KIND_STRING;
    }
    if (value.type() == typeid(System::TimeSpan)) {
        return CNA_PROPERTY_VALUE_KIND_TIME_SPAN;
    }
    return CNA_PROPERTY_VALUE_KIND_UNKNOWN;
}

// Every typed getter validates the slot's kind here rather than letting the canonical unboxing throw:
// a caller can act on "that key holds something else", and cannot act on a generic internal failure.
[[nodiscard]] CNA_Result RequireKind(
    const PropertyDictionary& dictionary,
    const std::string& key,
    const CNA_PropertyValueKind expectedKind)
{
    std::any value;
    if (!dictionary.TryGetValue(key, value)) {
        return InvalidArgument("The property dictionary does not hold that key.");
    }
    if (KindOf(value) != expectedKind) {
        return InvalidState("The property holds a value of a different kind.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowForRead(
    const CNA_Handle handle,
    const CNA_StringView key,
    const CNA_PropertyValueKind expectedKind,
    std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource>* const outDictionary,
    std::string* const outKey)
{
    if (const CNA_Result result = ReadText(key, "The property key is invalid.", outKey);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = BorrowDictionary(handle, outDictionary);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return RequireKind(*(*outDictionary)->value, *outKey, expectedKind);
}

[[nodiscard]] CNA_Result BorrowForWrite(
    const CNA_Handle handle,
    const CNA_StringView key,
    std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource>* const outDictionary,
    std::string* const outKey)
{
    if (const CNA_Result result = ReadText(key, "The property key is invalid.", outKey);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return BorrowDictionary(handle, outDictionary);
}

[[nodiscard]] CNA_GameDefaults ToC(const GameDefaults& defaults) noexcept
{
    const std::optional<Color> primary = defaults.getPrimaryColorProperty();
    const std::optional<Color> secondary = defaults.getSecondaryColorProperty();
    CNA_GameDefaults value = {
        sizeof(CNA_GameDefaults),
        StructureVersion,
        static_cast<CNA_GameDifficulty>(defaults.getGameDifficultyProperty()),
        static_cast<CNA_ControllerSensitivity>(defaults.getControllerSensitivityProperty()),
        static_cast<CNA_RacingCameraAngle>(defaults.getRacingCameraAngleProperty()),
        ToCBool(primary.has_value()),
        ToCBool(secondary.has_value()),
        ToCBool(defaults.getAutoAimProperty()),
        ToCBool(defaults.getAutoCenterProperty()),
        ToCBool(defaults.getMoveWithRightThumbStickProperty()),
        ToCBool(defaults.getInvertYAxisProperty()),
        ToCBool(defaults.getManualTransmissionProperty()),
        ToCBool(defaults.getAccelerateWithButtonsProperty()),
        ToCBool(defaults.getBrakeWithButtonsProperty()),
        {0U, 0U, 0U},
        {0U, 0U, 0U, 0U},
        {0U, 0U, 0U, 0U}
    };
    if (primary.has_value()) {
        value.primary_color.r = primary->getRProperty();
        value.primary_color.g = primary->getGProperty();
        value.primary_color.b = primary->getBProperty();
        value.primary_color.a = primary->getAProperty();
    }
    if (secondary.has_value()) {
        value.secondary_color.r = secondary->getRProperty();
        value.secondary_color.g = secondary->getGProperty();
        value.secondary_color.b = secondary->getBProperty();
        value.secondary_color.a = secondary->getAProperty();
    }
    return value;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result CreateOwnedPropertyDictionary(
    std::shared_ptr<Microsoft::Xna::Framework::GamerServices::PropertyDictionary> value,
    CNA_Handle* const outDictionary)
{
    if (value == nullptr || outDictionary == nullptr) {
        return InvalidArgument("The property dictionary factory arguments are invalid.");
    }
    *outDictionary = CNA_INVALID_HANDLE;
    return CreateResourceHandle<PropertyDictionaryResource>(
        ObjectKind::PropertyDictionary, std::move(value), outDictionary);
}

} // namespace CNA::C::Detail

CNA_Result cna_property_dictionary_try_get_value_kind_ext(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_Bool* const outFound,
    CNA_PropertyValueKind* const outKind)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outKind == nullptr) {
            return InvalidArgument("The property kind output is invalid.");
        }
        *outFound = CNA_FALSE;
        *outKind = CNA_PROPERTY_VALUE_KIND_UNKNOWN;
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::any value;
        if (!dictionary->value->TryGetValue(nativeKey, value)) {
            return CNA_RESULT_SUCCESS;
        }
        *outFound = CNA_TRUE;
        *outKind = KindOf(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_stream_size_ext(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_Bool* const outHasStream,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasStream == nullptr || outBytes == nullptr) {
            return InvalidArgument("The property stream output is invalid.");
        }
        *outHasStream = CNA_FALSE;
        *outBytes = UINT64_C(0);
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_STREAM,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IO::Stream* const stream = dictionary->value->GetValueStream(nativeKey);
        if (stream == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        *outHasStream = CNA_TRUE;
        *outBytes = static_cast<uint64_t>(stream->getLengthProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_key_size_at(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const int32_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The key size output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> keys = dictionary->value->Keys();
        if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) {
            return InvalidArgument("The key index is outside the property dictionary.");
        }
        *outBytes = keys[static_cast<std::size_t>(index)].size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_copy_key_at(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const int32_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> keys = dictionary->value->Keys();
        if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) {
            return InvalidArgument("The key index is outside the property dictionary.");
        }
        return CopyText(
            keys[static_cast<std::size_t>(index)],
            destination,
            capacity,
            outBytes);
    });
}

// The routes generated from modules/c-api/spec/GamerProperties.toml.
#include "generated/GamerProperties.inc"
