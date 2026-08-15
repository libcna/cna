// SPDX-License-Identifier: MS-PL

#include "CNA/C/net_sessions.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiNetDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"
#include "System/AsyncCallback.hpp"
#include "System/IAsyncResult.hpp"
#include "System/TimeSpan.hpp"

#include <cstddef>
#include <any>
#include <functional>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowNetworkGamer;
using CNA::C::Detail::BorrowNetworkSessionProperties;
using CNA::C::Detail::CreateBorrowedNetworkGamer;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::CreateOwnedNetworkSessionProperties;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::Net::AvailableNetworkSession;
using Microsoft::Xna::Framework::Net::GameEndedEventArgs;
using Microsoft::Xna::Framework::Net::GamerJoinedEventArgs;
using Microsoft::Xna::Framework::Net::GamerLeftEventArgs;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::HostChangedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs;
using Microsoft::Xna::Framework::Net::WriteLeaderboardsEventArgs;
using Microsoft::Xna::Framework::Net::AvailableNetworkSessionCollection;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionState;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
using Microsoft::Xna::Framework::Net::QualityOfService;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

struct AvailableSessionResource final {
    std::shared_ptr<AvailableNetworkSession> value;
};

struct AvailableSessionCollectionResource final {
    std::shared_ptr<AvailableNetworkSessionCollection> value;
};

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

// The canonical creation routes hand back a caller-owned raw pointer, so the C resource owns the
// deletion. Remote gamers the session merely references are retained here as well, because the
// canonical AddRemoteGamer explicitly does not take ownership.
struct NetworkSessionResource final {
    NetworkSession* value = nullptr;
    std::vector<std::shared_ptr<void>> retainedGamers;
    std::size_t activeGamerViews = 0U;

    NetworkSessionResource() = default;
    NetworkSessionResource(const NetworkSessionResource&) = delete;
    NetworkSessionResource& operator=(const NetworkSessionResource&) = delete;

    ~NetworkSessionResource()
    {
        if (value != nullptr) {
            value->Dispose();
            delete value;
        }
    }
};

[[nodiscard]] CNA_Result GetSessionResource(
    const CNA_Handle handle,
    std::shared_ptr<NetworkSessionResource>* const outSession)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::NetworkSession,
        outSession);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned NetworkSession handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetSession(
    const CNA_Handle handle,
    std::shared_ptr<AvailableSessionResource>* const outSession)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::AvailableNetworkSession,
        outSession);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvailableNetworkSession handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_Handle handle,
    std::shared_ptr<AvailableSessionCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::AvailableNetworkSessionCollection,
        outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvailableNetworkSessionCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    const char* const message)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument(message);
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, message);
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateSessionHandle(
    AvailableNetworkSession value,
    CNA_AvailableNetworkSessionHandle* const outSession)
{
    const auto resource = std::make_shared<AvailableSessionResource>();
    resource->value = std::make_shared<AvailableNetworkSession>(std::move(value));
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::AvailableNetworkSession,
        resource,
        outSession);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvailableNetworkSession handle could not be created.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result SessionQuery(
    const CNA_Handle handle,
    const void* const output,
    const char* const message,
    TCallable&& callable)
{
    if (output == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<AvailableSessionResource> session;
    if (const CNA_Result result = GetSession(handle, &session); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*session->value);
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_available_network_session_create_ext(
    const CNA_AvailableNetworkSessionCreateInfo* const createInfo,
    const CNA_QualityOfService* const qualityOfService,
    CNA_AvailableNetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The AvailableNetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_AvailableNetworkSessionCreateInfo) ||
            createInfo->struct_version != StructureVersion) {
            return InvalidArgument("The discovered-session creation configuration is invalid.");
        }
        for (std::size_t index = 0U; index < sizeof(createInfo->reserved); ++index) {
            if (createInfo->reserved[index] != 0U) {
                return InvalidArgument(
                    "The discovered-session creation configuration is invalid.");
            }
        }
        if (createInfo->session_type > CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS) {
            return InvalidArgument(
                "The requested session type is not a canonical NetworkSessionType identity.");
        }
        if (qualityOfService != nullptr &&
            (qualityOfService->struct_size < sizeof(CNA_QualityOfService) ||
             qualityOfService->struct_version != StructureVersion)) {
            return InvalidArgument("The quality-of-service description is invalid.");
        }

        std::string hostGamertag;
        std::string hostAddress;
        if (const CNA_Result result = CopyStringView(createInfo->host_gamertag, true, &hostGamertag);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The host gamertag is not valid UTF-8.");
        }
        if (const CNA_Result result = CopyStringView(createInfo->host_address, true, &hostAddress);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The host address is not valid UTF-8.");
        }

        NetworkSessionProperties properties;
        if (createInfo->session_properties != CNA_INVALID_HANDLE) {
            NetworkSessionProperties* source = nullptr;
            if (const CNA_Result result = BorrowNetworkSessionProperties(
                    createInfo->session_properties,
                    &source);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            properties = *source;
        }

        // The canonical quality-of-service type offers exactly two constructions: unmeasured, and
        // one built from a single round-trip sample. Only the sample can be carried in, which is
        // why the throughput fields of the C description are not read here.
        const QualityOfService measured = qualityOfService == nullptr
            ? QualityOfService::CreateInternal()
            : QualityOfService::CreateInternal(
                  System::TimeSpan(qualityOfService->average_roundtrip_ticks));

        return CreateSessionHandle(
            AvailableNetworkSession::CreateInternal(
                static_cast<int>(createInfo->current_gamer_count),
                hostGamertag,
                static_cast<int>(createInfo->open_private_gamer_slots),
                static_cast<int>(createInfo->open_public_gamer_slots),
                std::move(properties),
                measured,
                hostAddress,
                createInfo->host_port,
                static_cast<NetworkSessionType>(createInfo->session_type)),
            outSession);
    });
}

CNA_Result cna_available_network_session_get_current_gamer_count(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outValue,
            "The gamer-count output is null.",
            [outValue](AvailableNetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getCurrentGamerCountProperty());
            });
    });
}

CNA_Result cna_available_network_session_get_host_gamertag_size(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outBytes,
            "The host-gamertag size output is null.",
            [outBytes](AvailableNetworkSession& session) {
                *outBytes = session.getHostGamertagProperty().size();
            });
    });
}

CNA_Result cna_available_network_session_copy_host_gamertag(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvailableSessionResource> session;
        if (const CNA_Result result = GetSession(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            session->value->getHostGamertagProperty(),
            destination,
            capacity,
            outBytes,
            "The host-gamertag output buffer is invalid or too small.");
    });
}

CNA_Result cna_available_network_session_get_open_private_gamer_slots(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outValue,
            "The private-slot output is null.",
            [outValue](AvailableNetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getOpenPrivateGamerSlotsProperty());
            });
    });
}

CNA_Result cna_available_network_session_get_open_public_gamer_slots(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outValue,
            "The public-slot output is null.",
            [outValue](AvailableNetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getOpenPublicGamerSlotsProperty());
            });
    });
}

