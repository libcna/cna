// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/DateTime.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::TryBorrowSignedInGamerQuietly;
using CNA::C::Detail::ValidateCanonicalBool;

using Microsoft::Xna::Framework::GamerServices::Achievement;
using Microsoft::Xna::Framework::GamerServices::AchievementCollection;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

constexpr uint32_t StructureVersion = UINT32_C(1);

// An achievement is a value: the collection stores copies and equality compares every field, so a
// handle never has to be the same object the collection holds -- only equal to it.
struct AchievementResource final {
    std::shared_ptr<Achievement> value;
};

struct AchievementCollectionResource final {
    std::shared_ptr<AchievementCollection> value;
};

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result BorrowAchievement(
    const CNA_Handle handle,
    std::shared_ptr<AchievementResource>* const outAchievement)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::Achievement, outAchievement);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned Achievement handle is invalid for this call.");
}

[[nodiscard]] CNA_Result BorrowCollection(
    const CNA_Handle handle,
    std::shared_ptr<AchievementCollectionResource>* const outCollection)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::AchievementCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned achievement collection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CopyAchievementText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The achievement text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the achievement text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
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

[[nodiscard]] CNA_Result PublishAchievement(Achievement value, CNA_Handle* const outAchievement)
{
    const auto resource = std::make_shared<AchievementResource>();
    resource->value = std::make_shared<Achievement>(std::move(value));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::Achievement, resource, outAchievement);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Achievement handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishCollection(
    AchievementCollection value,
    CNA_Handle* const outCollection)
{
    const auto resource = std::make_shared<AchievementCollectionResource>();
    resource->value = std::make_shared<AchievementCollection>(std::move(value));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::AchievementCollection, resource, outCollection);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned achievement collection handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
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

} // namespace

CNA_Result cna_achievement_create_ext(
    const CNA_StringView key,
    const CNA_StringView name,
    const CNA_StringView description,
    const CNA_Bool displayBeforeEarned,
    const CNA_Bool isEarned,
    const int64_t earnedDateTimeTicks,
    CNA_AchievementHandle* const outAchievement)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAchievement == nullptr) {
            return InvalidInput("The Achievement output handle is null.");
        }
        *outAchievement = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateCanonicalBool(displayBeforeEarned, "display_before_earned");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateCanonicalBool(isEarned, "is_earned");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeKey;
        std::string nativeName;
        std::string nativeDescription;
        if (const CNA_Result result = ToNativeText(key, "The achievement key is invalid.", &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToNativeText(name, "The achievement name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToNativeText(description, "The achievement description is invalid.", &nativeDescription);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishAchievement(
            Achievement::CreateInternal(
                nativeKey,
                nativeName,
                nativeDescription,
                displayBeforeEarned != CNA_FALSE,
                isEarned != CNA_FALSE,
                System::DateTime(earnedDateTimeTicks)),
            outAchievement);
    });
}

