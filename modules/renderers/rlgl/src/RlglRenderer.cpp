// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "CNA/Logger.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglThreadContextLeaseControl
    {
    public:
        explicit RlglThreadContextLeaseControl(
            std::shared_ptr<PlatformGlContextOwner> platformContext)
            : platformContext(std::move(platformContext))
        {
        }

        std::shared_ptr<PlatformGlContextOwner> platformContext;
        std::recursive_mutex mutex;
    };

    namespace
    {
        constexpr const char* kRendererName = "RLGL";

        std::mutex lifecycleMutex;
        bool lifecycleActive = false;

        CNA::Platform::GlContextDescription RequestedContext(const bool robustAccess)
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 3;
            description.minorVersion = 3;
            description.profile = CNA::Platform::GlProfile::Core;
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            description.robustAccess = robustAccess;
            description.loseContextOnReset = robustAccess;
            return description;
        }

        void ClaimLifecycle()
        {
            const std::scoped_lock lock(lifecycleMutex);
            if (lifecycleActive)
            {
                throw std::runtime_error(
                    "RLGL: rlgl 6.0 keeps process-global renderer state; only one live RLGL "
                    "GraphicsDevice is supported");
            }
            lifecycleActive = true;
        }

        void ReleaseLifecycle()
        {
            const std::scoped_lock lock(lifecycleMutex);
            lifecycleActive = false;
        }

        void ThrowInjectedLifecycleFailure(
            const char* const variable, const char* const operation,
            const char* const stage)
        {
            const char* const requested = std::getenv(variable);
            if (requested != nullptr && std::string_view(requested) == stage)
            {
                throw std::runtime_error(
                    std::string("RLGL: injected ") + operation +
                    " failure at stage '" + stage + "'");
            }
        }

        void ThrowInjectedCreateFailure(const char* const stage)
        {
            ThrowInjectedLifecycleFailure(
                "CNA_RLGL_DEBUG_FAIL_CREATE_STAGE", "renderer creation", stage);
        }

        void ThrowInjectedRecreateFailure(const char* const stage)
        {
            ThrowInjectedLifecycleFailure(
                "CNA_RLGL_DEBUG_FAIL_RECREATE_STAGE", "context recreation", stage);
        }

        void ThrowInjectedBindFailure(const char* const stage)
        {
            ThrowInjectedLifecycleFailure(
                "CNA_RLGL_DEBUG_FAIL_BIND_STAGE", "context binding", stage);
        }

        [[nodiscard]] int AppliedBackBufferFormat(
            const CNA::Platform::GlContextDescription& granted)
        {
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            if (granted.redBits == 10 && granted.greenBits == 10 &&
                granted.blueBits == 10 && granted.alphaBits == 2)
            {
                return static_cast<int>(SurfaceFormat::Rgba1010102);
            }
            if (granted.redBits >= 8 && granted.greenBits >= 8 &&
                granted.blueBits >= 8 && granted.alphaBits >= 8)
            {
                return static_cast<int>(SurfaceFormat::Color);
            }
            if (granted.redBits == 5 && granted.greenBits == 6 &&
                granted.blueBits == 5 && granted.alphaBits == 0)
            {
                return static_cast<int>(SurfaceFormat::Bgr565);
            }
            if (granted.redBits == 5 && granted.greenBits == 5 &&
                granted.blueBits == 5 && granted.alphaBits == 1)
            {
                return static_cast<int>(SurfaceFormat::Bgra5551);
            }
            if (granted.redBits == 4 && granted.greenBits == 4 &&
                granted.blueBits == 4 && granted.alphaBits == 4)
            {
                return static_cast<int>(SurfaceFormat::Bgra4444);
            }
            throw std::runtime_error(
                "RLGL: platform granted a default framebuffer format that has no exact "
                "classic XNA SurfaceFormat mapping");
        }

        [[nodiscard]] int AppliedDepthStencilFormat(
            const CNA::Platform::GlContextDescription& granted)
        {
            using Microsoft::Xna::Framework::Graphics::DepthFormat;
            if (granted.depthBits <= 0) return static_cast<int>(DepthFormat::None);
            if (granted.stencilBits > 0)
                return static_cast<int>(DepthFormat::Depth24Stencil8);
            if (granted.depthBits >= 24)
                return static_cast<int>(DepthFormat::Depth24);
            return static_cast<int>(DepthFormat::Depth16);
        }

        [[nodiscard]] const char* GraphicsResetStatusName(
            const Bridge::GraphicsResetStatus status)
        {
            switch (status)
            {
            case Bridge::GraphicsResetStatus::NoError: return "no error";
            case Bridge::GraphicsResetStatus::Guilty: return "guilty context reset";
            case Bridge::GraphicsResetStatus::Innocent: return "innocent context reset";
            case Bridge::GraphicsResetStatus::Unknown: return "unknown context reset";
            }
            return "unknown context reset";
        }

        [[noreturn]] void Unsupported(const char* operation, const char* task)
        {
            throw System::NotSupportedException(
                std::string(kRendererName) + ": " + operation +
                " is not implemented yet (plans/plan_rlgl.md " + task + ")");
        }

        class RlglThreadContextLease final : public IRendererThreadContextLease
        {
        public:
            explicit RlglThreadContextLease(std::function<void()> release)
                : release_(std::move(release))
            {
            }

            ~RlglThreadContextLease() override
            {
                release_();
            }

        private:
            std::function<void()> release_;
        };

        struct RlglThreadContextLeaseState
        {
            std::size_t depth = 0;
            CNA::Platform::GlContextBinding previousBinding;
            RendererThreadContextLeaseRelease release =
                RendererThreadContextLeaseRelease::RestorePreviousBinding;
        };

        std::unordered_map<const RlglThreadContextLeaseControl*, RlglThreadContextLeaseState>&
        ThreadContextLeaseStates()
        {
            static thread_local std::unordered_map<
                const RlglThreadContextLeaseControl*, RlglThreadContextLeaseState> states;
            return states;
        }

        void RetargetThreadContextLeaseBinding(
            const RlglThreadContextLeaseControl& control,
            const CNA::Platform::GlContextHandle oldContext,
            const CNA::Platform::GlContextBinding& replacement)
        {
            auto& states = ThreadContextLeaseStates();
            const auto state = states.find(&control);
            if (state != states.end() && state->second.previousBinding.context == oldContext)
                state->second.previousBinding = replacement;
        }

        [[nodiscard]] bool HasActiveThreadContextLease(
            const RlglThreadContextLeaseControl& control)
        {
            const auto& states = ThreadContextLeaseStates();
            const auto state = states.find(&control);
            return state != states.end() && state->second.depth > 0;
        }

        void ReleaseThreadContextLease(
            const std::shared_ptr<RlglThreadContextLeaseControl>& control) noexcept
        {
            auto& states = ThreadContextLeaseStates();
            const auto it = states.find(control.get());
            if (it == states.end() || it->second.depth == 0)
            {
                CNA::Logger::Error(
                    "RLGL renderer context lease released without matching acquisition",
                    CNA::LogCategory::RENDER);
                return;
            }

            --it->second.depth;
            if (it->second.depth == 0)
            {
                try
                {
                    control->platformContext->RestoreBinding(
                        it->second.previousBinding, it->second.release);
                }
                catch (const std::exception& error)
                {
                    CNA::Logger::Error(
                        std::string("Failed to release RLGL context ownership: ") + error.what(),
                        CNA::LogCategory::RENDER);
                }
                states.erase(it);
            }
            control->mutex.unlock();
        }
    }

    RlglResourceLifetime::RlglResourceLifetime(
        const std::shared_ptr<RlglThreadContextLeaseControl>& contextControl)
        : contextControl_(contextControl)
    {
    }

    bool RlglResourceLifetime::Register(IRlglNativeResource& resource)
    {
        const auto control = contextControl_.lock();
        if (!control)
            throw std::runtime_error("RLGL: renderer resource lifetime is no longer available");

        const std::scoped_lock lock(control->mutex);
        if (!active_.load(std::memory_order_acquire))
            throw std::runtime_error("RLGL: renderer resource lifetime is shutting down");
        const bool recoveryEnabled = recoveryEnabled_.load(std::memory_order_relaxed);
        resources_.push_back(&resource);
        try
        {
            if (recoveryEnabled) recoveryResources_.push_back(&resource);
        }
        catch (...)
        {
            resources_.pop_back();
            throw;
        }
        registeredResources_.fetch_add(1, std::memory_order_relaxed);
        if (recoveryEnabled)
        {
            registeredRecoveryResources_.fetch_add(1, std::memory_order_relaxed);
        }
        return recoveryEnabled;
    }

    void RlglResourceLifetime::SetRecoveryEnabled(const bool enabled)
    {
        const auto control = contextControl_.lock();
        if (!control)
        {
            recoveryEnabled_.store(enabled, std::memory_order_release);
            return;
        }

        const std::scoped_lock lock(control->mutex);
        recoveryEnabled_.store(enabled, std::memory_order_release);
    }

    void RlglResourceLifetime::InvalidateNativeResourcesForContextLoss() noexcept
    {
        const auto control = contextControl_.lock();
        if (!control) return;

        const std::scoped_lock lock(control->mutex);
        if (!active_.load(std::memory_order_acquire)) return;
        for (auto resource = resources_.rbegin(); resource != resources_.rend(); ++resource)
            (*resource)->InvalidateNativeResource();
        contextLossInvalidations_.fetch_add(1, std::memory_order_relaxed);
    }

    void RlglResourceLifetime::RestoreNativeResourcesAfterContextRecreation()
    {
        const auto control = contextControl_.lock();
        if (!control)
            throw std::runtime_error("RLGL: renderer resource lifetime is no longer available");

        const std::scoped_lock lock(control->mutex);
        if (!active_.load(std::memory_order_acquire))
            throw std::runtime_error("RLGL: renderer resource lifetime is shutting down");

        std::vector<IRlglNativeResource*> restored;
        restored.reserve(recoveryResources_.size());
        try
        {
            for (std::size_t index = 0; index < recoveryResources_.size(); ++index)
            {
                if (const char* const requested =
                        std::getenv("CNA_RLGL_DEBUG_FAIL_RESOURCE_RESTORE_AT"))
                {
                    char* end = nullptr;
                    const unsigned long failAt = std::strtoul(requested, &end, 10);
                    if (end != requested && *end == '\0' && failAt == index)
                    {
                        throw std::runtime_error(
                            "RLGL: injected resource restoration failure at index " +
                            std::to_string(index));
                    }
                }
                IRlglNativeResource* const resource = recoveryResources_[index];
                restored.push_back(resource);
                resource->RecreateNativeResource();
            }
        }
        catch (...)
        {
            for (auto resource = restored.rbegin(); resource != restored.rend(); ++resource)
                (*resource)->ReleaseNativeResource();
            failedResourceRestorations_.fetch_add(1, std::memory_order_relaxed);
            throw;
        }
        resourceRestorations_.fetch_add(1, std::memory_order_relaxed);
    }

    void RlglResourceLifetime::ReleaseNativeResourcesForRecoveryRollback() noexcept
    {
        const auto control = contextControl_.lock();
        if (!control) return;

        const std::scoped_lock lock(control->mutex);
        if (!active_.load(std::memory_order_acquire)) return;
        for (auto resource = recoveryResources_.rbegin();
             resource != recoveryResources_.rend(); ++resource)
        {
            (*resource)->ReleaseNativeResource();
        }
    }

    void RlglResourceLifetime::Dispose(IRlglNativeResource& resource) noexcept
    {
        const auto control = contextControl_.lock();
        if (!control)
        {
            lateDisposals_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const std::scoped_lock lock(control->mutex);
        const auto found = std::find(resources_.begin(), resources_.end(), &resource);
        if (found == resources_.end())
        {
            if (!active_.load(std::memory_order_acquire))
                lateDisposals_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        resources_.erase(found);
        registeredResources_.fetch_sub(1, std::memory_order_relaxed);
        const auto recovery =
            std::find(recoveryResources_.begin(), recoveryResources_.end(), &resource);
        if (recovery != recoveryResources_.end())
        {
            recoveryResources_.erase(recovery);
            registeredRecoveryResources_.fetch_sub(1, std::memory_order_relaxed);
        }

        if (!active_.load(std::memory_order_acquire))
        {
            lateDisposals_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        try
        {
            const auto previousBinding = control->platformContext->GetCurrentBinding();
            control->platformContext->MakeCurrent();
            resource.ReleaseNativeResource();
            releasedResources_.fetch_add(1, std::memory_order_relaxed);
            control->platformContext->RestoreBinding(
                previousBinding,
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
        }
        catch (const std::exception& error)
        {
            CNA::Logger::Error(
                std::string("RLGL: failed to release a native resource: ") + error.what(),
                CNA::LogCategory::RENDER);
        }
    }

    void RlglResourceLifetime::Shutdown() noexcept
    {
        const auto control = contextControl_.lock();
        if (!control)
        {
            active_.store(false, std::memory_order_release);
            resources_.clear();
            recoveryResources_.clear();
            registeredResources_.store(0, std::memory_order_relaxed);
            registeredRecoveryResources_.store(0, std::memory_order_relaxed);
            return;
        }

        const std::scoped_lock lock(control->mutex);
        if (!active_.exchange(false, std::memory_order_acq_rel)) return;

        try
        {
            const auto previousBinding = control->platformContext->GetCurrentBinding();
            control->platformContext->MakeCurrent();
            for (auto resource = resources_.rbegin(); resource != resources_.rend(); ++resource)
            {
                (*resource)->ReleaseNativeResource();
                releasedResources_.fetch_add(1, std::memory_order_relaxed);
            }
            control->platformContext->RestoreBinding(
                previousBinding,
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
        }
        catch (const std::exception& error)
        {
            CNA::Logger::Error(
                std::string("RLGL: failed to release native children during shutdown: ") +
                    error.what(),
                CNA::LogCategory::RENDER);
        }
        resources_.clear();
        recoveryResources_.clear();
        registeredResources_.store(0, std::memory_order_relaxed);
        registeredRecoveryResources_.store(0, std::memory_order_relaxed);
    }

    RlglResourceLifetimeSnapshot
    RlglResourceLifetime::GetSnapshotForTesting() const noexcept
    {
        RlglResourceLifetimeSnapshot snapshot;
        const auto readCounters = [&snapshot, this]()
        {
            snapshot.registeredResources =
                registeredResources_.load(std::memory_order_relaxed);
            snapshot.releasedResources =
                releasedResources_.load(std::memory_order_relaxed);
            snapshot.lateDisposals = lateDisposals_.load(std::memory_order_relaxed);
            snapshot.active = active_.load(std::memory_order_acquire);
            snapshot.recoveryResources =
                registeredRecoveryResources_.load(std::memory_order_relaxed);
            snapshot.contextLossInvalidations =
                contextLossInvalidations_.load(std::memory_order_relaxed);
            snapshot.resourceRestorations =
                resourceRestorations_.load(std::memory_order_relaxed);
            snapshot.failedResourceRestorations =
                failedResourceRestorations_.load(std::memory_order_relaxed);
            snapshot.recoveryEnabledForNewResources =
                recoveryEnabled_.load(std::memory_order_acquire);
        };

        const auto control = contextControl_.lock();
        if (!control)
        {
            readCounters();
            return snapshot;
        }

        const std::scoped_lock lock(control->mutex);
        readCounters();
        for (const IRlglNativeResource* const resource : recoveryResources_)
        {
            const RlglResourceRecoveryInfo info = resource->GetRecoveryInfo();
            snapshot.retainedCpuBytes += info.retainedCpuBytes;
            snapshot.definedTextureSubresources += info.definedTextureSubresources;
            if (info.contentLostOnReset) ++snapshot.contentLostResources;
            else ++snapshot.restorableResources;
        }
        return snapshot;
    }

    RlglRenderer::RlglRenderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface)
        , platformGlService_(&RequirePlatformGlContext(args.glContext, kRendererName))
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , swapInterval_(args.swapInterval)
        , requestedMultiSampleCount_(args.multiSampleCount)
        , deviceEventCallback_(args.deviceEventCallback)
    {
        RequirePlatformGlWindow(args.surface, kRendererName);
        ClaimLifecycle();
        lifecycleClaimed_ = true;

        try
        {
            ThrowInjectedCreateFailure("before-context");
            CreateContext(args.multiSampleCount);
            ThrowInjectedCreateFailure("after-context");
            const std::string version = InitializeContextState();
            ThrowInjectedCreateFailure("after-bridge");
            contextGeneration_ = 1;
            threadContextLeaseControl_ =
                std::make_shared<RlglThreadContextLeaseControl>(platformContext_);
            resourceLifetime_ =
                std::make_shared<RlglResourceLifetime>(threadContextLeaseControl_);
            resourceLifetime_->SetRecoveryEnabled(args.contextRecoveryEnabled);
            ThrowInjectedCreateFailure("after-registry");
            IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
            registered_ = true;

            CNA::Logger::Info(
                "RLGL renderer initialized standalone rlgl 6.0 with OpenGL " + version,
                CNA::LogCategory::RENDER);
        }
        catch (...)
        {
            if (rlglInitialized_)
            {
                DestroyRendererNativeState();
                Bridge::Shutdown();
                rlglInitialized_ = false;
            }
            if (resourceLifetime_) resourceLifetime_->Shutdown();
            resourceLifetime_.reset();
            threadContextLeaseControl_.reset();
            platformContext_.reset();
            ReleaseLifecycle();
            lifecycleClaimed_ = false;
            throw;
        }
    }

    RlglRenderer::~RlglRenderer()
    {
        const auto leaseControl = threadContextLeaseControl_;
        std::unique_lock<std::recursive_mutex> operationLock;
        if (leaseControl)
            operationLock = std::unique_lock<std::recursive_mutex>(leaseControl->mutex);

        if (registered_)
        {
            IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());
            registered_ = false;
        }

        if (resourceLifetime_) resourceLifetime_->Shutdown();

        if (rlglInitialized_ && platformContext_)
        {
            try
            {
                platformContext_->MakeCurrent();
                DestroyRendererNativeState();
                Bridge::Shutdown();
            }
            catch (const std::exception& error)
            {
                CNA::Logger::Error(
                    std::string("RLGL: failed to make the context current during shutdown: ") +
                        error.what(),
                    CNA::LogCategory::RENDER);
            }
            rlglInitialized_ = false;
        }

        resourceLifetime_.reset();
        threadContextLeaseControl_.reset();
        platformContext_.reset();
        if (lifecycleClaimed_)
        {
            ReleaseLifecycle();
            lifecycleClaimed_ = false;
        }
    }

    void RlglRenderer::CreateContext(const int requestedMultiSampleCount)
    {
        requestedMultiSampleCount_ = requestedMultiSampleCount;
        const std::array<bool, 2> attempts{{true, false}};
        std::exception_ptr lastFailure;
        for (std::size_t attempt = 0; attempt < attempts.size(); ++attempt)
        {
            try
            {
                platformContext_ = std::make_shared<PlatformGlContextOwner>(
                    *platformGlService_, surface_.GetWindowId(),
                    RequestedContext(attempts[attempt]));
                break;
            }
            catch (const CNA::Platform::PlatformException&)
            {
                lastFailure = std::current_exception();
            }
        }
        if (!platformContext_)
        {
            if (lastFailure) std::rethrow_exception(lastFailure);
            throw std::runtime_error("RLGL: no OpenGL context creation attempt was made");
        }

        RefreshContextAttributes();
    }

    void RlglRenderer::RefreshContextAttributes()
    {
        const auto granted = platformContext_->GetAttributes();
        if (granted.profile != CNA::Platform::GlProfile::Core ||
            granted.majorVersion < 3 ||
            (granted.majorVersion == 3 && granted.minorVersion < 3))
        {
            throw std::runtime_error(
                "RLGL: platform did not grant the required OpenGL 3.3 core context");
        }

        depthBits_ = granted.depthBits;
        stencilBits_ = granted.stencilBits;
        backBufferFormat_ = AppliedBackBufferFormat(granted);
        depthStencilFormat_ = AppliedDepthStencilFormat(granted);
        robustContext_ = granted.robustAccess && granted.loseContextOnReset;
    }

    std::string RlglRenderer::InitializeContextState()
    {
        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        const std::string version = Bridge::Initialize(
            platformContext_->GetLoader(), width, height);
        rlglInitialized_ = true;
        maxTextureSize_ = Bridge::GetMaxTextureSize();
        maxSamplerSlots_ = Bridge::GetMaxSamplerSlots();
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        maxVertexSamplerSlots_ = Bridge::GetMaxCompiledEffectVertexSamplerSlots();
#endif
        maxRenderTargets_ = Bridge::GetMaxRenderTargets();
        maxRenderTargetSamples_ = Bridge::GetMaxRenderTargetSamples();
        maxSamplerAnisotropy_ = Bridge::GetMaxSamplerAnisotropy();
        RecreateBackbufferStorage(width, height);
        nativeLossPollingAvailable_ =
            robustContext_ && Bridge::SupportsGraphicsResetStatus();
        if (maxSamplerSlots_ < static_cast<int>(samplers_.size()))
        {
            throw std::runtime_error(
                "RLGL: the OpenGL context exposes fewer than 16 fragment texture units");
        }
        platformContext_->SetSwapInterval(swapInterval_);
        return version;
    }

    void RlglRenderer::InvalidateRendererNativeState() noexcept
    {
        for (SamplerRecord& sampler : samplers_)
        {
            sampler.realized = sampler.realized || sampler.id != 0;
            sampler.id = 0;
        }
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        for (SamplerRecord& sampler : vertexSamplers_)
        {
            sampler.realized = sampler.realized || sampler.id != 0;
            sampler.id = 0;
        }
#endif
        restorePrimitivePipeline_ = restorePrimitivePipeline_ || primitivePipeline_ != nullptr;
        primitivePipeline_.reset();
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        restoreCompiledDrawResources_ =
            restoreCompiledDrawResources_ || compiledEffectDrawResources_ != nullptr;
        compiledEffectDrawResources_.reset();
        restoreMojoShaderContext_ = restoreMojoShaderContext_ || mojoShaderContext_ != nullptr;
        mojoShaderContext_ = nullptr;
#endif
        mrtFramebuffer_ = 0;
        backbufferStorage_.reset();
        Bridge::AbandonLostContext();
        rlglInitialized_ = false;
    }

    void RlglRenderer::DestroyRendererNativeState() noexcept
    {
        if (primitivePipeline_)
        {
            Bridge::DestroyPrimitivePipeline(*primitivePipeline_);
            primitivePipeline_.reset();
        }
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        if (compiledEffectDrawResources_)
        {
            Bridge::DestroyCompiledEffectDrawResources(*compiledEffectDrawResources_);
            compiledEffectDrawResources_.reset();
        }
        DestroyCompiledEffectContext();
#endif
        Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
        if (backbufferStorage_)
            Bridge::DestroyRenderTarget2D(*backbufferStorage_);
        backbufferStorage_.reset();
        std::array<unsigned int, 16> samplerIds{};
        for (std::size_t index = 0; index < samplers_.size(); ++index)
        {
            samplerIds[index] = samplers_[index].id;
            samplers_[index].id = 0;
        }
        Bridge::DestroySamplers(samplerIds.data(), samplerIds.size());
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        std::array<unsigned int, 4> vertexSamplerIds{};
        for (std::size_t index = 0; index < vertexSamplers_.size(); ++index)
        {
            vertexSamplerIds[index] = vertexSamplers_[index].id;
            vertexSamplers_[index].id = 0;
        }
        Bridge::DestroySamplers(vertexSamplerIds.data(), vertexSamplerIds.size());
#endif
    }

    void RlglRenderer::RestoreRendererNativeState()
    {
        for (std::size_t index = 0; index < samplers_.size(); ++index)
        {
            SamplerRecord& sampler = samplers_[index];
            if (!sampler.realized) continue;
            sampler.id = Bridge::CreateSampler();
            ApplySamplerRecord(static_cast<int>(index), sampler);
        }
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        for (std::size_t index = 0; index < vertexSamplers_.size(); ++index)
        {
            SamplerRecord& sampler = vertexSamplers_[index];
            if (!sampler.realized) continue;
            if (index >= static_cast<std::size_t>(maxVertexSamplerSlots_))
            {
                throw System::NotSupportedException(
                    "RLGL context recovery: the replacement GL context exposes fewer "
                    "compiled-effect vertex sampler slots");
            }
            sampler.id = Bridge::CreateSampler();
            ApplyCompiledEffectVertexSamplerRecord(static_cast<int>(index), sampler);
        }
#endif
        if (restorePrimitivePipeline_)
        {
            primitivePipeline_ = std::make_unique<Bridge::PrimitivePipeline>(
                Bridge::CreatePrimitivePipeline());
        }
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        if (restoreMojoShaderContext_) (void)GetMojoShaderContext();
        if (restoreCompiledDrawResources_)
        {
            compiledEffectDrawResources_ =
                std::make_unique<Bridge::CompiledEffectDrawResources>(
                    Bridge::CreateCompiledEffectDrawResources());
        }
#endif
    }

    void RlglRenderer::RestoreCurrentRenderTargets()
    {
        if (currentRenderTargetCount_ == 0)
        {
            BindBackbuffer();
            return;
        }

        if (currentRenderTargetCount_ == 1)
        {
            if (currentRenderTargets_[0] != nullptr)
                currentRenderTargets_[0]->BindAsRenderTarget();
            else if (currentRenderTargetCubes_[0] != nullptr)
                currentRenderTargetCubes_[0]->BindAsRenderTargetFace(
                    currentRenderTargetCubeFaces_[0]);
            else
                throw std::runtime_error(
                    "RLGL: active render target was unavailable after context recreation");
            return;
        }

        std::array<Bridge::MrtAttachment, 4> attachments{};
        for (int slot = 0; slot < currentRenderTargetCount_; ++slot)
        {
            if (currentRenderTargets_[slot] != nullptr)
            {
                const RenderTargetResourceSnapshot snapshot =
                    GetRenderTargetResourceSnapshotForTesting(
                        *currentRenderTargets_[slot]);
                attachments[slot] = {
                    snapshot.colorTexture,
                    snapshot.multisampleColorRenderbuffer,
                    snapshot.depthStencilRenderbuffer,
                    100,
                    snapshot.depthFormat,
                    snapshot.multiSampleCount};
            }
            else if (currentRenderTargetCubes_[slot] != nullptr)
            {
                const int face = currentRenderTargetCubeFaces_[slot];
                const RenderTargetCubeResourceSnapshot snapshot =
                    GetRenderTargetCubeResourceSnapshotForTesting(
                        *currentRenderTargetCubes_[slot]);
                attachments[slot] = {
                    snapshot.colorTexture,
                    snapshot.multisampleColorRenderbuffers[
                        static_cast<std::size_t>(face)],
                    snapshot.depthStencilRenderbuffer,
                    face,
                    snapshot.depthFormat,
                    snapshot.multiSampleCount};
            }
            else
            {
                throw std::runtime_error(
                    "RLGL: active MRT attachment was unavailable after context recreation");
            }
        }

        unsigned int candidate = Bridge::CreateMrtFramebuffer(
            attachments.data(), currentRenderTargetCount_);
        try
        {
            Bridge::BindFramebuffer(candidate);
            mrtFramebuffer_ = candidate;
            candidate = 0;
        }
        catch (...)
        {
            Bridge::DestroyMrtFramebuffer(candidate);
            throw;
        }
    }

    void RlglRenderer::ReapplyDeviceState()
    {
        if (blend_.stateApplied)
        {
            Bridge::ApplyBlendState(
                blend_.colorSource, blend_.alphaSource,
                blend_.colorDestination, blend_.alphaDestination,
                blend_.colorFunction, blend_.alphaFunction,
                blend_.writeState.colorWriteChannels,
                blend_.writeState.multiSampleMask);
        }
        if (blend_.factorApplied)
        {
            Bridge::SetBlendFactor(
                blend_.factor[0], blend_.factor[1],
                blend_.factor[2], blend_.factor[3]);
        }
        if (blend_.enabledApplied) Bridge::SetBlendEnabled(blend_.enabled);

        if (stencil_.stateApplied)
        {
            Bridge::ApplyDepthStencilState(
                stencil_.depthEnable, stencil_.depthWriteEnable,
                stencil_.depthFunction, stencil_.enabled, stencil_.function,
                stencil_.pass, stencil_.fail, stencil_.depthFail,
                stencil_.readMask, stencil_.writeMask, stencil_.reference,
                stencil_.twoSided, stencil_.counterClockwiseFunction,
                stencil_.counterClockwisePass, stencil_.counterClockwiseFail,
                stencil_.counterClockwiseDepthFail);
        }
        else
        {
            if (stencil_.depthEnableAssigned)
                Bridge::SetDepthTestEnabled(stencil_.depthEnable);
            if (stencil_.depthWriteAssigned)
                Bridge::SetDepthWriteEnabled(stencil_.depthWriteEnable);
        }
        if (stencil_.referenceAssigned)
        {
            Bridge::SetStencilReference(
                stencil_.enabled, stencil_.twoSided,
                stencil_.function, stencil_.counterClockwiseFunction,
                stencil_.readMask, stencil_.reference);
        }
        if (rasterizerStateApplied_) ApplyCurrentRasterizerState();

        if (viewport_.applied)
        {
            int framebufferHeight = currentRenderTargetHeight_;
            if (currentRenderTargetCount_ == 0)
            {
                int physicalWidth = 0;
                GetPhysicalSize(physicalWidth, framebufferHeight);
                (void)physicalWidth;
            }
            Bridge::SetViewport(
                viewport_.x, framebufferHeight - viewport_.y - viewport_.height,
                viewport_.width, viewport_.height,
                viewport_.minDepth, viewport_.maxDepth);
        }
        if (scissor_.applied)
        {
            int framebufferHeight = currentRenderTargetHeight_;
            if (currentRenderTargetCount_ == 0)
            {
                int physicalWidth = 0;
                GetPhysicalSize(physicalWidth, framebufferHeight);
                (void)physicalWidth;
            }
            Bridge::SetScissor(
                scissor_.x, framebufferHeight - scissor_.y - scissor_.height,
                scissor_.width, scissor_.height);
        }
    }

    void RlglRenderer::NotifyDeviceEvent(const RendererDeviceEvent event)
    {
        if (deviceEventCallback_) deviceEventCallback_(event);
    }

    std::unique_ptr<IRendererThreadContextLease>
    RlglRenderer::AcquireThreadContextLeaseEXT(
        const RendererThreadContextLeaseRelease release)
    {
        const auto control = threadContextLeaseControl_;
        if (!control)
            throw std::runtime_error("RLGL: renderer context is no longer available");

        control->mutex.lock();
        bool outerAcquisition = false;
        CNA::Platform::GlContextBinding ownedBinding;
        try
        {
            if (contextRecoveryState_.load(std::memory_order_acquire) !=
                RlglContextRecoveryState::Available)
            {
                throw std::runtime_error(
                    "RLGL: renderer context is unavailable; restore it before acquiring draw access");
            }
            auto& state = ThreadContextLeaseStates()[control.get()];
            if (state.depth == 0)
            {
                outerAcquisition = true;
                state.previousBinding = control->platformContext->GetCurrentBinding();
                state.release = release;
                ThrowInjectedBindFailure("before-make-current");
                control->platformContext->MakeCurrent();
                ownedBinding = control->platformContext->GetCurrentBinding();
                ThrowInjectedBindFailure("after-make-current");
                ThrowIfNativeContextWasReset();
            }
            ++state.depth;
        }
        catch (...)
        {
            if (outerAcquisition)
            {
                const auto found = ThreadContextLeaseStates().find(control.get());
                if (found != ThreadContextLeaseStates().end())
                {
                    try
                    {
                        const bool lostRendererBinding =
                            contextRecoveryState_.load(std::memory_order_acquire) !=
                                RlglContextRecoveryState::Available &&
                            found->second.previousBinding.context == ownedBinding.context;
                        if (lostRendererBinding) control->platformContext->ClearCurrent();
                        else control->platformContext->RestoreBinding(
                            found->second.previousBinding,
                            RendererThreadContextLeaseRelease::RestorePreviousBinding);
                    }
                    catch (const std::exception& restoreError)
                    {
                        CNA::Logger::Error(
                            std::string("RLGL: failed to restore a binding after lease acquisition ") +
                                "failed: " + restoreError.what(),
                            CNA::LogCategory::RENDER);
                    }
                }
            }
            ThreadContextLeaseStates().erase(control.get());
            control->mutex.unlock();
            throw;
        }

        try
        {
            return std::make_unique<RlglThreadContextLease>(
                [control]() { ReleaseThreadContextLease(control); });
        }
        catch (...)
        {
            ReleaseThreadContextLease(control);
            throw;
        }
    }

    std::shared_ptr<RlglResourceLifetime>
    RlglRenderer::GetResourceLifetimeForTesting() const noexcept
    {
        return resourceLifetime_;
    }

    void RlglRenderer::SetContextRecoveryEnabled(const bool enabled)
    {
        if (resourceLifetime_) resourceLifetime_->SetRecoveryEnabled(enabled);
    }

    bool RlglRenderer::CanBeginDrawEXT() const
    {
        return contextRecoveryState_.load(std::memory_order_acquire) ==
            RlglContextRecoveryState::Available;
    }

    void RlglRenderer::TransitionToContextLost(
        const std::string& reason, const bool nativeDetection)
    {
        const auto control = threadContextLeaseControl_;
        std::unique_lock<std::recursive_mutex> lock;
        if (control) lock = std::unique_lock<std::recursive_mutex>(control->mutex);
        if (contextRecoveryState_.load(std::memory_order_acquire) !=
            RlglContextRecoveryState::Available)
        {
            return;
        }
        contextRecoveryState_.store(
            RlglContextRecoveryState::Lost, std::memory_order_release);
        unavailableReason_ = reason;
        if (nativeDetection)
            detectedNativeLosses_.fetch_add(1, std::memory_order_relaxed);
        if (resourceLifetime_)
            resourceLifetime_->InvalidateNativeResourcesForContextLoss();
        InvalidateRendererNativeState();
        NotifyDeviceEvent(RendererDeviceEvent::Lost);
    }

    void RlglRenderer::ThrowIfNativeContextWasReset()
    {
        const Bridge::GraphicsResetStatus status = Bridge::PollGraphicsResetStatus();
        if (status == Bridge::GraphicsResetStatus::NoError) return;
        const std::string reason = std::string("RLGL: native OpenGL context loss detected (") +
            GraphicsResetStatusName(status) + ")";
        TransitionToContextLost(reason, true);
        throw std::runtime_error(reason);
    }

    void RlglRenderer::DebugSimulateContextLoss()
    {
        const auto control = threadContextLeaseControl_;
        if (!control)
            throw std::runtime_error("RLGL: renderer context is no longer available");

        const std::scoped_lock lock(control->mutex);
        TransitionToContextLost("RLGL: debug-simulated OpenGL context loss", false);
    }

    void RlglRenderer::DebugRestoreContext()
    {
        const auto control = threadContextLeaseControl_;
        if (!control || !platformContext_)
            throw std::runtime_error("RLGL: renderer context is no longer available");

        const std::scoped_lock lock(control->mutex);
        const RlglContextRecoveryState previousState =
            contextRecoveryState_.load(std::memory_order_acquire);
        if (previousState == RlglContextRecoveryState::Available) return;
        if (previousState == RlglContextRecoveryState::Recreating)
            throw std::runtime_error("RLGL: context recreation is already in progress");

        contextRecoveryState_.store(
            RlglContextRecoveryState::Recreating, std::memory_order_release);
        unavailableReason_.clear();

        CNA::Platform::GlContextBinding previousBinding;
        bool previousBindingCaptured = false;
        bool previousWasRenderer = false;

        const auto restoreCallerBinding = [
            this, &previousBinding, &previousBindingCaptured, &previousWasRenderer](
            const bool recreationSucceeded) noexcept
        {
            if (!previousBindingCaptured) return;
            try
            {
                if (previousWasRenderer)
                {
                    if (!recreationSucceeded) platformContext_->ClearCurrent();
                    return;
                }
                platformContext_->RestoreBinding(
                    previousBinding,
                    RendererThreadContextLeaseRelease::RestorePreviousBinding);
            }
            catch (const std::exception& error)
            {
                CNA::Logger::Error(
                    std::string("RLGL: failed to restore caller binding after context ") +
                        "recreation: " + error.what(),
                    CNA::LogCategory::RENDER);
            }
        };

        try
        {
            NotifyDeviceEvent(RendererDeviceEvent::Resetting);
            previousBinding = platformContext_->GetCurrentBinding();
            previousBindingCaptured = true;
            platformContext_->MakeCurrent();
            const CNA::Platform::GlContextBinding ownedBinding =
                platformContext_->GetCurrentBinding();
            previousWasRenderer = previousBinding.context != nullptr &&
                previousBinding.context == ownedBinding.context;
            previousWasRenderer = previousWasRenderer ||
                HasActiveThreadContextLease(*control);
            ThrowInjectedRecreateFailure("before-context");
            try
            {
                platformContext_->Recreate();
            }
            catch (...)
            {
                RetargetThreadContextLeaseBinding(
                    *control, ownedBinding.context, {});
                throw;
            }
            RetargetThreadContextLeaseBinding(
                *control, ownedBinding.context, platformContext_->GetCurrentBinding());
            ThrowInjectedRecreateFailure("after-context");
            RefreshContextAttributes();
            const std::string version = InitializeContextState();
            ThrowInjectedRecreateFailure("after-bridge");
            RestoreRendererNativeState();
            if (resourceLifetime_)
                resourceLifetime_->RestoreNativeResourcesAfterContextRecreation();
            RestoreCurrentRenderTargets();
            ReapplyDeviceState();
            ThrowInjectedRecreateFailure("after-state");

            ++contextGeneration_;
            contextRecoveryState_.store(
                RlglContextRecoveryState::Available, std::memory_order_release);
            restoreCallerBinding(true);
            CNA::Logger::Info(
                "RLGL renderer recreated standalone rlgl 6.0 with OpenGL " + version,
                CNA::LogCategory::RENDER);
        }
        catch (const std::exception& error)
        {
            unavailableReason_ = error.what();
            if (rlglInitialized_)
            {
                try
                {
                    platformContext_->MakeCurrent();
                    if (resourceLifetime_)
                        resourceLifetime_->ReleaseNativeResourcesForRecoveryRollback();
                    DestroyRendererNativeState();
                    Bridge::Shutdown();
                }
                catch (const std::exception& cleanupError)
                {
                    CNA::Logger::Error(
                        std::string("RLGL: failed to clean a partial context recreation: ") +
                            cleanupError.what(),
                        CNA::LogCategory::RENDER);
                }
                rlglInitialized_ = false;
            }
            contextRecoveryState_.store(
                RlglContextRecoveryState::Unavailable, std::memory_order_release);
            restoreCallerBinding(false);
            throw;
        }

        // A user DeviceReset handler may throw. The native transaction is already complete at
        // this point, so preserve the available context and the restored caller binding.
        NotifyDeviceEvent(RendererDeviceEvent::Reset);
    }

    RlglContextRecoverySnapshot
    RlglRenderer::GetContextRecoverySnapshotForTesting() const
    {
        const auto control = threadContextLeaseControl_;
        std::unique_lock<std::recursive_mutex> lock;
        if (control) lock = std::unique_lock<std::recursive_mutex>(control->mutex);

        RlglContextRecoverySnapshot snapshot;
        snapshot.state = contextRecoveryState_.load(std::memory_order_acquire);
        snapshot.contextGeneration = contextGeneration_;
        snapshot.rlglInitialized = rlglInitialized_;
        snapshot.realizedSamplers = static_cast<std::size_t>(std::count_if(
            samplers_.begin(), samplers_.end(),
            [](const SamplerRecord& sampler) { return sampler.realized; }));
        snapshot.liveSamplers = static_cast<std::size_t>(std::count_if(
            samplers_.begin(), samplers_.end(),
            [](const SamplerRecord& sampler) { return sampler.id != 0; }));
        for (std::size_t index = 0; index < samplers_.size(); ++index)
            snapshot.samplerIds[index] = samplers_[index].id;
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        snapshot.realizedVertexSamplers = static_cast<std::size_t>(std::count_if(
            vertexSamplers_.begin(), vertexSamplers_.end(),
            [](const SamplerRecord& sampler) { return sampler.realized; }));
        snapshot.liveVertexSamplers = static_cast<std::size_t>(std::count_if(
            vertexSamplers_.begin(), vertexSamplers_.end(),
            [](const SamplerRecord& sampler) { return sampler.id != 0; }));
        for (std::size_t index = 0; index < vertexSamplers_.size(); ++index)
            snapshot.vertexSamplerIds[index] = vertexSamplers_[index].id;
#endif
        snapshot.primitivePipelineRealized = restorePrimitivePipeline_;
        snapshot.primitivePipelineLive = primitivePipeline_ != nullptr;
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        snapshot.compiledDrawResourcesRealized = restoreCompiledDrawResources_;
        snapshot.compiledDrawResourcesLive = compiledEffectDrawResources_ != nullptr;
        snapshot.mojoShaderContextRealized = restoreMojoShaderContext_;
        snapshot.mojoShaderContextLive = mojoShaderContext_ != nullptr;
#endif
        snapshot.appliedBackBufferFormat = backBufferFormat_;
        snapshot.appliedDepthStencilFormat = depthStencilFormat_;
        snapshot.appliedMultiSampleCount = multiSampleCount_;
        snapshot.robustContext = robustContext_;
        snapshot.nativeLossPollingAvailable = nativeLossPollingAvailable_;
        snapshot.presentationResets =
            presentationResets_.load(std::memory_order_relaxed);
        snapshot.detectedNativeLosses =
            detectedNativeLosses_.load(std::memory_order_relaxed);
        snapshot.unavailableReason = unavailableReason_;
        return snapshot;
    }

    void RlglRenderer::Clear(const float r, const float g, const float b, const float a)
    {
        Bridge::Clear(Bridge::ColorPlane, r, g, b, a, 1.0f, 0);
    }

    void RlglRenderer::Present()
    {
        ThrowIfNativeContextWasReset();
        try
        {
            ResolveBackbufferToDefault();
            platformContext_->SwapBuffers();
        }
        catch (const std::exception& error)
        {
            TransitionToContextLost(
                std::string("RLGL: native presentation failed: ") + error.what(), true);
            throw;
        }
    }

    void RlglRenderer::GetPhysicalSize(int& width, int& height) const
    {
        surface_.GetDrawableSize(width, height);
    }

    void RlglRenderer::RecreateBackbufferStorage(const int width, const int height)
    {
        if (width <= 0 || height <= 0) return;

        std::unique_ptr<Bridge::RenderTargetStorage> replacement;
        const int requestedSamples = requestedMultiSampleCount_ > 1
            ? std::min(requestedMultiSampleCount_, maxRenderTargetSamples_)
            : 0;
        if (requestedSamples > 1)
        {
            replacement = std::make_unique<Bridge::RenderTargetStorage>(
                Bridge::CreateRenderTarget2D(
                    width, height, 1, depthStencilFormat_, requestedSamples,
                    backBufferFormat_));
        }

        std::unique_ptr<Bridge::RenderTargetStorage> previous =
            std::move(backbufferStorage_);
        backbufferStorage_ = std::move(replacement);
        multiSampleCount_ = backbufferStorage_
            ? backbufferStorage_->multiSampleCount
            : 0;
        if (previous) Bridge::DestroyRenderTarget2D(*previous);

        if (currentRenderTargetCount_ == 0) BindBackbuffer();
    }

    void RlglRenderer::BindBackbuffer()
    {
        if (backbufferStorage_ && backbufferStorage_->multiSampleCount > 0)
            Bridge::BindFramebuffer(backbufferStorage_->framebuffer);
        else
            Bridge::BindDefaultFramebuffer();
    }

    void RlglRenderer::ResolveBackbufferToDefault()
    {
        if (!backbufferStorage_ || backbufferStorage_->multiSampleCount <= 0) return;

        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        Bridge::ResolveRenderTarget2D(*backbufferStorage_, width, height);
        Bridge::BlitResolvedBackbufferToDefault(
            *backbufferStorage_, width, height);
    }

    void RlglRenderer::GetLogicalSize(int& width, int& height) const
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
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth &&
            physicalHeight > 0)
        {
            width = static_cast<int>(
                std::lround(static_cast<double>(physicalWidth) * virtualHeight_ / physicalHeight));
        }
        else
        {
            width = virtualWidth_ > 0 ? virtualWidth_ : physicalWidth;
        }
    }

    void RlglRenderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void RlglRenderer::GetDefaultViewportRect(
        int& x, int& y, int& width, int& height)
    {
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);

        x = 0;
        y = 0;
        width = std::max(0, physicalWidth);
        height = std::max(0, physicalHeight);

        if (physicalWidth <= 0 || physicalHeight <= 0 ||
            presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth ||
            presentationMode_ == CnaPresentationMode::Stretch ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
        {
            return;
        }

        const double scaleX = static_cast<double>(physicalWidth) / virtualWidth_;
        const double scaleY = static_cast<double>(physicalHeight) / virtualHeight_;
        const double scale = presentationMode_ == CnaPresentationMode::Overscan
                                 ? std::max(scaleX, scaleY)
                                 : std::min(scaleX, scaleY);
        width = static_cast<int>(std::lround(virtualWidth_ * scale));
        height = static_cast<int>(std::lround(virtualHeight_ * scale));
        x = static_cast<int>(std::lround((physicalWidth - width) * 0.5));
        y = static_cast<int>(std::lround((physicalHeight - height) * 0.5));
    }

    void RlglRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        Bridge::SetFramebufferSize(width, height);
        RecreateBackbufferStorage(width, height);
    }

    void RlglRenderer::SetVirtualResolution(const int width, const int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void RlglRenderer::SetPresentationMode(const int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void RlglRenderer::SetSwapInterval(const int interval)
    {
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    int RlglRenderer::GetAppliedMultiSampleCountEXT(
        const int requestedMultiSampleCount) const
    {
        (void)requestedMultiSampleCount;
        return multiSampleCount_;
    }

    int RlglRenderer::ApplyMultiSampleCount(const int requestedMultiSampleCount)
    {
        const int previousRequest = requestedMultiSampleCount_;
        requestedMultiSampleCount_ = requestedMultiSampleCount;
        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        try
        {
            RecreateBackbufferStorage(width, height);
        }
        catch (...)
        {
            requestedMultiSampleCount_ = previousRequest;
            throw;
        }
        return multiSampleCount_;
    }

    void RlglRenderer::UpdatePresentationFormatEXT(
        const int backBufferFormat, const int depthStencilFormat,
        const bool isFullScreen)
    {
        (void)backBufferFormat;
        (void)depthStencilFormat;
        (void)isFullScreen;
        presentationResets_.fetch_add(1, std::memory_order_relaxed);
    }

    int RlglRenderer::GetAppliedBackBufferFormatEXT(const int requestedFormat) const
    {
        (void)requestedFormat;
        return backBufferFormat_;
    }

    int RlglRenderer::GetAppliedDepthStencilFormatEXT(const int requestedFormat) const
    {
        (void)requestedFormat;
        return depthStencilFormat_;
    }

    bool RlglRenderer::SupportsDepthStencil() const
    {
        return SupportsDepthBuffer() && SupportsStencilBuffer();
    }

    bool RlglRenderer::SupportsCapability(const CNA::GraphicsCapability capability) const
    {
        // Native GL availability is not a CNA implementation promise. Each row opts in only after
        // its resource/state/draw path and observable behavior have dedicated validation.
        switch (capability)
        {
        case CNA::GraphicsCapability::AnisotropicFiltering:
            return maxSamplerAnisotropy_ > 1.0f;
        case CNA::GraphicsCapability::DepthStencilBuffer:
            return SupportsDepthStencil();
        case CNA::GraphicsCapability::StencilBuffer:
            return SupportsStencilBuffer();
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            return maxRenderTargetSamples_ >= 2;
        case CNA::GraphicsCapability::MultipleRenderTargets:
            return maxRenderTargets_ >= 2;
        case CNA::GraphicsCapability::ThreeD:
        case CNA::GraphicsCapability::WireFrame:
        case CNA::GraphicsCapability::OcclusionQuery:
        case CNA::GraphicsCapability::CustomEffects:
        case CNA::GraphicsCapability::MultiStreamVertexInput:
        case CNA::GraphicsCapability::Instancing:
            return true;
        default:
            return false;
        }
    }

    bool RlglRenderer::SupportsShaderLanguageEXT(
        const int language, const int stage) const
    {
        if (language != static_cast<int>(CNA::ShaderLanguageEXT::GlslDesktop))
            return false;
        return stage == static_cast<int>(CNA::ShaderStageEXT::Vertex) ||
            stage == static_cast<int>(CNA::ShaderStageEXT::Fragment);
    }

    void RlglRenderer::ReadBackbuffer(
        const int x, const int y, const int w, const int h, uint8_t* pixels)
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        GetPhysicalSize(framebufferWidth, framebufferHeight);
        (void)framebufferWidth;
        ResolveBackbufferToDefault();
        Bridge::ReadBackbuffer(x, y, w, h, framebufferHeight, pixels);
    }

    std::unique_ptr<ITextureRenderer> RlglRenderer::CreateTexture(const ImageData& data)
    {
        return CreateTextureRenderer(data, resourceLifetime_);
    }

    std::unique_ptr<ITextureCubeRenderer> RlglRenderer::CreateTextureCube(
        const int size, const bool mipMap, const int surfaceFormat)
    {
        if (ClassifyTextureCubeFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw System::NotSupportedException(
                "RLGL: requested TextureCube SurfaceFormat is not implemented "
                "(plans/plan_rlgl.md RLGL-045)");
        }
        return CreateTextureCubeRenderer(size, mipMap, surfaceFormat, resourceLifetime_);
    }

    std::unique_ptr<IEffectRenderer> RlglRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        return Bridge::CreateShaderEffectRenderer(
            vertSrc, fragSrc, resourceLifetime_, maxSamplerSlots_);
    }

    std::unique_ptr<IRenderTargetRenderer> RlglRenderer::CreateRenderTarget2D(
        const int width, const int height, const int depthFormat,
        const bool preserveContents, const bool mipMap,
        const int multiSampleCount)
    {
        return CreateRenderTargetRenderer(
            width, height, depthFormat, preserveContents,
            mipMap, multiSampleCount,
            static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color),
            resourceLifetime_);
    }

    std::unique_ptr<IRenderTargetRenderer> RlglRenderer::CreateRenderTarget2DEXT(
        const int width, const int height, const int depthFormat,
        const bool preserveContents, const bool mipMap,
        const int multiSampleCount, const int surfaceFormat)
    {
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw System::NotSupportedException(
                "RLGL: requested RenderTarget2D SurfaceFormat is not renderable on this "
                "OpenGL context (plans/plan_rlgl.md RLGL-042)");
        }
        return CreateRenderTargetRenderer(
            width, height, depthFormat, preserveContents,
            mipMap, multiSampleCount, surfaceFormat, resourceLifetime_);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> RlglRenderer::CreateRenderTargetCube(
        const int size, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount)
    {
        return CreateRenderTargetCubeRenderer(
            size, depthFormat, preserveContents, mipMap, multiSampleCount,
            static_cast<int>(
                Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color),
            resourceLifetime_);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> RlglRenderer::CreateRenderTargetCubeEXT(
        const int size, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount, const int surfaceFormat)
    {
        if (ClassifyRenderTargetCubeFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw System::NotSupportedException(
                "RLGL: requested RenderTargetCube SurfaceFormat is not renderable on this "
                "OpenGL context (plans/plan_rlgl.md RLGL-046)");
        }
        return CreateRenderTargetCubeRenderer(
            size, depthFormat, preserveContents,
            mipMap, multiSampleCount, surfaceFormat, resourceLifetime_);
    }

    int RlglRenderer::GetMaxRenderTargetsForProfileEXT(const int graphicsProfile) const
    {
        return graphicsProfile == 1 ? maxRenderTargets_ : 1;
    }

    int RlglRenderer::GetMaxCubeSizeForProfileEXT(const int graphicsProfile) const
    {
        return std::min(maxTextureSize_, graphicsProfile == 1 ? 4096 : 512);
    }

    RendererFormatVerdict RlglRenderer::ClassifySurfaceFormatEXT(
        const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
        case SurfaceFormat::Bgr565:
        case SurfaceFormat::Bgra5551:
        case SurfaceFormat::Bgra4444:
        case SurfaceFormat::NormalizedByte2:
        case SurfaceFormat::NormalizedByte4:
        case SurfaceFormat::Rgba1010102:
        case SurfaceFormat::Rg32:
        case SurfaceFormat::Rgba64:
        case SurfaceFormat::Alpha8:
        case SurfaceFormat::Single:
        case SurfaceFormat::Vector2:
        case SurfaceFormat::Vector4:
        case SurfaceFormat::HalfSingle:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return RendererFormatVerdict::Supported;
        case SurfaceFormat::Dxt1:
        case SurfaceFormat::Dxt3:
        case SurfaceFormat::Dxt5:
            return RendererFormatVerdict::Supported;
        default:
            return RendererFormatVerdict::Unsupported;
        }
    }

    RendererFormatVerdict RlglRenderer::ClassifyTextureCubeFormatEXT(
        const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        return format == SurfaceFormat::Color || format == SurfaceFormat::Dxt1 ||
            format == SurfaceFormat::Dxt3 || format == SurfaceFormat::Dxt5
                ? RendererFormatVerdict::Supported
                : RendererFormatVerdict::Unsupported;
    }

    RendererFormatVerdict RlglRenderer::ClassifyRenderTargetFormatEXT(
        const int surfaceFormat) const
    {
        return Bridge::ProbeRenderTargetFormat(surfaceFormat)
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    RendererFormatVerdict RlglRenderer::ClassifyRenderTargetCubeFormatEXT(
        const int surfaceFormat) const
    {
        return Bridge::ProbeRenderTargetCubeFormat(surfaceFormat)
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    RendererFormatVerdict RlglRenderer::ClassifyColorTransferFormatEXT(
        const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        if (format == SurfaceFormat::NormalizedByte2 ||
            format == SurfaceFormat::NormalizedByte4)
        {
            return RendererFormatVerdict::Unsupported;
        }
        return RendererFormatVerdict::Defer;
    }

    bool RlglRenderer::IsCompressedTransferFormatEXT(const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
            format == SurfaceFormat::Dxt5;
    }

    bool RlglRenderer::IsCompressedCubeTransferFormatEXT(const int surfaceFormat) const
    {
        return IsCompressedTransferFormatEXT(surfaceFormat) &&
            ClassifyTextureCubeFormatEXT(surfaceFormat) == RendererFormatVerdict::Supported;
    }

    bool RlglRenderer::LoadsCompressedContentNativelyEXT() const
    {
        return true;
    }

    std::unique_ptr<ISpriteBatchRenderer> RlglRenderer::CreateSpriteBatch()
    {
        return CreateSpriteBatchRenderer(*this, resourceLifetime_);
    }

    std::unique_ptr<IOcclusionQueryRenderer> RlglRenderer::CreateOcclusionQuery()
    {
        return CreateOcclusionQueryRenderer(resourceLifetime_);
    }

    void RlglRenderer::GetSpriteBatchViewportSize(int& width, int& height)
    {
        if (currentRenderTargetCount_ > 0)
        {
            width = currentRenderTargetWidth_;
            height = currentRenderTargetHeight_;
            return;
        }
        int logicalWidth = 0;
        int logicalHeight = 0;
        GetLogicalSize(logicalWidth, logicalHeight);
        if (viewportIsDefault_)
        {
            width = logicalWidth;
            height = logicalHeight;
            return;
        }

        int defaultX = 0;
        int defaultY = 0;
        int defaultWidth = 0;
        int defaultHeight = 0;
        GetDefaultViewportRect(
            defaultX, defaultY, defaultWidth, defaultHeight);
        (void)defaultX;
        (void)defaultY;
        if (logicalWidth > 0 && logicalHeight > 0 &&
            defaultWidth > 0 && defaultHeight > 0)
        {
            width = static_cast<int>(std::lround(
                static_cast<double>(currentViewportWidth_) * logicalWidth / defaultWidth));
            height = static_cast<int>(std::lround(
                static_cast<double>(currentViewportHeight_) * logicalHeight / defaultHeight));
        }
        else
        {
            width = currentViewportWidth_;
            height = currentViewportHeight_;
        }
    }

    RlglRenderer::SamplerRecord& RlglRenderer::GetSamplerRecord(const int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(samplers_.size()) || slot >= maxSamplerSlots_)
            throw std::out_of_range("RLGL: sampler slot is outside the XNA range");
        SamplerRecord& sampler = samplers_[static_cast<std::size_t>(slot)];
        if (sampler.id == 0) sampler.id = Bridge::CreateSampler();
        sampler.realized = true;
        return sampler;
    }

    void RlglRenderer::ApplySamplerRecord(const int slot, SamplerRecord& sampler)
    {
        Bridge::ApplySampler(
            sampler.id, slot, sampler.filter,
            sampler.addressU, sampler.addressV, sampler.addressW,
            sampler.maxAnisotropy, sampler.maxMipLevel, sampler.lodBias);
    }

#if defined(CNA_RLGL_COMPILED_EFFECTS)
    RlglRenderer::SamplerRecord&
    RlglRenderer::GetCompiledEffectVertexSamplerRecord(const int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(vertexSamplers_.size()) ||
            slot >= maxVertexSamplerSlots_)
        {
            throw System::NotSupportedException(
                "RLGL compiled effect: vertex sampler register exceeds the live GL/MojoShader "
                "limit");
        }
        SamplerRecord& sampler = vertexSamplers_[static_cast<std::size_t>(slot)];
        if (sampler.id == 0) sampler.id = Bridge::CreateSampler();
        sampler.realized = true;
        return sampler;
    }

    void RlglRenderer::ApplyCompiledEffectVertexSamplerRecord(
        const int slot, SamplerRecord& sampler)
    {
        Bridge::ApplySampler(
            sampler.id, Bridge::GetCompiledEffectVertexSamplerOffset() + slot,
            sampler.filter, sampler.addressU, sampler.addressV, sampler.addressW,
            sampler.maxAnisotropy, sampler.maxMipLevel, sampler.lodBias);
    }
