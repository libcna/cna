// SPDX-License-Identifier: MS-PL

#include "Win32GraphicsServices.hpp"

#include "Win32Error.hpp"
#include "Win32Utf.hpp"
#include "Win32Window.hpp"

#include "../Common/SurfaceFrameValidation.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <cstring>

namespace CNA::Platform::Win32 {

    namespace {

        // --- WGL extension tokens ------------------------------------------------------------
        //
        // Spelled out rather than included: <GL/wglext.h> is not part of a MinGW sysroot, and
        // dragging in a GL header would put OpenGL's whole type system into a platform
        // translation unit for six integers.
        constexpr int kWglContextMajorVersionArb = 0x2091;
        constexpr int kWglContextMinorVersionArb = 0x2092;
        constexpr int kWglContextProfileMaskArb = 0x9126;
        constexpr int kWglContextCoreProfileBitArb = 0x00000001;
        constexpr int kWglContextCompatibilityProfileBitArb = 0x00000002;
        constexpr int kWglContextEsProfileBitExt = 0x00000004;

        using WglCreateContextAttribsArbFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
        using WglSwapIntervalExtFn = BOOL(WINAPI*)(int);

        void* ResolveGlProc(const char* const name)
        {
            // wglGetProcAddress only answers for extension entry points, and only while a context
            // is current. Core OpenGL 1.1 lives in opengl32.dll itself, so both sources are tried
            // -- a loader that skips the second returns null for glClear.
            if (void* address = reinterpret_cast<void*>(wglGetProcAddress(name)))
            {
                // Several drivers historically returned these sentinels instead of null.
                const auto value = reinterpret_cast<std::intptr_t>(address);
                if (value != 0 && value != 1 && value != 2 && value != 3 && value != -1)
                    return address;
            }
            static const HMODULE openGl = LoadLibraryW(L"opengl32.dll");
            if (openGl == nullptr)
                return nullptr;
            return reinterpret_cast<void*>(::GetProcAddress(openGl, name));
        }

        PIXELFORMATDESCRIPTOR DescribeFormat(const GlContextDescription& description)
        {
            PIXELFORMATDESCRIPTOR format{};
            format.nSize = sizeof(format);
            format.nVersion = 1;
            format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
            if (description.doubleBuffer)
                format.dwFlags |= PFD_DOUBLEBUFFER;
            format.iPixelType = PFD_TYPE_RGBA;
            format.cColorBits = 32;
            format.cAlphaBits = 8;
            format.cDepthBits = static_cast<BYTE>(std::clamp(description.depthBits, 0, 32));
            format.cStencilBits = static_cast<BYTE>(std::clamp(description.stencilBits, 0, 8));
            format.iLayerType = PFD_MAIN_PLANE;
            return format;
        }

        int ProfileBit(const GlProfile profile)
        {
            switch (profile)
            {
                case GlProfile::Core:          return kWglContextCoreProfileBitArb;
                case GlProfile::Compatibility: return kWglContextCompatibilityProfileBitArb;
                case GlProfile::Es:            return kWglContextEsProfileBitExt;
            }
            return kWglContextCoreProfileBitArb;
        }

        std::uint32_t ToBgra(const std::uint32_t rgba)
        {
            // SurfaceFrame is RGBA8 in memory order, i.e. R at the lowest address. A 32-bit GDI
            // DIB reads each DWORD as 0xAARRGGBB, so red and blue exchange places.
            return (rgba & 0xFF00FF00u) | ((rgba & 0x00FF0000u) >> 16) |
                   ((rgba & 0x000000FFu) << 16);
        }

    } // namespace

    // --- OpenGL --------------------------------------------------------------------------------

    Win32GlContext::Win32GlContext(Win32PlatformAccess& access)
        : access_(&access)
    {
    }

    Win32GlContext::~Win32GlContext()
    {
        if (!contexts_.empty())
            wglMakeCurrent(nullptr, nullptr);
        for (const ContextRecord& record : contexts_)
        {
            if (record.context != nullptr)
                wglDeleteContext(record.context);
        }
    }