CNA_Result cna_available_network_session_get_quality_of_service(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    CNA_QualityOfService* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr || outValue->struct_size < sizeof(CNA_QualityOfService) ||
            outValue->struct_version != StructureVersion) {
            return InvalidArgument("The quality-of-service output structure is invalid.");
        }
        std::shared_ptr<AvailableSessionResource> session;
        if (const CNA_Result result = GetSession(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const QualityOfService& quality = session->value->getQualityOfServiceProperty();
        outValue->is_available = quality.getIsAvailableProperty() ? CNA_TRUE : CNA_FALSE;
        std::memset(outValue->reserved, 0, sizeof(outValue->reserved));
        outValue->average_roundtrip_ticks =
            static_cast<int64_t>(quality.getAverageRoundtripTimeProperty().getTicksProperty());
        outValue->minimum_roundtrip_ticks =
            static_cast<int64_t>(quality.getMinimumRoundtripTimeProperty().getTicksProperty());
        outValue->bytes_per_second_downstream =
            static_cast<int32_t>(quality.getBytesPerSecondDownstreamProperty());
        outValue->bytes_per_second_upstream =
            static_cast<int32_t>(quality.getBytesPerSecondUpstreamProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_available_network_session_copy_session_properties(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    CNA_NetworkSessionPropertiesHandle* const outProperties)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProperties == nullptr) {
            return InvalidArgument("The NetworkSessionProperties output handle is null.");
        }
        *outProperties = CNA_INVALID_HANDLE;
        std::shared_ptr<AvailableSessionResource> session;
        if (const CNA_Result result = GetSession(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateOwnedNetworkSessionProperties(
            session->value->getSessionPropertiesProperty(),
            outProperties);
    });
}

CNA_Result cna_available_network_session_equals(
    const CNA_AvailableNetworkSessionHandle leftHandle,
    const CNA_AvailableNetworkSessionHandle rightHandle,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The equality output is null.");
        }
        std::shared_ptr<AvailableSessionResource> left;
        std::shared_ptr<AvailableSessionResource> right;
        if (const CNA_Result result = GetSession(leftHandle, &left);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetSession(rightHandle, &right);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = (*left->value == *right->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_available_network_session_not_equals(
    const CNA_AvailableNetworkSessionHandle leftHandle,
    const CNA_AvailableNetworkSessionHandle rightHandle,
    CNA_Bool* const outNotEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNotEqual == nullptr) {
            return InvalidArgument("The inequality output is null.");
        }
        std::shared_ptr<AvailableSessionResource> left;
        std::shared_ptr<AvailableSessionResource> right;
        if (const CNA_Result result = GetSession(leftHandle, &left);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetSession(rightHandle, &right);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outNotEqual = (*left->value != *right->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_available_network_session_get_connect_address_size_ext(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outBytes,
            "The connect-address size output is null.",
            [outBytes](AvailableNetworkSession& session) {
                *outBytes = session.GetConnectAddress().size();
            });
    });
}

CNA_Result cna_available_network_session_copy_connect_address_ext(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvailableSessionResource> session;
        if (const CNA_Result result = GetSession(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            session->value->GetConnectAddress(),
            destination,
            capacity,
            outBytes,
            "The connect-address output buffer is invalid or too small.");
    });
}

CNA_Result cna_available_network_session_get_connect_port_ext(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    uint16_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outValue,
            "The connect-port output is null.",
            [outValue](AvailableNetworkSession& session) {
                *outValue = session.GetConnectPort();
            });
    });
}

CNA_Result cna_available_network_session_get_session_type_ext(
    const CNA_AvailableNetworkSessionHandle sessionHandle,
    CNA_NetworkSessionType* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SessionQuery(
            sessionHandle,
            outValue,
            "The session-type output is null.",
            [outValue](AvailableNetworkSession& session) {
                *outValue = static_cast<CNA_NetworkSessionType>(session.GetSessionType());
            });
    });
}

CNA_Result cna_available_network_session_destroy(
    const CNA_AvailableNetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvailableSessionResource> session;
        if (const CNA_Result result = GetSession(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(sessionHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned AvailableNetworkSession handle could not be released.");
    });
}

CNA_Result cna_available_network_session_collection_create_ext(
    const CNA_AvailableNetworkSessionHandle* const sessions,
    const uint64_t count,
    CNA_AvailableNetworkSessionCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (sessions == nullptr && count != 0U) {
            return InvalidArgument("The discovered-session array is invalid.");
        }
        if (count > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The discovered-session count exceeds the canonical range.");
        }

        std::vector<AvailableNetworkSession> copied;
        copied.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = 0U; index < count; ++index) {
            std::shared_ptr<AvailableSessionResource> session;
            if (const CNA_Result result = GetSession(sessions[index], &session);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            copied.push_back(*session->value);
        }

        const auto resource = std::make_shared<AvailableSessionCollectionResource>();
        resource->value = std::make_shared<AvailableNetworkSessionCollection>(
            AvailableNetworkSessionCollection::CreateInternal(std::move(copied)));
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::AvailableNetworkSessionCollection,
            resource,
            outCollection);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned AvailableNetworkSessionCollection handle could not be created.");
    });
}

CNA_Result cna_available_network_session_collection_get_count(
    const CNA_AvailableNetworkSessionCollectionHandle collectionHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The collection-count output is null.");
        }
        std::shared_ptr<AvailableSessionCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(collection->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_available_network_session_collection_copy_session(
    const CNA_AvailableNetworkSessionCollectionHandle collectionHandle,
    const int32_t index,
    CNA_AvailableNetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The AvailableNetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        std::shared_ptr<AvailableSessionCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index < 0 || index >= static_cast<int32_t>(collection->value->getCountProperty())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The discovered-session index is outside the collection.");
        }
        return CreateSessionHandle(collection->value->getItem(index), outSession);
    });
}

CNA_Result cna_available_network_session_collection_get_is_disposed(
    const CNA_AvailableNetworkSessionCollectionHandle collectionHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return InvalidArgument("The disposal output is null.");
        }
        std::shared_ptr<AvailableSessionCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = collection->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_available_network_session_collection_dispose(
    const CNA_AvailableNetworkSessionCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvailableSessionCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_available_network_session_collection_destroy(
    const CNA_AvailableNetworkSessionCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvailableSessionCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(collectionHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned AvailableNetworkSessionCollection handle could not be released.");
    });
}

