// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"

namespace Microsoft::Xna::Framework::Storage
{
    StorageDeviceNotConnectedException::StorageDeviceNotConnectedException()
        : System::Runtime::InteropServices::ExternalException(
              "The storage device bound to the container is not connected.") {}

    StorageDeviceNotConnectedException::StorageDeviceNotConnectedException(
        const std::string& message)
        : System::Runtime::InteropServices::ExternalException(message) {}

    StorageDeviceNotConnectedException::StorageDeviceNotConnectedException(
        const std::string& message, std::exception_ptr innerException)
        : System::Runtime::InteropServices::ExternalException(message, innerException) {}

    StorageDeviceNotConnectedException::StorageDeviceNotConnectedException(
        const System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context)
        : System::Runtime::InteropServices::ExternalException(
              CNA::Internal::ExceptionSerialization::ReadMessage(info),
              CNA::Internal::ExceptionSerialization::ReadInnerException(info))
    {
        (void)context;
    }

    void StorageDeviceNotConnectedException::GetObjectData(
        System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context) const
    {
        (void)context;
        CNA::Internal::ExceptionSerialization::WriteBaseState(
            info, getMessageProperty(), getInnerExceptionProperty());
    }

} // namespace Microsoft::Xna::Framework::Storage
