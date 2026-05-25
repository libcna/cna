#include "Microsoft/Xna/Framework/PreparingDeviceSettingsEventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    PreparingDeviceSettingsEventArgs::PreparingDeviceSettingsEventArgs(GraphicsDeviceInformation& graphicsDeviceInformation)
        : graphicsDeviceInformation_(&graphicsDeviceInformation)
    {
    }

    GraphicsDeviceInformation& PreparingDeviceSettingsEventArgs::getGraphicsDeviceInformationProperty()
    {
        return *graphicsDeviceInformation_;
    }

    const GraphicsDeviceInformation& PreparingDeviceSettingsEventArgs::getGraphicsDeviceInformationProperty() const
    {
        return *graphicsDeviceInformation_;
    }
}
