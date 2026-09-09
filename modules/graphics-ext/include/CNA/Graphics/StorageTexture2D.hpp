// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers
{
    class IStorageTexture2DRenderer;
}
namespace Microsoft::Xna::Framework::Graphics
{
    class ShaderEffect;
}

namespace CNA::Graphics
{
    class ComputeShader;

    /** @addtogroup cnaext_engine
     *  @{
     */

    /** @brief Declares every operation for which a storage texture is created. */
    enum class StorageTexture2DUsage : std::uint32_t
    {
        /** @brief No operation is declared. This value is not valid by itself. */
        None = 0,
        /** @brief A compute shader may read texels through a storage-image binding. */
        StorageRead = UINT32_C(1) << 0,
        /** @brief A compute shader may write texels through a storage-image binding. */
        StorageWrite = UINT32_C(1) << 1,
        /** @brief A graphics or compute shader may sample the texture. */
        Sampled = UINT32_C(1) << 2,
        /** @brief Linear or mip filtering may be used while sampling. */
        Filterable = UINT32_C(1) << 3,
        /** @brief Mip subresources may be copied or read back from the texture. */
        TransferSource = UINT32_C(1) << 4,
        /** @brief Mip subresources may be uploaded or copied into the texture. */
        TransferDestination = UINT32_C(1) << 5
    };

