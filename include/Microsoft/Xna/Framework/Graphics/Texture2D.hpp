// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

struct SDL_Texture;

namespace System::IO { class Stream; }

namespace CNA::Internal::Backends
{
    class ITextureBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

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
        NOXNA explicit Texture2D(const std::string& assetName);
        /**
         * @brief Loads a Texture2D from a file asset using the given device.
         * @param assetName     Path to the image file.
         * @param graphicsDevice The device to upload the texture to.
         */
        NOXNA Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice);

        /**
         * @brief Creates an empty Texture2D with Color format.
         * @param graphicsDevice The device to create the texture on.
         * @param width          Width in pixels.
         * @param height         Height in pixels.
         */
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height);

        /**
         * @brief Creates a Texture2D with explicit format and optional mip levels.
         * @param graphicsDevice The device to create the texture on.
         * @param width          Width in pixels.
         * @param height         Height in pixels.
         * @param mipMap         True to generate a full mipmap chain.
         * @param format         The desired surface format.
         */
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height,
                  bool mipMap, SurfaceFormat format);

        /** @brief Destructor. */
        NOXNA ~Texture2D() override;

        Texture2D(const Texture2D&) = default;
        Texture2D& operator=(const Texture2D&) = default;
        Texture2D(Texture2D&&) noexcept = default;
        Texture2D& operator=(Texture2D&&) noexcept = default;

        /** @brief Returns the fully qualified .NET type name of this class. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns the texture width in pixels. */
        [[nodiscard]] int getWidthProperty()  const { return width; }
        /** @brief Returns the texture height in pixels. */
        [[nodiscard]] int getHeightProperty() const { return height; }
        /** @brief Returns a Rectangle with origin (0,0) and the texture dimensions. */
        [[nodiscard]] Rectangle getBoundsProperty() const;

        // Format and LevelCount are inherited from Texture.

        /**
         * @brief Uploads pixel data to the texture.
         * @param data         Pointer to the Color array.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(const Color* data, int elementCount);

        /**
         * @brief Uploads pixel data to a specific mip level and optional sub-rectangle.
         * @param level        Mip level to write (0 = full size).
         * @param rect         Sub-rectangle to update, or nullptr for the entire level.
         * @param data         Pointer to the Color array.
         * @param startIndex   First element in @p data to use.
         * @param elementCount Number of Color elements to upload.
         */
        void SetData(int level, const Rectangle* rect, const Color* data, int startIndex, int elementCount);

        /**
         * @brief Reads pixel data from the texture into the provided array.
         * @param data         Output array to receive the pixel data.
         * @param startIndex   First element in @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int startIndex, int elementCount) const;

        /**
         * @brief Reads all pixel data from the texture into the provided array.
         * @param data         Output array to receive the pixel data.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(Color* data, int elementCount) const;

        /**
         * @brief Reads pixel data from a specific mip level and optional sub-rectangle.
         * @param level        Mip level to read (0 = full size).
         * @param rect         Sub-rectangle to read, or nullptr for the entire level.
         * @param data         Output array to receive the pixel data.
         * @param startIndex   First element in @p data to write to.
         * @param elementCount Number of Color elements to read.
         */
        void GetData(int level, const Rectangle* rect, Color* data, int startIndex, int elementCount) const;

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
        NOXNA void SaveAsPng(const std::string& filename) const;

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
        NOXNA void SaveAsJpeg(const std::string& filename) const;

        /**
         * @brief Uploads raw RGBA pixel data to the texture.
         * @param data       Pointer to the RGBA byte buffer (4 bytes per pixel).
         * @param pixelCount Total number of pixels (width * height).
         */
        NOXNA void SetDataRGBA(const uint8_t* data, int pixelCount);

        /** @brief Returns a reference to the GPU texture backend. */
        NOXNA ITextureBackend& GetBackend() const { return *backend_; }

        /**
         * @brief Returns a weak pointer to the GPU texture backend.
         *
         * Used by ContentManager's weak texture cache.
         * @return A weak_ptr to the backend; may be expired if the texture is destroyed.
         */
        NOXNA std::weak_ptr<ITextureBackend> GetBackendWeak() const { return backend_; }

        /**
         * @brief Returns a weak pointer to the CPU-side pixel buffer.
         *
         * Used by ContentManager's weak texture cache.
         * @return A weak_ptr to the pixel buffer; may be expired if context recovery is disabled.
         */
        NOXNA std::weak_ptr<std::vector<uint8_t>> GetCpuPixelsWeak() const { return cpuPixels_; }

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
        NOXNA static Texture2D CreateFromPixels(GraphicsDevice& device,
                                                int w, int h,
                                                const std::vector<std::uint8_t>& rgba);

        /**
         * @brief Reconstructs a Texture2D from a cached backend and CPU pixel buffer without reloading from disk.
         *
         * @param device    The device to associate with the texture.
         * @param w         Width in pixels.
         * @param h         Height in pixels.
         * @param fmt       Surface format.
         * @param levelCount Number of mip levels.
         * @param backend   Shared backend handle from a previous Texture2D.
         * @param cpuPixels Shared CPU pixel buffer from a previous Texture2D.
         * @return The reconstructed Texture2D.
         */
        NOXNA static Texture2D ReconstructFromCache(GraphicsDevice& device,
                                                    int w, int h,
                                                    SurfaceFormat fmt,
                                                    int levelCount,
                                                    std::shared_ptr<ITextureBackend> backend,
                                                    std::shared_ptr<std::vector<uint8_t>> cpuPixels);

    protected:
        /**
         * @brief Constructs a Texture2D from a pre-built backend (used by RenderTarget2D).
         * @param device     The owning device.
         * @param w          Width in pixels.
         * @param h          Height in pixels.
         * @param fmt        Surface format.
         * @param levelCount Number of mip levels.
         * @param backend    Shared ownership of the pre-built GPU backend.
         */
        Texture2D(GraphicsDevice& device, int w, int h, SurfaceFormat fmt, int levelCount,
                  std::shared_ptr<ITextureBackend> backend);

        /**
         * @brief Returns the raw non-owning backend pointer.
         *
         * Used by RenderTarget2D after construction.
         * @return Pointer to the backend, or nullptr.
         */
        [[nodiscard]] ITextureBackend* GetBackendRaw() const { return backend_.get(); }

        /** @brief Releases the GPU texture handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    public:
        /**
         * @brief Returns true while the GPU texture handle is allocated.
         *
         * Becomes false immediately after `Dispose()` is called.
         */
        NOXNA [[nodiscard]] bool HasBackend() const { return backend_ != nullptr; }

    private:
        std::shared_ptr<ITextureBackend> backend_;
        int width  = 0;
        int height = 0;
        std::shared_ptr<std::vector<uint8_t>> cpuPixels_;
        std::shared_ptr<std::vector<std::vector<uint8_t>>> extraMipLevels_;

        /// Frees cpuPixels_ when context recovery is disabled, saving ~1x texture RAM.
        void MaybeFreeCpuPixels();

        /// Builds a Texture2D from already-decoded RGBA8 pixel data; shared by both FromStream overloads.
        static Texture2D MakeTextureFromPixels(GraphicsDevice& device, int w, int h,
                                               std::vector<std::uint8_t>&& rgba);

        void storeCpuPixels(const uint8_t* rgba, int pixelCount);
        std::vector<uint8_t>& getMipBuffer(int level);
        const std::vector<uint8_t>* getMipBufferConst(int level) const;

        [[nodiscard]] SDL_Texture* GetNativeTextureInternal() const;

        friend class SpriteBatch;
        friend class GraphicsDevice;
    };
}
