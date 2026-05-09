#include "CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <SDL3/SDL_gpu.h>

namespace CNA::Internal::Backends::SdlRenderer {

    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    // --- SdlTextureBackend ---

    SdlTextureBackend::SdlTextureBackend(SDL_Renderer* renderer, const ImageData& data) {
        width = data.width;
        height = data.height;

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!texture) {
            throw std::runtime_error(std::string("Failed to create SDL texture: ") + SDL_GetError());
        }

        if (!SDL_UpdateTexture(texture, nullptr, data.pixels.data(), width * 4)) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
            throw std::runtime_error(std::string("Failed to update SDL texture: ") + SDL_GetError());
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
        if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)) {
            throw std::runtime_error(std::string("SDL_SetRenderDrawBlendMode failed: ") + SDL_GetError());
        }
    }

    void SdlSpriteBatchBackend::End() {
        begun = false;
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y) {
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;

        SDL_FRect dst { x, y, static_cast<float>(sdlTex.width), static_cast<float>(sdlTex.height) };
        if (!SDL_RenderTexture(renderer, sdlTex.texture, nullptr, &dst)) {
            throw std::runtime_error(std::string("SDL_RenderTexture failed: ") + SDL_GetError());
        }
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                    const Rectangle& destinationRectangle,
                                    const Rectangle& sourceRectangle,
                                    const Color& color) {
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;

        if (!SDL_SetTextureColorMod(sdlTex.texture, color.getRProperty(), color.getGProperty(), color.getBProperty())) {
            throw std::runtime_error(std::string("SDL_SetTextureColorMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureAlphaMod(sdlTex.texture, color.getAProperty())) {
            throw std::runtime_error(std::string("SDL_SetTextureAlphaMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureBlendMode(sdlTex.texture, SDL_BLENDMODE_BLEND)) {
            throw std::runtime_error(std::string("SDL_SetTextureBlendMode failed: ") + SDL_GetError());
        }

        SDL_FRect src { (float)sourceRectangle.X, (float)sourceRectangle.Y, (float)sourceRectangle.Width, (float)sourceRectangle.Height };
        SDL_FRect dst { (float)destinationRectangle.X, (float)destinationRectangle.Y, (float)destinationRectangle.Width, (float)destinationRectangle.Height };
        if (!SDL_RenderTexture(renderer, sdlTex.texture, &src, &dst)) {
            throw std::runtime_error(std::string("SDL_RenderTexture failed: ") + SDL_GetError());
        }
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                    const Rectangle& destinationRectangle,
                                    const Rectangle& sourceRectangle,
                                    const Color& color,
                                    float rotation,
                                    const Vector2& origin,
                                    SpriteEffects effects,
                                    float layerDepth) {
        (void)layerDepth;
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;

        if (!SDL_SetTextureColorMod(sdlTex.texture, color.getRProperty(), color.getGProperty(), color.getBProperty())) {
            throw std::runtime_error(std::string("SDL_SetTextureColorMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureAlphaMod(sdlTex.texture, color.getAProperty())) {
            throw std::runtime_error(std::string("SDL_SetTextureAlphaMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureBlendMode(sdlTex.texture, SDL_BLENDMODE_BLEND)) {
            throw std::runtime_error(std::string("SDL_SetTextureBlendMode failed: ") + SDL_GetError());
        }

        SDL_FRect src { (float)sourceRectangle.X, (float)sourceRectangle.Y, (float)sourceRectangle.Width, (float)sourceRectangle.Height };
        SDL_FRect dst { (float)destinationRectangle.X, (float)destinationRectangle.Y, (float)destinationRectangle.Width, (float)destinationRectangle.Height };

        SDL_FPoint sdlCenter { (origin.X / src.w) * dst.w, (origin.Y / src.h) * dst.h };
        double rotationDeg = (double)rotation * 180.0 / 3.14159265358979323846;

        SDL_FlipMode flip = SDL_FLIP_NONE;
        if (((int)effects & (int)SpriteEffects::FlipHorizontally) && ((int)effects & (int)SpriteEffects::FlipVertically))
            flip = SDL_FLIP_HORIZONTAL_AND_VERTICAL;
        else if ((int)effects & (int)SpriteEffects::FlipHorizontally) flip = SDL_FLIP_HORIZONTAL;
        else if ((int)effects & (int)SpriteEffects::FlipVertically) flip = SDL_FLIP_VERTICAL;

        if (!SDL_RenderTextureRotated(renderer, sdlTex.texture, &src, &dst, rotationDeg, &sdlCenter, flip)) {
            throw std::runtime_error(std::string("SDL_RenderTextureRotated failed: ") + SDL_GetError());
        }
    }

    // --- SdlGraphicsBackend ---

    static SDL_RendererLogicalPresentation toSdlPresentationMode(CnaPresentationMode mode) {
        switch (mode) {
            case CnaPresentationMode::Letterbox:             return SDL_LOGICAL_PRESENTATION_LETTERBOX;
            case CnaPresentationMode::Overscan:              return SDL_LOGICAL_PRESENTATION_OVERSCAN;
            case CnaPresentationMode::Stretch:               return SDL_LOGICAL_PRESENTATION_STRETCH;
            case CnaPresentationMode::NativeBackBuffer:      return SDL_LOGICAL_PRESENTATION_DISABLED;
            case CnaPresentationMode::FixedHeightDynamicWidth: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
            default:                                         return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        }
    }
    static const char* presentationModeName(CnaPresentationMode mode) {
        switch (mode) {
            case CnaPresentationMode::Letterbox:             return "LETTERBOX";
            case CnaPresentationMode::Overscan:              return "OVERSCAN";
            case CnaPresentationMode::Stretch:               return "STRETCH";
            case CnaPresentationMode::NativeBackBuffer:      return "NATIVE_BACKBUFFER";
            case CnaPresentationMode::FixedHeightDynamicWidth: return "FIXED_HEIGHT_DYNAMIC_WIDTH";
            default:                                         return "UNKNOWN";
        }
    }
    /// When mode is FixedHeightDynamicWidth, derive logicalW from the actual
    /// renderer output size so the canvas exactly matches the surface AR.
    static void applyLogicalPresentation(SDL_Renderer* renderer,
                                         int& logicalWidth, int& logicalHeight,
                                         CnaPresentationMode mode) {
        if (mode == CnaPresentationMode::FixedHeightDynamicWidth) {
            int outputW = 0, outputH = 0;
            SDL_GetRenderOutputSize(renderer, &outputW, &outputH);
            if (outputH > 0 && logicalHeight > 0) {
                logicalWidth = (int)((double)outputW * logicalHeight / outputH + 0.5);
            }
            SDL_Log("[Renderer] FixedHeightDynamicWidth: outputSize=%dx%d logicalSize=%dx%d",
                    outputW, outputH, logicalWidth, logicalHeight);
        }
        SDL_RendererLogicalPresentation sdlMode = toSdlPresentationMode(mode);
        if (!SDL_SetRenderLogicalPresentation(renderer, logicalWidth, logicalHeight, sdlMode)) {
            SDL_Log("[Renderer] WARNING: SDL_SetRenderLogicalPresentation failed: %s", SDL_GetError());
        } else {
            SDL_Log("[Renderer] SDL_SetRenderLogicalPresentation set to %dx%d %s",
                    logicalWidth, logicalHeight, presentationModeName(mode));
        }
    }

    SdlGraphicsBackend::SdlGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                           CnaPresentationMode mode)
        : window(window), logicalWidth(virtualWidth), logicalHeight(virtualHeight), presentationMode_(mode) {
        if (!window) throw std::runtime_error("SdlGraphicsBackend initialized with null window.");

        // NOTE: SDL_Window is NOT owned by the backend.
        // It is owned by GraphicsDevice or higher level platform layer.

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
        if (!SDL_SetRenderVSync(renderer, 1)) {
            std::cerr << "Warning: SDL_SetRenderVSync failed: " << SDL_GetError() << std::endl;
        }

        // Log physical output size vs. requested virtual size, then configure
        // SDL logical presentation so the game's virtual coordinate space is
        // scaled / letterboxed to fit the real surface on every platform.
        {
            int outputW = 0, outputH = 0;
            SDL_GetRenderOutputSize(renderer, &outputW, &outputH);
            int winW = 0, winH = 0;
            SDL_GetWindowSize(window, &winW, &winH);
            SDL_Log("[Renderer] virtualSize=%dx%d windowSize=%dx%d rendererOutputSize=%dx%d",
                    virtualWidth, virtualHeight, winW, winH, outputW, outputH);

            if (virtualWidth > 0 && virtualHeight > 0) {
                applyLogicalPresentation(renderer, logicalWidth, logicalHeight, mode);
            } else {
                SDL_Log("[Renderer] No logical presentation set (virtualSize not provided)");
            }
        }

        const char* name = SDL_GetRendererName(renderer);

        if (!name) {
            SDL_Log("SDL_GetRendererName failed: %s", SDL_GetError());
        } else if (SDL_strcmp(name, "opengl") == 0) {
            SDL_Log("SDL_Renderer uses OpenGL");
            std::cout << "SDL_Renderer uses OpenGL" << std::endl;
        } else if (SDL_strcmp(name, "gpu") == 0) {
            SDL_GPUDevice* device = SDL_GetGPURendererDevice(renderer);
            if (device) {
                const char* gpuDriver = SDL_GetGPUDeviceDriver(device);
                SDL_Log("SDL_Renderer = gpu, current backend = %s",
                        gpuDriver ? gpuDriver : "unknown");
                std::cout << "SDL_Renderer = gpu, current backend = " << (gpuDriver ? gpuDriver : "unknown") << std::endl;
            } else {
                SDL_Log("Renderer is gpu, but GPU device could not be find out: %s", SDL_GetError());
                std::cout << "Renderer is gpu, but GPU device could not be find out: " << SDL_GetError() << std::endl;
            }
        } else if (SDL_strcmp(name, "vulkan") == 0) {
            SDL_Log("SDL_Renderer uses Vulkan");
            std::cout << "SDL_Renderer uses Vulkan" << std::endl;
        } else {
            SDL_Log("SDL_Renderer backend: %s", name);
            std::cout << "SDL_Renderer backend: " << name << std::endl;
        }
    }

    SdlGraphicsBackend::~SdlGraphicsBackend() {
        if (renderer) SDL_DestroyRenderer(renderer);
        // window is NOT owned by the backend.
        // No SDL_Quit or subsystem shutdown here - managed centrally.
    }

    void SdlGraphicsBackend::Clear(float r, float g, float b, float a) {
        if (!SDL_SetRenderDrawColor(renderer, (Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), (Uint8)(a * 255.0f))) {
            throw std::runtime_error(std::string("SDL_SetRenderDrawColor failed: ") + SDL_GetError());
        }
        if (!SDL_RenderClear(renderer)) {
            throw std::runtime_error(std::string("SDL_RenderClear failed: ") + SDL_GetError());
        }
    }

    void SdlGraphicsBackend::Present() {
        if (!SDL_RenderPresent(renderer)) {
            throw std::runtime_error(std::string("SDL_RenderPresent failed: ") + SDL_GetError());
        }
    }

    void SdlGraphicsBackend::SetVirtualResolution(int width, int height) {
        logicalWidth  = width;
        logicalHeight = height;
        if (renderer && (logicalWidth > 0 || logicalHeight > 0)) {
            applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
        }
    }

    void SdlGraphicsBackend::SetPresentationMode(int mode) {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        SDL_Log("[Renderer] SetPresentationMode: %s", presentationModeName(presentationMode_));
        if (renderer && (logicalWidth > 0 || logicalHeight > 0)) {
            applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
        }
    }

    void SdlGraphicsBackend::GetViewportSize(int& width, int& height) {
        // Return the logical (virtual) resolution, not the physical surface size.
        // SDL_SetRenderLogicalPresentation handles the physical-to-logical mapping,
        // so the game always works in its own coordinate space.
        width  = logicalWidth;
        height = logicalHeight;
    }

    std::unique_ptr<ITextureBackend> SdlGraphicsBackend::CreateTexture(const ImageData& data) {
        return std::make_unique<SdlTextureBackend>(renderer, data);
    }

    std::unique_ptr<ISpriteBatchBackend> SdlGraphicsBackend::CreateSpriteBatch() {
        return std::make_unique<SdlSpriteBatchBackend>(renderer);
    }

    // ---- 3D: explicit STUB. SDL_Renderer cannot do 3D. ----
    static void ThrowNo3D() {
        throw std::runtime_error(
            "SDL_Renderer backend: 3D rendering is not supported by this backend yet. "
            "Use the EasyGL backend for 3D.");
    }
    void SdlGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D(); }
    void SdlGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D(); }
    std::unique_ptr<IVertexBufferBackend> SdlGraphicsBackend::CreateVertexBuffer(int) { ThrowNo3D(); return nullptr; }
    std::unique_ptr<IIndexBufferBackend>  SdlGraphicsBackend::CreateIndexBuffer16(int) { ThrowNo3D(); return nullptr; }
    void SdlGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                   const Matrix&, const Matrix&, const Matrix&,
                                                   PrimitiveType, int) { ThrowNo3D(); }
    void SdlGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                                          const IIndexBufferBackend&,
                                                          const Matrix&, const Matrix&, const Matrix&,
                                                          PrimitiveType, int) { ThrowNo3D(); }
}

namespace CNA::Internal::Backends {
#ifdef CNA_BACKEND_SDL_RENDERER
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args) {
        return std::make_unique<SdlRenderer::SdlGraphicsBackend>(args.window, args.virtualWidth, args.virtualHeight,
                                                                 args.presentationMode);
    }
#endif
}
