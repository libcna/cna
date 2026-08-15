// SPDX-License-Identifier: MS-PL

#include "CNA/C/net_sessions.h"
#include "CnaCApiNetDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"
#include "System/TimeSpan.hpp"

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowNetworkSessionProperties;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::CreateOwnedNetworkSessionProperties;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::Net::AvailableNetworkSession;
using Microsoft::Xna::Framework::Net::AvailableNetworkSessionCollection;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
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
