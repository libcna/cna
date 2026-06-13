// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Describes the view bounds for the render-target surface. */
    class Viewport
    {
    private:
        int Height_;
        int Width_;

    public:
        /** @brief X coordinate of the upper-left corner of the viewport in pixels. */
        int x;
        /** @brief Y coordinate of the upper-left corner of the viewport in pixels. */
        int y;
        /** @brief Lower limit of the depth range of the viewport. */
        float minDepth;
        /** @brief Upper limit of the depth range of the viewport. */
        float maxDepth;

        DEF_PROP(int, Height, getter1, setter1, member0, static0, constret1, ref1, constmet1)
        DEF_PROP(int, Width,  getter1, setter1, member0, static0, constret1, ref1, constmet1)

        /** @brief Constructs an empty viewport with all fields set to zero. */
        Viewport();

        /**
         * @brief Constructs a viewport from a position and size; MinDepth=0, MaxDepth=1.
         *
         * @param x      X coordinate of the upper-left corner in pixels.
         * @param y      Y coordinate of the upper-left corner in pixels.
         * @param width  Width of the viewport in pixels.
         * @param height Height of the viewport in pixels.
         */
        Viewport(int x, int y, int width, int height);

        /**
         * @brief Constructs a viewport from a rectangle; MinDepth=0, MaxDepth=1.
         *
         * @param bounds Rectangle that defines the location and size of the viewport.
         */
        explicit Viewport(const Microsoft::Xna::Framework::Rectangle& bounds);

        /**
         * @brief Gets the aspect ratio of this viewport (width / height).
         * @return Aspect ratio, or 0 if either dimension is zero.
         */
        [[nodiscard]] float getAspectRatioProperty() const;

        /**
         * @brief Gets the viewport area as a Rectangle.
         * @return Rectangle whose position and size match this viewport.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle getBoundsProperty() const;

        /**
         * @brief Sets the viewport area from a Rectangle.
         * @param value Rectangle that defines the new viewport position and size.
         */
        void setBoundsProperty(const Microsoft::Xna::Framework::Rectangle& value);

        /**
         * @brief Gets the subset of the viewport guaranteed to be visible on lower-quality displays.
         * @return Same rectangle as Bounds.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle getTitleSafeAreaProperty() const;

        /**
         * @brief Projects a world-space point into screen space.
         *
         * @param source     The world-space point to project.
         * @param projection The projection matrix.
         * @param view       The view matrix.
         * @param world      The world matrix.
         * @return The projected screen-space point.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Project(
            Microsoft::Xna::Framework::Vector3 source,
            const Microsoft::Xna::Framework::Matrix& projection,
            const Microsoft::Xna::Framework::Matrix& view,
            const Microsoft::Xna::Framework::Matrix& world) const;

        /**
         * @brief Unprojects a screen-space point back into world space.
         *
         * @param source     The screen-space point to unproject.
         * @param projection The projection matrix.
         * @param view       The view matrix.
         * @param world      The world matrix.
         * @return The unprojected world-space point.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Unproject(
            Microsoft::Xna::Framework::Vector3 source,
            const Microsoft::Xna::Framework::Matrix& projection,
            const Microsoft::Xna::Framework::Matrix& view,
            const Microsoft::Xna::Framework::Matrix& world) const;
    };
}
