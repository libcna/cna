#include "CNA/Internal/Renderers/SdlRenderer/SdlRenderer.hpp"
#include "CNA/Platform/Detail/Sdl3RendererInterop.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <SDL3/SDL_gpu.h>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Internal::Renderers::SdlRenderer
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;

    namespace
    {
        [[nodiscard]] SDL_Texture* GetNativeSdlTexture(const ITextureRenderer& texture)
        {
            const auto* sdlTexture = dynamic_cast<const ISdlTextureRenderer*>(&texture);
            return sdlTexture != nullptr ? sdlTexture->GetNativeSdlTexture() : nullptr;
        }
    }

    // --- SdlTextureRenderer ---

    SdlTextureRenderer::SdlTextureRenderer(SDL_Renderer* renderer, const ImageData& data)
    {
        width = data.width;
        height = data.height;

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!texture)
        {
            throw std::runtime_error(std::string("Failed to create SDL texture: ") + SDL_GetError());
        }

        if (!SDL_UpdateTexture(texture, nullptr, data.pixels.data(), width * 4))
        {
            SDL_DestroyTexture(texture);
            texture = nullptr;
            throw std::runtime_error(std::string("Failed to update SDL texture: ") + SDL_GetError());
        }
    }

    SdlTextureRenderer::~SdlTextureRenderer()
    {
        if (texture)
        {
            SDL_DestroyTexture(texture);
        }
    }

    void SdlTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!texture || !rgba) return;
        SDL_UpdateTexture(texture, nullptr, rgba, stride);
    }

    void SdlTextureRenderer::UpdatePixelsLevel(int level, const uint8_t*, int, int)
    {
        throw std::runtime_error(
            "SDL_Renderer does not support mip-level texture uploads (level " + std::to_string(level) +
            "): SDL_Renderer's 2D texture API has no native mip chain or per-level LOD sampling. "
            "Use Texture2D::SetData(level=0, ...) only, or generate mips via a mipMap-aware renderer.");
    }

    // --- SdlSpriteBatchRenderer ---

    SdlSpriteBatchRenderer::SdlSpriteBatchRenderer(SDL_Renderer* r) : renderer(r)
    {
    }

    void SdlSpriteBatchRenderer::Begin()
    {
        // Task 695 finding: this previously hardcoded SDL_SetRenderDrawBlendMode(SDL_BLENDMODE_BLEND)
        // unconditionally, clobbering whatever SdlRenderer::ApplyBlendState had just set via
        // GraphicsDevice::setBlendStateProperty(blendState) -- called by SpriteBatch::Begin()
        // immediately before renderer_->Begin() runs (see SpriteBatch.cpp). This was invisible while
        // ApplyBlendState's own mapping was still incomplete (BlendState::AlphaBlend/NonPremultiplied
        // both happened to resolve to this exact same SDL_BLENDMODE_BLEND value anyway), but became a
        // real, visible bug once ApplyBlendState was fixed to distinguish them correctly.
        if (!renderer) throw std::runtime_error("SdlSpriteBatchRenderer::Begin failed: renderer is null.");
        begun = true;
    }

    void SdlSpriteBatchRenderer::End()
    {
        begun = false;
    }

    void SdlSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "SDL_Renderer does not support custom SpriteBatch Effects: "
                "no programmable shader stage exists on this renderer.");
    }

    void SdlSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // Task 701 finding: TextureFilter has 9 values encoding separate min/mag/mip filter
        // components (see TextureFilter.hpp's own doc comments -- "shrink" = minification,
        // "expand" = magnification), but SDL_SetTextureScaleMode takes a single SDL_ScaleMode
        // applied uniformly (no separate min/mag/mip control, and no LOD/mipmap-level sampling
        // at all in SDL_Renderer's 2D blit pipeline). Since SpriteBatch draws are near-universally
        // magnification-dominant (sprites scaled up or 1:1, never minified with proper LOD
        // selection on this renderer), the MAGNIFICATION ("expand") component is the one that
        // visibly matters and is used here as the effective filter:
        //   Linear=0 (mag=Linear), Anisotropic=2 (linear-based, no SDL equivalent -- approximate
        //   with Linear), LinearMipPoint=3 (mag=Linear), MinPointMagLinearMipLinear=7 (mag=Linear),
        //   MinPointMagLinearMipPoint=8 (mag=Linear) -> SDL_SCALEMODE_LINEAR.
        //   Point=1, PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6
        //   (all mag=Point) -> SDL_SCALEMODE_NEAREST.
        // Previously only textureFilter==0 mapped to Linear -- Anisotropic/LinearMipPoint/
        // MinPointMagLinearMipLinear/MinPointMagLinearMipPoint were silently downgraded to Point
        // filtering despite specifying a Linear magnification filter.
        switch (textureFilter)
        {
            case 0: // Linear
            case 2: // Anisotropic
            case 3: // LinearMipPoint
            case 7: // MinPointMagLinearMipLinear
            case 8: // MinPointMagLinearMipPoint
                scaleMode = SDL_SCALEMODE_LINEAR;
                break;
            default: // Point, PointMipLinear, MinLinearMagPointMipLinear, MinLinearMagPointMipPoint
                scaleMode = SDL_SCALEMODE_NEAREST;
                break;
        }
    }

    void SdlSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        if (!begun) throw std::runtime_error("SdlSpriteBatchRenderer::Draw called before Begin().");
        // Task 705 finding: texture may be an SdlRenderTargetRenderer (a RenderTarget2D sampled as
        // a Texture2D after unbinding) -- a sibling class of SdlTextureRenderer, NOT a subclass, so
        // an unchecked static_cast<const SdlTextureRenderer&> here would be undefined behavior.
        // The SDL-specific sibling interface is implemented by both concrete texture kinds; a
        // foreign renderer texture keeps the established harmless no-draw behaviour.
        SDL_Texture* nativeTex = GetNativeSdlTexture(texture);
        if (!nativeTex) return;
        SDL_SetTextureScaleMode(nativeTex, scaleMode);

        SDL_FRect dst{x, y, static_cast<float>(texture.GetWidth()), static_cast<float>(texture.GetHeight())};
        if (!SDL_RenderTexture(renderer, nativeTex, nullptr, &dst))
        {
            throw std::runtime_error(std::string("SDL_RenderTexture failed: ") + SDL_GetError());
        }
    }

    void SdlSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                     const Rectangle& destinationRectangle,
                                     const Rectangle& sourceRectangle,
                                     const Color& color)
    {
        if (!begun) throw std::runtime_error("SdlSpriteBatchRenderer::Draw called before Begin().");
        // Task 705 finding: see the (x,y) Draw overload above -- texture may be an
        // SdlRenderTargetRenderer, a sibling class of SdlTextureRenderer, so an unchecked
        // static_cast<const SdlTextureRenderer&> here would be undefined behavior.
        SDL_Texture* nativeTex = GetNativeSdlTexture(texture);
        if (!nativeTex) return;
        SDL_SetTextureScaleMode(nativeTex, scaleMode);

        if (!SDL_SetTextureColorMod(nativeTex, color.getRProperty(), color.getGProperty(), color.getBProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureColorMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureAlphaMod(nativeTex, color.getAProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureAlphaMod failed: ") + SDL_GetError());
        }
        SDL_BlendMode currentBlendMode = SDL_BLENDMODE_BLEND;
        SDL_GetRenderDrawBlendMode(renderer, &currentBlendMode);
        if (!SDL_SetTextureBlendMode(nativeTex, currentBlendMode))
        {
            throw std::runtime_error(std::string("SDL_SetTextureBlendMode failed: ") + SDL_GetError());
        }

        SDL_FRect src{
            (float)sourceRectangle.X, (float)sourceRectangle.Y, (float)sourceRectangle.Width,
            (float)sourceRectangle.Height
        };
        SDL_FRect dst{
            (float)destinationRectangle.X, (float)destinationRectangle.Y, (float)destinationRectangle.Width,
            (float)destinationRectangle.Height
        };
        if (!SDL_RenderTexture(renderer, nativeTex, &src, &dst))
        {
            throw std::runtime_error(std::string("SDL_RenderTexture failed: ") + SDL_GetError());
        }
    }

    void SdlSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                     const Rectangle& destinationRectangle,
                                     const Rectangle& sourceRectangle,
                                     const Color& color,
                                     float rotation,
                                     const Vector2& origin,
                                     SpriteEffects effects,
                                     float layerDepth)
    {
        (void)layerDepth;
        if (!begun) throw std::runtime_error("SdlSpriteBatchRenderer::Draw called before Begin().");
        // Task 705 finding: see the (x,y) Draw overload above -- texture may be an
        // SdlRenderTargetRenderer, a sibling class of SdlTextureRenderer, so an unchecked
        // static_cast<const SdlTextureRenderer&> here would be undefined behavior.
        SDL_Texture* nativeTex = GetNativeSdlTexture(texture);
        if (!nativeTex) return;
        SDL_SetTextureScaleMode(nativeTex, scaleMode);

        if (!SDL_SetTextureColorMod(nativeTex, color.getRProperty(), color.getGProperty(), color.getBProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureColorMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureAlphaMod(nativeTex, color.getAProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureAlphaMod failed: ") + SDL_GetError());
        }
        SDL_BlendMode currentBlendMode = SDL_BLENDMODE_BLEND;
        SDL_GetRenderDrawBlendMode(renderer, &currentBlendMode);
        if (!SDL_SetTextureBlendMode(nativeTex, currentBlendMode))
        {
            throw std::runtime_error(std::string("SDL_SetTextureBlendMode failed: ") + SDL_GetError());
        }

        SDL_FRect src{
            (float)sourceRectangle.X, (float)sourceRectangle.Y, (float)sourceRectangle.Width,
            (float)sourceRectangle.Height
        };

        // Task 671 finding: XNA's Draw(destinationRectangle, ..., origin, ...) contract requires
        // `origin` (in source-texture pixel space) to map to exactly (destinationRectangle.X,
        // destinationRectangle.Y) on screen, invariant under rotation (FNA's real
        // GenerateVertexInfo formula subtracts origin before rotating, then adds
        // destinationRectangle.X/Y). SDL_RenderTextureRotated's own contract is the opposite:
        // `center` is a pivot point *within* dstrect's local space, and dstrect itself is placed
        // unrotated first -- so the pivot's actual screen position is
        // (dstrect.x + center.x, dstrect.y + center.y), not dstrect's own (x,y). Passing
        // destinationRectangle.X/Y straight through as dst.x/y (as this code previously did)
        // therefore placed the rotation pivot destinationRectangle.Width/Height away from where
        // XNA requires it. Fixed by offsetting dst.x/y by -sdlCenter so the pivot lands exactly
        // on destinationRectangle.X/Y, matching every other renderer's already-correct behavior.
        const float sdlCenterX = (origin.X / src.w) * static_cast<float>(destinationRectangle.Width);
        const float sdlCenterY = (origin.Y / src.h) * static_cast<float>(destinationRectangle.Height);
        SDL_FRect dst{
            (float)destinationRectangle.X - sdlCenterX, (float)destinationRectangle.Y - sdlCenterY,
            (float)destinationRectangle.Width, (float)destinationRectangle.Height
        };
        SDL_FPoint sdlCenter{sdlCenterX, sdlCenterY};
        double rotationDeg = (double)rotation * 180.0 / 3.14159265358979323846;

        SDL_FlipMode flip = SDL_FLIP_NONE;
        if (((int)effects & (int)SpriteEffects::FlipHorizontally) && ((int)effects & (int)
            SpriteEffects::FlipVertically))
            flip = SDL_FLIP_HORIZONTAL_AND_VERTICAL;
        else if ((int)effects & (int)SpriteEffects::FlipHorizontally) flip = SDL_FLIP_HORIZONTAL;
        else if ((int)effects & (int)SpriteEffects::FlipVertically) flip = SDL_FLIP_VERTICAL;

        // Task 675: SpriteBatch::Begin's transformMatrix was previously silently ignored on this
        // renderer (SetTransformMatrix has no override, so the shared no-op default ran) --
        // SDL_RenderTextureRotated has no way to accept an arbitrary transform on top of its own
        // rotation/flip. Fixed via SDL_RenderTextureAffine, which maps 3 quad corners (origin,
        // right, down) to arbitrary destination points, for the genuinely general case (only
        // taken when transformMatrix isn't Identity -- the common case keeps using
        // SDL_RenderTextureRotated above, unchanged, zero regression risk). The 4 unrotated local
        // corners (relative to the origin pivot, mirroring the sdlCenter math above) are rotated
        // by `rotation` exactly like FNA's own GenerateVertexInfo formula, translated to screen
        // space, then transformed by transformMatrix via Vector2::Transform (world/camera
        // transform applied on top of the sprite's own placement, matching FNA's real vertex
        // pipeline order). Flip is applied by permuting which corner feeds which
        // SDL_RenderTextureAffine parameter (each parameter fixes which SOURCE corner it
        // represents; flipping swaps which SCREEN corner that source corner lands on) rather than
        // by an SDL_FlipMode, which this API doesn't accept.
        if (transformMatrix != Matrix::getIdentityProperty())
        {
            const float cosR = std::cos(rotation);
            const float sinR = std::sin(rotation);
            auto rotateAndPlace = [&](float localX, float localY) -> Vector2
            {
                const float rx = localX * cosR - localY * sinR;
                const float ry = localX * sinR + localY * cosR;
                return Vector2(static_cast<float>(destinationRectangle.X) + rx,
                               static_cast<float>(destinationRectangle.Y) + ry);
            };
            const float w = static_cast<float>(destinationRectangle.Width);
            const float h = static_cast<float>(destinationRectangle.Height);
            const Vector2 topLeft     = Vector2::Transform(rotateAndPlace(-sdlCenterX,     -sdlCenterY),     transformMatrix);
            const Vector2 topRight    = Vector2::Transform(rotateAndPlace(w - sdlCenterX,  -sdlCenterY),     transformMatrix);
            const Vector2 bottomLeft  = Vector2::Transform(rotateAndPlace(-sdlCenterX,     h - sdlCenterY),  transformMatrix);
            const Vector2 bottomRight = Vector2::Transform(rotateAndPlace(w - sdlCenterX,  h - sdlCenterY),  transformMatrix);

            const bool flipH = flip == SDL_FLIP_HORIZONTAL || flip == SDL_FLIP_HORIZONTAL_AND_VERTICAL;
            const bool flipV = flip == SDL_FLIP_VERTICAL   || flip == SDL_FLIP_HORIZONTAL_AND_VERTICAL;
            const Vector2& originCorner = (flipH && flipV) ? bottomRight : flipH ? topRight    : flipV ? bottomLeft  : topLeft;
            const Vector2& rightCorner  = (flipH && flipV) ? bottomLeft  : flipH ? topLeft     : flipV ? bottomRight : topRight;
            const Vector2& downCorner   = (flipH && flipV) ? topRight    : flipH ? bottomRight : flipV ? topLeft     : bottomLeft;

            SDL_FPoint sdlOrigin{originCorner.X, originCorner.Y};
            SDL_FPoint sdlRight{rightCorner.X, rightCorner.Y};
            SDL_FPoint sdlDown{downCorner.X, downCorner.Y};
            if (!SDL_RenderTextureAffine(renderer, nativeTex, &src, &sdlOrigin, &sdlRight, &sdlDown))
            {
                throw std::runtime_error(std::string("SDL_RenderTextureAffine failed: ") + SDL_GetError());
            }
            return;
        }

        if (!SDL_RenderTextureRotated(renderer, nativeTex, &src, &dst, rotationDeg, &sdlCenter, flip))
        {
            throw std::runtime_error(std::string("SDL_RenderTextureRotated failed: ") + SDL_GetError());
        }
    }

    // --- SdlRenderer ---

    static SDL_RendererLogicalPresentation toSdlPresentationMode(CnaPresentationMode mode)
    {
        switch (mode)
        {
        case CnaPresentationMode::Letterbox: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        case CnaPresentationMode::Overscan: return SDL_LOGICAL_PRESENTATION_OVERSCAN;
        case CnaPresentationMode::Stretch: return SDL_LOGICAL_PRESENTATION_STRETCH;
        case CnaPresentationMode::NativeBackBuffer: return SDL_LOGICAL_PRESENTATION_DISABLED;
        case CnaPresentationMode::FixedHeightDynamicWidth: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        default: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        }
    }

    static const char* presentationModeName(CnaPresentationMode mode)
    {
        switch (mode)
        {
        case CnaPresentationMode::Letterbox: return "LETTERBOX";
        case CnaPresentationMode::Overscan: return "OVERSCAN";
        case CnaPresentationMode::Stretch: return "STRETCH";
        case CnaPresentationMode::NativeBackBuffer: return "NATIVE_BACKBUFFER";
        case CnaPresentationMode::FixedHeightDynamicWidth: return "FIXED_HEIGHT_DYNAMIC_WIDTH";
        default: return "UNKNOWN";
        }
    }

    /// When mode is FixedHeightDynamicWidth, derive logicalW from the actual
    /// renderer output size so the canvas exactly matches the surface AR.
    static void applyLogicalPresentation(SDL_Renderer* renderer,
                                         int& logicalWidth, int& logicalHeight,
                                         CnaPresentationMode mode)
    {
        if (mode == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            int outputW = 0, outputH = 0;
            SDL_GetRenderOutputSize(renderer, &outputW, &outputH);
            if (outputH > 0 && logicalHeight > 0)
            {
                logicalWidth = (int)((double)outputW * logicalHeight / outputH + 0.5);
            }
            SDL_Log("[Renderer] FixedHeightDynamicWidth: outputSize=%dx%d logicalSize=%dx%d",
                    outputW, outputH, logicalWidth, logicalHeight);
        }
        SDL_RendererLogicalPresentation sdlMode = toSdlPresentationMode(mode);
        if (!SDL_SetRenderLogicalPresentation(renderer, logicalWidth, logicalHeight, sdlMode))
        {
            SDL_Log("[Renderer] WARNING: SDL_SetRenderLogicalPresentation failed: %s", SDL_GetError());
        }
        else
        {
            SDL_Log("[Renderer] SDL_SetRenderLogicalPresentation set to %dx%d %s",
                    logicalWidth, logicalHeight, presentationModeName(mode));
        }
    }

    SdlRenderer::SdlRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                           CnaPresentationMode mode, int swapInterval)
        : window(window), logicalWidth(virtualWidth), logicalHeight(virtualHeight), presentationMode_(mode)
    {
        if (!window) throw std::runtime_error("SdlRenderer initialized with null window.");

        // NOTE: SDL_Window is NOT owned by the renderer.
        // It is owned by GraphicsDevice or higher level platform layer.

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer)
        {
            throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
        // SDL3 SDL_SetRenderVSync only supports 0 (off) or 1 (on) — map Two→1, Immediate→0.
        if (!SDL_SetRenderVSync(renderer, swapInterval > 0 ? 1 : 0))
        {
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

            if (virtualWidth > 0 && virtualHeight > 0)
            {
                applyLogicalPresentation(renderer, logicalWidth, logicalHeight, mode);
            }
            else
            {
                SDL_Log("[Renderer] No logical presentation set (virtualSize not provided)");
            }
        }

        const char* name = SDL_GetRendererName(renderer);

        if (!name)
        {
            SDL_Log("SDL_GetRendererName failed: %s", SDL_GetError());
        }
        else if (SDL_strcmp(name, "opengl") == 0)
        {
            SDL_Log("SDL_Renderer uses OpenGL");
            std::cout << "SDL_Renderer uses OpenGL" << std::endl;
        }
        else if (SDL_strcmp(name, "gpu") == 0)
        {
            SDL_GPUDevice* device = SDL_GetGPURendererDevice(renderer);
            if (device)
            {
                const char* gpuDriver = SDL_GetGPUDeviceDriver(device);
                SDL_Log("SDL_Renderer = gpu, current renderer = %s",
                        gpuDriver ? gpuDriver : "unknown");
                std::cout << "SDL_Renderer = gpu, current renderer = " << (gpuDriver ? gpuDriver : "unknown") <<
                    std::endl;
            }
            else
            {
                SDL_Log("Renderer is gpu, but GPU device could not be find out: %s", SDL_GetError());
                std::cout << "Renderer is gpu, but GPU device could not be find out: " << SDL_GetError() << std::endl;
            }
        }
        else if (SDL_strcmp(name, "vulkan") == 0)
        {
            SDL_Log("SDL_Renderer uses Vulkan");
            std::cout << "SDL_Renderer uses Vulkan" << std::endl;
        }
        else
        {
            SDL_Log("SDL_Renderer renderer: %s", name);
            std::cout << "SDL_Renderer renderer: " << name << std::endl;
        }

        // Task 456: one-time startup capability dump. This renderer is 2D-only by design (Tasks
        // 720-729's own exhaustive audit) -- no MSAA/MRT/anisotropic-filtering/3D capability.
        std::cout << "CNA: SDL_Renderer capabilities -- 2D-only renderer; no MSAA, no MRT "
                     "(more than 1 simultaneous render target throws), no anisotropic filtering; "
                     "unsupported 3D calls preserve their established throw/null behavior by "
                     "default and can be changed to warn-once safe stubs with "
                     "Unsupported3DGraphicsCallBehavior::WarnAndStub; "
                     "SurfaceFormat: Color only (Task 176)" << std::endl;

        registeredWindowId_ = SDL_GetWindowID(window);
        IGraphicsRenderer::RegisterForWindow(registeredWindowId_, this);
    }

    SdlRenderer::~SdlRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(registeredWindowId_);
        if (renderer) SDL_DestroyRenderer(renderer);
        // window is NOT owned by the renderer.
        // No SDL_Quit or subsystem shutdown here - managed centrally.
    }

    bool SdlRenderer::TransformWindowToLogical(const float windowX, const float windowY,
                                                float& logicalX, float& logicalY) const
    {
        return renderer != nullptr &&
               SDL_RenderCoordinatesFromWindow(
                   renderer, windowX, windowY, &logicalX, &logicalY);
    }

    bool SdlRenderer::TransformLogicalToWindow(const float logicalX, const float logicalY,
                                                float& windowX, float& windowY) const
    {
        return renderer != nullptr &&
               SDL_RenderCoordinatesToWindow(
                   renderer, logicalX, logicalY, &windowX, &windowY);
    }

    void SdlRenderer::Clear(float r, float g, float b, float a)
    {
        if (!SDL_SetRenderDrawColor(renderer, (Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f),
                                    (Uint8)(a * 255.0f)))
        {
            throw std::runtime_error(std::string("SDL_SetRenderDrawColor failed: ") + SDL_GetError());
        }
        if (!SDL_RenderClear(renderer))
        {
            throw std::runtime_error(std::string("SDL_RenderClear failed: ") + SDL_GetError());
        }
    }

    void SdlRenderer::Present()
    {
        // Re-apply logical presentation whenever the physical output size changes.
        // On Android the surface may be 0x0 at construction time; by re-checking
        // here we pick up the valid size as soon as it becomes available.
        if (renderer && logicalHeight > 0)
        {
            int outputW = 0, outputH = 0;
            SDL_GetRenderOutputSize(renderer, &outputW, &outputH);
            if (outputW > 0 && outputH > 0 &&
                (outputW != lastOutputW_ || outputH != lastOutputH_))
            {
                SDL_Log("[Renderer] Output size changed: %dx%d -> %dx%d, re-applying logical presentation",
                        lastOutputW_, lastOutputH_, outputW, outputH);
                lastOutputW_ = outputW;
                lastOutputH_ = outputH;
                applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
            }
        }
        if (!SDL_RenderPresent(renderer))
        {
            throw std::runtime_error(std::string("SDL_RenderPresent failed: ") + SDL_GetError());
        }
    }

    void SdlRenderer::SetVirtualResolution(int width, int height)
    {
        logicalWidth = width;
        logicalHeight = height;
        if (renderer && (logicalWidth > 0 || logicalHeight > 0))
        {
            applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
        }
    }

    void SdlRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        SDL_Log("[Renderer] SetPresentationMode: %s", presentationModeName(presentationMode_));
        if (renderer && (logicalWidth > 0 || logicalHeight > 0))
        {
            applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
        }
    }

    void SdlRenderer::SetSwapInterval(int interval)
    {
        if (!renderer) return;
        // Task 713 finding: interval is already exactly 0 (PresentInterval::Immediate), 1
        // (Default/One), or 2 (Two) per GraphicsDevice.cpp's own toSwapInterval() -- this
        // previously collapsed ANY positive interval down to 1, silently discarding
        // PresentInterval::Two's "wait for two vertical retrace periods, half refresh rate"
        // semantics. SDL_SetRenderVSync's own doc comment confirms passing 2 directly means
        // exactly that ("synchronize present with every second vertical refresh"), so the value
        // is now passed straight through instead of collapsed.
        if (SDL_SetRenderVSync(renderer, interval)) return;
        // Not every value is supported by every driver (SDL_SetRenderVSync's own doc comment --
        // confirmed empirically: this project's own sandbox GL driver rejects interval=2). Fall
        // back to the closest universally-supported approximation (every-refresh vsync) rather
        // than silently leaving vsync at whatever value happened to be set previously.
        if (interval > 1)
            SDL_SetRenderVSync(renderer, 1);
    }

    int SdlRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        // Task 714 decision: SDL_Renderer's 2D blit pipeline has no MSAA control at all -- accept
        // any requested MultiSampleCount without throwing (a caller targeting this renderer
        // alongside the other 3 MSAA-capable ones shouldn't be penalized for a harmless,
        // unactionable request; SpriteBatch's 2D draws have no anti-aliasing seams to smooth over
        // in the first place), but always report back 0 (matches FNA's own device-clamped
        // write-back semantics -- this renderer's real clamped maximum genuinely is 0). Logged
        // once per non-zero request so a game that expects MSAA and doesn't see it has a clear
        // diagnostic trail.
        if (requestedMultiSampleCount > 0)
        {
            SDL_Log("[Renderer] MultiSampleCount=%d requested but ignored: SDL_Renderer's 2D pipeline has no MSAA control.",
                    requestedMultiSampleCount);
        }
        return 0;
    }

    void SdlRenderer::GetViewportSize(int& width, int& height)
    {
        // If virtual resolution was never configured, fall back to the physical
        // output size so the game at least gets a valid non-zero viewport.
        if ((logicalWidth <= 0 || logicalHeight <= 0) && renderer)
        {
            int outputW = 0, outputH = 0;
            SDL_GetRenderOutputSize(renderer, &outputW, &outputH);
            if (outputW > 0 && outputH > 0)
            {
                SDL_Log("[Renderer] GetViewportSize: virtual size unset, falling back to physical %dx%d",
                        outputW, outputH);
                width = outputW;
                height = outputH;
                return;
            }
        }
        // Return the logical (virtual) resolution, not the physical surface size.
        // SDL_SetRenderLogicalPresentation handles the physical-to-logical mapping,
        // so the game always works in its own coordinate space.
        width = logicalWidth;
        height = logicalHeight;
    }

    // Task 666: SDL_RenderReadPixels operates in physical output coordinates, but callers
    // (GraphicsDevice::GetBackBufferData) pass logical (virtual-resolution) coordinates,
    // matching every other renderer's convention and GetViewportSize()'s own logical values.
    // When targeting the actual window backbuffer (SDL_GetRenderTarget == nullptr), map through
    // SDL_GetRenderLogicalPresentationRect() -- this project's pixel tests always create a window
    // matching the virtual resolution 1:1 (no letterbox/stretch scaling), so the mapped rect's
    // size should exactly match the logical size; if it doesn't, something is scaling and
    // exact-pixel readback can't be trusted, so this throws clearly rather than silently
    // returning wrong/aliased data. When a custom render target is bound instead, logical
    // presentation doesn't apply at all -- the requested coordinates already address the target
    // texture directly.
    void SdlRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (!renderer)
            throw std::runtime_error("ReadBackbuffer: no renderer");

        int originX = 0, originY = 0;
        if (SDL_GetRenderTarget(renderer) == nullptr)
        {
            SDL_FRect presentRect{};
            SDL_GetRenderLogicalPresentationRect(renderer, &presentRect);
            const int physW = static_cast<int>(presentRect.w);
            const int physH = static_cast<int>(presentRect.h);
            if (physW != logicalWidth || physH != logicalHeight)
            {
                throw std::runtime_error("ReadBackbuffer: physical/logical size mismatch "
                                          "(letterbox or stretch scaling active) -- exact-pixel "
                                          "readback unsupported");
            }
            originX = static_cast<int>(presentRect.x);
            originY = static_cast<int>(presentRect.y);
        }

        SDL_Rect region{ originX + x, originY + y, w, h };
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, &region);
        if (!surface)
            throw std::runtime_error(std::string("SDL_RenderReadPixels failed: ") + SDL_GetError());

        SDL_Surface* converted = surface;
        bool ownsConverted = false;
        if (surface->format != SDL_PIXELFORMAT_RGBA32)
        {
            converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
            if (!converted)
            {
                SDL_DestroySurface(surface);
                throw std::runtime_error(std::string("SDL_ConvertSurface failed: ") + SDL_GetError());
            }
            ownsConverted = true;
        }

        const auto* base = static_cast<const uint8_t*>(converted->pixels);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src = base + static_cast<std::size_t>(row) * converted->pitch;
            std::memcpy(pixels + static_cast<std::size_t>(row) * w * 4, src, static_cast<std::size_t>(w) * 4);
        }

        if (ownsConverted)
            SDL_DestroySurface(converted);
        SDL_DestroySurface(surface);
    }

    // Maps Microsoft::Xna::Framework::Graphics::Blend's 13 values to SDL_BlendFactor. The first
    // 10 (One..InverseDestinationAlpha) have exact SDL equivalents; BlendFactor/InverseBlendFactor
    // (a constant colour set via GraphicsDevice::BlendFactor) and SourceAlphaSaturation have no
    // SDL_BlendFactor equivalent at all -- throw rather than silently substitute a wrong factor
    // (Task 700's own "must not silently ... per unsupported combination" framing).
    static SDL_BlendFactor ToSdlBlendFactor(int blend)
    {
        switch (blend)
        {
            case 0: return SDL_BLENDFACTOR_ONE;                 // Blend::One
            case 1: return SDL_BLENDFACTOR_ZERO;                // Blend::Zero
            case 2: return SDL_BLENDFACTOR_SRC_COLOR;           // Blend::SourceColor
            case 3: return SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR; // Blend::InverseSourceColor
            case 4: return SDL_BLENDFACTOR_SRC_ALPHA;           // Blend::SourceAlpha
            case 5: return SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; // Blend::InverseSourceAlpha
            case 6: return SDL_BLENDFACTOR_DST_COLOR;           // Blend::DestinationColor
            case 7: return SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR; // Blend::InverseDestinationColor
            case 8: return SDL_BLENDFACTOR_DST_ALPHA;           // Blend::DestinationAlpha
            case 9: return SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA; // Blend::InverseDestinationAlpha
            default:
                throw std::runtime_error(
                    "SDL_Renderer does not support Blend::BlendFactor/InverseBlendFactor/"
                    "SourceAlphaSaturation: no equivalent SDL_BlendFactor exists for a constant "
                    "blend-factor colour or alpha saturation.");
        }
    }

    // Maps BlendFunction's 5 values to SDL_BlendOperation -- a direct 1:1 match.
    static SDL_BlendOperation ToSdlBlendOperation(int func)
    {
        switch (func)
        {
            case 0:  return SDL_BLENDOPERATION_ADD;          // BlendFunction::Add
            case 1:  return SDL_BLENDOPERATION_SUBTRACT;     // BlendFunction::Subtract
            case 2:  return SDL_BLENDOPERATION_REV_SUBTRACT; // BlendFunction::ReverseSubtract
            case 3:  return SDL_BLENDOPERATION_MAXIMUM;      // BlendFunction::Max
            default: return SDL_BLENDOPERATION_MINIMUM;      // BlendFunction::Min
        }
    }

    void SdlRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc,
                                              const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: SDL_Renderer (2D) exposes only a blend mode via SDL_ComposeCustomBlendMode
        // — there is NO per-channel colour-write-mask API and NO coverage-sample-mask API. Both
        // BlendState.ColorWriteChannels* and BlendState.MultiSampleMask are therefore genuinely
        // inexpressible on this renderer (documented capability gap, not a silent drop).
        const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
            ToSdlBlendFactor(colorSrcBlend), ToSdlBlendFactor(colorDstBlend),
            ToSdlBlendOperation(colorBlendFunc),
            ToSdlBlendFactor(alphaSrcBlend), ToSdlBlendFactor(alphaDstBlend),
            ToSdlBlendOperation(alphaBlendFunc));
        blendMode_ = mode;
        if (!SDL_SetRenderDrawBlendMode(renderer, blendMode_))
        {
            throw std::runtime_error(std::string("SDL_SetRenderDrawBlendMode failed: ") + SDL_GetError());
        }
    }

    void SdlRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
        {
            SDL_SetRenderClipRect(renderer, nullptr);
            return;
        }
        SDL_Rect rect{ x, y, w, h };
        SDL_SetRenderClipRect(renderer, &rect);
    }

    // --- SdlRenderTargetRenderer ---

    SdlRenderTargetRenderer::SdlRenderTargetRenderer(SDL_Renderer* r, int w, int h)
        : renderer(r), width(w), height(h)
    {
        texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_TARGET, w, h);
        if (!texture)
            throw std::runtime_error(std::string("SdlRenderTargetRenderer: SDL_CreateTexture failed: ") + SDL_GetError());
    }

    SdlRenderTargetRenderer::~SdlRenderTargetRenderer()
    {
        if (texture) SDL_DestroyTexture(texture);
    }

    void SdlRenderTargetRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (texture && rgba) SDL_UpdateTexture(texture, nullptr, rgba, stride);
    }

    bool SdlRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        // Task 681's precedent: an SDL_Renderer texture has no native mip chain at all, so a
        // level > 0 request is rejected rather than answered from level 0.
        if (level > 0)
            throw System::NotSupportedException(
                "SdlRenderTargetRenderer::GetData: SDL_Renderer render targets have no mip chain; "
                "level " + std::to_string(level) + " was requested.");
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping into an
        // apparently valid one.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(width) || bottom > static_cast<std::int64_t>(height))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(width) + "x" +
                    std::to_string(height) + " render target.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");
        if (!renderer || !texture || !data)
            return false;

        // SDL_RenderReadPixels always reads the CURRENT target, so this target has to be made
        // current for the duration of the read and the caller's target restored afterwards.
        SDL_Texture* const previousTarget = SDL_GetRenderTarget(renderer);
        if (!SDL_SetRenderTarget(renderer, texture))
            return false;

        const SDL_Rect region{ x, y, w, h };
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, &region);
        SDL_SetRenderTarget(renderer, previousTarget);
        if (!surface)
            return false;

        SDL_Surface* converted = surface;
        bool ownsConverted = false;
        if (surface->format != SDL_PIXELFORMAT_RGBA32)
        {
            converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
            if (!converted)
            {
                SDL_DestroySurface(surface);
                return false;
            }
            ownsConverted = true;
        }

        const auto* base = static_cast<const uint8_t*>(converted->pixels);
        auto* dst = static_cast<uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                        base + static_cast<std::size_t>(row) * converted->pitch, rowBytes);

        if (ownsConverted) SDL_DestroySurface(converted);
        SDL_DestroySurface(surface);
        return true;
    }

    void SdlRenderTargetRenderer::BindAsRenderTarget()
    {
        SDL_SetRenderTarget(renderer, texture);
    }

    void SdlRenderTargetRenderer::UnbindAsRenderTarget()
    {
        SDL_SetRenderTarget(renderer, nullptr);
    }

    // ---

    std::unique_ptr<ITextureRenderer> SdlRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SdlTextureRenderer>(renderer, data);
    }

    std::unique_ptr<IRenderTargetRenderer> SdlRenderer::CreateRenderTarget2D(int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        return std::make_unique<SdlRenderTargetRenderer>(renderer, w, h);
    }

    void SdlRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
            SDL_SetRenderTarget(renderer, nullptr);
    }

    void SdlRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
            throw std::runtime_error(
                "SDL_Renderer does not support multiple simultaneous render targets (MRT): "
                "requested " + std::to_string(count) + ", but this renderer's 2D render pipeline "
                "supports exactly one active render target at a time.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "SDL_Renderer does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(
            count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    std::unique_ptr<ISpriteBatchRenderer> SdlRenderer::CreateSpriteBatch()
    {
        return std::make_unique<SdlSpriteBatchRenderer>(renderer);
    }

    // ---- 3D: SDL_Renderer is intentionally 2D-only. ----
    // Throw remains the default. WarnAndStub logs each method once and returns safely.
    void SdlRenderer::ClearColorAndDepth(float, float, float, float, float) { HandleUnsupported3DCall("SDL_Renderer", "ClearColorAndDepth"); }
    void SdlRenderer::ClearDepth(float) { HandleUnsupported3DCall("SDL_Renderer", "ClearDepth"); }
    void SdlRenderer::ClearStencil(int) { HandleUnsupported3DCall("SDL_Renderer", "ClearStencil"); }
    void SdlRenderer::ClearDepthAndStencil(float, int) { HandleUnsupported3DCall("SDL_Renderer", "ClearDepthAndStencil"); }
    void SdlRenderer::ClearColorAndStencil(float, float, float, float, int) { HandleUnsupported3DCall("SDL_Renderer", "ClearColorAndStencil"); }
    void SdlRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { HandleUnsupported3DCall("SDL_Renderer", "ClearColorDepthAndStencil"); }
    void SdlRenderer::SetDepthTestEnabled(bool)  { HandleUnsupported3DCall("SDL_Renderer", "SetDepthTestEnabled"); }
    void SdlRenderer::SetBlendEnabled(bool)      { HandleUnsupported3DCall("SDL_Renderer", "SetBlendEnabled"); }
    void SdlRenderer::SetDepthWriteEnabled(bool) { HandleUnsupported3DCall("SDL_Renderer", "SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> SdlRenderer::CreateVertexBuffer(
        int vertexCapacity)
    {
        HandleUnsupported3DCall("SDL_Renderer", "CreateVertexBuffer");
        return std::make_unique<NoOpVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> SdlRenderer::CreateIndexBuffer16(int indexCapacity)
    {
        HandleUnsupported3DCall("SDL_Renderer", "CreateIndexBuffer16");
        return std::make_unique<NoOpIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<ITexture3DRenderer> SdlRenderer::CreateTexture3D(
        int width, int height, int depth, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("SDL_Renderer", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(width, height, depth);
    }

    std::unique_ptr<ITextureCubeRenderer> SdlRenderer::CreateTextureCube(
        int size, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("SDL_Renderer", "CreateTextureCube");
        return std::make_unique<NoOpTextureCubeRenderer>(size);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SdlRenderer::CreateRenderTargetCube(
        int size, int, bool, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("SDL_Renderer", "CreateRenderTargetCube");
        return std::make_unique<NoOpRenderTargetCubeRenderer>(size);
    }

    std::unique_ptr<IOcclusionQueryRenderer> SdlRenderer::CreateOcclusionQuery()
    {
        HandleUnsupported3DCall("SDL_Renderer", "CreateOcclusionQuery");
        return std::make_unique<NoOpOcclusionQueryRenderer>();
    }

    void SdlRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&,
                                                   const Matrix&, const Matrix&, const Matrix&,
                                                   PrimitiveType, int) { HandleUnsupported3DCall("SDL_Renderer", "DrawColoredPrimitives"); }

    void SdlRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&,
                                                          const IIndexBufferRenderer&,
                                                          const Matrix&, const Matrix&, const Matrix&,
                                                          PrimitiveType, int) { HandleUnsupported3DCall("SDL_Renderer", "DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_SDL_RENDERER
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace SdlRenderer { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> SdlRenderer::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        // Fully qualified from the global namespace on purpose: this family's namespace and its
        // class are both called SdlRenderer, so inside the qualified definition above the name
        // SdlRenderer resolves to the NAMESPACE and SdlRenderer::SdlRenderer no longer names the
        // class. The only family where the P2 rename collides with its own naming.
        return std::make_unique<::CNA::Internal::Renderers::SdlRenderer::SdlRenderer>(
            CNA::Platform::Detail::ResolveSdl3RendererWindow(args.surface.windowId),
            args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
#endif
}
