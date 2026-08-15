// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "IglConversions.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <igl/CommandBuffer.h>
#include <igl/CommandQueue.h>
#include <igl/Device.h>
#include <igl/RenderCommandEncoder.h>
#include <igl/Shader.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        [[nodiscard]] int CalculateMipLevels(const int width, const int height)
        {
            int levels = 1;
            int w = width;
            int h = height;
            while (w > 1 || h > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }
    }

    IglRenderer::IglRenderer(const GraphicsRendererCreateArgs& args)
    {
        const Detail::RendererBackend backend = Detail::ResolveRendererBackend();
        surface_ = std::make_unique<IglPlatformSurface>(args, backend);

        virtualWidth_ = args.virtualWidth;
        virtualHeight_ = args.virtualHeight;
        presentationMode_ = args.presentationMode;

        dynamicVertexPool_ = std::make_unique<IglDynamicBufferPool>(
            GetDevice(), igl::BufferDesc::BufferTypeBits::Vertex, "CNA dynamic vertices");
        dynamicIndexPool_ = std::make_unique<IglDynamicBufferPool>(
            GetDevice(), igl::BufferDesc::BufferTypeBits::Index, "CNA dynamic indices");
        dynamicUniformPool_ = std::make_unique<IglDynamicBufferPool>(
            GetDevice(), igl::BufferDesc::BufferTypeBits::Uniform, "CNA dynamic uniforms");

        RebuildBackBufferTarget();

        IGraphicsRenderer::RegisterForWindow(surface_->GetWindowId(), this);
    }

    IglRenderer::~IglRenderer()
    {
        try
        {
            FlushPendingFrameEXT();
        }
        catch (const std::exception& error)
        {
            CNA::Logger::Warn(std::string("IGL renderer: the final frame could not be flushed: ") +
                                  error.what(),
                              CNA::LogCategory::RENDER);
        }

        if (surface_ != nullptr)
            IGraphicsRenderer::UnregisterForWindow(surface_->GetWindowId());

        // Destroy in dependency order while the device (and, on OpenGL, its context) is still alive.
        pipelines_.clear();
        builtInShaders_.clear();
        vertexInputStates_.clear();
        depthStencilStates_.clear();
        samplerStates_.clear();
        dummyTexture2D_.reset();
        dummyTextureCube_.reset();
        multiRenderTarget_.reset();
        backBufferTarget_.reset();
        dynamicVertexPool_.reset();
        dynamicIndexPool_.reset();
        dynamicUniformPool_.reset();
        encoder_.reset();
        commandBuffer_.reset();
        surface_.reset();
    }

    // ---------------------------------------------------------------------------------------------
    // Presentation geometry
    // ---------------------------------------------------------------------------------------------

    namespace
    {
        /** @brief What the presentation policy resolves to for one drawable size. */
        struct PresentationLayout
        {
            int logicalWidth = 0;
            int logicalHeight = 0;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };

        [[nodiscard]] PresentationLayout ComputePresentation(const CnaPresentationMode mode,
                                                             const int virtualWidth,
                                                             const int virtualHeight,
                                                             const int drawableWidth,
                                                             const int drawableHeight)
        {
            PresentationLayout layout;
            const int surfaceWidth = std::max(1, drawableWidth);
            const int surfaceHeight = std::max(1, drawableHeight);

            if (virtualWidth <= 0 || virtualHeight <= 0 ||
                mode == CnaPresentationMode::NativeBackBuffer)
            {
                layout.logicalWidth = surfaceWidth;
                layout.logicalHeight = surfaceHeight;
                layout.width = surfaceWidth;
                layout.height = surfaceHeight;
                return layout;
            }

            if (mode == CnaPresentationMode::FixedHeightDynamicWidth)
            {
                // The preferred height stays the logical height and the width follows the surface's
                // aspect ratio, so a wider device simply shows more of the world -- then the derived
                // canvas fills the surface exactly, with no bars and no crop.
                layout.logicalHeight = virtualHeight;
                layout.logicalWidth = static_cast<int>(std::lround(
                    static_cast<double>(surfaceWidth) * static_cast<double>(virtualHeight) /
                    static_cast<double>(surfaceHeight)));
                layout.logicalWidth = std::max(1, layout.logicalWidth);
                layout.width = surfaceWidth;
                layout.height = surfaceHeight;
                return layout;
            }

            layout.logicalWidth = virtualWidth;
            layout.logicalHeight = virtualHeight;

            if (mode == CnaPresentationMode::Stretch)
            {
                layout.width = surfaceWidth;
                layout.height = surfaceHeight;
                return layout;
            }

            const double scaleX = static_cast<double>(surfaceWidth) / virtualWidth;
            const double scaleY = static_cast<double>(surfaceHeight) / virtualHeight;
            const double scale = mode == CnaPresentationMode::Overscan ? std::max(scaleX, scaleY)
                                                                        : std::min(scaleX, scaleY);

            layout.width = std::max(1, static_cast<int>(std::lround(virtualWidth * scale)));
            layout.height = std::max(1, static_cast<int>(std::lround(virtualHeight * scale)));
            layout.x = (surfaceWidth - layout.width) / 2;
            layout.y = (surfaceHeight - layout.height) / 2;
            return layout;
        }
    }

    void IglRenderer::RebuildBackBufferTarget()
    {
        const CNA::Platform::WindowSize drawable = surface_->GetDrawableSize();
        const PresentationLayout layout = ComputePresentation(
            presentationMode_, virtualWidth_, virtualHeight_, drawable.width, drawable.height);

        logicalWidth_ = layout.logicalWidth;
        logicalHeight_ = layout.logicalHeight;
        presentX_ = layout.x;
        presentY_ = layout.y;
        presentWidth_ = layout.width;
        presentHeight_ = layout.height;

        backBufferAcquired_ = false;
        backBufferTarget_.reset();
        boundTarget_ = nullptr;
    }

    /**
     * @brief The back buffer expressed as a bindable target. CNAEXT.
     *
     * Wraps the framebuffer the platform surface hands out each frame; unlike a render target it
     * owns nothing, because the swap chain does.
     */
    class IglBackBufferTarget final : public IglBoundTarget
    {
    public:
        /**
         * @brief Binds the back buffer's framebuffer and size.
         * @param framebuffer Swap-chain framebuffer.
         * @param width       Drawable width in pixels.
         * @param height      Drawable height in pixels.
         * @param sampleCount Real surface sample count.
         */
        IglBackBufferTarget(std::shared_ptr<igl::IFramebuffer> framebuffer, const int width,
                            const int height, const int sampleCount)
            : framebuffer_(std::move(framebuffer))
            , width_(width)
            , height_(height)
            , sampleCount_(std::max(1, sampleCount))
        {
        }

        /** @brief Returns the swap-chain framebuffer. */
        [[nodiscard]] const std::shared_ptr<igl::IFramebuffer>& GetFramebuffer() const override
        {
            return framebuffer_;
        }

        /** @brief Returns the drawable width in pixels. */
        [[nodiscard]] int GetTargetWidth() const override { return width_; }

        /** @brief Returns the drawable height in pixels. */
        [[nodiscard]] int GetTargetHeight() const override { return height_; }

        /** @brief Returns the surface's real sample count. */
        [[nodiscard]] int GetTargetSampleCount() const override { return sampleCount_; }

        /** @brief Replaces the framebuffer after a resize or a new frame's acquisition. */
        void SetFramebuffer(std::shared_ptr<igl::IFramebuffer> framebuffer, const int width,
                            const int height)
        {
            framebuffer_ = std::move(framebuffer);
            width_ = width;
            height_ = height;
        }

    private:
        std::shared_ptr<igl::IFramebuffer> framebuffer_;
        int width_ = 0;
        int height_ = 0;
        int sampleCount_ = 1;
    };

    IglBoundTarget& IglRenderer::CurrentTarget()
    {
        if (boundTarget_ != nullptr)
            return *boundTarget_;

        if (!backBufferAcquired_)
        {
            const CNA::Platform::WindowSize drawable = surface_->GetDrawableSize();
            const std::shared_ptr<igl::IFramebuffer>& framebuffer =
                surface_->AcquireBackBufferFramebuffer();

            if (auto* existing = dynamic_cast<IglBackBufferTarget*>(backBufferTarget_.get()))
            {
                existing->SetFramebuffer(framebuffer, drawable.width, drawable.height);
            }
            else
            {
                backBufferTarget_ = std::make_unique<IglBackBufferTarget>(
                    framebuffer, drawable.width, drawable.height,
                    surface_->GetSurfaceSampleCount());
            }
            backBufferAcquired_ = true;
        }

        boundTarget_ = backBufferTarget_.get();
        return *boundTarget_;
    }

    // ---------------------------------------------------------------------------------------------
    // Frame and pass management
    // ---------------------------------------------------------------------------------------------

    void IglRenderer::BeginFrameIfNeeded()
    {
        if (frameRecording_)
            return;

        frameIndex_ = (frameIndex_ + 1) % kIglFramesInFlight;
        dynamicVertexPool_->BeginFrame(frameIndex_);
        dynamicIndexPool_->BeginFrame(frameIndex_);
        dynamicUniformPool_->BeginFrame(frameIndex_);

        igl::Result result;
        igl::CommandBufferDesc desc;
        desc.debugName = "CNA frame";
        commandBuffer_ = GetCommandQueue().createCommandBuffer(desc, &result);
        if (!commandBuffer_ || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a command buffer (" +
                                     result.message + ")");
        }
        frameRecording_ = true;
    }

    void IglRenderer::BeginPass(const bool clearColor, const bool clearDepth,
                                const bool clearStencil)
    {
        EndPass();
        BeginFrameIfNeeded();

        IglBoundTarget& target = CurrentTarget();
        const std::shared_ptr<igl::IFramebuffer>& framebuffer = target.GetFramebuffer();
        if (!framebuffer)
            throw std::runtime_error("IGL renderer: the bound target has no framebuffer");

        const bool hasDepth = framebuffer->getDepthAttachment() != nullptr;
        const bool hasStencil = framebuffer->getStencilAttachment() != nullptr;

        igl::RenderPassDesc pass;
        pass.colorAttachments.resize(
            static_cast<std::size_t>(std::max(1, target.GetColorAttachmentCount())));
        for (igl::RenderPassDesc::AttachmentDesc& attachment : pass.colorAttachments)
        {
            attachment.loadAction = clearColor ? igl::LoadAction::Clear
                                               : (target.PreservesContents() || !clearColor
                                                      ? igl::LoadAction::Load
                                                      : igl::LoadAction::DontCare);
            attachment.storeAction = target.GetTargetSampleCount() > 1
                                         ? igl::StoreAction::MsaaResolve
                                         : igl::StoreAction::Store;
            attachment.clearColor = igl::Color(clearColor_[0], clearColor_[1], clearColor_[2],
                                               clearColor_[3]);
        }

        const int cubeFace = target.GetCubeFace();
        if (cubeFace >= 0)
        {
            for (igl::RenderPassDesc::AttachmentDesc& attachment : pass.colorAttachments)
                attachment.face = static_cast<std::uint8_t>(cubeFace);
        }

        pass.depthAttachment.loadAction =
            hasDepth ? (clearDepth ? igl::LoadAction::Clear : igl::LoadAction::Load)
                     : igl::LoadAction::DontCare;
        pass.depthAttachment.storeAction =
            hasDepth ? igl::StoreAction::Store : igl::StoreAction::DontCare;
        pass.depthAttachment.clearDepth = clearDepth_;

        pass.stencilAttachment.loadAction =
            hasStencil ? (clearStencil ? igl::LoadAction::Clear : igl::LoadAction::Load)
                       : igl::LoadAction::DontCare;
        pass.stencilAttachment.storeAction =
            hasStencil ? igl::StoreAction::Store : igl::StoreAction::DontCare;
        pass.stencilAttachment.clearStencil = static_cast<std::uint32_t>(clearStencil_);

        igl::Result result;
        encoder_ = commandBuffer_->createRenderCommandEncoder(pass, framebuffer, &result);
        if (!encoder_ || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not open a render pass (" +
                                     result.message + ")");
        }

        passOpen_ = true;
        ApplyPassViewportAndScissor();
    }

    void IglRenderer::EnsurePassOpen()
    {
        if (!passOpen_)
            BeginPass(false, false, false);
    }

    void IglRenderer::EndPass()
    {
        if (!passOpen_)
            return;

        encoder_->endEncoding();
        encoder_.reset();
        passOpen_ = false;

        if (boundTarget_ != nullptr)
            boundTarget_->GenerateMipsAfterPass(GetCommandQueue());
    }

    void IglRenderer::ApplyPassViewportAndScissor()
    {
        if (!passOpen_)
            return;

        IglBoundTarget& target = CurrentTarget();
        const float targetWidth = static_cast<float>(std::max(1, target.GetTargetWidth()));
        const float targetHeight = static_cast<float>(std::max(1, target.GetTargetHeight()));

        // The viewport always covers the whole attachment. XNA's own Viewport rectangle, and the
        // letterbox/overscan presentation rectangle, are folded into the projection matrix instead
        // (see SubmitDraw): IGL's two backends do not agree on what a non-zero viewport origin
        // means, whereas a full-surface viewport is identical on both. Depth range is passed
        // through, because that part is consistent.
        const igl::Viewport viewport{0.0f, 0.0f, targetWidth, targetHeight, viewportMinDepth_,
                                     viewportMaxDepth_};
        encoder_->bindViewport(viewport);

        // Clipping is then done by the scissor, which is where XNA's viewport rectangle and
        // ScissorRectangle both really take effect.
        int left = 0;
        int top = 0;
        int right = static_cast<int>(targetWidth);
        int bottom = static_cast<int>(targetHeight);

        const bool isBackBuffer = boundTarget_ == backBufferTarget_.get();
        const float scaleX = isBackBuffer && logicalWidth_ > 0
                                 ? static_cast<float>(presentWidth_) /
                                       static_cast<float>(logicalWidth_)
                                 : 1.0f;
        const float scaleY = isBackBuffer && logicalHeight_ > 0
                                 ? static_cast<float>(presentHeight_) /
                                       static_cast<float>(logicalHeight_)
                                 : 1.0f;
        const int originX = isBackBuffer ? presentX_ : 0;
        const int originY = isBackBuffer ? presentY_ : 0;

        const auto clipTo = [&](const int x, const int y, const int w, const int h) {
            const int l = originX + static_cast<int>(std::lround(x * scaleX));
            const int t = originY + static_cast<int>(std::lround(y * scaleY));
            left = std::max(left, l);
            top = std::max(top, t);
            right = std::min(right, l + static_cast<int>(std::lround(w * scaleX)));
            bottom = std::min(bottom, t + static_cast<int>(std::lround(h * scaleY)));
        };

        if (isBackBuffer)
            clipTo(0, 0, logicalWidth_, logicalHeight_);
        if (viewportSet_)
            clipTo(viewportRect_[0], viewportRect_[1], viewportRect_[2], viewportRect_[3]);
        if (scissorEnabled_)
            clipTo(scissorRect_[0], scissorRect_[1], scissorRect_[2], scissorRect_[3]);

        const int width = std::max(0, right - left);
        const int height = std::max(0, bottom - top);

        // IGL passes the scissor rectangle straight to the native API, and the two disagree about
        // where its origin is: OpenGL measures Y from the bottom of the attachment, Vulkan from the
        // top. The rectangle is computed here with a top-left origin and converted for OpenGL.
        const int y = IsVulkanBackend()
                          ? top
                          : static_cast<int>(targetHeight) - top - height;

        encoder_->bindScissorRect(igl::ScissorRect{
            static_cast<std::uint32_t>(std::max(0, left)),
            static_cast<std::uint32_t>(std::max(0, y)),
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height)});
    }

    void IglRenderer::SubmitFrame(const bool present)
    {
        if (!frameRecording_)
            return;

        EndPass();

        if (present && backBufferAcquired_)
        {
            const igl::SurfaceTextures textures = surface_->AcquireSurfaceTextures();
            commandBuffer_->present(textures.color);
        }

        GetCommandQueue().submit(*commandBuffer_, present);
        if (present)
        {
            GetCommandQueue().endFrame();
            surface_->Present();
        }

        commandBuffer_.reset();
        frameRecording_ = false;
        backBufferAcquired_ = false;
    }

    void IglRenderer::FlushPendingFrameEXT()
    {
        if (!frameRecording_)
            return;
        SubmitFrame(false);
    }

    // ---------------------------------------------------------------------------------------------
    // Clears and presentation
    // ---------------------------------------------------------------------------------------------

    void IglRenderer::Clear(const float r, const float g, const float b, const float a)
    {
        clearColor_[0] = r;
        clearColor_[1] = g;
        clearColor_[2] = b;
        clearColor_[3] = a;
        BeginPass(true, false, false);
    }

    void IglRenderer::ClearColorAndDepth(const float r, const float g, const float b, const float a,
                                          const float depth)
    {
        clearColor_[0] = r;
        clearColor_[1] = g;
        clearColor_[2] = b;
        clearColor_[3] = a;
        clearDepth_ = depth;
        BeginPass(true, true, false);
    }

    void IglRenderer::ClearDepth(const float depth)
    {
        clearDepth_ = depth;
        BeginPass(false, true, false);
    }

    void IglRenderer::ClearStencil(const int stencil)
    {
        clearStencil_ = stencil;
        BeginPass(false, false, true);
    }

    void IglRenderer::ClearDepthAndStencil(const float depth, const int stencil)
    {
        clearDepth_ = depth;
        clearStencil_ = stencil;
        BeginPass(false, true, true);
    }

    void IglRenderer::ClearColorAndStencil(const float r, const float g, const float b,
                                            const float a, const int stencil)
    {
        clearColor_[0] = r;
        clearColor_[1] = g;
        clearColor_[2] = b;
        clearColor_[3] = a;
        clearStencil_ = stencil;
        BeginPass(true, false, true);
    }

    void IglRenderer::ClearColorDepthAndStencil(const float r, const float g, const float b,
                                                 const float a, const float depth,
                                                 const int stencil)
    {
        clearColor_[0] = r;
        clearColor_[1] = g;
        clearColor_[2] = b;
        clearColor_[3] = a;
        clearDepth_ = depth;
        clearStencil_ = stencil;
        BeginPass(true, true, true);
    }

    void IglRenderer::Present()
    {
        // A frame with nothing recorded still has to acquire and present a drawable, or the swap
        // chain never advances and the window shows a stale image.
        BeginFrameIfNeeded();
        (void)CurrentTarget();
        SubmitFrame(true);
    }

    void IglRenderer::GetViewportSize(int& width, int& height)
    {
        width = logicalWidth_;
        height = logicalHeight_;
    }

    void IglRenderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        x = presentX_;
        y = presentY_;
        width = presentWidth_;
        height = presentHeight_;
    }

    void IglRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        FlushPendingFrameEXT();
        surface_->OnSurfaceChanged(surface);
        RebuildBackBufferTarget();
    }

    void IglRenderer::SetVirtualResolution(const int width, const int height)
    {
        FlushPendingFrameEXT();
        virtualWidth_ = width;
        virtualHeight_ = height;
        RebuildBackBufferTarget();
    }

    void IglRenderer::SetPresentationMode(const int mode)
    {
        FlushPendingFrameEXT();
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        RebuildBackBufferTarget();
    }

    void IglRenderer::SetSwapInterval(const int interval)
    {
        surface_->SetSwapInterval(interval);
    }

    int IglRenderer::GetMultiSampleCount() const
    {
        const int samples = surface_->GetSurfaceSampleCount();
        return samples > 1 ? samples : 0;
    }

    int IglRenderer::ApplyMultiSampleCount(int /*requestedMultiSampleCount*/)
    {
        // Neither backend can widen the presented surface after the device exists: the OpenGL
        // visual is fixed when the platform creates the context, and IGL's Vulkan swap-chain images
        // are single-sample by construction. Reporting the count that is genuinely in effect is the
        // honest answer, and it is what GraphicsDevice.Reset writes back into
        // PresentationParameters.
        return GetMultiSampleCount();
    }

    bool IglRenderer::TransformWindowToLogical(const float windowX, const float windowY,
                                                float& logX, float& logY) const
    {
        if (presentWidth_ <= 0 || presentHeight_ <= 0)
            return false;

        const float drawableX = surface_->WindowToDrawable(windowX);
        const float drawableY = surface_->WindowToDrawable(windowY);

        logX = (drawableX - static_cast<float>(presentX_)) *
               static_cast<float>(logicalWidth_) / static_cast<float>(presentWidth_);
        logY = (drawableY - static_cast<float>(presentY_)) *
               static_cast<float>(logicalHeight_) / static_cast<float>(presentHeight_);
        return true;
    }

    bool IglRenderer::TransformLogicalToWindow(const float logX, const float logY, float& windowX,
                                                float& windowY) const
    {
        if (logicalWidth_ <= 0 || logicalHeight_ <= 0)
            return false;

        const float drawableX = static_cast<float>(presentX_) +
                                logX * static_cast<float>(presentWidth_) /
                                    static_cast<float>(logicalWidth_);
        const float drawableY = static_cast<float>(presentY_) +
                                logY * static_cast<float>(presentHeight_) /
                                    static_cast<float>(logicalHeight_);

        windowX = surface_->DrawableToWindow(drawableX);
        windowY = surface_->DrawableToWindow(drawableY);
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Resource factories
    // ---------------------------------------------------------------------------------------------

    std::unique_ptr<ITextureRenderer> IglRenderer::CreateTexture(const ImageData& data)
    {
        const int width = std::max(1, data.width);
        const int height = std::max(1, data.height);
        const int mipLevels = std::max(1, data.mipLevels);

        igl::TextureDesc desc = igl::TextureDesc::new2D(
            ToIglSurfaceFormat(data.surfaceFormat), static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), igl::TextureDesc::TextureUsageBits::Sampled,
            "CNA Texture2D");
        desc.numMipLevels = static_cast<std::uint32_t>(mipLevels);

        igl::Result result;
        std::shared_ptr<igl::ITexture> texture = GetDevice().createTexture(desc, &result);
        if (!texture || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a texture (" + result.message +
                                     ")");
        }

        auto renderer = std::make_unique<IglTextureRenderer>(this, std::move(texture), width, height,
                                                             mipLevels);
        if (!data.pixels.empty())
            renderer->UpdatePixels(data.pixels.data(), width * 4);
        return renderer;
    }

    std::unique_ptr<ITextureCubeRenderer> IglRenderer::CreateTextureCube(const int size,
                                                                         const bool mipMap,
                                                                         const int surfaceFormat)
    {
        if (size <= 0)
            throw std::runtime_error("IGL renderer: a TextureCube needs a positive face size");

        const int mipLevels = mipMap ? CalculateMipLevels(size, size) : 1;

        igl::TextureDesc desc = igl::TextureDesc::newCube(
            ToIglSurfaceFormat(surfaceFormat), static_cast<std::uint32_t>(size),
            static_cast<std::uint32_t>(size), igl::TextureDesc::TextureUsageBits::Sampled,
            "CNA TextureCube");
        desc.numMipLevels = static_cast<std::uint32_t>(mipLevels);

        igl::Result result;
        std::shared_ptr<igl::ITexture> texture = GetDevice().createTexture(desc, &result);
        if (!texture || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a cube texture (" +
                                     result.message + ")");
        }

        return std::make_unique<IglTextureCubeRenderer>(this, std::move(texture), size, mipLevels);
    }

    std::unique_ptr<ITexture3DRenderer> IglRenderer::CreateTexture3D(const int w, const int h,
                                                                     const int depth,
                                                                     const bool mipMap,
                                                                     const int surfaceFormat)
    {
        if (w <= 0 || h <= 0 || depth <= 0)
            throw std::runtime_error("IGL renderer: a Texture3D needs positive dimensions");

        const int mipLevels = mipMap ? CalculateMipLevels(w, h) : 1;

        igl::TextureDesc desc = igl::TextureDesc::new3D(
            ToIglSurfaceFormat(surfaceFormat), static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(depth),
            igl::TextureDesc::TextureUsageBits::Sampled, "CNA Texture3D");
        desc.numMipLevels = static_cast<std::uint32_t>(mipLevels);

        igl::Result result;
        std::shared_ptr<igl::ITexture> texture = GetDevice().createTexture(desc, &result);
        if (!texture || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a volume texture (" +
                                     result.message + ")");
        }

        return std::make_unique<IglTexture3DRenderer>(this, std::move(texture), w, h, depth,
                                                      mipLevels);
    }

    std::unique_ptr<ISpriteBatchRenderer> IglRenderer::CreateSpriteBatch()
    {
        return std::make_unique<IglSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IVertexBufferRenderer> IglRenderer::CreateVertexBuffer(const int vertex_capacity)
    {
        return std::make_unique<IglVertexBufferRenderer>(this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> IglRenderer::CreateIndexBuffer16(const int index_capacity)
    {
        return std::make_unique<IglIndexBufferRenderer>(this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> IglRenderer::CreateIndexBuffer32(const int index_capacity)
    {
        return std::make_unique<IglIndexBufferRenderer>(this, index_capacity, true);
    }

    std::unique_ptr<IOcclusionQueryRenderer> IglRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<IglOcclusionQueryRenderer>(*this);
    }

    std::unique_ptr<IEffectRenderer> IglRenderer::CreateEffectRenderer(const std::string& vertSrc,
                                                                       const std::string& fragSrc)
    {
        auto effect = std::make_unique<IglEffectRenderer>(*this);
        if (!vertSrc.empty() && !fragSrc.empty())
            (void)effect->CompileProgram(vertSrc, fragSrc);
        return effect;
    }

    std::unique_ptr<IRenderTargetRenderer> IglRenderer::CreateRenderTarget2D(
        const int w, const int h, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(w, h, depthFormat, preserveContents, mipMap,
                                       multiSampleCount, 0);
    }

    std::unique_ptr<IRenderTargetRenderer> IglRenderer::CreateRenderTarget2DEXT(
        const int w, const int h, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount, const int surfaceFormat)
    {
        if (w <= 0 || h <= 0)
            throw std::runtime_error("IGL renderer: a RenderTarget2D needs positive dimensions");

        const int mipLevels = mipMap ? CalculateMipLevels(w, h) : 1;
        const int samples = std::max(1, multiSampleCount);
        const igl::TextureFormat colorFormat = ToIglSurfaceFormat(surfaceFormat);

        igl::TextureDesc colorDesc = igl::TextureDesc::new2D(
            colorFormat, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
            igl::TextureDesc::TextureUsageBits::Sampled |
                igl::TextureDesc::TextureUsageBits::Attachment,
            "CNA RenderTarget2D");
        colorDesc.numMipLevels = static_cast<std::uint32_t>(mipLevels);

        igl::Result result;
        std::shared_ptr<igl::ITexture> color = GetDevice().createTexture(colorDesc, &result);
        if (!color || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a render target colour "
                                     "texture (" + result.message + ")");
        }

        std::shared_ptr<igl::ITexture> multisampleColor;
        if (samples > 1)
        {
            igl::TextureDesc msaaDesc = igl::TextureDesc::new2D(
                colorFormat, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                igl::TextureDesc::TextureUsageBits::Attachment, "CNA RenderTarget2D MSAA");
            msaaDesc.numSamples = static_cast<std::uint32_t>(samples);
            multisampleColor = GetDevice().createTexture(msaaDesc, &result);
            if (!multisampleColor || !result.isOk())
            {
                // A device that cannot allocate the requested sample count is not a failure: XNA's
                // own contract is that MultiSampleCount reports what was applied, so this falls back
                // to single-sample and says so through the returned renderer.
                multisampleColor.reset();
            }
        }
        const int appliedSamples = multisampleColor ? samples : 1;

        std::shared_ptr<igl::ITexture> depth;
        int appliedDepthFormat = 0;
        const igl::TextureFormat depthIglFormat = ToIglDepthFormat(depthFormat);
        if (depthIglFormat != igl::TextureFormat::Invalid)
        {
            igl::TextureDesc depthDesc = igl::TextureDesc::new2D(
                depthIglFormat, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                igl::TextureDesc::TextureUsageBits::Attachment, "CNA RenderTarget2D depth");
            depthDesc.numSamples = static_cast<std::uint32_t>(appliedSamples);
            depth = GetDevice().createTexture(depthDesc, &result);
            appliedDepthFormat = depth ? depthFormat : 0;
        }

        igl::FramebufferDesc framebufferDesc;
        if (multisampleColor)
        {
            framebufferDesc.colorAttachments[0].texture = multisampleColor;
            framebufferDesc.colorAttachments[0].resolveTexture = color;
        }
        else
        {
            framebufferDesc.colorAttachments[0].texture = color;
        }
        framebufferDesc.depthAttachment.texture = depth;
        if (depth && DepthFormatHasStencil(depthFormat))
            framebufferDesc.stencilAttachment.texture = depth;
        framebufferDesc.debugName = "CNA RenderTarget2D";

        std::shared_ptr<igl::IFramebuffer> framebuffer =
            GetDevice().createFramebuffer(framebufferDesc, &result);
        if (!framebuffer || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a render target framebuffer (" +
                                     result.message + ")");
        }

        return std::make_unique<IglRenderTargetRenderer>(
            this, std::move(color), std::move(multisampleColor), std::move(depth),
            std::move(framebuffer), w, h, mipLevels, appliedSamples, preserveContents,
            appliedDepthFormat);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> IglRenderer::CreateRenderTargetCube(
        const int size, const int depthFormat, const bool preserveContents, const bool mipMap,
        const int multiSampleCount)
    {
        if (size <= 0)
            throw std::runtime_error("IGL renderer: a RenderTargetCube needs a positive face size");

        const int mipLevels = mipMap ? CalculateMipLevels(size, size) : 1;
        const int samples = std::max(1, multiSampleCount);

        igl::TextureDesc colorDesc = igl::TextureDesc::newCube(
            igl::TextureFormat::RGBA_UNorm8, static_cast<std::uint32_t>(size),
            static_cast<std::uint32_t>(size),
            igl::TextureDesc::TextureUsageBits::Sampled |
                igl::TextureDesc::TextureUsageBits::Attachment,
            "CNA RenderTargetCube");
        colorDesc.numMipLevels = static_cast<std::uint32_t>(mipLevels);

        igl::Result result;
        std::shared_ptr<igl::ITexture> color = GetDevice().createTexture(colorDesc, &result);
        if (!color || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a cube render target (" +
                                     result.message + ")");
        }

        // FNA allocates exactly one depth/stencil buffer for a whole RenderTargetCube rather than
        // one per face, and every face pass shares it.
        std::shared_ptr<igl::ITexture> depth;
        const igl::TextureFormat depthIglFormat = ToIglDepthFormat(depthFormat);
        if (depthIglFormat != igl::TextureFormat::Invalid)
        {
            igl::TextureDesc depthDesc = igl::TextureDesc::new2D(
                depthIglFormat, static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size),
                igl::TextureDesc::TextureUsageBits::Attachment, "CNA RenderTargetCube depth");
            depth = GetDevice().createTexture(depthDesc, &result);
        }

        std::array<std::shared_ptr<igl::IFramebuffer>, 6> faces;
        for (int face = 0; face < 6; ++face)
        {
            igl::FramebufferDesc desc;
            desc.colorAttachments[0].texture = color;
            desc.depthAttachment.texture = depth;
            if (depth && DepthFormatHasStencil(depthFormat))
                desc.stencilAttachment.texture = depth;
            desc.debugName = "CNA RenderTargetCube face";

            faces[static_cast<std::size_t>(face)] = GetDevice().createFramebuffer(desc, &result);
            if (!faces[static_cast<std::size_t>(face)] || !result.isOk())
            {
                throw std::runtime_error(
                    "IGL renderer: could not create a cube render target face framebuffer (" +
                    result.message + ")");
            }
        }

        // MSAA on a cube face would need a multisampled cube attachment plus a per-face resolve,
        // which IGL's framebuffer description cannot express for a cube; the applied count is
        // reported as 1 rather than echoing the request.
        (void)samples;

        return std::make_unique<IglRenderTargetCubeRenderer>(this, std::move(color),
                                                             std::move(depth), std::move(faces),
                                                             size, mipLevels, 1, preserveContents);
    }

    // ---------------------------------------------------------------------------------------------
    // Render target binding
    // ---------------------------------------------------------------------------------------------

    void IglRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt == nullptr)
        {
            SetRenderTargets(nullptr, 0);
            return;
        }

        const RenderTargetBindingDescriptor descriptor =
            RenderTargetBindingDescriptor::ForRenderTarget2D(rt, 0, rt->GetWidth(), rt->GetHeight(),
                                                             rt->GetMultiSampleCount());
        SetRenderTargets(&descriptor, 1);
    }

    void IglRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                        const int count)
    {
        EndPass();
        multiRenderTarget_.reset();

        if (renderTargets == nullptr || count <= 0)
        {
            boundTarget_ = nullptr;
            (void)CurrentTarget();
            return;
        }

        if (count == 1)
        {
            const RenderTargetBindingDescriptor& descriptor = renderTargets[0];
            if (descriptor.IsRenderTargetCubeFace())
            {
                auto* cube = dynamic_cast<IglRenderTargetCubeRenderer*>(
                    descriptor.GetRenderTargetCube());
                if (cube == nullptr)
                {
                    throw std::runtime_error(
                        "IGL renderer: SetRenderTargets was given a RenderTargetCube this renderer "
                        "does not own");
                }
                boundTarget_ = cube->GetFaceBinding(descriptor.GetCubeFace());
                if (boundTarget_ == nullptr)
                {
                    throw std::runtime_error(
                        "IGL renderer: SetRenderTargets was given an out-of-range cube face");
                }
                return;
            }

            auto* target = dynamic_cast<IglRenderTargetRenderer*>(descriptor.GetRenderTarget2D());
            if (target == nullptr)
            {
                throw std::runtime_error(
                    "IGL renderer: SetRenderTargets was given a RenderTarget2D this renderer does "
                    "not own");
            }
            boundTarget_ = target;
            return;
        }

        // Multi-target binds.
        std::vector<IglRenderTargetRenderer*> targets;
        igl::FramebufferDesc desc;
        desc.debugName = "CNA MRT";

        const int slots = std::min(count, static_cast<int>(IGL_COLOR_ATTACHMENTS_MAX));
        if (slots < count)
        {
            throw std::runtime_error(
                "IGL renderer: " + std::to_string(count) +
                " simultaneous render targets were bound, but IGL supports at most " +
                std::to_string(IGL_COLOR_ATTACHMENTS_MAX) +
                ". The binding set is never silently truncated.");
        }

        for (int slot = 0; slot < slots; ++slot)
        {
            const RenderTargetBindingDescriptor& descriptor = renderTargets[slot];
            if (descriptor.IsRenderTargetCubeFace())
            {
                throw std::runtime_error(
                    "IGL renderer: a RenderTargetCube face cannot take part in a multi-render-target "
                    "bind on this renderer.");
            }

            auto* target = dynamic_cast<IglRenderTargetRenderer*>(descriptor.GetRenderTarget2D());
            if (target == nullptr)
            {
                throw std::runtime_error(
                    "IGL renderer: SetRenderTargets was given a RenderTarget2D this renderer does "
                    "not own");
            }

            desc.colorAttachments[static_cast<std::size_t>(slot)].texture =
                target->GetColorTexture();
            targets.push_back(target);
        }

        igl::Result result;
        std::shared_ptr<igl::IFramebuffer> framebuffer =
            GetDevice().createFramebuffer(desc, &result);
        if (!framebuffer || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a multi-target framebuffer (" +
                                     result.message + ")");
        }

        multiRenderTarget_ = std::make_unique<IglMultiRenderTarget>(
            std::move(framebuffer), targets, targets.front()->GetWidth(),
            targets.front()->GetHeight(), 1, targets.front()->PreservesContents());
        boundTarget_ = multiRenderTarget_.get();
    }

    void IglRenderer::ReadBackbuffer(const int x, const int y, const int w, const int h,
                                      std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error("IGL renderer: ReadBackbuffer was given an empty region");

        IglBoundTarget* previous = boundTarget_;
        boundTarget_ = nullptr;
        IglBoundTarget& backBuffer = CurrentTarget();
        const std::shared_ptr<igl::IFramebuffer> framebuffer = backBuffer.GetFramebuffer();
        boundTarget_ = previous;

        if (!framebuffer)
            throw std::runtime_error("IGL renderer: the back buffer has no framebuffer to read");

        FlushPendingFrameEXT();

        const float scaleX = logicalWidth_ > 0 ? static_cast<float>(presentWidth_) /
                                                     static_cast<float>(logicalWidth_)
                                               : 1.0f;
        const float scaleY = logicalHeight_ > 0 ? static_cast<float>(presentHeight_) /
                                                      static_cast<float>(logicalHeight_)
                                                : 1.0f;

        const int physicalX = presentX_ + static_cast<int>(std::lround(x * scaleX));
        const int physicalWidth = std::max(1, static_cast<int>(std::lround(w * scaleX)));
        const int physicalHeight = std::max(1, static_cast<int>(std::lround(h * scaleY)));
        // The colour attachment stores its rows bottom-first on both backends, so the requested
        // top-relative rectangle is converted before the copy and the result is flipped afterwards.
        const int surfaceHeight = std::max(1, backBuffer.GetTargetHeight());
        const int physicalTop = presentY_ + static_cast<int>(std::lround(y * scaleY));
        const int physicalY = std::max(0, surfaceHeight - physicalTop - physicalHeight);

        std::vector<std::uint8_t> block(static_cast<std::size_t>(physicalWidth) *
                                        static_cast<std::size_t>(physicalHeight) * 4);
        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            static_cast<std::uint32_t>(std::max(0, physicalX)),
            static_cast<std::uint32_t>(physicalY),
            static_cast<std::uint32_t>(physicalWidth),
            static_cast<std::uint32_t>(physicalHeight));
        framebuffer->copyBytesColorAttachment(GetCommandQueue(), 0, block.data(), range, 0);

        // Nearest-neighbour down to the requested logical size, sampling each logical pixel's centre
        // so every value handed back is a colour the frame genuinely contained.
        for (int row = 0; row < h; ++row)
        {
            const int sourceRow = std::clamp(
                physicalHeight - 1 -
                    static_cast<int>((row + 0.5f) * static_cast<float>(physicalHeight) /
                                     static_cast<float>(h)),
                0, physicalHeight - 1);
            for (int column = 0; column < w; ++column)
            {
                const int sourceColumn = std::clamp(
                    static_cast<int>((column + 0.5f) * static_cast<float>(physicalWidth) /
                                     static_cast<float>(w)),
                    0, physicalWidth - 1);
                const std::size_t source =
                    (static_cast<std::size_t>(sourceRow) * physicalWidth + sourceColumn) * 4;
                const std::size_t destination =
                    (static_cast<std::size_t>(row) * w + column) * 4;
                std::memcpy(pixels + destination, block.data() + source, 4);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Graphics state
    // ---------------------------------------------------------------------------------------------

    void IglRenderer::ApplyBlendState(const int colorSrcBlend, const int alphaSrcBlend,
                                       const int colorDstBlend, const int alphaDstBlend,
                                       const int colorBlendFunc, const int alphaBlendFunc,
                                       const BlendWriteState& writeState)
    {
        blendColorSrc_ = colorSrcBlend;
        blendAlphaSrc_ = alphaSrcBlend;
        blendColorDst_ = colorDstBlend;
        blendAlphaDst_ = alphaDstBlend;
        blendColorFunc_ = colorBlendFunc;
        blendAlphaFunc_ = alphaBlendFunc;
        blendWriteState_ = writeState;

        // XNA has no "blend enabled" switch: BlendState.Opaque is One/Zero with Add, which is
        // exactly a disabled blend. Detecting it keeps the pipeline's own blendEnabled flag
        // truthful without inventing a state the public API does not have.
        const bool opaque = colorSrcBlend == 0 && alphaSrcBlend == 0 && colorDstBlend == 1 &&
                            alphaDstBlend == 1 && colorBlendFunc == 0 && alphaBlendFunc == 0;
        blendEnabled_ = !opaque;
    }

    void IglRenderer::SetBlendFactor(const float r, const float g, const float b, const float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void IglRenderer::SetBlendEnabled(const bool enabled)
    {
        blendEnabled_ = enabled;
    }

    void IglRenderer::ApplyDepthStencilState(
        const bool depthEnable, const bool depthWriteEnable, const int depthFunc,
        const bool stencilEnable, const int stencilFunc, const int stencilPass,
        const int stencilFail, const int stencilDepthFail, const int stencilMask,
        const int stencilWriteMask, const int referenceStencil, const bool twoSidedStencilMode,
        const int ccwStencilFunc, const int ccwStencilPass, const int ccwStencilFail,
        const int ccwStencilDepthFail)
    {
        depthStencil_.depthEnable = depthEnable;
        depthStencil_.depthWriteEnable = depthWriteEnable;
        depthStencil_.depthFunc = depthFunc;
        depthStencil_.stencilEnable = stencilEnable;
        depthStencil_.stencilFunc = stencilFunc;
        depthStencil_.stencilPass = stencilPass;
        depthStencil_.stencilFail = stencilFail;
        depthStencil_.stencilDepthFail = stencilDepthFail;
        depthStencil_.stencilMask = stencilMask;
        depthStencil_.stencilWriteMask = stencilWriteMask;
        depthStencil_.referenceStencil = referenceStencil;
        depthStencil_.twoSidedStencilMode = twoSidedStencilMode;
        depthStencil_.ccwStencilFunc = ccwStencilFunc;
        depthStencil_.ccwStencilPass = ccwStencilPass;
        depthStencil_.ccwStencilFail = ccwStencilFail;
        depthStencil_.ccwStencilDepthFail = ccwStencilDepthFail;
    }

    void IglRenderer::SetReferenceStencil(const int value)
    {
        depthStencil_.referenceStencil = value;
    }

    void IglRenderer::SetDepthTestEnabled(const bool enabled)
    {
        depthStencil_.depthEnable = enabled;
    }

    void IglRenderer::SetDepthWriteEnabled(const bool enabled)
    {
        depthStencil_.depthWriteEnable = enabled;
    }

    void IglRenderer::ApplyRasterizerState(const int cullMode, const int fillMode,
                                            const bool scissorTestEnable, const float depthBias,
                                            const float slopeScaleDepthBias)
    {
        cullMode_ = cullMode;
        fillMode_ = fillMode;
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;

        if (scissorEnabled_ != scissorTestEnable)
        {
            scissorEnabled_ = scissorTestEnable;
            ApplyPassViewportAndScissor();
        }
    }

    void IglRenderer::ApplySamplerState(const int slot, const int filter, const int addressU,
                                         const int addressV, const int maxAnisotropy)
    {
        if (slot < 0 || slot >= kIglTrackedSamplerSlots)
            return;

        SamplerTracking& tracking = samplers_[static_cast<std::size_t>(slot)];
        tracking.filter = filter;
        tracking.addressU = addressU;
        tracking.addressV = addressV;
        tracking.maxAnisotropy = maxAnisotropy;
    }

    void IglRenderer::ApplySamplerMipState(const int slot, const int maxMipLevel,
                                            const float lodBias)
    {
        if (slot < 0 || slot >= kIglTrackedSamplerSlots)
            return;

        SamplerTracking& tracking = samplers_[static_cast<std::size_t>(slot)];
        tracking.maxMipLevel = maxMipLevel;
        // IGL's SamplerStateDesc has no LOD-bias field on any backend, so the value is recorded for
        // diagnostics but genuinely does not reach the GPU. Documented in docs/igl-renderer.md.
        tracking.lodBias = lodBias;
    }

    void IglRenderer::SetScissorRect(const int x, const int y, const int w, const int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        ApplyPassViewportAndScissor();
    }

    void IglRenderer::SetViewport(const int x, const int y, const int w, const int h,
                                   const float minDepth, const float maxDepth)
    {
        viewportSet_ = true;
        viewportRect_[0] = x;
        viewportRect_[1] = y;
        viewportRect_[2] = w;
        viewportRect_[3] = h;
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
        ApplyPassViewportAndScissor();
    }

    // ---------------------------------------------------------------------------------------------
    // Capabilities
    // ---------------------------------------------------------------------------------------------

    bool IglRenderer::SupportsCapability(const CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        case CNA::GraphicsCapability::ThreeD:
        case CNA::GraphicsCapability::MultipleRenderTargets:
        case CNA::GraphicsCapability::CustomEffects:
        case CNA::GraphicsCapability::Texture3D:
        case CNA::GraphicsCapability::Instancing:
        case CNA::GraphicsCapability::AdditiveBlending:
            return true;

        case CNA::GraphicsCapability::DepthStencilBuffer:
        case CNA::GraphicsCapability::StencilBuffer:
            return SupportsDepthStencil();

        case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            // Real on a render target, and on the back buffer only when the platform granted a
            // multisampled GL visual; the Vulkan swap chain is always single-sample here.
            return true;

        case CNA::GraphicsCapability::AnisotropicFiltering:
            return true;

        case CNA::GraphicsCapability::WireFrame:
            // igl::PolygonFillMode::Line is honoured by the OpenGL backend; Vulkan needs the
            // fillModeNonSolid device feature, which IGL does not request.
            return surface_->GetBackend() == Detail::RendererBackend::OpenGL;

        case CNA::GraphicsCapability::OcclusionQuery:
            // IGL exposes no occlusion-query object on any backend at the pinned revision.
            return false;

        case CNA::GraphicsCapability::MultiStreamVertexInput:
            // Real: igl::VertexAttribute carries a bufferIndex, so a declaration split across
            // several bound buffers is expressed natively rather than collapsed onto stream 0.
            return true;
        }

        return false;
    }

    bool IglRenderer::SupportsDepthStencil() const
    {
        return surface_->GetBackBufferDepthFormat() != igl::TextureFormat::Invalid;
    }

    int IglRenderer::GetMaxTextureDimension() const
    {
        return 16384;
    }

    void IglRenderer::SetStringMarkerEXT(const char* marker)
    {
        if (marker == nullptr || !frameRecording_ || !commandBuffer_)
            return;
        commandBuffer_->pushDebugGroupLabel(marker);
        commandBuffer_->popDebugGroupLabel();
    }
}
