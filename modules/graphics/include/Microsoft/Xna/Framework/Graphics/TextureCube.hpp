// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Internal::Renderers
{
    class ITextureCubeRenderer;
}

namespace System::IO { class Stream; }

namespace CNA::Internal::Graphics
{
    /** @brief Identifies XNA packed-vector elements that expose their exact storage word. */
    template<typename T>
    concept TextureCubePackedElement = requires(T value, const T constantValue)
    {
        constantValue.getPackedValueProperty();
        value.setPackedValueProperty(constantValue.getPackedValueProperty());
    };
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a cube map texture (six faces of equal size). */
    class TextureCube : public Texture
    {
    public:
        using Texture::Dispose;

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
        CNAEXT ~TextureCube() override;

        /** @brief Copy-constructs a value wrapper that shares the underlying texture resource. */
        CNAEXT TextureCube(const TextureCube&) = default;
        /** @brief Copy-assigns a value wrapper that shares the underlying texture resource. */
        CNAEXT TextureCube& operator=(const TextureCube&) = default;
        /** @brief Movable — transfers ownership of the GPU renderer handle. */
        CNAEXT TextureCube(TextureCube&&) noexcept = default;
        /** @brief Movable — transfers ownership of the GPU renderer handle. */
        CNAEXT TextureCube& operator=(TextureCube&&) noexcept = default;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the width and height of a cube map face in texels. */
        [[nodiscard]] int getSizeProperty() const;

        // getFormatProperty() and getLevelCountProperty() are inherited from Texture.

        /**
         * @brief Uploads data to the entire specified cube face.
         *
         * @param face         The cube map face to write to.
         * @param data         Pointer to the source Color array.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(CubeMapFace face, const Color* data, int elementCount);

        /**
         * @brief Uploads a subset of data to the entire specified cube face.
         *
         * @param face         The cube map face to write to.
         * @param data         Pointer to the source Color array.
         * @param startIndex   First element within @p data to start reading.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(CubeMapFace face, const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads data to a sub-rectangle of a mip level on the specified cube face.
         *
         * REMED-GFX-135: this call has exactly two outcomes -- the complete requested region is
         * stored, or it throws. It never returns after storing nothing or only part of the region.
         * Every argument is validated before anything is uploaded, so a rejected call leaves the
         * resource and the source array untouched.
         *
         * @param face         The cube map face to write to.
         * @param level        Mip level to write (0 = full size).
         * @param rect         Sub-rectangle to update, or nullptr for the entire level.
         * @param data         Pointer to the source Color array.
         * @param startIndex   First element within @p data to start reading.
         * @param elementCount Number of Color elements the caller offers; must be at least the
         *                     number of texels in the requested region, of which exactly that many
         *                     are read starting at @p startIndex.
         * @throws System::ObjectDisposedException if this TextureCube has been disposed.
         * @throws System::NotSupportedException if this renderer cannot store the requested face,
         *         mip level or region -- including a renderer that creates no cube-map resource.
         * @throws std::invalid_argument if @p data is null.
         * @throws std::out_of_range for an invalid face, level, startIndex, elementCount or
         *         rectangle.
         */
        void SetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                     const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads packed-vector data to an entire cube face.
         *
         * @tparam T An XNA packed-vector type whose packed width matches the cube format.
         * @param face Cube face to update.
         * @param data Source packed elements.
         * @param elementCount Number of available elements.
         */
        template<CNA::Internal::Graphics::TextureCubePackedElement T>
        void SetData(CubeMapFace face, const T* data, int elementCount)
        {
            SetData(face, 0, nullptr, data, 0, elementCount);
        }

        /**
         * @brief Uploads a source window of packed-vector data to an entire cube face.
         *
         * @tparam T An XNA packed-vector type whose packed width matches the cube format.
         * @param face Cube face to update.
         * @param data Source packed elements.
         * @param startIndex First source element.
         * @param elementCount Number of available elements beginning at @p startIndex.
         */
        template<CNA::Internal::Graphics::TextureCubePackedElement T>
        void SetData(CubeMapFace face, const T* data, int startIndex, int elementCount)
        {
            SetData(face, 0, nullptr, data, startIndex, elementCount);
        }

