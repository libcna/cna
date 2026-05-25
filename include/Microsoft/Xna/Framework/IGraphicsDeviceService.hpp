#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace Microsoft::Xna::Framework
{
    /// Provides access to the graphics device service.
    class IGraphicsDeviceService
    {
    public:
        virtual ~IGraphicsDeviceService() = default;

        /// Gets the current graphics device, or nullptr when no device is available.
        [[nodiscard]] virtual Graphics::GraphicsDevice* getGraphicsDeviceProperty() const = 0;
    };
}
