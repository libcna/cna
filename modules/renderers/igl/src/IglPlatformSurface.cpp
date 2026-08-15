// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglPlatformSurface.hpp"

#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"

#include <igl/CommandQueue.h>
#include <igl/Device.h>
#include <igl/Framebuffer.h>

#include <algorithm>
#include <stdexcept>
#include <string>

// IGL's Linux OpenGL context is expressed in Xlib terms. Confine the X11 headers to this
// translation unit and drop their object-like macros before any CNA namespace is entered -- `None`
// and `Status` in particular collide with ordinary identifiers throughout this project.
#if defined(CNA_IGL_HAS_OPENGL) && defined(__linux__) && !defined(__ANDROID__)
#   include <igl/opengl/glx/Context.h>
#   include <igl/opengl/glx/Device.h>
#   include <igl/opengl/glx/PlatformDevice.h>
#   include <dlfcn.h>
#   undef None
#   undef Status
#   undef Success
#   undef Bool
#   undef True
#   undef False
#   undef Always
#   undef Complex
#   define CNA_IGL_SURFACE_GLX 1
#endif

#if defined(CNA_IGL_HAS_VULKAN)
#   include <igl/vulkan/Common.h>
#   include <igl/vulkan/Device.h>
#   include <igl/vulkan/HWDevice.h>
#   include <igl/vulkan/PlatformDevice.h>
#   include <igl/vulkan/VulkanContext.h>
#   define CNA_IGL_SURFACE_VULKAN 1
#endif

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        /** @brief The GL context attributes IGL's OpenGL backend needs on this window. */
        CNA::Platform::GlContextDescription RequestedGlContext(const int multiSampleCount)
        {
            CNA::Platform::GlContextDescription description;
            // IGL's OpenGL backend targets desktop GL 4.x; its own samples request 4.6 and fall
            // back through the driver. 4.1 is the floor at which every feature this renderer uses
            // (uniform blocks, instanced draws, seamless cube maps) is core.
            description.majorVersion = 4;
            description.minorVersion = 1;
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

#ifdef CNA_IGL_SURFACE_GLX
        /**
         * @brief Resolves one GLX entry point through the platform loader, then the process image.
         *
         * The platform's own loader is asked first because it is the resolver that already agrees
         * with the context CNA created. `dlsym` on the already-loaded libGL is the fallback for a
         * loader that only answers for GL (rather than GLX) names.
         */
        void* ResolveGlxSymbol(const CNA::Platform::GlProcAddressLoader loader, const char* name)
        {
            if (loader != nullptr)
            {
                if (void* resolved = loader(name); resolved != nullptr)
                    return resolved;
            }
            return dlsym(RTLD_DEFAULT, name);
        }
#endif
    }

    /**
     * @brief Backend-specific state kept out of the header so IGL's backend headers stay private.
     */
    struct IglPlatformSurface::Impl
    {
        /// Owns the platform-created GL context for the OpenGL backend; null on every other one.
        std::unique_ptr<PlatformGlContextOwner> glContext;
    };

    IglPlatformSurface::IglPlatformSurface(const GraphicsRendererCreateArgs& args,
                                           const Detail::RendererBackend backend)
        : backend_(backend)
        , surface_(args.surface, "IGL renderer")
        , impl_(std::make_unique<Impl>())
    {
        const CNA::Platform::WindowSize drawable = surface_.GetDrawableSize();
        swapChainWidth_ = std::max(1, drawable.width);
        swapChainHeight_ = std::max(1, drawable.height);
        swapInterval_ = args.swapInterval;

        CreateDevice(args);

        igl::Result result;
        commandQueue_ = device_->createCommandQueue({}, &result);
        if (!commandQueue_ || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a command queue (" +
                                     result.message + ")");
        }
    }

    IglPlatformSurface::~IglPlatformSurface()
    {
        // Release in reverse dependency order and while the GL context (if any) is still current:
        // impl_ is destroyed last because it owns that context.
        surfaceTextures_ = igl::SurfaceTextures{};
        backBuffer_.reset();
        commandQueue_.reset();
        device_.reset();
    }

    void IglPlatformSurface::CreateDevice(const GraphicsRendererCreateArgs& args)
    {
        switch (backend_)
        {
        case Detail::RendererBackend::OpenGL:
        {
#ifdef CNA_IGL_SURFACE_GLX
            CNA::Platform::X11NativeWindow x11;
            if (!CNA::Platform::TryGetX11(surface_.GetNativeHandle(), x11))
            {
                throw std::runtime_error(
                    "IGL renderer: the OpenGL backend needs an X11 native window on this platform, "
                    "got " + CNA::Platform::Describe(surface_.GetNativeHandle()));
            }

            impl_->glContext = std::make_unique<PlatformGlContextOwner>(
                RequirePlatformGlContext(args.glContext, "IGL"),
                RequirePlatformGlWindow(args.surface, "IGL"),
                RequestedGlContext(args.multiSampleCount));
            impl_->glContext->SetSwapInterval(swapInterval_);

            const CNA::Platform::GlContextDescription granted = impl_->glContext->GetAttributes();
            surfaceSampleCount_ =
                granted.multisampleBuffers > 0 && granted.multisampleSamples > 1
                    ? granted.multisampleSamples
                    : 1;

            // The platform has just made its context current on this window, so the GLX context
            // handle is exactly what glXGetCurrentContext() reports. Reading it back this way,
            // rather than casting the platform's opaque handle, keeps the renderer independent of
            // how a platform implementation happens to represent a GL context.
            using PfnGetCurrentContext = igl::opengl::glx::GLXContext (*)();
            const auto getCurrentContext = reinterpret_cast<PfnGetCurrentContext>(
                ResolveGlxSymbol(impl_->glContext->GetLoader(), "glXGetCurrentContext"));
            if (getCurrentContext == nullptr)
            {
                throw std::runtime_error(
                    "IGL renderer: glXGetCurrentContext could not be resolved, so the platform's "
                    "GL context cannot be handed to IGL");
            }

            igl::opengl::glx::GLXContext glxContext = getCurrentContext();
            if (glxContext == nullptr)
            {
                throw std::runtime_error(
                    "IGL renderer: the platform reported no current GLX context after binding one");
            }

            auto iglContext = std::make_unique<igl::opengl::glx::Context>(
                nullptr /* shared module */,
                static_cast<Display*>(x11.display),
                static_cast<igl::opengl::glx::GLXDrawable>(x11.window),
                glxContext);
            device_ = std::make_unique<igl::opengl::glx::Device>(std::move(iglContext));

            backBufferColorFormat_ = igl::TextureFormat::RGBA_UNorm8;
            backBufferDepthFormat_ = igl::TextureFormat::S8_UInt_Z24_UNorm;
            CNA::Logger::Info("IGL renderer: OpenGL backend on GLX, " +
                                  std::to_string(surfaceSampleCount_) + "x MSAA surface",
                              CNA::LogCategory::RENDER);
            return;
#else
            throw std::runtime_error(
                "IGL renderer: the OpenGL backend is not available in this build or on this "
                "platform");
#endif
        }

        case Detail::RendererBackend::Vulkan:
        {
#ifdef CNA_IGL_SURFACE_VULKAN
            CNA::Platform::X11NativeWindow x11;
            if (!CNA::Platform::TryGetX11(surface_.GetNativeHandle(), x11))
            {
                throw std::runtime_error(
                    "IGL renderer: the Vulkan backend needs an X11 native window on this platform, "
                    "got " + CNA::Platform::Describe(surface_.GetNativeHandle()));
            }

            igl::vulkan::VulkanContextConfig config;
            config.terminateOnValidationError = false;
            config.enableValidation = false;
            config.enableGPUAssistedValidation = false;
            config.enableExtraLogs = false;
            config.requestedSwapChainTextureFormat = igl::TextureFormat::RGBA_UNorm8;

            std::unique_ptr<igl::vulkan::VulkanContext> context = igl::vulkan::HWDevice::createContext(
                config,
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11.window)),
                0,
                nullptr,
                x11.display);
            if (!context)
                throw std::runtime_error("IGL renderer: could not create a Vulkan context");

            std::vector<igl::HWDeviceDesc> devices = igl::vulkan::HWDevice::queryDevices(
                *context, igl::HWDeviceQueryDesc(igl::HWDeviceType::DiscreteGpu), nullptr);
            if (devices.empty())
            {
                devices = igl::vulkan::HWDevice::queryDevices(
                    *context, igl::HWDeviceQueryDesc(igl::HWDeviceType::IntegratedGpu), nullptr);
            }
            if (devices.empty())
            {
                devices = igl::vulkan::HWDevice::queryDevices(
                    *context, igl::HWDeviceQueryDesc(igl::HWDeviceType::SoftwareGpu), nullptr);
            }
            if (devices.empty())
                throw std::runtime_error("IGL renderer: Vulkan reported no usable physical device");

            igl::Result result;
            device_ = igl::vulkan::HWDevice::create(std::move(context),
                                                    devices.front(),
                                                    static_cast<uint32_t>(swapChainWidth_),
                                                    static_cast<uint32_t>(swapChainHeight_),
                                                    0,
                                                    nullptr,
                                                    nullptr,
                                                    "CNA IGL renderer",
                                                    &result);
            if (!device_ || !result.isOk())
            {
                throw std::runtime_error("IGL renderer: could not create a Vulkan device (" +
                                         result.message + ")");
            }

            // IGL's Vulkan swap-chain images are single-sample, and this renderer does not add its
            // own resolve pass for the back buffer, so a multisample request cannot be honoured
            // here. Reporting 1 keeps GraphicsDevice.PresentationParameters.MultiSampleCount
            // truthful instead of echoing a count nothing applied.
            surfaceSampleCount_ = 1;
            backBufferColorFormat_ = igl::TextureFormat::RGBA_UNorm8;
            backBufferDepthFormat_ = igl::TextureFormat::S8_UInt_Z24_UNorm;
            CNA::Logger::Info("IGL renderer: Vulkan backend", CNA::LogCategory::RENDER);
            return;
#else
            throw std::runtime_error(
                "IGL renderer: the Vulkan backend is not available in this build");
#endif
        }
        }

        throw std::runtime_error("IGL renderer: unknown backend requested");
    }

    igl::SurfaceTextures IglPlatformSurface::AcquireSurfaceTextures()
    {
        switch (backend_)
        {
        case Detail::RendererBackend::OpenGL:
        {
#ifdef CNA_IGL_SURFACE_GLX
            auto* platformDevice = device_->getPlatformDevice<igl::opengl::glx::PlatformDevice>();
            if (platformDevice == nullptr)
                throw std::runtime_error("IGL renderer: no GLX platform device on this IGL device");

            igl::Result result;
            std::shared_ptr<igl::ITexture> color = platformDevice->createTextureFromNativeDrawable(
                static_cast<uint32_t>(swapChainWidth_),
                static_cast<uint32_t>(swapChainHeight_),
                &result);
            if (!color || !result.isOk())
            {
                throw std::runtime_error(
                    "IGL renderer: could not resolve the default framebuffer's colour surface (" +
                    result.message + ")");
            }

            std::shared_ptr<igl::ITexture> depth = platformDevice->createTextureFromNativeDepth(
                static_cast<uint32_t>(swapChainWidth_),
                static_cast<uint32_t>(swapChainHeight_),
                &result);
            if (!depth || !result.isOk())
            {
                // A window without a depth visual is a legitimate configuration; the renderer then
                // reports no depth/stencil support rather than failing to start.
                depth.reset();
                backBufferDepthFormat_ = igl::TextureFormat::Invalid;
            }

            surfaceTextures_ = igl::SurfaceTextures{std::move(color), std::move(depth)};
            return surfaceTextures_;
#else
            break;
#endif
        }

        case Detail::RendererBackend::Vulkan:
        {
#ifdef CNA_IGL_SURFACE_VULKAN
            auto* platformDevice = device_->getPlatformDevice<igl::vulkan::PlatformDevice>();
            if (platformDevice == nullptr)
                throw std::runtime_error("IGL renderer: no Vulkan platform device on this IGL device");

            igl::Result result;
            std::shared_ptr<igl::ITexture> color =
                platformDevice->createTextureFromNativeDrawable(&result);
            if (!color || !result.isOk())
            {
                throw std::runtime_error("IGL renderer: could not acquire a swap-chain image (" +
                                         result.message + ")");
            }

            std::shared_ptr<igl::ITexture> depth = platformDevice->createTextureFromNativeDepth(
                static_cast<uint32_t>(swapChainWidth_),
                static_cast<uint32_t>(swapChainHeight_),
                &result);
            if (!depth || !result.isOk())
            {
                depth.reset();
                backBufferDepthFormat_ = igl::TextureFormat::Invalid;
            }

            backBufferColorFormat_ = color->getFormat();
            surfaceTextures_ = igl::SurfaceTextures{std::move(color), std::move(depth)};
            return surfaceTextures_;
#else
            break;
#endif
        }
        }

        throw std::runtime_error("IGL renderer: the selected backend cannot acquire a drawable");
    }

    const std::shared_ptr<igl::IFramebuffer>& IglPlatformSurface::AcquireBackBufferFramebuffer()
    {
        const igl::SurfaceTextures textures = AcquireSurfaceTextures();

        if (!backBuffer_)
        {
            igl::FramebufferDesc desc;
            desc.colorAttachments[0].texture = textures.color;
            desc.depthAttachment.texture = textures.depth;
            desc.stencilAttachment.texture = textures.depth;
            desc.debugName = "CNA back buffer";

            igl::Result result;
            backBuffer_ = device_->createFramebuffer(desc, &result);
            if (!backBuffer_ || !result.isOk())
            {
                throw std::runtime_error(
                    "IGL renderer: could not create the back-buffer framebuffer (" +
                    result.message + ")");
            }
            return backBuffer_;
        }

        backBuffer_->updateDrawable(textures);
        return backBuffer_;
    }

    void IglPlatformSurface::Present()
    {
        // Both backends present through ICommandBuffer::present(), issued by the renderer alongside
        // the frame's submission; there is deliberately nothing left to do here. The hook exists so
        // a future backend that needs a separate swap step has one place to implement it, rather
        // than the renderer growing a backend test.
    }

    void IglPlatformSurface::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);

        const CNA::Platform::WindowSize drawable = surface_.GetDrawableSize();
        const int width = std::max(1, drawable.width);
        const int height = std::max(1, drawable.height);
        if (width == swapChainWidth_ && height == swapChainHeight_)
            return;

        swapChainWidth_ = width;
        swapChainHeight_ = height;
        ResizeSwapChain();
    }

    void IglPlatformSurface::ResizeSwapChain()
    {
        // The framebuffer is dropped rather than re-pointed: its attachments are sized surfaces on
        // both backends, so the next AcquireBackBufferFramebuffer() rebuilds it around the new ones.
        backBuffer_.reset();
        surfaceTextures_ = igl::SurfaceTextures{};

        switch (backend_)
        {
        case Detail::RendererBackend::OpenGL:
            // The default framebuffer follows the window itself; IGL's stand-in textures are
            // recreated by the next createTextureFromNativeDrawable() call at the new size.
            return;

        case Detail::RendererBackend::Vulkan:
#ifdef CNA_IGL_SURFACE_VULKAN
        {
            auto* vulkanDevice = static_cast<igl::vulkan::Device*>(device_.get());
            if (auto* platformDevice = device_->getPlatformDevice<igl::vulkan::PlatformDevice>())
                platformDevice->clear();
            const igl::Result result = vulkanDevice->getVulkanContext().initSwapchain(
                static_cast<uint32_t>(swapChainWidth_), static_cast<uint32_t>(swapChainHeight_));
            if (!result.isOk())
            {
                throw std::runtime_error("IGL renderer: swap-chain resize failed (" +
                                         result.message + ")");
            }
        }
#endif
            return;
        }
    }

    bool IglPlatformSurface::SetSwapInterval(const int interval)
    {
        swapInterval_ = interval;
        if (impl_->glContext != nullptr)
            return impl_->glContext->SetSwapInterval(interval);

        // IGL's Vulkan swap chain fixes its present mode when it is created, and this renderer does
        // not tear the swap chain down to change it. Reporting false is the interface's own way of
        // saying the platform declined, which is exactly what happened.
        return false;
    }
}
