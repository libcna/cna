// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace System::IO { class Stream; }

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a cube map texture (six faces of equal size). */
    class TextureCube : public GraphicsResource
    {
    public:
        /**
         * @brief Creates a cube map texture with the given face size and format.
         *
         * @param device  The graphics device to create the texture on.
         * @param size    Width and height of each cube face in texels.
         * @param mipMap  True to generate a full mipmap chain.
         * @param format  The desired surface format.
         */
        TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format);

        /** @brief Destructor. */
        NOXNA ~TextureCube() override = default;

        /** @brief Returns the fully qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the width and height of a cube map face in texels. */
        [[nodiscard]] int getSizeProperty() const;
        /** @brief Returns the surface format of the texture data. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;
        /** @brief Returns the number of mipmap levels in this texture. */
        [[nodiscard]] int getLevelCountProperty() const;

        /**
         * @brief Uploads data to the entire specified cube face.
         *
         * @param face         The cube map face to write to.
         * @param data         Pointer to the source Color array.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(CubeMapFace face, const Color* data, int elementCount);

        /**
         * @brief Uploads data to a sub-rectangle of a mip level on the specified cube face.
         *
         * @param face         The cube map face to write to.
         * @param level        Mip level to write (0 = full size).
         * @param rect         Sub-rectangle to update, or nullptr for the entire level.
         * @param data         Pointer to the source Color array.
         * @param startIndex   First element within @p data to start reading.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                     const Color* data, int startIndex, int elementCount);

        /**
         * @brief Reads all data from the specified cube face into the provided array.
         *
         * @param face         The cube map face to read from.
         * @param data         Output array to receive the Color data.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(CubeMapFace face, Color* data, int elementCount) const;

        /**
         * @brief Reads data from a sub-rectangle of a mip level on the specified cube face.
         *
         * @param face         The cube map face to read from.
         * @param level        Mip level to read (0 = full size).
         * @param rect         Sub-rectangle to read, or nullptr for the entire level.
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                     Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Creates a TextureCube by decoding DDS image data from a stream.
         *
         * @param device The graphics device to create the texture on.
         * @param stream The input stream containing DDS-encoded cube map data.
         * @return The decoded TextureCube.
         */
        NOXNA static TextureCube DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream);

    private:
        int size_;
        SurfaceFormat format_;
        int levelCount_;
    };
}
