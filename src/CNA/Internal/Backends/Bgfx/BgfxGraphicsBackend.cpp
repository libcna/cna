#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

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

        // Task 926: created WITHOUT initial _mem (mutable -- bgfx::createTexture2D's own doc:
        // "If _mem is non-NULL, created texture will be immutable"), mirroring
        // BgfxTextureCubeBackend's established pattern, so UpdatePixels/UpdatePixelsLevel below
        // can genuinely re-upload later. hasMips now genuinely threaded through (was hardcoded
        // false regardless of data.mipLevels) so a real mip chain can be allocated and later
        // populated via UpdatePixelsLevel.
        textureHandle = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            data.mipLevels > 1,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
        );

        if (!bgfx::isValid(textureHandle))
        {
            throw std::runtime_error("Failed to create BGFX texture handle.");
        }

        const bgfx::Memory* memory = bgfx::copy(data.pixels.data(), static_cast<uint32_t>(data.pixels.size()));
        bgfx::updateTexture2D(textureHandle, 0, 0, 0, 0,
                               static_cast<uint16_t>(width), static_cast<uint16_t>(height), memory);
    }

    void BgfxTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!bgfx::isValid(textureHandle) || !rgba) return;
        const uint32_t size = static_cast<uint32_t>(stride) * static_cast<uint32_t>(height);
        const bgfx::Memory* mem = bgfx::copy(rgba, size);
        bgfx::updateTexture2D(textureHandle, 0, 0, 0, 0,
                              static_cast<uint16_t>(width), static_cast<uint16_t>(height), mem,
                              static_cast<uint16_t>(stride));
    }

    // Task 926 (split from Task 867): real GPU upload for level>0, mirroring
    // BgfxTextureCubeBackend::SetData's established bgfx::updateTextureCube pattern --
    // previously the shared IGraphicsBackend no-op default, silently discarding the caller's
    // mip-level data.
    void BgfxTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (!bgfx::isValid(textureHandle) || !rgba || level < 0) return;
        const uint32_t size = static_cast<uint32_t>(levelW) * static_cast<uint32_t>(levelH) * 4u;
        const bgfx::Memory* mem = bgfx::copy(rgba, size);
        bgfx::updateTexture2D(textureHandle, 0, static_cast<uint8_t>(level), 0, 0,
                              static_cast<uint16_t>(levelW), static_cast<uint16_t>(levelH), mem);
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
        // Task 951: force the reserved highest-id view (Detail::kBackbufferFlushViewId) to be the
        // last one bgfx processes this frame -- see that constant's own comment for why. Touching
        // it (rather than resetting any real render target's own view) never discards a still-
        // pending draw queued against a concurrently-active render target within this same
        // un-advanced frame.
        bgfx::setViewFrameBuffer(Detail::kBackbufferFlushViewId, BGFX_INVALID_HANDLE);
        bgfx::touch(Detail::kBackbufferFlushViewId);

        // Request a screenshot of the default backbuffer (BGFX_INVALID_HANDLE = swapchain).
        // The callback fires after bgfx::frame() flushes the current command buffer.
        readbackCallback_.screenshotReady = false;
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "cna_readback");

        // Advance up to 3 frames to give the render thread time to fire the callback.
        // In single-threaded bgfx mode (typical on Linux) the first frame() suffices.
        for (int attempt = 0; attempt < 3 && !readbackCallback_.screenshotReady; ++attempt)
            bgfx::frame();
        spriteVpValid_ = false; // REMED-GFX-072: sprite viewport is per-frame; clear for the next one.
        EndFrameSegments();     // REMED-GFX-065: recycle the per-frame viewport-segment view ids.

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
        // REMED-GFX-078: upload the per-slot render-target V-flip flags accumulated by
        // BindSamplerSlot for this draw (all-zero = ordinary textures, sampling unchanged), then
        // clear them so the next draw starts fresh. bgfx resets uniform state per submit(), so like
        // the fog/depth-bias uniforms this must be set right before the submit; programs that don't
        // declare u_rtFlipV simply ignore it.
        if (bgfx::isValid(rtFlipVUnif_))
            bgfx::setUniform(rtFlipVUnif_, rtFlipV_);
        if (bgfx::isValid(activeOcclusionQuery_))
            bgfx::submit(currentViewId_, program, activeOcclusionQuery_);
        else
            bgfx::submit(currentViewId_, program);
        rtFlipV_[0] = rtFlipV_[1] = rtFlipV_[2] = rtFlipV_[3] = 0.0f;
    }

    // REMED-GFX-078: safe replacement for the former static_cast<const BgfxTextureBackend&> at every
    // 3D-effect sampler slot. `texture` (params.texture0/1, PBR maps, dual-texture, env-map's 2D
    // base) may be a BgfxTextureBackend OR a BgfxRenderTargetBackend (a RenderTarget2D used as an
    // effect texture -- legal XNA, since RenderTarget2D is-a Texture2D). Those are unrelated sibling
    // classes, so the old downcast was UB that read the wrong pooled handle for a render target;
    // both implement IBgfxSamplable, so the real handle is resolved through that. When the source is
    // a render-target color attachment on an originBottomLeft renderer, the slot (0-3) is recorded in
    // rtFlipV_ so the shader V-flips its sample (REMED-GFX-067's bottom-up-FBO compensation, applied
    // here at the generic-effect UV instead of at SpriteBatch's CPU-side quad).
    void BgfxGraphicsBackend::BindSamplerSlot(int slot, bgfx::UniformHandle sampler,
                                              const ITextureBackend* texture,
                                              bgfx::TextureHandle fallback)
    {
        if (!bgfx::isValid(sampler)) return;
        bgfx::TextureHandle handle = fallback;
        if (texture)
        {
            if (const auto* samplable = dynamic_cast<const IBgfxSamplable*>(texture))
            {
                handle = samplable->GetBgfxTextureHandle();
                if (samplable->IsRenderTargetColorSource() && slot >= 0 && slot < 4
                    && bgfx::getCaps()->originBottomLeft)
                    rtFlipV_[slot] = 1.0f;
            }
        }
        bgfx::setTexture(static_cast<uint8_t>(slot), sampler, handle, samplerFlags_[slot]);
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

    bool BgfxGraphicsBackend::ReadTextureRegionEXT(bgfx::TextureHandle srcTexture, int level,
                                                   int x, int y, int w, int h, void* data,
                                                   int layer)
    {
        // Task 914's proven readback shape (BgfxTextureCubeBackend/BgfxTexture3DBackend::GetData):
        // a temporary BLIT_DST|READ_BACK texture is the only thing bgfx::readTexture accepts.
        const bgfx::TextureHandle readback = bgfx::createTexture2D(
            static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(readback))
        {
            // BGFX_CAPS_TEXTURE_BLIT/READ_BACK are not guaranteed on every renderer this backend
            // can select. REMED-GFX-127: report the missing capability instead of returning with
            // the caller's buffer unwritten -- the shared layer turns this into a deterministic
            // System::NotSupportedException rather than a fabricated transparent-black frame.
            std::cerr << "CNA: bgfx RenderTarget2D::GetData readback texture creation failed -- "
                          "BGFX_CAPS_TEXTURE_BLIT/READ_BACK may not be supported on "
                       << bgfx::getRendererName(bgfx::getRendererType()) << "\n";
            return false;
        }

        // The blit must run AFTER every render-target and viewport-segment view queued this frame,
        // or it would copy the previous frame's content. bgfx processes views in ascending id
        // order and kBackbufferFlushViewId is the reserved highest id, above both the RT base range
        // and the per-frame segment range (see Detail's own view-id partition comment).
        // REMED-GFX-134: `layer` is the source array layer -- a cube target's face index. It stays
        // 0 for the plain 2D render target this helper was written for.
        bgfx::blit(Detail::kBackbufferFlushViewId, readback, 0, 0, 0, 0,
                   srcTexture, static_cast<uint8_t>(level),
                   static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                   static_cast<uint16_t>(layer),
                   static_cast<uint16_t>(w), static_cast<uint16_t>(h), 1);

        const uint32_t targetFrame = bgfx::readTexture(readback, data);
        const bool completed = AdvanceFramesUntil(targetFrame);
        // bgfx::frame() ran, so this backend's per-frame state must be recycled exactly as
        // ReadBackbuffer does after its own frame advance -- otherwise the segment view ids
        // allocated before this readback would be treated as still belonging to the current frame.
        spriteVpValid_ = false;
        EndFrameSegments();

        bgfx::destroy(readback);
        return completed;
    }

    // --- BgfxTextureCubeBackend ---

    // REMED-GFX-135: mirrors TextureCube.cpp's CalculateMipLevels(size,size) -- bgfx builds the
    // same full chain when `mipMap` is true, so this is what `handle` really has storage for.
    static int BgfxCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    // Mirrors Texture3D.cpp's CalculateMipLevels(width, height) -- depth does not participate.
    static int BgfxVolumeMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    BgfxTextureCubeBackend::BgfxTextureCubeBackend(int size, bool mipMap, int /*surfaceFormat*/)
        : size_(size)
        , levelCount_(mipMap ? BgfxCubeMipLevels(size) : 1)
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

    bool BgfxTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                         const void* data, int dataLength)
    {
        // REMED-GFX-135: each of these used to be a silent `return`, which the shared layer could
        // not tell apart from a completed upload.
        if (!bgfx::isValid(handle) || !data || w <= 0 || h <= 0) return false;
        if (face < 0 || face >= 6 || level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const uint32_t regionBytes = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4u;
        if (dataLength < 0 || static_cast<uint32_t>(dataLength) < regionBytes) return false;

        // Sized to the REGION, not to dataLength: bgfx::updateTextureCube reads exactly w*h*4 bytes
        // out of `mem`, and a longer block would be an allocation this call never uses.
        const bgfx::Memory* mem = bgfx::copy(data, regionBytes);
        bgfx::updateTextureCube(handle,
            0,
            static_cast<uint8_t>(face),
            static_cast<uint8_t>(level),
            static_cast<uint16_t>(x), static_cast<uint16_t>(y),
            static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            mem);
        return true;
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
    bool BgfxTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                         void* data, int dataLength) const
    {
        // REMED-GFX-130: each of these used to be a silent `return`, which the shared layer turned
        // into a complete transparent-black face instead of a refusal.
        if (!bgfx::isValid(handle) || !data || dataLength <= 0 || w <= 0 || h <= 0) return false;
        if (face < 0 || face >= 6 || level < 0 || dataLength < w * h * 4) return false;

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
            return false;
        }

        bgfx::blit(0, readback, 0, 0, 0, 0,
                   handle, static_cast<uint8_t>(level),
                   static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(face),
                   static_cast<uint16_t>(w), static_cast<uint16_t>(h), 1);

        const uint32_t targetFrame = bgfx::readTexture(readback, data);
        const bool completed = AdvanceFramesUntil(targetFrame);

        bgfx::destroy(readback);
        return completed;
    }

    std::unique_ptr<ITextureCubeBackend> BgfxGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<BgfxTextureCubeBackend>(size, mipMap, surfaceFormat);
    }

    // --- BgfxTexture3DBackend ---

    BgfxTexture3DBackend::BgfxTexture3DBackend(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
        : width_(w), height_(h), depth_(depth)
        , levelCount_(mipMap ? BgfxVolumeMipLevels(w, h) : 1)
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

    bool BgfxTexture3DBackend::SetData(int level, int x, int y, int z,
                                       int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        // REMED-GFX-135: see BgfxTextureCubeBackend::SetData -- silent returns looked like writes.
        if (!bgfx::isValid(handle) || !data || w <= 0 || h <= 0 || depth <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        const uint32_t regionBytes =
            static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * static_cast<uint32_t>(depth) * 4u;
        if (dataLength < 0 || static_cast<uint32_t>(dataLength) < regionBytes) return false;

        const bgfx::Memory* mem = bgfx::copy(data, regionBytes);
        bgfx::updateTexture3D(handle,
            static_cast<uint8_t>(level),
            static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(z),
            static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(depth),
            mem);
        return true;
    }

    // Task 914: real GPU readback, previously a total no-op -- mirrors
    // BgfxTextureCubeBackend::GetData's approach exactly, except the temporary readback texture is
    // itself a 3D texture (sized to the requested w/h/depth region) rather than a plain 2D one,
    // since a 3D source's _depth argument applies to blit regardless of the destination's own
    // dimensionality and this keeps both sides symmetric.
    bool BgfxTexture3DBackend::GetData(int level, int x, int y, int z,
                                       int w, int h, int depth,
                                       void* data, int dataLength) const
    {
        // REMED-GFX-130: see BgfxTextureCubeBackend::GetData -- silent returns fabricated a volume.
        if (!bgfx::isValid(handle) || !data || dataLength <= 0 || w <= 0 || h <= 0 || depth <= 0) return false;
        if (level < 0 || dataLength < w * h * depth * 4) return false;

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
            return false;
        }

        bgfx::blit(0, readback, 0, 0, 0, 0,
                   handle, static_cast<uint8_t>(level),
                   static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(z),
                   static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(depth));

        const uint32_t targetFrame = bgfx::readTexture(readback, data);
        const bool completed = AdvanceFramesUntil(targetFrame);

        bgfx::destroy(readback);
        return completed;
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
            // the backbuffer, [kFirstSegmentViewId, 255) is reserved for REMED-GFX-065's per-frame
            // viewport-segment views, and kBackbufferFlushViewId (Task 951) is reserved at the top
            // end, leaving ids [1, kFirstSegmentViewId) for render targets/MRT base views.
            static constexpr bgfx::ViewId kMaxBgfxViews = kFirstSegmentViewId;
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

    BgfxRenderTargetBackend::BgfxRenderTargetBackend(BgfxGraphicsBackend* owner,
                                                      int w, int h, int depthFormat, bool preserve,
                                                      int requestedMultiSampleCount, bool mipMap)
        : width(w), height(h), preserveContents(preserve), viewId_(Detail::AllocateRtViewId()),
          owner_(owner)
    {
        // REMED-GFX-127: bgfx allocates the FULL chain when hasMips is set (same convention the
        // EasyGL target uses), so GetData can validate a mip request against a real level count
        // instead of guessing.
        mipLevels_ = 1;
        if (mipMap)
        {
            int dim = std::max(w, h);
            while (dim > 1) { dim >>= 1; ++mipLevels_; }
        }

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
        // REMED-GFX-018: do not mutate this base view's clear state here. A same-frame A->B->A
        // rebind would retroactively overwrite A's already-recorded clear because bgfx view state
        // is resolved only at frame(). The owning graphics backend assigns CLEAR_NONE or the exact
        // requested mask when the first draw/clear on this binding receives its ordered view.
    }

    void BgfxRenderTargetBackend::UnbindAsRenderTarget()
    {
        bgfx::setViewFrameBuffer(viewId_, BGFX_INVALID_HANDLE);
    }

    bool BgfxRenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level >= mipLevels_)
            throw System::NotSupportedException(
                "BgfxRenderTargetBackend::GetData: this render target has " +
                std::to_string(mipLevels_) + " mip level(s); level " + std::to_string(level) +
                " was requested.");

        const int levelW = std::max(1, width >> level);
        const int levelH = std::max(1, height >> level);
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping into an
        // apparently valid one.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(levelW) || bottom > static_cast<std::int64_t>(levelH))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(levelW) + "x" +
                    std::to_string(levelH) + " mip level.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");
        if (owner_ == nullptr || data == nullptr || !bgfx::isValid(colorTex))
            return false;

        // REMED-GFX-067 established that a render target's colour attachment stores its texel
        // memory BOTTOM-UP on originBottomLeft renderers (OpenGL/GLES/WebGL) -- that finding fixed
        // sampling; the identical correction belongs on the readback path, or GetData would hand
        // back a vertically mirrored frame. The requested rectangle is mapped into bottom-up
        // coordinates for the blit and the returned rows are then flipped back, so the public
        // result is top-left-origin like every other backend. No-op on Vulkan/D3D/Metal.
        const bool bottomUp = bgfx::getCaps()->originBottomLeft;
        const int srcY = bottomUp ? (levelH - (y + h)) : y;
        if (!owner_->ReadTextureRegionEXT(colorTex, level, x, srcY, w, h, data))
            return false;

        if (bottomUp && h > 1)
        {
            auto* pixels = static_cast<std::uint8_t*>(data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
            std::vector<std::uint8_t> scratch(rowBytes);
            for (int topRow = 0; topRow < h / 2; ++topRow)
            {
                std::uint8_t* top = pixels + static_cast<std::size_t>(topRow) * rowBytes;
                std::uint8_t* bottom = pixels + static_cast<std::size_t>(h - 1 - topRow) * rowBytes;
                std::memcpy(scratch.data(), top, rowBytes);
                std::memcpy(top, bottom, rowBytes);
                std::memcpy(bottom, scratch.data(), rowBytes);
            }
        }
        return true;
    }

    // ---

    std::unique_ptr<IRenderTargetBackend> BgfxGraphicsBackend::CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // multiSampleCount: BGFX_TEXTURE_RT_MSAA_Xn (Task 878/879) — see BgfxRenderTargetBackend.
        // mipMap (Task 906): real mip chain, auto-regenerated by bgfx itself on every resolve.
        return std::make_unique<BgfxRenderTargetBackend>(this, w, h, depthFormat, preserveContents,
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
            // REMED-GFX-065: this RT's own view is the base for its segment chain this frame.
            ResetSegmentTarget(bgfxRt->viewId_, bgfxRt->fbo);
        }
        else
        {
            bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
            currentViewId_ = 0;
            spriteViewId = 0;
            currentRtWidth_ = currentRtHeight_ = 0;
            // REMED-GFX-065: back to the backbuffer -> base view 0, its own default framebuffer.
            ResetSegmentTarget(0, BGFX_INVALID_HANDLE);
        }
    }

    void BgfxGraphicsBackend::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
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
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(
                    renderTargets[0].GetRenderTargetCube(),
                    renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }
        for (int i = 0; i < count; ++i)
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "Bgfx SetRenderTargets: cube faces in a multi-target set are not "
                    "implemented by this CNA backend.");
        // Multi-target: build a temporary framebuffer from the color textures. Task 910: MRT
        // gets its own freshly-allocated view id too (not the old hardcoded 1), so a 2nd,
        // different MRT setup bound later within the same un-advanced frame doesn't clobber
        // this one's already-recorded draws.
        static constexpr int kMaxAttachments = 8; // bgfx BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS default
        bgfx::Attachment attachments[kMaxAttachments];
        int n = count < kMaxAttachments ? count : kMaxAttachments;
        for (int i = 0; i < n; ++i)
        {
            auto* bgfxRt = static_cast<BgfxRenderTargetBackend*>(
                renderTargets[i].GetRenderTarget2D());
            attachments[i].init(bgfxRt->colorTex);
        }
        mrtFbo_ = bgfx::createFrameBuffer(static_cast<uint8_t>(n), attachments);
        mrtViewId_ = Detail::AllocateRtViewId();
        bgfx::setViewFrameBuffer(mrtViewId_, mrtFbo_);
        currentViewId_ = mrtViewId_;
        spriteViewId = mrtViewId_;
        currentRtWidth_  = static_cast<uint16_t>(renderTargets[0].GetWidth());
        currentRtHeight_ = static_cast<uint16_t>(renderTargets[0].GetHeight());
        // REMED-GFX-065: the MRT view is the base for its segment chain; a segment preserves the whole
        // multi-attachment framebuffer (mrtFbo_ carries every color attachment).
        ResetSegmentTarget(mrtViewId_, mrtFbo_);
    }

    // --- BgfxRenderTargetCubeBackend ---

    BgfxRenderTargetCubeBackend::BgfxRenderTargetCubeBackend(BgfxGraphicsBackend* owner, int size,
                                                              int depthFormat, bool mipMap,
                                                              int requestedMultiSampleCount)
        : owner_(owner), size_(size), viewId_(Detail::AllocateRtViewId())
    {
        // REMED-GFX-134: what bgfx really allocates for this cube, so GetData can refuse a level
        // that has no storage instead of answering with a clamped one.
        levelCount_ = mipMap ? BgfxCubeMipLevels(size) : 1;
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
        // REMED-GFX-134: `fbo` is only an alias into faceFbos_, so it is never destroyed separately.
        for (auto& faceFbo : faceFbos_)
            if (bgfx::isValid(faceFbo)) bgfx::destroy(faceFbo);
        fbo = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(cubeTex))  bgfx::destroy(cubeTex);
        if (bgfx::isValid(depthTex)) bgfx::destroy(depthTex);
        Detail::ReleaseRtViewId(viewId_);
    }

    void BgfxRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (face < 0 || face >= 6) return;
        const auto slot = static_cast<std::size_t>(face);
        // REMED-GFX-134: create this face's framebuffer once and KEEP it. Destroying the previous
        // face's handle here left whatever draws were already recorded against it pointing at a
        // destroyed framebuffer, so the first of several faces bound within one un-advanced bgfx
        // frame was silently dropped.
        if (!bgfx::isValid(faceFbos_[slot]))
        {
            bgfx::Attachment atts[2];
            atts[0].init(cubeTex, bgfx::Access::Write, static_cast<uint16_t>(face));
            int numAttachments = 1;
            if (bgfx::isValid(depthTex))
            {
                // Task 951: bgfx::Attachment::init()'s _resolve parameter defaults to
                // BGFX_RESOLVE_AUTO_GEN_MIPS (desired for atts[0]'s colour/cube-face attachment,
                // see Task 907's own comment above) -- but bgfx hard-asserts if a DEPTH attachment
                // is given that same default ("Depth textures do not support MSAA resolve"), since
                // depthTex is never created with hasMips=true (see the constructor above) and mip
                // auto-regeneration makes no sense for a depth buffer regardless. BGFX_RESOLVE_NONE
                // is depthTex's own correct, do-nothing resolve mode.
                atts[1].init(depthTex, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
                numAttachments = 2;
            }
            faceFbos_[slot] = bgfx::createFrameBuffer(numAttachments, atts);
        }
        fbo = faceFbos_[slot];

        // REMED-GFX-134: only (re)point the BASE view when it has not already recorded a draw this
        // frame. Once it has, REMED-GFX-018/065's own machinery routes this face onto a fresh
        // ordered segment view and gives THAT view this framebuffer (see SelectViewportSegment);
        // re-pointing the base here instead would redirect the earlier face's recorded draws at
        // this face's attachment, which is exactly how the first face's content was lost.
        if (owner_ == nullptr || !owner_->BaseViewUsedThisFrameEXT(viewId_))
        {
            bgfx::setViewFrameBuffer(viewId_, fbo);
            bgfx::setViewRect(viewId_, 0, 0, static_cast<uint16_t>(size_),
                              static_cast<uint16_t>(size_));
        }
    }

    void BgfxRenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        bgfx::setViewFrameBuffer(viewId_, BGFX_INVALID_HANDLE);
    }

    bool BgfxRenderTargetCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        // REMED-GFX-134: closes the refusal this class inherited from ITextureCubeBackend's
        // `return false` default.
        if (owner_ == nullptr || !bgfx::isValid(cubeTex) || data == nullptr) return false;
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        // REMED-GFX-134, this backend's two measured boundaries. `bgfx::blit` reports no error for
        // either of these and simply copies untouched memory, so answering them would be exactly
        // the fabricated transparent-black face REMED-GFX-130 removed -- one level further down.
        //
        //  * A MULTISAMPLED cube target: bgfx owns the multisample storage behind `cubeTex` and
        //    exposes no resolved handle a blit can source, and there is no capability bit to ask.
        //    Measured all-zero on the OpenGL renderer this backend selects here.
        //  * A mip level above 0: the per-face `BGFX_RESOLVE_AUTO_GEN_MIPS` chain
        //    `BindAsRenderTargetFace` requests is not regenerated for a cube render target on that
        //    renderer, so those levels hold nothing that was ever rendered. Level 0 -- and every
        //    mip of a PLAIN BgfxTextureCubeBackend, whose levels come from SetData -- is exact.
        //
        // Both are recorded as independent findings rather than papered over here.
        if (multiSampleCount > 0) return false;
        if (level > 0) return false;

        // REMED-GFX-134: complete the current frame BEFORE queueing the readback blit. bgfx runs a
        // framebuffer's MSAA resolve and its BGFX_RESOLVE_AUTO_GEN_MIPS mip regeneration when it
        // tears the framebuffer down at frame end, so a blit queued alongside this frame's draws
        // copies pre-resolve memory -- measurably all-zero for a multisampled cube target and for
        // every mip level above 0 -- even though it already runs on the reserved highest view id.
        owner_->CompleteFrameForResolveEXT();

        // REMED-GFX-067: a render target's colour attachment stores its texel memory BOTTOM-UP on
        // originBottomLeft renderers (OpenGL/GLES/WebGL). BgfxRenderTargetBackend::GetData already
        // makes this correction for a 2D target; a rendered cube face is rasterized exactly the
        // same way, so it needs it too -- applied once, here, and never on the plain-TextureCube
        // path whose content came from an upload instead.
        const bool bottomUp = bgfx::getCaps()->originBottomLeft;
        const int srcY = bottomUp ? (levelSize - (y + h)) : y;
        if (!owner_->ReadTextureRegionEXT(cubeTex, level, x, srcY, w, h, data, face))
            return false;

        if (bottomUp && h > 1)
        {
            auto* pixels = static_cast<std::uint8_t*>(data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
            std::vector<std::uint8_t> scratch(rowBytes);
            for (int topRow = 0; topRow < h / 2; ++topRow)
            {
                std::uint8_t* top = pixels + static_cast<std::size_t>(topRow) * rowBytes;
                std::uint8_t* bottom = pixels + static_cast<std::size_t>(h - 1 - topRow) * rowBytes;
                std::memcpy(scratch.data(), top, rowBytes);
                std::memcpy(top, bottom, rowBytes);
                std::memcpy(bottom, scratch.data(), rowBytes);
            }
        }
        return true;
    }

    std::unique_ptr<IRenderTargetCubeBackend> BgfxGraphicsBackend::CreateRenderTargetCube(int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        // mipMap (Task 907): real per-face mip chain, auto-regenerated by bgfx itself, same
        // mechanism as Task 906's RenderTarget2D fix.
        // multiSampleCount (Task 903): now wired up via BGFX_TEXTURE_RT_MSAA_Xn, mirroring
        // BgfxRenderTargetBackend's Task 878/879 treatment -- see BgfxRenderTargetCubeBackend's
        // constructor.
        return std::make_unique<BgfxRenderTargetCubeBackend>(this, size, depthFormat, mipMap, multiSampleCount);
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
        // REMED-GFX-065: this cube face's view (BindAsRenderTargetFace just (re)bound fbo to it) is the
        // base for its segment chain. Binding a new face rebuilds fbo, so re-point the tracker each time.
        ResetSegmentTarget(bgfxRt->viewId_, bgfxRt->fbo);
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
        bool sourceIsRenderTarget = false;
        if (samplable)
        {
            handle = samplable->GetBgfxTextureHandle();
            // REMED-GFX-067: a RenderTarget2D's FBO color memory is bottom-up on originBottomLeft
            // renderers; SubmitSprite flips its sampled V so it reads back upright.
            sourceIsRenderTarget = samplable->IsRenderTargetColorSource();
        }
        // Task 750: apply this Begin()'s SamplerState to slot 0 right before each submit,
        // mirroring EasyGL's/Vulkan's identical FlushBatch()-time ApplySamplerState() call.
        graphicsBackend.ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        graphicsBackend.SubmitSprite(handle, sourceIsRenderTarget, texture.GetWidth(), texture.GetHeight(),
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

    void BgfxSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
    {
        graphicsBackend.spriteTransform_ = m;
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
                litTextured3DVertexLitProgram_ = tryCreateProgram(kLitTextured3dVertexLitShaders,
                                                             "vs_lit_textured3d_vertexlit",
                                                             "fs_lit_textured3d_vertexlit",
                                                             "lit_textured3d_vertexlit");
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
                dualTextureColored3DProgram_ = tryCreateProgram(kDualTexture3dShaders,
                                                             "vs_dual_texture_colored3d",
                                                             "fs_dual_texture3d",
                                                             "dual_texture_colored3d");
                skinned3DProgram_         = tryCreateProgram(kSkinned3dShaders,
                                                             "vs_skinned3d",
                                                             "fs_skinned3d",
                                                             "skinned3d");
                skinned3DVertexLitProgram_ = tryCreateProgram(kSkinned3dVertexLitShaders,
                                                             "vs_skinned3d_vertexlit",
                                                             "fs_skinned3d_vertexlit",
                                                             "skinned3d_vertexlit");
                instanced3DProgram_       = tryCreateProgram(kInstanced3dShaders,
                                                             "vs_instanced3d",
                                                             "fs_instanced3d",
                                                             "instanced3d");
                envMap3DProgram_          = tryCreateProgram(kEnvMap3dShaders,
                                                             "vs_env_map3d",
                                                             "fs_env_map3d",
                                                             "env_map3d");
                // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: PbrEffect/SkinnedPbrEffect --
                // both vertex shaders share the one fs_pbr3d fragment shader (identical BRDF,
                // see compile_shaders.py's kPbr3dShaders comment).
                pbr3DProgram_             = tryCreateProgram(kPbr3dShaders,
                                                             "vs_pbr3d",
                                                             "fs_pbr3d",
                                                             "pbr3d");
                pbrSkinned3DProgram_      = tryCreateProgram(kPbr3dShaders,
                                                             "vs_pbr_skinned3d",
                                                             "fs_pbr3d",
                                                             "pbr_skinned3d");

                wvpUniform_         = bgfx::createUniform("u_wvp",            bgfx::UniformType::Mat4);
                depthBiasUnif_      = bgfx::createUniform("u_depthBias",      bgfx::UniformType::Vec4);
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
                weightsPerVertex3DUnif_ = bgfx::createUniform("u_weightsPerVertex", bgfx::UniformType::Vec4);
                vpInstanced3DUnif_  = bgfx::createUniform("u_vp",            bgfx::UniformType::Mat4);

                world3DUnif_         = bgfx::createUniform("u_world",          bgfx::UniformType::Mat4);
                normalMatrix3DUnif_  = bgfx::createUniform("u_normalMatrix",   bgfx::UniformType::Mat3);
                eyePos3DUnif_        = bgfx::createUniform("u_eyePos",         bgfx::UniformType::Vec4);
                emissiveColor3DUnif_ = bgfx::createUniform("u_emissiveColor",  bgfx::UniformType::Vec4);
                envMapAmountUnif_    = bgfx::createUniform("u_envMapAmount",   bgfx::UniformType::Vec4);
                envMapSpecularUnif_  = bgfx::createUniform("u_envMapSpecular", bgfx::UniformType::Vec4);
                envMapSampler_       = bgfx::createUniform("s_envMap",         bgfx::UniformType::Sampler);

                // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: PbrEffect/SkinnedPbrEffect uniforms.
                metallicRoughnessFactorUnif_ = bgfx::createUniform("u_metallicRoughnessFactor", bgfx::UniformType::Vec4);
                normalMapSampler_            = bgfx::createUniform("s_texNormal",             bgfx::UniformType::Sampler);
                metallicRoughnessSampler_    = bgfx::createUniform("s_texMetallicRoughness",  bgfx::UniformType::Sampler);
                emissiveMapSampler_          = bgfx::createUniform("s_texEmissive",           bgfx::UniformType::Sampler);
                occlusionMapSampler_         = bgfx::createUniform("s_texOcclusion",          bgfx::UniformType::Sampler);
                // REMED-GFX-078: per-slot render-target V-flip flags (see rtFlipV_ / BindSamplerSlot).
                rtFlipVUnif_                 = bgfx::createUniform("u_rtFlipV",               bgfx::UniformType::Vec4);

                // 1x1 opaque white fallback texture (Task 379) — sampled whenever a draw's
                // texture0 is null, matching EasyGL/Vulkan's identical fallback.
                const uint8_t whitePixel[4] = {255, 255, 255, 255};
                const bgfx::Memory* whiteMem = bgfx::copy(whitePixel, sizeof(whitePixel));
                defaultWhiteTexture3D_ = bgfx::createTexture2D(
                    1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                    BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, whiteMem);

                // plan_cnj.md CNB-58 (Phase 13A): fallback for PbrEffect::NormalMap when unbound
                // -- a "flat" tangent-space normal (0,0,1) encoded as RGB (128,128,255), matching
                // EasyGLGraphicsBackend::EnsureDefaultFlatNormalTexture()'s identical rationale.
                const uint8_t flatNormalPixel[4] = {128, 128, 255, 255};
                const bgfx::Memory* flatNormalMem = bgfx::copy(flatNormalPixel, sizeof(flatNormalPixel));
                defaultFlatNormalTexture3D_ = bgfx::createTexture2D(
                    1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                    BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, flatNormalMem);
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

    bool BgfxGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::OcclusionQuery:
                return (bgfx::getCaps()->supported & BGFX_CAPS_OCCLUSION_QUERY) != 0;
            default:
                return true;
        }
    }

    BgfxGraphicsBackend::~BgfxGraphicsBackend()
    {
        auto destroyU = [](bgfx::UniformHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
        auto destroyP = [](bgfx::ProgramHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };

        destroyU(wvpUniform_);
        destroyU(depthBiasUnif_);
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
        destroyU(weightsPerVertex3DUnif_);
        destroyU(vpInstanced3DUnif_);
        destroyU(world3DUnif_);
        destroyU(normalMatrix3DUnif_);
        destroyU(eyePos3DUnif_);
        destroyU(emissiveColor3DUnif_);
        destroyU(envMapAmountUnif_);
        destroyU(envMapSpecularUnif_);
        destroyU(envMapSampler_);
        destroyU(metallicRoughnessFactorUnif_);
        destroyU(normalMapSampler_);
        destroyU(metallicRoughnessSampler_);
        destroyU(emissiveMapSampler_);
        destroyU(occlusionMapSampler_);
        if (bgfx::isValid(defaultWhiteTexture3D_)) { bgfx::destroy(defaultWhiteTexture3D_); defaultWhiteTexture3D_ = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(defaultFlatNormalTexture3D_)) { bgfx::destroy(defaultFlatNormalTexture3D_); defaultFlatNormalTexture3D_ = BGFX_INVALID_HANDLE; }
        destroyP(colored3DProgram_);
        destroyP(textured3DProgram_);
        destroyP(coloredTextured3DProgram_);
        destroyP(litTextured3DProgram_);
        destroyP(litTextured3DVertexLitProgram_);
        destroyP(alphaTest3DProgram_);
        destroyP(alphaTestColoredTextured3DProgram_);
        destroyP(dualTexture3DProgram_);
        destroyP(dualTextureColored3DProgram_);
        destroyP(skinned3DProgram_);
        destroyP(skinned3DVertexLitProgram_);
        destroyP(instanced3DProgram_);
        destroyP(envMap3DProgram_);
        destroyP(pbr3DProgram_);
        destroyP(pbrSkinned3DProgram_);
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
        const uint16_t fullViewWidth  = (spriteViewId != 0 && currentRtWidth_  > 0) ? currentRtWidth_  : cachedWidth;
        const uint16_t fullViewHeight = (spriteViewId != 0 && currentRtHeight_ > 0) ? currentRtHeight_ : cachedHeight;

        // REMED-GFX-072: honor a custom GraphicsDevice.Viewport for the sprite view. XNA/FNA build
        // the SpriteBatch ortho from Viewport.Width/Height (CreateOrthographicOffCenter(0,
        // Viewport.Width, Viewport.Height, 0)) with a rasterizer viewport at Viewport.X/Y/W/H, so a
        // sprite at viewport-local (0,0) lands at the target pixel (Viewport.X, Viewport.Y) at 1:1
        // pixel scale. In bgfx the view rect is BOTH the rasterizer viewport AND the region that
        // setViewClear() clears, so we CANNOT shrink the sprite view rect to the sub-region without
        // scoping Clear() to it (Clear must clear the whole target -- XNA semantics). Instead we keep
        // the view rect full and use an OFFSET ortho: screen = Viewport.XY + local, which reproduces
        // the exact XNA placement and 1:1 scale over the full target. The viewport is the one captured
        // at sprite-submit time (spriteVp*, see SubmitSprite), so it survives a viewport restore
        // before Present/readback. Default full-target viewport keeps the prior full-target ortho.
        // Deviation vs a true rasterizer viewport: a sprite whose local coordinates exceed the
        // Viewport is not hard-clipped at the Viewport edge (only at the target edge); acceptable
        // because SpriteBatch content is authored in viewport-local space. Multiple differing
        // viewports on one view in one frame remain last-wins (REMED-GFX-065).
        const bool spriteCustomVp = spriteVpValid_ && spriteVpSet_ && spriteVpW_ > 0 && spriteVpH_ > 0 &&
            (spriteVpX_ != 0 || spriteVpY_ != 0 || spriteVpW_ != fullViewWidth || spriteVpH_ != fullViewHeight);

        bgfx::setViewRect(spriteViewId, 0, 0, fullViewWidth, fullViewHeight);

        float ortho[16];
        if (spriteCustomVp)
        {
            const float vx = static_cast<float>(spriteVpX_);
            const float vy = static_cast<float>(spriteVpY_);
            bx::mtxOrtho(ortho, -vx, static_cast<float>(fullViewWidth) - vx,
                         static_cast<float>(fullViewHeight) - vy, -vy, 0.0f, 1000.0f, 0.0f,
                         bgfx::getCaps()->homogeneousDepth);
        }
        else
        {
            bx::mtxOrtho(ortho, 0.0f, static_cast<float>(fullViewWidth), static_cast<float>(fullViewHeight),
                         0.0f, 0.0f, 1000.0f, 0.0f,
                         bgfx::getCaps()->homogeneousDepth);
        }
        // Task 808: fold in SpriteBatch::Begin()'s transformMatrix (identity when not explicitly
        // set, so this is a no-op for ordinary 3D draws sharing this same view -- 3D draws supply
        // their own world-view-projection via a per-draw uniform and never read bgfx's built-in
        // view/proj, so this view-level transform only ever affects the embedded sprite shader's
        // u_viewProj). bx's own vector convention is ROW-vector (bx::vec4MulMtx documents
        // "row vector _vec by matrix _mat", i.e. v' = v * M) -- matches EasyGL's own
        // `transform_ * orthoM` combined-matrix semantics exactly (apply the caller's transform
        // first, then project): bx::mtxMul(result, a, b) computes result = a * b, so the combined
        // matrix for v' = v * transform * ortho is orthoWithTransform = transformColMajor * ortho.
        float transformColMajor[16];
        spriteTransform_.ToColumnMajor(transformColMajor);
        float orthoWithTransform[16];
        bx::mtxMul(orthoWithTransform, transformColMajor, ortho);
        bgfx::setViewTransform(spriteViewId, nullptr, orthoWithTransform);
        // REMED-GFX-018: the active ordered view owns its exact clear mask. Segment ids no longer
        // imply CLEAR_NONE: a Clear after a draw is itself recorded on a segment view, while a
        // viewport/transform-only segment explicitly owns CLEAR_NONE. Keeping the mask alongside
        // the current view also prevents later EnsureViewState calls from widening a colour-only,
        // depth-only, or stencil-only request.
        bgfx::setViewClear(spriteViewId, currentViewClearFlags_,
                           clearRgba, clearDepthValue_, clearStencilValue_);
    }

    void BgfxGraphicsBackend::RecordClear(uint16_t clearFlags)
    {
        // A clear is a full-target ordered operation. The first operation on a target may use its
        // base view; every later clear needs a fresh monotonically-increasing segment so bgfx
        // cannot move it before earlier draws or collapse two clear masks/values last-wins.
        const bool useBaseView = !segmentActive_ && !segmentNeedsFreshView_;
        if (useBaseView)
        {
            currentViewId_ = spriteViewId = segmentTargetBaseId_;
            segCurIsBase_ = true;
            if (segmentTargetBaseId_ < segmentBaseUsed_.size())
                segmentBaseUsed_[segmentTargetBaseId_] = true;
        }
        else
        {
            const bgfx::ViewId segmentId = AllocateSegmentViewId();
            bgfx::setViewFrameBuffer(segmentId, segmentTargetFbo_);
            currentViewId_ = spriteViewId = segmentId;
            segCurIsBase_ = false;
        }

        segmentActive_ = true;
        segmentNeedsFreshView_ = false;
        segCurHasVp_ = false; // Clear ignores GraphicsDevice.Viewport and always owns the full target.
        segCurX_ = segCurY_ = segCurW_ = segCurH_ = 0;
        segCurSpriteTransformValid_ = false;
        currentViewClearFlags_ = clearFlags;

        EnsureViewState();
        bgfx::touch(spriteViewId);
    }

    void BgfxGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        RecordClear(BGFX_CLEAR_COLOR);
    }

    void BgfxGraphicsBackend::Present()
    {
        // If the current target was rebound after its base view had already been consumed this
        // frame, configuring that base merely for Present would retroactively clobber the earlier
        // operation. The next real draw/clear will allocate its required fresh segment.
        if (!segmentNeedsFreshView_)
            EnsureViewState();
        bgfx::frame();
        spriteVpValid_ = false; // REMED-GFX-072: sprite viewport is per-frame; clear for the next one.
        EndFrameSegments();     // REMED-GFX-065: recycle the per-frame viewport-segment view ids.
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

    void BgfxGraphicsBackend::SubmitSprite(bgfx::TextureHandle textureHandle, bool sourceIsRenderTarget,
                                           int texWidth, int texHeight,
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

        // REMED-GFX-072: capture the GraphicsDevice.Viewport active for THIS sprite before
        // EnsureViewState sizes/places the sprite view, so a custom sub-Viewport survives a viewport
        // restore before Present/readback (bgfx applies view state at frame(); see spriteVp* decl).
        spriteVpValid_ = true;
        spriteVpSet_ = viewportSet_;
        spriteVpX_ = viewportX_; spriteVpY_ = viewportY_;
        spriteVpW_ = viewportW_; spriteVpH_ = viewportH_;

        // REMED-GFX-065: pick/allocate this sprite's ordered viewport segment BEFORE EnsureViewState so
        // its full-rect + offset-ortho land on the segment view belonging to this batch's viewport.
        // REMED-GFX-084: spritePath=true also segments on the batch's view transform (spriteTransform_),
        // which EnsureViewState is about to bake into this view's view-global setViewTransform.
        SelectViewportSegment(true);

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

        // REMED-GFX-067: on originBottomLeft renderers (OpenGL/GLES/WebGL) a render target's color
        // attachment stores its texel memory bottom-up, so sampling it with the ordinary top-down V
        // yields a vertically-mirrored image (an ordinary Texture2D is top-down and unaffected).
        // Flip V for render-target sources so RenderTarget2D content blits back upright — matching
        // every other backend's public XNA top-left orientation. Composes with FlipVertically above
        // (two swaps cancel, preserving the user's requested flip). No-op on Vulkan/D3D/Metal
        // (originBottomLeft == false), where the FBO memory is already top-down.
        if (sourceIsRenderTarget && bgfx::getCaps()->originBottomLeft)
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
        // Task 768: gated on scissorEnabled_, not just a non-zero rect -- see that member's own
        // declaration comment for why the two must be tracked independently.
        ApplyScissorOverride();
        bgfx::setState(colorWriteFlags_ | BGFX_STATE_MSAA
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

    // Task 923: XNA BlendFunction enum -> bgfx blend-equation state bits (mirrors Task 868's own
    // Vulkan ToVkBlendOp mapping exactly): Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4
    static uint64_t XnaBlendFunctionToBgfxEquation(int blendFunc)
    {
        switch (blendFunc)
        {
        case 1:  return BGFX_STATE_BLEND_EQUATION_SUB;
        case 2:  return BGFX_STATE_BLEND_EQUATION_REVSUB;
        case 3:  return BGFX_STATE_BLEND_EQUATION_MAX;
        case 4:  return BGFX_STATE_BLEND_EQUATION_MIN;
        default: return BGFX_STATE_BLEND_EQUATION_ADD;
        }
    }

    void BgfxGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 → Opaque preset (all 4 factors): src=One, dst=Zero → no blend
        if (colorSrcBlend == 0 && colorDstBlend == 1 &&
            alphaSrcBlend == 0 && alphaDstBlend == 1)
        {
            blendFlags_ = 0;
        }
        else
        {
            // Task 923: previously alphaSrcBlend/alphaDstBlend were unused parameters (the alpha
            // channel silently reused the colour channel's own factors via BGFX_STATE_BLEND_FUNC),
            // and colorBlendFunc/alphaBlendFunc were entirely ignored (always implicitly Add).
            blendFlags_ = BGFX_STATE_BLEND_FUNC_SEPARATE(
                              XnaBlendToBgfxFactor(colorSrcBlend),
                              XnaBlendToBgfxFactor(colorDstBlend),
                              XnaBlendToBgfxFactor(alphaSrcBlend),
                              XnaBlendToBgfxFactor(alphaDstBlend))
                        | BGFX_STATE_BLEND_EQUATION_SEPARATE(
                              XnaBlendFunctionToBgfxEquation(colorBlendFunc),
                              XnaBlendFunctionToBgfxEquation(alphaBlendFunc));
        }
        // REMED-GFX-077: BlendState.ColorWriteChannels (slot 0) → the per-draw BGFX_STATE_WRITE_*
        // bits (bgfx colour write is a single global-per-draw mask; BGFX_STATE_WRITE_{R,G,B,A} =
        // 0x1/0x2/0x4/0x8 = the XNA ColorWriteChannels bit layout). bgfx has NO per-attachment
        // colour write, so independent MRT masks (ColorWriteChannels1/2/3) are not representable
        // (documented capability gap → REMED-GFX-085), and bgfx has no per-draw sample-coverage
        // mask, so BlendState.MultiSampleMask is not representable either (documented gap).
        const int cw = writeState.colorWriteChannels[0];
        uint64_t cwf = 0;
        if (ColorWriteHasRed  (cw)) cwf |= BGFX_STATE_WRITE_R;
        if (ColorWriteHasGreen(cw)) cwf |= BGFX_STATE_WRITE_G;
        if (ColorWriteHasBlue (cw)) cwf |= BGFX_STATE_WRITE_B;
        if (ColorWriteHasAlpha(cw)) cwf |= BGFX_STATE_WRITE_A;
        colorWriteFlags_ = cwf;
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

        // Task 764: cache every stencil parameter except the reference value itself, so a later
        // standalone SetReferenceStencil() call can rebuild stencilFront_/stencilBack_ without
        // requiring a full ApplyDepthStencilState re-application (mirrors Vulkan's
        // referenceStencil_ member, Task 870/319).
        stencilEnableCached_       = stencilEnable;
        stencilFuncCached_         = stencilFunc;
        stencilPassCached_         = stencilPass;
        stencilFailCached_         = stencilFail;
        stencilDepthFailCached_    = stencilDepthFail;
        stencilMaskCached_         = stencilMask;
        stencilWriteMaskCached_    = stencilWriteMask;
        twoSidedStencilModeCached_ = twoSidedStencilMode;
        ccwStencilFuncCached_      = ccwStencilFunc;
        ccwStencilPassCached_      = ccwStencilPass;
        ccwStencilFailCached_      = ccwStencilFail;
        ccwStencilDepthFailCached_ = ccwStencilDepthFail;
        referenceStencilCached_    = referenceStencil;
        RebuildStencilState();
    }

    void BgfxGraphicsBackend::RebuildStencilState()
    {
        if (stencilEnableCached_)
        {
            if (twoSidedStencilModeCached_)
            {
                // Task 763 empirical finding, mirrors Task 870's identical Vulkan fix: this
                // backend never sets BGFX_STATE_FRONT_CCW in ApplyRasterizerState, so bgfx's own
                // default glFrontFace is GL_CW (see bgfx's renderer_gl.cpp) -- the OPPOSITE of
                // EasyGL's effective convention (EasyGL never overrides GL's own hardware default
                // of GL_CCW-is-front). CullMode itself is unaffected by this (BGFX_STATE_CULL_CW/
                // CCW already cull the correct raw winding regardless of glFrontFace, verified by
                // this whole project's existing cull-mode test suite) -- only bgfx::setStencil's
                // own front/back split follows raw glFrontFace directly, with no equivalent
                // compensating indirection. Confirmed via a genuinely differential
                // stencil_twosided test (a back-facing triangle's CounterClockwiseStencilFunction/
                // Fail must apply and did not until front/back were swapped here). Swapped
                // pragmatically so XNA's "front"/"CounterClockwise" stencil settings land on
                // whichever bgfx slot the GPU actually evaluates for each raw winding.
                stencilFront_ = BuildBgfxStencil(ccwStencilFuncCached_, ccwStencilPassCached_,
                                                 ccwStencilFailCached_, ccwStencilDepthFailCached_,
                                                 stencilMaskCached_, stencilWriteMaskCached_,
                                                 referenceStencilCached_);
                stencilBack_ = BuildBgfxStencil(stencilFuncCached_, stencilPassCached_,
                                                stencilFailCached_, stencilDepthFailCached_,
                                                stencilMaskCached_, stencilWriteMaskCached_,
                                                referenceStencilCached_);
            }
            else
            {
                stencilFront_ = BuildBgfxStencil(stencilFuncCached_, stencilPassCached_,
                                                 stencilFailCached_, stencilDepthFailCached_,
                                                 stencilMaskCached_, stencilWriteMaskCached_,
                                                 referenceStencilCached_);
                stencilBack_ = stencilFront_;
            }
        }
        else
        {
            stencilFront_ = BGFX_STENCIL_NONE;
            stencilBack_  = BGFX_STENCIL_NONE;
        }
    }

    void BgfxGraphicsBackend::SetReferenceStencil(int value)
    {
        referenceStencilCached_ = value;
        RebuildStencilState();
    }

    void BgfxGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                    bool scissorTestEnable,
                                                    float depthBias,
                                                    float /*slopeScaleDepthBias*/)
    {
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        switch (cullMode)
        {
        case 1:  cullFlags_ = BGFX_STATE_CULL_CW;  break;
        case 2:  cullFlags_ = BGFX_STATE_CULL_CCW; break;
        default: cullFlags_ = 0;                    break;
        }
        // Task 768: scissorEnabled_ is a genuinely independent flag, NOT derived from whether the
        // rect happens to be non-zero -- SetScissorRect() can be called before or after this, in
        // either order, and must not silently re-enable a disabled scissor test (see
        // scissorEnabled_'s own declaration comment).
        scissorEnabled_ = scissorTestEnable;
        // FillMode: Solid=0, WireFrame=1 (Task 766; see ExpandWireframeIndices).
        wireframe_ = (fillMode == 1);
        // Task 767: bgfx has no native polygon-offset mechanism, so DepthBias is emulated via a
        // per-draw vertex-shader Z-offset (see u_depthBias in every 3D vertex shader source).
        // SlopeScaleDepthBias is deliberately NOT emulated (project-owner decision, 2026-07-10)
        // and remains a documented, separately-tracked gap -- see depthBias_'s own declaration.
        depthBias_ = depthBias;
    }

    bool BgfxGraphicsBackend::ExpandWireframeIndices(const BgfxIndexBufferBackend* ib,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      int startIndex, int baseVertex,
                                                      int firstVertex,
                                                      bgfx::TransientIndexBuffer& outTib)
    {
        // Only triangle geometry needs expanding; line/point primitives are already "wireframe".
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip)
            return false;
        if (primitiveCount <= 0) return false;

        auto readSrc = [&](int pos) -> uint32_t {
            if (!ib) return static_cast<uint32_t>(firstVertex + pos);
            const auto& bytes = ib->cpuData;
            if (ib->IsThirtyTwoBit()) {
                uint32_t v;
                std::memcpy(&v, bytes.data() + static_cast<std::size_t>(startIndex + pos) * 4, 4);
                return v + static_cast<uint32_t>(baseVertex);
            }
            uint16_t v;
            std::memcpy(&v, bytes.data() + static_cast<std::size_t>(startIndex + pos) * 2, 2);
            return static_cast<uint32_t>(v) + static_cast<uint32_t>(baseVertex);
        };

        const int edgeCount = primitiveCount * 3; // 3 edges per triangle, 2 indices per edge
        if (bgfx::getAvailTransientIndexBuffer(static_cast<uint32_t>(edgeCount * 2), true) <
            static_cast<uint32_t>(edgeCount * 2))
            return false;
        bgfx::allocTransientIndexBuffer(&outTib, static_cast<uint32_t>(edgeCount * 2), true);
        auto* dst = reinterpret_cast<uint32_t*>(outTib.data);

        int w = 0;
        auto edge = [&](uint32_t a, uint32_t b) { dst[w++] = a; dst[w++] = b; };
        if (primitive == PrimitiveType::TriangleList) {
            for (int t = 0; t < primitiveCount; ++t) {
                const uint32_t a = readSrc(3 * t), b = readSrc(3 * t + 1), c = readSrc(3 * t + 2);
                edge(a, b); edge(b, c); edge(c, a);
            }
        } else { // TriangleStrip: primitiveCount triangles over primitiveCount+2 vertices
            for (int t = 0; t < primitiveCount; ++t) {
                const uint32_t a = readSrc(t), b = readSrc(t + 1), c = readSrc(t + 2);
                edge(a, b); edge(b, c); edge(c, a);
            }
        }
        return true;
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
        // submit -- for RENDER-TARGET views as well as the backbuffer since REMED-GFX-063.
        // Depth range is an acceptable Bgfx deviation -- bgfx has no per-view depth range knob
        // equivalent to VkViewport's minDepth/maxDepth (mirrors SetVirtualResolution's documented
        // no-op precedent for parameters Bgfx has no matching concept for).
        if (w <= 0 || h <= 0) { viewportW_ = viewportH_ = 0; viewportSet_ = false; return; }
        viewportX_   = static_cast<uint16_t>(x);
        viewportY_   = static_cast<uint16_t>(y);
        viewportW_   = static_cast<uint16_t>(w);
        viewportH_   = static_cast<uint16_t>(h);
        viewportSet_ = true;
    }

    // REMED-GFX-065: allocate the next ordered per-frame viewport-segment view id. Monotonic so bgfx's
    // ascending-view-id execution order == CNA submission order for a target's segments. Recycled every
    // frame by EndFrameSegments(). GFX-018 also uses this pool for ordered clear operations and target
    // rebinds. Exhausting the reserved [kFirstSegmentViewId, 255) range throws a clear error rather than
    // silently wrapping (which would corrupt an earlier view's state).
    bgfx::ViewId BgfxGraphicsBackend::AllocateSegmentViewId()
    {
        if (segmentNextId_ >= Detail::kBackbufferFlushViewId)
            throw std::runtime_error(
                "Bgfx: exhausted per-frame ordered view-segment ids (more than 63 viewport/"
                "transform/clear/target-rebind segments in one frame -- REMED-GFX-065/GFX-018)");
        return segmentNextId_++;
    }

    // REMED-GFX-065: repoint the segment tracker at a newly-bound target. The next draw starts from this
    // target's base view (segmentActive_ = false); a later viewport change on it consumes a segment id.
    void BgfxGraphicsBackend::ResetSegmentTarget(bgfx::ViewId baseId, bgfx::FrameBufferHandle fbo)
    {
        segmentTargetBaseId_ = baseId;
        segmentTargetFbo_    = fbo;
        segmentActive_       = false;
        segmentNeedsFreshView_ =
            baseId < segmentBaseUsed_.size() && segmentBaseUsed_[baseId];
        currentViewId_ = spriteViewId = baseId;
        currentViewClearFlags_ = BGFX_CLEAR_NONE;
    }

    // REMED-GFX-065: recycle the per-frame segment id pool and fall back to the current target's base
    // view. Called right after every bgfx::frame() (Present / ReadBackbuffer), so next frame's first
    // draw on the still-bound target starts from its base view again.
    void BgfxGraphicsBackend::EndFrameSegments()
    {
        segmentNextId_  = Detail::kFirstSegmentViewId;
        segmentActive_  = false;
        segmentNeedsFreshView_ = false;
        segmentBaseUsed_.fill(false);
        currentViewId_  = segmentTargetBaseId_;
        spriteViewId    = segmentTargetBaseId_;
        currentViewClearFlags_ = BGFX_CLEAR_NONE;
    }

    // REMED-GFX-065: resolve whether the active GraphicsDevice.Viewport is a genuine CUSTOM sub-region
    // of the current target (vs the full-target/default viewport), and hand back the target's full pixel
    // size. Mirrors EnsureViewState()'s own `spriteCustomVp` test exactly: a full-target viewport (what
    // GraphicsDevice resets on every SetRenderTarget) must NOT be treated as custom, else every ordinary
    // draw would needlessly segment. RT size when a target is bound (base id != 0), else backbuffer size.
    bool BgfxGraphicsBackend::CurrentCustomViewport(uint16_t& fullW, uint16_t& fullH) const
    {
        fullW = (segmentTargetBaseId_ != 0 && currentRtWidth_  > 0) ? currentRtWidth_  : cachedWidth;
        fullH = (segmentTargetBaseId_ != 0 && currentRtHeight_ > 0) ? currentRtHeight_ : cachedHeight;
        return viewportSet_ && viewportW_ > 0 && viewportH_ > 0 &&
               (viewportX_ != 0 || viewportY_ != 0 || viewportW_ != fullW || viewportH_ != fullH);
    }

    // REMED-GFX-065: pick the view id this draw/batch submits to, preserving XNA draw order across
    // multiple viewports on one target in one frame. The FIRST viewport on a target uses its BASE view
    // (view 0 / RT id) exactly as before this task -- a custom viewport still shrinks that base view's
    // rect (REMED-GFX-063), so a single-viewport frame is byte-identical to pre-GFX-065 (one view, no
    // extra state). Only a SECOND, DIFFERENT viewport in the same frame routes to a freshly-allocated
    // ordered segment view (same framebuffer, higher id so bgfx's ascending-view-id execution keeps
    // submission order). GFX-018 gives every public Clear its own full-target ordered view, so draw-only
    // viewport/transform segments load and preserve ALL attachments. Consecutive draws sharing the
    // active view's viewport reuse it (no id churn). Once off the base we never return to it.
    void BgfxGraphicsBackend::SelectViewportSegment(bool spritePath)
    {
        uint16_t fullW = 0, fullH = 0;
        const bool hasVp = CurrentCustomViewport(fullW, fullH);

        // REMED-GFX-084: for a SpriteBatch submission the batch's Begin(transformMatrix) is baked into
        // the view-GLOBAL setViewTransform by EnsureViewState (orthoWithTransform = spriteTransform_ *
        // ortho), resolved once at bgfx::frame() -> last-write-wins. So the sprite view transform is part
        // of the view-global state a segment owns, exactly like the viewport rect: two same-viewport
        // batches with different transforms must land on DIFFERENT ordered segments. The 3D path never
        // programs setViewTransform (it supplies its own world-view-projection per draw via a uniform and
        // ignores bgfx's built-in view/proj), so a 3D draw neither keys on nor commits the sprite
        // transform (segCurSpriteTransformValid_ stays false, letting a following sprite adopt the view).
        // Because the viewport rect is also part of the segment key, keying on the raw spriteTransform_
        // is exactly equivalent to keying on the effective orthoWithTransform (ortho is a pure function
        // of the viewport + full size, and is invertible): identical (viewport, transform) <=> identical
        // effective view matrix. Matrix::operator== is exact per-component float equality, so a default
        // Begin() (Matrix::Identity) and an explicit Matrix::Identity reuse; a genuinely different
        // transform does not (no epsilon merging of meaningfully different transforms).
        auto commitSpriteTransform = [&] {
            if (spritePath) { segCurSpriteTransformValid_ = true; segCurSpriteTransform_ = spriteTransform_; }
            else            { segCurSpriteTransformValid_ = false; }
        };

        if (!segmentActive_)
        {
            // First draw/batch on this binding normally uses the base view. On a same-frame target
            // rebind, however, that base has already recorded an earlier operation and must not be
            // reconfigured last-wins; continue on a fresh ordered segment instead (GFX-018).
            if (segmentNeedsFreshView_)
            {
                const bgfx::ViewId segmentId = AllocateSegmentViewId();
                bgfx::setViewFrameBuffer(segmentId, segmentTargetFbo_);
                currentViewId_ = spriteViewId = segmentId;
                segCurIsBase_ = false;
            }
            else
            {
                currentViewId_ = spriteViewId = segmentTargetBaseId_;
                segCurIsBase_ = true;
                if (segmentTargetBaseId_ < segmentBaseUsed_.size())
                    segmentBaseUsed_[segmentTargetBaseId_] = true;
            }

            segmentActive_ = true;
            segmentNeedsFreshView_ = false;
            segCurHasVp_   = hasVp;
            segCurX_ = viewportX_; segCurY_ = viewportY_; segCurW_ = viewportW_; segCurH_ = viewportH_;
            currentViewClearFlags_ = BGFX_CLEAR_NONE;
            bgfx::setViewRect(currentViewId_, 0, 0, fullW, fullH);
            bgfx::setViewClear(currentViewId_, BGFX_CLEAR_NONE,
                               clearRgba, clearDepthValue_, clearStencilValue_);
            commitSpriteTransform();
            return;
        }

        const bool sameVp = (segCurHasVp_ == hasVp) &&
            (!hasVp || (segCurX_ == viewportX_ && segCurY_ == viewportY_ &&
                        segCurW_ == viewportW_ && segCurH_ == viewportH_));

        // Sprite-transform compatibility (only for the sprite path): the active view is compatible if no
        // sprite has committed a transform to it yet (it can adopt this batch's) or its committed
        // transform equals this batch's. A 3D reuse never consults or changes this.
        bool transformCompatible = true;
        if (spritePath && segCurSpriteTransformValid_)
            transformCompatible = (segCurSpriteTransform_ == spriteTransform_);

        if (sameVp && transformCompatible)
        {
            // Reuse the active view (base or segment) -> no allocation. If it has no committed sprite
            // transform yet (e.g. established by a 3D draw), adopt this batch's now so a later differing
            // transform on the same viewport starts a new ordered segment.
            if (spritePath && !segCurSpriteTransformValid_)
            { segCurSpriteTransformValid_ = true; segCurSpriteTransform_ = spriteTransform_; }
            return;
        }

        // A different viewport OR (sprite path) a different view transform appeared on this target within
        // the frame -> next ordered segment view.
        const bgfx::ViewId segId = AllocateSegmentViewId();
        bgfx::setViewFrameBuffer(segId, segmentTargetFbo_);
        // Default to the target's FULL rect (the sprite path keeps this + an offset ortho; a custom 3D
        // viewport shrinks it in ApplyViewportOverride). This is a draw-only view and therefore LOADS
        // every attachment. GFX-018 records full-target clears on their own ordered views, so viewport
        // segmentation no longer needs (and must not perform) an implicit depth/stencil clear that
        // could destroy state the public ClearOptions mask was required to preserve.
        bgfx::setViewRect(segId, 0, 0, fullW, fullH);
        currentViewClearFlags_ = BGFX_CLEAR_NONE;
        bgfx::setViewClear(segId, BGFX_CLEAR_NONE,
                           clearRgba, clearDepthValue_, clearStencilValue_);
        currentViewId_ = spriteViewId = segId;
        segCurIsBase_  = false;
        segCurHasVp_   = hasVp;
        segCurX_ = viewportX_; segCurY_ = viewportY_; segCurW_ = viewportW_; segCurH_ = viewportH_;
        commitSpriteTransform();
    }

    void BgfxGraphicsBackend::ApplyViewportOverride()
    {
        // REMED-GFX-065: pick/allocate this draw's ordered viewport segment BEFORE setting the rect, so a
        // second viewport's sub-rect lands on its OWN segment view rather than clobbering the first
        // viewport's draws on a shared view. For the first viewport this is still the base view (shrunk
        // here just like pre-GFX-063). REMED-GFX-084: spritePath=false -- the 3D path never programs
        // setViewTransform, so it keys on the viewport only and does not touch the sprite-transform key.
        SelectViewportSegment(false);

        // REMED-GFX-063: a custom sub-region Viewport applies to render-target views too, not only the
        // backbuffer. bgfx view rect is PER-VIEW state; SelectViewportSegment() has pointed currentViewId_
        // at this viewport's own view, so setting the sub-rect here no longer clobbers an earlier
        // viewport's draws (REMED-GFX-065). A full/default viewport keeps the view's full rect. Top-left
        // origin, no Y-flip (bgfx normalizes the renderer origin). Viewport.MinDepth/MaxDepth remain an
        // accepted Bgfx deviation (no per-view depth-range knob -- see SetViewport()).
        uint16_t fullW = 0, fullH = 0;
        if (CurrentCustomViewport(fullW, fullH))
            bgfx::setViewRect(currentViewId_, viewportX_, viewportY_, viewportW_, viewportH_);
    }

    void BgfxGraphicsBackend::ApplyScissorOverride()
    {
        // Task 768: bgfx::setScissor is per-draw-call state (bgfx::RenderDraw::m_scissor resets
        // to "no scissor" for every draw unless set again) -- must be called before every 3D
        // submit(). Gated on scissorEnabled_ (not just a non-zero rect -- see that member's own
        // declaration comment for why the two must be tracked independently).
        if (scissorEnabled_ && scissorW_ > 0 && scissorH_ > 0)
            bgfx::setScissor(scissorX_, scissorY_, scissorW_, scissorH_);
    }

    void BgfxGraphicsBackend::ApplySamplerState(int slot, int filter,
                                                 int addressU, int addressV,
                                                 int /*maxAnisotropy*/)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        uint32_t flags = 0;
        // TextureFilter → bgfx sampler flags. XNA: Linear=0, Point=1, Anisotropic=2,
        // LinearMipPoint=3, PointMipLinear=4, MinLinearMagPointMipLinear=5,
        // MinLinearMagPointMipPoint=6, MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8.
        // Task 743 finding: cases 3-8 previously all fell through to the `default` (plain linear)
        // branch, silently ignoring the Min/Mag/Point-vs-Linear split entirely -- unlike
        // EasyGLGraphicsBackend::ApplySamplerState, which already maps all 9 values correctly via
        // GL's combined min-filter enum. bgfx's 3 independent per-axis bits (MIN/MAG/MIP_POINT)
        // let every value be represented exactly, more directly than GL's combined enum approach.
        switch (filter)
        {
        case 1:  // Point
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
            break;
        case 2:  // Anisotropic — bgfx handles via ANISOTROPIC flag
            flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
            break;
        case 3:  // LinearMipPoint: Min=Linear, Mag=Linear, Mip=Point
            flags |= BGFX_SAMPLER_MIP_POINT;
            break;
        case 4:  // PointMipLinear: Min=Point, Mag=Point, Mip=Linear
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
            break;
        case 5:  // MinLinearMagPointMipLinear: Min=Linear, Mag=Point, Mip=Linear
            flags |= BGFX_SAMPLER_MAG_POINT;
            break;
        case 6:  // MinLinearMagPointMipPoint: Min=Linear, Mag=Point, Mip=Point
            flags |= BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
            break;
        case 7:  // MinPointMagLinearMipLinear: Min=Point, Mag=Linear, Mip=Linear
            flags |= BGFX_SAMPLER_MIN_POINT;
            break;
        case 8:  // MinPointMagLinearMipPoint: Min=Point, Mag=Linear, Mip=Point
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MIP_POINT;
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

    void BgfxGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        clearDepthValue_ = depth;
        RecordClear(static_cast<uint16_t>(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH));
    }

    void BgfxGraphicsBackend::ClearDepth(float depth)
    {
        clearDepthValue_ = depth;
        RecordClear(BGFX_CLEAR_DEPTH);
    }

    void BgfxGraphicsBackend::ClearStencil(int stencil)
    {
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(BGFX_CLEAR_STENCIL);
    }

    void BgfxGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        clearDepthValue_ = depth;
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(static_cast<uint16_t>(BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL));
    }

    void BgfxGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(static_cast<uint16_t>(BGFX_CLEAR_COLOR | BGFX_CLEAR_STENCIL));
    }

    void BgfxGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        clearDepthValue_ = depth;
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(static_cast<uint16_t>(
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL));
    }

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
        else if (stride == 48)
        {
            // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: VertexPositionNormalTangentTexture
            // (PbrEffect): pos(3f=12) + normal(3f=12) + tangent(4f=16, xyz + bitangent
            // handedness in w) + uv(2f=8) -- see EasyGLGraphicsBackend.cpp's own "case 48"
            // comment for the full cross-backend-duplication note.
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Tangent,   4, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
        }
        else if (stride == 56)
        {
            // CNB-67 (Phase 13C) Bgfx port: the stride-52 SkinnedVertex layout with a per-vertex
            // Color (normalized ubyte4) appended at the end (offset 52), matching
            // EasyGLGraphicsBackend.cpp's own "case 56" comment exactly.
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Weight,    4, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Indices,   4, bgfx::AttribType::Uint8);
            layout.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true);
        }
        else if (stride == 68)
        {
            // PBR + skinning combo: VertexPositionNormalTangentTextureSkinned -- the stride-48
            // Position+Normal+Tangent+TextureCoordinate layout with the stride-52/56 skinning
            // suffix (BlendWeight, BlendIndices) appended, matching
            // EasyGLGraphicsBackend.cpp's own "case 68" comment exactly.
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Tangent,   4, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Weight,    4, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Indices,   4, bgfx::AttribType::Uint8);
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
        : capacity_(capacity)
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
        const bool layoutChanged = stride_in_bytes != stride;
        if (layoutChanged)
        {
            stride = stride_in_bytes;
            layout = MakeBgfxLayout(stride_in_bytes);
        }

        // REMED-GFX-109: bgfx records resource updates in the frame's pre-command buffer, then
        // executes the complete pre-command buffer before submitting any draw. Consequently
        // update(A handle, B bytes) after a draw that captured A's handle overwrites the bytes
        // observed by that earlier draw. Rotate only when the current native version has already
        // been referenced by a draw (or when its layout changes); multiple updates before a draw
        // and multiple draws between updates keep one version.
        if (layoutChanged || submittedSinceUpdate_ || !bgfx::isValid(handle))
        {
            const auto replacement = bgfx::createDynamicVertexBuffer(
                static_cast<uint32_t>(capacity_),
                layout,
                BGFX_BUFFER_ALLOW_RESIZE);
            if (!bgfx::isValid(replacement))
                throw std::runtime_error(
                    "Bgfx: failed to allocate an immutable vertex-buffer update version.");
            const auto previous = handle;
            handle = replacement;
            if (bgfx::isValid(previous)) bgfx::destroy(previous);
        }
        if (!bgfx::isValid(handle) || !data || vertex_count <= 0) return;
        const uint32_t byteSize = static_cast<uint32_t>(vertex_count) * static_cast<uint32_t>(stride_in_bytes);
        cpuData.assign(static_cast<const uint8_t*>(data),
                       static_cast<const uint8_t*>(data) + byteSize);
        bgfx::update(handle, 0, bgfx::copy(data, byteSize));
        submittedSinceUpdate_ = false;
    }

    std::unique_ptr<IVertexBufferBackend> BgfxGraphicsBackend::CreateVertexBuffer(int capacity)
    {
        return std::make_unique<BgfxVertexBufferBackend>(capacity);
    }

    // --- BgfxIndexBufferBackend ---

    BgfxIndexBufferBackend::BgfxIndexBufferBackend(int capacity, bool thirtyTwoBit)
        : is32bit(thirtyTwoBit)
        , capacity_(capacity)
        , nativeCreationFlags_(
              BGFX_BUFFER_ALLOW_RESIZE |
              (thirtyTwoBit ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE))
    {
        handle = bgfx::createDynamicIndexBuffer(
            static_cast<uint32_t>(capacity),
            nativeCreationFlags_);
    }

    BgfxIndexBufferBackend::~BgfxIndexBufferBackend()
    {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }

    void BgfxIndexBufferBackend::SetData16(const void* data, int index_count)
    {
        if (is32bit)
            throw std::runtime_error(
                "BgfxIndexBufferBackend: SetData16 called on a 32-bit index buffer.");
        indexCount = index_count;
        if (!data || index_count <= 0) { cpuData.clear(); return; }

        // REMED-GFX-109: preserve the native bytes referenced by every already-submitted draw.
        // Both public IndexBuffer and DynamicIndexBuffer use this same backend object.
        if (submittedSinceUpdate_ || !bgfx::isValid(handle))
        {
            const auto replacement = bgfx::createDynamicIndexBuffer(
                static_cast<uint32_t>(capacity_), nativeCreationFlags_);
            if (!bgfx::isValid(replacement))
                throw std::runtime_error(
                    "Bgfx: failed to allocate an immutable index-buffer update version.");
            const auto previous = handle;
            handle = replacement;
            if (bgfx::isValid(previous)) bgfx::destroy(previous);
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        cpuData.assign(bytes, bytes + static_cast<std::size_t>(index_count) * 2u);
        bgfx::update(handle, 0, bgfx::copy(data, static_cast<uint32_t>(index_count) * 2u));
        submittedSinceUpdate_ = false;
    }

    void BgfxIndexBufferBackend::SetData32(const void* data, int index_count)
    {
        if (!is32bit)
            throw std::runtime_error(
                "BgfxIndexBufferBackend: SetData32 called on a 16-bit index buffer.");
        indexCount = index_count;
        if (!data || index_count <= 0) { cpuData.clear(); return; }

        // Keep the handle's declared native format unchanged across REMED-GFX-109 versioning.
        if (submittedSinceUpdate_ || !bgfx::isValid(handle))
        {
            const auto replacement = bgfx::createDynamicIndexBuffer(
                static_cast<uint32_t>(capacity_), nativeCreationFlags_);
            if (!bgfx::isValid(replacement))
                throw std::runtime_error(
                    "Bgfx: failed to allocate an immutable index-buffer update version.");
            const auto previous = handle;
            handle = replacement;
            if (bgfx::isValid(previous)) bgfx::destroy(previous);
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        cpuData.assign(bytes, bytes + static_cast<std::size_t>(index_count) * 4u);
        bgfx::update(handle, 0, bgfx::copy(data, static_cast<uint32_t>(index_count) * 4u));
        submittedSinceUpdate_ = false;
    }

    std::unique_ptr<IIndexBufferBackend> BgfxGraphicsBackend::CreateIndexBuffer16(int capacity)
    {
        return std::make_unique<BgfxIndexBufferBackend>(capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> BgfxGraphicsBackend::CreateIndexBuffer32(int capacity)
    {
        return std::make_unique<BgfxIndexBufferBackend>(capacity, true);
    }

    // --- 3D draw calls ---

    // Task 767: scale factor for the DepthBias vertex-shader Z-offset emulation. Real GL/Vulkan
    // polygon-offset hardware multiplies the raw DepthBias value by an implementation-defined
    // "minimum resolvable difference" of the depth buffer format (commonly ~1/(2^24-1) for a
    // 24-bit depth buffer) before adding it to window-space depth -- mirrored here so a given
    // DepthBias value produces a roughly comparable visual shift on Bgfx as on EasyGL/Vulkan.
    static constexpr float kDepthBiasScale = 1.0f / 16777215.0f;

    void BgfxGraphicsBackend::SetDepthBiasUniform()
    {
        const float depthBias4[4] = { depthBias_ * kDepthBiasScale, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(depthBiasUnif_, depthBias4);
    }

    // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: binds the base color map (unit 0, shared
    // texColor3DSampler_) plus PbrEffect's 4 additional maps (units 1-4, bound before unit 0 to
    // leave it active last, matching EasyGLGraphicsBackend::BindDrawParams()'s established
    // envMap/texture2 unit-ordering precedent). Each fallback texture is the correct "map
    // absent" constant for its own semantic -- see EnsureDefaultFlatNormalTexture()'s doc
    // comment (mirrored on defaultFlatNormalTexture3D_ above) for the normal-map case; the other
    // 3 all reuse defaultWhiteTexture3D_ since their respective factor/no-op semantics already
    // make (1,1,1,1) the correct "map absent" value.
    void BgfxGraphicsBackend::BindPbrTextures(const GpuDrawParams& params)
    {
        // REMED-GFX-078: each slot resolved through IBgfxSamplable (see BindSamplerSlot) so a
        // RenderTarget2D set as any PBR map binds its real handle instead of UB-casting to
        // BgfxTextureBackend. Slots 1-4 are bound before slot 0 so slot 0 stays active last
        // (matching EasyGL's unit ordering). Fallbacks are each map's "absent" constant.
        BindSamplerSlot(1, normalMapSampler_,           params.pbrNormalMap,            defaultFlatNormalTexture3D_);
        BindSamplerSlot(2, metallicRoughnessSampler_,   params.pbrMetallicRoughnessMap, defaultWhiteTexture3D_);
        BindSamplerSlot(3, emissiveMapSampler_,         params.pbrEmissiveMap,          defaultWhiteTexture3D_);
        BindSamplerSlot(4, occlusionMapSampler_,        params.pbrOcclusionMap,         defaultWhiteTexture3D_);
        BindSamplerSlot(0, texColor3DSampler_,          params.texture0,                defaultWhiteTexture3D_);
    }

    /// REMED-GFX-111: the single shared topology mapper for every Bgfx draw path. bgfx carries
    /// topology as per-submission state bits (BGFX_STATE_PT_*), never as a cached pipeline object,
    /// so each draw's own value is captured at its own bgfx::setState() call.
    ///
    /// Every CNA topology is now named explicitly. TriangleList is bgfx's zero-valued default, and
    /// leaving it as an unnamed `default:` fall-through is exactly how PointListEXT used to inherit
    /// triangle-list state: an indexed one-point draw consumed the correct single index but
    /// rasterized nothing, and a multi-point draw filled the area spanned by its first three
    /// vertices. Point size is deliberately left at zero: bgfx's OpenGL renderer resolves that to
    /// glPointSize(1) and every other renderer rasterizes one-pixel points, matching XNA's
    /// PointListEXT, which exposes no point-size state.
    static uint64_t ToTopologyFlag(PrimitiveType p)
    {
        switch (p)
        {
        case PrimitiveType::TriangleList:  return 0; // bgfx's default primitive state
        case PrimitiveType::TriangleStrip: return BGFX_STATE_PT_TRISTRIP;
        case PrimitiveType::LineList:      return BGFX_STATE_PT_LINES;
        case PrimitiveType::LineStrip:     return BGFX_STATE_PT_LINESTRIP;
        case PrimitiveType::PointListEXT:  return BGFX_STATE_PT_POINTS;
        }
        return 0;
    }

    /// REMED-GFX-113: the exact vertex range a non-indexed draw owes, resolved once per draw.
    /// `vertexStart` is a vertex-ELEMENT offset and the consumed count is topology-derived, so the
    /// binding is [vertexStart, vertexStart + consumed). bgfx's whole-buffer setVertexBuffer
    /// overload passes (0, UINT32_MAX) and then clamps to the buffer's own allocated size, which
    /// silently widens every partial range into a whole-buffer draw -- hence the explicit range
    /// here and the rejection (never a clamp) of a range that leaves the logical buffer.
    struct BgfxVertexRange
    {
        uint32_t start = 0;
        uint32_t count = 0;
    };

    static BgfxVertexRange ResolveNonIndexedVertexRange(
        const BgfxVertexBufferBackend& vb, PrimitiveType primitive,
        int primitiveCount, int vertexStart)
    {
        if (primitiveCount <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "primitiveCount must be greater than zero.");
        }
        if (vertexStart < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "vertexStart", std::to_string(vertexStart),
                "vertexStart must not be negative.");
        }
        // 64-bit throughout: primitiveCount * 3 and vertexStart + consumed both overflow int for
        // large public arguments, and an overflowed range must be rejected, not wrapped.
        std::int64_t consumed = 0;
        switch (primitive)
        {
        case PrimitiveType::TriangleList:
            consumed = static_cast<std::int64_t>(primitiveCount) * 3; break;
        case PrimitiveType::TriangleStrip:
            consumed = static_cast<std::int64_t>(primitiveCount) + 2; break;
        case PrimitiveType::LineList:
            consumed = static_cast<std::int64_t>(primitiveCount) * 2; break;
        case PrimitiveType::LineStrip:
            consumed = static_cast<std::int64_t>(primitiveCount) + 1; break;
        case PrimitiveType::PointListEXT:
            consumed = primitiveCount; break;
        }
        const std::int64_t available = static_cast<std::int64_t>(vb.vertexCount);
        if (vertexStart > available || consumed > available - static_cast<std::int64_t>(vertexStart))
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "The requested primitive range exceeds the bound vertex buffer.");
        }
        return BgfxVertexRange{
            static_cast<uint32_t>(vertexStart), static_cast<uint32_t>(consumed)};
    }

    /// REMED-GFX-118: the exact index and vertex ranges an indexed draw owes, resolved once per
    /// draw. `startIndex` is an index-ELEMENT offset and the consumed index count is
    /// topology-derived, so the index binding is [startIndex, startIndex + consumed). bgfx has no
    /// draw-time base-vertex argument, so `baseVertex` becomes the vertex binding's start element
    /// and every decoded index picks it up exactly once; the safe remainder of the buffer stays
    /// bound because minVertexIndex/numVertices are range hints, not a second address.
    struct BgfxIndexedRange
    {
        uint32_t indexStart = 0;
        uint32_t indexCount = 0;
        uint32_t vertexStart = 0;
        uint32_t vertexCount = 0;
    };

    static BgfxIndexedRange ResolveIndexedRange(
        const BgfxVertexBufferBackend& vb, const BgfxIndexBufferBackend& ib,
        PrimitiveType primitive, int primitiveCount, int startIndex, int baseVertex)
    {
        if (primitiveCount <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "primitiveCount must be greater than zero.");
        }
        if (startIndex < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "startIndex", std::to_string(startIndex),
                "startIndex must not be negative.");
        }
        if (baseVertex < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "baseVertex", std::to_string(baseVertex),
                "baseVertex must not be negative.");
        }
        // 64-bit throughout: primitiveCount * 3 and startIndex + consumed both overflow int for
        // large public arguments, and an overflowed range must be rejected, not wrapped.
        std::int64_t consumed = 0;
        switch (primitive)
        {
        case PrimitiveType::TriangleList:
            consumed = static_cast<std::int64_t>(primitiveCount) * 3; break;
        case PrimitiveType::TriangleStrip:
            consumed = static_cast<std::int64_t>(primitiveCount) + 2; break;
        case PrimitiveType::LineList:
            consumed = static_cast<std::int64_t>(primitiveCount) * 2; break;
        case PrimitiveType::LineStrip:
            consumed = static_cast<std::int64_t>(primitiveCount) + 1; break;
        case PrimitiveType::PointListEXT:
            consumed = primitiveCount; break;
        }
        const std::int64_t availableIndices = static_cast<std::int64_t>(ib.indexCount);
        if (startIndex > availableIndices ||
            consumed > availableIndices - static_cast<std::int64_t>(startIndex))
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "The requested primitive range exceeds the bound index buffer.");
        }
        const std::int64_t availableVertices = static_cast<std::int64_t>(vb.vertexCount);
        if (baseVertex >= availableVertices)
        {
            throw System::ArgumentOutOfRangeException(
                "baseVertex", std::to_string(baseVertex),
                "The requested base vertex exceeds the bound vertex buffer.");
        }
        return BgfxIndexedRange{
            static_cast<uint32_t>(startIndex),
            static_cast<uint32_t>(consumed),
            static_cast<uint32_t>(baseVertex),
            static_cast<uint32_t>(availableVertices - baseVertex)};
    }

    static uint32_t IndexCountForPrimitives(PrimitiveType primitive, int primitiveCount)
    {
        switch (primitive)
        {
        case PrimitiveType::TriangleList:
            return static_cast<uint32_t>(primitiveCount * 3);
        case PrimitiveType::TriangleStrip:
            return static_cast<uint32_t>(primitiveCount + 2);
        case PrimitiveType::LineList:
            return static_cast<uint32_t>(primitiveCount * 2);
        case PrimitiveType::LineStrip:
            return static_cast<uint32_t>(primitiveCount + 1);
        case PrimitiveType::PointListEXT:
            return static_cast<uint32_t>(primitiveCount);
        }
        return 0;
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
        SetDepthBiasUniform();
        // This path carries no BasicEffect diffuse/VertexColorEnabled (no GpuDrawParams at
        // all); preserve the historical behavior of outputting the raw vertex colors
        // unmodified (diffuseColor=white, vertexColorEnabled=true — Task 364).
        const float whiteDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bgfx::setUniform(diffuseColor3DUnif_, whiteDiffuse);
        const float vceOn[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(vertexColorEn3DUnif_, vceOn);

        bgfx::setVertexBuffer(0, vb.handle);
        vb.MarkSubmitted();
        // Task 766: FillMode::WireFrame -- re-expand triangle vertices into a line-list edge
        // buffer (bgfx has no native polygon-fill-mode toggle, unlike D3D9/Vulkan).
        bgfx::TransientIndexBuffer wireTib;
        const bool useWireframe = wireframe_
            && ExpandWireframeIndices(nullptr, primitive, primitiveCount, 0, 0, 0, wireTib);
        if (useWireframe) bgfx::setIndexBuffer(&wireTib);
        ApplyScissorOverride();
        bgfx::setStencil(stencilFront_, stencilBack_);
        bgfx::setState((colorWriteFlags_
                       // Task 759: BGFX_STATE_WRITE_Z must NOT be unconditionally included
                       // here -- depthFlags_ (set by ApplyDepthStencilState from the real
                       // DepthBufferWriteEnable) already carries it when writes are actually
                       // requested; including it again here unconditionally made
                       // DepthBufferWriteEnable=false a complete no-op on every 3D draw.
                       | blendFlags_ | depthFlags_ | cullFlags_)
                       | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive)),
                       blendFactorPacked_);
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
        SetDepthBiasUniform();
        // See DrawColoredPrimitives above: preserve the historical raw-vertex-color output
        // for this no-GpuDrawParams legacy path (Task 364).
        const float whiteDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bgfx::setUniform(diffuseColor3DUnif_, whiteDiffuse);
        const float vceOn[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(vertexColorEn3DUnif_, vceOn);

        bgfx::setVertexBuffer(0, vb.handle);
        vb.MarkSubmitted();
        // Task 766: see DrawColoredPrimitives above.
        bgfx::TransientIndexBuffer wireTib;
        const bool useWireframe = wireframe_
            && ExpandWireframeIndices(&ib, primitive, primitiveCount, 0, 0, 0, wireTib);
        if (useWireframe) {
            bgfx::setIndexBuffer(&wireTib);
        } else {
            bgfx::setIndexBuffer(ib.handle);
            ib.MarkSubmitted();
        }
        ApplyScissorOverride();
        bgfx::setStencil(stencilFront_, stencilBack_);
        bgfx::setState((colorWriteFlags_
                       // Task 759: BGFX_STATE_WRITE_Z must NOT be unconditionally included
                       // here -- depthFlags_ (set by ApplyDepthStencilState from the real
                       // DepthBufferWriteEnable) already carries it when writes are actually
                       // requested; including it again here unconditionally made
                       // DepthBufferWriteEnable=false a complete no-op on every 3D draw.
                       | blendFlags_ | depthFlags_ | cullFlags_)
                       | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive)),
                       blendFactorPacked_);
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
        SetDepthBiasUniform();

        // Task 888: fog uniforms are set unconditionally (not per-branch) since bgfx uniforms
        // are shared by name across every program -- any program declaring u_fogColor/
        // u_fogParams as inputs picks these up automatically; programs that don't simply ignore
        // them, no error.
        float fogColor4[4]  = { params.fogColor[0], params.fogColor[1], params.fogColor[2], 0.0f };
        bgfx::setUniform(fogColorUnif_, fogColor4);
        // REMED-GFX-010: u_fogParams now carries the FNA view-space fog vector (GpuDrawParams.fogVector,
        // = SetFogVector(World*View, fogStart, fogEnd)). The shader computes
        // v_fogFactor = 1 - saturate(dot(vec4(pos,1), u_fogParams)); all-zero when fog is disabled.
        float fogParams4[4] = { params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3] };
        bgfx::setUniform(fogParamsUnif_, fogParams4);

        // REMED-GFX-113: bind exactly the requested vertex range. The range is resolved (and an
        // out-of-buffer request rejected) before anything is submitted natively.
        const BgfxVertexRange range = ResolveNonIndexedVertexRange(
            vb, primitive, primitiveCount, params.vertexStart);
        // Task 766: see DrawColoredPrimitives above.
        bgfx::TransientIndexBuffer wireTib;
        const bool useWireframe = wireframe_
            && ExpandWireframeIndices(nullptr, primitive, primitiveCount, 0, 0,
                                      params.vertexStart, wireTib);
        // The wireframe path already expresses the exact range through absolute expanded indices
        // (ExpandWireframeIndices synthesizes vertexStart + local), so its vertex binding must
        // still start at element zero for those indices to address the intended vertices.
        const uint32_t bindStart = useWireframe ? 0u : range.start;
        const uint32_t bindCount = useWireframe
            ? static_cast<uint32_t>(vb.vertexCount)
            : range.count;
        bgfx::setVertexBuffer(0, vb.handle, bindStart, bindCount);
        lastNonIndexedBindStartEXT_ = bindStart;
        lastNonIndexedBindCountEXT_ = bindCount;
        vb.MarkSubmitted();
        if (useWireframe) bgfx::setIndexBuffer(&wireTib);
        ApplyScissorOverride();
        bgfx::setStencil(stencilFront_, stencilBack_);
        const uint64_t state = (colorWriteFlags_
                       // Task 759: BGFX_STATE_WRITE_Z must NOT be unconditionally included
                       // here -- depthFlags_ (set by ApplyDepthStencilState from the real
                       // DepthBufferWriteEnable) already carries it when writes are actually
                       // requested; including it again here unconditionally made
                       // DepthBufferWriteEnable=false a complete no-op on every 3D draw.
                                | blendFlags_ | depthFlags_ | cullFlags_)
                               | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive));
        bgfx::setState(state, blendFactorPacked_);

        const bool alphaTestActive = (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        if (params.dualTexture && params.vertexColorEnabled && bgfx::isValid(dualTextureColored3DProgram_))
        {
            // Task 889: stride-24 (VertexPositionColorTexture) variant — reads a_color0 and
            // gates it by VertexColorEnabled, mirroring Task 887's alphaTestColoredTextured3DProgram_.
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vcEn[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vcEn);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // REMED-GFX-078: DualTextureEffect's second layer -- same IBgfxSamplable resolution as
            // slot 0, so each of the two slots independently binds a RenderTarget2D safely and gets
            // its own per-slot V-flip flag (u_rtFlipV.y here). Null falls back to opaque white.
            BindSamplerSlot(1, texColor3DSampler2_, params.texture1, defaultWhiteTexture3D_);
            SubmitViewProgram(dualTextureColored3DProgram_);
        }
        else if (params.dualTexture && bgfx::isValid(dualTexture3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // REMED-GFX-078: DualTextureEffect's second layer -- same IBgfxSamplable resolution as
            // slot 0, so each of the two slots independently binds a RenderTarget2D safely and gets
            // its own per-slot V-flip flag (u_rtFlipV.y here). Null falls back to opaque white.
            BindSamplerSlot(1, texColor3DSampler2_, params.texture1, defaultWhiteTexture3D_);
            SubmitViewProgram(dualTexture3DProgram_);
        }
        else if (params.pbr && params.skinned && bgfx::isValid(pbrSkinned3DProgram_))
        {
            // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: SkinnedPbrEffect -- same BRDF as
            // PbrEffect below, plus the bone-palette skin transform (see vs_pbr_skinned3d.sc).
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float amb[4] = { params.ambientColor[0], params.ambientColor[1],
                             params.ambientColor[2], 0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor, 0.0f, 0.0f };
            bgfx::setUniform(metallicRoughnessFactorUnif_, mrFactor);
            float dir0[4] = { params.light0Dir[0], params.light0Dir[1], params.light0Dir[2], 0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir0);
            float diff0[4] = { params.light0Diffuse[0], params.light0Diffuse[1],
                                params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff0);
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
            float eyePos[4] = { params.eyePositionWorld[0], params.eyePositionWorld[1],
                                 params.eyePositionWorld[2], 0.0f };
            bgfx::setUniform(eyePos3DUnif_, eyePos);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            if (params.boneCount > 0 && bgfx::isValid(bonesUnif_))
                bgfx::setUniform(bonesUnif_, params.boneTransforms, static_cast<uint16_t>(params.boneCount));
            float weightsPerVertex[4] = { static_cast<float>(params.weightsPerVertex), 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(weightsPerVertex3DUnif_, weightsPerVertex);
            // REMED-GFX-006: upload the World inverse-transpose (same ComputeNormalMatrix3x3 the
            // lit/PBR paths use) so vs_pbr_skinned3d.sc composes it with the bone skin for the
            // normal. Previously the shader used raw World (audit Variant B), wrong under
            // non-uniform scale. bgfx's shaderc has no in-shader inverse()/transpose(), so the
            // matrix is supplied CPU-side.
            float normalMatrixSkin[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrixSkin);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrixSkin);
            BindPbrTextures(params);
            SubmitViewProgram(pbrSkinned3DProgram_);
        }
        else if (params.pbr && bgfx::isValid(pbr3DProgram_))
        {
            // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: PbrEffect -- real glTF 2.0
            // metallic-roughness BRDF (see fs_pbr3d.sc's own doc comment).
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float amb[4] = { params.ambientColor[0], params.ambientColor[1],
                             params.ambientColor[2], 0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor, 0.0f, 0.0f };
            bgfx::setUniform(metallicRoughnessFactorUnif_, mrFactor);
            float dir0[4] = { params.light0Dir[0], params.light0Dir[1], params.light0Dir[2], 0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir0);
            float diff0[4] = { params.light0Diffuse[0], params.light0Diffuse[1],
                                params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff0);
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
            float normalMatrix[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrix);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrix);
            float eyePos[4] = { params.eyePositionWorld[0], params.eyePositionWorld[1],
                                 params.eyePositionWorld[2], 0.0f };
            bgfx::setUniform(eyePos3DUnif_, eyePos);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            BindPbrTextures(params);
            SubmitViewProgram(pbr3DProgram_);
        }
        else if (params.skinned && bgfx::isValid(skinned3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // CNB-67 (Phase 13C) Bgfx port: stride-56 SkinnedEffect+Color VertexColorEnabled gate
            // (see fs_skinned3d.sc/fs_skinned3d_vertexlit.sc's own comment) -- reuses the shared
            // u_vertexColorEnabled3D uniform, same as every other VertexColorEnabled-aware branch.
            float vceSkinned[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vceSkinned);
            float amb[4]  = { params.ambientColor[0],  params.ambientColor[1],  params.ambientColor[2],  0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float dir[4]  = { params.light0Dir[0],     params.light0Dir[1],     params.light0Dir[2],     0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir);
            float diff[4] = { params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff);
            // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            float litEn[4] = { params.lightingEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(lightingEn3DUnif_, litEn);
            // Task 899: EmissiveColor was never forwarded to the skinned3d shader at all -- see
            // fs_skinned3d.sc's comment for the full finding.
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            // Task 894: World (for world-space position -> eye vector), EyePosition, and
            // per-light + material specular.
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
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
            if (params.boneCount > 0 && bgfx::isValid(bonesUnif_))
                bgfx::setUniform(bonesUnif_, params.boneTransforms, static_cast<uint16_t>(params.boneCount));
            // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex
            // (1, 2, or 4) weight/index pairs.
            float weightsPerVertex[4] = { static_cast<float>(params.weightsPerVertex), 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(weightsPerVertex3DUnif_, weightsPerVertex);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // Task 1104: XNA's real SkinnedEffect default is per-vertex lighting
            // (PreferPerPixelLighting=false); fall back to the per-pixel-lit program only if the
            // vertex-lit sibling failed to compile on this renderer.
            // REMED-GFX-006: upload the World inverse-transpose (same ComputeNormalMatrix3x3 the
            // lit path uses) so vs_skinned3d.sc / vs_skinned3d_vertexlit.sc compose it with the
            // bone skin for the normal. Previously the shaders applied only the bone 3x3 (audit
            // Variant A -- no world factor), wrong under non-identity World. bgfx's shaderc has no
            // in-shader inverse()/transpose(), so the matrix is supplied CPU-side.
            float normalMatrixSkin[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrixSkin);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrixSkin);
            SubmitViewProgram((!params.preferPerPixelLighting && bgfx::isValid(skinned3DVertexLitProgram_))
                ? skinned3DVertexLitProgram_ : skinned3DProgram_);
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
            // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            float amount[4] = { params.envMapAmount, params.fresnelEnabled ? 1.0f : 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(envMapAmountUnif_, amount);
            float specular[4] = { params.envMapSpecular[0], params.envMapSpecular[1],
                                   params.envMapSpecular[2], params.fresnelFactor };
            bgfx::setUniform(envMapSpecularUnif_, specular);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
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
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(alphaTestColoredTextured3DProgram_);
        }
        else if (alphaTestActive && bgfx::isValid(alphaTest3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
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

            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // Task 1104: XNA's real BasicEffect default is per-vertex lighting
            // (PreferPerPixelLighting=false); fall back to the per-pixel-lit program only if the
            // vertex-lit sibling failed to compile on this renderer.
            SubmitViewProgram((!params.preferPerPixelLighting && bgfx::isValid(litTextured3DVertexLitProgram_))
                ? litTextured3DVertexLitProgram_ : litTextured3DProgram_);
        }
        else if (params.textureEnabled && params.vertexColorEnabled
                 && bgfx::isValid(coloredTextured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vcEn[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vcEn);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(coloredTextured3DProgram_);
        }
        else if (params.textureEnabled && bgfx::isValid(textured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
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

    void BgfxGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                      const IIndexBufferBackend& ib_in,
                                                      const Matrix& world, const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        // Task 948: indexed counterpart of DrawPrimitivesEx -- previously unimplemented, so
        // every indexed Effect-bound draw (e.g. any Content.Load<Model>() mesh) silently fell
        // back to the base IGraphicsBackend default (DrawIndexedColoredPrimitives), discarding
        // GpuDrawParams entirely (diffuse color, texture, lighting, fog all lost). Mirrors
        // DrawPrimitivesEx's own full GpuDrawParams dispatch exactly -- see that function for
        // per-branch comments -- with an index buffer bound instead of a plain vertex draw.
        auto& vb = static_cast<const BgfxVertexBufferBackend&>(vb_in);
        auto& ib = static_cast<const BgfxIndexBufferBackend&>(ib_in);
        if (!bgfx::isValid(vb.handle) || !bgfx::isValid(ib.handle)) return;

        ApplyViewportOverride();

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);
        bgfx::setUniform(wvpUniform_, wvp_col);
        SetDepthBiasUniform();

        // Task 888: fog uniforms are set unconditionally (not per-branch) since bgfx uniforms
        // are shared by name across every program -- any program declaring u_fogColor/
        // u_fogParams as inputs picks these up automatically; programs that don't simply ignore
        // them, no error.
        float fogColor4[4]  = { params.fogColor[0], params.fogColor[1], params.fogColor[2], 0.0f };
        bgfx::setUniform(fogColorUnif_, fogColor4);
        // REMED-GFX-010: u_fogParams now carries the FNA view-space fog vector (GpuDrawParams.fogVector,
        // = SetFogVector(World*View, fogStart, fogEnd)). The shader computes
        // v_fogFactor = 1 - saturate(dot(vec4(pos,1), u_fogParams)); all-zero when fog is disabled.
        float fogParams4[4] = { params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3] };
        bgfx::setUniform(fogParamsUnif_, fogParams4);

        // Task 766: see DrawColoredPrimitives above.
        bgfx::TransientIndexBuffer wireTib;
        const bool useWireframe = wireframe_
            && ExpandWireframeIndices(&ib, primitive, primitiveCount, params.startIndex,
                                      params.baseVertex, 0, wireTib);
        // REMED-GFX-107: bgfx has no draw-time base-vertex argument. Starting the vertex
        // binding at baseVertex gives every decoded index the required signed addend. The
        // public minVertexIndex/numVertices values are range hints, not another address:
        // binding the complete safe remainder preserves valid geometry without rebasing indices.
        const uint32_t vertexStart = useWireframe
            ? 0u
            : static_cast<uint32_t>(params.baseVertex);
        const uint32_t vertexCount = static_cast<uint32_t>(vb.vertexCount) - vertexStart;
        bgfx::setVertexBuffer(0, vb.handle, vertexStart, vertexCount);
        vb.MarkSubmitted();
        if (useWireframe) {
            bgfx::setIndexBuffer(&wireTib);
        } else {
            bgfx::setIndexBuffer(
                ib.handle,
                static_cast<uint32_t>(params.startIndex),
                IndexCountForPrimitives(primitive, primitiveCount));
            ib.MarkSubmitted();
        }
        ApplyScissorOverride();
        bgfx::setStencil(stencilFront_, stencilBack_);
        const uint64_t state = (colorWriteFlags_
                       // Task 759: BGFX_STATE_WRITE_Z must NOT be unconditionally included
                       // here -- depthFlags_ (set by ApplyDepthStencilState from the real
                       // DepthBufferWriteEnable) already carries it when writes are actually
                       // requested; including it again here unconditionally made
                       // DepthBufferWriteEnable=false a complete no-op on every 3D draw.
                                | blendFlags_ | depthFlags_ | cullFlags_)
                               | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive));
        bgfx::setState(state, blendFactorPacked_);

        const bool alphaTestActive = (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        if (params.dualTexture && params.vertexColorEnabled && bgfx::isValid(dualTextureColored3DProgram_))
        {
            // Task 889: stride-24 (VertexPositionColorTexture) variant — reads a_color0 and
            // gates it by VertexColorEnabled, mirroring Task 887's alphaTestColoredTextured3DProgram_.
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vcEn[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vcEn);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // REMED-GFX-078: DualTextureEffect's second layer -- same IBgfxSamplable resolution as
            // slot 0, so each of the two slots independently binds a RenderTarget2D safely and gets
            // its own per-slot V-flip flag (u_rtFlipV.y here). Null falls back to opaque white.
            BindSamplerSlot(1, texColor3DSampler2_, params.texture1, defaultWhiteTexture3D_);
            SubmitViewProgram(dualTextureColored3DProgram_);
        }
        else if (params.dualTexture && bgfx::isValid(dualTexture3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // REMED-GFX-078: DualTextureEffect's second layer -- same IBgfxSamplable resolution as
            // slot 0, so each of the two slots independently binds a RenderTarget2D safely and gets
            // its own per-slot V-flip flag (u_rtFlipV.y here). Null falls back to opaque white.
            BindSamplerSlot(1, texColor3DSampler2_, params.texture1, defaultWhiteTexture3D_);
            SubmitViewProgram(dualTexture3DProgram_);
        }
        else if (params.pbr && params.skinned && bgfx::isValid(pbrSkinned3DProgram_))
        {
            // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: SkinnedPbrEffect -- same BRDF as
            // PbrEffect below, plus the bone-palette skin transform (see vs_pbr_skinned3d.sc).
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float amb[4] = { params.ambientColor[0], params.ambientColor[1],
                             params.ambientColor[2], 0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor, 0.0f, 0.0f };
            bgfx::setUniform(metallicRoughnessFactorUnif_, mrFactor);
            float dir0[4] = { params.light0Dir[0], params.light0Dir[1], params.light0Dir[2], 0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir0);
            float diff0[4] = { params.light0Diffuse[0], params.light0Diffuse[1],
                                params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff0);
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
            float eyePos[4] = { params.eyePositionWorld[0], params.eyePositionWorld[1],
                                 params.eyePositionWorld[2], 0.0f };
            bgfx::setUniform(eyePos3DUnif_, eyePos);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            if (params.boneCount > 0 && bgfx::isValid(bonesUnif_))
                bgfx::setUniform(bonesUnif_, params.boneTransforms, static_cast<uint16_t>(params.boneCount));
            float weightsPerVertex[4] = { static_cast<float>(params.weightsPerVertex), 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(weightsPerVertex3DUnif_, weightsPerVertex);
            // REMED-GFX-006: upload the World inverse-transpose (same ComputeNormalMatrix3x3 the
            // lit/PBR paths use) so vs_pbr_skinned3d.sc composes it with the bone skin for the
            // normal. Previously the shader used raw World (audit Variant B), wrong under
            // non-uniform scale. bgfx's shaderc has no in-shader inverse()/transpose(), so the
            // matrix is supplied CPU-side.
            float normalMatrixSkin[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrixSkin);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrixSkin);
            BindPbrTextures(params);
            SubmitViewProgram(pbrSkinned3DProgram_);
        }
        else if (params.pbr && bgfx::isValid(pbr3DProgram_))
        {
            // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: PbrEffect -- real glTF 2.0
            // metallic-roughness BRDF (see fs_pbr3d.sc's own doc comment).
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float amb[4] = { params.ambientColor[0], params.ambientColor[1],
                             params.ambientColor[2], 0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor, 0.0f, 0.0f };
            bgfx::setUniform(metallicRoughnessFactorUnif_, mrFactor);
            float dir0[4] = { params.light0Dir[0], params.light0Dir[1], params.light0Dir[2], 0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir0);
            float diff0[4] = { params.light0Diffuse[0], params.light0Diffuse[1],
                                params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff0);
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
            float normalMatrix[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrix);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrix);
            float eyePos[4] = { params.eyePositionWorld[0], params.eyePositionWorld[1],
                                 params.eyePositionWorld[2], 0.0f };
            bgfx::setUniform(eyePos3DUnif_, eyePos);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            BindPbrTextures(params);
            SubmitViewProgram(pbr3DProgram_);
        }
        else if (params.skinned && bgfx::isValid(skinned3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // CNB-67 (Phase 13C) Bgfx port: stride-56 SkinnedEffect+Color VertexColorEnabled gate
            // (see fs_skinned3d.sc/fs_skinned3d_vertexlit.sc's own comment) -- reuses the shared
            // u_vertexColorEnabled3D uniform, same as every other VertexColorEnabled-aware branch.
            float vceSkinned[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vceSkinned);
            float amb[4]  = { params.ambientColor[0],  params.ambientColor[1],  params.ambientColor[2],  0.0f };
            bgfx::setUniform(ambientColor3DUnif_, amb);
            float dir[4]  = { params.light0Dir[0],     params.light0Dir[1],     params.light0Dir[2],     0.0f };
            bgfx::setUniform(light0Dir3DUnif_, dir);
            float diff[4] = { params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2], 0.0f };
            bgfx::setUniform(light0Diff3DUnif_, diff);
            // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            float litEn[4] = { params.lightingEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(lightingEn3DUnif_, litEn);
            // Task 899: EmissiveColor was never forwarded to the skinned3d shader at all -- see
            // fs_skinned3d.sc's comment for the full finding.
            float emissive[4] = { params.emissiveColor[0], params.emissiveColor[1],
                                   params.emissiveColor[2], 0.0f };
            bgfx::setUniform(emissiveColor3DUnif_, emissive);
            // Task 894: World (for world-space position -> eye vector), EyePosition, and
            // per-light + material specular.
            bgfx::setUniform(world3DUnif_, params.worldColMajor);
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
            if (params.boneCount > 0 && bgfx::isValid(bonesUnif_))
                bgfx::setUniform(bonesUnif_, params.boneTransforms, static_cast<uint16_t>(params.boneCount));
            // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex
            // (1, 2, or 4) weight/index pairs.
            float weightsPerVertex[4] = { static_cast<float>(params.weightsPerVertex), 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(weightsPerVertex3DUnif_, weightsPerVertex);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // Task 1104: XNA's real SkinnedEffect default is per-vertex lighting
            // (PreferPerPixelLighting=false); fall back to the per-pixel-lit program only if the
            // vertex-lit sibling failed to compile on this renderer.
            // REMED-GFX-006: upload the World inverse-transpose (same ComputeNormalMatrix3x3 the
            // lit path uses) so vs_skinned3d.sc / vs_skinned3d_vertexlit.sc compose it with the
            // bone skin for the normal. Previously the shaders applied only the bone 3x3 (audit
            // Variant A -- no world factor), wrong under non-identity World. bgfx's shaderc has no
            // in-shader inverse()/transpose(), so the matrix is supplied CPU-side.
            float normalMatrixSkin[9];
            ComputeNormalMatrix3x3(params.worldColMajor, normalMatrixSkin);
            bgfx::setUniform(normalMatrix3DUnif_, normalMatrixSkin);
            SubmitViewProgram((!params.preferPerPixelLighting && bgfx::isValid(skinned3DVertexLitProgram_))
                ? skinned3DVertexLitProgram_ : skinned3DProgram_);
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
            // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
            float dir1[4] = { params.light1Dir[0], params.light1Dir[1], params.light1Dir[2], 0.0f };
            bgfx::setUniform(light1Dir3DUnif_, dir1);
            float diff1[4] = { params.light1Diffuse[0], params.light1Diffuse[1],
                                params.light1Diffuse[2], 0.0f };
            bgfx::setUniform(light1Diff3DUnif_, diff1);
            float dir2[4] = { params.light2Dir[0], params.light2Dir[1], params.light2Dir[2], 0.0f };
            bgfx::setUniform(light2Dir3DUnif_, dir2);
            float diff2[4] = { params.light2Diffuse[0], params.light2Diffuse[1],
                                params.light2Diffuse[2], 0.0f };
            bgfx::setUniform(light2Diff3DUnif_, diff2);
            float amount[4] = { params.envMapAmount, params.fresnelEnabled ? 1.0f : 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(envMapAmountUnif_, amount);
            float specular[4] = { params.envMapSpecular[0], params.envMapSpecular[1],
                                   params.envMapSpecular[2], params.fresnelFactor };
            bgfx::setUniform(envMapSpecularUnif_, specular);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
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
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(alphaTestColoredTextured3DProgram_);
        }
        else if (alphaTestActive && bgfx::isValid(alphaTest3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
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

            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            // Task 1104: XNA's real BasicEffect default is per-vertex lighting
            // (PreferPerPixelLighting=false); fall back to the per-pixel-lit program only if the
            // vertex-lit sibling failed to compile on this renderer.
            SubmitViewProgram((!params.preferPerPixelLighting && bgfx::isValid(litTextured3DVertexLitProgram_))
                ? litTextured3DVertexLitProgram_ : litTextured3DProgram_);
        }
        else if (params.textureEnabled && params.vertexColorEnabled
                 && bgfx::isValid(coloredTextured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            float vcEn[4] = { params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(vertexColorEn3DUnif_, vcEn);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(coloredTextured3DProgram_);
        }
        else if (params.textureEnabled && bgfx::isValid(textured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureBackend; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
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
        if (!bgfx::isValid(vb.handle) || !bgfx::isValid(ib.handle) || instVb.cpuData.empty())
            return;

        // REMED-GFX-118: resolved (and validated in 64-bit) before anything is allocated or
        // submitted, so an out-of-buffer request is an error rather than a clamped native draw.
        const BgfxIndexedRange range = ResolveIndexedRange(
            vb, ib, primitive, primitiveCount, params.startIndex, params.baseVertex);

        ApplyViewportOverride();

        const int instCount = std::max(1, instanceCount);
        const uint16_t instStride = static_cast<uint16_t>(instVb.stride > 0 ? instVb.stride : 64);

        // REMED-GFX-118: the per-instance stream owes every requested instance its own record.
        // Copying min(requested, available) instead left the surplus instances reading
        // uninitialised transient memory; an over-long instance range is now rejected -- before
        // the transient-capacity probe below, so an invalid request is never reported as a
        // temporary out-of-memory skip.
        const std::size_t copyBytes = static_cast<std::size_t>(instCount) * instStride;
        if (copyBytes > instVb.cpuData.size())
        {
            throw System::ArgumentOutOfRangeException(
                "instanceCount", std::to_string(instanceCount),
                "The requested instance count exceeds the bound per-instance vertex buffer.");
        }

        if (bgfx::getAvailInstanceDataBuffer(static_cast<uint32_t>(instCount), instStride) <
            static_cast<uint32_t>(instCount))
            return;

        bgfx::InstanceDataBuffer idb{};
        bgfx::allocInstanceDataBuffer(&idb, static_cast<uint32_t>(instCount), instStride);
        std::memcpy(idb.data, instVb.cpuData.data(), copyBytes);

        const Matrix vp = view * projection;
        float vp_col[16];
        vp.ToColumnMajor(vp_col);
        bgfx::setUniform(vpInstanced3DUnif_, vp_col);
        SetDepthBiasUniform();

        // Task 766: see DrawColoredPrimitives above.
        bgfx::TransientIndexBuffer wireTib;
        const bool useWireframe = wireframe_
            && ExpandWireframeIndices(&ib, primitive, primitiveCount, params.startIndex,
                                      params.baseVertex, 0, wireTib);
        // REMED-GFX-118: bind the exact requested geometry range, the same way the ordinary
        // indexed path has since REMED-GFX-107. This call previously passed the whole-buffer
        // setVertexBuffer/setIndexBuffer overloads, so baseVertex, startIndex and the
        // topology-derived index count never reached the native instanced draw at all -- every
        // instance consumed the complete index buffer from element zero. instanceCount stays
        // independent of this range: it only chooses how many instances consume it.
        const uint32_t vertexStart = useWireframe ? 0u : range.vertexStart;
        const uint32_t vertexCount = useWireframe
            ? static_cast<uint32_t>(vb.vertexCount)
            : range.vertexCount;
        bgfx::setVertexBuffer(0, vb.handle, vertexStart, vertexCount);
        vb.MarkSubmitted();
        lastInstancedVertexBindStartEXT_ = vertexStart;
        lastInstancedVertexBindCountEXT_ = vertexCount;
        if (useWireframe) {
            bgfx::setIndexBuffer(&wireTib);
            lastInstancedIndexBindStartEXT_ = 0;
            lastInstancedIndexBindCountEXT_ =
                wireTib.size / (wireTib.isIndex16 ? 2u : 4u);
        } else {
            bgfx::setIndexBuffer(ib.handle, range.indexStart, range.indexCount);
            ib.MarkSubmitted();
            lastInstancedIndexBindStartEXT_ = range.indexStart;
            lastInstancedIndexBindCountEXT_ = range.indexCount;
        }
        bgfx::setInstanceDataBuffer(&idb);
        lastInstancedInstanceCountEXT_ = static_cast<uint32_t>(instCount);
        ApplyScissorOverride();
        bgfx::setStencil(stencilFront_, stencilBack_);
        const uint64_t state = (colorWriteFlags_
                       // Task 759: BGFX_STATE_WRITE_Z must NOT be unconditionally included
                       // here -- depthFlags_ (set by ApplyDepthStencilState from the real
                       // DepthBufferWriteEnable) already carries it when writes are actually
                       // requested; including it again here unconditionally made
                       // DepthBufferWriteEnable=false a complete no-op on every 3D draw.
                                | blendFlags_ | depthFlags_ | cullFlags_)
                               | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive));
        bgfx::setState(state, blendFactorPacked_);
        SubmitViewProgram(instanced3DProgram_);
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
