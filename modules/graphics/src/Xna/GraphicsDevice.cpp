// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
    using CNA::Internal::Renderers::RenderTargetBindingDescriptor;

    namespace
    {
        // Matches FNA's internal GraphicsDevice.MAX_RENDERTARGET_BINDINGS.
        constexpr std::size_t MAX_RENDERTARGET_BINDINGS = 4;

        int toSwapInterval(PresentInterval pi)
        {
            switch (pi)
            {
                case PresentInterval::Immediate: return 0;
                case PresentInterval::Two:       return 2;
                default:                         return 1; // Default and One
            }
        }

        void NormalizeAppliedPresentationFormats(
            CNA::Internal::Renderers::IGraphicsRenderer& renderer,
            PresentationParameters& parameters)
        {
            parameters.setBackBufferFormatProperty(static_cast<SurfaceFormat>(
                renderer.GetAppliedBackBufferFormatEXT(
                    static_cast<int>(parameters.getBackBufferFormatProperty()))));
            parameters.setDepthStencilFormatProperty(static_cast<DepthFormat>(
                renderer.GetAppliedDepthStencilFormatEXT(
                    static_cast<int>(parameters.getDepthStencilFormatProperty()))));
        }

        [[nodiscard]] bool hasClearFlag(ClearOptions options, ClearOptions flag)
        {
            return (static_cast<int>(options) & static_cast<int>(flag)) != 0;
        }

        // MERGE (plan_platform.md PLAT-8): LogWindowDebugState() lived here on this branch and
        // printed a window's raw flag bitmask after creation. It was a debug aid for the
        // window-kind work, its one call site was inside the creation path that now goes through
        // the platform contract, and it cannot be expressed without reaching past that contract to
        // the windowing library. next does not have it. Dropped rather than smuggled through.

        /// plan_runtimerenderer.md RTR-P8-5: a documented debug facility for verifying that a
        /// configured fallback chain actually works, without having to break a driver to find out.
        ///
        /// CNA_DEBUG_UNAVAILABLE_RENDERERS is a comma-separated list of renderer names to treat as
        /// though their availability probe had failed. It sits alongside the renderer-specific
        /// debug variables this project already has (CNA_BGFX_TRACE_*, CNA_LLGL_DEBUG) and the
        /// existing DebugSimulateContextLoss() channel: a deliberate, named test seam rather than
        /// something the resolution path does on its own.
        /// Shared by the two debug seams below: is rendererName listed in this variable?
        [[nodiscard]] bool isRendererListedIn(const char* variable, std::string_view rendererName)
        {
            const char* raw = std::getenv(variable);
            if (raw == nullptr || *raw == '\0')
                return false;

            std::string_view remaining(raw);
            while (!remaining.empty())
            {
                const std::size_t comma = remaining.find(',');
                std::string_view entry = remaining.substr(0, comma);
                remaining = comma == std::string_view::npos ? std::string_view()
                                                            : remaining.substr(comma + 1);

                while (!entry.empty() && entry.front() == ' ')
                    entry.remove_prefix(1);
                while (!entry.empty() && entry.back() == ' ')
                    entry.remove_suffix(1);

                if (entry.size() == rendererName.size()
                    && std::equal(entry.begin(), entry.end(), rendererName.begin(),
                                  [](char a, char b) {
                                      return std::toupper(static_cast<unsigned char>(a))
                                          == std::toupper(static_cast<unsigned char>(b));
                                  }))
                {
                    return true;
                }
            }
            return false;
        }

        // MERGE (plan_runtimerenderer.md P1/P3 x plan_platform.md PLAT-*): `next` added
        // RendererWindowRequirements and filled it with one `#ifdef CNA_RENDERER_<X>` per family --
        // the pattern P1/P3 removed from this file and that
        // scripts/check_runtime_renderer_discipline.py pins as absent. Neither design is discarded:
        // the requirement is derived from the ACTIVE renderer's descriptor instead.
        // RendererWindowKind and WindowRenderIntent are the same enumeration under two names, so the
        // mapping is total and adding a renderer needs no edit here -- the point of the descriptor.
        [[nodiscard]] CNA::Platform::WindowRenderIntent renderIntentFor(
            CNA::Internal::Renderers::RendererWindowKind kind)
        {
            using CNA::Internal::Renderers::RendererWindowKind;
            switch (kind)
            {
            case RendererWindowKind::OpenGL: return CNA::Platform::WindowRenderIntent::OpenGl;
            case RendererWindowKind::Vulkan: return CNA::Platform::WindowRenderIntent::Vulkan;
            case RendererWindowKind::Metal:  return CNA::Platform::WindowRenderIntent::Metal;
            case RendererWindowKind::Plain:
            case RendererWindowKind::None:
            default:                         return CNA::Platform::WindowRenderIntent::None;
            }
        }

        /// CNA_DEBUG_UNAVAILABLE_RENDERERS: treat these renderers' availability probe as failing.
        [[nodiscard]] bool isDebugForcedUnavailable(std::string_view rendererName)
        {
            return isRendererListedIn("CNA_DEBUG_UNAVAILABLE_RENDERERS", rendererName);
        }

        /// CNA_DEBUG_FAIL_RENDERER_INIT: treat these renderers as failing during INITIALIZATION,
        /// which is a materially different path from failing the probe -- by then the window
        /// already exists, so a candidate needing a different window kind forces CNA to destroy and
        /// recreate it (plan_runtimerenderer.md design decision 8). That transition is otherwise
        /// unreachable without a genuinely broken driver.
        [[nodiscard]] bool isDebugForcedInitFailure(std::string_view rendererName)
        {
            return isRendererListedIn("CNA_DEBUG_FAIL_RENDERER_INIT", rendererName);
        }

        /// plan_runtimerenderer.md RTR-P4: the single point where the runtime selection meets the
        /// compiled-in set. The generated registry translation unit publishes that set into the
        /// selection layer before main() runs, so by the time anything here asks, the answer is real.
        [[nodiscard]] const CNA::Internal::Renderers::GraphicsRendererDescriptor& selectedDescriptor()
        {
            namespace Renderers = CNA::Internal::Renderers;


            const CNA::GraphicsRendererType selected = CNA::GraphicsRendererSelection::GetSelected();
            const auto* descriptor = Renderers::GraphicsRendererRegistry::Find(selected);

            // Unreachable through the public API -- SetPreferred() refuses an identity that is not
            // compiled in, and the default always is. Falling back to the build default rather than
            // dereferencing null keeps a future caller honest instead of crashing.
            return descriptor != nullptr ? *descriptor
                                         : Renderers::GraphicsRendererRegistry::Default();
        }

    }

    GraphicsDevice::GraphicsDevice()
        : GraphicsDevice(
            GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::Reach,
            PresentationParameters()
        )
    {
    }

    GraphicsDevice::GraphicsDevice(
        GraphicsAdapter& adapter,
        GraphicsProfile graphicsProfile,
        const PresentationParameters& presentationParameters
    )
        : platform_(&CNA::Platform::GetCurrentPlatform()),
          platformWindow_(nullptr),
          videoSubsystemAcquired_(false),
          surfacePresenter_(nullptr),
          renderer_(nullptr),
          viewport_(),
          currentVertexBuffer_(nullptr),
          currentIndexBuffer_(nullptr),
          currentEffect_(nullptr),
          virtualWidth_(presentationParameters.getBackBufferWidthProperty()),
          virtualHeight_(presentationParameters.getBackBufferHeightProperty()),
          adapter_(&adapter),
          graphicsProfile_(graphicsProfile),
          presentationParameters_(presentationParameters),
          isDisposed_(false),
          blendState_(BlendState::Opaque),
          depthStencilState_(DepthStencilState::Default),
          rasterizerState_(RasterizerState::CullCounterClockwise),
          blendFactor_(Color::White),
          textures_(this),
          vertexTextures_(this)
    {
        // The Touch Panel needs this for normalized-to-pixel touch coordinate scaling.
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayWidthProperty(virtualWidth_);
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayHeightProperty(virtualHeight_);

        // MERGE: `next` wrapped construction in a try whose catch (below) tears down native
        // resources and releases the platform video subsystem -- that cleanup is kept. Inside it,
        // resolveRenderer() replaces next's fixed createOrAttachWindow/applyPresentationParameters/
        // createRenderer sequence: it performs exactly those steps and adds the fallback chain, so
        // running both would create the window twice.
        try
        {
            resolveRenderer();
            UpdateViewportFromWindow();

            // Task 896/955: blendState_/depthStencilState_/rasterizerState_ above were only ever
            // set as C++-level fields, never pushed to the renderer's actual GPU state — every
            // renderer started from its own hardcoded internal default (e.g. EasyGL's depth test is
            // plain OpenGL, which defaults to disabled, until something explicitly enables it) until
            // a game explicitly set one of these 3 state properties itself. Real FNA's own
            // GraphicsDevice constructor does exactly this same 3-line sync unconditionally
            // (GraphicsDevice.cs: "BlendState = BlendState.Opaque; DepthStencilState =
            // DepthStencilState.Default; RasterizerState = RasterizerState.CullCounterClockwise;") —
            // Task 896 ported only the 3rd line; this now ports the other 2 as well, matching FNA.
            setBlendStateProperty(blendState_);
            // A 2D-only renderer has no native depth/stencil state to initialize. Skipping this one
            // constructor-time synchronization lets such a renderer reject every later public state
            // assignment consistently, instead of needing a special first-call exception.
            if (renderer_->SupportsDepthStencil())
                setDepthStencilStateProperty(depthStencilState_);
            setRasterizerStateProperty(rasterizerState_);
        }
        catch (...)
        {
            destroyNativeResources();
            if (videoSubsystemAcquired_)
            {
                platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
                videoSubsystemAcquired_ = false;
            }
            throw;
        }
    }

    GraphicsDevice::~GraphicsDevice()
    {
        Dispose();
    }

    GraphicsAdapter& GraphicsDevice::getAdapterProperty() const
    {
        return adapter_ != nullptr ? *adapter_ : GraphicsAdapter::getDefaultAdapterProperty();
    }

    GraphicsProfile GraphicsDevice::getGraphicsProfileProperty() const
    {
        return graphicsProfile_;
    }

    PresentationParameters& GraphicsDevice::getPresentationParametersProperty()
    {
        return presentationParameters_;
    }

    const PresentationParameters& GraphicsDevice::getPresentationParametersProperty() const
    {
        return presentationParameters_;
    }

    const Viewport& GraphicsDevice::getViewportProperty() const
    {
        return viewport_;
    }

    void GraphicsDevice::setViewportProperty(const Viewport& value)
    {
        if (renderer_)
            renderer_->SetViewport(value.getXProperty(), value.getYProperty(),
                                   value.getWidthProperty(), value.getHeightProperty(),
                                   value.getMinDepthProperty(), value.getMaxDepthProperty());
        // Commit the public cache only after the native/renderer operation succeeds.  In particular,
        // D3D9's checked SetViewport path may reject a bad/lost device; retaining the old viewport
        // prevents CNA from claiming the failed dimensions are active.
        viewport_ = value;
    }

    const IndexBuffer* GraphicsDevice::getIndicesProperty() const
    {
        return currentIndexBuffer_;
    }

    void GraphicsDevice::setIndicesProperty(const IndexBuffer* indexBuffer)
    {
        SetIndexBuffer(indexBuffer);
    }

    bool GraphicsDevice::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    void GraphicsDevice::Clear(const Color& color)
    {
        // Task 928: real XNA/FNA's single-argument overload clears the target, depth buffer,
        // AND stencil together -- Clear(ClearOptions.Target | ClearOptions.DepthBuffer |
        // ClearOptions.Stencil, color, Viewport.MaxDepth, 0) -- not just the color target. The
        // depth value used is the device's own CURRENT viewport's MaxDepth (not a hardcoded 1.0),
        // matching FNA's exact `Viewport.MaxDepth` reference (a GraphicsDevice property, not a
        // static constant).
        Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
              color, getViewportProperty().getMaxDepthProperty(), 0);
    }

    void GraphicsDevice::Clear(float r, float g, float b, float a)
    {
        if (renderer_ != nullptr)
        {
            renderer_->Clear(r, g, b, a);
        }
    }

    void GraphicsDevice::Clear(ClearOptions options, const Color& color, float depth, int stencil)
    {
        if (renderer_ == nullptr)
        {
            return;
        }

        if (hasClearFlag(options, ClearOptions::DepthBuffer))
        {
            if (depth < 0.0f || depth > 1.0f)
                throw System::ArgumentOutOfRangeException(
                    "depth", std::to_string(depth),
                    "'depth' must be between 0.0 and 1.0.");
        }

        // GDI-050: depth and stencil are independent attachment decisions. Historically this used
        // one SupportsDepthStencil()/HasRealDepthBuffer() answer and reduced a target with no depth
        // to ClearOptions::Target, silently deleting Stencil too. That made GDI's real standalone
        // CPU stencil plane unreachable through the public GraphicsDevice API. Ask for each aspect
        // independently and mask only the unsupported flag. Combined depth/stencil renderers retain
        // their prior behavior through the interface's compatibility defaults.
        bool hasRealDepthBuffer = false;
        bool hasRealStencilBuffer = false;
        if (!currentRenderTargets_.empty())
        {
            // REMED-GFX-142: a bound RenderTargetCube has to be asked too. This branch only ever
            // recognized RenderTarget2D, so a cube binding fell through to `rt == nullptr` and
            // both attachment flags were silently dropped, on every renderer, whatever depth
            // format the cube actually had. SetRenderTargets already asks both target kinds (see
            // its own `IsRenderTarget2D()` branch); this is the same question at the other call
            // site.
            Texture* bound = currentRenderTargets_[0].getRenderTargetProperty();
            const auto* rt = dynamic_cast<RenderTarget2D*>(bound);
            const auto* cube = (rt == nullptr) ? dynamic_cast<RenderTargetCube*>(bound) : nullptr;
            if (cube != nullptr)
            {
                const DepthFormat depthFormat = cube->getDepthStencilFormatProperty();
                const bool depthFormatRequested = depthFormat != DepthFormat::None;
                const bool stencilFormatRequested = depthFormat == DepthFormat::Depth24Stencil8;
                const auto* cubeRenderer = cube->GetRenderTargetCubeRenderer();
                hasRealDepthBuffer =
                    cubeRenderer && cubeRenderer->HasRealDepthBuffer(depthFormatRequested);
                hasRealStencilBuffer =
                    cubeRenderer && cubeRenderer->HasRealStencilBuffer(stencilFormatRequested);
            }
            else
            {
                const DepthFormat depthFormat =
                    rt ? rt->getDepthStencilFormatProperty() : DepthFormat::None;
                const bool depthFormatRequested = depthFormat != DepthFormat::None;
                const bool stencilFormatRequested = depthFormat == DepthFormat::Depth24Stencil8;
                const auto* rtRenderer = rt ? rt->GetRenderTargetRenderer() : nullptr;
                hasRealDepthBuffer = rtRenderer && rtRenderer->HasRealDepthBuffer(depthFormatRequested);
                hasRealStencilBuffer =
                    rtRenderer && rtRenderer->HasRealStencilBuffer(stencilFormatRequested);
            }
        }
        else
        {
            hasRealDepthBuffer = renderer_->SupportsDepthBuffer();
            hasRealStencilBuffer = renderer_->SupportsStencilBuffer();
        }
        if (!hasRealDepthBuffer)
        {
            options &= ~ClearOptions::DepthBuffer;
        }
        if (!hasRealStencilBuffer)
        {
            options &= ~ClearOptions::Stencil;
        }

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const bool clearTarget  = hasClearFlag(options, ClearOptions::Target);
        const bool clearDepth   = hasClearFlag(options, ClearOptions::DepthBuffer);
        // Task 871: ClearOptions::Stencil was previously entirely ignored here -- neither checked
        // against `options` nor threaded through to any renderer, so a requested stencil clear
        // silently did nothing on every renderer.
        const bool clearStencil = hasClearFlag(options, ClearOptions::Stencil);

        if (clearTarget && clearDepth && clearStencil)
        {
            renderer_->ClearColorDepthAndStencil(r, g, b, a, depth, stencil);
        }
        else if (clearTarget && clearDepth)
        {
            renderer_->ClearColorAndDepth(r, g, b, a, depth);
        }
        else if (clearTarget && clearStencil)
        {
            renderer_->ClearColorAndStencil(r, g, b, a, stencil);
        }
        else if (clearDepth && clearStencil)
        {
            renderer_->ClearDepthAndStencil(depth, stencil);
        }
        else if (clearTarget)
        {
            renderer_->Clear(r, g, b, a);
        }
        else if (clearDepth)
        {
            renderer_->ClearDepth(depth);
        }
        else if (clearStencil)
        {
            renderer_->ClearStencil(stencil);
        }
    }

    void GraphicsDevice::Clear(const Color& color, float depth)
    {
        Clear(ClearOptions::Target | ClearOptions::DepthBuffer, color, depth, 0);
    }

    void GraphicsDevice::Present()
    {
        if (renderTargetBound_)
            throw System::InvalidOperationException("Cannot present while render targets are bound");

        if (renderer_ != nullptr)
        {
            renderer_->Present();
            UpdateViewportFromWindow();
        }
    }

    void GraphicsDevice::Reset(const PresentationParameters& presentationParameters, GraphicsAdapter& adapter)
    {
        Reset(presentationParameters, &adapter);
    }

    void GraphicsDevice::Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter)
    {
        const PresentationParameters previousPresentationParameters =
            presentationParameters_.Clone();
        GraphicsAdapter* const previousAdapter = adapter_;
        const int previousVirtualWidth = virtualWidth_;
        const int previousVirtualHeight = virtualHeight_;

        DeviceResetting.Raise(this, System::EventArgs::Empty);

        PresentationParameters appliedPresentationParameters = presentationParameters.Clone();
        if (renderer_ != nullptr)
            NormalizeAppliedPresentationFormats(*renderer_, appliedPresentationParameters);
        presentationParameters_ = appliedPresentationParameters;
        if (adapter != nullptr)
        {
            adapter_ = adapter;
        }

        virtualWidth_ = presentationParameters_.getBackBufferWidthProperty();
        virtualHeight_ = presentationParameters_.getBackBufferHeightProperty();

        // The Touch Panel needs this too, for the same reason as the constructor.
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayWidthProperty(virtualWidth_);
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayHeightProperty(virtualHeight_);

        try
        {
            applyPresentationParametersToWindow();

            if (renderer_ != nullptr)
                renderer_->SetVirtualResolution(virtualWidth_, virtualHeight_);
        }
        catch (...)
        {
            // REMED-GFX-029: a renderer resize is a failed Reset, not a partially successful one.
            // Restore all public presentation bookkeeping before rethrowing the original native
            // diagnostic. Viewport/scissor, active target, and depth state have not been touched
            // yet at this point. The DIRECTX3 transaction guarantees its native A surface set is still
            // live, so restoring the former window/presentation values makes the entire
            // GraphicsDevice state agree with that same A set.
            const std::exception_ptr resizeFailure = std::current_exception();
            presentationParameters_ = previousPresentationParameters;
            adapter_ = previousAdapter;
            virtualWidth_ = previousVirtualWidth;
            virtualHeight_ = previousVirtualHeight;
            Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayWidthProperty(
                previousVirtualWidth);
            Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayHeightProperty(
                previousVirtualHeight);
            try
            {
                applyPresentationParametersToWindow();
            }
            catch (...)
            {
                // Do not replace the precise resize-stage failure with a secondary window
                // rollback diagnostic. The stored presentation state is already restored.
            }
            std::rethrow_exception(resizeFailure);
        }

        if (renderer_ != nullptr)
        {
            // Task 902: reconfigure the renderer's actual MSAA sample count in place, mirroring
            // FNA's own PresentationParameters.MultiSampleCount = FNA3D_GetMaxMultiSampleCount(...)
            // write-back of the real, device-clamped value after FNA3D_ResetBackbuffer().
            const int appliedMultiSampleCount = renderer_->ApplyMultiSampleCount(
                presentationParameters_.getMultiSampleCountProperty());
            presentationParameters_.setMultiSampleCountProperty(appliedMultiSampleCount);

            // Previously missing: this Reset() overload never forwarded PresentationInterval to
            // the renderer, unlike SetPresentationParameters()'s own identical field -- meaning
            // GraphicsDeviceManager.SynchronizeWithVerticalRetrace/ApplyChanges() (which always
            // goes through this path, not SetPresentationParameters()) never actually reached
            // IGraphicsRenderer::SetSwapInterval() on any renderer. Matches SetPresentationParameters()'s
            // own forwarding exactly.
            renderer_->SetSwapInterval(toSwapInterval(presentationParameters_.getPresentationIntervalProperty()));

            // plan_dx9.md D9-30/D9-33: same "actually reach the renderer" rationale as
            // ApplyMultiSampleCount above, for back-buffer/depth-stencil format and fullscreen --
            // needed because Game commonly constructs this GraphicsDevice (and its renderer) with
            // default PresentationParameters before GraphicsDeviceManager.ApplyChanges() ever runs.
            renderer_->UpdatePresentationFormatEXT(
                static_cast<int>(presentationParameters_.getBackBufferFormatProperty()),
                static_cast<int>(presentationParameters_.getDepthStencilFormatProperty()),
                presentationParameters_.getIsFullScreenProperty());
        }

        UpdateViewportFromWindow();
        DeviceReset.Raise(this, System::EventArgs::Empty);
    }

    void GraphicsDevice::Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        // Copy and clear the resource list before iterating.
        // This makes RemoveResourceReference a no-op when called re-entrantly
        // from within the resources' own Dispose() methods (matches FNA pattern).
        std::vector<GraphicsResource*> toDispose = std::move(resources_);
        resources_.clear();

        for (GraphicsResource* res : toDispose)
            static_cast<System::IDisposable*>(res)->Dispose();

        Disposing.Raise(this, System::EventArgs::Empty);
        destroyNativeResources();
        if (videoSubsystemAcquired_)
        {
            platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
            videoSubsystemAcquired_ = false;
        }
        isDisposed_ = true;
    }

    void GraphicsDevice::OnResourceCreated(System::Object* resource)
    {
        if (!ResourceCreated.Empty())
            ResourceCreated.Raise(this, ResourceCreatedEventArgs(resource));
    }

    void GraphicsDevice::OnResourceDestroyed(const std::string& name, System::Object* tag)
    {
        if (!ResourceDestroyed.Empty())
            ResourceDestroyed.Raise(this, ResourceDestroyedEventArgs(name, tag));
    }

    void GraphicsDevice::AddResourceReference(GraphicsResource* resource)
    {
        resources_.push_back(resource);
    }

    void GraphicsDevice::RemoveResourceReference(GraphicsResource* resource)
    {
        for (std::size_t i = 0; i < resources_.size(); ++i)
        {
            if (resources_[i] == resource)
            {
                // Unordered removal — list order does not matter
                resources_[i] = resources_.back();
                resources_.pop_back();
                return;
            }
        }
    }

    void GraphicsDevice::SetDepthTestEnabled(bool enabled)
    {
        if (renderer_ != nullptr) renderer_->SetDepthTestEnabled(enabled);
    }

    void GraphicsDevice::SetBlendEnabled(bool enabled)
    {
        if (renderer_ != nullptr) renderer_->SetBlendEnabled(enabled);
    }

    void GraphicsDevice::SetDepthWriteEnabled(bool enabled)
    {
        if (renderer_ != nullptr) renderer_->SetDepthWriteEnabled(enabled);
    }

    void GraphicsDevice::SetGraphicsProfileEXT(GraphicsProfile profile)
    {
        graphicsProfile_ = profile;
    }

    void GraphicsDevice::SetVertexBuffer(const VertexBuffer* vertexBuffer)
    {
        SetVertexBuffer(vertexBuffer, 0);
    }

    void GraphicsDevice::SetIndexBuffer(const IndexBuffer* indexBuffer)
    {
        if (indexBuffer && indexBuffer->getIsDisposedProperty())
            throw System::ObjectDisposedException(indexBuffer->getNameProperty());
        currentIndexBuffer_ = indexBuffer;
    }

    const VertexBuffer* GraphicsDevice::GetVertexBuffer() const
    {
        return currentVertexBuffer_;
    }

    const IndexBuffer* GraphicsDevice::GetIndexBuffer() const
    {
        return currentIndexBuffer_;
    }

    const IndexBuffer* GraphicsDevice::Indices() const
    {
        return currentIndexBuffer_;
    }

    void GraphicsDevice::Indices(const IndexBuffer* indexBuffer)
    {
        SetIndexBuffer(indexBuffer);
    }

    namespace
    {
        void ExtractMatrices(const Effect* effect, Matrix& world, Matrix& view, Matrix& proj)
        {
            world = Matrix::getIdentityProperty();
            view  = Matrix::getIdentityProperty();
            proj  = Matrix::getIdentityProperty();
            if (const auto* m = dynamic_cast<const IEffectMatrices*>(effect))
            {
                world = m->getWorldProperty();
                view  = m->getViewProperty();
                proj  = m->getProjectionProperty();
            }
        }

        /// Elements a draw consumes for `primitiveCount` primitives of `primitiveType`, computed in
        /// 64-bit so an overflowing request is rejected instead of wrapping. Index elements for the
        /// indexed entry points, vertex elements for the non-indexed ones — the XNA formulas are
        /// the same for both.
        int CheckedPrimitiveElementCount(PrimitiveType primitiveType, int primitiveCount)
        {
            std::int64_t count = 0;
            switch (primitiveType)
            {
            case PrimitiveType::TriangleList:
                count = static_cast<std::int64_t>(primitiveCount) * 3;
                break;
            case PrimitiveType::TriangleStrip:
                count = static_cast<std::int64_t>(primitiveCount) + 2;
                break;
            case PrimitiveType::LineList:
                count = static_cast<std::int64_t>(primitiveCount) * 2;
                break;
            case PrimitiveType::LineStrip:
                count = static_cast<std::int64_t>(primitiveCount) + 1;
                break;
            case PrimitiveType::PointListEXT:
                count = primitiveCount;
                break;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
            if (count > std::numeric_limits<int>::max())
            {
                throw System::ArgumentOutOfRangeException(
                    "primitiveCount", std::to_string(primitiveCount),
                    "The requested primitive range is too large.");
            }
            return static_cast<int>(count);
        }
    }

    int GraphicsDevice::CurrentVertexBufferOffset() const
    {
        if (currentVertexBuffers_.empty() ||
            currentVertexBuffers_[0].getVertexBufferProperty() != currentVertexBuffer_)
        {
            return 0;
        }
        return currentVertexBuffers_[0].getVertexOffsetProperty();
    }

    int GraphicsDevice::FoldedVertexStreamOffset() const
    {
        if (currentVertexBuffers_.empty() ||
            currentVertexBuffers_[0].getVertexBufferProperty() != currentVertexBuffer_)
        {
            // No binding describes the bound buffer: the pre-VertexBufferBinding contract, where
            // the stream starts at element zero and nothing is folded.
            return 0;
        }
        int folded = std::numeric_limits<int>::max();
        for (const auto& binding : currentVertexBuffers_)
        {
            if (binding.getInstanceFrequencyProperty() != 0)
                continue;   // per-instance streams advance with instanceVertexOffset, not baseVertex
            if (binding.getVertexBufferProperty() == nullptr)
                continue;
            folded = std::min(folded, binding.getVertexOffsetProperty());
        }
        return folded == std::numeric_limits<int>::max() ? 0 : folded;
    }

    void GraphicsDevice::FillVertexStreamBindings(
        CNA::Internal::Renderers::GpuDrawParams& p, int foldedOffset,
        bool allowLegacyEmptyDeclarationFallback) const
    {
        p.vertexStreamCount = 0;
        p.combinedVertexStride = 0;

        // REMED-GFX-233: the CNAEXT VertexBuffer(device, count) convenience constructor has an
        // intentionally empty declaration. Its typed SetData overload still uploads a real,
        // renderer-known packed stride, but there is no declared stride to put in a stream tuple.
        // Describing that one legacy buffer as a zero-stride stream makes a stream-aware CPU
        // reader fetch record zero for every vertex, producing a degenerate triangle. Keep this
        // exact single, per-vertex, empty-declaration shape on the pre-REMED-GFX-201 contract: the
        // Draw*PrimitivesEx `vb` argument is authoritative and vertexStreamCount==0 selects its
        // renderer upload stride. FoldedVertexStreamOffset() has already carried any binding offset
        // through vertexStart/baseVertex. Nonzero declarations and every multi-stream shape keep
        // the immutable stream-array path below unchanged.
        if (allowLegacyEmptyDeclarationFallback && currentVertexBuffers_.size() == 1)
        {
            const VertexBufferBinding& legacyBinding = currentVertexBuffers_[0];
            const VertexBuffer* legacyBuffer = legacyBinding.getVertexBufferProperty();
            if (legacyBuffer != nullptr && legacyBuffer == currentVertexBuffer_ &&
                legacyBinding.getInstanceFrequencyProperty() == 0 &&
                legacyBuffer->getVertexDeclarationProperty().GetVertexElements().empty() &&
                legacyBuffer->getVertexDeclarationProperty().getVertexStrideProperty() == 0)
            {
                return;
            }
        }

        if (currentVertexBuffers_.empty() ||
            currentVertexBuffers_[0].getVertexBufferProperty() != currentVertexBuffer_)
        {
            // Same pre-VertexBufferBinding contract as FoldedVertexStreamOffset(): describe the
            // one bound buffer so a renderer never has to fall back to reading public state.
            if (currentVertexBuffer_ == nullptr)
                return;
            auto& only = p.vertexStreams[0];
            only.slot = 0;
            only.buffer = &currentVertexBuffer_->GetRenderer();
            only.strideInBytes =
                currentVertexBuffer_->getVertexDeclarationProperty().getVertexStrideProperty();
            only.combinedByteBase = 0;
            only.vertexOffset = 0;
            only.instanceFrequency = 0;
            only.vertexCount = currentVertexBuffer_->GetRenderer().GetVertexCount();
            p.vertexStreamCount = 1;
            p.combinedVertexStride = only.strideInBytes;
            return;
        }

        const int bindingCount = std::min<int>(
            static_cast<int>(currentVertexBuffers_.size()),
            CNA::Internal::Renderers::kMaxVertexStreams);
        int combinedByteBase = 0;
        // REMED-GFX-201: XNA composes the bound declarations by SEMANTIC, not by position. FNA3D's
        // drivers walk the bindings in slot order tracking every (usage, usageIndex) pair already
        // claimed; a later element that repeats a claimed pair is pushed to the next free usage
        // index, which no stock vertex shader has an input for, so it resolves to "Stream not in
        // use!" and is skipped. A second stream that simply repeats stream 0's declaration
        // therefore contributes nothing at all -- it is not a second half of the vertex.
        bool usageClaimed[static_cast<std::size_t>(VertexElementUsage::TessellateFactor) + 1u]
                         [16] = {};
        const auto claimUsage = [&](const VertexElement& element) -> bool {
            const auto usage = static_cast<std::size_t>(element.getVertexElementUsageProperty());
            const int index = element.getUsageIndexProperty();
            if (usage >= std::size(usageClaimed) || index < 0 || index >= 16)
                return false;
            if (usageClaimed[usage][static_cast<std::size_t>(index)])
                return false;
            usageClaimed[usage][static_cast<std::size_t>(index)] = true;
            return true;
        };

        for (int slot = 0; slot < bindingCount; ++slot)
        {
            const VertexBufferBinding& binding = currentVertexBuffers_[static_cast<std::size_t>(slot)];
            const VertexBuffer* buffer = binding.getVertexBufferProperty();
            if (buffer == nullptr)
                continue;   // SetVertexBuffers rejects nulls; a defaulted binding is simply unused

            // REMED-GFX-202: the usage claim covers per-instance streams too, exactly as FNA3D's
            // own drivers walk one `attrUse` table across every binding regardless of frequency.
            {
                const auto& elements = buffer->getVertexDeclarationProperty().GetVertexElements();
                int claimed = 0;
                for (const VertexElement& element : elements)
                    if (claimUsage(element)) ++claimed;
                if (!elements.empty() && claimed == 0)
                    continue;   // every element repeats an earlier stream's: not in use
                if (claimed != static_cast<int>(elements.size()))
                {
                    throw System::NotSupportedException(
                        "The VertexBuffer bound to slot " + std::to_string(slot) +
                        " repeats some but not all of an earlier binding's vertex element "
                        "usages. CNA describes a combined vertex layout by its byte stride, so a "
                        "partially-duplicated stream has no expressible layout.");
                }
            }

            auto& stream = p.vertexStreams[static_cast<std::size_t>(p.vertexStreamCount)];
            stream.slot = slot;
            stream.buffer = &buffer->GetRenderer();
            stream.strideInBytes =
                buffer->getVertexDeclarationProperty().getVertexStrideProperty();
            stream.instanceFrequency = binding.getInstanceFrequencyProperty();
            stream.vertexCount = buffer->GetRenderer().GetVertexCount();
            if (stream.instanceFrequency == 0)
            {
                stream.combinedByteBase = combinedByteBase;
                stream.vertexOffset = binding.getVertexOffsetProperty() - foldedOffset;
                combinedByteBase += stream.strideInBytes;
                p.combinedVertexStride += stream.strideInBytes;
            }
            else
            {
                // A per-instance stream contributes no bytes to the per-vertex layout and is not
                // advanced by baseVertex, so it keeps its whole public offset.
                stream.combinedByteBase = 0;
                stream.vertexOffset = binding.getVertexOffsetProperty();
            }
            ++p.vertexStreamCount;
        }
    }

    void GraphicsDevice::ValidateVertexStreamCapability(
        const CNA::Internal::Renderers::GpuDrawParams& p) const
    {
        const bool multipleVertexStreams = CNA::Internal::Renderers::HasMultipleVertexStreams(p);
        const bool multipleInstanceStreams =
            CNA::Internal::Renderers::HasMultipleInstanceStreams(p);
        if (!multipleVertexStreams && !multipleInstanceStreams)
            return;   // the classic shapes every renderer has always had

        const int perVertexStreams = CNA::Internal::Renderers::PerVertexStreamCount(p);
        const int instanceStreams = CNA::Internal::Renderers::InstanceStreamCount(p);

        if (!renderer_->SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput))
        {
            // REMED-GFX-202: one capability, one message, whichever rate is over-wide -- both are
            // the same native gap (the renderer derives its input elements from a single byte
            // stride and binds one per-instance buffer), and both would otherwise be rendered from
            // a subset of the bound streams.
            throw System::NotSupportedException(
                "This graphics renderer cannot bind more than one VertexBufferBinding of the same "
                "input rate (" + std::to_string(perVertexStreams) + " per-vertex and " +
                std::to_string(instanceStreams) + " per-instance streams were bound). Query "
                "GraphicsDevice::SupportsCapability(GraphicsCapability::MultiStreamVertexInput) "
                "before splitting a VertexDeclaration across streams or binding several "
                "per-instance streams.");
        }

        const int rendererMaximum = renderer_->GetMaxVertexStreams();
        if (perVertexStreams > rendererMaximum || instanceStreams > rendererMaximum)
        {
            throw System::NotSupportedException(
                "This graphics renderer supports at most " + std::to_string(rendererMaximum) +
                " vertex streams of one input rate, but " + std::to_string(perVertexStreams) +
                " per-vertex and " + std::to_string(instanceStreams) +
                " per-instance streams were bound. The binding list is never silently truncated.");
        }
    }

    void GraphicsDevice::ValidateInstanceStreamRanges(
        const CNA::Internal::Renderers::GpuDrawParams& p, int instanceCount) const
    {
        for (int i = 0; i < p.vertexStreamCount; ++i)
        {
            const auto& stream = p.vertexStreams[static_cast<std::size_t>(i)];
            if (stream.instanceFrequency <= 0)
                continue;
            // REMED-GFX-118's arithmetic, per stream: one record is consumed for each complete
            // frequency-sized group of instances, and the first instance starts at record
            // `VertexOffset` -- D3D11's StartInstanceLocation is 0 in FNA3D's own driver and
            // OpenGL has no start-instance term at all. Every term is an element count of THIS
            // stream, so a short stream is rejected even when another one is long enough.
            const int requiredElements = 1 + (instanceCount - 1) / stream.instanceFrequency;
            const int available = stream.vertexCount;
            if (stream.vertexOffset > available ||
                requiredElements > available - stream.vertexOffset)
            {
                throw System::ArgumentOutOfRangeException(
                    "instanceCount", std::to_string(instanceCount),
                    "The requested instance range exceeds the per-instance vertex buffer bound to "
                    "slot " + std::to_string(stream.slot) + '.');
            }
        }
    }

    void GraphicsDevice::ValidateVertexStreamRanges(
        const CNA::Internal::Renderers::GpuDrawParams& p,
        int startElement,
        int elementCount,
        const char* parameterName,
        const std::string& parameterValue) const
    {
        for (int i = 0; i < p.vertexStreamCount; ++i)
        {
            const auto& stream = p.vertexStreams[static_cast<std::size_t>(i)];
            if (stream.instanceFrequency != 0)
                continue;   // the instanced route validates its per-instance stream separately
            // Every term is an element count of THIS stream, so the arithmetic is done against
            // this stream's own capacity -- a short secondary stream is rejected even when
            // stream 0 could satisfy the same request.
            const int available = stream.vertexCount;
            if (stream.vertexOffset > available ||
                startElement > available - stream.vertexOffset ||
                elementCount > available - stream.vertexOffset - startElement)
            {
                throw System::ArgumentOutOfRangeException(
                    parameterName, parameterValue,
                    "The requested vertex range exceeds the vertex buffer bound to slot " +
                        std::to_string(stream.slot) + '.');
            }
        }
    }

    void GraphicsDevice::DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount)
    {
        if (renderer_ == nullptr)
            return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawPrimitives");

        if (currentVertexBuffer_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: no vertex buffer is bound.");

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        System::ArgumentOutOfRangeException::ThrowIfNegative(vertexStart, "vertexStart");

        // REMED-GFX-113: vertexStart is a vertex-element offset and primitiveCount fixes the exact
        // topology-derived vertex count, so the pair [vertexStart, vertexStart + consumed) must fit
        // the bound buffer. Rejecting here keeps every renderer's native binding an exact range: a
        // request that leaves the buffer is an error, never a silently clamped or widened draw.
        const int consumedVertexCount =
            CheckedPrimitiveElementCount(primitiveType, primitiveCount);
        const int availableVertexCount = currentVertexBuffer_->GetRenderer().GetVertexCount();
        // REMED-GFX-200: the binding offset moves the whole range, so it is part of what must fit.
        const int bindingVertexOffset = CurrentVertexBufferOffset();
        if (bindingVertexOffset > availableVertexCount ||
            vertexStart > availableVertexCount - bindingVertexOffset ||
            consumedVertexCount > availableVertexCount - bindingVertexOffset - vertexStart)
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "The requested primitive range exceeds the bound vertex buffer.");
        }

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams p;
        currentEffect_->FillGpuDrawParams(p);
        // REMED-GFX-200: VertexBufferBinding.VertexOffset shifts a stream's base, and vertexStart
        // indexes that stream -- both in vertex elements, against that stream's own declaration
        // stride. Their sum is the first record fetched, so carrying the shared part in vertexStart
        // delivers it through the element-unit channel every renderer already converts to bytes
        // exactly once with the stream's own stride, and applies each of the two exactly once.
        // p.vertexBufferOffset is deliberately left at 0 here: it is the instanced route's separate
        // per-stream channel (REMED-GFX-122/123), and populating it as well would double the offset.
        // REMED-GFX-201: with several streams bound, "the shared part" is the smallest of their
        // offsets and each stream carries the rest itself; with one it is the whole offset and the
        // remainder is 0, which is exactly what REMED-GFX-200 measured.
        const int foldedOffset = FoldedVertexStreamOffset();
        p.vertexStart = vertexStart + foldedOffset;
        FillVertexStreamBindings(
            p, foldedOffset, /*allowLegacyEmptyDeclarationFallback=*/true);
        // Argument validation first, capability second: an out-of-range request is wrong on every
        // renderer, so it must report the same public exception everywhere.
        ValidateVertexStreamRanges(
            p, p.vertexStart, consumedVertexCount,
            "primitiveCount", std::to_string(primitiveCount));
        ValidateVertexStreamCapability(p);
        applySamplerStatesToRenderer();
        renderer_->DrawPrimitivesEx(
            currentVertexBuffer_->GetRenderer(),
            world, view, proj,
            primitiveType, primitiveCount, p
        );
    }

    void GraphicsDevice::DrawIndexedPrimitives(
        PrimitiveType primitiveType,
        int baseVertex,
        int minVertexIndex,
        int numVertices,
        int startIndex,
        int primitiveCount
    )
    {
        if (renderer_ == nullptr)
            return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawIndexedPrimitives");

        if (currentVertexBuffer_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound.");

        if (currentIndexBuffer_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no index buffer is bound.");

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        System::ArgumentOutOfRangeException::ThrowIfNegative(startIndex, "startIndex");
        System::ArgumentOutOfRangeException::ThrowIfNegative(baseVertex, "baseVertex");
        System::ArgumentOutOfRangeException::ThrowIfNegative(minVertexIndex, "minVertexIndex");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(numVertices, "numVertices");

        const int consumedIndexCount =
            CheckedPrimitiveElementCount(primitiveType, primitiveCount);
        const int availableIndexCount = currentIndexBuffer_->GetRenderer().GetIndexCount();
        if (startIndex > availableIndexCount ||
            consumedIndexCount > availableIndexCount - startIndex)
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "The requested primitive range exceeds the bound index buffer.");
        }

        const int availableVertexCount = currentVertexBuffer_->GetRenderer().GetVertexCount();
        // REMED-GFX-200: the binding offset moves the whole declared range, so it is part of what
        // must fit -- exactly as DrawInstancedPrimitives below has counted it since REMED-GFX-118.
        const int bindingVertexOffset = CurrentVertexBufferOffset();
        if (bindingVertexOffset > availableVertexCount ||
            baseVertex > availableVertexCount - bindingVertexOffset ||
            minVertexIndex > availableVertexCount - bindingVertexOffset - baseVertex ||
            numVertices > availableVertexCount - bindingVertexOffset - baseVertex - minVertexIndex)
        {
            throw System::ArgumentOutOfRangeException(
                "numVertices", std::to_string(numVertices),
                "The declared vertex range exceeds the bound vertex buffer.");
        }

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams p;
        currentEffect_->FillGpuDrawParams(p);
        p.startIndex = startIndex;
        // REMED-GFX-200: VertexBufferBinding.VertexOffset shifts a stream's base and baseVertex is
        // added to every decoded index -- both in vertex elements, against that stream's own
        // declaration stride. Their sum is what each decoded index is displaced by, so carrying the
        // shared part in baseVertex delivers it through the element-unit channel every renderer
        // already converts to bytes exactly once with the stream's own stride, and applies each of
        // the two exactly once. startIndex is untouched: it selects index elements, which a binding
        // offset never displaces. p.vertexBufferOffset stays 0 here -- see DrawPrimitives above.
        // REMED-GFX-201: baseVertex advances EVERY per-vertex stream, each by that many of its own
        // elements, which is FNA3D's `vertexStride * (vertexOffset + baseVertex)` per binding.
        const int foldedOffset = FoldedVertexStreamOffset();
        p.baseVertex = baseVertex + foldedOffset;
        p.minVertexIndex = minVertexIndex;
        p.numVertices = numVertices;
        FillVertexStreamBindings(
            p, foldedOffset, /*allowLegacyEmptyDeclarationFallback=*/true);
        // The declared window is [baseVertex + minVertexIndex, + numVertices) in every stream's
        // own elements, so every stream must hold it -- not only the one named by `vb`. Argument
        // validation precedes the capability gate for the reason DrawPrimitives states.
        ValidateVertexStreamRanges(
            p, p.baseVertex + minVertexIndex, numVertices,
            "numVertices", std::to_string(numVertices));
        ValidateVertexStreamCapability(p);
        applySamplerStatesToRenderer();
        renderer_->DrawIndexedPrimitivesEx(
            currentVertexBuffer_->GetRenderer(),
            currentIndexBuffer_->GetRenderer(),
            world, view, proj,
            primitiveType, primitiveCount, p
        );
    }

    void GraphicsDevice::DrawInstancedPrimitives(
        PrimitiveType primitiveType,
        int baseVertex,
        int minVertexIndex,
        int numVertices,
        int startIndex,
        int primitiveCount,
        int instanceCount
    )
    {
        if (renderer_ == nullptr)
            return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawInstancedPrimitives");

        if (currentVertexBuffer_ == nullptr)
            throw std::runtime_error(
                "GraphicsDevice::DrawInstancedPrimitives: no vertex buffer is bound.");

        if (currentIndexBuffer_ == nullptr)
            throw std::runtime_error(
                "GraphicsDevice::DrawInstancedPrimitives: no index buffer is bound.");

        if (currentEffect_ == nullptr)
            throw std::runtime_error(
                "GraphicsDevice::DrawInstancedPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        System::ArgumentOutOfRangeException::ThrowIfNegative(startIndex, "startIndex");
        System::ArgumentOutOfRangeException::ThrowIfNegative(baseVertex, "baseVertex");
        System::ArgumentOutOfRangeException::ThrowIfNegative(minVertexIndex, "minVertexIndex");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(numVertices, "numVertices");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(instanceCount, "instanceCount");

        // REMED-GFX-118: the instanced entry point takes the same indexed contract as
        // DrawIndexedPrimitives above -- startIndex is an index-element offset, primitiveCount
        // fixes the exact topology-derived index count, baseVertex is added to every decoded index
        // once, and minVertexIndex/numVertices declare the referenced vertex window. Rejecting a
        // request that leaves either bound buffer here keeps every renderer's native binding an
        // exact range instead of a silently widened or clamped draw. instanceCount is validated
        // independently: it never widens or narrows the geometry range.
        const int consumedIndexCount =
            CheckedPrimitiveElementCount(primitiveType, primitiveCount);
        const int availableIndexCount = currentIndexBuffer_->GetRenderer().GetIndexCount();
        if (startIndex > availableIndexCount ||
            consumedIndexCount > availableIndexCount - startIndex)
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "The requested primitive range exceeds the bound index buffer.");
        }

        // REMED-GFX-200: the same binding-0 rule the ordinary routes above now use, single-sourced.
        const int vertexBufferOffset = CurrentVertexBufferOffset();

        const int availableVertexCount = currentVertexBuffer_->GetRenderer().GetVertexCount();
        if (vertexBufferOffset > availableVertexCount ||
            baseVertex > availableVertexCount - vertexBufferOffset ||
            minVertexIndex > availableVertexCount - vertexBufferOffset - baseVertex ||
            numVertices > availableVertexCount - vertexBufferOffset - baseVertex - minVertexIndex)
        {
            throw System::ArgumentOutOfRangeException(
                "numVertices", std::to_string(numVertices),
                "The declared vertex range exceeds the bound vertex buffer.");
        }

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams p;
        currentEffect_->FillGpuDrawParams(p);
        p.instanceCount = instanceCount;
        p.startIndex    = startIndex;
        p.baseVertex    = baseVertex;
        p.minVertexIndex = minVertexIndex;
        p.numVertices    = numVertices;
        // REMED-GFX-202: the SAME complete stream description the two ordinary routes build, from
        // the same helper -- FNA prepares one binding array for all three draw routes and an
        // instance stream is simply an entry whose InstanceFrequency is greater than zero.
        //
        // The fold is 0 here, unlike the ordinary routes. Those fold because their renderers have
        // no per-binding native offset channel at all and must deliver the offset through
        // vertexStart/baseVertex (REMED-GFX-200); this route's renderers do have one -- that is what
        // REMED-GFX-122/123 built -- so every stream carries its whole public VertexOffset and
        // baseVertex stays exactly the caller's value. That is also FNA3D's own D3D11 driver:
        // `offset = binding.vertexOffset * stride` per binding, with `BaseVertexLocation`
        // passed separately to DrawIndexedInstanced. Folding here would additionally be wrong,
        // because baseVertex must NOT advance a per-instance stream and the smallest offset among
        // the per-vertex streams has no reason to be shared with them.
        FillVertexStreamBindings(
            p, /*foldedOffset=*/0, /*allowLegacyEmptyDeclarationFallback=*/false);
        // The declared window is [baseVertex + minVertexIndex, + numVertices) in every per-vertex
        // stream's own elements, and every per-instance stream owes one record per complete
        // frequency-sized group of instances. Argument validation precedes the capability gate for
        // the reason DrawPrimitives states: an out-of-range request is wrong on every renderer.
        ValidateVertexStreamRanges(
            p, baseVertex + minVertexIndex, numVertices,
            "numVertices", std::to_string(numVertices));
        ValidateInstanceStreamRanges(p, instanceCount);
        ValidateVertexStreamCapability(p);
        applySamplerStatesToRenderer();
        renderer_->DrawInstancedPrimitivesEx(
            currentVertexBuffer_->GetRenderer(),
            currentIndexBuffer_->GetRenderer(),
            world, view, proj,
            primitiveType, primitiveCount, instanceCount, p
        );
    }

    void GraphicsDevice::DrawUserPrimitives(
        PrimitiveType primitiveType,
        const void* vertexData,
        int vertexOffset,
        int primitiveCount
    )
    {
        if (renderer_ == nullptr)
            return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");

        // Compute vertex count from primitive type (mirrors FNA PrimitiveVerts).
        int totalVerts;
        switch (primitiveType)
        {
            case PrimitiveType::TriangleList:  totalVerts = primitiveCount * 3; break;
            case PrimitiveType::TriangleStrip: totalVerts = primitiveCount + 2; break;
            case PrimitiveType::LineList:      totalVerts = primitiveCount * 2; break;
            case PrimitiveType::LineStrip:     totalVerts = primitiveCount + 1; break;
            case PrimitiveType::PointListEXT:  totalVerts = primitiveCount;     break;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }

        // vertexData points to an array of VertexPositionColor starting at vertexOffset.
        const auto* vertices = static_cast<const VertexPositionColor*>(vertexData) + vertexOffset;

        // Pack into the GPU stream this vertex type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionColorStream;

        std::vector<GpuVertex> packed(static_cast<std::size_t>(totalVerts));
        for (int i = 0; i < totalVerts; ++i)
            packed[i] = CNA::Internal::Graphics::Pack(vertices[i]);

        // Upload to a temporary vertex buffer and draw.
        auto tmpVb = renderer_->CreateVertexBuffer(totalVerts);
        tmpVb->SetData(packed.data(), totalVerts, sizeof(GpuVertex));

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams drawParams;
        currentEffect_->FillGpuDrawParams(drawParams);
        drawParams.vertexColorEnabled = true;
        applySamplerStatesToRenderer();
        renderer_->DrawPrimitivesEx(*tmpVb, world, view, proj, primitiveType, primitiveCount,
                                    drawParams);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType primitiveType,
        const void* vertexData,
        int vertexOffset,
        int numVertices,
        const void* indexData,
        int indexOffset,
        int primitiveCount
    )
    {
        if (renderer_ == nullptr)
            return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");

        // Compute total index count from primitive type (mirrors FNA PrimitiveVerts).
        int indexCount = 0;
        switch (primitiveType)
        {
            case PrimitiveType::TriangleList:  indexCount = primitiveCount * 3; break;
            case PrimitiveType::TriangleStrip: indexCount = primitiveCount + 2; break;
            case PrimitiveType::LineList:      indexCount = primitiveCount * 2; break;
            case PrimitiveType::LineStrip:     indexCount = primitiveCount + 1; break;
            case PrimitiveType::PointListEXT:  indexCount = primitiveCount;     break;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }

        // Pack vertices from caller array (assumed VertexPositionColor layout).
        const auto* vertices = static_cast<const VertexPositionColor*>(vertexData) + vertexOffset;
        using GpuVertex = CNA::Internal::Graphics::PositionColorStream;

        std::vector<GpuVertex> packed(static_cast<std::size_t>(numVertices));
        for (int i = 0; i < numVertices; ++i)
            packed[i] = CNA::Internal::Graphics::Pack(vertices[i]);

        // Copy 16-bit indices with offset applied.
        const auto* indices = static_cast<const std::uint16_t*>(indexData) + indexOffset;
        std::vector<std::uint16_t> indexCopy(static_cast<std::size_t>(indexCount));
        for (int i = 0; i < indexCount; ++i)
            indexCopy[i] = indices[i];

        auto tmpVb = renderer_->CreateVertexBuffer(numVertices);
        tmpVb->SetData(packed.data(), numVertices, sizeof(GpuVertex));

        auto tmpIb = renderer_->CreateIndexBuffer16(indexCount);
        tmpIb->SetData16(indexCopy.data(), indexCount);

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams drawParams;
        currentEffect_->FillGpuDrawParams(drawParams);
        drawParams.vertexColorEnabled = true;
        drawParams.numVertices = numVertices;
        applySamplerStatesToRenderer();
        renderer_->DrawIndexedPrimitivesEx(*tmpVb, *tmpIb, world, view, proj, primitiveType,
                                           primitiveCount, drawParams);
    }

    // -----------------------------------------------------------------------
    // DrawUserPrimitives — typed overloads
    // -----------------------------------------------------------------------

    // Public CNAEXT static — mirrors FNA's private PrimitiveVerts().
    int GraphicsDevice::PrimitiveVerts(PrimitiveType type, int primitiveCount)
    {
        switch (type)
        {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }
    }

    namespace
    {
        int VertexCountForUserPrimitives(PrimitiveType type, int primitiveCount)
        {
            return GraphicsDevice::PrimitiveVerts(type, primitiveCount);
        }

        // The GPU vertex streams of the built-in types, shared with their VertexDeclarations and
        // with VertexBuffer so a stride can never be described in two places at once.
        using GpuVPC  = CNA::Internal::Graphics::PositionColorStream;         // 16
        using GpuVPT  = CNA::Internal::Graphics::PositionTextureStream;       // 20
        using GpuVPCT = CNA::Internal::Graphics::PositionColorTextureStream;  // 24
        using GpuVPNT = CNA::Internal::Graphics::PositionNormalTextureStream; // 32

        int VertexElementFormatByteSize(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single: return 4;
                case VertexElementFormat::Vector2: return 8;
                case VertexElementFormat::Vector3: return 12;
                case VertexElementFormat::Vector4: return 16;
                case VertexElementFormat::Color:
                case VertexElementFormat::Byte4:
                case VertexElementFormat::Short2:
                case VertexElementFormat::NormalizedShort2:
                case VertexElementFormat::HalfVector2:
                    return 4;
                case VertexElementFormat::Short4:
                case VertexElementFormat::NormalizedShort4:
                case VertexElementFormat::HalfVector4:
                    return 8;
            }
            return 0;
        }

        // A declaration handed to a DrawUser overload is the only description of the source
        // bytes, so it has to describe a stream at all before a single byte is read: at least one
        // element, a positive stride, and no element reaching past that stride into what would be
        // the next vertex.
        int ValidateUserVertexDeclaration(const VertexDeclaration& declaration)
        {
            const int stride = declaration.getVertexStrideProperty();
            const std::vector<VertexElement>& elements = declaration.GetVertexElements();
            if (elements.empty() || stride <= 0)
            {
                throw System::ArgumentException(
                    "The VertexDeclaration must describe at least one element and a positive "
                    "vertex stride.",
                    "vertexDeclaration");
            }
            for (const VertexElement& element : elements)
            {
                const int offset = element.getOffsetProperty();
                const int size =
                    VertexElementFormatByteSize(element.getVertexElementFormatProperty());
                if (offset < 0 || size <= 0 || offset > stride - size)
                {
                    throw System::ArgumentException(
                        "The VertexDeclaration contains an element outside its own vertex stride.",
                        "vertexDeclaration");
                }
            }
            return stride;
        }

        // Source ranges are checked in 64 bits and rejected, never clamped: a range that would
        // wrap a 32-bit byte count must not silently address a small offset instead.
        void ValidateUserSourceRange(std::int64_t first,
                                     std::int64_t count,
                                     std::int64_t elementSize,
                                     const char* firstName,
                                     const char* countName)
        {
            if (first < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    firstName, std::to_string(first), "This parameter must be non-negative.");
            }
            if (count < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    countName, std::to_string(count), "This parameter must be non-negative.");
            }
            if ((first + count) * elementSize >
                static_cast<std::int64_t>(std::numeric_limits<int>::max()))
            {
                throw System::ArgumentOutOfRangeException(
                    firstName, std::to_string(first),
                    "The requested source byte range is too large.");
            }
        }
    }

    // Grows the scratch buffer only when the requested size exceeds current capacity, so
    // steady-state DrawUserPrimitives/DrawUserIndexedPrimitives calls (same or shrinking vertex
    // counts) reuse the existing allocation instead of allocating on every draw.
    void* GraphicsDevice::AcquireUserVertexScratch(std::size_t bytes)
    {
        if (userVertexScratch_.size() < bytes) userVertexScratch_.resize(bytes);
        return userVertexScratch_.data();
    }

    void* GraphicsDevice::AcquireUserIndexScratch(std::size_t bytes)
    {
        if (userIndexScratch_.size() < bytes) userIndexScratch_.resize(bytes);
        return userIndexScratch_.data();
    }

    // DrawUserPrimitives — VertexPositionColor
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColor* data, int offset, int count)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPC*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPC)));
        for (int i = 0; i < n; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(data[offset + i]);
        }
        auto vb = renderer_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPC));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionTexture* data, int offset, int count)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPT*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPT)));
        for (int i = 0; i < n; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(data[offset + i]);
        }
        auto vb = renderer_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionColorTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColorTexture* data, int offset, int count)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPCT*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPCT)));
        for (int i = 0; i < n; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(data[offset + i]);
        }
        auto vb = renderer_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPCT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionNormalTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionNormalTexture* data, int offset, int count)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPNT*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPNT)));
        for (int i = 0; i < n; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(data[offset + i]);
        }
        auto vb = renderer_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPNT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — explicit VertexDeclaration (FNA second generic overload)
    //
    // The source is a GPU vertex stream the caller already packed: the declaration is the only
    // description of it, so the bytes are consumed exactly at the declared stride and nothing is
    // converted here. An array of a built-in vertex structure is not such a stream and reaches
    // this overload only after the typed overload below has packed it.
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const void* vertexData, int vertexOffset,
                                            int primitiveCount,
                                            const VertexDeclaration& vertexDeclaration)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        const int stride = ValidateUserVertexDeclaration(vertexDeclaration);
        const int n      = VertexCountForUserPrimitives(type, primitiveCount);
        ValidateUserSourceRange(vertexOffset, n, stride, "vertexOffset", "primitiveCount");
        // Apply vertexOffset in bytes then upload n vertices worth of raw data.
        const auto* src = static_cast<const std::uint8_t*>(vertexData)
                          + static_cast<std::ptrdiff_t>(vertexOffset) * stride;
        auto vb = renderer_->CreateVertexBuffer(n);
        vb->SetVertexDeclaration(vertexDeclaration);
        vb->SetData(src, n, static_cast<std::size_t>(stride));
        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
        applySamplerStatesToRenderer();
        renderer_->DrawPrimitivesEx(*vb, world, view, proj, type, primitiveCount, p);
    }

    // The object-to-stream conversion for the explicit-declaration draws. It runs here and only
    // here: the packed stream is then handed to the raw overload above, which consumes it exactly
    // as it consumes a caller-packed one.
    template <typename VertexT>
    void GraphicsDevice::DrawUserPrimitivesFromObjects(
        PrimitiveType type, const VertexT* vertexData, int vertexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        using Stream = CNA::Internal::Graphics::VertexStream<VertexT>;
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        const int stride = ValidateUserVertexDeclaration(vertexDeclaration);
        if (stride != CNA::Internal::Graphics::VertexStreamStride<VertexT>)
        {
            throw System::ArgumentException(
                "The VertexDeclaration's VertexStride is not the GPU vertex stream stride of the "
                "supplied vertex type.",
                "vertexDeclaration");
        }
        const int n = VertexCountForUserPrimitives(type, primitiveCount);
        ValidateUserSourceRange(vertexOffset, n, static_cast<std::int64_t>(sizeof(VertexT)),
                                "vertexOffset", "primitiveCount");
        auto* packed = static_cast<Stream*>(
            AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(Stream)));
        for (int i = 0; i < n; ++i)
            packed[i] = CNA::Internal::Graphics::Pack(vertexData[vertexOffset + i]);
        DrawUserPrimitives(type, static_cast<const void*>(packed), 0, primitiveCount,
                           vertexDeclaration);
    }

    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColor* vertexData, int vertexOffset,
                                            int primitiveCount,
                                            const VertexDeclaration& vertexDeclaration)
    {
        DrawUserPrimitivesFromObjects(type, vertexData, vertexOffset, primitiveCount,
                                      vertexDeclaration);
    }

    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionTexture* vertexData,
                                            int vertexOffset, int primitiveCount,
                                            const VertexDeclaration& vertexDeclaration)
    {
        DrawUserPrimitivesFromObjects(type, vertexData, vertexOffset, primitiveCount,
                                      vertexDeclaration);
    }

    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColorTexture* vertexData,
                                            int vertexOffset, int primitiveCount,
                                            const VertexDeclaration& vertexDeclaration)
    {
        DrawUserPrimitivesFromObjects(type, vertexData, vertexOffset, primitiveCount,
                                      vertexDeclaration);
    }

    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionNormalTexture* vertexData,
                                            int vertexOffset, int primitiveCount,
                                            const VertexDeclaration& vertexDeclaration)
    {
        DrawUserPrimitivesFromObjects(type, vertexData, vertexOffset, primitiveCount,
                                      vertexDeclaration);
    }

    // -----------------------------------------------------------------------
    // DrawUserIndexedPrimitives — typed overloads
    // -----------------------------------------------------------------------

    namespace
    {
        int IndexCountForPrimitives(PrimitiveType type, int primitiveCount)
        {
            switch (type)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
                default:
                    throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColor* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPC*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPC)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPC));
        auto ib = renderer_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPT)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPT));
        auto ib = renderer_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColorTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPCT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPCT)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPCT));
        auto ib = renderer_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionNormalTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPNT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPNT)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPNT));
        auto ib = renderer_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    // DrawUserIndexedPrimitives — 32-bit index overloads

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColor* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPC*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPC)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPC));
        auto ib = renderer_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPT)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPT));
        auto ib = renderer_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColorTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPCT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPCT)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPCT));
        auto ib = renderer_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionNormalTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPNT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPNT)));
        for (int i = 0; i < numVerts; ++i)
        {
            packed[i] = CNA::Internal::Graphics::Pack(vertices[vOffset + i]);
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPNT));
        auto ib = renderer_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToRenderer();
          renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    // DrawUserIndexedPrimitives — explicit VertexDeclaration (FNA second generic overloads)
    //
    // Same contract as the non-indexed raw overload: the source is a caller-packed GPU vertex
    // stream, consumed at the declared stride with no conversion.

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const void* vertexData, int vOffset, int numVerts,
                                                   const std::uint16_t* indexData, int iOffset, int primCount,
                                                   const VertexDeclaration& vd)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int stride = ValidateUserVertexDeclaration(vd);
        const int ic     = IndexCountForPrimitives(type, primCount);
        ValidateUserSourceRange(vOffset, numVerts, stride, "vertexOffset", "numVertices");
        ValidateUserSourceRange(iOffset, ic, static_cast<std::int64_t>(sizeof(std::uint16_t)),
                                "indexOffset", "primitiveCount");
        const auto* src  = static_cast<const std::uint8_t*>(vertexData)
                           + static_cast<std::ptrdiff_t>(vOffset) * stride;
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetVertexDeclaration(vd);
        vb->SetData(src, numVerts, static_cast<std::size_t>(stride));
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indexData + iOffset, indexData + iOffset + ic, idx);
        auto ib = renderer_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
        applySamplerStatesToRenderer();
        renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const void* vertexData, int vOffset, int numVerts,
                                                   const std::uint32_t* indexData, int iOffset, int primCount,
                                                   const VertexDeclaration& vd)
    {
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int stride = ValidateUserVertexDeclaration(vd);
        const int ic     = IndexCountForPrimitives(type, primCount);
        ValidateUserSourceRange(vOffset, numVerts, stride, "vertexOffset", "numVertices");
        ValidateUserSourceRange(iOffset, ic, static_cast<std::int64_t>(sizeof(std::uint32_t)),
                                "indexOffset", "primitiveCount");
        const auto* src  = static_cast<const std::uint8_t*>(vertexData)
                           + static_cast<std::ptrdiff_t>(vOffset) * stride;
        auto vb = renderer_->CreateVertexBuffer(numVerts);
        vb->SetVertexDeclaration(vd);
        vb->SetData(src, numVerts, static_cast<std::size_t>(stride));
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indexData + iOffset, indexData + iOffset + ic, idx);
        auto ib = renderer_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Renderers::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
        applySamplerStatesToRenderer();
        renderer_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p);
    }

    // The indexed object-to-stream conversion. Like its non-indexed sibling it packs the source
    // range once and then delegates to the raw overload, so the vertex offset is applied in
    // objects here and never again in bytes downstream, while the index offset stays in indices
    // and is applied by the raw overload alone.
    template <typename VertexT, typename IndexT>
    void GraphicsDevice::DrawUserIndexedPrimitivesFromObjects(
        PrimitiveType type, const VertexT* vertexData, int vertexOffset, int numVertices,
        const IndexT* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        using Stream = CNA::Internal::Graphics::VertexStream<VertexT>;
        if (!renderer_) return;
        renderer_->Ensure3DSupported("GraphicsDevice::DrawUserIndexedPrimitives");
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        const int stride = ValidateUserVertexDeclaration(vertexDeclaration);
        if (stride != CNA::Internal::Graphics::VertexStreamStride<VertexT>)
        {
            throw System::ArgumentException(
                "The VertexDeclaration's VertexStride is not the GPU vertex stream stride of the "
                "supplied vertex type.",
                "vertexDeclaration");
        }
        ValidateUserSourceRange(vertexOffset, numVertices,
                                static_cast<std::int64_t>(sizeof(VertexT)),
                                "vertexOffset", "numVertices");
        auto* packed = static_cast<Stream*>(
            AcquireUserVertexScratch(static_cast<std::size_t>(numVertices) * sizeof(Stream)));
        for (int i = 0; i < numVertices; ++i)
            packed[i] = CNA::Internal::Graphics::Pack(vertexData[vertexOffset + i]);
        DrawUserIndexedPrimitives(type, static_cast<const void*>(packed), 0, numVertices,
                                  indexData, indexOffset, primitiveCount, vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionColor* vertexData, int vertexOffset,
        int numVertices, const std::uint16_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionTexture* vertexData, int vertexOffset,
        int numVertices, const std::uint16_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionColorTexture* vertexData, int vertexOffset,
        int numVertices, const std::uint16_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionNormalTexture* vertexData, int vertexOffset,
        int numVertices, const std::uint16_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionColor* vertexData, int vertexOffset,
        int numVertices, const std::uint32_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionTexture* vertexData, int vertexOffset,
        int numVertices, const std::uint32_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionColorTexture* vertexData, int vertexOffset,
        int numVertices, const std::uint32_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType type, const VertexPositionNormalTexture* vertexData, int vertexOffset,
        int numVertices, const std::uint32_t* indexData, int indexOffset, int primitiveCount,
        const VertexDeclaration& vertexDeclaration)
    {
        DrawUserIndexedPrimitivesFromObjects(type, vertexData, vertexOffset, numVertices,
                                             indexData, indexOffset, primitiveCount,
                                             vertexDeclaration);
    }

    CNA::GraphicsRendererType GraphicsDevice::GetGraphicsRendererType() const
    {
        // plan_runtimerenderer.md RTR-P7-3: this device's own renderer. activeDescriptor_ is null
        // only if the accessor is somehow reached before resolution, in which case the build
        // default is the honest answer.
        return activeDescriptor_ != nullptr ? activeDescriptor_->type
                                            : CNA::getCurrentGraphicsRendererType();
    }

    std::string_view GraphicsDevice::GetGraphicsRendererName() const
    {
        return CNA::getGraphicsRendererName(GetGraphicsRendererType());
    }

    CNA::Internal::Renderers::IGraphicsRenderer& GraphicsDevice::GetRenderer() const
    {
        if (renderer_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice renderer is not available.");
        }

        return *renderer_;
    }

    bool GraphicsDevice::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // CompiledEffects was appended after many renderer-specific capability switches were
        // written. Their historical catch-all may return true for unknown enum values, so this
        // security/compatibility boundary requires a separate explicit renderer opt-in.
        if (capability == CNA::GraphicsCapability::CompiledEffects)
            return GetRenderer().SupportsCompiledEffects();
        // plan_modern.md MOD-100/MOD-101: the float render-target entries are derived, for the same
        // reason CompiledEffects is -- a renderer whose capability switch ends in
        // `default: return true` would otherwise claim that values above 1.0 survive a
        // render-to-target purely because it has never heard of the enumerator. Each is answered by
        // the format the CNAEXT engine layer's HDR pipeline would actually allocate: RGBA32F for
        // the 32-bit entry, and HdrBlendable (CNA's RGBA16F) for the half-float one.
        if (capability == CNA::GraphicsCapability::FloatRenderTargets)
            return SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4);
        if (capability == CNA::GraphicsCapability::HalfFloatRenderTargets)
            return SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable);
        // Derived for the same reason (MOD-123): answered by a renderer virtual whose default is
        // false, never by a capability switch whose default is true.
        if (capability == CNA::GraphicsCapability::HalfFloatTextureLinearFiltering)
            return GetRenderer().SupportsHalfFloatTextureLinearFilteringEXT();
        // MOD-1500: compute is derived for exactly the same reason.
        if (capability == CNA::GraphicsCapability::ComputeShaders)
            return GetRenderer().SupportsComputeShadersEXT();
        return GetRenderer().SupportsCapability(capability);
    }

    bool GraphicsDevice::ExecutesShaderEffectSourceEXT() const
    {
        return GetRenderer().ExecutesShaderEffectSourceEXT();
    }

    bool GraphicsDevice::SupportsShadowSamplingEXT() const
    {
        return GetRenderer().SupportsShadowSamplingEXT();
    }

    bool GraphicsDevice::SupportsImageBasedLightingEXT() const
    {
        return GetRenderer().SupportsImageBasedLightingEXT();
    }

    int GraphicsDevice::GetMaxComputeWorkGroupCountEXT(const int axis) const
    {
        if (axis < 0 || axis > 2) return 0;
        return GetRenderer().GetMaxComputeWorkGroupCountEXT(axis);
    }

    int GraphicsDevice::GetMaxComputeWorkGroupSizeEXT(const int axis) const
    {
        if (axis < 0 || axis > 2) return 0;
        return GetRenderer().GetMaxComputeWorkGroupSizeEXT(axis);
    }

    int GraphicsDevice::GetMaxComputeWorkGroupInvocationsEXT() const
    {
        return GetRenderer().GetMaxComputeWorkGroupInvocationsEXT();
    }

    bool GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat format) const
    {
        // plan_modern.md MOD-103/MOD-104: asks the same question RenderTarget2D's constructor asks
        // (plan_runtimerenderer.md design decision 9's tri-state verdict), so the two can never
        // disagree -- a format this returns true for is a format RenderTarget2D will accept, and one
        // it returns false for is one the constructor refuses.
        switch (GetRenderer().ClassifyRenderTargetFormatEXT(static_cast<int>(format)))
        {
            case CNA::Internal::Renderers::RendererFormatVerdict::Supported:
                return true;
            case CNA::Internal::Renderers::RendererFormatVerdict::Unsupported:
                return false;
            case CNA::Internal::Renderers::RendererFormatVerdict::Defer:
                break;
        }
        // Defer means "the framework's own rule applies", and that rule (Texture::ValidateFormat)
        // admits Color alone -- which is the literal truth for a renderer that has not implemented
        // any other render-target format.
        return format == SurfaceFormat::Color;
    }

    int GraphicsDevice::GetMaxTextureDimension() const
    {
        return GetRenderer().GetMaxTextureDimension();
    }

    void GraphicsDevice::SetUnsupported3DGraphicsCallBehavior(
        CNA::Unsupported3DGraphicsCallBehavior behavior)
    {
        GetRenderer().SetUnsupported3DGraphicsCallBehavior(behavior);
    }

    CNA::Unsupported3DGraphicsCallBehavior
    GraphicsDevice::GetUnsupported3DGraphicsCallBehavior() const
    {
        return GetRenderer().GetUnsupported3DGraphicsCallBehavior();
    }

    void GraphicsDevice::SetCurrentEffect(Effect* effect)
    {
        currentEffect_ = effect;
    }

    void GraphicsDevice::ClearCurrentEffectIf(const Effect* effect) noexcept
    {
        if (currentEffect_ == effect) currentEffect_ = nullptr;
    }

    const std::string& GraphicsDevice::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.GraphicsDevice";
        return typeName;
    }

    void GraphicsDevice::SetPresentationParameters(const PresentationParameters& pp)
    {
        PresentationParameters applied = pp.Clone();
        if (renderer_)
        {
            NormalizeAppliedPresentationFormats(*renderer_, applied);
            // This CNAEXT store-only path does not reconfigure MSAA. A renderer that clamped the
            // request at construction reports what is really in effect rather than letting a
            // request that was never applied be echoed back.
            applied.setMultiSampleCountProperty(renderer_->GetAppliedMultiSampleCountEXT(
                applied.getMultiSampleCountProperty()));
            renderer_->SetSwapInterval(toSwapInterval(pp.getPresentationIntervalProperty()));
        }
        presentationParameters_ = applied;
    }

    void GraphicsDevice::RecreateRendererForMultiSampleCount(int multiSampleCount)
    {
        presentationParameters_.setMultiSampleCountProperty(multiSampleCount);
        renderer_.reset();
        createRenderer();
        UpdateViewportFromWindow();
    }

    std::uintptr_t GraphicsDevice::GetWindowHandleInternal() const
    {
        return presentationParameters_.getDeviceWindowHandleProperty();
    }

    CNA::Platform::IPlatformWindow* GraphicsDevice::GetPlatformWindowInternal() const
    {
        return platformWindow_.get();
    }

    void GraphicsDevice::createOrAttachWindow()
    {
        // RTR-P5-17: deliberately the RESOLVED descriptor, not a fresh resolution. A reconstruction
        // (Reset, multisample change) must rebuild the same renderer -- an MSAA failure on a device
        // the game is already using is a real error, not a reason to change renderer mid-game. The
        // same value decides both which platform services this family gets and, below, who creates
        // it, so it is resolved once here.
        const auto& descriptor =
            activeDescriptor_ != nullptr ? *activeDescriptor_ : selectedDescriptor();

        // No real window, ever -- HEADLESS/SOFTWARE/STUB/PORTABLEGL, matching the constructor's own
        // needsVideoSubsystem check. GraphicsRendererCreateArgs::window stays nullptr;
        // UpdateViewportFromWindow() already falls back to the renderer's own GetViewportSize()
        // first and only touches the window if that yields nothing, and
        // applyPresentationParametersToWindow() already early-returns without one, so
        // neither needs its own guard.
        if (!descriptor.needsWindow)
        {
            platformWindow_.reset();
            ownsWindow_ = false;
            return;
        }

        // Runtime opt-in, same effect as the descriptor check above. Only renderers that can
        // genuinely run without a swap chain support this (D3D12 today) -- see
        // PresentationParameters::getHeadlessEXTProperty()'s own doc comment. A renderer that cannot
        // (D3D11's constructor always creates a swap chain; EasyGL's GL context is bound to a
        // window) will throw from its own constructor, which is the honest outcome: it is a real
        // "this renderer cannot do that" error, not something GraphicsDevice should paper over.
        if (presentationParameters_.getHeadlessEXTProperty())
        {
            platformWindow_.reset();
            return;
        }

        platform_->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
        videoSubsystemAcquired_ = true;

        const auto requestedHandle = presentationParameters_.getDeviceWindowHandleProperty();
        if (requestedHandle != 0)
        {
            // The public XNA property remains an integer compatibility token. Only the active
            // platform interprets it; GraphicsDevice never casts it to a toolkit-native type.
            platformWindow_ = platform_->AdoptWindowHandle(requestedHandle);
            // The caller keeps ownership: CNA may neither destroy this window nor rebuild it for a
            // fallback candidate that wants a different window kind (RTR-P5-12).
            ownsWindow_ = false;

            // An attached window is just as much the active input surface as a CNA-owned one.
            Microsoft::Xna::Framework::Input::TextInputEXT::setWindowHandleProperty(
                platformWindow_->GetWindowHandle());
            Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_setWindowId(
                platformWindow_->GetId());
            Microsoft::Xna::Framework::Input::Mouse::INTERNAL_setWindow(
                platformWindow_->GetWindowHandle(), platformWindow_->GetId());
            return;
        }

        const int width = presentationParameters_.getBackBufferWidthProperty() > 0
                              ? presentationParameters_.getBackBufferWidthProperty()
                              : 1024;

        const int height = presentationParameters_.getBackBufferHeightProperty() > 0
                               ? presentationParameters_.getBackBufferHeightProperty()
                               : 768;

        CNA::Platform::WindowDescription description;
        description.title = "Game";
        description.width = width;
        description.height = height;

        // MERGE (plan_platform.md PLAT-8 x plan_runtimerenderer.md P1/P2): next derived the render
        // intent, the high-density backing and the framebuffer bits from #if defined(CNA_RENDERER_*)
        // chains. That is exactly the construct P1/P3 removed, and it cannot be right in a
        // multi-renderer binary: several families are compiled in at once, so the chains describe
        // whichever macros happen to be defined rather than the renderer actually being created.
        // The descriptor already answers all three questions for the family in hand.
        description.highDpi = descriptor.wantsHighDpi;
        description.renderIntent = renderIntentFor(descriptor.windowKind);

        // A desktop GLX visual fixes these attributes when the window is created, before the
        // renderer asks IPlatformGlContext for a context. Supplying them at this boundary also
        // keeps the equivalent EGL selection deterministic on ES hosts. Families that pick their
        // config at context-creation time (the EasyGL set) leave the request zeroed, which reaches
        // the platform as "no request" -- the same outcome next got by omitting them from its list.
        //
        // FNA3D primes the same attributes itself inside FNA3D_PrepareWindowAttributes(), but the
        // platform resets the process-global GL attribute state as the first step of creating an
        // OpenGL-intent window, so that priming does not survive. Stating the requirement through
        // WindowDescription instead is what makes it survive: the platform applies this request
        // after its own reset. Without it the FNA3D OpenGL driver presents into a visual with no
        // stencil bits and every stencil test silently passes.
        description.openGlFramebuffer.depthBits      = descriptor.glFramebuffer.depthBits;
        description.openGlFramebuffer.stencilBits    = descriptor.glFramebuffer.stencilBits;
        description.openGlFramebuffer.doubleBuffered = descriptor.glFramebuffer.doubleBuffered;
        if (descriptor.glFramebuffer.wantsMultiSample)
        {
            description.openGlFramebuffer.samples =
                presentationParameters_.getMultiSampleCountProperty();
        }

        platformWindow_ = platform_->CreateWindow(description);
        if (!platformWindow_)
        {
            throw std::runtime_error("platform returned a null window");
        }
        // CNA created it, so CNA may tear it down and rebuild it for a candidate of another window
        // kind. MERGE: the platform wrapper tracks the same fact for lifetime purposes; this flag
        // is what the fallback policy reads (see the member's own comment).
        ownsWindow_ = true;

        const std::uintptr_t windowHandle = platformWindow_->GetWindowHandle();
        // Strict XNA exposes a native window handle. Headless/terminal windows still have a
        // platform token for routing input and GameWindow identity, but must not masquerade as a
        // native handle in PresentationParameters.
        if (platform_->GetCapabilities().nativeWindowHandle)
            presentationParameters_.setDeviceWindowHandleProperty(windowHandle);

        // Publish the window to the text-input subsystem (mirrors FNA, which sets
        // TextInputEXT.WindowHandle at window creation). Required for StartTextInput etc.
        Microsoft::Xna::Framework::Input::TextInputEXT::setWindowHandleProperty(windowHandle);
        Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_setWindowId(
            platformWindow_->GetId());

        // Publish the same window to Mouse (mirrors FNA setting Mouse.WindowHandle at window
        // creation, SDL3_FNAPlatform.cs). Lets SetPosition / relative-mouse-mode target the
        // real window instead of relying on a native mouse-focus fallback.
        Microsoft::Xna::Framework::Input::Mouse::INTERNAL_setWindow(
            windowHandle, platformWindow_->GetId());
    }

    void GraphicsDevice::SetContextRecoveryEnabled(bool enabled)
    {
        contextRecoveryEnabled_ = enabled;
        if (renderer_)
            renderer_->SetContextRecoveryEnabled(enabled);
    }

    void GraphicsDevice::SetStringMarkerEXT(const std::string& marker)
    {
        if (renderer_)
            renderer_->SetStringMarkerEXT(marker.c_str());
    }

    void GraphicsDevice::discardOwnedWindow()
    {
        if (platformWindow_ == nullptr)
            return;

        // MERGE: a surface presenter is bound to the window it was created for, so it cannot
        // outlive one. destroyNativeResources() already drops it in the same order; a fallback that
        // rebuilds the window has to do the same or the next candidate inherits a presenter
        // pointing at a destroyed window.
        surfacePresenter_.reset();

        if (ownsWindow_)
        {
            // Clear the shared input handles first: leaving them pointing at a window that is about
            // to be destroyed is exactly the dangling-handle bug destroyNativeResources() already
            // guards against.
            const std::uintptr_t windowHandle = platformWindow_->GetWindowHandle();
            if (Microsoft::Xna::Framework::Input::TextInputEXT::getWindowHandleProperty()
                == windowHandle)
            {
                Microsoft::Xna::Framework::Input::TextInputEXT::setWindowHandleProperty(0);
            }
            if (Microsoft::Xna::Framework::Input::Mouse::getWindowHandleProperty()
                == windowHandle)
            {
                Microsoft::Xna::Framework::Input::Mouse::INTERNAL_setWindow(0, 0);
            }

            // MERGE (plan_platform.md PLAT-8): destroying the window is the platform wrapper's job.
            // The wrapper created by CreateWindow() owns its window; the one returned by
            // AdoptWindowHandle() does not, so resetting a borrowed wrapper cannot destroy a
            // caller's window even if ownsWindow_ were ever wrong.
        }

        platformWindow_.reset();
        ownsWindow_ = false;
        presentationParameters_.setDeviceWindowHandleProperty(0);
    }

    void GraphicsDevice::resolveRenderer()
    {
        namespace Renderers = CNA::Internal::Renderers;
        using CNA::GraphicsRendererFallbackReason;
        using CNA::GraphicsRendererFallbackRecord;

        const auto attemptOrder = CNA::GraphicsRendererSelectionAccessEXT::GetAttemptOrder();

        // Design decision 6: with fallback disabled this loop runs exactly once and any failure
        // propagates unchanged, so a build that never opts in behaves as it always did.
        std::string firstFailure;

        for (const CNA::GraphicsRendererType candidateType : attemptOrder)
        {
            const Renderers::GraphicsRendererDescriptor* candidate =
                Renderers::GraphicsRendererRegistry::Find(candidateType);

            if (candidate == nullptr)
            {
                // A chain may legitimately name renderers other configurations have; record the
                // skip rather than omitting it, so the history explains every step.
                CNA::GraphicsRendererSelectionAccessEXT::RecordFallback(
                    GraphicsRendererFallbackRecord{
                        candidateType, GraphicsRendererFallbackReason::NotCompiledIn,
                        "not compiled into this build"});
                continue;
            }

            // Design decision 8: a candidate needing a different window kind cannot reuse the
            // window a previous attempt created. Recreating it is only legal while this device owns
            // it -- with a caller-supplied DeviceWindowHandle the attempt is refused outright
            // rather than silently run against an incompatible window.
            if (platformWindow_ != nullptr
                && !Renderers::AreWindowKindsCompatible(activeWindowKind_, candidate->windowKind))
            {
                if (!ownsWindow_)
                {
                    CNA::GraphicsRendererSelectionAccessEXT::RecordFallback(
                        GraphicsRendererFallbackRecord{
                            candidateType, GraphicsRendererFallbackReason::WindowKindConflict,
                            "needs a different window kind, and the window was supplied by the "
                            "caller through PresentationParameters.DeviceWindowHandle, so CNA may "
                            "not destroy and recreate it"});
                    continue;
                }
                discardOwnedWindow();
            }

            if (isDebugForcedUnavailable(candidate->name))
            {
                CNA::GraphicsRendererSelectionAccessEXT::RecordFallback(
                    GraphicsRendererFallbackRecord{
                        candidateType, GraphicsRendererFallbackReason::ProbeUnavailable,
                        "forced unavailable by CNA_DEBUG_UNAVAILABLE_RENDERERS"});
                continue;
            }

            if (!candidate->isAvailable())
            {
                CNA::GraphicsRendererSelectionAccessEXT::RecordFallback(
                    GraphicsRendererFallbackRecord{
                        candidateType, GraphicsRendererFallbackReason::ProbeUnavailable,
                        "the renderer reported it cannot run on this machine"});
                continue;
            }

            activeDescriptor_ = candidate;
            try
            {
                // PresentationParameters::HeadlessEXT is the runtime opt-in equivalent of the
                // descriptor's own needsVideoSubsystem: a renderer that normally wants a window
                // (D3D12) can be asked for a genuinely off-screen device instead. Skipping
                // Skipping the video subsystem is the point -- it is what lets such a device run
                // with no display server at all, not merely without a visible window.
                //
                // RTR-P5-15: done per candidate, not once up front. A fallback chain can legitimately
                // cross this boundary (VULKAN -> HEADLESS, or the reverse), and the answer belongs to
                // whichever renderer is actually being attempted. Subsystem acquisition is
                // reference counted, so a second attempt that also needs video is harmless.
                if (candidate->needsVideoSubsystem
                    && !presentationParameters_.getHeadlessEXTProperty())
                {
                    platform_->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
                    videoSubsystemAcquired_ = true;
                }

                if (platformWindow_ == nullptr)
                {
                    activeWindowKind_ = candidate->windowKind;
                    createOrAttachWindow();
                    applyPresentationParametersToWindow();
                }
                if (isDebugForcedInitFailure(candidate->name))
                {
                    throw std::runtime_error(
                        std::string(candidate->name) +
                        ": initialization failed (forced by CNA_DEBUG_FAIL_RENDERER_INIT)");
                }

                createRenderer();
            }
            catch (const std::exception& e)
            {
                activeDescriptor_ = nullptr;
                renderer_.reset();
                if (firstFailure.empty())
                    firstFailure = std::string(candidate->name) + ": " + e.what();

                // Design decision 6: without an opted-in chain this is simply the game's error.
                if (attemptOrder.size() == 1)
                    throw;

                CNA::GraphicsRendererSelectionAccessEXT::RecordFallback(
                    GraphicsRendererFallbackRecord{
                        candidateType, GraphicsRendererFallbackReason::InitializationFailed,
                        e.what()});

                // plan_runtimerenderer.md RTR-P5-13: only a window this device OWNS may be dropped
                // here. discardOwnedWindow() correctly refuses to destroy a caller-supplied window,
                // but it also dropped the window and zeroed DeviceWindowHandle unconditionally -- so
                // after any initialization failure CNA forgot the caller's window entirely and the
                // next candidate created one of its own. Two consequences, both wrong:
                //
                //   * the caller's window was silently abandoned: not destroyed, but no longer
                //     drawn into, and their DeviceWindowHandle field was set to 0 without a word
                //   * design decision 8's refusal became unreachable. That check needs
                //     a live window at the top of the next iteration, and nothing could ever
                //     leave it non-null, so WindowKindConflict was dead code
                //
                // Keeping the caller's window across the failure restores both: the next candidate
                // either accepts it, or is refused with WindowKindConflict naming why.
                if (ownsWindow_)
                    discardOwnedWindow();
                continue;
            }

            // plan_runtimerenderer.md design decision 5, refined by RTR-P5-18: the selection
            // latches on SUCCESSFUL resolution, not at the start of construction.
            //
            // Latching up front was the first cut, and it was wrong in a way the fallback tests
            // caught: a construction that threw still froze the selection, so a game that caught
            // the error could not then try a different configuration -- and GetActive() would
            // report a renderer that had never been created. Everything that consumes the decision
            // (window flags, whether a window exists at all, the video subsystem) happens inside
            // this function, so latching at its end is still before anything outside GraphicsDevice
            // can have acted on the answer.
            //
            // It forbids CHANGING the selection, not creating a renderer again:
            // RecreateRendererForMultiSampleCount() rebuilds the same renderer on a live device.
            CNA::GraphicsRendererSelectionAccessEXT::Latch(candidate->type);
            return;
        }

        // Design decision 7: an exhausted chain carries the FIRST failure as the primary cause --
        // that is the renderer the game actually asked for -- with the rest as accumulated detail.
        std::string detail;
        for (const auto& record : CNA::GraphicsRendererSelection::GetFallbackHistory())
        {
            detail += "\n  - " + std::string(CNA::getGraphicsRendererName(record.type)) + " (" +
                      std::string(CNA::getGraphicsRendererFallbackReasonName(record.reason)) +
                      "): " + record.message;
        }
        throw System::InvalidOperationException(
            "CNA: no graphics renderer could be created." +
            (firstFailure.empty() ? std::string() : "\n  first failure -- " + firstFailure) +
            "\n  attempted:" + detail);
    }

    void GraphicsDevice::createRenderer()
    {
        GraphicsRendererCreateArgs args;
        if (platformWindow_ != nullptr)
        {
            args.surface.windowId = platformWindow_->GetId();
            args.surface.nativeHandle = platformWindow_->GetNativeHandle();
            args.surface.drawableSize = platformWindow_->GetPixelSize();
            args.surface.displayScale = platformWindow_->GetDisplayScale();
        }
        // MERGE (plan_platform.md PLAT-8 x plan_runtimerenderer.md design decision 2): next picked
        // these services with `#if defined(CNA_RENDERER_<X>)` chains. The descriptor of the family
        // actually being constructed answers the same question correctly in a multi-renderer build,
        // where several of those macros can be defined at once.
        const auto& descriptor =
            activeDescriptor_ != nullptr ? *activeDescriptor_ : selectedDescriptor();

        if (descriptor.needsSurfacePresenter && platformWindow_ != nullptr)
        {
            if (surfacePresenter_ == nullptr)
                surfacePresenter_ = platform_->CreateSurfacePresenter(*platformWindow_);
            args.surfacePresenter = surfacePresenter_.get();
        }

        // Context-backed renderers receive only the narrow GL service plus the surface value
        // snapshot. They never resolve a native window or reach through IPlatform.
        if (descriptor.needsGlContext)
            args.glContext = platform_->GetGlContext();

        if (descriptor.needsVulkanSurface)
            args.vulkanSurface = platform_->GetVulkanSurface();

        args.virtualWidth = virtualWidth_;
        args.virtualHeight = virtualHeight_;
        args.contextRecoveryEnabled = contextRecoveryEnabled_;
        args.multiSampleCount = presentationParameters_.getMultiSampleCountProperty();
        args.swapInterval = toSwapInterval(presentationParameters_.getPresentationIntervalProperty());
        // plan_dx9.md D9-30: real presentation-parameter fidelity for renderers that need it (D3D9);
        // every other renderer continues to ignore these exactly as before the fields existed.
        args.backBufferFormat = static_cast<int>(presentationParameters_.getBackBufferFormatProperty());
        args.depthStencilFormat = static_cast<int>(presentationParameters_.getDepthStencilFormatProperty());
        args.isFullScreen = presentationParameters_.getIsFullScreenProperty();
        args.graphicsProfile = static_cast<int>(graphicsProfile_);
        // plan_dx9.md D9-34: forward a REAL, renderer-detected device-lost/reset event to this
        // GraphicsDevice's own public XNA events. Nine of the ten renderers never call this.
        args.deviceEventCallback = [this](CNA::Internal::Renderers::RendererDeviceEvent event)
        {
            switch (event)
            {
                case CNA::Internal::Renderers::RendererDeviceEvent::Lost:
                    deviceStatus_ = GraphicsDeviceStatus::Lost;
                    DeviceLost.Raise(this, System::EventArgs::Empty);
                    break;
                case CNA::Internal::Renderers::RendererDeviceEvent::Resetting:
                    deviceStatus_ = GraphicsDeviceStatus::NotReset;
                    DeviceResetting.Raise(this, System::EventArgs::Empty);
                    break;
                case CNA::Internal::Renderers::RendererDeviceEvent::Reset:
                    deviceStatus_ = GraphicsDeviceStatus::Normal;
                    DeviceReset.Raise(this, System::EventArgs::Empty);
                    break;
            }
        };

        // plan_runtimerenderer.md design decision 4: reached through the descriptor rather than a
        // single shared factory symbol, which is what lets several renderer archives coexist.
        renderer_ = descriptor.create(args);

        if (renderer_ != nullptr)
        {
            renderer_->SetVirtualResolution(virtualWidth_, virtualHeight_);
            NormalizeAppliedPresentationFormats(*renderer_, presentationParameters_);
            // A renderer that clamps to its own real modes during construction (GDI supports one
            // optional mode, 4x) surfaces that result immediately. Reset() already performs the
            // same write-back via ApplyMultiSampleCount(); direct construction previously retained
            // the unapplied request.
            presentationParameters_.setMultiSampleCountProperty(
                renderer_->GetAppliedMultiSampleCountEXT(
                    presentationParameters_.getMultiSampleCountProperty()));
            if (!rendererStartupNameLogged_)
            {
                // plan_runtimerenderer.md RTR-P7-10: the ACTIVE renderer, not the compile-time
                // one. In a multi-renderer build those differ, and printing the compile-time name
                // would misreport every runtime selection -- which is exactly the situation this
                // line exists to make visible.
                // MERGE: next routed this through CNA::Logger, which writes to stderr and honours
                // log levels -- a diagnostic has no business on stdout, and
                // GraphicsDeviceRendererTest::StartupDiagnosticNeverWritesToStdout pins that. next
                // logged the COMPILE-TIME name; the runtime-selected one is kept, since reporting
                // the compile-time name is precisely the misreport RTR-P7-10 exists to prevent.
                std::string startupMessage =
                    std::string("CNA: graphics renderer: ") + std::string(descriptor.name);
                if (CNA::GraphicsRendererSelection::GetAvailable().size() > 1)
                {
                    startupMessage +=
                        " (selected at runtime from "
                        + std::to_string(CNA::GraphicsRendererSelection::GetAvailable().size())
                        + " compiled in)";
                }
                CNA::Logger::Info(startupMessage, CNA::LogCategory::RENDER);
                rendererStartupNameLogged_ = true;
            }
        }
    }

    void GraphicsDevice::destroyNativeResources()
    {
        renderer_.reset();
        surfacePresenter_.reset();

        if (platformWindow_ != nullptr)
        {
            // Clear shared input tokens for both owned and caller-provided windows when they still
            // point at this device. Caller-provided windows remain borrowed.
            const std::uintptr_t windowHandle = platformWindow_->GetWindowHandle();
            if (Microsoft::Xna::Framework::Input::TextInputEXT::getWindowHandleProperty()
                == windowHandle)
            {
                Microsoft::Xna::Framework::Input::TextInputEXT::setWindowHandleProperty(0);
            }
            if (Microsoft::Xna::Framework::Input::Mouse::getWindowHandleProperty()
                == windowHandle)
            {
                Microsoft::Xna::Framework::Input::Mouse::INTERNAL_setWindow(0, 0);
            }
        }

        platformWindow_.reset();
    }

    void GraphicsDevice::UpdateViewportFromWindow()
    {
        if (renderer_ != nullptr && platformWindow_ != nullptr)
        {
            CNA::Internal::Renderers::RendererSurfaceInfo surface;
            surface.windowId = platformWindow_->GetId();
            surface.nativeHandle = platformWindow_->GetNativeHandle();
            surface.drawableSize = platformWindow_->GetPixelSize();
            surface.displayScale = platformWindow_->GetDisplayScale();
            renderer_->OnSurfaceChanged(surface);
        }

        int width = 0;
        int height = 0;

        if (renderer_ != nullptr)
        {
            renderer_->GetViewportSize(width, height);
        }

        if ((width <= 0 || height <= 0) && platformWindow_ != nullptr)
        {
            const CNA::Platform::WindowBounds bounds = platformWindow_->GetClientBounds();
            width = bounds.width;
            height = bounds.height;
        }
        if (width <= 0 || height <= 0)
        {
            return;
        }

        // The PHYSICAL rectangle actually pushed to the renderer's GL/GPU viewport -- distinct
        // from `width`/`height` above (the LOGICAL size GraphicsDevice.Viewport.Width/Height
        // exposes to game code). Equal to (0, 0, width, height) for every renderer that doesn't
        // override GetDefaultViewportRect() (i.e. every renderer before this method existed, and
        // every renderer except OpenGL2 today) -- only a renderer implementing real
        // Letterbox/Overscan/Stretch returns something else.
        int physX = 0, physY = 0, physWidth = width, physHeight = height;
        if (renderer_)
            renderer_->GetDefaultViewportRect(physX, physY, physWidth, physHeight);

        // Compared against the last size *this method itself* produced, not against
        // viewport_'s current width/height: viewport_ may hold a game-set custom
        // sub-region Viewport (e.g. split-screen) whose dimensions legitimately differ
        // from the backbuffer, and FNA's Present() never touches Viewport at all. Using
        // viewport_ as the "did anything change" signal would silently stomp such a
        // Viewport back to full-window size on the very next Present() call even though
        // no resize occurred. Also compares the PHYSICAL rectangle, not just the logical
        // size: under Letterbox/Overscan the logical size can stay fixed across a window
        // resize while the physical rectangle still needs to be re-applied.
        if (width == lastKnownViewportWidth_ && height == lastKnownViewportHeight_ &&
            physX == lastKnownViewportPhysX_ && physY == lastKnownViewportPhysY_ &&
            physWidth == lastKnownViewportPhysWidth_ && physHeight == lastKnownViewportPhysHeight_)
        {
            return;
        }

        lastKnownViewportWidth_ = width;
        lastKnownViewportHeight_ = height;
        lastKnownViewportPhysX_ = physX;
        lastKnownViewportPhysY_ = physY;
        lastKnownViewportPhysWidth_ = physWidth;
        lastKnownViewportPhysHeight_ = physHeight;

        viewport_.setXProperty(0);
        viewport_.setYProperty(0);
        viewport_.setMinDepthProperty(0.0f);
        viewport_.setMaxDepthProperty(1.0f);
        viewport_.setWidthProperty(width);
        viewport_.setHeightProperty(height);

        // Mutates viewport_'s fields directly (not via setViewportProperty(), to preserve the
        // "compared against lastKnownViewportWidth/Height_, not viewport_" semantics above) --
        // push the reset value to the renderer explicitly (Task 880) so a window resize actually
        // updates the GPU-side viewport too, not just the C++-side Viewport property. Pushes the
        // PHYSICAL rectangle (physX/Y/Width/Height), not the logical width/height -- see
        // GetDefaultViewportRect()'s own doc comment for why those can legitimately differ.
        if (renderer_)
            renderer_->SetViewport(physX, physY, physWidth, physHeight, 0.0f, 1.0f);

        // A real backbuffer-size change resets both rectangles to the complete new target. Keep
        // the no-size-change early return above so a normal Present() and a same-dimension reset
        // still preserve game-defined sub-viewports/scissors.
        setScissorRectangleProperty(Rectangle(0, 0, width, height));
    }

    void GraphicsDevice::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        if (renderer_ != nullptr)
        {
            renderer_->SetVirtualResolution(width, height);
        }

        // Commit public dimensions only after the renderer has accepted its complete replacement
        // set. A throwing resize therefore cannot make PresentationParameters report dimensions
        // that the live native surfaces do not have.
        virtualWidth_ = width;
        virtualHeight_ = height;
        presentationParameters_.setBackBufferWidthProperty(width);
        presentationParameters_.setBackBufferHeightProperty(height);

        UpdateViewportFromWindow();
    }

    void GraphicsDevice::SetPresentationMode(int mode)
    {
        if (renderer_ != nullptr)
        {
            renderer_->SetPresentationMode(mode);
        }
    }

    void GraphicsDevice::applySamplerStatesToRenderer()
    {
        if (!renderer_) return;
        for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
        {
            const SamplerState& ss = samplerStates_[i];
            renderer_->ApplySamplerState(i,
                (int)ss.getFilterProperty(),
                (int)ss.getAddressUProperty(),
                (int)ss.getAddressVProperty(),
                ss.getMaxAnisotropyProperty());
            renderer_->ApplySamplerMipState(i, ss.getMaxMipLevelProperty(),
                                           ss.getMipMapLevelOfDetailBiasProperty());
            renderer_->ApplySamplerAddressW(i, (int)ss.getAddressWProperty());
        }
    }

    void GraphicsDevice::applyPresentationParametersToWindow()
    {
        if (platformWindow_ == nullptr)
        {
            return;
        }

        // Task 902: fullscreen switching may not be available in headless / virtual-display
        // test environments (Xvfb). The PP value is already stored above this call, so a
        // renderer that cannot actually switch fullscreen still has the correct stored state --
        // matches GraphicsDeviceManager::applyToExistingRenderer()'s identical non-fatal handling
        // (Task 224), which this method now supersedes as the single fullscreen-application path.
        const bool fullScreen = presentationParameters_.getIsFullScreenProperty();
        platformWindow_->SetFullscreenMode(
            fullScreen ? CNA::Platform::WindowFullscreenMode::ExclusiveFullscreen
                       : CNA::Platform::WindowFullscreenMode::Windowed);

        const int width = presentationParameters_.getBackBufferWidthProperty();
        const int height = presentationParameters_.getBackBufferHeightProperty();
        if (width > 0 && height > 0)
        {
#ifndef __ANDROID__
            platformWindow_->SetSize(width, height);
            platformWindow_->Sync();

            // Window resizing is asynchronous on some windowing systems (e.g.
            // X11 without a window manager to negotiate geometry immediately, as under a bare
            // Xvfb) — logical/physical size queries can keep reporting the OLD
            // size for a short window afterward. UpdateViewportFromWindow() (called right after
            // this by every Reset()/ApplyChanges() path) needs the NEW size immediately: it feeds
            // renderer_->SetViewport(), and nothing re-issues that call later purely because the
            // physical size changed (INTERNAL_OnClientSizeChanged's own re-trigger is keyed off
            // the LOGICAL/virtual viewport size, which does not change across this resize) -- so a
            // stale read here would bake a wrong glViewport-equivalent in for the rest of the
            // session. The platform contract's Sync() provides that ordering point.
#endif
        }
    }

    // --- New XNA 4.0 API methods ---

    GraphicsDeviceStatus GraphicsDevice::getGraphicsDeviceStatusProperty() const
    {
        // plan_dx9.md D9-34: tracks the real renderer-reported status via deviceStatus_ (updated by
        // the deviceEventCallback lambda in createRenderer()). Every renderer except D3D9 never calls
        // that callback, so this stays Normal for them -- identical behavior to before this field
        // existed.
        return deviceStatus_;
    }

    DisplayMode GraphicsDevice::getDisplayModeProperty() const
    {
        if (presentationParameters_.getIsFullScreenProperty())
        {
            int w = 0, h = 0;
            if (renderer_) renderer_->GetViewportSize(w, h);
            return DisplayMode(w, h, SurfaceFormat::Color);
        }
        return getAdapterProperty().getCurrentDisplayModeProperty();
    }

    TextureCollection& GraphicsDevice::getTexturesProperty() { return textures_; }
    SamplerStateCollection& GraphicsDevice::getSamplerStatesProperty() { return samplerStates_; }
    TextureCollection& GraphicsDevice::getVertexTexturesProperty() { return vertexTextures_; }
    SamplerStateCollection& GraphicsDevice::getVertexSamplerStatesProperty() { return vertexSamplerStates_; }

    BlendState& GraphicsDevice::getBlendStateProperty() { return blendState_; }
    const BlendState& GraphicsDevice::getBlendStateProperty() const { return blendState_; }
    void GraphicsDevice::setBlendStateProperty(const BlendState& value)
    {
        if (renderer_)
        {
            // REMED-GFX-077: the four per-MRT colour write masks + the coverage sample mask travel
            // alongside the six blend factor/function ordinals (previously dropped here entirely).
            CNA::Internal::Renderers::BlendWriteState writeState;
            writeState.colorWriteChannels[0] = (int)value.getColorWriteChannelsProperty();
            writeState.colorWriteChannels[1] = (int)value.getColorWriteChannels1Property();
            writeState.colorWriteChannels[2] = (int)value.getColorWriteChannels2Property();
            writeState.colorWriteChannels[3] = (int)value.getColorWriteChannels3Property();
            writeState.multiSampleMask = static_cast<unsigned int>(value.getMultiSampleMaskProperty());
            renderer_->ApplyBlendState(
                (int)value.getColorSourceBlendProperty(),
                (int)value.getAlphaSourceBlendProperty(),
                (int)value.getColorDestinationBlendProperty(),
                (int)value.getAlphaDestinationBlendProperty(),
                (int)value.getColorBlendFunctionProperty(),
                (int)value.getAlphaBlendFunctionProperty(),
                writeState);
            renderer_->SetBlendFactor(
                value.getBlendFactorProperty().getRProperty() / 255.0f,
                value.getBlendFactorProperty().getGProperty() / 255.0f,
                value.getBlendFactorProperty().getBProperty() / 255.0f,
                value.getBlendFactorProperty().getAProperty() / 255.0f);
        }
        // Commit the public cache only after both native applications succeed: a failed D3D9
        // render-state update can otherwise leave the old GPU state active.
        blendState_ = value;
        blendFactor_ = value.getBlendFactorProperty();
    }

    DepthStencilState& GraphicsDevice::getDepthStencilStateProperty() { return depthStencilState_; }
    const DepthStencilState& GraphicsDevice::getDepthStencilStateProperty() const { return depthStencilState_; }
    void GraphicsDevice::setDepthStencilStateProperty(const DepthStencilState& value)
    {
        if (renderer_)
        {
            renderer_->ApplyDepthStencilState(
                value.getDepthBufferEnableProperty(),
                value.getDepthBufferWriteEnableProperty(),
                (int)value.getDepthBufferFunctionProperty(),
                value.getStencilEnableProperty(),
                (int)value.getStencilFunctionProperty(),
                (int)value.getStencilPassProperty(),
                (int)value.getStencilFailProperty(),
                (int)value.getStencilDepthBufferFailProperty(),
                value.getStencilMaskProperty(),
                value.getStencilWriteMaskProperty(),
                value.getReferenceStencilProperty(),
                value.getTwoSidedStencilModeProperty(),
                (int)value.getCounterClockwiseStencilFunctionProperty(),
                (int)value.getCounterClockwiseStencilPassProperty(),
                (int)value.getCounterClockwiseStencilFailProperty(),
                (int)value.getCounterClockwiseStencilDepthBufferFailProperty());
            renderer_->SetReferenceStencil(value.getReferenceStencilProperty());
        }
        // Commit only after both native operations succeed, so the public cache never describes a
        // depth/stencil configuration that a failed renderer update did not install.
        depthStencilState_ = value;
        referenceStencil_ = value.getReferenceStencilProperty();
    }

    RasterizerState& GraphicsDevice::getRasterizerStateProperty() { return rasterizerState_; }
    const RasterizerState& GraphicsDevice::getRasterizerStateProperty() const { return rasterizerState_; }
    void GraphicsDevice::setRasterizerStateProperty(const RasterizerState& value)
    {
        if (renderer_)
            renderer_->ApplyRasterizerState(
                (int)value.getCullModeProperty(),
                (int)value.getFillModeProperty(),
                value.getScissorTestEnableProperty(),
                value.getDepthBiasProperty(),
                value.getSlopeScaleDepthBiasProperty());
        rasterizerState_ = value;
    }

    Rectangle GraphicsDevice::getScissorRectangleProperty() const { return scissorRectangle_; }
    void GraphicsDevice::setScissorRectangleProperty(const Rectangle& value)
    {
        if (renderer_)
            renderer_->SetScissorRect(value.X, value.Y, value.Width, value.Height);
        scissorRectangle_ = value;
    }

    Color GraphicsDevice::getBlendFactorProperty() const { return blendFactor_; }
    void GraphicsDevice::setBlendFactorProperty(const Color& value)
    {
        blendFactor_ = value;
        if (renderer_)
            renderer_->SetBlendFactor(
                value.getRProperty() / 255.0f,
                value.getGProperty() / 255.0f,
                value.getBProperty() / 255.0f,
                value.getAProperty() / 255.0f);
    }

    int GraphicsDevice::getMultiSampleMaskProperty() const { return multiSampleMask_; }
    void GraphicsDevice::setMultiSampleMaskProperty(int value) { multiSampleMask_ = value; }

    int GraphicsDevice::getReferenceStencilProperty() const { return referenceStencil_; }
    void GraphicsDevice::setReferenceStencilProperty(int value)
    {
        if (renderer_)
            renderer_->SetReferenceStencil(value);
        // Match the other state setters: a renderer rejection must not leave the public cache
        // describing state that was never installed.
        referenceStencil_ = value;
    }

    void GraphicsDevice::Reset()
    {
        Reset(presentationParameters_, adapter_);
    }

    void GraphicsDevice::Reset(const PresentationParameters& presentationParameters)
    {
        Reset(presentationParameters, adapter_);
    }

    void GraphicsDevice::GetBackBufferData(Color* data, int elementCount)
    {
        GetBackBufferData(nullptr, data, 0, elementCount);
    }

    void GraphicsDevice::GetBackBufferData(Color* data, int startIndex, int elementCount)
    {
        GetBackBufferData(nullptr, data, startIndex, elementCount);
    }

    void GraphicsDevice::GetBackBufferData(const Rectangle* rect, Color* data, int startIndex, int elementCount)
    {
        if (data == nullptr)
            throw std::invalid_argument("data");

        // REMED-GFX-165: the read region is derived from the AUTHORITATIVE backbuffer description --
        // PresentationParameters -- never from the renderer's live viewport. A rectangle-less call
        // addresses the COMPLETE logical backbuffer (W x H pixels), independent of any sub-viewport,
        // scissor, previously bound render target or aspect-scaled/stale drawable size the renderer may
        // currently report. Previously this path sized itself from renderer_->GetViewportSize(), which
        // under the default FixedHeightDynamicWidth presentation mode is height-locked to the virtual
        // resolution and width-scaled by the window's aspect (and, before the window is realised,
        // derived from a stale physical size) -- so on WebGPU and the SDL GPU renderer a correctly sized W x H read
        // was rejected as "too small" while it was byte-exact on the renderers whose viewport happened
        // to equal the backbuffer.
        const int backBufferWidth = presentationParameters_.getBackBufferWidthProperty();
        const int backBufferHeight = presentationParameters_.getBackBufferHeightProperty();

        int x, y, w, h;
        if (rect)
        {
            x = rect->X;
            y = rect->Y;
            w = rect->Width;
            h = rect->Height;
            // A rectangle is validated against the REAL backbuffer bounds, before any native copy, so
            // an out-of-range request fails deterministically rather than reading outside the resource.
            if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
                x + w > backBufferWidth || y + h > backBufferHeight)
                throw std::out_of_range(
                    "GetBackBufferData: rectangle is outside the backbuffer bounds");
        }
        else
        {
            x = 0;
            y = 0;
            w = backBufferWidth;
            h = backBufferHeight;
        }

        if (const char* trace = std::getenv("CNA_BACKBUFFER_READ_TRACE"); trace != nullptr && *trace != '\0')
        {
            int viewportW = -1, viewportH = -1;
            if (renderer_ != nullptr)
                renderer_->GetViewportSize(viewportW, viewportH);
            std::fprintf(stderr,
                         "[GFX-165] GetBackBufferData rect=%d backbuffer=%dx%d viewport=%dx%d "
                         "region=(%d,%d,%dx%d) elementCount=%d startIndex=%d\n",
                         rect ? 1 : 0, backBufferWidth, backBufferHeight, viewportW, viewportH,
                         x, y, w, h, elementCount, startIndex);
            std::fflush(stderr);
        }

        if (elementCount < w * h)
            throw std::runtime_error("GetBackBufferData: data array too small for requested region");
        Texture::ValidateGetDataFormat(presentationParameters_.getBackBufferFormatProperty(), 4);

        // Color inherits a vtable pointer, so its first byte is NOT the R component.
        // Use a plain byte buffer for ReadBackbuffer, then unpack each RGBA group
        // into a Color(r, g, b, a) to avoid writing into the vtable pointer.
        const int pixelCount = w * h;
        std::vector<uint8_t> buf(static_cast<std::size_t>(pixelCount) * 4);
        renderer_->ReadBackbuffer(x, y, w, h, buf.data());
        for (int i = 0; i < pixelCount; ++i)
        {
            const uint8_t* p = buf.data() + i * 4;
            data[startIndex + i] = Color(p[0], p[1], p[2], p[3]);
        }
    }

    void GraphicsDevice::ResetViewportAndScissorForRenderTarget(int width, int height)
    {
        setViewportProperty(Viewport(0, 0, width, height));
        setScissorRectangleProperty(Rectangle(0, 0, width, height));
    }

    void GraphicsDevice::SetRenderTarget(RenderTarget2D* renderTarget)
    {
        // REMED-GFX-PGL-AUDIT: this single-target overload used to call renderer_->SetRenderTarget2D()
        // directly instead of routing through SetRenderTargets(). That skipped the null-renderer
        // check SetRenderTargets() already performs (RenderTarget2D::GetRenderTargetRenderer() is
        // null on a renderer that keeps IGraphicsRenderer's nullptr CreateRenderTarget2D() default,
        // e.g. Stub/PortableGL/OpenGLES1/OpenGL1): `renderTarget ? renderTarget->GetRenderTargetRenderer()
        // : nullptr` collapsed to the SAME nullptr the "unbind" call passes, so the renderer accepted
        // it as an ordinary restore-backbuffer request instead of refusing an unsupported binding --
        // while this method still recorded the target as bound and reset the viewport/scissor to its
        // size, so every draw after a `SetRenderTarget(rt)` on such a renderer silently landed in the
        // real backbuffer with no error at all. Delegating to SetRenderTargets() (already exercised by
        // the analogous cube overload below) reuses its existing NotSupportedException check and
        // keeps this a single code path to keep correct.
        if (renderTarget == nullptr)
        {
            SetRenderTargets({});
            return;
        }
        if (renderTarget->getIsDisposedProperty())
            throw System::ObjectDisposedException(renderTarget->getNameProperty());
        // SetRenderTargets() below already performs the DiscardContents clear (see its own
        // "Matches FNA" comment) for the target it just bound, using the same
        // HasRealDepthBuffer/HasRealStencilBuffer-derived ClearOptions this method used to compute
        // a second time here -- that duplicate block was removed so a DiscardContents target is
        // cleared exactly once, not twice.
        SetRenderTargets({RenderTargetBinding(renderTarget)});
    }

    void GraphicsDevice::SetRenderTarget(RenderTargetCube* renderTarget, CubeMapFace cubeMapFace)
    {
        // FNA/XNA parity: the singular cube overload is only a convenience wrapper around the
        // normalized binding path. Keeping one path guarantees identical face selection,
        // viewport/scissor reset, RenderTargetUsage handling, resolve, and public state.
        if (renderTarget == nullptr)
        {
            SetRenderTargets({});
            return;
        }
        SetRenderTargets({RenderTargetBinding(
            static_cast<Texture*>(renderTarget), cubeMapFace)});
    }

    void GraphicsDevice::SetRenderTargets(const std::vector<RenderTargetBinding>& renderTargets)
    {
        if (renderTargets.size() > MAX_RENDERTARGET_BINDINGS)
            throw std::invalid_argument("SetRenderTargets: at most " +
                std::to_string(MAX_RENDERTARGET_BINDINGS) + " render targets may be bound at once.");

        // D9-103 follow-up: GraphicsProfile.Reach's own MaxRenderTargets=1 ceiling (D9-100's own
        // table) -- a SEPARATE, lower, software-imposed limit from MAX_RENDERTARGET_BINDINGS
        // above (XNA's own general 4-target ceiling) and from D9-54's own hardware-cap
        // enforcement inside the renderer (NumSimultaneousRTs, which could be higher).
        // plan_runtimerenderer.md design decision 9: asked of the active renderer; renderers with
        // no profile distinction report no ceiling.
        if (renderer_ != nullptr)
        {
            const int profile = static_cast<int>(graphicsProfile_);
            const int maxForProfile = renderer_->GetMaxRenderTargetsForProfileEXT(profile);
            if (static_cast<int>(renderTargets.size()) > maxForProfile)
            {
                throw System::NotSupportedException(
                    "SetRenderTargets: " + std::to_string(renderTargets.size()) +
                    " render targets exceeds GraphicsProfile." +
                    (profile == 1 ? std::string("HiDef") : std::string("Reach")) +
                    "'s own maximum of " + std::to_string(maxForProfile));
            }
        }

        if (renderTargets.empty())
        {
            if (renderer_)
                renderer_->SetRenderTargets(nullptr, 0);
            // The renderer transition is checked/transactional on D3D9.  Do not claim the
            // backbuffer or its dimensions until that native restoration actually succeeds.
            currentRenderTargets_.clear();
            renderTargetBound_ = false;
            ResetViewportAndScissorForRenderTarget(presentationParameters_.getBackBufferWidthProperty(),
                                                    presentationParameters_.getBackBufferHeightProperty());
            return;
        }

        // REMED-GFX-096: normalize each public binding into one slot-aligned renderer-neutral
        // descriptor. The old vector<IRenderTargetRenderer*> dynamic-cast every entry only to
        // RenderTarget2D, turning every cube into nullptr and discarding its selected face.
        std::vector<RenderTargetBindingDescriptor> descriptors;
        descriptors.reserve(renderTargets.size());
        std::vector<IRenderTarget*> publicTargets;
        publicTargets.reserve(renderTargets.size());
        for (std::size_t i = 0; i < renderTargets.size(); ++i)
        {
            const auto& binding = renderTargets[i];
            Texture* texture = binding.getRenderTargetProperty();
            if (!texture)
                throw std::invalid_argument(
                    "SetRenderTargets: binding " + std::to_string(i)
                    + " has a null render target.");
            if (texture->getIsDisposedProperty())
                throw System::ObjectDisposedException(texture->getNameProperty());

            if (auto* rt2D = dynamic_cast<RenderTarget2D*>(texture))
            {
                if (binding.getArraySliceProperty() != 0)
                    throw System::NotSupportedException(
                        "SetRenderTargets: RenderTarget2D array slices are not supported; "
                        "arraySlice must be 0.");
                auto* targetRenderer = rt2D->GetRenderTargetRenderer();
                if (!targetRenderer)
                    throw System::NotSupportedException(
                        "SetRenderTargets: this renderer does not support RenderTarget2D.");
                descriptors.push_back(RenderTargetBindingDescriptor::ForRenderTarget2D(
                    targetRenderer, 0, rt2D->getWidthProperty(), rt2D->getHeightProperty(),
                    rt2D->getMultiSampleCountProperty()));
                publicTargets.push_back(rt2D);
            }
            else if (auto* cube = dynamic_cast<RenderTargetCube*>(texture))
            {
                const int face = static_cast<int>(binding.getCubeMapFaceProperty());
                if (face < static_cast<int>(CubeMapFace::PositiveX)
                    || face > static_cast<int>(CubeMapFace::NegativeZ))
                    throw System::ArgumentOutOfRangeException("cubeMapFace");
                auto* targetRenderer = cube->GetRenderTargetCubeRenderer();
                if (!targetRenderer)
                    throw System::NotSupportedException(
                        "SetRenderTargets: this renderer does not support RenderTargetCube.");
                descriptors.push_back(
                    RenderTargetBindingDescriptor::ForRenderTargetCubeFace(
                        targetRenderer, face, cube->getWidthProperty(),
                        cube->getMultiSampleCountProperty()));
                publicTargets.push_back(cube);
            }
            else
            {
                throw std::invalid_argument(
                    "SetRenderTargets: binding " + std::to_string(i)
                    + " must contain a RenderTarget2D or RenderTargetCube.");
            }
        }

        // XNA/FNA/native MRT compatibility is subresource-based. Different faces of one cube are
        // distinct and may be bound together; the identical face (or 2D slice) may not occupy two
        // slots. Dimensions and real device-applied sample counts must agree.
        for (std::size_t i = 1; i < descriptors.size(); ++i)
        {
            if (descriptors[i].GetWidth() != descriptors[0].GetWidth()
                || descriptors[i].GetHeight() != descriptors[0].GetHeight())
                throw std::runtime_error(
                    "SetRenderTargets: render targets must have matching dimensions.");
            if (descriptors[i].GetAppliedMultiSampleCount()
                != descriptors[0].GetAppliedMultiSampleCount())
                throw std::runtime_error(
                    "SetRenderTargets: render targets must have matching applied sample counts.");
            for (std::size_t previous = 0; previous < i; ++previous)
                if (descriptors[i].IsSameSubresource(descriptors[previous]))
                    throw std::runtime_error(
                        "SetRenderTargets: the same render-target subresource cannot be bound "
                        "to more than one slot.");
        }

        if (renderer_)
            renderer_->SetRenderTargets(
                descriptors.data(), static_cast<int>(descriptors.size()));

        currentRenderTargets_ = renderTargets;
        renderTargetBound_ = true;
        IRenderTarget* first = publicTargets[0];
        // Matches FNA: Viewport/ScissorRectangle reset to the FIRST bound target's size.
        ResetViewportAndScissorForRenderTarget(
            first->getWidthProperty(), first->getHeightProperty());
        if (first->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents)
        {
            // See SetRenderTarget(RenderTarget2D*)'s independent attachment rationale.
            const DepthFormat depthFormat = first->getDepthStencilFormatProperty();
            const bool depthFormatRequested = depthFormat != DepthFormat::None;
            const bool stencilFormatRequested = depthFormat == DepthFormat::Depth24Stencil8;
            const auto& firstDescriptor = descriptors[0];
            const bool hasDepthBuffer = firstDescriptor.IsRenderTarget2D()
                ? firstDescriptor.GetRenderTarget2D()->HasRealDepthBuffer(depthFormatRequested)
                : firstDescriptor.GetRenderTargetCube()->HasRealDepthBuffer(
                    depthFormatRequested);
            const bool hasStencilBuffer = firstDescriptor.IsRenderTarget2D()
                ? firstDescriptor.GetRenderTarget2D()->HasRealStencilBuffer(stencilFormatRequested)
                : firstDescriptor.GetRenderTargetCube()->HasRealStencilBuffer(
                    stencilFormatRequested);
            // REMED-GFX-142: see the singular overload's identical Stencil rationale.
            ClearOptions discardOptions = ClearOptions::Target;
            if (hasDepthBuffer)
                discardOptions |= ClearOptions::DepthBuffer;
            if (hasStencilBuffer)
                discardOptions |= ClearOptions::Stencil;
            Clear(discardOptions, Color(0, 0, 0, 255), 1.0f, 0);
        }
    }

    std::vector<RenderTargetBinding> GraphicsDevice::GetRenderTargets() const
    {
        return currentRenderTargets_;
    }

    void GraphicsDevice::SetVertexBuffer(const VertexBuffer* vertexBuffer, int vertexOffset)
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(vertexOffset, "vertexOffset");
        if (vertexBuffer && vertexBuffer->getIsDisposedProperty())
            throw System::ObjectDisposedException(vertexBuffer->getNameProperty());

        currentVertexBuffer_ = vertexBuffer;
        currentVertexBuffers_.clear();
        if (vertexBuffer != nullptr)
        {
            currentVertexBuffers_.emplace_back(
                const_cast<VertexBuffer*>(vertexBuffer), vertexOffset, 0);
        }
    }

    void GraphicsDevice::SetVertexBuffers(const std::vector<VertexBufferBinding>& vertexBuffers)
    {
        constexpr int kMaxVertexBufferBindings = 16; // XNA4 HiDef spec limit
        if (static_cast<int>(vertexBuffers.size()) > kMaxVertexBufferBindings)
            throw System::ArgumentOutOfRangeException(
                "vertexBuffers",
                std::to_string(vertexBuffers.size()),
                "Max Vertex Buffers supported is " + std::to_string(kMaxVertexBufferBindings));

        // A null-buffer binding is a legal unused slot in XNA -- FNA itself stores
        // VertexBufferBinding.None entries -- so only the binding count is validated here;
        // the draw dispatch already skips defaulted bindings.
        currentVertexBuffers_ = vertexBuffers;
        currentVertexBuffer_ = vertexBuffers.empty()
            ? nullptr
            : vertexBuffers[0].getVertexBufferProperty();
    }

    std::vector<VertexBufferBinding> GraphicsDevice::GetVertexBuffers() const
    {
        return currentVertexBuffers_;
    }
}
