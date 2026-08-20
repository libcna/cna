// SPDX-License-Identifier: MS-PL
#pragma once

// Test-only adapters for renderer-owned integration executables that deliberately create raw SDL
// windows. Production renderers must use CNA::Platform services; these adapters let the old direct
// pixel tests exercise that same interface without reaching back into production with SDL types.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace CNA::Examples
{
    inline CNA::Internal::Renderers::RendererSurfaceInfo SdlTestSurface(SDL_Window* window)
    {
        CNA::Internal::Renderers::RendererSurfaceInfo surface;
        if (window == nullptr) return surface;
        surface.windowId = SDL_GetWindowID(window);
        SDL_GetWindowSizeInPixels(window, &surface.drawableSize.width, &surface.drawableSize.height);
        surface.displayScale = SDL_GetWindowPixelDensity(window);
        if (!(surface.displayScale > 0.0f)) surface.displayScale = 1.0f;

        const char* driver = SDL_GetCurrentVideoDriver();
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        if (driver == nullptr || properties == 0) return surface;

        using CNA::Platform::NativeWindowSystem;
        if (std::strcmp(driver, "windows") == 0)
        {
            surface.nativeHandle.system = NativeWindowSystem::Win32;
            surface.nativeHandle.window = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        }
        else if (std::strcmp(driver, "x11") == 0)
        {
            surface.nativeHandle.system = NativeWindowSystem::X11;
            surface.nativeHandle.display = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            surface.nativeHandle.windowId = static_cast<std::uint64_t>(SDL_GetNumberProperty(
                properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        }
        else if (std::strcmp(driver, "wayland") == 0)
        {
            surface.nativeHandle.system = NativeWindowSystem::Wayland;
            surface.nativeHandle.display = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            surface.nativeHandle.surface = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        }
        else if (std::strcmp(driver, "cocoa") == 0)
        {
            surface.nativeHandle.system = NativeWindowSystem::Cocoa;
            surface.nativeHandle.window = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        }
        else if (std::strcmp(driver, "android") == 0)
        {
            surface.nativeHandle.system = NativeWindowSystem::Android;
            surface.nativeHandle.window = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
        }
        else if (std::strcmp(driver, "emscripten") == 0)
        {
            surface.nativeHandle.system = NativeWindowSystem::Web;
        }
        else
        {
            surface.nativeHandle.system = NativeWindowSystem::Headless;
        }
        return surface;
    }

    inline CNA::Internal::Renderers::GraphicsRendererCreateArgs SdlTestRendererArgs(
        SDL_Window* window,
        CNA::Platform::IPlatformGlContext* glContext,
        CNA::Platform::IPlatformSurfacePresenter* presenter,
        const int virtualWidth,
        const int virtualHeight,
        const CNA::Internal::Renderers::CnaPresentationMode presentationMode,
        const int swapInterval = 1,
        std::function<void(CNA::Internal::Renderers::RendererDeviceEvent)> deviceEventCallback = {})
    {
        CNA::Internal::Renderers::GraphicsRendererCreateArgs args;
        args.surface = SdlTestSurface(window);
        args.glContext = glContext;
        args.surfacePresenter = presenter;
        args.virtualWidth = virtualWidth;
        args.virtualHeight = virtualHeight;
        args.presentationMode = presentationMode;
        args.swapInterval = swapInterval;
        args.deviceEventCallback = std::move(deviceEventCallback);
        return args;
    }

    class SdlTestGlContext final : public CNA::Platform::IPlatformGlContext
    {
    public:
        explicit SdlTestGlContext(SDL_Window* window, const bool forceMakeCurrentFailure = false)
            : window_(window), forceMakeCurrentFailure_(forceMakeCurrentFailure)
        {
        }

        [[nodiscard]] int DestroyCount() const noexcept { return destroyCount_; }

        /**
         * @brief Test-only: makes GetContextAttributes() report @p attributes instead of what the
         * driver actually granted.
         *
         * A renderer that refuses an inadequate context can otherwise only be tested on hardware
         * that provides one, which is no test at all. Overriding what the platform REPORTS -- the
         * exact value such a renderer reads -- exercises the refusal deterministically without
         * needing a deliberately crippled GL context. Pass std::nullopt to go back to reporting
         * the real granted attributes.
         */
        void SetReportedAttributesForTesting(
            std::optional<CNA::Platform::GlContextDescription> attributes)
        {
            reportedAttributes_ = std::move(attributes);
        }

        [[nodiscard]] CNA::Platform::GlContextHandle CreateContext(
            const CNA::Platform::WindowId window,
            const CNA::Platform::GlContextDescription& description) override
        {
            if (window_ == nullptr || window != SDL_GetWindowID(window_))
                throw CNA::Platform::PlatformException("SdlTestGlContext::CreateContext",
                                                       "unknown test window");
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, description.majorVersion);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, description.minorVersion);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                description.profile == CNA::Platform::GlProfile::Core ? SDL_GL_CONTEXT_PROFILE_CORE
                : description.profile == CNA::Platform::GlProfile::Compatibility
                    ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY : SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, description.depthBits);
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, description.stencilBits);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, description.doubleBuffer ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, description.multisampleBuffers);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, description.multisampleSamples);
            SDL_GLContext context = SDL_GL_CreateContext(window_);
            if (context == nullptr)
                throw CNA::Platform::PlatformException("SdlTestGlContext::CreateContext", SDL_GetError());
            return context;
        }

        void DestroyContext(const CNA::Platform::GlContextHandle context) override
        {
            if (context != nullptr)
            {
                ++destroyCount_;
                SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context));
            }
        }

        void MakeCurrent(const CNA::Platform::WindowId window,
                         const CNA::Platform::GlContextHandle context) override
        {
            if (forceMakeCurrentFailure_ && context != nullptr)
                throw CNA::Platform::PlatformException(
                    "SdlTestGlContext::MakeCurrent", "injected test failure");
            if (window_ == nullptr || window != SDL_GetWindowID(window_)
                || !SDL_GL_MakeCurrent(window_, static_cast<SDL_GLContext>(context)))
                throw CNA::Platform::PlatformException("SdlTestGlContext::MakeCurrent", SDL_GetError());
        }

        void SwapBuffers(const CNA::Platform::WindowId window) override
        {
            if (window_ == nullptr || window != SDL_GetWindowID(window_) || !SDL_GL_SwapWindow(window_))
                throw CNA::Platform::PlatformException("SdlTestGlContext::SwapBuffers", SDL_GetError());
        }

        bool SetSwapInterval(const int interval) override { return SDL_GL_SetSwapInterval(interval); }

        [[nodiscard]] void* GetProcAddress(const std::string& name) const override
        {
            return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name.c_str()));
        }

        [[nodiscard]] CNA::Platform::GlProcAddressLoader GetProcAddressLoader() const override
        {
            return &LoadProc;
        }

        [[nodiscard]] CNA::Platform::GlContextDescription GetContextAttributes(
            CNA::Platform::GlContextHandle) const override
        {
            if (reportedAttributes_.has_value()) return *reportedAttributes_;
            CNA::Platform::GlContextDescription result;
            int profile = 0;
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &result.majorVersion);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &result.minorVersion);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
            result.profile = profile == SDL_GL_CONTEXT_PROFILE_CORE ? CNA::Platform::GlProfile::Core
                : profile == SDL_GL_CONTEXT_PROFILE_ES ? CNA::Platform::GlProfile::Es
                                                       : CNA::Platform::GlProfile::Compatibility;
            SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &result.depthBits);
            SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &result.stencilBits);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &result.multisampleBuffers);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &result.multisampleSamples);
            int doubleBuffer = 0;
            SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &doubleBuffer);
            result.doubleBuffer = doubleBuffer != 0;
            return result;
        }

    private:
        static void* LoadProc(const char* name)
        {
            return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
        }

        std::optional<CNA::Platform::GlContextDescription> reportedAttributes_;
        SDL_Window* window_ = nullptr;
        bool forceMakeCurrentFailure_ = false;
        int destroyCount_ = 0;
    };

    class SdlTestSurfacePresenter final : public CNA::Platform::IPlatformSurfacePresenter
    {
    public:
        explicit SdlTestSurfacePresenter(SDL_Window* window) : window_(window)
        {
            if (window_ == nullptr)
                throw CNA::Platform::PlatformException("SdlTestSurfacePresenter", "null window");
            renderer_ = SDL_CreateRenderer(window_, nullptr);
            if (renderer_ == nullptr)
                throw CNA::Platform::PlatformException("SdlTestSurfacePresenter", SDL_GetError());
        }

        ~SdlTestSurfacePresenter() override
        {
            if (texture_ != nullptr) SDL_DestroyTexture(texture_);
            if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
        }

        void SetScaleMode(const CNA::Platform::PresentScaleMode mode,
                          const CNA::Platform::PresentFilter filter) override
        {
            mode_ = mode;
            filter_ = filter;
            if (texture_ != nullptr) ApplyFilter();
        }

        bool SetVSync(const bool enabled) override
        {
            return SDL_SetRenderVSync(renderer_, enabled ? 1 : SDL_RENDERER_VSYNC_DISABLED);
        }

        void Present(const CNA::Platform::SurfaceFrame& frame) override
        {
            if (frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0)
                throw CNA::Platform::PlatformException("SdlTestSurfacePresenter::Present",
                                                       "invalid frame");
            EnsureTexture(frame.width, frame.height);
            if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels,
                                   frame.strideBytes > 0 ? frame.strideBytes : frame.width * 4))
                throw CNA::Platform::PlatformException("SdlTestSurfacePresenter::Present", SDL_GetError());

            const SDL_RendererLogicalPresentation logical =
                mode_ == CNA::Platform::PresentScaleMode::Letterbox ? SDL_LOGICAL_PRESENTATION_LETTERBOX
                : mode_ == CNA::Platform::PresentScaleMode::Overscan ? SDL_LOGICAL_PRESENTATION_OVERSCAN
                : mode_ == CNA::Platform::PresentScaleMode::Stretch ? SDL_LOGICAL_PRESENTATION_STRETCH
                                                                    : SDL_LOGICAL_PRESENTATION_DISABLED;
            SDL_SetRenderLogicalPresentation(renderer_, frame.width, frame.height, logical);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderClear(renderer_);

            SDL_FRect unscaled{};
            const SDL_FRect* destination = nullptr;
            if (mode_ == CNA::Platform::PresentScaleMode::None
                || mode_ == CNA::Platform::PresentScaleMode::Native)
            {
                int targetWidth = 0;
                int targetHeight = 0;
                GetTargetSize(targetWidth, targetHeight);
                unscaled.x = mode_ == CNA::Platform::PresentScaleMode::None
                    ? static_cast<float>(targetWidth - frame.width) * 0.5f : 0.0f;
                unscaled.y = mode_ == CNA::Platform::PresentScaleMode::None
                    ? static_cast<float>(targetHeight - frame.height) * 0.5f : 0.0f;
                unscaled.w = static_cast<float>(frame.width);
                unscaled.h = static_cast<float>(frame.height);
                destination = &unscaled;
            }
            SDL_RenderTexture(renderer_, texture_, nullptr, destination);
            SDL_RenderPresent(renderer_);
        }

        void GetTargetSize(int& width, int& height) const override
        {
            if (!SDL_GetRenderOutputSize(renderer_, &width, &height)) width = height = 0;
        }

    private:
        void ApplyFilter()
        {
            SDL_SetTextureScaleMode(texture_, filter_ == CNA::Platform::PresentFilter::Linear
                ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        }

        void EnsureTexture(const int width, const int height)
        {
            if (texture_ != nullptr && textureWidth_ == width && textureHeight_ == height) return;
            if (texture_ != nullptr) SDL_DestroyTexture(texture_);
            texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING, width, height);
            if (texture_ == nullptr)
                throw CNA::Platform::PlatformException("SdlTestSurfacePresenter::Present", SDL_GetError());
            textureWidth_ = width;
            textureHeight_ = height;
            ApplyFilter();
        }

        SDL_Window* window_ = nullptr;
        SDL_Renderer* renderer_ = nullptr;
        SDL_Texture* texture_ = nullptr;
        int textureWidth_ = 0;
        int textureHeight_ = 0;
        CNA::Platform::PresentScaleMode mode_ = CNA::Platform::PresentScaleMode::Letterbox;
        CNA::Platform::PresentFilter filter_ = CNA::Platform::PresentFilter::Linear;
    };
} // namespace CNA::Examples
