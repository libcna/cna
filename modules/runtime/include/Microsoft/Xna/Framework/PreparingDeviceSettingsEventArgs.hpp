// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief The arguments to the GraphicsDeviceManager PreparingDeviceSettings event. */
    class PreparingDeviceSettingsEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Creates a new instance with the given default device settings.
         * @param graphicsDeviceInformation The default device settings that may be overridden by subscribers.
         */
        explicit PreparingDeviceSettingsEventArgs(GraphicsDeviceInformation& graphicsDeviceInformation);

        /**
         * @brief Returns the graphics device settings that will be used in device creation.
         * @return A mutable reference to the graphics device information.
         */
        [[nodiscard]] GraphicsDeviceInformation& getGraphicsDeviceInformationProperty();

        /**
         * @brief Returns the graphics device settings that will be used in device creation.
         * @return A const reference to the graphics device information.
         */
        [[nodiscard]] const GraphicsDeviceInformation& getGraphicsDeviceInformationProperty() const;

    private:
        GraphicsDeviceInformation* graphicsDeviceInformation_;
    };
}
