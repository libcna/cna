// SPDX-License-Identifier: MS-PL

#include "CNA/C/net_gamers.h"
#include "CnaCApiNetDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
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
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
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

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

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
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::NetworkGamer, outGamer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned NetworkGamer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetMachine(
    const CNA_Handle handle,
    std::shared_ptr<NetworkMachineResource>* const outMachine)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::NetworkMachine,
        outMachine);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned NetworkMachine handle is invalid for this call.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result GamerQuery(
    const CNA_Handle handle,
    const void* const output,
    const char* const message,
    TCallable&& callable)
{
    if (output == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<NetworkGamerResource> gamer;
    if (const CNA_Result result = GetGamer(handle, &gamer); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*gamer->value);
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result GamerCommand(const CNA_Handle handle, TCallable&& callable)
{
    std::shared_ptr<NetworkGamerResource> gamer;
    if (const CNA_Result result = GetGamer(handle, &gamer); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*gamer->value);
    return CNA_RESULT_SUCCESS;
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
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The canonical collection reported a null gamer.");
    }
    const auto resource = std::make_shared<NetworkGamerResource>();
    resource->value = value;
    resource->session = session;
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::NetworkGamer,
        resource,
        outGamer);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The borrowed NetworkGamer handle could not be created.");
    }
    resource->viewOwner = std::move(viewOwner);
    resource->viewCounter = viewCounter;
    if (viewCounter != nullptr) {
        *viewCounter += 1U;
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

CNA_Result cna_network_gamer_create(
    const CNA_Handle session,
    const CNA_StringView gamertag,
    CNA_NetworkGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The NetworkGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
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
        if (const CNA_Result result = CopyStringView(gamertag, true, &gamertagCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The gamertag is not valid UTF-8.");
        }

        const auto resource = std::make_shared<NetworkGamerResource>();
        resource->owned = gamertagCopy.empty()
            ? std::make_shared<NetworkGamer>(NetworkGamer::CreateInternal(nativeSession))
            : std::make_shared<NetworkGamer>(
                  NetworkGamer::CreateInternal(nativeSession, gamertagCopy));
        resource->value = resource->owned.get();
        resource->session = session;
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::NetworkGamer,
            resource,
            outGamer);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned NetworkGamer handle could not be created.");
    });
}

CNA_Result cna_network_gamer_get_has_left_session(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getHasLeftSessionProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_set_has_left_session_ext(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerCommand(gamerHandle, [value](NetworkGamer& gamer) {
            gamer.SetHasLeftSession(value != CNA_FALSE);
        });
    });
}

CNA_Result cna_network_gamer_get_has_voice(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getHasVoiceProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_get_id(
    const CNA_NetworkGamerHandle gamerHandle,
    uint8_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer identifier output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = static_cast<uint8_t>(gamer.getIdProperty());
            });
    });
}

CNA_Result cna_network_gamer_set_id_ext(
    const CNA_NetworkGamerHandle gamerHandle,
    const uint8_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerCommand(gamerHandle, [value](NetworkGamer& gamer) {
            gamer.SetId(static_cast<SharpRuntime::bytecs>(value));
        });
    });
}

CNA_Result cna_network_gamer_get_is_guest(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsGuestProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_get_is_host(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsHostProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_set_is_host_ext(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerCommand(gamerHandle, [value](NetworkGamer& gamer) {
            gamer.SetIsHost(value != CNA_FALSE);
        });
    });
}

CNA_Result cna_network_gamer_get_is_local(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsLocalProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_get_is_muted_by_local_user(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsMutedByLocalUserProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_get_is_private_slot(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsPrivateSlotProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_get_is_ready(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsReadyProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_set_is_ready(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerCommand(gamerHandle, [value](NetworkGamer& gamer) {
            gamer.setIsReadyProperty(value != CNA_FALSE);
        });
    });
}

CNA_Result cna_network_gamer_get_is_talking(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outValue,
            "The gamer state output is null.",
            [outValue](NetworkGamer& gamer) {
                *outValue = gamer.getIsTalkingProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_gamer_copy_machine(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_NetworkMachineHandle* const outMachine)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMachine == nullptr) {
            return InvalidArgument("The NetworkMachine output handle is null.");
        }
        *outMachine = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkGamerResource> gamer;
        if (const CNA_Result result = GetGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<NetworkMachineResource>();
        resource->value = std::make_shared<NetworkMachine>(gamer->value->getMachineProperty());
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::NetworkMachine,
            resource,
            outMachine);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned NetworkMachine handle could not be created.");
    });
}

