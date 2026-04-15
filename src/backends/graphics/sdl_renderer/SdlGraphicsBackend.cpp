#include "SdlGraphicsBackend.hpp"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include "CNA/Logger.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    // --- SdlTextureBackend ---

    SdlTextureBackend::SdlTextureBackend(SDL_Renderer* renderer, const std::string& assetName) {
        if (assetName.empty()) return;

        texture = IMG_LoadTexture(renderer, assetName.c_str());
        if (!texture) {
            std::string path = std::filesystem::current_path();
            CNA::Logger::Debug("Current path is " + path);
            throw std::runtime_error("Failed to load texture: " + assetName + " Error: " + SDL_GetError());
        }

        float w, h;
        if (SDL_GetTextureSize(texture, &w, &h)) {
            width = static_cast<int>(w);
            height = static_cast<int>(h);
        }
    }

    SdlTextureBackend::~SdlTextureBackend() {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }

    // --- SdlSpriteBatchBackend ---

    SdlSpriteBatchBackend::SdlSpriteBatchBackend(SDL_Renderer* r) : renderer(r) {}

    void SdlSpriteBatchBackend::Begin() {
        if (!renderer) throw std::runtime_error("SdlSpriteBatchBackend::Begin failed: renderer is null.");
        begun = true;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    }

    void SdlSpriteBatchBackend::End() {
        begun = false;
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y) {
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;

        SDL_FRect dst { x, y, static_cast<float>(sdlTex.width), static_cast<float>(sdlTex.height) };
        SDL_RenderTexture(renderer, sdlTex.texture, nullptr, &dst);
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                    const Rectangle& sourceRectangle,
                                    const Rectangle& destinationRectangle,
                                    const Color& color) {
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;

        SDL_SetTextureColorMod(sdlTex.texture, color.getRProperty(), color.getGProperty(), color.getBProperty());
        SDL_SetTextureAlphaMod(sdlTex.texture, color.getAProperty());
        SDL_SetTextureBlendMode(sdlTex.texture, SDL_BLENDMODE_BLEND);

        SDL_FRect src { (float)sourceRectangle.X, (float)sourceRectangle.Y, (float)sourceRectangle.Width, (float)sourceRectangle.Height };
        SDL_FRect dst { (float)destinationRectangle.X, (float)destinationRectangle.Y, (float)destinationRectangle.Width, (float)destinationRectangle.Height };
        SDL_RenderTexture(renderer, sdlTex.texture, &src, &dst);
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                    const Rectangle& sourceRectangle,
                                    const Rectangle& destinationRectangle,
                                    const Color& color,
                                    float rotation,
                                    const Vector2& origin,
                                    SpriteEffects effects,
                                    float layerDepth) {
        (void)layerDepth;
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;

        SDL_SetTextureColorMod(sdlTex.texture, color.getRProperty(), color.getGProperty(), color.getBProperty());
        SDL_SetTextureAlphaMod(sdlTex.texture, color.getAProperty());
        SDL_SetTextureBlendMode(sdlTex.texture, SDL_BLENDMODE_BLEND);

        SDL_FRect src { (float)sourceRectangle.X, (float)sourceRectangle.Y, (float)sourceRectangle.Width, (float)sourceRectangle.Height };
        SDL_FRect dst { (float)destinationRectangle.X, (float)destinationRectangle.Y, (float)destinationRectangle.Width, (float)destinationRectangle.Height };

        SDL_FPoint sdlCenter { (origin.X / src.w) * dst.w, (origin.Y / src.h) * dst.h };
        double rotationDeg = (double)rotation * 180.0 / 3.14159265358979323846;

        SDL_FlipMode flip = SDL_FLIP_NONE;
        if (((int)effects & (int)SpriteEffects::FlipHorizontally) && ((int)effects & (int)SpriteEffects::FlipVertically))
            flip = SDL_FLIP_HORIZONTAL_AND_VERTICAL;
        else if ((int)effects & (int)SpriteEffects::FlipHorizontally) flip = SDL_FLIP_HORIZONTAL;
        else if ((int)effects & (int)SpriteEffects::FlipVertically) flip = SDL_FLIP_VERTICAL;

        SDL_RenderTextureRotated(renderer, sdlTex.texture, &src, &dst, rotationDeg, &sdlCenter, flip);
    }

    // --- SdlGraphicsBackend ---

    SdlGraphicsBackend::SdlGraphicsBackend() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            throw std::runtime_error(std::string("SDL video subsystem initialization failed: ") + SDL_GetError());

        window = SDL_CreateWindow("CNA Game", 800, 600, SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            SDL_DestroyWindow(window);
            throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
        SDL_SetRenderVSync(renderer, 1);
    }

    SdlGraphicsBackend::~SdlGraphicsBackend() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void SdlGraphicsBackend::Clear(float r, float g, float b, float a) {
        SDL_SetRenderDrawColor(renderer, (Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), (Uint8)(a * 255.0f));
        SDL_RenderClear(renderer);
    }

    void SdlGraphicsBackend::Present() {
        SDL_RenderPresent(renderer);
    }

    void SdlGraphicsBackend::GetViewportSize(int& width, int& height) {
        SDL_GetWindowSize(window, &width, &height);
    }

    std::unique_ptr<ITextureBackend> SdlGraphicsBackend::CreateTexture(const std::string& assetName) {
        return std::make_unique<SdlTextureBackend>(renderer, assetName);
    }

    std::unique_ptr<ISpriteBatchBackend> SdlGraphicsBackend::CreateSpriteBatch() {
        return std::make_unique<SdlSpriteBatchBackend>(renderer);
    }

    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend() {
        return std::make_unique<SdlGraphicsBackend>();
    }

}
