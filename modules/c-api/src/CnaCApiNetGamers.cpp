// SPDX-License-Identifier: MS-PL

#include "CNA/C/net_gamers.h"
#include "CnaCApiNetDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"
#include "System/TimeSpan.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::EmbeddedNul;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidStateIf;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateCanonicalBool;
using Microsoft::Xna::Framework::Net::GameEndedEventArgs;
using Microsoft::Xna::Framework::Net::GamerJoinedEventArgs;
using Microsoft::Xna::Framework::Net::GamerLeftEventArgs;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::HostChangedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkMachine;
using Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::WriteLeaderboardsEventArgs;

struct NetworkMachineResource final {
    std::shared_ptr<NetworkMachine> value;
    std::size_t activeGamerViews = 0U;
};

// A gamer handle is either an owner of its own canonical gamer or a view onto one a machine's
// collection owns. The view keeps its owner alive and is counted, so the owner cannot be released
// while a view still points into it.
struct NetworkGamerResource final {
    std::shared_ptr<NetworkGamer> owned;
    NetworkGamer* value = nullptr;
    std::shared_ptr<void> viewOwner;
    std::size_t* viewCounter = nullptr;
    CNA_Handle session = CNA_INVALID_HANDLE;

    NetworkGamerResource() = default;
    NetworkGamerResource(const NetworkGamerResource&) = delete;
    NetworkGamerResource& operator=(const NetworkGamerResource&) = delete;

    ~NetworkGamerResource()
    {
        if (viewOwner != nullptr && viewCounter != nullptr && *viewCounter != 0U) {
            *viewCounter -= 1U;
        }
    }
};

[[nodiscard]] CNA_Result GetGamer(
    const CNA_Handle handle,
    std::shared_ptr<NetworkGamerResource>* const outGamer)
{
    return LookupHandle(handle, ObjectKind::NetworkGamer, outGamer);
}

[[nodiscard]] CNA_Result GetMachine(
    const CNA_Handle handle,
    std::shared_ptr<NetworkMachineResource>* const outMachine)
{
    return LookupHandle(handle, ObjectKind::NetworkMachine, outMachine);
}

