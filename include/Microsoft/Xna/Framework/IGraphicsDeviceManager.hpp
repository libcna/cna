#pragma once

namespace Microsoft::Xna::Framework
{
    /// Creates and drives drawing for the graphics device used by Game.
    class IGraphicsDeviceManager
    {
    public:
        virtual ~IGraphicsDeviceManager() = default;

        /// Prepares the device for drawing. Returns false when drawing should be skipped.
        [[nodiscard]] virtual bool BeginDraw() = 0;

        /// Creates the graphics device, or recreates it when one already exists.
        virtual void CreateDevice() = 0;

        /// Finishes drawing and presents the back buffer.
        virtual void EndDraw() = 0;
    };
}