namespace {

[[nodiscard]] CNA_Result ValidateSessionType(const CNA_NetworkSessionType sessionType)
{
    if (sessionType > CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS) {
        return InvalidArgument(
            "The requested session type is not a canonical NetworkSessionType identity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowProperties(
    const CNA_Handle handle,
    NetworkSessionProperties* const outValue)
{
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    NetworkSessionProperties* source = nullptr;
    if (const CNA_Result result = BorrowNetworkSessionProperties(handle, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outValue = *source;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateSessionResourceHandle(
    NetworkSession* const created,
    CNA_NetworkSessionHandle* const outSession)
{
    if (created == nullptr) {
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The canonical creation returned no session.");
    }
    const auto resource = std::make_shared<NetworkSessionResource>();
    resource->value = created;
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::NetworkSession,
        resource,
        outSession);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned NetworkSession handle could not be created.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result NetworkSessionQuery(
    const CNA_Handle handle,
    const void* const output,
    const char* const message,
    TCallable&& callable)
{
    if (output == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<NetworkSessionResource> session;
    if (const CNA_Result result = GetSessionResource(handle, &session);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*session->value);
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result NetworkSessionCommand(const CNA_Handle handle, TCallable&& callable)
{
    std::shared_ptr<NetworkSessionResource> session;
    if (const CNA_Result result = GetSessionResource(handle, &session);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*session->value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SelectRoster(
    NetworkSession& session,
    const uint32_t roster,
    const Microsoft::Xna::Framework::GamerServices::GamerCollection<NetworkGamer>** const outAll,
    const Microsoft::Xna::Framework::GamerServices::GamerCollection<
        Microsoft::Xna::Framework::Net::LocalNetworkGamer>** const outLocal)
{
    *outAll = nullptr;
    *outLocal = nullptr;
    switch (roster) {
        case CNA_NETWORK_SESSION_ROSTER_ALL:
            *outAll = &session.getAllGamersProperty();
            return CNA_RESULT_SUCCESS;
        case CNA_NETWORK_SESSION_ROSTER_LOCAL:
            *outLocal = &session.getLocalGamersProperty();
            return CNA_RESULT_SUCCESS;
        case CNA_NETWORK_SESSION_ROSTER_REMOTE:
            *outAll = &session.getRemoteGamersProperty();
            return CNA_RESULT_SUCCESS;
        case CNA_NETWORK_SESSION_ROSTER_PREVIOUS:
            *outAll = &session.getPreviousGamersProperty();
            return CNA_RESULT_SUCCESS;
        default:
            return InvalidArgument("The requested roster is not a canonical session roster.");
    }
}

} // namespace

CNA_Result cna_network_session_create(
    const CNA_NetworkSessionType sessionType,
    const int32_t maxLocalGamers,
    const int32_t maxGamers,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateSessionResourceHandle(
            NetworkSession::Create(
                static_cast<NetworkSessionType>(sessionType),
                static_cast<int>(maxLocalGamers),
                static_cast<int>(maxGamers)),
            outSession);
    });
}

CNA_Result cna_network_session_create_with_properties(
    const CNA_NetworkSessionType sessionType,
    const int32_t maxLocalGamers,
    const int32_t maxGamers,
    const int32_t privateGamerSlots,
    const CNA_NetworkSessionPropertiesHandle sessionProperties,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(sessionProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateSessionResourceHandle(
            NetworkSession::Create(
                static_cast<NetworkSessionType>(sessionType),
                static_cast<int>(maxLocalGamers),
                static_cast<int>(maxGamers),
                static_cast<int>(privateGamerSlots),
                std::move(properties)),
            outSession);
    });
}

CNA_Result cna_network_session_create_with_local_gamers(
    const CNA_NetworkSessionType sessionType,
    const CNA_Handle* const localGamers,
    const uint64_t count,
    const int32_t maxGamers,
    const int32_t privateGamerSlots,
    const CNA_NetworkSessionPropertiesHandle sessionProperties,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (localGamers == nullptr && count != 0U) {
            return InvalidArgument("The local-gamer array is invalid.");
        }
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Microsoft::Xna::Framework::GamerServices::SignedInGamer*> nativeGamers;
        nativeGamers.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = 0U; index < count; ++index) {
            Microsoft::Xna::Framework::GamerServices::SignedInGamer* gamer = nullptr;
            if (const CNA_Result result = CNA::C::Detail::BorrowSignedInGamer(
                    localGamers[index],
                    &gamer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            nativeGamers.push_back(gamer);
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(sessionProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateSessionResourceHandle(
            NetworkSession::Create(
                static_cast<NetworkSessionType>(sessionType),
                nativeGamers,
                static_cast<int>(maxGamers),
                static_cast<int>(privateGamerSlots),
                std::move(properties)),
            outSession);
    });
}

CNA_Result cna_network_session_get_is_disposed(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outIsDisposed,
            "The disposal output is null.",
            [outIsDisposed](NetworkSession& session) {
                *outIsDisposed = session.getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_session_get_gamer_count(
    const CNA_NetworkSessionHandle sessionHandle,
    const uint32_t roster,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The gamer-count output is null.");
        }
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Microsoft::Xna::Framework::GamerServices::GamerCollection<NetworkGamer>* all =
            nullptr;
        const Microsoft::Xna::Framework::GamerServices::GamerCollection<
            Microsoft::Xna::Framework::Net::LocalNetworkGamer>* local = nullptr;
        if (const CNA_Result result = SelectRoster(*session->value, roster, &all, &local);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = all != nullptr
            ? static_cast<int32_t>(all->getCountProperty())
            : static_cast<int32_t>(local->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_get_gamer(
    const CNA_NetworkSessionHandle sessionHandle,
    const uint32_t roster,
    const int32_t index,
    CNA_NetworkGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The NetworkGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Microsoft::Xna::Framework::GamerServices::GamerCollection<NetworkGamer>* all =
            nullptr;
        const Microsoft::Xna::Framework::GamerServices::GamerCollection<
            Microsoft::Xna::Framework::Net::LocalNetworkGamer>* local = nullptr;
        if (const CNA_Result result = SelectRoster(*session->value, roster, &all, &local);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int32_t count = all != nullptr
            ? static_cast<int32_t>(all->getCountProperty())
            : static_cast<int32_t>(local->getCountProperty());
        if (index < 0 || index >= count) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The gamer index is outside that roster.");
        }
        NetworkGamer* const value = all != nullptr
            ? (*all)[static_cast<int>(index)]
            : static_cast<NetworkGamer*>(
                  static_cast<Microsoft::Xna::Framework::Net::LocalNetworkGamer*>(
                      (*local)[static_cast<int>(index)]));
        return CreateBorrowedNetworkGamer(
            value,
            session,
            &session->activeGamerViews,
            sessionHandle,
            outGamer);
    });
}

CNA_Result cna_network_session_get_allow_host_migration(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The session state output is null.",
            [outValue](NetworkSession& session) {
                *outValue = session.getAllowHostMigrationProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_session_set_allow_host_migration(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [value](NetworkSession& session) {
            session.setAllowHostMigrationProperty(value != CNA_FALSE);
        });
    });
}

CNA_Result cna_network_session_get_allow_join_in_progress(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The session state output is null.",
            [outValue](NetworkSession& session) {
                *outValue = session.getAllowJoinInProgressProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_session_set_allow_join_in_progress(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_Bool value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [value](NetworkSession& session) {
            session.setAllowJoinInProgressProperty(value != CNA_FALSE);
        });
    });
}

CNA_Result cna_network_session_get_bytes_per_second_received(
    const CNA_NetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The throughput output is null.",
            [outValue](NetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getBytesPerSecondReceivedProperty());
            });
    });
}

CNA_Result cna_network_session_get_bytes_per_second_sent(
    const CNA_NetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The throughput output is null.",
            [outValue](NetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getBytesPerSecondSentProperty());
            });
    });
}

CNA_Result cna_network_session_get_host(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_NetworkGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The NetworkGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* const host = session->value->getHostProperty();
        if (host == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        return CreateBorrowedNetworkGamer(
            host,
            session,
            &session->activeGamerViews,
            sessionHandle,
            outGamer);
    });
}

CNA_Result cna_network_session_get_is_everyone_ready(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The session state output is null.",
            [outValue](NetworkSession& session) {
                *outValue = session.getIsEveryoneReadyProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_session_get_is_host(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The session state output is null.",
            [outValue](NetworkSession& session) {
                *outValue = session.getIsHostProperty() ? CNA_TRUE : CNA_FALSE;
            });
    });
}

CNA_Result cna_network_session_get_max_gamers(
    const CNA_NetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The gamer-limit output is null.",
            [outValue](NetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getMaxGamersProperty());
            });
    });
}

CNA_Result cna_network_session_set_max_gamers(
    const CNA_NetworkSessionHandle sessionHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [value](NetworkSession& session) {
            session.setMaxGamersProperty(static_cast<int>(value));
        });
    });
}

CNA_Result cna_network_session_get_private_gamer_slots(
    const CNA_NetworkSessionHandle sessionHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The private-slot output is null.",
            [outValue](NetworkSession& session) {
                *outValue = static_cast<int32_t>(session.getPrivateGamerSlotsProperty());
            });
    });
}

CNA_Result cna_network_session_set_private_gamer_slots(
    const CNA_NetworkSessionHandle sessionHandle,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [value](NetworkSession& session) {
            session.setPrivateGamerSlotsProperty(static_cast<int>(value));
        });
    });
}

