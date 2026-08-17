// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

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

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::TryBorrowSignedInGamerQuietly;

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::GamerServices::GameDefaults;
using Microsoft::Xna::Framework::GamerServices::LeaderboardOutcome;
using Microsoft::Xna::Framework::GamerServices::PropertyDictionary;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

[[nodiscard]] CNA_Result BorrowDictionary(
    const CNA_Handle handle,
    std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource>* const outDictionary)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::PropertyDictionary, outDictionary);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned property dictionary handle is invalid for this call.");
}

[[nodiscard]] CNA_Result ToNativeText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (view.data == nullptr && view.byte_length != UINT64_C(0)) {
        return InvalidInput(message);
    }
    outText->assign(
        view.data == nullptr ? "" : view.data,
        static_cast<std::size_t>(view.byte_length));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyPropertyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The property text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the property text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
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
        return InvalidInput("The property dictionary does not hold that key.");
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
    if (const CNA_Result result = ToNativeText(key, "The property key is invalid.", outKey);
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
    if (const CNA_Result result = ToNativeText(key, "The property key is invalid.", outKey);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return BorrowDictionary(handle, outDictionary);
}

[[nodiscard]] CNA_Result BorrowSignedIn(const CNA_Handle handle, SignedInGamer** const outGamer)
{
    if (const CNA_Result result = TryBorrowSignedInGamerQuietly(handle, outGamer);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned SignedInGamer handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
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
        primary.has_value() ? CNA_TRUE : CNA_FALSE,
        secondary.has_value() ? CNA_TRUE : CNA_FALSE,
        defaults.getAutoAimProperty() ? CNA_TRUE : CNA_FALSE,
        defaults.getAutoCenterProperty() ? CNA_TRUE : CNA_FALSE,
        defaults.getMoveWithRightThumbStickProperty() ? CNA_TRUE : CNA_FALSE,
        defaults.getInvertYAxisProperty() ? CNA_TRUE : CNA_FALSE,
        defaults.getManualTransmissionProperty() ? CNA_TRUE : CNA_FALSE,
        defaults.getAccelerateWithButtonsProperty() ? CNA_TRUE : CNA_FALSE,
        defaults.getBrakeWithButtonsProperty() ? CNA_TRUE : CNA_FALSE,
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
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The property dictionary factory arguments are invalid.");
    }
    *outDictionary = CNA_INVALID_HANDLE;
    const auto resource = std::make_shared<PropertyDictionaryResource>();
    resource->value = std::move(value);
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::PropertyDictionary, resource, outDictionary);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned property dictionary handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

CNA_Result cna_game_defaults_init(CNA_GameDefaults* const outDefaults)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDefaults == nullptr) {
            return InvalidInput("The GameDefaults output is null.");
        }
        *outDefaults = ToC(GameDefaults::CreateInternal());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_game_defaults(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_GameDefaults* const outDefaults)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDefaults == nullptr || outDefaults->struct_size < sizeof(CNA_GameDefaults) ||
            outDefaults->struct_version != StructureVersion) {
            return InvalidInput("The GameDefaults output structure is invalid.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDefaults = ToC(gamer->getGameDefaultsProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_create_ext(CNA_PropertyDictionaryHandle* const outDictionary)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDictionary == nullptr) {
            return InvalidInput("The property dictionary output handle is null.");
        }
        // The only constructor is private; the canonical factory with an empty map is how an empty
        // dictionary is made, and the typed setters fill it from there.
        return CNA::C::Detail::CreateOwnedPropertyDictionary(
            std::make_shared<PropertyDictionary>(PropertyDictionary::CreateInternal({})),
            outDictionary);
    });
}

CNA_Result cna_property_dictionary_destroy(const CNA_PropertyDictionaryHandle dictionaryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(dictionaryHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned property dictionary handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_count(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The property count output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(dictionary->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_is_read_only(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    CNA_Bool* const outIsReadOnly)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsReadOnly == nullptr) {
            return InvalidInput("The read-only output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsReadOnly = dictionary->value->getIsReadOnlyProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_contains_key(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidInput("The containment output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = dictionary->value->ContainsKey(nativeKey) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_try_get_value_kind_ext(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_Bool* const outFound,
    CNA_PropertyValueKind* const outKind)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outKind == nullptr) {
            return InvalidInput("The property kind output is invalid.");
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

CNA_Result cna_property_dictionary_get_date_time_ticks(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_DATE_TIME,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks =
            static_cast<int64_t>(dictionary->value->GetValueDateTime(nativeKey).getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_double(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    double* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_DOUBLE,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = dictionary->value->GetValueDouble(nativeKey);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_int32(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_INT32,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(dictionary->value->GetValueInt32(nativeKey));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_int64(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    int64_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_INT64,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int64_t>(dictionary->value->GetValueInt64(nativeKey));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_outcome(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_LeaderboardOutcome* const outOutcome)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOutcome == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_OUTCOME,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOutcome =
            static_cast<CNA_LeaderboardOutcome>(dictionary->value->GetValueOutcome(nativeKey));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_get_single(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_SINGLE,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = dictionary->value->GetValueSingle(nativeKey);
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
            return InvalidInput("The property stream output is invalid.");
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

CNA_Result cna_property_dictionary_get_string_size(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_STRING,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = dictionary->value->GetValueString(nativeKey).size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_copy_string(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_STRING,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyPropertyText(
            dictionary->value->GetValueString(nativeKey),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_property_dictionary_get_time_span_ticks(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The property value output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result = BorrowForRead(
                dictionaryHandle,
                key,
                CNA_PROPERTY_VALUE_KIND_TIME_SPAN,
                &dictionary,
                &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks =
            static_cast<int64_t>(dictionary->value->GetValueTimeSpan(nativeKey).getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_date_time_ticks(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, System::DateTime(ticks));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_double(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const double value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_int32(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_int64(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const int64_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, static_cast<long long>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_outcome(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const CNA_LeaderboardOutcome outcome)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outcome > CNA_LEADERBOARD_OUTCOME_MAXIMUM) {
            return InvalidInput("The leaderboard outcome is not a defined identity.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, static_cast<LeaderboardOutcome>(outcome));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_single(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_string(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const CNA_StringView value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        std::string nativeValue;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToNativeText(value, "The property text is invalid.", &nativeValue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, nativeValue);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_set_time_span_ticks(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->SetValue(nativeKey, System::TimeSpan(ticks));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_remove(
    const CNA_PropertyDictionaryHandle dictionaryHandle,
    const CNA_StringView key,
    CNA_Bool* const outRemoved)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRemoved == nullptr) {
            return InvalidInput("The removal output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        std::string nativeKey;
        if (const CNA_Result result =
                BorrowForWrite(dictionaryHandle, key, &dictionary, &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRemoved = dictionary->value->Remove(nativeKey) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_property_dictionary_clear(const CNA_PropertyDictionaryHandle dictionaryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dictionary->value->Clear();
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
            return InvalidInput("The key size output is null.");
        }
        std::shared_ptr<CNA::C::Detail::PropertyDictionaryResource> dictionary;
        if (const CNA_Result result = BorrowDictionary(dictionaryHandle, &dictionary);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<std::string> keys = dictionary->value->Keys();
        if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) {
            return InvalidInput("The key index is outside the property dictionary.");
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
            return InvalidInput("The key index is outside the property dictionary.");
        }
        return CopyPropertyText(
            keys[static_cast<std::size_t>(index)],
            destination,
            capacity,
            outBytes);
    });
}
