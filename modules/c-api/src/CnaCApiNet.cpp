// SPDX-License-Identifier: MS-PL

#include "CNA/C/net.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinError.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/TimeSpan.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetLastError;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinError;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionState;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
using Microsoft::Xna::Framework::Net::PacketReader;
using Microsoft::Xna::Framework::Net::PacketWriter;
using Microsoft::Xna::Framework::Net::QualityOfService;
using Microsoft::Xna::Framework::Net::SendDataOptions;

constexpr uint32_t StructureVersion = UINT32_C(1);

template<typename TEnum>
[[nodiscard]] constexpr uint32_t NativeOrdinal(const TEnum value) noexcept
{
    return static_cast<uint32_t>(static_cast<std::underlying_type_t<TEnum>>(value));
}

static_assert(NativeOrdinal(NetworkSessionEndReason::ClientSignedOut) ==
    CNA_NETWORK_SESSION_END_REASON_CLIENT_SIGNED_OUT);
static_assert(NativeOrdinal(NetworkSessionEndReason::HostEndedSession) ==
    CNA_NETWORK_SESSION_END_REASON_HOST_ENDED_SESSION);
static_assert(NativeOrdinal(NetworkSessionEndReason::RemovedByHost) ==
    CNA_NETWORK_SESSION_END_REASON_REMOVED_BY_HOST);
static_assert(NativeOrdinal(NetworkSessionEndReason::Disconnected) ==
    CNA_NETWORK_SESSION_END_REASON_DISCONNECTED);

static_assert(NativeOrdinal(NetworkSessionJoinError::SessionNotFound) ==
    CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_FOUND);
static_assert(NativeOrdinal(NetworkSessionJoinError::SessionNotJoinable) ==
    CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_JOINABLE);
static_assert(NativeOrdinal(NetworkSessionJoinError::SessionFull) ==
    CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_FULL);

static_assert(NativeOrdinal(NetworkSessionState::Lobby) == CNA_NETWORK_SESSION_STATE_LOBBY);
static_assert(NativeOrdinal(NetworkSessionState::Playing) == CNA_NETWORK_SESSION_STATE_PLAYING);
static_assert(NativeOrdinal(NetworkSessionState::Ended) == CNA_NETWORK_SESSION_STATE_ENDED);

static_assert(NativeOrdinal(NetworkSessionType::Local) == CNA_NETWORK_SESSION_TYPE_LOCAL);
static_assert(NativeOrdinal(NetworkSessionType::SystemLink) ==
    CNA_NETWORK_SESSION_TYPE_SYSTEM_LINK);
static_assert(NativeOrdinal(NetworkSessionType::PlayerMatch) ==
    CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH);
static_assert(NativeOrdinal(NetworkSessionType::Ranked) == CNA_NETWORK_SESSION_TYPE_RANKED);
static_assert(NativeOrdinal(NetworkSessionType::LocalWithLeaderboards) ==
    CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS);

static_assert(NativeOrdinal(SendDataOptions::None) == CNA_SEND_DATA_OPTIONS_NONE);
static_assert(NativeOrdinal(SendDataOptions::Reliable) == CNA_SEND_DATA_OPTIONS_RELIABLE);
static_assert(NativeOrdinal(SendDataOptions::InOrder) == CNA_SEND_DATA_OPTIONS_IN_ORDER);
static_assert(NativeOrdinal(SendDataOptions::ReliableInOrder) ==
    CNA_SEND_DATA_OPTIONS_RELIABLE_IN_ORDER);
static_assert(NativeOrdinal(SendDataOptions::Chat) == CNA_SEND_DATA_OPTIONS_CHAT);

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

struct NetworkSessionPropertiesResource final {
    std::unique_ptr<NetworkSessionProperties> value;
    std::size_t activeEnumerators = 0U;
};

// The canonical enumerator dereferences its backing vector without a bounds check before the first
// advance, so the C layer tracks whether a current element exists and refuses the read instead.
struct PropertyEnumeratorResource final {
    std::unique_ptr<System::Collections::Generic::IEnumerator<std::optional<int>>> value;
    std::shared_ptr<NetworkSessionPropertiesResource> owner;
    bool hasCurrent = false;

