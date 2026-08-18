// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers
{
    /**
     * @brief Requires the narrow GL service used by a context-backed renderer.
     *
     * A missing service is a platform capability refusal, not a renderer-specific null-pointer
     * failure. Keeping that distinction here gives every standalone GL family the same boundary.
     */
    inline CNA::Platform::IPlatformGlContext& RequirePlatformGlContext(
        CNA::Platform::IPlatformGlContext* service, const char* rendererName)
    {
        if (service == nullptr)
        {
            throw CNA::Platform::PlatformNotSupportedException(
                CNA::Platform::PlatformCapability::OpenGlContext, rendererName);
        }
        return *service;
    }

    /** @brief Requires a real platform window identity for a window-backed GL renderer. */
    inline CNA::Platform::WindowId RequirePlatformGlWindow(
        const RendererSurfaceInfo& surface, const char* rendererName)
    {
        if (surface.windowId == 0)
        {
            throw CNA::Platform::PlatformException(
                std::string(rendererName) + "::CreateContext",
                "surface has no platform window id");
        }
        return surface.windowId;
    }

    // One renderer implementation is selected per process, so its current platform loader is a
    // process-wide dispatch edge just like the GL function-pointer tables it initialises. The
    // callback itself is owned by the platform and remains valid for the service's lifetime.
    inline CNA::Platform::GlProcAddressLoader platformGlProcAddressLoader = nullptr;

    /** @brief Resolves a GL entry point through the active platform context service. */
    inline void* LoadPlatformGlProcAddress(const char* name)
    {
        return platformGlProcAddressLoader != nullptr ? platformGlProcAddressLoader(name) : nullptr;
    }

    /**
     * @brief RAII owner for a platform-created GL context.
     *
     * Declare this before GL resource members: C++ then destroys those resources first and keeps
     * their context current until the last GL destructor has run. Creation is transactional;
     * failure to bind a newly-created context destroys it before propagating the exception.
     */
    class PlatformGlContextOwner final
    {
    public:
        PlatformGlContextOwner(CNA::Platform::IPlatformGlContext& service,
                               const CNA::Platform::WindowId window,
                               CNA::Platform::GlContextDescription description)
            : service_(service), window_(window), description_(std::move(description))
        {
            CreateAndBind();
        }

        ~PlatformGlContextOwner()
        {
            // Unbind before destroying, exactly as Recreate() below already does. Destroying a
            // context that is still current leaves the platform's GL state pointing at a dead
            // context, and on GLX that survives the window: the next SDL video-subsystem
            // initialisation in the same process then fails with "x11 not available".
            //
            // The asymmetry was invisible while GraphicsDevice held a surplus video-subsystem
            // reference, because the subsystem never actually shut down and so never had to come
            // back up. `next`'s RTR-P5-15 fix balanced that reference, and the IGL renderer -- the
            // one that drives GLX through igl::opengl::glx::Context, which does its own
            // glXMakeCurrent and context registration -- immediately could not create a second
            // device in a process. IGL's Vulkan backend, OPENGLES3 and OPENGL1 all survived the
            // same loop, which is what narrowed it to this line.
            service_.MakeCurrent(window_, nullptr);
            service_.DestroyContext(context_);
        }

        PlatformGlContextOwner(const PlatformGlContextOwner&) = delete;
        PlatformGlContextOwner& operator=(const PlatformGlContextOwner&) = delete;

        /** @brief Replaces the current context using the original description. */
        void Recreate()
        {
            if (context_ != nullptr)
            {
                service_.MakeCurrent(window_, nullptr);
                service_.DestroyContext(context_);
                context_ = nullptr;
            }
            CreateAndBind();
        }

        /** @brief Presents this context's window back buffer. */
        void SwapBuffers() { service_.SwapBuffers(window_); }

        /** @brief Re-establishes this context as current after another GL consumer changed it. */
        void MakeCurrent() { service_.MakeCurrent(window_, context_); }

        /** @brief Applies a swap interval to the current context. */
        bool SetSwapInterval(const int interval) { return service_.SetSwapInterval(interval); }

        /** @brief Returns the context attributes the driver actually granted. */
        [[nodiscard]] CNA::Platform::GlContextDescription GetAttributes() const
        {
            return service_.GetContextAttributes(context_);
        }

        /** @brief Returns the platform entry-point loader used by GL dispatch tables. */
        [[nodiscard]] CNA::Platform::GlProcAddressLoader GetLoader() const
        {
            return service_.GetProcAddressLoader();
        }

    private:
        void CreateAndBind()
        {
            context_ = service_.CreateContext(window_, description_);
            if (context_ == nullptr)
            {
                throw CNA::Platform::PlatformException(
                    "PlatformGlContextOwner::CreateContext", "platform returned a null context");
            }
            try
            {
                service_.MakeCurrent(window_, context_);
            }
            catch (...)
            {
                service_.DestroyContext(context_);
                context_ = nullptr;
                throw;
            }
            platformGlProcAddressLoader = service_.GetProcAddressLoader();
        }

        CNA::Platform::IPlatformGlContext& service_;
        CNA::Platform::WindowId window_ = 0;
        CNA::Platform::GlContextDescription description_;
        CNA::Platform::GlContextHandle context_ = nullptr;
    };

    /**
     * @brief Mutable platform-neutral window snapshot shared by standalone GL renderers.
     *
     * Drawable dimensions are physical framebuffer pixels. Window input coordinates use logical
     * client units, related by displayScale. Surface updates may resize or rescale a window but
     * may not silently retarget a renderer to a different stable window id.
     */
    class PlatformGlSurfaceState final
    {
    public:
        explicit PlatformGlSurfaceState(const RendererSurfaceInfo& surface)
            : window_(surface.windowId)
        {
            Update(surface);
        }

        /** @brief Refreshes size, scale, and native-handle snapshot data. */
        void Update(const RendererSurfaceInfo& surface)
        {
            if (surface.windowId != window_)
            {
                throw CNA::Platform::PlatformException(
                    "PlatformGlSurfaceState::Update", "stable window id changed");
            }
            surface_ = surface;
            if (!(surface_.displayScale > 0.0f)) surface_.displayScale = 1.0f;
        }

        /** @brief Returns physical drawable dimensions. */
        void GetDrawableSize(int& width, int& height) const
        {
            width = surface_.drawableSize.width;
            height = surface_.drawableSize.height;
        }

        /** @brief Returns the stable platform window identity. */
        [[nodiscard]] CNA::Platform::WindowId GetWindowId() const { return window_; }

        /** @brief Returns logical client dimensions. */
        void GetClientSize(int& width, int& height) const
        {
            width = static_cast<int>(std::lround(
                static_cast<double>(surface_.drawableSize.width) / surface_.displayScale));
            height = static_cast<int>(std::lround(
                static_cast<double>(surface_.drawableSize.height) / surface_.displayScale));
        }

        /** @brief Converts one logical client coordinate to physical drawable units. */
        [[nodiscard]] float WindowToDrawable(const float value) const
        {
            return value * surface_.displayScale;
        }

        /** @brief Converts one physical drawable coordinate to logical client units. */
        [[nodiscard]] float DrawableToWindow(const float value) const
        {
            return value / surface_.displayScale;
        }

    private:
        CNA::Platform::WindowId window_ = 0;
        RendererSurfaceInfo surface_;
    };
}