#endif

    void RlglRenderer::ApplySamplerState(
        const int slot, const int filter, const int addressU, const int addressV,
        const int maxAnisotropy)
    {
        SamplerRecord& sampler = GetSamplerRecord(slot);
        sampler.filter = filter;
        sampler.addressU = addressU;
        sampler.addressV = addressV;
        sampler.addressW = addressU;
        sampler.maxAnisotropy = maxAnisotropy;
        sampler.maxMipLevel = 0;
        sampler.lodBias = 0.0f;
        ApplySamplerRecord(slot, sampler);
    }

    void RlglRenderer::ApplySamplerMipState(
        const int slot, const int maxMipLevel, const float lodBias)
    {
        SamplerRecord& sampler = GetSamplerRecord(slot);
        sampler.maxMipLevel = maxMipLevel;
        sampler.lodBias = lodBias;
        ApplySamplerRecord(slot, sampler);
    }

    void RlglRenderer::ApplySamplerAddressW(const int slot, const int addressW)
    {
        SamplerRecord& sampler = GetSamplerRecord(slot);
        sampler.addressW = addressW;
        ApplySamplerRecord(slot, sampler);
    }

    int RlglRenderer::GetCurrentSampleCount() const
    {
        if (currentRenderTargetCount_ <= 0) return multiSampleCount_;
        if (currentRenderTargets_[0] != nullptr)
            return currentRenderTargets_[0]->GetMultiSampleCount();
        if (currentRenderTargetCubes_[0] != nullptr)
            return currentRenderTargetCubes_[0]->GetMultiSampleCount();
        throw std::logic_error("RLGL: active render-target set has no first attachment");
    }

    void RlglRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, const int count)
    {
        if (count < 0)
            throw std::invalid_argument("RLGL: render-target count cannot be negative");
        if (count == 0)
        {
            FinalizeCurrentRenderTargets();
            Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
            BindBackbuffer();
            ApplyCurrentRasterizerState();
            return;
        }
        if (renderTargets == nullptr)
            throw std::invalid_argument(
                "RLGL: nonzero render-target count requires a binding array");

        if (count > maxRenderTargets_)
        {
            throw System::NotSupportedException(
                "RLGL: requested render-target count exceeds the live OpenGL MRT limit");
        }

        std::array<IRenderTargetRenderer*, 4> targets{};
        std::array<IRenderTargetCubeRenderer*, 4> cubeTargets{};
        std::array<int, 4> cubeFaces{{-1, -1, -1, -1}};
        std::array<Bridge::MrtAttachment, 4> attachments{};
        int targetWidth = 0;
        int targetHeight = 0;
        int targetSamples = 0;
        int firstDepthFormat = 0;
        int firstDepthBits = 0;
        for (int slot = 0; slot < count; ++slot)
        {
            if (renderTargets[slot].IsRenderTarget2D())
            {
                targets[slot] = renderTargets[slot].GetRenderTarget2D();
                if (targets[slot] == nullptr)
                    throw std::invalid_argument("RLGL: null RenderTarget2D binding");
                const RenderTargetResourceSnapshot snapshot =
                    GetRenderTargetResourceSnapshotForTesting(*targets[slot]);
                if (snapshot.width != renderTargets[slot].GetWidth() ||
                    snapshot.height != renderTargets[slot].GetHeight() ||
                    snapshot.multiSampleCount !=
                        renderTargets[slot].GetAppliedMultiSampleCount())
                {
                    throw std::invalid_argument(
                        "RLGL: RenderTarget2D descriptor does not match native storage");
                }
                attachments[slot] = {
                    snapshot.colorTexture,
                    snapshot.multisampleColorRenderbuffer,
                    snapshot.depthStencilRenderbuffer,
                    100,
                    snapshot.depthFormat,
                    snapshot.multiSampleCount};
                if (slot == 0)
                {
                    targetWidth = snapshot.width;
                    targetHeight = snapshot.height;
                    targetSamples = snapshot.multiSampleCount;
                    firstDepthFormat = snapshot.depthFormat;
                    firstDepthBits = targets[slot]->DepthBufferBitsEXT();
                }
            }
            else if (renderTargets[slot].IsRenderTargetCubeFace())
            {
                cubeTargets[slot] = renderTargets[slot].GetRenderTargetCube();
                cubeFaces[slot] = renderTargets[slot].GetCubeFace();
                if (cubeTargets[slot] == nullptr ||
                    cubeFaces[slot] < 0 || cubeFaces[slot] >= 6)
                {
                    throw std::invalid_argument("RLGL: invalid RenderTargetCube face binding");
                }
                const RenderTargetCubeResourceSnapshot snapshot =
                    GetRenderTargetCubeResourceSnapshotForTesting(*cubeTargets[slot]);
                if (snapshot.size != renderTargets[slot].GetWidth() ||
                    snapshot.size != renderTargets[slot].GetHeight() ||
                    snapshot.multiSampleCount !=
                        renderTargets[slot].GetAppliedMultiSampleCount())
                {
                    throw std::invalid_argument(
                        "RLGL: RenderTargetCube descriptor does not match native storage");
                }
                attachments[slot] = {
                    snapshot.colorTexture,
                    snapshot.multisampleColorRenderbuffers[
                        static_cast<std::size_t>(cubeFaces[slot])],
                    snapshot.depthStencilRenderbuffer,
                    cubeFaces[slot],
                    snapshot.depthFormat,
                    snapshot.multiSampleCount};
                if (slot == 0)
                {
                    targetWidth = snapshot.size;
                    targetHeight = snapshot.size;
                    targetSamples = snapshot.multiSampleCount;
                    firstDepthFormat = snapshot.depthFormat;
                    firstDepthBits = cubeTargets[slot]->DepthBufferBitsEXT();
                }
            }
            else
            {
                throw std::invalid_argument("RLGL: unknown render-target binding kind");
            }

            if (slot > 0 &&
                (renderTargets[slot].GetWidth() != targetWidth ||
                 renderTargets[slot].GetHeight() != targetHeight ||
                 renderTargets[slot].GetAppliedMultiSampleCount() != targetSamples))
            {
                throw std::invalid_argument(
                    "RLGL: MRT dimensions and applied sample counts must match");
            }
            for (int previous = 0; previous < slot; ++previous)
            {
                const bool duplicate2D = targets[slot] != nullptr &&
                    targets[previous] == targets[slot];
                const bool duplicateCubeFace = cubeTargets[slot] != nullptr &&
                    cubeTargets[previous] == cubeTargets[slot] &&
                    cubeFaces[previous] == cubeFaces[slot];
                if (duplicate2D || duplicateCubeFace)
                    throw std::invalid_argument(
                        "RLGL: one render-target subresource cannot occupy multiple MRT slots");
            }
        }

        if (count == 1)
        {
            const bool sameBinding = currentRenderTargetCount_ == 1 &&
                currentRenderTargets_[0] == targets[0] &&
                currentRenderTargetCubes_[0] == cubeTargets[0] &&
                currentRenderTargetCubeFaces_[0] == cubeFaces[0];
            if (!sameBinding)
            {
                FinalizeCurrentRenderTargets();
                Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
            }
            if (targets[0] != nullptr) targets[0]->BindAsRenderTarget();
            else cubeTargets[0]->BindAsRenderTargetFace(cubeFaces[0]);
        }
        else
        {
            unsigned int candidate = Bridge::CreateMrtFramebuffer(
                attachments.data(), count);
            try
            {
                FinalizeCurrentRenderTargets();
                Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
                Bridge::BindFramebuffer(candidate);
                mrtFramebuffer_ = candidate;
                candidate = 0;
            }
            catch (...)
            {
                Bridge::DestroyMrtFramebuffer(candidate);
                throw;
            }
        }

        currentRenderTargets_ = targets;
        currentRenderTargetCubes_ = cubeTargets;
        currentRenderTargetCubeFaces_ = cubeFaces;
        currentRenderTargetCount_ = count;
        currentRenderTargetWidth_ = targetWidth;
        currentRenderTargetHeight_ = targetHeight;
        currentTargetDepthBits_ = firstDepthFormat != 0 ? firstDepthBits : 0;
        ApplyCurrentRasterizerState();
    }

    void RlglRenderer::FinalizeCurrentRenderTargets()
    {
        const auto targets = currentRenderTargets_;
        const auto cubeTargets = currentRenderTargetCubes_;
        const auto cubeFaces = currentRenderTargetCubeFaces_;
        const int count = currentRenderTargetCount_;
        for (int slot = 0; slot < count; ++slot)
        {
            if (targets[slot] != nullptr)
                targets[slot]->UnbindAsRenderTarget();
            else if (cubeTargets[slot] != nullptr)
                FinalizeRenderTargetCubeFace(*cubeTargets[slot], cubeFaces[slot]);
        }
        currentRenderTargets_.fill(nullptr);
        currentRenderTargetCubes_.fill(nullptr);
        currentRenderTargetCubeFaces_.fill(-1);
        currentRenderTargetCount_ = 0;
        currentRenderTargetWidth_ = 0;
        currentRenderTargetHeight_ = 0;
        currentTargetDepthBits_ = depthBits_;
    }

    void RlglRenderer::ClearColorAndDepth(
        const float r, const float g, const float b, const float a, const float depth)
    {
        Bridge::Clear(Bridge::ColorPlane | Bridge::DepthPlane, r, g, b, a, depth, 0);
    }

    void RlglRenderer::ClearDepth(const float depth)
    {
        Bridge::Clear(Bridge::DepthPlane, 0.0f, 0.0f, 0.0f, 0.0f, depth, 0);
    }

    void RlglRenderer::ClearStencil(const int stencil)
    {
        Bridge::Clear(Bridge::StencilPlane, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, stencil);
    }

    void RlglRenderer::ClearDepthAndStencil(const float depth, const int stencil)
    {
        Bridge::Clear(
            Bridge::DepthPlane | Bridge::StencilPlane,
            0.0f, 0.0f, 0.0f, 0.0f, depth, stencil);
    }

    void RlglRenderer::ClearColorAndStencil(
        const float r, const float g, const float b, const float a, const int stencil)
    {
        Bridge::Clear(
            Bridge::ColorPlane | Bridge::StencilPlane, r, g, b, a, 1.0f, stencil);
    }

    void RlglRenderer::ClearColorDepthAndStencil(
        const float r, const float g, const float b, const float a,
        const float depth, const int stencil)
    {
        Bridge::Clear(
            Bridge::ColorPlane | Bridge::DepthPlane | Bridge::StencilPlane,
            r, g, b, a, depth, stencil);
    }

    void RlglRenderer::SetDepthTestEnabled(const bool enabled)
    {
        stencil_.depthEnable = enabled;
        stencil_.depthEnableAssigned = true;
        Bridge::SetDepthTestEnabled(enabled);
    }

    void RlglRenderer::SetBlendEnabled(const bool enabled)
    {
        blend_.enabled = enabled;
        blend_.enabledApplied = true;
        Bridge::SetBlendEnabled(enabled);
    }

    void RlglRenderer::SetDepthWriteEnabled(const bool enabled)
    {
        stencil_.depthWriteEnable = enabled;
        stencil_.depthWriteAssigned = true;
        Bridge::SetDepthWriteEnabled(enabled);
    }

    void RlglRenderer::ApplyBlendState(
        const int colorSrcBlend, const int alphaSrcBlend,
        const int colorDstBlend, const int alphaDstBlend,
        const int colorBlendFunc, const int alphaBlendFunc,
        const BlendWriteState& writeState)
    {
        blend_.stateApplied = true;
        blend_.colorSource = colorSrcBlend;
        blend_.alphaSource = alphaSrcBlend;
        blend_.colorDestination = colorDstBlend;
        blend_.alphaDestination = alphaDstBlend;
        blend_.colorFunction = colorBlendFunc;
        blend_.alphaFunction = alphaBlendFunc;
        blend_.writeState = writeState;
        Bridge::ApplyBlendState(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc,
            writeState.colorWriteChannels, writeState.multiSampleMask);
    }

    void RlglRenderer::SetBlendFactor(
        const float r, const float g, const float b, const float a)
    {
        blend_.factorApplied = true;
        blend_.factor = {r, g, b, a};
        Bridge::SetBlendFactor(r, g, b, a);
    }

    void RlglRenderer::ApplyDepthStencilState(
        const bool depthEnable, const bool depthWriteEnable, const int depthFunc,
        const bool stencilEnable, const int stencilFunc,
        const int stencilPass, const int stencilFail, const int stencilDepthFail,
        const int stencilMask, const int stencilWriteMask, const int referenceStencil,
        const bool twoSidedStencilMode, const int ccwStencilFunc,
        const int ccwStencilPass, const int ccwStencilFail,
        const int ccwStencilDepthFail)
    {
        stencil_.stateApplied = true;
        stencil_.depthEnableAssigned = true;
        stencil_.depthWriteAssigned = true;
        stencil_.depthEnable = depthEnable;
        stencil_.depthWriteEnable = depthWriteEnable;
        stencil_.depthFunction = depthFunc;
        stencil_.enabled = stencilEnable;
        stencil_.twoSided = twoSidedStencilMode;
        stencil_.function = stencilFunc;
        stencil_.counterClockwiseFunction = ccwStencilFunc;
        stencil_.readMask = stencilMask;
        stencil_.writeMask = stencilWriteMask;
        stencil_.reference = referenceStencil;
        stencil_.pass = stencilPass;
        stencil_.fail = stencilFail;
        stencil_.depthFail = stencilDepthFail;
        stencil_.counterClockwisePass = ccwStencilPass;
        stencil_.counterClockwiseFail = ccwStencilFail;
        stencil_.counterClockwiseDepthFail = ccwStencilDepthFail;
        stencil_.referenceAssigned = true;
        Bridge::ApplyDepthStencilState(
            depthEnable, depthWriteEnable, depthFunc,
            stencilEnable, stencilFunc, stencilPass, stencilFail, stencilDepthFail,
            stencilMask, stencilWriteMask, referenceStencil, twoSidedStencilMode,
            ccwStencilFunc, ccwStencilPass, ccwStencilFail, ccwStencilDepthFail);
    }

    void RlglRenderer::SetReferenceStencil(const int value)
    {
        stencil_.reference = value;
        stencil_.referenceAssigned = true;
        Bridge::SetStencilReference(
            stencil_.enabled, stencil_.twoSided,
            stencil_.function, stencil_.counterClockwiseFunction,
            stencil_.readMask, stencil_.reference);
    }

    void RlglRenderer::ApplyRasterizerState(
        const int cullMode, const int fillMode, const bool scissorTestEnable,
        const float depthBias, const float slopeScaleDepthBias)
    {
        rasterizerCullMode_ = cullMode;
        rasterizerFillMode_ = fillMode;
        rasterizerScissorTestEnabled_ = scissorTestEnable;
        rasterizerDepthBias_ = depthBias;
        rasterizerSlopeScaleDepthBias_ = slopeScaleDepthBias;
        rasterizerStateApplied_ = true;
        ApplyCurrentRasterizerState();
    }

    void RlglRenderer::ApplyCurrentRasterizerState()
    {
        const int appliedDepthBits = currentRenderTargetCount_ > 0
            ? currentTargetDepthBits_ : depthBits_;
        const float depthScale = appliedDepthBits > 0
            ? std::ldexp(1.0f, appliedDepthBits) - 1.0f
            : 0.0f;
        Bridge::ApplyRasterizerState(
            rasterizerCullMode_, rasterizerFillMode_, rasterizerScissorTestEnabled_,
            rasterizerDepthBias_ * depthScale, rasterizerSlopeScaleDepthBias_);
    }

    std::unique_ptr<IVertexBufferRenderer> RlglRenderer::CreateVertexBuffer(
        const int vertexCapacity)
    {
        return CreateVertexBufferRenderer(vertexCapacity, resourceLifetime_);
    }

    std::unique_ptr<IIndexBufferRenderer> RlglRenderer::CreateIndexBuffer16(
        const int indexCapacity)
    {
        return CreateIndexBufferRenderer(indexCapacity, false, resourceLifetime_);
    }

    std::unique_ptr<IIndexBufferRenderer> RlglRenderer::CreateIndexBuffer32(
        const int indexCapacity)
    {
        return CreateIndexBufferRenderer(indexCapacity, true, resourceLifetime_);
    }

    void RlglRenderer::SetViewport(
        const int x, const int y, const int w, const int h,
        const float minDepth, const float maxDepth)
    {
        viewport_ = {true, x, y, w, h, minDepth, maxDepth};
        int framebufferHeight = currentRenderTargetCount_ > 0
            ? currentRenderTargetHeight_ : 0;
        if (currentRenderTargetCount_ == 0)
        {
            int framebufferWidth = 0;
            GetPhysicalSize(framebufferWidth, framebufferHeight);
            (void)framebufferWidth;
        }
        currentViewportWidth_ = w;
        currentViewportHeight_ = h;
        int defaultX = 0;
        int defaultY = 0;
        int defaultWidth = 0;
        int defaultHeight = 0;
        GetDefaultViewportRect(
            defaultX, defaultY, defaultWidth, defaultHeight);
        viewportIsDefault_ = x == defaultX && y == defaultY &&
            w == defaultWidth && h == defaultHeight;
        Bridge::SetViewport(x, framebufferHeight - y - h, w, h, minDepth, maxDepth);
    }

    void RlglRenderer::SetScissorRect(
        const int x, const int y, const int w, const int h)
    {
        scissor_ = {true, x, y, w, h};
        int framebufferHeight = currentRenderTargetCount_ > 0
            ? currentRenderTargetHeight_ : 0;
        if (currentRenderTargetCount_ == 0)
        {
            int framebufferWidth = 0;
            GetPhysicalSize(framebufferWidth, framebufferHeight);
            (void)framebufferWidth;
        }
        Bridge::SetScissor(x, framebufferHeight - y - h, w, h);
    }
}

namespace CNA::Internal::Renderers::Rlgl
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(
        const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<RlglRenderer>(args);
    }
}