    PropertyEnumeratorResource() = default;
    PropertyEnumeratorResource(const PropertyEnumeratorResource&) = delete;
    PropertyEnumeratorResource& operator=(const PropertyEnumeratorResource&) = delete;

    ~PropertyEnumeratorResource()
    {
        if (owner != nullptr && owner->activeEnumerators != 0U) {
            owner->activeEnumerators -= 1U;
        }
    }
};

struct PacketWriterResource final {
    std::unique_ptr<PacketWriter> value;
};

struct PacketReaderResource final {
    std::unique_ptr<PacketReader> value;
};

// The canonical list forwards Insert/RemoveAt straight to the backing vector without a bounds
// check, so an out-of-range index there is undefined behavior rather than an exception. The C layer
// decides those two cases itself instead of handing an invalid iterator to the canonical call.
[[nodiscard]] CNA_Result CheckedListIndex(
    const int32_t index,
    const int32_t count,
    const bool allowEnd,
    const char* const message)
{
    const int32_t limit = allowEnd ? count : count - 1;
    if (index < 0 || index > limit) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE, message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetProperties(
    const CNA_Handle handle,
    std::shared_ptr<NetworkSessionPropertiesResource>* const outProperties)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::NetworkSessionProperties,
        outProperties);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned NetworkSessionProperties handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetEnumerator(
    const CNA_Handle handle,
    std::shared_ptr<PropertyEnumeratorResource>* const outEnumerator)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::NetworkSessionPropertyEnumerator,
        outEnumerator);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned session-property enumerator handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetWriter(
    const CNA_Handle handle,
    std::shared_ptr<PacketWriterResource>* const outWriter)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::PacketWriter, outWriter);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned PacketWriter handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetReader(
    const CNA_Handle handle,
    std::shared_ptr<PacketReaderResource>* const outReader)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::PacketReader, outReader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned PacketReader handle is invalid for this call.");
}

[[nodiscard]] std::optional<int> ToNative(const CNA_OptionalInt32 value)
{
    if (value.has_value == CNA_FALSE) {
        return std::nullopt;
    }
    return std::optional<int>(static_cast<int>(value.value));
}

void StoreOptional(const std::optional<int>& value, CNA_OptionalInt32* const outValue)
{
    outValue->has_value = value.has_value() ? CNA_TRUE : CNA_FALSE;
    outValue->reserved[0] = 0U;
    outValue->reserved[1] = 0U;
    outValue->reserved[2] = 0U;
    outValue->value = value.has_value() ? static_cast<int32_t>(*value) : INT32_C(0);
}

[[nodiscard]] CNA_Result StoreQualityOfService(
    const QualityOfService& value,
    CNA_QualityOfService* const outValue)
{
    if (outValue == nullptr || outValue->struct_size < sizeof(CNA_QualityOfService) ||
        outValue->struct_version != StructureVersion) {
        return InvalidArgument("The quality-of-service output structure is invalid.");
    }
    outValue->is_available = value.getIsAvailableProperty() ? CNA_TRUE : CNA_FALSE;
    std::memset(outValue->reserved, 0, sizeof(outValue->reserved));
    outValue->average_roundtrip_ticks =
        static_cast<int64_t>(value.getAverageRoundtripTimeProperty().getTicksProperty());
    outValue->minimum_roundtrip_ticks =
        static_cast<int64_t>(value.getMinimumRoundtripTimeProperty().getTicksProperty());
    outValue->bytes_per_second_downstream =
        static_cast<int32_t>(value.getBytesPerSecondDownstreamProperty());
    outValue->bytes_per_second_upstream =
        static_cast<int32_t>(value.getBytesPerSecondUpstreamProperty());
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result WriterCommand(const CNA_Handle handle, TCallable&& callable)
{
    std::shared_ptr<PacketWriterResource> writer;
    if (const CNA_Result result = GetWriter(handle, &writer); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*writer->value);
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result ReaderCommand(
    const CNA_Handle handle,
    const void* const output,
    const char* const message,
    TCallable&& callable)
{
    if (output == nullptr) {
        return InvalidArgument(message);
    }
    std::shared_ptr<PacketReaderResource> reader;
    if (const CNA_Result result = GetReader(handle, &reader); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    callable(*reader->value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] Color ToNativeColor(const CNA_Color value)
{
    return Color(value.r, value.g, value.b, value.a);
}

[[nodiscard]] Matrix ToNativeMatrix(const CNA_Matrix& value)
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

void StoreMatrix(const Matrix& value, CNA_Matrix* const outValue)
{
    outValue->m11 = value.M11;
    outValue->m12 = value.M12;
    outValue->m13 = value.M13;
    outValue->m14 = value.M14;
    outValue->m21 = value.M21;
    outValue->m22 = value.M22;
    outValue->m23 = value.M23;
    outValue->m24 = value.M24;
    outValue->m31 = value.M31;
    outValue->m32 = value.M32;
    outValue->m33 = value.M33;
    outValue->m34 = value.M34;
    outValue->m41 = value.M41;
    outValue->m42 = value.M42;
    outValue->m43 = value.M43;
    outValue->m44 = value.M44;
}

// The packet buffers are private base-from-member details of the canonical reader and writer, so
// the adapter reaches them through the binary base's own stream accessor rather than by reaching
// into the type.
[[nodiscard]] CNA_Result GetPacketBuffer(
    System::IO::Stream* const stream,
    System::IO::MemoryStream** const outBuffer)
{
    auto* const buffer = dynamic_cast<System::IO::MemoryStream*>(stream);
    if (buffer == nullptr) {
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "The canonical packet buffer is not an in-memory stream.");
    }
    *outBuffer = buffer;
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_quality_of_service_init(CNA_QualityOfService* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreQualityOfService(QualityOfService::CreateInternal(), outValue);
    });
}

CNA_Result cna_quality_of_service_init_measured(
    const int64_t roundtripTicks,
    CNA_QualityOfService* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreQualityOfService(
            QualityOfService::CreateInternal(System::TimeSpan(roundtripTicks)),
            outValue);
    });
}

