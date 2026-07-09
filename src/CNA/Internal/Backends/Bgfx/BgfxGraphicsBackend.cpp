#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include "fs_ocornut_imgui.bin.h"
#include "vs_ocornut_imgui.bin.h"
#include "shaders/bgfx_shaders.hpp"

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

        // Normal matrix = transpose(inverse(world3x3)), via the cofactor/det shortcut, so
        // non-uniform-scale World transforms don't skew the transformed normal (Task 398 fix;
        // multiplying by World directly is only correct for rotation/uniform-scale/translation).
        void ComputeNormalMatrix3x3(const float* w, float out[9])
        {
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
            out[0] = (e * i - f * h) * invDet; out[1] = -(b * i - c * h) * invDet; out[2] = (b * f - c * e) * invDet;
            out[3] = -(d * i - f * g) * invDet; out[4] = (a * i - c * g) * invDet; out[5] = -(a * f - c * d) * invDet;
            out[6] = (d * h - e * g) * invDet; out[7] = -(a * h - b * g) * invDet; out[8] = (a * e - b * d) * invDet;
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

    // --- BgfxEffectBackend ---

    BgfxEffectBackend::~BgfxEffectBackend()
    {
        if (bgfx::isValid(program))
            bgfx::destroy(program);
    }

    void BgfxEffectBackend::Bind()
    {
        // No-op: bgfx programs are submitted per draw call, not bound globally.
    }

    std::unique_ptr<IEffectBackend> BgfxGraphicsBackend::CreateEffectBackend(
        const std::string& /*vertSrc*/, const std::string& /*fragSrc*/)
    {
        return std::make_unique<BgfxEffectBackend>();
    }

    void BgfxGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // Request a screenshot of the default backbuffer (BGFX_INVALID_HANDLE = swapchain).
        // The callback fires after bgfx::frame() flushes the current command buffer.
        readbackCallback_.screenshotReady = false;
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "cna_readback");

        // Advance up to 3 frames to give the render thread time to fire the callback.
        // In single-threaded bgfx mode (typical on Linux) the first frame() suffices.
        for (int attempt = 0; attempt < 3 && !readbackCallback_.screenshotReady; ++attempt)
            bgfx::frame();

        if (!readbackCallback_.screenshotReady)
            throw std::runtime_error(
                "BgfxGraphicsBackend::ReadBackbuffer: screenshot callback did not fire");

        const uint32_t pitch  = readbackCallback_.screenshotPitch;
        const bool     yflip  = readbackCallback_.screenshotYFlip;
        const uint32_t fbH    = readbackCallback_.screenshotHeight;
        const uint8_t* src    = readbackCallback_.screenshotBytes.data();
        // bgfx usually delivers BGRA8 on OpenGL; swap R↔B unless it's already RGBA8.
        const bool swapRB = (readbackCallback_.screenshotFormat == bgfx::TextureFormat::BGRA8);

        for (int row = 0; row < h; ++row)
        {
            const int srcRow = yflip ? (int(fbH) - 1 - (y + row)) : (y + row);
            const uint8_t* rowSrc = src + srcRow * pitch + x * 4;
            uint8_t* dst = pixels + row * w * 4;
            for (int col = 0; col < w; ++col)
            {
                if (swapRB)
                {
                    dst[col * 4 + 0] = rowSrc[col * 4 + 2]; // R ← B
                    dst[col * 4 + 1] = rowSrc[col * 4 + 1]; // G
                    dst[col * 4 + 2] = rowSrc[col * 4 + 0]; // B ← R
                    dst[col * 4 + 3] = rowSrc[col * 4 + 3]; // A
                }
                else
                {
                    dst[col * 4 + 0] = rowSrc[col * 4 + 0];
                    dst[col * 4 + 1] = rowSrc[col * 4 + 1];
                    dst[col * 4 + 2] = rowSrc[col * 4 + 2];
                    dst[col * 4 + 3] = rowSrc[col * 4 + 3];
                }
            }
        }
    }

    // --- BgfxOcclusionQueryBackend ---

    BgfxOcclusionQueryBackend::BgfxOcclusionQueryBackend(BgfxGraphicsBackend* owner)
        : owner_(owner)
    {
        handle = bgfx::createOcclusionQuery();
    }

    BgfxOcclusionQueryBackend::~BgfxOcclusionQueryBackend()
    {
        if (owner_ && bgfx::isValid(owner_->activeOcclusionQuery_)
            && owner_->activeOcclusionQuery_.idx == handle.idx)
            owner_->activeOcclusionQuery_ = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
    }

    void BgfxOcclusionQueryBackend::Begin()
    {
        if (owner_) owner_->activeOcclusionQuery_ = handle;
    }

    void BgfxOcclusionQueryBackend::End()
    {
        if (owner_) owner_->activeOcclusionQuery_ = BGFX_INVALID_HANDLE;
    }

    bool BgfxOcclusionQueryBackend::IsComplete() const
    {
        if (!bgfx::isValid(handle)) return false;
        return bgfx::getResult(handle) != bgfx::OcclusionQueryResult::NoResult;
    }

    int BgfxOcclusionQueryBackend::PixelCount() const
    {
        if (!bgfx::isValid(handle)) return 0;
        int32_t result = 0;
        auto r = bgfx::getResult(handle, &result);
        if (r == bgfx::OcclusionQueryResult::Visible) return result;
        return 0;
    }

    std::unique_ptr<IOcclusionQueryBackend> BgfxGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<BgfxOcclusionQueryBackend>(this);
    }

    void BgfxGraphicsBackend::SubmitViewProgram(bgfx::ProgramHandle program)
    {
        if (bgfx::isValid(activeOcclusionQuery_))
            bgfx::submit(currentViewId_, program, activeOcclusionQuery_);
        else
            bgfx::submit(currentViewId_, program);
    }

    // Task 914: advances bgfx frames until the given target frame number (as returned by
    // bgfx::readTexture()) has been reached, mirroring ReadBackbuffer's own established
    // retry-until-ready convention for bgfx's inherently async/deferred completion model.
    // Bounded by a generous attempt cap as a safety net (never observed to need more than 1-2
    // in this project's single-threaded-bgfx sandbox, matching ReadBackbuffer's own comment).
    static bool AdvanceFramesUntil(uint32_t targetFrame)
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t current = bgfx::frame();
            if (current >= targetFrame) return true;
        }
        return false;
    }

    // --- BgfxTextureCubeBackend ---

    BgfxTextureCubeBackend::BgfxTextureCubeBackend(int size, bool mipMap, int /*surfaceFormat*/)
        : size_(size)
    {
        // Task 914: mipMap now genuinely threaded through (was hardcoded false) -- verifiable now
        // that GetData() (below) provides a real readback path to check mip-level content.
        handle = bgfx::createTextureCube(
            static_cast<uint16_t>(size),
            mipMap,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE);
    }

    BgfxTextureCubeBackend::~BgfxTextureCubeBackend()
    {
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
    }

    void BgfxTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                         const void* data, int dataLength)
    {
        if (!bgfx::isValid(handle) || !data || dataLength <= 0) return;
        const bgfx::Memory* mem = bgfx::copy(data, static_cast<uint32_t>(dataLength));
        bgfx::updateTextureCube(handle,
            0,
            static_cast<uint8_t>(face),
            static_cast<uint8_t>(level),
            static_cast<uint16_t>(x), static_cast<uint16_t>(y),
            static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            mem);
    }

    // Task 914: real GPU readback, previously a total no-op (the shared ITextureCubeBackend
    // default silently left the caller's buffer untouched). bgfx::readTexture() requires the
    // SOURCE texture to be created with BGFX_TEXTURE_READ_BACK -- incompatible with this cube's
    // own shader-sampled `handle` ("can't be a GPU resource at the same time" per bgfx's own doc
    // comment) -- so a temporary plain 2D texture, sized exactly to the requested (w,h) region and
    // created with BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK, receives the requested face/mip
    // region via bgfx::blit() (dst is 2D so dstZ=0; src is a cube face so srcZ=face, per bgfx's own
    // blit() doc comment), then bgfx::readTexture() reads that temporary texture directly into the
    // caller's buffer. Confirmed BGFX_CAPS_TEXTURE_BLIT/READ_BACK are both supported in this
    // project's Xvfb/llvmpipe/OpenGL sandbox via a throwaway caps-log check before implementing.
    void BgfxTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                         void* data, int dataLength) const
    {
        if (!bgfx::isValid(handle) || !data || dataLength <= 0 || w <= 0 || h <= 0) return;

        const bgfx::TextureHandle readback = bgfx::createTexture2D(
            static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(readback))
        {
            // Task 455: BGFX_CAPS_TEXTURE_BLIT/READ_BACK aren't guaranteed on every renderer this
            // backend can select (confirmed present in this project's own Xvfb/llvmpipe/OpenGL
            // sandbox, but not runtime-checked) -- log clearly instead of silently leaving the
            // caller's buffer untouched, matching this file's own established diagnostic
            // convention for other renderer-capability fallbacks.
            std::cerr << "CNA: bgfx TextureCube::GetData readback texture creation failed -- "
                          "BGFX_CAPS_TEXTURE_BLIT/READ_BACK may not be supported on "
                       << bgfx::getRendererName(bgfx::getRendererType()) << "\n";
            return;
        }

        bgfx::blit(0, readback, 0, 0, 0, 0,
                   handle, static_cast<uint8_t>(level),
                   static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(face),
                   static_cast<uint16_t>(w), static_cast<uint16_t>(h), 1);

        const uint32_t targetFrame = bgfx::readTexture(readback, data);
        AdvanceFramesUntil(targetFrame);

        bgfx::destroy(readback);
    }

    std::unique_ptr<ITextureCubeBackend> BgfxGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<BgfxTextureCubeBackend>(size, mipMap, surfaceFormat);
    }

    // --- BgfxTexture3DBackend ---

    BgfxTexture3DBackend::BgfxTexture3DBackend(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        // Task 914: mipMap now genuinely threaded through (was hardcoded false) -- verifiable now
        // that GetData() (below) provides a real readback path to check mip-level content.
        handle = bgfx::createTexture3D(
            static_cast<uint16_t>(w),
            static_cast<uint16_t>(h),
            static_cast<uint16_t>(depth),
            mipMap,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE);
    }

    BgfxTexture3DBackend::~BgfxTexture3DBackend()
    {
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
    }

    void BgfxTexture3DBackend::SetData(int level, int x, int y, int z,
                                       int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        if (!bgfx::isValid(handle) || !data || dataLength <= 0) return;
        const bgfx::Memory* mem = bgfx::copy(data, static_cast<uint32_t>(dataLength));
        bgfx::updateTexture3D(handle,
            static_cast<uint8_t>(level),
            static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(z),
            static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(depth),
            mem);
    }

    // Task 914: real GPU readback, previously a total no-op -- mirrors
    // BgfxTextureCubeBackend::GetData's approach exactly, except the temporary readback texture is
    // itself a 3D texture (sized to the requested w/h/depth region) rather than a plain 2D one,
    // since a 3D source's _depth argument applies to blit regardless of the destination's own
    // dimensionality and this keeps both sides symmetric.
    void BgfxTexture3DBackend::GetData(int level, int x, int y, int z,
                                       int w, int h, int depth,
                                       void* data, int dataLength) const
    {
        if (!bgfx::isValid(handle) || !data || dataLength <= 0 || w <= 0 || h <= 0 || depth <= 0) return;

        const bgfx::TextureHandle readback = bgfx::createTexture3D(
            static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(depth),
            false, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(readback))
        {
            // Task 455: same finding as BgfxTextureCubeBackend::GetData above -- log clearly
            // instead of silently leaving the caller's buffer untouched.
            std::cerr << "CNA: bgfx Texture3D::GetData readback texture creation failed -- "
                          "BGFX_CAPS_TEXTURE_BLIT/READ_BACK may not be supported on "
                       << bgfx::getRendererName(bgfx::getRendererType()) << "\n";
            return;
        }

        bgfx::blit(0, readback, 0, 0, 0, 0,
                   handle, static_cast<uint8_t>(level),
                   static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(z),
                   static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(depth));

        const uint32_t targetFrame = bgfx::readTexture(readback, data);
        AdvanceFramesUntil(targetFrame);

        bgfx::destroy(readback);
    }

    std::unique_ptr<ITexture3DBackend> BgfxGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<BgfxTexture3DBackend>(w, h, depth, mipMap, surfaceFormat);
    }

    // --- BgfxRenderTargetBackend ---

    // Task 878/879: BGFX_TEXTURE_RT_MSAA_X2/X4/X8/X16 occupy the same 4-bit field as
    // BGFX_TEXTURE_RT (see bgfx/defines.h's BGFX_TEXTURE_RT_MASK/RT_SHIFT) -- they are mutually
    // exclusive alternatives, not flags to OR alongside it. requestedMultiSampleCount arrives
    // already rounded to a power of two (or 0) by RenderTarget2D's/RenderTargetCube's own
    // ClosestMSAAPower step before it ever reaches the backend, so this only needs to pick the
    // matching bgfx constant and clamp down to bgfx's max of 16x.
    static uint64_t BgfxMsaaRtFlag(int requestedMultiSampleCount, int& appliedOut)
    {
        if (requestedMultiSampleCount >= 16) { appliedOut = 16; return BGFX_TEXTURE_RT_MSAA_X16; }
        if (requestedMultiSampleCount >= 8)  { appliedOut = 8;  return BGFX_TEXTURE_RT_MSAA_X8;  }
        if (requestedMultiSampleCount >= 4)  { appliedOut = 4;  return BGFX_TEXTURE_RT_MSAA_X4;  }
        if (requestedMultiSampleCount >= 2)  { appliedOut = 2;  return BGFX_TEXTURE_RT_MSAA_X2;  }
        appliedOut = 0;
        return BGFX_TEXTURE_RT;
    }

    // Task 877: maps a Microsoft::Xna::Framework::Graphics::DepthFormat ordinal to the
    // bgfx::TextureFormat a render target's depth/stencil attachment should use. Returns false
    // for DepthFormat::None, meaning no depth/stencil attachment should be created at all.
    static bool MapDepthFormat(int depthFormat, ::bgfx::TextureFormat::Enum& outFormat)
    {
        switch (static_cast<DepthFormat>(depthFormat))
        {
        case DepthFormat::Depth16:
            outFormat = ::bgfx::TextureFormat::D16;
            return true;
        case DepthFormat::Depth24:
            outFormat = ::bgfx::TextureFormat::D24;
            return true;
        case DepthFormat::Depth24Stencil8:
            outFormat = ::bgfx::TextureFormat::D24S8;
            return true;
        case DepthFormat::None:
        default:
            return false;
        }
    }

    namespace Detail
    {
        // Task 910: view id 0 is permanently reserved for the backbuffer; every render target
        // (2D, cube, or MRT) gets a distinct id from this pool instead of the old hardcoded 1.
        // Free-list-backed so long-running games creating/destroying render targets don't
        // exhaust bgfx's view-id space (BGFX_CONFIG_MAX_VIEWS, 256 by default).
        static std::vector<bgfx::ViewId>& RtViewIdFreeList()
        {
            static std::vector<bgfx::ViewId> pool;
            return pool;
        }

        bgfx::ViewId AllocateRtViewId()
        {
            // Mirrors bgfx's own internal BGFX_CONFIG_MAX_VIEWS default (src/config.h, not part
            // of the public bgfx.h API so not #include-able here) -- view id 0 is reserved for
            // the backbuffer, leaving ids [1, kMaxBgfxViews) for render targets/MRT.
            static constexpr bgfx::ViewId kMaxBgfxViews = 256;
            auto& pool = RtViewIdFreeList();
            if (!pool.empty())
            {
                const bgfx::ViewId id = pool.back();
                pool.pop_back();
                return id;
            }
            static bgfx::ViewId nextId = 1;
            if (nextId >= kMaxBgfxViews)
                throw std::runtime_error("Bgfx: exhausted view ids (more concurrently-live "
                                          "render targets than bgfx supports views)");
            return nextId++;
        }

        void ReleaseRtViewId(bgfx::ViewId id)
        {
            RtViewIdFreeList().push_back(id);
        }
    }

    BgfxRenderTargetBackend::BgfxRenderTargetBackend(int w, int h, int depthFormat, bool preserve,
                                                      int requestedMultiSampleCount, bool mipMap)
        : width(w), height(h), preserveContents(preserve), viewId_(Detail::AllocateRtViewId())
    {
        int appliedMsaa = 0;
        const uint64_t msaaFlag = BgfxMsaaRtFlag(requestedMultiSampleCount, appliedMsaa);
        multiSampleCount = appliedMsaa;

        // Create color texture with render target flag (Task 878/879: an MSAA variant when
        // requested -- bgfx resolves the multisampled content into this same texture handle
        // internally; no explicit vkCmdResolveImage-style step or separate resolve texture is
        // needed on this backend, unlike EasyGL/Vulkan). Task 906: hasMips=true (mipMap) makes
        // bgfx allocate the full mip chain on this same handle; the framebuffer this texture is
        // attached to below then automatically regenerates it -- see createFrameBuffer(numAttachments,
        // TextureHandle*, ...)'s own real-mips-aware BGFX_RESOLVE_AUTO_GEN_MIPS default (confirmed
        // in bgfx.cpp), triggered internally whenever bgfx switches away from this framebuffer
        // (TextureGL::resolve() -> glGenerateMipmap, and the equivalent per-backend resolve path
        // on every other bgfx renderer). No custom shader/geometry needed, unlike Vulkan's
        // Task 878 vkCmdBlitImage cascade -- bgfx already has a real glGenerateMipmap-equivalent,
        // just gated behind the framebuffer-attachment resolve flag rather than surfaced on
        // bgfx::blit() (which really is a same-size copy only, as originally found).
        colorTex = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            mipMap, 1, bgfx::TextureFormat::RGBA8,
            msaaFlag | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        bgfx::TextureHandle attachments[2] = { colorTex, BGFX_INVALID_HANDLE };
        int numAttachments = 1;

        ::bgfx::TextureFormat::Enum depthBgfxFormat;
        if (MapDepthFormat(depthFormat, depthBgfxFormat))
        {
            // Depth attachment must share the color attachment's sample count.
            attachments[1] = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                false, 1, depthBgfxFormat, msaaFlag);
            numAttachments = 2;
        }

        fbo = bgfx::createFrameBuffer(numAttachments, attachments, true);
    }

    BgfxRenderTargetBackend::~BgfxRenderTargetBackend()
    {
        if (bgfx::isValid(fbo))
            bgfx::destroy(fbo);
        Detail::ReleaseRtViewId(viewId_);
    }

    void BgfxRenderTargetBackend::BindAsRenderTarget()
    {
        bgfx::setViewFrameBuffer(viewId_, fbo);
        bgfx::setViewRect(viewId_, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        // For PreserveContents, suppress the per-view clear that bgfx would otherwise
        // carry over from the previous frame (set by a DiscardContents RT or explicit Clear).
        if (preserveContents)
            bgfx::setViewClear(viewId_, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    }

    void BgfxRenderTargetBackend::UnbindAsRenderTarget()
    {
        bgfx::setViewFrameBuffer(viewId_, BGFX_INVALID_HANDLE);
    }

    // ---

    std::unique_ptr<IRenderTargetBackend> BgfxGraphicsBackend::CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // multiSampleCount: BGFX_TEXTURE_RT_MSAA_Xn (Task 878/879) — see BgfxRenderTargetBackend.
        // mipMap (Task 906): real mip chain, auto-regenerated by bgfx itself on every resolve.
        return std::make_unique<BgfxRenderTargetBackend>(w, h, depthFormat, preserveContents,
                                                          multiSampleCount, mipMap);
    }

    void BgfxGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (bgfx::isValid(mrtFbo_)) { bgfx::destroy(mrtFbo_); mrtFbo_ = BGFX_INVALID_HANDLE; }
        if (mrtViewId_ != Detail::kInvalidRtViewId) { Detail::ReleaseRtViewId(mrtViewId_); mrtViewId_ = Detail::kInvalidRtViewId; }
        if (rt)
        {
            // Task 910: use this RT's own stable view id, not a hardcoded shared one -- lets
            // more than one render target be bound within a single un-advanced bgfx frame
            // without one clobbering another's already-recorded draws.
            auto* bgfxRt = static_cast<BgfxRenderTargetBackend*>(rt);
            bgfxRt->BindAsRenderTarget();
            currentViewId_ = bgfxRt->viewId_;
            spriteViewId = bgfxRt->viewId_;
            currentRtWidth_  = static_cast<uint16_t>(rt->GetWidth());
            currentRtHeight_ = static_cast<uint16_t>(rt->GetHeight());
        }
        else
        {
            bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
            currentViewId_ = 0;
            spriteViewId = 0;
            currentRtWidth_ = currentRtHeight_ = 0;
        }
    }

    void BgfxGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (bgfx::isValid(mrtFbo_)) { bgfx::destroy(mrtFbo_); mrtFbo_ = BGFX_INVALID_HANDLE; }
        if (mrtViewId_ != Detail::kInvalidRtViewId) { Detail::ReleaseRtViewId(mrtViewId_); mrtViewId_ = Detail::kInvalidRtViewId; }
        if (count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count == 1)
        {
            SetRenderTarget2D(rts[0]);
            return;
        }
        // Multi-target: build a temporary framebuffer from the color textures. Task 910: MRT
        // gets its own freshly-allocated view id too (not the old hardcoded 1), so a 2nd,
        // different MRT setup bound later within the same un-advanced frame doesn't clobber
        // this one's already-recorded draws.
        static constexpr int kMaxAttachments = 8; // bgfx BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS default
        bgfx::Attachment attachments[kMaxAttachments];
        int n = count < kMaxAttachments ? count : kMaxAttachments;
        for (int i = 0; i < n; ++i)
        {
            auto* bgfxRt = static_cast<BgfxRenderTargetBackend*>(rts[i]);
            attachments[i].init(bgfxRt->colorTex);
        }
        mrtFbo_ = bgfx::createFrameBuffer(static_cast<uint8_t>(n), attachments);
        mrtViewId_ = Detail::AllocateRtViewId();
        bgfx::setViewFrameBuffer(mrtViewId_, mrtFbo_);
        currentViewId_ = mrtViewId_;
        spriteViewId = mrtViewId_;
        currentRtWidth_  = static_cast<uint16_t>(rts[0]->GetWidth());
        currentRtHeight_ = static_cast<uint16_t>(rts[0]->GetHeight());
    }

    // --- BgfxRenderTargetCubeBackend ---

    BgfxRenderTargetCubeBackend::BgfxRenderTargetCubeBackend(int size, int depthFormat, bool mipMap,
                                                              int requestedMultiSampleCount)
        : size_(size), viewId_(Detail::AllocateRtViewId())
    {
        // Task 903: BGFX_TEXTURE_RT_MSAA_Xn instead of plain BGFX_TEXTURE_RT when requested --
        // mirrors BgfxRenderTargetBackend's identical Task 878/879 treatment exactly; bgfx
        // resolves the multisampled content into this same cube texture handle internally, no
        // separate resolve texture or render-pass-shape distinction needed (unlike Vulkan).
        int appliedMsaa = 0;
        const uint64_t msaaFlag = BgfxMsaaRtFlag(requestedMultiSampleCount, appliedMsaa);
        multiSampleCount = appliedMsaa;

        // Cube map texture with render target flag — bgfx manages all 6 faces. Task 907:
        // hasMips=mipMap; BindAsRenderTargetFace's bgfx::Attachment::init() below already passes
        // BGFX_RESOLVE_AUTO_GEN_MIPS (its own default, unconditionally, unlike the simple
        // TextureHandle-array createFrameBuffer overload), so this is the only change needed --
        // same mechanism as Task 906's RenderTarget2D fix.
        cubeTex = bgfx::createTextureCube(static_cast<uint16_t>(size), mipMap, 1,
                                           bgfx::TextureFormat::RGBA8,
                                           msaaFlag | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        // Task 877: single 2D depth/stencil texture shared across all 6 faces (mirrors
        // VulkanRenderTargetCubeBackend's shared depthImage_) -- omitted entirely for
        // DepthFormat::None (previously RenderTargetCube had no depth attachment support at all
        // on this backend).
        ::bgfx::TextureFormat::Enum depthBgfxFormat;
        if (MapDepthFormat(depthFormat, depthBgfxFormat))
        {
            // Depth attachment must share the color attachment's sample count (Task 903).
            depthTex = bgfx::createTexture2D(static_cast<uint16_t>(size), static_cast<uint16_t>(size),
                false, 1, depthBgfxFormat, msaaFlag);
        }
        // The FBO is created per-face bind to attach the right face layer
        fbo = BGFX_INVALID_HANDLE;
    }

    BgfxRenderTargetCubeBackend::~BgfxRenderTargetCubeBackend()
    {
        if (bgfx::isValid(fbo))      bgfx::destroy(fbo);
        if (bgfx::isValid(cubeTex))  bgfx::destroy(cubeTex);
        if (bgfx::isValid(depthTex)) bgfx::destroy(depthTex);
        Detail::ReleaseRtViewId(viewId_);
    }

    void BgfxRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (bgfx::isValid(fbo)) bgfx::destroy(fbo);
        bgfx::Attachment atts[2];
        atts[0].init(cubeTex, bgfx::Access::Write, static_cast<uint16_t>(face));
        int numAttachments = 1;
        if (bgfx::isValid(depthTex))
        {
            atts[1].init(depthTex, bgfx::Access::Write);
            numAttachments = 2;
        }
        fbo = bgfx::createFrameBuffer(numAttachments, atts);
        bgfx::setViewFrameBuffer(viewId_, fbo);
        bgfx::setViewRect(viewId_, 0, 0, static_cast<uint16_t>(size_), static_cast<uint16_t>(size_));
    }

    void BgfxRenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        bgfx::setViewFrameBuffer(viewId_, BGFX_INVALID_HANDLE);
    }

    std::unique_ptr<IRenderTargetCubeBackend> BgfxGraphicsBackend::CreateRenderTargetCube(int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        // mipMap (Task 907): real per-face mip chain, auto-regenerated by bgfx itself, same
        // mechanism as Task 906's RenderTarget2D fix.
        // multiSampleCount (Task 903): now wired up via BGFX_TEXTURE_RT_MSAA_Xn, mirroring
        // BgfxRenderTargetBackend's Task 878/879 treatment -- see BgfxRenderTargetCubeBackend's
        // constructor.
        return std::make_unique<BgfxRenderTargetCubeBackend>(size, depthFormat, mipMap, multiSampleCount);
    }

    // Task 907 finding: the shared IGraphicsBackend default only calls BindAsRenderTargetFace,
    // never updating currentRtWidth_/currentRtHeight_ -- without this override, EnsureViewState()
    // falls back to the full window size for any SpriteBatch draw into a cube face (the identical
    // bug shape Task 901 already fixed for 2D RenderTarget2D), corrupting every cube-face render.
    void BgfxGraphicsBackend::SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face)
    {
        if (!rt) return;
        // Task 910: use this cube RT's own stable view id (shared by all 6 faces), not a
        // hardcoded shared one -- lets more than one render target (or cube face across more
        // than one cube) be bound within a single un-advanced bgfx frame safely.
        auto* bgfxRt = static_cast<BgfxRenderTargetCubeBackend*>(rt);
        bgfxRt->BindAsRenderTargetFace(face);
        currentViewId_   = bgfxRt->viewId_;
        spriteViewId     = bgfxRt->viewId_;
        currentRtWidth_  = static_cast<uint16_t>(rt->GetSize());
        currentRtHeight_ = static_cast<uint16_t>(rt->GetSize());
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
        // Task 878/879 (closes Task 873): use ITextureBackend's own virtual GetWidth()/
        // GetHeight() instead of an unsafe static_cast<const BgfxTextureBackend&> -- texture may
        // be a BgfxRenderTargetBackend (an unrelated sibling class), not just BgfxTextureBackend.
        Draw(
            texture,
            Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
            Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()),
            Color::White
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

        // Task 878/879 (closes Task 873): texture may be a BgfxTextureBackend OR a
        // BgfxRenderTargetBackend (RenderTarget2D sampled after unbinding) -- both implement
        // IBgfxSamplable, so query the real handle through that instead of an unsafe
        // static_cast<const BgfxTextureBackend&> that silently read the wrong pooled handle
        // type whenever texture was actually a render target.
        const auto* samplable = dynamic_cast<const IBgfxSamplable*>(&texture);
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        if (samplable) handle = samplable->GetBgfxTextureHandle();
        // Task 750: apply this Begin()'s SamplerState to slot 0 right before each submit,
        // mirroring EasyGL's/Vulkan's identical FlushBatch()-time ApplySamplerState() call.
        graphicsBackend.ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        graphicsBackend.SubmitSprite(handle, texture.GetWidth(), texture.GetHeight(),
                                     destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                                     layerDepth);
    }

    void BgfxSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void BgfxSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void BgfxCnaCallback::fatal(const char* /*_file*/, uint16_t /*_line*/,
                                bgfx::Fatal::Enum /*_code*/, const char* _str)
    {
        throw std::runtime_error(std::string("bgfx fatal: ") + (_str ? _str : "unknown"));
    }

    void BgfxCnaCallback::screenShot(const char* /*_filePath*/,
                                      uint32_t _w, uint32_t _h, uint32_t _pitch,
                                      bgfx::TextureFormat::Enum _format,
                                      const void* _data, uint32_t _size, bool _yflip)
    {
        screenshotWidth  = _w;
        screenshotHeight = _h;
        screenshotPitch  = _pitch;
        screenshotFormat = _format;
        screenshotYFlip  = _yflip;
        screenshotBytes.assign(static_cast<const uint8_t*>(_data),
                               static_cast<const uint8_t*>(_data) + _size);
        screenshotReady = true;
    }

    BgfxGraphicsBackend::BgfxGraphicsBackend(SDL_Window* window, int swapInterval)
        : window(window)
        , resetFlags_(swapInterval > 0 ? BGFX_RESET_VSYNC : BGFX_RESET_NONE)
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
        init.resolution.reset = resetFlags_;
        init.callback = &readbackCallback_;

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

            // Create 3D programs from embedded shaders.
            {
                const bgfx::RendererType::Enum rt = bgfx::getRendererType();

                auto tryCreateProgram = [&](const bgfx::EmbeddedShader* shaders,
                                            const char* vsName, const char* fsName,
                                            const char* label) -> bgfx::ProgramHandle
                {
                    bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(shaders, rt, vsName);
                    bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(shaders, rt, fsName);
                    if (bgfx::isValid(vs) && bgfx::isValid(fs))
                        return bgfx::createProgram(vs, fs, true);
                    if (bgfx::isValid(vs)) bgfx::destroy(vs);
                    if (bgfx::isValid(fs)) bgfx::destroy(fs);
                    std::cerr << "CNA: bgfx " << label << " shaders not supported on "
                              << bgfx::getRendererName(rt) << "\n";
                    return BGFX_INVALID_HANDLE;
                };

                colored3DProgram_         = tryCreateProgram(kColored3dShaders,
                                                             "vs_colored3d", "fs_colored3d",
                                                             "colored3d");
                textured3DProgram_        = tryCreateProgram(kTextured3dShaders,
                                                             "vs_textured3d", "fs_textured3d",
                                                             "textured3d");
                coloredTextured3DProgram_ = tryCreateProgram(kColoredTextured3dShaders,
                                                             "vs_colored_textured3d",
                                                             "fs_colored_textured3d",
                                                             "colored_textured3d");
                litTextured3DProgram_     = tryCreateProgram(kLitTextured3dShaders,
                                                             "vs_lit_textured3d",
                                                             "fs_lit_textured3d",
                                                             "lit_textured3d");
                alphaTest3DProgram_       = tryCreateProgram(kAlphaTest3dShaders,
                                                             "vs_alpha_test3d",
                                                             "fs_alpha_test3d",
                                                             "alpha_test3d");
                alphaTestColoredTextured3DProgram_ = tryCreateProgram(kAlphaTest3dShaders,
                                                             "vs_alpha_test_colored3d",
                                                             "fs_alpha_test3d",
                                                             "alpha_test_colored3d");
                dualTexture3DProgram_     = tryCreateProgram(kDualTexture3dShaders,
                                                             "vs_dual_texture3d",
                                                             "fs_dual_texture3d",
                                                             "dual_texture3d");
                skinned3DProgram_         = tryCreateProgram(kSkinned3dShaders,
                                                             "vs_skinned3d",
                                                             "fs_skinned3d",
                                                             "skinned3d");
                instanced3DProgram_       = tryCreateProgram(kInstanced3dShaders,
                                                             "vs_instanced3d",
                                                             "fs_instanced3d",
                                                             "instanced3d");
                envMap3DProgram_          = tryCreateProgram(kEnvMap3dShaders,
                                                             "vs_env_map3d",
                                                             "fs_env_map3d",
                                                             "env_map3d");

                wvpUniform_         = bgfx::createUniform("u_wvp",            bgfx::UniformType::Mat4);
                diffuseColor3DUnif_ = bgfx::createUniform("u_diffuseColor",   bgfx::UniformType::Vec4);
                ambientColor3DUnif_ = bgfx::createUniform("u_ambientColor",   bgfx::UniformType::Vec4);
                light0Dir3DUnif_    = bgfx::createUniform("u_light0Dir",      bgfx::UniformType::Vec4);
                light0Diff3DUnif_   = bgfx::createUniform("u_light0Diffuse",  bgfx::UniformType::Vec4);
                lightingEn3DUnif_   = bgfx::createUniform("u_lightingEnabled",bgfx::UniformType::Vec4);
                light1Dir3DUnif_    = bgfx::createUniform("u_light1Dir",      bgfx::UniformType::Vec4);
                light1Diff3DUnif_   = bgfx::createUniform("u_light1Diffuse",  bgfx::UniformType::Vec4);
                light2Dir3DUnif_    = bgfx::createUniform("u_light2Dir",      bgfx::UniformType::Vec4);
                light2Diff3DUnif_   = bgfx::createUniform("u_light2Diffuse",  bgfx::UniformType::Vec4);
                light0Spec3DUnif_   = bgfx::createUniform("u_light0Specular", bgfx::UniformType::Vec4);
                light1Spec3DUnif_   = bgfx::createUniform("u_light1Specular", bgfx::UniformType::Vec4);
                light2Spec3DUnif_   = bgfx::createUniform("u_light2Specular", bgfx::UniformType::Vec4);
                specularColorPower3DUnif_ = bgfx::createUniform("u_specularColorPower", bgfx::UniformType::Vec4);
                vertexColorEn3DUnif_= bgfx::createUniform("u_vertexColorEnabled3D", bgfx::UniformType::Vec4);
                fogColorUnif_       = bgfx::createUniform("u_fogColor",  bgfx::UniformType::Vec4);
                fogParamsUnif_      = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
                texColor3DSampler_  = bgfx::createUniform("s_texColor",       bgfx::UniformType::Sampler);
                alphaTestUnif_      = bgfx::createUniform("u_alphaTest",      bgfx::UniformType::Vec4);
                texColor3DSampler2_ = bgfx::createUniform("s_texColor2",      bgfx::UniformType::Sampler);
                bonesUnif_          = bgfx::createUniform("u_bones",          bgfx::UniformType::Mat4, 72);
                vpInstanced3DUnif_  = bgfx::createUniform("u_vp",            bgfx::UniformType::Mat4);

                world3DUnif_         = bgfx::createUniform("u_world",          bgfx::UniformType::Mat4);
                normalMatrix3DUnif_  = bgfx::createUniform("u_normalMatrix",   bgfx::UniformType::Mat3);
                eyePos3DUnif_        = bgfx::createUniform("u_eyePos",         bgfx::UniformType::Vec4);
                emissiveColor3DUnif_ = bgfx::createUniform("u_emissiveColor",  bgfx::UniformType::Vec4);
                envMapAmountUnif_    = bgfx::createUniform("u_envMapAmount",   bgfx::UniformType::Vec4);
                envMapSpecularUnif_  = bgfx::createUniform("u_envMapSpecular", bgfx::UniformType::Vec4);
                envMapSampler_       = bgfx::createUniform("s_envMap",         bgfx::UniformType::Sampler);

                // 1x1 opaque white fallback texture (Task 379) — sampled whenever a draw's
                // texture0 is null, matching EasyGL/Vulkan's identical fallback.
                const uint8_t whitePixel[4] = {255, 255, 255, 255};
                const bgfx::Memory* whiteMem = bgfx::copy(whitePixel, sizeof(whitePixel));
                defaultWhiteTexture3D_ = bgfx::createTexture2D(
                    1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                    BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, whiteMem);
            }

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

            // Task 456: one-time startup capability dump.
            {
                const bgfx::Caps* caps = bgfx::getCaps();
                std::cout << "CNA: Bgfx capabilities -- MRT up to "
                          << std::min<int>(static_cast<int>(caps->limits.maxFBAttachments), 4)
                          << " targets (FNA MAX_RENDERTARGET_BINDINGS, device supports up to "
                          << static_cast<int>(caps->limits.maxFBAttachments) << "); "
                             "occlusion query: " << ((caps->supported & BGFX_CAPS_OCCLUSION_QUERY) ? "supported" : "NOT supported")
                          << "; texture blit/readback: "
                          << ((caps->supported & (BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK))
                                  == (BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK) ? "supported" : "NOT supported")
                          << "; anisotropic filtering: applied via BGFX_SAMPLER_ANISOTROPIC flags "
                             "(device-dependent effect, no separate capability flag); "
                             "SurfaceFormat: Color only (Task 176)" << std::endl;
            }
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
        auto destroyU = [](bgfx::UniformHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
        auto destroyP = [](bgfx::ProgramHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };

        destroyU(wvpUniform_);
        destroyU(diffuseColor3DUnif_);
        destroyU(ambientColor3DUnif_);
        destroyU(light0Dir3DUnif_);
        destroyU(light0Diff3DUnif_);
        destroyU(light1Dir3DUnif_);
        destroyU(light1Diff3DUnif_);
        destroyU(light2Dir3DUnif_);
        destroyU(light2Diff3DUnif_);
        destroyU(light0Spec3DUnif_);
        destroyU(light1Spec3DUnif_);
        destroyU(light2Spec3DUnif_);
        destroyU(specularColorPower3DUnif_);
        destroyU(vertexColorEn3DUnif_);
        destroyU(fogColorUnif_);
        destroyU(fogParamsUnif_);
        destroyU(lightingEn3DUnif_);
        destroyU(texColor3DSampler_);
        destroyU(alphaTestUnif_);
        destroyU(texColor3DSampler2_);
        destroyU(bonesUnif_);
        destroyU(vpInstanced3DUnif_);
        destroyU(world3DUnif_);
        destroyU(normalMatrix3DUnif_);
        destroyU(eyePos3DUnif_);
        destroyU(emissiveColor3DUnif_);
        destroyU(envMapAmountUnif_);
        destroyU(envMapSpecularUnif_);
        destroyU(envMapSampler_);
        if (bgfx::isValid(defaultWhiteTexture3D_)) { bgfx::destroy(defaultWhiteTexture3D_); defaultWhiteTexture3D_ = BGFX_INVALID_HANDLE; }
        destroyP(colored3DProgram_);
        destroyP(textured3DProgram_);
        destroyP(coloredTextured3DProgram_);
        destroyP(litTextured3DProgram_);
        destroyP(alphaTest3DProgram_);
        destroyP(alphaTestColoredTextured3DProgram_);
        destroyP(dualTexture3DProgram_);
        destroyP(skinned3DProgram_);
        destroyP(instanced3DProgram_);
        destroyP(envMap3DProgram_);
        if (bgfx::isValid(mrtFbo_))         { bgfx::destroy(mrtFbo_);         mrtFbo_         = BGFX_INVALID_HANDLE; }
        if (mrtViewId_ != Detail::kInvalidRtViewId) { Detail::ReleaseRtViewId(mrtViewId_); mrtViewId_ = Detail::kInvalidRtViewId; }
        if (bgfx::isValid(textureSampler))  { bgfx::destroy(textureSampler);  textureSampler  = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(spriteProgram))   { bgfx::destroy(spriteProgram);   spriteProgram   = BGFX_INVALID_HANDLE; }

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
            bgfx::reset(cachedWidth, cachedHeight, resetFlags_);
        }

        // Task 878/879 fix: when spriteViewId is currently bound to a RenderTarget2D
        // (spriteViewId != 0), use that RT's own size for the view rect/2D-ortho-projection
        // instead of always stomping it back to the full window size. Previously this
        // unconditionally overwrote BindAsRenderTarget()'s correctly RT-sized
        // bgfx::setViewRect() call on every single Clear()/SubmitSprite() call, silently
        // corrupting all rendering (3D and 2D) into any RenderTarget2D smaller than the window
        // (the 3D/2D pipelines share spriteViewId/currentViewId_ -- see ApplyViewportOverride()'s
        // comment). Never caught before because no earlier test both rendered into a
        // differently-sized RT AND could pixel-verify the result (blocked by the separate Task
        // 873 SpriteBatch RT-sampling cast bug, fixed alongside this one).
        const uint16_t viewWidth  = (spriteViewId != 0 && currentRtWidth_  > 0) ? currentRtWidth_  : cachedWidth;
        const uint16_t viewHeight = (spriteViewId != 0 && currentRtHeight_ > 0) ? currentRtHeight_ : cachedHeight;

        bgfx::setViewRect(spriteViewId, 0, 0, viewWidth, viewHeight);

        float ortho[16];
        bx::mtxOrtho(ortho, 0.0f, static_cast<float>(viewWidth), static_cast<float>(viewHeight), 0.0f, 0.0f,
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

    void BgfxGraphicsBackend::SetSwapInterval(int interval)
    {
        resetFlags_ = (interval > 0) ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
        bgfx::reset(cachedWidth, cachedHeight, resetFlags_);
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

    void BgfxGraphicsBackend::SubmitSprite(bgfx::TextureHandle textureHandle, int texWidth, int texHeight,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           float layerDepth)
    {
        (void)layerDepth;

        if (!bgfx::isValid(textureHandle))
        {
            return;
        }
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0)
        {
            return;
        }

        EnsureViewState();

        float u1 = static_cast<float>(sourceRectangle.X) / static_cast<float>(texWidth);
        float v1 = static_cast<float>(sourceRectangle.Y) / static_cast<float>(texHeight);
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / static_cast<float>(texWidth);
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / static_cast<float>(texHeight);

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

        bgfx::setTexture(0, textureSampler, textureHandle, samplerFlags_[0]);
        bgfx::setVertexBuffer(0, &vertexBuffer);
        bgfx::setIndexBuffer(&indexBuffer);
        if (scissorW_ > 0 && scissorH_ > 0)
            bgfx::setScissor(scissorX_, scissorY_, scissorW_, scissorH_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                       | blendFlags_ | depthFlags_ | cullFlags_, blendFactorPacked_);
        bgfx::setStencil(stencilFront_, stencilBack_);
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

    static uint32_t XnaCompareFuncToBgfxStencilTest(int f)
    {
        // XNA CompareFunction: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
        //                      GreaterEqual=5, Greater=6, NotEqual=7
        switch (f)
        {
        case 1:  return BGFX_STENCIL_TEST_NEVER;    break;
        case 2:  return BGFX_STENCIL_TEST_LESS;     break;
        case 3:  return BGFX_STENCIL_TEST_LEQUAL;   break;
        case 4:  return BGFX_STENCIL_TEST_EQUAL;    break;
        case 5:  return BGFX_STENCIL_TEST_GEQUAL;   break;
        case 6:  return BGFX_STENCIL_TEST_GREATER;  break;
        case 7:  return BGFX_STENCIL_TEST_NOTEQUAL; break;
        default: return BGFX_STENCIL_TEST_ALWAYS;   break;
        }
    }

    static uint32_t XnaStencilOpToFailS(int op)
    {
        // XNA StencilOperation: Keep=0,Zero=1,Replace=2,Increment=3,
        //                       Decrement=4,IncrSat=5,DecrSat=6,Invert=7
        switch (op)
        {
        case 1:  return BGFX_STENCIL_OP_FAIL_S_ZERO;    break;
        case 2:  return BGFX_STENCIL_OP_FAIL_S_REPLACE;  break;
        case 3:  return BGFX_STENCIL_OP_FAIL_S_INCR;     break;
        case 4:  return BGFX_STENCIL_OP_FAIL_S_DECR;     break;
        case 5:  return BGFX_STENCIL_OP_FAIL_S_INCRSAT;  break;
        case 6:  return BGFX_STENCIL_OP_FAIL_S_DECRSAT;  break;
        case 7:  return BGFX_STENCIL_OP_FAIL_S_INVERT;   break;
        default: return BGFX_STENCIL_OP_FAIL_S_KEEP;     break;
        }
    }

    static uint32_t XnaStencilOpToFailZ(int op)
    {
        switch (op)
        {
        case 1:  return BGFX_STENCIL_OP_FAIL_Z_ZERO;    break;
        case 2:  return BGFX_STENCIL_OP_FAIL_Z_REPLACE;  break;
        case 3:  return BGFX_STENCIL_OP_FAIL_Z_INCR;     break;
        case 4:  return BGFX_STENCIL_OP_FAIL_Z_DECR;     break;
        case 5:  return BGFX_STENCIL_OP_FAIL_Z_INCRSAT;  break;
        case 6:  return BGFX_STENCIL_OP_FAIL_Z_DECRSAT;  break;
        case 7:  return BGFX_STENCIL_OP_FAIL_Z_INVERT;   break;
        default: return BGFX_STENCIL_OP_FAIL_Z_KEEP;     break;
        }
    }

    static uint32_t XnaStencilOpToPassZ(int op)
    {
        switch (op)
        {
        case 1:  return BGFX_STENCIL_OP_PASS_Z_ZERO;    break;
        case 2:  return BGFX_STENCIL_OP_PASS_Z_REPLACE;  break;
        case 3:  return BGFX_STENCIL_OP_PASS_Z_INCR;     break;
        case 4:  return BGFX_STENCIL_OP_PASS_Z_DECR;     break;
        case 5:  return BGFX_STENCIL_OP_PASS_Z_INCRSAT;  break;
        case 6:  return BGFX_STENCIL_OP_PASS_Z_DECRSAT;  break;
        case 7:  return BGFX_STENCIL_OP_PASS_Z_INVERT;   break;
        default: return BGFX_STENCIL_OP_PASS_Z_KEEP;     break;
        }
    }

    static uint32_t BuildBgfxStencil(int func, int pass, int fail, int depthFail,
                                     int mask, int writeMask, int ref)
    {
        return XnaCompareFuncToBgfxStencilTest(func)
             | XnaStencilOpToPassZ(pass)
             | XnaStencilOpToFailS(fail)
             | XnaStencilOpToFailZ(depthFail)
             | BGFX_STENCIL_FUNC_REF(static_cast<uint8_t>(ref))
             | BGFX_STENCIL_FUNC_RMASK(static_cast<uint8_t>(mask));
        (void)writeMask; // bgfx uses a global stencil write mask, not per-state
    }

    void BgfxGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                      int depthFunc,
                                                      bool stencilEnable, int stencilFunc,
                                                      int stencilPass, int stencilFail,
                                                      int stencilDepthFail,
                                                      int stencilMask, int stencilWriteMask,
                                                      int referenceStencil,
                                                      bool twoSidedStencilMode,
                                                      int ccwStencilFunc, int ccwStencilPass,
                                                      int ccwStencilFail, int ccwStencilDepthFail)
    {
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

        if (stencilEnable)
        {
            stencilFront_ = BuildBgfxStencil(stencilFunc, stencilPass, stencilFail,
                                             stencilDepthFail, stencilMask, stencilWriteMask,
                                             referenceStencil);
            stencilBack_ = twoSidedStencilMode
                ? BuildBgfxStencil(ccwStencilFunc, ccwStencilPass, ccwStencilFail,
                                   ccwStencilDepthFail, stencilMask, stencilWriteMask,
                                   referenceStencil)
                : stencilFront_;
        }
        else
        {
            stencilFront_ = BGFX_STENCIL_NONE;
            stencilBack_  = BGFX_STENCIL_NONE;
        }
    }

    void BgfxGraphicsBackend::ApplyRasterizerState(int cullMode, int /*fillMode*/,
                                                    bool scissorTestEnable,
                                                    float /*depthBias*/,
                                                    float /*slopeScaleDepthBias*/)
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

    void BgfxGraphicsBackend::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
    {
        // Storage-only (Task 880); applied via ApplyViewportOverride() right before each 3D
        // submit. Depth range is an acceptable Bgfx deviation -- bgfx has no per-view depth
        // range knob equivalent to VkViewport's minDepth/maxDepth (mirrors SetVirtualResolution's
        // documented no-op precedent for parameters Bgfx has no matching concept for).
        if (w <= 0 || h <= 0) { viewportW_ = viewportH_ = 0; viewportSet_ = false; return; }
        viewportX_   = static_cast<uint16_t>(x);
        viewportY_   = static_cast<uint16_t>(y);
        viewportW_   = static_cast<uint16_t>(w);
        viewportH_   = static_cast<uint16_t>(h);
        viewportSet_ = true;
    }

    void BgfxGraphicsBackend::ApplyViewportOverride()
    {
        // Only the backbuffer (view 0) honors a custom sub-region Viewport -- RT passes stay at
        // their EnsureViewState()/BindAsRenderTarget()-established full-RT-size default, since
        // currentViewId_/spriteViewId are shared between the deferred-per-frame 2D SpriteBatch
        // path and this 3D path, and a per-RT custom Viewport cannot be recovered from a single
        // frame-global stored value (mirrors Vulkan's identical RT-pass scoping decision).
        if (viewportSet_ && currentViewId_ == 0 && viewportW_ > 0 && viewportH_ > 0)
            bgfx::setViewRect(currentViewId_, viewportX_, viewportY_, viewportW_, viewportH_);
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
        // Packed RGBA8 blend factor passed as second arg to bgfx::setState in draw calls
        blendFactorPacked_ = (static_cast<uint32_t>(r * 255) << 24) |
                              (static_cast<uint32_t>(g * 255) << 16) |
                              (static_cast<uint32_t>(b * 255) << 8)  |
                               static_cast<uint32_t>(a * 255);
    }

    // ---- 3D: vertex/index buffers + draw wiring ----

    static void ThrowNo3DState()
    {
        throw std::runtime_error(
            "Bgfx backend: SetDepthTestEnabled / SetBlend* "
            "are not yet wired into bgfx state flags.");
    }

    void BgfxGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/)
    {
        Clear(r, g, b, a);
    }

    void BgfxGraphicsBackend::ClearDepth(float /*depth*/) { /* Bgfx depth-only clear not yet implemented */ }

    void BgfxGraphicsBackend::SetDepthTestEnabled(bool)  { ThrowNo3DState(); }
    void BgfxGraphicsBackend::SetBlendEnabled(bool)      { ThrowNo3DState(); }
    void BgfxGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3DState(); }

    // --- BgfxVertexBufferBackend ---

    static bgfx::VertexLayout MakeBgfxLayout(std::size_t stride)
    {
        bgfx::VertexLayout layout;
        layout.begin();
        if (stride == 52)
        {
            // Task 11.10: this layout is independently duplicated (magic stride 52) in
            // EasyGLGraphicsBackend.cpp's ApplyLayout and VulkanGraphicsBackend.cpp's
            // GetOrCreatePipelineSkinned3D - see EasyGLGraphicsBackend.cpp's own comment at its
            // "case 52" for the full cross-reference to the canonical
            // VertexPositionNormalTextureSkinned::getVertexDeclarationStatic() layout and why a
            // shared-derivation refactor was investigated but deferred.
            // SkinnedEffect: pos(3f=12) + normal(3f=12) + uv(2f=8) + weights(4f=16) + indices(4*u8=4)
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Weight,    4, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Indices,   4, bgfx::AttribType::Uint8);
        }
        else if (stride == 20)
        {
            // VertexPositionTexture: pos(3f=12) + uv(2f=8)
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
        }
        else if (stride == 24)
        {
            // VertexPositionColorTexture: pos(3f=12) + color(4xu8=4) + uv(2f=8)
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
        }
        else if (stride == 32)
        {
            // VertexPositionNormalTexture: pos(3f=12) + normal(3f=12) + uv(2f=8)
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
        }
        else
        {
            // VertexPositionColor (stride 16), and any other/unknown stride as a fallback.
            layout.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);       // 12 bytes at offset 0
            layout.add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true); // 4 bytes at offset 12
            if (stride > 16)
                layout.skip(static_cast<uint8_t>(stride - 16)); // pad to actual stride
        }
        layout.end();
        return layout;
    }

    BgfxVertexBufferBackend::BgfxVertexBufferBackend(int capacity)
    {
        layout = MakeBgfxLayout(16);
        handle = bgfx::createDynamicVertexBuffer(
            static_cast<uint32_t>(capacity),
            layout,
            BGFX_BUFFER_ALLOW_RESIZE);
    }

    BgfxVertexBufferBackend::~BgfxVertexBufferBackend()
    {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }

    void BgfxVertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        vertexCount = vertex_count;
        if (stride_in_bytes != stride)
        {
            stride = stride_in_bytes;
            layout = MakeBgfxLayout(stride_in_bytes);
            if (bgfx::isValid(handle)) bgfx::destroy(handle);
            handle = bgfx::createDynamicVertexBuffer(
                static_cast<uint32_t>(vertex_count),
                layout,
                BGFX_BUFFER_ALLOW_RESIZE);
        }
        if (!bgfx::isValid(handle) || !data || vertex_count <= 0) return;
        const uint32_t byteSize = static_cast<uint32_t>(vertex_count) * static_cast<uint32_t>(stride_in_bytes);
        cpuData.assign(static_cast<const uint8_t*>(data),
                       static_cast<const uint8_t*>(data) + byteSize);
        bgfx::update(handle, 0, bgfx::copy(data, byteSize));
    }

    std::unique_ptr<IVertexBufferBackend> BgfxGraphicsBackend::CreateVertexBuffer(int capacity)
    {
        return std::make_unique<BgfxVertexBufferBackend>(capacity);
    }

    // --- BgfxIndexBufferBackend ---

    BgfxIndexBufferBackend::BgfxIndexBufferBackend(int capacity, bool thirtyTwoBit)
        : is32bit(thirtyTwoBit)
    {
        const uint16_t flags = is32bit ? BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE
                                       : BGFX_BUFFER_ALLOW_RESIZE;
        handle = bgfx::createDynamicIndexBuffer(static_cast<uint32_t>(capacity), flags);
    }

    BgfxIndexBufferBackend::~BgfxIndexBufferBackend()
    {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }

    void BgfxIndexBufferBackend::SetData16(const void* data, int index_count)
    {
        indexCount = index_count;
        if (!bgfx::isValid(handle) || !data || index_count <= 0) return;
        bgfx::update(handle, 0, bgfx::copy(data, static_cast<uint32_t>(index_count) * 2u));
    }

    void BgfxIndexBufferBackend::SetData32(const void* data, int index_count)
    {
        indexCount = index_count;
        if (!bgfx::isValid(handle) || !data || index_count <= 0) return;
        bgfx::update(handle, 0, bgfx::copy(data, static_cast<uint32_t>(index_count) * 4u));
    }

    std::unique_ptr<IIndexBufferBackend> BgfxGraphicsBackend::CreateIndexBuffer16(int capacity)
    {
        return std::make_unique<BgfxIndexBufferBackend>(capacity, false);
    }

    // --- 3D draw calls ---

    static uint64_t ToTopologyFlag(PrimitiveType p)
    {
        switch (p)
        {
        case PrimitiveType::TriangleStrip: return BGFX_STATE_PT_TRISTRIP;
        case PrimitiveType::LineList:      return BGFX_STATE_PT_LINES;
        case PrimitiveType::LineStrip:     return BGFX_STATE_PT_LINESTRIP;
        default:                           return 0; // default = triangle list
        }
    }

    void BgfxGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                    const Matrix& world, const Matrix& view,
                                                    const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount)
    {
        if (!bgfx::isValid(colored3DProgram_)) return; // shader not loaded
        auto& vb = static_cast<const BgfxVertexBufferBackend&>(vb_in);
        if (!bgfx::isValid(vb.handle)) return;

        ApplyViewportOverride();

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);
        bgfx::setUniform(wvpUniform_, wvp_col);
        // This path carries no BasicEffect diffuse/VertexColorEnabled (no GpuDrawParams at
        // all); preserve the historical behavior of outputting the raw vertex colors
        // unmodified (diffuseColor=white, vertexColorEnabled=true — Task 364).
        const float whiteDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bgfx::setUniform(diffuseColor3DUnif_, whiteDiffuse);
        const float vceOn[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(vertexColorEn3DUnif_, vceOn);

        bgfx::setVertexBuffer(0, vb.handle);
        bgfx::setStencil(stencilFront_, stencilBack_);
        bgfx::setState((BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | blendFlags_ | depthFlags_ | cullFlags_)
                       | ToTopologyFlag(primitive), blendFactorPacked_);
        SubmitViewProgram(colored3DProgram_);
    }

    void BgfxGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                           const IIndexBufferBackend& ib_in,
                                                           const Matrix& world, const Matrix& view,
                                                           const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount)
    {
        if (!bgfx::isValid(colored3DProgram_)) return;
        auto& vb = static_cast<const BgfxVertexBufferBackend&>(vb_in);
        auto& ib = static_cast<const BgfxIndexBufferBackend&>(ib_in);
        if (!bgfx::isValid(vb.handle) || !bgfx::isValid(ib.handle)) return;

        ApplyViewportOverride();

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);
        bgfx::setUniform(wvpUniform_, wvp_col);
        // See DrawColoredPrimitives above: preserve the historical raw-vertex-color output
        // for this no-GpuDrawParams legacy path (Task 364).
        const float whiteDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bgfx::setUniform(diffuseColor3DUnif_, whiteDiffuse);
        const float vceOn[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(vertexColorEn3DUnif_, vceOn);

        bgfx::setVertexBuffer(0, vb.handle);
        bgfx::setIndexBuffer(ib.handle);
        bgfx::setStencil(stencilFront_, stencilBack_);
        bgfx::setState((BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | blendFlags_ | depthFlags_ | cullFlags_)
                       | ToTopologyFlag(primitive), blendFactorPacked_);
        SubmitViewProgram(colored3DProgram_);
    }

    void BgfxGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb_in,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               const GpuDrawParams& params)
    {
        auto& vb = static_cast<const BgfxVertexBufferBackend&>(vb_in);
        if (!bgfx::isValid(vb.handle)) return;

        ApplyViewportOverride();

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);
        bgfx::setUniform(wvpUniform_, wvp_col);

        // Task 888: fog uniforms are set unconditionally (not per-branch) since bgfx uniforms
        // are shared by name across every program -- any program declaring u_fogColor/
        // u_fogParams as inputs picks these up automatically; programs that don't simply ignore
        // them, no error.
        float fogColor4[4]  = { params.fogColor[0], params.fogColor[1], params.fogColor[2], 0.0f };
        bgfx::setUniform(fogColorUnif_, fogColor4);
        float fogParams4[4] = { params.fogEnabled ? 1.0f : 0.0f, params.fogStart, params.fogEnd, 0.0f };
        bgfx::setUniform(fogParamsUnif_, fogParams4);

        bgfx::setVertexBuffer(0, vb.handle);
        bgfx::setStencil(stencilFront_, stencilBack_);
        const uint64_t state = (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                                | blendFlags_ | depthFlags_ | cullFlags_)
                               | ToTopologyFlag(primitive);
        bgfx::setState(state, blendFactorPacked_);

        const bool alphaTestActive = (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        if (params.dualTexture && bgfx::isValid(dualTexture3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            if (bgfx::isValid(texColor3DSampler2_))
            {
                if (params.texture1)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture1);
                    bgfx::setTexture(1, texColor3DSampler2_, tex.textureHandle, samplerFlags_[1]);
                }
                else
                {
                    // Task 387: same fallback as slot 0 (Task 379) -- fall back to opaque white
                    // instead of leaving the previous draw's texture bound.
                    bgfx::setTexture(1, texColor3DSampler2_, defaultWhiteTexture3D_, samplerFlags_[1]);
                }
            }
            SubmitViewProgram(dualTexture3DProgram_);
        }
        else if (params.skinned && bgfx::isValid(skinned3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float amb[4]  = { params.ambientColor[0],  params.ambientColor[1],  params.ambientColor[2],  0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float dir[4]  = { params.light0Dir[0],     params.light0Dir[1],     params.light0Dir[2],     0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir);
            float diff[4] = { params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff);
            float litEn[4] = { params.lightingEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(lightingEn3DUnif_, litEn);
            // Task 899: EmissiveColor was never forwarded to the skinned3d shader at all -- see
            // fs_skinned3d.sc's comment for the full finding.
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            if (params.boneCount > 0 && bgfx::isValid(bonesUnif_))
                bgfx::setUniform(bonesUnif_, params.boneTransforms, static_cast<uint16_t>(params.boneCount));
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            SubmitViewProgram(skinned3DProgram_);
        }
        else if (params.envMapping && bgfx::isValid(envMap3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_,  params.diffuseColor);
            bgfx::setUniform(world3DUnif_,          params.worldColMajor);
            float normalMatrix[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrix);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrix);
            float eyePos[4] = { params.eyePositionWorld[0], params.eyePositionWorld[1],
                                 params.eyePositionWorld[2], 0.0f };
            bgfx::setUniform(eyePos3DUnif_, eyePos);
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            float dir[4]  = { params.light0Dir[0], params.light0Dir[1], params.light0Dir[2], 0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir);
            float diff[4] = { params.light0Diffuse[0], params.light0Diffuse[1],
                               params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff);
            float amount[4] = { params.envMapAmount, params.fresnelEnabled ? 1.0f : 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(envMapAmountUnif_, amount);
            float specular[4] = { params.envMapSpecular[0], params.envMapSpecular[1],
                                   params.envMapSpecular[2], params.fresnelFactor };
            bgfx::setUniform(envMapSpecularUnif_, specular);
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            if (params.envMap && bgfx::isValid(envMapSampler_))
            {
                // Task 907 (closes Task 874): dynamic_cast to the common cube-samplable
                // interface instead of an unsafe static_cast<const BgfxTextureCubeBackend&> --
                // params.envMap may be a BgfxRenderTargetCubeBackend (a sampled RenderTargetCube),
                // whose layout differs entirely from BgfxTextureCubeBackend's.
                if (const auto* samplable = dynamic_cast<const IBgfxCubeSamplable*>(params.envMap))
                    bgfx::setTexture(1, envMapSampler_, samplable->GetBgfxCubeTextureHandle());
            }
            SubmitViewProgram(envMap3DProgram_);
        }
        else if (alphaTestActive && params.vertexColorEnabled
                 && bgfx::isValid(alphaTestColoredTextured3DProgram_))
        {
            // Task 887: stride-24 (VertexPositionColorTexture) variant — reads a_color0 and
            // gates it by VertexColorEnabled, mirroring BasicEffect's coloredTextured3DProgram_.
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vcEn[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vcEn);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            SubmitViewProgram(alphaTestColoredTextured3DProgram_);
        }
        else if (alphaTestActive && bgfx::isValid(alphaTest3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            SubmitViewProgram(alphaTest3DProgram_);
        }
        else if (params.lightingEnabled && bgfx::isValid(litTextured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float amb[4] = { params.ambientColor[0], params.ambientColor[1],
                             params.ambientColor[2], 0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float dir[4] = { params.light0Dir[0], params.light0Dir[1],
                             params.light0Dir[2], 0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir);
            float diff[4] = { params.light0Diffuse[0], params.light0Diffuse[1],
                              params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff);
            float litEn[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(lightingEn3DUnif_, litEn);
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1],
                               params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1],
                               params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
            // Task 892 fix: correct inverse-transpose normal matrix, not the raw World/WVP.
            float normalMatrixLit[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrixLit);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrixLit);
            float eyePos[4] = { params.eyePositionWorld[0], params.eyePositionWorld[1],
                                 params.eyePositionWorld[2], 0.0f };
            bgfx::setUniform(eyePos3DUnif_, eyePos);
            float spec0[4] = { params.light0Specular[0], params.light0Specular[1],
                                params.light0Specular[2], 0.0f };
            bgfx::setUniform(light0Spec3DUnif_, spec0);
            float spec1[4] = { params.light1Specular[0], params.light1Specular[1],
                                params.light1Specular[2], 0.0f };
            bgfx::setUniform(light1Spec3DUnif_, spec1);
            float spec2[4] = { params.light2Specular[0], params.light2Specular[1],
                                params.light2Specular[2], 0.0f };
            bgfx::setUniform(light2Spec3DUnif_, spec2);
            float specColorPower[4] = { params.specularColor[0], params.specularColor[1],
                                         params.specularColor[2], params.specularPower };
            bgfx::setUniform(specularColorPower3DUnif_, specColorPower);

            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            SubmitViewProgram(litTextured3DProgram_);
        }
        else if (params.textureEnabled && params.vertexColorEnabled
                 && bgfx::isValid(coloredTextured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vcEn[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vcEn);
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            SubmitViewProgram(coloredTextured3DProgram_);
        }
        else if (params.textureEnabled && bgfx::isValid(textured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            if (bgfx::isValid(texColor3DSampler_))
            {
                if (params.texture0)
                {
                    auto& tex = static_cast<const BgfxTextureBackend&>(*params.texture0);
                    bgfx::setTexture(0, texColor3DSampler_, tex.textureHandle, samplerFlags_[0]);
                }
                else
                {
                    // Task 379: fall back to opaque white instead of leaving the previous
                    // draw's texture bound (matches EasyGL/Vulkan's identical fallback).
                    bgfx::setTexture(0, texColor3DSampler_, defaultWhiteTexture3D_, samplerFlags_[0]);
                }
            }
            SubmitViewProgram(textured3DProgram_);
        }
        else
        {
            if (!bgfx::isValid(colored3DProgram_)) return;
            // BasicEffect no-texture path (Task 364): honor DiffuseColor and gate the
            // per-vertex color multiply on VertexColorEnabled, matching FNA's
            // ComputeCommonVSOutput()/`vout.Diffuse *= vin.Color` semantics.
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vce[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vce);
            SubmitViewProgram(colored3DProgram_);
        }
    }

    void BgfxGraphicsBackend::DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                         const IIndexBufferBackend& ib_in,
                                                         const Matrix& /*world*/,
                                                         const Matrix& view,
                                                         const Matrix& projection,
                                                         PrimitiveType primitive,
                                                         int primitiveCount,
                                                         int instanceCount,
                                                         const GpuDrawParams& params)
    {
        if (params.instanceVb == nullptr) return;
        if (!bgfx::isValid(instanced3DProgram_) || !bgfx::isValid(vpInstanced3DUnif_)) return;

        auto& vb     = static_cast<const BgfxVertexBufferBackend&>(vb_in);
        auto& ib     = static_cast<const BgfxIndexBufferBackend&>(ib_in);
        auto& instVb = static_cast<const BgfxVertexBufferBackend&>(*params.instanceVb);
        if (!bgfx::isValid(vb.handle) || instVb.cpuData.empty()) return;

        ApplyViewportOverride();

        const int instCount = std::max(1, instanceCount);
        const uint16_t instStride = static_cast<uint16_t>(instVb.stride > 0 ? instVb.stride : 64);

        if (bgfx::getAvailInstanceDataBuffer(static_cast<uint32_t>(instCount), instStride) <
            static_cast<uint32_t>(instCount))
            return;

        bgfx::InstanceDataBuffer idb{};
        bgfx::allocInstanceDataBuffer(&idb, static_cast<uint32_t>(instCount), instStride);
        const std::size_t copyBytes = static_cast<std::size_t>(instCount) * instStride;
        std::memcpy(idb.data, instVb.cpuData.data(),
                    std::min(copyBytes, instVb.cpuData.size()));

        const Matrix vp = view * projection;
        float vp_col[16];
        vp.ToColumnMajor(vp_col);
        bgfx::setUniform(vpInstanced3DUnif_, vp_col);

        bgfx::setVertexBuffer(0, vb.handle);
        bgfx::setIndexBuffer(ib.handle);
        bgfx::setInstanceDataBuffer(&idb);
        bgfx::setStencil(stencilFront_, stencilBack_);
        const uint64_t state = (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                                | blendFlags_ | depthFlags_ | cullFlags_)
                               | ToTopologyFlag(primitive);
        bgfx::setState(state, blendFactorPacked_);
        SubmitViewProgram(instanced3DProgram_);
        (void)primitiveCount;
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_BGFX
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Bgfx::BgfxGraphicsBackend>(args.window, args.swapInterval);
    }
#endif
}
