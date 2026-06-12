// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes the drawable area of the render target.
    class Viewport
    {
    private:
        int Height_;
        int Width_;

    public:
        int x;
        int y;
        float minDepth;
        float maxDepth;

        DEF_PROP(int, Height, getter1, setter1, member0, static0, constret1, ref1, constmet1)
        DEF_PROP(int, Width,  getter1, setter1, member0, static0, constret1, ref1, constmet1)

        /// Constructs an empty viewport.
        Viewport();

        /// Constructs a viewport from position and size. MinDepth=0, MaxDepth=1.
        Viewport(int x, int y, int width, int height);

        /// Constructs a viewport from a rectangle. MinDepth=0, MaxDepth=1.
        explicit Viewport(const Microsoft::Xna::Framework::Rectangle& bounds);

        /// Read-only: width / height ratio (0 if either dimension is zero).
        [[nodiscard]] float getAspectRatioProperty() const;

        /// Gets the viewport area as a Rectangle.
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle getBoundsProperty() const;

        /// Sets the viewport area from a Rectangle.
        void setBoundsProperty(const Microsoft::Xna::Framework::Rectangle& value);

        /// The subset guaranteed to be visible on lower-quality displays (same as Bounds).
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle getTitleSafeAreaProperty() const;

        /// Projects a world-space point to screen space.
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Project(
            Microsoft::Xna::Framework::Vector3 source,
            const Microsoft::Xna::Framework::Matrix& projection,
            const Microsoft::Xna::Framework::Matrix& view,
            const Microsoft::Xna::Framework::Matrix& world) const;

        /// Unprojects a screen-space point back to world space.
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Unproject(
            Microsoft::Xna::Framework::Vector3 source,
            const Microsoft::Xna::Framework::Matrix& projection,
            const Microsoft::Xna::Framework::Matrix& view,
            const Microsoft::Xna::Framework::Matrix& world) const;
    };
}