CNA_Result cna_net_get_last_join_error(
    CNA_NetworkSessionJoinError* const outJoinError,
    CNA_Bool* const outHasJoinError)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outJoinError == nullptr || outHasJoinError == nullptr) {
            return InvalidArgument("The join-error output is null.");
        }
        // Reading the record must not itself clear it, so the diagnostic stays inspectable in the
        // same order as the rest of the per-thread error information.
        const CNA::C::Detail::LastError& error = GetLastError();
        *outHasJoinError = error.hasJoinError ? CNA_TRUE : CNA_FALSE;
        *outJoinError = error.hasJoinError
            ? static_cast<CNA_NetworkSessionJoinError>(error.joinError)
            : CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_FOUND;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_create(
    CNA_NetworkSessionPropertiesHandle* const outProperties)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProperties == nullptr) {
            return InvalidArgument("The NetworkSessionProperties output handle is null.");
        }
        *outProperties = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<NetworkSessionPropertiesResource>();
        resource->value = std::make_unique<NetworkSessionProperties>();
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::NetworkSessionProperties,
            resource,
            outProperties);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned NetworkSessionProperties handle could not be created.");
    });
}

CNA_Result cna_network_session_properties_get_count(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The property-count output is null.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(properties->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_get_is_read_only(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    CNA_Bool* const outIsReadOnly)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsReadOnly == nullptr) {
            return InvalidArgument("The read-only output is null.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsReadOnly = properties->value->getIsReadOnlyProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_get_item(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const int32_t index,
    CNA_OptionalInt32* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The property output is null.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CheckedListIndex(
                index,
                static_cast<int32_t>(properties->value->getCountProperty()),
                false,
                "The property index is outside the list.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        StoreOptional(properties->value->getItem(static_cast<int>(index)), outValue);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_set_item(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const int32_t index,
    const CNA_OptionalInt32 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index < 0) {
            return InvalidArgument("The property index must not be negative.");
        }
        properties->value->setItem(static_cast<int>(index), ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_index_of(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const CNA_OptionalInt32 value,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidArgument("The property-index output is null.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(properties->value->IndexOf(ToNative(value)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_insert(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const int32_t index,
    const CNA_OptionalInt32 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CheckedListIndex(
                index,
                static_cast<int32_t>(properties->value->getCountProperty()),
                true,
                "The property insertion index is outside the list.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        properties->value->Insert(static_cast<int>(index), ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_remove_at(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const int32_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = CheckedListIndex(
                index,
                static_cast<int32_t>(properties->value->getCountProperty()),
                false,
                "The property removal index is outside the list.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        properties->value->RemoveAt(static_cast<int>(index));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_add(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const CNA_OptionalInt32 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        properties->value->Add(ToNative(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_remove(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const CNA_OptionalInt32 value,
    CNA_Bool* const outRemoved)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRemoved == nullptr) {
            return InvalidArgument("The removal output is null.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRemoved = properties->value->Remove(ToNative(value)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_contains(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    const CNA_OptionalInt32 value,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidArgument("The containment output is null.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = properties->value->Contains(ToNative(value)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_clear(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        properties->value->Clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_copy_to(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    CNA_OptionalInt32* const destination,
    const uint64_t capacity,
    const int32_t index,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The property copy destination is invalid.");
        }
        if (index < 0) {
            return InvalidArgument("The property copy index must not be negative.");
        }
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const auto count = static_cast<uint64_t>(properties->value->getCountProperty());
        *outCount = count;
        const uint64_t required = count + static_cast<uint64_t>(index);
        if (capacity < required) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The property copy destination is too small.");
        }

        std::vector<std::optional<int>> native(static_cast<std::size_t>(required));
        properties->value->CopyTo(native, static_cast<int>(index));
        for (std::size_t element = 0U; element < native.size(); ++element) {
            StoreOptional(native[element], destination + element);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_properties_create_enumerator(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle,
    CNA_NetworkSessionPropertyEnumeratorHandle* const outEnumerator)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnumerator == nullptr) {
            return InvalidArgument("The enumerator output handle is null.");
        }
        *outEnumerator = CNA_INVALID_HANDLE;
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<PropertyEnumeratorResource>();
        resource->value.reset(properties->value->GetEnumerator());
        if (resource->value == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The canonical property list returned no enumerator.");
        }
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::NetworkSessionPropertyEnumerator,
            resource,
            outEnumerator);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned session-property enumerator handle could not be created.");
        }
        resource->owner = properties;
        properties->activeEnumerators += 1U;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_property_enumerator_move_next(
    const CNA_NetworkSessionPropertyEnumeratorHandle enumeratorHandle,
    CNA_Bool* const outHasCurrent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasCurrent == nullptr) {
            return InvalidArgument("The enumerator advance output is null.");
        }
        std::shared_ptr<PropertyEnumeratorResource> enumerator;
        if (const CNA_Result result = GetEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        enumerator->hasCurrent = enumerator->value->MoveNext();
        *outHasCurrent = enumerator->hasCurrent ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_property_enumerator_get_current(
    const CNA_NetworkSessionPropertyEnumeratorHandle enumeratorHandle,
    CNA_OptionalInt32* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The enumerator value output is null.");
        }
        std::shared_ptr<PropertyEnumeratorResource> enumerator;
        if (const CNA_Result result = GetEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!enumerator->hasCurrent) {
            return InvalidState("The enumerator is not positioned on a property.");
        }
        StoreOptional(enumerator->value->Current(), outValue);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_property_enumerator_reset(
    const CNA_NetworkSessionPropertyEnumeratorHandle enumeratorHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PropertyEnumeratorResource> enumerator;
        if (const CNA_Result result = GetEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        enumerator->value->Reset();
        enumerator->hasCurrent = false;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_network_session_property_enumerator_destroy(
    const CNA_NetworkSessionPropertyEnumeratorHandle enumeratorHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PropertyEnumeratorResource> enumerator;
        if (const CNA_Result result = GetEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(enumeratorHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned session-property enumerator handle could not be released.");
    });
}

CNA_Result cna_network_session_properties_destroy(
    const CNA_NetworkSessionPropertiesHandle propertiesHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<NetworkSessionPropertiesResource> properties;
        if (const CNA_Result result = GetProperties(propertiesHandle, &properties);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (properties->activeEnumerators != 0U) {
            return InvalidState(
                "Every enumerator over this property list must be destroyed first.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(propertiesHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned NetworkSessionProperties handle could not be released.");
    });
}

CNA_Result cna_packet_writer_create(
    const int32_t capacity,
    CNA_PacketWriterHandle* const outWriter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWriter == nullptr) {
            return InvalidArgument("The PacketWriter output handle is null.");
        }
        *outWriter = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<PacketWriterResource>();
        resource->value = std::make_unique<PacketWriter>(static_cast<int>(capacity));
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::PacketWriter,
            resource,
            outWriter);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned PacketWriter handle could not be created.");
    });
}

CNA_Result cna_packet_writer_get_length(
    const CNA_PacketWriterHandle writerHandle,
    int32_t* const outLength)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLength == nullptr) {
            return InvalidArgument("The packet-length output is null.");
        }
        return WriterCommand(writerHandle, [outLength](PacketWriter& writer) {
            *outLength = static_cast<int32_t>(writer.getLengthProperty());
        });
    });
}

CNA_Result cna_packet_writer_get_position(
    const CNA_PacketWriterHandle writerHandle,
    int32_t* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPosition == nullptr) {
            return InvalidArgument("The packet-position output is null.");
        }
        return WriterCommand(writerHandle, [outPosition](PacketWriter& writer) {
            *outPosition = static_cast<int32_t>(writer.getPositionProperty());
        });
    });
}