CNA_Result cna_network_session_copy_session_properties(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_NetworkSessionPropertiesHandle* const outProperties)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProperties == nullptr) {
            return InvalidArgument("The NetworkSessionProperties output handle is null.");
        }
        *outProperties = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateOwnedNetworkSessionProperties(
            session->value->getSessionPropertiesProperty(),
            outProperties);
    });
}

CNA_Result cna_network_session_get_session_state(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_NetworkSessionState* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The session state output is null.",
            [outValue](NetworkSession& session) {
                *outValue = static_cast<CNA_NetworkSessionState>(session.getSessionStateProperty());
            });
    });
}

CNA_Result cna_network_session_get_session_type(
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_NetworkSessionType* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The session type output is null.",
            [outValue](NetworkSession& session) {
                *outValue = static_cast<CNA_NetworkSessionType>(session.getSessionTypeProperty());
            });
    });
}

CNA_Result cna_network_session_get_simulated_latency_ticks(
    const CNA_NetworkSessionHandle sessionHandle,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outTicks,
            "The latency output is null.",
            [outTicks](NetworkSession& session) {
                *outTicks = static_cast<int64_t>(
                    session.getSimulatedLatencyProperty().getTicksProperty());
            });
    });
}

CNA_Result cna_network_session_set_simulated_latency_ticks(
    const CNA_NetworkSessionHandle sessionHandle,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [ticks](NetworkSession& session) {
            session.setSimulatedLatencyProperty(System::TimeSpan(ticks));
        });
    });
}

CNA_Result cna_network_session_get_simulated_packet_loss(
    const CNA_NetworkSessionHandle sessionHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outValue,
            "The packet-loss output is null.",
            [outValue](NetworkSession& session) {
                *outValue = session.getSimulatedPacketLossProperty();
            });
    });
}

CNA_Result cna_network_session_set_simulated_packet_loss(
    const CNA_NetworkSessionHandle sessionHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [value](NetworkSession& session) {
            session.setSimulatedPacketLossProperty(value);
        });
    });
}

CNA_Result cna_network_session_get_type_name_size(
    const CNA_NetworkSessionHandle sessionHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outBytes,
            "The type-name size output is null.",
            [outBytes](NetworkSession& session) { *outBytes = session.GetTypeName().size(); });
    });
}

CNA_Result cna_network_session_copy_type_name(
    const CNA_NetworkSessionHandle sessionHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            session->value->GetTypeName(),
            destination,
            capacity,
            outBytes,
            "The type-name output buffer is invalid or too small.");
    });
}

CNA_Result cna_network_session_update(const CNA_NetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [](NetworkSession& session) {
            session.Update();
        });
    });
}

CNA_Result cna_network_session_add_local_gamer(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_Handle signedInGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (signedInGamer != CNA_INVALID_HANDLE) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "Signed-in gamers have no C representation yet.");
        }
        return NetworkSessionCommand(sessionHandle, [](NetworkSession& session) {
            session.AddLocalGamer(nullptr);
        });
    });
}

CNA_Result cna_network_session_find_gamer_by_id(
    const CNA_NetworkSessionHandle sessionHandle,
    const uint8_t gamerId,
    CNA_NetworkGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The NetworkGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* const found =
            session->value->FindGamerById(static_cast<SharpRuntime::bytecs>(gamerId));
        if (found == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        return CreateBorrowedNetworkGamer(
            found,
            session,
            &session->activeGamerViews,
            sessionHandle,
            outGamer);
    });
}

CNA_Result cna_network_session_reset_ready(const CNA_NetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [](NetworkSession& session) {
            session.ResetReady();
        });
    });
}

CNA_Result cna_network_session_start_game(const CNA_NetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [](NetworkSession& session) {
            session.StartGame();
        });
    });
}

CNA_Result cna_network_session_end_game(const CNA_NetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [](NetworkSession& session) {
            session.EndGame();
        });
    });
}