        /**
         * @brief Uploads packed-vector data to a cube-face mip or rectangle.
         *
         * @tparam T An XNA packed-vector type whose packed width matches the cube format.
         * @param face Cube face to update.
         * @param level Mip level beginning at zero.
         * @param rect Destination rectangle, or null for the complete level.
         * @param data Source packed elements.
         * @param startIndex First source element.
         * @param elementCount Number of available elements beginning at @p startIndex.
         */
        template<CNA::Internal::Graphics::TextureCubePackedElement T>
        void SetData(CubeMapFace face, int level,
                     const Microsoft::Xna::Framework::Rectangle* rect,
                     const T* data, int startIndex, int elementCount)
        {
            if (data == nullptr)
                throw std::invalid_argument("TextureCube::SetData: data must not be null");
            using Word = std::remove_cvref_t<decltype(data[0].getPackedValueProperty())>;
            static_assert(std::is_unsigned_v<Word>);
            const int required = ValidateTypedTransferEXT(
                "TextureCube::SetData", face, level, rect,
                startIndex, elementCount, static_cast<int>(sizeof(Word)));
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(required) * sizeof(Word));
            for (int index = 0; index < required; ++index)
            {
                const Word value = data[startIndex + index].getPackedValueProperty();
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                    bytes[static_cast<std::size_t>(index) * sizeof(Word) + byte] =
                        static_cast<std::uint8_t>(value >> (byte * 8u));
            }
            SetTypedDataBytesEXT(face, level, rect, bytes.data(),
                                 static_cast<int>(sizeof(Word)));
        }

        /**
         * @brief Uploads exact block-compressed bytes to an entire cube face.
         *
         * This represents XNA's generic byte-array SetData route for Dxt1, Dxt3 and Dxt5 cube
         * textures. The byte count is the padded 4x4 block payload, not a texel count.
         *
         * @param face         The cube face to update.
         * @param data         Source block bytes.
         * @param elementCount Number of available bytes.
         */
        CNAEXT void SetData(CubeMapFace face, const std::uint8_t* data, int elementCount);

        /**
         * @brief Uploads exact block-compressed bytes to a cube-face mip or block-aligned region.
         *
         * @param face         The cube face to update.
         * @param level        Mip level to update.
         * @param rect         Region in texel coordinates, or null for the whole mip.
         * @param data         Source block bytes.
         * @param startIndex   First source byte.
         * @param elementCount Number of available source bytes from @p startIndex.
         */
        CNAEXT void SetData(CubeMapFace face, int level,
                            const Microsoft::Xna::Framework::Rectangle* rect,
                            const std::uint8_t* data, int startIndex, int elementCount);

        /**
         * @brief Reads all data from the specified cube face into the provided array.
         *
         * @param face         The cube map face to read from.
         * @param data         Output array to receive the Color data.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(CubeMapFace face, Color* data, int elementCount) const;

        /**
         * @brief Reads a subset of data from the entire specified cube face.
         *
         * @param face         The cube map face to read from.
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(CubeMapFace face, Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads data from a sub-rectangle of a mip level on the specified cube face.
         *
         * REMED-GFX-130: every overload above delegates here, so this describes all three. The call
         * has exactly two outcomes. It either writes the requested region's real content into
         * @p data starting at @p startIndex, or it throws and leaves @p data byte-for-byte
         * untouched -- there is no partially written or fabricated result. Elements of @p data
         * outside `[startIndex, startIndex + width * height)` are never modified, even when the
         * caller passes an @p elementCount larger than the region.
         *
         * @param face         The cube map face to read from.
         * @param level        Mip level to read (0 = full size).
         * @param rect         Sub-rectangle to read, or nullptr for the entire level.
         * @param data         Output array to receive the Color data.
         * @param startIndex   First element within @p data to write to.
         * @param elementCount Number of Color elements to read; must be at least the number of
         *                     texels in the requested region.
         * @throws System::ObjectDisposedException if this texture has been disposed.
         * @throws System::NotSupportedException if this graphics renderer cannot read the requested
         *         cube face/mip level back to the CPU (including renderers that create no cube-map
         *         resource at all).
         * @throws std::invalid_argument if @p data is null.
         * @throws std::out_of_range if @p face, @p level, @p startIndex, @p elementCount or the
         *         rectangle is out of range.
         */
        void GetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                     Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads an entire cube face into packed-vector elements.
         *
         * @tparam T An XNA packed-vector type whose packed width matches the cube format.
         * @param face Cube face to read.
         * @param data Destination packed elements.
         * @param elementCount Number of available destination elements.
         */
        template<CNA::Internal::Graphics::TextureCubePackedElement T>
        void GetData(CubeMapFace face, T* data, int elementCount) const
        {
            GetData(face, 0, nullptr, data, 0, elementCount);
        }