CNA_Result cna_packet_writer_set_position(
    const CNA_PacketWriterHandle writerHandle,
    const int32_t position)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [position](PacketWriter& writer) {
            writer.setPositionProperty(static_cast<int>(position));
        });
    });
}

CNA_Result cna_packet_writer_write_color(
    const CNA_PacketWriterHandle writerHandle,
    const CNA_Color value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(ToNativeColor(value));
        });
    });
}

CNA_Result cna_packet_writer_write_matrix(
    const CNA_PacketWriterHandle writerHandle,
    const CNA_Matrix value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [&value](PacketWriter& writer) {
            writer.Write(ToNativeMatrix(value));
        });
    });
}

CNA_Result cna_packet_writer_write_quaternion(
    const CNA_PacketWriterHandle writerHandle,
    const CNA_Quaternion value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(Quaternion(value.x, value.y, value.z, value.w));
        });
    });
}

CNA_Result cna_packet_writer_write_vector2(
    const CNA_PacketWriterHandle writerHandle,
    const CNA_Vector2 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(Vector2(value.x, value.y));
        });
    });
}

CNA_Result cna_packet_writer_write_vector3(
    const CNA_PacketWriterHandle writerHandle,
    const CNA_Vector3 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(Vector3(value.x, value.y, value.z));
        });
    });
}

