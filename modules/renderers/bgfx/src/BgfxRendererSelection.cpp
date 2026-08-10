#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::Bgfx::Detail
{
    namespace
    {
        constexpr const char* kRendererOverrideEnvVar = "CNA_BGFX_RENDERER";

        std::string NormalizeRendererValue(std::string_view value)
        {
            std::string normalized;
            normalized.reserve(value.size());

            for (const char ch : value)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) != 0)
                {
                    normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                }
            }

            return normalized;
        }
    }

    bgfx::RendererType::Enum GetDefaultRendererType()
    {
#if defined(__linux__)
        return bgfx::RendererType::OpenGL;
#else
        return bgfx::RendererType::Count;
#endif
    }

    bgfx::RendererType::Enum ParseRendererTypeOverride(const char* value)
    {
        const char* safeValue = value ? value : "";
        const std::string normalized = NormalizeRendererValue(safeValue);

        if (normalized.empty() || normalized == "AUTO" || normalized == "COUNT")
        {
            return bgfx::RendererType::Count;
        }
        if (normalized == "NOOP")
        {
            return bgfx::RendererType::Noop;
        }
        if (normalized == "D3D11" || normalized == "DIRECT3D11")
        {
            return bgfx::RendererType::Direct3D11;
        }
        if (normalized == "D3D12" || normalized == "DIRECT3D12")
        {
            return bgfx::RendererType::Direct3D12;
        }
        if (normalized == "METAL")
        {
            return bgfx::RendererType::Metal;
        }
        if (normalized == "GL" || normalized == "OPENGL")
        {
            return bgfx::RendererType::OpenGL;
        }
        if (normalized == "GLES" || normalized == "OPENGLES")
        {
            return bgfx::RendererType::OpenGLES;
        }
        if (normalized == "VK" || normalized == "VULKAN")
        {
            return bgfx::RendererType::Vulkan;
        }

        throw std::runtime_error(
            std::string("Unsupported BGFX renderer override in ") + kRendererOverrideEnvVar + ": '" + safeValue
            + "'. Supported values: AUTO, OPENGL, OPENGLES, VULKAN, METAL, DIRECT3D11, DIRECT3D12, NOOP."
        );
    }

    bgfx::RendererType::Enum ResolveRendererType(const char* value)
    {
        if (!value || value[0] == '\0')
        {
            return GetDefaultRendererType();
        }

        return ParseRendererTypeOverride(value);
    }
}
