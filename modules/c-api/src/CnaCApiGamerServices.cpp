// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

struct SignedInGamerResource final {
    std::shared_ptr<SignedInGamer> value;
    bool published = false;
};

// The canonical collection holds non-owning pointers and its setter frees only the previous
// wrapper, so the C layer keeps the published resources alive itself and drops them when the
// collection is replaced.
std::vector<std::shared_ptr<SignedInGamerResource>>& PublishedGamers()
{
    static std::vector<std::shared_ptr<SignedInGamerResource>> published;
    return published;
}

[[nodiscard]] CNA_Result GetSignedInGamer(
    const CNA_Handle handle,
    std::shared_ptr<SignedInGamerResource>* const outGamer)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::SignedInGamer, outGamer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned SignedInGamer handle is invalid for this call.");
}

// The two avatar name functions are pure value operations: no gamer, no handle, no thread affinity.
// They validate the identity here rather than letting the canonical `ArgumentException` reach the
// firewall, so an undefined identity is refused with a message that names the identity.
[[nodiscard]] CNA_Result CopyAvatarName(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The avatar name output buffer is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The avatar name output buffer is too small.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool TryMapAnimationPreset(
    const CNA_AvatarAnimationPreset preset,
    AvatarAnimationPreset* const outPreset) noexcept
{
    if (preset > CNA_AVATAR_ANIMATION_PRESET_MAXIMUM) {
        return false;
    }
    *outPreset = static_cast<AvatarAnimationPreset>(preset);
    return true;
}

[[nodiscard]] bool TryMapBodyType(
    const CNA_AvatarBodyType bodyType,
    AvatarBodyType* const outBodyType) noexcept
{
    if (bodyType > CNA_AVATAR_BODY_TYPE_MAXIMUM) {
        return false;
    }
    *outBodyType = static_cast<AvatarBodyType>(bodyType);
    return true;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result BorrowSignedInGamer(const CNA_Handle handle, SignedInGamer** const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The borrowed SignedInGamer output is null.");
    }
    *outGamer = nullptr;
    std::shared_ptr<SignedInGamerResource> gamer;
    if (const CNA_Result result = GetSignedInGamer(handle, &gamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outGamer = gamer->value.get();
    return CNA_RESULT_SUCCESS;
}

CNA_Result CreateBorrowedSignedInGamer(SignedInGamer* const value, CNA_Handle* const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The SignedInGamer output handle is null.");
    }
    *outGamer = CNA_INVALID_HANDLE;
    if (value == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    const auto resource = std::make_shared<SignedInGamerResource>();
    // A borrowed view never owns the canonical object, so the aliasing constructor keeps the
    // pointer without ever deleting it.
    resource->value = std::shared_ptr<SignedInGamer>(std::shared_ptr<void>(), value);
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::SignedInGamer,
        resource,
        outGamer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The borrowed SignedInGamer handle could not be created.");
}

CNA_Result ReleaseBorrowedSignedInGamer(const CNA_Handle handle)
{
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    return GetRuntimeHandles().Release(handle);
}

} // namespace CNA::C::Detail

CNA_Result cna_invite_accepted_event_info_init(
    const CNA_SignedInGamerHandle gamer,
    const CNA_Bool isCurrentSession,
    CNA_InviteAcceptedEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_InviteAcceptedEventInfo) ||
            outInfo->struct_version != UINT32_C(1)) {
            return InvalidArgument("The event description structure is invalid.");
        }
        if (gamer != CNA_INVALID_HANDLE) {
            std::shared_ptr<SignedInGamerResource> resource;
            if (const CNA_Result result = GetSignedInGamer(gamer, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        outInfo->gamer = gamer;
        outInfo->is_current_session = isCurrentSession;
        std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_create_ext(
    const CNA_StringView gamertag,
    const CNA_Bool isSignedInToLive,
    const CNA_Bool isGuest,
    const CNA_PlayerIndex playerIndex,
    CNA_SignedInGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The SignedInGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        if (playerIndex > CNA_PLAYER_INDEX_FOUR) {
            return InvalidArgument("The requested player is not a canonical PlayerIndex identity.");
        }
        std::string gamertagCopy;
        if (const CNA_Result result = CopyStringView(gamertag, true, &gamertagCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The gamertag is not valid UTF-8.");
        }

        const auto resource = std::make_shared<SignedInGamerResource>();
        resource->value = std::make_shared<SignedInGamer>(SignedInGamer::CreateInternal(
            gamertagCopy,
            isSignedInToLive != CNA_FALSE,
            isGuest != CNA_FALSE,
            static_cast<PlayerIndex>(playerIndex)));
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SignedInGamer,
            resource,
            outGamer);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned SignedInGamer handle could not be created.");
    });
}

CNA_Result cna_signed_in_gamer_get_gamertag_size(
    const CNA_SignedInGamerHandle gamerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The gamertag size output is null.");
        }
        std::shared_ptr<SignedInGamerResource> gamer;
        if (const CNA_Result result = GetSignedInGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = gamer->value->getGamertagProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_copy_gamertag(
    const CNA_SignedInGamerHandle gamerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The gamertag output buffer is invalid.");
        }
        std::shared_ptr<SignedInGamerResource> gamer;
        if (const CNA_Result result = GetSignedInGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::string& gamertag = gamer->value->getGamertagProperty();
        *outBytes = gamertag.size();
        if (capacity < gamertag.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The gamertag output buffer is too small.");
        }
        if (!gamertag.empty()) {
            std::memcpy(destination, gamertag.data(), gamertag.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_destroy(const CNA_SignedInGamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SignedInGamerResource> gamer;
        if (const CNA_Result result = GetSignedInGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (gamer->published) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The process-wide signed-in collection still references this gamer.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(gamerHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned SignedInGamer handle could not be released.");
    });
}

CNA_Result cna_gamer_set_signed_in_gamers_ext(
    const CNA_SignedInGamerHandle* const gamers,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (gamers == nullptr && count != 0U) {
            return InvalidArgument("The signed-in gamer array is invalid.");
        }
        std::vector<std::shared_ptr<SignedInGamerResource>> resources;
        std::vector<SignedInGamer*> pointers;
        resources.reserve(static_cast<std::size_t>(count));
        pointers.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = 0U; index < count; ++index) {
            std::shared_ptr<SignedInGamerResource> gamer;
            if (const CNA_Result result = GetSignedInGamer(gamers[index], &gamer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            resources.push_back(gamer);
            pointers.push_back(gamer->value.get());
        }

        Gamer::setSignedInGamersProperty(
            new SignedInGamerCollection(SignedInGamerCollection::CreateInternal(pointers)));
        for (const std::shared_ptr<SignedInGamerResource>& previous : PublishedGamers()) {
            previous->published = false;
        }
        for (const std::shared_ptr<SignedInGamerResource>& current : resources) {
            current->published = true;
        }
        PublishedGamers() = std::move(resources);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_signed_in_gamer_count(int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The signed-in gamer count output is null.");
        }
        SignedInGamerCollection* const collection = Gamer::getSignedInGamersProperty();
        *outCount = collection == nullptr
            ? INT32_C(0)
            : static_cast<int32_t>(collection->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_preset_get_clip_name_size_ext(
    const CNA_AvatarAnimationPreset preset,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The avatar clip-name size output is null.");
        }
        AvatarAnimationPreset nativePreset = AvatarAnimationPreset::Stand0;
        if (!TryMapAnimationPreset(preset, &nativePreset)) {
            return InvalidArgument("The avatar animation preset is not a defined identity.");
        }
        *outBytes = Microsoft::Xna::Framework::GamerServices::AvatarAnimationPresetToClipNameEXT(
                        nativePreset)
                        .size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_preset_copy_clip_name_ext(
    const CNA_AvatarAnimationPreset preset,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AvatarAnimationPreset nativePreset = AvatarAnimationPreset::Stand0;
        if (!TryMapAnimationPreset(preset, &nativePreset)) {
            return InvalidArgument("The avatar animation preset is not a defined identity.");
        }
        return CopyAvatarName(
            Microsoft::Xna::Framework::GamerServices::AvatarAnimationPresetToClipNameEXT(
                nativePreset),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_avatar_body_type_get_content_name_size_ext(
    const CNA_AvatarBodyType bodyType,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The avatar content-name size output is null.");
        }
        AvatarBodyType nativeBodyType = AvatarBodyType::Female;
        if (!TryMapBodyType(bodyType, &nativeBodyType)) {
            return InvalidArgument("The avatar body type is not a defined identity.");
        }
        *outBytes =
            Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT(nativeBodyType)
                .size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_body_type_copy_content_name_ext(
    const CNA_AvatarBodyType bodyType,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AvatarBodyType nativeBodyType = AvatarBodyType::Female;
        if (!TryMapBodyType(bodyType, &nativeBodyType)) {
            return InvalidArgument("The avatar body type is not a defined identity.");
        }
        return CopyAvatarName(
            Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT(nativeBodyType),
            destination,
            capacity,
            outBytes);
    });
}
