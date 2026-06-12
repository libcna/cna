// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /// Provides access to the graphics device service.
    class IGraphicsDeviceService
    {
    public:
        virtual ~IGraphicsDeviceService() = default;

        /// Gets the current graphics device.
        [[nodiscard]] virtual GraphicsDevice* getGraphicsDeviceProperty() const = 0;

        /// Returns the DeviceCreated event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() = 0;

        /// Returns the DeviceDisposing event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() = 0;

        /// Returns the DeviceReset event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceResetEvent() = 0;

        /// Returns the DeviceResetting event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceResettingEvent() = 0;
    };
}