    HDC Win32GlContext::DeviceContextFor(const WindowId window) const
    {
        Win32Window* const target = access_->FindWindow(window);
        if (target == nullptr)
            return nullptr;
        // CS_OWNDC means this is the window's own permanent device context, so it is not released
        // and stays valid for the window's lifetime.
        return GetDC(target->GetHwnd());
    }

    GlContextHandle Win32GlContext::CreateContext(const WindowId window,
                                                  const GlContextDescription& description)
    {
        if (!access_->GetPlatformCapabilities().openGlContext)
        {
            throw PlatformNotSupportedException(PlatformCapability::OpenGlContext,
                                                access_->GetPlatformName());
        }

        const HDC deviceContext = DeviceContextFor(window);
        if (deviceContext == nullptr)
        {
            throw PlatformException("Win32GlContext::CreateContext",
                                    "the window id does not name a live window");
        }

        const PIXELFORMATDESCRIPTOR wanted = DescribeFormat(description);
        const int format = ChoosePixelFormat(deviceContext, &wanted);
        if (format == 0)
            ThrowLastError("Win32GlContext::ChoosePixelFormat");

        // A device context accepts a pixel format exactly once. Setting it again -- which happens
        // if a caller creates a second context for the same window -- fails, so an already-set
        // format that matches is accepted rather than treated as an error.
        if (GetPixelFormat(deviceContext) == 0 &&
            SetPixelFormat(deviceContext, format, &wanted) == FALSE)
        {
            ThrowLastError("Win32GlContext::SetPixelFormat");
        }

        const HGLRC bootstrap = wglCreateContext(deviceContext);
        if (bootstrap == nullptr)
            ThrowLastError("Win32GlContext::wglCreateContext");

        HGLRC created = bootstrap;
        GlContextDescription granted = description;

        // wglCreateContextAttribsARB is itself an extension entry point, so it can only be
        // resolved while some context is current. That is the entire reason for the throwaway
        // legacy context above.
        const HGLRC previousContext = wglGetCurrentContext();
        const HDC previousDevice = wglGetCurrentDC();
        if (wglMakeCurrent(deviceContext, bootstrap) != FALSE)
        {
            const auto createAttribs = reinterpret_cast<WglCreateContextAttribsArbFn>(
                ResolveGlProc("wglCreateContextAttribsARB"));
            if (createAttribs != nullptr)
            {
                const int attributes[] = {kWglContextMajorVersionArb, description.majorVersion,
                                          kWglContextMinorVersionArb, description.minorVersion,
                                          kWglContextProfileMaskArb, ProfileBit(description.profile),
                                          0};
                if (const HGLRC modern = createAttribs(deviceContext, nullptr, attributes))
                {
                    wglMakeCurrent(nullptr, nullptr);
                    wglDeleteContext(bootstrap);
                    created = modern;
                }
            }
        }
        wglMakeCurrent(previousDevice, previousContext);

        if (created == nullptr)
        {
            throw PlatformException("Win32GlContext::CreateContext",
                                    "no OpenGL context could be created");
        }

        contexts_.push_back(ContextRecord{created, window, granted});
        return static_cast<GlContextHandle>(created);
    }

    void Win32GlContext::DestroyContext(const GlContextHandle context)
    {
        if (context == nullptr)
            return;

        const auto found = std::find_if(
            contexts_.begin(), contexts_.end(),
            [context](const ContextRecord& record) {
                return static_cast<GlContextHandle>(record.context) == context;
            });
        if (found == contexts_.end())
            return;

        if (wglGetCurrentContext() == found->context)
            wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(found->context);
        contexts_.erase(found);
    }

