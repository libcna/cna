// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Creates and drives drawing for the graphics device used by Game. */
    class IGraphicsDeviceManager
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IGraphicsDeviceManager() = default;

        /**
         * @brief Prepares the device for drawing. Returns false when drawing should be skipped.
         * @return true if drawing should proceed; false to skip the current frame.
         */
        [[nodiscard]] virtual bool BeginDraw() = 0;

        /** @brief Creates the graphics device, or recreates it when one already exists. */
        virtual void CreateDevice() = 0;

        /** @brief Finishes drawing and presents the back buffer. */
        virtual void EndDraw() = 0;
    };
}
