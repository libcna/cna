#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
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
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Bgfx
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;

    namespace
    {
        const char* kRendererOverrideEnvVar = "CNA_BGFX_RENDERER";

        // SurfaceFormat::Color is CNA's only supported Bgfx backbuffer format. bgfx's native
        // swapchain spelling for it is BGRA8 (also the Resolution default), made explicit here so
        // init and every later capability check/reset cannot drift apart.
        constexpr bgfx::TextureFormat::Enum kBackbufferFormat = bgfx::TextureFormat::BGRA8;

        // REMED-GFX-185: BGFX_TEXTURE_RT_MSAA_Xn allocates multisample storage, while this
        // independent per-draw bit makes rasterization actually evaluate all of its samples.
        // Every SpriteBatch and 3D submit carries it; on a single-sample target it is a no-op.
        constexpr uint64_t kMsaaRasterState = BGFX_STATE_MSAA;

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
                        "Failed to initialize BGFX renderer: SDL X11 display handle is not available.");
                }
                if (!platformData.nwh)
                {
                    throw std::runtime_error(
                        "Failed to initialize BGFX renderer: SDL X11 window handle is not available.");
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
                        "Failed to initialize BGFX renderer: SDL Wayland display handle is not available.");
                }
                if (!platformData.nwh)
                {
                    throw std::runtime_error(
                        "Failed to initialize BGFX renderer: SDL Wayland surface handle is not available.");
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

    BgfxTextureRenderer::BgfxTextureRenderer(const ImageData& data)
    {
        width = data.width;
        height = data.height;

        if (width <= 0 || height <= 0 || data.pixels.empty())
        {
            throw std::runtime_error("Failed to create BGFX texture: image data is empty.");
        }

        // Task 926: created WITHOUT initial _mem (mutable -- bgfx::createTexture2D's own doc:
        // "If _mem is non-NULL, created texture will be immutable"), mirroring
        // BgfxTextureCubeRenderer's established pattern, so UpdatePixels/UpdatePixelsLevel below
        // can genuinely re-upload later. hasMips now genuinely threaded through (was hardcoded
        // false regardless of data.mipLevels) so a real mip chain can be allocated and later
        // populated via UpdatePixelsLevel.
        creationFlags_ = BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        textureHandle = bgfx::createTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            data.mipLevels > 1,
            1,
            bgfx::TextureFormat::RGBA8,
            creationFlags_
        );

        if (!bgfx::isValid(textureHandle))
        {
            throw std::runtime_error("Failed to create BGFX texture handle.");
        }

        const bgfx::Memory* memory = bgfx::copy(data.pixels.data(), static_cast<uint32_t>(data.pixels.size()));
        bgfx::updateTexture2D(textureHandle, 0, 0, 0, 0,
                               static_cast<uint16_t>(width), static_cast<uint16_t>(height), memory);
    }

    void BgfxTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!bgfx::isValid(textureHandle) || !rgba) return;
        const uint32_t size = static_cast<uint32_t>(stride) * static_cast<uint32_t>(height);
        const bgfx::Memory* mem = bgfx::copy(rgba, size);
        bgfx::updateTexture2D(textureHandle, 0, 0, 0, 0,
                              static_cast<uint16_t>(width), static_cast<uint16_t>(height), mem,
                              static_cast<uint16_t>(stride));
    }

    // Task 926 (split from Task 867): real GPU upload for level>0, mirroring
    // BgfxTextureCubeRenderer::SetData's established bgfx::updateTextureCube pattern --
    // previously the shared IGraphicsRenderer no-op default, silently discarding the caller's
    // mip-level data.
    void BgfxTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (!bgfx::isValid(textureHandle) || !rgba || level < 0) return;
        const uint32_t size = static_cast<uint32_t>(levelW) * static_cast<uint32_t>(levelH) * 4u;
        const bgfx::Memory* mem = bgfx::copy(rgba, size);
        bgfx::updateTexture2D(textureHandle, 0, static_cast<uint8_t>(level), 0, 0,
                              static_cast<uint16_t>(levelW), static_cast<uint16_t>(levelH), mem);
    }

    BgfxTextureRenderer::~BgfxTextureRenderer()
    {
        if (bgfx::isValid(textureHandle))
        {
            bgfx::destroy(textureHandle);
            textureHandle = BGFX_INVALID_HANDLE;
        }
    }

    // --- BgfxEffectRenderer ---

    BgfxEffectRenderer::~BgfxEffectRenderer()
    {
        if (bgfx::isValid(program))
            bgfx::destroy(program);
    }

    void BgfxEffectRenderer::Bind()
    {
        // No-op: bgfx programs are submitted per draw call, not bound globally.
    }

    std::unique_ptr<IEffectRenderer> BgfxRenderer::CreateEffectRenderer(
        const std::string& /*vertSrc*/, const std::string& /*fragSrc*/)
    {
        return std::make_unique<BgfxEffectRenderer>();
    }

    void BgfxRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // REMED-GFX-195: a backbuffer read advances the frame even if a cube face remains publicly
        // bound, so publish that face's resolved image before the advance just as Present/GetData do.
        FinalizeCurrentCubeFaceEXT();

        // Task 951: force the reserved highest-id view (Detail::kBackbufferFlushViewId) to be the
        // last one bgfx processes this frame -- see that constant's own comment for why. Touching
        // it (rather than resetting any real render target's own view) never discards a still-
        // pending draw queued against a concurrently-active render target within this same
        // un-advanced frame.
        Detail::SetViewFrameBufferEXT(Detail::kBackbufferFlushViewId, BGFX_INVALID_HANDLE);
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
                "BgfxRenderer::ReadBackbuffer: screenshot callback did not fire");

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

    // --- BgfxOcclusionQueryRenderer ---

    BgfxOcclusionQueryRenderer::BgfxOcclusionQueryRenderer(BgfxRenderer* owner)
        : owner_(owner)
    {
        handle = bgfx::createOcclusionQuery();
    }

    BgfxOcclusionQueryRenderer::~BgfxOcclusionQueryRenderer()
    {
        if (owner_ && bgfx::isValid(owner_->activeOcclusionQuery_)
            && owner_->activeOcclusionQuery_.idx == handle.idx)
            owner_->activeOcclusionQuery_ = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
    }

    void BgfxOcclusionQueryRenderer::Begin()
    {
        if (owner_) owner_->activeOcclusionQuery_ = handle;
    }

    void BgfxOcclusionQueryRenderer::End()
    {
        if (owner_) owner_->activeOcclusionQuery_ = BGFX_INVALID_HANDLE;
    }

    bool BgfxOcclusionQueryRenderer::IsComplete() const
    {
        if (!bgfx::isValid(handle)) return false;
        return bgfx::getResult(handle) != bgfx::OcclusionQueryResult::NoResult;
    }

    int BgfxOcclusionQueryRenderer::PixelCount() const
    {
        if (!bgfx::isValid(handle)) return 0;
        int32_t result = 0;
        auto r = bgfx::getResult(handle, &result);
        if (r == bgfx::OcclusionQueryResult::Visible) return result;
        return 0;
    }

    std::unique_ptr<IOcclusionQueryRenderer> BgfxRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<BgfxOcclusionQueryRenderer>(this);
    }

    void BgfxRenderer::SubmitViewProgram(bgfx::ProgramHandle program)
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

    // REMED-GFX-078: safe replacement for the former static_cast<const BgfxTextureRenderer&> at every
    // 3D-effect sampler slot. `texture` (params.texture0/1, PBR maps, dual-texture, env-map's 2D
    // base) may be a BgfxTextureRenderer OR a BgfxRenderTargetRenderer (a RenderTarget2D used as an
    // effect texture -- legal XNA, since RenderTarget2D is-a Texture2D). Those are unrelated sibling
    // classes, so the old downcast was UB that read the wrong pooled handle for a render target;
    // both implement IBgfxSamplable, so the real handle is resolved through that. When the source is
    // a render-target color attachment on an originBottomLeft renderer, the slot (0-3) is recorded in
    // rtFlipV_ so the shader V-flips its sample (REMED-GFX-067's bottom-up-FBO compensation, applied
    // here at the generic-effect UV instead of at SpriteBatch's CPU-side quad).
    void BgfxRenderer::BindSamplerSlot(int slot, bgfx::UniformHandle sampler,
                                              const ITextureRenderer* texture,
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
    // bgfx::readTexture()) has been reached -- bgfx's readback is inherently deferred and the
    // destination is only guaranteed complete at that generation.
    //
    // REMED-GFX-154: the wait is bounded by THAT GENERATION, not by an attempt count. bgfx derives
    // the value it returns as `m_submit->m_frameNum + 2` (bgfx_p.h, Context::readTexture) and
    // bgfx::frame() advances the counter by exactly one and returns the new value, so the number of
    // frames still owed is derivable from what bgfx itself reported rather than guessed. The
    // previous spelling capped the loop at four attempts, which was a magic number sitting one step
    // above the three this path actually needs on the renderer used here -- a silent
    // `completed=false` (and therefore a NotSupportedException) waiting for the first machine that
    // needed one more. A counter that fails to advance is the only way out other than success, and
    // it is reported as a failure rather than spun on forever.
    static bool AdvanceFramesUntil(uint32_t targetFrame, int* outFramesAdvanced = nullptr,
                                   uint32_t* outReachedFrame = nullptr)
    {
        int advanced = 0;
        bool havePrevious = false;
        uint32_t previous = 0;
        for (;;)
        {
            const uint32_t current = bgfx::frame();
            ++advanced;
            if (outFramesAdvanced) *outFramesAdvanced = advanced;
            if (outReachedFrame) *outReachedFrame = current;
            if (current >= targetFrame) return true;
            if (havePrevious && current <= previous) return false;   // the counter stalled
            previous = current;
            havePrevious = true;
        }
    }

    bool BgfxRenderer::ReadTextureRegionEXT(bgfx::TextureHandle srcTexture, int level,
                                                   int x, int y, int w, int h, void* data,
                                                   int layer)
    {
        // Task 914's proven readback shape (BgfxTextureCubeRenderer/BgfxTexture3DRenderer::GetData):
        // a temporary BLIT_DST|READ_BACK texture is the only thing bgfx::readTexture accepts.
        const bgfx::TextureHandle readback = bgfx::createTexture2D(
            static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(readback))
        {
            // BGFX_CAPS_TEXTURE_BLIT/READ_BACK are not guaranteed on every renderer this renderer
            // can select. REMED-GFX-127: report the missing capability instead of returning with
            // the caller's buffer unwritten -- the shared layer turns this into a deterministic
            // System::NotSupportedException rather than a fabricated transparent-black frame.
            std::cerr << "CNA: bgfx RenderTarget2D::GetData readback texture creation failed -- "
                          "BGFX_CAPS_TEXTURE_BLIT/READ_BACK may not be supported on "
                       << bgfx::getRendererName(bgfx::getRendererType()) << "\n";
            return false;
        }

        // REMED-GFX-154: give the readback view a RENDER ITEM, and point it at the backbuffer.
        //
        // Queueing the blit on the reserved highest view id is necessary but NOT sufficient, and the
        // difference is what made the first read of a multisampled target come back all zero. A
        // renderer only VISITS a view that owns at least one render item: `RendererContextGL::submit`
        // iterates render items, and its per-view prologue is what calls `setFrameBuffer()` and then
        // `submitBlit(bs, view)`. A view holding nothing but a blit is never reached, so its blit
        // falls through to the tail `submitBlit(bs, BGFX_CONFIG_MAX_VIEWS)` -- which runs while the
        // PRODUCER's framebuffer is still the current one.
        //
        // That matters because a multisample RESOLVE is not performed at frame end. `setFrameBuffer`
        // is the only caller of `FrameBufferGL::resolve()`, and only when switching to a DIFFERENT
        // framebuffer: the resolve is the `glBlitFramebuffer` from `m_fbo[0]` (the multisample
        // renderbuffer) into `m_fbo[1]` (the single-sample texture that `colorTex` names). Copying
        // out of `colorTex` before that switch reads a texture nothing has written yet -- freshly
        // created and therefore zero-filled, so the readback SUCCEEDS over untouched memory instead
        // of failing, which is exactly the fabricated transparent-black result REMED-GFX-127/130
        // exist to forbid.
        //
        // Touching the view repairs both halves at once and in the right order: the renderer reaches
        // view kBackbufferFlushViewId, sees a framebuffer different from the producer's and resolves
        // it, and only THEN drains this blit. No extra public frame, Present, dummy target bind,
        // sleep, retry or second read is involved -- one empty submit on a view that is already
        // reserved for exactly this purpose. `ReadBackbuffer` has always done the same thing for the
        // same reason (Task 951); this path simply never learned it.
        Detail::SetViewFrameBufferEXT(Detail::kBackbufferFlushViewId, BGFX_INVALID_HANDLE);
        bgfx::touch(Detail::kBackbufferFlushViewId);

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
        int framesAdvanced = 0;
        uint32_t reachedFrame = 0;
        const bool completed = AdvanceFramesUntil(targetFrame, &framesAdvanced, &reachedFrame);
        if (traceReadback_)
        {
            ++readbackCallIndex_;
            std::cerr << "[GFX-154] readback#" << readbackCallIndex_
                      << " srcTex=" << srcTexture.idx << " level=" << level
                      << " layer=" << layer
                      << " rect=" << x << "," << y << " " << w << "x" << h
                      << " readbackTex=" << readback.idx
                      << " blitView=" << static_cast<int>(Detail::kBackbufferFlushViewId)
                      << " touchedBlitView=yes"
                      << " readTextureCompletionFrame=" << targetFrame
                      << " framesAdvanced=" << framesAdvanced
                      << " reachedFrame=" << reachedFrame
                      << " completed=" << (completed ? "yes" : "no")
                      << " cpuBytes=" << (static_cast<std::size_t>(w) * h * 4u) << "\n";
        }
        // bgfx::frame() ran, so this renderer's per-frame state must be recycled exactly as
        // ReadBackbuffer does after its own frame advance -- otherwise the segment view ids
        // allocated before this readback would be treated as still belonging to the current frame.
        spriteVpValid_ = false;
        EndFrameSegments();

        bgfx::destroy(readback);
        return completed;
    }

    // --- BgfxTextureCubeRenderer ---

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

    BgfxTextureCubeRenderer::BgfxTextureCubeRenderer(int size, bool mipMap, int /*surfaceFormat*/)
        : size_(size)
        , levelCount_(mipMap ? BgfxCubeMipLevels(size) : 1)
    {
        // Task 914: mipMap now genuinely threaded through (was hardcoded false) -- verifiable now
        // that GetData() (below) provides a real readback path to check mip-level content.
        creationFlags_ = BGFX_TEXTURE_NONE;
        handle = bgfx::createTextureCube(
            static_cast<uint16_t>(size),
            mipMap,
            1,
            bgfx::TextureFormat::RGBA8,
            creationFlags_);
    }

    BgfxTextureCubeRenderer::~BgfxTextureCubeRenderer()
    {
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
    }

    bool BgfxTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
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

    // Task 914: real GPU readback, previously a total no-op (the shared ITextureCubeRenderer
    // default silently left the caller's buffer untouched). bgfx::readTexture() requires the
    // SOURCE texture to be created with BGFX_TEXTURE_READ_BACK -- incompatible with this cube's
    // own shader-sampled `handle` ("can't be a GPU resource at the same time" per bgfx's own doc
    // comment) -- so a temporary plain 2D texture, sized exactly to the requested (w,h) region and
    // created with BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK, receives the requested face/mip
    // region via bgfx::blit() (dst is 2D so dstZ=0; src is a cube face so srcZ=face, per bgfx's own
    // blit() doc comment), then bgfx::readTexture() reads that temporary texture directly into the
    // caller's buffer. Confirmed BGFX_CAPS_TEXTURE_BLIT/READ_BACK are both supported in this
    // project's Xvfb/llvmpipe/OpenGL sandbox via a throwaway caps-log check before implementing.
    bool BgfxTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
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
            // renderer can select (confirmed present in this project's own Xvfb/llvmpipe/OpenGL
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

    std::unique_ptr<ITextureCubeRenderer> BgfxRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<BgfxTextureCubeRenderer>(size, mipMap, surfaceFormat);
    }

    // --- BgfxTexture3DRenderer ---

    BgfxTexture3DRenderer::BgfxTexture3DRenderer(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
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

    BgfxTexture3DRenderer::~BgfxTexture3DRenderer()
    {
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
    }

    bool BgfxTexture3DRenderer::SetData(int level, int x, int y, int z,
                                       int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        // REMED-GFX-135: see BgfxTextureCubeRenderer::SetData -- silent returns looked like writes.
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
    // BgfxTextureCubeRenderer::GetData's approach exactly, except the temporary readback texture is
    // itself a 3D texture (sized to the requested w/h/depth region) rather than a plain 2D one,
    // since a 3D source's _depth argument applies to blit regardless of the destination's own
    // dimensionality and this keeps both sides symmetric.
    bool BgfxTexture3DRenderer::GetData(int level, int x, int y, int z,
                                       int w, int h, int depth,
                                       void* data, int dataLength) const
    {
        // REMED-GFX-130: see BgfxTextureCubeRenderer::GetData -- silent returns fabricated a volume.
        if (!bgfx::isValid(handle) || !data || dataLength <= 0 || w <= 0 || h <= 0 || depth <= 0) return false;
        if (level < 0 || dataLength < w * h * depth * 4) return false;

        const bgfx::TextureHandle readback = bgfx::createTexture3D(
            static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(depth),
            false, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(readback))
        {
            // Task 455: same finding as BgfxTextureCubeRenderer::GetData above -- log clearly
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

    std::unique_ptr<ITexture3DRenderer> BgfxRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<BgfxTexture3DRenderer>(w, h, depth, mipMap, surfaceFormat);
    }

    // --- BgfxRenderTargetRenderer ---

    static bool MapDepthFormat(int depthFormat, ::bgfx::TextureFormat::Enum& outFormat);

    int Detail::NormalizeRenderTargetMsaaLimitEXT(uint32_t nativeLimit, bool colorSupported,
                                                   bool depthSupported)
    {
        if (!colorSupported || !depthSupported) return 0;
        const uint32_t capped = std::min<uint32_t>(nativeLimit, 16);
        if (capped >= 16) return 16;
        if (capped >= 8)  return 8;
        if (capped >= 4)  return 4;
        if (capped >= 2)  return 2;
        return 0;
    }

    /**
     * @brief Returns the active bgfx renderer's normalized render-target MSAA ceiling.
     *
     * REMED-GFX-185: the format bits answer whether RGBA8 and the requested depth format can be
     * multisampled at all; `Caps::Limits::maxMsaa` is the exact ceiling the native bgfx renderer
     * applies to `BGFX_TEXTURE_RT_MSAA_Xn`. The latter is supplied by the pinned bgfx capability
     * patch in `cmake/patches`, because upstream otherwise keeps the value private (`m_maxMsaa` on
     * OpenGL and the rewritten `s_msaa` tables on Vulkan/Direct3D/Metal).
     *
     * Returning one of 0/2/4/8/16 preserves CNA's established ClosestMSAAPower policy. In
     * particular, a native maximum that is not itself a power of two is rounded down before a
     * bgfx flag is chosen, so CNA never reports a count different from the one encoded for both
     * attachments.
     */
    static int BgfxRtMsaaLimit(int depthFormat)
    {
        const bgfx::Caps* caps = bgfx::getCaps();
        constexpr uint32_t required = BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA;
        const bool colorSupported =
            (caps->formats[bgfx::TextureFormat::RGBA8] & required) != 0;

        ::bgfx::TextureFormat::Enum depthBgfxFormat;
        const bool depthSupported = !MapDepthFormat(depthFormat, depthBgfxFormat) ||
            (caps->formats[depthBgfxFormat] & required) != 0;
        return Detail::NormalizeRenderTargetMsaaLimitEXT(
            caps->limits.maxMsaa, colorSupported, depthSupported);
    }

    // Task 878/879: BGFX_TEXTURE_RT_MSAA_X2/X4/X8/X16 occupy the same 4-bit field as
    // BGFX_TEXTURE_RT (see bgfx/defines.h's BGFX_TEXTURE_RT_MASK/RT_SHIFT) -- they are mutually
    // exclusive alternatives, not flags to OR alongside it. requestedMultiSampleCount arrives
    // already rounded to a power of two (or 0) by RenderTarget2D's/RenderTargetCube's own
    // ClosestMSAAPower step before it ever reaches the renderer, so this only needs to pick the
    // matching bgfx constant. REMED-GFX-185 additionally clamps to what the ACTIVE renderer will
    // really select, rather than reporting a larger flag request as if it had been applied.
    static uint64_t BgfxMsaaRtFlag(int requestedMultiSampleCount, int depthFormat, int& appliedOut)
    {
        const int capped = std::min(requestedMultiSampleCount, BgfxRtMsaaLimit(depthFormat));
        if (capped >= 16) { appliedOut = 16; return BGFX_TEXTURE_RT_MSAA_X16; }
        if (capped >= 8)  { appliedOut = 8;  return BGFX_TEXTURE_RT_MSAA_X8;  }
        if (capped >= 4)  { appliedOut = 4;  return BGFX_TEXTURE_RT_MSAA_X4;  }
        if (capped >= 2)  { appliedOut = 2;  return BGFX_TEXTURE_RT_MSAA_X2;  }
        appliedOut = 0;
        return BGFX_TEXTURE_RT;
    }

    // REMED-GFX-163: the two flag words every render-target attachment is built from. Both target
    // types used to spell this arithmetic out inline and they DRIFTED -- the cube path learned the
    // multisampled-depth rule under REMED-GFX-141 and its RenderTarget2D sibling never did, so a
    // legal `RenderTarget2D(dev, w, h, false, Color, Depth24, 4, usage)` killed the process. They
    // share these two helpers now so a rule can only ever be learned once, by both.

    /**
     * @brief Creation flags for a render target's COLOUR attachment.
     *
     * Sampler flags belong here because a colour attachment IS sampled -- that is the whole point of
     * rendering to a texture. bgfx resolves the multisampled content back into this same handle.
     */
    static uint64_t BgfxColorAttachmentFlags(uint64_t msaaFlag)
    {
        return msaaFlag | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    }

    /**
     * @brief Creation flags for a render target's DEPTH/STENCIL attachment.
     *
     * bgfx cannot resolve a multisampled depth surface, so `isFrameBufferValid` (bgfx.cpp, the
     * `bimg::isDepth` branch) reads the DEPTH TEXTURE'S OWN creation flags and requires that any
     * depth attachment whose `BGFX_TEXTURE_RT_MSAA_MASK` field decodes to more than one sample also
     * carries `BGFX_TEXTURE_RT_WRITE_ONLY` or `BGFX_TEXTURE_MSAA_SAMPLE`. It enforces that with a
     * BX_ASSERT -- a SIGTRAP that no public layer can catch and turn into a clean exception.
     *
     * `BGFX_TEXTURE_RT_WRITE_ONLY` is the correct half of that pair here, and it is this texture's
     * own truth rather than a way of silencing the assertion: CNA never samples, reads back or
     * resolves a render target's depth attachment: it is only depth-tested and depth-written while
     * the target is bound. `BGFX_TEXTURE_MSAA_SAMPLE` would instead keep a real
     * `GL_TEXTURE_2D_MULTISAMPLE` alive to be fetched per sample in a shader, which nothing here
     * does. bgfx's own Vulkan swapchain builds its depth attachment with exactly
     * `BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY` plus the sample field (renderer_vk.cpp,
     * SwapChainVK::createAttachments), so this matches the reference implementation's own choice.
     *
     * The flag is applied ONLY when the target really is multisampled. A single-sampled depth
     * attachment does not need it (bgfx's check exempts a sample field of 1 explicitly), and adding
     * it there would change the working path: bgfx's GL renderer creates a renderbuffer instead of a
     * texture when `writeOnly` is set, so a non-multisampled depth attachment would silently change
     * native resource kind for no reason. At two or more samples the renderbuffer is chosen by the
     * sample count alone, so this flag adds no native object and only suppresses the depth blit in
     * the resolve path -- which is precisely the resolve bgfx is telling us cannot be performed.
     *
     * The sample count itself is deliberately NOT reduced: colour and depth must report the same
     * `m_numSamples` or `isFrameBufferValid`'s "Mismatch in texture sample count" fires instead.
     */
    static uint64_t BgfxDepthAttachmentFlags(uint64_t msaaFlag, int appliedMsaa)
    {
        return appliedMsaa > 0 ? (msaaFlag | BGFX_TEXTURE_RT_WRITE_ONLY) : msaaFlag;
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
        // REMED-GFX-158: this renderer's own record of what every view's framebuffer binding is
        // meant to be. bgfx's view state is context state that survives a frame, and the only
        // thing other than this renderer that ever changes it is bgfx::reset(), which wipes all of
        // it -- so this array IS the intended state, and replaying it after a reset restores
        // precisely what the reset destroyed. Process-wide because bgfx itself is a process-wide
        // singleton.
        static std::array<bgfx::FrameBufferHandle, kMaxViews>& ViewFrameBufferMirror()
        {
            static std::array<bgfx::FrameBufferHandle, kMaxViews> mirror = [] {
                std::array<bgfx::FrameBufferHandle, kMaxViews> initial{};
                initial.fill(bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
                return initial;
            }();
            return mirror;
        }

        void SetViewFrameBufferEXT(bgfx::ViewId id, bgfx::FrameBufferHandle fb)
        {
            bgfx::setViewFrameBuffer(id, fb);
            if (static_cast<int>(id) < kMaxViews)
                ViewFrameBufferMirror()[id] = fb;
        }

        void ForgetFrameBufferEXT(bgfx::FrameBufferHandle fb)
        {
            if (!bgfx::isValid(fb)) return;
            for (auto& entry : ViewFrameBufferMirror())
                if (entry.idx == fb.idx)
                    entry = bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
        }

        void ResetBackbufferEXT(uint16_t width, uint16_t height, uint32_t flags)
        {
            const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
            const uint32_t formatCaps = bgfx::getCaps()->formats[kBackbufferFormat];
            const bool hasBackbuffer =
                (formatCaps & BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER) != 0;

            if (const char* trace = SDL_getenv("CNA_BGFX_TRACE_DIAGNOSTICS");
                trace != nullptr && trace[0] != '\0' && trace[0] != '0')
            {
                std::fprintf(stderr,
                             "[CNA bgfx reset] renderer=%s width=%u height=%u flags=0x%08x "
                             "format=BGRA8 formatCaps=0x%08x action=%s\n",
                             bgfx::getRendererName(rendererType), static_cast<unsigned>(width),
                             static_cast<unsigned>(height), static_cast<unsigned>(flags),
                             static_cast<unsigned>(formatCaps),
                             rendererType == bgfx::RendererType::Noop ? "skip-no-backbuffer" :
                             (hasBackbuffer ? "reset" : "reject"));
                std::fflush(stderr);
            }

            // bgfx's Noop renderer intentionally owns no swapchain/backbuffer. Its format table
            // advertises zero BACKBUFFER bits for every format, and renderer_noop.cpp's flip() is
            // empty. Width, height and interval remain CNA presentation state; there is no native
            // object to reconfigure and no rendering support to claim.
            if (rendererType == bgfx::RendererType::Noop)
                return;

            // Do not let a renderer/format negotiation defect cross bgfx's BX_ASSERT boundary.
            // A real renderer that cannot back its explicitly selected format is rejected before
            // reset with a catchable diagnostic, without falling back or fabricating the caps bit.
            if (!hasBackbuffer)
            {
                throw std::runtime_error(
                    std::string("BGFX renderer ") + bgfx::getRendererName(rendererType) +
                    " cannot reset CNA's BGRA8 backbuffer: "
                    "BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER is not advertised.");
            }

            bgfx::reset(width, height, flags, kBackbufferFormat);
            // Restore only the bindings that name a real framebuffer: reset has already left every
            // other view on the backbuffer, which is what an invalid entry means.
            const auto& mirror = ViewFrameBufferMirror();
            for (std::size_t id = 0; id < mirror.size(); ++id)
                if (bgfx::isValid(mirror[id]))
                    bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(id), mirror[id]);
        }
    }

    BgfxRenderTargetRenderer::BgfxRenderTargetRenderer(BgfxRenderer* owner,
                                                      int w, int h, int depthFormat, bool preserve,
                                                      int requestedMultiSampleCount, bool mipMap)
        : width(w), height(h), preserveContents(preserve), owner_(owner)
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
        const uint64_t msaaFlag = BgfxMsaaRtFlag(requestedMultiSampleCount, depthFormat, appliedMsaa);
        multiSampleCount = appliedMsaa;

        // Create color texture with render target flag (Task 878/879: an MSAA variant when
        // requested -- bgfx resolves the multisampled content into this same texture handle
        // internally; no explicit vkCmdResolveImage-style step or separate resolve texture is
        // needed on this renderer, unlike EasyGL/Vulkan). Task 906: hasMips=true (mipMap) makes
        // bgfx allocate the full mip chain on this same handle; the framebuffer this texture is
        // attached to below then automatically regenerates it -- see createFrameBuffer(numAttachments,
        // TextureHandle*, ...)'s own real-mips-aware BGFX_RESOLVE_AUTO_GEN_MIPS default (confirmed
        // in bgfx.cpp), triggered internally whenever bgfx switches away from this framebuffer
        // (TextureGL::resolve() -> glGenerateMipmap, and the equivalent per-renderer resolve path
        // on every other bgfx renderer). No custom shader/geometry needed, unlike Vulkan's
        // Task 878 vkCmdBlitImage cascade -- bgfx already has a real glGenerateMipmap-equivalent,
        // just gated behind the framebuffer-attachment resolve flag rather than surfaced on
        // bgfx::blit() (which really is a same-size copy only, as originally found).
        creationFlags_ = BgfxColorAttachmentFlags(msaaFlag);
        colorTex = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h),
            mipMap, 1, bgfx::TextureFormat::RGBA8,
            creationFlags_);

        bgfx::TextureHandle attachments[2] = { colorTex, BGFX_INVALID_HANDLE };
        int numAttachments = 1;

        ::bgfx::TextureFormat::Enum depthBgfxFormat;
        if (MapDepthFormat(depthFormat, depthBgfxFormat))
        {
            // Depth attachment must share the color attachment's sample count, and -- REMED-GFX-163 --
            // must say so with BGFX_TEXTURE_RT_WRITE_ONLY once that count exceeds one. See
            // BgfxDepthAttachmentFlags: without it bgfx aborts the process here rather than
            // returning an error this renderer could report.
            depthCreationFlags_ = BgfxDepthAttachmentFlags(msaaFlag, appliedMsaa);
            attachments[1] = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                false, 1, depthBgfxFormat, depthCreationFlags_);
            numAttachments = 2;
        }

        fbo = bgfx::createFrameBuffer(numAttachments, attachments, true);
    }

    BgfxRenderTargetRenderer::~BgfxRenderTargetRenderer()
    {
        if (bgfx::isValid(fbo))
        {
            // REMED-GFX-158: drop the mirrored bindings first -- bgfx recycles handle indices, so a
            // replay after this handle is gone could otherwise point a view at an unrelated
            // framebuffer that happened to inherit the index.
            Detail::ForgetFrameBufferEXT(fbo);
            bgfx::destroy(fbo);
        }
    }

    void BgfxRenderTargetRenderer::BindAsRenderTarget()
    {
        // REMED-GFX-179: binding selects only this framebuffer. The owning graphics renderer lazily
        // assigns the frame's next ordered view when a real draw/Clear commits to the binding.
    }

    void BgfxRenderTargetRenderer::UnbindAsRenderTarget()
    {
        // No persistent view binding exists to undo (REMED-GFX-179).
    }

    bool BgfxRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level >= mipLevels_)
            throw System::NotSupportedException(
                "BgfxRenderTargetRenderer::GetData: this render target has " +
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
        // result is top-left-origin like every other renderer. No-op on Vulkan/D3D/Metal.
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

    std::unique_ptr<IRenderTargetRenderer> BgfxRenderer::CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // multiSampleCount: BGFX_TEXTURE_RT_MSAA_Xn (Task 878/879) — see BgfxRenderTargetRenderer.
        // mipMap (Task 906): real mip chain, auto-regenerated by bgfx itself on every resolve.
        return std::make_unique<BgfxRenderTargetRenderer>(this, w, h, depthFormat, preserveContents,
                                                          multiSampleCount, mipMap);
    }

    void BgfxRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        FinalizeCurrentCubeFaceEXT();
        currentCubeTarget_ = nullptr;
        if (bgfx::isValid(mrtFbo_))
        {
            Detail::ForgetFrameBufferEXT(mrtFbo_);
            bgfx::destroy(mrtFbo_);
            mrtFbo_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mrtClearFbo_))
        {
            Detail::ForgetFrameBufferEXT(mrtClearFbo_);
            bgfx::destroy(mrtClearFbo_);
            mrtClearFbo_ = BGFX_INVALID_HANDLE;
        }
        if (rt)
        {
            auto* bgfxRt = static_cast<BgfxRenderTargetRenderer*>(rt);
            bgfxRt->BindAsRenderTarget();
            currentRtWidth_  = static_cast<uint16_t>(rt->GetWidth());
            currentRtHeight_ = static_cast<uint16_t>(rt->GetHeight());
            ResetSegmentTarget(bgfxRt->fbo);
        }
        else
        {
            Detail::SetViewFrameBufferEXT(0, BGFX_INVALID_HANDLE);
            currentRtWidth_ = currentRtHeight_ = 0;
            ResetSegmentTarget(BGFX_INVALID_HANDLE);
        }
    }

    void BgfxRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (bgfx::isValid(mrtFbo_))
        {
            Detail::ForgetFrameBufferEXT(mrtFbo_);
            bgfx::destroy(mrtFbo_);
            mrtFbo_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mrtClearFbo_))
        {
            Detail::ForgetFrameBufferEXT(mrtClearFbo_);
            bgfx::destroy(mrtClearFbo_);
            mrtClearFbo_ = BGFX_INVALID_HANDLE;
        }
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
        FinalizeCurrentCubeFaceEXT();
        currentCubeTarget_ = nullptr;
        for (int i = 0; i < count; ++i)
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "Bgfx SetRenderTargets: cube faces in a multi-target set are not "
                    "implemented by this CNA renderer.");
        // Multi-target: build a temporary framebuffer from the color textures. REMED-GFX-179's
        // lazy ordered allocator assigns it a view only when a draw/Clear actually uses it.
        static constexpr int kMaxAttachments = 8; // bgfx BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS default
        bgfx::Attachment attachments[kMaxAttachments];
        bgfx::Attachment clearAttachments[kMaxAttachments];
        int n = count < kMaxAttachments ? count : kMaxAttachments;
        for (int i = 0; i < n; ++i)
        {
            auto* bgfxRt = static_cast<BgfxRenderTargetRenderer*>(
                renderTargets[i].GetRenderTarget2D());
            // Every shader CNA can submit through this renderer has exactly one colour output.
            // Access::Write makes bgfx expose an attachment through the renderer's draw-buffer
            // list; on the OpenGL route its legacy gl_FragColor output is then broadcast to every
            // listed attachment.  The old persistent MRT base-view alias accidentally redirected
            // an earlier attachment set before execution and hid that effect.  GFX-179 gives each
            // ordered set its real view, so keep slot 0 writable and attach the remaining public
            // targets without advertising nonexistent shader outputs.  They retain their own
            // contents instead of receiving slot 0's colour, preserving the established MRT
            // attachment-set boundary while public draw order is no longer collapsed.
            attachments[i].init(bgfxRt->colorTex,
                                i == 0 ? bgfx::Access::Write : bgfx::Access::ReadWrite);
            clearAttachments[i].init(bgfxRt->colorTex);
        }
        mrtFbo_ = bgfx::createFrameBuffer(static_cast<uint8_t>(n), attachments);
        mrtClearFbo_ = bgfx::createFrameBuffer(static_cast<uint8_t>(n), clearAttachments);
        currentRtWidth_  = static_cast<uint16_t>(renderTargets[0].GetWidth());
        currentRtHeight_ = static_cast<uint16_t>(renderTargets[0].GetHeight());
        ResetSegmentTarget(mrtFbo_);
    }

    // --- BgfxRenderTargetCubeRenderer ---

    BgfxRenderTargetCubeRenderer::BgfxRenderTargetCubeRenderer(BgfxRenderer* owner, int size,
                                                              int depthFormat, bool mipMap,
                                                              int requestedMultiSampleCount)
        : owner_(owner), size_(size)
    {
        // REMED-GFX-134: what bgfx really allocates for this cube, so GetData can refuse a level
        // that has no storage instead of answering with a clamped one.
        levelCount_ = mipMap ? BgfxCubeMipLevels(size) : 1;
        // Task 903: BGFX_TEXTURE_RT_MSAA_Xn instead of plain BGFX_TEXTURE_RT when requested.
        int appliedMsaa = 0;
        const uint64_t msaaFlag = BgfxMsaaRtFlag(requestedMultiSampleCount, depthFormat, appliedMsaa);
        multiSampleCount = appliedMsaa;

        // REMED-GFX-195: bgfx's OpenGL TextureGL owns one `m_rbo` per TEXTURE, not per cube layer.
        // A multisampled cube texture therefore gives all six face framebuffers the same hidden
        // colour renderbuffer: A -> B -> partial-A reloads B. Keep single-sample cubes on the
        // established direct attachment path. For MSAA, expose a single-sample BLIT_DST cube and
        // render through six independently resolving 2D targets, one per face. Their mip chains are
        // generated by the same BGFX_RESOLVE_AUTO_GEN_MIPS attachment rule as before and copied
        // level-for-level by FinalizeCurrentCubeFaceEXT.
        if (appliedMsaa > 0 &&
            (bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_BLIT) == 0)
            throw System::NotSupportedException(
                "Bgfx RenderTargetCube: multisampling requires texture-blit support to preserve "
                "and publish independent cube faces.");

        creationFlags_ = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            (appliedMsaa > 0 ? BGFX_TEXTURE_BLIT_DST : BGFX_TEXTURE_RT);
        cubeTex = bgfx::createTextureCube(static_cast<uint16_t>(size), mipMap, 1,
                                           bgfx::TextureFormat::RGBA8,
                                           creationFlags_);
        if (!bgfx::isValid(cubeTex))
            throw System::NotSupportedException(
                "Bgfx RenderTargetCube: the requested cube texture could not be created.");

        if (appliedMsaa > 0)
        {
            const uint64_t producerFlags = BgfxColorAttachmentFlags(msaaFlag);
            msaaProducerCreationFlags_ = producerFlags;
            for (auto& faceTexture : msaaFaceColorTex_)
            {
                faceTexture = bgfx::createTexture2D(
                    static_cast<uint16_t>(size), static_cast<uint16_t>(size), mipMap, 1,
                    bgfx::TextureFormat::RGBA8, producerFlags);
                if (!bgfx::isValid(faceTexture))
                {
                    for (auto& created : msaaFaceColorTex_)
                        if (bgfx::isValid(created))
                        {
                            bgfx::destroy(created);
                            created = BGFX_INVALID_HANDLE;
                        }
                    bgfx::destroy(cubeTex);
                    cubeTex = BGFX_INVALID_HANDLE;
                    throw System::NotSupportedException(
                        "Bgfx RenderTargetCube: the requested per-face multisample storage could "
                        "not be created.");
                }
            }
        }
        // Task 877: single 2D depth/stencil texture shared across all 6 faces (mirrors
        // VulkanRenderTargetCubeRenderer's shared depthImage_) -- omitted entirely for
        // DepthFormat::None (previously RenderTargetCube had no depth attachment support at all
        // on this renderer).
        ::bgfx::TextureFormat::Enum depthBgfxFormat;
        if (MapDepthFormat(depthFormat, depthBgfxFormat))
        {
            // Depth attachment must share the color attachment's sample count (Task 903), and when
            // that count exceeds one it must also carry BGFX_TEXTURE_RT_WRITE_ONLY. REMED-GFX-141
            // discovered that rule here -- every depth-backed multisampled RenderTargetCube aborted
            // at the first BindAsRenderTargetFace, unnoticed because every existing bgfx cube MSAA
            // check used DepthFormat::None -- and Task 951's BGFX_RESOLVE_NONE on the ATTACHMENT is
            // not enough, because bgfx reads the TEXTURE's creation flags instead.
            //
            // REMED-GFX-163: the arithmetic that used to be spelled out here now lives in the shared
            // BgfxDepthAttachmentFlags, because RenderTarget2D needed the identical rule and did not
            // have it. Its full reasoning is documented there.
            depthCreationFlags_ = BgfxDepthAttachmentFlags(msaaFlag, appliedMsaa);
            depthTex = bgfx::createTexture2D(static_cast<uint16_t>(size), static_cast<uint16_t>(size),
                false, 1, depthBgfxFormat, depthCreationFlags_);
        }
        // The FBO is created per-face bind to attach the right face layer
        fbo = BGFX_INVALID_HANDLE;
    }

    BgfxRenderTargetCubeRenderer::~BgfxRenderTargetCubeRenderer()
    {
        // REMED-GFX-134: `fbo` is only an alias into faceFbos_, so it is never destroyed separately.
        for (auto& faceFbo : faceFbos_)
            if (bgfx::isValid(faceFbo))
            {
                Detail::ForgetFrameBufferEXT(faceFbo);   // REMED-GFX-158, see the 2D destructor
                bgfx::destroy(faceFbo);
            }
        fbo = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(cubeTex))  bgfx::destroy(cubeTex);
        for (auto& faceTexture : msaaFaceColorTex_)
            if (bgfx::isValid(faceTexture)) bgfx::destroy(faceTexture);
        if (bgfx::isValid(depthTex)) bgfx::destroy(depthTex);
    }

    void BgfxRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
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
            if (multiSampleCount > 0)
                atts[0].init(msaaFaceColorTex_[slot], bgfx::Access::Write);
            else
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
        boundFace_ = face;

        // REMED-GFX-179: no persistent/base view is repointed here. The owner assigns a fresh
        // ordered per-frame view to this face's framebuffer on its first real operation, so an
        // earlier face can never be redirected last-wins.
    }

    void BgfxRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        boundFace_ = -1;
    }

    bool BgfxRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        // REMED-GFX-134: closes the refusal this class inherited from ITextureCubeRenderer's
        // `return false` default.
        if (owner_ == nullptr || !bgfx::isValid(cubeTex) || data == nullptr) return false;
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        // REMED-GFX-138: GFX-134 measured untouched data here before the ordered completion below
        // existed. GFX-154 subsequently made that completion authoritative for every readback:
        // ending the producer frame resolves a multisampled cube attachment and runs its
        // per-face BGFX_RESOLVE_AUTO_GEN_MIPS before ReadTextureRegionEXT queues the blit. Direct
        // diagnostics now prove exact resolved level 0 and exact generated levels 1..N, including
        // the combined MSAA+mip case, so the two historical capability refusals are obsolete.

        // REMED-GFX-134: complete the current frame BEFORE queueing the readback blit. bgfx runs a
        // framebuffer's MSAA resolve and its BGFX_RESOLVE_AUTO_GEN_MIPS mip regeneration when it
        // tears the framebuffer down at frame end. A blit queued alongside this frame's draws
        // would otherwise copy pre-resolve memory even on the reserved highest view id.
        owner_->CompleteFrameForResolveEXT();

        // REMED-GFX-067: a render target's colour attachment stores its texel memory BOTTOM-UP on
        // originBottomLeft renderers (OpenGL/GLES/WebGL). BgfxRenderTargetRenderer::GetData already
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

    std::unique_ptr<IRenderTargetCubeRenderer> BgfxRenderer::CreateRenderTargetCube(int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-136: consumed by being deliberately unused. A bgfx view only clears what
        // setViewClear() tells it to, and this renderer leaves every render-target view at
        // BGFX_CLEAR_NONE unless a real Clear() call records flags for it (RecordClear), so a cube
        // face's attachment survives every bind by construction. The only clear a cube target ever
        // sees is the one GraphicsDevice::SetRenderTargets issues for a DiscardContents target.
        // mipMap (Task 907): real per-face mip chain, auto-regenerated by bgfx itself, same
        // mechanism as Task 906's RenderTarget2D fix.
        // multiSampleCount (Task 903): now wired up via BGFX_TEXTURE_RT_MSAA_Xn, mirroring
        // BgfxRenderTargetRenderer's Task 878/879 treatment -- see BgfxRenderTargetCubeRenderer's
        // constructor.
        return std::make_unique<BgfxRenderTargetCubeRenderer>(this, size, depthFormat, mipMap, multiSampleCount);
    }

    // Task 907 finding: the shared IGraphicsRenderer default only calls BindAsRenderTargetFace,
    // never updating currentRtWidth_/currentRtHeight_ -- without this override, EnsureViewState()
    // falls back to the full window size for any SpriteBatch draw into a cube face (the identical
    // bug shape Task 901 already fixed for 2D RenderTarget2D), corrupting every cube-face render.
    void BgfxRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        if (!rt) return;
        FinalizeCurrentCubeFaceEXT();
        auto* bgfxRt = static_cast<BgfxRenderTargetCubeRenderer*>(rt);
        bgfxRt->BindAsRenderTargetFace(face);
        currentCubeTarget_ = bgfxRt;
        currentRtWidth_  = static_cast<uint16_t>(rt->GetSize());
        currentRtHeight_ = static_cast<uint16_t>(rt->GetSize());
        ResetSegmentTarget(bgfxRt->fbo);
    }

    BgfxSpriteBatchRenderer::BgfxSpriteBatchRenderer(BgfxRenderer& graphicsRenderer)
        : graphicsRenderer(graphicsRenderer)
    {
    }

    void BgfxSpriteBatchRenderer::Begin()
    {
        begun = true;
    }

    void BgfxSpriteBatchRenderer::End()
    {
        begun = false;
    }

    void BgfxSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        // Task 878/879 (closes Task 873): use ITextureRenderer's own virtual GetWidth()/
        // GetHeight() instead of an unsafe static_cast<const BgfxTextureRenderer&> -- texture may
        // be a BgfxRenderTargetRenderer (an unrelated sibling class), not just BgfxTextureRenderer.
        Draw(
            texture,
            Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
            Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()),
            Color::White
        );
    }

    void BgfxSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle,
                                      const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None,
             0.0f);
    }

    void BgfxSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
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
            throw std::runtime_error("BgfxSpriteBatchRenderer::Draw called before Begin().");
        }

        // Task 878/879 (closes Task 873): texture may be a BgfxTextureRenderer OR a
        // BgfxRenderTargetRenderer (RenderTarget2D sampled after unbinding) -- both implement
        // IBgfxSamplable, so query the real handle through that instead of an unsafe
        // static_cast<const BgfxTextureRenderer&> that silently read the wrong pooled handle
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
        graphicsRenderer.ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        graphicsRenderer.SubmitSprite(handle, sourceIsRenderTarget, texture.GetWidth(), texture.GetHeight(),
                                     destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                                     layerDepth);
    }

    void BgfxSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void BgfxSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void BgfxSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        graphicsRenderer.spriteTransform_ = m;
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

    // REMED-GFX-154: bgfx's own diagnostics, on demand. See the declaration for why.
    void BgfxCnaCallback::traceVargs(const char* _filePath, uint16_t _line, const char* _format,
                                     va_list _argList)
    {
        if (!traceDiagnostics) return;
        char message[2048];
        std::vsnprintf(message, sizeof(message), _format, _argList);
        std::fprintf(stderr, "[bgfx] %s(%u): %s", _filePath ? _filePath : "?",
                     static_cast<unsigned>(_line), message);
        // bgfx's format strings usually already end in a newline; add one only when they do not.
        const std::size_t len = std::strlen(message);
        if (len == 0 || message[len - 1] != '\n') std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    BgfxRenderer::BgfxRenderer(SDL_Window* window, int swapInterval)
        : window(window)
        , resetFlags_(swapInterval > 0 ? BGFX_RESET_VSYNC : BGFX_RESET_NONE)
    {
        if (!window)
        {
            throw std::runtime_error("BgfxRenderer initialized with null window.");
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

        // REMED-GFX-154: decided BEFORE bgfx::init, because renderer selection and fallback are
        // among the things bgfx traces and they happen inside init itself.
        {
            const char* diag = SDL_getenv("CNA_BGFX_TRACE_DIAGNOSTICS");
            readbackCallback_.traceDiagnostics =
                diag != nullptr && diag[0] != '\0' && diag[0] != '0';
        }

        bgfx::Init init;
        init.type = requestedRendererType;
        init.vendorId = BGFX_PCI_ID_NONE;
        init.platformData = CreatePlatformData(window);
        init.resolution.width = initialWidth;
        init.resolution.height = initialHeight;
        init.resolution.reset = resetFlags_;
        init.resolution.formatColor = kBackbufferFormat;
        init.callback = &readbackCallback_;

        if (!init.platformData.nwh)
        {
            const char* videoDriver = SDL_GetCurrentVideoDriver();
            throw std::runtime_error(
                std::string("Failed to initialize BGFX renderer: native window handle is not available")
                + (videoDriver ? std::string(" for SDL video driver '") + videoDriver + "'." : ".")
            );
        }

        if (!bgfx::init(init))
        {
            throw std::runtime_error("bgfx::init failed.");
        }
        initialized = true;

        // REMED-GFX-157: make every view submit its draws in the order they were submitted.
        //
        // REMED-GFX-155 made the VIEWS execute in public order; this is the other half, the order
        // WITHIN a view. bgfx's default view mode sorts a view's draws by their sort key to
        // minimise state changes, so a SpriteBatch draw and a stock 3D draw issued into the same
        // bind cycle came out grouped by program rather than in the order the game issued them: the
        // sequence `3D draw; SpriteBatch.Draw` was inverted, and the sprite ended up underneath.
        // REMED-GFX-143's check O3 declared exactly that as an open defect on this renderer.
        //
        // ViewMode::Sequential is bgfx's own mechanism for "submission order is the contract", which
        // is what the XNA public API promises. It is set once here for every view id rather than in
        // the draw paths: view mode is persistent view state, this renderer never calls
        // bgfx::resetView, and doing it once costs nothing per frame and cannot be missed by a new
        // view id later. The trade is bgfx's state-change sorting, which is a throughput
        // optimisation that is not allowed to reorder observable output.
        for (int v = 0; v < Detail::kMaxViews; ++v)
            bgfx::setViewMode(static_cast<bgfx::ViewId>(v), bgfx::ViewMode::Sequential);

        // REMED-GFX-155: bgfx's view execution order is not observable through the public API, so it
        // is written to stderr on demand. Read once, here, so no draw path pays for the lookup.
        {
            const char* trace = SDL_getenv("CNA_BGFX_TRACE_VIEW_ORDER");
            traceViewOrder_ = trace != nullptr && trace[0] != '\0' && trace[0] != '0';
        }

        // REMED-GFX-154: same idea for the readback/resolve sequence, read once for the same reason.
        {
            const char* trace = SDL_getenv("CNA_BGFX_TRACE_READBACK");
            traceReadback_ = trace != nullptr && trace[0] != '\0' && trace[0] != '0';
        }

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
                // EasyGLRenderer::EnsureDefaultFlatNormalTexture()'s identical rationale.
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
            std::cout << "BGFX renderer requested renderer: " << RendererTypeName(requestedRendererType)
                << ", active renderer: " << bgfx::getRendererName(rendererType) << std::endl;

            // Task 456: one-time startup capability dump.
            {
                const bgfx::Caps* caps = bgfx::getCaps();
                if (rendererType == bgfx::RendererType::Noop)
                {
                    std::cout << "CNA: Bgfx capabilities -- Noop command-validation renderer; "
                                 "no rasterization, storage, native backbuffer/readback, or public "
                                 "rendering capabilities; BGRA8 backbuffer capability: "
                              << ((caps->formats[kBackbufferFormat] &
                                   BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER) ?
                                  "advertised" : "NOT advertised")
                              << "; render-target MSAA up to "
                              << static_cast<int>(caps->limits.maxMsaa)
                              << "x; SurfaceFormat negotiation: Color only (Task 176)" << std::endl;
                }
                else
                {
                    std::cout << "CNA: Bgfx capabilities -- MRT up to "
                              << std::min<int>(static_cast<int>(caps->limits.maxFBAttachments), 4)
                              << " targets (FNA MAX_RENDERTARGET_BINDINGS, device supports up to "
                              << static_cast<int>(caps->limits.maxFBAttachments) << "); "
                                 "render-target MSAA up to " << static_cast<int>(caps->limits.maxMsaa)
                              << "x; "
                                 "occlusion query: " << ((caps->supported & BGFX_CAPS_OCCLUSION_QUERY) ? "supported" : "NOT supported")
                              << "; texture blit/readback: "
                              << ((caps->supported & (BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK))
                                      == (BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK) ? "supported" : "NOT supported")
                              << "; anisotropic filtering: applied via BGFX_SAMPLER_ANISOTROPIC flags "
                                 "(device-dependent effect, no separate capability flag); "
                                 "SurfaceFormat: Color only (Task 176)" << std::endl;
                }
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

    bool BgfxRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // bgfx's Noop caps intentionally "pretend all features are available" so applications can
        // exercise command encoding without a GPU. CNA's public capability query instead promises
        // real rendering support. Noop rasterizes, stores and presents nothing, so reporting any of
        // these capabilities would be false even though its native validation harness accepts the
        // corresponding commands.
        if (bgfx::getRendererType() == bgfx::RendererType::Noop)
            return false;

        switch (capability)
        {
            case CNA::GraphicsCapability::OcclusionQuery:
                return (bgfx::getCaps()->supported & BGFX_CAPS_OCCLUSION_QUERY) != 0;
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: not yet implemented here. Every draw builds one bgfx::VertexLayout
                // from a single byte stride and submits it through setVertexBuffer(stream 0), so a
                // second per-vertex stream has no layout and no stream index. Reported honestly so
                // an ordinary multi-stream draw is rejected rather than rendered from stream 0.
                return false;
            default:
                return true;
        }
    }

    BgfxRenderer::~BgfxRenderer()
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
        if (bgfx::isValid(mrtFbo_))         { Detail::ForgetFrameBufferEXT(mrtFbo_); bgfx::destroy(mrtFbo_); mrtFbo_ = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(mrtClearFbo_))    { Detail::ForgetFrameBufferEXT(mrtClearFbo_); bgfx::destroy(mrtClearFbo_); mrtClearFbo_ = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(textureSampler))  { bgfx::destroy(textureSampler);  textureSampler  = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(spriteProgram))   { bgfx::destroy(spriteProgram);   spriteProgram   = BGFX_INVALID_HANDLE; }

        if (initialized)
        {
            bgfx::shutdown();
            initialized = false;
        }
    }

    void BgfxRenderer::EnsureViewState()
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
            // REMED-GFX-158: bgfx::reset() discards every view's framebuffer binding, including the
            // one belonging to the render target bound right now -- and this is reached from within
            // a bind cycle, between the bind and its first draw, whenever the window's real size
            // first differs from the size bgfx was initialised with. Restore what it drops.
            Detail::ResetBackbufferEXT(cachedWidth, cachedHeight, resetFlags_);
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

    void BgfxRenderer::RecordClear(uint16_t clearFlags)
    {
        // Every Clear is its own full-target ordered operation: reusing a view would collapse its
        // clear mask/value last-wins. Only the first backbuffer operation of a frame may use view 0;
        // every other operation draws from the unified monotonically increasing per-frame range.
        const bool initialBackbuffer = !segmentActive_ && !bgfx::isValid(segmentTargetFbo_) &&
                                       frameViewOrder_.empty();
        const bgfx::ViewId viewId = initialBackbuffer ? 0 : AllocateFrameViewId();
        const bool mrtClear = bgfx::isValid(mrtFbo_) && bgfx::isValid(mrtClearFbo_) &&
                              bgfx::isValid(segmentTargetFbo_) &&
                              segmentTargetFbo_.idx == mrtFbo_.idx;
        Detail::SetViewFrameBufferEXT(viewId, mrtClear ? mrtClearFbo_ : segmentTargetFbo_);
        currentViewId_ = spriteViewId = viewId;

        NoteViewUsedEXT(currentViewId_);   // REMED-GFX-155: public order, not numeric id order.
        segmentActive_ = true;
        segCurHasVp_ = false; // Clear ignores GraphicsDevice.Viewport and always owns the full target.
        segCurX_ = segCurY_ = segCurW_ = segCurH_ = 0;
        segCurSpriteTransformValid_ = false;
        currentViewClearFlags_ = clearFlags;

        EnsureViewState();
        bgfx::touch(spriteViewId);
        // The clear framebuffer exposes all MRT slots, while CNA's single-output draw framebuffer
        // exposes only slot 0. A following draw therefore needs its own ordered view/FBO binding;
        // keeping this clear view active would either broadcast slot 0 or stop clearing extras.
        if (mrtClear)
            segmentActive_ = false;
    }

    void BgfxRenderer::Clear(float r, float g, float b, float a)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        RecordClear(BGFX_CLEAR_COLOR);
    }

    void BgfxRenderer::Present()
    {
        FinalizeCurrentCubeFaceEXT();
        // A binding with no operation owns no view. Do not configure view 0 for such a pending bind
        // when earlier work exists, because bgfx view state is last-wins. With no earlier work,
        // EnsureViewState still performs any required backbuffer resize/reset.
        if (segmentActive_ || frameViewOrder_.empty())
            EnsureViewState();
        bgfx::frame();
        spriteVpValid_ = false; // REMED-GFX-072: sprite viewport is per-frame; clear for the next one.
        EndFrameSegments();     // REMED-GFX-179: recycle all public per-frame view ids.
    }

    void BgfxRenderer::SetSwapInterval(int interval)
    {
        resetFlags_ = (interval > 0) ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
        // REMED-GFX-158: a vsync change is a reset like any other and discards the same bindings.
        Detail::ResetBackbufferEXT(cachedWidth, cachedHeight, resetFlags_);
    }

    void BgfxRenderer::GetViewportSize(int& width, int& height)
    {
        if (!SDL_GetWindowSize(window, &width, &height))
        {
            throw std::runtime_error(std::string("SDL_GetWindowSize failed: ") + SDL_GetError());
        }
    }

    std::unique_ptr<ITextureRenderer> BgfxRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<BgfxTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> BgfxRenderer::CreateSpriteBatch()
    {
        return std::make_unique<BgfxSpriteBatchRenderer>(*this);
    }

    void BgfxRenderer::SubmitSprite(bgfx::TextureHandle textureHandle, bool sourceIsRenderTarget,
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
        // Mirror each absolute V around the whole texture for render-target sources so both a full
        // texture and a partial source rectangle select the same logical rows as an uploaded
        // Texture2D. This composes with FlipVertically above without changing the selected region.
        // No-op on Vulkan/D3D/Metal (originBottomLeft == false), where FBO memory is top-down.
        if (sourceIsRenderTarget && bgfx::getCaps()->originBottomLeft)
        {
            v1 = 1.0f - v1;
            v2 = 1.0f - v2;
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
        bgfx::setState(colorWriteFlags_ | kMsaaRasterState
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

    void BgfxRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
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

    void BgfxRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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

    void BgfxRenderer::RebuildStencilState()
    {
        if (stencilEnableCached_)
        {
            if (twoSidedStencilModeCached_)
            {
                // Task 763 empirical finding, mirrors Task 870's identical Vulkan fix: this
                // renderer never sets BGFX_STATE_FRONT_CCW in ApplyRasterizerState, so bgfx's own
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

    void BgfxRenderer::SetReferenceStencil(int value)
    {
        referenceStencilCached_ = value;
        RebuildStencilState();
    }

    void BgfxRenderer::ApplyRasterizerState(int cullMode, int fillMode,
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

    bool BgfxRenderer::ExpandWireframeIndices(const BgfxIndexBufferRenderer* ib,
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

    void BgfxRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) { scissorW_ = scissorH_ = 0; return; }
        scissorX_ = static_cast<uint16_t>(x);
        scissorY_ = static_cast<uint16_t>(y);
        scissorW_ = static_cast<uint16_t>(w);
        scissorH_ = static_cast<uint16_t>(h);
    }

    void BgfxRenderer::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
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

    // REMED-GFX-179: one unified ordered per-frame range for target binds, later backbuffer binds,
    // viewport/transform transitions, clears, and internal cube publication. The cursor is reset
    // after every bgfx::frame(); 255 remains reserved for readback/flush. Throw rather than wrap,
    // because reusing a same-frame id would redirect earlier work through last-written view state.
    bgfx::ViewId BgfxRenderer::AllocateFrameViewId()
    {
        if (frameNextViewId_ >= Detail::kBackbufferFlushViewId)
            throw std::runtime_error(
                "Bgfx: exhausted per-frame ordered view ids (more than 254 target-bind/viewport/"
                "transform/clear/resolve states in one frame; view 255 is reserved for readback)");
        return frameNextViewId_++;
    }

    // REMED-GFX-179: binding selects a framebuffer but consumes no view. The first real operation
    // allocates and programs its ordered per-frame view lazily.
    void BgfxRenderer::ResetSegmentTarget(bgfx::FrameBufferHandle fbo)
    {
        segmentTargetFbo_    = fbo;
        segmentActive_       = false;
        currentViewId_ = spriteViewId = 0;
        currentViewClearFlags_ = BGFX_CLEAR_NONE;
    }

    void BgfxRenderer::FinalizeCurrentCubeFaceEXT()
    {
        if (currentCubeTarget_ == nullptr || currentCubeTarget_->multiSampleCount <= 0 ||
            currentCubeTarget_->boundFace_ < 0 || !segmentActive_)
            return;

        const int face = currentCubeTarget_->boundFace_;
        const bgfx::TextureHandle source =
            currentCubeTarget_->msaaFaceColorTex_[static_cast<std::size_t>(face)];
        if (!bgfx::isValid(source) || !bgfx::isValid(currentCubeTarget_->cubeTex))
            return;

        // A real render item makes the renderer enter this ordered view. Its backbuffer binding
        // differs from the producer framebuffer, so the transition performs bgfx's hidden MSAA
        // resolve and mip generation BEFORE this view's blits are drained (the same measured rule
        // REMED-GFX-154's readback view relies on).
        const bgfx::ViewId resolveView = AllocateFrameViewId();
        Detail::SetViewFrameBufferEXT(resolveView, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(resolveView, 0, 0, 1, 1);
        bgfx::setViewClear(resolveView, BGFX_CLEAR_NONE,
                           clearRgba, clearDepthValue_, clearStencilValue_);
        NoteViewUsedEXT(resolveView);
        bgfx::touch(resolveView);

        for (int level = 0; level < currentCubeTarget_->levelCount_; ++level)
        {
            const uint16_t edge = static_cast<uint16_t>(
                std::max(1, currentCubeTarget_->size_ >> level));
            bgfx::blit(resolveView,
                       currentCubeTarget_->cubeTex, static_cast<uint8_t>(level), 0, 0,
                       static_cast<uint16_t>(face),
                       source, static_cast<uint8_t>(level), 0, 0, 0, edge, edge, 1);
        }

        // The copy view itself is now the latest public-order dependency. A subsequent operation
        // takes the next monotonically increasing ID, preserving producer -> resolve -> consumer.
        segmentActive_ = false;
    }

    // REMED-GFX-179: recycle the entire public per-frame ID range. The bound framebuffer survives,
    // but owns no view until the next frame's first real operation.
    void BgfxRenderer::EndFrameSegments()
    {
        frameNextViewId_ = Detail::kFirstPublicFrameViewId;
        segmentActive_  = false;
        currentViewId_  = 0;
        spriteViewId    = 0;
        currentViewClearFlags_ = BGFX_CLEAR_NONE;
        // REMED-GFX-155: the frame's programmed view order belongs to the frame that just ended.
        // Reprogramming right here (rather than only before the next bgfx::frame()) matters because
        // bgfx's remap table is CONTEXT state that survives a frame: leaving the previous frame's
        // permutation installed would order the next frame's first commands by a stale rule.
        if (traceViewOrder_)
        {
            std::fprintf(stderr, "[bgfx-view-order] frame used %zu view(s) in public order:",
                         frameViewOrder_.size());
            for (const bgfx::ViewId id : frameViewOrder_)
                std::fprintf(stderr, " %u", static_cast<unsigned>(id));
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
        frameViewOrder_.clear();
        frameViewOrdered_.fill(false);
        highestViewUsedThisFrame_ = -1;
    }

    // REMED-GFX-155: append @p id to this frame's public submission order the first time a command
    // commits to it, and reprogram bgfx's execution order to match. Called from the only two places
    // a view becomes the active submission target -- SelectViewportSegment() (both draw paths) and
    // RecordClear(). Reprogramming HERE rather than once before bgfx::frame() keeps the order valid
    // at every one of this renderer's frame-advance sites, including the readback paths that advance
    // a frame from a free function with no access to this object. Every public id is below the
    // reserved kBackbufferFlushViewId by construction.
    void BgfxRenderer::NoteViewUsedEXT(bgfx::ViewId id)
    {
        if (id >= Detail::kMaxViews)
            return;
        if (static_cast<int>(id) > highestViewUsedThisFrame_)
            highestViewUsedThisFrame_ = static_cast<int>(id);
        if (frameViewOrdered_[id])
            return;
        frameViewOrdered_[id] = true;
        frameViewOrder_.push_back(id);
    }

    // REMED-GFX-065: resolve whether the active GraphicsDevice.Viewport is a genuine CUSTOM sub-region
    // of the current target (vs the full-target/default viewport), and hand back the target's full pixel
    // size. Mirrors EnsureViewState()'s own `spriteCustomVp` test exactly: a full-target viewport (what
    // GraphicsDevice resets on every SetRenderTarget) must NOT be treated as custom, else every ordinary
    // draw would needlessly segment. A nonzero current target size identifies a render target;
    // backbuffer bindings reset both dimensions to zero.
    bool BgfxRenderer::CurrentCustomViewport(uint16_t& fullW, uint16_t& fullH) const
    {
        fullW = currentRtWidth_  > 0 ? currentRtWidth_  : cachedWidth;
        fullH = currentRtHeight_ > 0 ? currentRtHeight_ : cachedHeight;
        return viewportSet_ && viewportW_ > 0 && viewportH_ > 0 &&
               (viewportX_ != 0 || viewportY_ != 0 || viewportW_ != fullW || viewportH_ != fullH);
    }

    // REMED-GFX-065/GFX-179: pick the view this draw/batch submits to. A binding's first real draw
    // lazily receives an ordered per-frame view (or 0 for the frame's initial backbuffer work).
    // A later viewport/transform transition receives the next view. Consecutive compatible draws
    // reuse the active view, so draw count itself causes no ID growth.
    void BgfxRenderer::SelectViewportSegment(bool spritePath)
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
            const bool initialBackbuffer = !bgfx::isValid(segmentTargetFbo_) &&
                                           frameViewOrder_.empty();
            const bgfx::ViewId viewId = initialBackbuffer ? 0 : AllocateFrameViewId();
            Detail::SetViewFrameBufferEXT(viewId, segmentTargetFbo_);
            currentViewId_ = spriteViewId = viewId;

            NoteViewUsedEXT(currentViewId_);   // REMED-GFX-155: public order, not numeric id order.
            segmentActive_ = true;
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
            // Reuse the active view -> no allocation. If it has no committed sprite
            // transform yet (e.g. established by a 3D draw), adopt this batch's now so a later differing
            // transform on the same viewport starts a new ordered segment.
            if (spritePath && !segCurSpriteTransformValid_)
            { segCurSpriteTransformValid_ = true; segCurSpriteTransform_ = spriteTransform_; }
            return;
        }

        // A different viewport OR sprite transform appeared on this binding within the frame.
        const bgfx::ViewId segId = AllocateFrameViewId();
        Detail::SetViewFrameBufferEXT(segId, segmentTargetFbo_);
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
        NoteViewUsedEXT(segId);   // REMED-GFX-155: public order, not numeric id order.
        segCurHasVp_   = hasVp;
        segCurX_ = viewportX_; segCurY_ = viewportY_; segCurW_ = viewportW_; segCurH_ = viewportH_;
        commitSpriteTransform();
    }

    void BgfxRenderer::ApplyViewportOverride()
    {
        // REMED-GFX-065: pick/allocate this draw's ordered view BEFORE setting the rect, so a
        // second viewport's sub-rect lands on its OWN view rather than clobbering the first
        // viewport's draws on a shared view. REMED-GFX-084: spritePath=false -- the 3D path never programs
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

    void BgfxRenderer::ApplyScissorOverride()
    {
        // Task 768: bgfx::setScissor is per-draw-call state (bgfx::RenderDraw::m_scissor resets
        // to "no scissor" for every draw unless set again) -- must be called before every 3D
        // submit(). Gated on scissorEnabled_ (not just a non-zero rect -- see that member's own
        // declaration comment for why the two must be tracked independently).
        if (scissorEnabled_ && scissorW_ > 0 && scissorH_ > 0)
            bgfx::setScissor(scissorX_, scissorY_, scissorW_, scissorH_);
    }

    void BgfxRenderer::ApplySamplerState(int slot, int filter,
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
        // EasyGLRenderer::ApplySamplerState, which already maps all 9 values correctly via
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

    void BgfxRenderer::SetBlendFactor(float r, float g, float b, float a)
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
            "Bgfx renderer: SetDepthTestEnabled / SetBlend* "
            "are not yet wired into bgfx state flags.");
    }

    void BgfxRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        clearDepthValue_ = depth;
        RecordClear(static_cast<uint16_t>(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH));
    }

    void BgfxRenderer::ClearDepth(float depth)
    {
        clearDepthValue_ = depth;
        RecordClear(BGFX_CLEAR_DEPTH);
    }

    void BgfxRenderer::ClearStencil(int stencil)
    {
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(BGFX_CLEAR_STENCIL);
    }

    void BgfxRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        clearDepthValue_ = depth;
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(static_cast<uint16_t>(BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL));
    }

    void BgfxRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(static_cast<uint16_t>(BGFX_CLEAR_COLOR | BGFX_CLEAR_STENCIL));
    }

    void BgfxRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        clearRgba = ToRgba(ToByte(r), ToByte(g), ToByte(b), ToByte(a));
        clearDepthValue_ = depth;
        clearStencilValue_ = static_cast<uint8_t>(stencil);
        RecordClear(static_cast<uint16_t>(
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL));
    }

    void BgfxRenderer::SetDepthTestEnabled(bool)  { ThrowNo3DState(); }
    void BgfxRenderer::SetBlendEnabled(bool)      { ThrowNo3DState(); }
    void BgfxRenderer::SetDepthWriteEnabled(bool) { ThrowNo3DState(); }

    // --- BgfxVertexBufferRenderer ---

    // REMED-GFX-216: the authoritative description of a vertex stream is its VertexDeclaration --
    // usage, usage index, byte offset, format, component count and normalization, per element. The
    // stride only states the distance between records and cannot determine their composition:
    // `Position@0 + Color@12` and `Color@0 + Position@4` are both stride 16, and `Position@0 +
    // Color@12` is legal at stride 16 and at stride 32. Everything below derives the native layout
    // from the declaration; MakeBgfxLayout(stride) survives only as the pre-declaration default for
    // a buffer that has not been given one yet.

    /// One XNA vertex-element format expressed in bgfx's terms, plus the byte span the declaration
    /// spends on it -- which is what lets the builder verify that bgfx's own per-renderer attribute
    /// size agrees with the declaration instead of assuming it.
    struct BgfxElementFormat
    {
        bgfx::AttribType::Enum type = bgfx::AttribType::Float;
        uint8_t count = 0;
        bool normalized = false;
        uint8_t declaredBytes = 0;
        bool supported = false;
    };

    static BgfxElementFormat DescribeElementFormat(VertexElementFormat format)
    {
        // `asInt` is deliberately false for every entry. bgfx's `_asInt` selects the integer
        // attribute path (glVertexAttribIPointer and its equivalents), and every attribute the
        // shaders in this renderer declare is a float vector -- varying.def.sc declares
        // `vec4 a_indices : BLENDINDICES`, not an ivec4. Passing Byte4 as an integer attribute
        // would break the skinned bone-index lookup, so these arrive converted, exactly as the
        // stride table did before this task.
        switch (format)
        {
            case VertexElementFormat::Single:
                return {bgfx::AttribType::Float, 1, false, 4, true};
            case VertexElementFormat::Vector2:
                return {bgfx::AttribType::Float, 2, false, 8, true};
            case VertexElementFormat::Vector3:
                return {bgfx::AttribType::Float, 3, false, 12, true};
            case VertexElementFormat::Vector4:
                return {bgfx::AttribType::Float, 4, false, 16, true};
            case VertexElementFormat::Color:
                return {bgfx::AttribType::Uint8, 4, true, 4, true};
            case VertexElementFormat::Byte4:
                return {bgfx::AttribType::Uint8, 4, false, 4, true};
            case VertexElementFormat::Short2:
                return {bgfx::AttribType::Int16, 2, false, 4, true};
            case VertexElementFormat::Short4:
                return {bgfx::AttribType::Int16, 4, false, 8, true};
            case VertexElementFormat::NormalizedShort2:
                return {bgfx::AttribType::Int16, 2, true, 4, true};
            case VertexElementFormat::NormalizedShort4:
                return {bgfx::AttribType::Int16, 4, true, 8, true};
            case VertexElementFormat::HalfVector2:
                return {bgfx::AttribType::Half, 2, false, 4, true};
            case VertexElementFormat::HalfVector4:
                return {bgfx::AttribType::Half, 4, false, 8, true};
        }
        return {};
    }

    /// Maps an XNA usage + usage index onto the single bgfx attribute that carries it. bgfx has one
    /// slot per semantic for Position/Normal/Tangent/Bitangent/Indices/Weight and an indexed family
    /// for Color and TexCoord, so a usage index those families cannot express is reported rather
    /// than folded onto slot 0.
    static bool MapElementUsage(VertexElementUsage usage, int usageIndex,
                                bgfx::Attrib::Enum& out)
    {
        const auto indexed = [&](bgfx::Attrib::Enum base, int limit) {
            if (usageIndex < 0 || usageIndex > limit) return false;
            out = static_cast<bgfx::Attrib::Enum>(static_cast<int>(base) + usageIndex);
            return true;
        };
        const auto single = [&](bgfx::Attrib::Enum attrib) {
            if (usageIndex != 0) return false;
            out = attrib;
            return true;
        };
        switch (usage)
        {
            case VertexElementUsage::Position:          return single(bgfx::Attrib::Position);
            case VertexElementUsage::Normal:            return single(bgfx::Attrib::Normal);
            case VertexElementUsage::Tangent:           return single(bgfx::Attrib::Tangent);
            case VertexElementUsage::Binormal:          return single(bgfx::Attrib::Bitangent);
            case VertexElementUsage::BlendIndices:      return single(bgfx::Attrib::Indices);
            case VertexElementUsage::BlendWeight:       return single(bgfx::Attrib::Weight);
            case VertexElementUsage::Color:             return indexed(bgfx::Attrib::Color0, 3);
            case VertexElementUsage::TextureCoordinate: return indexed(bgfx::Attrib::TexCoord0, 15);
            default:                                    return false;
        }
    }

    static const char* DescribeUsageName(VertexElementUsage usage)
    {
        switch (usage)
        {
            case VertexElementUsage::Position:          return "Position";
            case VertexElementUsage::Color:             return "Color";
            case VertexElementUsage::TextureCoordinate: return "TextureCoordinate";
            case VertexElementUsage::Normal:            return "Normal";
            case VertexElementUsage::Binormal:          return "Binormal";
            case VertexElementUsage::Tangent:           return "Tangent";
            case VertexElementUsage::BlendIndices:      return "BlendIndices";
            case VertexElementUsage::BlendWeight:       return "BlendWeight";
            case VertexElementUsage::Depth:             return "Depth";
            case VertexElementUsage::Fog:               return "Fog";
            case VertexElementUsage::PointSize:         return "PointSize";
            case VertexElementUsage::Sample:            return "Sample";
            case VertexElementUsage::TessellateFactor:  return "TessellateFactor";
        }
        return "Unknown";
    }

    /// Advances the layout's running offset to @p target. bgfx's `skip` takes a uint8_t, so a gap
    /// wider than 255 bytes is walked in steps.
    static void SkipTo(bgfx::VertexLayout& layout, int target)
    {
        while (layout.getStride() < target)
        {
            const int remaining = target - layout.getStride();
            layout.skip(static_cast<uint8_t>(remaining > 255 ? 255 : remaining));
        }
    }

    /// Builds the native layout that @p declaration describes, exactly.
    ///
    /// bgfx places an attribute at the layout's current running offset and advances it by that
    /// attribute's size *for the active renderer* (`s_attribTypeSize` is per-renderer: Int16/Half
    /// with three components is 6 bytes on GL and 8 on D3D/Vulkan/WebGPU). The builder therefore
    /// never assumes where an `add` landed -- it skips to each element's declared offset first and
    /// then verifies that bgfx's own advance matched the declaration's byte span. A declaration
    /// this renderer cannot express natively is rejected here, before anything is created or
    /// submitted, rather than being replaced by a nearby built-in guess.
    static bgfx::VertexLayout MakeBgfxLayout(const VertexDeclaration& declaration)
    {
        const std::vector<VertexElement>& elements = declaration.GetVertexElements();
        const int declaredStride = declaration.getVertexStrideProperty();

        // Declaration order is not required to be offset order, and bgfx builds strictly forward.
        std::vector<const VertexElement*> ordered;
        ordered.reserve(elements.size());
        for (const VertexElement& element : elements)
            ordered.push_back(&element);
        std::sort(ordered.begin(), ordered.end(),
                  [](const VertexElement* a, const VertexElement* b) {
                      return a->getOffsetProperty() < b->getOffsetProperty();
                  });

        bgfx::VertexLayout layout;
        layout.begin();
        bool seen[bgfx::Attrib::Count] = {};
        for (const VertexElement* element : ordered)
        {
            const VertexElementUsage usage = element->getVertexElementUsageProperty();
            const int usageIndex = element->getUsageIndexProperty();
            const int offset = element->getOffsetProperty();

            bgfx::Attrib::Enum attrib = bgfx::Attrib::Count;
            if (!MapElementUsage(usage, usageIndex, attrib))
                throw System::NotSupportedException(
                    std::string("The bgfx renderer cannot bind vertex element usage ") +
                    DescribeUsageName(usage) + std::to_string(usageIndex) +
                    ": it has no native attribute for that semantic.");
            if (seen[attrib])
                throw System::NotSupportedException(
                    std::string("The vertex declaration binds ") + DescribeUsageName(usage) +
                    std::to_string(usageIndex) +
                    " more than once; a single stream cannot repeat a semantic.");
            seen[attrib] = true;

            const BgfxElementFormat format =
                DescribeElementFormat(element->getVertexElementFormatProperty());
            if (!format.supported)
                throw System::NotSupportedException(
                    std::string("The bgfx renderer cannot bind the vertex element format of ") +
                    DescribeUsageName(usage) + std::to_string(usageIndex) + '.');

            if (offset < static_cast<int>(layout.getStride()))
                throw System::NotSupportedException(
                    std::string("The vertex declaration places ") + DescribeUsageName(usage) +
                    std::to_string(usageIndex) + " at byte " + std::to_string(offset) +
                    ", which overlaps the preceding element; bgfx vertex layouts are strictly "
                    "forward-ordered.");
            SkipTo(layout, offset);

            layout.add(attrib, format.count, format.type, format.normalized);

            // bgfx's advance is renderer-dependent. If it disagrees with the declaration's own
            // byte span, every later element would silently land at the wrong offset -- say so
            // instead.
            const int expectedEnd = offset + static_cast<int>(format.declaredBytes);
            if (static_cast<int>(layout.getStride()) != expectedEnd)
                throw System::NotSupportedException(
                    std::string("The active bgfx renderer stores ") + DescribeUsageName(usage) +
                    std::to_string(usageIndex) + " in " +
                    std::to_string(static_cast<int>(layout.getStride()) - offset) +
                    " bytes, but the vertex declaration allots " +
                    std::to_string(static_cast<int>(format.declaredBytes)) + '.');

            if (expectedEnd > declaredStride)
                throw System::NotSupportedException(
                    std::string("The vertex declaration places ") + DescribeUsageName(usage) +
                    std::to_string(usageIndex) + " outside its own " +
                    std::to_string(declaredStride) + "-byte stride.");
        }

        // Trailing padding is part of the declaration: the record spacing is the declared stride,
        // not the sum of the elements.
        SkipTo(layout, declaredStride);
        layout.end();
        return layout;
    }

    static bgfx::VertexLayout MakeBgfxLayout(std::size_t stride)
    {
        bgfx::VertexLayout layout;
        layout.begin();
        if (stride == 52)
        {
            // Task 11.10: this layout is independently duplicated (magic stride 52) in
            // EasyGLRenderer.cpp's ApplyLayout and VulkanRenderer.cpp's
            // GetOrCreatePipelineSkinned3D - see EasyGLRenderer.cpp's own comment at its
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
            // handedness in w) + uv(2f=8) -- see EasyGLRenderer.cpp's own "case 48"
            // comment for the full cross-renderer-duplication note.
            layout.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Tangent,   4, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
        }
        else if (stride == 56)
        {
            // CNB-67 (Phase 13C) Bgfx port: the stride-52 SkinnedVertex layout with a per-vertex
            // Color (normalized ubyte4) appended at the end (offset 52), matching
            // EasyGLRenderer.cpp's own "case 56" comment exactly.
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
            // EasyGLRenderer.cpp's own "case 68" comment exactly.
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

    BgfxVertexBufferRenderer::BgfxVertexBufferRenderer(int capacity)
        : capacity_(capacity)
    {
        layout = MakeBgfxLayout(16);
        handle = bgfx::createDynamicVertexBuffer(
            static_cast<uint32_t>(capacity),
            layout,
            BGFX_BUFFER_ALLOW_RESIZE);
    }

    BgfxVertexBufferRenderer::~BgfxVertexBufferRenderer()
    {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }

    // REMED-GFX-216: two declarations that share a stride are different layouts, so the stored
    // declaration is compared element by element, not by stride. This runs immediately before every
    // real upload (VertexBuffer::UploadValidatedData), so the layout SetData builds below is always
    // the one the caller actually declared.
    void BgfxVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        const auto sameDeclaration = [&] {
            if (!hasDeclaration_) return false;
            if (declaration_.getVertexStrideProperty() !=
                vertexDeclaration.getVertexStrideProperty())
                return false;
            const std::vector<VertexElement>& a = declaration_.GetVertexElements();
            const std::vector<VertexElement>& b = vertexDeclaration.GetVertexElements();
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (a[i].getOffsetProperty() != b[i].getOffsetProperty() ||
                    a[i].getVertexElementFormatProperty() !=
                        b[i].getVertexElementFormatProperty() ||
                    a[i].getVertexElementUsageProperty() !=
                        b[i].getVertexElementUsageProperty() ||
                    a[i].getUsageIndexProperty() != b[i].getUsageIndexProperty())
                    return false;
            }
            return true;
        };
        if (sameDeclaration()) return;
        declaration_ = vertexDeclaration;
        hasDeclaration_ = true;
        declarationChanged_ = true;
    }

    void BgfxVertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        vertexCount = vertex_count;
        // REMED-GFX-216: the layout is rebuilt when the DECLARATION changes as well as when the
        // stride does. A stride comparison alone cannot see a re-declaration at the same stride,
        // which is precisely the collision this task exists for.
        const bool layoutChanged = stride_in_bytes != stride || declarationChanged_;
        if (layoutChanged)
        {
            stride = stride_in_bytes;
            // A declaration is authoritative when there is one. The stride overload remains only
            // for a buffer that has not been given a declaration yet -- it is a default, not a
            // description, and it is what this task removed from the normal path.
            layout = hasDeclaration_ && !declaration_.GetVertexElements().empty()
                         ? MakeBgfxLayout(declaration_)
                         : MakeBgfxLayout(stride_in_bytes);
            declarationChanged_ = false;
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

    std::unique_ptr<IVertexBufferRenderer> BgfxRenderer::CreateVertexBuffer(int capacity)
    {
        return std::make_unique<BgfxVertexBufferRenderer>(capacity);
    }

    // --- BgfxIndexBufferRenderer ---

    BgfxIndexBufferRenderer::BgfxIndexBufferRenderer(int capacity, bool thirtyTwoBit)
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

    BgfxIndexBufferRenderer::~BgfxIndexBufferRenderer()
    {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }

    void BgfxIndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        if (is32bit)
            throw std::runtime_error(
                "BgfxIndexBufferRenderer: SetData16 called on a 32-bit index buffer.");
        indexCount = index_count;
        if (!data || index_count <= 0) { cpuData.clear(); return; }

        // REMED-GFX-109: preserve the native bytes referenced by every already-submitted draw.
        // Both public IndexBuffer and DynamicIndexBuffer use this same renderer object.
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

    void BgfxIndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        if (!is32bit)
            throw std::runtime_error(
                "BgfxIndexBufferRenderer: SetData32 called on a 16-bit index buffer.");
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

    std::unique_ptr<IIndexBufferRenderer> BgfxRenderer::CreateIndexBuffer16(int capacity)
    {
        return std::make_unique<BgfxIndexBufferRenderer>(capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> BgfxRenderer::CreateIndexBuffer32(int capacity)
    {
        return std::make_unique<BgfxIndexBufferRenderer>(capacity, true);
    }

    // --- 3D draw calls ---

    // Task 767: scale factor for the DepthBias vertex-shader Z-offset emulation. Real GL/Vulkan
    // polygon-offset hardware multiplies the raw DepthBias value by an implementation-defined
    // "minimum resolvable difference" of the depth buffer format (commonly ~1/(2^24-1) for a
    // 24-bit depth buffer) before adding it to window-space depth -- mirrored here so a given
    // DepthBias value produces a roughly comparable visual shift on Bgfx as on EasyGL/Vulkan.
    static constexpr float kDepthBiasScale = 1.0f / 16777215.0f;

    void BgfxRenderer::SetDepthBiasUniform()
    {
        const float depthBias4[4] = { depthBias_ * kDepthBiasScale, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(depthBiasUnif_, depthBias4);
    }

    // plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: binds the base color map (unit 0, shared
    // texColor3DSampler_) plus PbrEffect's 4 additional maps (units 1-4, bound before unit 0 to
    // leave it active last, matching EasyGLRenderer::BindDrawParams()'s established
    // envMap/texture2 unit-ordering precedent). Each fallback texture is the correct "map
    // absent" constant for its own semantic -- see EnsureDefaultFlatNormalTexture()'s doc
    // comment (mirrored on defaultFlatNormalTexture3D_ above) for the normal-map case; the other
    // 3 all reuse defaultWhiteTexture3D_ since their respective factor/no-op semantics already
    // make (1,1,1,1) the correct "map absent" value.
    void BgfxRenderer::BindPbrTextures(const GpuDrawParams& params)
    {
        // REMED-GFX-078: each slot resolved through IBgfxSamplable (see BindSamplerSlot) so a
        // RenderTarget2D set as any PBR map binds its real handle instead of UB-casting to
        // BgfxTextureRenderer. Slots 1-4 are bound before slot 0 so slot 0 stays active last
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
        const BgfxVertexBufferRenderer& vb, PrimitiveType primitive,
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
        const BgfxVertexBufferRenderer& vb, const BgfxIndexBufferRenderer& ib,
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

    void BgfxRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb_in,
                                                    const Matrix& world, const Matrix& view,
                                                    const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount)
    {
        if (!bgfx::isValid(colored3DProgram_)) return; // shader not loaded
        auto& vb = static_cast<const BgfxVertexBufferRenderer&>(vb_in);
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
                       | kMsaaRasterState | blendFlags_ | depthFlags_ | cullFlags_)
                       | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive)),
                       blendFactorPacked_);
        SubmitViewProgram(colored3DProgram_);
    }

    void BgfxRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb_in,
                                                           const IIndexBufferRenderer& ib_in,
                                                           const Matrix& world, const Matrix& view,
                                                           const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount)
    {
        if (!bgfx::isValid(colored3DProgram_)) return;
        auto& vb = static_cast<const BgfxVertexBufferRenderer&>(vb_in);
        auto& ib = static_cast<const BgfxIndexBufferRenderer&>(ib_in);
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
                       | kMsaaRasterState | blendFlags_ | depthFlags_ | cullFlags_)
                       | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive)),
                       blendFactorPacked_);
        SubmitViewProgram(colored3DProgram_);
    }

    // -------------------------------------------------------------------------
    // REMED-GFX-181: env-gated diagnostics for the EnvironmentMapEffect two-slot binding.
    //
    // `bgfx::setTexture(_stage, _sampler, _handle, _flags)` defaults `_flags` to UINT32_MAX, and
    // bgfx reads that as a SENTINEL, not as a flag word: `EncoderImpl::setTexture` stores
    // `BGFX_SAMPLER_INTERNAL_DEFAULT` (0x10000000) whenever that bit is present in the argument,
    // and every renderer then resolves the binding as
    // `0 == (BGFX_SAMPLER_INTERNAL_DEFAULT & _flags) ? _flags : m_flags` -- i.e. an explicit word
    // REPLACES the texture's own creation state outright (masked to BGFX_SAMPLER_BITS_MASK), while
    // the sentinel selects that creation state. So "which sampler did this draw really use" is not
    // observable from the CNA side at all unless both candidates are printed next to the argument
    // that chose between them. This trace prints exactly that, once per env-map submit, and is off
    // unless CNA_BGFX_ENVMAP_TRACE is set to something other than empty or "0".
    // -------------------------------------------------------------------------
    namespace
    {
        bool EnvMapTraceEnabled()
        {
            static const bool enabled = []
            {
                const char* v = std::getenv("CNA_BGFX_ENVMAP_TRACE");
                return v != nullptr && v[0] != '\0' && std::strcmp(v, "0") != 0;
            }();
            return enabled;
        }

        /// Decodes the BGFX_SAMPLER_* bits that a per-binding override may carry
        /// (BGFX_SAMPLER_BITS_MASK: U/V/W address, MIN/MAG/MIP filter, COMPARE).
        std::string DescribeBgfxSamplerFlags(uint64_t flags)
        {
            std::string out;
            const auto add = [&out](const char* s) { if (!out.empty()) out += '+'; out += s; };
            switch (flags & BGFX_SAMPLER_MIN_MASK)
            {
            case BGFX_SAMPLER_MIN_POINT:       add("minPoint");  break;
            case BGFX_SAMPLER_MIN_ANISOTROPIC: add("minAniso");  break;
            default:                           add("minLinear"); break;
            }
            switch (flags & BGFX_SAMPLER_MAG_MASK)
            {
            case BGFX_SAMPLER_MAG_POINT:       add("magPoint");  break;
            case BGFX_SAMPLER_MAG_ANISOTROPIC: add("magAniso");  break;
            default:                           add("magLinear"); break;
            }
            add((flags & BGFX_SAMPLER_MIP_MASK) == BGFX_SAMPLER_MIP_POINT ? "mipPoint" : "mipLinear");
            switch (flags & BGFX_SAMPLER_U_MASK)
            {
            case BGFX_SAMPLER_U_MIRROR: add("uMirror"); break;
            case BGFX_SAMPLER_U_CLAMP:  add("uClamp");  break;
            case BGFX_SAMPLER_U_BORDER: add("uBorder"); break;
            default:                    add("uWrap");   break;
            }
            switch (flags & BGFX_SAMPLER_V_MASK)
            {
            case BGFX_SAMPLER_V_MIRROR: add("vMirror"); break;
            case BGFX_SAMPLER_V_CLAMP:  add("vClamp");  break;
            case BGFX_SAMPLER_V_BORDER: add("vBorder"); break;
            default:                    add("vWrap");   break;
            }
            switch (flags & BGFX_SAMPLER_W_MASK)
            {
            case BGFX_SAMPLER_W_MIRROR: add("wMirror"); break;
            case BGFX_SAMPLER_W_CLAMP:  add("wClamp");  break;
            case BGFX_SAMPLER_W_BORDER: add("wBorder"); break;
            default:                    add("wWrap");   break;
            }
            return out;
        }

        std::string DescribeHex(uint64_t v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08llx", static_cast<unsigned long long>(v));
            return buf;
        }

        /// The word the draw hands to bgfx, rendered the way bgfx will read it.
        std::string DescribeEffectiveFlags(uint32_t argument, uint64_t creationFlags)
        {
            if ((argument & 0x10000000u) != 0u)  // BGFX_SAMPLER_INTERNAL_DEFAULT (bgfx_p.h, private)
                return "DEFAULT->creation " + DescribeHex(creationFlags & 0xffffffffull)
                     + " (" + DescribeBgfxSamplerFlags(creationFlags) + ")";
            return "EXPLICIT " + DescribeHex(argument) + " (" + DescribeBgfxSamplerFlags(argument) + ")";
        }
    }

    void BgfxRenderer::TraceEnvMapBinding(const char* path, uint16_t viewId,
                                                  const ITextureRenderer* baseTexture,
                                                  uint32_t baseArgument,
                                                  const IBgfxCubeSamplable* cube,
                                                  uint32_t cubeArgument) const
    {
        if (!EnvMapTraceEnabled()) return;
        static uint32_t submitOrder = 0;
        const uint64_t baseCreation = baseTexture != nullptr
            ? [baseTexture]() -> uint64_t {
                  const auto* s = dynamic_cast<const IBgfxSamplable*>(baseTexture);
                  return s != nullptr ? s->GetBgfxCreationFlagsEXT() : 0ull;
              }()
            : 0ull;
        const uint64_t cubeCreation = cube != nullptr ? cube->GetBgfxCubeCreationFlagsEXT() : 0ull;
        std::cout << "[CNA_BGFX_ENVMAP] draw=" << submitOrder++
                  << " family=EnvironmentMap"
                  << " path=" << path
                  << " view=" << viewId
                  << " | captured0=" << DescribeHex(samplerFlags_[0])
                  << " (" << DescribeBgfxSamplerFlags(samplerFlags_[0]) << ")"
                  << " | captured1=" << DescribeHex(samplerFlags_[1])
                  << " (" << DescribeBgfxSamplerFlags(samplerFlags_[1]) << ")"
                  << " | baseCreation=" << DescribeHex(baseCreation)
                  << " | cubeCreation=" << DescribeHex(cubeCreation)
                  << " | effective0=" << DescribeEffectiveFlags(baseArgument, baseCreation)
                  << " | effective1=" << DescribeEffectiveFlags(cubeArgument, cubeCreation)
                  << std::endl;
    }

    void BgfxRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               const GpuDrawParams& params)
    {
        auto& vb = static_cast<const BgfxVertexBufferRenderer&>(vb_in);
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
                                | kMsaaRasterState | blendFlags_ | depthFlags_ | cullFlags_)
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor,
                                  params.pbrNormalScale, params.pbrOcclusionStrength };
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
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor,
                                  params.pbrNormalScale, params.pbrOcclusionStrength };
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            if (params.envMap && bgfx::isValid(envMapSampler_))
            {
                // Task 907 (closes Task 874): dynamic_cast to the common cube-samplable
                // interface instead of an unsafe static_cast<const BgfxTextureCubeRenderer&> --
                // params.envMap may be a BgfxRenderTargetCubeRenderer (a sampled RenderTargetCube),
                // whose layout differs entirely from BgfxTextureCubeRenderer's.
                if (const auto* samplable = dynamic_cast<const IBgfxCubeSamplable*>(params.envMap))
                {
                    // REMED-GFX-181: the five-argument overload, with the same captured public
                    // sampler word BindSamplerSlot passes for the base slot one line above. The
                    // four-argument form defaults `_flags` to UINT32_MAX, which bgfx reads as
                    // "use this texture's own creation state" -- so GraphicsDevice.SamplerStates[1]
                    // was translated into samplerFlags_[1] and then discarded here, and every cube
                    // sampled linear+wrap (an ordinary TextureCube) or linear+clamp (a
                    // RenderTargetCube) whatever the public state said.
                    bgfx::setTexture(1, envMapSampler_, samplable->GetBgfxCubeTextureHandle(),
                                     samplerFlags_[1]);
                    TraceEnvMapBinding("DrawPrimitivesEx", currentViewId_, params.texture0,
                                       samplerFlags_[0], samplable, samplerFlags_[1]);
                }
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(alphaTestColoredTextured3DProgram_);
        }
        else if (alphaTestActive && bgfx::isValid(alphaTest3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(coloredTextured3DProgram_);
        }
        else if (params.textureEnabled && bgfx::isValid(textured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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

    void BgfxRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                      const IIndexBufferRenderer& ib_in,
                                                      const Matrix& world, const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        // Task 948: indexed counterpart of DrawPrimitivesEx -- previously unimplemented, so
        // every indexed Effect-bound draw (e.g. any Content.Load<Model>() mesh) silently fell
        // back to the base IGraphicsRenderer default (DrawIndexedColoredPrimitives), discarding
        // GpuDrawParams entirely (diffuse color, texture, lighting, fog all lost). Mirrors
        // DrawPrimitivesEx's own full GpuDrawParams dispatch exactly -- see that function for
        // per-branch comments -- with an index buffer bound instead of a plain vertex draw.
        auto& vb = static_cast<const BgfxVertexBufferRenderer&>(vb_in);
        auto& ib = static_cast<const BgfxIndexBufferRenderer&>(ib_in);
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
                                | kMsaaRasterState | blendFlags_ | depthFlags_ | cullFlags_)
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor,
                                  params.pbrNormalScale, params.pbrOcclusionStrength };
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
            float mrFactor[4] = { params.pbrMetallicFactor, params.pbrRoughnessFactor,
                                  params.pbrNormalScale, params.pbrOcclusionStrength };
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            if (params.envMap && bgfx::isValid(envMapSampler_))
            {
                // Task 907 (closes Task 874): dynamic_cast to the common cube-samplable
                // interface instead of an unsafe static_cast<const BgfxTextureCubeRenderer&> --
                // params.envMap may be a BgfxRenderTargetCubeRenderer (a sampled RenderTargetCube),
                // whose layout differs entirely from BgfxTextureCubeRenderer's.
                if (const auto* samplable = dynamic_cast<const IBgfxCubeSamplable*>(params.envMap))
                {
                    // REMED-GFX-181: the five-argument overload, with the same captured public
                    // sampler word BindSamplerSlot passes for the base slot one line above. The
                    // four-argument form defaults `_flags` to UINT32_MAX, which bgfx reads as
                    // "use this texture's own creation state" -- so GraphicsDevice.SamplerStates[1]
                    // was translated into samplerFlags_[1] and then discarded here, and every cube
                    // sampled linear+wrap (an ordinary TextureCube) or linear+clamp (a
                    // RenderTargetCube) whatever the public state said.
                    bgfx::setTexture(1, envMapSampler_, samplable->GetBgfxCubeTextureHandle(),
                                     samplerFlags_[1]);
                    TraceEnvMapBinding("DrawIndexedPrimitivesEx", currentViewId_, params.texture0,
                                       samplerFlags_[0], samplable, samplerFlags_[1]);
                }
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(alphaTestColoredTextured3DProgram_);
        }
        else if (alphaTestActive && bgfx::isValid(alphaTest3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            bgfx::setUniform(alphaTestUnif_, params.alphaTest);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
            BindSamplerSlot(0, texColor3DSampler_, params.texture0, defaultWhiteTexture3D_);
            SubmitViewProgram(coloredTextured3DProgram_);
        }
        else if (params.textureEnabled && bgfx::isValid(textured3DProgram_))
        {
            bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
            // REMED-GFX-078: resolve through IBgfxSamplable (see BindSamplerSlot) so a RenderTarget2D
            // set as this effect's texture binds its real handle instead of UB-casting to
            // BgfxTextureRenderer; null falls back to opaque white (Task 379).
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

    void BgfxRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                         const IIndexBufferRenderer& ib_in,
                                                         const Matrix& /*world*/,
                                                         const Matrix& view,
                                                         const Matrix& projection,
                                                         PrimitiveType primitive,
                                                         int primitiveCount,
                                                         int instanceCount,
                                                         const GpuDrawParams& params)
    {
        // REMED-GFX-202: the per-instance stream is the lowest-slot entry of the shared
        // GpuVertexStreamBinding array whose InstanceFrequency is greater than zero.
        const auto* instanceStream = FirstInstanceStream(params);
        if (instanceStream == nullptr) return;
        // REMED-GFX-202: one stream of each rate (REMED-GFX-204 tracks widening it).
        RejectUnsupportedStreamCombination(params, "The bgfx renderer");
        if (!bgfx::isValid(instanced3DProgram_) || !bgfx::isValid(vpInstanced3DUnif_)) return;

        auto& vb     = static_cast<const BgfxVertexBufferRenderer&>(vb_in);
        auto& ib     = static_cast<const BgfxIndexBufferRenderer&>(ib_in);
        auto& instVb = static_cast<const BgfxVertexBufferRenderer&>(*instanceStream->buffer);
        if (!bgfx::isValid(vb.handle) || !bgfx::isValid(ib.handle) || instVb.cpuData.empty())
            return;

        // REMED-GFX-118: resolved (and validated in 64-bit) before anything is allocated or
        // submitted, so an out-of-buffer request is an error rather than a clamped native draw.
        const BgfxIndexedRange range = ResolveIndexedRange(
            vb, ib, primitive, primitiveCount, params.startIndex, params.baseVertex);

        // REMED-GFX-211: the GEOMETRY binding's own VertexOffset, which this route dropped
        // entirely. bgfx has no draw-time base-vertex argument -- setVertexBuffer's startVertex is
        // the only term that reaches a decoded index, and REMED-GFX-107/118 already deliver
        // baseVertex through it. The instanced route folds NOTHING into params.baseVertex
        // (REMED-GFX-202), unlike the ordinary routes, so this stream's whole public offset must be
        // added to that same term here, exactly once: the fetched element becomes
        // `VertexOffset + baseVertex + index`. The index buffer is untouched and startIndex stays
        // an index-element offset. RejectUnsupportedStreamCombination above guarantees exactly one
        // per-vertex stream on this route, so the fold advances that stream and no other.
        const GpuVertexStreamBinding* perVertexStream = FirstPerVertexStream(params);
        const int perVertexOffset = perVertexStream != nullptr ? perVertexStream->vertexOffset : 0;
        // The shared ValidateVertexStreamRanges runs first, and is strictly tighter, for any draw
        // that arrived through GraphicsDevice. This exists because Draw*PrimitivesEx is a public
        // interface method a harness may call with a hand-built GpuDrawParams, and because an
        // offset that was previously ignored now moves a real native binding -- an out-of-range one
        // must name its slot here rather than bind past the end of its own buffer.
        if (perVertexOffset < 0 ||
            perVertexOffset >= static_cast<int>(static_cast<uint32_t>(vb.vertexCount) -
                                                range.vertexStart))
        {
            throw System::ArgumentOutOfRangeException(
                "VertexOffset", std::to_string(perVertexOffset),
                "The requested vertex range exceeds the vertex buffer bound to slot " +
                    std::to_string(perVertexStream != nullptr ? perVertexStream->slot : 0) + '.');
        }

        ApplyViewportOverride();

        const int instCount = std::max(1, instanceCount);
        const uint16_t instStride = static_cast<uint16_t>(instVb.stride > 0 ? instVb.stride : 64);

        // REMED-GFX-213: instance i takes source record `VertexOffset + i / InstanceFrequency` --
        // the same rule glVertexAttribDivisor and D3D11's InstanceDataStepRate define. bgfx's
        // public instancing API represents no divisor at all: setInstanceDataBuffer takes only
        // (buffer, start, num), every supplied record advances exactly once per drawn instance, and
        // no renderer-specific step-rate mechanism is exposed through it -- so the grouping is
        // expanded into the instance-data buffer this route already allocates, exactly as
        // REMED-GFX-213 did for Vulkan.
        const int instanceFrequency = std::max(1, instanceStream->instanceFrequency);

        // REMED-GFX-118: the per-instance stream owes every requested instance its own record.
        // Copying min(requested, available) instead left the surplus instances reading
        // uninitialised transient memory; an over-long instance range is now rejected -- before
        // the transient-capacity probe below, so an invalid request is never reported as a
        // temporary out-of-memory skip.
        //
        // REMED-GFX-211/213: what the stream must hold is the highest SOURCE record the draw reads,
        // which is neither `instanceCount` records nor records starting at zero once the binding
        // carries an offset and a frequency. This is ValidateInstanceStreamRanges' arithmetic, in
        // this stream's own elements.
        const std::size_t copyBytes = static_cast<std::size_t>(instCount) * instStride;
        const std::int64_t availableRecords =
            static_cast<std::int64_t>(instVb.cpuData.size() / instStride);
        const std::int64_t lastSourceRecord =
            static_cast<std::int64_t>(instanceStream->vertexOffset) +
            (static_cast<std::int64_t>(instCount) - 1) / instanceFrequency;
        if (instanceStream->vertexOffset < 0 || lastSourceRecord >= availableRecords)
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
        // REMED-GFX-211: the first source record is this stream's OWN VertexOffset, converted with
        // this stream's own stride -- never binding 0's stride, and never baseVertex, which
        // addresses a per-vertex stream only.
        const uint8_t* instSrc = instVb.cpuData.data() +
            static_cast<std::size_t>(instanceStream->vertexOffset) * instStride;
        if (instanceFrequency == 1)
        {
            // Frequency 1 keeps the single bulk copy it has always been.
            std::memcpy(idb.data, instSrc, copyBytes);
        }
        else
        {
            // Exactly instCount destination records, one per instance, so nothing about the native
            // binding, the program or the vertex layout moves -- the divisor is a data-preparation
            // concern only, and no allocation beyond the single instance-data buffer above.
            for (int i = 0; i < instCount; ++i)
                std::memcpy(idb.data + static_cast<std::size_t>(i) * instStride,
                            instSrc + static_cast<std::size_t>(i / instanceFrequency) * instStride,
                            instStride);
        }

        const Matrix vp = view * projection;
        float vp_col[16];
        vp.ToColumnMajor(vp_col);
        bgfx::setUniform(vpInstanced3DUnif_, vp_col);
        SetDepthBiasUniform();
        // REMED-GFX-215: the same two uniforms the ordinary no-texture branch supplies (see the
        // terminal `else` of DrawColoredPrimitives), because BasicEffect's shader index has no
        // instancing term. This route previously supplied neither, so vs_instanced3d.sc emitted the
        // raw COLOR0 and both DiffuseColor and VertexColorEnabled were silently dropped. Both are
        // written unconditionally on every instanced draw: bgfx uniform values persist in the
        // renderer until overwritten, so an instanced draw that skipped either would inherit
        // whatever the preceding ordinary draw happened to set.
        bgfx::setUniform(diffuseColor3DUnif_, params.diffuseColor);
        const float vceInstanced[4] = {
            params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(vertexColorEn3DUnif_, vceInstanced);

        // Task 766: see DrawColoredPrimitives above.
        bgfx::TransientIndexBuffer wireTib;
        // REMED-GFX-211: the wireframe path rebases its expanded indices to absolute vertex
        // elements and then binds the stream from zero, so the geometry binding's offset has to
        // ride the addend here instead of the native start below -- once, on the same term as
        // baseVertex.
        const bool useWireframe = wireframe_
            && ExpandWireframeIndices(&ib, primitive, primitiveCount, params.startIndex,
                                      params.baseVertex + perVertexOffset, 0, wireTib);
        // REMED-GFX-118: bind the exact requested geometry range, the same way the ordinary
        // indexed path has since REMED-GFX-107. This call previously passed the whole-buffer
        // setVertexBuffer/setIndexBuffer overloads, so baseVertex, startIndex and the
        // topology-derived index count never reached the native instanced draw at all -- every
        // instance consumed the complete index buffer from element zero. instanceCount stays
        // independent of this range: it only chooses how many instances consume it.
        //
        // REMED-GFX-211: the geometry binding's VertexOffset joins baseVertex in the native start,
        // and the bound remainder shrinks by exactly as much, so the binding still ends at the
        // buffer's last element.
        const uint32_t vertexStart = useWireframe
            ? 0u
            : range.vertexStart + static_cast<uint32_t>(perVertexOffset);
        const uint32_t vertexCount = useWireframe
            ? static_cast<uint32_t>(vb.vertexCount)
            : static_cast<uint32_t>(vb.vertexCount) - vertexStart;
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
                                | kMsaaRasterState | blendFlags_ | depthFlags_ | cullFlags_)
                               | (useWireframe ? BGFX_STATE_PT_LINES : ToTopologyFlag(primitive));
        bgfx::setState(state, blendFactorPacked_);
        SubmitViewProgram(instanced3DProgram_);
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_BGFX
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Bgfx::BgfxRenderer>(args.window, args.swapInterval);
    }
#endif
}
