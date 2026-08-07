// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <memory>
#include <vector>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Internal::Backends
{
    class ITextureCubeBackend;
}

namespace System::IO { class Stream; }

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
        NOXNA ~TextureCube() override;

        /** @brief Not copyable — owns a unique GPU backend handle. */
        NOXNA TextureCube(const TextureCube&) = delete;
        /** @brief Not copyable — owns a unique GPU backend handle. */
        NOXNA TextureCube& operator=(const TextureCube&) = delete;
        /** @brief Movable — transfers ownership of the GPU backend handle. */
        NOXNA TextureCube(TextureCube&&) noexcept = default;
        /** @brief Movable — transfers ownership of the GPU backend handle. */
        NOXNA TextureCube& operator=(TextureCube&&) noexcept = default;

        /** @brief Returns the fully qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

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
         * @throws System::NotSupportedException if this backend cannot store the requested face,
         *         mip level or region -- including a backend that creates no cube-map resource.
         * @throws std::invalid_argument if @p data is null.
         * @throws std::out_of_range for an invalid face, level, startIndex, elementCount or
         *         rectangle.
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
         * @throws System::NotSupportedException if this graphics backend cannot read the requested
         *         cube face/mip level back to the CPU (including backends that create no cube-map
         *         resource at all).
         * @throws std::invalid_argument if @p data is null.
         * @throws std::out_of_range if @p face, @p level, @p startIndex, @p elementCount or the
         *         rectangle is out of range.
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

        /**
         * @brief Returns a reference to the backend implementation object.
         *
         * @return Reference to the backend ITextureCubeBackend.
         */
        NOXNA [[nodiscard]] CNA::Internal::Backends::ITextureCubeBackend& GetBackend() const { return *backend_; }

    protected:
        /**
         * @brief Constructs a TextureCube from a pre-built backend (used by RenderTargetCube).
         *
         * @param device     The owning device.
         * @param size       Width and height of each cube face in texels.
         * @param format     Surface format.
         * @param backend    Owning pointer to the pre-built GPU backend.
         * @param levelCount Number of mip levels the backend actually allocated (1 if none).
         */
        NOXNA TextureCube(GraphicsDevice& device, int size, SurfaceFormat format,
                          std::shared_ptr<CNA::Internal::Backends::ITextureCubeBackend> backend,
                          int levelCount = 1);

        /** @brief Returns the raw backend pointer (used by RenderTargetCube to retrieve the RT handle). */
        NOXNA [[nodiscard]] CNA::Internal::Backends::ITextureCubeBackend* GetBackendRaw() const { return backend_.get(); }

        /** @brief Releases the backend cube texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        int size_;
        // SKIA-149: shared (not unique) ownership so a SkiaEffectBackend can hold a weak_ptr for
        // cube-sampling lifetime tracking, matching Texture2D's identical ITextureBackend pattern.
        // TextureCube itself remains non-copyable; this only lets a second, weak observer outlive
        // a single call without becoming the resource's owner.
        std::shared_ptr<CNA::Internal::Backends::ITextureCubeBackend> backend_;
        /// Level-0-only CPU-side pixel shadow, one per face, lazily created on first SetData()
        /// at level 0 and shared with the backend via ITextureCubeBackend::ShareCpuPixels() for
        /// GL-style context-loss restoration (mirrors Texture2D::cpuPixels_'s own level-0-only
        /// scope -- higher mip levels are not restored from any CPU shadow, matching how
        /// Texture2D's own context recovery re-derives them via mipmap generation instead).
        std::array<std::shared_ptr<std::vector<uint8_t>>, 6> cpuPixels_;
    };
}
