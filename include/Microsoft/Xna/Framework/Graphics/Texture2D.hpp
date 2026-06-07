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

    /// Represents a 2D texture. Mirrors XNA 4.0 Texture2D.
    class Texture2D : public Texture
    {
    public:
        Texture2D();

        /// XNA 4.0: Texture2D(GraphicsDevice, string assetName) — loads from file.
        explicit Texture2D(const std::string& assetName);
        Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice);

        /// XNA 4.0: Texture2D(GraphicsDevice, int width, int height) — creates empty texture.
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height);

        /// XNA 4.0: Texture2D(GraphicsDevice, int width, int height, bool mipMap, SurfaceFormat format)
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height,
                  bool mipMap, SurfaceFormat format);

        ~Texture2D() override;

        Texture2D(const Texture2D&) = default;
        Texture2D& operator=(const Texture2D&) = default;
        Texture2D(Texture2D&&) noexcept = default;
        Texture2D& operator=(Texture2D&&) noexcept = default;

        [[nodiscard]] const std::string& GetTypeName() const override;

        /// XNA 4.0: Texture2D.Width
        [[nodiscard]] int getWidthProperty()  const { return width; }
        /// XNA 4.0: Texture2D.Height
        [[nodiscard]] int getHeightProperty() const { return height; }
        /// XNA 4.0: Texture2D.Bounds
        [[nodiscard]] Rectangle getBoundsProperty() const;

        // Format and LevelCount are inherited from Texture.

        /// XNA 4.0: Texture2D.SetData<Color>(Color[] data)
        void SetData(const Color* data, int elementCount);

        /// XNA 4.0: Texture2D.SetData<Color>(int level, Rectangle? rect, Color[], int startIndex, int elementCount)
        void SetData(int level, const Rectangle* rect, const Color* data, int startIndex, int elementCount);

        /// XNA 4.0: Texture2D.GetData<Color>(Color[] data, int startIndex, int elementCount)
        void GetData(Color* data, int startIndex, int elementCount) const;

        /// XNA 4.0: Texture2D.GetData<Color>(Color[] data)
        void GetData(Color* data, int elementCount) const;

        /// XNA 4.0: Texture2D.GetData<Color>(int level, Rectangle? rect, Color[], int startIndex, int elementCount)
        void GetData(int level, const Rectangle* rect, Color* data, int startIndex, int elementCount) const;

        /// XNA 4.0: Texture2D.FromStream(GraphicsDevice, Stream)
        static Texture2D FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream);

        /// XNA 4.0: Texture2D.SaveAsPng(Stream, int width, int height)
        void SaveAsPng(System::IO::Stream* stream, int width, int height) const;

        /// NOXNA convenience: saves as PNG directly to a file path.
        NOXNA void SaveAsPng(const std::string& filename) const;

        /// XNA 4.0: Texture2D.SaveAsJpeg(Stream, int width, int height)
        void SaveAsJpeg(System::IO::Stream* stream, int width, int height) const;

        /// NOXNA convenience: saves as JPEG directly to a file path.
        NOXNA void SaveAsJpeg(const std::string& filename) const;

        /// Updates texture pixels from a raw RGBA byte buffer. pixelCount = width * height.
        NOXNA void SetDataRGBA(const uint8_t* data, int pixelCount);

        NOXNA ITextureBackend& GetBackend() const { return *backend_; }

        /// @note Not in XNA 4.0 — prefer the Texture2D(device,w,h)+SetData pattern.
        NOXNA static Texture2D CreateFromPixels(GraphicsDevice& device,
                                                int w, int h,
                                                const std::vector<std::uint8_t>& rgba);

    protected:
        /// Used by RenderTarget2D: initializes with a pre-built backend (no CPU-side pixels).
        Texture2D(GraphicsDevice& device, int w, int h, SurfaceFormat fmt, int levelCount,
                  std::shared_ptr<ITextureBackend> backend);

        /// Returns the raw (non-owning) backend pointer. Used by RenderTarget2D after construction.
        [[nodiscard]] ITextureBackend* GetBackendRaw() const { return backend_.get(); }

    private:
        std::shared_ptr<ITextureBackend> backend_;
        int width  = 0;
        int height = 0;
        std::vector<uint8_t> cpuPixels_;             // RGBA8 CPU-side copy for mip level 0
        std::vector<std::vector<uint8_t>> extraMipLevels_; // CPU-side copies for mip levels 1+

        void storeCpuPixels(const uint8_t* rgba, int pixelCount);
        std::vector<uint8_t>& getMipBuffer(int level);
        const std::vector<uint8_t>* getMipBufferConst(int level) const;

        [[nodiscard]] SDL_Texture* GetNativeTextureInternal() const;

        friend class SpriteBatch;
        friend class GraphicsDevice;
    };
}
