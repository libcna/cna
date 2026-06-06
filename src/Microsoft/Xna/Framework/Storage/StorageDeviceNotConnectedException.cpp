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
        const std::string& message, const std::exception& innerException)
        : System::Runtime::InteropServices::ExternalException(message, innerException) {}

} // namespace Microsoft::Xna::Framework::Storage
