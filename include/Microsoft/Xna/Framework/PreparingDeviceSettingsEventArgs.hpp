// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /// The arguments to the GraphicsDeviceManager PreparingDeviceSettings event.
    class PreparingDeviceSettingsEventArgs : public System::EventArgs
    {
    public:
        /// Creates a new instance with the given default device settings.
        explicit PreparingDeviceSettingsEventArgs(GraphicsDeviceInformation& graphicsDeviceInformation);

        /// Returns the graphics device settings that will be used in device creation.
        [[nodiscard]] GraphicsDeviceInformation& getGraphicsDeviceInformationProperty();

        /// Returns the graphics device settings that will be used in device creation.
        [[nodiscard]] const GraphicsDeviceInformation& getGraphicsDeviceInformationProperty() const;

    private:
        GraphicsDeviceInformation* graphicsDeviceInformation_;
    };
}
