#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes the status of the GraphicsDevice.
    enum class GraphicsDeviceStatus
    {
        Normal,
        Lost,
        NotReset,
    };
}
