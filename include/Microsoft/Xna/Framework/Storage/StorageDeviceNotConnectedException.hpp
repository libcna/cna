// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "System/Runtime/InteropServices/ExternalException.hpp"
#include <string>

namespace Microsoft::Xna::Framework::Storage
{
    /// Thrown when an operation is attempted on a StorageDevice that is no longer connected.
    class StorageDeviceNotConnectedException
        : public System::Runtime::InteropServices::ExternalException
    {
    public:
        StorageDeviceNotConnectedException();
        explicit StorageDeviceNotConnectedException(const std::string& message);
        StorageDeviceNotConnectedException(const std::string& message,
                                           const std::exception& innerException);
        ~StorageDeviceNotConnectedException() override = default;
    };

} // namespace Microsoft::Xna::Framework::Storage