CNA_Result cna_achievement_destroy(const CNA_AchievementHandle achievementHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(achievementHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Achievement handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_get_info(
    const CNA_AchievementHandle achievementHandle,
    CNA_AchievementInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_AchievementInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The Achievement info output structure is invalid.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_AchievementInfo info = {
            sizeof(CNA_AchievementInfo),
            StructureVersion,
            static_cast<int32_t>(achievement->value->getGamerScoreProperty()),
            achievement->value->getDisplayBeforeEarnedProperty() ? CNA_TRUE : CNA_FALSE,
            achievement->value->getEarnedOnlineProperty() ? CNA_TRUE : CNA_FALSE,
            achievement->value->getIsEarnedProperty() ? CNA_TRUE : CNA_FALSE,
            0U,
            static_cast<int64_t>(achievement->value->getEarnedDateTimeProperty().getTicksProperty())
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_get_key_size(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The achievement key size output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = achievement->value->getKeyProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_copy_key(
    const CNA_AchievementHandle achievementHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAchievementText(
            achievement->value->getKeyProperty(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_achievement_get_name_size(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The achievement name size output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = achievement->value->getNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_copy_name(
    const CNA_AchievementHandle achievementHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAchievementText(
            achievement->value->getNameProperty(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_achievement_get_description_size(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The achievement description size output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = achievement->value->getDescriptionProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_copy_description(
    const CNA_AchievementHandle achievementHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAchievementText(
            achievement->value->getDescriptionProperty(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_achievement_get_how_to_earn_size(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The achievement how-to-earn size output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = achievement->value->getHowToEarnProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_copy_how_to_earn(
    const CNA_AchievementHandle achievementHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAchievementText(
            achievement->value->getHowToEarnProperty(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_achievement_get_picture_size(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The achievement picture size output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical accessor says outright that it is not implemented, so the refusal that
        // reaches a C caller is the canonical one rather than an empty answer invented here.
        System::IO::Stream* const picture = achievement->value->GetPicture();
        *outBytes = picture == nullptr ? UINT64_C(0)
                                       : static_cast<uint64_t>(picture->getLengthProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_equals(
    const CNA_AchievementHandle achievementHandle,
    const CNA_AchievementHandle otherHandle,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The achievement equality output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        std::shared_ptr<AchievementResource> other;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAchievement(otherHandle, &other);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // Both canonical equality operators are this one answer; inequality is its negation.
        *outEquals = (*achievement->value == *other->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_create_ext(
    const CNA_AchievementHandle* const achievements,
    const uint64_t count,
    CNA_AchievementCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidInput("The achievement collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (achievements == nullptr && count != UINT64_C(0)) {
            return InvalidInput("The achievement array is null.");
        }
        std::vector<Achievement> values;
        values.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = UINT64_C(0); index < count; ++index) {
            std::shared_ptr<AchievementResource> achievement;
            if (const CNA_Result result = BorrowAchievement(achievements[index], &achievement);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            values.push_back(*achievement->value);
        }
        return PublishCollection(
            AchievementCollection::CreateInternal(std::move(values)),
            outCollection);
    });
}

CNA_Result cna_achievement_collection_destroy(
    const CNA_AchievementCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned achievement collection handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_get_count(
    const CNA_AchievementCollectionHandle collectionHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The collection count output is null.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(collection->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_get_is_disposed(
    const CNA_AchievementCollectionHandle collectionHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return InvalidInput("The collection disposal output is null.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = collection->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_get_is_read_only(
    const CNA_AchievementCollectionHandle collectionHandle,
    CNA_Bool* const outIsReadOnly)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsReadOnly == nullptr) {
            return InvalidInput("The collection read-only output is null.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsReadOnly = collection->value->getIsReadOnlyProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_get_at(
    const CNA_AchievementCollectionHandle collectionHandle,
    const int32_t index,
    CNA_AchievementHandle* const outAchievement)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAchievement == nullptr) {
            return InvalidInput("The Achievement output handle is null.");
        }
        *outAchievement = CNA_INVALID_HANDLE;
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical indexer validates the range and throws; letting it do so keeps the rule in
        // one place. The copy is deliberate: the reference it answers points into storage that a
        // later insert or remove would invalidate.
        return PublishAchievement((*collection->value)[static_cast<int>(index)], outAchievement);
    });
}

CNA_Result cna_achievement_collection_get_by_key(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_StringView key,
    CNA_AchievementHandle* const outAchievement)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAchievement == nullptr) {
            return InvalidInput("The Achievement output handle is null.");
        }
        *outAchievement = CNA_INVALID_HANDLE;
        std::string nativeKey;
        if (const CNA_Result result = ToNativeText(key, "The achievement key is invalid.", &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishAchievement((*collection->value)[nativeKey], outAchievement);
    });
}

CNA_Result cna_achievement_collection_index_of(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_AchievementHandle achievementHandle,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidInput("The collection index output is null.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(collection->value->IndexOf(*achievement->value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_contains(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_AchievementHandle achievementHandle,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidInput("The collection containment output is null.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = collection->value->Contains(*achievement->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_add(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_AchievementHandle achievementHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementCollectionResource> collection;
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Add(*achievement->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_insert(
    const CNA_AchievementCollectionHandle collectionHandle,
    const int32_t index,
    const CNA_AchievementHandle achievementHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementCollectionResource> collection;
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Insert(static_cast<int>(index), *achievement->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_remove_at(
    const CNA_AchievementCollectionHandle collectionHandle,
    const int32_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->RemoveAt(static_cast<int>(index));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_remove(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_AchievementHandle achievementHandle,
    CNA_Bool* const outRemoved)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRemoved == nullptr) {
            return InvalidInput("The collection removal output is null.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRemoved = collection->value->Remove(*achievement->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_clear(
    const CNA_AchievementCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_collection_copy_to(
    const CNA_AchievementCollectionHandle collectionHandle,
    CNA_AchievementHandle* const destination,
    const uint64_t capacity,
    const int32_t index,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The collection copy output is invalid.");
        }
        if (index < 0) {
            return InvalidInput("The destination index is negative.");
        }
        std::shared_ptr<AchievementCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical copy writes into slots that already exist and an achievement has no default
        // constructor, so the destination is filled from the indexer first and then overwritten by
        // the canonical copy -- which is what actually gets exercised.
        const int count = collection->value->getCountProperty();
        std::vector<Achievement> values;
        values.reserve(static_cast<std::size_t>(count));
        for (int slot = 0; slot < count; ++slot) {
            values.push_back((*collection->value)[slot]);
        }
        collection->value->CopyTo(values, 0);
        *outCount = static_cast<uint64_t>(values.size());
        if (capacity < static_cast<uint64_t>(index) + values.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the collection.");
        }
        for (std::size_t slot = 0U; slot < values.size(); ++slot) {
            CNA_Handle published = CNA_INVALID_HANDLE;
            if (const CNA_Result result = PublishAchievement(values[slot], &published);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            destination[static_cast<uint64_t>(index) + static_cast<uint64_t>(slot)] = published;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_achievements(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_AchievementCollectionHandle* const outAchievements)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAchievements == nullptr) {
            return InvalidInput("The achievement collection output handle is null.");
        }
        *outAchievements = CNA_INVALID_HANDLE;
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishCollection(gamer->GetAchievements(), outAchievements);
    });
}

CNA_Result cna_signed_in_gamer_begin_get_achievements(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_AchievementCollectionHandle* const outAchievements)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                cna_signed_in_gamer_get_achievements(gamerHandle, outAchievements);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}