CNA_Result cna_packet_writer_write_vector4(
    const CNA_PacketWriterHandle writerHandle,
    const CNA_Vector4 value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(Vector4(value.x, value.y, value.z, value.w));
        });
    });
}

CNA_Result cna_packet_writer_write_single(
    const CNA_PacketWriterHandle writerHandle,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(value);
        });
    });
}

CNA_Result cna_packet_writer_write_double(
    const CNA_PacketWriterHandle writerHandle,
    const double value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return WriterCommand(writerHandle, [value](PacketWriter& writer) {
            writer.Write(value);
        });
    });
}

CNA_Result cna_packet_writer_copy_data_ext(
    const CNA_PacketWriterHandle writerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The packet copy destination is invalid.");
        }
        std::shared_ptr<PacketWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IO::MemoryStream* buffer = nullptr;
        if (const CNA_Result result = GetPacketBuffer(
                writer->value->getBaseStreamProperty(),
                &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        writer->value->Flush();
        const std::vector<SharpRuntime::bytecs>& data = buffer->GetBuffer();
        *outBytes = data.size();
        if (capacity < data.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The packet copy destination is too small.");
        }
        if (!data.empty()) {
            std::memcpy(destination, data.data(), data.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_packet_writer_destroy(const CNA_PacketWriterHandle writerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PacketWriterResource> writer;
        if (const CNA_Result result = GetWriter(writerHandle, &writer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(writerHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned PacketWriter handle could not be released.");
    });
}

CNA_Result cna_packet_reader_create(
    const int32_t capacity,
    CNA_PacketReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidArgument("The PacketReader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<PacketReaderResource>();
        resource->value = std::make_unique<PacketReader>(static_cast<int>(capacity));
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::PacketReader,
            resource,
            outReader);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned PacketReader handle could not be created.");
    });
}

CNA_Result cna_packet_reader_set_data_ext(
    const CNA_PacketReaderHandle readerHandle,
    const uint8_t* const data,
    const uint64_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr && count != 0U) {
            return InvalidArgument("The packet payload is invalid.");
        }
        if (count > static_cast<uint64_t>(std::numeric_limits<SharpRuntime::intcs>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The packet payload exceeds the canonical range.");
        }
        std::shared_ptr<PacketReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IO::MemoryStream* buffer = nullptr;
        if (const CNA_Result result = GetPacketBuffer(
                reader->value->getBaseStreamProperty(),
                &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        buffer->SetLength(0);
        buffer->setPositionProperty(0);
        if (count != 0U) {
            buffer->Write(data, 0, static_cast<SharpRuntime::intcs>(count));
        }
        buffer->setPositionProperty(0);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_packet_reader_get_length(
    const CNA_PacketReaderHandle readerHandle,
    int32_t* const outLength)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outLength,
            "The packet-length output is null.",
            [outLength](PacketReader& reader) {
                *outLength = static_cast<int32_t>(reader.getLengthProperty());
            });
    });
}

CNA_Result cna_packet_reader_get_position(
    const CNA_PacketReaderHandle readerHandle,
    int32_t* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outPosition,
            "The packet-position output is null.",
            [outPosition](PacketReader& reader) {
                *outPosition = static_cast<int32_t>(reader.getPositionProperty());
            });
    });
}

