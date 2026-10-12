// SPDX-License-Identifier: MS-PL

#include "CNA/C/net.h"
#include "CnaCApiNetDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

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

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::GetLastError;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NativeOrdinal;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;
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
        return OutOfRange(message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetProperties(
    const CNA_Handle handle,
    std::shared_ptr<NetworkSessionPropertiesResource>* const outProperties)
{
    return LookupHandle(handle, ObjectKind::NetworkSessionProperties, outProperties);
}

[[nodiscard]] CNA_Result GetEnumerator(
    const CNA_Handle handle,
    std::shared_ptr<PropertyEnumeratorResource>* const outEnumerator)
{
    return LookupHandle(handle, ObjectKind::NetworkSessionPropertyEnumerator, outEnumerator);
}

[[nodiscard]] CNA_Result GetWriter(
    const CNA_Handle handle,
    std::shared_ptr<PacketWriterResource>* const outWriter)
{
    return LookupHandle(handle, ObjectKind::PacketWriter, outWriter);
}

[[nodiscard]] CNA_Result GetReader(
    const CNA_Handle handle,
    std::shared_ptr<PacketReaderResource>* const outReader)
{
    return LookupHandle(handle, ObjectKind::PacketReader, outReader);
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
    outValue->has_value = ToCBool(value.has_value());
    outValue->reserved[0] = 0U;
    outValue->reserved[1] = 0U;
    outValue->reserved[2] = 0U;
    outValue->value = value.has_value() ? static_cast<int32_t>(*value) : INT32_C(0);
}

[[nodiscard]] CNA_Result StoreQualityOfService(
    const QualityOfService& value,
    CNA_QualityOfService* const outValue)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outValue, "The quality-of-service output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outValue->is_available = ToCBool(value.getIsAvailableProperty());
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

// The packet buffers are private base-from-member details of the canonical reader and writer, so
// the adapter reaches them through the binary base's own stream accessor rather than by reaching
// into the type.
[[nodiscard]] CNA_Result GetPacketBuffer(
    System::IO::Stream* const stream,
    System::IO::MemoryStream** const outBuffer)
{
    auto* const buffer = dynamic_cast<System::IO::MemoryStream*>(stream);
    if (buffer == nullptr) {
        return InternalError("The canonical packet buffer is not an in-memory stream.");
    }
    *outBuffer = buffer;
    return CNA_RESULT_SUCCESS;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result BorrowNetworkSessionProperties(
    const CNA_Handle handle,
    NetworkSessionProperties** const outProperties)
{
    if (outProperties == nullptr) {
        return InvalidArgument("The borrowed session-property output is null.");
    }
    *outProperties = nullptr;
    std::shared_ptr<NetworkSessionPropertiesResource> properties;
    if (const CNA_Result result = GetProperties(handle, &properties);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outProperties = properties->value.get();
    return CNA_RESULT_SUCCESS;
}

CNA_Result CreateOwnedNetworkSessionProperties(
    const NetworkSessionProperties& value,
    CNA_Handle* const outProperties)
{
    if (outProperties == nullptr) {
        return InvalidArgument("The NetworkSessionProperties output handle is null.");
    }
    *outProperties = CNA_INVALID_HANDLE;
    return CreateResourceHandle<NetworkSessionPropertiesResource>(
        ObjectKind::NetworkSessionProperties,
        std::make_unique<NetworkSessionProperties>(value),
        outProperties);
}

CNA_Result BorrowPacketReader(const CNA_Handle handle, PacketReader** const outReader)
{
    if (outReader == nullptr) {
        return InvalidArgument("The borrowed PacketReader output is null.");
    }
    *outReader = nullptr;
    std::shared_ptr<PacketReaderResource> reader;
    if (const CNA_Result result = GetReader(handle, &reader); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outReader = reader->value.get();
    return CNA_RESULT_SUCCESS;
}

CNA_Result BorrowPacketWriter(const CNA_Handle handle, PacketWriter** const outWriter)
{
    if (outWriter == nullptr) {
        return InvalidArgument("The borrowed PacketWriter output is null.");
    }
    *outWriter = nullptr;
    std::shared_ptr<PacketWriterResource> writer;
    if (const CNA_Result result = GetWriter(handle, &writer); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outWriter = writer->value.get();
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

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
        *outHasJoinError = ToCBool(error.hasJoinError);
        *outJoinError = error.hasJoinError
            ? static_cast<CNA_NetworkSessionJoinError>(error.joinError)
            : CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_FOUND;
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
        properties->value->Insert(static_cast<int>(index), ToNative(value));
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
        *outRemoved = ToCBool(properties->value->Remove(ToNative(value)));
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
        *outContains = ToCBool(properties->value->Contains(ToNative(value)));
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
            return BufferTooSmall("The property copy destination is too small.");
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
            return InternalError("The canonical property list returned no enumerator.");
        }
        if (const CNA_Result result = CreateHandle(ObjectKind::NetworkSessionPropertyEnumerator,
            resource,
            outEnumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        *outHasCurrent = ToCBool(enumerator->hasCurrent);
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
        return ReleaseHandle(propertiesHandle);
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
            return BufferTooSmall("The packet copy destination is too small.");
        }
        if (!data.empty()) {
            std::memcpy(destination, data.data(), data.size());
        }
        return CNA_RESULT_SUCCESS;
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
            return Overflow("The packet payload exceeds the canonical range.");
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

CNA_Result cna_packet_reader_copy_data_ext(
    const CNA_PacketReaderHandle readerHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The packet copy destination is invalid.");
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
        const std::vector<SharpRuntime::bytecs>& data = buffer->GetBuffer();
        *outBytes = data.size();
        if (capacity < data.size()) {
            return BufferTooSmall("The packet copy destination is too small.");
        }
        if (!data.empty()) {
            std::memcpy(destination, data.data(), data.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

// The routes generated from modules/c-api/spec/Net.toml.
#include "generated/Net.inc"
