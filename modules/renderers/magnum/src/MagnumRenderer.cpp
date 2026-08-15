// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumRenderer.hpp"

#include "CNA/Internal/Renderers/Magnum/MagnumEffectRenderer.hpp"
#include "CNA/Internal/Renderers/Magnum/MagnumSpriteBatchRenderer.hpp"

#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/Pair.h>
#include <Corrade/Containers/StringView.h>
#include <Magnum/GL/Context.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/PixelFormat.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/Image.h>
#include <Magnum/ImageView.h>
#include <Magnum/Math/Color.h>
#include <Magnum/PixelFormat.h>
#include <Magnum/Platform/GLContext.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Magnum
{
    using Renderer = Mg::GL::Renderer;

    namespace
    {
        constexpr int kBytesPerPixel = 4;
        /// Shader locations the stock programs reserve for an optional per-instance world matrix.
        constexpr int kInstanceMatrixBaseLocation = 12;
        /// Texture unit EnvironmentMapEffect's cube map is bound to. Units 0 and 1 belong to the
        /// diffuse and second-layer 2D textures, which an env-mapped draw may also carry.
        constexpr int kEnvironmentMapSlot = 2;
        /// PbrEffect's own map units. Unit 0 stays the base colour, as in every other program; a
        /// PBR draw carries no second colour layer and no cube map, so these cannot collide.
        constexpr int kPbrNormalMapSlot = 1;
        constexpr int kPbrMetallicRoughnessMapSlot = 2;
        constexpr int kPbrEmissiveMapSlot = 3;
        constexpr int kPbrOcclusionMapSlot = 4;
        constexpr int kPbrSpecularMapSlot = 5;
        constexpr int kPbrSpecularColorMapSlot = 6;

        CNA::Platform::GlContextDescription RequestedContext(const int multiSampleCount)
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 3;
            description.minorVersion = 3;
            description.profile = CNA::Platform::GlProfile::Core;
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            if (multiSampleCount > 1)
            {
                description.multisampleBuffers = 1;
                description.multisampleSamples = multiSampleCount;
            }
            return description;
        }

        Mg::Color4 ToMagnumColor(float r, float g, float b, float a)
        {
            return Mg::Color4{r, g, b, a};
        }

        /// Inverse-transpose of the world matrix's upper-left 3x3, so a non-uniform scale does not
        /// skew transformed normals the way the raw 3x3 would.
        void BuildNormalMatrix(const float* worldColumnMajor, float (&out)[9])
        {
            const float* w = worldColumnMajor;
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float inverse = (determinant != 0.0f) ? (1.0f / determinant) : 0.0f;
            out[0] = (e * i - f * h) * inverse;
            out[1] = -(b * i - c * h) * inverse;
            out[2] = (b * f - c * e) * inverse;
            out[3] = -(d * i - f * g) * inverse;
            out[4] = (a * i - c * g) * inverse;
            out[5] = -(a * f - c * d) * inverse;
            out[6] = (d * h - e * g) * inverse;
            out[7] = -(a * h - b * g) * inverse;
            out[8] = (a * e - b * d) * inverse;
        }
    }

    MagnumRenderer::MagnumRenderer(const GraphicsRendererCreateArgs& args)
        : platformContext_(std::make_unique<PlatformGlContextOwner>(
              RequirePlatformGlContext(args.glContext, "MAGNUM"),
              RequirePlatformGlWindow(args.surface, "MAGNUM"),
              RequestedContext(args.multiSampleCount)))
        , surface_(args.surface)
        , magnumContext_(
              std::make_unique<::Magnum::Platform::GLContext>(::Magnum::NoCreate))
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , sampleCount_(args.multiSampleCount > 1 ? args.multiSampleCount : 1)
        , swapInterval_(args.swapInterval)
    {
        platformContext_->SetSwapInterval(swapInterval_);

        // Magnum loads its OpenGL entry points from whichever context is current, so this must
        // happen after MakeCurrent and never before.
        if (!magnumContext_->tryCreate())
        {
            throw std::runtime_error(
                "Magnum could not initialize against the platform-created OpenGL context.");
        }

        const auto granted = platformContext_->GetAttributes();
        sampleCount_ = granted.multisampleBuffers > 0 && granted.multisampleSamples > 1
            ? granted.multisampleSamples : 1;

        maxMrtTargets_ = std::max(1, std::min({4,
            Mg::GL::AbstractFramebuffer::maxDrawBuffers(),
            Mg::GL::Framebuffer::maxColorAttachments()}));

        const auto version = Mg::GL::Context::current().versionString();
        const auto renderer = Mg::GL::Context::current().rendererString();
        std::cout << "CNA: Magnum renderer on " << std::string{version.data(), version.size()}
                  << " (renderer: " << std::string{renderer.data(), renderer.size()}
                  << "); MSAA " << sampleCount_ << "x; MRT up to " << maxMrtTargets_
                  << " targets; SurfaceFormat: Color only" << std::endl;

        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);
        currentViewport_ = Mg::Range2Di{{}, Mg::Vector2i{physicalWidth, physicalHeight}};
        Mg::GL::defaultFramebuffer.setViewport(currentViewport_);

        Renderer::setFeature(Renderer::Feature::DepthTest, true);
        Renderer::setFeature(Renderer::Feature::Blending, false);

        // Registered last, after every fallible step above has succeeded: a constructor that throws
        // never runs its destructor, so an earlier registration would leave a dangling entry in the
        // shared window registry.
        RegisterForWindow(surface_.GetWindowId(), this);
    }

    MagnumRenderer::~MagnumRenderer()
    {
        UnregisterForWindow(surface_.GetWindowId());

        // Every GL object this renderer owns has to die while the context that created it is still
        // current: Magnum's GL object destructors consult GL::Context::current() and abort outright
        // when there is none. Member destruction order alone is not enough -- it runs after this
        // body, so each owned resource is explicitly released in front of Magnum's context.
        // PlatformGlContextOwner is declared first and is consequently destroyed last.
        defaultWhiteTexture_.reset();
        defaultFlatNormalTexture_.reset();
        mrtFramebuffer_.reset();
        stockShaders_.reset();
        magnumContext_.reset();
    }

    bool MagnumRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // Exhaustive over every GraphicsCapability member, with no default arm, so a member added
        // after this switch is a -Wswitch diagnostic rather than a confident wrong answer in
        // either direction. Each answer names what backs it.
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD:
                // Real vertex/index buffers, 3D draw routes and depth/stencil state throughout.
                return true;
            case CNA::GraphicsCapability::DepthStencilBuffer:
                // The context is created with a depth and stencil buffer, and both are cleared,
                // tested and masked through GL::Renderer.
                return true;
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return maxMrtTargets_ > 1;
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return Mg::GL::Sampler::maxMaxAnisotropy() > 1.0f;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return Mg::GL::Renderbuffer::maxSamples() > 1;
            case CNA::GraphicsCapability::OcclusionQuery:
                // Real GL_SAMPLES_PASSED queries through Magnum's SampleQuery.
                return true;
            case CNA::GraphicsCapability::CustomEffects:
                // ShaderEffect GLSL is compiled and linked at runtime with uniform assignment by
                // name and real driver diagnostics.
                return true;
            case CNA::GraphicsCapability::Texture3D:
                // A real volume resource: Texture3D::SetData()/GetData() persist and retrieve
                // voxels through Magnum's Texture3D wrapper.
                return true;
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // Each stream is bound at its own byte offset and stride through its own
                // DynamicAttribute, and a per-instance stream is just one whose divisor is
                // non-zero, so a declaration split across several buffers binds natively here.
                return true;
            case CNA::GraphicsCapability::WireFrame:
                // Desktop OpenGL has a real glPolygonMode, unlike the ES-profile renderers that
                // have to re-expand triangles into lines.
                return true;
            case CNA::GraphicsCapability::Instancing:
                // Real instanced draws: per-instance streams bind through addVertexBufferInstanced
                // with their own divisors and the draw carries a real instance count.
                return true;
            case CNA::GraphicsCapability::StencilBuffer:
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
        }
        return false;
    }

    // ---- Presentation ----

    void MagnumRenderer::GetPhysicalSize(int& width, int& height) const
    {
        surface_.GetDrawableSize(width, height);
    }

    void MagnumRenderer::GetLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            GetPhysicalSize(width, height);
            return;
        }
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physicalHeight > 0)
        {
            width = static_cast<int>(
                static_cast<double>(physicalWidth) * virtualHeight_ / physicalHeight + 0.5);
        }
        else
        {
            width = virtualWidth_ > 0 ? virtualWidth_ : physicalWidth;
        }
    }

    void MagnumRenderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void MagnumRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
    }

    void MagnumRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void MagnumRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void MagnumRenderer::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    bool MagnumRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                         float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0)
            return false;
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);
        if (physicalHeight <= 0)
            return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physicalHeight);
        logX = surface_.WindowToDrawable(windowX) * scale;
        logY = surface_.WindowToDrawable(windowY) * scale;
        return true;
    }

    bool MagnumRenderer::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        // The exact inverse of the scale above. There is no offset term because the default
        // FixedHeightDynamicWidth presentation derives the logical WIDTH from the window aspect,
        // so the logical viewport fills the window and no letterbox bars exist to offset past.
        if (virtualHeight_ <= 0)
            return false;
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);
        if (physicalHeight <= 0)
            return false;
        const float inverseScale =
            static_cast<float>(physicalHeight) / static_cast<float>(virtualHeight_);
        windowX = surface_.DrawableToWindow(logX * inverseScale);
        windowY = surface_.DrawableToWindow(logY * inverseScale);
        return true;
    }

    void MagnumRenderer::Present()
    {
        platformContext_->SwapBuffers();
    }

    // ---- Destination selection ----

    Mg::GL::AbstractFramebuffer& MagnumRenderer::GetActiveFramebuffer() const
    {
        if (bound_->mrtCount > 0 && mrtFramebuffer_)
            return *mrtFramebuffer_;
        if (auto* target = dynamic_cast<MagnumRenderTargetRenderer*>(bound_->renderTarget2D))
            return target->GetFramebuffer();
        if (auto* cube = dynamic_cast<MagnumRenderTargetCubeRenderer*>(bound_->renderTargetCube))
            return cube->GetFramebuffer();
        return Mg::GL::defaultFramebuffer;
    }

    Mg::Vector2i MagnumRenderer::DestinationExtent() const
    {
        if (bound_->width > 0 && bound_->height > 0)
            return Mg::Vector2i{bound_->width, bound_->height};
        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        return Mg::Vector2i{width, height};
    }

    bool MagnumRenderer::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (bound_->renderTarget2D == nullptr && bound_->mrtCount == 0)
            return false;
        width = bound_->width;
        height = bound_->height;
        return true;
    }

    void MagnumRenderer::BindDefaultFramebuffer()
    {
        Mg::GL::defaultFramebuffer.bind();
        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        currentViewport_ = Mg::Range2Di{{}, Mg::Vector2i{width, height}};
        Mg::GL::defaultFramebuffer.setViewport(currentViewport_);
    }

    void MagnumRenderer::FinalizeCurrentTargets()
    {
        // Nothing else in this renderer invokes UnbindAsRenderTarget(), so a destination switch is
        // the only thing that runs the outgoing target's MSAA resolve and mip regeneration.
        if (bound_->renderTarget2D != nullptr)
            bound_->renderTarget2D->UnbindAsRenderTarget();
        if (bound_->renderTargetCube != nullptr)
            bound_->renderTargetCube->UnbindAsRenderTarget();
        for (int slot = 0; slot < bound_->mrtCount; ++slot)
        {
            if (bound_->mrt[static_cast<std::size_t>(slot)] != nullptr)
                bound_->mrt[static_cast<std::size_t>(slot)]->UnbindAsRenderTarget();
        }

        bound_->renderTarget2D = nullptr;
        bound_->renderTargetCube = nullptr;
        bound_->mrt = {};
        bound_->mrtCount = 0;
        bound_->width = 0;
        bound_->height = 0;
    }

    void MagnumRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        FinalizeCurrentTargets();
        if (rt == nullptr)
        {
            BindDefaultFramebuffer();
            return;
        }

        bound_->renderTarget2D = rt;
        bound_->width = rt->GetWidth();
        bound_->height = rt->GetHeight();
        rt->BindAsRenderTarget();
        currentViewport_ = Mg::Range2Di{{}, Mg::Vector2i{bound_->width, bound_->height}};
    }

    void MagnumRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        FinalizeCurrentTargets();
        if (rt == nullptr)
        {
            BindDefaultFramebuffer();
            return;
        }

        bound_->renderTargetCube = rt;
        bound_->width = rt->GetSize();
        bound_->height = rt->GetSize();
        rt->BindAsRenderTargetFace(face);
        currentViewport_ = Mg::Range2Di{{}, Mg::Vector2i{bound_->width, bound_->height}};
    }

    void MagnumRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                                 int count)
    {
        FinalizeCurrentTargets();
        if (renderTargets == nullptr || count <= 0)
        {
            BindDefaultFramebuffer();
            return;
        }

        // A single binding is routed through the dedicated single-target paths so the ordinary case
        // keeps using the target's own framebuffer rather than the shared multi-target one.
        if (count == 1)
        {
            if (renderTargets[0].IsRenderTargetCubeFace())
            {
                SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                        renderTargets[0].GetCubeFace());
            }
            else
            {
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            }
            return;
        }

        const int slots = std::min(count, maxMrtTargets_);
        const Mg::Vector2i extent{renderTargets[0].GetWidth(), renderTargets[0].GetHeight()};
        if (!mrtFramebuffer_)
            mrtFramebuffer_ = std::make_unique<Mg::GL::Framebuffer>(Mg::Range2Di{{}, extent});
        mrtFramebuffer_->setViewport(Mg::Range2Di{{}, extent});

        std::vector<Corrade::Containers::Pair<Mg::UnsignedInt,
                                              Mg::GL::Framebuffer::DrawAttachment>> drawBuffers;
        drawBuffers.reserve(static_cast<std::size_t>(slots));

        for (int slot = 0; slot < slots; ++slot)
        {
            auto* target = dynamic_cast<MagnumRenderTargetRenderer*>(
                renderTargets[slot].GetRenderTarget2D());
            if (target == nullptr)
                continue;

            // A multisampled slot attaches the very storage its own framebuffer renders into, not
            // its resolved texture -- otherwise a target would silently drop to single-sample for
            // as long as it was part of a set. GraphicsDevice already rejects a set whose applied
            // sample counts differ, so the attachments cannot end up mismatched here.
            //
            // Nothing extra is needed to resolve: the blit a target runs on unbind reads that same
            // renderbuffer regardless of which framebuffer rendered into it.
            if (Mg::GL::Renderbuffer* multisampleColor = target->GetMultisampleColorRenderbuffer())
            {
                mrtFramebuffer_->attachRenderbuffer(Mg::GL::Framebuffer::ColorAttachment{
                                                        static_cast<Mg::UnsignedInt>(slot)},
                                                    *multisampleColor);
            }
            else
            {
                mrtFramebuffer_->attachTexture(Mg::GL::Framebuffer::ColorAttachment{
                                                   static_cast<Mg::UnsignedInt>(slot)},
                                               target->GetColorTexture(), 0);
            }
            bound_->mrt[static_cast<std::size_t>(slot)] = target;
            drawBuffers.emplace_back(static_cast<Mg::UnsignedInt>(slot),
                                     Mg::GL::Framebuffer::ColorAttachment{
                                         static_cast<Mg::UnsignedInt>(slot)});

            if (slot == 0 && target->GetDepthRenderbuffer() != nullptr)
            {
                mrtFramebuffer_->attachRenderbuffer(
                    target->GetDepthFormat() == 3
                        ? Mg::GL::Framebuffer::BufferAttachment::DepthStencil
                        : Mg::GL::Framebuffer::BufferAttachment::Depth,
                    *target->GetDepthRenderbuffer());
            }
        }

        mrtFramebuffer_->mapForDraw(Corrade::Containers::arrayView(drawBuffers.data(),
                                                                   drawBuffers.size()));
        bound_->mrtCount = slots;
        bound_->width = extent.x();
        bound_->height = extent.y();
        mrtFramebuffer_->bind();
        currentViewport_ = Mg::Range2Di{{}, extent};
        ApplyColorWriteMasks();
    }

    // ---- Resource creation ----

    std::unique_ptr<ITextureRenderer> MagnumRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<MagnumTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> MagnumRenderer::CreateSpriteBatch()
    {
        return std::make_unique<MagnumSpriteBatchRenderer>(this);
    }

    std::unique_ptr<IOcclusionQueryRenderer> MagnumRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<MagnumOcclusionQueryRenderer>();
    }

    std::unique_ptr<IRenderTargetRenderer> MagnumRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // Consumed by being deliberately unused: a GL framebuffer's colour attachment IS the
        // texture, and binding a framebuffer never touches its contents, so a target's previous
        // image is preserved by construction. The only thing that clears one is the explicit clear
        // GraphicsDevice issues for a DiscardContents target.
        (void)preserveContents;
        return std::make_unique<MagnumRenderTargetRenderer>(w, h, depthFormat, bound_, mipMap,
                                                           multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> MagnumRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // See CreateRenderTarget2D above for why this is unused.
        (void)preserveContents;
        return std::make_unique<MagnumRenderTargetCubeRenderer>(size, depthFormat, bound_, mipMap,
                                                               multiSampleCount);
    }

    std::unique_ptr<ITexture3DRenderer> MagnumRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<MagnumTexture3DRenderer>(w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeRenderer> MagnumRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<MagnumTextureCubeRenderer>(size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IEffectRenderer> MagnumRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto effect = std::make_unique<MagnumEffectRenderer>();
        if (!effect->CompileProgram(vertSrc, fragSrc))
            std::cerr << "CNA: Magnum effect failed to build:\n" << effect->GetCompileError()
                      << std::endl;
        return effect;
    }

    std::unique_ptr<IVertexBufferRenderer> MagnumRenderer::CreateVertexBuffer(
        int vertex_capacity)
    {
        return std::make_unique<MagnumVertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> MagnumRenderer::CreateIndexBuffer16(
        int index_capacity)
    {
        return std::make_unique<MagnumIndexBufferRenderer>(index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> MagnumRenderer::CreateIndexBuffer32(
        int index_capacity)
    {
        return std::make_unique<MagnumIndexBufferRenderer>(index_capacity, true);
    }

    void MagnumRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            return;

        const Mg::Vector2i extent = DestinationExtent();
        const int glY = extent.y() - y - h;

        std::vector<uint8_t> scratch(static_cast<std::size_t>(w) * h * kBytesPerPixel);
        GetActiveFramebuffer().read(
            Mg::Range2Di{Mg::Vector2i{x, glY}, Mg::Vector2i{x + w, glY + h}},
            Mg::MutableImageView2D{Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{w, h},
                                   Corrade::Containers::ArrayView<void>{scratch.data(),
                                                                        scratch.size()}});

        // GL returns rows bottom-first; the public contract is top row first.
        const std::size_t rowBytes = static_cast<std::size_t>(w) * kBytesPerPixel;
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(pixels + static_cast<std::size_t>(row) * rowBytes,
                        scratch.data() + static_cast<std::size_t>(h - 1 - row) * rowBytes,
                        rowBytes);
        }
    }

    // ---- Clears ----

    void MagnumRenderer::ApplyColorWriteMasks()
    {
        const int activeSlots = std::max(1, bound_->mrtCount);
        for (int slot = 0; slot < activeSlots; ++slot)
        {
            const int mask = colorWriteMasks_[static_cast<std::size_t>(slot)];
            Renderer::setColorMask(static_cast<Mg::UnsignedInt>(slot),
                                   ColorWriteHasRed(mask), ColorWriteHasGreen(mask),
                                   ColorWriteHasBlue(mask), ColorWriteHasAlpha(mask));
        }
    }

    void MagnumRenderer::ForceAllColorWriteMasks()
    {
        const int activeSlots = std::max(1, bound_->mrtCount);
        for (int slot = 0; slot < activeSlots; ++slot)
            Renderer::setColorMask(static_cast<Mg::UnsignedInt>(slot), true, true, true, true);
    }

    bool MagnumRenderer::HasRestrictedColorWriteMask() const
    {
        const int activeSlots = std::max(1, bound_->mrtCount);
        for (int slot = 0; slot < activeSlots; ++slot)
        {
            if (colorWriteMasks_[static_cast<std::size_t>(slot)] != 15)
                return true;
        }
        return false;
    }

    void MagnumRenderer::Clear(float r, float g, float b, float a)
    {
        // XNA/FNA's Clear ignores BlendState's colour write masks, so any restriction the game set
        // is lifted for the duration of the clear and put straight back afterwards.
        const bool restricted = HasRestrictedColorWriteMask();
        if (restricted)
            ForceAllColorWriteMasks();

        const Mg::Color4 color = ToMagnumColor(r, g, b, a);
        Mg::GL::AbstractFramebuffer& framebuffer = GetActiveFramebuffer();
        if (&framebuffer == &Mg::GL::defaultFramebuffer)
        {
            Mg::GL::defaultFramebuffer.clearColor(color);
        }
        else
        {
            auto& offscreen = static_cast<Mg::GL::Framebuffer&>(framebuffer);
            const int slots = std::max(1, bound_->mrtCount);
            for (int slot = 0; slot < slots; ++slot)
                offscreen.clearColor(slot, color);
        }

        if (restricted)
            ApplyColorWriteMasks();
    }

    void MagnumRenderer::ClearDepth(float depth)
    {
        // Depth writes must be on for a depth clear to land, and XNA's Clear ignores
        // DepthStencilState.DepthBufferWriteEnable, so the game's own setting is restored after.
        if (!depthWriteEnabled_)
            Renderer::setDepthMask(true);
        GetActiveFramebuffer().clearDepth(depth);
        if (!depthWriteEnabled_)
            Renderer::setDepthMask(false);
    }

    void MagnumRenderer::ClearStencil(int stencil)
    {
        if (stencilWriteMask_ != 0xFF)
            Renderer::setStencilMask(0xFFu);
        GetActiveFramebuffer().clearStencil(stencil);
        if (stencilWriteMask_ != 0xFF)
            Renderer::setStencilMask(static_cast<Mg::UnsignedInt>(stencilWriteMask_));
    }

    void MagnumRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void MagnumRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        if (!depthWriteEnabled_)
            Renderer::setDepthMask(true);
        if (stencilWriteMask_ != 0xFF)
            Renderer::setStencilMask(0xFFu);
        GetActiveFramebuffer().clearDepthStencil(depth, stencil);
        if (!depthWriteEnabled_)
            Renderer::setDepthMask(false);
        if (stencilWriteMask_ != 0xFF)
            Renderer::setStencilMask(static_cast<Mg::UnsignedInt>(stencilWriteMask_));
    }

    void MagnumRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        ClearStencil(stencil);
    }

    void MagnumRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                          float depth, int stencil)
    {
        Clear(r, g, b, a);
        ClearDepthAndStencil(depth, stencil);
    }

    // ---- Pipeline state ----

    void MagnumRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 -- the Opaque preset, which is src=One/dst=Zero on both
        // channels and therefore no blending at all.
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        Renderer::setFeature(Renderer::Feature::Blending, blendEnabled);
        if (blendEnabled)
        {
            Renderer::setBlendFunction(ToBlendFunction(colorSrcBlend),
                                       ToBlendFunction(colorDstBlend),
                                       ToBlendFunction(alphaSrcBlend),
                                       ToBlendFunction(alphaDstBlend));
            Renderer::setBlendEquation(ToBlendEquation(colorBlendFunc),
                                       ToBlendEquation(alphaBlendFunc));
        }

        for (int slot = 0; slot < 4; ++slot)
            colorWriteMasks_[static_cast<std::size_t>(slot)] = writeState.colorWriteChannels[slot];
        ApplyColorWriteMasks();

        // BlendState.MultiSampleMask reaches this renderer but only its all-ones default is applied:
        // a real coverage mask needs GL_SAMPLE_MASK plus glSampleMaski, which is a documented gap
        // rather than a value silently dropped on the floor.
        (void)writeState.multiSampleMask;
    }

    void MagnumRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int depthFunc,
        bool stencilEnable, int stencilFunc,
        int stencilPass, int stencilFail, int stencilDepthFail,
        int stencilMask, int stencilWriteMask, int referenceStencil,
        bool twoSidedStencilMode,
        int ccwStencilFunc, int ccwStencilPass, int ccwStencilFail, int ccwStencilDepthFail)
    {
        Renderer::setFeature(Renderer::Feature::DepthTest, depthEnable);
        Renderer::setDepthMask(depthWriteEnable);
        depthWriteEnabled_ = depthWriteEnable;
        if (depthEnable)
            Renderer::setDepthFunction(ToDepthFunction(depthFunc));

        Renderer::setFeature(Renderer::Feature::StencilTest, stencilEnable);
        referenceStencil_ = referenceStencil;
        stencilReadMask_ = stencilMask;
        stencilFunction_ = stencilFunc;
        stencilWriteMask_ = stencilWriteMask;
        if (!stencilEnable)
            return;

        const auto onStencilFail = ToStencilOperation(stencilFail);
        const auto onDepthFail = ToStencilOperation(stencilDepthFail);
        const auto onPass = ToStencilOperation(stencilPass);

        if (twoSidedStencilMode)
        {
            Renderer::setStencilFunction(Renderer::PolygonFacing::Front,
                                         ToStencilFunction(stencilFunc), referenceStencil,
                                         static_cast<Mg::UnsignedInt>(stencilMask));
            Renderer::setStencilOperation(Renderer::PolygonFacing::Front,
                                          onStencilFail, onDepthFail, onPass);
            Renderer::setStencilMask(Renderer::PolygonFacing::Front,
                                     static_cast<Mg::UnsignedInt>(stencilWriteMask));

            Renderer::setStencilFunction(Renderer::PolygonFacing::Back,
                                         ToStencilFunction(ccwStencilFunc), referenceStencil,
                                         static_cast<Mg::UnsignedInt>(stencilMask));
            Renderer::setStencilOperation(Renderer::PolygonFacing::Back,
                                          ToStencilOperation(ccwStencilFail),
                                          ToStencilOperation(ccwStencilDepthFail),
                                          ToStencilOperation(ccwStencilPass));
            Renderer::setStencilMask(Renderer::PolygonFacing::Back,
                                     static_cast<Mg::UnsignedInt>(stencilWriteMask));
        }
        else
        {
            Renderer::setStencilFunction(ToStencilFunction(stencilFunc), referenceStencil,
                                         static_cast<Mg::UnsignedInt>(stencilMask));
            Renderer::setStencilOperation(onStencilFail, onDepthFail, onPass);
            Renderer::setStencilMask(static_cast<Mg::UnsignedInt>(stencilWriteMask));
        }
    }

    void MagnumRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                     bool scissorTestEnable,
                                                     float depthBias, float slopeScaleDepthBias)
    {
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2. OpenGL's default
        // front face is counter-clockwise, so a clockwise face is a back face.
        if (cullMode == 0)
        {
            Renderer::setFeature(Renderer::Feature::FaceCulling, false);
        }
        else
        {
            Renderer::setFeature(Renderer::Feature::FaceCulling, true);
            Renderer::setFaceCullingMode(cullMode == 1 ? Renderer::PolygonFacing::Back
                                                       : Renderer::PolygonFacing::Front);
        }

        Renderer::setFeature(Renderer::Feature::ScissorTest, scissorTestEnable);

        wireframe_ = (fillMode == 1);
        Renderer::setPolygonMode(wireframe_ ? Renderer::PolygonMode::Line
                                            : Renderer::PolygonMode::Fill);

        // DepthBias/SlopeScaleDepthBias map straight onto GL polygon offset, matching this
        // project's established convention. Always enabled -- factor 0 / units 0 is a genuine
        // no-op, so there is nothing to conditionally disable.
        Renderer::setFeature(Renderer::Feature::PolygonOffsetFill, true);
        Renderer::setPolygonOffset(slopeScaleDepthBias, depthBias);
    }

    void MagnumRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                  int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots)
            return;
        MagnumSamplerState& state = samplers_[static_cast<std::size_t>(slot)];
        state.filter = filter;
        state.addressU = addressU;
        state.addressV = addressV;
        state.maxAnisotropy = maxAnisotropy;
    }

    void MagnumRenderer::BindTextureToSlot(int slot, const ITextureRenderer* texture)
    {
        if (texture == nullptr || slot < 0 || slot >= kMaxSamplerSlots)
            return;

        const MagnumSamplerState& state = samplers_[static_cast<std::size_t>(slot)];
        // Magnum expresses sampler state on the texture object, so the slot's requested state is
        // written onto whatever texture the draw binds there rather than into a sampler object.
        if (auto* plain = dynamic_cast<const MagnumTextureRenderer*>(texture))
        {
            ApplySamplerStateTo(plain->GetTexture(), state, plain->GetLevelCount());
            plain->GetTexture().bind(slot);
            return;
        }
        if (auto* target = dynamic_cast<const MagnumRenderTargetRenderer*>(texture))
        {
            ApplySamplerStateTo(target->GetColorTexture(), state, target->GetLevelCount());
            target->GetColorTexture().bind(slot);
            return;
        }
        texture->BindGL();
    }

    void MagnumRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        Renderer::setBlendColor(ToMagnumColor(r, g, b, a));
    }

    void MagnumRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
        Renderer::setStencilFunction(ToStencilFunction(stencilFunction_), value,
                                     static_cast<Mg::UnsignedInt>(stencilReadMask_));
    }

    void MagnumRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;
        // GL's scissor origin is bottom-left; XNA's rectangle is top-left, and the flip uses the
        // ACTIVE destination's height so a target smaller than the window still scissors correctly.
        const Mg::Vector2i extent = DestinationExtent();
        const int glY = extent.y() - y - h;
        Renderer::setScissor(Mg::Range2Di{Mg::Vector2i{x, glY}, Mg::Vector2i{x + w, glY + h}});
        // Whether the scissor test runs at all is controlled exclusively by
        // RasterizerState.ScissorTestEnable, never here.
    }

    void MagnumRenderer::ApplyViewport(const Mg::Range2Di& viewport)
    {
        currentViewport_ = viewport;
        GetActiveFramebuffer().setViewport(viewport);
    }

    void MagnumRenderer::SetViewport(int x, int y, int w, int h,
                                            float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0)
            return;
        const Mg::Vector2i extent = DestinationExtent();
        const int glY = extent.y() - y - h;
        ApplyViewport(Mg::Range2Di{Mg::Vector2i{x, glY}, Mg::Vector2i{x + w, glY + h}});
        Renderer::setDepthRange(minDepth, maxDepth);
    }

    void MagnumRenderer::SetDepthTestEnabled(bool enabled)
    {
        Renderer::setFeature(Renderer::Feature::DepthTest, enabled);
    }

    void MagnumRenderer::SetBlendEnabled(bool enabled)
    {
        Renderer::setFeature(Renderer::Feature::Blending, enabled);
    }

    void MagnumRenderer::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
        Renderer::setDepthMask(enabled);
    }

    // ---- Draws ----

    MagnumProgram* MagnumRenderer::SelectProgram(std::size_t stride,
                                                        const GpuDrawParams& params)
    {
        if (params.customEffectRenderer != nullptr)
        {
            if (auto* effect = dynamic_cast<MagnumEffectRenderer*>(params.customEffectRenderer))
                return effect->IsValid() ? &effect->GetProgram() : nullptr;
            return nullptr;
        }

        MagnumStockSelector selector;
        selector.strideInBytes = stride;
        selector.dualTexture = params.dualTexture;
        selector.envMapping = params.envMapping;
        selector.skinned = params.skinned;
        selector.pbr = params.pbr;
        // XNA's own BasicEffect/SkinnedEffect default is per-vertex lighting; per-pixel is the
        // opt-in. An unlit draw renders identically either way, so it is not asked to pick.
        selector.vertexLighting = params.lightingEnabled && !params.preferPerPixelLighting;

        MagnumStockProgram stockProgram{};
        if (!SelectStockProgram(selector, stockProgram))
            return nullptr;
        return stockShaders_->ForProgram(stockProgram);
    }

    void MagnumRenderer::BindDrawParams(MagnumProgram& program,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               const GpuDrawParams& params, bool instanced)
    {
        const Matrix worldViewProjection = world * view * projection;
        float columnMajor[16];
        worldViewProjection.ToColumnMajor(columnMajor);
        program.SetMatrix4(program.LocationOf("uWVP"), columnMajor);
        program.SetMatrix4(program.LocationOf("uWorld"), params.worldColMajor);
        program.SetFloat(program.LocationOf("uCnaInstanced"), instanced ? 1.0f : 0.0f);

        float normalMatrix[9];
        BuildNormalMatrix(params.worldColMajor, normalMatrix);
        program.SetMatrix3(program.LocationOf("uNormalMatrix"), normalMatrix);

        program.SetVector4(program.LocationOf("uDiffuseColor"), Mg::Vector4{
            params.diffuseColor[0], params.diffuseColor[1],
            params.diffuseColor[2], params.diffuseColor[3]});
        program.SetFloat(program.LocationOf("uVertexColorEnabled"),
                         params.vertexColorEnabled ? 1.0f : 0.0f);
        program.SetFloat(program.LocationOf("uTextureEnabled"),
                         (params.textureEnabled && params.texture0 != nullptr) ? 1.0f : 0.0f);
        program.SetInt(program.LocationOf("uTexture"), 0);
        program.SetFloat(program.LocationOf("uLightingEnabled"),
                         params.lightingEnabled ? 1.0f : 0.0f);

        if (params.lightingEnabled)
        {
            program.SetVector3(program.LocationOf("uAmbientColor"), Mg::Vector3{
                params.ambientColor[0], params.ambientColor[1], params.ambientColor[2]});
            program.SetVector3(program.LocationOf("uLight0Dir"), Mg::Vector3{
                params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]});
            program.SetVector3(program.LocationOf("uLight0Diffuse"), Mg::Vector3{
                params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2]});
            program.SetVector3(program.LocationOf("uLight0Specular"), Mg::Vector3{
                params.light0Specular[0], params.light0Specular[1], params.light0Specular[2]});
            program.SetVector3(program.LocationOf("uLight1Dir"), Mg::Vector3{
                params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]});
            program.SetVector3(program.LocationOf("uLight1Diffuse"), Mg::Vector3{
                params.light1Diffuse[0], params.light1Diffuse[1], params.light1Diffuse[2]});
            program.SetVector3(program.LocationOf("uLight1Specular"), Mg::Vector3{
                params.light1Specular[0], params.light1Specular[1], params.light1Specular[2]});
            program.SetVector3(program.LocationOf("uLight2Dir"), Mg::Vector3{
                params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]});
            program.SetVector3(program.LocationOf("uLight2Diffuse"), Mg::Vector3{
                params.light2Diffuse[0], params.light2Diffuse[1], params.light2Diffuse[2]});
            program.SetVector3(program.LocationOf("uLight2Specular"), Mg::Vector3{
                params.light2Specular[0], params.light2Specular[1], params.light2Specular[2]});
            program.SetVector3(program.LocationOf("uSpecularColor"), Mg::Vector3{
                params.specularColor[0], params.specularColor[1], params.specularColor[2]});
            program.SetFloat(program.LocationOf("uSpecularPower"), params.specularPower);
            program.SetVector3(program.LocationOf("uEyePosition"), Mg::Vector3{
                params.eyePositionWorld[0], params.eyePositionWorld[1],
                params.eyePositionWorld[2]});
            program.SetVector3(program.LocationOf("uEmissiveColor"), Mg::Vector3{
                params.emissiveColor[0], params.emissiveColor[1], params.emissiveColor[2]});
        }

        program.SetVector4(program.LocationOf("uAlphaTest"), Mg::Vector4{
            params.alphaTest[0], params.alphaTest[1], params.alphaTest[2], params.alphaTest[3]});
        program.SetVector4(program.LocationOf("uFogVector"), Mg::Vector4{
            params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3]});
        program.SetVector3(program.LocationOf("uFogColor"), Mg::Vector3{
            params.fogColor[0], params.fogColor[1], params.fogColor[2]});

        // One flag per texture unit, filled in beside the bind it describes so the flag and the
        // resource can never disagree.
        Mg::Vector4 renderTargetFlips{0.0f, 0.0f, 0.0f, 0.0f};
        if (params.texture0 != nullptr)
        {
            BindTextureToSlot(0, params.texture0);
            program.SetInt(program.LocationOf("uTexture"), 0);
            renderTargetFlips[0] = SampledRowOrderIsBottomUp(params.texture0) ? 1.0f : 0.0f;
        }
        if (params.texture1 != nullptr)
        {
            BindTextureToSlot(1, params.texture1);
            program.SetInt(program.LocationOf("uTexture2"), 1);
            renderTargetFlips[1] = SampledRowOrderIsBottomUp(params.texture1) ? 1.0f : 0.0f;
        }
        // PbrEffect's material maps. Every one is sampled unconditionally by the shader, so an
        // absent map is bound to a neutral stand-in rather than left at whatever the previous draw
        // happened to leave in that unit.
        if (params.pbr)
        {
            EnsureDefaultPbrMaps();
            float occlusionFlip = 0.0f;
            float specularFlip = 0.0f;
            float specularColorFlip = 0.0f;
            program.SetVector4(program.LocationOf("uDiffuseColor"), Mg::Vector4{
                params.diffuseColor[0], params.diffuseColor[1],
                params.diffuseColor[2], params.diffuseColor[3]});
            BindPbrMap(program, "uNormalMap", kPbrNormalMapSlot, params.pbrNormalMap,
                       *defaultFlatNormalTexture_, renderTargetFlips[1]);
            BindPbrMap(program, "uMetallicRoughnessMap", kPbrMetallicRoughnessMapSlot,
                       params.pbrMetallicRoughnessMap, *defaultWhiteTexture_,
                       renderTargetFlips[2]);
            BindPbrMap(program, "uEmissiveMap", kPbrEmissiveMapSlot, params.pbrEmissiveMap,
                       *defaultWhiteTexture_, renderTargetFlips[3]);
            BindPbrMap(program, "uOcclusionMap", kPbrOcclusionMapSlot, params.pbrOcclusionMap,
                       *defaultWhiteTexture_, occlusionFlip);
            BindPbrMap(program, "uSpecularMap", kPbrSpecularMapSlot, params.pbrSpecularMap,
                       *defaultWhiteTexture_, specularFlip);
            BindPbrMap(program, "uSpecularColorMap", kPbrSpecularColorMapSlot,
                       params.pbrSpecularColorMap, *defaultWhiteTexture_, specularColorFlip);
            program.SetFloat(program.LocationOf("uMetallicFactor"), params.pbrMetallicFactor);
            program.SetFloat(program.LocationOf("uRoughnessFactor"), params.pbrRoughnessFactor);
            program.SetFloat(program.LocationOf("uNormalScale"), params.pbrNormalScale);
            program.SetFloat(program.LocationOf("uOcclusionStrength"), params.pbrOcclusionStrength);
            program.SetVector3(program.LocationOf("uSrgb"), Mg::Vector3{
                params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f,
                params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f,
                params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f});
            program.SetVector4(program.LocationOf("uSpecularFresnelInputs"), Mg::Vector4{
                params.pbrDielectricF0Unclamped[0], params.pbrDielectricF0Unclamped[1],
                params.pbrDielectricF0Unclamped[2], params.pbrSpecularFactor});
            program.SetVector3(program.LocationOf("uSpecularMapFlags"), Mg::Vector3{
                params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f,
                specularFlip, specularColorFlip});
            for (int row = 0; row < 10; ++row)
            {
                const float* values = params.pbrTextureTransformRows[row];
                program.SetVector4(program.LocationOf(
                    "uTextureTransformRows[" + std::to_string(row) + "]"),
                    Mg::Vector4{values[0], values[1], values[2], values[3]});
            }
            for (int row = 0; row < 4; ++row)
            {
                const float* values = params.pbrSpecularTextureTransformRows[row];
                program.SetVector4(program.LocationOf(
                    "uSpecularTextureTransformRows[" + std::to_string(row) + "]"),
                    Mg::Vector4{values[0], values[1], values[2], values[3]});
            }
            program.SetVector3(program.LocationOf("uAmbientColor"), Mg::Vector3{
                params.ambientColor[0], params.ambientColor[1], params.ambientColor[2]});
            program.SetVector3(program.LocationOf("uEmissiveColor"), Mg::Vector3{
                params.emissiveColor[0], params.emissiveColor[1], params.emissiveColor[2]});
            // A fifth unit does not fit in uRtFlipV's four components, so the occlusion map's own
            // flag travels in its own uniform. This is the only program that samples that far.
            program.SetVector4(program.LocationOf("uRtFlipVHi"),
                               Mg::Vector4{occlusionFlip, 0.0f, 0.0f, 0.0f});
        }

        // SkinnedEffect's bone palette. Only the bones the draw declares are uploaded -- the
        // array's remaining entries are never indexed, since a vertex's bone index cannot exceed
        // the palette the effect populated.
        if (params.skinned && params.boneCount > 0)
        {
            program.SetMatrix4Array(program.LocationOf("uBones"), params.boneTransforms,
                                    std::min(params.boneCount, kMagnumMaxBones));
            program.SetInt(program.LocationOf("uWeightsPerVertex"),
                           std::clamp(params.weightsPerVertex, 1, 4));
        }

        // EnvironmentMapEffect's cube map goes to its own unit so it never displaces the diffuse
        // texture, and the blend/Fresnel terms travel with it: a bound cube map with no blend
        // amount is a no-op reflection, which is not the same thing as no cube map at all.
        if (params.envMap != nullptr)
        {
            BindTextureCubeToSlot(kEnvironmentMapSlot, params.envMap);
            program.SetInt(program.LocationOf("uEnvMap"), kEnvironmentMapSlot);
            program.SetFloat(program.LocationOf("uEnvMapAmount"), params.envMapAmount);
            program.SetFloat(program.LocationOf("uFresnelEnabled"),
                             params.fresnelEnabled ? 1.0f : 0.0f);
            program.SetFloat(program.LocationOf("uFresnelFactor"), params.fresnelFactor);
            program.SetVector3(program.LocationOf("uEnvMapSpecular"), Mg::Vector3{
                params.envMapSpecular[0], params.envMapSpecular[1], params.envMapSpecular[2]});
        }

        program.SetVector4(program.LocationOf("uRtFlipV"), renderTargetFlips);
    }

    void MagnumRenderer::EnsureDefaultPbrMaps()
    {
        if (defaultWhiteTexture_ && defaultFlatNormalTexture_)
            return;

        const auto makeSingleTexel = [](const std::array<uint8_t, 4>& texel) {
            auto texture = std::make_unique<Mg::GL::Texture2D>();
            texture->setStorage(1, Mg::GL::TextureFormat::RGBA8, Mg::Vector2i{1, 1});
            texture->setSubImage(0, {}, Mg::ImageView2D{
                Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{1, 1},
                Corrade::Containers::ArrayView<const void>{texel.data(), texel.size()}});
            texture->setMinificationFilter(Mg::GL::SamplerFilter::Nearest,
                                          Mg::GL::SamplerMipmap::Base)
                    .setMagnificationFilter(Mg::GL::SamplerFilter::Nearest)
                    .setWrapping(Mg::GL::SamplerWrapping::ClampToEdge);
            return texture;
        };

        if (!defaultWhiteTexture_)
            defaultWhiteTexture_ = makeSingleTexel({255, 255, 255, 255});
        if (!defaultFlatNormalTexture_)
        {
            // (0.5, 0.5, 1) decodes to a +Z tangent-space normal, i.e. "no perturbation".
            defaultFlatNormalTexture_ = makeSingleTexel({128, 128, 255, 255});
        }
    }

    void MagnumRenderer::BindPbrMap(MagnumProgram& program, const char* uniformName,
                                           int slot, const ITextureRenderer* texture,
                                           Mg::GL::Texture2D& fallback, float& flipOut)
    {
        if (texture != nullptr)
        {
            BindTextureToSlot(slot, texture);
            flipOut = SampledRowOrderIsBottomUp(texture) ? 1.0f : 0.0f;
        }
        else
        {
            fallback.bind(slot);
            flipOut = 0.0f;
        }
        program.SetInt(program.LocationOf(uniformName), slot);
    }

    void MagnumRenderer::BindTextureCubeToSlot(int slot, const ITextureCubeRenderer* texture)
    {
        if (texture == nullptr || slot < 0 || slot >= kMaxSamplerSlots)
            return;

        const MagnumSamplerState& state = samplers_[static_cast<std::size_t>(slot)];
        if (auto* cube = dynamic_cast<const MagnumTextureCubeRenderer*>(texture))
        {
            ApplySamplerStateTo(cube->GetTexture(), state, cube->GetLevelCount());
            cube->GetTexture().bind(slot);
            return;
        }
        if (auto* target = dynamic_cast<const MagnumRenderTargetCubeRenderer*>(texture))
        {
            ApplySamplerStateTo(target->GetTexture(), state, target->GetLevelCount());
            target->GetTexture().bind(slot);
            return;
        }
        texture->BindGL();
    }

    void MagnumRenderer::DrawGeneric(const IVertexBufferRenderer& vb,
                                            const IIndexBufferRenderer* ib,
                                            const Matrix& world, const Matrix& view,
                                            const Matrix& projection,
                                            PrimitiveType primitive, int primitiveCount,
                                            int instanceCount, const GpuDrawParams& params)
    {
        if (primitiveCount <= 0 || instanceCount <= 0)
            return;

        const auto* vertexBuffer = dynamic_cast<const MagnumVertexBufferRenderer*>(&vb);
        if (vertexBuffer == nullptr)
            return;

        const std::size_t stride = CombinedVertexStrideOr(params, vertexBuffer->GetStride());
        if (stride == 0)
            return;

        const bool custom = params.customEffectRenderer != nullptr;

        // REMED-GFX-DECL-GUARD: the stock route resolves its attribute layout from the combined
        // stride alone, so a declaration those stride-derived offsets would misread must be
        // refused here, before any program is compiled, any mesh is built and anything is bound.
        // The custom-effect route binds each element from the declaration itself and is faithful
        // by construction, so it is not guarded.
        if (!custom)
        {
            const char* route = ib != nullptr
                ? (instanceCount > 1 ? "instanced-indexed" : "ordinary-indexed")
                : "ordinary";
            RequireFaithfulDeclarationEXT(*vertexBuffer, params, stride, route);
        }

        MagnumProgram* program = SelectProgram(stride, params);
        if (program == nullptr)
        {
            // An invalid custom effect already surfaced its diagnostics through
            // IEffectRenderer::GetCompileError(); skipping its draws is the established contract.
            if (custom)
                return;
            // A stock draw with no program for its layout must refuse rather than silently render
            // nothing -- the caller gets a catchable, accurate answer and the device stays usable.
            std::string flags;
            if (params.dualTexture) flags += " dual-texture";
            if (params.envMapping)  flags += " env-map";
            if (params.skinned)     flags += " skinned";
            if (params.pbr)         flags += " pbr";
            throw System::NotSupportedException(
                "Magnum: no stock program exists for a " + std::to_string(stride) +
                "-byte vertex record" + (flags.empty() ? std::string() : " with" + flags) +
                " -- the draw is refused rather than silently dropped.");
        }

        // A stream's stride comes from its VertexDeclaration, which is empty -- stride 0 -- for the
        // raw-upload route `VertexBuffer(device, count)` + `SetDataRaw(data, count, stride)` that
        // SkinnedEffect's own vertex layout is normally fed through. The real stride is then the one
        // the upload itself revealed, so that is what a zero falls back to; treating the zero as
        // "no stream" bound nothing at all and rendered an empty frame.
        const auto strideOf = [](const GpuVertexStreamBinding& stream,
                                 const MagnumVertexBufferRenderer& buffer) {
            return stream.strideInBytes > 0
                ? stream.strideInBytes
                : static_cast<int>(buffer.GetStride());
        };
        const GpuVertexStreamBinding* instanceStream = FirstInstanceStream(params);
        BindDrawParams(*program, world, view, projection, params,
                       instanceStream != nullptr && instanceCount > 1);

        const auto* indexBuffer = ib != nullptr
            ? dynamic_cast<const MagnumIndexBufferRenderer*>(ib)
            : nullptr;
        if (ib != nullptr && indexBuffer == nullptr)
            return;

        // The draw's shared start term goes through the native base-vertex parameter --
        // glDrawArrays' `first`, glDrawElementsBaseVertex's basevertex -- rather than being folded
        // into the attribute offsets. Both advance every per-vertex stream by that many of its OWN
        // elements and leave per-instance streams alone, which is exactly the documented semantics,
        // and keeping it out of the binding is what lets a draw sweeping its start vertex reuse one
        // cached vertex array instead of building a new one per value. A per-stream `vertexOffset`
        // remainder still IS folded in: it differs per stream, so no single native term expresses it.
        const int perVertexBase = ib != nullptr ? params.baseVertex : params.vertexStart;

        // Everything the binding depends on, and nothing that does not. Primitive, element count,
        // instance count, base vertex and index offset are all per-draw setters on an existing
        // vertex array, so none of them belong here.
        std::vector<std::uint64_t> cacheKey;
        cacheKey.reserve(8 + 5 * static_cast<std::size_t>(params.vertexStreamCount));
        cacheKey.push_back(custom ? 1u : 0u);
        cacheKey.push_back(static_cast<std::uint64_t>(stride));
        cacheKey.push_back(indexBuffer != nullptr ? indexBuffer->GetIdentity() : 0u);
        cacheKey.push_back(indexBuffer != nullptr
            ? static_cast<std::uint64_t>(indexBuffer->GetIndexSize()) : 0u);
        cacheKey.push_back(static_cast<std::uint64_t>(params.vertexStreamCount));
        if (params.vertexStreamCount > 0)
        {
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                const auto* streamBuffer =
                    dynamic_cast<const MagnumVertexBufferRenderer*>(stream.buffer);
                cacheKey.push_back(streamBuffer != nullptr ? streamBuffer->GetIdentity() : 0u);
                cacheKey.push_back(streamBuffer != nullptr
                    ? streamBuffer->GetDeclarationRevision() : 0u);
                cacheKey.push_back(streamBuffer != nullptr
                    && !streamBuffer->GetDeclarationElements().empty() ? 1u : 0u);
                cacheKey.push_back(streamBuffer != nullptr
                    ? static_cast<std::uint64_t>(strideOf(stream, *streamBuffer)) : 0u);
                cacheKey.push_back(static_cast<std::uint64_t>(stream.vertexOffset));
                cacheKey.push_back(static_cast<std::uint64_t>(stream.instanceFrequency));
            }
        }
        else
        {
            cacheKey.push_back(vertexBuffer->GetIdentity());
            cacheKey.push_back(vertexBuffer->GetDeclarationRevision());
            cacheKey.push_back(vertexBuffer->GetDeclarationElements().empty() ? 0u : 1u);
        }

        // The cache lives on the stream the draw named, so a destroyed buffer takes its own vertex
        // arrays with it rather than leaving the renderer holding arrays that bind deleted storage.
        const auto finishDraw = [&](Mg::GL::Mesh& mesh) {
            mesh.setPrimitive(ToMeshPrimitive(primitive));
            mesh.setCount(VertexCountForPrimitives(primitive, primitiveCount));
            mesh.setInstanceCount(std::max(1, instanceCount));
            mesh.setBaseVertex(perVertexBase);
            // setIndexOffset counts INDEX ELEMENTS, not bytes: Magnum multiplies it by the index
            // type size itself before adding the buffer's own base offset.
            if (indexBuffer != nullptr)
                mesh.setIndexOffset(params.startIndex);
            program->draw(mesh);
        };

        if (Mg::GL::Mesh* cached = vertexBuffer->FindCachedMesh(cacheKey))
        {
            finishDraw(*cached);
            return;
        }

        auto owned = std::make_unique<Mg::GL::Mesh>();
        Mg::GL::Mesh& mesh = *owned;

        // Binds one resolved attribute out of one bound stream. `elementBase` is in that stream's
        // own vertex (or instance) elements, so every stream advances by its OWN stride -- which is
        // the whole point of carrying the bindings separately rather than deriving them from
        // stream 0.
        const auto bindAttribute = [&mesh](const MagnumVertexBufferRenderer& buffer,
                                           const MagnumVertexAttribute& attribute,
                                           int strideInBytes, int elementBase, int instanceFrequency)
        {
            const GLintptr offset =
                static_cast<GLintptr>(elementBase) * strideInBytes + attribute.offsetInStream;
            if (offset < 0)
                return;
            if (instanceFrequency > 0)
            {
                mesh.addVertexBufferInstanced(
                    buffer.GetBuffer(), static_cast<Mg::UnsignedInt>(instanceFrequency),
                    offset, strideInBytes, ToDynamicAttribute(attribute));
            }
            else
            {
                mesh.addVertexBuffer(buffer.GetBuffer(), offset, strideInBytes,
                                     ToDynamicAttribute(attribute));
            }
        };

        if (params.vertexStreamCount > 0)
        {
            // Custom effects number their inputs in declaration order across the bound streams, in
            // slot order; a stock program's locations are fixed by the shader it was generated for,
            // so only the custom path advances this.
            int nextCustomLocation = 0;

            if (custom)
            {
                for (int i = 0; i < params.vertexStreamCount; ++i)
                {
                    const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                    const auto* streamBuffer =
                        dynamic_cast<const MagnumVertexBufferRenderer*>(stream.buffer);
                    if (streamBuffer == nullptr || stream.instanceFrequency > 0)
                        continue;
                    const int streamStride = strideOf(stream, *streamBuffer);
                    if (streamStride <= 0)
                        continue;

                    const std::vector<MagnumVertexAttribute> attributes =
                        streamBuffer->GetDeclarationElements().empty()
                            ? StockAttributesForStride(static_cast<std::size_t>(streamStride))
                            : AttributesForDeclaration(streamBuffer->GetDeclarationElements(),
                                                       nextCustomLocation, stream.combinedByteBase);
                    nextCustomLocation += static_cast<int>(attributes.size());
                    for (const MagnumVertexAttribute& attribute : attributes)
                    {
                        bindAttribute(*streamBuffer, attribute, streamStride,
                                      stream.vertexOffset, 0);
                    }
                }
            }
            else
            {
                // A stock program reads its inputs from fixed locations describing the COMBINED
                // vertex, so the layout is resolved once from the combined stride and each element
                // is then re-slotted into whichever stream actually holds those bytes. Deriving the
                // locations per stream instead is what makes a split declaration render from a
                // subset of its streams: the shader's location 1 does not become the second stream's
                // first element just because that stream was bound second.
                for (const MagnumVertexAttribute& combined : StockAttributesForStride(stride))
                {
                    const GpuVertexStreamSlot slot =
                        MapCombinedOffsetToStream(params, combined.offsetInStream);
                    if (slot.streamIndex < 0 || slot.streamIndex >= params.vertexStreamCount)
                        continue;
                    const GpuVertexStreamBinding& stream = params.vertexStreams[slot.streamIndex];
                    const auto* streamBuffer =
                        dynamic_cast<const MagnumVertexBufferRenderer*>(stream.buffer);
                    if (streamBuffer == nullptr)
                        continue;
                    const int streamStride = strideOf(stream, *streamBuffer);
                    if (streamStride <= 0)
                        continue;

                    MagnumVertexAttribute attribute = combined;
                    attribute.offsetInStream = slot.byteOffsetInStream;
                    bindAttribute(*streamBuffer, attribute, streamStride,
                                  stream.vertexOffset, 0);
                }
            }

            // Per-instance streams, in slot order. A stock program reads its optional per-instance
            // world matrix from the four reserved locations; a custom one continues numbering where
            // the per-vertex streams left off, matching how it declared its own inputs.
            //
            // The location counter runs ACROSS the instance streams rather than restarting at each
            // one: XNA lets a single per-instance matrix be split over several bindings (three
            // columns in one stream, the fourth in another), and each stream contributes the next
            // columns of that one matrix, not its own copy of column 0.
            int nextInstanceLocation = kInstanceMatrixBaseLocation;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                if (stream.instanceFrequency <= 0)
                    continue;
                const auto* streamBuffer =
                    dynamic_cast<const MagnumVertexBufferRenderer*>(stream.buffer);
                if (streamBuffer == nullptr)
                    continue;
                const int streamStride = strideOf(stream, *streamBuffer);
                if (streamStride <= 0)
                    continue;

                const int baseLocation = custom ? nextCustomLocation : nextInstanceLocation;
                std::vector<MagnumVertexAttribute> attributes;
                if (!streamBuffer->GetDeclarationElements().empty())
                {
                    // A per-instance stream's declaration offsets are already stream-local: it
                    // contributes nothing to the combined per-vertex layout, so there is no base to
                    // subtract here.
                    attributes = AttributesForDeclaration(streamBuffer->GetDeclarationElements(),
                                                          baseLocation, 0);
                }
                else
                {
                    // No declaration means the stock per-instance world matrix: four consecutive
                    // float4 columns.
                    for (int column = 0; column < 4; ++column)
                    {
                        MagnumVertexAttribute attribute;
                        attribute.location = baseLocation + column;
                        attribute.offsetInStream = column * 16;
                        attribute.components = 4;
                        attribute.format = 3;  // Vector4
                        attributes.push_back(attribute);
                    }
                }
                nextCustomLocation += static_cast<int>(attributes.size());
                nextInstanceLocation += static_cast<int>(attributes.size());

                // A per-instance slot is addressed by instance index, so the draw's base-vertex
                // term must not advance it -- only its own binding offset does.
                for (const MagnumVertexAttribute& attribute : attributes)
                {
                    bindAttribute(*streamBuffer, attribute, streamStride,
                                  stream.vertexOffset, stream.instanceFrequency);
                }
            }
        }
        else
        {
            // The internal routes (DrawUser*, DrawColoredPrimitives) bind no public
            // VertexBufferBinding at all and stage one buffer of their own.
            std::vector<MagnumVertexAttribute> attributes =
                vertexBuffer->GetDeclarationElements().empty()
                    ? StockAttributesForStride(stride)
                    : AttributesForDeclaration(vertexBuffer->GetDeclarationElements(), 0, 0);
            for (const MagnumVertexAttribute& attribute : attributes)
            {
                mesh.addVertexBuffer(vertexBuffer->GetBuffer(), attribute.offsetInStream,
                                     static_cast<GLsizei>(stride), ToDynamicAttribute(attribute));
            }
        }

        if (indexBuffer != nullptr)
        {
            // Bound at offset 0 and then moved per draw through setIndexOffset, so a sub-range
            // draw reuses the same vertex array rather than keying a new one per start index.
            mesh.setIndexBuffer(indexBuffer->GetBuffer(), 0,
                                indexBuffer->IsThirtyTwoBit() ? Mg::GL::MeshIndexType::UnsignedInt
                                                              : Mg::GL::MeshIndexType::UnsignedShort);
        }

        finishDraw(vertexBuffer->StoreCachedMesh(std::move(cacheKey), std::move(owned)));
    }

    void MagnumRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                      const Matrix& world, const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount)
    {
        GpuDrawParams params;
        params.vertexColorEnabled = true;
        DrawGeneric(vb, nullptr, world, view, projection, primitive, primitiveCount, 1, params);
    }

    void MagnumRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                             const IIndexBufferRenderer& ib,
                                                             const Matrix& world, const Matrix& view,
                                                             const Matrix& projection,
                                                             PrimitiveType primitive,
                                                             int primitiveCount)
    {
        GpuDrawParams params;
        params.vertexColorEnabled = true;
        DrawGeneric(vb, &ib, world, view, projection, primitive, primitiveCount, 1, params);
    }

    void MagnumRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                 const Matrix& world, const Matrix& view,
                                                 const Matrix& projection,
                                                 PrimitiveType primitive, int primitiveCount,
                                                 const GpuDrawParams& params)
    {
        DrawGeneric(vb, nullptr, world, view, projection, primitive, primitiveCount, 1, params);
    }

    void MagnumRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                        const IIndexBufferRenderer& ib,
                                                        const Matrix& world, const Matrix& view,
                                                        const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount,
                                                        const GpuDrawParams& params)
    {
        DrawGeneric(vb, &ib, world, view, projection, primitive, primitiveCount, 1, params);
    }

    void MagnumRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                          const IIndexBufferRenderer& ib,
                                                          const Matrix& world, const Matrix& view,
                                                          const Matrix& projection,
                                                          PrimitiveType primitive,
                                                          int primitiveCount, int instanceCount,
                                                          const GpuDrawParams& params)
    {
        DrawGeneric(vb, &ib, world, view, projection, primitive, primitiveCount,
                    std::max(1, instanceCount), params);
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_MAGNUM
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Magnum::MagnumRenderer>(args);
    }
#endif
}