CNA_Result cna_packet_reader_set_position(
    const CNA_PacketReaderHandle readerHandle,
    const int32_t position)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PacketReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->setPositionProperty(static_cast<int>(position));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_packet_reader_read_color(
    const CNA_PacketReaderHandle readerHandle,
    CNA_Color* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The color output is null.",
            [outValue](PacketReader& reader) {
                const Color value = reader.ReadColor();
                outValue->r = value.getRProperty();
                outValue->g = value.getGProperty();
                outValue->b = value.getBProperty();
                outValue->a = value.getAProperty();
            });
    });
}

CNA_Result cna_packet_reader_read_matrix(
    const CNA_PacketReaderHandle readerHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The matrix output is null.",
            [outValue](PacketReader& reader) { StoreMatrix(reader.ReadMatrix(), outValue); });
    });
}

CNA_Result cna_packet_reader_read_quaternion(
    const CNA_PacketReaderHandle readerHandle,
    CNA_Quaternion* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The quaternion output is null.",
            [outValue](PacketReader& reader) {
                const Quaternion value = reader.ReadQuaternion();
                outValue->x = value.X;
                outValue->y = value.Y;
                outValue->z = value.Z;
                outValue->w = value.W;
            });
    });
}

CNA_Result cna_packet_reader_read_vector2(
    const CNA_PacketReaderHandle readerHandle,
    CNA_Vector2* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The vector output is null.",
            [outValue](PacketReader& reader) {
                const Vector2 value = reader.ReadVector2();
                outValue->x = value.X;
                outValue->y = value.Y;
            });
    });
}

CNA_Result cna_packet_reader_read_vector3(
    const CNA_PacketReaderHandle readerHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The vector output is null.",
            [outValue](PacketReader& reader) {
                const Vector3 value = reader.ReadVector3();
                outValue->x = value.X;
                outValue->y = value.Y;
                outValue->z = value.Z;
            });
    });
}

CNA_Result cna_packet_reader_read_vector4(
    const CNA_PacketReaderHandle readerHandle,
    CNA_Vector4* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The vector output is null.",
            [outValue](PacketReader& reader) {
                const Vector4 value = reader.ReadVector4();
                outValue->x = value.X;
                outValue->y = value.Y;
                outValue->z = value.Z;
                outValue->w = value.W;
            });
    });
}

CNA_Result cna_packet_reader_read_single(
    const CNA_PacketReaderHandle readerHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The value output is null.",
            [outValue](PacketReader& reader) { *outValue = reader.ReadSingle(); });
    });
}

CNA_Result cna_packet_reader_read_double(
    const CNA_PacketReaderHandle readerHandle,
    double* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReaderCommand(
            readerHandle,
            outValue,
            "The value output is null.",
            [outValue](PacketReader& reader) { *outValue = reader.ReadDouble(); });
    });
}

CNA_Result cna_packet_reader_destroy(const CNA_PacketReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PacketReaderResource> reader;
        if (const CNA_Result result = GetReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(readerHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The owned PacketReader handle could not be released.");
    });
}