CNA_Result cna_network_session_send_network_event_ext(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_NetworkEventInfo* const eventInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (eventInfo == nullptr || eventInfo->struct_size < sizeof(CNA_NetworkEventInfo) ||
            eventInfo->struct_version != StructureVersion) {
            return InvalidArgument("The network event description is invalid.");
        }
        if (eventInfo->type > CNA_NETWORK_EVENT_TYPE_STATE_CHANGE ||
            eventInfo->reliable > CNA_SEND_DATA_OPTIONS_CHAT ||
            eventInfo->state > CNA_NETWORK_SESSION_STATE_ENDED ||
            eventInfo->reason > CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) {
            return InvalidArgument("The network event description names an unknown identity.");
        }
        if (eventInfo->packet == nullptr && eventInfo->packet_byte_count != 0U) {
            return InvalidArgument("The network event payload is invalid.");
        }

        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        NetworkSession::NetworkEvent event;
        event.Type = static_cast<NetworkSession::NetworkEventType>(eventInfo->type);
        event.Reliable =
            static_cast<Microsoft::Xna::Framework::Net::SendDataOptions>(eventInfo->reliable);
        event.State = static_cast<NetworkSessionState>(eventInfo->state);
        event.Reason = static_cast<NetworkSessionEndReason>(eventInfo->reason);
        if (eventInfo->gamer != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = BorrowNetworkGamer(eventInfo->gamer, &event.Gamer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        if (eventInfo->sender != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = BorrowNetworkGamer(eventInfo->sender, &event.Sender);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        if (eventInfo->packet_byte_count != 0U) {
            event.Packet.assign(
                eventInfo->packet,
                eventInfo->packet + eventInfo->packet_byte_count);
        }
        session->value->SendNetworkEvent(std::move(event));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_add_remote_gamer_ext(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_NetworkGamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowNetworkGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<void> retained;
        if (const CNA_Result result = CNA::C::Detail::RetainNetworkGamer(gamerHandle, &retained);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        session->value->AddRemoteGamer(gamer);
        // The canonical call deliberately does not take ownership, so the C layer keeps the gamer
        // resource alive for as long as the session references it.
        session->retainedGamers.push_back(std::move(retained));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_remove_gamer_ext(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_NetworkSessionEndReason reason)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (reason > CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) {
            return InvalidArgument(
                "The requested reason is not a canonical NetworkSessionEndReason identity.");
        }
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowNetworkGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        session->value->RemoveGamer(gamer, static_cast<NetworkSessionEndReason>(reason));
        std::shared_ptr<void> retained;
        if (CNA::C::Detail::RetainNetworkGamer(gamerHandle, &retained) == CNA_RESULT_SUCCESS) {
            for (std::size_t index = 0U; index < session->retainedGamers.size(); ++index) {
                if (session->retainedGamers[index] == retained) {
                    session->retainedGamers.erase(session->retainedGamers.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    break;
                }
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_get_owned_gamer_count_ext(
    const CNA_NetworkSessionHandle sessionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionQuery(
            sessionHandle,
            outCount,
            "The owned-gamer count output is null.",
            [outCount](NetworkSession& session) {
                *outCount = static_cast<uint64_t>(session.GetOwnedGamerCountForTesting());
            });
    });
}

CNA_Result cna_network_session_get_instance_count_ext(int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The instance-count output is null.");
        }
        *outCount = static_cast<int32_t>(NetworkSession::GetInstanceCountForTesting());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_get_active_action_count_ext(int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The action-count output is null.");
        }
        *outCount = static_cast<int32_t>(NetworkSession::GetActiveActionInstanceCountForTesting());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_dispose(const CNA_NetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return NetworkSessionCommand(sessionHandle, [](NetworkSession& session) {
            session.Dispose();
        });
    });
}

CNA_Result cna_network_session_destroy(const CNA_NetworkSessionHandle sessionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = GetSessionResource(sessionHandle, &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (session->activeGamerViews != 0U) {
            return InvalidState(
                "Every gamer view taken from this session must be released first.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(sessionHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned NetworkSession handle could not be released.");
    });
}

namespace CNA::C::Detail {

CNA_Result BorrowNetworkSession(const CNA_Handle handle, NetworkSession** const outSession)
{
    if (outSession == nullptr) {
        return InvalidArgument("The borrowed NetworkSession output is null.");
    }
    *outSession = nullptr;
    std::shared_ptr<NetworkSessionResource> session;
    if (const CNA_Result result = GetSessionResource(handle, &session);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outSession = session->value;
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

namespace {

// A subscription lives exactly as long as its registration handle. Instance events hold a weak
// reference to the session resource, so releasing a registration after the session is gone is a
// no-op; the static invite event needs no owner at all.
class SessionEventRegistration final {
public:
    using Unsubscribe = std::function<void(NetworkSession&)>;

    SessionEventRegistration(std::weak_ptr<NetworkSessionResource> owner, Unsubscribe unsubscribe)
        : owner_(std::move(owner)), unsubscribe_(std::move(unsubscribe))
    {
    }

    explicit SessionEventRegistration(std::function<void()> unsubscribeStatic)
        : unsubscribeStatic_(std::move(unsubscribeStatic))
    {
    }

    SessionEventRegistration(const SessionEventRegistration&) = delete;
    SessionEventRegistration& operator=(const SessionEventRegistration&) = delete;

    ~SessionEventRegistration()
    {
        Release();
    }

    void Release() noexcept
    {
        if (!subscribed_) {
            return;
        }
        subscribed_ = false;
        if (unsubscribeStatic_) {
            unsubscribeStatic_();
            return;
        }
        if (const std::shared_ptr<NetworkSessionResource> owner = owner_.lock()) {
            if (owner->value != nullptr && unsubscribe_) {
                unsubscribe_(*owner->value);
            }
        }
    }

private:
    std::weak_ptr<NetworkSessionResource> owner_;
    Unsubscribe unsubscribe_;
    std::function<void()> unsubscribeStatic_;
    bool subscribed_ = true;
};

[[nodiscard]] CNA_Result CreateRegistrationHandle(
    std::shared_ptr<SessionEventRegistration> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::NetworkSessionEventRegistration,
        registration,
        outRegistration);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    registration->Release();
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The session event registration handle could not be created.");
}

// Every payload gamer is handed to the callback as a handle that exists only for that call, so a
// consumer can never retain a pointer into session-owned state.
class ScopedGamerHandle final {
public:
    ScopedGamerHandle(
        NetworkGamer* const value,
        const std::shared_ptr<NetworkSessionResource>& owner,
        const CNA_Handle session)
    {
        if (value == nullptr) {
            return;
        }
        if (CreateBorrowedNetworkGamer(
                value,
                owner,
                &owner->activeGamerViews,
                session,
                &handle_) != CNA_RESULT_SUCCESS) {
            handle_ = CNA_INVALID_HANDLE;
        }
    }

    ScopedGamerHandle(const ScopedGamerHandle&) = delete;
    ScopedGamerHandle& operator=(const ScopedGamerHandle&) = delete;

    ~ScopedGamerHandle()
    {
        if (handle_ != CNA_INVALID_HANDLE) {
            (void)GetRuntimeHandles().Release(handle_);
        }
    }

    [[nodiscard]] CNA_Handle Get() const noexcept { return handle_; }

private:
    CNA_Handle handle_ = CNA_INVALID_HANDLE;
};

[[nodiscard]] CNA_Result BeginSubscribe(
    const CNA_Handle sessionHandle,
    const bool callbackIsNull,
    CNA_Handle* const outRegistration,
    std::shared_ptr<NetworkSessionResource>* const outSession)
{
    if (outRegistration == nullptr) {
        return InvalidArgument("The registration output handle is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callbackIsNull) {
        return InvalidArgument("The event callback is null.");
    }
    return GetSessionResource(sessionHandle, outSession);
}

} // namespace

CNA_Result cna_network_session_subscribe_game_started(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_GameStartedCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->GameStarted.Add(
            [sessionHandle, callback, context](System::Object*, const GameStartedEventArgs& /*arguments*/) {
                CNA_GameStartedEventInfo info = {sizeof(CNA_GameStartedEventInfo), UINT32_C(1)};
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.GameStarted.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_game_ended(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_GameEndedCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->GameEnded.Add(
            [sessionHandle, callback, context](System::Object*, const GameEndedEventArgs& /*arguments*/) {
                CNA_GameEndedEventInfo info = {sizeof(CNA_GameEndedEventInfo), UINT32_C(1)};
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.GameEnded.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_gamer_joined(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_GamerJoinedCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->GamerJoined.Add(
            [owner, sessionHandle, callback, context](System::Object*, const GamerJoinedEventArgs& arguments) {
                const std::shared_ptr<NetworkSessionResource> alive = owner.lock();
                if (alive == nullptr) {
                    return;
                }
                CNA_GamerJoinedEventInfo info = {sizeof(CNA_GamerJoinedEventInfo), UINT32_C(1)};
                const ScopedGamerHandle gamer(arguments.getGamerProperty(), alive, sessionHandle);
                info.gamer = gamer.Get();
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.GamerJoined.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_gamer_left(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_GamerLeftCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->GamerLeft.Add(
            [owner, sessionHandle, callback, context](System::Object*, const GamerLeftEventArgs& arguments) {
                const std::shared_ptr<NetworkSessionResource> alive = owner.lock();
                if (alive == nullptr) {
                    return;
                }
                CNA_GamerLeftEventInfo info = {sizeof(CNA_GamerLeftEventInfo), UINT32_C(1)};
                const ScopedGamerHandle gamer(arguments.getGamerProperty(), alive, sessionHandle);
                info.gamer = gamer.Get();
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.GamerLeft.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_host_changed(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_HostChangedCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->HostChanged.Add(
            [owner, sessionHandle, callback, context](System::Object*, const HostChangedEventArgs& arguments) {
                const std::shared_ptr<NetworkSessionResource> alive = owner.lock();
                if (alive == nullptr) {
                    return;
                }
                CNA_HostChangedEventInfo info = {sizeof(CNA_HostChangedEventInfo), UINT32_C(1)};
                const ScopedGamerHandle oldHost(arguments.getOldHostProperty(), alive, sessionHandle);
                const ScopedGamerHandle newHost(arguments.getNewHostProperty(), alive, sessionHandle);
                info.old_host = oldHost.Get();
                info.new_host = newHost.Get();
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.HostChanged.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_session_ended(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_NetworkSessionEndedCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->SessionEnded.Add(
            [sessionHandle, callback, context](System::Object*, const NetworkSessionEndedEventArgs& arguments) {
                CNA_NetworkSessionEndedEventInfo info = {sizeof(CNA_NetworkSessionEndedEventInfo), UINT32_C(1)};
                info.end_reason =
                    static_cast<CNA_NetworkSessionEndReason>(arguments.getEndReasonProperty());
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.SessionEnded.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_write_arbitrated_leaderboard(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_WriteLeaderboardsCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->WriteArbitratedLeaderboard.Add(
            [owner, sessionHandle, callback, context](System::Object*, const WriteLeaderboardsEventArgs& arguments) {
                const std::shared_ptr<NetworkSessionResource> alive = owner.lock();
                if (alive == nullptr) {
                    return;
                }
                CNA_WriteLeaderboardsEventInfo info = {sizeof(CNA_WriteLeaderboardsEventInfo), UINT32_C(1)};
                const ScopedGamerHandle gamer(arguments.getGamerProperty(), alive, sessionHandle);
                info.gamer = gamer.Get();
                info.is_leaving = arguments.getIsLeavingProperty() ? CNA_TRUE : CNA_FALSE;
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.WriteArbitratedLeaderboard.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_write_unarbitrated_leaderboard(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_WriteLeaderboardsCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->WriteUnarbitratedLeaderboard.Add(
            [owner, sessionHandle, callback, context](System::Object*, const WriteLeaderboardsEventArgs& arguments) {
                const std::shared_ptr<NetworkSessionResource> alive = owner.lock();
                if (alive == nullptr) {
                    return;
                }
                CNA_WriteLeaderboardsEventInfo info = {sizeof(CNA_WriteLeaderboardsEventInfo), UINT32_C(1)};
                const ScopedGamerHandle gamer(arguments.getGamerProperty(), alive, sessionHandle);
                info.gamer = gamer.Get();
                info.is_leaving = arguments.getIsLeavingProperty() ? CNA_TRUE : CNA_FALSE;
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.WriteUnarbitratedLeaderboard.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_write_true_skill(
    const CNA_NetworkSessionHandle sessionHandle,
    const CNA_WriteLeaderboardsCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionResource> session;
        if (const CNA_Result result = BeginSubscribe(
                sessionHandle,
                callback == nullptr,
                outRegistration,
                &session);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::weak_ptr<NetworkSessionResource> owner = session;
        const auto token = session->value->WriteTrueSkill.Add(
            [owner, sessionHandle, callback, context](System::Object*, const WriteLeaderboardsEventArgs& arguments) {
                const std::shared_ptr<NetworkSessionResource> alive = owner.lock();
                if (alive == nullptr) {
                    return;
                }
                CNA_WriteLeaderboardsEventInfo info = {sizeof(CNA_WriteLeaderboardsEventInfo), UINT32_C(1)};
                const ScopedGamerHandle gamer(arguments.getGamerProperty(), alive, sessionHandle);
                info.gamer = gamer.Get();
                info.is_leaving = arguments.getIsLeavingProperty() ? CNA_TRUE : CNA_FALSE;
                callback(sessionHandle, &info, context);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                owner,
                [token](NetworkSession& native) { native.WriteTrueSkill.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_subscribe_invite_accepted(
    const CNA_InviteAcceptedCallback callback,
    void* const context,
    CNA_NetworkSessionEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The event callback is null.");
        }
        const auto token = NetworkSession::InviteAccepted.Add(
            [callback, context](
                System::Object*,
                const Microsoft::Xna::Framework::GamerServices::InviteAcceptedEventArgs&
                    arguments) {
                CNA_InviteAcceptedEventInfo info = {
                    sizeof(CNA_InviteAcceptedEventInfo), UINT32_C(1)};
                CNA_Handle gamer = CNA_INVALID_HANDLE;
                (void)CNA::C::Detail::CreateBorrowedSignedInGamer(
                    arguments.getGamerProperty(),
                    &gamer);
                info.gamer = gamer;
                info.is_current_session =
                    arguments.getIsCurrentSessionProperty() ? CNA_TRUE : CNA_FALSE;
                callback(&info, context);
                (void)CNA::C::Detail::ReleaseBorrowedSignedInGamer(gamer);
            });
        return CreateRegistrationHandle(
            std::make_shared<SessionEventRegistration>(
                [token]() { NetworkSession::InviteAccepted.Remove(token); }),
            outRegistration);
    });
}

CNA_Result cna_network_session_unsubscribe(
    const CNA_NetworkSessionEventRegistrationHandle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SessionEventRegistration> registration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::NetworkSessionEventRegistration,
            &registration);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The session event registration handle is invalid.");
        }
        registration->Release();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The session event registration handle could not be released.");
    });
}

namespace {

using SignedInGamerList = std::vector<Microsoft::Xna::Framework::GamerServices::SignedInGamer*>;

[[nodiscard]] CNA_Result BorrowSignedInGamers(
    const CNA_Handle* const handles,
    const uint64_t count,
    SignedInGamerList* const outGamers)
{
    if (handles == nullptr && count != 0U) {
        return InvalidArgument("The local-gamer array is invalid.");
    }
    outGamers->reserve(static_cast<std::size_t>(count));
    for (uint64_t index = 0U; index < count; ++index) {
        Microsoft::Xna::Framework::GamerServices::SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowSignedInGamer(handles[index], &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        outGamers->push_back(gamer);
    }
    return CNA_RESULT_SUCCESS;
}

// CNA completes every canonical Begin/End pair before Begin returns, so the C route stays one
// synchronous call and simply invokes the completion delegate the canonical API already accepts.
[[nodiscard]] System::AsyncCallback CompletionDelegate(
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context)
{
    if (callback == nullptr) {
        return System::AsyncCallback{};
    }
    return [callback, context](System::IAsyncResult&) { callback(context); };
}

[[nodiscard]] CNA_Result CreateCollectionHandle(
    AvailableNetworkSessionCollection value,
    CNA_AvailableNetworkSessionCollectionHandle* const outCollection)
{
    const auto resource = std::make_shared<AvailableSessionCollectionResource>();
    resource->value = std::make_shared<AvailableNetworkSessionCollection>(std::move(value));
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::AvailableNetworkSessionCollection,
        resource,
        outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvailableNetworkSessionCollection handle could not be created.");
}

} // namespace

CNA_Result cna_network_session_create_async(
    const CNA_NetworkSessionType sessionType,
    const int32_t maxLocalGamers,
    const int32_t maxGamers,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginCreate(
            static_cast<NetworkSessionType>(sessionType),
            static_cast<int>(maxLocalGamers),
            static_cast<int>(maxGamers),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateSessionResourceHandle(NetworkSession::EndCreate(pending), outSession);
    });
}

CNA_Result cna_network_session_create_with_properties_async(
    const CNA_NetworkSessionType sessionType,
    const int32_t maxLocalGamers,
    const int32_t maxGamers,
    const int32_t privateGamerSlots,
    const CNA_NetworkSessionPropertiesHandle sessionProperties,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(sessionProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginCreate(
            static_cast<NetworkSessionType>(sessionType),
            static_cast<int>(maxLocalGamers),
            static_cast<int>(maxGamers),
            static_cast<int>(privateGamerSlots),
            std::move(properties),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateSessionResourceHandle(NetworkSession::EndCreate(pending), outSession);
    });
}

CNA_Result cna_network_session_create_with_local_gamers_async(
    const CNA_NetworkSessionType sessionType,
    const CNA_Handle* const localGamers,
    const uint64_t count,
    const int32_t maxGamers,
    const int32_t privateGamerSlots,
    const CNA_NetworkSessionPropertiesHandle sessionProperties,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamerList gamers;
        if (const CNA_Result result = BorrowSignedInGamers(localGamers, count, &gamers);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(sessionProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginCreate(
            static_cast<NetworkSessionType>(sessionType),
            gamers,
            static_cast<int>(maxGamers),
            static_cast<int>(privateGamerSlots),
            std::move(properties),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateSessionResourceHandle(NetworkSession::EndCreate(pending), outSession);
    });
}

CNA_Result cna_network_session_find(
    const CNA_NetworkSessionType sessionType,
    const int32_t maxLocalGamers,
    const CNA_NetworkSessionPropertiesHandle searchProperties,
    CNA_AvailableNetworkSessionCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(searchProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateCollectionHandle(
            NetworkSession::Find(
                static_cast<NetworkSessionType>(sessionType),
                static_cast<int>(maxLocalGamers),
                std::move(properties)),
            outCollection);
    });
}

CNA_Result cna_network_session_find_with_local_gamers(
    const CNA_NetworkSessionType sessionType,
    const CNA_Handle* const localGamers,
    const uint64_t count,
    const CNA_NetworkSessionPropertiesHandle searchProperties,
    CNA_AvailableNetworkSessionCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamerList gamers;
        if (const CNA_Result result = BorrowSignedInGamers(localGamers, count, &gamers);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(searchProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateCollectionHandle(
            NetworkSession::Find(
                static_cast<NetworkSessionType>(sessionType),
                gamers,
                std::move(properties)),
            outCollection);
    });
}

CNA_Result cna_network_session_find_async(
    const CNA_NetworkSessionType sessionType,
    const int32_t maxLocalGamers,
    const CNA_NetworkSessionPropertiesHandle searchProperties,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_AvailableNetworkSessionCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(searchProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginFind(
            static_cast<NetworkSessionType>(sessionType),
            static_cast<int>(maxLocalGamers),
            std::move(properties),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateCollectionHandle(NetworkSession::EndFind(pending), outCollection);
    });
}

CNA_Result cna_network_session_find_with_local_gamers_async(
    const CNA_NetworkSessionType sessionType,
    const CNA_Handle* const localGamers,
    const uint64_t count,
    const CNA_NetworkSessionPropertiesHandle searchProperties,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_AvailableNetworkSessionCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateSessionType(sessionType);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamerList gamers;
        if (const CNA_Result result = BorrowSignedInGamers(localGamers, count, &gamers);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkSessionProperties properties;
        if (const CNA_Result result = BorrowProperties(searchProperties, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginFind(
            static_cast<NetworkSessionType>(sessionType),
            gamers,
            std::move(properties),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateCollectionHandle(NetworkSession::EndFind(pending), outCollection);
    });
}

CNA_Result cna_network_session_join(
    const CNA_AvailableNetworkSessionHandle availableSessionHandle,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        std::shared_ptr<AvailableSessionResource> available;
        if (const CNA_Result result = GetSession(availableSessionHandle, &available);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateSessionResourceHandle(
            NetworkSession::Join(available->value.get()),
            outSession);
    });
}

CNA_Result cna_network_session_join_async(
    const CNA_AvailableNetworkSessionHandle availableSessionHandle,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        std::shared_ptr<AvailableSessionResource> available;
        if (const CNA_Result result = GetSession(availableSessionHandle, &available);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginJoin(
            available->value.get(),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateSessionResourceHandle(NetworkSession::EndJoin(pending), outSession);
    });
}

CNA_Result cna_network_session_join_invited(
    const int32_t maxLocalGamers,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        return CreateSessionResourceHandle(
            NetworkSession::JoinInvited(static_cast<int>(maxLocalGamers)),
            outSession);
    });
}

CNA_Result cna_network_session_join_invited_with_local_gamers(
    const CNA_Handle* const localGamers,
    const uint64_t count,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        SignedInGamerList gamers;
        if (const CNA_Result result = BorrowSignedInGamers(localGamers, count, &gamers);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateSessionResourceHandle(NetworkSession::JoinInvited(gamers), outSession);
    });
}

CNA_Result cna_network_session_join_invited_async(
    const int32_t maxLocalGamers,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        System::IAsyncResult* const pending = NetworkSession::BeginJoinInvited(
            static_cast<int>(maxLocalGamers),
            CompletionDelegate(callback, context),
            std::any{});
        return CreateSessionResourceHandle(NetworkSession::EndJoinInvited(pending), outSession);
    });
}

CNA_Result cna_network_session_join_invited_with_local_gamers_async(
    const CNA_Handle* const localGamers,
    const uint64_t count,
    const CNA_NetworkSessionAsyncCallback callback,
    void* const context,
    CNA_NetworkSessionHandle* const outSession)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSession == nullptr) {
            return InvalidArgument("The NetworkSession output handle is null.");
        }
        *outSession = CNA_INVALID_HANDLE;
        SignedInGamerList gamers;
        if (const CNA_Result result = BorrowSignedInGamers(localGamers, count, &gamers);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IAsyncResult* const pending = NetworkSession::BeginJoinInvited(
            gamers,
            CompletionDelegate(callback, context),
            std::any{});
        return CreateSessionResourceHandle(NetworkSession::EndJoinInvited(pending), outSession);
    });
}

namespace {

using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::SendDataOptions;

// A local gamer shares the network-gamer handle kind, because it is one. The routes below refuse a
// handle whose gamer is not actually local rather than reinterpreting it.
[[nodiscard]] CNA_Result BorrowLocalGamer(const CNA_Handle handle, LocalNetworkGamer** const outGamer)
{
    *outGamer = nullptr;
    NetworkGamer* gamer = nullptr;
    if (const CNA_Result result = BorrowNetworkGamer(handle, &gamer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const local = dynamic_cast<LocalNetworkGamer*>(gamer);
    if (local == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The gamer handle does not name a local gamer.");
    }
    *outGamer = local;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowOptionalGamer(const CNA_Handle handle, NetworkGamer** const outGamer)
{
    *outGamer = nullptr;
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    return BorrowNetworkGamer(handle, outGamer);
}

[[nodiscard]] CNA_Result ValidateSendOptions(const CNA_SendDataOptions options)
{
    if (options > CNA_SEND_DATA_OPTIONS_CHAT) {
        return InvalidArgument(
            "The requested option is not a canonical SendDataOptions identity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyPayload(
    const uint8_t* const data,
    const uint64_t count,
    std::vector<SharpRuntime::bytecs>* const outPayload)
{
    if (data == nullptr && count != 0U) {
        return InvalidArgument("The payload is invalid.");
    }
    if (count > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The payload exceeds the canonical range.");
    }
    outPayload->assign(data, data + count);
    return CNA_RESULT_SUCCESS;
}

// The sender a receive reports is a gamer the session owns, so it is handed back as a view that
// keeps that session alive for as long as the caller holds it.
[[nodiscard]] CNA_Result PublishSender(
    NetworkGamer* const sender,
    const CNA_Handle gamerHandle,
    CNA_Handle* const outSender)
{
    *outSender = CNA_INVALID_HANDLE;
    if (sender == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    CNA_Handle sessionHandle = CNA_INVALID_HANDLE;
    if (cna_network_gamer_get_session(gamerHandle, &sessionHandle) != CNA_RESULT_SUCCESS ||
        sessionHandle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    std::shared_ptr<NetworkSessionResource> session;
    if (GetSessionResource(sessionHandle, &session) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return CreateBorrowedNetworkGamer(
        sender,
        session,
        &session->activeGamerViews,
        sessionHandle,
        outSender);
}

} // namespace

CNA_Result cna_local_network_gamer_create_ext(
    const CNA_SignedInGamerHandle signedInGamerHandle,
    const CNA_NetworkSessionHandle sessionHandle,
    CNA_NetworkGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The NetworkGamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        Microsoft::Xna::Framework::GamerServices::SignedInGamer* signedIn = nullptr;
        if (signedInGamerHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = CNA::C::Detail::BorrowSignedInGamer(
                    signedInGamerHandle,
                    &signedIn);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        NetworkSession* session = nullptr;
        if (sessionHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = CNA::C::Detail::BorrowNetworkSession(
                    sessionHandle,
                    &session);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        return CNA::C::Detail::CreateOwnedLocalNetworkGamer(
            signedIn,
            session,
            sessionHandle,
            outGamer);
    });
}

CNA_Result cna_local_network_gamer_get_is_data_available(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The packet-availability output is null.");
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = gamer->getIsDataAvailableProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_get_signed_in_gamer(
    const CNA_NetworkGamerHandle gamerHandle,
    CNA_SignedInGamerHandle* const outSignedInGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSignedInGamer == nullptr) {
            return InvalidArgument("The SignedInGamer output handle is null.");
        }
        *outSignedInGamer = CNA_INVALID_HANDLE;
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CNA::C::Detail::CreateBorrowedSignedInGamer(
            gamer->getSignedInGamerProperty(),
            outSignedInGamer);
    });
}

CNA_Result cna_local_network_gamer_enable_send_voice(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_NetworkGamerHandle remoteGamerHandle,
    const CNA_Bool enable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* remote = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(remoteGamerHandle, &remote);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->EnableSendVoice(remote, enable != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_send_party_invites(const CNA_NetworkGamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendPartyInvites();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_receive_data(
    const CNA_NetworkGamerHandle gamerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    CNA_NetworkGamerHandle* const outSender,
    uint64_t* const outReceived)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSender == nullptr || outReceived == nullptr ||
            (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The receive output is invalid.");
        }
        *outSender = CNA_INVALID_HANDLE;
        *outReceived = 0U;
        if (capacity > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The receive capacity exceeds the canonical range.");
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> buffer(static_cast<std::size_t>(capacity));
        NetworkGamer* sender = nullptr;
        const int received = gamer->ReceiveData(buffer, sender);
        if (received > 0) {
            std::memcpy(destination, buffer.data(), static_cast<std::size_t>(received));
        }
        *outReceived = received < 0 ? 0U : static_cast<uint64_t>(received);
        return PublishSender(sender, gamerHandle, outSender);
    });
}

CNA_Result cna_local_network_gamer_receive_data_at(
    const CNA_NetworkGamerHandle gamerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    const int32_t offset,
    CNA_NetworkGamerHandle* const outSender,
    uint64_t* const outReceived)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSender == nullptr || outReceived == nullptr ||
            (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The receive output is invalid.");
        }
        *outSender = CNA_INVALID_HANDLE;
        *outReceived = 0U;
        if (capacity > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The receive capacity exceeds the canonical range.");
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> buffer(static_cast<std::size_t>(capacity));
        NetworkGamer* sender = nullptr;
        const int received = gamer->ReceiveData(buffer, static_cast<int>(offset), sender);
        if (received > 0) {
            std::memcpy(destination, buffer.data(), buffer.size());
        }
        *outReceived = received < 0 ? 0U : static_cast<uint64_t>(received);
        return PublishSender(sender, gamerHandle, outSender);
    });
}

CNA_Result cna_local_network_gamer_receive_data_into_packet_reader(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_PacketReaderHandle readerHandle,
    CNA_NetworkGamerHandle* const outSender,
    uint64_t* const outReceived)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSender == nullptr || outReceived == nullptr) {
            return InvalidArgument("The receive output is invalid.");
        }
        *outSender = CNA_INVALID_HANDLE;
        *outReceived = 0U;
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Net::PacketReader* reader = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowPacketReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* sender = nullptr;
        const int received = gamer->ReceiveData(*reader, sender);
        *outReceived = received < 0 ? 0U : static_cast<uint64_t>(received);
        return PublishSender(sender, gamerHandle, outSender);
    });
}

CNA_Result cna_local_network_gamer_send_data(
    const CNA_NetworkGamerHandle gamerHandle,
    const uint8_t* const data,
    const uint64_t count,
    const CNA_SendDataOptions options)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateSendOptions(options);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> payload;
        if (const CNA_Result result = CopyPayload(data, count, &payload);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendData(payload, static_cast<SendDataOptions>(options));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_send_data_range(
    const CNA_NetworkGamerHandle gamerHandle,
    const uint8_t* const data,
    const uint64_t count,
    const int32_t offset,
    const int32_t length,
    const CNA_SendDataOptions options)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateSendOptions(options);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> payload;
        if (const CNA_Result result = CopyPayload(data, count, &payload);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendData(
            payload,
            static_cast<int>(offset),
            static_cast<int>(length),
            static_cast<SendDataOptions>(options));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_send_data_to(
    const CNA_NetworkGamerHandle gamerHandle,
    const uint8_t* const data,
    const uint64_t count,
    const CNA_SendDataOptions options,
    const CNA_NetworkGamerHandle recipientHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateSendOptions(options);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* recipient = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(recipientHandle, &recipient);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> payload;
        if (const CNA_Result result = CopyPayload(data, count, &payload);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendData(payload, static_cast<SendDataOptions>(options), recipient);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_send_data_range_to(
    const CNA_NetworkGamerHandle gamerHandle,
    const uint8_t* const data,
    const uint64_t count,
    const int32_t offset,
    const int32_t length,
    const CNA_SendDataOptions options,
    const CNA_NetworkGamerHandle recipientHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateSendOptions(options);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* recipient = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(recipientHandle, &recipient);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<SharpRuntime::bytecs> payload;
        if (const CNA_Result result = CopyPayload(data, count, &payload);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendData(
            payload,
            static_cast<int>(offset),
            static_cast<int>(length),
            static_cast<SendDataOptions>(options),
            recipient);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_send_packet_writer(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_PacketWriterHandle writerHandle,
    const CNA_SendDataOptions options)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateSendOptions(options);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Net::PacketWriter* writer = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowPacketWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendData(*writer, static_cast<SendDataOptions>(options));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_send_packet_writer_to(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_PacketWriterHandle writerHandle,
    const CNA_SendDataOptions options,
    const CNA_NetworkGamerHandle recipientHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateSendOptions(options);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        NetworkGamer* recipient = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(recipientHandle, &recipient);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Net::PacketWriter* writer = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowPacketWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->SendData(*writer, static_cast<SendDataOptions>(options), recipient);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_clear_packet_queue_ext(
    const CNA_NetworkGamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->ClearPacketQueue();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_local_network_gamer_enqueue_packet_ext(
    const CNA_NetworkGamerHandle gamerHandle,
    const CNA_NetworkEventInfo* const eventInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (eventInfo == nullptr || eventInfo->struct_size < sizeof(CNA_NetworkEventInfo) ||
            eventInfo->struct_version != StructureVersion) {
            return InvalidArgument("The network event description is invalid.");
        }
        if (eventInfo->type > CNA_NETWORK_EVENT_TYPE_STATE_CHANGE ||
            eventInfo->reliable > CNA_SEND_DATA_OPTIONS_CHAT ||
            eventInfo->state > CNA_NETWORK_SESSION_STATE_ENDED ||
            eventInfo->reason > CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) {
            return InvalidArgument("The network event description names an unknown identity.");
        }
        if (eventInfo->packet == nullptr && eventInfo->packet_byte_count != 0U) {
            return InvalidArgument("The network event payload is invalid.");
        }
        LocalNetworkGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowLocalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        NetworkSession::NetworkEvent event;
        event.Type = static_cast<NetworkSession::NetworkEventType>(eventInfo->type);
        event.Reliable = static_cast<SendDataOptions>(eventInfo->reliable);
        event.State = static_cast<NetworkSessionState>(eventInfo->state);
        event.Reason = static_cast<NetworkSessionEndReason>(eventInfo->reason);
        if (const CNA_Result result = BorrowOptionalGamer(eventInfo->gamer, &event.Gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowOptionalGamer(eventInfo->sender, &event.Sender);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (eventInfo->packet_byte_count != 0U) {
            event.Packet.assign(
                eventInfo->packet,
                eventInfo->packet + eventInfo->packet_byte_count);
        }
        gamer->EnqueuePacket(std::move(event));
        return CNA_RESULT_SUCCESS;
    });
}