// Every event description carries the same versioned prefix, and a payload gamer handle is
// validated before it is stored so a description can never name a handle that was never a gamer.
[[nodiscard]] CNA_Result ValidateEventPrefix(
    const uint32_t structSize,
    const uint32_t structVersion,
    const std::size_t expectedSize)
{
    if (structSize < expectedSize || structVersion != StructureVersion) {
        return InvalidArgument("The event description structure is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateEventGamer(const CNA_Handle handle)
{
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<NetworkGamerResource> gamer;
    return GetGamer(handle, &gamer);
}

} // namespace

namespace CNA::C::Detail {

CNA_Result BorrowNetworkGamer(const CNA_Handle handle, NetworkGamer** const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The borrowed NetworkGamer output is null.");
    }
    *outGamer = nullptr;
    std::shared_ptr<NetworkGamerResource> gamer;
    if (const CNA_Result result = GetGamer(handle, &gamer); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outGamer = gamer->value;
    return CNA_RESULT_SUCCESS;
}

CNA_Result RetainNetworkGamer(const CNA_Handle handle, std::shared_ptr<void>* const outOwner)
{
    if (outOwner == nullptr) {
        return InvalidArgument("The retained NetworkGamer output is null.");
    }
    outOwner->reset();
    std::shared_ptr<NetworkGamerResource> gamer;
    if (const CNA_Result result = GetGamer(handle, &gamer); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outOwner = gamer;
    return CNA_RESULT_SUCCESS;
}

CNA_Result CreateBorrowedNetworkGamer(
    NetworkGamer* const value,
    std::shared_ptr<void> viewOwner,
    std::size_t* const viewCounter,
    const CNA_Handle session,
    CNA_Handle* const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The NetworkGamer output handle is null.");
    }
    *outGamer = CNA_INVALID_HANDLE;
    if (value == nullptr) {
        return InternalError("The canonical collection reported a null gamer.");
    }
    const auto resource = std::make_shared<NetworkGamerResource>();
    resource->value = value;
    resource->session = session;
    if (const CNA_Result result = CreateHandle(ObjectKind::NetworkGamer,
        resource,
        outGamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    resource->viewOwner = std::move(viewOwner);
    resource->viewCounter = viewCounter;
    if (viewCounter != nullptr) {
        *viewCounter += 1U;
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result CreateOwnedLocalNetworkGamer(
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* const signedInGamer,
    Microsoft::Xna::Framework::Net::NetworkSession* const session,
    const CNA_Handle sessionHandle,
    CNA_Handle* const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The NetworkGamer output handle is null.");
    }
    *outGamer = CNA_INVALID_HANDLE;
    const auto resource = std::make_shared<NetworkGamerResource>();
    resource->owned = std::make_shared<Microsoft::Xna::Framework::Net::LocalNetworkGamer>(
        Microsoft::Xna::Framework::Net::LocalNetworkGamer::CreateInternal(signedInGamer, session));
    resource->value = resource->owned.get();
    resource->session = sessionHandle;
    return CreateHandle(ObjectKind::NetworkGamer, resource, outGamer);
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result NetworkGamerCreate(
    const CNA_Handle session,
    const CNA_StringView gamertag,
    CNA_NetworkGamerHandle* const outGamer)
{
    Microsoft::Xna::Framework::Net::NetworkSession* nativeSession = nullptr;
    if (session != CNA_INVALID_HANDLE) {
        if (const CNA_Result result = CNA::C::Detail::BorrowNetworkSession(
                session,
                &nativeSession);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    std::string gamertagCopy;
    if (const CNA_Result result = ReadText(
            gamertag, "The gamertag is not valid UTF-8.", &gamertagCopy, EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const auto resource = std::make_shared<NetworkGamerResource>();
    resource->owned = gamertagCopy.empty()
        ? std::make_shared<NetworkGamer>(NetworkGamer::CreateInternal(nativeSession))
        : std::make_shared<NetworkGamer>(
              NetworkGamer::CreateInternal(nativeSession, gamertagCopy));
    resource->value = resource->owned.get();
    resource->session = session;
    return CreateHandle(ObjectKind::NetworkGamer, resource, outGamer);
}

[[nodiscard]] CNA_Result NetworkGamerGetSession(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Handle* const outSession)
{
    std::shared_ptr<NetworkGamerResource> gamer;
    if (const CNA_Result result = GetGamer(gamerHandle, &gamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical property reports the raw session pointer, so the C route answers with the
    // handle that supplied it and stays consistent when no session was supplied at all.
    if ((gamer->value->getSessionProperty() == nullptr) !=
        (gamer->session == CNA_INVALID_HANDLE)) {
        return InternalError("The canonical gamer reported a different session.");
    }
    *outSession = gamer->session;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result NetworkMachineGetGamer(
    const CNA_NetworkMachineHandle machineHandle,
    const int32_t index,
    CNA_NetworkGamerHandle* const outGamer)
{
    std::shared_ptr<NetworkMachineResource> machine;
    if (const CNA_Result result = GetMachine(machineHandle, &machine);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& gamers = machine->value->getGamersProperty();
    if (index < 0 || index >= static_cast<int32_t>(gamers.getCountProperty())) {
        return OutOfRange("The gamer index is outside the machine's collection.");
    }

    const auto resource = std::make_shared<NetworkGamerResource>();
    resource->value = gamers[static_cast<int>(index)];
    if (resource->value == nullptr) {
        return InternalError("The canonical machine reported a null gamer.");
    }
    if (const CNA_Result result = CreateHandle(ObjectKind::NetworkGamer,
        resource,
        outGamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    resource->viewOwner = machine;
    resource->viewCounter = &machine->activeGamerViews;
    machine->activeGamerViews += 1U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameEndedEventInfoInit(CNA_GameEndedEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_GameEndedEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const GameEndedEventArgs arguments;
    (void)arguments;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GameStartedEventInfoInit(CNA_GameStartedEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_GameStartedEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const GameStartedEventArgs arguments;
    (void)arguments;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamerJoinedEventInfoInit(
    const CNA_NetworkGamerHandle gamer,
    CNA_GamerJoinedEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_GamerJoinedEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEventGamer(gamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const GamerJoinedEventArgs arguments(nullptr);
    (void)arguments.getGamerProperty();
    outInfo->gamer = gamer;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamerLeftEventInfoInit(
    const CNA_NetworkGamerHandle gamer,
    CNA_GamerLeftEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_GamerLeftEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEventGamer(gamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const GamerLeftEventArgs arguments(nullptr);
    (void)arguments.getGamerProperty();
    outInfo->gamer = gamer;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result HostChangedEventInfoInit(
    const CNA_NetworkGamerHandle oldHost,
    const CNA_NetworkGamerHandle newHost,
    CNA_HostChangedEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_HostChangedEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEventGamer(oldHost);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEventGamer(newHost);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const HostChangedEventArgs arguments(nullptr, nullptr);
    (void)arguments.getOldHostProperty();
    (void)arguments.getNewHostProperty();
    outInfo->old_host = oldHost;
    outInfo->new_host = newHost;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result NetworkSessionEndedEventInfoInit(
    const CNA_NetworkSessionEndReason endReason,
    CNA_NetworkSessionEndedEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_NetworkSessionEndedEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (endReason > CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) {
        return InvalidArgument(
            "The requested reason is not a canonical NetworkSessionEndReason identity.");
    }
    const NetworkSessionEndedEventArgs arguments(
        static_cast<NetworkSessionEndReason>(endReason));
    outInfo->end_reason =
        static_cast<CNA_NetworkSessionEndReason>(arguments.getEndReasonProperty());
    std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result WriteLeaderboardsEventInfoInit(
    const CNA_NetworkGamerHandle gamer,
    const CNA_Bool isLeaving,
    CNA_WriteLeaderboardsEventInfo* const outInfo)
{
    if (const CNA_Result result = ValidateEventPrefix(
            outInfo->struct_size,
            outInfo->struct_version,
            sizeof(CNA_WriteLeaderboardsEventInfo));
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateEventGamer(gamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const WriteLeaderboardsEventArgs arguments =
        WriteLeaderboardsEventArgs::CreateInternal(nullptr, isLeaving != CNA_FALSE);
    (void)arguments.getGamerProperty();
    outInfo->gamer = gamer;
    outInfo->is_leaving = ToCBool(arguments.getIsLeavingProperty());
    std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/NetGamers.toml.
#include "generated/NetGamers.inc"
