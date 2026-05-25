#pragma once

#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /// Event arguments used before graphics device settings are applied.
    class PreparingDeviceSettingsEventArgs : public System::EventArgs
    {
    public:
        explicit PreparingDeviceSettingsEventArgs(GraphicsDeviceInformation& graphicsDeviceInformation);

        /// Gets the graphics device settings being prepared.
        [[nodiscard]] GraphicsDeviceInformation& getGraphicsDeviceInformationProperty();

        /// Gets the graphics device settings being prepared.
        [[nodiscard]] const GraphicsDeviceInformation& getGraphicsDeviceInformationProperty() const;

    private:
        GraphicsDeviceInformation* graphicsDeviceInformation_;
    };
}
