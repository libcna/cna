// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers
{
    class ITexture2DArrayRenderer;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class ShaderEffect;
}

namespace CNA::Graphics
{
    /** @addtogroup cnaext_engine
     *  @{
     */

    /** @brief Declares the operations for which a texture array is created. */
    enum class Texture2DArrayUsage : std::uint32_t
    {
        /** @brief No operation is declared. This value is not valid by itself. */
        None = 0,
        /** @brief The complete array may be sampled by a shader. */
        Sampled = UINT32_C(1) << 0,
        /** @brief Linear or mip filtering may be used while sampling. */
        Filterable = UINT32_C(1) << 1,
        /** @brief Array subresources may be copied or read back from the texture. */
        TransferSource = UINT32_C(1) << 2,
        /** @brief Array subresources may be uploaded or copied into the texture. */
        TransferDestination = UINT32_C(1) << 3
    };

    /**
     * @brief Combines two texture-array usage flags.
     *
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The combined mask.
     */
    [[nodiscard]] constexpr Texture2DArrayUsage operator|(Texture2DArrayUsage left,
                                                          Texture2DArrayUsage right) noexcept
    {
        return static_cast<Texture2DArrayUsage>(static_cast<std::uint32_t>(left) |
                                                static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Intersects two texture-array usage masks.
     *
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The flags present in both masks.
     */
    [[nodiscard]] constexpr Texture2DArrayUsage operator&(Texture2DArrayUsage left,
                                                          Texture2DArrayUsage right) noexcept
    {
        return static_cast<Texture2DArrayUsage>(static_cast<std::uint32_t>(left) &
                                                static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Immutable creation description for a sampled two-dimensional texture array.
     *
     * The constructor validates facts intrinsic to the description. `Texture2DArray` then
     * validates the value against the owning device's cached live limits and per-format usage
     * flags before it asks the renderer to allocate anything.
     */
    class Texture2DArrayDescriptor final
    {
    public:
        /**
         * @brief Creates an immutable texture-array description.
         *
         * @param width Width of every layer at mip level zero, in texels.
         * @param height Height of every layer at mip level zero, in texels.
         * @param layerCount Number of array layers.
         * @param mipLevelCount Number of mip levels, including level zero.
         * @param format Exact texel or compressed-block format.
         * @param usage Operations that may be performed on the resource. `Sampled` is required;
         *              `Filterable` additionally requires `Sampled`.
         * @throws std::invalid_argument If a count is not positive, the mip count exceeds the
         *         complete chain, the format is not a declared `SurfaceFormat`, the usage has an
         *         unknown bit, or the usage does not describe a sampled texture array.
         */
        Texture2DArrayDescriptor(
            int width, int height, int layerCount, int mipLevelCount,
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format,
            Texture2DArrayUsage usage);

        /**
         * @brief Returns the level-zero width in texels.
         * @return Positive width supplied at construction.
         */
        [[nodiscard]] int getWidth() const noexcept;

        /**
         * @brief Returns the level-zero height in texels.
         * @return Positive height supplied at construction.
         */
        [[nodiscard]] int getHeight() const noexcept;

        /**
         * @brief Returns the number of array layers.
         * @return Positive layer count supplied at construction.
         */
        [[nodiscard]] int getLayerCount() const noexcept;

        /**
         * @brief Returns the number of allocated mip levels.
         * @return Positive mip count supplied at construction.
         */
        [[nodiscard]] int getMipLevelCount() const noexcept;

        /**
         * @brief Returns the exact surface format.
         * @return Format supplied at construction.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat getFormat() const noexcept;

        /**
         * @brief Returns the immutable operation mask.
         * @return Usage supplied at construction.
         */
        [[nodiscard]] Texture2DArrayUsage getUsage() const noexcept;

    private:
        int width_;
        int height_;
        int layerCount_;
        int mipLevelCount_;
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format_;
        Texture2DArrayUsage usage_;
    };

    /**
     * @brief A renderer-neutral sampled array of equally sized two-dimensional textures.
     *
     * This is a CNA engine-layer resource, not an addition to XNA's `Texture2D`. It participates
     * in `GraphicsDevice` resource tracking and disposal, owns only a renderer-neutral internal
     * record, and deliberately exposes no native image or image-view handle.
     */
    class Texture2DArray final
        : public Microsoft::Xna::Framework::Graphics::GraphicsResource
    {
    public:
        /** @brief Makes the inherited parameterless disposal operation publicly visible. */
        using Microsoft::Xna::Framework::Graphics::GraphicsResource::Dispose;

        /**
         * @brief Validates and creates a sampled texture array on a graphics device.
         *
         * Validation completes before the renderer factory is called and before this object is
         * registered with the device. Unknown capability facts are not treated as support.
         *
         * @param device Device that owns the resource.
         * @param descriptor Immutable dimensions, format and intended operations.
         * @throws System::ObjectDisposedException If @p device is disposed.
         * @throws System::NotSupportedException If a live limit or format-usage requirement is
         *         unavailable, unknown or exceeded, or the renderer does not create the resource.
         */
        Texture2DArray(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const Texture2DArrayDescriptor& descriptor);

        /** @brief Releases the renderer resource before its owning device is destroyed. */
        CNAEXT ~Texture2DArray() override;

        /** @brief Copy construction is disabled because a tracked resource has one identity. */
        Texture2DArray(const Texture2DArray&) = delete;

        /** @brief Copy assignment is disabled because a tracked resource has one identity. */
        Texture2DArray& operator=(const Texture2DArray&) = delete;

        /** @brief Move construction is disabled because device tracking stores this object's address. */
        Texture2DArray(Texture2DArray&&) = delete;

        /** @brief Move assignment is disabled because device tracking stores this object's address. */
        Texture2DArray& operator=(Texture2DArray&&) = delete;

        /**
         * @brief Returns the immutable creation description.
         * @return Description retained by this resource.
         * @throws System::ObjectDisposedException If this resource has been disposed.
         */
        [[nodiscard]] const Texture2DArrayDescriptor& getDescriptor() const;

        /**
         * @brief Uploads tightly packed native-format bytes to one layer and mip rectangle.
         *
         * A null rectangle addresses the complete mip level. Compressed rectangles begin on a
         * block boundary; their width/height are whole blocks unless that edge reaches the mip
         * boundary. The byte count must exactly match the addressed texels or blocks.
         *
         * @param layer Zero-based array layer.
         * @param mipLevel Zero-based mip level.
         * @param rectangle Rectangle in mip-level texels, or null for the complete level.
         * @param data Source bytes in the descriptor's native `SurfaceFormat` representation.
         * @param byteCount Exact number of source bytes.
         * @throws System::ObjectDisposedException If this resource has been disposed.
         * @throws System::NotSupportedException If transfer-destination usage was not declared or
         *         the renderer refuses the transfer.
         * @throws std::out_of_range If a subresource or rectangle lies outside the resource.
         * @throws std::invalid_argument If data, compression alignment or byte count is invalid.
         */
        CNAEXT void setData(
            int layer, int mipLevel, const Microsoft::Xna::Framework::Rectangle* rectangle,
            const void* data, std::size_t byteCount);

        /**
         * @brief Reads tightly packed native-format bytes from one layer and mip rectangle.
         *
         * Rectangle, compression and exact-size rules are identical to @ref setData.
         *
         * @param layer Zero-based array layer.
         * @param mipLevel Zero-based mip level.
         * @param rectangle Rectangle in mip-level texels, or null for the complete level.
         * @param data Destination for native-format bytes.
         * @param byteCount Exact destination size in bytes.
         * @throws System::ObjectDisposedException If this resource has been disposed.
         * @throws System::NotSupportedException If transfer-source usage was not declared or the
         *         renderer refuses the transfer.
         * @throws std::out_of_range If a subresource or rectangle lies outside the resource.
         * @throws std::invalid_argument If data, compression alignment or byte count is invalid.
         */
        CNAEXT void getData(
            int layer, int mipLevel, const Microsoft::Xna::Framework::Rectangle* rectangle,
            void* data, std::size_t byteCount) const;

        /**
         * @brief Returns the fully qualified CNA type name.
         * @return `"CNA.Graphics.Texture2DArray"`.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Releases the internal renderer record before base-class disposal.
         * @param disposing True for explicit/device disposal; false during destruction.
         */
        void Dispose(bool disposing) override;

    private:
        friend class Microsoft::Xna::Framework::Graphics::ShaderEffect;

        struct Prepared
        {
            Texture2DArrayDescriptor descriptor;
            std::shared_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer> renderer;
        };

        static Prepared prepare(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const Texture2DArrayDescriptor& descriptor);

        Texture2DArray(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            Prepared prepared);

        Texture2DArrayDescriptor descriptor_;
        std::shared_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer> renderer_;
    };

    /** @} */ // end of cnaext_engine
}

#endif // CNA_CNAEXT
