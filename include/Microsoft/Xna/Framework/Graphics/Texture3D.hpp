// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Internal::Backends
{
    class ITexture3DBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a 3D (volume) texture. */
    class Texture3D : public GraphicsResource
    {
    public:
        using GraphicsResource::Dispose;

        /**
         * @brief Creates a 3D texture with the given dimensions and format.
         *
         * @param device  The graphics device to create the texture on.
         * @param width   Width in texels.
         * @param height  Height in texels.
         * @param depth   Depth (number of slices) in texels.
         * @param mipMap  True to generate a full mipmap chain.
         * @param format  The desired surface format.
         */
        Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format);

        /** @brief Destructor. */
        NOXNA ~Texture3D() override;

        /** @brief Returns the fully qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the texture width in texels. */
        [[nodiscard]] int getWidthProperty() const;
        /** @brief Returns the texture height in texels. */
        [[nodiscard]] int getHeightProperty() const;
        /** @brief Returns the texture depth (number of slices) in texels. */
        [[nodiscard]] int getDepthProperty() const;
        /** @brief Returns the surface format of the texture data. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;
        /** @brief Returns the number of mipmap levels in this texture. */
        [[nodiscard]] int getLevelCountProperty() const;

        /**
         * @brief Uploads data to the entire texture.
         *
         * @param data         Pointer to the Color array to upload.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(const Color* data, int elementCount);

        /**
         * @brief Uploads a subset of data to the entire texture.
         *
         * @param data         Pointer to the source Color array.
         * @param startIndex   First element within @p data to start reading.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads data to a sub-volume of the specified mip level.
         *
         * @param level        Mip level to write (0 = full size).
         * @param left         Left boundary of the sub-volume in texels.
         * @param top          Top boundary of the sub-volume in texels.
         * @param right        Right boundary (exclusive) of the sub-volume in texels.
         * @param bottom       Bottom boundary (exclusive) of the sub-volume in texels.
         * @param front        Front boundary of the sub-volume in texels (slice index).
         * @param back         Back boundary (exclusive) of the sub-volume in texels.
         * @param data         Pointer to the source Color array.
         * @param startIndex   First element within @p data to start reading.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(int level, int left, int top, int right, int bottom, int front, int back,
                     const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads raw byte data to a sub-volume using a native pointer.
         *
         * @param level      Mip level to write.
         * @param left       Left boundary in texels.
         * @param top        Top boundary in texels.
         * @param right      Right boundary (exclusive) in texels.
         * @param bottom     Bottom boundary (exclusive) in texels.
         * @param front      Front boundary in texels.
         * @param back       Back boundary (exclusive) in texels.
         * @param data       Pointer to the raw byte data.
         * @param dataLength Size of the data in bytes.
         */
        NOXNA void SetDataPointerEXT(int level, int left, int top, int right, int bottom, int front, int back,
                                     const void* data, int dataLength);

        /**
         * @brief Reads all texture data into the provided array.
         *
         * @param data         Output array to receive the Color data.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int elementCount) const;

        /**
         * @brief Reads a subset of texture data into the provided array.
         *
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads data from a sub-volume of the specified mip level.
         *
         * @param level        Mip level to read (0 = full size).
         * @param left         Left boundary of the sub-volume in texels.
         * @param top          Top boundary of the sub-volume in texels.
         * @param right        Right boundary (exclusive) in texels.
         * @param bottom       Bottom boundary (exclusive) in texels.
         * @param front        Front boundary in texels.
         * @param back         Back boundary (exclusive) in texels.
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(int level, int left, int top, int right, int bottom, int front, int back,
                     Color* data, int startIndex, int elementCount) const;

    protected:
        /** @brief Releases the backend 3D texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        int width_;
        int height_;
        int depth_;
        SurfaceFormat format_;
        int levelCount_;
        std::unique_ptr<CNA::Internal::Backends::ITexture3DBackend> backend_;
    };
}
