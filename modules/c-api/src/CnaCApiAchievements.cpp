// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/DateTime.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowSignedIn;
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CompleteAtOnce;
using CNA::C::Detail::CopyPictureStream;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Detail::ValidateCopyOutput;
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
    return CreateResourceHandle<AchievementResource>(
        ObjectKind::Achievement, std::make_shared<Achievement>(std::move(value)), outAchievement);
}

[[nodiscard]] CNA_Result PublishCollection(
    AchievementCollection value,
    CNA_Handle* const outCollection)
{
    return CreateResourceHandle<AchievementCollectionResource>(
        ObjectKind::AchievementCollection,
        std::make_shared<AchievementCollection>(std::move(value)),
        outCollection);
}

[[nodiscard]] CNA_Result AchievementCreateExt(
    const CNA_StringView key,
    const CNA_StringView name,
    const CNA_StringView description,
    const CNA_Bool displayBeforeEarned,
    const CNA_Bool isEarned,
    const int64_t earnedDateTimeTicks,
    CNA_AchievementHandle* const outAchievement)
{
    std::string nativeKey;
    std::string nativeName;
    std::string nativeDescription;
    if (const CNA_Result result = ReadText(key, "The achievement key is invalid.", &nativeKey);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ReadText(name, "The achievement name is invalid.", &nativeName);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ReadText(
            description, "The achievement description is invalid.", &nativeDescription);
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
}

[[nodiscard]] CNA_Result AchievementGetPictureSize(
    const CNA_AchievementHandle achievementHandle,
    uint64_t* const outBytes)
{
    std::shared_ptr<AchievementResource> achievement;
    if (const CNA_Result result = BorrowAchievement(achievementHandle, &achievement);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::unique_ptr<System::IO::Stream> picture(achievement->value->GetPicture());
    *outBytes = picture == nullptr ? UINT64_C(0)
                                   : static_cast<uint64_t>(picture->getLengthProperty());
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AchievementCollectionCreateExt(
    const CNA_AchievementHandle* const achievements,
    const uint64_t count,
    CNA_AchievementCollectionHandle* const outCollection)
{
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
}

[[nodiscard]] CNA_Result AchievementCollectionGetByKey(
    const CNA_AchievementCollectionHandle collectionHandle,
    const CNA_StringView key,
    CNA_AchievementHandle* const outAchievement)
{
    std::string nativeKey;
    if (const CNA_Result result = ReadText(key, "The achievement key is invalid.", &nativeKey);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<AchievementCollectionResource> collection;
    if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const Achievement* value;
    try {
        value = &(*collection->value)[nativeKey];
    } catch (const System::Collections::Generic::KeyNotFoundException&) {
        return InvalidArgument("No achievement has the requested key.");
    }
    return PublishAchievement(*value, outAchievement);
}

[[nodiscard]] CNA_Result AchievementCollectionInsert(
    const CNA_AchievementCollectionHandle collectionHandle,
    const int32_t index,
    const CNA_AchievementHandle achievementHandle)
{
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
}

[[nodiscard]] CNA_Result AchievementCollectionCopyTo(
    const CNA_AchievementCollectionHandle collectionHandle,
    CNA_AchievementHandle* const destination,
    const uint64_t capacity,
    const int32_t index,
    uint64_t* const outCount)
{
    if ((destination == nullptr && capacity != UINT64_C(0))) {
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
        return BufferTooSmall("The destination capacity is smaller than the collection.");
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
}

} // namespace

// The routes generated from modules/c-api/spec/Achievements.toml.
#include "generated/Achievements.inc"
