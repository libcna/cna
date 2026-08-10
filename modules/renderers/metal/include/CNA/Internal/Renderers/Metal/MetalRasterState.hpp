// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace CNA::Internal::Renderers::Metal
{
    /** @brief Portable representation of the viewport Metal applies to an encoder. */
    struct MetalViewportState
    {
        /** @brief Horizontal origin. */
        double x = 0.0;
        /** @brief Vertical origin. */
        double y = 0.0;
        /** @brief Viewport width. */
        double width = 1.0;
        /** @brief Viewport height. */
        double height = 1.0;
        /** @brief Minimum depth. */
        double minDepth = 0.0;
        /** @brief Maximum depth. */
        double maxDepth = 1.0;

        /**
         * @brief Compares every viewport component.
         *
         * @param other Viewport to compare.
         * @return True when every component matches.
         */
        [[nodiscard]] bool operator==(const MetalViewportState& other) const noexcept = default;
    };

    /** @brief Portable non-negative scissor rectangle ready for a Metal encoder. */
    struct MetalScissorState
    {
        /** @brief Horizontal origin. */
        std::size_t x = 0;
        /** @brief Vertical origin. */
        std::size_t y = 0;
        /** @brief Clamped width. */
        std::size_t width = 0;
        /** @brief Clamped height. */
        std::size_t height = 0;

        /**
         * @brief Compares every rectangle component.
         *
         * @param other Rectangle to compare.
         * @return True when every component matches.
         */
        [[nodiscard]] bool operator==(const MetalScissorState& other) const noexcept = default;
    };

    /**
     * @brief Tracks requested raster state independently from the current attachment extent.
     *
     * Beginning a fresh encoder updates only attachment dimensions. It never erases a caller's
     * requested viewport or scissor, so Clear and render-pass boundaries preserve state.
     */
    class MetalRasterState final
    {
    public:
        /**
         * @brief Records the attachment extent for a newly created encoder.
         *
         * @param width Attachment width in pixels.
         * @param height Attachment height in pixels.
         */
        void BeginEncoder(std::size_t width, std::size_t height) noexcept
        {
            attachmentWidth_ = width;
            attachmentHeight_ = height;
        }

        /**
         * @brief Stores the caller-requested viewport.
         *
         * @param x Horizontal origin.
         * @param y Vertical origin.
         * @param width Viewport width.
         * @param height Viewport height.
         * @param minDepth Minimum depth.
         * @param maxDepth Maximum depth.
         */
        void SetViewport(double x, double y, double width, double height,
                         double minDepth, double maxDepth) noexcept
        {
            viewport_ = {x, y, width, height, minDepth, maxDepth};
            viewportWasSet_ = true;
        }

        /**
         * @brief Stores the caller-requested scissor rectangle without applying it prematurely.
         *
         * @param x Horizontal origin.
         * @param y Vertical origin.
         * @param width Requested width.
         * @param height Requested height.
         */
        void SetScissor(int x, int y, int width, int height) noexcept
        {
            scissorX_ = x;
            scissorY_ = y;
            scissorWidth_ = width;
            scissorHeight_ = height;
            scissorWasSet_ = true;
        }

        /**
         * @brief Enables or disables use of the requested scissor.
         *
         * @param enabled True to clip to the requested rectangle; false for the full attachment.
         */
        void SetScissorEnabled(bool enabled) noexcept
        {
            scissorEnabled_ = enabled;
        }

        /**
         * @brief Returns the viewport to apply to the current encoder.
         *
         * Before the first explicit viewport request, the full current attachment is used.
         *
         * @return Requested viewport, or a full-attachment default.
         */
        [[nodiscard]] MetalViewportState EffectiveViewport() const noexcept
        {
            if (viewportWasSet_)
                return viewport_;
            return {0.0, 0.0, static_cast<double>(attachmentWidth_),
                    static_cast<double>(attachmentHeight_), 0.0, 1.0};
        }

        /**
         * @brief Returns a Metal-valid scissor for the current attachment.
         *
         * Disabled scissoring always yields the full attachment. Enabled requests are intersected
         * with the attachment using widened arithmetic, so negative origins, resizing, and very
         * large extents cannot wrap into an invalid Metal rectangle.
         *
         * @return Effective full or clamped scissor rectangle.
         */
        [[nodiscard]] MetalScissorState EffectiveScissor() const noexcept
        {
            if (!scissorEnabled_ || !scissorWasSet_)
                return {0, 0, attachmentWidth_, attachmentHeight_};

            const std::int64_t attachmentWidth =
                attachmentWidth_ > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
                    ? std::numeric_limits<std::int64_t>::max()
                    : static_cast<std::int64_t>(attachmentWidth_);
            const std::int64_t attachmentHeight =
                attachmentHeight_ > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
                    ? std::numeric_limits<std::int64_t>::max()
                    : static_cast<std::int64_t>(attachmentHeight_);
            const std::int64_t x = static_cast<std::int64_t>(scissorX_);
            const std::int64_t y = static_cast<std::int64_t>(scissorY_);
            const std::int64_t width = std::max<std::int64_t>(0, scissorWidth_);
            const std::int64_t height = std::max<std::int64_t>(0, scissorHeight_);
            const std::int64_t left = std::clamp<std::int64_t>(x, 0, attachmentWidth);
            const std::int64_t top = std::clamp<std::int64_t>(y, 0, attachmentHeight);
            // Inputs are 32-bit int values widened before addition, so both sums fit int64_t.
            const std::int64_t unclampedRight = x + width;
            const std::int64_t unclampedBottom = y + height;
            const std::int64_t right = std::clamp<std::int64_t>(unclampedRight, left, attachmentWidth);
            const std::int64_t bottom = std::clamp<std::int64_t>(unclampedBottom, top, attachmentHeight);
            return {static_cast<std::size_t>(left), static_cast<std::size_t>(top),
                    static_cast<std::size_t>(right - left),
                    static_cast<std::size_t>(bottom - top)};
        }

        /**
         * @brief Returns a non-empty rectangle that is always legal to submit to Metal.
         *
         * An empty logical intersection is represented by ShouldSkipDraw(). Metal itself rejects
         * zero scissor extents, so the encoder receives a harmless one-pixel rectangle while draw
         * submission is suppressed.
         *
         * @return Effective scissor when non-empty, otherwise a legal one-pixel placeholder.
         */
        [[nodiscard]] MetalScissorState NativeScissor() const noexcept
        {
            const MetalScissorState effective=EffectiveScissor();
            if(effective.width>0&&effective.height>0)
                return effective;
            return {0,0,1,1};
        }

        /**
         * @brief Reports whether enabled logical clipping removes every fragment.
         *
         * @return True when the effective logical scissor has an empty width or height.
         */
        [[nodiscard]] bool ShouldSkipDraw() const noexcept
        {
            const MetalScissorState effective=EffectiveScissor();
            return effective.width==0||effective.height==0;
        }

        /**
         * @brief Reports whether requested scissoring is enabled.
         *
         * @return True when the requested rectangle should be applied.
         */
        [[nodiscard]] bool IsScissorEnabled() const noexcept { return scissorEnabled_; }

    private:
        std::size_t attachmentWidth_ = 1;
        std::size_t attachmentHeight_ = 1;
        MetalViewportState viewport_{};
        bool viewportWasSet_ = false;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorWidth_ = 0;
        int scissorHeight_ = 0;
        bool scissorWasSet_ = false;
        bool scissorEnabled_ = false;
    };
}
