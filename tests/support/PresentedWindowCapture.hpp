// SPDX-License-Identifier: MS-PL
#pragma once
// Native observation only: selected X11 window or its Composite backing pixmap, never CNA pixels.
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(__linux__) && __has_include(<X11/Xlib.h>) && __has_include(<X11/Xutil.h>)
#define CNA_GAP08_X11_CAPTURE 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#undef None
#endif
namespace CNA::Testing {
enum class PresentedCaptureMethod { Window, Composite, Auto };
namespace PresentedCaptureDetail {
inline PresentedCaptureMethod ParseMethod(std::string_view value, PresentedCaptureMethod fallback)
{
    if (value.empty()) return fallback;
    if (value=="window") return PresentedCaptureMethod::Window;
    if (value=="composite") return PresentedCaptureMethod::Composite;
    if (value=="auto") return PresentedCaptureMethod::Auto;
    throw std::invalid_argument("CNA_PRESENT_CAPTURE_METHOD must be window, composite or auto");
}
inline const char* MethodName(PresentedCaptureMethod method)
{
    switch (method) {
        case PresentedCaptureMethod::Window: return "window";
        case PresentedCaptureMethod::Composite: return "composite";
        case PresentedCaptureMethod::Auto: return "auto";
    }
    return "invalid";
}
inline std::string GeometryProblem(int requestedWidth,int requestedHeight,int actualWidth,int actualHeight,int mapState)
{
    if (requestedWidth<=0 || requestedHeight<=0 || requestedWidth!=actualWidth || requestedHeight!=actualHeight)
        return "capture geometry mismatch: requested "+std::to_string(requestedWidth)+"x"+std::to_string(requestedHeight)+
               ", actual client "+std::to_string(actualWidth)+"x"+std::to_string(actualHeight);
    // Xlib IsViewable=2. Do not turn an unmapped/undersized drawable into an accepted crop.
    if (mapState!=2) return "capture window is not viewable (map_state="+std::to_string(mapState)+")";
    return {};
}
inline std::uint32_t Channel(unsigned long pixel,unsigned long mask)
{
    if (!mask) return 0;
    const auto shift=std::countr_zero(mask);
    return static_cast<std::uint32_t>(((pixel&mask)>>shift)*255ul/(mask>>shift));
}
inline std::uint32_t Rgb(unsigned long pixel,unsigned long red,unsigned long green,unsigned long blue)
{
    return (Channel(pixel,red)<<16)|(Channel(pixel,green)<<8)|Channel(pixel,blue);
}
// Fallback is on transport failure ONLY, never on whether colors match the test oracle.
template<class Composite,class Window>
std::optional<std::vector<std::uint32_t>> Read(PresentedCaptureMethod method,Composite composite,Window window)
{
    if (method!=PresentedCaptureMethod::Window) {
        auto result=composite();
        if (result || method==PresentedCaptureMethod::Composite) return result;
    }
    return window();
}
} // namespace PresentedCaptureDetail
#if defined(CNA_GAP08_X11_CAPTURE)
class PresentedWindow
{
public:
    // SOFTWARE callers retain the direct-window default. GPU fixtures request Auto explicitly.
    PresentedWindow(const Platform::NativeWindowHandle& handle,int width,int height,
                    bool createForeign=false,PresentedCaptureMethod preferred=PresentedCaptureMethod::Window)
        : width_(width),height_(height)
    {
        Platform::X11NativeWindow native;
        if (!Platform::TryGetX11(handle,native)) throw std::runtime_error("capture requires native X11");
        library_=dlopen("libX11.so.6",RTLD_LAZY|RTLD_NOLOAD);
        if (!library_) throw std::runtime_error("X11 native window exists without loaded libX11");
        try {
            const char* setting=std::getenv("CNA_PRESENT_CAPTURE_METHOD");
            method_=PresentedCaptureDetail::ParseMethod(setting?setting:"",preferred);
            diagnostic_=std::getenv("CNA_PRESENT_CAPTURE_DIAGNOSTICS")!=nullptr;
            // Separate observer connection: never consume SDL's events or attribute its X errors
            // to a capture probe. Use the supplied native display, never an implicit live DISPLAY.
            const char* name=Symbol<decltype(&XDisplayString)>("XDisplayString")(static_cast<Display*>(native.display));
            if (!name || !*name) throw std::runtime_error("capture native display has no explicit name");
            display_=Symbol<decltype(&XOpenDisplay)>("XOpenDisplay")(name);
            if (!display_) throw std::runtime_error("capture cannot open observer on supplied native display");
            xid_=static_cast<Window>(native.window);
            if (createForeign) {
                const Window root=Symbol<decltype(&XDefaultRootWindow)>("XDefaultRootWindow")(display_);
                xid_=Symbol<decltype(&XCreateSimpleWindow)>("XCreateSimpleWindow")(
                    display_,root,32,32,width,height,0,0,0);
                if (!xid_) throw std::runtime_error("capture XCreateSimpleWindow failed");
                Symbol<decltype(&XMapWindow)>("XMapWindow")(display_,xid_);
            }
            Sync();
            QueryExtensions();
            if (method_!=PresentedCaptureMethod::Window) InitializeComposite();
        } catch (...) { Close(); throw; }
    }
    ~PresentedWindow() { Close(); }
    PresentedWindow(const PresentedWindow&)=delete;
    PresentedWindow& operator=(const PresentedWindow&)=delete;
    std::uintptr_t NativeToken() const { return static_cast<std::uintptr_t>(xid_); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsAlive() const
    {
        XWindowAttributes attributes{};
        Errors errors(*this);
        const bool alive=Symbol<decltype(&XGetWindowAttributes)>("XGetWindowAttributes")(display_,xid_,&attributes)!=0;
        return errors.Finish() && alive;
    }
    void Fill(std::uint32_t rgb)
    {
        XWindowAttributes attributes{};
        Errors errors(*this);
        if (!Symbol<decltype(&XGetWindowAttributes)>("XGetWindowAttributes")(display_,xid_,&attributes) || !errors.Finish())
            throw std::runtime_error("capture Fill: window disappeared");
        if (!attributes.visual->red_mask || !attributes.visual->green_mask || !attributes.visual->blue_mask)
            throw std::runtime_error("capture requires TrueColor visual");
        const auto pack=[](unsigned v,unsigned long mask) {
            const auto shift=std::countr_zero(mask);
            return ((static_cast<unsigned long>(v)*(mask>>shift)/255ul)<<shift)&mask;
        };
        const auto pixel=pack((rgb>>16)&255,attributes.visual->red_mask)|
                         pack((rgb>>8)&255,attributes.visual->green_mask)|pack(rgb&255,attributes.visual->blue_mask);
        GC gc=Symbol<decltype(&XCreateGC)>("XCreateGC")(display_,xid_,0,nullptr);
        if (!gc) throw std::runtime_error("capture XCreateGC failed");
        Symbol<decltype(&XSetForeground)>("XSetForeground")(display_,gc,pixel);
        Symbol<decltype(&XFillRectangle)>("XFillRectangle")(display_,xid_,gc,0,0,width_,height_);
        Symbol<decltype(&XFreeGC)>("XFreeGC")(display_,gc);
        if (!errors.Finish()) throw std::runtime_error("capture Fill: "+errors.Describe());
    }
    bool ProbeCapture(std::string& diagnostic) const
    {
        try { const auto pixels=Capture(); diagnostic=lastDiagnostic_; return pixels.size()==static_cast<std::size_t>(width_)*height_; }
        catch (const std::exception& error) { diagnostic=error.what(); return false; }
    }
    const std::string& Diagnostics() const { return lastDiagnostic_; }
    std::vector<std::uint32_t> Capture() const
    {
        lastDiagnostic_="xid="+std::to_string(xid_)+" mode="+PresentedCaptureDetail::MethodName(method_)+" "+extensions_;
        XWindowAttributes attributes{};
        {
            Errors errors(*this);
            const bool read=Symbol<decltype(&XGetWindowAttributes)>("XGetWindowAttributes")(display_,xid_,&attributes)!=0;
            if (!errors.Finish() || !read) Fail("XGetWindowAttributes: "+errors.Describe());
        }
        std::ostringstream description;
        description<<"xid=0x"<<std::hex<<xid_<<std::dec<<" requested="<<width_<<'x'<<height_
                   <<" actual="<<attributes.width<<'x'<<attributes.height<<" depth="<<attributes.depth
                   <<" border="<<attributes.border_width<<" map_state="<<attributes.map_state
                   <<" mode="<<PresentedCaptureDetail::MethodName(method_)<<' '<<extensions_<<' '<<compositeStatus_;
        lastDiagnostic_=description.str();
        const auto problem=PresentedCaptureDetail::GeometryProblem(width_,height_,attributes.width,attributes.height,attributes.map_state);
        if (!problem.empty()) Fail(problem); // In particular reject a 16x16 read of an 8x8 window BEFORE GetImage.
        if (!attributes.visual || !attributes.visual->red_mask || !attributes.visual->green_mask || !attributes.visual->blue_mask)
            Fail("capture requires a TrueColor window visual");
        auto result=PresentedCaptureDetail::Read(method_,
            [&] { return CaptureComposite(attributes); },[&] { return CaptureWindow(attributes); });
        if (!result) Fail("no requested native capture transport succeeded");
        if (diagnostic_ && traceCount_++<12) {
            std::uint64_t hash=14695981039346656037ull;
            for (const auto rgb:*result) { hash^=rgb; hash*=1099511628211ull; }
            std::fprintf(stderr,"[present-capture] %s pixels=%zu first=%06x center=%06x hash=%016llx\n",
                lastDiagnostic_.c_str(),result->size(),result->front(),(*result)[result->size()/2],
                static_cast<unsigned long long>(hash));
        }
        return std::move(*result);
    }
    template<class Match> bool WaitFor(Platform::IPlatform& platform,Match match) const
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
    // XSetErrorHandler is process-global. Serialize these short observer-only traps, forward
    // other connections' errors to the previous handler, and restore it after XSync. No SDL
    // Display or event queue is used by our requests. Persistent state avoids callback pointers
    // into a destroyed stack guard when a renderer thread reports an unrelated X error.
    class Errors
    {
    public:
        explicit Errors(const PresentedWindow& window): window_(window),lock_(errorMutex_)
        {
            window_.Sync(); firstError_=XErrorEvent{};
            previous_=window_.Symbol<decltype(&XSetErrorHandler)>("XSetErrorHandler")(&HandleError);
            previousHandler_.store(previous_);
            observer_.store(window_.display_);
        }
        ~Errors()
        {
            window_.Sync();
            window_.Symbol<decltype(&XSetErrorHandler)>("XSetErrorHandler")(previous_);
            observer_.store(nullptr);
        }
        bool Finish() const { window_.Sync(); return firstError_.error_code==0; }
        std::string Describe() const
        {
            std::ostringstream text;
            text<<"X error="<<static_cast<unsigned>(firstError_.error_code)
                <<" request="<<static_cast<unsigned>(firstError_.request_code)<<'.'
                <<static_cast<unsigned>(firstError_.minor_code)<<" resource=0x"<<std::hex<<firstError_.resourceid;
            return text.str();
        }
    private:
        static int HandleError(Display* display,XErrorEvent* event)
        {
            if (display==observer_.load()) { if (!firstError_.error_code) firstError_=*event; return 0; }
            const auto previous=previousHandler_.load();
            return previous && previous!=&HandleError ? previous(display,event) : 0;
        }
        const PresentedWindow& window_;
        std::unique_lock<std::mutex> lock_;
        XErrorHandler previous_=nullptr;
        inline static std::mutex errorMutex_;
        inline static std::atomic<Display*> observer_{nullptr};
        inline static std::atomic<XErrorHandler> previousHandler_{nullptr};
        inline static XErrorEvent firstError_{};
    };
    template<class F> F Symbol(const char* name) const
    {
        const auto fn=reinterpret_cast<F>(dlsym(library_,name));
        if (!fn) throw std::runtime_error(std::string("missing X11 observation function ")+name);
        return fn;
    }
    template<class F> F CompositeSymbol(const char* name) const
    {
        const auto fn=reinterpret_cast<F>(dlsym(compositeLibrary_,name));
        if (!fn) throw std::runtime_error(std::string("missing Composite observation function ")+name);
        return fn;
    }
    void QueryExtensions()
    {
        std::ostringstream text;
        for (const char* name:{"Composite","DRI3","Present","MIT-SHM"}) {
            int opcode=0,event=0,error=0;
            const bool available=Symbol<decltype(&XQueryExtension)>("XQueryExtension")(display_,name,&opcode,&event,&error)!=0;
            text<<name<<'='<<(available?"yes":"no")<<' ';
        }
        extensions_=text.str();
    }
    void InitializeComposite()
    {
        compositeLibrary_=dlopen("libXcomposite.so.1",RTLD_LAZY|RTLD_LOCAL);
        if (!compositeLibrary_) { compositeStatus_="Composite library unavailable"; return; }
        using Query=Bool (*)(Display*,int*,int*);
        using Version=Status (*)(Display*,int*,int*);
        using Redirect=void (*)(Display*,Window,int);
        try {
            int event=0,error=0,major=0,minor=4;
            Errors errors(*this);
            if (!CompositeSymbol<Query>("XCompositeQueryExtension")(display_,&event,&error) ||
                !CompositeSymbol<Version>("XCompositeQueryVersion")(display_,&major,&minor) ||
                !errors.Finish() || (major==0 && minor<2)) {
                compositeStatus_="Composite>=0.2 unavailable"; return;
            }
            // Automatic redirection mirrors the output to its parent; manual redirection would
            // hide the window unless we also supplied a compositor. Negotiate version FIRST.
            (void)CompositeSymbol<Pixmap (*)(Display*,Window)>("XCompositeNameWindowPixmap");
            CompositeSymbol<Redirect>("XCompositeRedirectWindow")(display_,xid_,0 /*Automatic*/);
            redirected_=errors.Finish();
            compositeStatus_="Composite version="+std::to_string(major)+'.'+std::to_string(minor)+
                (redirected_?" redirected=automatic":" redirect failed: "+errors.Describe());
        } catch (const std::exception& error) { compositeStatus_=error.what(); }
    }
    std::optional<std::vector<std::uint32_t>> ReadDrawable(Drawable drawable,int x,int y,
                                                         const XWindowAttributes& attributes) const
    {
        XImage* image=Symbol<decltype(&XGetImage)>("XGetImage")(display_,drawable,x,y,width_,height_,AllPlanes,ZPixmap);
        if (!image) return std::nullopt;
        std::vector<std::uint32_t> rgb;
        try {
            rgb.reserve(static_cast<std::size_t>(width_)*height_);
            for (int row=0;row<height_;++row) for (int column=0;column<width_;++column) {
                const auto pixel=XGetPixel(image,column,row);
                // GetImage's pixmap reply has no Visual ID. The named pixmap shares its window's
                // depth/visual; use those masks instead of possibly zero XImage color masks.
                rgb.push_back(PresentedCaptureDetail::Rgb(pixel,attributes.visual->red_mask,
                    attributes.visual->green_mask,attributes.visual->blue_mask));
            }
        } catch (...) { XDestroyImage(image); throw; }
        XDestroyImage(image);
        return rgb;
    }
    std::optional<std::vector<std::uint32_t>> CaptureWindow(const XWindowAttributes& attributes) const
    {
        Errors errors(*this);
        auto result=ReadDrawable(xid_,0,0,attributes);
        if (!errors.Finish() || !result) { lastDiagnostic_+=" window failed: "+errors.Describe(); return std::nullopt; }
        lastDiagnostic_+=" selected=window";
        return result;
    }
    std::optional<std::vector<std::uint32_t>> CaptureComposite(const XWindowAttributes& attributes) const
    {
        if (!redirected_) { lastDiagnostic_+=" composite unavailable: "+compositeStatus_; return std::nullopt; }
        using Name=Pixmap (*)(Display*,Window);
        Errors errors(*this);
        // Name afresh after every swap/resize: an old name can keep obsolete backing storage alive.
        const Pixmap pixmap=CompositeSymbol<Name>("XCompositeNameWindowPixmap")(display_,xid_);
        if (!errors.Finish() || !pixmap) { lastDiagnostic_+=" NameWindowPixmap failed: "+errors.Describe(); return std::nullopt; }
        std::optional<std::vector<std::uint32_t>> result;
        try {
            Window root=0; int x=0,y=0; unsigned width=0,height=0,border=0,depth=0;
            const auto geometry=Symbol<decltype(&XGetGeometry)>("XGetGeometry")(display_,pixmap,&root,&x,&y,&width,&height,&border,&depth);
            const int offset=attributes.border_width; // Composite storage includes window borders.
            lastDiagnostic_+=" named="+std::to_string(width)+"x"+std::to_string(height)+" depth="+std::to_string(depth);
            if (errors.Finish() && geometry && width>=static_cast<unsigned>(width_+2*offset) &&
                height>=static_cast<unsigned>(height_+2*offset) && depth==static_cast<unsigned>(attributes.depth))
                result=ReadDrawable(pixmap,offset,offset,attributes);
            else lastDiagnostic_+=" named pixmap geometry/depth mismatch";
        } catch (...) { Symbol<decltype(&XFreePixmap)>("XFreePixmap")(display_,pixmap); throw; }
        Symbol<decltype(&XFreePixmap)>("XFreePixmap")(display_,pixmap);
        if (!errors.Finish() || !result) { lastDiagnostic_+=" composite read failed: "+errors.Describe(); return std::nullopt; }
        lastDiagnostic_+=" selected=composite pixmap="+std::to_string(pixmap);
        return result;
    }
    [[noreturn]] void Fail(const std::string& reason) const
    {
        lastDiagnostic_+=" failure: "+reason;
        if (diagnostic_) std::fprintf(stderr,"[present-capture] %s\n",lastDiagnostic_.c_str());
        throw std::runtime_error(lastDiagnostic_);
    }
    void Sync() const { Symbol<decltype(&XSync)>("XSync")(display_,False); }
    void Close() noexcept
    {
        // Closing our observer connection releases OUR automatic redirection/named resources and
        // any external test window we created. It never destroys a window owned by the SDL client.
        if (display_ && library_) {
            const auto close=reinterpret_cast<decltype(&XCloseDisplay)>(dlsym(library_,"XCloseDisplay"));
            if (close) close(display_);
        }
        display_=nullptr;
        if (compositeLibrary_) dlclose(compositeLibrary_);
        if (library_) dlclose(library_);
        compositeLibrary_=nullptr; library_=nullptr;
    }
    Display* display_=nullptr;
    Window xid_=0;
    void* library_=nullptr;
    void* compositeLibrary_=nullptr;
    PresentedCaptureMethod method_=PresentedCaptureMethod::Window;
    bool redirected_=false,diagnostic_=false;
    int width_=0,height_=0;
    std::string extensions_,compositeStatus_="Composite not requested";
    mutable std::string lastDiagnostic_;
    mutable int traceCount_=0;
};
#endif
} // namespace CNA::Testing
