#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"

#include <bgfx/embedded_shader.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include "fs_ocornut_imgui.bin.h"
#include "vs_ocornut_imgui.bin.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Bgfx
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    namespace
    {
        const char* kRendererOverrideEnvVar = "CNA_BGFX_RENDERER";

        struct SpriteVertex
        {
            float x;
            float y;
            float u;
            float v;
            uint32_t abgr;
        };

        const bgfx::VertexLayout& GetSpriteVertexLayout()
        {
            static bgfx::VertexLayout layout;
            static bool initialized = false;

            if (!initialized)
            {
                layout
                    .begin()
                    .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
                    .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                    .end();

                initialized = true;
            }

            return layout;
        }

        const bgfx::EmbeddedShader kEmbeddedShaders[] = {
            BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
            BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
            BGFX_EMBEDDED_SHADER_END()
        };

        uint8_t ToByte(float value)
        {
            const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
            return static_cast<uint8_t>(scaled);
        }

        uint32_t ToAbgr(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return (static_cast<uint32_t>(a) << 24)
                | (static_cast<uint32_t>(b) << 16)
                | (static_cast<uint32_t>(g) << 8)
                | static_cast<uint32_t>(r);
        }

        uint32_t ToRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return (static_cast<uint32_t>(r) << 24)
                | (static_cast<uint32_t>(g) << 16)
                | (static_cast<uint32_t>(b) << 8)
                | static_cast<uint32_t>(a);
        }

        uint32_t ToAbgr(const Color& color)
        {
            return ToAbgr(color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty());
        }

        const char* RendererTypeName(bgfx::RendererType::Enum type)
        {
            switch (type)
            {
            case bgfx::RendererType::Noop:
                return "Noop";
            case bgfx::RendererType::Direct3D11:
                return "Direct3D11";
            case bgfx::RendererType::Direct3D12:
                return "Direct3D12";
            case bgfx::RendererType::Metal:
                return "Metal";
            case bgfx::RendererType::OpenGL:
                return "OpenGL";
            case bgfx::RendererType::OpenGLES:
                return "OpenGLES";
            case bgfx::RendererType::Vulkan:
                return "Vulkan";
            case bgfx::RendererType::Count:
                return "Auto";
            default:
                return "Unknown";
            }
        }

        bgfx::PlatformData CreatePlatformData(SDL_Window* window)
        {
            bgfx::PlatformData platformData{};

            const SDL_PropertiesID windowProperties = SDL_GetWindowProperties(window);

#if defined(_WIN32)
            platformData.nwh = SDL_GetPointerProperty(windowProperties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
            platformData.nwh = SDL_GetPointerProperty(windowProperties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__linux__)
            const char* videoDriver = SDL_GetCurrentVideoDriver();

            if (videoDriver && SDL_strcmp(videoDriver, "x11") == 0)
            {
                platformData.type = bgfx::NativeWindowHandleType::Default;
                platformData.ndt = SDL_GetPointerProperty(windowProperties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
                                                          nullptr);

                const Uint64 windowHandle = SDL_GetNumberProperty(windowProperties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
                                                                  0);
                platformData.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(windowHandle));

                if (!platformData.ndt)
                {
                    throw std::runtime_error(
                        "Failed to initialize BGFX backend: SDL X11 display handle is not available.");
                }
                if (!platformData.nwh)
                {
                    throw std::runtime_error(
                        "Failed to initialize BGFX backend: SDL X11 window handle is not available.");
                }
            }
            else if (videoDriver && SDL_strcmp(videoDriver, "wayland") == 0)
            {
                platformData.type = bgfx::NativeWindowHandleType::Wayland;
                platformData.ndt = SDL_GetPointerProperty(windowProperties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,
                                                          nullptr);
                platformData.nwh = SDL_GetPointerProperty(windowProperties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
                                                          nullptr);

                if (!platformData.ndt)
                {
                    throw std::runtime_error(
                        "Failed to initialize BGFX backend: SDL Wayland display handle is not available.");
                }
                if (!platformData.nwh)
                {
                    throw std::runtime_error(
                        "Failed to initialize BGFX backend: SDL Wayland surface handle is not available.");
                }
            }
#endif

            return platformData;
        }

        bgfx::ProgramHandle CreateSpriteProgram()
        {
            const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();

            const bgfx::ShaderHandle vertexShader =
                bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "vs_ocornut_imgui");
            if (!bgfx::isValid(vertexShader))
            {
                throw std::runtime_error("Failed to create BGFX vertex shader (vs_ocornut_imgui).");
            }

            const bgfx::ShaderHandle fragmentShader =
                bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "fs_ocornut_imgui");
            if (!bgfx::isValid(fragmentShader))
            {
                bgfx::destroy(vertexShader);
                throw std::runtime_error("Failed to create BGFX fragment shader (fs_ocornut_imgui).");
            }

            const bgfx::ProgramHandle program = bgfx::createProgram(vertexShader, fragmentShader, true);
            if (!bgfx::isValid(program))
            {
                throw std::runtime_error("Failed to link BGFX sprite shader program.");
            }

            return program;
        }
    }

    BgfxTextureBackend::BgfxTextureBackend(const ImageData& data)
    {
        width = data.width;
        height = data.height;

        if (width <= 0 || height <= 0 || data.pixels.empty())
        {
            throw std::runtime_error("Failed to create BGFX texture: image data is empty.");
        }

        const bgfx::Memory* memory = bgfx::copy(data.pixels.data(), static_cast<uint32_t>(data.pixels.size()));
        textureHandle = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory
        );

        if (!bgfx::isValid(textureHandle))
        {
            throw std::runtime_error("Failed to create BGFX texture handle.");
        }
    }

    BgfxTextureBackend::~BgfxTextureBackend()
    {
        if (bgfx::isValid(textureHandle))
        {
            bgfx::destroy(textureHandle);
            textureHandle = BGFX_INVALID_HANDLE;
        }
    }

    BgfxSpriteBatchBackend::BgfxSpriteBatchBackend(BgfxGraphicsBackend& graphicsBackend)
        : graphicsBackend(graphicsBackend)
    {
    }

    void BgfxSpriteBatchBackend::Begin()
    {
        begun = true;
    }

    void BgfxSpriteBatchBackend::End()
    {
        begun = false;
    }

    void BgfxSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const auto& bgfxTex = static_cast<const BgfxTextureBackend&>(texture);
        Draw(
            texture,
            Rectangle(static_cast<int>(x), static_cast<int>(y), bgfxTex.width, bgfxTex.height),
            Rectangle(0, 0, bgfxTex.width, bgfxTex.height),
            Microsoft::Xna::Framework::White
        );
    }

    void BgfxSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle,
                                      const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None,
             0.0f);
    }

    void BgfxSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle,
                                      const Color& color,
                                      float rotation,
                                      const Vector2& origin,
                                      SpriteEffects effects,
                                      float layerDepth)
    {
        if (!begun)
        {
            throw std::runtime_error("BgfxSpriteBatchBackend::Draw called before Begin().");
        }

        const auto& bgfxTex = static_cast<const BgfxTextureBackend&>(texture);
        graphicsBackend.SubmitSprite(bgfxTex, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                                     layerDepth);
    }

    BgfxGraphicsBackend::BgfxGraphicsBackend(SDL_Window* window) : window(window)
    {
        if (!window)
        {
            throw std::runtime_error("BgfxGraphicsBackend initialized with null window.");
        }

        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &width, &height))
        {
            throw std::runtime_error(std::string("SDL_GetWindowSize failed: ") + SDL_GetError());
        }

        const uint16_t initialWidth = static_cast<uint16_t>(std::max(width, 1));
        const uint16_t initialHeight = static_cast<uint16_t>(std::max(height, 1));
        const char* rendererOverride = SDL_getenv(kRendererOverrideEnvVar);
        const bgfx::RendererType::Enum requestedRendererType = Detail::ResolveRendererType(rendererOverride);

        bgfx::Init init;
        init.type = requestedRendererType;
        init.vendorId = BGFX_PCI_ID_NONE;
        init.platformData = CreatePlatformData(window);
        init.resolution.width = initialWidth;
        init.resolution.height = initialHeight;
        init.resolution.reset = BGFX_RESET_VSYNC;

        if (!init.platformData.nwh)
        {
            const char* videoDriver = SDL_GetCurrentVideoDriver();
            throw std::runtime_error(
                std::string("Failed to initialize BGFX backend: native window handle is not available")
                + (videoDriver ? std::string(" for SDL video driver '") + videoDriver + "'." : ".")
            );
        }

        if (!bgfx::init(init))
        {
            throw std::runtime_error("bgfx::init failed.");
        }
        initialized = true;

        try
        {
            spriteProgram = CreateSpriteProgram();
            textureSampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

            if (!bgfx::isValid(textureSampler))
            {
                throw std::runtime_error("Failed to create BGFX sampler uniform (s_tex).");
            }

            cachedWidth = initialWidth;
            cachedHeight = initialHeight;
            EnsureViewState();

            const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
            std::cout << "BGFX backend requested renderer: " << RendererTypeName(requestedRendererType)
                << ", active renderer: " << bgfx::getRendererName(rendererType) << std::endl;
        }
        catch (...)
        {
            if (bgfx::isValid(textureSampler))
            {
                bgfx::destroy(textureSampler);
                textureSampler = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(spriteProgram))
            {
                bgfx::destroy(spriteProgram);
                spriteProgram = BGFX_INVALID_HANDLE;
            }
            if (initialized)
            {
                bgfx::shutdown();
                initialized = false;
            }
            throw;
        }
    }

    BgfxGraphicsBackend::~BgfxGraphicsBackend()
    {
        if (bgfx::isValid(textureSampler))
        {
            bgfx::destroy(textureSampler);
            textureSampler = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(spriteProgram))
        {
            bgfx::destroy(spriteProgram);
            spriteProgram = BGFX_INVALID_HANDLE;
        }

        if (initialized)
        {
            bgfx::shutdown();
            initialized = false;
        }
    }

    void BgfxGraphicsBackend::EnsureViewState()
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &width, &height))
        {
            throw std::runtime_error(std::string("SDL_GetWindowSize failed: ") + SDL_GetError());
        }

        const uint16_t newWidth = static_cast<uint16_t>(std::max(width, 1));
        const uint16_t newHeight = static_cast<uint16_t>(std::max(height, 1));

        if (newWidth != cachedWidth || newHeight != cachedHeight)
        {
            cachedWidth = newWidth;
            cachedHeight = newHeight;
            bgfx::reset(cachedWidth, cachedHeight, BGFX_RESET_VSYNC);
        }

        bgfx::setViewRect(spriteViewId, 0, 0, cachedWidth, cachedHeight);

        float ortho[16];
        bx::mtxOrtho(ortho, 0.0f, static_cast<float>(cachedWidth), static_cast<float>(cachedHeight), 0.0f, 0.0f,
                     1000.0f, 0.0f,
                     bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(spriteViewId, nullptr, ortho);
        bgfx::setViewClear(spriteViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearRgba, 1.0f, 0);
    }

    void BgfxGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        EnsureViewState();
        bgfx::touch(spriteViewId);
    }

    void BgfxGraphicsBackend::Present()
    {
        EnsureViewState();
        bgfx::frame();
    }

    void BgfxGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        if (!SDL_GetWindowSize(window, &width, &height))
        {
            throw std::runtime_error(std::string("SDL_GetWindowSize failed: ") + SDL_GetError());
        }
    }

    std::unique_ptr<ITextureBackend> BgfxGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<BgfxTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> BgfxGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<BgfxSpriteBatchBackend>(*this);
    }

    void BgfxGraphicsBackend::SubmitSprite(const BgfxTextureBackend& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           float layerDepth)
    {
        (void)layerDepth;

        if (!bgfx::isValid(texture.textureHandle))
        {
            return;
        }
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0)
        {
            return;
        }

        EnsureViewState();

        float u1 = static_cast<float>(sourceRectangle.X) / static_cast<float>(texture.width);
        float v1 = static_cast<float>(sourceRectangle.Y) / static_cast<float>(texture.height);
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / static_cast<float>(texture.width);
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / static_cast<float>(texture.height);

        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
        {
            std::swap(u1, u2);
        }
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
        {
            std::swap(v1, v2);
        }

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);

        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);

        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - origin.X) * scaleX;
        const float p0y = (0.0f - origin.Y) * scaleY;
        const float p1x = (sw - origin.X) * scaleX;
        const float p1y = (0.0f - origin.Y) * scaleY;
        const float p2x = (sw - origin.X) * scaleX;
        const float p2y = (sh - origin.Y) * scaleY;
        const float p3x = (0.0f - origin.X) * scaleX;
        const float p3y = (sh - origin.Y) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float x, float y, float& outX, float& outY)
        {
            outX = dx + x * cosR - y * sinR;
            outY = dy + x * sinR + y * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        const bgfx::VertexLayout& layout = GetSpriteVertexLayout();

        if (bgfx::getAvailTransientVertexBuffer(4, layout) < 4 || bgfx::getAvailTransientIndexBuffer(6) < 6)
        {
            return;
        }

        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::TransientIndexBuffer indexBuffer;
        bgfx::allocTransientVertexBuffer(&vertexBuffer, 4, layout);
        bgfx::allocTransientIndexBuffer(&indexBuffer, 6);

        const uint32_t abgr = ToAbgr(color);
        auto* vertices = reinterpret_cast<SpriteVertex*>(vertexBuffer.data);
        vertices[0] = {v0x, v0y, u1, v1, abgr};
        vertices[1] = {v1x, v1y, u2, v1, abgr};
        vertices[2] = {v2x, v2y, u2, v2, abgr};
        vertices[3] = {v3x, v3y, u1, v2, abgr};

        auto* indices = reinterpret_cast<uint16_t*>(indexBuffer.data);
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 2;
        indices[4] = 3;
        indices[5] = 0;

        bgfx::setTexture(0, textureSampler, texture.textureHandle, samplerFlags_[0]);
        bgfx::setVertexBuffer(0, &vertexBuffer);
        bgfx::setIndexBuffer(&indexBuffer);
        if (scissorW_ > 0 && scissorH_ > 0)
            bgfx::setScissor(scissorX_, scissorY_, scissorW_, scissorH_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                       | blendFlags_ | depthFlags_ | cullFlags_);
        bgfx::submit(spriteViewId, spriteProgram);
    }

    // ---- Graphics state ----

    static uint64_t XnaBlendToBgfxFactor(int blend)
    {
        // XNA Blend: One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4,
        //            InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
        //            DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        //            InverseBlendFactor=11, SourceAlphaSaturation=12
        switch (blend)
        {
        case 0:  return BGFX_STATE_BLEND_ONE;
        case 1:  return BGFX_STATE_BLEND_ZERO;
        case 2:  return BGFX_STATE_BLEND_SRC_COLOR;
        case 3:  return BGFX_STATE_BLEND_INV_SRC_COLOR;
        case 4:  return BGFX_STATE_BLEND_SRC_ALPHA;
        case 5:  return BGFX_STATE_BLEND_INV_SRC_ALPHA;
        case 6:  return BGFX_STATE_BLEND_DST_COLOR;
        case 7:  return BGFX_STATE_BLEND_INV_DST_COLOR;
        case 8:  return BGFX_STATE_BLEND_DST_ALPHA;
        case 9:  return BGFX_STATE_BLEND_INV_DST_ALPHA;
        case 10: return BGFX_STATE_BLEND_FACTOR;
        case 11: return BGFX_STATE_BLEND_INV_FACTOR;
        case 12: return BGFX_STATE_BLEND_SRC_ALPHA_SAT;
        default: return BGFX_STATE_BLEND_ONE;
        }
    }

    void BgfxGraphicsBackend::ApplyBlendState(int colorSrcBlend, int /*alphaSrcBlend*/,
                                               int colorDstBlend, int /*alphaDstBlend*/,
                                               int /*colorBlendFunc*/, int /*alphaBlendFunc*/)
    {
        if (colorSrcBlend == 0 && colorDstBlend == 1)
            blendFlags_ = 0;  // One, Zero → Opaque (no blend)
        else
            blendFlags_ = BGFX_STATE_BLEND_FUNC(
                XnaBlendToBgfxFactor(colorSrcBlend),
                XnaBlendToBgfxFactor(colorDstBlend));
    }

    void BgfxGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                      int depthFunc,
                                                      bool /*stencilEnable*/, int /*stencilFunc*/,
                                                      int /*stencilPass*/, int /*stencilFail*/,
                                                      int /*stencilDepthFail*/,
                                                      int /*stencilMask*/, int /*stencilWriteMask*/,
                                                      int /*referenceStencil*/,
                                                      bool /*twoSidedStencilMode*/,
                                                      int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                      int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        // bgfx stencil not mapped here; depth only
        depthFlags_ = depthWriteEnable ? BGFX_STATE_WRITE_Z : 0;
        if (depthEnable)
        {
            // XNA CompareFunction: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
            //                      GreaterEqual=5, Greater=6, NotEqual=7
            switch (depthFunc)
            {
            case 1:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_NEVER;    break;
            case 2:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_LESS;     break;
            case 3:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_LEQUAL;   break;
            case 4:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_EQUAL;    break;
            case 5:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_GEQUAL;   break;
            case 6:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_GREATER;  break;
            case 7:  depthFlags_ |= BGFX_STATE_DEPTH_TEST_NOTEQUAL; break;
            default: depthFlags_ |= BGFX_STATE_DEPTH_TEST_ALWAYS;   break;
            }
        }
    }

    void BgfxGraphicsBackend::ApplyRasterizerState(int cullMode, int /*fillMode*/,
                                                    bool scissorTestEnable)
    {
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        switch (cullMode)
        {
        case 1:  cullFlags_ = BGFX_STATE_CULL_CW;  break;
        case 2:  cullFlags_ = BGFX_STATE_CULL_CCW; break;
        default: cullFlags_ = 0;                    break;
        }
        // Scissor test state — disable by zeroing the rect
        if (!scissorTestEnable)
            scissorW_ = scissorH_ = 0;
    }

    void BgfxGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) { scissorW_ = scissorH_ = 0; return; }
        scissorX_ = static_cast<uint16_t>(x);
        scissorY_ = static_cast<uint16_t>(y);
        scissorW_ = static_cast<uint16_t>(w);
        scissorH_ = static_cast<uint16_t>(h);
    }

    void BgfxGraphicsBackend::ApplySamplerState(int slot, int filter,
                                                 int addressU, int addressV,
                                                 int /*maxAnisotropy*/)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        uint32_t flags = 0;
        // TextureFilter → bgfx sampler flags
        // Linear=0, Point=1, Anisotropic=2, *MipPoint=3, *MipLinear=4, ...
        switch (filter)
        {
        case 1:  // Point
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
            break;
        case 2:  // Anisotropic — bgfx handles via ANISOTROPIC flag
            flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
            break;
        default: // Linear and variants
            break; // BGFX_SAMPLER default is linear
        }
        // TextureAddressMode: Wrap=0, Clamp=1, Mirror=2
        if (addressU == 1)      flags |= BGFX_SAMPLER_U_CLAMP;
        else if (addressU == 2) flags |= BGFX_SAMPLER_U_MIRROR;
        if (addressV == 1)      flags |= BGFX_SAMPLER_V_CLAMP;
        else if (addressV == 2) flags |= BGFX_SAMPLER_V_MIRROR;
        samplerFlags_[slot] = flags;
    }

    void BgfxGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
        // Applied via bgfx::setBlendFactor (rgba packed as RGBA8) in next draw call
        const uint32_t packed = (static_cast<uint32_t>(r * 255) << 24) |
                                 (static_cast<uint32_t>(g * 255) << 16) |
                                 (static_cast<uint32_t>(b * 255) << 8)  |
                                  static_cast<uint32_t>(a * 255);
        bgfx::setBlendFactor(packed);
    }

    // ---- 3D: explicit STUB. Bgfx backend has no 3D path implemented yet. ----
    static void ThrowNo3D()
    {
        throw std::runtime_error(
            "Bgfx backend: 3D rendering is not supported by this backend yet. "
            "Use the EasyGL backend for 3D.");
    }

    void BgfxGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D(); }
    void BgfxGraphicsBackend::SetDepthTestEnabled(bool)  { ThrowNo3D(); }
    void BgfxGraphicsBackend::SetBlendEnabled(bool)      { ThrowNo3D(); }
    void BgfxGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D(); }

    std::unique_ptr<IVertexBufferBackend> BgfxGraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D();
        return nullptr;
    }

    std::unique_ptr<IIndexBufferBackend> BgfxGraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D();
        return nullptr;
    }

    void BgfxGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                    const Matrix&, const Matrix&, const Matrix&,
                                                    PrimitiveType, int) { ThrowNo3D(); }

    void BgfxGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                                           const IIndexBufferBackend&,
                                                           const Matrix&, const Matrix&, const Matrix&,
                                                           PrimitiveType, int) { ThrowNo3D(); }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_BGFX
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Bgfx::BgfxGraphicsBackend>(args.window);
    }
#endif
}