    void Win32GlContext::MakeCurrent(const WindowId window, const GlContextHandle context)
    {
        if (context == nullptr)
        {
            if (wglMakeCurrent(nullptr, nullptr) == FALSE)
                ThrowLastError("Win32GlContext::MakeCurrent");
            return;
        }

        const HDC deviceContext = DeviceContextFor(window);
        if (deviceContext == nullptr)
        {
            throw PlatformException("Win32GlContext::MakeCurrent",
                                    "the window id does not name a live window");
        }
        if (wglMakeCurrent(deviceContext, static_cast<HGLRC>(context)) == FALSE)
            ThrowLastError("Win32GlContext::MakeCurrent");

        const auto found = std::find_if(
            contexts_.begin(), contexts_.end(),
            [context](const ContextRecord& record) {
                return static_cast<GlContextHandle>(record.context) == context;
            });
        if (found != contexts_.end())
            found->window = window;
    }

    GlContextBinding Win32GlContext::GetCurrentBinding() const
    {
        GlContextBinding binding;
        const HGLRC current = wglGetCurrentContext();
        if (current == nullptr)
            return binding;

        binding.context = static_cast<GlContextHandle>(current);
        const auto found = std::find_if(
            contexts_.begin(), contexts_.end(),
            [current](const ContextRecord& record) { return record.context == current; });
        if (found != contexts_.end())
            binding.window = found->window;
        return binding;
    }

    void Win32GlContext::SwapBuffers(const WindowId window)
    {
        if (const HDC deviceContext = DeviceContextFor(window))
            ::SwapBuffers(deviceContext);
    }

    bool Win32GlContext::SetSwapInterval(const int interval)
    {
        const auto swapInterval =
            reinterpret_cast<WglSwapIntervalExtFn>(ResolveGlProc("wglSwapIntervalEXT"));
        if (swapInterval == nullptr)
            return false;
        return swapInterval(interval) != FALSE;
    }

    void* Win32GlContext::GetProcAddress(const std::string& name) const
    {
        return ResolveGlProc(name.c_str());
    }

    GlProcAddressLoader Win32GlContext::GetProcAddressLoader() const
    {
        return &ResolveGlProc;
    }

    GlContextDescription Win32GlContext::GetContextAttributes(const GlContextHandle context) const
    {
        const auto found = std::find_if(
            contexts_.begin(), contexts_.end(),
            [context](const ContextRecord& record) {
                return static_cast<GlContextHandle>(record.context) == context;
            });
        return found != contexts_.end() ? found->granted : GlContextDescription{};
    }

    // --- Vulkan --------------------------------------------------------------------------------

    Win32VulkanSurface::Win32VulkanSurface(Win32PlatformAccess& access)
        : access_(&access)
    {
        loader_ = LoadLibraryW(L"vulkan-1.dll");
        if (loader_ == nullptr)
            return;
        getInstanceProcAddr_ =
            reinterpret_cast<void*>(::GetProcAddress(loader_, "vkGetInstanceProcAddr"));
        if (getInstanceProcAddr_ == nullptr)
        {
            FreeLibrary(loader_);
            loader_ = nullptr;
        }
    }

    Win32VulkanSurface::~Win32VulkanSurface()
    {
        if (loader_ != nullptr)
            FreeLibrary(loader_);
    }

    bool Win32VulkanSurface::IsAvailable() const
    {
        return loader_ != nullptr && getInstanceProcAddr_ != nullptr;
    }

    std::vector<std::string> Win32VulkanSurface::GetInstanceExtensions() const
    {
        if (!access_->GetPlatformCapabilities().vulkanSurface)
        {
            throw PlatformNotSupportedException(PlatformCapability::VulkanSurface,
                                                access_->GetPlatformName());
        }
        return {"VK_KHR_surface", "VK_KHR_win32_surface"};
    }

    VulkanSurfaceHandle Win32VulkanSurface::CreateSurface(const VulkanInstanceHandle instance,
                                                          const WindowId window)
    {
        if (!access_->GetPlatformCapabilities().vulkanSurface)
        {
            throw PlatformNotSupportedException(PlatformCapability::VulkanSurface,
                                                access_->GetPlatformName());
        }
        if (instance == nullptr)
            throw PlatformException("Win32VulkanSurface::CreateSurface", "no instance given");

        Win32Window* const target = access_->FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("Win32VulkanSurface::CreateSurface",
                                    "the window id does not name a live window");
        }