CNA_Result cna_network_gamer_set_machine(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_NetworkMachineHandle machineHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkGamerResource> gamer;
        if (const CNA_Result result = GetGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<NetworkMachineResource> machine;
        if (const CNA_Result result = GetMachine(machineHandle, &machine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->value->setMachineProperty(*machine->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_gamer_get_roundtrip_ticks(
    const CNA_NetworkGamerHandle gamerHandle,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerQuery(
            gamerHandle,
            outTicks,
            "The round-trip output is null.",
            [outTicks](NetworkGamer& gamer) {
                *outTicks = static_cast<int64_t>(gamer.getRoundtripTimeProperty()
                    .getTicksProperty());
            });
    });
}

CNA_Result cna_network_gamer_set_roundtrip_ticks_ext(
    const CNA_NetworkGamerHandle gamerHandle,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return GamerCommand(gamerHandle, [ticks](NetworkGamer& gamer) {
            gamer.SetRoundtripTime(System::TimeSpan(ticks));
        });
    });
}

CNA_Result cna_network_gamer_get_session(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Handle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The session output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkGamerResource> gamer;
        if (const CNA_Result result = GetGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical property reports the raw session pointer, so the C route answers with the
        // handle that supplied it and stays consistent when no session was supplied at all.
        if ((gamer->value->getSessionProperty() == nullptr) !=
            (gamer->session == CNA_INVALID_HANDLE)) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical gamer reported a different session.");
        }
        *outSession = gamer->session;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_gamer_destroy(const CNA_NetworkGamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkGamerResource> gamer;
        if (const CNA_Result result = GetGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(gamerHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned NetworkGamer handle could not be released.");
    });
}

CNA_Result cna_network_machine_create(CNA_NetworkMachineHandle* const outMachine)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMachine == nullptr) {
            return InvalidArgument("The NetworkMachine output handle is null.");
        }
        *outMachine = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<NetworkMachineResource>();
        resource->value = std::make_shared<NetworkMachine>(NetworkMachine::CreateInternal());
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::NetworkMachine,
            resource,
            outMachine);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned NetworkMachine handle could not be created.");
    });
}

CNA_Result cna_network_machine_get_gamer_count(
    const CNA_NetworkMachineHandle machineHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The gamer-count output is null.");
        }
        std::shared_ptr<NetworkMachineResource> machine;
        if (const CNA_Result result = GetMachine(machineHandle, &machine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(machine->value->getGamersProperty().getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_machine_get_gamer(
    const CNA_NetworkMachineHandle machineHandle,
    const int32_t index,
    CNA_NetworkGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The NetworkGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkMachineResource> machine;
        if (const CNA_Result result = GetMachine(machineHandle, &machine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& gamers = machine->value->getGamersProperty();
        if (index < 0 || index >= static_cast<int32_t>(gamers.getCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The gamer index is outside the machine's collection.");
        }

        const auto resource = std::make_shared<NetworkGamerResource>();
        resource->value = gamers[static_cast<int>(index)];
        if (resource->value == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical machine reported a null gamer.");
        }
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::NetworkGamer,
            resource,
            outGamer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The borrowed NetworkGamer handle could not be created.");
        }
        resource->viewOwner = machine;
        resource->viewCounter = &machine->activeGamerViews;
        machine->activeGamerViews += 1U;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_machine_remove_from_session(const CNA_NetworkMachineHandle machineHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkMachineResource> machine;
        if (const CNA_Result result = GetMachine(machineHandle, &machine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        machine->value->RemoveFromSession();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_machine_destroy(const CNA_NetworkMachineHandle machineHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkMachineResource> machine;
        if (const CNA_Result result = GetMachine(machineHandle, &machine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (machine->activeGamerViews != 0U) {
            return InvalidState(
                "Every gamer view taken from this machine must be released first.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(machineHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned NetworkMachine handle could not be released.");
    });
}

CNA_Result cna_game_ended_event_info_init(CNA_GameEndedEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
    });
}

CNA_Result cna_game_started_event_info_init(CNA_GameStartedEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
    });
}

CNA_Result cna_gamer_joined_event_info_init(
    const CNA_NetworkGamerHandle gamer,
    CNA_GamerJoinedEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
    });
}

CNA_Result cna_gamer_left_event_info_init(
    const CNA_NetworkGamerHandle gamer,
    CNA_GamerLeftEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
    });
}

CNA_Result cna_host_changed_event_info_init(
    const CNA_NetworkGamerHandle oldHost,
    const CNA_NetworkGamerHandle newHost,
    CNA_HostChangedEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
    });
}

CNA_Result cna_network_session_ended_event_info_init(
    const CNA_NetworkSessionEndReason endReason,
    CNA_NetworkSessionEndedEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
    });
}

CNA_Result cna_write_leaderboards_event_info_init(
    const CNA_NetworkGamerHandle gamer,
    const CNA_Bool isLeaving,
    CNA_WriteLeaderboardsEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidArgument("The event description output is null.");
        }
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
        outInfo->is_leaving = arguments.getIsLeavingProperty() ? CNA_TRUE : CNA_FALSE;
        std::memset(outInfo->reserved, 0, sizeof(outInfo->reserved));
        return CNA_RESULT_SUCCESS;
    });
}
