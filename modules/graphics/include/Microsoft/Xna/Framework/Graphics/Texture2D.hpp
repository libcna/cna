// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

namespace System::IO { class Stream; }

namespace Microsoft::Xna::Framework
{
    struct Vector2;
    struct Vector4;
}

namespace CNA::Internal::Renderers
{
    class ITextureRenderer;
}

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Alpha8;
    struct Bgr565;
    struct Bgra5551;
    struct Bgra4444;
    struct HalfSingle;
    struct HalfVector2;
    struct HalfVector4;
    struct NormalizedByte2;
    struct NormalizedByte4;
    struct Rg32;
    struct Rgba1010102;
    struct Rgba64;
}

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Renderers;

    /** @brief Represents a 2D texture. Mirrors XNA 4.0 Texture2D. */
    class Texture2D : public Texture
    {
    public:
        using Texture::Dispose;

        /** @brief Constructs a default, uninitialized Texture2D. */
        Texture2D();

        /**
         * @brief Loads a Texture2D from a file asset by name.
         * @param assetName Path to the image file.
         */
        CNAEXT explicit Texture2D(const std::string& assetName);
        /**
         * @brief Loads a Texture2D from a file asset using the given device.
         * @param assetName     Path to the image file.
         * @param graphicsDevice The device to upload the texture to.
         */
        CNAEXT Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice);

        /**
         * @brief Creates an empty Texture2D with Color format.
         * @param graphicsDevice The device to create the texture on.
         * @param width          Width in pixels.
         * @param height         Height in pixels.
         * @throws System::NotSupportedException if @p width or @p height exceeds the active
         *         renderer's maximum texture dimension (see GraphicsDevice::GetMaxTextureDimension()).
         */
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height);

        /**
         * @brief Creates a Texture2D with explicit format and optional mip levels.
         * @param graphicsDevice The device to create the texture on.
         * @param width          Width in pixels.
         * @param height         Height in pixels.
         * @param mipMap         True to generate a full mipmap chain.
         * @param format         The desired surface format.
         * @throws System::NotSupportedException if @p width or @p height exceeds the active
         *         renderer's maximum texture dimension (see GraphicsDevice::GetMaxTextureDimension()).
         */
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height,
                  bool mipMap, SurfaceFormat format);

        /** @brief Destructor. */
        CNAEXT ~Texture2D() override;

        Texture2D(const Texture2D&) = default;
        Texture2D& operator=(const Texture2D&) = default;
        Texture2D(Texture2D&&) noexcept = default;
        Texture2D& operator=(Texture2D&&) noexcept = default;

        /** @brief Returns the fully qualified .NET type name of this class. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the texture width in pixels. */
        [[nodiscard]] int getWidthProperty()  const { return width; }
        /** @brief Returns the texture height in pixels. */
        [[nodiscard]] int getHeightProperty() const { return height; }
        /** @brief Returns a Rectangle with origin (0,0) and the texture dimensions. */
        [[nodiscard]] Rectangle getBoundsProperty() const;

        // Format and LevelCount are inherited from Texture.

        /**
         * @brief Uploads pixel data to a Color-compatible texture.
         *
         * In a Skia build, ColorBgraEXT and ColorSrgbEXT also use this overload. Each Color's
         * R/G/B/A properties name the four raw transfer bytes in order; ColorBgraEXT sampling
         * therefore interprets those bytes as B/G/R/A, while ColorSrgbEXT decodes RGB once.
         * @param data         Pointer to the Color array.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(const Color* data, int elementCount);

        /**
         * @brief Uploads raw Color-compatible data to a mip level and optional sub-rectangle.
         * @param level        Mip level to write (0 = full size).
         * @param rect         Sub-rectangle to update, or nullptr for the entire level.
         * @param data         Pointer to the Color array.
         * @param startIndex   First element in @p data to use.
         * @param elementCount Number of Color elements to upload.
         * @throws std::runtime_error if @p level is 0, @p rect does not cover the full level,
         *         and the CPU-side pixel shadow has been freed because context recovery is
         *         disabled (see GraphicsDevice::SetContextRecoveryEnabled) — reconstructing a
         *         partial update from a freed shadow would silently corrupt already-uploaded
         *         GPU pixels outside @p rect.
         */
        void SetData(int level, const Rectangle* rect, const Color* data, int startIndex, int elementCount);

        /** @brief Uploads exact packed Bgr565 texels to a Bgr565 texture. */
        void SetData(const PackedVector::Bgr565* data, int elementCount);
        /** @brief Uploads exact packed Bgr565 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Bgr565* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed Bgra4444 texels to a Bgra4444 texture. */
        void SetData(const PackedVector::Bgra4444* data, int elementCount);
        /** @brief Uploads exact packed Bgra4444 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Bgra4444* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed Bgra5551 texels to a Bgra5551 texture. */
        void SetData(const PackedVector::Bgra5551* data, int elementCount);
        /** @brief Uploads exact packed Bgra5551 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Bgra5551* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed Rgba1010102 texels to an Rgba1010102 texture. */
        void SetData(const PackedVector::Rgba1010102* data, int elementCount);
        /** @brief Uploads exact packed Rgba1010102 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Rgba1010102* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed Alpha8 texels to an Alpha8 texture. */
        void SetData(const PackedVector::Alpha8* data, int elementCount);
        /** @brief Uploads exact packed Alpha8 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Alpha8* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed Rg32 texels to an Rg32 texture. */
        void SetData(const PackedVector::Rg32* data, int elementCount);
        /** @brief Uploads exact packed Rg32 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Rg32* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed Rgba64 texels to an Rgba64 texture. */
        void SetData(const PackedVector::Rgba64* data, int elementCount);
        /** @brief Uploads exact packed Rgba64 texels to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::Rgba64* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed signed-normalized XY bytes. */
        void SetData(const PackedVector::NormalizedByte2* data, int elementCount);
        /** @brief Uploads exact packed signed-normalized XY bytes to a mip or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::NormalizedByte2* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed signed-normalized XYZW bytes. */
        void SetData(const PackedVector::NormalizedByte4* data, int elementCount);
        /** @brief Uploads exact packed signed-normalized XYZW bytes to a mip or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::NormalizedByte4* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact IEEE binary32 values to a Single texture. */
        void SetData(const float* data, int elementCount);
        /** @brief Uploads exact IEEE binary32 values to a Single mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const float* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact two-component binary32 values to a Vector2 texture. */
        void SetData(const Vector2* data, int elementCount);
        /** @brief Uploads exact two-component binary32 values to a Vector2 mip or rectangle. */
        void SetData(int level, const Rectangle* rect, const Vector2* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact four-component binary32 values to a Vector4 texture. */
        void SetData(const Vector4* data, int elementCount);
        /** @brief Uploads exact four-component binary32 values to a Vector4 mip or rectangle. */
        void SetData(int level, const Rectangle* rect, const Vector4* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed binary16 values to a HalfSingle texture. */
        void SetData(const PackedVector::HalfSingle* data, int elementCount);
        /** @brief Uploads exact packed binary16 values to a HalfSingle mip or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::HalfSingle* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed RG binary16 values to a HalfVector2 texture. */
        void SetData(const PackedVector::HalfVector2* data, int elementCount);
        /** @brief Uploads exact packed RG binary16 values to a HalfVector2 mip or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::HalfVector2* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact packed RGBA binary16 values to HalfVector4 or HdrBlendable. */
        void SetData(const PackedVector::HalfVector4* data, int elementCount);
        /** @brief Uploads exact packed RGBA binary16 values to a mip level or rectangle. */
        void SetData(int level, const Rectangle* rect, const PackedVector::HalfVector4* data,
                     int startIndex, int elementCount);
        /** @brief Uploads exact unsigned bytes to a ByteEXT texture, or compressed blocks to a Dxt1/Dxt3/Dxt5 texture. */
        CNAEXT void SetData(const std::uint8_t* data, int elementCount);
        /**
         * @brief Uploads exact unsigned bytes to a ByteEXT texture, or exact compressed blocks
         * to a Dxt1/Dxt3/Dxt5 texture. For a compressed format, level/rect coordinates are texel
         * space and must be block-aligned or reach the level's edge; elementCount is the exact
         * padded block byte count for the requested region.
         */
        CNAEXT void SetData(int level, const Rectangle* rect, const std::uint8_t* data,
                           int startIndex, int elementCount);
        /** @brief Uploads exact unsigned 16-bit values to a UShortEXT texture. */
        CNAEXT void SetData(const std::uint16_t* data, int elementCount);
        /** @brief Uploads exact unsigned 16-bit values to a UShortEXT mip level or rectangle. */
        CNAEXT void SetData(int level, const Rectangle* rect, const std::uint16_t* data,
                           int startIndex, int elementCount);

        /** @brief Preserves the legacy null-pointer overload resolution after packed overloads. */
        void SetData(std::nullptr_t, int elementCount)
        {
            SetData(static_cast<const Color*>(nullptr), elementCount);
        }
        /** @brief Preserves the legacy null-pointer overload resolution after packed overloads. */
        void SetData(int level, const Rectangle* rect, std::nullptr_t,
                     int startIndex, int elementCount)
        {
            SetData(level, rect, static_cast<const Color*>(nullptr), startIndex, elementCount);
        }

        /**
         * @brief Reads raw Color-compatible bytes from the texture into the provided array.
         * @param data         Output array to receive the pixel data.
         * @param startIndex   First element in @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads all raw Color-compatible bytes from the texture into the provided array.
         * @param data         Output array to receive the pixel data.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int elementCount) const;

        /**
         * @brief Reads raw Color-compatible bytes from a mip level or sub-rectangle.
         * @param level        Mip level to read (0 = full size).
         * @param rect         Sub-rectangle to read, or nullptr for the entire level.
         * @param data         Output array to receive the pixel data.
         * @param startIndex   First element in @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(int level, const Rectangle* rect, Color* data, int startIndex, int elementCount) const;

        /** @brief Reads exact packed Bgr565 texels from a Bgr565 texture. */
        void GetData(PackedVector::Bgr565* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Bgr565 texels from a Bgr565 texture. */
        void GetData(PackedVector::Bgr565* data, int elementCount) const;
        /** @brief Reads exact packed Bgr565 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Bgr565* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed Bgra4444 texels from a Bgra4444 texture. */
        void GetData(PackedVector::Bgra4444* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Bgra4444 texels from a Bgra4444 texture. */
        void GetData(PackedVector::Bgra4444* data, int elementCount) const;
        /** @brief Reads exact packed Bgra4444 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Bgra4444* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed Bgra5551 texels from a Bgra5551 texture. */
        void GetData(PackedVector::Bgra5551* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Bgra5551 texels from a Bgra5551 texture. */
        void GetData(PackedVector::Bgra5551* data, int elementCount) const;
        /** @brief Reads exact packed Bgra5551 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Bgra5551* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed Rgba1010102 texels from an Rgba1010102 texture. */
        void GetData(PackedVector::Rgba1010102* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Rgba1010102 texels from an Rgba1010102 texture. */
        void GetData(PackedVector::Rgba1010102* data, int elementCount) const;
        /** @brief Reads exact packed Rgba1010102 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Rgba1010102* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed Alpha8 texels from an Alpha8 texture. */
        void GetData(PackedVector::Alpha8* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Alpha8 texels from an Alpha8 texture. */
        void GetData(PackedVector::Alpha8* data, int elementCount) const;
        /** @brief Reads exact packed Alpha8 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Alpha8* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed Rg32 texels from an Rg32 texture. */
        void GetData(PackedVector::Rg32* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Rg32 texels from an Rg32 texture. */
        void GetData(PackedVector::Rg32* data, int elementCount) const;
        /** @brief Reads exact packed Rg32 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Rg32* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed Rgba64 texels from an Rgba64 texture. */
        void GetData(PackedVector::Rgba64* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed Rgba64 texels from an Rgba64 texture. */
        void GetData(PackedVector::Rgba64* data, int elementCount) const;
        /** @brief Reads exact packed Rgba64 texels from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::Rgba64* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed signed-normalized XY bytes. */
        void GetData(PackedVector::NormalizedByte2* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed signed-normalized XY bytes. */
        void GetData(PackedVector::NormalizedByte2* data, int elementCount) const;
        /** @brief Reads exact packed signed-normalized XY bytes from a mip or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::NormalizedByte2* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed signed-normalized XYZW bytes. */
        void GetData(PackedVector::NormalizedByte4* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed signed-normalized XYZW bytes. */
        void GetData(PackedVector::NormalizedByte4* data, int elementCount) const;
        /** @brief Reads exact packed signed-normalized XYZW bytes from a mip or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::NormalizedByte4* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact IEEE binary32 values from a Single texture. */
        void GetData(float* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact IEEE binary32 values from a Single texture. */
        void GetData(float* data, int elementCount) const;
        /** @brief Reads exact IEEE binary32 values from a Single mip or rectangle. */
        void GetData(int level, const Rectangle* rect, float* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact two-component binary32 values from a Vector2 texture. */
        void GetData(Vector2* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact two-component binary32 values from a Vector2 texture. */
        void GetData(Vector2* data, int elementCount) const;
        /** @brief Reads exact two-component binary32 values from a Vector2 mip or rectangle. */
        void GetData(int level, const Rectangle* rect, Vector2* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact four-component binary32 values from a Vector4 texture. */
        void GetData(Vector4* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact four-component binary32 values from a Vector4 texture. */
        void GetData(Vector4* data, int elementCount) const;
        /** @brief Reads exact four-component binary32 values from a Vector4 mip or rectangle. */
        void GetData(int level, const Rectangle* rect, Vector4* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed binary16 values from a HalfSingle texture. */
        void GetData(PackedVector::HalfSingle* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed binary16 values from a HalfSingle texture. */
        void GetData(PackedVector::HalfSingle* data, int elementCount) const;
        /** @brief Reads exact packed binary16 values from a HalfSingle mip or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::HalfSingle* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed RG binary16 values from a HalfVector2 texture. */
        void GetData(PackedVector::HalfVector2* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed RG binary16 values from a HalfVector2 texture. */
        void GetData(PackedVector::HalfVector2* data, int elementCount) const;
        /** @brief Reads exact packed RG binary16 values from a HalfVector2 mip or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::HalfVector2* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact packed RGBA binary16 values from HalfVector4 or HdrBlendable. */
        void GetData(PackedVector::HalfVector4* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact packed RGBA binary16 values from HalfVector4 or HdrBlendable. */
        void GetData(PackedVector::HalfVector4* data, int elementCount) const;
        /** @brief Reads exact packed RGBA binary16 values from a mip level or rectangle. */
        void GetData(int level, const Rectangle* rect, PackedVector::HalfVector4* data,
                     int startIndex, int elementCount) const;
        /** @brief Reads exact unsigned bytes from a ByteEXT texture. */
        CNAEXT void GetData(std::uint8_t* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact unsigned bytes from a ByteEXT texture, or compressed blocks from a Dxt1/Dxt3/Dxt5 texture. */
        CNAEXT void GetData(std::uint8_t* data, int elementCount) const;
        /**
         * @brief Reads exact unsigned bytes from a ByteEXT mip level or rectangle, or exact
         * compressed blocks from a Dxt1/Dxt3/Dxt5 mip level or rectangle. For a compressed
         * format, rect coordinates are texel space and must be block-aligned or reach the
         * level's edge; elementCount is the exact padded block byte count for the region.
         */
        CNAEXT void GetData(int level, const Rectangle* rect, std::uint8_t* data,
                           int startIndex, int elementCount) const;
        /** @brief Reads exact unsigned 16-bit values from a UShortEXT texture. */
        CNAEXT void GetData(std::uint16_t* data, int startIndex, int elementCount) const;
        /** @brief Reads all exact unsigned 16-bit values from a UShortEXT texture. */
        CNAEXT void GetData(std::uint16_t* data, int elementCount) const;
        /** @brief Reads exact unsigned 16-bit values from a UShortEXT mip level or rectangle. */
        CNAEXT void GetData(int level, const Rectangle* rect, std::uint16_t* data,
                           int startIndex, int elementCount) const;

        /** @brief Preserves the legacy null-pointer overload resolution after packed overloads. */
        void GetData(std::nullptr_t, int elementCount) const
        {
            GetData(static_cast<Color*>(nullptr), elementCount);
        }
        /** @brief Preserves the legacy null-pointer overload resolution after packed overloads. */
        void GetData(std::nullptr_t, int startIndex, int elementCount) const
        {
            GetData(static_cast<Color*>(nullptr), startIndex, elementCount);
        }
        /** @brief Preserves the legacy null-pointer overload resolution after packed overloads. */
        void GetData(int level, const Rectangle* rect, std::nullptr_t,
                     int startIndex, int elementCount) const
        {
            GetData(level, rect, static_cast<Color*>(nullptr), startIndex, elementCount);
        }

        /**
         * @brief Creates a Texture2D by decoding image data from a stream.
         * @param graphicsDevice The device to create the texture on.
         * @param stream         The input stream containing encoded image data.
         * @return The decoded Texture2D.
         */
        static Texture2D FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream);

        /**
         * @brief Creates a Texture2D by decoding image data from a stream, resized or cropped
         *        to a requested size.
         *
         * When @p zoom is false, the decoded image is scaled down to fit within a
         * @p width x @p height box while preserving its aspect ratio (the resulting texture may
         * be smaller than @p width or @p height in one dimension). When @p zoom is true, the
         * image is scaled up to cover the box and centre-cropped so the resulting texture is
         * exactly @p width x @p height.
         *
         * @param graphicsDevice The device to create the texture on.
         * @param stream         The input stream containing encoded image data.
         * @param width          Requested width in pixels.
         * @param height         Requested height in pixels.
         * @param zoom           False to fit within the box preserving aspect ratio; true to
         *                       scale-and-crop to exactly fill the box.
         * @return The decoded, resized Texture2D.
         */
        static Texture2D FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream,
                                    int width, int height, bool zoom);

        /**
         * @brief Saves the texture as a PNG image to the given stream.
         * @param stream  Output stream to write the PNG data to.
         * @param width   Width to encode (should match the texture width).
         * @param height  Height to encode (should match the texture height).
         */
        void SaveAsPng(System::IO::Stream* stream, int width, int height) const;

        /**
         * @brief Saves the texture as a PNG image directly to a file.
         * @param filename Destination file path.
         */
        CNAEXT void SaveAsPng(const std::string& filename) const;

        /**
         * @brief Saves the texture as a JPEG image to the given stream.
         * @param stream  Output stream to write the JPEG data to.
         * @param width   Width to encode.
         * @param height  Height to encode.
         */
        void SaveAsJpeg(System::IO::Stream* stream, int width, int height) const;

        /**
         * @brief Saves the texture as a JPEG image directly to a file.
         * @param filename Destination file path.
         */
        CNAEXT void SaveAsJpeg(const std::string& filename) const;

        /**
         * @brief Uploads raw RGBA pixel data to the texture.
         * @param data       Pointer to the RGBA byte buffer (4 bytes per pixel).
         * @param pixelCount Total number of pixels (width * height).
         */
        CNAEXT void SetDataRGBA(const uint8_t* data, int pixelCount);

        /** @brief Returns a reference to the GPU texture renderer. */
        CNAEXT ITextureRenderer& GetRenderer() const { return *renderer_; }

        /**
         * @brief Returns a weak pointer to the GPU texture renderer.
         *
         * Used by ContentManager's weak texture cache.
         * @return A weak_ptr to the renderer; may be expired if the texture is destroyed.
         */
        CNAEXT std::weak_ptr<ITextureRenderer> GetRendererWeak() const { return renderer_; }

        /**
         * @brief Returns a weak pointer to the CPU-side pixel buffer.
         *
         * Used by ContentManager's weak texture cache.
         * @return A weak_ptr to the pixel buffer; may be expired if context recovery is disabled.
         */
        CNAEXT std::weak_ptr<std::vector<uint8_t>> GetCpuPixelsWeak() const { return cpuPixels_; }

        /**
         * @brief Creates a Texture2D from a raw RGBA pixel vector.
         *
         * Prefer the Texture2D(device, w, h) + SetData pattern for XNA-compatible code.
         *
         * @param device The device to create the texture on.
         * @param w      Width in pixels.
         * @param h      Height in pixels.
         * @param rgba   RGBA pixel data (4 bytes per pixel, size must equal w * h * 4).
         * @return The created Texture2D.
         */
        CNAEXT static Texture2D CreateFromPixels(GraphicsDevice& device,
                                                int w, int h,
                                                const std::vector<std::uint8_t>& rgba);

        /**
         * @brief CNAEXT test-only: builds a CPU-only Texture2D (no GraphicsDevice, no GPU renderer)
         *        from raw pixels, so headless tests can exercise CPU pixel paths such as
         *        MouseCursor::FromTexture2D without a real graphics device.
         *
         * @param w      Width in pixels.
         * @param h      Height in pixels.
         * @param format Surface format the texture reports.
         * @param pixels w * h Color values in row-major order.
         * @return A Texture2D holding the pixels CPU-side with a null renderer.
         */
        CNAEXT static Texture2D CreateCpuOnlyForTests(int w, int h, SurfaceFormat format,
                                                     const std::vector<Color>& pixels);

        /**
         * @brief CNAEXT test-only: builds a Texture2D wrapping an arbitrary renderer (e.g. a
         *        mock/recording ITextureRenderer) without a GraphicsDevice, so callers that
         *        dereference GetRenderer() (such as SpriteBatch::Draw) can be exercised
         *        headlessly against distinct, identifiable texture instances.
         *
         * @param w       Width in pixels.
         * @param h       Height in pixels.
         * @param renderer Renderer to attach; must be non-null.
         * @return A Texture2D wrapping the given renderer, with no GraphicsDevice.
         */
        CNAEXT static Texture2D CreateWithRendererForTests(int w, int h,
                                                         std::shared_ptr<ITextureRenderer> renderer);

        /**
         * @brief Reconstructs a Texture2D from a cached renderer and CPU pixel buffer without reloading from disk.
         *
         * @param device    The device to associate with the texture.
         * @param w         Width in pixels.
         * @param h         Height in pixels.
         * @param fmt       Surface format.
         * @param levelCount Number of mip levels.
         * @param renderer   Shared renderer handle from a previous Texture2D.
         * @param cpuPixels Shared CPU pixel buffer from a previous Texture2D.
         * @return The reconstructed Texture2D.
         */
        CNAEXT static Texture2D ReconstructFromCache(GraphicsDevice& device,
                                                    int w, int h,
                                                    SurfaceFormat fmt,
                                                    int levelCount,
                                                    std::shared_ptr<ITextureRenderer> renderer,
                                                    std::shared_ptr<std::vector<uint8_t>> cpuPixels);

    protected:
        /**
         * @brief Constructs a Texture2D from a pre-built renderer (used by RenderTarget2D).
         * @param device     The owning device.
         * @param w          Width in pixels.
         * @param h          Height in pixels.
         * @param fmt        Surface format.
         * @param levelCount Number of mip levels.
         * @param renderer    Shared ownership of the pre-built GPU renderer.
         */
        Texture2D(GraphicsDevice& device, int w, int h, SurfaceFormat fmt, int levelCount,
                  std::shared_ptr<ITextureRenderer> renderer);

        /**
         * @brief Returns the raw non-owning renderer pointer.
         *
         * Used by RenderTarget2D after construction.
         * @return Pointer to the renderer, or nullptr.
         */
        [[nodiscard]] ITextureRenderer* GetRendererRaw() const { return renderer_.get(); }

        /** @brief Releases the GPU texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    public:
        /**
         * @brief Returns true while the GPU texture handle is allocated.
         *
         * Becomes false immediately after `Dispose()` is called.
         */
        CNAEXT [[nodiscard]] bool HasRenderer() const { return renderer_ != nullptr; }

    private:
        std::shared_ptr<ITextureRenderer> renderer_;
        int width  = 0;
        int height = 0;
        std::shared_ptr<std::vector<uint8_t>> cpuPixels_;
        std::shared_ptr<std::vector<std::vector<uint8_t>>> extraMipLevels_;

        /// Declares that this resource's live renderer is the sole authority for its pixels and
        /// that no CPU shadow may be trusted. True only for a real RenderTarget2D: its content
        /// genuinely comes from GPU rendering, never SetData(), so an empty CPU shadow means "ask
        /// the renderer", not "the shadow was freed" (see GetData()'s fallback). This applies to
        /// every mip -- target descendants can be regenerated after a render pass or parent
        /// upload, so common-layer transfer staging is never authoritative -- and it is why
        /// SetData() updates a target's existing renderer in place instead of replacing it.
        ///
        /// It is NOT a synonym for "was built by the protected constructor below".
        /// ReconstructFromCache() borrows that constructor to wrap an already-created renderer and
        /// clears this flag again: a cache hit is an ordinary content texture whose CPU shadow is
        /// authoritative and whose shared renderer must never be written through (REMED-GFX-223).
        bool gpuOnlyContent_ = false;

        /// Frees cpuPixels_ when context recovery is disabled, saving ~1x texture RAM.
        void MaybeFreeCpuPixels();

        /// Builds a single-level Texture2D from transformed/decoded RGBA8 pixel data.
        static Texture2D MakeTextureFromPixels(GraphicsDevice& device, int w, int h,
                                               std::vector<std::uint8_t>&& rgba);

        /// Builds a Texture2D from an exact, fully decoded RGBA8 mip chain.
        static Texture2D MakeTextureFromMipPixels(
            GraphicsDevice& device, int w, int h,
            std::vector<std::vector<std::uint8_t>>&& rgbaLevels);

        void storeCpuPixels(const uint8_t* rgba, int pixelCount);
        std::vector<uint8_t>& getMipBuffer(int level);
        const std::vector<uint8_t>* getMipBufferConst(int level) const;
        [[nodiscard]] int getBytesPerTexel() const;
        void SetDataBytes(int level, const Rectangle* rect, const std::uint8_t* data,
                          int startIndex, int elementCount, int elementBytes);
        void GetDataBytes(int level, const Rectangle* rect, std::uint8_t* data,
                          int startIndex, int elementCount, int elementBytes) const;

        /// Raw block-compressed byte upload for Dxt1/Dxt3/Dxt5 (SKIA-140). Unlike SetDataBytes,
        /// there is no CPU-side mirror: each call reads back, patches, and re-uploads through
        /// the renderer directly, since compressed storage cannot be sliced by texel row.
        void SetCompressedDataBytes(int level, const Rectangle* rect, const std::uint8_t* data,
                                    int startIndex, int elementCount);
        /// Raw block-compressed byte readback counterpart of SetCompressedDataBytes.
        void GetCompressedDataBytes(int level, const Rectangle* rect, std::uint8_t* data,
                                    int startIndex, int elementCount) const;

        friend class SpriteBatch;
        friend class GraphicsDevice;
    };
}
