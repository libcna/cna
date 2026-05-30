#pragma once

#include <memory>
#include <string>

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
     * @brief Represents a 2D texture loaded from an image file.
     *
     * The actual rendering backend is hidden behind an internal interface.
     */
    class Texture2D
    {
    private:
        std::shared_ptr<ITextureBackend> backend_;

        int width = 0;
        int height = 0;

    public:
        /**
         * @brief Creates an empty texture.
         */
        Texture2D();

        /**
         * @brief Loads a texture from a file path using the active graphics device.
         *
         * @param assetName Full path to the image file.
         */
        explicit Texture2D(const std::string& assetName);
        Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice);

        /**
         * @brief Destroys the texture wrapper.
         */
        ~Texture2D();

        Texture2D(const Texture2D&) = default;
        Texture2D& operator=(const Texture2D&) = default;
        Texture2D(Texture2D&&) noexcept = default;
        Texture2D& operator=(Texture2D&&) noexcept = default;

        /**
         * @brief Returns the bounds of the texture.
         *
         * @return Texture bounds in pixels.
         */
        [[nodiscard]] Rectangle getBoundsProperty() const;

        ITextureBackend& GetBackend() const { return *backend_; }

    private:
        /**
         * @brief Returns the internal SDL texture for engine-internal use.
         */
        [[nodiscard]] SDL_Texture* GetNativeTextureInternal() const;

        friend class SpriteBatch;
        friend class GraphicsDevice;
    };
}
