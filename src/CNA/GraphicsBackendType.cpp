#include "CNA/GraphicsBackendType.hpp"

#include "CNA/CNAException.hpp"

namespace CNA
{
    GraphicsBackendType getCurrentGraphicsBackendType()
    {
#if defined(CNA_BACKEND_SDL_RENDERER)
        return GraphicsBackendType::SdlRenderer;
#elif defined(CNA_BACKEND_EASYGL)
        return GraphicsBackendType::EasyGL;
#elif defined(CNA_BACKEND_BGFX)
        return GraphicsBackendType::Bgfx;
#elif defined(CNA_BACKEND_VULKAN)
        return GraphicsBackendType::Vulkan;
#elif defined(CNA_BACKEND_WEBGPU)
        return GraphicsBackendType::WebGPU;
#elif defined(CNA_BACKEND_HEADLESS)
        return GraphicsBackendType::Headless;
#elif defined(CNA_BACKEND_SOFTWARE)
        return GraphicsBackendType::Software;
#elif defined(CNA_BACKEND_D3D11)
        return GraphicsBackendType::D3D11;
#elif defined(CNA_BACKEND_D3D12)
        return GraphicsBackendType::D3D12;
#elif defined(CNA_BACKEND_CANVAS)
        return GraphicsBackendType::Canvas;
#elif defined(CNA_BACKEND_ASCII)
        return GraphicsBackendType::Ascii;
#elif defined(CNA_BACKEND_DX3)
        return GraphicsBackendType::Dx3;
#elif defined(CNA_BACKEND_D3D9)
        return GraphicsBackendType::D3D9;
#elif defined(CNA_BACKEND_SDL_GPU)
        return GraphicsBackendType::SdlGpu;
#else
#error "CNA: no CNA_BACKEND_* compile definition set -- graphics backend selection (cmake/BackendSelection.cmake) is broken"
#endif
    }

    const std::string& getCurrentGraphicsBackendName()
    {
        switch (getCurrentGraphicsBackendType())
        {
            case GraphicsBackendType::SdlRenderer: { static const std::string s = "SDL_RENDERER"; return s; }
            case GraphicsBackendType::EasyGL:      { static const std::string s = "EASYGL";        return s; }
            case GraphicsBackendType::Bgfx:         { static const std::string s = "BGFX";          return s; }
            case GraphicsBackendType::Vulkan:       { static const std::string s = "VULKAN";        return s; }
            case GraphicsBackendType::WebGPU:       { static const std::string s = "WEBGPU";        return s; }
            case GraphicsBackendType::Headless:     { static const std::string s = "HEADLESS";      return s; }
            case GraphicsBackendType::Software:     { static const std::string s = "SOFTWARE";      return s; }
            case GraphicsBackendType::D3D11:        { static const std::string s = "D3D11";         return s; }
            case GraphicsBackendType::D3D12:        { static const std::string s = "D3D12";         return s; }
            case GraphicsBackendType::Canvas:       { static const std::string s = "CANVAS";        return s; }
            case GraphicsBackendType::Ascii:        { static const std::string s = "ASCII";         return s; }
            case GraphicsBackendType::Dx3:          { static const std::string s = "DX3";           return s; }
            case GraphicsBackendType::D3D9:         { static const std::string s = "D3D9";          return s; }
            case GraphicsBackendType::SdlGpu:       { static const std::string s = "SDL_GPU";       return s; }
        }
        throw CNAException("Unknown GraphicsBackendType.");
    }
} // CNA