        /**
         * @brief Reads an entire cube face into a destination window of packed-vector elements.
         *
         * @tparam T An XNA packed-vector type whose packed width matches the cube format.
         * @param face Cube face to read.
         * @param data Destination packed elements.
         * @param startIndex First destination element.
         * @param elementCount Number of available elements beginning at @p startIndex.
         */
        template<CNA::Internal::Graphics::TextureCubePackedElement T>
        void GetData(CubeMapFace face, T* data, int startIndex, int elementCount) const
        {
            GetData(face, 0, nullptr, data, startIndex, elementCount);
        }

        /**
         * @brief Reads a cube-face mip or rectangle into packed-vector elements.
         *
         * @tparam T An XNA packed-vector type whose packed width matches the cube format.
         * @param face Cube face to read.
         * @param level Mip level beginning at zero.
         * @param rect Source rectangle, or null for the complete level.
         * @param data Destination packed elements.
         * @param startIndex First destination element.
         * @param elementCount Number of available elements beginning at @p startIndex.
         */
        template<CNA::Internal::Graphics::TextureCubePackedElement T>
        void GetData(CubeMapFace face, int level,
                     const Microsoft::Xna::Framework::Rectangle* rect,
                     T* data, int startIndex, int elementCount) const
        {
            if (data == nullptr)
                throw std::invalid_argument("TextureCube::GetData: data must not be null");
            using Word = std::remove_cvref_t<decltype(data[0].getPackedValueProperty())>;
            static_assert(std::is_unsigned_v<Word>);
            const int required = ValidateTypedTransferEXT(
                "TextureCube::GetData", face, level, rect,
                startIndex, elementCount, static_cast<int>(sizeof(Word)));
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(required) * sizeof(Word));
            GetTypedDataBytesEXT(face, level, rect, bytes.data(),
                                 static_cast<int>(sizeof(Word)));
            for (int index = 0; index < required; ++index)
            {
                Word value = 0;
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                    value |= static_cast<Word>(
                        bytes[static_cast<std::size_t>(index) * sizeof(Word) + byte])
                        << (byte * 8u);
                data[startIndex + index].setPackedValueProperty(value);
            }
        }

        /**
         * @brief Creates a TextureCube by decoding DDS image data from a stream.
         *
         * @param device The graphics device to create the texture on.
         * @param stream The input stream containing DDS-encoded cube map data.
         * @return The decoded TextureCube.
         */
        CNAEXT static TextureCube DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream);

        /**
         * @brief Returns a reference to the renderer implementation object.
         *
         * @return Reference to the renderer ITextureCubeRenderer.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::ITextureCubeRenderer& GetRenderer() const { return *renderer_; }

    protected:
        /**
         * @brief Constructs a TextureCube from a pre-built renderer (used by RenderTargetCube).
         *
         * @param device     The owning device.
         * @param size       Width and height of each cube face in texels.
         * @param format     Surface format.
         * @param renderer    Owning pointer to the pre-built GPU renderer.
         * @param levelCount Number of mip levels the renderer actually allocated (1 if none).
         */
        CNAEXT TextureCube(GraphicsDevice& device, int size, SurfaceFormat format,
                          std::shared_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> renderer,
                          int levelCount = 1);

        /** @brief Returns the raw renderer pointer (used by RenderTargetCube to retrieve the RT handle). */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::ITextureCubeRenderer* GetRendererRaw() const { return renderer_.get(); }

        /** @brief Releases the renderer cube texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        [[nodiscard]] int ValidateTypedTransferEXT(
            const char* api, CubeMapFace face, int level,
            const Microsoft::Xna::Framework::Rectangle* rect,
            int startIndex, int elementCount, int elementBytes) const;
        void SetTypedDataBytesEXT(
            CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
            const std::uint8_t* data, int elementBytes);
        void GetTypedDataBytesEXT(
            CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
            std::uint8_t* data, int elementBytes) const;

        int size_;
        // Shared ownership gives C++ value wrappers the reference-resource behavior used by CNA's
        // Texture2D mapping and lets effect renderers observe cube lifetime through weak_ptr.
        std::shared_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> renderer_;
        /// Level-0-only CPU-side pixel shadow, one per face, lazily created on first SetData()
        /// at level 0 and shared with the renderer via ITextureCubeRenderer::ShareCpuPixels() for
        /// GL-style context-loss restoration (mirrors Texture2D::cpuPixels_'s own level-0-only
        /// scope -- higher mip levels are not restored from any CPU shadow, matching how
        /// Texture2D's own context recovery re-derives them via mipmap generation instead).
        std::array<std::shared_ptr<std::vector<uint8_t>>, 6> cpuPixels_;
    };
}