        // Vulkan's own declarations, restated locally so this module never includes vulkan.h --
        // the contract deliberately keeps these handles opaque, and pulling the real header in
        // would make every consumer of this translation unit depend on the Vulkan SDK.
        struct VkWin32SurfaceCreateInfoKHR
        {
            std::uint32_t sType;
            const void* pNext;
            std::uint32_t flags;
            HINSTANCE hinstance;
            HWND hwnd;
        };
        constexpr std::uint32_t kStructureTypeWin32SurfaceCreateInfo = 1000009000;
        using PfnVoidFunction = void (*)();
        using GetInstanceProcAddrFn = PfnVoidFunction (*)(void*, const char*);
        using CreateWin32SurfaceFn = std::int32_t (*)(void*, const VkWin32SurfaceCreateInfoKHR*,
                                                      const void*, std::uint64_t*);

        const auto getProc = reinterpret_cast<GetInstanceProcAddrFn>(getInstanceProcAddr_);
        const auto createSurface = reinterpret_cast<CreateWin32SurfaceFn>(
            getProc(instance, "vkCreateWin32SurfaceKHR"));
        if (createSurface == nullptr)
        {
            throw PlatformException("Win32VulkanSurface::CreateSurface",
                                    "the instance does not export vkCreateWin32SurfaceKHR; was "
                                    "VK_KHR_win32_surface enabled?");
        }

        VkWin32SurfaceCreateInfoKHR info{};
        info.sType = kStructureTypeWin32SurfaceCreateInfo;
        info.hinstance = Win32WindowClass::GetInstance();
        info.hwnd = target->GetHwnd();

