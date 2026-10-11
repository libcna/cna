// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"

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

using CNA::C::Detail::BorrowSignedIn;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReadTextBytes;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::GamerServices::Achievement;
using Microsoft::Xna::Framework::GamerServices::AchievementCollection;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

// An achievement is a value: the collection stores copies and equality compares every field, so a
// handle never has to be the same object the collection holds -- only equal to it.
struct AchievementResource final {
    std::shared_ptr<Achievement> value;
};

struct AchievementCollectionResource final {
    std::shared_ptr<AchievementCollection> value;
};

[[nodiscard]] CNA_Result BorrowAchievement(
    const CNA_Handle handle,
    std::shared_ptr<AchievementResource>* const outAchievement)
{
    return LookupHandle(handle, ObjectKind::Achievement, outAchievement);
}

[[nodiscard]] CNA_Result BorrowCollection(
    const CNA_Handle handle,
    std::shared_ptr<AchievementCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::AchievementCollection, outCollection);
}

[[nodiscard]] CNA_Result PublishAchievement(Achievement value, CNA_Handle* const outAchievement)
{
    const auto resource = std::make_shared<AchievementResource>();
    resource->value = std::make_shared<Achievement>(std::move(value));
    return CreateHandle(ObjectKind::Achievement, resource, outAchievement);
}

[[nodiscard]] CNA_Result PublishCollection(
    AchievementCollection value,
    CNA_Handle* const outCollection)
{
    const auto resource = std::make_shared<AchievementCollectionResource>();
    resource->value = std::make_shared<AchievementCollection>(std::move(value));
    return CreateHandle(ObjectKind::AchievementCollection, resource, outCollection);
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
            return InvalidArgument("The Achievement output handle is null.");
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
        if (const CNA_Result result = ReadTextBytes(key, "The achievement key is invalid.", &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ReadTextBytes(name, "The achievement name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ReadTextBytes(description, "The achievement description is invalid.", &nativeDescription);
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

CNA_Result cna_achievement_get_picture_size(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The achievement picture size output is null.");
        }
        std::shared_ptr<AchievementResource> achievement;
        if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::unique_ptr<System::IO::Stream> picture(achievement->value->GetPicture());
        *outBytes = picture == nullptr ? UINT64_C(0)
                                       : static_cast<uint64_t>(picture->getLengthProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_achievement_copy_picture(const CNA_AchievementHandle handle,uint8_t* destination,uint64_t capacity,uint64_t* outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if(!outBytes||(!destination&&capacity))return InvalidArgument("The picture output is invalid.");
        std::shared_ptr<AchievementResource> achievement;
        if(const auto result=BorrowAchievement(handle,&achievement);result!=CNA_RESULT_SUCCESS)return result;
        std::unique_ptr<System::IO::Stream> picture(achievement->value->GetPicture());
        return CNA::C::Detail::CopyPictureStream(picture.get(),destination,capacity,outBytes);
    });
}

CNA_Result cna_achievement_collection_create_ext(
    const CNA_AchievementHandle* const achievements,
    const uint64_t count,
    CNA_AchievementCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The achievement collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (achievements == nullptr && count != UINT64_C(0)) {
            return InvalidArgument("The achievement array is null.");
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

CNA_Result cna_achievement_collection_get_by_key(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_StringView key,
    CNA_AchievementHandle* const outAchievement)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAchievement == nullptr) {
            return InvalidArgument("The Achievement output handle is null.");
        }
        *outAchievement = CNA_INVALID_HANDLE;
        std::string nativeKey;
        if (const CNA_Result result = ReadTextBytes(key, "The achievement key is invalid.", &nativeKey);
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

CNA_Result cna_achievement_collection_copy_to(
    const CNA_AchievementCollectionHandle collectionHandle,
    CNA_AchievementHandle* const destination,
    const uint64_t capacity,
    const int32_t index,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidArgument("The collection copy output is invalid.");
        }
        if (index < 0) {
            return InvalidArgument("The destination index is negative.");
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

// The mechanically regular routes of this family, generated from modules/c-api/spec/Achievements.toml.
#include "generated/Achievements.inc"
