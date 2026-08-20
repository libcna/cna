// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Internal::Renderers
{
    class ITexture3DRenderer;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a 3D (volume) texture. */
    class Texture3D : public Texture
    {
    public:
        using Texture::Dispose;

        /**
         * @brief Creates a 3D texture with the given dimensions and format.
         *
         * @param device  The graphics device to create the texture on.
         * @param width   Width in texels.
         * @param height  Height in texels.
         * @param depth   Depth (number of slices) in texels.
         * @param mipMap  True to allocate a full mipmap chain for authored per-level data.
         * @param format  The desired surface format.
         */
        Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format);

        /** @brief Destructor. */
        CNAEXT ~Texture3D() override;

        /**
         * @brief Copying is not allowed.
         *
         * CNAEXT, explicit for clarity: `renderer_`'s `std::unique_ptr` member already makes this
         * implicit, but plans/plan_xnb.md XNB-25 needed a real move path added (see below) and every
         * other similarly-shaped GPU-resource class in this codebase (`VertexBuffer`,
         * `IndexBuffer`, `TextureCube`) already declares both explicitly rather than relying on
         * what the compiler happens to imply.
         */
        CNAEXT Texture3D(const Texture3D&) = delete;
        /** @brief Copy-assignment is not allowed. */
        CNAEXT Texture3D& operator=(const Texture3D&) = delete;
        /**
         * @brief Move-constructs a Texture3D, transferring GPU handle ownership.
         *
         * CNAEXT: this class had no move path at all until plans/plan_xnb.md XNB-25's `Texture3DReader`
         * needed one -- a user-declared destructor already suppressed the implicit move
         * constructor the compiler would otherwise have generated, and the pre-existing
         * `std::unique_ptr` member independently blocks the implicit copy constructor, so this
         * type could not previously be returned by value at all (not even via NRVO, which the
         * standard never guarantees).
         */
        CNAEXT Texture3D(Texture3D&&) noexcept;
        /** @brief Move-assigns a Texture3D, transferring GPU handle ownership. */
        CNAEXT Texture3D& operator=(Texture3D&&) noexcept;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the texture width in texels. */
        [[nodiscard]] int getWidthProperty() const;
        /** @brief Returns the texture height in texels. */
        [[nodiscard]] int getHeightProperty() const;
        /** @brief Returns the texture depth (number of slices) in texels. */
        [[nodiscard]] int getDepthProperty() const;

        // getFormatProperty() and getLevelCountProperty() are inherited from Texture.

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
         * REMED-GFX-135: this call has exactly two outcomes -- the complete requested box is
         * stored, or it throws. It never returns after storing nothing or only part of the box.
         * Every argument is validated before anything is uploaded, so a rejected call leaves the
         * resource and the source array untouched.
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
         * @param elementCount Number of Color elements the caller offers; must be at least the
         *                     number of voxels in the requested box, of which exactly that many
         *                     are read starting at @p startIndex.
         * @throws System::ObjectDisposedException if this Texture3D has been disposed.
         * @throws System::NotSupportedException if this renderer cannot store the requested mip
         *         level or box.
         * @throws std::invalid_argument if @p data is null.
         * @throws std::out_of_range for an invalid level, startIndex, elementCount or box.
         */
        void SetData(int level, int left, int top, int right, int bottom, int front, int back,
                     const Color* data, int startIndex, int elementCount);

        /**
         * @brief Uploads raw byte data to a sub-volume using a native pointer.
         *
         * REMED-GFX-135: shares SetData's completion contract -- it returns only after the whole
         * box has been stored, and otherwise throws.
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
         * @throws System::ObjectDisposedException if this Texture3D has been disposed.
         * @throws System::NotSupportedException if this renderer did not store the whole box.
         * @throws std::invalid_argument if @p data is null.
         */
        CNAEXT void SetDataPointerEXT(int level, int left, int top, int right, int bottom, int front, int back,
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
         * REMED-GFX-130: both overloads above delegate here, so this describes all three. The call
         * has exactly two outcomes. It either writes the requested box's real content into @p data
         * starting at @p startIndex -- slice by slice front to back, each slice row-major with the
         * top row first -- or it throws and leaves @p data byte-for-byte untouched. There is no
         * partially written or fabricated result, and elements outside
         * `[startIndex, startIndex + width * height * depth)` are never modified.
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
         * @param elementCount Number of Color elements to read; must be at least the number of
         *                     voxels in the requested box.
         * @throws System::ObjectDisposedException if this texture has been disposed.
         * @throws System::NotSupportedException if this graphics renderer cannot read the requested
         *         volume/mip level back to the CPU (including renderers that create no volume
         *         resource at all).
         * @throws std::invalid_argument if @p data is null.
         * @throws std::out_of_range if @p level, @p startIndex, @p elementCount or the box is out
         *         of range.
         */
        void GetData(int level, int left, int top, int right, int bottom, int front, int back,
                     Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Returns a reference to the renderer implementation object.
         *
         * @return Reference to the renderer ITexture3DRenderer.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::ITexture3DRenderer& GetRenderer() const { return *renderer_; }

    protected:
        /** @brief Releases the renderer 3D texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        int width_;
        int height_;
        int depth_;
        // SKIA-149: shared (not unique) ownership so a SkiaEffectRenderer can hold a weak_ptr for
        // volume-sampling lifetime tracking, matching Texture2D's identical ITextureRenderer
        // pattern. Texture3D itself remains non-copyable; this only lets a second, weak observer
        // outlive a single call without becoming the resource's owner.
        std::shared_ptr<CNA::Internal::Renderers::ITexture3DRenderer> renderer_;
    };
}