        std::uint64_t surface = 0;
        const std::int32_t result = createSurface(instance, &info, nullptr, &surface);
        if (result != 0 || surface == 0)
        {
            throw PlatformException("Win32VulkanSurface::CreateSurface",
                                    "vkCreateWin32SurfaceKHR failed with VkResult " +
                                        std::to_string(result));
        }
        return static_cast<VulkanSurfaceHandle>(surface);
    }

    void Win32VulkanSurface::DestroySurface(const VulkanInstanceHandle instance,
                                            const VulkanSurfaceHandle surface)
    {
        if (surface == 0 || instance == nullptr || !IsAvailable())
            return;

        using PfnVoidFunction = void (*)();
        using GetInstanceProcAddrFn = PfnVoidFunction (*)(void*, const char*);
        using DestroySurfaceFn = void (*)(void*, std::uint64_t, const void*);

        const auto getProc = reinterpret_cast<GetInstanceProcAddrFn>(getInstanceProcAddr_);
        if (const auto destroy =
                reinterpret_cast<DestroySurfaceFn>(getProc(instance, "vkDestroySurfaceKHR")))
        {
            destroy(instance, surface, nullptr);
        }
    }

    // --- surface presentation --------------------------------------------------------------------

    Win32SurfacePresenter::Win32SurfacePresenter(Win32Window& window)
        : window_(&window)
    {
    }

    Win32SurfacePresenter::~Win32SurfacePresenter() = default;

    void Win32SurfacePresenter::SetScaleMode(const PresentScaleMode mode, const PresentFilter filter)
    {
        scaleMode_ = mode;
        filter_ = filter;
    }

    bool Win32SurfacePresenter::SetVSync(const bool enabled)
    {
        // GDI blits are not synchronised to vertical blank and there is no supported way to make
        // them so. Recorded and reported as declined, rather than accepted and silently ignored.
        vsync_ = enabled;
        return false;
    }

    void Win32SurfacePresenter::GetTargetSize(int& width, int& height) const
    {
        const WindowSize size = window_->GetPixelSize();
        width = size.width;
        height = size.height;
    }

    void Win32SurfacePresenter::Present(const SurfaceFrame& frame)
    {
        const int stride = Common::ValidateSurfaceFrame(frame, "Win32SurfacePresenter::Present");

        int targetWidth = 0;
        int targetHeight = 0;
        GetTargetSize(targetWidth, targetHeight);
        if (targetWidth <= 0 || targetHeight <= 0)
            return;

        const std::size_t pixelCount =
            static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
        converted_.resize(pixelCount);
        for (int row = 0; row < frame.height; ++row)
        {
            const auto* source = reinterpret_cast<const std::uint32_t*>(
                frame.pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride));
            std::uint32_t* destination =
                converted_.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width);
            for (int column = 0; column < frame.width; ++column)
                destination[column] = ToBgra(source[column]);
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = frame.width;
        // Negative height selects a top-down DIB, matching SurfaceFrame's documented row order.
        info.bmiHeader.biHeight = -frame.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        // Fitting the image to the window: the destination rectangle is what implements the five
        // scale modes, so the pixels themselves are never resampled here.
        int destinationX = 0;
        int destinationY = 0;
        int destinationWidth = targetWidth;
        int destinationHeight = targetHeight;
        switch (scaleMode_)
        {
            case PresentScaleMode::Stretch:
                break;

            case PresentScaleMode::Letterbox:
            case PresentScaleMode::Overscan:
            {
                const double scaleX =
                    static_cast<double>(targetWidth) / static_cast<double>(frame.width);
                const double scaleY =
                    static_cast<double>(targetHeight) / static_cast<double>(frame.height);
                const double scale = scaleMode_ == PresentScaleMode::Letterbox
                                         ? std::min(scaleX, scaleY)
                                         : std::max(scaleX, scaleY);
                destinationWidth = static_cast<int>(static_cast<double>(frame.width) * scale);
                destinationHeight = static_cast<int>(static_cast<double>(frame.height) * scale);
                destinationX = (targetWidth - destinationWidth) / 2;
                destinationY = (targetHeight - destinationHeight) / 2;
                break;
            }

            case PresentScaleMode::None:
                destinationWidth = frame.width;
                destinationHeight = frame.height;
                destinationX = (targetWidth - destinationWidth) / 2;
                destinationY = (targetHeight - destinationHeight) / 2;
                break;

            case PresentScaleMode::Native:
                destinationWidth = frame.width;
                destinationHeight = frame.height;
                break;
        }

        const HDC deviceContext = GetDC(window_->GetHwnd());
        if (deviceContext == nullptr)
            throw PlatformException("Win32SurfacePresenter::Present", DescribeLastError());

        SetStretchBltMode(deviceContext,
                          filter_ == PresentFilter::Linear ? HALFTONE : COLORONCOLOR);
        if (filter_ == PresentFilter::Linear)
            SetBrushOrgEx(deviceContext, 0, 0, nullptr);

        // A letterboxed or unscaled image leaves margins the previous frame is still in. Clearing
        // them is what keeps the border black instead of a smear of the last larger frame.
        if (destinationX > 0 || destinationY > 0 || destinationWidth < targetWidth ||
            destinationHeight < targetHeight)
        {
            RECT full{0, 0, targetWidth, targetHeight};
            FillRect(deviceContext, &full,
                     reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }

        const int copied = StretchDIBits(
            deviceContext, destinationX, destinationY, destinationWidth, destinationHeight, 0, 0,
            frame.width, frame.height, converted_.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        // StretchDIBits returns a scan-line count in an int, and signals failure with GDI_ERROR --
        // which is a DWORD constant, so it has to be narrowed before the comparison or the
        // signed/unsigned promotion makes the test never true.
        constexpr int kGdiError = static_cast<int>(GDI_ERROR);
        const DWORD error = copied == kGdiError ? GetLastError() : 0;
        ReleaseDC(window_->GetHwnd(), deviceContext);

        if (copied == kGdiError)
            ThrowError("Win32SurfacePresenter::Present", error);
    }

} // namespace CNA::Platform::Win32
