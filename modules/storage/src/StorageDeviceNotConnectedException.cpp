// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"

namespace Microsoft::Xna::Framework::Storage
{
    StorageDeviceNotConnectedException::StorageDeviceNotConnectedException()
        // XNA .ctor() IL_0000-0006 delegates to ExternalException's default constructor.
        : System::Runtime::InteropServices::ExternalException() {}

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
