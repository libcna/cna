// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "System/IO/Stream.hpp"

namespace {

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::IoError;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;

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
    return LookupHandle(handle, ObjectKind::SignedInGamer, outGamer);
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

CNA_Result TryBorrowSignedInGamerQuietly(
    const CNA_Handle handle,
    SignedInGamer** const outGamer) noexcept
{
    if (outGamer == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }
    *outGamer = nullptr;
    std::shared_ptr<SignedInGamerResource> gamer;
    if (const CNA_Result result =
            GetRuntimeHandles().Get(handle, ObjectKind::SignedInGamer, &gamer);
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
    return CreateHandle(ObjectKind::SignedInGamer, resource, outGamer);
}

CNA_Result ReleaseBorrowedSignedInGamer(const CNA_Handle handle)
{
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    return GetRuntimeHandles().Release(handle);
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result InviteAcceptedEventInfoInit(
    const CNA_SignedInGamerHandle gamer,
    const CNA_Bool isCurrentSession,
    CNA_InviteAcceptedEventInfo* const outInfo)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(outInfo, "The event description structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
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
}

[[nodiscard]] CNA_Result SignedInGamerCreateExt(
    const CNA_StringView gamertag,
    const CNA_Bool isSignedInToLive,
    const CNA_Bool isGuest,
    const CNA_PlayerIndex playerIndex,
    CNA_SignedInGamerHandle* const outGamer)
{
    if (playerIndex > CNA_PLAYER_INDEX_FOUR) {
        return InvalidArgument("The requested player is not a canonical PlayerIndex identity.");
    }
    std::string gamertagCopy;
    if (const CNA_Result result = ReadText(
            gamertag, "The gamertag is not valid UTF-8.", &gamertagCopy, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    return CreateResourceHandle<SignedInGamerResource>(
        ObjectKind::SignedInGamer,
        std::make_shared<SignedInGamer>(SignedInGamer::CreateInternal(
            gamertagCopy,
            isSignedInToLive != CNA_FALSE,
            isGuest != CNA_FALSE,
            static_cast<PlayerIndex>(playerIndex))),
        outGamer);
}

[[nodiscard]] CNA_Result GamerSetSignedInGamersExt(
    const CNA_SignedInGamerHandle* const gamers,
    const uint64_t count)
{
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
}

[[nodiscard]] CNA_Result GamerGetSignedInGamerCount(int32_t* const outCount)
{
    SignedInGamerCollection* const collection = Gamer::getSignedInGamersProperty();
    *outCount = collection == nullptr
        ? INT32_C(0)
        : static_cast<int32_t>(collection->getCountProperty());
    return CNA_RESULT_SUCCESS;
}

} // namespace

namespace CNA::C::Detail {
CNA_Result CopyPictureStream(
    System::IO::Stream* const stream,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (const CNA_Result result = ValidateCopyOutput(destination, capacity, outBytes);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    constexpr SharpRuntime::longcs MaximumPictureBytes = 16 * 1024 * 1024;
    const SharpRuntime::longcs length = stream != nullptr ? stream->getLengthProperty() : 0;
    if (length < 0 || length > MaximumPictureBytes) {
        return IoError("Invalid picture stream size.");
    }
    *outBytes = static_cast<uint64_t>(length);
    if (capacity < *outBytes) {
        return BufferTooSmall("The picture destination is too small.");
    }
    int position = 0;
    while (position < length) {
        const auto read = stream->Read(destination, position, length - position);
        if (read <= 0) {
            return IoError("Truncated picture stream.");
        }
        position += read;
    }
    return CNA_RESULT_SUCCESS;
}
}

// The routes generated from modules/c-api/spec/GamerServices.toml.
#include "generated/GamerServices.inc"
