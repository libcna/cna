// SPDX-License-Identifier: MS-PL
#pragma once
// Native observation for presentation tests. This reads the selected X11 WINDOW after swap,
// never the CNA backbuffer. Kept in shared test support; no native API enters production contracts.
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include <bit>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__linux__) && __has_include(<X11/Xlib.h>) && __has_include(<X11/Xutil.h>)
#define CNA_GAP08_X11_CAPTURE 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#undef None
#endif
namespace CNA::Testing {
#if defined(CNA_GAP08_X11_CAPTURE)
class PresentedWindow
{
public:
    PresentedWindow(const Platform::NativeWindowHandle& handle, int width, int height,
                    bool createForeign = false)
        : width_(width), height_(height)
    {
        Platform::X11NativeWindow native;
        if (!Platform::TryGetX11(handle, native))
            throw std::runtime_error("requires a native X11 window");
        display_ = static_cast<Display*>(native.display);
        xid_ = static_cast<Window>(native.window);
        library_ = dlopen("libX11.so.6", RTLD_LAZY | RTLD_NOLOAD);
        if (!library_) throw std::runtime_error("X11 native window exists without loaded libX11");
        if (createForeign)
        {
            const Window root = Symbol<Window (*)(Display*)>("XDefaultRootWindow")(display_);
            xid_ = Symbol<Window (*)(Display*, Window, int, int, unsigned, unsigned,
                                    unsigned, unsigned long, unsigned long)>("XCreateSimpleWindow")(
                display_, root, 32, 32, width, height, 0, 0, 0);
            if (!xid_) throw std::runtime_error("XCreateSimpleWindow failed");
            owns_ = true;
            Symbol<int (*)(Display*, Window)>("XMapWindow")(display_, xid_);
        }
        Sync();
    }
    ~PresentedWindow()
    {
        if (owns_) Symbol<int (*)(Display*, Window)>("XDestroyWindow")(display_, xid_);
        if (library_) dlclose(library_);
    }
    PresentedWindow(const PresentedWindow&) = delete;
    PresentedWindow& operator=(const PresentedWindow&) = delete;
    std::uintptr_t NativeToken() const { return static_cast<std::uintptr_t>(xid_); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsAlive() const
    {
        XWindowAttributes a{};
        return Symbol<Status (*)(Display*,Window,XWindowAttributes*)>("XGetWindowAttributes")(
            display_, xid_, &a) != 0;
    }
    void Fill(std::uint32_t rgb)
    {
        XWindowAttributes a{};
        if (!Symbol<Status (*)(Display*,Window,XWindowAttributes*)>("XGetWindowAttributes")(
                display_, xid_, &a)) throw std::runtime_error("window disappeared");
        if (!a.visual->red_mask || !a.visual->green_mask || !a.visual->blue_mask)
            throw std::runtime_error("native capture requires a TrueColor visual");
        const auto pack = [](unsigned v, unsigned long mask) {
            const auto shift = std::countr_zero(mask);
            return ((static_cast<unsigned long>(v) * (mask >> shift) / 255ul) << shift) & mask;
        };
        const unsigned long pixel = pack((rgb >> 16) & 255, a.visual->red_mask) |
                                    pack((rgb >> 8) & 255, a.visual->green_mask) |
                                    pack(rgb & 255, a.visual->blue_mask);
        GC gc = Symbol<GC (*)(Display*, Drawable, unsigned long, XGCValues*)>("XCreateGC")(
            display_, xid_, 0, nullptr);
        if (!gc) throw std::runtime_error("XCreateGC failed");
        Symbol<int (*)(Display*,GC,unsigned long)>("XSetForeground")(display_,gc,pixel);
        Symbol<int (*)(Display*,Drawable,GC,int,int,unsigned,unsigned)>("XFillRectangle")(
            display_,xid_,gc,0,0,width_,height_);
        Symbol<int (*)(Display*,GC)>("XFreeGC")(display_,gc);
        Sync();
    }
    std::vector<std::uint32_t> Capture() const
    {
        Sync();
        XImage* image = Symbol<XImage* (*)(Display*,Drawable,int,int,unsigned,unsigned,
                                          unsigned long,int)>("XGetImage")(
            display_,xid_,0,0,width_,height_,AllPlanes,ZPixmap);
        if (!image) throw std::runtime_error("XGetImage failed on the presented native window");
        std::vector<std::uint32_t> rgb;
        rgb.reserve(static_cast<std::size_t>(width_)*height_);
        const auto channel = [](unsigned long p, unsigned long mask) -> std::uint32_t {
            if (!mask) return 0;
            const auto shift = std::countr_zero(mask);
            return static_cast<std::uint32_t>(((p & mask) >> shift) * 255ul / (mask >> shift));
        };
        for (int y=0; y<height_; ++y) for (int x=0; x<width_; ++x)
        {
            const unsigned long value = XGetPixel(image,x,y);
            rgb.push_back((channel(value,image->red_mask)<<16) |
                          (channel(value,image->green_mask)<<8) | channel(value,image->blue_mask));
        }
        XDestroyImage(image);
        return rgb;
    }
    template<class Match> bool WaitFor(Platform::IPlatform& platform, Match match) const
    {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do {
            std::vector<Platform::PlatformEvent> events;
            platform.PollEvents(events);
            if (match(Capture())) return true;
            platform.Delay(10);
        } while (std::chrono::steady_clock::now()<end);
        return false;
    }
private:
    template<class F> F Symbol(const char* name) const
    {
        auto fn = reinterpret_cast<F>(dlsym(library_,name));
        if (!fn) throw std::runtime_error(std::string("missing X11 observation function ")+name);
        return fn;
    }
    void Sync() const { Symbol<int (*)(Display*,Bool)>("XSync")(display_,False); }
    Display* display_=nullptr;
    Window xid_=0;
    void* library_=nullptr;
    bool owns_=false;
    int width_=0, height_=0;
};
#endif
} // namespace CNA::Testing