    /**
     * @brief Combines two storage-texture usage flags.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The combined mask.
     */
    [[nodiscard]] constexpr StorageTexture2DUsage operator|(
        StorageTexture2DUsage left, StorageTexture2DUsage right) noexcept
    {
        return static_cast<StorageTexture2DUsage>(static_cast<std::uint32_t>(left) |
                                                  static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Intersects two storage-texture usage masks.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The flags present in both masks.
     */
    [[nodiscard]] constexpr StorageTexture2DUsage operator&(
        StorageTexture2DUsage left, StorageTexture2DUsage right) noexcept
    {
        return static_cast<StorageTexture2DUsage>(static_cast<std::uint32_t>(left) &
                                                  static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Immutable creation description for a two-dimensional storage texture.
     *
     * Intrinsic validation happens in this value. `StorageTexture2D` separately validates the
     * description against the owning device's cached limits and per-format usage facts before
     * asking a renderer to allocate anything.
     */
    class StorageTexture2DDescriptor final
    {
    public:
        /**
         * @brief Creates an immutable storage-texture description.
         * @param width Level-zero width in texels.
         * @param height Level-zero height in texels.
         * @param mipLevelCount Number of mip levels, including level zero.
         * @param format Exact texel format.
         * @param usage Declared storage, sampling and transfer operations. At least one of
         *        `StorageRead` or `StorageWrite` is required; `Filterable` requires `Sampled`.
         * @throws std::invalid_argument If a dimension/count/format/usage is intrinsically invalid.
         */
        StorageTexture2DDescriptor(
            int width, int height, int mipLevelCount,
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format,
            StorageTexture2DUsage usage);

        /**
         * @brief Returns the level-zero width.
         * @return Positive width in texels.
         */
        [[nodiscard]] int getWidth() const noexcept;

        /**
         * @brief Returns the level-zero height.
         * @return Positive height in texels.
         */
        [[nodiscard]] int getHeight() const noexcept;

        /**
         * @brief Returns the allocated mip count.
         * @return Positive number of mip levels.
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
        [[nodiscard]] StorageTexture2DUsage getUsage() const noexcept;

    private:
        int width_;
        int height_;
        int mipLevelCount_;
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format_;
        StorageTexture2DUsage usage_;
    };

    /**
     * @brief A renderer-neutral two-dimensional image intended for compute read or write access.
     *
     * This CNA engine-layer resource is deliberately separate from XNA `Texture2D`: it has an
     * immutable usage contract suitable for APIs that cannot legally bind CNA's mutable XNA
     * texture storage as an image. It exposes no native image, view, layout or barrier vocabulary.
     */
    class StorageTexture2D final
        : public Microsoft::Xna::Framework::Graphics::GraphicsResource
    {
    public:
        /** @brief Makes the inherited parameterless disposal operation publicly visible. */
        using Microsoft::Xna::Framework::Graphics::GraphicsResource::Dispose;

        /**
         * @brief Validates and creates a storage texture on a graphics device.
         * @param device Device that owns the resource.
         * @param descriptor Immutable dimensions, format and intended operations.
         * @throws System::ObjectDisposedException If @p device is disposed.
         * @throws System::NotSupportedException If a live limit or format requirement is unknown,
         *         unsupported or exceeded, or the renderer refuses allocation.
         */
        StorageTexture2D(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const StorageTexture2DDescriptor& descriptor);

        /** @brief Releases the renderer record before its owning device is destroyed. */
        CNAEXT ~StorageTexture2D() override;

        /** @brief Copy construction is disabled because a tracked resource has one identity. */
        StorageTexture2D(const StorageTexture2D&) = delete;

        /** @brief Copy assignment is disabled because a tracked resource has one identity. */
        StorageTexture2D& operator=(const StorageTexture2D&) = delete;

        /** @brief Move construction is disabled because device tracking stores this object's address. */
        StorageTexture2D(StorageTexture2D&&) = delete;

        /** @brief Move assignment is disabled because device tracking stores this object's address. */
        StorageTexture2D& operator=(StorageTexture2D&&) = delete;

        /**
         * @brief Returns the immutable creation description.
         * @return Description retained by this resource.
         * @throws System::ObjectDisposedException If the resource is disposed.
         */
        [[nodiscard]] const StorageTexture2DDescriptor& getDescriptor() const;

        /**
         * @brief Uploads tightly packed native-format bytes to one mip rectangle.
         * @param mipLevel Zero-based mip level.
         * @param rectangle Rectangle in mip texels, or null for the whole level.
         * @param data Source bytes in the descriptor's native format representation.
         * @param byteCount Exact number of source bytes.
         * @throws System::ObjectDisposedException If the resource is disposed.
         * @throws System::NotSupportedException If transfer-destination use was not declared or
         *         the renderer refuses the upload.
         * @throws std::out_of_range If the mip or rectangle is outside the resource.
         * @throws std::invalid_argument If data, compression alignment or size is invalid.
         */
        CNAEXT void setData(
            int mipLevel, const Microsoft::Xna::Framework::Rectangle* rectangle,
            const void* data, std::size_t byteCount);

        /**
         * @brief Reads tightly packed native-format bytes from one mip rectangle.
         * @param mipLevel Zero-based mip level.
         * @param rectangle Rectangle in mip texels, or null for the whole level.
         * @param data Destination for native-format bytes.
         * @param byteCount Exact destination size.
         * @throws System::ObjectDisposedException If the resource is disposed.
         * @throws System::NotSupportedException If transfer-source use was not declared or the
         *         renderer refuses the readback.
         * @throws std::out_of_range If the mip or rectangle is outside the resource.
         * @throws std::invalid_argument If data, compression alignment or size is invalid.
         */
        CNAEXT void getData(
            int mipLevel, const Microsoft::Xna::Framework::Rectangle* rectangle,
            void* data, std::size_t byteCount) const;

        /**
         * @brief Returns the fully qualified CNA type name.
         * @return `"CNA.Graphics.StorageTexture2D"`.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Releases the internal renderer record before base-class disposal.
         * @param disposing True for explicit/device disposal; false during destruction.
         */
        void Dispose(bool disposing) override;

    private:
        friend class ComputeShader;
        friend class Microsoft::Xna::Framework::Graphics::ShaderEffect;

        struct Prepared
        {
            StorageTexture2DDescriptor descriptor;
            std::shared_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer> renderer;
        };

        static Prepared prepare(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const StorageTexture2DDescriptor& descriptor);

        StorageTexture2D(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, Prepared prepared);

        StorageTexture2DDescriptor descriptor_;
        std::shared_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer> renderer_;
    };

    /** @} */ // end of cnaext_engine
}

#endif // CNA_CNAEXT
