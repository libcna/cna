#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

struct SDL_Texture;

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Internal::Backends
{
    class ITextureBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

    /**
     * @brief Represents a 2D texture. Mirrors XNA 4.0 Texture2D.
     */
    class Texture2D
    {
    private:
        std::shared_ptr<ITextureBackend> backend_;
        GraphicsDevice* device_ = nullptr; // non-owning; set by the (device,w,h) constructor
        int width  = 0;
        int height = 0;

    public:
        Texture2D();

        /// XNA 4.0: Texture2D(GraphicsDevice, string assetName) — loads from file.
        explicit Texture2D(const std::string& assetName);
        Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice);

        /// XNA 4.0: Texture2D(GraphicsDevice, int width, int height) — creates empty texture.
        Texture2D(GraphicsDevice& graphicsDevice, int width, int height);

        ~Texture2D();

        Texture2D(const Texture2D&) = default;
        Texture2D& operator=(const Texture2D&) = default;
        Texture2D(Texture2D&&) noexcept = default;
        Texture2D& operator=(Texture2D&&) noexcept = default;

        /// XNA 4.0: Texture2D.Bounds
        [[nodiscard]] Rectangle getBoundsProperty() const;

        /// XNA 4.0: Texture2D.SetData<Color>(Color[] data)
        void SetData(const Color* data, int elementCount);

        ITextureBackend& GetBackend() const { return *backend_; }

        /// @note Not in XNA 4.0 — prefer the Texture2D(device,w,h)+SetData pattern.
        NOXNA static Texture2D CreateFromPixels(GraphicsDevice& device,
                                                int w, int h,
                                                const std::vector<std::uint8_t>& rgba);

    private:
        [[nodiscard]] SDL_Texture* GetNativeTextureInternal() const;

        friend class SpriteBatch;
        friend class GraphicsDevice;
    };
}
