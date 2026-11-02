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

#include "CNA/GamerServices/Configuration.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include <mutex>
#include <thread>
#include <algorithm>
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

CNA_Result RetainSignedInGamer(
    const CNA_Handle handle, std::shared_ptr<SignedInGamer>* const outGamer)
{
    if (outGamer == nullptr) return InvalidArgument("The retained SignedInGamer output is null.");
    outGamer->reset();
    std::shared_ptr<SignedInGamerResource> gamer;
    if (const CNA_Result result = GetSignedInGamer(handle, &gamer); result != CNA_RESULT_SUCCESS)
        return result;
    *outGamer = gamer->value;
    return CNA_RESULT_SUCCESS;
}

std::vector<std::shared_ptr<SignedInGamer>> RetainPublishedSignedInGamers()
{
    std::vector<std::shared_ptr<SignedInGamer>> owners;
    owners.reserve(PublishedGamers().size());
    for (const auto& gamer : PublishedGamers()) owners.push_back(gamer->value);
    return owners;
}

CNA_Result CreateBorrowedSignedInGamer(
    SignedInGamer* const value, CNA_Handle* const outGamer, std::shared_ptr<SignedInGamer> owner)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The SignedInGamer output handle is null.");
    }
    *outGamer = CNA_INVALID_HANDLE;
    if (value == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    const auto resource = std::make_shared<SignedInGamerResource>();
    if (owner != nullptr && owner.get() != value)
        return InternalError("The retained SignedInGamer owner does not match the view.");
    // Event-scoped native borrows have no owner; persistent local views share canonical backing.
    resource->value = owner != nullptr ? std::move(owner)
        : std::shared_ptr<SignedInGamer>(std::shared_ptr<void>(), value);
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

using ServiceConfiguration = CNA::GamerServices::Configuration;
std::mutex configurationMutex;
std::thread::id configurationThread;

CNA_Result ConfigurationAccess()
{
    if (configurationThread != std::thread::id{} && configurationThread != std::this_thread::get_id())
        return CNA::C::Detail::Fail(CNA_RESULT_THREAD, CNA_ERROR_CATEGORY_THREAD,
            "Configuration belongs to its first caller thread.");
    if (Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::getIsInitializedProperty())
        return CNA::C::Detail::InvalidState("Configuration cannot change while the dispatcher is initialized.");
    configurationThread = std::this_thread::get_id();
    return CNA_RESULT_SUCCESS;
}

template<std::size_t N>
CNA_Result ConfigurationText(const char (&text)[N], std::string* output)
{
    const auto end = std::find(text, text + N, '\0');
    if (end == text + N) return InvalidArgument("Configuration text is not terminated.");
    return ReadText({text, static_cast<uint64_t>(end - text)}, "Invalid configuration UTF-8.", output);
}

CNA_Result ReadConfiguration(const CNA_GamerServicesConfiguration* input, ServiceConfiguration* output)
{
    if (const auto result = ValidateVersionedStructure(input, "Invalid configuration structure.", 1);
        result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ValidateCanonicalBool(input->insecure_loopback, "insecure_loopback"); result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ValidateCanonicalBool(input->avatar_catalog_updates, "avatar_catalog_updates"); result != CNA_RESULT_SUCCESS) return result;
    for (auto byte : input->reserved) if (byte) return InvalidArgument("Configuration reserved bytes must be zero.");
    if (const auto result = ConfigurationText(input->endpoint, &output->endpoint); result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ConfigurationText(input->game_id, &output->gameId); result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ConfigurationText(input->ca_bundle, &output->caBundle); result != CNA_RESULT_SUCCESS) return result;
    if (const auto result = ConfigurationText(input->title_version, &output->titleVersion); result != CNA_RESULT_SUCCESS) return result;
    output->insecureLoopback = input->insecure_loopback != CNA_FALSE;
    output->avatarCatalogUpdates = input->avatar_catalog_updates != CNA_FALSE;
    output->maxAvatarCatalogBytes = input->max_avatar_catalog_bytes;
    CNA::GamerServices::validateConfiguration(*output);
    return CNA_RESULT_SUCCESS;
}

CNA_Result ConfigurationInit(CNA_GamerServicesConfiguration* output)
{
    if (!output) return InvalidArgument("Configuration output is null.");
    CNA_GamerServicesConfiguration value{};
    value.struct_size = sizeof(value);
    value.struct_version = 1;
    value.avatar_catalog_updates = CNA_TRUE;
    value.max_avatar_catalog_bytes = ServiceConfiguration{}.maxAvatarCatalogBytes;
    *output = value;
    return CNA_RESULT_SUCCESS;
}

CNA_Result ConfigurationValidate(const CNA_GamerServicesConfiguration* input)
{
    ServiceConfiguration value;
    return ReadConfiguration(input, &value);
}

CNA_Result ConfigurationSetOverride(const CNA_GamerServicesConfiguration* input)
{
    std::lock_guard lock(configurationMutex);
    if (const auto result = ConfigurationAccess(); result != CNA_RESULT_SUCCESS) return result;
    std::optional<ServiceConfiguration> value;
    if (input) {
        value.emplace();
        if (const auto result = ReadConfiguration(input, &*value); result != CNA_RESULT_SUCCESS) return result;
    }
    CNA::GamerServices::setConfigurationOverride(std::move(value));
    return CNA_RESULT_SUCCESS;
}

CNA_Result ConfigurationResolve(CNA_GamerServicesConfiguration* output)
{
    if (const auto result = ValidateVersionedStructure(output, "Invalid configuration output.", 1);
        result != CNA_RESULT_SUCCESS) return result;
    std::lock_guard lock(configurationMutex);
    if (const auto result = ConfigurationAccess(); result != CNA_RESULT_SUCCESS) return result;
    const auto native = CNA::GamerServices::resolveConfiguration();
    CNA_GamerServicesConfiguration value{};
    ConfigurationInit(&value);
    if (native.endpoint.size() >= sizeof(value.endpoint) || native.gameId.size() >= sizeof(value.game_id) ||
        native.caBundle.size() >= sizeof(value.ca_bundle) || native.titleVersion.size() >= sizeof(value.title_version))
        return CNA::C::Detail::Overflow("Resolved configuration exceeds the C text limits.");
    for (const auto* text : {&native.endpoint, &native.gameId, &native.caBundle, &native.titleVersion}) {
        std::string checkedText;
        if (const auto result = ReadText({text->data(), text->size()},
                "Invalid resolved configuration UTF-8.", &checkedText, EmbeddedNul::Refused);
            result != CNA_RESULT_SUCCESS) return result;
    }
    std::memcpy(value.endpoint, native.endpoint.data(), native.endpoint.size());
    std::memcpy(value.game_id, native.gameId.data(), native.gameId.size());
    std::memcpy(value.ca_bundle, native.caBundle.data(), native.caBundle.size());
    std::memcpy(value.title_version, native.titleVersion.data(), native.titleVersion.size());
    value.insecure_loopback = CNA::C::Detail::ToCBool(native.insecureLoopback);
    value.avatar_catalog_updates = CNA::C::Detail::ToCBool(native.avatarCatalogUpdates);
    value.max_avatar_catalog_bytes = native.maxAvatarCatalogBytes;
    ServiceConfiguration checked;
    if (const auto result = ReadConfiguration(&value, &checked); result != CNA_RESULT_SUCCESS) return result;
    *output = value;
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
