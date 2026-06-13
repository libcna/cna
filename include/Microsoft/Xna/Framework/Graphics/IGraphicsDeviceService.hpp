// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /** @brief Provides access to the graphics device and its lifecycle events. */
    class IGraphicsDeviceService
    {
    public:
        /** @brief Virtual destructor. */
        NOXNA virtual ~IGraphicsDeviceService() = default;

        /** @brief Returns the current graphics device. */
        [[nodiscard]] virtual GraphicsDevice* getGraphicsDeviceProperty() const = 0;

        /** @brief Returns the DeviceCreated event handler. */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() = 0;

        /** @brief Returns the DeviceDisposing event handler. */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() = 0;

        /** @brief Returns the DeviceReset event handler. */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceResetEvent() = 0;

        /** @brief Returns the DeviceResetting event handler. */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDeviceResettingEvent() = 0;
    };
}
