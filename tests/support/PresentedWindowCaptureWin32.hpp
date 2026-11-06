// SPDX-License-Identifier: MS-PL
#pragma once
// Win32 counterpart of G08a's PresentedWindowCapture.hpp. Observe the presented CLIENT,
// never GetBackBufferData/GetRenderTargetData. Ordinary-present control must pass first.
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <chrono>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CreateWindow
namespace CNA::Testing {
class PresentedWin32Window
{
public:
    PresentedWin32Window(const Platform::NativeWindowHandle& handle, int width, int height)
        : width_(width), height_(height)
    {
        Platform::Win32NativeWindow native;
        if (!Platform::TryGetWin32(handle,native))
            throw std::runtime_error("requires a Win32 native window");
        window_=static_cast<HWND>(native.hwnd);
        RECT client{};
        if (!GetClientRect(window_,&client) || client.right!=width || client.bottom!=height)
            throw std::runtime_error("native client size differs from requested capture");
    }
    std::uintptr_t NativeToken() const { return reinterpret_cast<std::uintptr_t>(window_); }
    bool IsAlive() const { return IsWindow(window_)!=FALSE; }
    void Fill(std::uint32_t rgb)
    {
        HDC dc=GetDC(window_);
        if (!dc) throw std::runtime_error("GetDC failed");
        HBRUSH brush=CreateSolidBrush(RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));
        RECT client{0,0,width_,height_};
        const bool filled=brush && FillRect(dc,&client,brush)!=0;
        if (brush) DeleteObject(brush);
        ReleaseDC(window_,dc);
        GdiFlush();
        if (!filled) throw std::runtime_error("native client fill failed");
    }
    std::vector<std::uint32_t> Capture() const
    {
        HDC source=GetDC(window_);
        if (!source) throw std::runtime_error("GetDC failed");
        HDC copy=CreateCompatibleDC(source);
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width_;
        info.bmiHeader.biHeight=-height_; // DIB rows are top-down.
        info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        void* storage=nullptr;
        HBITMAP bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&storage,nullptr,0);
        if (!copy || !bitmap || !storage)
        {
            if (bitmap) DeleteObject(bitmap);
            if (copy) DeleteDC(copy);
            ReleaseDC(window_,source);
            throw std::runtime_error("native capture allocation failed");
        }
        HGDIOBJ previous=SelectObject(copy,bitmap);
        const bool copied=BitBlt(copy,0,0,width_,height_,source,0,0,SRCCOPY)!=FALSE;
        GdiFlush();
        std::vector<std::uint32_t> rgb(static_cast<std::size_t>(width_)*height_);
        if (copied)
        {
            const auto* bgra=static_cast<const std::uint8_t*>(storage);
            for (std::size_t i=0;i<rgb.size();++i)
                rgb[i]=(std::uint32_t(bgra[4*i+2])<<16) |
                       (std::uint32_t(bgra[4*i+1])<<8) | bgra[4*i];
        }
        SelectObject(copy,previous);
        DeleteObject(bitmap); DeleteDC(copy); ReleaseDC(window_,source);
        if (!copied) throw std::runtime_error("BitBlt of presented client failed");
        return rgb;
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
    HWND window_=nullptr; // Borrowed; capture never destroys the host.
    int width_=0,height_=0;
};
} // namespace CNA::Testing
#endif
