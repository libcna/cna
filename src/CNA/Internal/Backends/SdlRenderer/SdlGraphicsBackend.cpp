#include "CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <SDL3/SDL_gpu.h>

namespace CNA::Internal::Backends::SdlRenderer
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    // --- SdlTextureBackend ---

    SdlTextureBackend::SdlTextureBackend(SDL_Renderer* renderer, const ImageData& data)
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

    SdlTextureBackend::~SdlTextureBackend()
    {
        if (texture)
        {
            SDL_DestroyTexture(texture);
        }
    }

    void SdlTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!texture || !rgba) return;
        SDL_UpdateTexture(texture, nullptr, rgba, stride);
    }

    void SdlTextureBackend::UpdatePixelsLevel(int level, const uint8_t*, int, int)
    {
        throw std::runtime_error(
            "SDL_Renderer does not support mip-level texture uploads (level " + std::to_string(level) +
            "): SDL_Renderer's 2D texture API has no native mip chain or per-level LOD sampling. "
            "Use Texture2D::SetData(level=0, ...) only, or generate mips via a mipMap-aware backend.");
    }

    // --- SdlSpriteBatchBackend ---

    SdlSpriteBatchBackend::SdlSpriteBatchBackend(SDL_Renderer* r) : renderer(r)
    {
    }

    void SdlSpriteBatchBackend::Begin()
    {
        // Task 695 finding: this previously hardcoded SDL_SetRenderDrawBlendMode(SDL_BLENDMODE_BLEND)
        // unconditionally, clobbering whatever SdlGraphicsBackend::ApplyBlendState had just set via
        // GraphicsDevice::setBlendStateProperty(blendState) -- called by SpriteBatch::Begin()
        // immediately before backend_->Begin() runs (see SpriteBatch.cpp). This was invisible while
        // ApplyBlendState's own mapping was still incomplete (BlendState::AlphaBlend/NonPremultiplied
        // both happened to resolve to this exact same SDL_BLENDMODE_BLEND value anyway), but became a
        // real, visible bug once ApplyBlendState was fixed to distinguish them correctly.
        if (!renderer) throw std::runtime_error("SdlSpriteBatchBackend::Begin failed: renderer is null.");
        begun = true;
    }

    void SdlSpriteBatchBackend::End()
    {
        begun = false;
    }

    void SdlSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "SDL_Renderer does not support custom SpriteBatch Effects: "
                "no programmable shader stage exists on this backend.");
    }

    void SdlSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        // Task 701 finding: TextureFilter has 9 values encoding separate min/mag/mip filter
        // components (see TextureFilter.hpp's own doc comments -- "shrink" = minification,
        // "expand" = magnification), but SDL_SetTextureScaleMode takes a single SDL_ScaleMode
        // applied uniformly (no separate min/mag/mip control, and no LOD/mipmap-level sampling
        // at all in SDL_Renderer's 2D blit pipeline). Since SpriteBatch draws are near-universally
        // magnification-dominant (sprites scaled up or 1:1, never minified with proper LOD
        // selection on this backend), the MAGNIFICATION ("expand") component is the one that
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

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;
        SDL_SetTextureScaleMode(sdlTex.texture, scaleMode);

        SDL_FRect dst{x, y, static_cast<float>(sdlTex.width), static_cast<float>(sdlTex.height)};
        if (!SDL_RenderTexture(renderer, sdlTex.texture, nullptr, &dst))
        {
            throw std::runtime_error(std::string("SDL_RenderTexture failed: ") + SDL_GetError());
        }
    }

    void SdlSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                     const Rectangle& destinationRectangle,
                                     const Rectangle& sourceRectangle,
                                     const Color& color)
    {
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;
        SDL_SetTextureScaleMode(sdlTex.texture, scaleMode);

        if (!SDL_SetTextureColorMod(sdlTex.texture, color.getRProperty(), color.getGProperty(), color.getBProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureColorMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureAlphaMod(sdlTex.texture, color.getAProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureAlphaMod failed: ") + SDL_GetError());
        }
        SDL_BlendMode currentBlendMode = SDL_BLENDMODE_BLEND;
        SDL_GetRenderDrawBlendMode(renderer, &currentBlendMode);
        if (!SDL_SetTextureBlendMode(sdlTex.texture, currentBlendMode))
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
        if (!SDL_RenderTexture(renderer, sdlTex.texture, &src, &dst))
        {
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
                                     float layerDepth)
    {
        (void)layerDepth;
        if (!begun) throw std::runtime_error("SdlSpriteBatchBackend::Draw called before Begin().");
        auto& sdlTex = static_cast<const SdlTextureBackend&>(texture);
        if (!sdlTex.texture) return;
        SDL_SetTextureScaleMode(sdlTex.texture, scaleMode);

        if (!SDL_SetTextureColorMod(sdlTex.texture, color.getRProperty(), color.getGProperty(), color.getBProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureColorMod failed: ") + SDL_GetError());
        }
        if (!SDL_SetTextureAlphaMod(sdlTex.texture, color.getAProperty()))
        {
            throw std::runtime_error(std::string("SDL_SetTextureAlphaMod failed: ") + SDL_GetError());
        }
        SDL_BlendMode currentBlendMode = SDL_BLENDMODE_BLEND;
        SDL_GetRenderDrawBlendMode(renderer, &currentBlendMode);
        if (!SDL_SetTextureBlendMode(sdlTex.texture, currentBlendMode))
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
        // on destinationRectangle.X/Y, matching every other backend's already-correct behavior.
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
        // backend (SetTransformMatrix has no override, so the shared no-op default ran) --
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
            if (!SDL_RenderTextureAffine(renderer, sdlTex.texture, &src, &sdlOrigin, &sdlRight, &sdlDown))
            {
                throw std::runtime_error(std::string("SDL_RenderTextureAffine failed: ") + SDL_GetError());
            }
            return;
        }

        if (!SDL_RenderTextureRotated(renderer, sdlTex.texture, &src, &dst, rotationDeg, &sdlCenter, flip))
        {
            throw std::runtime_error(std::string("SDL_RenderTextureRotated failed: ") + SDL_GetError());
        }
    }

    // --- SdlGraphicsBackend ---

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

    SdlGraphicsBackend::SdlGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                           CnaPresentationMode mode, int swapInterval)
        : window(window), logicalWidth(virtualWidth), logicalHeight(virtualHeight), presentationMode_(mode)
    {
        if (!window) throw std::runtime_error("SdlGraphicsBackend initialized with null window.");

        // NOTE: SDL_Window is NOT owned by the backend.
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
                SDL_Log("SDL_Renderer = gpu, current backend = %s",
                        gpuDriver ? gpuDriver : "unknown");
                std::cout << "SDL_Renderer = gpu, current backend = " << (gpuDriver ? gpuDriver : "unknown") <<
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
            SDL_Log("SDL_Renderer backend: %s", name);
            std::cout << "SDL_Renderer backend: " << name << std::endl;
        }
    }

    SdlGraphicsBackend::~SdlGraphicsBackend()
    {
        if (renderer) SDL_DestroyRenderer(renderer);
        // window is NOT owned by the backend.
        // No SDL_Quit or subsystem shutdown here - managed centrally.
    }

    void SdlGraphicsBackend::Clear(float r, float g, float b, float a)
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

    void SdlGraphicsBackend::Present()
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

    void SdlGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        logicalWidth = width;
        logicalHeight = height;
        if (renderer && (logicalWidth > 0 || logicalHeight > 0))
        {
            applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
        }
    }

    void SdlGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        SDL_Log("[Renderer] SetPresentationMode: %s", presentationModeName(presentationMode_));
        if (renderer && (logicalWidth > 0 || logicalHeight > 0))
        {
            applyLogicalPresentation(renderer, logicalWidth, logicalHeight, presentationMode_);
        }
    }

    void SdlGraphicsBackend::SetSwapInterval(int interval)
    {
        if (renderer)
            SDL_SetRenderVSync(renderer, interval > 0 ? 1 : 0);
    }

    void SdlGraphicsBackend::GetViewportSize(int& width, int& height)
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
    // matching every other backend's convention and GetViewportSize()'s own logical values.
    // When targeting the actual window backbuffer (SDL_GetRenderTarget == nullptr), map through
    // SDL_GetRenderLogicalPresentationRect() -- this project's pixel tests always create a window
    // matching the virtual resolution 1:1 (no letterbox/stretch scaling), so the mapped rect's
    // size should exactly match the logical size; if it doesn't, something is scaling and
    // exact-pixel readback can't be trusted, so this throws clearly rather than silently
    // returning wrong/aliased data. When a custom render target is bound instead, logical
    // presentation doesn't apply at all -- the requested coordinates already address the target
    // texture directly.
    void SdlGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
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

    void SdlGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc)
    {
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

    void SdlGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
        {
            SDL_SetRenderClipRect(renderer, nullptr);
            return;
        }
        SDL_Rect rect{ x, y, w, h };
        SDL_SetRenderClipRect(renderer, &rect);
    }

    // --- SdlRenderTargetBackend ---

    SdlRenderTargetBackend::SdlRenderTargetBackend(SDL_Renderer* r, int w, int h)
        : renderer(r), width(w), height(h)
    {
        texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_TARGET, w, h);
        if (!texture)
            throw std::runtime_error(std::string("SdlRenderTargetBackend: SDL_CreateTexture failed: ") + SDL_GetError());
    }

    SdlRenderTargetBackend::~SdlRenderTargetBackend()
    {
        if (texture) SDL_DestroyTexture(texture);
    }

    void SdlRenderTargetBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (texture && rgba) SDL_UpdateTexture(texture, nullptr, rgba, stride);
    }

    void SdlRenderTargetBackend::BindAsRenderTarget()
    {
        SDL_SetRenderTarget(renderer, texture);
    }

    void SdlRenderTargetBackend::UnbindAsRenderTarget()
    {
        SDL_SetRenderTarget(renderer, nullptr);
    }

    // ---

    std::unique_ptr<ITextureBackend> SdlGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SdlTextureBackend>(renderer, data);
    }

    std::unique_ptr<IRenderTargetBackend> SdlGraphicsBackend::CreateRenderTarget2D(int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        return std::make_unique<SdlRenderTargetBackend>(renderer, w, h);
    }

    void SdlGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
            SDL_SetRenderTarget(renderer, nullptr);
    }

    std::unique_ptr<ISpriteBatchBackend> SdlGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SdlSpriteBatchBackend>(renderer);
    }

    // ---- 3D: SDL_Renderer is intentionally 2D-only. All 3D calls throw. ----
    static void ThrowNo3D(const char* methodName)
    {
        throw std::runtime_error(
            std::string("SDL_Renderer does not support 3D: ") + methodName);
    }

    void SdlGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void SdlGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void SdlGraphicsBackend::SetDepthTestEnabled(bool)  { ThrowNo3D("SetDepthTestEnabled"); }
    void SdlGraphicsBackend::SetBlendEnabled(bool)      { ThrowNo3D("SetBlendEnabled"); }
    void SdlGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> SdlGraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
        return nullptr;
    }

    std::unique_ptr<IIndexBufferBackend> SdlGraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
        return nullptr;
    }

    void SdlGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                   const Matrix&, const Matrix&, const Matrix&,
                                                   PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }

    void SdlGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                                          const IIndexBufferBackend&,
                                                          const Matrix&, const Matrix&, const Matrix&,
                                                          PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_SDL_RENDERER
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<SdlRenderer::SdlGraphicsBackend>(args.window, args.virtualWidth, args.virtualHeight,
                                                                 args.presentationMode, args.swapInterval);
    }
#endif
}
